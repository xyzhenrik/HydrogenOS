#pragma once

#include <stdint.h>

#include "kbd_keys.h"

/*
 * Terminal emulator core: runs /bin/shell behind a pair of pipes, interprets its
 * output into a scrollback buffer and does the line editing that a tty would
 * do. It knows nothing about windows; the desktop draws the frame it renders.
 * The protocol between terminal and shell is described in docs/TERMINAL.md.
 *
 * The buffer holds logical lines and wraps them when rendering, so a resize
 * reflows all of the scrollback.
 */

enum {
    TERMINAL_COLS_MAX = 256,
    TERMINAL_ROWS_MAX = 128,
    TERMINAL_REQUEST_ARGUMENT_MAX = 256,
};

/* Keys beyond plain characters, as sys_kbd_read() reports them (see
 * kbd_keys.h) with the modifier flags removed. Plain characters and control
 * characters (Ctrl+C is 3) come from the same call. */
enum {
    TERMINAL_KEY_LEFT = KBD_KEY_LEFT,
    TERMINAL_KEY_RIGHT = KBD_KEY_RIGHT,
    TERMINAL_KEY_UP = KBD_KEY_UP,
    TERMINAL_KEY_DOWN = KBD_KEY_DOWN,
    TERMINAL_KEY_HOME = KBD_KEY_HOME,
    TERMINAL_KEY_END = KBD_KEY_END,
    TERMINAL_KEY_DELETE = KBD_KEY_DELETE,
    TERMINAL_KEY_PAGE_UP = KBD_KEY_PAGE_UP,
    TERMINAL_KEY_PAGE_DOWN = KBD_KEY_PAGE_DOWN,
};

/* Colour 0 is the default text colour; 1-8 are the ANSI colours black, red,
 * green, yellow, blue, magenta, cyan and white; 9-16 their bright variants. */
enum { TERMINAL_COLORS = 17 };

typedef struct TerminalCell {
    uint8_t ch;
    uint8_t color;
} TerminalCell;

/* What the window has to show: rows of cols cells, row-major. The cursor is
 * (-1, -1) when it is scrolled out of view or the shell is gone. */
typedef struct TerminalFrame {
    int cols;
    int rows;
    int cursor_col;
    int cursor_row;
    TerminalCell cells[TERMINAL_ROWS_MAX * TERMINAL_COLS_MAX];
} TerminalFrame;

typedef enum TerminalRequestKind {
    TERMINAL_REQUEST_EDITOR,
    TERMINAL_REQUEST_FILES,
} TerminalRequestKind;

/* Something a program asked the desktop to do. An empty argument means none. */
typedef struct TerminalRequest {
    TerminalRequestKind kind;
    char argument[TERMINAL_REQUEST_ARGUMENT_MAX];
} TerminalRequest;

/* Results of terminal_pump(). */
enum {
    TERMINAL_EVENT_OUTPUT = 1u << 0,
    TERMINAL_EVENT_EXITED = 1u << 1,
};

typedef struct Terminal Terminal;

/* The terminal is a large allocation; both can fail with a null/negative
 * result. terminal_start() launches the shell and may be repeated after it
 * exited. */
Terminal *terminal_create(int cols, int rows);
void terminal_destroy(Terminal *terminal);
int terminal_start(Terminal *terminal);

/* Moves shell output into the buffer, forwards queued input and notices the
 * shell exiting. Never blocks. */
unsigned terminal_pump(Terminal *terminal);

/* Interprets bytes as program output, as if the shell had written them. */
void terminal_feed(Terminal *terminal, const char *data, int length);

void terminal_key(Terminal *terminal, int key);

/* Positive rows scroll towards older output. */
void terminal_scroll(Terminal *terminal, int rows);
void terminal_resize(Terminal *terminal, int cols, int rows);

const TerminalFrame *terminal_render(Terminal *terminal);

/* Text of one frame row without trailing blanks. Returns its length. */
int terminal_frame_text(const TerminalFrame *frame, int row, char *out, int cap);

/* True while the shell waits at its prompt; alive is true from a successful
 * terminal_start() until the shell exits. */
int terminal_idle(const Terminal *terminal);
int terminal_alive(const Terminal *terminal);
int terminal_exit_status(const Terminal *terminal);

/* Pops the oldest pending request; returns 0 when there is none. */
int terminal_take_request(Terminal *terminal, TerminalRequest *out);
