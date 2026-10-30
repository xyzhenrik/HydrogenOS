#include "crash.h"
#include "errno.h"
#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

/*
 * Boot test for the kernel crash dump. It drains any dump left over from
 * earlier tests, then provokes the two ways to get one while the system keeps
 * running (a fatal fault of a child process and a simulated kernel oops),
 * collects each into /var/crash and checks its sections. It also checks that
 * an unprivileged caller is refused and that collecting empties the kernel's
 * copy. Prints CRASH PASS.
 */

enum {
    PATH_CAP = 64,
    UNPRIVILEGED_UID = 1000,
    ROLE_STATUS_DENIED = 0,
    ROLE_STATUS_WRONG = 1,
};

static char text[HYDROGEN_CRASH_DUMP_MAX + 1];

static int fail(const char *what) {
    dprintf(1, "CRASH FAIL: %s\n", what);
    return 1;
}

static int str_equal(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int read_dump(const char *path) {
    long fd = open(path, OPEN_READ);
    if (fd < 0)
        return -1;
    unsigned long total = 0;
    long got;
    while (total < sizeof(text) - 1 &&
           (got = read((int)fd, text + total, sizeof(text) - 1 - total)) > 0)
        total += (unsigned long)got;
    close((int)fd);
    text[total] = '\0';
    return total > 0 ? 0 : -1;
}

static int contains_all(const char *const *needles) {
    for (; *needles; ++needles) {
        if (!strstr(text, *needles)) {
            dprintf(1, "CRASH FAIL: dump lacks \"%s\"\n", *needles);
            return 0;
        }
    }
    return 1;
}

/* Collects the pending dump into /var/crash and loads it into text. */
static int collect_and_read(void) {
    char path[PATH_CAP];
    long rc = crash_collect(path, sizeof(path));
    if (rc != 1)
        return fail("collect did not store a dump");
    if (read_dump(path) != 0)
        return fail("collected dump is not readable");
    return 0;
}

/* Page zero is never mapped; the address is read from memory so that the
   compiler cannot see the null-page store and reject or drop it. */
static volatile uintptr_t unmapped_address = 16;

static int role_fault(void) {
    *(volatile int *)unmapped_address = 1;
    return 0;
}

static int role_unprivileged(void) {
    if (sys_setuid(UNPRIVILEGED_UID) != 0)
        return ROLE_STATUS_WRONG;
    long rc = sys_crash(HYDROGEN_CRASH_OP_TEST, 0, 0);
    return rc == -EPERM ? ROLE_STATUS_DENIED : ROLE_STATUS_WRONG;
}

static int run_child(const char *role, int *status) {
    const char *argv[] = {"/lib/tests/crashtest", role, 0};
    long pid = spawn_args("/lib/tests/crashtest", argv, 0, SPAWN_STDIO_NULL,
                          SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    if (pid <= 0)
        return -1;
    return wait_pid((int)pid, status) == pid ? 0 : -1;
}

static int check_user_fault(void) {
    int status;
    if (run_child("fault", &status) != 0 || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGSEGV)
        return fail("fault child did not die of SIGSEGV");
    if (collect_and_read() != 0)
        return 1;
    static const char *const needles[] = {
        "HydrogenOS crash dump", "Kind:     user fault", "Reason:   SIGSEGV",
        "(crashtest)", "Registers", "Backtrace", "  #0 0x", "Processes",
        "Log", 0,
    };
    return contains_all(needles) ? 0 : 1;
}

static int check_oops(void) {
    if (sys_crash(HYDROGEN_CRASH_OP_TEST, 0, 0) != 0)
        return fail("simulated oops was refused or not stored");
    if (collect_and_read() != 0)
        return 1;
    static const char *const needles[] = {
        "HydrogenOS crash dump", "Kind:     oops", "crashtest: simulated oops",
        "Backtrace", "  #0 0x", "Processes", "* ", "crashtest", "Log", 0,
    };
    return contains_all(needles) ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && str_equal(argv[1], "fault"))
        return role_fault();
    if (argc > 1 && str_equal(argv[1], "unprivileged"))
        return role_unprivileged();

    char path[PATH_CAP];
    if (crash_collect(path, sizeof(path)) < 0)
        return fail("draining earlier dumps");

    int status;
    if (run_child("unprivileged", &status) != 0 || status != ROLE_STATUS_DENIED)
        return fail("unprivileged caller was not refused");
    if (check_user_fault() != 0 || check_oops() != 0)
        return 1;
    if (sys_crash(HYDROGEN_CRASH_OP_READ, text, HYDROGEN_CRASH_DUMP_MAX) != -ENOENT)
        return fail("collected dump is still pending in the kernel");

    write(1, "CRASH PASS\n", 11);
    return 0;
}
