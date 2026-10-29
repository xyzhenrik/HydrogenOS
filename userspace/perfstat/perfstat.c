#include "mini_libc.h"
#include "perf.h"
#include "syscall.h"

/*
 * perfstat [command [args...]]
 *
 * Without a command, reports the kernel performance counters accumulated
 * since boot. With one, runs it with the caller's stdio and reports only what
 * the machine did while it ran (every process counts, not just the command).
 */

enum {
    PERFSTAT_TOP_SYSCALLS = 10,
    PERFSTAT_PATH_CAP = 128,
    PERFSTAT_MAX_ARGS = 32,
    VECTOR_PAGE_FAULT = HYDROGEN_PERF_VECTOR_PAGE_FAULT,
    VECTOR_TIMER = 32,
    VECTOR_KEYBOARD = 33,
    VECTOR_MOUSE = 44,
    VECTOR_SYSCALL = 128,
};

static const char *const syscall_names[HYDROGEN_PERF_SYSCALL_SLOTS] = {
    [SYS_EXIT] = "exit",
    [SYS_WRITE] = "write",
    [SYS_READ] = "read",
    [SYS_OPEN] = "open",
    [SYS_CLOSE] = "close",
    [SYS_LSEEK] = "lseek",
    [SYS_SPAWN] = "spawn",
    [SYS_WAIT] = "wait",
    [SYS_GETPID] = "getpid",
    [SYS_SLEEP] = "sleep",
    [SYS_YIELD] = "yield",
    [SYS_TIME_MS] = "time_ms",
    [SYS_TTY_SET_COLOR] = "tty_set_color",
    [SYS_PIPE] = "pipe",
    [SYS_HANDLE_SET_INHERIT] = "handle_set_inherit",
    [SYS_SPAWN_WITH_STDIO] = "spawn_with_stdio",
    [SYS_DUP] = "dup",
    [SYS_DUP2] = "dup2",
    [SYS_DUP3] = "dup3",
    [SYS_POLL] = "poll",
    [SYS_POLL_WAIT] = "poll_wait",
    [SYS_POLL_MANY] = "poll_many",
    [SYS_SET_PRIORITY] = "set_priority",
    [SYS_STAT] = "stat",
    [SYS_FSTAT] = "fstat",
    [SYS_READDIR] = "readdir",
    [SYS_GETCWD] = "getcwd",
    [SYS_CHDIR] = "chdir",
    [SYS_MKDIR] = "mkdir",
    [SYS_UNLINK] = "unlink",
    [SYS_RENAME] = "rename",
    [SYS_WRITEF] = "writef",
    [SYS_FB_INFO] = "fb_info",
    [SYS_FB_BLIT] = "fb_blit",
    [SYS_MOUSE_STATE] = "mouse_state",
    [SYS_KBD_READ] = "kbd_read",
    [SYS_CONSOLE_CURSOR] = "console_cursor",
    [SYS_FB_BLIT_STRIDED] = "fb_blit_strided",
    [SYS_FB_CLEAR] = "fb_clear",
    [SYS_SYSINFO] = "sysinfo",
    [SYS_SET_LAYOUT] = "set_layout",
    [SYS_MMAP] = "mmap",
    [SYS_MUNMAP] = "munmap",
    [SYS_RMDIR] = "rmdir",
    [SYS_SHUTDOWN] = "shutdown",
    [SYS_REBOOT] = "reboot",
    [SYS_NET_INFO] = "net_info",
    [SYS_NET_PING] = "net_ping",
    [SYS_SPAWN_ARGS] = "spawn_args",
    [SYS_TRY_WAIT] = "try_wait",
    [SYS_RTC_TIME] = "rtc_time",
    [SYS_MPROTECT] = "mprotect",
    [SYS_KILL] = "kill",
    [SYS_SIGACTION] = "sigaction",
    [SYS_SIGRETURN] = "sigreturn",
    [SYS_SOCKET] = "socket",
    [SYS_BIND] = "bind",
    [SYS_SENDTO] = "sendto",
    [SYS_RECVFROM] = "recvfrom",
    [SYS_LINK] = "link",
    [SYS_SYMLINK] = "symlink",
    [SYS_READLINK] = "readlink",
    [68] = "sigprocmask",
    [69] = "connect",
    [70] = "listen",
    [71] = "accept",
    [72] = "shutdown_sock",
    [73] = "execve",
    [74] = "getuid",
    [75] = "setuid",
    [76] = "getgid",
    [77] = "setgid",
    [78] = "chmod",
    [79] = "chown",
    [80] = "truncate",
    [SYS_PERF_READ] = "perf_read",
    [HYDROGEN_PERF_SYSCALL_OTHER] = "other (>= 95)",
};

