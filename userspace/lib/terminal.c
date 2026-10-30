#include "terminal.h"

#include "builtin_names.h"
#include "mini_libc.h"
#include "signal.h"

enum {
    LINE_CAP = 256,
    SCROLLBACK_LINES = 2000,
    INPUT_MAX = 1000,
    HISTORY_ENTRIES = 64,
    OUTPUT_CHUNK = 512,
    OUTPUT_CHUNKS_PER_PUMP = 32,
    INPUT_QUEUE_CAP = 4096,
    OSC_CAP = 300,
    PARAMS_MAX = 8,
    TAB_WIDTH = 8,
    CWD_CAP = 256,
    REQUEST_QUEUE_CAP = 4,
    CANDIDATES_MAX = 256,
    CANDIDATE_BYTES = 16384,
    CANDIDATE_GAP = 2,
    ENTRY_TYPE_DIRECTORY = 4,
    EXIT_STATUS_LOST = 127,
    SHUTDOWN_GRACE_MS = 500,
    SHUTDOWN_POLL_MS = 5,
    KEY_CTRL_A = 1,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_CTRL_K = 11,
    KEY_CTRL_L = 12,
    KEY_CTRL_U = 21,
    KEY_CTRL_W = 23,
    KEY_BACKSPACE = 0x7F,
};

#define SHELL_PATH "/bin/shell"
#define SHELL_START_DIRECTORY "/home/user"
#define PROMPT_CWD_DEFAULT "/"

typedef struct Line {
    uint16_t length;
    TerminalCell cells[LINE_CAP];
} Line;

typedef enum ParseState {
    PARSE_TEXT,
    PARSE_ESCAPE,
    PARSE_CSI,
    PARSE_OSC,
    PARSE_OSC_ESCAPE,
} ParseState;

typedef struct Candidate {
    uint16_t offset;
    uint8_t is_directory;
} Candidate;

struct Terminal {
    /* Scrollback: absolute line numbers grow forever, a line lives in slot
     * number % SCROLLBACK_LINES. The last line is first + count - 1. */
    unsigned long first;
    unsigned long count;
    /* Lines before page_start were cleared off the screen and are only
     * reachable by scrolling back. */
    unsigned long page_start;
    unsigned long cursor_line;
    int cursor_col;
    int scroll_offset;
    int cols;
    int rows;

    ParseState parse_state;
    int params[PARAMS_MAX];
    int param_count;
    int param_open;
    int csi_ignored;
    char osc[OSC_CAP];
    int osc_length;
    int foreground;
    int bold;

    long shell_pid;
    int input_fd;
    int output_fd;
    int exit_status;
    int idle;
    char input_queue[INPUT_QUEUE_CAP];
    int input_queue_length;
    char cwd[CWD_CAP];
    TerminalCell prompt[LINE_CAP];
    int prompt_length;

    char edit[INPUT_MAX + 1];
    int edit_length;
    int edit_cursor;
    char history[HISTORY_ENTRIES][INPUT_MAX + 1];
    int history_count;
    int history_head;
    int history_position;
    char draft[INPUT_MAX + 1];

    TerminalRequest requests[REQUEST_QUEUE_CAP];
    int request_count;

    char candidate_names[CANDIDATE_BYTES];
    int candidate_names_used;
    Candidate candidates[CANDIDATES_MAX];
    int candidate_count;

    TerminalFrame frame;

    /* Last, so that terminal_create() clears everything before it and leaves
     * the lines alone: each one is initialised when it is appended. */
    Line lines[SCROLLBACK_LINES];
};

/* ---------------------------------------------------------------- buffer */

static Line *line_at(Terminal *t, unsigned long number) {
    return &t->lines[number % SCROLLBACK_LINES];
}

static unsigned long last_line(const Terminal *t) {
    return t->first + t->count - 1;
}

static int overlay_visible(const Terminal *t) { return t->shell_pid != 0; }

/* Appends an empty line, dropping the oldest one when the buffer is full. */
static void append_line(Terminal *t) {
    if (t->count == SCROLLBACK_LINES) {
        t->first++;
        if (t->page_start < t->first)
            t->page_start = t->first;
        if (t->cursor_line < t->first)
            t->cursor_line = t->first;
    } else {
        t->count++;
    }
    line_at(t, last_line(t))->length = 0;
    /* Keep a scrolled-back view on the same text while output arrives. */
    if (t->scroll_offset > 0)
        t->scroll_offset++;
}

static void newline(Terminal *t) {
    t->cursor_col = 0;
    if (t->cursor_line == last_line(t))
        append_line(t);
    t->cursor_line++;
}

static void put_cell(Terminal *t, uint8_t ch, uint8_t color) {
    Line *line;
    if (t->cursor_col >= LINE_CAP)
        newline(t);
    line = line_at(t, t->cursor_line);
    while (line->length < t->cursor_col) {
        line->cells[line->length].ch = ' ';
        line->cells[line->length].color = 0;
        line->length++;
    }
    line->cells[t->cursor_col].ch = ch;
    line->cells[t->cursor_col].color = color;
    if (t->cursor_col == line->length)
        line->length++;
    t->cursor_col++;
}

