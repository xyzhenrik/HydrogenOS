#include "crash.h"

#include "core/kmsg.h"
#include "core/process.h"
#include "core/time.h"
#include "core/usercopy.h"
#include "crash_abi.h"
#include "drivers/block.h"
#include "errno.h"
#include "fs/ext2.h"
#include "io.h"
#include "paging.h"
#include "version.h"

/*
 * Disk layout of the reserved region, which holds the last REGION_SECTORS
 * sectors of the boot disk: one header sector, then the dump. The image build
 * appends that space behind the filesystem (see the Makefile); crash_init()
 * refuses to use it unless it provably lies beyond everything the filesystem
 * owns. Data is written and flushed before the header, so a header that
 * passes its checks always describes a complete dump.
 */
enum {
    SECTOR_SIZE = 512,
    REGION_SECTORS = 64,
    HEADER_VERSION = 1,
    BACKTRACE_DEPTH = 16u,
    PROCESS_ROWS = 64u,
    LOG_LINES = 24u,
    LOG_MAX_BYTES = 4096,
    FNV_OFFSET = 0x811C9DC5u,
    FNV_PRIME = 0x01000193u,
};

typedef struct crash_header {
    char magic[8];
    uint32_t version;
    uint32_t length;
    uint32_t checksum; /* FNV-1a over the dump */
} crash_header_t;

_Static_assert(HYDROGEN_CRASH_DUMP_MAX / SECTOR_SIZE + 1 <= REGION_SECTORS,
               "dump and header must fit the reserved region");

static const char header_magic[8] = {'H', 'Y', 'D', 'C', 'R', 'S', 'H', '1'};

static char dump_buffer[HYDROGEN_CRASH_DUMP_MAX] __attribute__((aligned(16)));
static uint8_t header_sector[SECTOR_SIZE] __attribute__((aligned(16)));
static size_t dump_length;

static uint32_t region_lba;   /* 0: no usable reserved region */
static int pending;           /* a dump waits to be collected */
static int pending_on_disk;   /* ... and the disk, not dump_buffer, holds it */
static int recording;
static const regs_t *trap_frame;

/* ---------------------------------------------------------------- text */

static void put_char(char c) {
    if (dump_length < sizeof(dump_buffer))
        dump_buffer[dump_length++] = c;
}

static void put_text(const char *s) {
    for (; *s; ++s)
        put_char(*s);
}

static void put_decimal(uint64_t value) {
    char digits[20];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0)
        put_char(digits[--count]);
}

static void put_decimal_padded(uint64_t value, int width) {
    int count = 1;
    for (uint64_t rest = value; rest >= 10; rest /= 10)
        count++;
    for (; count < width; ++count)
        put_char(' ');
    put_decimal(value);
}

static void put_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    put_text("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        put_char(digits[(value >> shift) & 0xF]);
}

static void put_register(const char *name, uint64_t value) {
    put_text(name);
    put_char(' ');
    put_hex(value);
}

static void put_text_padded(const char *s, size_t width) {
    size_t length = 0;
    for (; s[length]; ++length)
        put_char(s[length]);
    for (; length < width; ++length)
        put_char(' ');
}

/* ------------------------------------------------------------ sections */

static const char *kind_name(crash_kind_t kind) {
    switch (kind) {
    case CRASH_KIND_PANIC: return "panic";
    case CRASH_KIND_OOPS: return "oops";
    case CRASH_KIND_USER_FAULT: return "user fault";
    }
    return "unknown";
}

static const char *state_name(uint8_t state) {
    switch (state) {
    case PROCESS_RUNNABLE: return "runnable";
    case PROCESS_BLOCKED_WAIT: return "wait";
    case PROCESS_BLOCKED_PIPE_READ: return "pipe-read";
    case PROCESS_BLOCKED_PIPE_WRITE: return "pipe-write";
    case PROCESS_BLOCKED_POLL: return "poll";
    case PROCESS_SLEEPING: return "sleeping";
    case PROCESS_BLOCKED_KERNEL: return "kernel-wait";
    case PROCESS_BLOCKED_SOCKET: return "socket";
    }
    return "?";
}

static const char *priority_name(uint8_t priority) {
    switch (priority) {
    case PROCESS_PRIO_IDLE: return "idle";
    case PROCESS_PRIO_NORMAL: return "normal";
    case PROCESS_PRIO_HIGH: return "high";
    case PROCESS_PRIO_RT: return "rt";
    }
    return "?";
}

static int canonical(uint64_t address) {
    uint64_t top = address >> 47;
    return top == 0 || top == 0x1FFFF;
}

/* Reads the saved frame pointer and return address stored at frame_address. */
static int read_frame_pair(uint64_t frame_address, int user, uint64_t pair[2]) {
    if ((frame_address & 7) != 0 || !canonical(frame_address))
        return -1;
    if (user)
        return copy_from_user(pair, (const void *)(uintptr_t)frame_address,
                              2 * sizeof(uint64_t)) == 0 ? 0 : -1;
    if (!paging_is_mapped(frame_address) ||
        !paging_is_mapped(frame_address + sizeof(uint64_t)))
        return -1;
    pair[0] = ((const volatile uint64_t *)(uintptr_t)frame_address)[0];
    pair[1] = ((const volatile uint64_t *)(uintptr_t)frame_address)[1];
    return 0;
}

