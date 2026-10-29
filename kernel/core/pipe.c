#include "pipe.h"
#include "waitq.h"

enum {
    KERNEL_PIPE_POOL_SIZE = 64,
    KERNEL_PIPE_BUFFER_CAP = KERNEL_PIPE_CAPACITY,
};

struct kernel_pipe {
    uint8_t used;
    uint32_t readers;
    uint32_t writers;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t size;
    /* Blocked readers and writers wake one at a time, in arrival order;
     * pollers watch the pipe in either direction and all wake together. */
    WaitQueue read_waiters;
    WaitQueue write_waiters;
    WaitQueue poll_waiters;
    uint8_t buffer[KERNEL_PIPE_BUFFER_CAP];
};

static kernel_pipe_t pipes[KERNEL_PIPE_POOL_SIZE];

static void kernel_pipe_reset(kernel_pipe_t *pipe) {
    if (!pipe) return;
    pipe->used = 0;
    pipe->readers = 0;
    pipe->writers = 0;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->size = 0;
    wait_queue_init(&pipe->read_waiters);
    wait_queue_init(&pipe->write_waiters);
    wait_queue_init(&pipe->poll_waiters);
}

static void kernel_pipe_maybe_release(kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return;
    if (pipe->readers == 0 && pipe->writers == 0) {
        kernel_pipe_reset(pipe);
    }
}

int kernel_pipe_create(kernel_pipe_t **out_pipe) {
    if (!out_pipe) return -1;
    for (int i = 0; i < KERNEL_PIPE_POOL_SIZE; ++i) {
        if (!pipes[i].used) {
            kernel_pipe_reset(&pipes[i]);
            pipes[i].used = 1;
            pipes[i].readers = 1;
            pipes[i].writers = 1;
            *out_pipe = &pipes[i];
            return 0;
        }
    }
    return -1;
}

/*
 * A new reference must come from an existing live one, so a pipe with no
 * references of either kind cannot be revived: its slot may already be
 * reused by an unrelated pipe.
 */
static int kernel_pipe_acquire(kernel_pipe_t *pipe, uint32_t *count) {
    if (!pipe->used || (pipe->readers == 0 && pipe->writers == 0))
        return -1;
    if (*count == UINT32_MAX)
        return -1;
    (*count)++;
    return 0;
}

int kernel_pipe_acquire_reader(kernel_pipe_t *pipe) {
    if (!pipe)
        return -1;
    return kernel_pipe_acquire(pipe, &pipe->readers);
}

int kernel_pipe_acquire_writer(kernel_pipe_t *pipe) {
    if (!pipe)
        return -1;
    return kernel_pipe_acquire(pipe, &pipe->writers);
}

WaitQueue *kernel_pipe_read_waiters(kernel_pipe_t *pipe) {
    return &pipe->read_waiters;
}

WaitQueue *kernel_pipe_write_waiters(kernel_pipe_t *pipe) {
    return &pipe->write_waiters;
}

WaitQueue *kernel_pipe_poll_waiters(kernel_pipe_t *pipe) {
    return &pipe->poll_waiters;
}

int kernel_pipe_has_readers(const kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return 0;
    return pipe->readers > 0;
}

int kernel_pipe_has_writers(const kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return 0;
    return pipe->writers > 0;
}

int kernel_pipe_has_data(const kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return 0;
    return pipe->size > 0;
}

int kernel_pipe_has_space(const kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return 0;
    return pipe->size < KERNEL_PIPE_BUFFER_CAP;
}

int kernel_pipe_read(kernel_pipe_t *pipe, uint8_t *out, uint32_t out_sz,
                     uint32_t *out_len) {
    if (!pipe || !pipe->used || !out || !out_len) return -1;
    *out_len = 0;
    while (*out_len < out_sz && pipe->size > 0) {
        out[*out_len] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % KERNEL_PIPE_BUFFER_CAP;
        pipe->size--;
        (*out_len)++;
    }
    if (*out_len == 0 && pipe->writers > 0) {
        return 1;
    }
    if (*out_len > 0) {
        wait_queue_wake_one(&pipe->write_waiters, 0);
        wait_queue_wake_all(&pipe->poll_waiters, 0);
        /* A woken reader takes only what it asked for; pass the rest on. */
        if (pipe->size > 0)
            wait_queue_wake_one(&pipe->read_waiters, 0);
    }
    return 0;
}

int kernel_pipe_write(kernel_pipe_t *pipe, const uint8_t *in, uint32_t in_sz,
                      uint32_t *out_len) {
    if (!pipe || !pipe->used || !in || !out_len) return -1;
    if (pipe->readers == 0) return -2;
    *out_len = 0;
    while (*out_len < in_sz && pipe->size < KERNEL_PIPE_BUFFER_CAP) {
        pipe->buffer[pipe->write_pos] = in[*out_len];
        pipe->write_pos = (pipe->write_pos + 1) % KERNEL_PIPE_BUFFER_CAP;
        pipe->size++;
        (*out_len)++;
    }
    if (*out_len == 0 && pipe->size >= KERNEL_PIPE_BUFFER_CAP) {
        return 1;
    }
    if (*out_len > 0) {
        wait_queue_wake_one(&pipe->read_waiters, 0);
        wait_queue_wake_all(&pipe->poll_waiters, 0);
        if (pipe->size < KERNEL_PIPE_BUFFER_CAP)
            wait_queue_wake_one(&pipe->write_waiters, 0);
    }
    return 0;
}

void kernel_pipe_close_reader(kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return;
    if (pipe->readers > 0) pipe->readers--;
    if (pipe->readers == 0) {
        wait_queue_wake_all(&pipe->write_waiters, 0);
        wait_queue_wake_all(&pipe->poll_waiters, 0);
    }
    kernel_pipe_maybe_release(pipe);
}

void kernel_pipe_close_writer(kernel_pipe_t *pipe) {
    if (!pipe || !pipe->used) return;
    if (pipe->writers > 0) pipe->writers--;
    if (pipe->writers == 0) {
        wait_queue_wake_all(&pipe->read_waiters, 0);
        wait_queue_wake_all(&pipe->poll_waiters, 0);
    }
    kernel_pipe_maybe_release(pipe);
}
