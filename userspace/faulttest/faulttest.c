#include "mini_libc.h"
#include "syscall.h"

#include <stdint.h>

#define PAGE_SIZE 4096ul
#define RC_EFAULT (-14l)
#define PROT_RW (MMAP_PROT_READ | MMAP_PROT_WRITE)
#define SCRATCH_BYTES 256

/* Length passed with every bad buffer: big enough that the kernel has to
   touch the target, small enough that the range cannot wrap by itself. */
#define BAD_LEN 16

typedef struct BadPointer {
    const char *name;
    uintptr_t value;
} BadPointer;

static const BadPointer bad_pointers[] = {
    {"low unmapped", 0x1000},
    {"kernel identity map", 0x100000},
    {"kernel half", 0xFFFF800000001000ul},
    {"top page", 0xFFFFFFFFFFFFF000ul},
    {"unmapped user", 0x0000100000000000ul},
    {"user limit straddle", 0x00007FFFFFFFFFF8ul},
    {"first non-canonical", 0x0000800000000000ul},
    {"non-canonical", 0x1234567800000000ul},
    {"sign bit only", 0x8000000000000000ul},
    {"wraps at 2^64", 0xFFFFFFFFFFFFFFF8ul},
};

static char scratch[SCRATCH_BYTES];
static int open_fd = -1;
static int failures;
static const char *argv_bad_entry[2];

static const char *const *argv_with(uintptr_t entry) {
    argv_bad_entry[0] = (const char *)entry;
    argv_bad_entry[1] = 0;
    return argv_bad_entry;
}

static void fail(const char *what) {
    printf("FAULT FAIL %s\n", what);
    ++failures;
}

static void expect_fault(const char *name, const char *pointer, long result) {
    if (result == RC_EFAULT)
        return;
    printf("FAULT FAIL %s with %s pointer returned %ld\n", name, pointer, result);
    ++failures;
}

/* Every case passes `bad` as the pointer under test and valid values for all
   other arguments, so only that pointer can explain an -EFAULT. NULL is not in
   the table because it is a legal value for optional pointers (argv, envp,
   wait status). */
#define FAULT_CASES(X) \
    X("open path", sys_open((const char *)bad, OPEN_READ)) \
    X("stat path", sys_stat((const char *)bad, scratch)) \
    X("readdir path", sys_readdir((const char *)bad, 0, scratch)) \
    X("chdir path", syscall1(SYS_CHDIR, (long)bad)) \
    X("mkdir path", sys_mkdir((const char *)bad)) \
    X("rmdir path", sys_rmdir((const char *)bad)) \
    X("unlink path", sys_unlink((const char *)bad)) \
    X("rename old path", sys_rename((const char *)bad, "/faulttest.new")) \
    X("rename new path", sys_rename("/faulttest.old", (const char *)bad)) \
    X("writef path", sys_writef((const char *)bad, "x", 1)) \
    X("spawn path", sys_spawn((const char *)bad, 0)) \
    X("spawn_with_stdio path", \
      sys_spawn_with_stdio((const char *)bad, SPAWN_STDIO_INHERIT, \
                           SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT)) \
    X("spawn_args path", \
      sys_spawn_args((const char *)bad, 0, 0, SPAWN_STDIO_INHERIT, \
                     SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT)) \
    X("spawn_args argv", \
      sys_spawn_args("/echo", (const char *const *)bad, 0, \
                     SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT, \
                     SPAWN_STDIO_INHERIT)) \
    X("spawn_args envp", \
      sys_spawn_args("/echo", 0, (const char *const *)bad, \
                     SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT, \
                     SPAWN_STDIO_INHERIT)) \
    X("spawn_args argv entry", \
      sys_spawn_args("/echo", argv_with(bad), 0, SPAWN_STDIO_INHERIT, \
                     SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT)) \
    X("write buffer", sys_write(1, (const void *)bad, BAD_LEN)) \
    X("read buffer", sys_read(0, (void *)bad, BAD_LEN)) \
    X("writef data", sys_writef("/faulttest.tmp", (const void *)bad, BAD_LEN)) \
    X("fb_blit source", sys_fb_blit(0, 0, 2, 2, (const void *)bad)) \
    X("fb_blit_strided source", \
      sys_fb_blit_strided(0, 0, 2, 2, (const void *)bad, 8)) \
    X("stat buffer", sys_stat("/", (void *)bad)) \
    X("fstat buffer", sys_fstat(open_fd, (void *)bad)) \
    X("readdir buffer", sys_readdir("/", 0, (void *)bad)) \
    X("getcwd buffer", syscall2(SYS_GETCWD, (long)bad, 64)) \
    X("pipe fds", sys_pipe((int *)bad)) \
    X("wait status", sys_wait(0x7FFFFFFF, (int *)bad)) \
    X("try_wait status", sys_try_wait(0x7FFFFFFF, (int *)bad)) \
    X("poll_many list", sys_poll_many((void *)bad, 4, 0)) \
    X("sysinfo", sys_sysinfo((sysinfo_t *)bad)) \
    X("mouse_state", sys_mouse_state((int *)bad)) \
    X("fb_info", sys_fb_info((unsigned int *)bad)) \
    X("rtc_time", sys_rtc_time((struct hydrogen_rtc_time *)bad)) \
    X("net_info", sys_net_info((net_info_t *)bad))

