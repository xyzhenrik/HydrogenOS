#pragma once

#include <stdint.h>

#include "isr.h"

int user_spawn_from_vfs(const char *path, regs_t *parent_regs, uint64_t *out_pid);
int user_spawn_with_stdio_from_vfs(const char *path, regs_t *parent_regs,
                                   uint64_t *out_pid, int stdin_fd,
                                   int stdout_fd, int stderr_fd);
