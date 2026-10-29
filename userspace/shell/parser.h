#pragma once

#include <stdint.h>

#include "lexer.h"
#include "shell_limits.h"

typedef struct ShellRedirect {
    uint8_t kind;
    uint8_t fd;
    uint16_t target; /* token index of the target word */
} ShellRedirect;

typedef struct ShellCommand {
    uint16_t words[SHELL_MAX_ARGS]; /* token indices */
    uint8_t word_count;
    uint8_t redirect_count;
    ShellRedirect redirects[SHELL_MAX_REDIRECTS];
} ShellCommand;

/* How a pipeline depends on the exit status of the one before it. */
typedef enum ShellConnector {
    CONNECT_ALWAYS = 0, /* first pipeline, or after ; or & */
    CONNECT_IF_SUCCESS, /* after && */
    CONNECT_IF_FAILURE, /* after || */
} ShellConnector;

typedef struct ShellPipeline {
    ShellCommand commands[SHELL_MAX_STAGES];
    uint8_t command_count;
    uint8_t connector;
    uint8_t background;
    /* On the first pipeline of a backgrounded "a && b || c &" list, the number
     * of pipelines the list spans; otherwise 1. The list runs as one job. */
    uint8_t list_length;
    uint16_t source_start; /* byte span in the line, used for job listings */
    uint16_t source_end;   /* for a backgrounded list: through its last pipeline */
} ShellPipeline;

typedef struct ShellProgram {
    ShellPipeline pipelines[SHELL_MAX_PIPELINES];
    uint8_t pipeline_count;
} ShellProgram;

/* Builds the pipeline list for one tokenized line. Returns 0, or -1 with a
 * static message in *error. */
int parser_parse(const ShellTokenList *tokens, ShellProgram *out,
                 const char **error);
