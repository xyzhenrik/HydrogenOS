#include "mini_libc.h"
#include "syscall.h"

#define PS_STATUS_CAP 1024
#define PS_PATH_CAP 64
#define PS_FIELD_CAP 64
#define PS_DT_DIR 4

/* Reads the whole file with as few calls as possible: procfs renders on
   every read, so a single large read yields a consistent snapshot. */
static long read_file(const char *path, char *buf, unsigned long cap) {
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

static int is_pid_name(const char *name) {
    if (name[0] < '1' || name[0] > '9')
        return 0;
    for (const char *c = name; *c; ++c) {
        if (*c < '0' || *c > '9')
            return 0;
    }
    return 1;
}

/* Extracts the value of a "Label:   value" line; missing fields become "?". */
static void status_field(const char *status, const char *label, char *out,
                         unsigned long cap) {
    unsigned long label_len = strlen(label);
    const char *line = status;
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
            return;
        }
        while (*line && *line != '\n')
            ++line;
        if (*line == '\n')
            ++line;
    }
    out[0] = '?';
    out[1] = '\0';
}

int main(void) {
    char status[PS_STATUS_CAP];
    char path[PS_PATH_CAP];
    char pid[PS_FIELD_CAP];
    char ppid[PS_FIELD_CAP];
    char state[PS_FIELD_CAP];
    char prio[PS_FIELD_CAP];
    char handles[PS_FIELD_CAP];
    char pages[PS_FIELD_CAP];
    char name[PS_FIELD_CAP];
    dirent_t entry;

    printf("%5s %5s %-10s %-7s %4s %6s %s\n", "PID", "PPID", "STATE", "PRIO",
           "HND", "PAGES", "NAME");
    int pos = 0;
    for (;;) {
        int rc = readdir_pos("/proc", pos, &entry);
        if (rc < 0) {
            fprintf(2, "ps: cannot read /proc\n");
            return 1;
        }
        if (rc == 0)
            break;
        ++pos;
        if (entry.d_type != PS_DT_DIR || !is_pid_name(entry.d_name))
            continue;
        snprintf(path, sizeof(path), "/proc/%s/status", entry.d_name);
        /* The process may have exited since the directory was listed. */
        if (read_file(path, status, sizeof(status)) <= 0)
            continue;
        status_field(status, "Pid", pid, sizeof(pid));
        status_field(status, "PPid", ppid, sizeof(ppid));
        status_field(status, "State", state, sizeof(state));
        status_field(status, "Priority", prio, sizeof(prio));
        status_field(status, "Handles", handles, sizeof(handles));
        status_field(status, "Pages", pages, sizeof(pages));
        status_field(status, "Name", name, sizeof(name));
        printf("%5s %5s %-10s %-7s %4s %6s %s\n", pid, ppid, state, prio,
               handles, pages, name);
    }
    return 0;
}
