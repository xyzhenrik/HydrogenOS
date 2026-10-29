#include "expand.h"

#include "env.h"

typedef struct ExpandBuilder {
    char *out;
    size_t cap;
    size_t len;
    int overflow;
} ExpandBuilder;

static void put_char(ExpandBuilder *b, char c) {
    if (b->len + 1 >= b->cap) {
        b->overflow = 1;
        return;
    }
    b->out[b->len++] = c;
}

static void put_string(ExpandBuilder *b, const char *s) {
    while (s && *s)
        put_char(b, *s++);
}

static void put_status(ExpandBuilder *b, int status) {
    char digits[12];
    int count = 0;
    unsigned int value = (unsigned int)status;
    if (status < 0) {
        put_char(b, '-');
        value = (unsigned int)(-(long)status);
    }
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (count > 0)
        put_char(b, digits[--count]);
}

static int is_name_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

/* raw[*pos] is '$'; advances *pos past the whole substitution. */
static int expand_dollar(const char *raw, size_t raw_len, size_t *pos,
                         int last_status, ExpandBuilder *b, const char **error) {
    size_t i = *pos + 1;
    if (i < raw_len && raw[i] == '?') {
        put_status(b, last_status);
        *pos = i + 1;
        return 0;
    }
    if (i < raw_len && raw[i] == '{') {
        size_t start = i + 1;
        size_t end = start;
        while (end < raw_len && raw[end] != '}')
            end++;
        if (end >= raw_len) {
            *error = "bad substitution: missing '}'";
            return -1;
        }
        if (end - start == 1 && raw[start] == '?') {
            put_status(b, last_status);
        } else if (env_name_valid(raw + start, end - start)) {
            put_string(b, env_get(raw + start, end - start));
        } else {
            *error = "bad substitution";
            return -1;
        }
        *pos = end + 1;
        return 0;
    }
    if (i < raw_len && is_name_start(raw[i])) {
        size_t end = i;
        while (end < raw_len && is_name_char(raw[end]))
            end++;
        put_string(b, env_get(raw + i, end - i));
        *pos = end;
        return 0;
    }
    put_char(b, '$');
    *pos = i;
    return 0;
}

static int escapable_in_double_quotes(char c) {
    return c == '$' || c == '"' || c == '\\' || c == '`';
}

int expand_word(const char *raw, size_t raw_len, int last_status, char *out,
                size_t out_cap, ShellExpansion *result, const char **error) {
    ExpandBuilder b = {out, out_cap, 0, 0};
    size_t i = 0;
    result->quoted = 0;
    if (out_cap == 0) {
        *error = "word too long";
        return -1;
    }
    while (i < raw_len) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw_len && raw[i + 1] == '\n') {
            i += 2;
        } else if (c == '\\') {
            result->quoted = 1;
            if (i + 1 < raw_len) {
                put_char(&b, raw[i + 1]);
                i += 2;
            } else {
                put_char(&b, '\\');
                i++;
            }
        } else if (c == '\'') {
            result->quoted = 1;
            i++;
            while (i < raw_len && raw[i] != '\'')
                put_char(&b, raw[i++]);
            i++;
        } else if (c == '"') {
            result->quoted = 1;
            i++;
            while (i < raw_len && raw[i] != '"') {
                if (raw[i] == '\\' && i + 1 < raw_len && raw[i + 1] == '\n') {
                    i += 2;
                } else if (raw[i] == '\\' && i + 1 < raw_len &&
                           escapable_in_double_quotes(raw[i + 1])) {
                    put_char(&b, raw[i + 1]);
                    i += 2;
                } else if (raw[i] == '$') {
                    if (expand_dollar(raw, raw_len, &i, last_status, &b, error) != 0)
                        return -1;
                } else {
                    put_char(&b, raw[i++]);
                }
            }
            i++;
        } else if (c == '$') {
            if (expand_dollar(raw, raw_len, &i, last_status, &b, error) != 0)
                return -1;
        } else {
            put_char(&b, c);
            i++;
        }
    }
    if (b.overflow) {
        *error = "word too long";
        return -1;
    }
    out[b.len] = 0;
    result->length = b.len;
    return 0;
}
