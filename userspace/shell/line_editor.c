#include "line_editor.h"

#include "mini_libc.h"
#include "shell_limits.h"

enum {
    HISTORY_CAP = 16,
    TTY_COLOR_BLACK = 0,
    TTY_COLOR_BLUE = 9,
    TTY_COLOR_WHITE = 15,
    KEY_ESCAPE = 27,
    KEY_DELETE = 127,
};

static const char prompt[] = "hydrogen$ ";

static char history[HISTORY_CAP][SHELL_LINE_MAX];
static int history_count = 0;
static int history_next = 0;

static void write_char(char c) { write(1, &c, 1); }

static void write_prompt(void) {
    tty_set_color(TTY_COLOR_BLUE, TTY_COLOR_BLACK);
    write(1, prompt, sizeof(prompt) - 1);
    tty_set_color(TTY_COLOR_WHITE, TTY_COLOR_BLACK);
}

static void read_char(char *out) {
    while (read(0, out, 1) <= 0)
        yield_cpu();
}

static void redraw_line(const char *buf, int len, int cursor) {
    write_char('\r');
    write_prompt();
    if (len > 0)
        write(1, buf, (size_t)len);
    write_char(' ');
    write_char('\r');
    write_prompt();
    if (cursor > 0)
        write(1, buf, (size_t)cursor);
}

static void copy_line(char *dst, const char *src, int cap) {
    int i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int lines_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void history_push(const char *line) {
    if (!*line)
        return;
    if (history_count > 0 &&
        lines_equal(history[(history_next + HISTORY_CAP - 1) % HISTORY_CAP], line))
        return;
    copy_line(history[history_next], line, SHELL_LINE_MAX);
    history_next = (history_next + 1) % HISTORY_CAP;
    if (history_count < HISTORY_CAP)
        history_count++;
}

static const char *history_get(int index) {
    int oldest = (history_next - history_count + HISTORY_CAP) % HISTORY_CAP;
    return history[(oldest + index) % HISTORY_CAP];
}

int line_editor_read(char *buf, int cap) {
    int len = 0;
    int cursor = 0;
    int history_index = history_count;
    char draft[SHELL_LINE_MAX];
    draft[0] = 0;
    buf[0] = 0;
    write_prompt();
    for (;;) {
        char c = 0;
        read_char(&c);
        if (c == '\r' || c == '\n') {
            write_char('\n');
            buf[len] = 0;
            history_push(buf);
            return len;
        }
        if (c == KEY_ESCAPE) {
            char seq = 0;
            read_char(&seq);
            if (seq != '[')
                continue;
            read_char(&seq);
            if (seq == 'D' && cursor > 0) {
                cursor--;
            } else if (seq == 'C' && cursor < len) {
                cursor++;
            } else if (seq == 'A' && history_index > 0) {
                if (history_index == history_count)
                    copy_line(draft, buf, SHELL_LINE_MAX);
                history_index--;
                copy_line(buf, history_get(history_index), cap);
                len = cursor = (int)strlen(buf);
            } else if (seq == 'B' && history_index < history_count) {
                history_index++;
                copy_line(buf, history_index == history_count
                                   ? draft
                                   : history_get(history_index),
                          cap);
                len = cursor = (int)strlen(buf);
            } else if (seq == 'H') {
                cursor = 0;
            } else if (seq == 'F') {
                cursor = len;
            } else if (seq == '3') {
                read_char(&seq);
                if (seq != '~' || cursor >= len)
                    continue;
                for (int i = cursor; i + 1 < len; ++i)
                    buf[i] = buf[i + 1];
                buf[--len] = 0;
            } else {
                continue;
            }
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c == '\b' || c == KEY_DELETE) {
            if (cursor == 0)
                continue;
            for (int i = cursor - 1; i + 1 < len; ++i)
                buf[i] = buf[i + 1];
            buf[--len] = 0;
            cursor--;
            redraw_line(buf, len, cursor);
            continue;
        }
        if ((unsigned char)c >= 0x20 && len + 1 < cap) {
            for (int i = len; i > cursor; --i)
                buf[i] = buf[i - 1];
            buf[cursor++] = c;
            buf[++len] = 0;
            history_index = history_count;
            redraw_line(buf, len, cursor);
        }
    }
}
