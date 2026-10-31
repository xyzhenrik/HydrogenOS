#include "brace.h"

#include "shell_limits.h"

/* Text still to be walked after the current segment ends; the segments form
 * a stack on the C stack, one frame per open group. */
typedef struct Continuation {
    const char *begin;
    const char *end;
    const struct Continuation *next;
} Continuation;

typedef struct Expansion {
    char word[SHELL_LINE_MAX + 1];
    size_t length;
    BraceEmit emit;
    void *context;
} Expansion;

/* p points at a quote character; returns the position after the quoted run. */
static const char *skip_quoted(const char *p, const char *end) {
    char quote = *p++;
    while (p < end && *p != quote) {
        if (quote == '"' && *p == '\\' && p + 1 < end)
            p++;
        p++;
    }
    return p < end ? p + 1 : end;
}

/* p points at "${"; returns the position after the closing '}'. */
static const char *skip_parameter(const char *p, const char *end) {
    p += 2;
    while (p < end && *p != '}')
        p++;
    return p < end ? p + 1 : end;
}

/* Scans from the first character of an alternative to the ',' or '}' that
 * ends it at the group's own nesting level. Returns 0 when the text ends
 * first, so the group is not closed. */
static const char *alternative_end(const char *p, const char *end) {
    int depth = 0;
    while (p < end) {
        char c = *p;
        if (c == '\\') {
            p += p + 1 < end ? 2 : 1;
        } else if (c == '\'' || c == '"') {
            p = skip_quoted(p, end);
        } else if (c == '$' && p + 1 < end && p[1] == '{') {
            p = skip_parameter(p, end);
        } else if (c == '{') {
            depth++;
            p++;
        } else if (c == '}' && depth > 0) {
            depth--;
            p++;
        } else if (c == '}' || (c == ',' && depth == 0)) {
            return p;
        } else {
            p++;
        }
    }
    return 0;
}

/* open points at '{'. For a real group (closed, with a top-level comma)
 * returns the position of its '}'; otherwise 0. */
static const char *group_close(const char *open, const char *end) {
    const char *p = open + 1;
    int has_comma = 0;
    for (;;) {
        p = alternative_end(p, end);
        if (!p)
            return 0;
        if (*p == '}')
            return has_comma ? p : 0;
        has_comma = 1;
        p++;
    }
}

/* Appends [p, end) to the word being built, expanding groups on the way and
 * emitting the word once the continuation is used up. Every character the
 * word receives comes from the raw input, so it never outgrows it. */
static int walk(Expansion *state, const char *p, const char *end,
                const Continuation *rest) {
    for (;;) {
        if (p == end) {
            if (!rest)
                return state->emit(state->context, state->word, state->length);
            p = rest->begin;
            end = rest->end;
            rest = rest->next;
            continue;
        }
        char c = *p;
        const char *literal_end = p + 1;
        if (c == '\\') {
            literal_end = p + 1 < end ? p + 2 : p + 1;
        } else if (c == '\'' || c == '"') {
            literal_end = skip_quoted(p, end);
        } else if (c == '$' && p + 1 < end && p[1] == '{') {
            literal_end = skip_parameter(p, end);
        } else if (c == '{') {
            const char *close = group_close(p, end);
            if (close) {
                Continuation after = {close + 1, end, rest};
                size_t mark = state->length;
                const char *alternative = p + 1;
                for (;;) {
                    const char *stop = alternative_end(alternative, end);
                    int rc = walk(state, alternative, stop, &after);
                    if (rc != 0)
                        return rc;
                    state->length = mark;
                    if (stop == close)
                        return 0;
                    alternative = stop + 1;
                }
            }
        }
        while (p < literal_end)
            state->word[state->length++] = *p++;
    }
}

int brace_expand(const char *raw, size_t raw_len, BraceEmit emit, void *context) {
    Expansion state;
    state.length = 0;
    state.emit = emit;
    state.context = context;
    return walk(&state, raw, raw + raw_len, 0);
}
