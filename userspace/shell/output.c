#include "output.h"

#include "mini_libc.h"

enum {
    OUTPUT_BUFFER_CAP = 512,
    TTY_COLOR_BLACK = 0,
    TTY_COLOR_RED = 12,
    TTY_COLOR_WHITE = 15,
};

static const char ansi_error[] = "\x1b[91m";
static const char ansi_reset[] = "\x1b[0m";

static char buffer[OUTPUT_BUFFER_CAP];
static size_t buffer_len = 0;
static int buffer_fd = -1;
static OutColor color_mode = OUT_COLOR_NONE;
static int color_paused = 0;

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

/* Console tinting never reaches a stream. ANSI sequences do, so they only go
 * to the shell's own stderr: a message for a child's stderr (a pipe or file)
 * must stay plain text. */
static int tint_enabled(int fd) {
    if (color_mode == OUT_COLOR_ANSI)
        return fd == 2 && !color_paused;
    return color_mode == OUT_COLOR_CONSOLE;
}

void out_error_to(int fd, const char *prefix, const char *subject,
                  const char *message) {
    int tint;
    if (fd < 0)
        return;
    out_flush();
    tint = tint_enabled(fd);
    if (tint && color_mode == OUT_COLOR_CONSOLE)
        tty_set_color(TTY_COLOR_RED, TTY_COLOR_BLACK);
    else if (tint)
        out_str(fd, ansi_error);
    out_str(fd, prefix);
    if (subject) {
        out_str(fd, ": ");
        out_str(fd, subject);
    }
    out_str(fd, ": ");
    out_str(fd, message);
    if (tint && color_mode == OUT_COLOR_ANSI)
        out_str(fd, ansi_reset);
    out_char(fd, '\n');
    out_flush();
    if (tint && color_mode == OUT_COLOR_CONSOLE)
        tty_set_color(TTY_COLOR_WHITE, TTY_COLOR_BLACK);
}

void out_error(const char *prefix, const char *subject, const char *message) {
    out_error_to(2, prefix, subject, message);
}

void out_set_color(OutColor mode) { color_mode = mode; }

void out_pause_color(int paused) { color_paused = paused; }
