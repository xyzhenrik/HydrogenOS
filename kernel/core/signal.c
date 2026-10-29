#include "signal.h"

#include "errno.h"
#include "gdt.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "syscall.h"

#define USER_MIN_ADDRESS 0x1000ULL
#define USER_RED_ZONE 128ULL

#define RFLAGS_ALWAYS_SET 0x2ULL
#define RFLAGS_INTERRUPT_ENABLE 0x200ULL
/* CF, PF, AF, ZF, SF, DF, OF: the flags a process may legitimately change. */
#define RFLAGS_USER_MASK 0xCD5ULL

#define CONSOLE_CHAIN_DEPTH_MAX 16

#define SIGNAL_FAULT_MASK (SIGNAL_BIT(SIGILL) | SIGNAL_BIT(SIGTRAP) | \
                           SIGNAL_BIT(SIGBUS) | SIGNAL_BIT(SIGFPE) | \
                           SIGNAL_BIT(SIGSEGV))

#define SIGNAL_VALID_MASK (SIGNAL_BIT(SIGINT) | SIGNAL_BIT(SIGILL) | \
                           SIGNAL_BIT(SIGTRAP) | SIGNAL_BIT(SIGABRT) | \
                           SIGNAL_BIT(SIGBUS) | SIGNAL_BIT(SIGFPE) | \
                           SIGNAL_BIT(SIGKILL) | SIGNAL_BIT(SIGUSR1) | \
                           SIGNAL_BIT(SIGSEGV) | SIGNAL_BIT(SIGUSR2) | \
                           SIGNAL_BIT(SIGTERM) | SIGNAL_BIT(SIGCHLD))

/* The frame a handler is entered with: the return address (into the restorer)
 * directly below the saved context. */
typedef struct signal_frame {
    uint64_t return_address;
    hydrogen_sigcontext_t context;
} signal_frame_t;

static uint32_t console_owner_pid = 0;

static int signal_valid(int sig) {
    return sig > 0 && sig < SIGNAL_COUNT &&
           (SIGNAL_VALID_MASK & SIGNAL_BIT(sig)) != 0;
}

static int signal_default_ignored(int sig) {
    return sig == SIGCHLD;
}

static int signal_ignored(const signal_state_t *state, int sig) {
    uint64_t handler = state->actions[sig].handler;
    return handler == SIG_IGN ||
           (handler == SIG_DFL && signal_default_ignored(sig));
}

static const char *signal_name(int sig) {
    switch (sig) {
    case SIGILL: return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGSEGV: return "SIGSEGV";
    default: return "signal";
    }
}

static void signal_log_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}

