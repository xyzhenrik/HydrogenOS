#pragma once

#include <stdint.h>

/*
 * SYS_PERF_READ result. The layout is part of the syscall ABI: fields are only
 * ever appended at the end of hydrogen_perf_counters_t and every change bumps
 * HYDROGEN_PERF_VERSION. userspace/lib/perf.h mirrors this file.
 */
#define HYDROGEN_PERF_VERSION 1u
#define HYDROGEN_PERF_SYSCALL_SLOTS 96u
/* Slot 95 aggregates every syscall number >= 95 (including SYS_PUTCHAR). */
#define HYDROGEN_PERF_SYSCALL_OTHER (HYDROGEN_PERF_SYSCALL_SLOTS - 1u)
#define HYDROGEN_PERF_IRQ_VECTORS 256u
#define HYDROGEN_PERF_VECTOR_PAGE_FAULT 14u

/* All values are monotonic since boot; cycles are TSC ticks. */
typedef struct hydrogen_perf_counters {
    uint64_t user_cycles;          /* processes executing in user mode */
    uint64_t kernel_cycles;        /* syscalls, interrupts and scheduling */
    uint64_t idle_cycles;          /* CPU halted with nothing to run */
    uint64_t context_switches;     /* CPU handed to a different process */
    uint64_t wakeups;              /* blocked processes resumed */
    uint64_t idle_entries;         /* HLTs with nothing to run */
    uint64_t pipe_bytes_read;
    uint64_t pipe_bytes_written;
    uint64_t net_rx_packets;
    uint64_t net_tx_packets;
    uint64_t net_rx_bytes;
    uint64_t net_tx_bytes;
    uint64_t syscall_calls[HYDROGEN_PERF_SYSCALL_SLOTS];
    uint64_t syscall_cycles[HYDROGEN_PERF_SYSCALL_SLOTS]; /* entry to return of the handler */
    uint64_t irq_counts[HYDROGEN_PERF_IRQ_VECTORS];       /* includes exceptions and vector 128 (syscall) */
} hydrogen_perf_counters_t;

typedef struct hydrogen_perf_snapshot {
    uint32_t version;       /* HYDROGEN_PERF_VERSION */
    uint32_t size;          /* sizeof(hydrogen_perf_snapshot_t) of the kernel */
    uint32_t syscall_slots;
    uint32_t irq_vectors;
    uint64_t tsc;           /* TSC when the snapshot was taken */
    uint64_t tsc_khz;       /* TSC ticks per millisecond; 0 until measurable */
    uint64_t uptime_ms;
    hydrogen_perf_counters_t counters;
} hydrogen_perf_snapshot_t;
