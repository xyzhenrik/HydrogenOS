#include "mini_libc.h"
#include "syscall.h"
#include "wallclock.h"

/*
 * logtest - boot test for syslogd.
 *
 * init binds this service's stderr to syslogd (stderr=syslog). The test writes
 * a unique marker line there and waits until the marker shows up in
 * /var/log/system.log behind a timestamp and the service name. It also
 * expects the stdout of bootprobe (log=syslog) and the standard directory
 * layout.
 */

#define LOGTEST_LOG_PATH  "/var/log/system.log"
#define LOGTEST_OLD_PATH  "/var/log/system.log.1"
#define LOGTEST_ERR_ENOENT (-2)
#define LOGTEST_SERVICE   "logtest"
#define LOGTEST_PEER_TEXT " bootprobe: bootprobe: /shell ok at "

enum {
    LOGTEST_TIMEOUT_MS = 15000,
    LOGTEST_POLL_MS = 50,
    LOGTEST_LINE_CAP = 512,
    LOGTEST_CHUNK = 256,
    LOGTEST_MARKER_CAP = 96,
};

typedef struct scan_result {
    int marker_found;
    int peer_found;
} scan_result_t;

static void fail(const char *reason) {
    printf("SYSLOG FAIL: %s\n", reason);
}

static int same_bytes(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int contains(const char *haystack, size_t haystack_len, const char *needle) {
    size_t needle_len = strlen(needle);
    for (size_t i = 0; i + needle_len <= haystack_len; ++i) {
        if (same_bytes(haystack + i, needle, needle_len)) {
            return 1;
        }
    }
    return 0;
}

/*
 * A record reads "<stamp> <service>: <text>". The stamp is a date or, without a
 * readable RTC, a bracketed uptime, so it starts with a digit or '['.
 */
static int is_marker_record(const char *line, size_t len, const char *suffix) {
    size_t suffix_len = strlen(suffix);
    if (len < suffix_len + 2 || line[len - suffix_len - 1] != ' ') {
        return 0;
    }
    if (line[0] != '[' && (line[0] < '0' || line[0] > '9')) {
        return 0;
    }
    return same_bytes(line + len - suffix_len, suffix, suffix_len);
}

static void scan_line(const char *line, size_t len, const char *suffix,
                      scan_result_t *result) {
    if (is_marker_record(line, len, suffix)) {
        result->marker_found = 1;
    }
    if (contains(line, len, LOGTEST_PEER_TEXT)) {
        result->peer_found = 1;
    }
}

/* Returns 0 on success, 1 if the file does not exist, -1 on other errors. */
static int scan_file(const char *path, const char *suffix, scan_result_t *result) {
    char chunk[LOGTEST_CHUNK];
    char line[LOGTEST_LINE_CAP];
    size_t line_len = 0;
    long fd = open(path, OPEN_READ);
    if (fd < 0) {
        return fd == LOGTEST_ERR_ENOENT ? 1 : -1;
    }
    for (;;) {
        long got = read((int)fd, chunk, sizeof(chunk));
        if (got < 0) {
            close((int)fd);
            return -1;
        }
        if (got == 0) {
            break;
        }
        for (long i = 0; i < got; ++i) {
            if (chunk[i] == '\n') {
                scan_line(line, line_len, suffix, result);
                line_len = 0;
            } else if (line_len < sizeof(line)) {
                line[line_len++] = chunk[i];
            }
        }
    }
    close((int)fd);
    return 0;
}

/* A rotation between the writes of two services moves older lines to the .1 file. */
static int scan_log(const char *suffix, scan_result_t *result) {
    if (scan_file(LOGTEST_OLD_PATH, suffix, result) < 0) {
        return -1;
    }
    return scan_file(LOGTEST_LOG_PATH, suffix, result);
}

static int is_directory(const char *path) {
    stat_t st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int main(void) {
    static const char *const layout[] = {
        "/bin", "/etc", "/tmp", "/home", "/var", "/var/log",
    };
    char marker[LOGTEST_MARKER_CAP];
    char suffix[LOGTEST_MARKER_CAP + sizeof(LOGTEST_SERVICE) + 2];
    scan_result_t result;
    int64_t epoch = 0;
    unsigned long deadline;

    for (size_t i = 0; i < sizeof(layout) / sizeof(layout[0]); ++i) {
        if (!is_directory(layout[i])) {
            fail("standard directory missing");
            return 1;
        }
    }

    wallclock_now(0, &epoch, 0);
    snprintf(marker, sizeof(marker), "marker %lu-%lu-%lu", (unsigned long)epoch,
             (unsigned long)sys_getpid(), time_ms());
    snprintf(suffix, sizeof(suffix), "%s: %s", LOGTEST_SERVICE, marker);
    dprintf(2, "%s\n", marker);

    deadline = time_ms() + LOGTEST_TIMEOUT_MS;
    for (;;) {
        result.marker_found = 0;
        result.peer_found = 0;
        if (scan_log(suffix, &result) == 0 && result.marker_found && result.peer_found) {
            break;
        }
        if (time_ms() >= deadline) {
            fail(result.marker_found ? "bootprobe output not logged"
                                     : "marker not found in the system log");
            return 1;
        }
        sleep_ms(LOGTEST_POLL_MS);
    }

    printf("SYSLOG PASS\n");
    return 0;
}