static uint8_t current_color(const Terminal *t) {
    if (t->bold && t->foreground >= 1 && t->foreground <= 8)
        return (uint8_t)(t->foreground + 8);
    return (uint8_t)t->foreground;
}

/* Writes plain program text in the default colour, as echo of the user's own
 * typing and the terminal's notices are. */
static void put_text(Terminal *t, const char *text) {
    for (; *text; ++text) {
        if (*text == '\n')
            newline(t);
        else
            put_cell(t, (uint8_t)*text, 0);
    }
}

static void erase_in_line(Terminal *t, int mode) {
    Line *line = line_at(t, t->cursor_line);
    if (mode == 0) {
        if (t->cursor_col < line->length)
            line->length = (uint16_t)t->cursor_col;
    } else if (mode == 1) {
        for (int i = 0; i <= t->cursor_col && i < line->length; ++i) {
            line->cells[i].ch = ' ';
            line->cells[i].color = 0;
        }
    } else if (mode == 2) {
        line->length = 0;
    }
}

/* Clearing the screen keeps the old content as scrollback: the cleared page
 * simply starts at the cursor line. */
static void erase_in_screen(Terminal *t, int mode) {
    if (mode == 0) {
        erase_in_line(t, 0);
        t->count = t->cursor_line - t->first + 1;
    } else if (mode == 2 || mode == 3) {
        line_at(t, t->cursor_line)->length = 0;
        t->count = t->cursor_line - t->first + 1;
        t->page_start = t->cursor_line;
        t->cursor_col = 0;
    }
}

static void move_cursor_to(Terminal *t, int row, int col) {
    unsigned long target = t->page_start + (unsigned long)(row > 0 ? row - 1 : 0);
    while (target > last_line(t))
        append_line(t);
    t->cursor_line = target;
    t->cursor_col = col > 0 ? col - 1 : 0;
    if (t->cursor_col >= LINE_CAP)
        t->cursor_col = LINE_CAP - 1;
}

/* ---------------------------------------------------------------- parsing */

static int param_or(const Terminal *t, int index, int fallback) {
    if (index >= t->param_count || t->params[index] < 0)
        return fallback;
    return t->params[index];
}

static void select_graphic_rendition(Terminal *t) {
    if (t->param_count == 0) {
        t->foreground = 0;
        t->bold = 0;
        return;
    }
    for (int i = 0; i < t->param_count; ++i) {
        int code = t->params[i] < 0 ? 0 : t->params[i];
        if (code == 0) {
            t->foreground = 0;
            t->bold = 0;
        } else if (code == 1) {
            t->bold = 1;
        } else if (code == 22) {
            t->bold = 0;
        } else if (code >= 30 && code <= 37) {
            t->foreground = 1 + code - 30;
        } else if (code == 39) {
            t->foreground = 0;
        } else if (code >= 90 && code <= 97) {
            t->foreground = 9 + code - 90;
        }
    }
}

static void dispatch_csi(Terminal *t, char final) {
    int n;
    if (t->csi_ignored)
        return;
    switch (final) {
    case 'm':
        select_graphic_rendition(t);
        break;
    case 'H':
    case 'f':
        move_cursor_to(t, param_or(t, 0, 1), param_or(t, 1, 1));
        break;
    case 'J':
        erase_in_screen(t, param_or(t, 0, 0));
        break;
    case 'K':
        erase_in_line(t, param_or(t, 0, 0));
        break;
    case 'A':
        n = param_or(t, 0, 1);
        if (n < 1)
            n = 1;
        t->cursor_line = t->cursor_line - t->page_start > (unsigned long)n
                             ? t->cursor_line - (unsigned long)n
                             : t->page_start;
        break;
    case 'B':
        n = param_or(t, 0, 1);
        for (int i = 0; i < (n < 1 ? 1 : n); ++i) {
            if (t->cursor_line == last_line(t))
                append_line(t);
            t->cursor_line++;
        }
        break;
    case 'C':
        n = param_or(t, 0, 1);
        t->cursor_col += n < 1 ? 1 : n;
        if (t->cursor_col >= LINE_CAP)
            t->cursor_col = LINE_CAP - 1;
        break;
    case 'D':
        n = param_or(t, 0, 1);
        t->cursor_col -= n < 1 ? 1 : n;
        if (t->cursor_col < 0)
            t->cursor_col = 0;
        break;
    default:
        break;
    }
}

static void mark_prompt(Terminal *t) {
    const Line *line = line_at(t, t->cursor_line);
    t->idle = 1;
    t->prompt_length = line->length;
    for (int i = 0; i < line->length; ++i)
        t->prompt[i] = line->cells[i];
}