/* Follows the frame-pointer chain starting at rbp. Every address is checked
 * before it is read, and the chain must climb, so a damaged stack ends the
 * trace instead of the report. */
static void put_backtrace(int have_rip, uint64_t rip, uint64_t rbp, int user) {
    unsigned index = 0;
    put_text("Backtrace\n");
    if (have_rip) {
        put_text("  #0 ");
        put_hex(rip);
        put_char('\n');
        index = 1;
    }
    for (; index < BACKTRACE_DEPTH; ++index) {
        uint64_t pair[2];
        if (read_frame_pair(rbp, user, pair) != 0 || pair[1] == 0)
            break;
        put_text("  #");
        put_decimal(index);
        put_char(' ');
        put_hex(pair[1]);
        put_char('\n');
        if (pair[0] <= rbp)
            break;
        rbp = pair[0];
    }
}

static void put_registers(const regs_t *f) {
    put_text("Registers\n");
    put_text("  "); put_register("RAX", f->rax); put_text("  "); put_register("RBX", f->rbx); put_char('\n');
    put_text("  "); put_register("RCX", f->rcx); put_text("  "); put_register("RDX", f->rdx); put_char('\n');
    put_text("  "); put_register("RSI", f->rsi); put_text("  "); put_register("RDI", f->rdi); put_char('\n');
    put_text("  "); put_register("RBP", f->rbp); put_text("  "); put_register("RSP", f->rsp); put_char('\n');
    put_text("  "); put_register("R8 ", f->r8);  put_text("  "); put_register("R9 ", f->r9);  put_char('\n');
    put_text("  "); put_register("R10", f->r10); put_text("  "); put_register("R11", f->r11); put_char('\n');
    put_text("  "); put_register("R12", f->r12); put_text("  "); put_register("R13", f->r13); put_char('\n');
    put_text("  "); put_register("R14", f->r14); put_text("  "); put_register("R15", f->r15); put_char('\n');
    put_text("  "); put_register("RIP", f->rip); put_text("  "); put_register("RFL", f->rflags); put_char('\n');
    put_text("  "); put_register("CS ", f->cs);  put_text("  "); put_register("SS ", f->ss);  put_char('\n');
    put_text("  vector ");
    put_decimal(f->int_no);
    put_text("  error ");
    put_hex(f->err_code);
    if (f->int_no == 14) {
        put_text("  cr2 ");
        put_hex(read_cr2());
    }
    put_char('\n');
}

static void put_processes(const process_t *current) {
    put_text("Processes\n  PID  PPID  STATE        PRIO    PAGES  NAME\n");
    uint32_t after = 0;
    for (unsigned row = 0; row < PROCESS_ROWS; ++row) {
        process_info_t info;
        if (process_next_info(after, &info) != 0)
            break;
        after = info.pid;
        put_text(current && current->pid == info.pid ? "* " : "  ");
        put_decimal_padded(info.pid, 3);
        put_text("  ");
        put_decimal_padded(info.parent_pid, 4);
        put_text("  ");
        put_text_padded(state_name(info.state), 11);
        put_text("  ");
        put_text_padded(priority_name(info.priority), 6);
        put_text("  ");
        put_decimal_padded(info.user_pages, 6);
        put_text("  ");
        put_text(info.name);
        put_char('\n');
    }
}

static void put_log(void) {
    put_text("Log\n");
    size_t room = sizeof(dump_buffer) - dump_length;
    if (room > LOG_MAX_BYTES)
        room = LOG_MAX_BYTES;
    if (room < 2)
        return;
    dump_length += kmsg_copy_tail(dump_buffer + dump_length, room, LOG_LINES);
}

static void build_header(crash_kind_t kind, const char *reason) {
    process_t *current = process_current();

    dump_length = 0;
    put_text(HYDROGEN_OS_NAME " crash dump\n");
    put_text("Kind:     ");
    put_text(kind_name(kind));
    put_text("\nReason:   ");
    put_text(reason ? reason : "unspecified");
    put_text("\nVersion:  " HYDROGEN_VERSION " " HYDROGEN_ARCH "\n");
    put_text("Uptime:   ");
    put_decimal(time_ms());
    put_text(" ms\nProcess:  ");
    if (current && current != process_kernel()) {
        put_text("pid ");
        put_decimal(current->pid);
        put_text(" (");
        put_text(current->name);
        put_text(")\n");
    } else {
        put_text("kernel\n");
    }
}

static uint32_t checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < length; ++i)
        hash = (hash ^ bytes[i]) * FNV_PRIME;
    return hash;
}

/* -------------------------------------------------------------- storage */

