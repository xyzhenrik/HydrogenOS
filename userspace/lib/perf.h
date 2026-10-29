#pragma once

#include <stdint.h>

#include "syscall.h"

/*
 * SYS_PERF_READ snapshot; mirrors kernel/include/perf_abi.h. Counters are
 * monotonic since boot and cycles are TSC ticks, so rates and shares come from
 * the difference of two snapshots (snapshot.tsc is the time base).
 */
#define HYDROGEN_PERF_VERSION 1u
#define HYDROGEN_PERF_SYSCALL_SLOTS 96u
/* Slot 95 aggregates every syscall number >= 95 (including SYS_PUTCHAR). */
#define HYDROGEN_PERF_SYSCALL_OTHER (HYDROGEN_PERF_SYSCALL_SLOTS - 1u)
#define HYDROGEN_PERF_IRQ_VECTORS 256u
#define HYDROGEN_PERF_VECTOR_PAGE_FAULT 14u

typedef struct hydrogen_perf_counters {
    uint64_t user_cycles;
    uint64_t kernel_cycles;
    uint64_t idle_cycles;
    uint64_t context_switches;
    uint64_t wakeups;
    uint64_t idle_entries;
    uint64_t pipe_bytes_read;
    uint64_t pipe_bytes_written;
    uint64_t net_rx_packets;
    uint64_t net_tx_packets;
    uint64_t net_rx_bytes;
    uint64_t net_tx_bytes;
    uint64_t syscall_calls[HYDROGEN_PERF_SYSCALL_SLOTS];
    uint64_t syscall_cycles[HYDROGEN_PERF_SYSCALL_SLOTS];
    uint64_t irq_counts[HYDROGEN_PERF_IRQ_VECTORS];
} hydrogen_perf_counters_t;

typedef struct hydrogen_perf_snapshot {
    uint32_t version;
    uint32_t size;
    uint32_t syscall_slots;
    uint32_t irq_vectors;
    uint64_t tsc;
    uint64_t tsc_khz;
    uint64_t uptime_ms;
    hydrogen_perf_counters_t counters;
} hydrogen_perf_snapshot_t;

/* Returns the number of bytes stored (at most size) or -errno. */
static inline long sys_perf_read(void *buffer, unsigned long size) {
    return syscall2(SYS_PERF_READ, (long)buffer, (long)size);
}

/* Reads a whole snapshot; 0 on success, -1 if the kernel speaks another
 * version or returns a short snapshot. */
static inline int perf_snapshot_read(hydrogen_perf_snapshot_t *out) {
    long n = sys_perf_read(out, sizeof(*out));
    if (n != (long)sizeof(*out) || out->version != HYDROGEN_PERF_VERSION ||
        out->size != sizeof(*out))
        return -1;
    return 0;
}
