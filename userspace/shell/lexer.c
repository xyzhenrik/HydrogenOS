#include "lexer.h"

#include <stddef.h>

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int is_operator_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int push_token(ShellTokenList *out, const ShellToken *token,
                      const char **error) {
    /* One slot always stays free for the closing TOKEN_END. */
    if (out->count >= SHELL_MAX_TOKENS - 1) {
        *error = "too many tokens";
        return -1;
    }
    out->tokens[out->count++] = *token;
    return 0;
}

static int scan_word(const char *line, size_t pos, size_t *end,
                     const char **error) {
    size_t i = pos;
    while (line[i] && !is_blank(line[i]) && !is_operator_char(line[i])) {
        char c = line[i];
        if (c == '\\') {
            i++;
            if (line[i])
                i++;
            continue;
        }
        if (c == '\'') {
            i++;
            while (line[i] && line[i] != '\'')
                i++;
            if (!line[i]) {
                *error = "unterminated single quote";
                return -1;
            }
            i++;
            continue;
        }
        if (c == '"') {
            i++;
            while (line[i] && line[i] != '"') {
                if (line[i] == '\\' && line[i + 1])
                    i += 2;
                else
                    i++;
            }
            if (!line[i]) {
                *error = "unterminated double quote";
                return -1;
            }
            i++;
            continue;
        }
        i++;
    }
    *end = i;
    return 0;
}

static size_t scan_redirect(const char *line, size_t pos, ShellToken *token) {
    if (line[pos] == '<') {
        if (line[pos + 1] == '&') {
            token->redirect_kind = REDIRECT_DUPLICATE;
            return pos + 2;
        }
        token->redirect_kind = REDIRECT_INPUT;
        return pos + 1;
    }
    if (line[pos + 1] == '>') {
        token->redirect_kind = REDIRECT_APPEND;
        return pos + 2;
    }
    if (line[pos + 1] == '&') {
        token->redirect_kind = REDIRECT_DUPLICATE;
        return pos + 2;
    }
    token->redirect_kind = REDIRECT_OUTPUT;
    return pos + 1;
}

int lexer_tokenize(const char *line, ShellTokenList *out, const char **error) {
    size_t pos = 0;
    out->count = 0;
    for (;;) {
        ShellToken token = {0};
        size_t end = pos;
        while (is_blank(line[pos]))
            pos++;
        char c = line[pos];
        if (c == 0 || c == '#')
            break;
        if (pos >= SHELL_LINE_MAX) {
            *error = "line too long";
            return -1;
        }
        token.start = (uint16_t)pos;
        if (is_digit(c) && (line[pos + 1] == '<' || line[pos + 1] == '>')) {
            token.type = TOKEN_REDIRECT;
            token.redirect_fd = (uint8_t)(c - '0');
            end = scan_redirect(line, pos + 1, &token);
        } else if (c == '<' || c == '>') {
            token.type = TOKEN_REDIRECT;
            token.redirect_fd = (uint8_t)(c == '<' ? 0 : 1);
            end = scan_redirect(line, pos, &token);
        } else if (c == '|') {
            token.type = line[pos + 1] == '|' ? TOKEN_OR : TOKEN_PIPE;
            end = pos + (token.type == TOKEN_OR ? 2 : 1);
        } else if (c == '&') {
            token.type = line[pos + 1] == '&' ? TOKEN_AND : TOKEN_BACKGROUND;
            end = pos + (token.type == TOKEN_AND ? 2 : 1);
        } else if (c == ';') {
            token.type = TOKEN_SEQUENCE;
            end = pos + 1;
        } else {
            token.type = TOKEN_WORD;
            if (scan_word(line, pos, &end, error) != 0)
                return -1;
        }
        if (end > SHELL_LINE_MAX) {
            *error = "line too long";
            return -1;
        }
        token.length = (uint16_t)(end - pos);
        if (push_token(out, &token, error) != 0)
            return -1;
        pos = end;
    }
    ShellToken final = {0};
    final.type = TOKEN_END;
    final.start = (uint16_t)(pos < SHELL_LINE_MAX ? pos : SHELL_LINE_MAX);
    out->tokens[out->count++] = final;
    return 0;
}
