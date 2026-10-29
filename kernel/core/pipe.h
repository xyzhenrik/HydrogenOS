#pragma once

#include <stdint.h>

#include "waitq.h"

typedef struct kernel_pipe kernel_pipe_t;

enum {
    KERNEL_PIPE_CAPACITY = 512,
};

int kernel_pipe_create(kernel_pipe_t **out_pipe);
int kernel_pipe_acquire_reader(kernel_pipe_t *pipe);
int kernel_pipe_acquire_writer(kernel_pipe_t *pipe);
/* Queues the pipe wakes on its own events (data, space, last end closed). A
 * waiter registers before it blocks and re-checks the pipe when woken. */
WaitQueue *kernel_pipe_read_waiters(kernel_pipe_t *pipe);
WaitQueue *kernel_pipe_write_waiters(kernel_pipe_t *pipe);
WaitQueue *kernel_pipe_poll_waiters(kernel_pipe_t *pipe);
int kernel_pipe_has_readers(const kernel_pipe_t *pipe);
int kernel_pipe_has_writers(const kernel_pipe_t *pipe);
int kernel_pipe_has_data(const kernel_pipe_t *pipe);
int kernel_pipe_has_space(const kernel_pipe_t *pipe);
int kernel_pipe_read(kernel_pipe_t *pipe, uint8_t *out, uint32_t out_sz,
                     uint32_t *out_len);
int kernel_pipe_write(kernel_pipe_t *pipe, const uint8_t *in, uint32_t in_sz,
                      uint32_t *out_len);
void kernel_pipe_close_reader(kernel_pipe_t *pipe);
void kernel_pipe_close_writer(kernel_pipe_t *pipe);
