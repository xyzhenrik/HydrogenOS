#include "errno.h"
#include "mini_libc.h"
#include "syscall.h"

#include <stdint.h>

/*
 * Boot test for kernel write protection and child exit records.
 *
 * Part 1: with CR0.WP set, the kernel must refuse to write into a read-only
 * user page on behalf of a syscall. Every output buffer is a READ-only
 * mapping; each call has to return -EFAULT, leave the page untouched, and not
 * consume anything it would have delivered (a pipe byte, a child's status).
 *
 * Part 2: exit records are owned by the parent and unbounded. Far more
 * children than any fixed table could hold die unreaped, and every status is
 * still delivered exactly once; a second wait reports -ECHILD.
 *
 * Without arguments it runs as the coordinator and spawns itself per role
 * (argv[1]): "child <status> [delay_ms]" exits with <status> after the delay,
 * "orphaner" leaves children behind when it exits.
 */

#define PAGE_SIZE       4096ul
#define PROT_RW         (MMAP_PROT_READ | MMAP_PROT_WRITE)
#define FILL_BYTE       0xA5
#define CHILD_COUNT     100
#define ORPHAN_COUNT    20
#define SLOW_CHILD_MS   80
#define SLOW_STATUS     42
#define BARRIER_MS      10000
#define NO_SUCH_PID     0x7FFFFF00

static int fail(const char *what, long value) {
    dprintf(2, "wptest: FAIL %s (%ld)\n", what, value);
    return 1;
}

