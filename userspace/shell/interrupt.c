#include "interrupt.h"

#include "mini_libc.h"
#include "signal.h"

static volatile int pending = 0;
static volatile int targets[SHELL_MAX_STAGES];
static volatile int target_count = 0;

static void forward(void) {
    for (int i = 0; i < target_count; ++i) {
        int pid = targets[i];
        if (pid > 0)
            kill(pid, SIGINT);
    }
}

static void on_sigint(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    pending = 1;
    forward();
}

void interrupt_install(void) { signal_install(SIGINT, on_sigint); }

int interrupt_pending(void) { return pending; }

void interrupt_clear(void) { pending = 0; }

void interrupt_set_targets(const int *pids, int count) {
    target_count = 0;
    for (int i = 0; i < count && i < SHELL_MAX_STAGES; ++i)
        targets[i] = pids[i];
    target_count = count < SHELL_MAX_STAGES ? count : SHELL_MAX_STAGES;
    if (pending)
        forward();
}

void interrupt_target_reaped(int pid) {
    for (int i = 0; i < target_count; ++i) {
        if (targets[i] == pid)
            targets[i] = 0;
    }
}
