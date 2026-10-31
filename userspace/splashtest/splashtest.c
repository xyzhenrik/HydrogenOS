#include "mini_libc.h"
#include "syscall.h"

#define BOOT_PATH "/proc/boot"
#define CMDLINE_PATH "/proc/cmdline"
#define BOOT_BUF_CAP 128
#define CMDLINE_BUF_CAP 300
#define PROGRESS_MAX 1000ul
#define SERVICES_BASE 300ul
#define RC_ENOENT (-2l)
#define RC_EINVAL (-22l)
#define RC_EROFS (-30l)

static void fail(const char *reason) {
    dprintf(2, "SPLASH FAIL: %s\n", reason);
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

/* Returns 1 if /proc/cmdline contains "verbose" as a whole word, 0 if not, -1
   if it cannot be read. */
static int cmdline_is_verbose(void) {
    static const char flag[] = "verbose";
    char text[CMDLINE_BUF_CAP];
    long fd = open(CMDLINE_PATH, 0);
    if (fd < 0)
        return -1;
    long len = read((int)fd, text, sizeof(text) - 1);
    close((int)fd);
    if (len < 0)
        return -1;
    text[len] = '\0';

    const char *cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\n')
            ++cursor;
        const char *start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\n')
            ++cursor;
        if ((unsigned long)(cursor - start) == sizeof(flag) - 1 &&
            strncmp(start, flag, sizeof(flag) - 1) == 0)
            return 1;
    }
    return 0;
}

static long report(const char *text) {
    return sys_writef(BOOT_PATH, text, strlen(text));
}

/* The boot runs without a kernel command line unless `verbose` is passed, and
   the desktop, which ends the splash, starts only after this test. So the
   splash must be up, with the bar where init's service reports put it, unless
   `verbose` turned it off. */
static int check_state(int *active) {
    char splash[16];
    unsigned long progress = 0;
    int verbose = cmdline_is_verbose();
    if (verbose < 0) {
        fail("read /proc/cmdline");
        return -1;
    }
    if (read_boot(splash, sizeof(splash), &progress) != 0) {
        fail("read /proc/boot");
        return -1;
    }
    if (verbose) {
        if (!str_eq(splash, "disabled") || progress != 0) {
            fail("verbose did not disable the splash");
            return -1;
        }
        *active = 0;
        return 0;
    }
    if (!str_eq(splash, "active")) {
        fail("splash is not active during boot");
        return -1;
    }
    if (progress <= SERVICES_BASE || progress >= PROGRESS_MAX) {
        fail("init's service progress did not reach the bar");
        return -1;
    }
    *active = 1;
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
            dprintf(2, "SPLASH FAIL: accepted report %lu\n", i);
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

/* Only init may report. A well-formed report from this process must be
   refused without effect, and a partial blit must neither draw nor end the
   splash. */
static int check_unprivileged(int active) {
    static const char *const reports[] = { "services 1 2\n", "end\n" };
    static const unsigned int pixel = 0;
    char splash[16];
    unsigned long before = 0;
    unsigned long after = 0;

    if (read_boot(splash, sizeof(splash), &before) != 0) {
        fail("read before reports");
        return -1;
    }
    for (unsigned long i = 0; i < sizeof(reports) / sizeof(reports[0]); ++i) {
        if (report(reports[i]) != RC_EROFS) {
            dprintf(2, "SPLASH FAIL: unprivileged report %lu accepted\n", i);
            return -1;
        }
    }
    if (active && sys_fb_blit(0, 0, 1, 1, &pixel) != 0) {
        fail("partial blit failed during splash");
        return -1;
    }
    if (read_boot(splash, sizeof(splash), &after) != 0) {
        fail("read after reports");
        return -1;
    }
    if (after != before || str_eq(splash, "active") != active) {
        fail("unprivileged writes changed the splash");
        return -1;
    }
    return 0;
}

int main(void) {
    int active = 0;
    if (check_state(&active) != 0 || check_rejections() != 0 ||
        check_unprivileged(active) != 0)
        return 1;
    write(1, "SPLASH PASS\n", 12);
    return 0;
}
