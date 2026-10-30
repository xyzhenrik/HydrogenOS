#pragma once

#include <stddef.h>

/* Receives one word produced by brace expansion, still in raw form (quotes
 * and escapes intact). A nonzero return stops the expansion. */
typedef int (*BraceEmit)(void *context, const char *raw, size_t raw_len);

/* Expands unquoted {a,b,...} groups in one raw word into one word per
 * alternative, in order: "x{a,b}y{1,2}" yields xay1 xay2 xby1 xby2. Groups
 * nest. A brace without a matching '}' or without a top-level comma, a
 * quoted or escaped brace and the "${" of a parameter substitution stay
 * literal. There are no {1..3} ranges. raw_len must not exceed
 * SHELL_LINE_MAX. Returns 0, or the first nonzero value returned by emit. */
int brace_expand(const char *raw, size_t raw_len, BraceEmit emit, void *context);
