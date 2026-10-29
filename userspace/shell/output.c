#include "output.h"

#include "mini_libc.h"

enum {
    OUTPUT_BUFFER_CAP = 512,
    TTY_COLOR_BLACK = 0,
    TTY_COLOR_RED = 12,
    TTY_COLOR_WHITE = 15,
};

static char buffer[OUTPUT_BUFFER_CAP];
static size_t buffer_len = 0;
static int buffer_fd = -1;
static int color_enabled = 0;

static void write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        long wrote = write(fd, buf, len);
        if (wrote <= 0)
            return;
        buf += wrote;
        len -= (size_t)wrote;
    }
}

void out_flush(void) {
    if (buffer_len > 0)
        write_all(buffer_fd, buffer, buffer_len);
    buffer_len = 0;
}

void out_write(int fd, const char *buf, size_t len) {
    if (!buf)
        return;
    if (fd != buffer_fd) {
        out_flush();
        buffer_fd = fd;
    }
    while (len > 0) {
        size_t room = OUTPUT_BUFFER_CAP - buffer_len;
        size_t take = len < room ? len : room;
        memcpy(buffer + buffer_len, buf, take);
        buffer_len += take;
        buf += take;
        len -= take;
        if (buffer_len == OUTPUT_BUFFER_CAP)
            out_flush();
    }
}

void out_str(int fd, const char *s) {
    if (s)
        out_write(fd, s, strlen(s));
}

void out_char(int fd, char c) { out_write(fd, &c, 1); }

void out_u64(int fd, unsigned long value) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0);
    while (count > 0)
        out_char(fd, digits[--count]);
}

void out_error_to(int fd, const char *prefix, const char *subject,
                  const char *message) {
    if (fd < 0)
        return;
    out_flush();
    if (color_enabled)
        tty_set_color(TTY_COLOR_RED, TTY_COLOR_BLACK);
    out_str(fd, prefix);
    if (subject) {
        out_str(fd, ": ");
        out_str(fd, subject);
    }
    out_str(fd, ": ");
    out_str(fd, message);
    out_char(fd, '\n');
    out_flush();
    if (color_enabled)
        tty_set_color(TTY_COLOR_WHITE, TTY_COLOR_BLACK);
}

void out_error(const char *prefix, const char *subject, const char *message) {
    out_error_to(2, prefix, subject, message);
}

void out_set_color(int enabled) { color_enabled = enabled; }