static int store_header(uint32_t length, uint32_t sum) {
    crash_header_t *header = (crash_header_t *)header_sector;
    for (size_t i = 0; i < sizeof(header_sector); ++i)
        header_sector[i] = 0;
    if (length != 0) {
        for (size_t i = 0; i < sizeof(header->magic); ++i)
            header->magic[i] = header_magic[i];
        header->version = HEADER_VERSION;
        header->length = length;
        header->checksum = sum;
    }
    if (block_write(region_lba, 1, header_sector) != 0 || block_flush() != 0)
        return -EIO;
    return 0;
}

static int persist(void) {
    size_t sectors = (dump_length + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (size_t i = dump_length; i < sectors * SECTOR_SIZE; ++i)
        dump_buffer[i] = 0;
    if (block_write(region_lba + 1, (uint8_t)sectors, dump_buffer) != 0 ||
        block_flush() != 0)
        return -EIO;
    return store_header((uint32_t)dump_length,
                        checksum(dump_buffer, dump_length));
}

/* Loads the dump the header describes into dump_buffer. Returns 0 or
 * -ENOENT if the region holds no complete dump. */
static int load(void) {
    const crash_header_t *header = (const crash_header_t *)header_sector;
    if (block_read(region_lba, 1, header_sector) != 0)
        return -ENOENT;
    for (size_t i = 0; i < sizeof(header->magic); ++i) {
        if (header->magic[i] != header_magic[i])
            return -ENOENT;
    }
    if (header->version != HEADER_VERSION || header->length == 0 ||
        header->length > sizeof(dump_buffer))
        return -ENOENT;
    size_t length = header->length;
    uint32_t expected = header->checksum;
    size_t sectors = (length + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (block_read(region_lba + 1, (uint8_t)sectors, dump_buffer) != 0)
        return -ENOENT;
    if (checksum(dump_buffer, length) != expected)
        return -ENOENT;
    dump_length = length;
    return 0;
}

void crash_init(void) {
    const block_dev_t *device = block_primary();
    ext2_info_t fs;
    if (!device || !device->write || device->sector_size != SECTOR_SIZE ||
        !ext2_is_mounted() || ext2_info(&fs) != 0)
        return;

    uint64_t fs_end = (uint64_t)fs.base_lba +
                      (uint64_t)fs.blocks * (fs.block_size / SECTOR_SIZE);
    if (device->sector_count < REGION_SECTORS)
        return;
    uint32_t first = device->sector_count - REGION_SECTORS;
    if (fs_end > first)
        return;

    region_lba = first;
    if (load() == 0) {
        pending = 1;
        pending_on_disk = 1;
    }
}

/* -------------------------------------------------------------- public */

void crash_note_trap_frame(const regs_t *frame) {
    trap_frame = frame;
}

int crash_record(crash_kind_t kind, const char *reason, const regs_t *frame) {
    if (recording)
        return -EBUSY;
    recording = 1;

    if (!frame && kind == CRASH_KIND_PANIC)
        frame = trap_frame;

    build_header(kind, reason);
    process_t *current = process_current();
    if (frame) {
        put_registers(frame);
        put_backtrace(1, frame->rip, frame->rbp, (frame->cs & 3) == 3);
    } else {
        put_backtrace(0, 0, (uint64_t)(uintptr_t)__builtin_frame_address(0), 0);
    }
    put_processes(current);
    put_log();

    int rc = 0;
    if (region_lba) {
        rc = persist();
        pending_on_disk = rc == 0;
    } else {
        pending_on_disk = 0;
    }
    pending = 1;
    recording = 0;
    return rc;
}

void crash_record_user_fault(const process_t *p, const char *signal_name) {
    if (pending || recording)
        return;
    recording = 1;

    build_header(CRASH_KIND_USER_FAULT, signal_name);
    if (p->signals.fault_signal != 0) {
        put_text("Fault:    address ");
        put_hex(p->signals.fault_addr);
        put_text(", vector ");
        put_decimal(p->signals.fault_trap);
        put_text(", error ");
        put_hex(p->signals.fault_error);
        put_char('\n');
    }
    put_registers(&p->frame);
    put_backtrace(1, p->frame.rip, p->frame.rbp, 1);
    put_processes(p);
    put_log();

    pending_on_disk = region_lba != 0 && persist() == 0;
    pending = 1;
    recording = 0;
}

long crash_syscall(uint64_t op, void *buffer, size_t size) {
    if (!credentials_is_root(&process_current()->cred))
        return -EPERM;

    switch (op) {
    case HYDROGEN_CRASH_OP_TEST:
        return crash_record(CRASH_KIND_OOPS, "crashtest: simulated oops", 0);

    case HYDROGEN_CRASH_OP_READ: {
        if (!pending)
            return -ENOENT;
        if (pending_on_disk && load() != 0) {
            pending = 0;
            pending_on_disk = 0;
            return -ENOENT;
        }
        size_t copied = dump_length < size ? dump_length : size;
        int rc = copy_to_user(buffer, dump_buffer, copied);
        return rc != 0 ? rc : (long)copied;
    }

    case HYDROGEN_CRASH_OP_CLEAR: {
        int rc = 0;
        if (region_lba && pending)
            rc = store_header(0, 0);
        pending = 0;
        pending_on_disk = 0;
        return rc;
    }

    default:
        return -EINVAL;
    }
}
