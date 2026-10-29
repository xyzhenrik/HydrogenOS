#pragma once

/* Prints the prompt and reads one line from the console with cursor
 * movement (left/right/home/end/delete) and a small history (up/down).
 * Returns the line length; buf is always NUL-terminated. */
int line_editor_read(char *buf, int cap);
