#pragma once

/* Upper bounds shared by the shell modules. The shell allocates everything
 * statically, so these sizes define its whole memory footprint. */
enum {
    SHELL_LINE_MAX = 1024,
    SHELL_MAX_TOKENS = 128,
    SHELL_MAX_ARGS = 32,
    SHELL_MAX_REDIRECTS = 8,
    SHELL_MAX_STAGES = 8,
    SHELL_MAX_PIPELINES = 16,
    SHELL_EXPAND_CAP = 2048,
    SHELL_PATH_MAX = 256,
};

#define SHELL_SELF_PATH "/shell"
