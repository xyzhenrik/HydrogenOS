#include "mini_libc.h"
#include "syscall.h"
#include "signal.h"

typedef enum init_service_type {
    INIT_SERVICE_ONESHOT = 0,
    INIT_SERVICE_SUPERVISED = 1,
    INIT_SERVICE_DAEMON = 2,
} init_service_type_t;

typedef enum init_restart_policy {
    INIT_RESTART_NEVER = 0,
    INIT_RESTART_ON_FAILURE = 1,
    INIT_RESTART_ALWAYS = 2,
} init_restart_policy_t;

typedef enum init_stdio_mode {
    INIT_STDIO_INHERIT = 0,
    INIT_STDIO_PIPE = 1,
    INIT_STDIO_CAPTURE = 2,
    INIT_STDIO_NULL = 3,
    INIT_STDIO_CHANNEL = 4,
    INIT_STDIO_TEE = 5,
    INIT_STDIO_LOGSINK = 6,
    INIT_STDIO_SYSLOG = 7,
} init_stdio_mode_t;

enum {
    INIT_MAX_SERVICES = 96,
    INIT_MAX_CHANNELS = 16,
    INIT_MAX_TEES = 16,
    INIT_MAX_LOGSINKS = 16,
    INIT_MAX_SYSLOG_SOURCES = 12,
    INIT_NAME_CAP = 32,
    INIT_PATH_CAP = 64,
    INIT_CHANNEL_NAME_CAP = 32,
    INIT_CONFIG_CAP = 16384,
    INIT_STDIO_TEXT_CAP = 128,
    INIT_STREAM_CAP = 512,
    INIT_DEFAULT_RETRIES = 3,
    INIT_MAX_RETRIES = 16,
    INIT_BACKOFF_CAP_MS = 30000,
    INIT_SPLASH_GRACE_SECONDS = 30,
    INIT_DEFAULT_TIMEOUT_MS = 120000,
    INIT_MAX_TIMEOUT_MS = 86400000,
    /* A daemon that ran this long before dying starts over with a fresh retry
     * budget; otherwise a long-lived service would exhaust it over weeks. */
    INIT_DAEMON_STABLE_MS = 10000,
    /* Interval at which init looks for exited daemons and due restarts. */
    INIT_TICK_MS = 10,
};

typedef struct init_service_runtime {
    int stdin_read_fd;
    int stdin_write_fd;
    int stdout_read_fd;
    int stdout_write_fd;
    int stderr_read_fd;
    int stderr_write_fd;
} init_service_runtime_t;

struct init_service;
struct init_service_table;
typedef int (*init_service_setup_fn)(struct init_service_table *table,
                                     const struct init_service *svc,
                                     init_service_runtime_t *rt);
typedef void (*init_service_cleanup_fn)(struct init_service_table *table,
                                        const struct init_service *svc,
                                        init_service_runtime_t *rt);
typedef int (*init_service_verify_fn)(struct init_service_table *table,
                                      const struct init_service *svc,
                                      init_service_runtime_t *rt, int status);

typedef struct init_service {
    const char *name;
    const char *path;
    unsigned long provides_mask;
    unsigned long deps_mask;
    init_service_type_t type;
    init_restart_policy_t restart_policy;
    unsigned long restart_delay_ms;
    unsigned long max_retries;
    unsigned long timeout_ms;
    init_stdio_mode_t stdin_mode;
    init_stdio_mode_t stdout_mode;
    init_stdio_mode_t stderr_mode;
    int stdin_channel_index;
    int stdout_channel_index;
    int stderr_channel_index;
    int syslog_source_index;
    unsigned char log_server;
    unsigned char display_owner;
    char stdin_text[INIT_STDIO_TEXT_CAP];
    char stdout_text[INIT_STDIO_TEXT_CAP];
    char stderr_text[INIT_STDIO_TEXT_CAP];
    init_service_setup_fn setup;
    init_service_cleanup_fn cleanup;
    init_service_verify_fn verify;
} init_service_t;

typedef struct init_channel {
    char name[INIT_CHANNEL_NAME_CAP];
    int read_fd;
    int write_fd;
    int reader_service_index;
    int writer_service_count;
    int writer_spawned_count;
} init_channel_t;

typedef struct init_tee {
    char name[INIT_CHANNEL_NAME_CAP];
    char data[INIT_STREAM_CAP];
    unsigned long len;
    unsigned long writer_count;
} init_tee_t;

typedef struct init_logsink {
    char name[INIT_CHANNEL_NAME_CAP];
    char data[INIT_STREAM_CAP];
    unsigned long len;
    unsigned long writer_count;
    unsigned long writer_done_count;
    unsigned char has_expected;
    unsigned char verified;
    char expected[INIT_STREAM_CAP];
} init_logsink_t;

/*
 * One pipe per service whose stdout/stderr go to syslogd. The write end stays
 * in init until the service is done; syslogd receives the read end as an
 * inherited handle and labels everything read from it with the service name.
 */
typedef struct init_syslog_source {
    int service_index;
    int read_fd;
    int write_fd;
} init_syslog_source_t;

typedef enum init_daemon_phase {
    INIT_DAEMON_NOT_STARTED = 0,
    INIT_DAEMON_RUNNING = 1,
    INIT_DAEMON_BACKOFF = 2,
    INIT_DAEMON_STOPPED = 3,
} init_daemon_phase_t;

/* Supervision state of one daemon, indexed like the service table. */
typedef struct init_daemon {
    init_daemon_phase_t phase;
    long pid;
    unsigned long failures;
    unsigned long started_ms;
    unsigned long restart_at_ms;
    init_service_runtime_t runtime;
} init_daemon_t;

typedef struct init_service_table {
    init_service_t services[INIT_MAX_SERVICES];
    init_daemon_t daemons[INIT_MAX_SERVICES];
    char names[INIT_MAX_SERVICES][INIT_NAME_CAP];
    char paths[INIT_MAX_SERVICES][INIT_PATH_CAP];
    init_channel_t channels[INIT_MAX_CHANNELS];
    init_tee_t tees[INIT_MAX_TEES];
    init_logsink_t logsinks[INIT_MAX_LOGSINKS];
    init_syslog_source_t syslog_sources[INIT_MAX_SYSLOG_SOURCES];
    int log_server_index;
    unsigned long daemon_failed_count;
    unsigned long count;
    unsigned long channel_count;
    unsigned long tee_count;
    unsigned long logsink_count;
    unsigned long syslog_source_count;
} init_service_table_t;

static int tee_append(init_service_table_t *table, int tee_index,
                      const char *data, unsigned long len);
static int logsink_append(init_service_table_t *table, int logsink_index,
                          const char *data, unsigned long len);
static int verify_completed_logsinks(init_service_table_t *table);
static int channel_add_writer_ref(init_service_table_t *table, int channel_index);
static int channel_remove_writer_ref(init_service_table_t *table, int channel_index);

static const char default_init_config[] =
    "logsink syslog \"sink-a\\nsink-b\\n\"\n"
    "service bootprobe oneshot /bootprobe 100 restart=on-failure\n"
    "service handoffcheck oneshot /handoffcheck 100 restart=on-failure "
    "stdin=pipe \"init handoff ok\\n\" bootprobe\n"
    "service stdiocheck oneshot /stdiocheck 100 restart=on-failure "
    "stdin=pipe \"init stdio ok\\n\" "
    "stdout=capture \"stdiocheck: stdout ok\\n\" "
    "stderr=capture \"stdiocheck: stderr ok\\n\" "
    "bootprobe handoffcheck\n"
    "service stdiobadbind oneshot /stdiobadbind 100 restart=on-failure "
    "bootprobe handoffcheck stdiocheck\n"
    "service dupcheck oneshot /dupcheck 100 restart=on-failure "
    "stdout=capture \"dupcheck: dup ok\\n\" "
    "bootprobe handoffcheck stdiocheck stdiobadbind\n"
    "service logproducer_a oneshot /logproducer_a 100 restart=on-failure "
    "stdout=channel logbus "
    "bootprobe handoffcheck stdiocheck stdiobadbind dupcheck\n"
    "service logproducer_b oneshot /logproducer_b 100 restart=on-failure "
    "stdout=channel logbus "
    "bootprobe handoffcheck stdiocheck stdiobadbind logproducer_a\n"
    "service logcollector oneshot /logcollector 100 restart=on-failure "
    "stdin=channel logbus "
    "stdout=capture \"logcollector: multiwriter ok\\n\" "
    "bootprobe handoffcheck stdiocheck stdiobadbind logproducer_b\n"
    "service logsink_writer_a oneshot /logsink_writer_a 100 restart=on-failure "
    "stdout=logsink syslog "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector\n"
    "service logsink_writer_b oneshot /logsink_writer_b 100 restart=on-failure "
    "stdout=logsink syslog "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector logsink_writer_a\n"
    "service teewriter oneshot /teewriter 100 restart=on-failure "
    "stdout=tee teebus "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector logsink_writer_b\n"
    "service teereader_a oneshot /teereader_a 100 restart=on-failure "
    "stdin=tee teebus "
    "stdout=capture \"teereader_a: tee ok\\n\" "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector teewriter\n"
    "service teereader_b oneshot /teereader_b 100 restart=on-failure "
    "stdin=tee teebus "
    "stdout=capture \"teereader_b: tee ok\\n\" "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector teewriter\n"
    "service pipeproducer oneshot /pipeproducer 100 restart=on-failure "
    "stdout=channel bootseq "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector teereader_b\n"
    "service pipeconsumer oneshot /pipeconsumer 100 restart=on-failure "
    "stdin=channel bootseq "
    "stdout=capture \"pipeconsumer: channel ok\\n\" "
    "bootprobe handoffcheck stdiocheck stdiobadbind logcollector pipeproducer\n"
    "service stdionullcheck oneshot /stdionullcheck 100 restart=on-failure "
    "stdin=null stdout=null stderr=null "
    "bootprobe handoffcheck stdiocheck stdiobadbind pipeconsumer\n"
    "service shell supervised /shell 100 restart=always "
    "stdin=inherit stdout=inherit stderr=inherit "
    "bootprobe handoffcheck stdiocheck stdiobadbind dupcheck logcollector pipeconsumer stdionullcheck\n";

