#pragma once

#include <stddef.h>
#include <stdint.h>

#include "isr.h"
#include "signal_abi.h"

struct process;

typedef struct signal_state {
    uint32_t pending;
    uint32_t blocked;
    hydrogen_sigaction_t actions[SIGNAL_COUNT];
    /* Details of the last synchronous fault, handed to its handler. */
    int fault_signal;
    uint64_t fault_addr;
    uint64_t fault_trap;
    uint64_t fault_error;
    /* The signal that terminated the process; 0 while it lives. */
    int fatal_signal;
} signal_state_t;

enum {
    SIGNAL_DELIVER_NONE = 0,    /* nothing to do, or p is not in user mode */
    SIGNAL_DELIVER_HANDLER = 1, /* p's frame now enters a handler */
    SIGNAL_DELIVER_KILLED = 2,  /* p was terminated and must not run again */
};

void signal_state_init(signal_state_t *state);

/* Prepares state for a new program image: caught signals fall back to their
 * default action, ignored ones stay ignored. The pending and blocked masks are
 * kept. */
void signal_reset_for_exec(signal_state_t *state);

/* True if p has a signal that would act on it now (a handler or termination). */
int signal_deliverable(const struct process *p);

/* Queues sig for target and aborts the blocking syscall it sits in. Returns 0,
 * -EINVAL for an unknown signal or -ESRCH for a dead target. */
int signal_send(struct process *target, int sig);

/* Syscall bodies; user pointers are probed against p's page tables. Signals
 * that would terminate the bootstrap process are refused with -EPERM. */
int64_t signal_kill(uint32_t pid, int sig);
int64_t signal_sigaction(struct process *p, int sig, uint64_t act_user,
                         uint64_t old_user);

/* Restores p from the sigcontext at the user stack pointer of r. On success r
 * holds the restored state; an invalid context kills p with SIGSEGV and
 * returns -EFAULT. */
int signal_return(struct process *p, regs_t *r);

/* Maps a CPU exception vector to the signal it raises in user mode; 0 for
 * vectors that are never the process's fault. */
int signal_from_exception(uint64_t vector);

/* Raises a synchronous fault in p. The signal cannot be blocked or ignored:
 * it falls back to its default action, which terminates p. */
void signal_raise_fault(struct process *p, int sig, uint64_t trap,
                        uint64_t error, uint64_t addr);

/* Runs the next deliverable signal for p, which must be the active process
 * about to return to user mode. Handlers are entered by rewriting p->frame. */
int signal_deliver(struct process *p);

/* Console foreground tracking for Ctrl+C: the process that owns console input
 * is the last reader that is not a descendant of the previous owner. */
void signal_console_note_reader(const struct process *reader);

/* Sends SIGINT to the job the console owner is waiting for. Returns 1 if a job
 * was interrupted, 0 if the interrupt is ordinary input. */
int signal_console_interrupt(void);
