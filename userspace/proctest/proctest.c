#include "mini_libc.h"
#include "syscall.h"

#define PROCTEST_BUF_CAP 1024
#define PROCTEST_PATH_CAP 64
#define PROCTEST_FIELD_CAP 64
#define PROCTEST_DT_DIR 4
#define PROCTEST_DT_REG 8

static void fail(const char *reason) {
    fprintf(1, "PROC FAIL: %s\n", reason);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static int bytes_eq(const char *a, const char *b, long len) {
    for (long i = 0; i < len; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

/* Reads the file with read calls of at most `chunk` bytes. */
static long read_chunked(const char *path, char *buf, unsigned long cap,
                         unsigned long chunk) {
    long fd = open(path, 0);
    if (fd < 0)
        return -1;
    unsigned long total = 0;
    while (total < cap - 1) {
        unsigned long want = cap - 1 - total;
        if (want > chunk)
            want = chunk;
        long n = read((int)fd, buf + total, want);
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

static long read_whole(const char *path, char *buf, unsigned long cap) {
    return read_chunked(path, buf, cap, cap);
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
            while (v[n] && v[n] != '\n') {
                if (n >= cap - 1)
                    return -1;
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

/* Parses a leading decimal number; trailing text (e.g. " kB") is allowed. */
static int field_u64(const char *text, const char *label, uint64_t *out) {
    char value[PROCTEST_FIELD_CAP];
    if (field(text, label, value, sizeof(value)) != 0)
        return -1;
    if (value[0] < '0' || value[0] > '9')
        return -1;
    uint64_t v = 0;
    for (const char *c = value; *c >= '0' && *c <= '9'; ++c)
        v = v * 10u + (uint64_t)(*c - '0');
    *out = v;
    return 0;
}

static int check_status(long pid) {
    char path[PROCTEST_PATH_CAP];
    char whole[PROCTEST_BUF_CAP];
    char chunked[PROCTEST_BUF_CAP];
    char value[PROCTEST_FIELD_CAP];
    uint64_t num = 0;

    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    long len = read_whole(path, whole, sizeof(whole));
    if (len <= 0) {
        fail("read own status");
        return -1;
    }
    if (field(whole, "Name", value, sizeof(value)) != 0 ||
        !str_eq(value, "proctest")) {
        fail("status name");
        return -1;
    }
    if (field_u64(whole, "Pid", &num) != 0 || num != (uint64_t)pid) {
        fail("status pid");
        return -1;
    }
    if (field_u64(whole, "PPid", &num) != 0 || num == 0) {
        fail("status ppid");
        return -1;
    }
    if (field(whole, "State", value, sizeof(value)) != 0 ||
        !str_eq(value, "running")) {
        fail("status state");
        return -1;
    }
    if (field(whole, "Priority", value, sizeof(value)) != 0) {
        fail("status priority");
        return -1;
    }
    /* The status file's own descriptor is open while it is rendered. */
    if (field_u64(whole, "Handles", &num) != 0 || num == 0) {
        fail("status handles");
        return -1;
    }
    if (field_u64(whole, "Pages", &num) != 0 || num == 0) {
        fail("status pages");
        return -1;
    }

    long chunked_len = read_chunked(path, chunked, sizeof(chunked), 7);
    if (chunked_len != len || !bytes_eq(whole, chunked, len)) {
        fail("status offset reads");
        return -1;
    }

    long fd = open(path, 0);
    if (fd < 0) {
        fail("reopen status");
        return -1;
    }
    long end = lseek((int)fd, 0, SEEK_END);
    long at = lseek((int)fd, 5, SEEK_SET);
    long n = read((int)fd, chunked, sizeof(chunked));
    long past = lseek((int)fd, len + 10, SEEK_SET);
    long eof = read((int)fd, chunked + PROCTEST_BUF_CAP / 2, 16);
    close((int)fd);
    if (end != len || at != 5 || n != len - 5 ||
        !bytes_eq(whole + 5, chunked, len - 5) || past != len + 10 ||
        eof != 0) {
        fail("status seek");
        return -1;
    }
    return 0;
}

static int check_meminfo(void) {
    char text[PROCTEST_BUF_CAP];
    uint64_t total = 0;
    uint64_t free = 0;
    uint64_t heap_total = 0;
    uint64_t heap_used = 0;
    uint64_t heap_free = 0;

    if (read_whole("/proc/meminfo", text, sizeof(text)) <= 0) {
        fail("read meminfo");
        return -1;
    }
    if (field_u64(text, "MemTotal", &total) != 0 ||
        field_u64(text, "MemFree", &free) != 0 || total == 0 ||
        free > total) {
        fail("meminfo pmm");
        return -1;
    }
    if (field_u64(text, "HeapTotal", &heap_total) != 0 ||
        field_u64(text, "HeapUsed", &heap_used) != 0 ||
        field_u64(text, "HeapFree", &heap_free) != 0 || heap_used == 0 ||
        heap_used + heap_free != heap_total) {
        fail("meminfo heap");
        return -1;
    }
    return 0;
}

static int check_misc_files(void) {
    char text[PROCTEST_BUF_CAP];
    long len = read_whole("/proc/version", text, sizeof(text));
    if (len <= 0 || !starts_with(text, "HydrogenOS ") || text[len - 1] != '\n') {
        fail("version");
        return -1;
    }
    len = read_whole("/proc/uptime", text, sizeof(text));
    long i = 0;
    while (i < len && text[i] >= '0' && text[i] <= '9')
        ++i;
    if (i == 0 || i + 4 != len || text[i] != '.' || text[i + 1] < '0' ||
        text[i + 1] > '9' || text[i + 2] < '0' || text[i + 2] > '9' ||
        text[i + 3] != '\n') {
        fail("uptime");
        return -1;
    }
    return 0;
}

static int check_listing(long pid) {
    char own[PROCTEST_FIELD_CAP];
    dirent_t entry;
    int seen_meminfo = 0;
    int seen_uptime = 0;
    int seen_version = 0;
    int seen_self = 0;
    stat_t st;

    if (stat("/proc", &st) != 0 || !S_ISDIR(st.st_mode)) {
        fail("stat /proc");
        return -1;
    }
    snprintf(own, sizeof(own), "%d", (int)pid);
    for (int pos = 0;; ++pos) {
        int rc = readdir_pos("/proc", pos, &entry);
        if (rc < 0) {
            fail("readdir /proc");
            return -1;
        }
        if (rc == 0)
            break;
        if (entry.d_type == PROCTEST_DT_REG) {
            if (str_eq(entry.d_name, "meminfo"))
                seen_meminfo = 1;
            else if (str_eq(entry.d_name, "uptime"))
                seen_uptime = 1;
            else if (str_eq(entry.d_name, "version"))
                seen_version = 1;
        } else if (entry.d_type == PROCTEST_DT_DIR &&
                   str_eq(entry.d_name, own)) {
            seen_self = 1;
        }
    }
    if (!seen_meminfo || !seen_uptime || !seen_version || !seen_self) {
        fail("listing incomplete");
        return -1;
    }

    char path[PROCTEST_PATH_CAP];
    snprintf(path, sizeof(path), "/proc/%s", own);
    if (readdir_pos(path, 0, &entry) != 1 || !str_eq(entry.d_name, "status") ||
        readdir_pos(path, 1, &entry) != 0) {
        fail("pid dir listing");
        return -1;
    }
    return 0;
}

static int check_rejections(long pid) {
    char path[PROCTEST_PATH_CAP];
    static const char *const bad[] = {
        "/proc",
        "/proc/0/status",
        "/proc/4294967296/status",
        "/proc/999999/status",
        "/proc/nosuchfile",
    };
    for (unsigned long i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        long fd = open(bad[i], 0);
        if (fd >= 0) {
            close((int)fd);
            fail(bad[i]);
            return -1;
        }
    }
    snprintf(path, sizeof(path), "/proc/0%d/status", (int)pid);
    long fd = open(path, 0);
    if (fd >= 0) {
        close((int)fd);
        fail("non-canonical pid accepted");
        return -1;
    }
    snprintf(path, sizeof(path), "/proc/%d/nosuchfile", (int)pid);
    fd = open(path, 0);
    if (fd >= 0) {
        close((int)fd);
        fail("unknown pid entry accepted");
        return -1;
    }
    if (sys_writef("/proc/version", "x", 1) == 0) {
        fail("procfs accepted write");
        return -1;
    }
    return 0;
}

int main(void) {
    long pid = sys_getpid();
    if (pid <= 0) {
        fail("getpid");
        return 1;
    }
    if (check_status(pid) != 0 || check_meminfo() != 0 ||
        check_misc_files() != 0 || check_listing(pid) != 0 ||
        check_rejections(pid) != 0)
        return 1;
    write(1, "PROC PASS\n", 10);
    return 0;
}