static unsigned parse_unsigned(const char *s) {
    unsigned value = 0;
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (unsigned)(*s++ - '0');
    return value;
}

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int all_bytes(const unsigned char *p, unsigned long len, unsigned char v) {
    for (unsigned long i = 0; i < len; ++i) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

static long spawn_child(unsigned status, unsigned delay_ms, int out_fd) {
    char status_arg[12];
    char delay_arg[12];
    snprintf(status_arg, sizeof(status_arg), "%u", status);
    snprintf(delay_arg, sizeof(delay_arg), "%u", delay_ms);
    const char *argv[] = {"wptest", "child", status_arg, delay_arg, 0};
    return spawn_args("/wptest", argv, 0, SPAWN_STDIO_NULL, out_fd,
                      SPAWN_STDIO_INHERIT);
}

static unsigned status_for(unsigned index) {
    return (index * 37u + 5u) & 0x7Fu;
}

/* ------------------------------------------------------------------ */
/* Part 1: read-only pages                                             */
/* ------------------------------------------------------------------ */

static int check_readonly_buffers(void) {
    long rc = sys_mmap(0, PAGE_SIZE, PROT_RW, 0);
    if (rc <= 0)
        return fail("mmap", rc);
    unsigned char *page = (unsigned char *)rc;
    for (unsigned long i = 0; i < PAGE_SIZE; ++i)
        page[i] = FILL_BYTE;
    rc = sys_mprotect(page, PAGE_SIZE, MMAP_PROT_READ, 0);
    if (rc != 0)
        return fail("mprotect read", rc);

    int fds[2];
    rc = pipe(fds);
    if (rc != 0)
        return fail("pipe", rc);
    if (write(fds[1], "xyz", 3) != 3)
        return fail("pipe write", 0);

    rc = read(fds[0], page, 3);
    if (rc != -EFAULT)
        return fail("read into read-only page", rc);

    /* The rejected read must not have eaten the data. */
    unsigned char scratch[4];
    rc = read(fds[0], scratch, 3);
    if (rc != 3 || scratch[0] != 'x' || scratch[2] != 'z')
        return fail("pipe data lost after rejected read", rc);

    /* An empty pipe must fail at once instead of parking the caller. */
    rc = read(fds[0], page, 1);
    if (rc != -EFAULT)
        return fail("blocking read into read-only page", rc);

    rc = sys_pipe((int *)page);
    if (rc != -EFAULT)
        return fail("pipe into read-only page", rc);
    rc = sys_stat("/", page);
    if (rc != -EFAULT)
        return fail("stat into read-only page", rc);
    rc = sys_rtc_time((struct hydrogen_rtc_time *)page);
    if (rc != -EFAULT)
        return fail("rtc_time into read-only page", rc);
    rc = sys_poll_many(page, 1, 0);
    if (rc != -EFAULT)
        return fail("poll_many on read-only page", rc);

    if (!all_bytes(page, PAGE_SIZE, FILL_BYTE))
        return fail("read-only page was modified", 0);

    /* Waiting with a read-only status pointer fails but keeps the child. */
    long child = spawn_child(SLOW_STATUS, SLOW_CHILD_MS, SPAWN_STDIO_NULL);
    if (child <= 0)
        return fail("spawn child", child);
    rc = sys_wait((int)child, (int *)page);
    if (rc != -EFAULT)
        return fail("wait with read-only status", rc);
    rc = sys_try_wait((int)child, (int *)page);
    if (rc != -EFAULT)
        return fail("try_wait with read-only status", rc);
    int status = -1;
    if (wait_pid((int)child, &status) != child || status != SLOW_STATUS)
        return fail("child lost after rejected wait", status);

    /* The same call works once the page is writable again. */
    rc = sys_mprotect(page, PAGE_SIZE, PROT_RW, 0);
    if (rc != 0)
        return fail("mprotect write", rc);
    if (write(fds[1], "abc", 3) != 3)
        return fail("pipe write 2", 0);
    rc = read(fds[0], page, 3);
    if (rc != 3 || page[0] != 'a' || page[2] != 'c' || page[3] != FILL_BYTE)
        return fail("read into writable page", rc);

    close(fds[0]);
    close(fds[1]);
    rc = sys_munmap(page, PAGE_SIZE);
    if (rc != 0)
        return fail("munmap", rc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Part 2: exit records                                                */
/* ------------------------------------------------------------------ */

static int check_blocking_wait(void) {
    long child = spawn_child(SLOW_STATUS, SLOW_CHILD_MS, SPAWN_STDIO_NULL);
    if (child <= 0)
        return fail("spawn slow child", child);
    int status = -1;
    long rc = wait_pid((int)child, &status);
    if (rc != child || status != SLOW_STATUS)
        return fail("blocking wait", status);
    return 0;
}

static int check_unreaped_children(void) {
    long pids[CHILD_COUNT];
    int barrier[2];
    long rc = pipe(barrier);
    if (rc != 0)
        return fail("barrier pipe", rc);

    /* Children keep the write end as stdout; EOF means all of them are dead
     * while none has been waited for. */
    for (int i = 0; i < CHILD_COUNT; ++i) {
        pids[i] = spawn_child(status_for((unsigned)i), 0, barrier[1]);
        if (pids[i] <= 0)
            return fail("spawn child", pids[i]);
    }
    close(barrier[1]);
    char byte;
    while ((rc = poll_wait_fd(barrier[0], POLL_READ, BARRIER_MS)) > 0) {
        if (read(barrier[0], &byte, 1) == 0)
            break;
    }
    if (rc <= 0)
        return fail("children did not finish", rc);
    close(barrier[0]);

    for (int i = CHILD_COUNT - 1; i >= 0; --i) {
        int status = -1;
        rc = wait_pid((int)pids[i], &status);
        if (rc != pids[i])
            return fail("wait pid", rc);
        if ((unsigned)status != status_for((unsigned)i))
            return fail("wait status", status);
    }

    for (int i = 0; i < CHILD_COUNT; i += 17) {
        int status = -1;
        rc = sys_wait((int)pids[i], &status);
        if (rc != -ECHILD)
            return fail("wait on reaped child", rc);
        rc = sys_try_wait((int)pids[i], &status);
        if (rc != -ECHILD)
            return fail("try_wait on reaped child", rc);
    }
    if (sys_wait(NO_SUCH_PID, 0) != -ECHILD)
        return fail("wait on unknown pid", 0);
    if (sys_try_wait(NO_SUCH_PID, 0) != -ECHILD)
        return fail("try_wait on unknown pid", 0);
    if (sys_wait((int)sys_getpid(), 0) != -ECHILD)
        return fail("wait on self", 0);
    return 0;
}

static int check_orphans(void) {
    const char *argv[] = {"wptest", "orphaner", 0};
    long orphaner = spawn_args("/wptest", argv, 0, SPAWN_STDIO_NULL,
                               SPAWN_STDIO_NULL, SPAWN_STDIO_INHERIT);
    if (orphaner <= 0)
        return fail("spawn orphaner", orphaner);
    int status = -1;
    if (wait_pid((int)orphaner, &status) != orphaner || status != 0)
        return fail("orphaner status", status);
    /* The orphaner's children die after it; the kernel must cope. */
    sleep_ms(3 * SLOW_CHILD_MS);
    long pid = spawn_child(SLOW_STATUS, 0, SPAWN_STDIO_NULL);
    if (pid <= 0 || wait_pid((int)pid, &status) != pid || status != SLOW_STATUS)
        return fail("kernel unusable after orphans", status);
    return 0;
}

static int run_coordinator(void) {
    if (check_readonly_buffers() != 0 || check_blocking_wait() != 0 ||
        check_unreaped_children() != 0 || check_orphans() != 0)
        return 1;
    write(1, "WP PASS\n", 8);
    return 0;
}

/* Leaves unreaped zombies and live children behind. */
static int role_orphaner(void) {
    for (int i = 0; i < ORPHAN_COUNT; ++i) {
        if (spawn_child(status_for((unsigned)i), 0, SPAWN_STDIO_NULL) <= 0)
            return 10;
        if (spawn_child(status_for((unsigned)i), 2 * SLOW_CHILD_MS,
                        SPAWN_STDIO_NULL) <= 0)
            return 11;
    }
    sleep_ms(SLOW_CHILD_MS / 4);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2)
        return run_coordinator();
    if (str_equal(argv[1], "orphaner"))
        return role_orphaner();
    if (str_equal(argv[1], "child") && argc >= 4) {
        unsigned delay = parse_unsigned(argv[3]);
        if (delay != 0)
            sleep_ms(delay);
        return (int)parse_unsigned(argv[2]);
    }
    return fail("unknown role", 0);
}
