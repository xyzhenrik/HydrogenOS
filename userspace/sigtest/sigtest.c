#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

#define STRINGIFY_VALUE(x) #x
#define STRINGIFY(x) STRINGIFY_VALUE(x)

#define RC_EINTR (-4l)
#define RC_EPERM (-1l)
#define RC_ESRCH (-3l)
#define RC_EFAULT (-14l)
#define RC_EINVAL (-22l)

#define EXIT_SEGV_HANDLED 42
#define EXIT_WIDE_STATUS_SEEN 0xFF
#define BOOTSTRAP_PID 1
#define KICK_INTERVAL_MS 100
#define KICK_ATTEMPTS 50
#define STATE_TIMEOUT_MS 5000
#define SPIN_TIMEOUT_MS 10000
#define UNMAPPED_ADDRESS 0x0000600000000000ul

static volatile int usr1_count;
static volatile int usr1_in_handler;
static volatile int usr1_overlapped;
static volatile int usr1_signo;
static volatile int chld_count;
static volatile int term_signo;
static volatile int spin_flag;

/* Overwrites every register it may and returns, so that only SYS_SIGRETURN
 * can give the interrupted code its registers back. */
void clobber_handler(void);
__asm__(".text\n"
        ".globl clobber_handler\n"
        "clobber_handler:\n"
        "    movabs $0x7777777777777777, %rbx\n"
        "    movabs $0x7777777777777777, %rbp\n"
        "    movabs $0x7777777777777777, %r8\n"
        "    movabs $0x7777777777777777, %r9\n"
        "    movabs $0x7777777777777777, %r10\n"
        "    movabs $0x7777777777777777, %r12\n"
        "    movabs $0x7777777777777777, %r13\n"
        "    movabs $0x7777777777777777, %r14\n"
        "    movabs $0x7777777777777777, %r15\n"
        "    movabs $0x7777777777777777, %rdx\n"
        "    movabs $0x7777777777777777, %rdi\n"
        "    movabs $0x7777777777777777, %rsi\n"
        "    mov $0x7777, %eax\n"
        "    clc\n"
        "    ret\n");

/* register_roundtrip(pid): sends SIGUSR2 to pid with known register contents
 * and returns 1 if all of them (and the carry flag) survived the handler. */
long register_roundtrip(long pid);
__asm__(".text\n"
        ".globl register_roundtrip\n"
        "register_roundtrip:\n"
        "    push %rbx\n"
        "    push %rbp\n"
        "    push %r12\n"
        "    push %r13\n"
        "    push %r14\n"
        "    push %r15\n"
        "    push %rdi\n"
        "    movabs $0x0101010101010101, %rbx\n"
        "    movabs $0x0202020202020202, %rbp\n"
        "    movabs $0x0303030303030303, %r8\n"
        "    movabs $0x0404040404040404, %r9\n"
        "    movabs $0x0505050505050505, %r10\n"
        "    movabs $0x0606060606060606, %r12\n"
        "    movabs $0x0707070707070707, %r13\n"
        "    movabs $0x0808080808080808, %r14\n"
        "    movabs $0x0909090909090909, %r15\n"
        "    movabs $0x0a0a0a0a0a0a0a0a, %rdx\n"
        "    mov $" STRINGIFY(SIGUSR2) ", %esi\n"
        "    mov $" STRINGIFY(SYS_KILL) ", %eax\n"
        "    stc\n"
        "    syscall\n"
        "    jnc 1f\n"
        "    test %rax, %rax\n"
        "    jnz 1f\n"
        "    cmp (%rsp), %rdi\n"
        "    jne 1f\n"
        "    cmp $" STRINGIFY(SIGUSR2) ", %rsi\n"
        "    jne 1f\n"
        "    movabs $0x0101010101010101, %rcx\n"
        "    cmp %rcx, %rbx\n"
        "    jne 1f\n"
        "    movabs $0x0202020202020202, %rcx\n"
        "    cmp %rcx, %rbp\n"
        "    jne 1f\n"
        "    movabs $0x0303030303030303, %rcx\n"
        "    cmp %rcx, %r8\n"
        "    jne 1f\n"
        "    movabs $0x0404040404040404, %rcx\n"
        "    cmp %rcx, %r9\n"
        "    jne 1f\n"
        "    movabs $0x0505050505050505, %rcx\n"
        "    cmp %rcx, %r10\n"
        "    jne 1f\n"
        "    movabs $0x0606060606060606, %rcx\n"
        "    cmp %rcx, %r12\n"
        "    jne 1f\n"
        "    movabs $0x0707070707070707, %rcx\n"
        "    cmp %rcx, %r13\n"
        "    jne 1f\n"
        "    movabs $0x0808080808080808, %rcx\n"
        "    cmp %rcx, %r14\n"
        "    jne 1f\n"
        "    movabs $0x0909090909090909, %rcx\n"
        "    cmp %rcx, %r15\n"
        "    jne 1f\n"
        "    movabs $0x0a0a0a0a0a0a0a0a, %rcx\n"
        "    cmp %rcx, %rdx\n"
        "    jne 1f\n"
        "    mov $1, %eax\n"
        "    jmp 2f\n"
        "1:  xor %eax, %eax\n"
        "2:  add $8, %rsp\n"
        "    pop %r15\n"
        "    pop %r14\n"
        "    pop %r13\n"
        "    pop %r12\n"
        "    pop %rbp\n"
        "    pop %rbx\n"
        "    ret\n");

