#include "env.h"
#include "exec.h"
#include "input.h"
#include "interrupt.h"
#include "jobs.h"
#include "line_editor.h"
#include "mini_libc.h"
#include "output.h"
#include "shell_limits.h"

enum {
    SCRIPT_CAP = 16384,
    STATUS_USAGE = 2,
    STATUS_NOT_FOUND = 127,
    TTY_COLOR_BLACK = 0,
    TTY_COLOR_CYAN = 11,
    TTY_COLOR_WHITE = 15,
};

static char script_buffer[SCRIPT_CAP];
static char line_buffer[SHELL_LINE_MAX + 1];

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Length of the next logical line: everything up to the first newline that
 * is neither quoted nor part of a comment. */
static size_t logical_line_length(const char *s) {
    char quote = 0;
    size_t i = 0;
    while (s[i]) {
        char c = s[i];
        if (quote == '\'') {
            if (c == '\'')
                quote = 0;
            i++;
            continue;
        }
        if (c == '\\' && s[i + 1]) {
            i += 2;
            continue;
        }
        if (quote == '"') {
            if (c == '"')
                quote = 0;
            i++;
            continue;
        }
        if (c == '\n')
            break;
        if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '#' && (i == 0 || is_blank(s[i - 1]) || s[i - 1] == ';')) {
            while (s[i] && s[i] != '\n')
                i++;
            break;
        }
        i++;
    }
    return i;
}

static int run_string(const char *script) {
    while (*script) {
        size_t len = logical_line_length(script);
        if (len > SHELL_LINE_MAX) {
            out_error("shell", 0, "line too long");
            return STATUS_USAGE;
        }
        memcpy(line_buffer, script, len);
        line_buffer[len] = 0;
        exec_line(line_buffer);
        if (exec_exit_requested())
            break;
        script += len;
        if (*script == '\n')
            script++;
    }
    return exec_last_status();
}

/* Reads a whole script into script_buffer. */
static int load_script(int fd) {
    size_t used = 0;
    for (;;) {
        long n;
        if (used == sizeof(script_buffer) - 1) {
            char probe;
            if (read(fd, &probe, 1) != 0) {
                out_error("shell", 0, "script too large");
                return -1;
            }
            break;
        }
        n = read(fd, script_buffer + used, sizeof(script_buffer) - 1 - used);
        if (n < 0) {
            out_error("shell", 0, "cannot read script");
            return -1;
        }
        if (n == 0)
            break;
        used += (size_t)n;
    }
    script_buffer[used] = 0;
    return 0;
}

static int run_interactive(void) {
    exec_set_interactive(1);
    out_set_color(OUT_COLOR_CONSOLE);
    tty_set_color(TTY_COLOR_CYAN, TTY_COLOR_BLACK);
    out_str(1, "HydrogenOS");
    out_flush();
    tty_set_color(TTY_COLOR_WHITE, TTY_COLOR_BLACK);
    out_str(1, " user shell. Type 'help'.\n");
    out_flush();
    for (;;) {
        jobs_print(1);
        if (line_editor_read(line_buffer, (int)sizeof(line_buffer)) == 0)
            continue;
        exec_line(line_buffer);
        if (exec_exit_requested())
            return exec_last_status();
    }
}

/* The prompt of a shell behind a terminal emulator. Besides the visible text
 * it carries two private sequences (docs/TERMINAL.md): OSC 7 reports the
 * working directory for path completion, and OSC 133;B tells the emulator
 * that the shell now waits for a line, so it knows where input starts. */
static void print_terminal_prompt(void) {
    char cwd[SHELL_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        memcpy(cwd, "/", 2);
    jobs_print(1);
    out_str(1, "\x1b]7;");
    out_str(1, cwd);
    out_str(1, "\a\x1b[32mhydrogen\x1b[0m \x1b[94m");
    out_str(1, cwd);
    out_str(1, "\x1b[0m$ \x1b]133;B\a");
    out_flush();
}

/* Reads commands from stdin, which the terminal emulator feeds one finished
 * line at a time, and writes to pipes it renders. Ctrl+C arrives as SIGINT
 * and is passed on to the foreground command. End of input ends the shell. */
static int run_terminal(void) {
    int status;
    exec_set_interactive(1);
    exec_set_terminal_host(1);
    out_set_color(OUT_COLOR_ANSI);
    interrupt_install();
    out_str(1, "HydrogenOS shell. Type 'help' for a list of commands.\n");
    for (;;) {
        int length;
        print_terminal_prompt();
        length = input_read_line(line_buffer, (int)sizeof(line_buffer));
        if (length < 0) {
            out_str(1, "exit\n");
            out_flush();
            break;
        }
        interrupt_clear();
        if (length == 0)
            continue;
        exec_line(line_buffer);
        if (exec_exit_requested())
            break;
    }
    status = exec_last_status();
    jobs_terminate_all();
    return status;
}

static int usage(void) {
    out_error("shell", 0, "usage: shell [-c command | -s | -i | script]");
    return STATUS_USAGE;
}

int main(int argc, char **argv, char **envp) {
    long fd;
    int status;
    env_import(envp);
    if (!env_get("PATH", 4) && (env_set("PATH", 4, "/bin:/sbin") != 0 ||
                                env_export("PATH", 4) != 0)) {
        out_error("shell", 0, "cannot initialise PATH");
        return STATUS_USAGE;
    }
    if (argc < 2)
        return run_interactive();
    if (str_eq(argv[1], "-c"))
        return argc == 3 ? run_string(argv[2]) : usage();
    if (str_eq(argv[1], "-i"))
        return argc == 2 ? run_terminal() : usage();
    if (str_eq(argv[1], "-s")) {
        if (argc != 2)
            return usage();
        return load_script(0) == 0 ? run_string(script_buffer) : STATUS_USAGE;
    }
    if (argc != 2 || argv[1][0] == '-')
        return usage();
    fd = open(argv[1], OPEN_READ);
    if (fd < 0) {
        out_error("shell", argv[1], "cannot open script");
        return STATUS_NOT_FOUND;
    }
    status = load_script((int)fd);
    close((int)fd);
    return status == 0 ? run_string(script_buffer) : STATUS_USAGE;
}
