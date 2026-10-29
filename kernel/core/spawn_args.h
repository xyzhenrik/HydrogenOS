#pragma once

#include <stdint.h>

#include "process.h"

enum {
    SPAWN_ARGS_MAX_ENTRIES = 64,
    SPAWN_ARGS_MAX_BYTES = 3072,
};

/* Kernel-side copy of the argv/envp vectors handed to a new process. The
 * strings are packed back to back, argv entries first, each NUL-terminated. */
typedef struct spawn_args {
    uint32_t argc;
    uint32_t envc;
    uint32_t bytes;
    char strings[SPAWN_ARGS_MAX_BYTES];
} spawn_args_t;

/* Places argv/envp on the initial user stack of a process that has not run
 * yet and sets its entry registers: rdi = argc, rsi = argv, rdx = envp. */
int spawn_args_apply(process_t *proc, const spawn_args_t *args);