/* Child modes that end by abusing the kernel. They never return. */
static void sigreturn_with_context(unsigned long cs) {
    static uint64_t fake[24] __attribute__((aligned(16)));
    sigcontext_t *context = (sigcontext_t *)fake;
    context->rip = 0x80000000ul;
    context->rsp = 0x80100000ul;
    context->cs = cs;
    context->ss = 0x1B;
    context->rflags = 0x202;
    __asm__ volatile("mov %0, %%rsp\n"
                     "mov $" STRINGIFY(SYS_SIGRETURN) ", %%eax\n"
                     "syscall\n"
                     :
                     : "r"(fake)
                     : "memory");
    for (;;) {
    }
}

static void kill_self_on_bad_stack(void) {
    long pid = sys_getpid();
    __asm__ volatile("mov $0x20, %%rsp\n"
                     "mov $" STRINGIFY(SYS_KILL) ", %%eax\n"
                     "mov $" STRINGIFY(SIGUSR1) ", %%esi\n"
                     "syscall\n"
                     :
                     : "D"(pid)
                     : "rax", "rsi", "memory");
    for (;;) {
    }
}

static void usr1_handler(int sig, const sigcontext_t *context) {
    (void)context;
    usr1_signo = sig;
    if (usr1_in_handler)
        usr1_overlapped = 1;
    usr1_in_handler = 1;
    /* Blocked while its own handler runs: delivered once this one returned. */
    if (usr1_count == 0)
        kill((int)sys_getpid(), SIGUSR1);
    usr1_count++;
    usr1_in_handler = 0;
}

static void chld_handler(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    chld_count++;
}

static void term_handler(int sig, const sigcontext_t *context) {
    (void)context;
    term_signo = sig;
}

static void spin_handler(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    spin_flag = 1;
}

static void segv_handler(int sig, const sigcontext_t *context) {
    if (sig == SIGSEGV && context->trap == 14 && context->fault_addr == 0x10)
        sys_exit(EXIT_SEGV_HANDLED);
    sys_exit(EXIT_SEGV_HANDLED + 1);
}

static void store_to(uintptr_t address) {
    volatile int *volatile target = (volatile int *)address;
    *target = 1;
}

static void announce_ready(void) {
    write(1, "R", 1);
}

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int contains(const char *text, const char *needle) {
    for (; *text; ++text) {
        const char *t = text;
        const char *n = needle;
        while (*n && *t == *n) {
            ++t;
            ++n;
        }
        if (*n == 0)
            return 1;
    }
    return 0;
}

