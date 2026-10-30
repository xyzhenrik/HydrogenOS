#pragma once

/* Tokenizes, parses and runs one logical line. Returns its exit status,
 * which also becomes the value of $?. */
int exec_line(const char *line);

int exec_last_status(void);

/* Set by the exit builtin, whose return value becomes the final status;
 * callers stop reading input once it is set. */
void exec_request_exit(void);
int exec_exit_requested(void);

/* Interactive shells announce background jobs and finished jobs. */
void exec_set_interactive(int interactive);

/* A shell behind the desktop terminal emulator: the builtins that ask the
 * desktop for something (clear, edit, files) are available. */
void exec_set_terminal_host(int terminal_host);
int exec_terminal_host(void);
