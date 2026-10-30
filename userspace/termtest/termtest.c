#include "mini_libc.h"
#include "terminal.h"

/* Boot test for the terminal core: drives a real /shell through the same pipes
 * the desktop terminal uses, with typed keys as input, and reads the rendered
 * frame back. No display is involved. */

enum {
    COLS = 80,
    ROWS = 24,
    PROMPT_TIMEOUT_MS = 20000,
    COMMAND_TIMEOUT_MS = 8000,
    SETTLE_MS = 300,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    TEXT_CAP = COLS + 1,
    COLOR_BRIGHT_RED = 10,
};

static Terminal *terminal;

static int fail(const char *step) {
    dprintf(2, "TERM FAIL %s\n", step);
    return -1;
}

static int ends_with(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

/* Index of the first frame row for which test(row_text) holds, or -1. */
static int find_row(int (*test)(const char *, const char *), const char *argument) {
    const TerminalFrame *frame = terminal_render(terminal);
    char text[TEXT_CAP];
    for (int row = 0; row < frame->rows; ++row) {
        terminal_frame_text(frame, row, text, sizeof(text));
        if (test(text, argument))
            return row;
    }
    return -1;
}

static int row_equals(const char *text, const char *argument) { return strcmp(text, argument) == 0; }
static int row_contains(const char *text, const char *argument) { return strstr(text, argument) != 0; }

static int has_line(const char *line) { return find_row(row_equals, line) >= 0; }
static int has_text(const char *text) { return find_row(row_contains, text) >= 0; }

/* The row the cursor is on, i.e. the prompt line being edited. */
static int cursor_row_ends_with(const char *suffix) {
    const TerminalFrame *frame = terminal_render(terminal);
    char text[TEXT_CAP];
    if (frame->cursor_row < 0)
        return 0;
    terminal_frame_text(frame, frame->cursor_row, text, sizeof(text));
    return ends_with(text, suffix);
}

static void type(const char *text) {
    for (; *text; ++text)
        terminal_key(terminal, (int)*text);
}

/* Pumps until the shell is back at its prompt. */
static int wait_prompt(long timeout_ms) {
    unsigned long deadline = time_ms() + (unsigned long)timeout_ms;
    while (time_ms() < deadline) {
        terminal_pump(terminal);
        if (terminal_idle(terminal))
            return 0;
        if (!terminal_alive(terminal))
            return -1;
        sleep_ms(2);
    }
    return -1;
}

static int wait_for_line(const char *line, long timeout_ms) {
    unsigned long deadline = time_ms() + (unsigned long)timeout_ms;
    while (time_ms() < deadline) {
        terminal_pump(terminal);
        if (has_line(line))
            return 0;
        sleep_ms(2);
    }
    return -1;
}

static void settle(long ms) {
    unsigned long deadline = time_ms() + (unsigned long)ms;
    while (time_ms() < deadline) {
        terminal_pump(terminal);
        sleep_ms(2);
    }
}

static int run(const char *command) {
    type(command);
    terminal_key(terminal, '\n');
    return wait_prompt(COMMAND_TIMEOUT_MS);
}

static void abandon_line(void) { terminal_key(terminal, KEY_CTRL_C); wait_prompt(COMMAND_TIMEOUT_MS); }

static int test_commands(void) {
    if (run("echo hello | cat") != 0 || !has_line("hello"))
        return fail("pipe");
    if (run("ls /bin > /tmp/termtest.out") != 0 || run("cat /tmp/termtest.out") != 0 ||
        !has_line("shell") || run("rm /tmp/termtest.out") != 0)
        return fail("redirect");
    if (run("GREETING=hi") != 0 || run("echo $GREETING") != 0 || !has_line("hi"))
        return fail("variable");
    if (run("echo a~b{c}") != 0 || !has_line("a~b{c}"))
        return fail("special characters");
    if (run("echo ~") != 0 || !has_line("/home/user") || run("echo '~'") != 0 || !has_line("~"))
        return fail("tilde");
    if (run("ps") != 0 || !has_text("PPID"))
        return fail("ps");
    if (run("uptime") != 0 || !has_text("up "))
        return fail("uptime");
    return 0;
}

static int test_colors(void) {
    int row;
    const TerminalFrame *frame;
    if (run("cat /termtest.missing") != 0)
        return fail("error command");
    row = find_row(row_contains, "cannot open");
    if (row < 0)
        return fail("error text");
    frame = terminal_render(terminal);
    if (frame->cells[row * frame->cols].color != COLOR_BRIGHT_RED)
        return fail("error colour");
    return 0;
}

static int test_interrupt(void) {
    static const char *const commands[] = {
        "echo ready1; sleep 600000",
        "echo ready2; sleep 600000 | cat",
    };
    static const char *const markers[] = {"ready1", "ready2"};
    for (int i = 0; i < 2; ++i) {
        type(commands[i]);
        terminal_key(terminal, '\n');
        if (wait_for_line(markers[i], COMMAND_TIMEOUT_MS) != 0)
            return fail("interrupt start");
        settle(SETTLE_MS);
        terminal_key(terminal, KEY_CTRL_C);
        if (wait_prompt(COMMAND_TIMEOUT_MS) != 0)
            return fail("interrupt");
        if (!has_text("^C"))
            return fail("interrupt echo");
    }
    if (run("echo alive") != 0 || !has_line("alive"))
        return fail("shell after interrupt");
    return 0;
}

static int test_editing(void) {
    type("ech");
    terminal_key(terminal, '\t');
    if (!cursor_row_ends_with("$ echo"))
        return fail("complete command");
    abandon_line();

    type("ls /bin/she");
    terminal_key(terminal, '\t');
    if (!cursor_row_ends_with("$ ls /bin/shell"))
        return fail("complete path");
    abandon_line();

    type("cd /tm");
    terminal_key(terminal, '\t');
    if (!cursor_row_ends_with("$ cd /tmp/"))
        return fail("complete directory");
    abandon_line();

    type("ls /lib/tests/s");
    terminal_key(terminal, '\t');
    if (!has_text("shelltest") || !has_text("sigtest") || !cursor_row_ends_with("$ ls /lib/tests/s"))
        return fail("list candidates");
    abandon_line();

    if (run("echo again") != 0)
        return fail("history command");
    terminal_key(terminal, TERMINAL_KEY_UP);
    if (!cursor_row_ends_with("$ echo again"))
        return fail("history");
    terminal_key(terminal, TERMINAL_KEY_HOME);
    terminal_key(terminal, TERMINAL_KEY_DELETE);
    terminal_key(terminal, 'E');
    if (!cursor_row_ends_with("$ Echo again"))
        return fail("line editing");
    abandon_line();
    return 0;
}

static int test_screen(void) {
    const TerminalFrame *frame;
    char text[TEXT_CAP];
    if (run("clear") != 0)
        return fail("clear");
    frame = terminal_render(terminal);
    terminal_frame_text(frame, 0, text, sizeof(text));
    if (strncmp(text, "hydrogen", 8) != 0)
        return fail("prompt after clear");
    terminal_frame_text(frame, 1, text, sizeof(text));
    if (text[0] != 0)
        return fail("screen not cleared");
    return 0;
}

static void feed_numbered_lines(int from, int to) {
    char line[24];
    for (int i = from; i <= to; ++i) {
        int length = snprintf(line, sizeof(line), "line %d\n", i);
        terminal_feed(terminal, line, length);
    }
}

static int test_scrollback(void) {
    const TerminalFrame *frame;
    char text[TEXT_CAP];
    int first;

    feed_numbered_lines(1, 1500);
    if (!has_line("line 1500"))
        return fail("scrollback tail");
    terminal_scroll(terminal, 30);
    if (!has_line("line 1460") || has_line("line 1500"))
        return fail("scroll up");
    terminal_scroll(terminal, -1000);
    if (!has_line("line 1500"))
        return fail("scroll down");

    feed_numbered_lines(1501, 3000);
    terminal_scroll(terminal, 100000);
    frame = terminal_render(terminal);
    terminal_frame_text(frame, 0, text, sizeof(text));
    if (strncmp(text, "line ", 5) != 0)
        return fail("oldest line");
    first = atoi(text + 5);
    if (first < 2 || first > 2000)
        return fail("scrollback size");
    terminal_key(terminal, TERMINAL_KEY_PAGE_DOWN);
    terminal_scroll(terminal, -100000);
    return has_line("line 3000") ? 0 : fail("scroll to the end");
}

static int test_reflow(void) {
    const TerminalFrame *frame;
    char text[TEXT_CAP];
    char wide[101];
    for (int i = 0; i < 100; ++i)
        wide[i] = (char)('a' + i % 26);
    wide[100] = '\n';
    terminal_feed(terminal, "\n", 1);
    terminal_feed(terminal, wide, 101);
    terminal_resize(terminal, 40, ROWS);
    frame = terminal_render(terminal);
    /* The cursor line follows: bottom rows are the prompt, then the 100 cells
     * wrapped to 40, 40 and 20. */
    for (int row = 0; row + 2 < frame->rows; ++row) {
        terminal_frame_text(frame, row, text, sizeof(text));
        if (strncmp(text, "abcdefghijklmnopqrstuvwxyzabcdefghijklmn", 40) == 0) {
            terminal_frame_text(frame, row + 2, text, sizeof(text));
            terminal_resize(terminal, COLS, ROWS);
            return strlen(text) == 20 ? 0 : fail("reflow tail");
        }
    }
    terminal_resize(terminal, COLS, ROWS);
    return fail("reflow");
}

static int test_exit(void) {
    unsigned long deadline = time_ms() + COMMAND_TIMEOUT_MS;
    terminal_key(terminal, KEY_CTRL_D);
    while (terminal_alive(terminal) && time_ms() < deadline) {
        terminal_pump(terminal);
        sleep_ms(2);
    }
    if (terminal_alive(terminal))
        return fail("end of input");
    return terminal_exit_status(terminal) == 0 ? 0 : fail("exit status");
}

int main(void) {
    terminal = terminal_create(COLS, ROWS);
    if (!terminal)
        return fail("create");
    if (terminal_start(terminal) != 0 || wait_prompt(PROMPT_TIMEOUT_MS) != 0)
        return fail("start");
    if (test_commands() != 0 || test_colors() != 0 || test_interrupt() != 0 ||
        test_editing() != 0 || test_screen() != 0 || test_scrollback() != 0 ||
        test_reflow() != 0 || test_exit() != 0)
        return 1;
    terminal_destroy(terminal);
    printf("TERM PASS\n");
    return 0;
}