static void write_u64(unsigned long v) {
    char buf[24];
    int i = 0;
    if (v == 0) {
        write(1, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

static void write_str(const char *s) {
    if (!s) {
        return;
    }
    write(1, s, strlen(s));
}

/*
 * Tells the kernel's boot splash how far startup has come. The report is
 * advisory: without a splash the kernel ignores it, so a failed write must not
 * affect boot.
 */
static void report_boot(const char *text) {
    (void)sys_writef("/proc/boot", text, strlen(text));
}

static void report_boot_progress(unsigned long done, unsigned long total) {
    char text[48];
    int len = snprintf(text, sizeof(text), "services %lu %lu\n", done, total);
    if (len > 0 && (unsigned long)len < sizeof(text)) {
        report_boot(text);
    }
}

static void runtime_reset(init_service_runtime_t *rt) {
    if (!rt) {
        return;
    }
    rt->stdin_read_fd = -1;
    rt->stdin_write_fd = -1;
    rt->stdout_read_fd = -1;
    rt->stdout_write_fd = -1;
    rt->stderr_read_fd = -1;
    rt->stderr_write_fd = -1;
}

/*
 * Status lines go out in one write so output of a just-spawned service cannot
 * land between the service name and the status text.
 */
static void write_service_prefix(const init_service_t *svc, const char *suffix) {
    char line[128];
    int len = snprintf(line, sizeof(line), "init: %s%s", svc->name,
                       suffix ? suffix : "");
    if (len > 0 && (unsigned long)len < sizeof(line)) {
        write(1, line, (unsigned long)len);
        return;
    }
    write_str("init: ");
    write_str(svc->name);
    write_str(suffix);
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int str_copy(char *dst, unsigned long cap, const char *src) {
    unsigned long i = 0;
    if (!dst || !src || cap == 0) {
        return -1;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
    return src[i] ? -1 : 0;
}

static void channel_reset(init_channel_t *channel) {
    if (!channel) {
        return;
    }
    channel->name[0] = 0;
    channel->read_fd = -1;
    channel->write_fd = -1;
    channel->reader_service_index = -1;
    channel->writer_service_count = 0;
    channel->writer_spawned_count = 0;
}

static void tee_reset(init_tee_t *tee) {
    if (!tee) {
        return;
    }
    tee->name[0] = 0;
    tee->len = 0;
    tee->writer_count = 0;
    tee->data[0] = 0;
}

static void logsink_reset(init_logsink_t *logsink) {
    if (!logsink) {
        return;
    }
    logsink->name[0] = 0;
    logsink->len = 0;
    logsink->writer_count = 0;
    logsink->writer_done_count = 0;
    logsink->has_expected = 0;
    logsink->verified = 0;
    logsink->data[0] = 0;
    logsink->expected[0] = 0;
}

static int clear_input_binding(init_service_table_t *table,
                               init_stdio_mode_t mode,
                               int index,
                               int service_index) {
    if (!table) {
        return -1;
    }
    if (mode != INIT_STDIO_CHANNEL) {
        return 0;
    }
    if (index < 0 || index >= (int)table->channel_count) {
        return -1;
    }
    if (table->channels[index].reader_service_index >= 0 &&
        table->channels[index].reader_service_index != service_index) {
        return -1;
    }
    table->channels[index].reader_service_index = -1;
    return 0;
}

static int input_mode_uses_binding(init_stdio_mode_t mode) {
    return mode == INIT_STDIO_CHANNEL || mode == INIT_STDIO_TEE;
}

static int output_mode_uses_binding(init_stdio_mode_t mode) {
    return mode == INIT_STDIO_CHANNEL ||
           mode == INIT_STDIO_TEE ||
           mode == INIT_STDIO_LOGSINK;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char translate_escape(char c) {
    switch (c) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    default:
        return c;
    }
}

static char *skip_space(char *s) {
    if (!s) {
        return 0;
    }
    while (*s && is_space(*s)) {
        ++s;
    }
    return s;
}

static int has_directive(const char *line, const char *directive) {
    if (!line || !directive) {
        return 0;
    }
    while (*line && is_space(*line)) {
        ++line;
    }
    while (*directive) {
        if (*line != *directive) {
            return 0;
        }
        ++line;
        ++directive;
    }
    return *line == 0 || is_space(*line) || *line == '#';
}

static char *next_token(char **cursor, int *out_error) {
    char *start;
    char *it;
    char *read;
    char *write;
    if (out_error) {
        *out_error = 0;
    }
    if (!cursor || !*cursor) {
        return 0;
    }
    start = skip_space(*cursor);
    if (!start || *start == 0 || *start == '#') {
        *cursor = start;
        return 0;
    }
    if (*start == '"') {
        read = start + 1;
        write = start;
        while (*read && *read != '"') {
            if (*read == '\\' && read[1]) {
                ++read;
                *write++ = translate_escape(*read++);
                continue;
            }
            *write++ = *read++;
        }
        if (*read != '"') {
            if (out_error) {
                *out_error = 1;
            }
            *cursor = read;
            return 0;
        }
        *write = 0;
        *cursor = read + 1;
        return start;
    }
    it = start;
    while (*it && !is_space(*it) && *it != '#') {
        ++it;
    }
    if (*it == '#') {
        *it = 0;
        *cursor = it;
        return start;
    }
    if (*it) {
        *it = 0;
        ++it;
    }
    *cursor = it;
    return start;
}

static int parse_u64_token(const char *s, unsigned long *out) {
    unsigned long value = 0;
    if (!s || !*s || !out) {
        return -1;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        value = value * 10 + (unsigned long)(*s - '0');
        ++s;
    }
    *out = value;
    return 0;
}

static void close_fd_slot(int *fd) {
    if (!fd || *fd < 0) {
        return;
    }
    close(*fd);
    *fd = -1;
}

static void close_runtime_fds(init_service_runtime_t *rt) {
    if (!rt) {
        return;
    }
    close_fd_slot(&rt->stdin_read_fd);
    close_fd_slot(&rt->stdin_write_fd);
    close_fd_slot(&rt->stdout_read_fd);
    close_fd_slot(&rt->stdout_write_fd);
    close_fd_slot(&rt->stderr_read_fd);
    close_fd_slot(&rt->stderr_write_fd);
}

static void cleanup_runtime_fds(init_service_table_t *table,
                                const init_service_t *svc,
                                init_service_runtime_t *rt) {
    (void)table;
    (void)svc;
    close_runtime_fds(rt);
}

static void close_runtime_child_stdio(init_service_runtime_t *rt) {
    if (!rt) {
        return;
    }
    close_fd_slot(&rt->stdin_read_fd);
    close_fd_slot(&rt->stdout_write_fd);
    close_fd_slot(&rt->stderr_write_fd);
}

static long read_capture_fd(int fd, char *buf, unsigned long cap) {
    unsigned long off = 0;
    if (fd < 0 || !buf || cap == 0) {
        return -1;
    }
    while (off + 1 < cap) {
        long got = read(fd, buf + off, cap - off - 1);
        if (got < 0) {
            return -1;
        }
        if (got == 0) {
            break;
        }
        off += (unsigned long)got;
    }
    buf[off] = 0;
    return (long)off;
}

static int configure_pipe(int *read_fd, int *write_fd) {
    int fds[2] = { -1, -1 };
    if (!read_fd || !write_fd) {
        return -1;
    }
    if (pipe(fds) != 0) {
        return -1;
    }
    *read_fd = fds[0];
    *write_fd = fds[1];
    return 0;
}

static int service_needs_runtime_stdio(const init_service_t *svc) {
    if (!svc) {
        return 0;
    }
    return svc->stdin_mode == INIT_STDIO_PIPE ||
           svc->stdin_mode == INIT_STDIO_TEE ||
           svc->stdout_mode == INIT_STDIO_CAPTURE ||
           svc->stdout_mode == INIT_STDIO_TEE ||
           svc->stdout_mode == INIT_STDIO_LOGSINK ||
           svc->stderr_mode == INIT_STDIO_CAPTURE ||
           svc->stderr_mode == INIT_STDIO_TEE ||
           svc->stderr_mode == INIT_STDIO_LOGSINK;
}

static int service_uses_explicit_stdio(const init_service_t *svc) {
    if (!svc) {
        return 0;
    }
    return svc->stdin_mode != INIT_STDIO_INHERIT ||
           svc->stdout_mode != INIT_STDIO_INHERIT ||
           svc->stderr_mode != INIT_STDIO_INHERIT;
}

static int setup_service_stdio(init_service_table_t *table, const init_service_t *svc,
                               init_service_runtime_t *rt) {
    unsigned long len;
    if (!svc || !rt) {
        return -1;
    }
    runtime_reset(rt);
    if (svc->stdin_mode == INIT_STDIO_PIPE) {
        len = strlen(svc->stdin_text);
        if (configure_pipe(&rt->stdin_read_fd, &rt->stdin_write_fd) != 0) {
            close_runtime_fds(rt);
            return -1;
        }
        if (len > 0 && write(rt->stdin_write_fd, svc->stdin_text, len) != (long)len) {
            close_runtime_fds(rt);
            return -1;
        }
        close_fd_slot(&rt->stdin_write_fd);
    }
    if (svc->stdin_mode == INIT_STDIO_TEE) {
        int tee_index = svc->stdin_channel_index;
        if (!table || tee_index < 0 || tee_index >= (int)table->tee_count) {
            close_runtime_fds(rt);
            return -1;
        }
        len = table->tees[tee_index].len;
        if (configure_pipe(&rt->stdin_read_fd, &rt->stdin_write_fd) != 0) {
            close_runtime_fds(rt);
            return -1;
        }
        if (len > 0 &&
            write(rt->stdin_write_fd, table->tees[tee_index].data, len) != (long)len) {
            close_runtime_fds(rt);
            return -1;
        }
        close_fd_slot(&rt->stdin_write_fd);
    }
    if ((svc->stdout_mode == INIT_STDIO_CAPTURE ||
         svc->stdout_mode == INIT_STDIO_TEE ||
         svc->stdout_mode == INIT_STDIO_LOGSINK) &&
        configure_pipe(&rt->stdout_read_fd, &rt->stdout_write_fd) != 0) {
        close_runtime_fds(rt);
        return -1;
    }
    if ((svc->stderr_mode == INIT_STDIO_CAPTURE ||
         svc->stderr_mode == INIT_STDIO_TEE ||
         svc->stderr_mode == INIT_STDIO_LOGSINK) &&
        configure_pipe(&rt->stderr_read_fd, &rt->stderr_write_fd) != 0) {
        close_runtime_fds(rt);
        return -1;
    }
    return 0;
}

static int verify_service_stdio(init_service_table_t *table, const init_service_t *svc,
                                init_service_runtime_t *rt,
                                int status) {
    char stdout_buf[INIT_STREAM_CAP];
    char stderr_buf[INIT_STREAM_CAP];
    long stdout_len = 0;
    long stderr_len = 0;
    (void)status;
    if (!svc || !rt) {
        return -1;
    }
    if (svc->stdout_mode == INIT_STDIO_CAPTURE ||
        svc->stdout_mode == INIT_STDIO_TEE ||
        svc->stdout_mode == INIT_STDIO_LOGSINK) {
        stdout_len = read_capture_fd(rt->stdout_read_fd, stdout_buf, sizeof(stdout_buf));
        if (stdout_len < 0) {
            write_service_prefix(svc, " stdout collection failed\n");
            return -1;
        }
        if (svc->stdout_mode == INIT_STDIO_CAPTURE && !str_eq(stdout_buf, svc->stdout_text)) {
            write_service_prefix(svc, " unexpected stdout: ");
            write_str(stdout_buf);
            if (stdout_len == 0 || stdout_buf[stdout_len - 1] != '\n') {
                write(1, "\n", 1);
            }
            return -1;
        }
        if (svc->stdout_mode == INIT_STDIO_CAPTURE) {
            write_service_prefix(svc, " stdout captured: ");
            write_str(stdout_buf);
        } else if (svc->stdout_mode == INIT_STDIO_TEE) {
            if (tee_append(table, svc->stdout_channel_index, stdout_buf,
                           (unsigned long)stdout_len) != 0) {
                write_service_prefix(svc, " tee append failed\n");
                return -1;
            }
        } else if (svc->stdout_mode == INIT_STDIO_LOGSINK) {
            if (logsink_append(table, svc->stdout_channel_index, stdout_buf,
                               (unsigned long)stdout_len) != 0) {
                write_service_prefix(svc, " logsink append failed\n");
                return -1;
            }
            table->logsinks[svc->stdout_channel_index].writer_done_count++;
        }
    }
    if (svc->stderr_mode == INIT_STDIO_CAPTURE ||
        svc->stderr_mode == INIT_STDIO_TEE ||
        svc->stderr_mode == INIT_STDIO_LOGSINK) {
        stderr_len = read_capture_fd(rt->stderr_read_fd, stderr_buf, sizeof(stderr_buf));
        if (stderr_len < 0) {
            write_service_prefix(svc, " stderr collection failed\n");
            return -1;
        }
        if (svc->stderr_mode == INIT_STDIO_CAPTURE && !str_eq(stderr_buf, svc->stderr_text)) {
            write_service_prefix(svc, " unexpected stderr: ");
            write_str(stderr_buf);
            if (stderr_len == 0 || stderr_buf[stderr_len - 1] != '\n') {
                write(1, "\n", 1);
            }
            return -1;
        }
        if (svc->stderr_mode == INIT_STDIO_CAPTURE) {
            write_service_prefix(svc, " stderr captured: ");
            write_str(stderr_buf);
        } else if (svc->stderr_mode == INIT_STDIO_TEE) {
            if (tee_append(table, svc->stderr_channel_index, stderr_buf,
                           (unsigned long)stderr_len) != 0) {
                write_service_prefix(svc, " tee append failed\n");
                return -1;
            }
        } else if (svc->stderr_mode == INIT_STDIO_LOGSINK) {
            if (logsink_append(table, svc->stderr_channel_index, stderr_buf,
                               (unsigned long)stderr_len) != 0) {
                write_service_prefix(svc, " logsink append failed\n");
                return -1;
            }
            table->logsinks[svc->stderr_channel_index].writer_done_count++;
        }
    }
    return 0;
}

static void bind_service_hooks(init_service_t *svc) {
    if (!svc) {
        return;
    }
    svc->setup = 0;
    svc->cleanup = 0;
    svc->verify = 0;
    if (service_needs_runtime_stdio(svc)) {
        svc->setup = setup_service_stdio;
        svc->cleanup = cleanup_runtime_fds;
    }
    if (svc->stdout_mode == INIT_STDIO_CAPTURE ||
        svc->stdout_mode == INIT_STDIO_TEE ||
        svc->stdout_mode == INIT_STDIO_LOGSINK ||
        svc->stderr_mode == INIT_STDIO_CAPTURE ||
        svc->stderr_mode == INIT_STDIO_TEE ||
        svc->stderr_mode == INIT_STDIO_LOGSINK) {
        svc->verify = verify_service_stdio;
    }
}

static int find_channel_index(const init_service_table_t *table, const char *name) {
    if (!table || !name) {
        return -1;
    }
    for (unsigned long i = 0; i < table->channel_count; ++i) {
        if (str_eq(table->channels[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_or_add_channel(init_service_table_t *table, const char *name) {
    int index;
    if (!table || !name) {
        return -1;
    }
    index = find_channel_index(table, name);
    if (index >= 0) {
        return index;
    }
    if (table->channel_count >= INIT_MAX_CHANNELS) {
        return -1;
    }
    index = (int)table->channel_count;
    channel_reset(&table->channels[index]);
    if (str_copy(table->channels[index].name, sizeof(table->channels[index].name), name) != 0) {
        return -1;
    }
    table->channel_count++;
    return index;
}

static int find_tee_index(const init_service_table_t *table, const char *name) {
    if (!table || !name) {
        return -1;
    }
    for (unsigned long i = 0; i < table->tee_count; ++i) {
        if (str_eq(table->tees[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_or_add_tee(init_service_table_t *table, const char *name) {
    int index;
    if (!table || !name) {
        return -1;
    }
    index = find_tee_index(table, name);
    if (index >= 0) {
        return index;
    }
    if (table->tee_count >= INIT_MAX_TEES) {
        return -1;
    }
    index = (int)table->tee_count;
    tee_reset(&table->tees[index]);
    if (str_copy(table->tees[index].name, sizeof(table->tees[index].name), name) != 0) {
        return -1;
    }
    table->tee_count++;
    return index;
}

static int tee_append(init_service_table_t *table, int tee_index,
                      const char *data, unsigned long len) {
    init_tee_t *tee;
    if (!table || tee_index < 0 || tee_index >= (int)table->tee_count ||
        !data || len == 0) {
        return len == 0 ? 0 : -1;
    }
    tee = &table->tees[tee_index];
    if (tee->len + len >= sizeof(tee->data)) {
        return -1;
    }
    for (unsigned long i = 0; i < len; ++i) {
        tee->data[tee->len + i] = data[i];
    }
    tee->len += len;
    tee->data[tee->len] = 0;
    return 0;
}

static int find_logsink_index(const init_service_table_t *table, const char *name) {
    if (!table || !name) {
        return -1;
    }
    for (unsigned long i = 0; i < table->logsink_count; ++i) {
        if (str_eq(table->logsinks[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_or_add_logsink(init_service_table_t *table, const char *name) {
    int index;
    if (!table || !name) {
        return -1;
    }
    index = find_logsink_index(table, name);
    if (index >= 0) {
        return index;
    }
    if (table->logsink_count >= INIT_MAX_LOGSINKS) {
        return -1;
    }
    index = (int)table->logsink_count;
    logsink_reset(&table->logsinks[index]);
    if (str_copy(table->logsinks[index].name, sizeof(table->logsinks[index].name), name) != 0) {
        return -1;
    }
    table->logsink_count++;
    return index;
}

static int logsink_append(init_service_table_t *table, int logsink_index,
                          const char *data, unsigned long len) {
    init_logsink_t *logsink;
    if (!table || logsink_index < 0 || logsink_index >= (int)table->logsink_count ||
        !data || len == 0) {
        return len == 0 ? 0 : -1;
    }
    logsink = &table->logsinks[logsink_index];
    if (logsink->len + len >= sizeof(logsink->data)) {
        return -1;
    }
    for (unsigned long i = 0; i < len; ++i) {
        logsink->data[logsink->len + i] = data[i];
    }
    logsink->len += len;
    logsink->data[logsink->len] = 0;
    return 0;
}

static int verify_completed_logsinks(init_service_table_t *table) {
    if (!table) {
        return -1;
    }
    for (unsigned long i = 0; i < table->logsink_count; ++i) {
        init_logsink_t *logsink = &table->logsinks[i];
        if (!logsink->has_expected || logsink->verified) {
            continue;
        }
        if (logsink->writer_done_count < logsink->writer_count) {
            continue;
        }
        if (!str_eq(logsink->data, logsink->expected)) {
            write_str("init: logsink ");
            write_str(logsink->name);
            write_str(" unexpected data: ");
            write_str(logsink->data);
            if (logsink->len == 0 || logsink->data[logsink->len - 1] != '\n') {
                write(1, "\n", 1);
            }
            return -1;
        }
        write_str("init: logsink ");
        write_str(logsink->name);
        write_str(" captured: ");
        write_str(logsink->data);
        logsink->verified = 1;
    }
    return 0;
}

static int add_output_binding_ref(init_service_table_t *table,
                                  init_stdio_mode_t mode, int index) {
    if (!table || index < 0) {
        return -1;
    }
    if (mode == INIT_STDIO_CHANNEL) {
        return channel_add_writer_ref(table, index);
    }
    if (mode == INIT_STDIO_TEE) {
        if (index >= (int)table->tee_count) {
            return -1;
        }
        table->tees[index].writer_count++;
        return 0;
    }
    if (mode == INIT_STDIO_LOGSINK) {
        if (index >= (int)table->logsink_count) {
            return -1;
        }
        table->logsinks[index].writer_count++;
        return 0;
    }
    return 0;
}

static int remove_output_binding_ref(init_service_table_t *table,
                                     init_stdio_mode_t mode, int index) {
    if (!table || index < 0) {
        return -1;
    }
    if (mode == INIT_STDIO_CHANNEL) {
        return channel_remove_writer_ref(table, index);
    }
    if (mode == INIT_STDIO_TEE) {
        if (index >= (int)table->tee_count || table->tees[index].writer_count == 0) {
            return -1;
        }
        table->tees[index].writer_count--;
        return 0;
    }
    if (mode == INIT_STDIO_LOGSINK) {
        if (index >= (int)table->logsink_count || table->logsinks[index].writer_count == 0) {
            return -1;
        }
        table->logsinks[index].writer_count--;
        return 0;
    }
    return 0;
}

static int ensure_channel_pipe(init_service_table_t *table, int channel_index) {
    init_channel_t *channel;
    int fds[2] = { -1, -1 };
    if (!table || channel_index < 0 || channel_index >= (int)table->channel_count) {
        return -1;
    }
    channel = &table->channels[channel_index];
    if (channel->read_fd >= 0 || channel->write_fd >= 0) {
        return 0;
    }
    if (pipe(fds) != 0) {
        return -1;
    }
    channel->read_fd = fds[0];
    channel->write_fd = fds[1];
    return 0;
}

static void close_channel_read(init_service_table_t *table, int channel_index) {
    if (!table || channel_index < 0 || channel_index >= (int)table->channel_count) {
        return;
    }
    close_fd_slot(&table->channels[channel_index].read_fd);
}

static void close_channel_write(init_service_table_t *table, int channel_index) {
    if (!table || channel_index < 0 || channel_index >= (int)table->channel_count) {
        return;
    }
    close_fd_slot(&table->channels[channel_index].write_fd);
}

static int channel_add_writer_ref(init_service_table_t *table, int channel_index) {
    if (!table || channel_index < 0 || channel_index >= (int)table->channel_count) {
        return -1;
    }
    table->channels[channel_index].writer_service_count++;
    return 0;
}

static int channel_remove_writer_ref(init_service_table_t *table, int channel_index) {
    if (!table || channel_index < 0 || channel_index >= (int)table->channel_count) {
        return -1;
    }
    if (table->channels[channel_index].writer_service_count <= 0) {
        return -1;
    }
    table->channels[channel_index].writer_service_count--;
    return 0;
}

static int release_output_binding(init_service_table_t *table,
                                  init_stdio_mode_t mode, int index) {
    if (!output_mode_uses_binding(mode) || index < 0) {
        return 0;
    }
    return remove_output_binding_ref(table, mode, index);
}

static int add_syslog_source(init_service_table_t *table, int service_index) {
    init_syslog_source_t *source;
    if (!table || table->syslog_source_count >= INIT_MAX_SYSLOG_SOURCES) {
        return -1;
    }
    source = &table->syslog_sources[table->syslog_source_count];
    source->service_index = service_index;
    source->read_fd = -1;
    source->write_fd = -1;
    return (int)table->syslog_source_count++;
}

/* Every logging service must start after the log server that will read it. */
static int validate_syslog_topology(const init_service_table_t *table) {
    if (!table || table->syslog_source_count == 0) {
        return 0;
    }
    if (table->log_server_index < 0) {
        return -1;
    }
    for (unsigned long i = 0; i < table->syslog_source_count; ++i) {
        if (table->syslog_sources[i].service_index <= table->log_server_index) {
            return -1;
        }
    }
    return 0;
}

static void close_syslog_source_write(init_service_table_t *table,
                                      const init_service_t *svc) {
    if (!table || !svc || svc->syslog_source_index < 0 ||
        svc->syslog_source_index >= (int)table->syslog_source_count) {
        return;
    }
    close_fd_slot(&table->syslog_sources[svc->syslog_source_index].write_fd);
}

static void close_syslog_sources(init_service_table_t *table) {
    for (unsigned long i = 0; i < table->syslog_source_count; ++i) {
        close_fd_slot(&table->syslog_sources[i].read_fd);
        close_fd_slot(&table->syslog_sources[i].write_fd);
    }
}

/*
 * Starts the log server with "<service>:<fd>" arguments. The read ends are
 * inherited only for this one spawn so no other service can hold a source
 * open and delay its end-of-file.
 */
static long spawn_log_server(init_service_table_t *table, const init_service_t *svc) {
    char args[INIT_MAX_SYSLOG_SOURCES][INIT_NAME_CAP + 8];
    const char *argv[INIT_MAX_SYSLOG_SOURCES + 2];
    unsigned long count = table->syslog_source_count;
    long pid;

    for (unsigned long i = 0; i < count; ++i) {
        init_syslog_source_t *source = &table->syslog_sources[i];
        if (configure_pipe(&source->read_fd, &source->write_fd) != 0 ||
            set_handle_inherit(source->read_fd, 1) != 0 ||
            snprintf(args[i], sizeof(args[i]), "%s:%d",
                     table->names[source->service_index], source->read_fd) <= 0) {
            close_syslog_sources(table);
            return -1;
        }
        argv[i + 1] = args[i];
    }
    argv[0] = svc->name;
    argv[count + 1] = 0;
    pid = spawn_args(svc->path, argv, 0, SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT,
                     SPAWN_STDIO_INHERIT);
    for (unsigned long i = 0; i < count; ++i) {
        close_fd_slot(&table->syslog_sources[i].read_fd);
    }
    if (pid < 0) {
        close_syslog_sources(table);
    }
    return pid;
}

static int parse_stdio_mode(const char *token, const char *prefix,
                            init_stdio_mode_t *out_mode) {
    const char *value;
    if (!token || !prefix || !out_mode) {
        return -1;
    }
    value = token;
    while (*prefix) {
        if (*value != *prefix) {
            return -1;
        }
        ++value;
        ++prefix;
    }
    if (str_eq(value, "inherit")) {
        *out_mode = INIT_STDIO_INHERIT;
        return 0;
    }
    if (str_eq(value, "null")) {
        *out_mode = INIT_STDIO_NULL;
        return 0;
    }
    if (str_eq(value, "pipe")) {
        *out_mode = INIT_STDIO_PIPE;
        return 0;
    }
    if (str_eq(value, "capture")) {
        *out_mode = INIT_STDIO_CAPTURE;
        return 0;
    }
    return -1;
}

static int find_service_index(const init_service_table_t *table, const char *name) {
    if (!table || !name) {
        return -1;
    }
    for (unsigned long i = 0; i < table->count; ++i) {
        if (str_eq(table->services[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int parse_service_type(const char *token, init_service_type_t *out) {
    if (!token || !out) {
        return -1;
    }
    if (str_eq(token, "oneshot")) {
        *out = INIT_SERVICE_ONESHOT;
        return 0;
    }
    if (str_eq(token, "supervised")) {
        *out = INIT_SERVICE_SUPERVISED;
        return 0;
    }
    if (str_eq(token, "daemon")) {
        *out = INIT_SERVICE_DAEMON;
        return 0;
    }
    return -1;
}

static init_restart_policy_t default_restart_policy(init_service_type_t type) {
    if (type == INIT_SERVICE_SUPERVISED || type == INIT_SERVICE_DAEMON) {
        return INIT_RESTART_ALWAYS;
    }
    return INIT_RESTART_ON_FAILURE;
}

static int parse_restart_policy(const char *token, init_restart_policy_t *out) {
    if (!token || !out) {
        return -1;
    }
    if (str_eq(token, "never")) {
        *out = INIT_RESTART_NEVER;
        return 0;
    }
    if (str_eq(token, "on-failure")) {
        *out = INIT_RESTART_ON_FAILURE;
        return 0;
    }
    if (str_eq(token, "always")) {
        *out = INIT_RESTART_ALWAYS;
        return 0;
    }
    return -1;
}

static const char *option_value(const char *token, const char *prefix) {
    if (!token || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (*token != *prefix) {
            return 0;
        }
        ++token;
        ++prefix;
    }
    return token;
}

static int parse_restart_option(const char *token, init_restart_policy_t *out) {
    const char *value = option_value(token, "restart=");
    if (!value || !out) {
        return -1;
    }
    return parse_restart_policy(value, out);
}

static int parse_retries_option(const char *token, unsigned long *out) {
    const char *value = option_value(token, "retries=");
    unsigned long retries;
    if (!value || !out) {
        return -1;
    }
    if (parse_u64_token(value, &retries) != 0 || retries > INIT_MAX_RETRIES) {
        return -1;
    }
    *out = retries;
    return 0;
}

static unsigned long default_timeout_ms(init_service_type_t type) {
    return type == INIT_SERVICE_ONESHOT ? INIT_DEFAULT_TIMEOUT_MS : 0;
}

static int parse_timeout_option(const char *token, unsigned long *out) {
    const char *value = option_value(token, "timeout=");
    unsigned long timeout;
    if (!value || !out) {
        return -1;
    }
    if (parse_u64_token(value, &timeout) != 0 || timeout > INIT_MAX_TIMEOUT_MS) {
        return -1;
    }
    *out = timeout;
    return 0;
}

static int parse_logsink_line(init_service_table_t *table, char *line) {
    char *cursor = line;
    int parse_error = 0;
    char *directive = next_token(&cursor, &parse_error);
    char *name;
    char *expected;
    int logsink_index;
    if (parse_error) {
        return -1;
    }
    if (!directive) {
        return 0;
    }
    if (!str_eq(directive, "logsink")) {
        return -1;
    }
    name = next_token(&cursor, &parse_error);
    expected = next_token(&cursor, &parse_error);
    if (parse_error || !name || !expected) {
        return -1;
    }
    logsink_index = find_or_add_logsink(table, name);
    if (logsink_index < 0) {
        return -1;
    }
    if (str_copy(table->logsinks[logsink_index].expected,
                 sizeof(table->logsinks[logsink_index].expected), expected) != 0) {
        return -1;
    }
    table->logsinks[logsink_index].has_expected = 1;
    return 0;
}

static int parse_service_line(init_service_table_t *table, char *line) {
    char *cursor = line;
    int parse_error = 0;
    char *directive = next_token(&cursor, &parse_error);
    char *name;
    char *type_token;
    char *path;
    char *restart_token;
    unsigned long restart_delay_ms = 0;
    unsigned long max_retries = INIT_DEFAULT_RETRIES;
    unsigned long timeout_ms;
    unsigned long deps_mask = 0;
    int service_index;
    init_service_type_t type;
    init_restart_policy_t restart_policy;
    init_stdio_mode_t stdin_mode = INIT_STDIO_INHERIT;
    init_stdio_mode_t stdout_mode = INIT_STDIO_INHERIT;
    init_stdio_mode_t stderr_mode = INIT_STDIO_INHERIT;
    int stdin_channel_index = -1;
    int stdout_channel_index = -1;
    int stderr_channel_index = -1;
    int syslog_source_index = -1;
    int is_log_server = 0;
    int is_display_owner = 0;
    char stdin_text[INIT_STDIO_TEXT_CAP];
    char stdout_text[INIT_STDIO_TEXT_CAP];
    char stderr_text[INIT_STDIO_TEXT_CAP];
    init_service_t *svc;
    stdin_text[0] = 0;
    stdout_text[0] = 0;
    stderr_text[0] = 0;
    if (parse_error) {
        return -1;
    }
    if (!directive) {
        return 0;
    }
    if (!str_eq(directive, "service")) {
        return -1;
    }
    if (table->count >= INIT_MAX_SERVICES) {
        return -1;
    }
    service_index = (int)table->count;
    name = next_token(&cursor, &parse_error);
    type_token = next_token(&cursor, &parse_error);
    path = next_token(&cursor, &parse_error);
    restart_token = next_token(&cursor, &parse_error);
    if (parse_error) {
        return -1;
    }
    if (!name || !type_token || !path || !restart_token) {
        return -1;
    }
    if (parse_service_type(type_token, &type) != 0 ||
        parse_u64_token(restart_token, &restart_delay_ms) != 0 ||
        str_copy(table->names[table->count], INIT_NAME_CAP, name) != 0 ||
        str_copy(table->paths[table->count], INIT_PATH_CAP, path) != 0) {
        return -1;
    }
    restart_policy = default_restart_policy(type);
    timeout_ms = default_timeout_ms(type);
    for (;;) {
        char *dep = next_token(&cursor, &parse_error);
        int dep_index;
        char *payload;
        init_stdio_mode_t prev_stdin_mode = stdin_mode;
        init_stdio_mode_t prev_stdout_mode = stdout_mode;
        init_stdio_mode_t prev_stderr_mode = stderr_mode;
        int prev_stdin_index = stdin_channel_index;
        int prev_stdout_index = stdout_channel_index;
        int prev_stderr_index = stderr_channel_index;
        if (parse_error) {
            return -1;
        }
        if (!dep) {
            break;
        }
        if (parse_restart_option(dep, &restart_policy) == 0 ||
            parse_retries_option(dep, &max_retries) == 0 ||
            parse_timeout_option(dep, &timeout_ms) == 0) {
            continue;
        }
        if (str_eq(dep, "stdin=channel")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if (input_mode_uses_binding(prev_stdin_mode) &&
                prev_stdin_index >= 0 &&
                clear_input_binding(table, prev_stdin_mode, prev_stdin_index, service_index) != 0) {
                return -1;
            }
            stdin_channel_index = find_or_add_channel(table, payload);
            if (stdin_channel_index < 0) {
                return -1;
            }
            if (table->channels[stdin_channel_index].reader_service_index >= 0 &&
                table->channels[stdin_channel_index].reader_service_index != service_index) {
                return -1;
            }
            table->channels[stdin_channel_index].reader_service_index = service_index;
            stdin_mode = INIT_STDIO_CHANNEL;
            stdin_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stdout=channel")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stdout_mode == INIT_STDIO_CHANNEL ||
                 prev_stdout_mode == INIT_STDIO_TEE ||
                 prev_stdout_mode == INIT_STDIO_LOGSINK) &&
                prev_stdout_index >= 0 &&
                remove_output_binding_ref(table, prev_stdout_mode, prev_stdout_index) != 0) {
                return -1;
            }
            stdout_channel_index = find_or_add_channel(table, payload);
            if (stdout_channel_index < 0) {
                return -1;
            }
            if (add_output_binding_ref(table, INIT_STDIO_CHANNEL, stdout_channel_index) != 0) {
                return -1;
            }
            stdout_mode = INIT_STDIO_CHANNEL;
            stdout_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stderr=channel")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stderr_mode == INIT_STDIO_CHANNEL ||
                 prev_stderr_mode == INIT_STDIO_TEE ||
                 prev_stderr_mode == INIT_STDIO_LOGSINK) &&
                prev_stderr_index >= 0 &&
                remove_output_binding_ref(table, prev_stderr_mode, prev_stderr_index) != 0) {
                return -1;
            }
            stderr_channel_index = find_or_add_channel(table, payload);
            if (stderr_channel_index < 0) {
                return -1;
            }
            if (add_output_binding_ref(table, INIT_STDIO_CHANNEL, stderr_channel_index) != 0) {
                return -1;
            }
            stderr_mode = INIT_STDIO_CHANNEL;
            stderr_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stdin=tee")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if (input_mode_uses_binding(prev_stdin_mode) &&
                prev_stdin_index >= 0 &&
                clear_input_binding(table, prev_stdin_mode, prev_stdin_index, service_index) != 0) {
                return -1;
            }
            stdin_channel_index = find_or_add_tee(table, payload);
            if (stdin_channel_index < 0) {
                return -1;
            }
            stdin_mode = INIT_STDIO_TEE;
            stdin_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stdout=tee")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stdout_mode == INIT_STDIO_CHANNEL ||
                 prev_stdout_mode == INIT_STDIO_TEE ||
                 prev_stdout_mode == INIT_STDIO_LOGSINK) &&
                prev_stdout_index >= 0 &&
                remove_output_binding_ref(table, prev_stdout_mode, prev_stdout_index) != 0) {
                return -1;
            }
            stdout_channel_index = find_or_add_tee(table, payload);
            if (stdout_channel_index < 0 ||
                add_output_binding_ref(table, INIT_STDIO_TEE, stdout_channel_index) != 0) {
                return -1;
            }
            stdout_mode = INIT_STDIO_TEE;
            stdout_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stderr=tee")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stderr_mode == INIT_STDIO_CHANNEL ||
                 prev_stderr_mode == INIT_STDIO_TEE ||
                 prev_stderr_mode == INIT_STDIO_LOGSINK) &&
                prev_stderr_index >= 0 &&
                remove_output_binding_ref(table, prev_stderr_mode, prev_stderr_index) != 0) {
                return -1;
            }
            stderr_channel_index = find_or_add_tee(table, payload);
            if (stderr_channel_index < 0 ||
                add_output_binding_ref(table, INIT_STDIO_TEE, stderr_channel_index) != 0) {
                return -1;
            }
            stderr_mode = INIT_STDIO_TEE;
            stderr_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stdout=logsink")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stdout_mode == INIT_STDIO_CHANNEL ||
                 prev_stdout_mode == INIT_STDIO_TEE ||
                 prev_stdout_mode == INIT_STDIO_LOGSINK) &&
                prev_stdout_index >= 0 &&
                remove_output_binding_ref(table, prev_stdout_mode, prev_stdout_index) != 0) {
                return -1;
            }
            stdout_channel_index = find_or_add_logsink(table, payload);
            if (stdout_channel_index < 0 ||
                add_output_binding_ref(table, INIT_STDIO_LOGSINK, stdout_channel_index) != 0) {
                return -1;
            }
            stdout_mode = INIT_STDIO_LOGSINK;
            stdout_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "stderr=logsink")) {
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload) {
                return -1;
            }
            if ((prev_stderr_mode == INIT_STDIO_CHANNEL ||
                 prev_stderr_mode == INIT_STDIO_TEE ||
                 prev_stderr_mode == INIT_STDIO_LOGSINK) &&
                prev_stderr_index >= 0 &&
                remove_output_binding_ref(table, prev_stderr_mode, prev_stderr_index) != 0) {
                return -1;
            }
            stderr_channel_index = find_or_add_logsink(table, payload);
            if (stderr_channel_index < 0 ||
                add_output_binding_ref(table, INIT_STDIO_LOGSINK, stderr_channel_index) != 0) {
                return -1;
            }
            stderr_mode = INIT_STDIO_LOGSINK;
            stderr_text[0] = 0;
            continue;
        }
        if (str_eq(dep, "log=server")) {
            is_log_server = 1;
            continue;
        }
        if (str_eq(dep, "display=owner")) {
            is_display_owner = 1;
            continue;
        }
        if (str_eq(dep, "stdout=syslog") || str_eq(dep, "stderr=syslog") ||
            str_eq(dep, "log=syslog")) {
            if (!str_eq(dep, "stderr=syslog")) {
                if (release_output_binding(table, prev_stdout_mode, prev_stdout_index) != 0) {
                    return -1;
                }
                stdout_mode = INIT_STDIO_SYSLOG;
                stdout_channel_index = -1;
                stdout_text[0] = 0;
            }
            if (!str_eq(dep, "stdout=syslog")) {
                if (release_output_binding(table, prev_stderr_mode, prev_stderr_index) != 0) {
                    return -1;
                }
                stderr_mode = INIT_STDIO_SYSLOG;
                stderr_channel_index = -1;
                stderr_text[0] = 0;
            }
            continue;
        }
        if (parse_stdio_mode(dep, "stdin=", &stdin_mode) == 0) {
            if (input_mode_uses_binding(prev_stdin_mode) &&
                prev_stdin_index >= 0 &&
                clear_input_binding(table, prev_stdin_mode, prev_stdin_index, service_index) != 0) {
                return -1;
            }
            if (stdin_mode == INIT_STDIO_PIPE) {
                payload = next_token(&cursor, &parse_error);
                if (parse_error || !payload ||
                    str_copy(stdin_text, sizeof(stdin_text), payload) != 0) {
                    return -1;
                }
            } else {
                stdin_text[0] = 0;
            }
            stdin_channel_index = -1;
            continue;
        }
        if (parse_stdio_mode(dep, "stdout=", &stdout_mode) == 0) {
            if (stdout_mode == INIT_STDIO_CAPTURE) {
                payload = next_token(&cursor, &parse_error);
                if (parse_error || !payload ||
                    str_copy(stdout_text, sizeof(stdout_text), payload) != 0) {
                    return -1;
                }
            } else {
                stdout_text[0] = 0;
            }
            if ((prev_stdout_mode == INIT_STDIO_CHANNEL ||
                 prev_stdout_mode == INIT_STDIO_TEE ||
                 prev_stdout_mode == INIT_STDIO_LOGSINK) &&
                prev_stdout_index >= 0 &&
                remove_output_binding_ref(table, prev_stdout_mode, prev_stdout_index) != 0) {
                return -1;
            }
            stdout_channel_index = -1;
            continue;
        }
        if (parse_stdio_mode(dep, "stderr=", &stderr_mode) == 0) {
            if (stderr_mode == INIT_STDIO_CAPTURE) {
                payload = next_token(&cursor, &parse_error);
                if (parse_error || !payload ||
                    str_copy(stderr_text, sizeof(stderr_text), payload) != 0) {
                    return -1;
                }
            } else {
                stderr_text[0] = 0;
            }
            if ((prev_stderr_mode == INIT_STDIO_CHANNEL ||
                 prev_stderr_mode == INIT_STDIO_TEE ||
                 prev_stderr_mode == INIT_STDIO_LOGSINK) &&
                prev_stderr_index >= 0 &&
                remove_output_binding_ref(table, prev_stderr_mode, prev_stderr_index) != 0) {
                return -1;
            }
            stderr_channel_index = -1;
            continue;
        }
        if (str_eq(dep, "stdin=pipe")) {
            if (input_mode_uses_binding(prev_stdin_mode) &&
                prev_stdin_index >= 0 &&
                clear_input_binding(table, prev_stdin_mode, prev_stdin_index, service_index) != 0) {
                return -1;
            }
            payload = next_token(&cursor, &parse_error);
            if (parse_error || !payload ||
                str_copy(stdin_text, sizeof(stdin_text), payload) != 0) {
                return -1;
            }
            stdin_mode = INIT_STDIO_PIPE;
            continue;
        }
        dep_index = find_service_index(table, dep);
        if (dep_index < 0) {
            return -1;
        }
        deps_mask |= (1ul << (unsigned long)dep_index);
    }
    if (stdin_mode == INIT_STDIO_CAPTURE ||
        stdin_mode == INIT_STDIO_LOGSINK ||
        stdout_mode == INIT_STDIO_PIPE ||
        stderr_mode == INIT_STDIO_PIPE ||
        (stdin_mode == INIT_STDIO_CHANNEL && stdin_channel_index < 0) ||
        (stdin_mode == INIT_STDIO_TEE && stdin_channel_index < 0) ||
        (stdout_mode == INIT_STDIO_CHANNEL && stdout_channel_index < 0) ||
        (stdout_mode == INIT_STDIO_TEE && stdout_channel_index < 0) ||
        (stdout_mode == INIT_STDIO_LOGSINK && stdout_channel_index < 0) ||
        (stderr_mode == INIT_STDIO_CHANNEL && stderr_channel_index < 0) ||
        (stderr_mode == INIT_STDIO_TEE && stderr_channel_index < 0) ||
        (stderr_mode == INIT_STDIO_LOGSINK && stderr_channel_index < 0)) {
        return -1;
    }
    if (!input_mode_uses_binding(stdin_mode) && stdin_channel_index >= 0) {
        return -1;
    }
    if (!output_mode_uses_binding(stdout_mode) && stdout_channel_index >= 0) {
        return -1;
    }
    if (!output_mode_uses_binding(stderr_mode) && stderr_channel_index >= 0) {
        return -1;
    }
    if (is_log_server &&
        (type != INIT_SERVICE_DAEMON || table->log_server_index >= 0 ||
         stdin_mode != INIT_STDIO_INHERIT || stdout_mode != INIT_STDIO_INHERIT ||
         stderr_mode != INIT_STDIO_INHERIT)) {
        return -1;
    }
    /* The log server's sources are pipes created for one particular instance,
     * so a second instance could not take over from the first. */
    if (is_log_server) {
        restart_policy = INIT_RESTART_NEVER;
    }
    /* Shared stream bindings count writers per spawn and are closed after the
     * last one, which a restarted daemon could no longer write to. */
    if (type == INIT_SERVICE_DAEMON && restart_policy != INIT_RESTART_NEVER &&
        (stdin_mode == INIT_STDIO_CHANNEL || stdin_mode == INIT_STDIO_TEE ||
         output_mode_uses_binding(stdout_mode) ||
         output_mode_uses_binding(stderr_mode))) {
        return -1;
    }
    if (stdout_mode == INIT_STDIO_SYSLOG || stderr_mode == INIT_STDIO_SYSLOG) {
        syslog_source_index = add_syslog_source(table, service_index);
        if (syslog_source_index < 0) {
            return -1;
        }
    }
    if (is_log_server) {
        table->log_server_index = service_index;
    }
    svc = &table->services[table->count];
    svc->name = table->names[table->count];
    svc->path = table->paths[table->count];
    svc->provides_mask = 1ul << table->count;
    svc->deps_mask = deps_mask;
    svc->type = type;
    svc->restart_policy = restart_policy;
    svc->restart_delay_ms = restart_delay_ms;
    svc->max_retries = max_retries;
    svc->timeout_ms = timeout_ms;
    svc->stdin_mode = stdin_mode;
    svc->stdout_mode = stdout_mode;
    svc->stderr_mode = stderr_mode;
    svc->stdin_channel_index = stdin_channel_index;
    svc->stdout_channel_index = stdout_channel_index;
    svc->stderr_channel_index = stderr_channel_index;
    svc->syslog_source_index = syslog_source_index;
    svc->log_server = (unsigned char)is_log_server;
    svc->display_owner = (unsigned char)is_display_owner;
    if (str_copy(svc->stdin_text, sizeof(svc->stdin_text), stdin_text) != 0 ||
        str_copy(svc->stdout_text, sizeof(svc->stdout_text), stdout_text) != 0 ||
        str_copy(svc->stderr_text, sizeof(svc->stderr_text), stderr_text) != 0) {
        return -1;
    }
    bind_service_hooks(svc);
    ++table->count;
    return 0;
}

static int parse_config_line(init_service_table_t *table, char *line) {
    char *cursor = skip_space(line);
    if (!cursor || *cursor == 0 || *cursor == '#') {
        return 0;
    }
    if (has_directive(cursor, "service")) {
        return parse_service_line(table, line);
    }
    if (has_directive(cursor, "logsink")) {
        return parse_logsink_line(table, line);
    }
    return -1;
}

static int parse_service_config(init_service_table_t *table, char *buf) {
    char *line = buf;
    unsigned long line_no = 1;
    if (!table || !buf) {
        return -1;
    }
    table->count = 0;
    table->channel_count = 0;
    table->tee_count = 0;
    table->logsink_count = 0;
    table->syslog_source_count = 0;
    table->log_server_index = -1;
    for (unsigned long i = 0; i < INIT_MAX_CHANNELS; ++i) {
        channel_reset(&table->channels[i]);
    }
    for (unsigned long i = 0; i < INIT_MAX_TEES; ++i) {
        tee_reset(&table->tees[i]);
    }
    for (unsigned long i = 0; i < INIT_MAX_LOGSINKS; ++i) {
        logsink_reset(&table->logsinks[i]);
    }
    while (*line) {
        char *next = line;
        while (*next && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = 0;
            ++next;
        }
        if (parse_config_line(table, line) != 0) {
            write_str("init: config parse failed at line ");
            write_u64(line_no);
            write_str(": ");
            write_str(line);
            write(1, "\n", 1);
            table->count = 0;
            return -1;
        }
        line = next;
        line_no++;
    }
    if (validate_syslog_topology(table) != 0) {
        write_str("init: syslog users need an earlier log=server daemon\n");
        table->count = 0;
        return -1;
    }
    return table->count == 0 ? -1 : 0;
}

static int load_service_table_from_text(init_service_table_t *table,
                                        const char *text,
                                        const char *source_label) {
    static char buf[INIT_CONFIG_CAP];
    if (!table || !text) {
        return -1;
    }
    if (str_copy(buf, sizeof(buf), text) != 0) {
        return -1;
    }
    if (parse_service_config(table, buf) != 0) {
        return -1;
    }
    if (source_label) {
        write_str("init: loaded ");
        write_str(source_label);
        write(1, "\n", 1);
    }
    return 0;
}

static int load_service_table(init_service_table_t *table) {
    static char buf[INIT_CONFIG_CAP];
    long fd;
    long got;
    if (!table) {
        return -1;
    }
    fd = open("/init.rc", 0);
    if (fd >= 0) {
        got = read((int)fd, buf, sizeof(buf) - 1);
        close((int)fd);
        if (got > 0 && got < (long)(sizeof(buf) - 1)) {
            buf[got] = 0;
            if (parse_service_config(table, buf) == 0) {
                write(1, "init: loaded /init.rc\n", 22);
                return 0;
            }
            write(1, "init: invalid /init.rc, using built-in config\n", 46);
        } else {
            write(1, "init: unreadable /init.rc, using built-in config\n", 49);
        }
    } else {
        write(1, "init: /init.rc missing, using built-in config\n", 46);
    }
    return load_service_table_from_text(table, default_init_config, "built-in init config");
}

static long spawn_with_runtime(init_service_table_t *table, const init_service_t *svc,
                               init_service_runtime_t *rt) {
    long pid = -1;
    int stdin_fd = SPAWN_STDIO_INHERIT;
    int stdout_fd = SPAWN_STDIO_INHERIT;
    int stderr_fd = SPAWN_STDIO_INHERIT;
    runtime_reset(rt);
    if (!table || !svc || !rt) {
        return -1;
    }
    if (svc->log_server) {
        return spawn_log_server(table, svc);
    }
    if (svc->setup && svc->setup(table, svc, rt) != 0) {
        write_service_prefix(svc, " setup failed\n");
        return -1;
    }
    switch (svc->stdin_mode) {
    case INIT_STDIO_INHERIT:
        stdin_fd = SPAWN_STDIO_INHERIT;
        break;
    case INIT_STDIO_PIPE:
    case INIT_STDIO_TEE:
        if (rt->stdin_read_fd < 0) {
            write_service_prefix(svc, " stdin pipe missing\n");
            return -1;
        }
        stdin_fd = rt->stdin_read_fd;
        break;
    case INIT_STDIO_NULL:
        stdin_fd = SPAWN_STDIO_NULL;
        break;
    case INIT_STDIO_CHANNEL:
        if (ensure_channel_pipe(table, svc->stdin_channel_index) != 0) {
            write_service_prefix(svc, " stdin channel setup failed\n");
            return -1;
        }
        /* Every writer may already have been started or skipped. */
        if (table->channels[svc->stdin_channel_index].writer_spawned_count >=
            table->channels[svc->stdin_channel_index].writer_service_count) {
            close_channel_write(table, svc->stdin_channel_index);
        }
        stdin_fd = table->channels[svc->stdin_channel_index].read_fd;
        break;
    default:
        write_service_prefix(svc, " invalid stdin mode\n");
        return -1;
    }
    switch (svc->stdout_mode) {
    case INIT_STDIO_INHERIT:
        stdout_fd = SPAWN_STDIO_INHERIT;
        break;
    case INIT_STDIO_CAPTURE:
    case INIT_STDIO_TEE:
    case INIT_STDIO_LOGSINK:
        if (rt->stdout_write_fd < 0) {
            write_service_prefix(svc, " stdout capture missing\n");
            return -1;
        }
        stdout_fd = rt->stdout_write_fd;
        break;
    case INIT_STDIO_NULL:
        stdout_fd = SPAWN_STDIO_NULL;
        break;
    case INIT_STDIO_CHANNEL:
        if (ensure_channel_pipe(table, svc->stdout_channel_index) != 0) {
            write_service_prefix(svc, " stdout channel setup failed\n");
            return -1;
        }
        stdout_fd = table->channels[svc->stdout_channel_index].write_fd;
        break;
    case INIT_STDIO_SYSLOG:
        stdout_fd = table->syslog_sources[svc->syslog_source_index].write_fd;
        if (stdout_fd < 0) {
            write_service_prefix(svc, " stdout syslog pipe missing\n");
            return -1;
        }
        break;
    default:
        write_service_prefix(svc, " invalid stdout mode\n");
        return -1;
    }
    switch (svc->stderr_mode) {
    case INIT_STDIO_INHERIT:
        stderr_fd = SPAWN_STDIO_INHERIT;
        break;
    case INIT_STDIO_CAPTURE:
    case INIT_STDIO_TEE:
    case INIT_STDIO_LOGSINK:
        if (rt->stderr_write_fd < 0) {
            write_service_prefix(svc, " stderr capture missing\n");
            return -1;
        }
        stderr_fd = rt->stderr_write_fd;
        break;
    case INIT_STDIO_NULL:
        stderr_fd = SPAWN_STDIO_NULL;
        break;
    case INIT_STDIO_CHANNEL:
        if (ensure_channel_pipe(table, svc->stderr_channel_index) != 0) {
            write_service_prefix(svc, " stderr channel setup failed\n");
            return -1;
        }
        stderr_fd = table->channels[svc->stderr_channel_index].write_fd;
        break;
    case INIT_STDIO_SYSLOG:
        stderr_fd = table->syslog_sources[svc->syslog_source_index].write_fd;
        if (stderr_fd < 0) {
            write_service_prefix(svc, " stderr syslog pipe missing\n");
            return -1;
        }
        break;
    default:
        write_service_prefix(svc, " invalid stderr mode\n");
        return -1;
    }
    if (service_uses_explicit_stdio(svc)) {
        pid = spawn_with_stdio(svc->path, stdin_fd, stdout_fd, stderr_fd);
    } else {
        pid = spawn(svc->path);
    }
    if (pid >= 0) {
        close_runtime_child_stdio(rt);
        if (svc->stdin_mode == INIT_STDIO_CHANNEL) {
            close_channel_read(table, svc->stdin_channel_index);
        }
        if (svc->stdout_mode == INIT_STDIO_CHANNEL) {
            table->channels[svc->stdout_channel_index].writer_spawned_count++;
            if (table->channels[svc->stdout_channel_index].writer_spawned_count >=
                table->channels[svc->stdout_channel_index].writer_service_count) {
                close_channel_write(table, svc->stdout_channel_index);
            }
        }
        if (svc->stderr_mode == INIT_STDIO_CHANNEL) {
            table->channels[svc->stderr_channel_index].writer_spawned_count++;
            if (table->channels[svc->stderr_channel_index].writer_spawned_count >=
                table->channels[svc->stderr_channel_index].writer_service_count) {
                close_channel_write(table, svc->stderr_channel_index);
            }
        }
    }
    return pid;
}

static int service_should_restart(const init_service_t *svc, int success) {
    if (!svc) {
        return 0;
    }
    switch (svc->restart_policy) {
    case INIT_RESTART_NEVER:
        return 0;
    case INIT_RESTART_ON_FAILURE:
        return success ? 0 : 1;
    case INIT_RESTART_ALWAYS:
        return 1;
    default:
        return 0;
    }
}

/* Delay before the Nth consecutive retry: the configured base delay doubled
 * per failure, capped so a large retries= cannot park a service for minutes. */
static unsigned long retry_backoff_ms(const init_service_t *svc,
                                      unsigned long failures) {
    unsigned long delay = svc->restart_delay_ms;
    for (unsigned long i = 1; i < failures && delay < INIT_BACKOFF_CAP_MS; ++i) {
        delay *= 2;
    }
    return delay < INIT_BACKOFF_CAP_MS ? delay : INIT_BACKOFF_CAP_MS;
}

static void release_runtime(init_service_table_t *table, const init_service_t *svc,
                            init_service_runtime_t *rt) {
    if (svc->cleanup) {
        svc->cleanup(table, svc, rt);
    } else {
        close_runtime_fds(rt);
    }
}

static int daemon_may_restart(const init_service_t *svc) {
    return svc->type == INIT_SERVICE_DAEMON && svc->restart_policy != INIT_RESTART_NEVER;
}

/* Starts (or restarts) a daemon instance. Returns 0 once it is running. */
static int daemon_spawn(init_service_table_t *table, unsigned long index) {
    const init_service_t *svc = &table->services[index];
    init_daemon_t *daemon = &table->daemons[index];
    long pid;
    daemon->started_ms = time_ms();
    pid = spawn_with_runtime(table, svc, &daemon->runtime);
    if (pid < 0) {
        write_service_prefix(svc, " spawn failed\n");
        release_runtime(table, svc, &daemon->runtime);
        return -1;
    }
    daemon->pid = pid;
    daemon->phase = INIT_DAEMON_RUNNING;
    return 0;
}

/* The daemon will not run again: let its log source reach end-of-file. */
static void daemon_stop(init_service_table_t *table, unsigned long index) {
    table->daemons[index].phase = INIT_DAEMON_STOPPED;
    close_syslog_source_write(table, &table->services[index]);
}

/*
 * Applies the restart policy after an instance ended: either schedules the
 * next start with the same backoff as run_service, or stops the daemon for
 * good once the policy or the retry budget says so.
 */
static void daemon_run_ended(init_service_table_t *table, unsigned long index,
                             int success) {
    const init_service_t *svc = &table->services[index];
    init_daemon_t *daemon = &table->daemons[index];
    unsigned long delay;
    char note[96];

    if (!service_should_restart(svc, success)) {
        if (!success) {
            ++table->daemon_failed_count;
        }
        daemon_stop(table, index);
        return;
    }
    if (success) {
        daemon->failures = 0;
        delay = svc->restart_delay_ms;
        write_service_prefix(svc, " restart policy requests restart\n");
    } else {
        if (time_ms() - daemon->started_ms >= INIT_DAEMON_STABLE_MS) {
            daemon->failures = 0;
        }
        if (daemon->failures >= svc->max_retries) {
            snprintf(note, sizeof(note), " giving up after %lu failed attempts\n",
                     daemon->failures + 1);
            write_service_prefix(svc, note);
            write_service_prefix(svc, " failed permanently\n");
            ++table->daemon_failed_count;
            daemon_stop(table, index);
            return;
        }
        ++daemon->failures;
        delay = retry_backoff_ms(svc, daemon->failures);
        snprintf(note, sizeof(note), " retry %lu/%lu in %lu ms\n", daemon->failures,
                 svc->max_retries, delay);
        write_service_prefix(svc, note);
    }
    daemon->restart_at_ms = time_ms() + delay;
    daemon->phase = INIT_DAEMON_BACKOFF;
}

/*
 * Reaps daemons that have exited and restarts those whose backoff has run
 * out. Never blocks, so it can be called from any waiting loop in init.
 */
static void daemons_tick(init_service_table_t *table) {
    for (unsigned long i = 0; i < table->count; ++i) {
        const init_service_t *svc = &table->services[i];
        init_daemon_t *daemon = &table->daemons[i];
        if (daemon->phase == INIT_DAEMON_RUNNING) {
            int status = 0;
            int success = 0;
            long rc = try_wait_pid((int)daemon->pid, &status);
            if (rc == 0) {
                continue;
            }
            if (rc < 0) {
                write_service_prefix(svc, " wait failed\n");
            } else {
                write_service_prefix(svc, " exited with status ");
                write_u64((unsigned long)status);
                write(1, "\n", 1);
                success = status == 0;
            }
            release_runtime(table, svc, &daemon->runtime);
            daemon_run_ended(table, i, success);
        } else if (daemon->phase == INIT_DAEMON_BACKOFF &&
                   (long)(time_ms() - daemon->restart_at_ms) >= 0) {
            if (daemon_spawn(table, i) != 0) {
                daemon_run_ended(table, i, 0);
            } else {
                write_service_prefix(svc, " restarted as daemon\n");
            }
        }
    }
}

static unsigned long daemons_active(const init_service_table_t *table) {
    unsigned long active = 0;
    for (unsigned long i = 0; i < table->count; ++i) {
        if (table->daemons[i].phase == INIT_DAEMON_RUNNING ||
            table->daemons[i].phase == INIT_DAEMON_BACKOFF) {
            ++active;
        }
    }
    return active;
}

/* Sleeps like sleep_ms while keeping the daemons supervised. */
static void supervised_sleep(init_service_table_t *table, unsigned long ms) {
    unsigned long start = time_ms();
    for (;;) {
        unsigned long elapsed;
        daemons_tick(table);
        elapsed = time_ms() - start;
        if (elapsed >= ms) {
            return;
        }
        sleep_ms(ms - elapsed < INIT_TICK_MS ? ms - elapsed : INIT_TICK_MS);
    }
}

enum {
    INIT_WAIT_EXITED = 0,
    INIT_WAIT_FAILED = 1,
    INIT_WAIT_TIMED_OUT = 2,
};

/*
 * Waits for a started service to exit while supervising the daemons. A
 * service that outlives its timeout is killed and reaped; its exit status is
 * then that of the kill.
 */
static int wait_for_service(init_service_table_t *table, const init_service_t *svc,
                            long pid, int *status) {
    unsigned long start = time_ms();
    for (;;) {
        long rc = try_wait_pid((int)pid, status);
        if (rc < 0) {
            return INIT_WAIT_FAILED;
        }
        if (rc > 0) {
            return INIT_WAIT_EXITED;
        }
        daemons_tick(table);
        if (svc->timeout_ms != 0 && time_ms() - start >= svc->timeout_ms) {
            if (kill((int)pid, SIGKILL) < 0 || wait_pid((int)pid, status) < 0) {
                return INIT_WAIT_FAILED;
            }
            return INIT_WAIT_TIMED_OUT;
        }
        sleep_ms(INIT_TICK_MS);
    }
}

/*
 * Runs a service until its restart policy stops it. Returns 0 on success and
 * 1 once the service has failed for good, either by policy or because it used
 * up its retries. *spawned is set when at least one instance was started.
 */
static int run_service(init_service_table_t *table, const init_service_t *svc,
                       int *spawned) {
    init_service_runtime_t rt;
    unsigned long failures = 0;
    char note[96];
    runtime_reset(&rt);
    *spawned = 0;
    for (;;) {
        int success = 0;
        long pid = spawn_with_runtime(table, svc, &rt);
        if (pid < 0) {
            write_service_prefix(svc, " spawn failed\n");
        } else {
            int status = 0;
            int outcome;
            *spawned = 1;
            outcome = wait_for_service(table, svc, pid, &status);
            if (outcome == INIT_WAIT_FAILED) {
                write_service_prefix(svc, " wait failed\n");
            } else if (outcome == INIT_WAIT_TIMED_OUT) {
                snprintf(note, sizeof(note), " timed out after %lu ms, killed\n",
                         svc->timeout_ms);
                write_service_prefix(svc, note);
            } else {
                write_service_prefix(svc, " exited with status ");
                write_u64((unsigned long)status);
                write(1, "\n", 1);
                if (status == 0) {
                    success = 1;
                    if (svc->verify && svc->verify(table, svc, &rt, status) != 0) {
                        success = 0;
                    }
                    if (success && verify_completed_logsinks(table) != 0) {
                        success = 0;
                    }
                }
            }
        }
        release_runtime(table, svc, &rt);
        if (!service_should_restart(svc, success)) {
            close_syslog_source_write(table, svc);
            return success ? 0 : 1;
        }
        if (success) {
            failures = 0;
            write_service_prefix(svc, " restart policy requests restart\n");
            if (svc->restart_delay_ms > 0) {
                supervised_sleep(table, svc->restart_delay_ms);
            }
            continue;
        }
        if (failures >= svc->max_retries) {
            snprintf(note, sizeof(note), " giving up after %lu failed attempts\n",
                     failures + 1);
            write_service_prefix(svc, note);
            return 1;
        }
        ++failures;
        unsigned long delay = retry_backoff_ms(svc, failures);
        snprintf(note, sizeof(note), " retry %lu/%lu in %lu ms\n", failures,
                 svc->max_retries, delay);
        write_service_prefix(svc, note);
        if (delay > 0) {
            supervised_sleep(table, delay);
        }
    }
}

/*
 * A service that will never run must not leave a channel reader waiting for
 * an end-of-file: drop its writer reference and close the write end once all
 * remaining writers have been started.
 */
static void release_unspawned_channel_writers(init_service_table_t *table,
                                              const init_service_t *svc) {
    const init_stdio_mode_t modes[2] = { svc->stdout_mode, svc->stderr_mode };
    const int indexes[2] = { svc->stdout_channel_index, svc->stderr_channel_index };
    for (int i = 0; i < 2; ++i) {
        init_channel_t *channel;
        if (modes[i] != INIT_STDIO_CHANNEL ||
            channel_remove_writer_ref(table, indexes[i]) != 0) {
            continue;
        }
        channel = &table->channels[indexes[i]];
        if (channel->writer_spawned_count >= channel->writer_service_count) {
            close_channel_write(table, indexes[i]);
        }
    }
}

static int first_unmet_dependency(const init_service_table_t *table,
                                   const init_service_t *svc,
                                   unsigned long ready_mask) {
    unsigned long missing = svc->deps_mask & ~ready_mask;
    for (unsigned long i = 0; i < table->count; ++i) {
        if (missing & (1ul << i)) {
            return (int)i;
        }
    }
    return -1;
}

int main(void) {
    static init_service_table_t table;
    unsigned long ready_mask = 0;
    unsigned long failed_count = 0;
    unsigned long daemon_count = 0;
    unsigned long display_owner_count = 0;

    if (load_service_table(&table) != 0) {
        write(1, "init: failed to load service config\n", 36);
        return 1;
    }
    write(1, "init: entering service startup\n", 31);
    for (unsigned long i = 0; i < table.count; ++i) {
        const init_service_t *svc = &table.services[i];
        int spawned = 0;
        int missing;
        report_boot_progress(i, table.count);
        missing = first_unmet_dependency(&table, svc, ready_mask);
        if (missing >= 0) {
            char note[96];
            snprintf(note, sizeof(note), " skipped, dependency %s did not start\n",
                     table.services[missing].name);
            write_service_prefix(svc, note);
            release_unspawned_channel_writers(&table, svc);
            ++failed_count;
            continue;
        }
        write_service_prefix(svc, " start\n");
        if (svc->type == INIT_SERVICE_DAEMON) {
            if (daemon_spawn(&table, i) != 0) {
                write_service_prefix(svc, " failed permanently\n");
                release_unspawned_channel_writers(&table, svc);
                ++failed_count;
                continue;
            }
            write_service_prefix(svc, " started as daemon\n");
            /* A daemon that may be restarted keeps writing to the same log
             * pipe, so init holds its write end until the daemon is stopped. */
            if (!daemon_may_restart(svc)) {
                close_syslog_source_write(&table, svc);
            }
            daemon_count++;
            display_owner_count += svc->display_owner;
            ready_mask |= svc->provides_mask;
            continue;
        }
        if (run_service(&table, svc, &spawned) != 0) {
            write_service_prefix(svc, " failed permanently\n");
            if (!spawned) {
                release_unspawned_channel_writers(&table, svc);
            }
            ++failed_count;
            continue;
        }
        ready_mask |= svc->provides_mask;
    }
    report_boot_progress(table.count, table.count);
    if (display_owner_count == 0) {
        /* Nothing is going to take over the screen, so show the console. */
        report_boot("end\n");
    }
    if (failed_count > 0) {
        char note[64];
        int len = snprintf(note, sizeof(note), "init: %lu service(s) failed or skipped\n",
                           failed_count);
        if (len > 0 && (unsigned long)len < sizeof(note)) {
            write(1, note, (unsigned long)len);
        }
    }
    if (daemon_count > 0) {
        unsigned long grace_end_ms = time_ms() + INIT_SPLASH_GRACE_SECONDS * 1000ul;
        int grace_expired = 0;

        /* Keep the session alive while daemons own the console/framebuffer. */
        while (daemons_active(&table) > 0) {
            if (!grace_expired && (long)(time_ms() - grace_end_ms) >= 0) {
                /* A display owner that never presented a frame must not leave
                   the screen on the splash; once it has, this is a no-op. */
                report_boot("end\n");
                grace_expired = 1;
            }
            daemons_tick(&table);
            sleep_ms(INIT_TICK_MS);
        }
    }
    return failed_count > 0 || table.daemon_failed_count > 0 ? 1 : 0;
}
