#pragma once

/* Reads one line from stdin for a shell that sits behind a terminal emulator:
 * the emulator does the line editing and sends finished lines, so this only
 * has to split the byte stream. The line is stored without its newline.
 * Returns its length, or -1 at end of input. A line of cap or more bytes is
 * dropped whole and reported as empty. */
int input_read_line(char *buf, int cap);
