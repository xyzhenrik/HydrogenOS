#include "perf.h"

#include "core/process.h"
#include "core/time.h"
#include "core/usercopy.h"
#include "errno.h"

/*
 * Kernel code runs with interrupts disabled, apart from the sti/hlt window of
 * the idle loop, which touches no counter. Every CPU only writes its own
 * perf_cpu_t, so the counters are plain increments without atomics or locks.
 * perf_local() is the one place that has to learn about a second CPU.
 */
enum {
    PERF_MAX_CPUS = 1,
    /* Below this the TSC frequency estimate is dominated by tick granularity. */
    PERF_TSC_KHZ_MIN_MS = 100,
};

typedef struct perf_cpu {
    hydrogen_perf_counters_t counters;
    /* Everything since this stamp is not yet attributed to anyone. */
    uint64_t cursor_tsc;
    uint64_t trap_tsc;
    uint8_t in_user; /* cursor marks the start of a user-mode stretch */
} perf_cpu_t;

static perf_cpu_t perf_cpus[PERF_MAX_CPUS];
static uint64_t boot_tsc;
static uint64_t boot_ms;

static perf_cpu_t *perf_local(void) {
    return &perf_cpus[0];
}

void perf_init(void) {
    perf_cpu_t *cpu = perf_local();
    boot_tsc = perf_rdtsc();
    boot_ms = time_ms();
    cpu->cursor_tsc = boot_tsc;
    cpu->in_user = 0;
}

static void perf_charge_kernel(perf_cpu_t *cpu, process_t *who, uint64_t now) {
    uint64_t delta = now - cpu->cursor_tsc;
    who->kernel_cycles += delta;
    cpu->counters.kernel_cycles += delta;
    cpu->cursor_tsc = now;
}

int perf_trap_enter(const regs_t *frame) {
    perf_cpu_t *cpu = perf_local();
    uint64_t now = perf_rdtsc();
    int from_user = (frame->cs & 3) == 3;

    if (frame->int_no < HYDROGEN_PERF_IRQ_VECTORS)
        cpu->counters.irq_counts[frame->int_no]++;
    if (from_user) {
        uint64_t delta = now - cpu->cursor_tsc;
        if (cpu->in_user) {
            process_current()->user_cycles += delta;
            cpu->counters.user_cycles += delta;
        } else {
            /* User mode was entered without returning from a trap (the first
               process): everything since the cursor was kernel work that no
               process asked for. */
            cpu->counters.kernel_cycles += delta;
        }
        cpu->in_user = 0;
        cpu->cursor_tsc = now;
    }
    cpu->trap_tsc = now;
    return from_user;
}

void perf_syscall_done(uint64_t number) {
    perf_cpu_t *cpu = perf_local();
    uint64_t slot = number < HYDROGEN_PERF_SYSCALL_OTHER
                        ? number
                        : HYDROGEN_PERF_SYSCALL_OTHER;
    cpu->counters.syscall_calls[slot]++;
    cpu->counters.syscall_cycles[slot] += perf_rdtsc() - cpu->trap_tsc;
}

void perf_trap_exit(int from_user, const regs_t *next) {
    perf_cpu_t *cpu = perf_local();
    uint64_t now = perf_rdtsc();

    if (from_user)
        perf_charge_kernel(cpu, process_current(), now);
    cpu->in_user = (next->cs & 3) == 3;
    if (cpu->in_user)
        cpu->cursor_tsc = now;
}

void perf_sched_enter(process_t *prev, int blocked) {
    perf_cpu_t *cpu = perf_local();

    perf_charge_kernel(cpu, prev, perf_rdtsc());
    if (blocked)
        prev->perf_was_blocked = 1;
}

void perf_sched_dispatch(const process_t *prev, process_t *next) {
    perf_cpu_t *cpu = perf_local();

    if (next->perf_was_blocked) {
        next->perf_was_blocked = 0;
        cpu->counters.wakeups++;
    }
    if (next != prev)
        cpu->counters.context_switches++;
}

void perf_idle_enter(void) {
    perf_cpu_t *cpu = perf_local();
    uint64_t now = perf_rdtsc();

    cpu->counters.kernel_cycles += now - cpu->cursor_tsc;
    cpu->cursor_tsc = now;
    cpu->counters.idle_entries++;
}

void perf_idle_leave(void) {
    perf_cpu_t *cpu = perf_local();
    uint64_t now = perf_rdtsc();

    cpu->counters.idle_cycles += now - cpu->cursor_tsc;
    cpu->cursor_tsc = now;
}

void perf_count_pipe_read(uint32_t bytes) {
    perf_local()->counters.pipe_bytes_read += bytes;
}

void perf_count_pipe_write(uint32_t bytes) {
    perf_local()->counters.pipe_bytes_written += bytes;
}

void perf_count_net_rx(uint32_t bytes) {
    hydrogen_perf_counters_t *counters = &perf_local()->counters;
    counters->net_rx_packets++;
    counters->net_rx_bytes += bytes;
}

void perf_count_net_tx(uint32_t bytes) {
    hydrogen_perf_counters_t *counters = &perf_local()->counters;
    counters->net_tx_packets++;
    counters->net_tx_bytes += bytes;
}

void perf_snapshot(hydrogen_perf_snapshot_t *out) {
    uint64_t now = perf_rdtsc();
    uint64_t uptime = time_ms();
    uint64_t elapsed_ms = uptime - boot_ms;

    out->version = HYDROGEN_PERF_VERSION;
    out->size = sizeof(*out);
    out->syscall_slots = HYDROGEN_PERF_SYSCALL_SLOTS;
    out->irq_vectors = HYDROGEN_PERF_IRQ_VECTORS;
    out->tsc = now;
    out->tsc_khz = elapsed_ms >= PERF_TSC_KHZ_MIN_MS
                       ? (now - boot_tsc) / elapsed_ms
                       : 0;
    out->uptime_ms = uptime;
    out->counters = perf_local()->counters;
}

long perf_read_user(void *buffer, size_t size) {
    hydrogen_perf_snapshot_t snapshot;

    /* version and size must fit so a caller can always tell what it got */
    if (size < 2 * sizeof(uint32_t))
        return -EINVAL;
    perf_snapshot(&snapshot);
    if (size > sizeof(snapshot))
        size = sizeof(snapshot);
    int rc = copy_to_user(buffer, &snapshot, size);
    return rc == 0 ? (long)size : rc;
}
