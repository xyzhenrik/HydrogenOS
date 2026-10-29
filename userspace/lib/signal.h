#pragma once

#include <stdint.h>

/* Mirrors kernel/include/signal_abi.h. */

#define SIGINT   2
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGTERM  15
#define SIGCHLD  17

#define SIG_DFL 0ul
#define SIG_IGN 1ul

#define SIGNAL_BIT(sig) (1u << (sig))

/* "how" argument of sigprocmask. */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* Exit statuses of processes killed by a signal carry this flag. */
#define SIGNAL_STATUS_SIGNALED 0x40000000
#define WIFSIGNALED(status) (((status) & SIGNAL_STATUS_SIGNALED) != 0)
#define WTERMSIG(status) ((status) & 0xFF)

typedef struct sigaction {
    unsigned long handler;
    unsigned long restorer;
    unsigned int mask;
    unsigned int flags;
} sigaction_t;

typedef struct sigcontext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rbx, rdx, rcx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
    uint64_t blocked;
    uint64_t fault_addr;
    uint64_t trap;
    uint64_t error_code;
} sigcontext_t;

typedef void (*signal_handler_t)(int sig, const sigcontext_t *context);

/* Entered when a handler returns; calls sys_sigreturn. */
void sigreturn_trampoline(void);

long sigaction(int sig, const sigaction_t *act, sigaction_t *old);
long kill(int pid, int sig);

/* Changes the calling process's blocked-signal mask and stores the previous
 * one in *old (if not 0). A blocked signal stays pending until it is
 * unblocked; SIGKILL cannot be blocked. A query is SIG_BLOCK with set 0. */
long sigprocmask(int how, unsigned int set, unsigned int *old);

/* Installs a handler with the sigreturn trampoline as its return path. */
long signal_install(int sig, signal_handler_t handler);

/* Short name without the SIG prefix ("TERM"), or 0 for an unknown number. */
const char *signal_name(int sig);
/* Text a shell prints for a process killed by sig; "" if none is wanted. */
const char *signal_description(int sig);
/* Accepts "TERM", "SIGTERM" or a decimal number. Returns 0 for the number 0
 * (the existence probe of kill) and -1 if the signal is unknown. */
int signal_number(const char *name);
