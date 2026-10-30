#pragma once

#include <stddef.h>

/*
 * Ring buffer of the most recent console and serial output. It exists so that
 * a crash dump can show what the system said last, including syslogd's lines,
 * which reach the console like any other output.
 */
void kmsg_push(char c);

/*
 * Copies the last max_lines lines (as far as they fit in cap - 1 bytes) to
 * out in chronological order, NUL-terminated, with carriage returns dropped
 * and other control characters replaced by '?'. Returns the text length.
 */
size_t kmsg_copy_tail(char *out, size_t cap, unsigned max_lines);
