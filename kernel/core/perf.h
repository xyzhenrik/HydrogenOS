#pragma once

#include <stddef.h>
#include <stdint.h>

#include "isr.h"
#include "perf_abi.h"

struct process;

static inline uint64_t perf_rdtsc(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

void perf_init(void);

/*
 * Trap bracketing, called by isr_handler around every interrupt, exception and
 * syscall. perf_trap_enter() counts the vector and charges the user-mode time
 * since the last return to user mode; it returns whether the trap came from
 * user mode, which the caller hands back to perf_trap_exit() together with the
 * frame that is resumed.
 */
int perf_trap_enter(const regs_t *frame);
void perf_syscall_done(uint64_t number);
void perf_trap_exit(int from_user, const regs_t *next);

/*
 * Scheduler hooks. perf_sched_enter() runs at the start of process_schedule()
 * with the process that was running; blocked tells whether it just went to
 * sleep. perf_sched_dispatch() runs once a process is chosen to resume.
 */
void perf_sched_enter(struct process *prev, int blocked);
void perf_sched_dispatch(const struct process *prev, struct process *next);
void perf_idle_enter(void);
void perf_idle_leave(void);

void perf_count_pipe_read(uint32_t bytes);
void perf_count_pipe_write(uint32_t bytes);
void perf_count_net_rx(uint32_t bytes);
void perf_count_net_tx(uint32_t bytes);

void perf_snapshot(hydrogen_perf_snapshot_t *out);

/* SYS_PERF_READ: copies the snapshot, truncated to size bytes, to user space.
 * Returns the byte count or a negative errno. */
long perf_read_user(void *buffer, size_t size);
