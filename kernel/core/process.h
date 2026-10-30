#pragma once

#include <stdint.h>

#include "isr.h"
#include "signal.h"
#include "vmm_user.h"

struct kernel_pipe;
struct net_socket;
struct exit_record;

enum {
    PROCESS_MAX_HANDLE = 64,
    PROCESS_POLL_MAX_FDS = PROCESS_MAX_HANDLE + 3,
    PROCESS_HANDLE_PATH_CAP = 128,
    PROCESS_CWD_CAP = 256,
    PROCESS_NAME_CAP = 32,
    PROCESS_HANDLE_FLAG_INHERIT = 1u << 0,
    PROCESS_HANDLE_FLAG_READ = 1u << 1,
    PROCESS_HANDLE_FLAG_WRITE = 1u << 2,
};

/* Mirrors the OPEN_* flags of the sys_open ABI. */
enum {
    PROCESS_OPEN_READ = 1u << 0,
    PROCESS_OPEN_WRITE = 1u << 1,
    PROCESS_OPEN_CREATE = 1u << 2,
    PROCESS_OPEN_TRUNCATE = 1u << 3,
    PROCESS_OPEN_APPEND = 1u << 4,
    PROCESS_OPEN_KNOWN_FLAGS = PROCESS_OPEN_READ | PROCESS_OPEN_WRITE |
                               PROCESS_OPEN_CREATE | PROCESS_OPEN_TRUNCATE |
                               PROCESS_OPEN_APPEND,
};

enum {
    PROCESS_DUP3_INHERIT = 1u << 0,
    PROCESS_DUP3_CLOSE_ON_SPAWN = 1u << 1,
};

typedef enum process_state {
    PROCESS_RUNNABLE = 0,
    PROCESS_BLOCKED_WAIT = 1,
    PROCESS_BLOCKED_PIPE_READ = 2,
    PROCESS_BLOCKED_PIPE_WRITE = 3,
    PROCESS_BLOCKED_POLL = 4,
    PROCESS_SLEEPING = 5,
    PROCESS_BLOCKED_KERNEL = 6,
    PROCESS_BLOCKED_SOCKET = 7,
} process_state_t;

typedef enum process_priority {
    PROCESS_PRIO_IDLE   = 0, /* background, only runs when nothing else wants to */
    PROCESS_PRIO_NORMAL = 1, /* default for all user processes */
    PROCESS_PRIO_HIGH   = 2, /* system services, init */
    PROCESS_PRIO_RT     = 3, /* real-time, always preferred */
} process_priority_t;

typedef enum process_handle_type {
    PROCESS_HANDLE_FREE = 0,
    PROCESS_HANDLE_FILE = 1,
    PROCESS_HANDLE_PIPE_READER = 2,
    PROCESS_HANDLE_PIPE_WRITER = 3,
    PROCESS_HANDLE_NULL_READER = 4,
    PROCESS_HANDLE_NULL_WRITER = 5,
    PROCESS_HANDLE_CONSOLE_READER = 6,
    PROCESS_HANDLE_CONSOLE_WRITER = 7,
    PROCESS_HANDLE_SOCKET = 8,
} process_handle_type_t;

enum {
    PROCESS_STDIO_BIND_CLEAR = -1,
    PROCESS_STDIO_BIND_INHERIT = -2,
    PROCESS_STDIO_BIND_NULL = -3,
};

enum {
    PROCESS_RESOURCE_OK = 0,
    PROCESS_RESOURCE_ERR = -1,
    PROCESS_RESOURCE_NO_HANDLE_SLOT = -2,
    PROCESS_RESOURCE_NO_KERNEL_RESOURCE = -3,
    PROCESS_RESOURCE_IO_ERROR = -4,
    PROCESS_RESOURCE_INVALID = -5,
};

enum {
    PROCESS_POLL_READ = 1u << 0,
    PROCESS_POLL_WRITE = 1u << 1,
    PROCESS_POLL_HUP = 1u << 2,
    PROCESS_POLL_NVAL = 1u << 3,
};

typedef struct process_file_handle {
    uint32_t refs;
    uint32_t offset;
    uint32_t open_flags;
    char path[PROCESS_HANDLE_PATH_CAP];
} process_file_handle_t;