static void check_bad_pointers(void) {
    for (size_t i = 0; i < sizeof(bad_pointers) / sizeof(bad_pointers[0]); ++i) {
        uintptr_t bad = bad_pointers[i].value;
        const char *pointer = bad_pointers[i].name;
#define CHECK_CASE(label, call) expect_fault(label, pointer, (call));
        FAULT_CASES(CHECK_CASE)
#undef CHECK_CASE
    }
}

static void check_null_pointers(void) {
    expect_fault("open path", "null", sys_open(0, OPEN_READ));
    expect_fault("stat path", "null", sys_stat(0, scratch));
    expect_fault("stat buffer", "null", sys_stat("/", 0));
    expect_fault("write buffer", "null", sys_write(1, 0, BAD_LEN));
    expect_fault("read buffer", "null", sys_read(0, 0, BAD_LEN));
    expect_fault("getcwd buffer", "null", syscall2(SYS_GETCWD, 0, 64));
    expect_fault("pipe fds", "null", sys_pipe(0));
    expect_fault("sysinfo", "null", sys_sysinfo(0));
    expect_fault("poll_many list", "null", sys_poll_many(0, 4, 0));
}

/* A valid base combined with a length that wraps the address space or runs
   off the end of user space must be rejected rather than truncated. */
static void check_bad_lengths(void) {
    static const unsigned long lengths[] = {
        ~0ul,
        0x8000000000000000ul,
        0x0000800000000000ul,
        0x00007FFFFFFFFFFFul,
    };
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        expect_fault("write length", "valid", sys_write(1, scratch, lengths[i]));
        expect_fault("read length", "valid", sys_read(0, scratch, lengths[i]));
    }
    expect_fault("writef length", "valid",
                 sys_writef("/faulttest.tmp", scratch, 0xFFFFFFFFul));
}

static void check_page_boundaries(void) {
    uint8_t *base = (uint8_t *)sys_mmap(0, 2 * PAGE_SIZE, PROT_RW, 0);
    if ((long)base <= 0 || sys_munmap(base + PAGE_SIZE, PAGE_SIZE) != 0) {
        fail("map page followed by a hole");
        return;
    }
    uint8_t *page_end = base + PAGE_SIZE;
    uint8_t *fits = page_end - sizeof(sysinfo_t);

    if (sys_sysinfo((sysinfo_t *)fits) != 0)
        fail("buffer ending at the page end rejected");
    expect_fault("buffer crossing into hole", "page end",
                 sys_sysinfo((sysinfo_t *)(fits + 1)));
    expect_fault("write crossing into hole", "page end",
                 sys_write(1, page_end - 8, BAD_LEN));

    for (int i = 1; i <= 8; ++i)
        page_end[-i] = 'a';
    expect_fault("unterminated path", "page end",
                 sys_open((const char *)(page_end - 8), OPEN_READ));
    if (sys_munmap(base, PAGE_SIZE) != 0)
        fail("unmap first page");

    base = (uint8_t *)sys_mmap(0, 2 * PAGE_SIZE, PROT_RW, 0);
    if ((long)base <= 0) {
        fail("map two adjacent pages");
        return;
    }
    page_end = base + PAGE_SIZE;
    /* "/" ends the first page and its NUL starts the second one. */
    page_end[-1] = '/';
    page_end[0] = 0;
    if (sys_stat((const char *)(page_end - 1), scratch) != 0)
        fail("path straddling two mapped pages rejected");
    if (sys_sysinfo((sysinfo_t *)(page_end - 4)) != 0)
        fail("buffer straddling two mapped pages rejected");
    if (sys_munmap(base, 2 * PAGE_SIZE) != 0)
        fail("unmap adjacent pages");
}

/* The kernel must neither write through a read-only mapping nor refuse to
   read from one. */
static void check_read_only(void) {
    uint8_t *page = (uint8_t *)sys_mmap(0, PAGE_SIZE, MMAP_PROT_READ, 0);
    if ((long)page <= 0) {
        fail("map read-only page");
        return;
    }
    expect_fault("sysinfo into read-only page", "read-only",
                 sys_sysinfo((sysinfo_t *)page));
    expect_fault("read into read-only page", "read-only",
                 sys_read(0, page, BAD_LEN));
    expect_fault("stat into read-only page", "read-only", sys_stat("/", page));
    if (sys_stat((const char *)page, scratch) == RC_EFAULT)
        fail("read from read-only page rejected");
    if (sys_munmap(page, PAGE_SIZE) != 0)
        fail("unmap read-only page");
}

static int check_valid_calls(void) {
    sysinfo_t info;
    if (sys_sysinfo(&info) != 0 || sys_stat("/", scratch) != 0 ||
        syscall2(SYS_GETCWD, (long)scratch, SCRATCH_BYTES) != 0) {
        fail("syscall with valid pointers");
        return -1;
    }
    open_fd = (int)sys_open("/faulttest", OPEN_READ);
    if (open_fd < 3) {
        fail("open own executable");
        return -1;
    }
    return 0;
}

int main(void) {
    if (check_valid_calls() != 0)
        return 1;
    check_bad_pointers();
    check_null_pointers();
    check_bad_lengths();
    check_page_boundaries();
    check_read_only();
    sys_close(open_fd);
    if (failures != 0) {
        printf("FAULT FAIL %d checks\n", failures);
        return 1;
    }
    printf("FAULT PASS\n");
    return 0;
}
