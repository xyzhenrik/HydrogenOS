#include "mini_libc.h"
#include "syscall.h"

#define BOOT_PATH "/proc/boot"
#define BOOT_BUF_CAP 128
#define PROGRESS_MAX 1000ul
#define SERVICES_BASE 300ul
#define RC_EINVAL (-22l)
#define RC_ENOENT (-2l)
#define RC_EROFS (-30l)

static void fail(const char *reason) {
    dprintf(1, "SPLASH FAIL: %s\n", reason);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Parses the Splash and Progress fields of /proc/boot. */
static int read_boot(char *splash, unsigned long splash_cap,
                     unsigned long *progress) {
    char text[BOOT_BUF_CAP];
    long fd = open(BOOT_PATH, 0);
    if (fd < 0)
        return -1;
    long len = read((int)fd, text, sizeof(text) - 1);
    close((int)fd);
    if (len <= 0)
        return -1;
    text[len] = '\0';

    const char *line = text;
    int seen = 0;
    while (*line) {
        const char *colon = line;
        while (*colon && *colon != ':' && *colon != '\n')
            ++colon;
        if (*colon != ':')
            return -1;
        const char *value = colon + 1;
        while (*value == ' ')
            ++value;
        const char *end = value;
        while (*end && *end != '\n')
            ++end;
        if (colon - line == 6 && strncmp(line, "Splash", 6) == 0) {
            unsigned long n = (unsigned long)(end - value);
            if (n == 0 || n >= splash_cap)
                return -1;
            for (unsigned long i = 0; i < n; ++i)
                splash[i] = value[i];
            splash[n] = '\0';
            seen |= 1;
        } else if (colon - line == 8 && strncmp(line, "Progress", 8) == 0) {
            unsigned long v = 0;
            if (value == end)
                return -1;
            for (const char *c = value; c < end; ++c) {
                if (*c < '0' || *c > '9')
                    return -1;
                v = v * 10ul + (unsigned long)(*c - '0');
            }
            *progress = v;
            seen |= 2;
        }
        line = *end == '\n' ? end + 1 : end;
    }
    return seen == 3 ? 0 : -1;
}

static long report(const char *text) {
    return sys_writef(BOOT_PATH, text, strlen(text));
}

static int check_state(void) {
    char splash[16];
    unsigned long progress = 0;
    if (read_boot(splash, sizeof(splash), &progress) != 0) {
        fail("read /proc/boot");
        return -1;
    }
    if (!str_eq(splash, "active") && !str_eq(splash, "finished") &&
        !str_eq(splash, "disabled")) {
        fail("unknown splash state");
        return -1;
    }
    if (progress > PROGRESS_MAX) {
        fail("progress out of range");
        return -1;
    }
    /* Init has been reporting service progress since it started. */
    if (str_eq(splash, "active") && progress < SERVICES_BASE) {
        fail("init progress missing");
        return -1;
    }
    return 0;
}

static int check_rejections(void) {
    static const char *const malformed[] = {
        "bogus\n",
        "services\n",
        "services 1\n",
        "services a b\n",
        "services 1 0\n",
        "services 5 3\n",
        "services 1 2 3\n",
        "services -1 2\n",
        "services 99999999999 100000000000\n",
        "end now\n",
        "",
        "services 1 2 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",
    };
    for (unsigned long i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        if (report(malformed[i]) != RC_EINVAL) {
            dprintf(1, "SPLASH FAIL: accepted report %lu\n", i);
            return -1;
        }
    }
    if (sys_writef("/proc/uptime", "services 1 2\n", 13) != RC_EROFS) {
        fail("read-only node accepted write");
        return -1;
    }
    if (sys_writef("/proc/nosuchnode", "end\n", 4) != RC_ENOENT) {
        fail("unknown node accepted write");
        return -1;
    }
    return 0;
}

static int check_monotonic(void) {
    char splash[16];
    unsigned long before = 0;
    unsigned long after = 0;
    if (read_boot(splash, sizeof(splash), &before) != 0) {
        fail("read before report");
        return -1;
    }
    /* A report that lags behind the current progress must not move it back. */
    if (report("services 0 1000\n") != 0) {
        fail("valid report rejected");
        return -1;
    }
    if (read_boot(splash, sizeof(splash), &after) != 0 || after < before) {
        fail("progress moved backwards");
        return -1;
    }
    return 0;
}

int main(void) {
    if (check_state() != 0 || check_rejections() != 0 || check_monotonic() != 0)
        return 1;
    write(1, "SPLASH PASS\n", 12);
    return 0;
}
