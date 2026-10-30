#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

/*
 * Boot test for init's supervision and for SYS_SIGPROCMASK.
 *   - supervisehang is a oneshot with a short timeout: init must have killed
 *     it and carried on, which is why this test runs at all
 *   - superviseflap is a daemon whose first instance fails: init must have
 *     started a second one and left it alone
 *   - a blocked signal stays pending, does not cut a sleep short, collapses
 *     with repeats and runs its handler when it is unblocked
 */

#define RC_ESRCH (-3l)
#define RC_EINVAL (-22l)
#define RC_EFAULT (-14l)

#define RUNS_PATH "/tmp/supervise.runs"
#define HANG_PATH "/tmp/supervise.hang"
#define EXPECTED_RUNS 2
#define RESTART_WAIT_MS 15000
#define POLL_INTERVAL_MS 20
#define SETTLE_MS 500
#define BLOCKED_SLEEP_MS 60
/* The scheduler may wake a sleeper one 10 ms tick early. */
#define SLEEP_SLACK_MS 10
#define UNMAPPED_ADDRESS 0x0000600000000000ul
#define UNKNOWN_SIGNAL_BIT (1u << 20)

static const char *fail_reason;

static volatile int usr1_count;
static volatile unsigned int usr1_mask_inside;

static int fail(const char *reason) {
    if (!fail_reason)
        fail_reason = reason;
    return -1;
}

static void usr1_handler(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    unsigned int mask = 0;
    sigprocmask(SIG_BLOCK, 0, &mask);
    usr1_mask_inside = mask;
    ++usr1_count;
}

static unsigned int current_mask(void) {
    unsigned int mask = 0xFFFFFFFFu;
    if (sigprocmask(SIG_BLOCK, 0, &mask) != 0)
        return 0xFFFFFFFFu;
    return mask;
}

static int read_pids(const char *path, long *pids, int capacity) {
    char buf[128];
    int count = 0;
    long fd = open(path, OPEN_READ);
    long length;
    const char *cursor = buf;
    if (fd < 0)
        return -1;
    length = read((int)fd, buf, sizeof(buf) - 1);
    close((int)fd);
    if (length < 0)
        return -1;
    buf[length] = 0;
    while (*cursor && count < capacity) {
        char *end;
        long pid = (long)strtoul(cursor, &end, 10);
        if (end == cursor || *end != '\n')
            break;
        pids[count++] = pid;
        cursor = end + 1;
    }
    return count;
}

static int check_hung_oneshot_killed(void) {
    long pid;
    if (read_pids(HANG_PATH, &pid, 1) != 1)
        return fail("hung oneshot never started");
    if (kill((int)pid, 0) != RC_ESRCH)
        return fail("hung oneshot still alive after its timeout");
    return 0;
}

static int check_daemon_restarted(void) {
    long pids[4];
    unsigned long start = time_ms();
    int count = 0;

    while (time_ms() - start < RESTART_WAIT_MS) {
        count = read_pids(RUNS_PATH, pids, 4);
        if (count >= EXPECTED_RUNS)
            break;
        sleep_ms(POLL_INTERVAL_MS);
    }
    if (count < EXPECTED_RUNS)
        return fail("daemon was not restarted");
    sleep_ms(SETTLE_MS);
    count = read_pids(RUNS_PATH, pids, 4);
    if (count != EXPECTED_RUNS)
        return fail("daemon restarted more than once");
    if (pids[0] == pids[1])
        return fail("restart reused the old process");
    if (kill((int)pids[0], 0) != RC_ESRCH)
        return fail("failed daemon instance still alive");
    if (kill((int)pids[1], 0) != 0)
        return fail("restarted daemon is not running");
    return 0;
}

static int check_mask_arguments(void) {
    unsigned int old = 0;
    if (current_mask() != 0)
        return fail("initial signal mask not empty");
    if (sigprocmask(3, SIGNAL_BIT(SIGUSR1), 0) != RC_EINVAL)
        return fail("unknown how accepted");
    if (sigprocmask(SIG_BLOCK, UNKNOWN_SIGNAL_BIT, 0) != RC_EINVAL)
        return fail("unknown signal accepted");
    if (sigprocmask(SIG_BLOCK, SIGNAL_BIT(SIGUSR1),
                    (unsigned int *)UNMAPPED_ADDRESS) != RC_EFAULT)
        return fail("unmapped old pointer accepted");
    if (current_mask() != 0)
        return fail("failed call changed the mask");
    if (sigprocmask(SIG_SETMASK, SIGNAL_BIT(SIGKILL) | SIGNAL_BIT(SIGUSR2),
                    &old) != 0 || old != 0)
        return fail("setmask failed");
    if (current_mask() != SIGNAL_BIT(SIGUSR2))
        return fail("SIGKILL was blocked");
    if (sigprocmask(SIG_UNBLOCK, SIGNAL_BIT(SIGUSR2), &old) != 0 ||
        old != SIGNAL_BIT(SIGUSR2) || current_mask() != 0)
        return fail("unblock failed");
    return 0;
}

static int check_deferred_delivery(void) {
    unsigned int old = 0;
    unsigned long start;
    long self = sys_getpid();

    if (signal_install(SIGUSR1, usr1_handler) != 0)
        return fail("cannot install handler");
    if (sigprocmask(SIG_BLOCK, SIGNAL_BIT(SIGUSR1), &old) != 0 || old != 0)
        return fail("block failed");
    if (kill((int)self, SIGUSR1) != 0 || kill((int)self, SIGUSR1) != 0)
        return fail("cannot signal self");
    start = time_ms();
    sleep_ms(BLOCKED_SLEEP_MS);
    if (time_ms() - start + SLEEP_SLACK_MS < BLOCKED_SLEEP_MS)
        return fail("blocked signal cut the sleep short");
    if (usr1_count != 0)
        return fail("handler ran while blocked");
    if (sigprocmask(SIG_UNBLOCK, SIGNAL_BIT(SIGUSR1), &old) != 0 ||
        old != SIGNAL_BIT(SIGUSR1))
        return fail("unblock failed");
    if (usr1_count != 1)
        return fail("pending signal not delivered once on unblock");
    if (usr1_mask_inside != SIGNAL_BIT(SIGUSR1))
        return fail("signal not blocked inside its handler");
    if (current_mask() != 0)
        return fail("mask not restored after the handler");
    return 0;
}

int main(void) {
    if (check_hung_oneshot_killed() != 0 || check_daemon_restarted() != 0 ||
        check_mask_arguments() != 0 || check_deferred_delivery() != 0) {
        dprintf(2, "SUPERVISE FAIL: %s\n", fail_reason);
        return 1;
    }
    sys_unlink(RUNS_PATH);
    sys_unlink(HANG_PATH);
    printf("SUPERVISE PASS\n");
    return 0;
}
