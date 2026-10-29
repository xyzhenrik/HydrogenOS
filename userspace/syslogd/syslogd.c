#include "mini_libc.h"
#include "syscall.h"
#include "wallclock.h"

/*
 * syslogd - system logger.
 *
 * init starts it as the log server (service option "log=server") and passes
 * one pipe read end per logged service as an inherited handle. Every argument
 * has the form "<service>:<fd>". Each line a service writes to such a pipe is
 * stamped with the wall clock and the service name, appended to
 * /var/log/system.log and forwarded to the console (serial). When the log
 * file would grow past SYSLOG_FILE_LIMIT it is rotated to system.log.1.
 * The daemon exits once every source has reached end-of-file.
 */

#define SYSLOG_ERR_ENOENT (-2)
#define SYSLOG_ERR_EEXIST (-17)

#define SYSLOG_SELF_NAME "syslogd"
#define SYSLOG_DIR_VAR   "/var"
#define SYSLOG_DIR_LOG   "/var/log"
#define SYSLOG_PATH      "/var/log/system.log"
#define SYSLOG_PATH_OLD  "/var/log/system.log.1"
#define SYSLOG_EXIT_TEXT "all log sources closed, exiting"

enum {
    SYSLOG_MAX_SOURCES = 16,
    SYSLOG_NAME_CAP = 32,
    SYSLOG_LINE_CAP = 256,
    SYSLOG_READ_CAP = 512,
    SYSLOG_STAMP_CAP = 32,
    SYSLOG_OUT_CAP = 4096,
    SYSLOG_FILE_LIMIT = 64 * 1024,
};

typedef struct log_source {
    int fd;
    char name[SYSLOG_NAME_CAP];
    char line[SYSLOG_LINE_CAP];
    size_t line_len;
} log_source_t;

typedef struct log_file {
    int fd;
    size_t size;
    int failure_reported;
} log_file_t;

static log_source_t sources[SYSLOG_MAX_SOURCES];
static size_t source_count;
static log_file_t log_file = { .fd = -1 };
static char out_buf[SYSLOG_OUT_CAP];
static size_t out_len;

static void report(const char *what) {
    fprintf(2, "syslogd: %s\n", what);
}

static void format_stamp(char *buf, size_t cap) {
    CivilTime now;
    unsigned long uptime_ms;
    if (wallclock_now(&now, 0, 0) == 0 && time_format(buf, cap, "%F %T", &now) > 0) {
        return;
    }
    uptime_ms = time_ms();
    snprintf(buf, cap, "[%5lu.%03lu]", uptime_ms / 1000, uptime_ms % 1000);
}

static int ensure_log_directory(void) {
    long rc = sys_mkdir(SYSLOG_DIR_VAR);
    if (rc != 0 && rc != SYSLOG_ERR_EEXIST) {
        return -1;
    }
    rc = sys_mkdir(SYSLOG_DIR_LOG);
    return (rc == 0 || rc == SYSLOG_ERR_EEXIST) ? 0 : -1;
}

static void log_file_close(void) {
    if (log_file.fd >= 0) {
        close(log_file.fd);
        log_file.fd = -1;
    }
}

static int log_file_open(void) {
    stat_t st;
    long fd;
    if (ensure_log_directory() != 0) {
        return -1;
    }
    fd = open(SYSLOG_PATH, OPEN_WRITE | OPEN_CREATE | OPEN_APPEND);
    if (fd < 0) {
        return -1;
    }
    if (fstat((int)fd, &st) != 0) {
        close((int)fd);
        return -1;
    }
    log_file.fd = (int)fd;
    log_file.size = st.st_size;
    return 0;
}

/* Keeps appending to the current file if the rotation cannot be completed. */
static void log_file_rotate(void) {
    long rc;
    log_file_close();
    rc = sys_unlink(SYSLOG_PATH_OLD);
    if (rc == 0 || rc == SYSLOG_ERR_ENOENT) {
        rc = sys_rename(SYSLOG_PATH, SYSLOG_PATH_OLD);
    }
    if (rc != 0) {
        report("rotation failed");
    }
}

static int log_file_append(const char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        long put = write(log_file.fd, data + done, len - done);
        if (put <= 0) {
            return -1;
        }
        done += (size_t)put;
    }
    log_file.size += len;
    return 0;
}

static void flush_output(void) {
    if (out_len == 0) {
        return;
    }
    write(1, out_buf, out_len);
    if (log_file.fd >= 0 && log_file.size > 0 &&
        log_file.size + out_len > SYSLOG_FILE_LIMIT) {
        log_file_rotate();
    }
    if (log_file.fd < 0 && log_file_open() != 0) {
        if (!log_file.failure_reported) {
            report("cannot open " SYSLOG_PATH);
            log_file.failure_reported = 1;
        }
        out_len = 0;
        return;
    }
    log_file.failure_reported = 0;
    if (log_file_append(out_buf, out_len) != 0) {
        report("write to " SYSLOG_PATH " failed");
        log_file_close();
    }
    out_len = 0;
}