static int child_main(int argc, char **argv) {
    const char *mode = argv[1];
    long rc;

    if (str_equal(mode, "sleeper")) {
        signal_install(SIGTERM, term_handler);
        announce_ready();
        for (;;)
            sleep_ms(60000);
    }
    if (str_equal(mode, "segv")) {
        announce_ready();
        store_to(0);
        return 1;
    }
    if (str_equal(mode, "segvhandler")) {
        signal_install(SIGSEGV, segv_handler);
        announce_ready();
        store_to(0x10);
        return 1;
    }
    if (str_equal(mode, "ill")) {
        announce_ready();
        __asm__ volatile("ud2");
        return 1;
    }
    if (str_equal(mode, "fpe")) {
        announce_ready();
        __asm__ volatile("xor %%edx, %%edx\n"
                         "mov $1, %%eax\n"
                         "xor %%ecx, %%ecx\n"
                         "div %%ecx\n"
                         :
                         :
                         : "rax", "rcx", "rdx");
        return 1;
    }
    if (str_equal(mode, "eintr-sleep")) {
        signal_install(SIGTERM, term_handler);
        announce_ready();
        rc = sys_sleep(60000);
        return rc == RC_EINTR && term_signo == SIGTERM ? 0 : 1;
    }
    if (str_equal(mode, "eintr-pipe")) {
        int fds[2];
        char byte;
        if (pipe(fds) != 0)
            return 1;
        signal_install(SIGTERM, term_handler);
        announce_ready();
        rc = read(fds[0], &byte, 1);
        return rc == RC_EINTR && term_signo == SIGTERM ? 0 : 1;
    }
    if (str_equal(mode, "spin")) {
        unsigned long deadline = time_ms() + SPIN_TIMEOUT_MS;
        signal_install(SIGUSR1, spin_handler);
        announce_ready();
        while (!spin_flag && time_ms() < deadline) {
        }
        return spin_flag ? 0 : 1;
    }
    if (str_equal(mode, "badreturn")) {
        announce_ready();
        sigreturn_with_context(0x08);
    }
    if (str_equal(mode, "badstack")) {
        signal_install(SIGUSR1, usr1_handler);
        announce_ready();
        kill_self_on_bad_stack();
    }
    if (str_equal(mode, "kicker") && argc >= 4) {
        int target = 0;
        int sig = signal_number(argv[3]);
        for (const char *c = argv[2]; *c; ++c)
            target = target * 10 + (*c - '0');
        announce_ready();
        for (int attempt = 0; attempt < KICK_ATTEMPTS; ++attempt) {
            sleep_ms(KICK_INTERVAL_MS);
            kill(target, sig);
        }
        return 0;
    }
    if (str_equal(mode, "exitwide")) {
        announce_ready();
        return -1;
    }
    if (str_equal(mode, "exit")) {
        announce_ready();
        return 7;
    }
    return 2;
}

static int fail(const char *what) {
    dprintf(1, "SIG FAIL: %s\n", what);
    return 1;
}

/* Starts this program in the given child mode with its stdout connected to a
 * pipe, and returns once the child reports it is set up. */
static long start_child(const char *mode, const char *arg1, const char *arg2) {
    const char *argv[5] = {"/lib/tests/sigtest", mode, arg1, arg2, 0};
    int fds[2];
    long pid;
    long count = 0;
    char byte = 0;

    if (pipe(fds) != 0)
        return -1;
    pid = spawn_args("/lib/tests/sigtest", argv, 0, SPAWN_STDIO_NULL, fds[1],
                     SPAWN_STDIO_NULL);
    close(fds[1]);
    if (pid > 0) {
        do {
            count = read(fds[0], &byte, 1);
        } while (count == RC_EINTR);
    }
    close(fds[0]);
    return pid > 0 && count == 1 && byte == 'R' ? pid : -1;
}

static int wait_for_state(long pid, const char *state) {
    char path[32];
    char text[256];
    unsigned long deadline = time_ms() + STATE_TIMEOUT_MS;

    snprintf(path, sizeof(path), "/proc/%ld/status", pid);
    while (time_ms() < deadline) {
        long fd = open(path, OPEN_READ);
        if (fd >= 0) {
            long count = read((int)fd, text, sizeof(text) - 1);
            close((int)fd);
            if (count > 0) {
                text[count] = 0;
                if (contains(text, state))
                    return 1;
            }
        }
        yield_cpu();
    }
    return 0;
}

static int expect_status(long pid, int expected, const char *what) {
    int status = -1;
    if (wait_pid((int)pid, &status) != pid || status != expected) {
        dprintf(1, "SIG FAIL: %s (status %d)\n", what, status);
        return 1;
    }
    return 0;
}

#define KILLED_BY(sig) (SIGNAL_STATUS_SIGNALED | (sig))

