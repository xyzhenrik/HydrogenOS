#include "mini_libc.h"
#include "perf.h"
#include "syscall.h"

/*
 * top [-d seconds] [-n iterations] [-b]
 *
 * Refreshing process list. CPU shares are the change of the kernel's cycle
 * accounting (/proc/<pid>/stat) between two refreshes over the cycles the
 * whole machine spent in the same window, so the rows add up to the "busy"
 * share of the header. Press q to quit; -b prints frames one after another
 * instead of clearing the screen, which is what to use when piping the output.
 */

enum {
    TOP_MAX_TASKS = 64,
    TOP_DEFAULT_INTERVAL_S = 2,
    TOP_MAX_INTERVAL_S = 60,
    TOP_MAX_ROWS = 24,
    TOP_TEXT_CAP = 512,
    TOP_FIELD_CAP = 32,
    TOP_PATH_CAP = 32,
    TOP_DT_DIR = 4,
    KEY_CTRL_C = 3,
};

typedef struct task {
    uint32_t pid;
    char name[TOP_FIELD_CAP];
    char state[TOP_FIELD_CAP];
    uint64_t user_cycles;
    uint64_t kernel_cycles;
    uint32_t busy_permille;
    uint32_t user_permille;
    uint32_t kernel_permille;
} task_t;

typedef struct sample {
    hydrogen_perf_snapshot_t system;
    task_t tasks[TOP_MAX_TASKS];
    unsigned count;
} sample_t;

static sample_t samples[2];

static int is_pid_name(const char *name) {
    if (name[0] < '1' || name[0] > '9')
        return 0;
    for (const char *c = name; *c; ++c) {
        if (*c < '0' || *c > '9')
            return 0;
    }
    return 1;
}

