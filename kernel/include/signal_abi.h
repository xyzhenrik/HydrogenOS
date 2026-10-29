#pragma once

#include <stdint.h>

/*
 * Signal ABI shared with userspace (mirrored in userspace/lib/signal.h).
 * Signal numbers 1..31 are valid; a mask has bit N set for signal N.
 */

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

#define SIGNAL_COUNT 32

/* "how" argument of SYS_SIGPROCMASK. */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIG_DFL 0ULL
#define SIG_IGN 1ULL

#define SIGNAL_BIT(sig) (1u << (sig))

/* SYS_EXIT keeps only the low byte of its argument, so a status that carries
 * SIGNAL_STATUS_SIGNALED can only come from the kernel. */
#define SIGNAL_EXIT_STATUS_MASK 0xFFu

/* Exit status of a process that was terminated by a signal; the low byte is
 * the signal number. */
#define SIGNAL_STATUS_SIGNALED 0x40000000
#define SIGNAL_STATUS(sig) (SIGNAL_STATUS_SIGNALED | (sig))

typedef struct hydrogen_sigaction {
    uint64_t handler;  /* SIG_DFL, SIG_IGN or the address of handler(int sig, const hydrogen_sigcontext_t *) */
    uint64_t restorer; /* trampoline that calls SYS_SIGRETURN; required with a handler */
    uint32_t mask;     /* signals blocked in addition to the delivered one while the handler runs */
    uint32_t flags;    /* reserved, must be 0 */
} hydrogen_sigaction_t;

/*
 * User state saved on the user stack when a handler is entered. The handler
 * returns into the restorer with the stack pointer at this structure, and
 * SYS_SIGRETURN restores the process from it after validating it.
 */
typedef struct hydrogen_sigcontext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rbx, rdx, rcx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
    uint64_t blocked;     /* signal mask to reinstate on return */
    uint64_t fault_addr;  /* CR2 of a page fault, otherwise 0 */
    uint64_t trap;        /* CPU exception vector of a fault, otherwise 0 */
    uint64_t error_code;  /* CPU error code of a fault, otherwise 0 */
} hydrogen_sigcontext_t;