static hydrogen_perf_snapshot_t before;
static hydrogen_perf_snapshot_t after;

static const char *vector_name(unsigned vector) {
    switch (vector) {
    case VECTOR_PAGE_FAULT:
        return "page-fault";
    case VECTOR_TIMER:
        return "timer";
    case VECTOR_KEYBOARD:
        return "keyboard";
    case VECTOR_MOUSE:
        return "mouse";
    case VECTOR_SYSCALL:
        return "syscall";
    default:
        return vector < 32 ? "exception" : "";
    }
}

static unsigned long percent_tenths(uint64_t part, uint64_t whole) {
    return whole ? (unsigned long)(part * 1000u / whole) : 0;
}

static void print_share(const char *label, uint64_t cycles, uint64_t total) {
    unsigned long tenths = percent_tenths(cycles, total);
    printf("  %-8s %16lu cycles  %3lu.%lu%%\n", label, (unsigned long)cycles,
           tenths / 10, tenths % 10);
}

/* Fills order with the used syscall slots, most calls first. */
static unsigned rank_syscalls(const hydrogen_perf_counters_t *delta,
                              unsigned *order) {
    unsigned used = 0;
    for (unsigned slot = 0; slot < HYDROGEN_PERF_SYSCALL_SLOTS; ++slot) {
        if (delta->syscall_calls[slot] == 0)
            continue;
        unsigned at = used++;
        while (at > 0 && delta->syscall_calls[order[at - 1]] <
                             delta->syscall_calls[slot]) {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = slot;
    }
    return used;
}

/* Counters only grow, so the window is a plain element-wise difference. */
static void subtract(hydrogen_perf_counters_t *delta,
                     const hydrogen_perf_counters_t *later,
                     const hydrogen_perf_counters_t *earlier) {
    uint64_t *out = (uint64_t *)delta;
    const uint64_t *hi = (const uint64_t *)later;
    const uint64_t *lo = (const uint64_t *)earlier;
    for (unsigned i = 0; i < sizeof(*delta) / sizeof(uint64_t); ++i)
        out[i] = hi[i] - lo[i];
}

static void print_report(const hydrogen_perf_counters_t *c, uint64_t window_ms,
                         uint64_t tsc_khz) {
    uint64_t total = c->user_cycles + c->kernel_cycles + c->idle_cycles;
    uint64_t interrupts = 0;
    uint64_t syscalls = 0;
    unsigned order[HYDROGEN_PERF_SYSCALL_SLOTS];

    for (unsigned v = 0; v < HYDROGEN_PERF_IRQ_VECTORS; ++v)
        interrupts += c->irq_counts[v];
    for (unsigned s = 0; s < HYDROGEN_PERF_SYSCALL_SLOTS; ++s)
        syscalls += c->syscall_calls[s];

    printf("window            %lu.%03lu s   tsc %lu kHz\n",
           (unsigned long)(window_ms / 1000u),
           (unsigned long)(window_ms % 1000u), (unsigned long)tsc_khz);
    printf("cpu\n");
    print_share("user", c->user_cycles, total);
    print_share("kernel", c->kernel_cycles, total);
    print_share("idle", c->idle_cycles, total);
    printf("scheduler         %lu context switches, %lu wakeups, %lu idle halts\n",
           (unsigned long)c->context_switches, (unsigned long)c->wakeups,
           (unsigned long)c->idle_entries);
    printf("pipes             %lu bytes read, %lu bytes written\n",
           (unsigned long)c->pipe_bytes_read,
           (unsigned long)c->pipe_bytes_written);
    printf("network           rx %lu packets / %lu bytes, tx %lu packets / %lu bytes\n",
           (unsigned long)c->net_rx_packets, (unsigned long)c->net_rx_bytes,
           (unsigned long)c->net_tx_packets, (unsigned long)c->net_tx_bytes);
    printf("page faults       %lu\n",
           (unsigned long)c->irq_counts[VECTOR_PAGE_FAULT]);

    printf("\ninterrupts        %lu total\n", (unsigned long)interrupts);
    for (unsigned v = 0; v < HYDROGEN_PERF_IRQ_VECTORS; ++v) {
        if (c->irq_counts[v])
            printf("  vec %-3u %12lu  %s\n", v, (unsigned long)c->irq_counts[v],
                   vector_name(v));
    }

    printf("\nsyscalls          %lu total\n", (unsigned long)syscalls);
    printf("  %-20s %10s %14s %12s\n", "NAME", "CALLS", "CYCLES", "CYC/CALL");
    unsigned used = rank_syscalls(c, order);
    for (unsigned i = 0; i < used && i < PERFSTAT_TOP_SYSCALLS; ++i) {
        unsigned slot = order[i];
        char fallback[16];
        const char *name = syscall_names[slot];
        if (!name) {
            snprintf(fallback, sizeof(fallback), "syscall %u", slot);
            name = fallback;
        }
        printf("  %-20s %10lu %14lu %12lu\n", name,
               (unsigned long)c->syscall_calls[slot],
               (unsigned long)c->syscall_cycles[slot],
               (unsigned long)(c->syscall_cycles[slot] / c->syscall_calls[slot]));
    }
}

static long run_command(int argc, char **argv) {
    const char *args[PERFSTAT_MAX_ARGS + 1];
    char path[PERFSTAT_PATH_CAP];
    int status = -1;

    if (argc > PERFSTAT_MAX_ARGS) {
        dprintf(2, "perfstat: too many arguments\n");
        return -1;
    }
    if (argv[0][0] == '/')
        snprintf(path, sizeof(path), "%s", argv[0]);
    else
        snprintf(path, sizeof(path), "/%s", argv[0]);
    for (int i = 0; i < argc; ++i)
        args[i] = argv[i];
    args[argc] = 0;

    if (perf_snapshot_read(&before) != 0) {
        dprintf(2, "perfstat: cannot read performance counters\n");
        return -1;
    }
    long pid = spawn_args(path, args, 0, SPAWN_STDIO_INHERIT,
                          SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT);
    if (pid <= 0) {
        dprintf(2, "perfstat: cannot run %s\n", path);
        return -1;
    }
    if (wait_pid((int)pid, &status) != pid) {
        dprintf(2, "perfstat: wait failed\n");
        return -1;
    }
    return status;
}

int main(int argc, char **argv) {
    hydrogen_perf_counters_t delta;
    uint64_t window_ms;
    long status = 0;

    if (argc > 1)
        status = run_command(argc - 1, argv + 1);
    if (argc > 1 && status < 0)
        return 1;
    if (perf_snapshot_read(&after) != 0) {
        dprintf(2, "perfstat: cannot read performance counters\n");
        return 1;
    }

    if (argc > 1) {
        subtract(&delta, &after.counters, &before.counters);
        window_ms = after.uptime_ms - before.uptime_ms;
        printf("command exit status %ld\n", status);
    } else {
        delta = after.counters;
        window_ms = after.uptime_ms;
    }
    print_report(&delta, window_ms, after.tsc_khz);
    return 0;
}