static long read_whole(const char *path, char *buf, unsigned long cap) {
    long fd = open(path, 0);
    if (fd < 0)
        return -1;
    unsigned long total = 0;
    while (total < cap - 1) {
        long n = read((int)fd, buf + total, cap - 1 - total);
        if (n < 0) {
            close((int)fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (unsigned long)n;
    }
    close((int)fd);
    buf[total] = '\0';
    return (long)total;
}

static int field(const char *text, const char *label, char *out,
                 unsigned long cap) {
    unsigned long label_len = strlen(label);
    const char *line = text;
    while (*line) {
        unsigned long i = 0;
        while (i < label_len && line[i] == label[i])
            ++i;
        if (i == label_len && line[i] == ':') {
            const char *v = line + i + 1;
            while (*v == ' ')
                ++v;
            unsigned long n = 0;
            while (v[n] && v[n] != '\n' && n < cap - 1) {
                out[n] = v[n];
                ++n;
            }
            out[n] = '\0';
            return 0;
        }
        while (*line && *line != '\n')
            ++line;
        if (*line == '\n')
            ++line;
    }
    return -1;
}

static int field_u64(const char *text, const char *label, uint64_t *out) {
    char value[TOP_FIELD_CAP];
    if (field(text, label, value, sizeof(value)) != 0)
        return -1;
    *out = strtoull(value, 0, 10);
    return 0;
}

static int read_task(const char *pid_name, task_t *task) {
    char path[TOP_PATH_CAP];
    char text[TOP_TEXT_CAP];
    uint64_t pid;

    snprintf(path, sizeof(path), "/proc/%s/stat", pid_name);
    /* The process may have exited since the directory was listed. */
    if (read_whole(path, text, sizeof(text)) <= 0 ||
        field(text, "Name", task->name, sizeof(task->name)) != 0 ||
        field(text, "State", task->state, sizeof(task->state)) != 0 ||
        field_u64(text, "Pid", &pid) != 0 ||
        field_u64(text, "UserCycles", &task->user_cycles) != 0 ||
        field_u64(text, "KernelCycles", &task->kernel_cycles) != 0)
        return -1;
    task->pid = (uint32_t)pid;
    return 0;
}

static int take_sample(sample_t *out) {
    dirent_t entry;

    if (perf_snapshot_read(&out->system) != 0)
        return -1;
    out->count = 0;
    for (int pos = 0; out->count < TOP_MAX_TASKS; ++pos) {
        int rc = readdir_pos("/proc", pos, &entry);
        if (rc < 0)
            return -1;
        if (rc == 0)
            break;
        if (entry.d_type == TOP_DT_DIR && is_pid_name(entry.d_name) &&
            read_task(entry.d_name, &out->tasks[out->count]) == 0)
            ++out->count;
    }
    return 0;
}

static uint32_t permille(uint64_t part, uint64_t whole) {
    if (whole == 0)
        return 0;
    uint64_t value = part * 1000u / whole;
    return value > 1000u ? 1000u : (uint32_t)value;
}

static const task_t *find_task(const sample_t *sample, uint32_t pid) {
    for (unsigned i = 0; i < sample->count; ++i) {
        if (sample->tasks[i].pid == pid)
            return &sample->tasks[i];
    }
    return 0;
}

/* Fills the per-task shares of cur from the window since prev. A task that
 * appeared inside the window was not running before it, so it starts at 0. */
static void compute_shares(sample_t *cur, const sample_t *prev,
                           uint64_t window_cycles) {
    for (unsigned i = 0; i < cur->count; ++i) {
        task_t *task = &cur->tasks[i];
        const task_t *old = find_task(prev, task->pid);
        uint64_t user = task->user_cycles - (old ? old->user_cycles : 0);
        uint64_t kernel = task->kernel_cycles - (old ? old->kernel_cycles : 0);
        task->user_permille = permille(user, window_cycles);
        task->kernel_permille = permille(kernel, window_cycles);
        task->busy_permille = permille(user + kernel, window_cycles);
    }
}

static void sort_by_busy(sample_t *sample) {
    for (unsigned i = 1; i < sample->count; ++i) {
        task_t key = sample->tasks[i];
        unsigned j = i;
        while (j > 0 &&
               (sample->tasks[j - 1].busy_permille < key.busy_permille ||
                (sample->tasks[j - 1].busy_permille == key.busy_permille &&
                 sample->tasks[j - 1].pid > key.pid))) {
            sample->tasks[j] = sample->tasks[j - 1];
            --j;
        }
        sample->tasks[j] = key;
    }
}

static void print_percent(uint32_t value_permille) {
    printf("%3u.%u", value_permille / 10u, value_permille % 10u);
}

static void print_frame(const sample_t *cur, const sample_t *prev) {
    const hydrogen_perf_counters_t *now = &cur->system.counters;
    const hydrogen_perf_counters_t *then = &prev->system.counters;
    uint64_t user = now->user_cycles - then->user_cycles;
    uint64_t kernel = now->kernel_cycles - then->kernel_cycles;
    uint64_t idle = now->idle_cycles - then->idle_cycles;
    uint64_t window_ms = cur->system.uptime_ms - prev->system.uptime_ms;
    uint64_t switches = now->context_switches - then->context_switches;
    uint64_t interrupts = 0;
    unsigned rows = cur->count < TOP_MAX_ROWS ? cur->count : TOP_MAX_ROWS;

    for (unsigned v = 0; v < HYDROGEN_PERF_IRQ_VECTORS; ++v)
        interrupts += now->irq_counts[v] - then->irq_counts[v];
    if (window_ms == 0)
        window_ms = 1;

    printf("top - up %lus, %u tasks, %lu ctx/s, %lu irq/s\n",
           (unsigned long)(cur->system.uptime_ms / 1000u), cur->count,
           (unsigned long)(switches * 1000u / window_ms),
           (unsigned long)(interrupts * 1000u / window_ms));
    printf("CPU: ");
    print_percent(permille(user, user + kernel + idle));
    printf("%% user, ");
    print_percent(permille(kernel, user + kernel + idle));
    printf("%% kernel, ");
    print_percent(permille(idle, user + kernel + idle));
    printf("%% idle\n\n");
    printf("%5s %-16s %-11s %6s %6s %6s %9s\n", "PID", "NAME", "STATE", "%CPU",
           "%USR", "%SYS", "TIME(s)");
    for (unsigned i = 0; i < rows; ++i) {
        const task_t *task = &cur->tasks[i];
        uint64_t ms = cur->system.tsc_khz
                          ? (task->user_cycles + task->kernel_cycles) /
                                cur->system.tsc_khz
                          : 0;
        printf("%5u %-16s %-11s ", task->pid, task->name, task->state);
        print_percent(task->busy_permille);
        printf(" ");
        print_percent(task->user_permille);
        printf(" ");
        print_percent(task->kernel_permille);
        printf(" %6lu.%02lu\n", (unsigned long)(ms / 1000u),
               (unsigned long)(ms % 1000u / 10u));
    }
    if (cur->count > rows)
        printf("... %u more\n", cur->count - rows);
    fflush(stdout);
}

/* Waits for the next refresh; returns 1 once the user asked to quit. Without a
 * usable keyboard on stdin it simply sleeps. */
static int wait_for_refresh(unsigned interval_ms, int *keyboard) {
    if (*keyboard) {
        long ready = poll_wait_fd(0, POLL_READ, (long)interval_ms);
        if (ready > 0) {
            char key;
            if (read(0, &key, 1) == 1)
                return key == 'q' || key == 'Q' || key == KEY_CTRL_C;
            *keyboard = 0;
        } else if (ready == 0) {
            return 0;
        } else {
            *keyboard = 0;
        }
    }
    if (!*keyboard)
        sleep_ms(interval_ms);
    return 0;
}

static int usage(void) {
    dprintf(2, "usage: top [-d seconds] [-n iterations] [-b]\n");
    return 2;
}

static int parse_number(const char *text, unsigned long min, unsigned long max,
                        unsigned long *out) {
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value < min || value > max)
        return -1;
    *out = value;
    return 0;
}

int main(int argc, char **argv) {
    unsigned long interval_s = TOP_DEFAULT_INTERVAL_S;
    unsigned long iterations = 0;
    int batch = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-b") == 0) {
            batch = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            if (parse_number(argv[++i], 1, TOP_MAX_INTERVAL_S, &interval_s) != 0)
                return usage();
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            if (parse_number(argv[++i], 1, 1000000, &iterations) != 0)
                return usage();
        } else {
            return usage();
        }
    }

    int keyboard = !batch;
    unsigned current = 0;
    if (take_sample(&samples[current]) != 0) {
        dprintf(2, "top: cannot read performance counters\n");
        return 1;
    }
    for (unsigned long frame = 1; iterations == 0 || frame <= iterations;
         ++frame) {
        if (wait_for_refresh((unsigned)(interval_s * 1000u), &keyboard))
            break;
        unsigned previous = current;
        current ^= 1u;
        if (take_sample(&samples[current]) != 0) {
            dprintf(2, "top: cannot read performance counters\n");
            return 1;
        }
        sample_t *cur = &samples[current];
        const sample_t *prev = &samples[previous];
        const hydrogen_perf_counters_t *a = &prev->system.counters;
        const hydrogen_perf_counters_t *b = &cur->system.counters;
        compute_shares(cur, prev,
                       (b->user_cycles - a->user_cycles) +
                           (b->kernel_cycles - a->kernel_cycles) +
                           (b->idle_cycles - a->idle_cycles));
        sort_by_busy(cur);
        if (!batch)
            sys_fb_clear();
        else if (frame > 1)
            printf("\n");
        print_frame(cur, prev);
    }
    return 0;
}