typedef struct process_handle_entry {
    uint8_t used;
    uint8_t type;
    uint16_t flags;
    union {
        process_file_handle_t *file;
        struct kernel_pipe *pipe;
        struct net_socket *socket;
    } resource;
} process_handle_entry_t;

typedef struct process_poll_fd {
    int fd;
    uint32_t events;
    uint32_t revents;
} process_poll_fd_t;

/* Polled by the scheduler on every tick while a process is in
 * PROCESS_BLOCKED_KERNEL. Returns 1 and stores the syscall result once the
 * wait is over. Runs under an arbitrary address space, so it must not touch
 * user memory. */
typedef int (*process_kernel_wait_fn)(uintptr_t arg, uint64_t *out_result);

typedef struct process {
    uint64_t *pml4;
    uint32_t pid;
    uint32_t parent_pid;
    char name[PROCESS_NAME_CAP]; /* executable basename, for diagnostics only */
    char cwd[PROCESS_CWD_CAP];  /* current working directory, always absolute, default "/" */
    process_handle_entry_t handles[PROCESS_MAX_HANDLE];
    process_handle_entry_t stdio[3];
    regs_t frame;
    uint8_t has_frame;
    uint8_t alive;
    uint8_t state;
    uint8_t priority;        /* process_priority_t */
    uint32_t timeslice_ms;   /* remaining time this timeslice */
    uint64_t sleep_until_ms; /* if > 0: sleeping until time_ms() >= sleep_until_ms */
    uint32_t waiting_for_pid;
    uintptr_t waiting_status_ptr;
    uint32_t wait_result_pid;
    int wait_status;
    uint8_t wait_ready;
    /* Reserved at creation for this process's own exit, and the exits of its
     * children that have not been waited for. */
    struct exit_record *own_exit_record;
    struct exit_record *child_exit_records;
    int waiting_handle;
    uint8_t waiting_is_stdio;
    struct kernel_pipe *waiting_pipe;
    int waiting_poll_fd;
    uint32_t waiting_poll_events;
    uintptr_t waiting_poll_array_ptr;
    uint32_t waiting_poll_count;
    uint64_t waiting_poll_deadline_ms;
    uint8_t waiting_poll_has_deadline;
    uint8_t waiting_poll_net; /* poll set contains a socket */
    int waiting_socket_fd;
    /* Pipes the blocked poll watches, snapshotted at block time so a pipe
     * event can find its waiters without touching their address spaces. */
    struct kernel_pipe *waiting_poll_pipes[PROCESS_POLL_MAX_FDS];
    uint32_t waiting_poll_pipe_count;
    VmmUserSpace vm;
    signal_state_t signals;
    process_kernel_wait_fn kernel_wait_fn;
    uintptr_t kernel_wait_arg;
    struct process *next;
} process_t;

/* Read-only snapshot of a process, decoupled from process_t internals so
   introspection consumers (procfs) never hold pointers into the table. */
typedef struct process_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint8_t state;      /* process_state_t */
    uint8_t priority;   /* process_priority_t */
    uint8_t is_current;
    uint32_t handle_count;
    uint32_t user_pages;
    char name[PROCESS_NAME_CAP];
    char cwd[PROCESS_CWD_CAP];
} process_info_t;

void process_init(uint64_t *kernel_pml4);
process_t *process_create(uint64_t *pml4, uint32_t parent_pid);
process_t *process_find_by_pid(uint32_t pid);
void process_set_name(process_t *p, const char *path);
int process_get_info(uint32_t pid, process_info_t *out);
int process_next_info(uint32_t after_pid, process_info_t *out);
void process_destroy(process_t *p);
process_t *process_current(void);
process_t *process_kernel(void);
void process_set_current(process_t *p);
void process_mark_exited(process_t *p, int status);
int process_inherit_handles(process_t *dst, const process_t *src);
int process_inherit_stdio(process_t *dst, const process_t *src);
int process_bind_stdio_from_fd(process_t *dst, int stdio_fd,
                               const process_t *src, int src_fd);
