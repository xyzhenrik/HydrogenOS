#pragma once

#include <stdint.h>

#include "isr.h"
#include "spawn_args.h"

enum {
    USER_SPAWN_ERR_EXEC = -1,
    USER_SPAWN_ERR_STDIO = -2,
};

int user_spawn_from_vfs(const char *path, regs_t *parent_regs, uint64_t *out_pid);
int user_spawn_with_stdio_from_vfs(const char *path, regs_t *parent_regs,
                                   uint64_t *out_pid, int stdin_fd,
                                   int stdout_fd, int stderr_fd);
int user_spawn_args_from_vfs(const char *path, const spawn_args_t *args,
                             uint64_t *out_pid, int stdin_fd, int stdout_fd,
                             int stderr_fd);
