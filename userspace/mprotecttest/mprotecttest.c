#include "mini_libc.h"
#include "syscall.h"

#include <stdint.h>

/*
 * Boot test for sys_mprotect, PROT_NONE mappings, W^X and guard pages.
 *
 * Without arguments it checks the syscall in-process and then re-spawns
 * itself once per fault case. Every fault case performs one forbidden access
 * and must be killed by the kernel (PROCESS_EXIT_FAULTED); a child that
 * survives the access exits with status 0 and fails the test. The guard
 * cases come in pairs: the "-edge" child touches the last legal byte and must
 * survive, so the following child can only die because of the guard page.
 */

#define PAGE_SIZE       4096ul
#define RC_ENOMEM       (-12l)
#define RC_EFAULT       (-14l)
#define RC_EACCES       (-13l)
#define RC_EINVAL       (-22l)
#define PROT_RW         (MMAP_PROT_READ | MMAP_PROT_WRITE)
#define PROT_RX         (MMAP_PROT_READ | MMAP_PROT_EXEC)
#define EXIT_FAULTED    139
#define SPLIT_PAGES     4200ul
#define LARGE_REQUEST   (256ul * 1024ul)
/* malloc prefixes each large mapping with a 16-byte header and rounds the
 * total up to whole pages; the guard page follows. */
#define MALLOC_HEADER   16ul
/* Lowest page of the user stack, as set up by the process loader. */
#define STACK_BOTTOM    0x80100000ul

static const unsigned char ret_42[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };

static int fail(const char *what) {
    write(1, "MPROTECT FAIL: ", 15);
    write(1, what, strlen(what));
    write(1, "\n", 1);
    return 1;
}

static int name_is(const char *name, const char *expected) {
    while (*name && *name == *expected) {
        name++;
        expected++;
    }
    return *name == *expected;
}