int process_wait(process_t *waiter, uint32_t pid, int *out_status);
int process_begin_wait(process_t *waiter, uint32_t pid, uintptr_t status_ptr);
void process_set_frame(process_t *p, const regs_t *frame);
int process_resolve_path(const process_t *p, const char *path, char *out,
                         uint32_t out_sz);
int process_handle_open_file(process_t *p, const char *path, uint32_t flags);
int process_handle_create_pipe(process_t *p, int *out_read_handle,
                               int *out_write_handle);
int process_dup_fd(process_t *p, int src_fd);
int process_dup2_fd(process_t *p, int src_fd, int dst_fd);
int process_dup3_fd(process_t *p, int src_fd, int dst_fd, uint32_t flags);
int process_fd_set_inherit(process_t *p, int fd, int inherit);
int process_begin_pipe_read_wait(process_t *p, int handle);
int process_begin_pipe_write_wait(process_t *p, int handle);
int process_begin_pipe_read_wait_stdio(process_t *p, int stdio_fd);
int process_begin_pipe_write_wait_stdio(process_t *p, int stdio_fd);
int process_begin_fd_read_wait(process_t *p, int fd);
int process_begin_fd_write_wait(process_t *p, int fd);
int process_begin_fd_poll_wait(process_t *p, int fd, uint32_t events,
                               int64_t timeout_ms);
int process_begin_poll_many_wait(process_t *p, uintptr_t array_ptr,
                                 uint32_t count, int64_t timeout_ms);
/* Takes over the caller's reference to socket on success and returns its fd.
 * Returns PROCESS_RESOURCE_NO_HANDLE_SLOT when the handle table is full; the
 * caller then still owns the reference. */
int process_handle_attach_socket(process_t *p, struct net_socket *socket);
/* 0, -1 for an invalid fd, -2 when the fd is not a socket. */
int process_fd_socket(process_t *p, int fd, struct net_socket **out);
/* Blocks p until the socket behind fd has a datagram; the scheduler then
 * repeats the pending SYS_RECVFROM with the registers saved at syscall entry. */
int process_begin_socket_recv_wait(process_t *p, int fd);
int process_fd_seek(process_t *p, int fd, int64_t off, int whence,
                    uint64_t *out_pos);
int process_fd_poll(process_t *p, int fd, uint32_t events,
                    uint32_t *out_ready);
int process_poll_many(process_t *p, process_poll_fd_t *fds, uint32_t count,
                      uint32_t *out_ready_count);
/* process_poll_many() on a poll list in the current process' user memory;
   returns 0 or a negative errno. */
int process_poll_many_user(process_t *p, uintptr_t user_fds, uint32_t count,
                           uint32_t *out_ready_count);
int process_fd_read(process_t *p, int fd, uint8_t *out, uint32_t out_sz,
                    uint32_t *out_len);
int process_fd_write(process_t *p, int fd, const uint8_t *in, uint32_t in_sz,
                     uint32_t *out_len);
int process_handle_read(process_t *p, int handle, uint8_t *out, uint32_t out_sz,
                        uint32_t *out_len);
int process_handle_write(process_t *p, int handle, const uint8_t *in,
                         uint32_t in_sz, uint32_t *out_len);
int process_handle_close(process_t *p, int handle);
int process_stdio_read(process_t *p, int stdio_fd, uint8_t *out, uint32_t out_sz,
                       uint32_t *out_len);
int process_stdio_write(process_t *p, int stdio_fd, const uint8_t *in,
                        uint32_t in_sz, uint32_t *out_len);
int process_stdio_close(process_t *p, int stdio_fd);
int process_has_runnable_other_than(const process_t *p);
void process_set_priority(process_t *p, uint8_t prio);
void process_begin_sleep(process_t *p, uint32_t ms);
/* Aborts the blocking syscall p is in so that it returns -EINTR. Returns 1 if
 * a wait was aborted; a wait that already completed is left to finish. */
int process_interrupt_wait(process_t *p);
int process_begin_kernel_wait(process_t *p, process_kernel_wait_fn fn,
                              uintptr_t arg);
void process_wake_sleepers(void);
regs_t *process_schedule(regs_t *r);