/* Control bytes are replaced so a service cannot inject terminal escapes. */
static char sanitize(char c) {
    if (c == '\t') {
        return c;
    }
    return ((unsigned char)c < 0x20 || c == 0x7f) ? '?' : c;
}

static void emit_record(const char *name, const char *text, size_t text_len) {
    char stamp[SYSLOG_STAMP_CAP];
    size_t stamp_len;
    size_t name_len = strlen(name);
    size_t record_len;

    while (text_len > 0 && (text[text_len - 1] == '\r' || text[text_len - 1] == ' ')) {
        --text_len;
    }
    if (text_len == 0) {
        return;
    }
    format_stamp(stamp, sizeof(stamp));
    stamp_len = strlen(stamp);
    record_len = stamp_len + 1 + name_len + 2 + text_len + 1;
    if (out_len + record_len > sizeof(out_buf)) {
        flush_output();
    }
    memcpy(out_buf + out_len, stamp, stamp_len);
    out_len += stamp_len;
    out_buf[out_len++] = ' ';
    memcpy(out_buf + out_len, name, name_len);
    out_len += name_len;
    out_buf[out_len++] = ':';
    out_buf[out_len++] = ' ';
    for (size_t i = 0; i < text_len; ++i) {
        out_buf[out_len++] = sanitize(text[i]);
    }
    out_buf[out_len++] = '\n';
}

static void source_emit_line(log_source_t *src) {
    emit_record(src->name, src->line, src->line_len);
    src->line_len = 0;
}

static void source_feed(log_source_t *src, const char *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\n') {
            source_emit_line(src);
            continue;
        }
        if (src->line_len == sizeof(src->line)) {
            source_emit_line(src);
        }
        src->line[src->line_len++] = data[i];
    }
}

static void source_close(size_t index) {
    log_source_t *src = &sources[index];
    source_emit_line(src);
    close(src->fd);
    sources[index] = sources[--source_count];
}

static int parse_source(const char *arg, log_source_t *out) {
    size_t colon = 0;
    size_t len = strlen(arg);
    long fd = 0;
    for (size_t i = 0; i < len; ++i) {
        if (arg[i] == ':') {
            colon = i;
        }
    }
    if (colon == 0 || colon >= SYSLOG_NAME_CAP || colon + 1 >= len) {
        return -1;
    }
    for (size_t i = colon + 1; i < len; ++i) {
        if (arg[i] < '0' || arg[i] > '9' || fd > 1000) {
            return -1;
        }
        fd = fd * 10 + (arg[i] - '0');
    }
    if (fd < 3) {
        return -1;
    }
    memcpy(out->name, arg, colon);
    out->name[colon] = 0;
    out->fd = (int)fd;
    out->line_len = 0;
    return 0;
}

int main(int argc, char **argv) {
    static poll_fd_t poll_set[SYSLOG_MAX_SOURCES];
    char chunk[SYSLOG_READ_CAP];
    char banner[48];

    if (argc - 1 > SYSLOG_MAX_SOURCES) {
        report("too many log sources");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (parse_source(argv[i], &sources[source_count]) != 0) {
            report("invalid log source argument");
            return 1;
        }
        ++source_count;
    }

    snprintf(banner, sizeof(banner), "started, %lu log sources", (unsigned long)source_count);
    emit_record(SYSLOG_SELF_NAME, banner, strlen(banner));
    flush_output();

    while (source_count > 0) {
        long ready;
        for (size_t i = 0; i < source_count; ++i) {
            poll_set[i].fd = sources[i].fd;
            poll_set[i].events = POLL_READ;
            poll_set[i].revents = 0;
        }
        ready = poll_many_wait(poll_set, (unsigned int)source_count, POLL_WAIT_INFINITE);
        if (ready < 0) {
            report("poll failed");
            return 1;
        }
        /* Closing swaps the last source into the slot, so walk backwards. */
        for (size_t i = source_count; i-- > 0;) {
            long got;
            if (poll_set[i].revents == 0) {
                continue;
            }
            got = read(sources[i].fd, chunk, sizeof(chunk));
            if (got > 0) {
                source_feed(&sources[i], chunk, (size_t)got);
            } else {
                source_close(i);
            }
        }
        flush_output();
    }

    emit_record(SYSLOG_SELF_NAME, SYSLOG_EXIT_TEXT, strlen(SYSLOG_EXIT_TEXT));
    flush_output();
    log_file_close();
    return 0;
}