static int all_bytes(const unsigned char *p, unsigned long len, unsigned char v) {
    for (unsigned long i = 0; i < len; ++i) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

static unsigned char *map_pages(unsigned long pages, unsigned int prot) {
    long rc = sys_mmap(0, pages * PAGE_SIZE, prot, 0);
    return rc > 0 ? (unsigned char *)rc : 0;
}

static int check_errors(void) {
    unsigned char *p = map_pages(4, PROT_RW);
    if (!p)
        return fail("map for errors");

    if (sys_mprotect(p + 1, PAGE_SIZE, MMAP_PROT_READ, 0) != RC_EINVAL)
        return fail("unaligned addr accepted");
    if (sys_mprotect(p, 0, MMAP_PROT_READ, 0) != RC_EINVAL)
        return fail("len 0 accepted");
    if (sys_mprotect((void *)0x80000000ul, PAGE_SIZE, MMAP_PROT_READ, 0) != RC_EINVAL)
        return fail("range outside window accepted");
    if (sys_mprotect(p, ~0ul, MMAP_PROT_READ, 0) != RC_EINVAL)
        return fail("overflowing len accepted");
    if (sys_mprotect(p, 2 * PAGE_SIZE, MMAP_PROT_WRITE, 0) != RC_EINVAL)
        return fail("write-only accepted");
    if (sys_mprotect(p, 2 * PAGE_SIZE, PROT_RW | 0x8u, 0) != RC_EINVAL)
        return fail("unknown prot accepted");
    if (sys_mprotect(p, 2 * PAGE_SIZE, MMAP_PROT_READ, 1u) != RC_EINVAL)
        return fail("unknown flags accepted");
    if (sys_mprotect(p, 2 * PAGE_SIZE, PROT_RW | MMAP_PROT_EXEC, 0) != RC_EACCES)
        return fail("W|X accepted without flag");
    if (sys_mmap(0, PAGE_SIZE, PROT_RW | MMAP_PROT_EXEC, 0) != RC_EACCES)
        return fail("mmap W|X accepted without flag");

    /* A range with a hole is rejected as a whole. */
    if (sys_munmap(p + 2 * PAGE_SIZE, PAGE_SIZE) != 0)
        return fail("punch hole");
    if (sys_mprotect(p, 4 * PAGE_SIZE, MMAP_PROT_READ, 0) != RC_ENOMEM)
        return fail("range with hole accepted");
    if (sys_mprotect(p + 3 * PAGE_SIZE, PAGE_SIZE, MMAP_PROT_READ, 0) != 0)
        return fail("mapped tail rejected");
    if (sys_mprotect(p + 2 * PAGE_SIZE, PAGE_SIZE, MMAP_PROT_READ, 0) != RC_ENOMEM)
        return fail("unmapped range accepted");
    p[0] = 1;
    p[PAGE_SIZE] = 2;
    if (sys_munmap(p, 4 * PAGE_SIZE) != 0)
        return fail("errors cleanup");
    return 0;
}

static int check_roundtrip(void) {
    unsigned char *p = map_pages(8, PROT_RW);
    if (!p)
        return fail("map roundtrip");
    for (unsigned long i = 0; i < 8; ++i)
        memset(p + i * PAGE_SIZE, (int)(0x30 + i), PAGE_SIZE);

    if (sys_mprotect(p + 2 * PAGE_SIZE, 3 * PAGE_SIZE, MMAP_PROT_READ, 0) != 0)
        return fail("make read-only");
    for (unsigned long i = 0; i < 8; ++i) {
        if (!all_bytes(p + i * PAGE_SIZE, PAGE_SIZE, (unsigned char)(0x30 + i)))
            return fail("data lost by read-only");
    }
    p[0] = 0x71;
    p[7 * PAGE_SIZE] = 0x72;

    if (sys_mprotect(p + 3 * PAGE_SIZE, PAGE_SIZE, MMAP_PROT_NONE, 0) != 0)
        return fail("make none");
    if (sys_mprotect(p + 3 * PAGE_SIZE, PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("restore from none");
    if (!all_bytes(p + 3 * PAGE_SIZE, PAGE_SIZE, 0x33))
        return fail("data lost by none");
    memset(p + 3 * PAGE_SIZE, 0x44, PAGE_SIZE);

    if (sys_mprotect(p, 8 * PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("restore all");
    memset(p + 2 * PAGE_SIZE, 0x55, PAGE_SIZE);
    if (p[0] != 0x71 || p[7 * PAGE_SIZE] != 0x72 ||
        !all_bytes(p + 3 * PAGE_SIZE, PAGE_SIZE, 0x44))
        return fail("data lost by restore");

    /* Unmapping must still work on the re-merged region. */
    if (sys_munmap(p + 2 * PAGE_SIZE, 4 * PAGE_SIZE) != 0 ||
        sys_munmap(p, 2 * PAGE_SIZE) != 0 ||
        sys_munmap(p + 6 * PAGE_SIZE, 2 * PAGE_SIZE) != 0)
        return fail("roundtrip cleanup");
    return 0;
}

static int check_none_mapping(void) {
    unsigned char *p = map_pages(3, MMAP_PROT_NONE);
    if (!p)
        return fail("map prot none");
    if (sys_mprotect(p + PAGE_SIZE, PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("open middle of none mapping");
    if (!all_bytes(p + PAGE_SIZE, PAGE_SIZE, 0))
        return fail("none mapping not zero-filled");
    p[PAGE_SIZE] = 9;
    if (sys_mprotect(p, 3 * PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("open none mapping");
    if (p[PAGE_SIZE] != 9 || !all_bytes(p, PAGE_SIZE, 0))
        return fail("none mapping content");
    if (sys_munmap(p, 3 * PAGE_SIZE) != 0)
        return fail("none mapping cleanup");
    return 0;
}

static int check_exec(void) {
    unsigned char *p = map_pages(1, PROT_RW);
    if (!p)
        return fail("map exec");
    memcpy(p, ret_42, sizeof(ret_42));
    if (sys_mprotect(p, PAGE_SIZE, PROT_RX, 0) != 0)
        return fail("make executable");
    if (((int (*)(void))(uintptr_t)p)() != 42)
        return fail("mprotected page not executable");
    if (sys_mprotect(p, PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("drop exec");
    if (sys_mprotect(p, PAGE_SIZE, PROT_RW | MMAP_PROT_EXEC, MMAP_FLAG_ALLOW_WX) != 0)
        return fail("W|X refused with flag");
    p[sizeof(ret_42)] = 0x90;
    if (((int (*)(void))(uintptr_t)p)() != 42)
        return fail("W|X page not executable");
    if (sys_munmap(p, PAGE_SIZE) != 0)
        return fail("exec cleanup");
    return 0;
}

/* Alternating protections need one region per page; the per-process region
 * limit must reject the overflow without changing anything, and a single
 * call must merge everything back. */
static int check_split_limit(void) {
    unsigned char *p = map_pages(SPLIT_PAGES, PROT_RW);
    if (!p)
        return fail("map split range");
    unsigned long applied = 0;
    long rc = 0;
    for (unsigned long i = 1; i < SPLIT_PAGES; i += 2) {
        rc = sys_mprotect(p + i * PAGE_SIZE, PAGE_SIZE, MMAP_PROT_READ, 0);
        if (rc != 0)
            break;
        applied++;
    }
    if (rc != RC_ENOMEM || applied < 1000)
        return fail("region limit not enforced");
    if (sys_mprotect(p, SPLIT_PAGES * PAGE_SIZE, PROT_RW, 0) != 0)
        return fail("merge back");
    for (unsigned long i = 0; i < SPLIT_PAGES; i += 64)
        p[i * PAGE_SIZE] = 1;
    if (sys_mprotect(p + PAGE_SIZE, PAGE_SIZE, MMAP_PROT_READ, 0) != 0)
        return fail("limit not released by merge");
    if (sys_munmap(p, SPLIT_PAGES * PAGE_SIZE) != 0)
        return fail("split cleanup");
    return 0;
}

/* Syscalls act for the kernel, which ignores page rights; they must still
 * refuse buffers that touch a PROT_NONE page instead of writing into it. */
static int check_kernel_access(void) {
    unsigned char *p = map_pages(2, PROT_RW);
    if (!p || sys_mprotect(p + PAGE_SIZE, PAGE_SIZE, MMAP_PROT_NONE, 0) != 0)
        return fail("map kernel access");
    int fds[2];
    if (sys_pipe(fds) != 0)
        return fail("pipe");
    char msg[16] = "0123456789abcdef";
    if (write(fds[1], msg, sizeof(msg)) != (long)sizeof(msg))
        return fail("pipe write");
    unsigned char *straddle = p + PAGE_SIZE - 8;
    if (read(fds[0], straddle, sizeof(msg)) != RC_EFAULT)
        return fail("read into guard accepted");
    if (write(fds[1], straddle, sizeof(msg)) != RC_EFAULT)
        return fail("write from guard accepted");
    if (read(fds[0], p + PAGE_SIZE - sizeof(msg), sizeof(msg)) != (long)sizeof(msg))
        return fail("read below guard rejected");
    sys_close(fds[0]);
    sys_close(fds[1]);
    if (sys_munmap(p, 2 * PAGE_SIZE) != 0)
        return fail("kernel access cleanup");
    return 0;
}

/* Performs one forbidden access and returns if the kernel allowed it. */
static int fault_case(const char *name) {
    unsigned char *p;
    if (name_is(name, "write-readonly")) {
        p = map_pages(2, PROT_RW);
        if (!p || sys_mprotect(p, PAGE_SIZE, MMAP_PROT_READ, 0) != 0)
            return 2;
        p[PAGE_SIZE] = 1;
        p[0] = 1;
    } else if (name_is(name, "read-none")) {
        p = map_pages(1, PROT_RW);
        if (!p || sys_mprotect(p, PAGE_SIZE, MMAP_PROT_NONE, 0) != 0)
            return 2;
        return ((volatile unsigned char *)p)[0] == 0 ? 0 : 3;
    } else if (name_is(name, "write-none")) {
        p = map_pages(1, MMAP_PROT_NONE);
        if (!p)
            return 2;
        p[0] = 1;
    } else if (name_is(name, "exec-data")) {
        p = map_pages(1, PROT_RW);
        if (!p)
            return 2;
        memcpy(p, ret_42, sizeof(ret_42));
        ((int (*)(void))(uintptr_t)p)();
    } else if (name_is(name, "exec-dropped")) {
        p = map_pages(1, PROT_RW);
        if (!p)
            return 2;
        memcpy(p, ret_42, sizeof(ret_42));
        if (sys_mprotect(p, PAGE_SIZE, PROT_RX, 0) != 0 ||
            sys_mprotect(p, PAGE_SIZE, MMAP_PROT_READ, 0) != 0)
            return 2;
        ((int (*)(void))(uintptr_t)p)();
    } else if (name_is(name, "malloc-edge") || name_is(name, "malloc-guard")) {
        p = (unsigned char *)malloc(LARGE_REQUEST);
        if (!p)
            return 2;
        unsigned char *mapping = (unsigned char *)((uintptr_t)p & ~(PAGE_SIZE - 1));
        unsigned char *guard = mapping +
            ((LARGE_REQUEST + MALLOC_HEADER + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
        ((volatile unsigned char *)guard)[-1] = 1;
        if (name_is(name, "malloc-guard"))
            ((volatile unsigned char *)guard)[PAGE_SIZE - 1] = 1;
    } else if (name_is(name, "stack-edge") || name_is(name, "stack-guard")) {
        volatile unsigned char *bottom = (volatile unsigned char *)STACK_BOTTOM;
        bottom[0] = 1;
        if (name_is(name, "stack-guard"))
            bottom[-1] = 1;
    } else {
        return 4;
    }
    return 0;
}

static int run_child(const char *name, int expected_status) {
    const char *argv[] = { "/mprotecttest", name, 0 };
    long pid = spawn_args("/mprotecttest", argv, 0, SPAWN_STDIO_NULL,
                          SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    if (pid <= 0)
        return fail("spawn fault child");
    int status = -1;
    if (wait_pid((int)pid, &status) != pid)
        return fail("wait fault child");
    if (status != expected_status) {
        write(1, "MPROTECT FAIL: unexpected status: ", 34);
        write(1, name, strlen(name));
        write(1, "\n", 1);
        return 1;
    }
    return 0;
}

static int check_faults(void) {
    static const char *const cases[] = {
        "write-readonly", "read-none", "write-none", "exec-data",
        "exec-dropped", "malloc-guard", "stack-guard",
    };
    if (run_child("malloc-edge", 0) != 0 || run_child("stack-edge", 0) != 0)
        return 1;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (run_child(cases[i], EXIT_FAULTED) != 0)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2)
        return fault_case(argv[1]);

    if (check_errors() || check_roundtrip() || check_none_mapping() ||
        check_exec() || check_kernel_access() ||
        check_split_limit() || check_faults())
        return 1;
    write(1, "MPROTECT PASS\n", 14);
    return 0;
}
