#include "parser.h"

static int parse_command(const ShellTokenList *tokens, int *index,
                         ShellCommand *cmd, const char **error) {
    int i = *index;
    cmd->word_count = 0;
    cmd->redirect_count = 0;
    for (;;) {
        const ShellToken *token = &tokens->tokens[i];
        if (token->type == TOKEN_WORD) {
            if (cmd->word_count >= SHELL_MAX_ARGS) {
                *error = "too many arguments";
                return -1;
            }
            cmd->words[cmd->word_count++] = (uint16_t)i;
            i++;
            continue;
        }
        if (token->type == TOKEN_REDIRECT) {
            ShellRedirect *redirect;
            if (cmd->redirect_count >= SHELL_MAX_REDIRECTS) {
                *error = "too many redirections";
                return -1;
            }
            /* The list ends with TOKEN_END, so i + 1 is always in range. */
            if (tokens->tokens[i + 1].type != TOKEN_WORD) {
                *error = "missing redirection target";
                return -1;
            }
            redirect = &cmd->redirects[cmd->redirect_count++];
            redirect->kind = token->redirect_kind;
            redirect->fd = token->redirect_fd;
            redirect->target = (uint16_t)(i + 1);
            i += 2;
            continue;
        }
        break;
    }
    if (cmd->word_count == 0 && cmd->redirect_count == 0) {
        *error = "syntax error: missing command";
        return -1;
    }
    *index = i;
    return 0;
}

static int parse_pipeline(const ShellTokenList *tokens, int *index,
                          ShellPipeline *pipeline, const char **error) {
    int i = *index;
    pipeline->command_count = 0;
    pipeline->background = 0;
    pipeline->list_length = 1;
    pipeline->source_start = tokens->tokens[i].start;
    for (;;) {
        const ShellToken *last;
        if (pipeline->command_count >= SHELL_MAX_STAGES) {
            *error = "pipeline too long";
            return -1;
        }
        if (parse_command(tokens, &i,
                          &pipeline->commands[pipeline->command_count],
                          error) != 0)
            return -1;
        pipeline->command_count++;
        last = &tokens->tokens[i - 1];
        pipeline->source_end = (uint16_t)(last->start + last->length);
        if (tokens->tokens[i].type != TOKEN_PIPE)
            break;
        i++;
    }
    *index = i;
    return 0;
}

int parser_parse(const ShellTokenList *tokens, ShellProgram *out,
                 const char **error) {
    int i = 0;
    uint8_t connector = CONNECT_ALWAYS;
    int list_start = 0; /* first pipeline of the current and/or list */
    out->pipeline_count = 0;
    while (tokens->tokens[i].type != TOKEN_END) {
        ShellPipeline *pipeline;
        uint8_t type;
        if (out->pipeline_count >= SHELL_MAX_PIPELINES) {
            *error = "too many commands on one line";
            return -1;
        }
        pipeline = &out->pipelines[out->pipeline_count];
        if (parse_pipeline(tokens, &i, pipeline, error) != 0)
            return -1;
        pipeline->connector = connector;
        out->pipeline_count++;

        type = tokens->tokens[i].type;
        if (type == TOKEN_END)
            break;
        i++;
        if (type == TOKEN_SEQUENCE || type == TOKEN_BACKGROUND) {
            if (type == TOKEN_BACKGROUND) {
                ShellPipeline *first = &out->pipelines[list_start];
                first->background = 1;
                first->list_length = (uint8_t)(out->pipeline_count - list_start);
                first->source_end = pipeline->source_end;
            }
            list_start = out->pipeline_count;
            connector = CONNECT_ALWAYS;
            continue;
        }
        if (type != TOKEN_AND && type != TOKEN_OR) {
            *error = "syntax error: unexpected operator";
            return -1;
        }
        connector = type == TOKEN_AND ? CONNECT_IF_SUCCESS : CONNECT_IF_FAILURE;
        if (tokens->tokens[i].type == TOKEN_END) {
            *error = "syntax error: unexpected end of line";
            return -1;
        }
    }
    return 0;
}