static void signal_log_decimal(uint32_t value) {
    char text[10];
    int length = 0;
    do {
        text[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (length > 0)
        serial_putc(text[--length]);
}

static void signal_log_fatal_fault(const process_t *p, int sig) {
    serial_write("signal: pid ");
    signal_log_decimal(p->pid);
    serial_write(" (");
    serial_write(p->name);
    serial_write(") killed by ");
    serial_write(signal_name(sig));
    serial_write(" at rip=");
    signal_log_hex(p->frame.rip);
    if (p->signals.fault_signal == sig) {
        serial_write(" addr=");
        signal_log_hex(p->signals.fault_addr);
    }
    serial_write("\n");
}

static int signal_user_range_accessible(const process_t *p, uint64_t address,
                                        uint64_t length, int write) {
    return address >= USER_MIN_ADDRESS &&
           paging_user_range_accessible(p->pml4, (uintptr_t)address,
                                        (size_t)length, write);
}

static int signal_user_address_ok(uint64_t address) {
    return address >= USER_MIN_ADDRESS && address < PAGING_USER_TOP;
}

static int signal_next_ready(const signal_state_t *state) {
    uint32_t ready = state->pending &
                     (~state->blocked | SIGNAL_BIT(SIGKILL));
    if (ready == 0)
        return 0;
    if (ready & SIGNAL_BIT(SIGKILL))
        return SIGKILL;
    /* A fault belongs to the instruction that is about to run again: it goes
     * first, so its handler (or the fatal log line) sees that context rather
     * than another handler's. */
    if (ready & SIGNAL_FAULT_MASK)
        return __builtin_ctz(ready & SIGNAL_FAULT_MASK);
    return __builtin_ctz(ready);
}

/* Resets sig to its default action and queues it, whatever the process asked
 * for: used when a handler cannot run. */
static void signal_force(process_t *p, int sig) {
    signal_state_t *state = &p->signals;
    state->actions[sig].handler = SIG_DFL;
    state->actions[sig].restorer = 0;
    state->actions[sig].mask = 0;
    state->actions[sig].flags = 0;
    state->blocked &= ~SIGNAL_BIT(sig);
    state->pending |= SIGNAL_BIT(sig);
}

static void signal_terminate(process_t *p, int sig) {
    if (SIGNAL_FAULT_MASK & SIGNAL_BIT(sig))
        signal_log_fatal_fault(p, sig);
    p->signals.fatal_signal = sig;
    process_mark_exited(p, (int)SIGNAL_STATUS(sig));
}

/* Whether sig would end the process right away (no handler, not ignored). */
static int signal_fatal_now(const signal_state_t *state, int sig) {
    return sig == SIGKILL ||
           (state->actions[sig].handler == SIG_DFL &&
            !signal_default_ignored(sig));
}

void signal_state_init(signal_state_t *state) {
    state->pending = 0;
    state->blocked = 0;
    for (int sig = 0; sig < SIGNAL_COUNT; ++sig) {
        state->actions[sig].handler = SIG_DFL;
        state->actions[sig].restorer = 0;
        state->actions[sig].mask = 0;
        state->actions[sig].flags = 0;
    }
    state->fault_signal = 0;
    state->fault_addr = 0;
    state->fault_trap = 0;
    state->fault_error = 0;
    state->fatal_signal = 0;
}

int signal_deliverable(const process_t *p) {
    return p != 0 && signal_next_ready(&p->signals) != 0;
}

int signal_send(process_t *target, int sig) {
    if (!signal_valid(sig))
        return -EINVAL;
    if (!target || !target->alive)
        return -ESRCH;
    if (sig != SIGKILL && signal_ignored(&target->signals, sig))
        return 0;
    target->signals.pending |= SIGNAL_BIT(sig);
    if (signal_deliverable(target))
        process_interrupt_wait(target);
    return 0;
}

int64_t signal_kill(uint32_t pid, int sig) {
    if (sig != 0 && !signal_valid(sig))
        return -EINVAL;
    process_t *target = process_find_by_pid(pid);
    if (!target)
        return -ESRCH;
    if (sig == 0)
        return 0;
    /* Nothing else could take over the console if the bootstrap process died. */
    if (syscall_is_bootstrap(target) && signal_fatal_now(&target->signals, sig))
        return -EPERM;
    return signal_send(target, sig);
}

static int signal_action_valid(const hydrogen_sigaction_t *act) {
    if (act->flags != 0)
        return 0;
    if (act->handler == SIG_DFL || act->handler == SIG_IGN)
        return 1;
    return signal_user_address_ok(act->handler) &&
           signal_user_address_ok(act->restorer);
}

int64_t signal_sigaction(process_t *p, int sig, uint64_t act_user,
                         uint64_t old_user) {
    hydrogen_sigaction_t act;
    hydrogen_sigaction_t old;
    signal_state_t *state = &p->signals;

    if (!signal_valid(sig) || sig == SIGKILL)
        return -EINVAL;
    if (act_user) {
        if (!signal_user_range_accessible(p, act_user, sizeof(act), 0))
            return -EFAULT;
        act = *(const hydrogen_sigaction_t *)(uintptr_t)act_user;
        if (!signal_action_valid(&act))
            return -EINVAL;
    }
    if (old_user &&
        !signal_user_range_accessible(p, old_user, sizeof(old), 1))
        return -EFAULT;

    old = state->actions[sig];
    if (act_user) {
        act.mask &= SIGNAL_VALID_MASK & ~SIGNAL_BIT(SIGKILL);
        state->actions[sig] = act;
        if (signal_ignored(state, sig))
            state->pending &= ~SIGNAL_BIT(sig);
    }
    if (old_user)
        *(hydrogen_sigaction_t *)(uintptr_t)old_user = old;
    return 0;
}

int signal_from_exception(uint64_t vector) {
    switch (vector) {
    case 0:  /* divide error */
    case 16: /* x87 floating point */
    case 19: /* SIMD floating point */
        return SIGFPE;
    case 1:  /* debug */
    case 3:  /* breakpoint */
        return SIGTRAP;
    case 6:  /* invalid opcode */
    case 7:  /* device not available */
        return SIGILL;
    case 17: /* alignment check */
        return SIGBUS;
    case 4:  /* overflow */
    case 5:  /* bound range */
    case 10: /* invalid TSS */
    case 11: /* segment not present */
    case 12: /* stack fault */
    case 13: /* general protection */
    case 14: /* page fault */
        return SIGSEGV;
    default:
        return 0;
    }
}

void signal_raise_fault(process_t *p, int sig, uint64_t trap, uint64_t error,
                        uint64_t addr) {
    signal_state_t *state = &p->signals;
    if ((state->blocked & SIGNAL_BIT(sig)) || signal_ignored(state, sig))
        signal_force(p, sig);
    state->pending |= SIGNAL_BIT(sig);
    state->fault_signal = sig;
    state->fault_addr = addr;
    state->fault_trap = trap;
    state->fault_error = error;
}

static void signal_save_context(const process_t *p, int sig,
                                hydrogen_sigcontext_t *ctx) {
    const regs_t *f = &p->frame;
    ctx->r15 = f->r15; ctx->r14 = f->r14; ctx->r13 = f->r13; ctx->r12 = f->r12;
    ctx->r11 = f->r11; ctx->r10 = f->r10; ctx->r9 = f->r9; ctx->r8 = f->r8;
    ctx->rsi = f->rsi; ctx->rdi = f->rdi; ctx->rbp = f->rbp; ctx->rbx = f->rbx;
    ctx->rdx = f->rdx; ctx->rcx = f->rcx; ctx->rax = f->rax;
    ctx->rip = f->rip; ctx->cs = f->cs; ctx->rflags = f->rflags;
    ctx->rsp = f->rsp; ctx->ss = f->ss;
    ctx->blocked = p->signals.blocked;
    if (p->signals.fault_signal == sig) {
        ctx->fault_addr = p->signals.fault_addr;
        ctx->trap = p->signals.fault_trap;
        ctx->error_code = p->signals.fault_error;
    } else {
        ctx->fault_addr = 0;
        ctx->trap = 0;
        ctx->error_code = 0;
    }
}

/* Builds the signal frame below the user stack pointer (and its red zone) and
 * points p's frame at the handler. Fails if the stack cannot take the frame. */
static int signal_enter_handler(process_t *p, int sig) {
    signal_state_t *state = &p->signals;
    const hydrogen_sigaction_t *action = &state->actions[sig];
    const uint64_t size = sizeof(signal_frame_t);
    uint64_t sp = p->frame.rsp;

    if (sp < USER_RED_ZONE + size + USER_MIN_ADDRESS || sp >= PAGING_USER_TOP)
        return -1;
    /* A function entry sees rsp = 8 (mod 16): the return address was pushed
     * onto a 16-byte aligned stack. */
    uint64_t base = ((sp - USER_RED_ZONE - size - 8) & ~15ULL) + 8;
    if (!signal_user_range_accessible(p, base, size, 1))
        return -1;

    signal_frame_t *frame = (signal_frame_t *)(uintptr_t)base;
    signal_save_context(p, sig, &frame->context);
    frame->return_address = action->restorer;

    state->blocked |= SIGNAL_BIT(sig) | action->mask;
    state->blocked &= ~SIGNAL_BIT(SIGKILL);
    if (state->fault_signal == sig)
        state->fault_signal = 0;

    regs_t *f = &p->frame;
    f->rip = action->handler;
    f->rsp = base;
    f->rdi = (uint64_t)sig;
    f->rsi = base + offsetof(signal_frame_t, context);
    f->rax = 0;
    f->rflags = RFLAGS_INTERRUPT_ENABLE | RFLAGS_ALWAYS_SET;
    return 0;
}

int signal_deliver(process_t *p) {
    signal_state_t *state = &p->signals;

    if (p->frame.cs != GDT_USER_CODE_SELECTOR)
        return SIGNAL_DELIVER_NONE;
    for (;;) {
        int sig = signal_next_ready(state);
        if (sig == 0)
            return SIGNAL_DELIVER_NONE;
        state->pending &= ~SIGNAL_BIT(sig);

        uint64_t handler = state->actions[sig].handler;
        if (signal_fatal_now(state, sig)) {
            signal_terminate(p, sig);
            return SIGNAL_DELIVER_KILLED;
        }
        if (handler == SIG_DFL || handler == SIG_IGN)
            continue;
        if (signal_enter_handler(p, sig) != 0) {
            /* No stack for the frame: the process cannot be resumed sanely. */
            state->fault_signal = SIGSEGV;
            state->fault_addr = p->frame.rsp;
            signal_terminate(p, SIGSEGV);
            return SIGNAL_DELIVER_KILLED;
        }
        return SIGNAL_DELIVER_HANDLER;
    }
}

static int signal_context_valid(const hydrogen_sigcontext_t *ctx) {
    return ctx->cs == GDT_USER_CODE_SELECTOR &&
           ctx->ss == GDT_USER_DATA_SELECTOR &&
           signal_user_address_ok(ctx->rip) && ctx->rsp < PAGING_USER_TOP;
}

int signal_return(process_t *p, regs_t *r) {
    hydrogen_sigcontext_t ctx;
    uint64_t sp = r->rsp;

    if (!signal_user_range_accessible(p, sp, sizeof(ctx), 0)) {
        signal_force(p, SIGSEGV);
        return -EFAULT;
    }
    ctx = *(const hydrogen_sigcontext_t *)(uintptr_t)sp;
    if (!signal_context_valid(&ctx)) {
        signal_force(p, SIGSEGV);
        return -EFAULT;
    }

    r->r15 = ctx.r15; r->r14 = ctx.r14; r->r13 = ctx.r13; r->r12 = ctx.r12;
    r->r11 = ctx.r11; r->r10 = ctx.r10; r->r9 = ctx.r9; r->r8 = ctx.r8;
    r->rsi = ctx.rsi; r->rdi = ctx.rdi; r->rbp = ctx.rbp; r->rbx = ctx.rbx;
    r->rdx = ctx.rdx; r->rcx = ctx.rcx; r->rax = ctx.rax;
    r->rip = ctx.rip;
    r->cs = GDT_USER_CODE_SELECTOR;
    r->ss = GDT_USER_DATA_SELECTOR;
    r->rsp = ctx.rsp;
    r->rflags = (ctx.rflags & RFLAGS_USER_MASK) | RFLAGS_INTERRUPT_ENABLE |
                RFLAGS_ALWAYS_SET;
    p->signals.blocked = (uint32_t)ctx.blocked & SIGNAL_VALID_MASK &
                         ~SIGNAL_BIT(SIGKILL);
    return 0;
}

static int signal_is_descendant(const process_t *p, const process_t *ancestor) {
    uint32_t pid = p->parent_pid;
    for (int depth = 0; pid != 0 && depth < CONSOLE_CHAIN_DEPTH_MAX; ++depth) {
        if (pid == ancestor->pid)
            return 1;
        const process_t *parent = process_find_by_pid(pid);
        if (!parent)
            return 0;
        pid = parent->parent_pid;
    }
    return 0;
}

void signal_console_note_reader(const process_t *reader) {
    const process_t *owner = process_find_by_pid(console_owner_pid);
    if (owner && (owner == reader || signal_is_descendant(reader, owner)))
        return;
    console_owner_pid = reader->pid;
}

int signal_console_interrupt(void) {
    const process_t *owner = process_find_by_pid(console_owner_pid);
    int interrupted = 0;

    if (!owner)
        return 0;
    uint32_t job_pid = owner->state == PROCESS_BLOCKED_WAIT
                           ? owner->waiting_for_pid : 0;
    for (int depth = 0; job_pid != 0 && depth < CONSOLE_CHAIN_DEPTH_MAX;
         ++depth) {
        process_t *job = process_find_by_pid(job_pid);
        if (!job)
            break;
        /* Sending aborts the job's own wait, so read it first. */
        job_pid = job->state == PROCESS_BLOCKED_WAIT ? job->waiting_for_pid : 0;
        if (signal_send(job, SIGINT) == 0)
            interrupted = 1;
    }
    return interrupted;
}