static void queue_request(Terminal *t, TerminalRequestKind kind,
                          const char *argument) {
    TerminalRequest *request;
    int length = (int)strlen(argument);
    if (t->request_count == REQUEST_QUEUE_CAP || length >= TERMINAL_REQUEST_ARGUMENT_MAX)
        return;
    request = &t->requests[t->request_count++];
    request->kind = kind;
    memcpy(request->argument, argument, (size_t)length + 1);
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

/* Operating system commands: "7;path" reports the shell's working directory,
 * "133;B" marks the end of the prompt, "7770;..." carries desktop requests. */
static void dispatch_osc(Terminal *t) {
    const char *text = t->osc;
    t->osc[t->osc_length] = 0;
    if (starts_with(text, "7;")) {
        int length = (int)strlen(text + 2);
        if (length < CWD_CAP)
            memcpy(t->cwd, text + 2, (size_t)length + 1);
    } else if (starts_with(text, "133;B")) {
        mark_prompt(t);
    } else if (starts_with(text, "7770;edit;")) {
        queue_request(t, TERMINAL_REQUEST_EDITOR, text + 10);
    } else if (starts_with(text, "7770;files")) {
        queue_request(t, TERMINAL_REQUEST_FILES, "");
    }
}

static void begin_sequence(Terminal *t, ParseState state) {
    t->parse_state = state;
    t->param_count = 0;
    t->param_open = 0;
    t->csi_ignored = 0;
    t->osc_length = 0;
}

static void feed_csi(Terminal *t, uint8_t c) {
    if (c >= '0' && c <= '9') {
        if (!t->param_open) {
            if (t->param_count == PARAMS_MAX) {
                t->csi_ignored = 1;
                return;
            }
            t->params[t->param_count++] = 0;
            t->param_open = 1;
        }
        if (t->params[t->param_count - 1] < 10000)
            t->params[t->param_count - 1] = t->params[t->param_count - 1] * 10 + (c - '0');
    } else if (c == ';') {
        if (!t->param_open) {
            if (t->param_count == PARAMS_MAX) {
                t->csi_ignored = 1;
                return;
            }
            t->params[t->param_count++] = -1;
        }
        t->param_open = 0;
    } else if (c >= 0x40 && c <= 0x7E) {
        dispatch_csi(t, (char)c);
        t->parse_state = PARSE_TEXT;
    } else if (c >= 0x20 && c <= 0x3F) {
        /* Private markers and intermediates: sequences we do not model. */
        t->csi_ignored = 1;
    } else {
        t->parse_state = PARSE_TEXT;
    }
}

static void feed_text(Terminal *t, uint8_t c) {
    switch (c) {
    case 0x1B:
        t->parse_state = PARSE_ESCAPE;
        break;
    case '\n':
        newline(t);
        break;
    case '\r':
        t->cursor_col = 0;
        break;
    case '\b':
        if (t->cursor_col > 0)
            t->cursor_col--;
        break;
    case '\t': {
        int spaces = TAB_WIDTH - t->cursor_col % TAB_WIDTH;
        for (int i = 0; i < spaces; ++i)
            put_cell(t, ' ', current_color(t));
        break;
    }
    default:
        if (c >= 0x20 && c != KEY_BACKSPACE)
            put_cell(t, c, current_color(t));
        break;
    }
}

static void feed_byte(Terminal *t, uint8_t c) {
    switch (t->parse_state) {
    case PARSE_TEXT:
        feed_text(t, c);
        break;
    case PARSE_ESCAPE:
        if (c == '[')
            begin_sequence(t, PARSE_CSI);
        else if (c == ']')
            begin_sequence(t, PARSE_OSC);
        else
            t->parse_state = PARSE_TEXT;
        break;
    case PARSE_CSI:
        feed_csi(t, c);
        break;
    case PARSE_OSC:
        if (c == '\a') {
            dispatch_osc(t);
            t->parse_state = PARSE_TEXT;
        } else if (c == 0x1B) {
            t->parse_state = PARSE_OSC_ESCAPE;
        } else if (t->osc_length < OSC_CAP - 1) {
            t->osc[t->osc_length++] = (char)c;
        }
        break;
    case PARSE_OSC_ESCAPE:
        if (c == '\\')
            dispatch_osc(t);
        t->parse_state = PARSE_TEXT;
        break;
    }
}

void terminal_feed(Terminal *t, const char *data, int length) {
    for (int i = 0; i < length; ++i)
        feed_byte(t, (uint8_t)data[i]);
}

/* ------------------------------------------------------------ shell link */

static int exit_code(int wait_status) {
    if (WIFSIGNALED(wait_status))
        return 128 + WTERMSIG(wait_status);
    return wait_status;
}

static void close_descriptor(int *fd) {
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}

static void release_shell(Terminal *t) {
    close_descriptor(&t->input_fd);
    close_descriptor(&t->output_fd);
    t->shell_pid = 0;
    t->idle = 0;
    t->input_queue_length = 0;
}

static void flush_input(Terminal *t) {
    while (t->input_queue_length > 0 && t->input_fd >= 0 &&
           (poll_fd(t->input_fd, POLL_WRITE) & POLL_WRITE)) {
        long written = write(t->input_fd, t->input_queue, (size_t)t->input_queue_length);
        if (written <= 0) {
            t->input_queue_length = 0;
            return;
        }
        memmove(t->input_queue, t->input_queue + written,
                (size_t)(t->input_queue_length - written));
        t->input_queue_length -= (int)written;
    }
}

static void queue_input(Terminal *t, const char *data, int length) {
    if (t->shell_pid == 0 || t->input_fd < 0)
        return;
    if (t->input_queue_length + length > INPUT_QUEUE_CAP)
        return;
    memcpy(t->input_queue + t->input_queue_length, data, (size_t)length);
    t->input_queue_length += length;
    flush_input(t);
}

static unsigned drain_output(Terminal *t) {
    unsigned events = 0;
    char chunk[OUTPUT_CHUNK];
    for (int i = 0; i < OUTPUT_CHUNKS_PER_PUMP && t->output_fd >= 0; ++i) {
        long n;
        if (!(poll_fd(t->output_fd, POLL_READ) & POLL_READ))
            break;
        n = read(t->output_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        terminal_feed(t, chunk, (int)n);
        events |= TERMINAL_EVENT_OUTPUT;
    }
    return events;
}

static void announce_exit(Terminal *t) {
    char text[64];
    if (t->cursor_col > 0)
        newline(t);
    if (t->exit_status == 0)
        return;
    snprintf(text, sizeof(text), "[Shell exited with status %d. Press Enter to restart.]\n",
             t->exit_status);
    put_text(t, text);
}

unsigned terminal_pump(Terminal *t) {
    unsigned events;
    int status = 0;
    long rc;
    if (t->shell_pid == 0)
        return 0;
    flush_input(t);
    events = drain_output(t);
    rc = try_wait_pid((int)t->shell_pid, &status);
    if (rc == 0)
        return events;
    /* The shell may have written its last words just before it exited. */
    events |= drain_output(t);
    t->exit_status = rc > 0 ? exit_code(status) : EXIT_STATUS_LOST;
    release_shell(t);
    announce_exit(t);
    return events | TERMINAL_EVENT_OUTPUT | TERMINAL_EVENT_EXITED;
}

int terminal_start(Terminal *t) {
    static const char *const argv[] = {"shell", "-i", 0};
    static const char *const envp[] = {"PATH=/bin:/sbin", "HOME=/home/user", 0};
    char saved_directory[CWD_CAP];
    int input_pipe[2];
    int output_pipe[2];
    long pid;

    if (t->shell_pid != 0)
        return 0;
    if (pipe(input_pipe) != 0)
        return -1;
    if (pipe(output_pipe) != 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        return -1;
    }
    /* The shell starts in the directory this process is in at that moment;
     * the desktop's own working directory is not the user's. */
    if (!getcwd(saved_directory, sizeof(saved_directory)))
        saved_directory[0] = 0;
    chdir(SHELL_START_DIRECTORY);
    pid = spawn_args(SHELL_PATH, argv, envp, input_pipe[0], output_pipe[1],
                     output_pipe[1]);
    if (saved_directory[0])
        chdir(saved_directory);
    close(input_pipe[0]);
    close(output_pipe[1]);
    if (pid <= 0) {
        close(input_pipe[1]);
        close(output_pipe[0]);
        t->exit_status = EXIT_STATUS_LOST;
        if (t->cursor_col > 0)
            newline(t);
        put_text(t, "[Cannot start /bin/shell. Press Enter to try again.]\n");
        return -1;
    }
    t->shell_pid = pid;
    t->input_fd = input_pipe[1];
    t->output_fd = output_pipe[0];
    t->idle = 0;
    memcpy(t->cwd, PROMPT_CWD_DEFAULT, sizeof(PROMPT_CWD_DEFAULT));
    return 0;
}

Terminal *terminal_create(int cols, int rows) {
    Terminal *t = (Terminal *)malloc(sizeof(Terminal));
    if (!t)
        return 0;
    memset(t, 0, offsetof(Terminal, lines));
    t->count = 1;
    t->lines[0].length = 0;
    t->input_fd = -1;
    t->output_fd = -1;
    t->history_position = -1;
    memcpy(t->cwd, PROMPT_CWD_DEFAULT, sizeof(PROMPT_CWD_DEFAULT));
    terminal_resize(t, cols, rows);
    return t;
}

/* Closing the window asks the shell to wind down like a hangup would: the
 * foreground command is interrupted, and the shell ends itself at its next
 * read from the closed input, taking its background jobs along. Only a shell
 * that does not oblige is killed. */
static void stop_shell(Terminal *t) {
    unsigned long deadline = time_ms() + SHUTDOWN_GRACE_MS;
    int status = 0;
    kill((int)t->shell_pid, SIGINT);
    close_descriptor(&t->input_fd);
    close_descriptor(&t->output_fd);
    while (try_wait_pid((int)t->shell_pid, &status) == 0) {
        if (time_ms() >= deadline) {
            kill((int)t->shell_pid, SIGKILL);
            wait_pid((int)t->shell_pid, &status);
            break;
        }
        sleep_ms(SHUTDOWN_POLL_MS);
    }
    t->shell_pid = 0;
}

void terminal_destroy(Terminal *t) {
    if (!t)
        return;
    if (t->shell_pid != 0)
        stop_shell(t);
    free(t);
}

int terminal_idle(const Terminal *t) { return t->idle; }
int terminal_alive(const Terminal *t) { return t->shell_pid != 0; }
int terminal_exit_status(const Terminal *t) { return t->exit_status; }

int terminal_take_request(Terminal *t, TerminalRequest *out) {
    if (t->request_count == 0)
        return 0;
    *out = t->requests[0];
    t->request_count--;
    memmove(&t->requests[0], &t->requests[1],
            (size_t)t->request_count * sizeof(TerminalRequest));
    return 1;
}

/* --------------------------------------------------------------- viewing */

static int line_rows(const Terminal *t, unsigned long number) {
    int cells = t->lines[number % SCROLLBACK_LINES].length;
    int rows;
    if (number == t->cursor_line && overlay_visible(t))
        cells += t->edit_length + 1;
    rows = (cells + t->cols - 1) / t->cols;
    return rows < 1 ? 1 : rows;
}

typedef struct Layout {
    long total;
    long document;
    long max_offset;
} Layout;

/* The document is every line wrapped to the window width, followed by blank
 * rows when the cleared page is shorter than the window. */
static Layout measure(const Terminal *t) {
    Layout layout;
    long page_start_row = 0;
    long total = 0;
    long page_rows;
    long padding;
    for (unsigned long n = t->first; n <= last_line(t); ++n) {
        if (n == t->page_start)
            page_start_row = total;
        total += line_rows(t, n);
    }
    page_rows = total - page_start_row;
    padding = page_rows < t->rows ? t->rows - page_rows : 0;
    layout.total = total;
    layout.document = total + padding;
    layout.max_offset = layout.document > t->rows ? layout.document - t->rows : 0;
    return layout;
}

void terminal_scroll(Terminal *t, int rows) {
    long offset = (long)t->scroll_offset + rows;
    long max_offset = measure(t).max_offset;
    if (offset < 0)
        offset = 0;
    if (offset > max_offset)
        offset = max_offset;
    t->scroll_offset = (int)offset;
}

void terminal_resize(Terminal *t, int cols, int rows) {
    cols = cols < 1 ? 1 : (cols > TERMINAL_COLS_MAX ? TERMINAL_COLS_MAX : cols);
    rows = rows < 1 ? 1 : (rows > TERMINAL_ROWS_MAX ? TERMINAL_ROWS_MAX : rows);
    if (cols == t->cols && rows == t->rows)
        return;
    t->cols = cols;
    t->rows = rows;
    terminal_scroll(t, 0);
}

static void fill_row(Terminal *t, TerminalCell *row, unsigned long number, int sub_row) {
    const Line *line = &t->lines[number % SCROLLBACK_LINES];
    int overlay = number == t->cursor_line && overlay_visible(t);
    for (int col = 0; col < t->cols; ++col) {
        int index = sub_row * t->cols + col;
        row[col].ch = ' ';
        row[col].color = 0;
        if (index < line->length) {
            if (line->cells[index].ch)
                row[col] = line->cells[index];
        } else if (overlay && index < line->length + t->edit_length) {
            row[col].ch = (uint8_t)t->edit[index - line->length];
        }
    }
}

const TerminalFrame *terminal_render(Terminal *t) {
    TerminalFrame *frame = &t->frame;
    Layout layout = measure(t);
    long bottom;
    long top;
    long row = 0;

    if (t->scroll_offset > layout.max_offset)
        t->scroll_offset = (int)layout.max_offset;
    bottom = layout.document - t->scroll_offset;
    top = bottom - t->rows;
    frame->cols = t->cols;
    frame->rows = t->rows;
    frame->cursor_col = -1;
    frame->cursor_row = -1;
    for (int i = 0; i < t->rows * t->cols; ++i) {
        frame->cells[i].ch = ' ';
        frame->cells[i].color = 0;
    }
    for (unsigned long n = t->first; n <= last_line(t); ++n) {
        int rows = line_rows(t, n);
        for (int sub = 0; sub < rows; ++sub) {
            long visible = row + sub - top;
            if (visible >= 0 && visible < t->rows)
                fill_row(t, &frame->cells[visible * t->cols], n, sub);
        }
        if (n == t->cursor_line && overlay_visible(t)) {
            int index = t->lines[n % SCROLLBACK_LINES].length + t->edit_cursor;
            long cursor_row = row + index / t->cols - top;
            if (cursor_row >= 0 && cursor_row < t->rows) {
                frame->cursor_row = (int)cursor_row;
                frame->cursor_col = index % t->cols;
            }
        }
        row += rows;
    }
    return frame;
}

int terminal_frame_text(const TerminalFrame *frame, int row, char *out, int cap) {
    int length = 0;
    if (row < 0 || row >= frame->rows || cap < 1) {
        if (cap > 0)
            out[0] = 0;
        return 0;
    }
    for (int col = 0; col < frame->cols && length + 1 < cap; ++col)
        out[length++] = (char)frame->cells[row * frame->cols + col].ch;
    while (length > 0 && out[length - 1] == ' ')
        length--;
    out[length] = 0;
    return length;
}

/* --------------------------------------------------------------- editing */

static void edit_insert(Terminal *t, const char *text, int length) {
    if (t->edit_length + length > INPUT_MAX)
        return;
    memmove(t->edit + t->edit_cursor + length, t->edit + t->edit_cursor,
            (size_t)(t->edit_length - t->edit_cursor + 1));
    memcpy(t->edit + t->edit_cursor, text, (size_t)length);
    t->edit_length += length;
    t->edit_cursor += length;
}

static void edit_delete(Terminal *t, int from, int to) {
    memmove(t->edit + from, t->edit + to, (size_t)(t->edit_length - to + 1));
    t->edit_length -= to - from;
    if (t->edit_cursor > from)
        t->edit_cursor = t->edit_cursor < to ? from : t->edit_cursor - (to - from);
}

static void edit_clear(Terminal *t) {
    t->edit_length = 0;
    t->edit_cursor = 0;
    t->edit[0] = 0;
    t->history_position = -1;
}

static void edit_set(Terminal *t, const char *text) {
    int length = (int)strlen(text);
    memcpy(t->edit, text, (size_t)length + 1);
    t->edit_length = length;
    t->edit_cursor = length;
}

static const char *history_entry(const Terminal *t, int index) {
    int oldest = (t->history_head - t->history_count + HISTORY_ENTRIES) % HISTORY_ENTRIES;
    return t->history[(oldest + index) % HISTORY_ENTRIES];
}

static void history_push(Terminal *t) {
    if (t->edit_length == 0)
        return;
    if (t->history_count > 0 &&
        strcmp(history_entry(t, t->history_count - 1), t->edit) == 0)
        return;
    memcpy(t->history[t->history_head], t->edit, (size_t)t->edit_length + 1);
    t->history_head = (t->history_head + 1) % HISTORY_ENTRIES;
    if (t->history_count < HISTORY_ENTRIES)
        t->history_count++;
}

static void history_step(Terminal *t, int direction) {
    int position = t->history_position < 0 ? t->history_count : t->history_position;
    int next = position + direction;
    if (next < 0 || next > t->history_count)
        return;
    if (t->history_position < 0)
        memcpy(t->draft, t->edit, (size_t)t->edit_length + 1);
    t->history_position = next == t->history_count ? -1 : next;
    edit_set(t, next == t->history_count ? t->draft : history_entry(t, next));
}

static void append_edit_to_screen(Terminal *t) {
    for (int i = 0; i < t->edit_length; ++i)
        put_cell(t, (uint8_t)t->edit[i], 0);
}

static void submit(Terminal *t) {
    append_edit_to_screen(t);
    newline(t);
    history_push(t);
    t->edit[t->edit_length] = '\n';
    queue_input(t, t->edit, t->edit_length + 1);
    t->idle = 0;
    edit_clear(t);
}

static void interrupt(Terminal *t) {
    append_edit_to_screen(t);
    put_text(t, "^C");
    if (t->idle) {
        newline(t);
        queue_input(t, "\n", 1);
    } else {
        kill((int)t->shell_pid, SIGINT);
    }
    t->idle = 0;
    edit_clear(t);
}

static void end_of_input(Terminal *t) {
    if (t->edit_cursor < t->edit_length) {
        edit_delete(t, t->edit_cursor, t->edit_cursor + 1);
    } else if (t->edit_length == 0 && t->idle && t->input_queue_length == 0) {
        close_descriptor(&t->input_fd);
        t->idle = 0;
    }
}

static void delete_word_back(Terminal *t) {
    int start = t->edit_cursor;
    while (start > 0 && t->edit[start - 1] == ' ')
        start--;
    while (start > 0 && t->edit[start - 1] != ' ')
        start--;
    edit_delete(t, start, t->edit_cursor);
}

/* ------------------------------------------------------------ completion */

static int word_break(char c) {
    return c == ' ' || c == '\t' || c == '|' || c == '&' || c == ';' ||
           c == '<' || c == '>' || c == '(' || c == ')';
}

/* True when the character at index is protected by a backslash. */
static int escaped_at(const char *text, int index) {
    int backslashes = 0;
    while (index > 0 && text[index - 1] == '\\') {
        backslashes++;
        index--;
    }
    return backslashes % 2;
}

/* Removes the backslashes the shell would remove, giving the file name the
 * typed text stands for. out holds at least length + 1 bytes. */
static void unescape(const char *text, int length, char *out) {
    int used = 0;
    for (int i = 0; i < length; ++i) {
        if (text[i] == '\\' && i + 1 < length)
            i++;
        out[used++] = text[i];
    }
    out[used] = 0;
}

static int needs_escape(char c) {
    return word_break(c) || c == '\\' || c == '\'' || c == '"' || c == '$' ||
           c == '#' || c == '*' || c == '?';
}

static void add_candidate(Terminal *t, const char *name, int is_directory) {
    int length = (int)strlen(name);
    if (t->candidate_count == CANDIDATES_MAX ||
        t->candidate_names_used + length + 1 > CANDIDATE_BYTES)
        return;
    for (int i = 0; i < t->candidate_count; ++i) {
        if (strcmp(t->candidate_names + t->candidates[i].offset, name) == 0)
            return;
    }
    memcpy(t->candidate_names + t->candidate_names_used, name, (size_t)length + 1);
    t->candidates[t->candidate_count].offset = (uint16_t)t->candidate_names_used;
    t->candidates[t->candidate_count].is_directory = (uint8_t)is_directory;
    t->candidate_count++;
    t->candidate_names_used += length + 1;
}

static const char *candidate_name(const Terminal *t, int index) {
    return t->candidate_names + t->candidates[index].offset;
}

static void sort_candidates(Terminal *t) {
    for (int i = 1; i < t->candidate_count; ++i) {
        Candidate moving = t->candidates[i];
        int j = i;
        while (j > 0 && strcmp(t->candidate_names + t->candidates[j - 1].offset,
                               t->candidate_names + moving.offset) > 0) {
            t->candidates[j] = t->candidates[j - 1];
            j--;
        }
        t->candidates[j] = moving;
    }
}

static void collect_directory(Terminal *t, const char *directory, const char *prefix,
                              int skip_directories) {
    size_t prefix_length = strlen(prefix);
    dirent_t entry;
    for (int pos = 0; readdir_pos(directory, pos, &entry) == 1; ++pos) {
        int is_directory = entry.d_type == ENTRY_TYPE_DIRECTORY;
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
            continue;
        if (entry.d_name[0] == '.' && prefix[0] != '.')
            continue;
        if (skip_directories && is_directory)
            continue;
        if (strncmp(entry.d_name, prefix, prefix_length) == 0)
            add_candidate(t, entry.d_name, is_directory);
    }
}

static void collect_commands(Terminal *t, const char *prefix) {
    static const char *const builtin_names[] = {
#define BUILTIN_NAME(name) #name,
        SHELL_COMPLETION_NAMES(BUILTIN_NAME)
#undef BUILTIN_NAME
    };
    size_t prefix_length = strlen(prefix);
    for (size_t i = 0; i < sizeof(builtin_names) / sizeof(builtin_names[0]); ++i) {
        if (strncmp(builtin_names[i], prefix, prefix_length) == 0)
            add_candidate(t, builtin_names[i], 0);
    }
    collect_directory(t, "/bin", prefix, 1);
    collect_directory(t, "/sbin", prefix, 1);
}

static int command_position(const Terminal *t, int word_start) {
    int i = word_start;
    while (i > 0 && (t->edit[i - 1] == ' ' || t->edit[i - 1] == '\t'))
        i--;
    return i == 0 || t->edit[i - 1] == '|' || t->edit[i - 1] == '&' ||
           t->edit[i - 1] == ';' || t->edit[i - 1] == '(';
}

/* Inserts name at [from, edit_cursor), backslash-escaping what the shell
 * would otherwise split or expand. */
static void replace_word_tail(Terminal *t, int from, const char *name,
                              const char *suffix) {
    char text[2 * 256 + 4];
    int length = 0;
    int word_initial = from == 0 || (word_break(t->edit[from - 1]) && !escaped_at(t->edit, from - 1));
    for (const char *c = name; *c && length < (int)sizeof(text) - 4; ++c) {
        if (needs_escape(*c) || (*c == '~' && c == name && word_initial))
            text[length++] = '\\';
        text[length++] = *c;
    }
    for (const char *c = suffix; *c; ++c)
        text[length++] = *c;
    /* edit_insert refuses a line that is too long, which must not leave the
     * typed word deleted and nothing in its place. */
    if (t->edit_length - (t->edit_cursor - from) + length > INPUT_MAX)
        return;
    edit_delete(t, from, t->edit_cursor);
    edit_insert(t, text, length);
}

static void show_candidates(Terminal *t) {
    int widest = 0;
    int column_width;
    int per_row;
    char label[260];

    append_edit_to_screen(t);
    newline(t);
    for (int i = 0; i < t->candidate_count; ++i) {
        int length = (int)strlen(candidate_name(t, i)) + t->candidates[i].is_directory;
        if (length > widest)
            widest = length;
    }
    column_width = widest + CANDIDATE_GAP;
    per_row = t->cols / column_width;
    if (per_row < 1)
        per_row = 1;
    for (int i = 0; i < t->candidate_count; ++i) {
        int length = snprintf(label, sizeof(label), "%s%s", candidate_name(t, i),
                              t->candidates[i].is_directory ? "/" : "");
        put_text(t, label);
        if ((i + 1) % per_row == 0 || i + 1 == t->candidate_count) {
            newline(t);
        } else {
            for (int pad = length; pad < column_width; ++pad)
                put_cell(t, ' ', 0);
        }
    }
    /* The prompt again, with the half-typed command still to be edited. */
    for (int i = 0; i < t->prompt_length; ++i)
        put_cell(t, t->prompt[i].ch, t->prompt[i].color);
}

static void complete(Terminal *t) {
    char typed[INPUT_MAX + 1];
    char plain[INPUT_MAX + 1];
    char directory[CWD_CAP + INPUT_MAX + 2];
    int word_start = t->edit_cursor;
    int typed_length;
    int base_start;
    const char *base;
    int common;

    while (word_start > 0 &&
           !(word_break(t->edit[word_start - 1]) && !escaped_at(t->edit, word_start - 1)))
        word_start--;
    typed_length = t->edit_cursor - word_start;
    memcpy(typed, t->edit + word_start, (size_t)typed_length);
    typed[typed_length] = 0;

    t->candidate_count = 0;
    t->candidate_names_used = 0;
    {
        /* The last slash splits directory from name whether or not it was
         * escaped, since an escaped slash is still a slash in the path. */
        const char *slash = strrchr(typed, '/');
        if (!slash && command_position(t, word_start)) {
            /* Every command would be a flood, not a completion. */
            if (typed_length == 0)
                return;
            unescape(typed, typed_length, plain);
            base = plain;
            base_start = word_start;
            collect_commands(t, plain);
        } else {
            int dir_length = slash ? (int)(slash - typed) + 1 : 0;
            char dir_plain[INPUT_MAX + 1];
            unescape(typed, dir_length, dir_plain);
            unescape(typed + dir_length, typed_length - dir_length, plain);
            if (dir_length == 0)
                snprintf(directory, sizeof(directory), "%s", t->cwd);
            else if (dir_plain[0] == '/')
                snprintf(directory, sizeof(directory), "%s", dir_plain);
            else
                snprintf(directory, sizeof(directory), "%s/%s", t->cwd, dir_plain);
            base = plain;
            base_start = word_start + dir_length;
            collect_directory(t, directory, base, 0);
        }
    }
    if (t->candidate_count == 0)
        return;
    sort_candidates(t);

    if (t->candidate_count == 1) {
        replace_word_tail(t, base_start, candidate_name(t, 0),
                          t->candidates[0].is_directory ? "/" : " ");
        return;
    }
    common = (int)strlen(candidate_name(t, 0));
    for (int i = 1; i < t->candidate_count; ++i) {
        const char *name = candidate_name(t, i);
        int same = 0;
        while (same < common && name[same] == candidate_name(t, 0)[same])
            same++;
        common = same;
    }
    if (common > (int)strlen(base)) {
        char shared[257];
        memcpy(shared, candidate_name(t, 0), (size_t)common);
        shared[common] = 0;
        replace_word_tail(t, base_start, shared, "");
    } else {
        show_candidates(t);
    }
}

/* ------------------------------------------------------------------ keys */

void terminal_key(Terminal *t, int key) {
    int page = t->rows > 1 ? t->rows - 1 : 1;
    if (key == TERMINAL_KEY_PAGE_UP) {
        terminal_scroll(t, page);
        return;
    }
    if (key == TERMINAL_KEY_PAGE_DOWN) {
        terminal_scroll(t, -page);
        return;
    }
    /* Without a shell the only thing left to do is start the next one. */
    if (t->shell_pid == 0) {
        if (key == '\n' || key == '\r') {
            newline(t);
            terminal_start(t);
        }
        return;
    }
    /* Anything that edits the command line brings the prompt back into view. */
    t->scroll_offset = 0;
    switch (key) {
    case '\n':
    case '\r':
        submit(t);
        break;
    case '\t':
        complete(t);
        break;
    case '\b':
    case KEY_BACKSPACE:
        if (t->edit_cursor > 0)
            edit_delete(t, t->edit_cursor - 1, t->edit_cursor);
        break;
    case TERMINAL_KEY_DELETE:
        if (t->edit_cursor < t->edit_length)
            edit_delete(t, t->edit_cursor, t->edit_cursor + 1);
        break;
    case TERMINAL_KEY_LEFT:
        if (t->edit_cursor > 0)
            t->edit_cursor--;
        break;
    case TERMINAL_KEY_RIGHT:
        if (t->edit_cursor < t->edit_length)
            t->edit_cursor++;
        break;
    case TERMINAL_KEY_HOME:
    case KEY_CTRL_A:
        t->edit_cursor = 0;
        break;
    case TERMINAL_KEY_END:
    case KEY_CTRL_E:
        t->edit_cursor = t->edit_length;
        break;
    case TERMINAL_KEY_UP:
        history_step(t, -1);
        break;
    case TERMINAL_KEY_DOWN:
        history_step(t, 1);
        break;
    case KEY_CTRL_C:
        interrupt(t);
        break;
    case KEY_CTRL_D:
        end_of_input(t);
        break;
    case KEY_CTRL_K:
        edit_delete(t, t->edit_cursor, t->edit_length);
        break;
    case KEY_CTRL_U:
        edit_delete(t, 0, t->edit_cursor);
        break;
    case KEY_CTRL_W:
        delete_word_back(t);
        break;
    case KEY_CTRL_L:
        t->page_start = t->cursor_line;
        break;
    default:
        if (key >= 0x20 && key <= 0x7E) {
            char c = (char)key;
            edit_insert(t, &c, 1);
        }
        break;
    }
}