static int test_argument_errors(void) {
    sigaction_t act = {.handler = SIG_IGN, .restorer = 0, .mask = 0, .flags = 0};
    sigaction_t old;
    int self = (int)sys_getpid();

    if (sigaction(SIGKILL, &act, 0) != RC_EINVAL)
        return fail("SIGKILL handler accepted");
    if (sigaction(0, &act, 0) != RC_EINVAL || sigaction(64, &act, 0) != RC_EINVAL ||
        sigaction(3, &act, 0) != RC_EINVAL)
        return fail("invalid signal number accepted");
    act.flags = 1;
    if (sigaction(SIGUSR1, &act, 0) != RC_EINVAL)
        return fail("reserved flags accepted");
    act.flags = 0;
    act.handler = 0x10;
    if (sigaction(SIGUSR1, &act, 0) != RC_EINVAL)
        return fail("null-page handler accepted");
    if (sigaction(SIGUSR1, (const sigaction_t *)UNMAPPED_ADDRESS, 0) != RC_EFAULT ||
        sigaction(SIGUSR1, 0, (sigaction_t *)UNMAPPED_ADDRESS) != RC_EFAULT)
        return fail("unmapped sigaction pointer accepted");
    if (sigaction(SIGUSR1, 0, &old) != 0 || old.handler != SIG_DFL)
        return fail("default disposition");
    if (kill(self, 64) != RC_EINVAL || kill(self, 3) != RC_EINVAL)
        return fail("kill with invalid signal");
    if (kill(0, SIGTERM) != RC_ESRCH || kill(0x7FFFFFF, SIGTERM) != RC_ESRCH)
        return fail("kill of missing process");
    if (kill(self, 0) != 0)
        return fail("kill existence probe");
    return 0;
}

static int test_handler_and_registers(void) {
    sigaction_t old;
    sigaction_t ignore = {.handler = SIG_IGN, .restorer = 0, .mask = 0, .flags = 0};
    int self = (int)sys_getpid();

    signal_install(SIGUSR1, usr1_handler);
    if (kill(self, SIGUSR1) != 0)
        return fail("kill(self)");
    if (usr1_count != 2 || usr1_signo != SIGUSR1)
        return fail("handler did not run twice");
    if (usr1_overlapped)
        return fail("signal not blocked in its own handler");
    if (sigaction(SIGUSR1, 0, &old) != 0 || old.handler != (unsigned long)usr1_handler ||
        old.restorer != (unsigned long)sigreturn_trampoline)
        return fail("sigaction readback");

    if (sigaction(SIGUSR2, &ignore, 0) != 0 || kill(self, SIGUSR2) != 0)
        return fail("ignored signal");
    signal_install(SIGUSR2, (signal_handler_t)clobber_handler);
    if (register_roundtrip(self) != 1)
        return fail("sigreturn did not restore the registers");
    return 0;
}

static int test_sleeper_killed(void) {
    long pid = start_child("sleeper", 0, 0);
    if (pid < 0)
        return fail("start sleeper");
    if (!wait_for_state(pid, "sleeping"))
        return fail("sleeper never slept");
    if (kill((int)pid, SIGTERM) != 0)
        return fail("SIGTERM to sleeper");
    if (!wait_for_state(pid, "sleeping"))
        return fail("sleeper did not resume sleeping after its handler");
    if (kill((int)pid, SIGKILL) != 0)
        return fail("SIGKILL to sleeper");
    return expect_status(pid, KILLED_BY(SIGKILL), "sleeper not killed by SIGKILL");
}

static int test_fault_child(const char *mode, int signal, const char *what) {
    long pid = start_child(mode, 0, 0);
    if (pid < 0)
        return fail(what);
    return expect_status(pid, KILLED_BY(signal), what);
}

static int test_faults(void) {
    long pid;

    if (test_fault_child("segv", SIGSEGV, "null dereference not SIGSEGV") ||
        test_fault_child("ill", SIGILL, "ud2 not SIGILL") ||
        test_fault_child("fpe", SIGFPE, "division by zero not SIGFPE") ||
        test_fault_child("badreturn", SIGSEGV, "forged sigreturn context accepted") ||
        test_fault_child("badstack", SIGSEGV, "handler frame on bad stack"))
        return 1;
    pid = start_child("segvhandler", 0, 0);
    if (pid < 0)
        return fail("start segvhandler");
    return expect_status(pid, EXIT_SEGV_HANDLED, "SIGSEGV handler fault details");
}

