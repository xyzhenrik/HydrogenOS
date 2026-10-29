#include "mini_libc.h"
#include "perf.h"
#include "syscall.h"

/*
 * Boot test for the kernel performance counters. It takes a snapshot, runs a
 * workload with a known shape (syscalls, a pipe ping-pong with a helper
 * process, a CPU burn, a sleep and a crashing child) and checks that every
 * counter the workload touches grew by at least what the workload caused.
 * Other processes run concurrently, so the checks are lower bounds only.
 */

enum {
    GETPID_CALLS = 100,
    PINGPONG_ROUNDS = 50,
    BURN_ITERATIONS = 3000000,
    SLEEP_MS = 60,
    IO_TIMEOUT_MS = 3000,
    TEXT_CAP = 2048,
    FIELD_CAP = 64,
    PATH_CAP = 32,
    COUNTER_WORDS = sizeof(hydrogen_perf_counters_t) / sizeof(uint64_t),
    TIMER_VECTOR = 32,
    SIGSEGV_STATUS = 0x40000000 | 11,
};

static int fail(const char *what) {
    dprintf(1, "PERF FAIL: %s\n", what);
    return 1;
}

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static long read_whole(const char *path, char *buf, unsigned long cap) {
    long fd = open(path, 0);
    if (fd < 0)
        return -1;
    unsigned long total = 0;
    while (total < cap - 1) {
        long n = read((int)fd, buf + total, cap - 1 - total);
        if (n < 0) {
            close((int)fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (unsigned long)n;
    }
    close((int)fd);
    buf[total] = '\0';
    return (long)total;
}

/* Copies the value of a "Label:   value" line into out; 0 or -1. */
static int field(const char *text, const char *label, char *out,
                 unsigned long cap) {
    unsigned long label_len = strlen(label);
    const char *line = text;
    while (*line) {
        unsigned long i = 0;
        while (i < label_len && line[i] == label[i])
            ++i;
        if (i == label_len && line[i] == ':') {
            const char *v = line + i + 1;
            while (*v == ' ')
                ++v;
            unsigned long n = 0;
            while (v[n] && v[n] != '\n') {
                if (n >= cap - 1)
                    return -1;
                out[n] = v[n];
                ++n;
            }
            out[n] = '\0';
            return 0;
        }
        while (*line && *line != '\n')
            ++line;
        if (*line == '\n')
            ++line;
    }
    return -1;
}

/* Parses the leading decimal number of a field; trailing units are allowed. */
static int field_u64(const char *text, const char *label, uint64_t *out) {
    char value[FIELD_CAP];
    if (field(text, label, value, sizeof(value)) != 0 || value[0] < '0' ||
        value[0] > '9')
        return -1;
    uint64_t v = 0;
    for (const char *c = value; *c >= '0' && *c <= '9'; ++c)
        v = v * 10u + (uint64_t)(*c - '0');
    *out = v;
    return 0;
}

/* Replies to every stdin byte with the same byte. */
static int role_echo(void) {
    unsigned char byte;
    for (;;) {
        long n = read(0, &byte, 1);
        if (n < 0)
            return 20;
        if (n == 0)
            return 0;
        if (write(1, &byte, 1) != 1)
            return 21;
    }
}

/* Dies of SIGSEGV, which costs exactly one page fault. */
static int role_crash(void) {
    *(volatile int *)0 = 1;
    return 30;
}

static long spawn_role(const char *role, int in_fd, int out_fd) {
    const char *argv[] = {"perftest", role, 0};
    return spawn_args("/perftest", argv, 0, in_fd, out_fd, SPAWN_STDIO_INHERIT);
}

static int check_interface(void) {
    hydrogen_perf_snapshot_t snapshot;
    char scratch[16];

    if (perf_snapshot_read(&snapshot) != 0)
        return fail("snapshot version or size");
    if (snapshot.syscall_slots != HYDROGEN_PERF_SYSCALL_SLOTS ||
        snapshot.irq_vectors != HYDROGEN_PERF_IRQ_VECTORS)
        return fail("snapshot table sizes");
    if (snapshot.tsc == 0 || snapshot.uptime_ms == 0)
        return fail("snapshot time base");
    if (snapshot.tsc_khz == 0)
        return fail("tsc frequency estimate");
    if (sys_perf_read(scratch, 8) != 8 || sys_perf_read(scratch, 7) >= 0)
        return fail("short snapshot read");
    if (sys_perf_read(0, sizeof(snapshot)) >= 0)
        return fail("null buffer accepted");
    return 0;
}

static int run_pingpong(void) {
    int request[2];
    int reply[2];

    if (pipe(request) != 0 || pipe(reply) != 0)
        return fail("pipe");
    long pid = spawn_role("echo", request[0], reply[1]);
    if (pid <= 0)
        return fail("spawn echo");
    close(request[0]);
    close(reply[1]);

    for (unsigned round = 0; round < PINGPONG_ROUNDS; ++round) {
        unsigned char sent = (unsigned char)(round * 3 + 1);
        unsigned char got = 0;
        if (write(request[1], &sent, 1) != 1)
            return fail("pingpong write");
        if (poll_wait_fd(reply[0], POLL_READ, IO_TIMEOUT_MS) <= 0 ||
            read(reply[0], &got, 1) != 1 || got != sent)
            return fail("pingpong reply");
    }
    close(request[1]);
    int status = -1;
    if (wait_pid((int)pid, &status) != pid || status != 0)
        return fail("echo status");
    close(reply[0]);
    return 0;
}

static int run_crash(void) {
    long pid = spawn_role("crash", SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    int status = -1;
    if (pid <= 0)
        return fail("spawn crash");
    if (wait_pid((int)pid, &status) != pid || status != SIGSEGV_STATUS)
        return fail("crash status");
    return 0;
}

static volatile unsigned burn_sink;

static void burn_cpu(void) {
    for (unsigned i = 0; i < BURN_ITERATIONS; ++i)
        burn_sink += i;
}

static int check_growth(const hydrogen_perf_snapshot_t *a,
                        const hydrogen_perf_snapshot_t *b) {
    const uint64_t *before = (const uint64_t *)&a->counters;
    const uint64_t *after = (const uint64_t *)&b->counters;
    const hydrogen_perf_counters_t *ca = &a->counters;
    const hydrogen_perf_counters_t *cb = &b->counters;

    for (unsigned i = 0; i < COUNTER_WORDS; ++i) {
        if (after[i] < before[i])
            return fail("counter went backwards");
    }
    if (b->tsc <= a->tsc || b->uptime_ms < a->uptime_ms + SLEEP_MS)
        return fail("time base did not advance");

    uint64_t getpids = cb->syscall_calls[SYS_GETPID] - ca->syscall_calls[SYS_GETPID];
    if (getpids < GETPID_CALLS)
        return fail("getpid call count");
    if (cb->syscall_cycles[SYS_GETPID] <= ca->syscall_cycles[SYS_GETPID])
        return fail("getpid cycles");
    if (cb->syscall_calls[SYS_WRITE] - ca->syscall_calls[SYS_WRITE] < PINGPONG_ROUNDS ||
        cb->syscall_calls[SYS_READ] - ca->syscall_calls[SYS_READ] < PINGPONG_ROUNDS)
        return fail("read/write call count");
    if (cb->syscall_calls[SYS_SLEEP] - ca->syscall_calls[SYS_SLEEP] < 1)
        return fail("sleep call count");

    if (cb->pipe_bytes_written - ca->pipe_bytes_written < 2 * PINGPONG_ROUNDS ||
        cb->pipe_bytes_read - ca->pipe_bytes_read < 2 * PINGPONG_ROUNDS)
        return fail("pipe bytes");
    if (cb->context_switches - ca->context_switches < PINGPONG_ROUNDS / 2)
        return fail("context switches");
    if (cb->wakeups - ca->wakeups < PINGPONG_ROUNDS / 2)
        return fail("wakeups");
    if (cb->irq_counts[TIMER_VECTOR] - ca->irq_counts[TIMER_VECTOR] < 2)
        return fail("timer interrupts");
    if (cb->irq_counts[HYDROGEN_PERF_VECTOR_PAGE_FAULT] -
            ca->irq_counts[HYDROGEN_PERF_VECTOR_PAGE_FAULT] < 1)
        return fail("page faults");
    if (cb->irq_counts[128] - ca->irq_counts[128] < getpids)
        return fail("syscall vector count");

    uint64_t user = cb->user_cycles - ca->user_cycles;
    uint64_t kernel = cb->kernel_cycles - ca->kernel_cycles;
    uint64_t idle = cb->idle_cycles - ca->idle_cycles;
    uint64_t elapsed = b->tsc - a->tsc;
    if (user == 0 || kernel == 0)
        return fail("cpu time split");
    /* Everything between the snapshots is attributed to one of the three
       buckets; the bounds only absorb the two partly counted syscalls at the
       ends. */
    if (user + kernel + idle < elapsed / 2 ||
        user + kernel + idle > elapsed + elapsed / 20)
        return fail("cpu time does not add up");
    return 0;
}

static int check_procfs(const hydrogen_perf_snapshot_t *last) {
    char text[TEXT_CAP];
    char path[PATH_CAP];
    char value[FIELD_CAP];
    uint64_t number = 0;

    if (read_whole("/proc/stat", text, sizeof(text)) <= 0)
        return fail("read /proc/stat");
    if (field_u64(text, "Tsc", &number) != 0 || number < last->tsc)
        return fail("stat tsc");
    if (field_u64(text, "CtxSwitches", &number) != 0 ||
        number < last->counters.context_switches)
        return fail("stat ctx switches");
    if (field_u64(text, "Syscalls", &number) != 0 || number == 0)
        return fail("stat syscalls");
    if (field_u64(text, "PageFaults", &number) != 0 || number == 0)
        return fail("stat page faults");
    if (field_u64(text, "PipeWriteBytes", &number) != 0 ||
        number < last->counters.pipe_bytes_written)
        return fail("stat pipe bytes");
    if (field_u64(text, "CpuIdle", &number) != 0 ||
        field_u64(text, "NetRxPackets", &number) != 0)
        return fail("stat fields");

    if (read_whole("/proc/interrupts", text, sizeof(text)) <= 0)
        return fail("read /proc/interrupts");
    if (!strstr(text, " timer\n") || !strstr(text, " syscall\n") ||
        !strstr(text, " page-fault\n"))
        return fail("interrupts names");

    snprintf(path, sizeof(path), "/proc/%d/stat", (int)sys_getpid());
    if (read_whole(path, text, sizeof(text)) <= 0)
        return fail("read pid stat");
    if (field(text, "Name", value, sizeof(value)) != 0 ||
        !str_equal(value, "perftest"))
        return fail("pid stat name");
    if (field(text, "State", value, sizeof(value)) != 0 ||
        !str_equal(value, "running"))
        return fail("pid stat state");
    if (field_u64(text, "Pid", &number) != 0 || number != (uint64_t)sys_getpid())
        return fail("pid stat pid");
    uint64_t user = 0;
    uint64_t kernel = 0;
    uint64_t tsc = 0;
    if (field_u64(text, "UserCycles", &user) != 0 || user == 0 ||
        field_u64(text, "KernelCycles", &kernel) != 0 || kernel == 0 ||
        field_u64(text, "Tsc", &tsc) != 0 || tsc < last->tsc)
        return fail("pid stat cycles");
    /* No process can have run longer than the machine has been up. */
    if (user + kernel > last->tsc_khz * last->uptime_ms * 12 / 10)
        return fail("process ran longer than the machine");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && str_equal(argv[1], "echo"))
        return role_echo();
    if (argc > 1 && str_equal(argv[1], "crash"))
        return role_crash();

    if (check_interface() != 0)
        return 1;

    hydrogen_perf_snapshot_t before;
    hydrogen_perf_snapshot_t after;
    if (perf_snapshot_read(&before) != 0)
        return fail("first snapshot");

    for (unsigned i = 0; i < GETPID_CALLS; ++i)
        (void)sys_getpid();
    burn_cpu();
    if (run_pingpong() != 0 || run_crash() != 0)
        return 1;
    sleep_ms(SLEEP_MS);

    if (perf_snapshot_read(&after) != 0)
        return fail("second snapshot");
    if (check_growth(&before, &after) != 0 || check_procfs(&after) != 0)
        return 1;
    write(1, "PERF PASS\n", 10);
    return 0;
}
