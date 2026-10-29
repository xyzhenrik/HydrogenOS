#pragma once

#include <stddef.h>

typedef struct ShellExpansion {
    size_t length;
    int quoted; /* the raw word contained quotes or escapes */
} ShellExpansion;

/* Expands one raw word: $NAME, ${NAME} and $? are substituted (also inside
 * double quotes), then quotes and backslash escapes are removed. There is no
 * field splitting or globbing. The result is NUL-terminated in out.
 * Returns 0, or -1 with a static message in *error. */
int expand_word(const char *raw, size_t raw_len, int last_status, char *out,
                size_t out_cap, ShellExpansion *result, const char **error);
