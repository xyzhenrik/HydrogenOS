#pragma once

#include <stdint.h>

#include "shell_limits.h"

typedef enum ShellTokenType {
    TOKEN_WORD = 0,
    TOKEN_PIPE,       /* |  */
    TOKEN_AND,        /* && */
    TOKEN_OR,         /* || */
    TOKEN_BACKGROUND, /* &  */
    TOKEN_SEQUENCE,   /* ;  */
    TOKEN_REDIRECT,   /* [n]< [n]> [n]>> [n]>& [n]<& */
    TOKEN_END,
} ShellTokenType;

typedef enum ShellRedirectKind {
    REDIRECT_INPUT = 0,
    REDIRECT_OUTPUT,
    REDIRECT_APPEND,
    REDIRECT_DUPLICATE,
} ShellRedirectKind;

/* Words keep their raw source text (quotes and escapes intact); expansion
 * happens right before a command runs so that $? sees earlier commands. */
typedef struct ShellToken {
    uint8_t type;
    uint8_t redirect_kind;
    uint8_t redirect_fd;
    uint16_t start;
    uint16_t length;
} ShellToken;

typedef struct ShellTokenList {
    ShellToken tokens[SHELL_MAX_TOKENS];
    int count;
} ShellTokenList;

/* Splits one line into tokens; the list always ends with TOKEN_END.
 * Returns 0, or -1 with a static message in *error. */
int lexer_tokenize(const char *line, ShellTokenList *out, const char **error);