static int test_interrupted_calls(void) {
    long pid = start_child("eintr-sleep", 0, 0);
    if (pid < 0)
        return fail("start eintr-sleep");
    if (!wait_for_state(pid, "sleeping") || kill((int)pid, SIGTERM) != 0)
        return fail("interrupt sleep");
    if (expect_status(pid, 0, "sleep not interrupted with EINTR"))
        return 1;

    pid = start_child("eintr-pipe", 0, 0);
    if (pid < 0)
        return fail("start eintr-pipe");
    if (!wait_for_state(pid, "pipe-read") || kill((int)pid, SIGTERM) != 0)
        return fail("interrupt pipe read");
    if (expect_status(pid, 0, "pipe read not interrupted with EINTR"))
        return 1;

    pid = start_child("spin", 0, 0);
    if (pid < 0)
        return fail("start spin");
    if (kill((int)pid, SIGUSR1) != 0)
        return fail("signal to running process");
    return expect_status(pid, 0, "handler not run in a preempted process");
}

static int test_wait_interrupted(void) {
    sigaction_t restore = {.handler = SIG_DFL, .restorer = 0, .mask = 0, .flags = 0};
    char self_text[16];
    int status = 0;
    long sleeper;
    long kicker;

    spin_flag = 0;
    signal_install(SIGUSR1, spin_handler);
    sleeper = start_child("sleeper", 0, 0);
    snprintf(self_text, sizeof(self_text), "%ld", sys_getpid());
    kicker = start_child("kicker", self_text, "USR1");
    if (sleeper < 0 || kicker < 0)
        return fail("start wait test children");
    if (sys_wait((int)sleeper, &status) != RC_EINTR || !spin_flag)
        return fail("wait not interrupted");
    if (kill((int)kicker, SIGKILL) != 0 || kill((int)sleeper, SIGKILL) != 0)
        return fail("kill wait test children");
    if (expect_status(kicker, KILLED_BY(SIGKILL), "kicker status") ||
        expect_status(sleeper, KILLED_BY(SIGKILL), "sleeper status"))
        return 1;
    return sigaction(SIGUSR1, &restore, 0) == 0 ? 0 : fail("restore SIGUSR1");
}

static int test_child_signal(void) {
    sigaction_t restore = {.handler = SIG_DFL, .restorer = 0, .mask = 0, .flags = 0};
    long pid;

    chld_count = 0;
    signal_install(SIGCHLD, chld_handler);
    pid = start_child("exit", 0, 0);
    if (pid < 0)
        return fail("start exit child");
    if (expect_status(pid, 7, "exit status"))
        return 1;
    if (chld_count < 1)
        return fail("no SIGCHLD for an exited child");
    return sigaction(SIGCHLD, &restore, 0) == 0 ? 0 : fail("restore SIGCHLD");
}

/* The kernel must refuse what would take it down: killing the bootstrap
 * process, unmapped user pointers, exit statuses posing as a signal death. */
static int test_kernel_guards(void) {
    long pid;

    if (kill(BOOTSTRAP_PID, 0) != 0)
        return fail("bootstrap process missing");
    if (kill(BOOTSTRAP_PID, SIGKILL) != RC_EPERM ||
        kill(BOOTSTRAP_PID, SIGTERM) != RC_EPERM)
        return fail("bootstrap process killable");
    if (sys_write(1, (const void *)UNMAPPED_ADDRESS, 1) != RC_EFAULT ||
        sys_read(0, (void *)UNMAPPED_ADDRESS, 1) != RC_EFAULT ||
        sys_stat("/", (void *)UNMAPPED_ADDRESS) != RC_EFAULT)
        return fail("unmapped syscall pointer accepted");
    pid = start_child("exitwide", 0, 0);
    if (pid < 0)
        return fail("start exitwide");
    return expect_status(pid, EXIT_WIDE_STATUS_SEEN, "exit status not masked");
}

int main(int argc, char **argv) {
    if (argc >= 2)
        return child_main(argc, argv);
    if (test_argument_errors() || test_handler_and_registers() ||
        test_sleeper_killed() || test_faults() || test_interrupted_calls() ||
        test_wait_interrupted() || test_child_signal() || test_kernel_guards())
        return 1;
    dprintf(1, "SIG PASS\n");
    return 0;
}
