#include "process.h"
#include "core/time.h"
#include "higher_half.h"
#include "keyboard.h"
#include "pipe.h"
#include "serial.h"
#include "tty.h"
#include "fs/vfs.h"
#include "kheap.h"
#include "paging.h"
#include "net/netif.h"
#include "net/socket.h"

#define PROCESS_EXIT_SLOTS 32

typedef struct process_exit_record {
    uint32_t used;
    uint32_t pid;
    uint32_t parent_pid;
    int status;
} process_exit_record_t;

static process_t kernel_proc;
static process_t *current_proc = 0;
static process_t *process_list = 0;
static uint32_t next_pid = 1;
static process_exit_record_t exit_records[PROCESS_EXIT_SLOTS];
static char console_escape_pending[8];
static uint32_t console_escape_len = 0;
static uint32_t console_escape_pos = 0;

#define PROCESS_ERR_EIO 5
#define PROCESS_ERR_EBADF 9
#define PROCESS_ERR_EPIPE 32

static void process_handle_release(process_handle_entry_t *entry);
static void process_wake_pipe_waiters(kernel_pipe_t *pipe);
static int process_resume_blocked_socket(process_t *p);
static void process_update_socket_waits(void);
static int process_resume_blocked_io(process_t *p);
static int process_resume_blocked_poll(process_t *p);
static void process_update_poll_timeouts(void);
static int process_handle_clone(process_handle_entry_t *dst,
                                const process_handle_entry_t *src);
static uint16_t process_handle_default_flags(process_handle_type_t type);
static int process_entry_can_read(const process_handle_entry_t *entry);
static int process_entry_can_write(const process_handle_entry_t *entry);
static void process_entry_set_inherit(process_handle_entry_t *entry,
                                      int inherit);
static void process_entry_apply_dup_defaults(process_handle_entry_t *entry,
                                             int dst_fd);
static int process_dup_flags_valid(uint32_t flags);
static void process_entry_apply_dup3_flags(process_handle_entry_t *entry,
                                           int dst_fd, uint32_t flags);
static void process_console_queue_escape(const char *seq, uint32_t len);
static int process_console_take_pending(void);
static int process_console_take_char(void);
static int process_console_fallback_read(uint8_t *out, uint32_t out_sz,
                                         uint32_t *out_len);
static int process_console_fallback_write(const uint8_t *in, uint32_t in_sz,
                                          uint32_t *out_len);
static int process_stdio_binding_allowed(int stdio_fd,
                                         const process_handle_entry_t *entry);
static int process_entry_read(process_handle_entry_t *entry, uint8_t *out,
                              uint32_t out_sz, uint32_t *out_len);
static int process_entry_write(process_handle_entry_t *entry, const uint8_t *in,
                               uint32_t in_sz, uint32_t *out_len);
static int process_entry_file_size(const process_handle_entry_t *entry,
                                   uint32_t *out_size);
static int process_poll_events_valid(uint32_t events);
static int process_poll_array_ok(uintptr_t ptr, uint32_t count);
static void process_clear_poll_wait(process_t *p);

static process_handle_entry_t process_console_reader_entry = {
    .used = 1,
    .type = PROCESS_HANDLE_CONSOLE_READER,
    .flags = PROCESS_HANDLE_FLAG_READ | PROCESS_HANDLE_FLAG_INHERIT,
};

static process_handle_entry_t process_console_writer_entry = {
    .used = 1,
    .type = PROCESS_HANDLE_CONSOLE_WRITER,
    .flags = PROCESS_HANDLE_FLAG_WRITE | PROCESS_HANDLE_FLAG_INHERIT,
};

static void process_console_queue_escape(const char *seq, uint32_t len) {
    if (!seq || len == 0 || len > sizeof(console_escape_pending))
        return;
    for (uint32_t i = 0; i < len; ++i) {
        console_escape_pending[i] = seq[i];
    }
    console_escape_len = len;
    console_escape_pos = 0;
}

static int process_console_take_pending(void) {
    if (console_escape_pos >= console_escape_len)
        return -1;
    int c = (unsigned char)console_escape_pending[console_escape_pos++];
    if (console_escape_pos >= console_escape_len) {
        console_escape_pos = 0;
        console_escape_len = 0;
    }
    return c;
}

static int process_console_take_char(void) {
    int pending = process_console_take_pending();
    if (pending >= 0)
        return pending;

    int c = keyboard_read_char();
    if (c < 0) {
        c = serial_getc();
        if (c == '\r')
            c = '\n';
        return c;
    }

    if (c < 0x100)
        return c;

    switch (c) {
    case KEY_LEFT:
        process_console_queue_escape("\x1b[D", 3);
        break;
    case KEY_RIGHT:
        process_console_queue_escape("\x1b[C", 3);
        break;
    case KEY_UP:
        process_console_queue_escape("\x1b[A", 3);
        break;
    case KEY_DOWN:
        process_console_queue_escape("\x1b[B", 3);
        break;
    case KEY_HOME:
        process_console_queue_escape("\x1b[H", 3);
        break;
    case KEY_END:
        process_console_queue_escape("\x1b[F", 3);
        break;
    case KEY_DEL:
        process_console_queue_escape("\x1b[3~", 4);
        break;
    default:
        return -1;
    }

    return process_console_take_pending();
}

static int process_console_fallback_read(uint8_t *out, uint32_t out_sz,
                                         uint32_t *out_len) {
    if (!out || !out_len)
        return -1;

    *out_len = 0;
    while (*out_len < out_sz) {
        int c = process_console_take_char();
        if (c < 0) {
            /* Do not sleep with interrupts enabled inside the syscall:
               the IRQ frame would be a kernel frame and can corrupt the
               user return path. Return to user mode and let the caller retry. */
            return 0;
        }
        out[(*out_len)++] = (uint8_t)c;
        if (c == '\n')
            break;
    }
    return 0;
}

static int process_console_fallback_write(const uint8_t *in, uint32_t in_sz,
                                          uint32_t *out_len) {
    if (!in || !out_len)
        return -1;
    for (uint32_t i = 0; i < in_sz; ++i) {
        tty_putc((char)in[i]);
    }
    *out_len = in_sz;
    return 0;
}

static int process_fd_valid(int fd) {
    return fd >= 0 && fd < PROCESS_MAX_HANDLE + 3;
}

static int process_poll_events_valid(uint32_t events) {
    uint32_t known = PROCESS_POLL_READ | PROCESS_POLL_WRITE | PROCESS_POLL_HUP;
    return (events & ~known) == 0;
}

static int process_poll_array_ok(uintptr_t ptr, uint32_t count) {
    uintptr_t end;
    uint64_t len;

    if (ptr < 0x1000 || count == 0 || count > PROCESS_POLL_MAX_FDS) {
        return 0;
    }
    len = (uint64_t)count * (uint64_t)sizeof(process_poll_fd_t);
    if (len > (uint64_t)(uintptr_t)KERNEL_VMA_BASE) {
        return 0;
    }
    end = ptr + (uintptr_t)len - 1;
    if (end < ptr || end >= (uintptr_t)KERNEL_VMA_BASE) {
        return 0;
    }
    return 1;
}

static void process_clear_poll_wait(process_t *p) {
    if (!p) {
        return;
    }
    p->waiting_poll_fd = -1;
    p->waiting_poll_events = 0;
    p->waiting_poll_array_ptr = 0;
    p->waiting_poll_count = 0;
    p->waiting_poll_deadline_ms = 0;
    p->waiting_poll_has_deadline = 0;
    p->waiting_poll_net = 0;
}

static void process_handle_reset(process_handle_entry_t *entry) {
    if (!entry) return;
    entry->used = 0;
    entry->type = PROCESS_HANDLE_FREE;
    entry->flags = 0;
    entry->resource.file = 0;
}

static void process_handles_reset(process_t *p) {
    if (!p) return;
    for (int i = 0; i < PROCESS_MAX_HANDLE; ++i) {
        process_handle_reset(&p->handles[i]);
    }
    for (int i = 0; i < 3; ++i) {
        process_handle_reset(&p->stdio[i]);
    }
}

static void process_handles_release(process_t *p) {
    if (!p) return;
    for (int i = 0; i < PROCESS_MAX_HANDLE; ++i) {
        process_handle_release(&p->handles[i]);
    }
    for (int i = 0; i < 3; ++i) {
        process_handle_release(&p->stdio[i]);
    }
}

static uint16_t process_handle_default_flags(process_handle_type_t type) {
    switch (type) {
    case PROCESS_HANDLE_FILE:
    case PROCESS_HANDLE_PIPE_READER:
    case PROCESS_HANDLE_NULL_READER:
    case PROCESS_HANDLE_CONSOLE_READER:
        return PROCESS_HANDLE_FLAG_READ;
    case PROCESS_HANDLE_PIPE_WRITER:
    case PROCESS_HANDLE_NULL_WRITER:
    case PROCESS_HANDLE_CONSOLE_WRITER:
        return PROCESS_HANDLE_FLAG_WRITE;
    default:
        return 0;
    }
}

static void process_entry_set_inherit(process_handle_entry_t *entry,
                                      int inherit) {
    if (!entry)
        return;
    if (inherit) {
        entry->flags |= PROCESS_HANDLE_FLAG_INHERIT;
    } else {
        entry->flags &= (uint16_t)~PROCESS_HANDLE_FLAG_INHERIT;
    }
}

static void process_entry_apply_dup_defaults(process_handle_entry_t *entry,
                                             int dst_fd) {
    process_entry_set_inherit(entry, dst_fd >= 0 && dst_fd < 3);
}

static int process_dup_flags_valid(uint32_t flags) {
    uint32_t known = PROCESS_DUP3_INHERIT | PROCESS_DUP3_CLOSE_ON_SPAWN;
    if ((flags & ~known) != 0)
        return 0;
    if ((flags & PROCESS_DUP3_INHERIT) != 0 &&
        (flags & PROCESS_DUP3_CLOSE_ON_SPAWN) != 0)
        return 0;
    return 1;
}

static void process_entry_apply_dup3_flags(process_handle_entry_t *entry,
                                           int dst_fd, uint32_t flags) {
    process_entry_apply_dup_defaults(entry, dst_fd);
    if ((flags & PROCESS_DUP3_INHERIT) != 0) {
        process_entry_set_inherit(entry, 1);
    } else if ((flags & PROCESS_DUP3_CLOSE_ON_SPAWN) != 0) {
        process_entry_set_inherit(entry, 0);
    }
}

static uint32_t process_str_len(const char *s) {
    uint32_t len = 0;
    if (!s)
        return 0;
    while (s[len]) {
        ++len;
    }
    return len;
}

static int process_path_pop_segment(char *path, uint32_t *len) {
    if (!path || !len || *len == 0)
        return -1;
    while (*len > 1 && path[*len - 1] != '/') {
        --(*len);
    }
    if (*len > 1 && path[*len - 1] == '/') {
        --(*len);
    }
    path[*len] = 0;
    return 0;
}

static int process_path_append_segment(char *path, uint32_t *len,
                                       uint32_t cap, const char *segment,
                                       uint32_t segment_len) {
    uint32_t off;
    if (!path || !len || !segment || cap == 0 || segment_len == 0)
        return -1;
    off = *len;
    if (off == 0) {
        if (cap < 2)
            return -1;
        path[0] = '/';
        path[1] = 0;
        off = 1;
    }
    if (off > 1) {
        if (off + 1 >= cap)
            return -1;
        path[off++] = '/';
    }
    if (off + segment_len >= cap)
        return -1;
    for (uint32_t i = 0; i < segment_len; ++i) {
        path[off + i] = segment[i];
    }
    off += segment_len;
    path[off] = 0;
    *len = off;
    return 0;
}

static int process_path_consume(char *path, uint32_t *len, uint32_t cap,
                                const char *input) {
    const char *it;
    if (!path || !len || !input)
        return -1;
    it = input;
    while (*it) {
        const char *segment = 0;
        uint32_t segment_len = 0;
        while (*it == '/') {
            ++it;
        }
        if (*it == 0) {
            break;
        }
        segment = it;
        while (*it && *it != '/') {
            ++it;
            ++segment_len;
        }
        if (segment_len == 1 && segment[0] == '.') {
            continue;
        }
        if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
            if (process_path_pop_segment(path, len) != 0)
                return -1;
            continue;
        }
        if (process_path_append_segment(path, len, cap, segment, segment_len) != 0)
            return -1;
    }
    return 0;
}

int process_resolve_path(const process_t *p, const char *path, char *out,
                         uint32_t out_sz) {
    uint32_t len = 0;
    if (!p || !path || !out || out_sz < 2)
        return -1;

    out[0] = '/';
    out[1] = 0;
    len = 1;

    if (path[0] == '/') {
        return process_path_consume(out, &len, out_sz, path);
    }

    if (process_str_len(p->cwd) >= out_sz)
        return -1;
    for (len = 0; len + 1 < out_sz && p->cwd[len]; ++len) {
        out[len] = p->cwd[len];
    }
    if (p->cwd[len] != 0)
        return -1;
    if (len == 0) {
        out[0] = '/';
        out[1] = 0;
        len = 1;
    } else {
        out[len] = 0;
    }
    return process_path_consume(out, &len, out_sz, path);
}

static int process_is_runnable(const process_t *p) {
    return p && p->alive && p->has_frame && p->state == PROCESS_RUNNABLE;
}

static process_handle_entry_t *process_handle_get(process_t *p, int handle) {
    if (!p || p == &kernel_proc || handle < 0 || handle >= PROCESS_MAX_HANDLE)
        return 0;
    process_handle_entry_t *entry = &p->handles[handle];
    if (!entry->used || entry->type == PROCESS_HANDLE_FREE)
        return 0;
    return entry;
}

static process_handle_entry_t *process_handle_get_slot(process_t *p, int handle) {
    if (!p || p == &kernel_proc || handle < 0 || handle >= PROCESS_MAX_HANDLE)
        return 0;
    return &p->handles[handle];
}

static process_handle_entry_t *process_stdio_get(process_t *p, int stdio_fd) {
    if (!p || p == &kernel_proc || stdio_fd < 0 || stdio_fd >= 3)
        return 0;
    process_handle_entry_t *entry = &p->stdio[stdio_fd];
    if (!entry->used || entry->type == PROCESS_HANDLE_FREE)
        return 0;
    return entry;
}

static process_handle_entry_t *process_stdio_get_with_fallback(process_t *p,
                                                               int stdio_fd) {
    process_handle_entry_t *entry = process_stdio_get(p, stdio_fd);
    if (entry)
        return entry;
    if (!p || p == &kernel_proc || stdio_fd < 0 || stdio_fd >= 3)
        return 0;
    return (stdio_fd == 0) ? &process_console_reader_entry
                           : &process_console_writer_entry;
}

static process_handle_entry_t *process_stdio_get_slot(process_t *p, int stdio_fd) {
    if (!p || p == &kernel_proc || stdio_fd < 0 || stdio_fd >= 3)
        return 0;
    return &p->stdio[stdio_fd];
}

static const process_handle_entry_t *process_fd_get_const(const process_t *p, int fd) {
    if (!p || p == &kernel_proc || !process_fd_valid(fd))
        return 0;
    if (fd < 3) {
        return process_stdio_get_with_fallback((process_t *)p, fd);
    }
    return process_handle_get((process_t *)p, fd - 3);
}

static process_handle_entry_t *process_fd_get_slot(process_t *p, int fd) {
    if (!p || p == &kernel_proc || !process_fd_valid(fd))
        return 0;
    if (fd < 3)
        return process_stdio_get_slot(p, fd);
    return process_handle_get_slot(p, fd - 3);
}

static int process_handle_alloc(process_t *p, process_handle_type_t type) {
    if (!p || p == &kernel_proc || type == PROCESS_HANDLE_FREE)
        return -1;
    for (int i = 0; i < PROCESS_MAX_HANDLE; ++i) {
        if (!p->handles[i].used) {
            process_handle_reset(&p->handles[i]);
            p->handles[i].used = 1;
            p->handles[i].type = (uint8_t)type;
            p->handles[i].flags = process_handle_default_flags(type);
            return i;
        }
    }
    return -1;
}

static void process_handle_release(process_handle_entry_t *entry) {
    process_file_handle_t *file = 0;
    kernel_pipe_t *pipe = 0;
    if (!entry || !entry->used) {
        if (entry) process_handle_reset(entry);
        return;
    }
    switch ((process_handle_type_t)entry->type) {
    case PROCESS_HANDLE_FILE:
        file = entry->resource.file;
        if (file && file->refs > 0) {
            file->refs--;
            if (file->refs == 0) {
                kfree(file);
            }
        }
        break;
    case PROCESS_HANDLE_PIPE_READER:
        pipe = entry->resource.pipe;
        kernel_pipe_close_reader(entry->resource.pipe);
        break;
    case PROCESS_HANDLE_PIPE_WRITER:
        pipe = entry->resource.pipe;
        kernel_pipe_close_writer(entry->resource.pipe);
        break;
    case PROCESS_HANDLE_SOCKET:
        net_socket_release(entry->resource.socket);
        break;
    default:
        break;
    }
    process_handle_reset(entry);
    if (pipe) {
        process_wake_pipe_waiters(pipe);
    }
}

static int process_handle_clone(process_handle_entry_t *dst,
                                const process_handle_entry_t *src) {
    if (!dst || !src || !src->used || src->type == PROCESS_HANDLE_FREE)
        return -1;
    process_handle_reset(dst);
    dst->used = 1;
    dst->type = src->type;
    dst->flags = src->flags;
    switch ((process_handle_type_t)src->type) {
    case PROCESS_HANDLE_FILE:
        dst->resource.file = src->resource.file;
        if (!dst->resource.file) {
            process_handle_reset(dst);
            return -1;
        }
        dst->resource.file->refs++;
        return 0;
    case PROCESS_HANDLE_PIPE_READER:
        if (kernel_pipe_acquire_reader(src->resource.pipe) != 0) {
            process_handle_reset(dst);
            return -1;
        }
        dst->resource.pipe = src->resource.pipe;
        return 0;
    case PROCESS_HANDLE_PIPE_WRITER:
        if (kernel_pipe_acquire_writer(src->resource.pipe) != 0) {
            process_handle_reset(dst);
            return -1;
        }
        dst->resource.pipe = src->resource.pipe;
        return 0;
    case PROCESS_HANDLE_SOCKET:
        if (net_socket_acquire(src->resource.socket) != 0) {
            process_handle_reset(dst);
            return -1;
        }
        dst->resource.socket = src->resource.socket;
        return 0;
    case PROCESS_HANDLE_NULL_READER:
    case PROCESS_HANDLE_NULL_WRITER:
    case PROCESS_HANDLE_CONSOLE_READER:
    case PROCESS_HANDLE_CONSOLE_WRITER:
        return 0;
    default:
        process_handle_reset(dst);
        return -1;
    }
}

static int process_entry_can_read(const process_handle_entry_t *entry) {
    return entry && entry->used &&
           (entry->flags & PROCESS_HANDLE_FLAG_READ) != 0;
}

static int process_entry_can_write(const process_handle_entry_t *entry) {
    return entry && entry->used &&
           (entry->flags & PROCESS_HANDLE_FLAG_WRITE) != 0;
}

static int process_stdio_binding_allowed(int stdio_fd,
                                         const process_handle_entry_t *entry) {
    if (!entry || !entry->used)
        return 0;
    if (stdio_fd == 0)
        return process_entry_can_read(entry);
    if (stdio_fd == 1 || stdio_fd == 2)
        return process_entry_can_write(entry);
    return 0;
}

static int process_entry_read(process_handle_entry_t *entry, uint8_t *out,
                              uint32_t out_sz, uint32_t *out_len) {
    if (!entry || !out || !process_entry_can_read(entry))
        return -1;
    switch ((process_handle_type_t)entry->type) {
    case PROCESS_HANDLE_FILE:
        if (!entry->resource.file)
            return -1;
        {
            int rc = vfs_read_file_at(entry->resource.file->path,
                                      entry->resource.file->offset,
                                      out, out_sz, out_len);
            if (rc == 0) {
                entry->resource.file->offset += *out_len;
            }
            return rc;
        }
    case PROCESS_HANDLE_PIPE_READER:
        return kernel_pipe_read(entry->resource.pipe, out, out_sz, out_len);
    case PROCESS_HANDLE_NULL_READER:
        *out_len = 0;
        return 0;
    case PROCESS_HANDLE_CONSOLE_READER:
        return process_console_fallback_read(out, out_sz, out_len);
    default:
        return -1;
    }
}

/* Returns PROCESS_RESOURCE_IO_ERROR (-4) on filesystem failure so the
 * syscall layer can report EIO instead of EBADF. */
static int process_file_write(process_handle_entry_t *entry, const uint8_t *in,
                              uint32_t in_sz, uint32_t *out_len) {
    process_file_handle_t *file = entry->resource.file;
    uint32_t offset;
    if (!file)
        return -1;
    offset = file->offset;
    if ((file->open_flags & PROCESS_OPEN_APPEND) != 0) {
        vfs_stat_t st;
        if (vfs_stat(file->path, &st) != 0 || st.type != 1)
            return PROCESS_RESOURCE_IO_ERROR;
        offset = st.size;
    }
    if (in_sz > UINT32_MAX - offset)
        return PROCESS_RESOURCE_IO_ERROR;
    if (vfs_write_file_at(file->path, offset, in, in_sz) != 0)
        return PROCESS_RESOURCE_IO_ERROR;
    file->offset = offset + in_sz;
    *out_len = in_sz;
    return 0;
}

static int process_entry_write(process_handle_entry_t *entry, const uint8_t *in,
                               uint32_t in_sz, uint32_t *out_len) {
    if (!entry || !in || !out_len || !process_entry_can_write(entry))
        return -1;
    switch ((process_handle_type_t)entry->type) {
    case PROCESS_HANDLE_FILE:
        return process_file_write(entry, in, in_sz, out_len);
    case PROCESS_HANDLE_PIPE_WRITER:
        return kernel_pipe_write(entry->resource.pipe, in, in_sz, out_len);
    case PROCESS_HANDLE_NULL_WRITER:
        *out_len = in_sz;
        return 0;
    case PROCESS_HANDLE_CONSOLE_WRITER:
        return process_console_fallback_write(in, in_sz, out_len);
    default:
        return -1;
    }
}

static int process_entry_file_size(const process_handle_entry_t *entry,
                                   uint32_t *out_size) {
    vfs_stat_t st;
    if (!entry || !out_size || entry->type != PROCESS_HANDLE_FILE ||
        !entry->resource.file) {
        return -1;
    }
    if (vfs_stat(entry->resource.file->path, &st) != 0 || st.type != 1) {
        return -1;
    }
    *out_size = st.size;
    return 0;
}

static process_t *process_find_child(uint32_t parent_pid, uint32_t pid) {
    if (!process_list || pid == 0) return 0;
    process_t *it = process_list;
    do {
        if (it->pid == pid && it->parent_pid == parent_pid && it->alive) {
            return it;
        }
        it = it->next;
    } while (it && it != process_list);
    return 0;
}

process_t *process_find_by_pid(uint32_t pid) {
    if (!process_list || pid == 0) return 0;
    process_t *it = process_list;
    do {
        if (it->pid == pid && it->alive) {
            return it;
        }
        it = it->next;
    } while (it && it != process_list);
    return 0;
}

void process_set_name(process_t *p, const char *path) {
    if (!p || p == &kernel_proc || !path)
        return;
    const char *base = path;
    for (const char *c = path; *c; ++c) {
        if (*c == '/' && c[1] != '\0')
            base = c + 1;
    }
    uint32_t i = 0;
    while (base[i] && base[i] != '/' && i < PROCESS_NAME_CAP - 1) {
        p->name[i] = base[i];
        ++i;
    }
    p->name[i] = '\0';
}

static uint32_t process_count_handles(const process_t *p) {
    uint32_t count = 0;
    for (int i = 0; i < PROCESS_MAX_HANDLE; ++i) {
        if (p->handles[i].used)
            ++count;
    }
    for (int i = 0; i < 3; ++i) {
        if (p->stdio[i].used)
            ++count;
    }
    return count;
}

static void process_fill_info(const process_t *p, process_info_t *out) {
    out->pid = p->pid;
    out->parent_pid = p->parent_pid;
    out->state = p->state;
    out->priority = p->priority;
    out->is_current = (p == current_proc);
    out->handle_count = process_count_handles(p);
    out->user_pages = (uint32_t)paging_count_user_pages(p->pml4);
    for (uint32_t i = 0; i < PROCESS_NAME_CAP; ++i) {
        out->name[i] = p->name[i];
        if (p->name[i] == '\0')
            break;
    }
    out->name[PROCESS_NAME_CAP - 1] = '\0';
    for (uint32_t i = 0; i < PROCESS_CWD_CAP; ++i) {
        out->cwd[i] = p->cwd[i];
        if (p->cwd[i] == '\0')
            break;
    }
    out->cwd[PROCESS_CWD_CAP - 1] = '\0';
}

int process_get_info(uint32_t pid, process_info_t *out) {
    if (!out)
        return -1;
    const process_t *p = process_find_by_pid(pid);
    if (!p)
        return -1;
    process_fill_info(p, out);
    return 0;
}

/* Yields live processes in ascending pid order regardless of the scheduler's
   ring order, so a caller can resume iteration from any pid it last saw. */
int process_next_info(uint32_t after_pid, process_info_t *out) {
    if (!out || !process_list)
        return -1;
    const process_t *best = 0;
    const process_t *it = process_list;
    do {
        if (it->alive && it->pid > after_pid &&
            (!best || it->pid < best->pid)) {
            best = it;
        }
        it = it->next;
    } while (it && it != process_list);
    if (!best)
        return -1;
    process_fill_info(best, out);
    return 0;
}

static void process_complete_wait(process_t *p) {
    if (!p || !p->wait_ready) return;

    if (p->waiting_status_ptr != 0) {
        int *status_ptr = (int *)(uintptr_t)p->waiting_status_ptr;
        *status_ptr = p->wait_status;
    }

    p->frame.rax = p->wait_result_pid;
    p->waiting_for_pid = 0;
    p->waiting_status_ptr = 0;
    p->wait_result_pid = 0;
    p->wait_status = 0;
    p->wait_ready = 0;
}

static int process_resume_blocked_io(process_t *p) {
    if (!p || p->state != PROCESS_RUNNABLE || p->waiting_pipe == 0 ||
        p->waiting_handle < 0) {
        return 0;
    }

    process_handle_entry_t *entry = p->waiting_is_stdio ?
        process_stdio_get(p, p->waiting_handle) :
        process_handle_get(p, p->waiting_handle);
    uint8_t *buf = (uint8_t *)(uintptr_t)p->frame.rsi;
    const uint8_t *in = (const uint8_t *)(uintptr_t)p->frame.rsi;
    uint32_t len = (uint32_t)p->frame.rdx;
    uint32_t done = 0;
    int rc = -1;
    int err = PROCESS_ERR_EIO;

    if (!entry) {
        p->waiting_pipe = 0;
        p->waiting_handle = -1;
        p->waiting_is_stdio = 0;
        process_clear_poll_wait(p);
        p->frame.rax = (uint64_t)-PROCESS_ERR_EIO;
        return 0;
    }

    switch ((process_handle_type_t)entry->type) {
    case PROCESS_HANDLE_PIPE_READER:
        rc = p->waiting_is_stdio ?
            process_stdio_read(p, p->waiting_handle, buf, len, &done) :
            process_handle_read(p, p->waiting_handle, buf, len, &done);
        if (rc == 1) {
            p->state = PROCESS_BLOCKED_PIPE_READ;
            return -1;
        }
        break;
    case PROCESS_HANDLE_PIPE_WRITER:
        rc = p->waiting_is_stdio ?
            process_stdio_write(p, p->waiting_handle, in, len, &done) :
            process_handle_write(p, p->waiting_handle, in, len, &done);
        if (rc == 1) {
            p->state = PROCESS_BLOCKED_PIPE_WRITE;
            return -1;
        }
        if (rc == -2) {
            err = PROCESS_ERR_EPIPE;
        }
        break;
    default:
        break;
    }

    p->waiting_pipe = 0;
    p->waiting_handle = -1;
    p->waiting_is_stdio = 0;
    if (rc != 0) {
        p->frame.rax = (uint64_t)-err;
    } else {
        p->frame.rax = done;
    }
    return 0;
}

/*
 * Completes a SYS_RECVFROM that blocked on an empty queue. The syscall
 * arguments are still in the saved frame: rsi = buffer, rdx = capacity,
 * r8 = optional sender address; they were validated at syscall entry.
 */
static int process_resume_blocked_socket(process_t *p) {
    struct net_socket *socket = 0;
    uint32_t received = 0;
    int rc;

    if (!p || p->state != PROCESS_RUNNABLE || p->waiting_socket_fd < 0)
        return 0;

    if (process_fd_socket(p, p->waiting_socket_fd, &socket) != 0) {
        p->waiting_socket_fd = -1;
        p->frame.rax = (uint64_t)-PROCESS_ERR_EBADF;
        return 0;
    }
    rc = net_socket_recvfrom(socket, (uint8_t *)(uintptr_t)p->frame.rsi,
                             (size_t)p->frame.rdx,
                             (net_sockaddr_t *)(uintptr_t)p->frame.r8, &received);
    if (rc != 0) {
        p->state = PROCESS_BLOCKED_SOCKET;
        return -1;
    }
    p->waiting_socket_fd = -1;
    p->frame.rax = received;
    return 0;
}

static int process_resume_blocked_poll(process_t *p) {
    process_poll_fd_t *fds;
    uint32_t ready_count = 0;
    uint32_t ready = 0;
    uint64_t result = 0;

    if (!p || p->state != PROCESS_RUNNABLE ||
        (p->waiting_poll_fd < 0 && p->waiting_poll_count == 0)) {
        return 0;
    }

    if (p->waiting_poll_count > 0) {
        if (!process_poll_array_ok(p->waiting_poll_array_ptr,
                                   p->waiting_poll_count)) {
            p->waiting_pipe = 0;
            p->waiting_handle = -1;
            p->waiting_is_stdio = 0;
            process_clear_poll_wait(p);
            p->frame.rax = (uint64_t)-PROCESS_ERR_EIO;
            return 0;
        }
        fds = (process_poll_fd_t *)(uintptr_t)p->waiting_poll_array_ptr;
        if (process_poll_many(p, fds, p->waiting_poll_count, &ready_count) != 0) {
            p->waiting_pipe = 0;
            p->waiting_handle = -1;
            p->waiting_is_stdio = 0;
            process_clear_poll_wait(p);
            p->frame.rax = (uint64_t)-PROCESS_ERR_EIO;
            return 0;
        }
    } else if (process_fd_poll(p, p->waiting_poll_fd, p->waiting_poll_events,
                               &ready) != 0) {
        p->waiting_pipe = 0;
        p->waiting_handle = -1;
        p->waiting_is_stdio = 0;
        process_clear_poll_wait(p);
        p->frame.rax = (uint64_t)-PROCESS_ERR_EIO;
        return 0;
    }

    if (p->waiting_poll_count > 0 ? (ready_count == 0) : (ready == 0)) {
        if (!p->waiting_poll_has_deadline ||
            time_ms() < p->waiting_poll_deadline_ms) {
            p->state = PROCESS_BLOCKED_POLL;
            return -1;
        }
    }

    result = (p->waiting_poll_count > 0) ? (uint64_t)ready_count
                                         : (uint64_t)ready;
    p->waiting_pipe = 0;
    p->waiting_handle = -1;
    p->waiting_is_stdio = 0;
    process_clear_poll_wait(p);
    p->frame.rax = result;
    return 0;
}

static void process_wake_pipe_waiters(kernel_pipe_t *pipe) {
    if (!pipe || !process_list) return;
    process_t *it = process_list;
    do {
        if (it->alive && it->state == PROCESS_BLOCKED_POLL) {
            it->state = PROCESS_RUNNABLE;
        } else if (it->alive &&
                   (it->state == PROCESS_BLOCKED_PIPE_READ ||
                    it->state == PROCESS_BLOCKED_PIPE_WRITE) &&
                   it->waiting_pipe == pipe) {
            it->state = PROCESS_RUNNABLE;
        }
        it = it->next;
    } while (it && it != process_list);
}

static void process_update_poll_timeouts(void) {
    uint64_t now;
    process_t *it;

    if (!process_list) {
        return;
    }

    now = time_ms();
    it = process_list;
    do {
        if (it->alive &&
            it->state == PROCESS_BLOCKED_POLL &&
            it->waiting_poll_has_deadline &&
            now >= it->waiting_poll_deadline_ms) {
            it->state = PROCESS_RUNNABLE;
        }
        it = it->next;
    } while (it && it != process_list);
}

void process_init(uint64_t *kernel_pml4) {
    kernel_proc.pml4 = kernel_pml4;
    kernel_proc.pid = 0;
    kernel_proc.parent_pid = 0;
    kernel_proc.has_frame = 0;
    kernel_proc.alive = 1;
    kernel_proc.state = PROCESS_RUNNABLE;
    kernel_proc.priority = PROCESS_PRIO_HIGH;
    kernel_proc.timeslice_ms = 10;
    kernel_proc.sleep_until_ms = 0;
    kernel_proc.waiting_for_pid = 0;
    kernel_proc.waiting_status_ptr = 0;
    kernel_proc.wait_result_pid = 0;
    kernel_proc.wait_status = 0;
    kernel_proc.wait_ready = 0;
    kernel_proc.waiting_handle = -1;
    kernel_proc.waiting_is_stdio = 0;
    kernel_proc.waiting_pipe = 0;
    kernel_proc.waiting_socket_fd = -1;
    process_clear_poll_wait(&kernel_proc);
    kernel_proc.kernel_wait_fn = 0;
    kernel_proc.kernel_wait_arg = 0;
    process_handles_reset(&kernel_proc);
    kernel_proc.next = 0;
    for (int i = 0; i < PROCESS_EXIT_SLOTS; ++i) {
        exit_records[i].used = 0;
        exit_records[i].pid = 0;
        exit_records[i].parent_pid = 0;
        exit_records[i].status = 0;
    }
    current_proc = &kernel_proc;
}

process_t *process_create(uint64_t *pml4, uint32_t parent_pid) {
    if (!pml4) return 0;
    process_t *p = (process_t *)kalloc(sizeof(process_t));
    if (!p) return 0;
    p->pml4 = pml4;
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    p->name[0] = '\0';
    p->cwd[0] = '/';
    p->cwd[1] = '\0';
    p->has_frame = 0;
    p->alive = 1;
    p->state = PROCESS_RUNNABLE;
    p->priority = PROCESS_PRIO_NORMAL;
    p->timeslice_ms = 20;
    p->sleep_until_ms = 0;
    p->waiting_for_pid = 0;
    p->waiting_status_ptr = 0;
    p->wait_result_pid = 0;
    p->wait_status = 0;
    p->wait_ready = 0;
    p->waiting_handle = -1;
    p->waiting_is_stdio = 0;
    p->waiting_pipe = 0;
    p->waiting_socket_fd = -1;
    vmm_user_init(&p->vm);
    process_clear_poll_wait(p);
    p->kernel_wait_fn = 0;
    p->kernel_wait_arg = 0;
    process_handles_reset(p);
    if (!process_list) {
        process_list = p;
        p->next = p;
    } else if (current_proc && current_proc != &kernel_proc) {
        p->next = current_proc->next;
        current_proc->next = p;
    } else {
        p->next = process_list->next;
        process_list->next = p;
    }
    return p;
}

void process_destroy(process_t *p) {
    if (!p || p == &kernel_proc) return;
    if (process_list == p) {
        if (p->next == p) {
            process_list = 0;
        } else {
            process_t *prev = p;
            while (prev->next != p) prev = prev->next;
            prev->next = p->next;
            process_list = p->next;
        }
    } else if (process_list) {
        process_t *prev = process_list;
        while (prev->next != process_list && prev->next != p) {
            prev = prev->next;
        }
        if (prev->next == p) {
            prev->next = p->next;
        }
    }
    if (p->pml4 && p->pml4 != paging_kernel_pml4()) {
        vmm_user_release(&p->vm, p->pml4);
        paging_destroy_user_pml4(p->pml4);
    }
    process_handles_release(p);
    kfree(p);
}

process_t *process_current(void) { return current_proc; }

process_t *process_kernel(void) { return &kernel_proc; }

void process_set_current(process_t *p) {
    if (!p) return;
    current_proc = p;
}

void process_mark_exited(process_t *p, int status) {
    if (!p || p == &kernel_proc) return;
    p->alive = 0;
    p->state = PROCESS_RUNNABLE;

    /* Close this process's handles at exit time (not just at reap time) so
       pipe EOF/HUP propagates and processes blocked in poll/write wake up. */
    process_handles_release(p);

    if (process_list) {
        process_t *it = process_list;
        do {
            if (it->alive && it->pid == p->parent_pid &&
                it->state == PROCESS_BLOCKED_WAIT &&
                it->waiting_for_pid == p->pid) {
                it->state = PROCESS_RUNNABLE;
                it->wait_result_pid = p->pid;
                it->wait_status = status;
                it->wait_ready = 1;
                return;
            }
            it = it->next;
        } while (it && it != process_list);
    }

    int slot = -1;
    for (int i = 0; i < PROCESS_EXIT_SLOTS; ++i) {
        if (exit_records[i].used && exit_records[i].pid == p->pid) {
            slot = i;
            break;
        }
        if (slot < 0 && !exit_records[i].used) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
    }
    exit_records[slot].used = 1;
    exit_records[slot].pid = p->pid;
    exit_records[slot].parent_pid = p->parent_pid;
    exit_records[slot].status = status;
}

int process_inherit_handles(process_t *dst, const process_t *src) {
    if (!dst || !src || dst == &kernel_proc)
        return -1;
    /* Inherit CWD from parent */
    for (int i = 0; i < (int)sizeof(dst->cwd) - 1; ++i) {
        dst->cwd[i] = src->cwd[i];
        if (src->cwd[i] == '\0') break;
    }
    dst->cwd[sizeof(dst->cwd) - 1] = '\0';
    for (int i = 0; i < PROCESS_MAX_HANDLE; ++i) {
        const process_handle_entry_t *src_entry = &src->handles[i];
        if (!src_entry->used || src_entry->type == PROCESS_HANDLE_FREE)
            continue;
        if ((src_entry->flags & PROCESS_HANDLE_FLAG_INHERIT) == 0)
            continue;
        process_handle_entry_t *dst_entry = process_handle_get_slot(dst, i);
        if (!dst_entry || dst_entry->used)
            goto fail;
        if (process_handle_clone(dst_entry, src_entry) != 0)
            goto fail;
    }
    return 0;

fail:
    process_handles_release(dst);
    return -1;
}

int process_inherit_stdio(process_t *dst, const process_t *src) {
    if (!dst || !src || dst == &kernel_proc)
        return -1;
    for (int i = 0; i < 3; ++i) {
        const process_handle_entry_t *src_entry = &src->stdio[i];
        process_handle_entry_t *dst_entry = process_stdio_get_slot(dst, i);
        if (!dst_entry)
            return -1;
        if (!src_entry->used || src_entry->type == PROCESS_HANDLE_FREE) {
            process_handle_reset(dst_entry);
            continue;
        }
        if ((src_entry->flags & PROCESS_HANDLE_FLAG_INHERIT) == 0) {
            process_handle_reset(dst_entry);
            continue;
        }
        if (process_handle_clone(dst_entry, src_entry) != 0) {
            for (int j = 0; j <= i; ++j) {
                process_handle_release(&dst->stdio[j]);
            }
            return -1;
        }
    }
    return 0;
}

int process_bind_stdio_from_fd(process_t *dst, int stdio_fd,
                               const process_t *src, int src_fd) {
    process_handle_entry_t *dst_entry = process_stdio_get_slot(dst, stdio_fd);
    const process_handle_entry_t *src_entry = 0;
    if (!dst || !src || !dst_entry)
        return -1;
    if (src_fd == PROCESS_STDIO_BIND_INHERIT) {
        return 0;
    }
    process_handle_release(dst_entry);
    if (src_fd == PROCESS_STDIO_BIND_CLEAR) {
        return 0;
    }
    if (src_fd == PROCESS_STDIO_BIND_NULL) {
        process_handle_type_t type =
            (stdio_fd == 0) ? PROCESS_HANDLE_NULL_READER : PROCESS_HANDLE_NULL_WRITER;
        process_handle_reset(dst_entry);
        dst_entry->used = 1;
        dst_entry->type = (uint8_t)type;
        dst_entry->flags = process_handle_default_flags(type);
        process_entry_set_inherit(dst_entry, 1);
        return 0;
    }
    if (src_fd < 0)
        return -1;
    if (src_fd < 3)
        src_entry = process_stdio_get_with_fallback((process_t *)src, src_fd);
    else
        src_entry = process_handle_get((process_t *)src, src_fd - 3);
    if (!src_entry || !process_stdio_binding_allowed(stdio_fd, src_entry))
        return -1;
    if (process_handle_clone(dst_entry, src_entry) != 0)
        return -1;
    process_entry_set_inherit(dst_entry, 1);
    return 0;
}

int process_wait(process_t *waiter, uint32_t pid, int *out_status) {
    if (!waiter || waiter == &kernel_proc || pid == 0) return -1;
    for (int i = 0; i < PROCESS_EXIT_SLOTS; ++i) {
        if (!exit_records[i].used || exit_records[i].pid != pid ||
            exit_records[i].parent_pid != waiter->pid)
            continue;
        if (out_status) {
            *out_status = exit_records[i].status;
        }
        exit_records[i].used = 0;
        exit_records[i].pid = 0;
        exit_records[i].parent_pid = 0;
        exit_records[i].status = 0;
        return 0;
    }
    if (process_find_child(waiter->pid, pid)) {
        return 1;
    }
    return -1;
}

int process_begin_wait(process_t *waiter, uint32_t pid, uintptr_t status_ptr) {
    if (!waiter || waiter == &kernel_proc || pid == 0) return -1;
    waiter->state = PROCESS_BLOCKED_WAIT;
    waiter->waiting_for_pid = pid;
    waiter->waiting_status_ptr = status_ptr;
    waiter->wait_result_pid = 0;
    waiter->wait_status = 0;
    waiter->wait_ready = 0;
    return 0;
}

void process_set_frame(process_t *p, const regs_t *frame) {
    if (!p || !frame) return;
    p->frame = *frame;
    p->has_frame = 1;
}

static int process_open_flags_valid(uint32_t flags) {
    if ((flags & ~(uint32_t)PROCESS_OPEN_KNOWN_FLAGS) != 0)
        return 0;
    if ((flags & (PROCESS_OPEN_READ | PROCESS_OPEN_WRITE)) == 0)
        return 0;
    if ((flags & (PROCESS_OPEN_CREATE | PROCESS_OPEN_TRUNCATE |
                  PROCESS_OPEN_APPEND)) != 0 &&
        (flags & PROCESS_OPEN_WRITE) == 0)
        return 0;
    return 1;
}

int process_handle_open_file(process_t *p, const char *path, uint32_t flags) {
    static const uint8_t empty = 0;
    if (!process_open_flags_valid(flags))
        return PROCESS_RESOURCE_INVALID;
    if (!p || p == &kernel_proc || !path || !vfs_is_ready())
        return PROCESS_RESOURCE_ERR;
    char norm[PROCESS_HANDLE_PATH_CAP];
    if (process_resolve_path(p, path, norm, sizeof(norm)) != 0)
        return PROCESS_RESOURCE_ERR;
    vfs_stat_t st;
    int exists = vfs_stat(norm, &st) == 0;
    if (exists && st.type != 1)
        return PROCESS_RESOURCE_ERR;
    if (!exists && (flags & PROCESS_OPEN_CREATE) == 0)
        return PROCESS_RESOURCE_ERR;
    int handle = process_handle_alloc(p, PROCESS_HANDLE_FILE);
    if (handle < 0)
        return PROCESS_RESOURCE_NO_HANDLE_SLOT;
    process_file_handle_t *file =
        (process_file_handle_t *)kalloc(sizeof(process_file_handle_t));
    if (!file) {
        process_handle_reset(&p->handles[handle]);
        return PROCESS_RESOURCE_NO_KERNEL_RESOURCE;
    }
    /* Create/truncate only after every resource is secured, so a failed open
     * never leaves a modified file behind. */
    if ((!exists || (flags & PROCESS_OPEN_TRUNCATE) != 0) &&
        vfs_write_file(norm, &empty, 0) != 0) {
        kfree(file);
        process_handle_reset(&p->handles[handle]);
        return PROCESS_RESOURCE_IO_ERROR;
    }
    file->refs = 1;
    file->offset = 0;
    file->open_flags = flags;
    process_handle_entry_t *entry = &p->handles[handle];
    entry->flags &= (uint16_t)~(PROCESS_HANDLE_FLAG_READ | PROCESS_HANDLE_FLAG_WRITE);
    if ((flags & PROCESS_OPEN_READ) != 0)
        entry->flags |= PROCESS_HANDLE_FLAG_READ;
    if ((flags & PROCESS_OPEN_WRITE) != 0)
        entry->flags |= PROCESS_HANDLE_FLAG_WRITE;
    for (int i = 0; i < PROCESS_HANDLE_PATH_CAP; ++i) {
        file->path[i] = norm[i];
        if (norm[i] == 0) break;
    }
    entry->resource.file = file;
    return handle;
}

int process_handle_create_pipe(process_t *p, int *out_read_handle,
                               int *out_write_handle) {
    if (!p || p == &kernel_proc || !out_read_handle || !out_write_handle)
        return PROCESS_RESOURCE_ERR;
    kernel_pipe_t *pipe = 0;
    if (kernel_pipe_create(&pipe) != 0)
        return PROCESS_RESOURCE_NO_KERNEL_RESOURCE;

    int read_handle = process_handle_alloc(p, PROCESS_HANDLE_PIPE_READER);
    if (read_handle < 0) {
        kernel_pipe_close_reader(pipe);
        kernel_pipe_close_writer(pipe);
        return PROCESS_RESOURCE_NO_HANDLE_SLOT;
    }
    p->handles[read_handle].resource.pipe = pipe;

    int write_handle = process_handle_alloc(p, PROCESS_HANDLE_PIPE_WRITER);
    if (write_handle < 0) {
        process_handle_release(&p->handles[read_handle]);
        kernel_pipe_close_writer(pipe);
        return PROCESS_RESOURCE_NO_HANDLE_SLOT;
    }
    p->handles[write_handle].resource.pipe = pipe;

    *out_read_handle = read_handle;
    *out_write_handle = write_handle;
    return PROCESS_RESOURCE_OK;
}

int process_handle_attach_socket(process_t *p, struct net_socket *socket) {
    int handle = process_handle_alloc(p, PROCESS_HANDLE_SOCKET);
    if (handle < 0)
        return PROCESS_RESOURCE_NO_HANDLE_SLOT;
    p->handles[handle].resource.socket = socket;
    return handle + 3;
}

int process_fd_socket(process_t *p, int fd, struct net_socket **out) {
    const process_handle_entry_t *entry;

    if (!process_fd_valid(fd))
        return -1;
    entry = process_fd_get_const(p, fd);
    if (!entry)
        return -1;
    if (entry->type != PROCESS_HANDLE_SOCKET)
        return -2;
    *out = entry->resource.socket;
    return 0;
}

int process_begin_socket_recv_wait(process_t *p, int fd) {
    struct net_socket *socket = 0;

    if (!p || p == &kernel_proc || process_fd_socket(p, fd, &socket) != 0)
        return -1;
    p->state = PROCESS_BLOCKED_SOCKET;
    p->waiting_socket_fd = fd;
    return 0;
}

int process_dup_fd(process_t *p, int src_fd) {
    const process_handle_entry_t *src_entry = process_fd_get_const(p, src_fd);
    if (!src_entry)
        return -1;
    for (int handle = 0; handle < PROCESS_MAX_HANDLE; ++handle) {
        process_handle_entry_t *dst_entry = process_handle_get_slot(p, handle);
        if (!dst_entry || dst_entry->used)
            continue;
        if (process_handle_clone(dst_entry, src_entry) != 0)
            return -1;
        process_entry_apply_dup_defaults(dst_entry, handle + 3);
        return handle + 3;
    }
    return -2;
}

int process_dup2_fd(process_t *p, int src_fd, int dst_fd) {
    const process_handle_entry_t *src_entry = 0;
    process_handle_entry_t *dst_entry = 0;
    if (!process_fd_valid(src_fd) || !process_fd_valid(dst_fd))
        return -1;
    src_entry = process_fd_get_const(p, src_fd);
    if (!src_entry)
        return -1;
    if (src_fd == dst_fd)
        return dst_fd;
    dst_entry = process_fd_get_slot(p, dst_fd);
    if (!dst_entry)
        return -1;
    process_handle_release(dst_entry);
    if (process_handle_clone(dst_entry, src_entry) != 0) {
        process_handle_reset(dst_entry);
        return -1;
    }
    process_entry_apply_dup_defaults(dst_entry, dst_fd);
    return dst_fd;
}

int process_dup3_fd(process_t *p, int src_fd, int dst_fd, uint32_t flags) {
    const process_handle_entry_t *src_entry = 0;
    process_handle_entry_t *dst_entry = 0;
    if (!process_dup_flags_valid(flags))
        return -2;
    if (!process_fd_valid(src_fd) || !process_fd_valid(dst_fd))
        return -1;
    if (src_fd == dst_fd)
        return -2;
    src_entry = process_fd_get_const(p, src_fd);
    if (!src_entry)
        return -1;
    dst_entry = process_fd_get_slot(p, dst_fd);
    if (!dst_entry)
        return -1;
    process_handle_release(dst_entry);
    if (process_handle_clone(dst_entry, src_entry) != 0) {
        process_handle_reset(dst_entry);
        return -1;
    }
    process_entry_apply_dup3_flags(dst_entry, dst_fd, flags);
    return dst_fd;
}

int process_fd_set_inherit(process_t *p, int fd, int inherit) {
    process_handle_entry_t *entry = 0;
    if (!process_fd_valid(fd))
        return -1;
    if (fd < 3) {
        entry = process_stdio_get(p, fd);
        if (!entry)
            return 0;
    } else {
        entry = process_handle_get(p, fd - 3);
    }
    if (!entry)
        return -1;
    process_entry_set_inherit(entry, inherit != 0);
    return 0;
}

int process_begin_pipe_read_wait(process_t *p, int handle) {
    process_handle_entry_t *entry = process_handle_get(p, handle);
    if (!entry || entry->type != PROCESS_HANDLE_PIPE_READER)
        return -1;
    p->state = PROCESS_BLOCKED_PIPE_READ;
    p->waiting_handle = handle;
    p->waiting_is_stdio = 0;
    p->waiting_pipe = entry->resource.pipe;
    process_clear_poll_wait(p);
    return 0;
}

int process_begin_pipe_write_wait(process_t *p, int handle) {
    process_handle_entry_t *entry = process_handle_get(p, handle);
    if (!entry || entry->type != PROCESS_HANDLE_PIPE_WRITER)
        return -1;
    p->state = PROCESS_BLOCKED_PIPE_WRITE;
    p->waiting_handle = handle;
    p->waiting_is_stdio = 0;
    p->waiting_pipe = entry->resource.pipe;
    process_clear_poll_wait(p);
    return 0;
}

int process_begin_pipe_read_wait_stdio(process_t *p, int stdio_fd) {
    process_handle_entry_t *entry = process_stdio_get(p, stdio_fd);
    if (!entry || entry->type != PROCESS_HANDLE_PIPE_READER)
        return -1;
    p->state = PROCESS_BLOCKED_PIPE_READ;
    p->waiting_handle = stdio_fd;
    p->waiting_is_stdio = 1;
    p->waiting_pipe = entry->resource.pipe;
    process_clear_poll_wait(p);
    return 0;
}

int process_begin_pipe_write_wait_stdio(process_t *p, int stdio_fd) {
    process_handle_entry_t *entry = process_stdio_get(p, stdio_fd);
    if (!entry || entry->type != PROCESS_HANDLE_PIPE_WRITER)
        return -1;
    p->state = PROCESS_BLOCKED_PIPE_WRITE;
    p->waiting_handle = stdio_fd;
    p->waiting_is_stdio = 1;
    p->waiting_pipe = entry->resource.pipe;
    process_clear_poll_wait(p);
    return 0;
}

int process_begin_fd_read_wait(process_t *p, int fd) {
    if (fd < 0)
        return -1;
    if (fd < 3)
        return process_begin_pipe_read_wait_stdio(p, fd);
    return process_begin_pipe_read_wait(p, fd - 3);
}

int process_begin_fd_write_wait(process_t *p, int fd) {
    if (fd < 0)
        return -1;
    if (fd < 3)
        return process_begin_pipe_write_wait_stdio(p, fd);
    return process_begin_pipe_write_wait(p, fd - 3);
}

int process_begin_fd_poll_wait(process_t *p, int fd, uint32_t events,
                               int64_t timeout_ms) {
    const process_handle_entry_t *entry;

    if (!p || p == &kernel_proc || !process_fd_valid(fd)) {
        return -1;
    }
    entry = process_fd_get_const(p, fd);
    if (!entry) {
        return -1;
    }

    /*
     * waiting_pipe/waiting_handle describe a pending pipe read or write that
     * process_resume_blocked_io() completes on wake. A poll transfers no data,
     * so they must stay clear; otherwise the wake path would perform a read
     * into the poll arguments and consume the bytes the poll reported.
     */
    p->state = PROCESS_BLOCKED_POLL;
    p->waiting_handle = -1;
    p->waiting_is_stdio = 0;
    p->waiting_pipe = 0;
    p->waiting_poll_fd = fd;
    p->waiting_poll_events = events;
    p->waiting_poll_array_ptr = 0;
    p->waiting_poll_count = 0;
    p->waiting_poll_net = entry->type == PROCESS_HANDLE_SOCKET;
    if (timeout_ms >= 0) {
        p->waiting_poll_deadline_ms = time_ms() + (uint64_t)timeout_ms;
        p->waiting_poll_has_deadline = 1;
    } else {
        p->waiting_poll_deadline_ms = 0;
        p->waiting_poll_has_deadline = 0;
    }
    return 0;
}

int process_begin_poll_many_wait(process_t *p, uintptr_t array_ptr,
                                 uint32_t count, int64_t timeout_ms) {
    if (!p || p == &kernel_proc || !process_poll_array_ok(array_ptr, count)) {
        return -1;
    }

    p->state = PROCESS_BLOCKED_POLL;
    p->waiting_handle = -1;
    p->waiting_is_stdio = 0;
    p->waiting_pipe = 0;
    p->waiting_poll_fd = -1;
    p->waiting_poll_events = 0;
    p->waiting_poll_array_ptr = array_ptr;
    p->waiting_poll_count = count;
    p->waiting_poll_net = 0;
    for (uint32_t i = 0; i < count; ++i) {
        int fd = ((const process_poll_fd_t *)array_ptr)[i].fd;
        const process_handle_entry_t *entry =
            process_fd_valid(fd) ? process_fd_get_const(p, fd) : 0;
        if (entry && entry->type == PROCESS_HANDLE_SOCKET)
            p->waiting_poll_net = 1;
    }
    if (timeout_ms >= 0) {
        p->waiting_poll_deadline_ms = time_ms() + (uint64_t)timeout_ms;
        p->waiting_poll_has_deadline = 1;
    } else {
        p->waiting_poll_deadline_ms = 0;
        p->waiting_poll_has_deadline = 0;
    }
    return 0;
}

int process_fd_seek(process_t *p, int fd, int64_t off, int whence,
                    uint64_t *out_pos) {
    const process_handle_entry_t *entry;
    int64_t base = 0;
    int64_t next = 0;
    uint32_t size = 0;

    if (!out_pos || !process_fd_valid(fd)) {
        return -1;
    }
    entry = process_fd_get_const(p, fd);
    if (!entry) {
        return -1;
    }
    if (entry->type != PROCESS_HANDLE_FILE || !entry->resource.file) {
        return -2;
    }

    switch (whence) {
    case 0:
        base = 0;
        break;
    case 1:
        base = (int64_t)entry->resource.file->offset;
        break;
    case 2:
        if (process_entry_file_size(entry, &size) != 0) {
            return -1;
        }
        base = (int64_t)size;
        break;
    default:
        return -3;
    }

    next = base + off;
    if (next < 0) {
        return -3;
    }

    entry->resource.file->offset = (uint32_t)next;
    *out_pos = (uint64_t)next;
    return 0;
}

int process_fd_poll(process_t *p, int fd, uint32_t events,
                    uint32_t *out_ready) {
    const process_handle_entry_t *entry;
    uint32_t ready = 0;

    if (!out_ready || !process_fd_valid(fd)) {
        return -1;
    }
    entry = process_fd_get_const(p, fd);
    if (!entry) {
        return -1;
    }

    switch ((process_handle_type_t)entry->type) {
    case PROCESS_HANDLE_FILE:
        if ((events & PROCESS_POLL_READ) != 0 && process_entry_can_read(entry)) {
            ready |= PROCESS_POLL_READ;
        }
        if ((events & PROCESS_POLL_WRITE) != 0 && process_entry_can_write(entry)) {
            ready |= PROCESS_POLL_WRITE;
        }
        break;
    case PROCESS_HANDLE_PIPE_READER:
        if ((events & PROCESS_POLL_READ) != 0 &&
            (kernel_pipe_has_data(entry->resource.pipe) ||
             !kernel_pipe_has_writers(entry->resource.pipe))) {
            ready |= PROCESS_POLL_READ;
        }
        if ((events & PROCESS_POLL_HUP) != 0 &&
            !kernel_pipe_has_writers(entry->resource.pipe)) {
            ready |= PROCESS_POLL_HUP;
        }
        break;
    case PROCESS_HANDLE_PIPE_WRITER:
        if ((events & PROCESS_POLL_WRITE) != 0 &&
            kernel_pipe_has_readers(entry->resource.pipe) &&
            kernel_pipe_has_space(entry->resource.pipe)) {
            ready |= PROCESS_POLL_WRITE;
        }
        if ((events & PROCESS_POLL_HUP) != 0 &&
            !kernel_pipe_has_readers(entry->resource.pipe)) {
            ready |= PROCESS_POLL_HUP;
        }
        break;
    case PROCESS_HANDLE_NULL_READER:
        if ((events & PROCESS_POLL_READ) != 0) {
            ready |= PROCESS_POLL_READ;
        }
        if ((events & PROCESS_POLL_HUP) != 0) {
            ready |= PROCESS_POLL_HUP;
        }
        break;
    case PROCESS_HANDLE_SOCKET:
        if ((events & PROCESS_POLL_READ) != 0 &&
            net_socket_poll_readable(entry->resource.socket)) {
            ready |= PROCESS_POLL_READ;
        }
        /* A UDP send never waits for queue space. */
        if ((events & PROCESS_POLL_WRITE) != 0) {
            ready |= PROCESS_POLL_WRITE;
        }
        break;
    case PROCESS_HANDLE_NULL_WRITER:
    case PROCESS_HANDLE_CONSOLE_WRITER:
        if ((events & PROCESS_POLL_WRITE) != 0) {
            ready |= PROCESS_POLL_WRITE;
        }
        break;
    case PROCESS_HANDLE_CONSOLE_READER:
    default:
        break;
    }

    *out_ready = ready;
    return 0;
}

int process_poll_many(process_t *p, process_poll_fd_t *fds, uint32_t count,
                      uint32_t *out_ready_count) {
    uint32_t ready_count = 0;
    uint32_t ready = 0;

    if (!fds || !out_ready_count || count == 0 || count > PROCESS_POLL_MAX_FDS) {
        return -1;
    }

    for (uint32_t i = 0; i < count; ++i) {
        fds[i].revents = 0;
        if (!process_poll_events_valid(fds[i].events)) {
            return -2;
        }
        if (fds[i].fd == -1) {
            continue;
        }
        if (!process_fd_valid(fds[i].fd) ||
            process_fd_get_const(p, fds[i].fd) == 0) {
            fds[i].revents = PROCESS_POLL_NVAL;
            ready_count++;
            continue;
        }
        if (process_fd_poll(p, fds[i].fd, fds[i].events, &ready) != 0) {
            return -1;
        }
        fds[i].revents = ready;
        if (ready != 0) {
            ready_count++;
        }
    }

    *out_ready_count = ready_count;
    return 0;
}

int process_handle_read(process_t *p, int handle, uint8_t *out, uint32_t out_sz,
                        uint32_t *out_len) {
    if (!out)
        return -1;
    process_handle_entry_t *entry = process_handle_get(p, handle);
    if (!entry)
        return -1;
    int rc = process_entry_read(entry, out, out_sz, out_len);
    if (rc == 0 && *out_len > 0 && entry->type == PROCESS_HANDLE_PIPE_READER) {
        process_wake_pipe_waiters(entry->resource.pipe);
    }
    return rc;
}

int process_handle_write(process_t *p, int handle, const uint8_t *in,
                         uint32_t in_sz, uint32_t *out_len) {
    if (!in || !out_len)
        return -1;
    process_handle_entry_t *entry = process_handle_get(p, handle);
    if (!entry)
        return -1;
    int rc = process_entry_write(entry, in, in_sz, out_len);
    if (rc != 0)
        return rc;
    if (*out_len > 0 && entry->type == PROCESS_HANDLE_PIPE_WRITER) {
        process_wake_pipe_waiters(entry->resource.pipe);
    }
    return 0;
}

int process_fd_read(process_t *p, int fd, uint8_t *out, uint32_t out_sz,
                    uint32_t *out_len) {
    if (!out || !out_len || !process_fd_valid(fd))
        return -1;
    if (fd < 3)
        return process_stdio_read(p, fd, out, out_sz, out_len);
    return process_handle_read(p, fd - 3, out, out_sz, out_len);
}

int process_fd_write(process_t *p, int fd, const uint8_t *in, uint32_t in_sz,
                     uint32_t *out_len) {
    if (!in || !out_len || !process_fd_valid(fd))
        return -1;
    if (fd < 3)
        return process_stdio_write(p, fd, in, in_sz, out_len);
    return process_handle_write(p, fd - 3, in, in_sz, out_len);
}

int process_handle_close(process_t *p, int handle) {
    process_handle_entry_t *entry = process_handle_get(p, handle);
    if (!entry)
        return -1;
    process_handle_release(entry);
    return 0;
}

int process_stdio_read(process_t *p, int stdio_fd, uint8_t *out, uint32_t out_sz,
                       uint32_t *out_len) {
    if (!out)
        return -1;
    process_handle_entry_t *entry = process_stdio_get_with_fallback(p, stdio_fd);
    if (!entry)
        return -1;
    int rc = process_entry_read(entry, out, out_sz, out_len);
    if (rc == 0 && *out_len > 0 && entry->type == PROCESS_HANDLE_PIPE_READER) {
        process_wake_pipe_waiters(entry->resource.pipe);
    }
    return rc;
}

int process_stdio_write(process_t *p, int stdio_fd, const uint8_t *in,
                        uint32_t in_sz, uint32_t *out_len) {
    if (!in || !out_len)
        return -1;
    process_handle_entry_t *entry = process_stdio_get_with_fallback(p, stdio_fd);
    if (!entry)
        return -1;
    int rc = process_entry_write(entry, in, in_sz, out_len);
    if (rc != 0)
        return rc;
    if (*out_len > 0 && entry->type == PROCESS_HANDLE_PIPE_WRITER) {
        process_wake_pipe_waiters(entry->resource.pipe);
    }
    return 0;
}

int process_stdio_close(process_t *p, int stdio_fd) {
    process_handle_entry_t *entry = process_stdio_get(p, stdio_fd);
    if (!entry)
        return -1;
    process_handle_release(entry);
    return 0;
}

static uint32_t process_timeslice_for_prio(uint8_t prio) {
    switch (prio) {
    case PROCESS_PRIO_RT:     return 5;
    case PROCESS_PRIO_HIGH:   return 10;
    case PROCESS_PRIO_NORMAL: return 20;
    case PROCESS_PRIO_IDLE:   return 50;
    default:                  return 20;
    }
}

void process_set_priority(process_t *p, uint8_t prio) {
    if (!p) return;
    if (prio > PROCESS_PRIO_RT) prio = PROCESS_PRIO_RT;
    p->priority = prio;
    p->timeslice_ms = process_timeslice_for_prio(prio);
}

void process_begin_sleep(process_t *p, uint32_t ms) {
    if (!p) return;
    uint64_t now = time_ms();
    uint64_t until = now + (uint64_t)ms;
    /* Clamp to uint32_t range; the system uptime wraps much later */
    p->sleep_until_ms = (uint32_t)(until & 0xFFFFFFFFULL);
    p->state = PROCESS_SLEEPING;
}

int process_begin_kernel_wait(process_t *p, process_kernel_wait_fn fn,
                              uintptr_t arg) {
    if (!p || p == &kernel_proc || !fn)
        return -1;
    p->kernel_wait_fn = fn;
    p->kernel_wait_arg = arg;
    p->state = PROCESS_BLOCKED_KERNEL;
    return 0;
}

static void process_update_kernel_waits(void) {
    process_t *it;
    uint64_t result = 0;

    if (!process_list) return;

    it = process_list;
    do {
        if (it->alive && it->state == PROCESS_BLOCKED_KERNEL &&
            it->kernel_wait_fn(it->kernel_wait_arg, &result)) {
            it->kernel_wait_fn = 0;
            it->kernel_wait_arg = 0;
            it->frame.rax = result;
            it->state = PROCESS_RUNNABLE;
        }
        it = it->next;
    } while (it && it != process_list);
}

/*
 * Sockets have no interrupt-driven receive path: the NIC is polled whenever
 * some process waits on a socket. Receivers wake once their queue is
 * non-empty; poll waiters wake on any new datagram and re-check their set.
 */
static void process_update_socket_waits(void) {
    process_t *it;
    int waiting = 0;

    if (!process_list) return;

    it = process_list;
    do {
        if (it->alive && (it->state == PROCESS_BLOCKED_SOCKET ||
                          (it->state == PROCESS_BLOCKED_POLL && it->waiting_poll_net)))
            waiting = 1;
        it = it->next;
    } while (it && it != process_list);
    if (!waiting) return;

    net_poll();
    int received = net_socket_take_rx_event();
    it = process_list;
    do {
        if (it->alive && it->state == PROCESS_BLOCKED_SOCKET) {
            struct net_socket *socket = 0;
            if (process_fd_socket(it, it->waiting_socket_fd, &socket) != 0 ||
                net_socket_pending(socket))
                it->state = PROCESS_RUNNABLE;
        } else if (received && it->alive && it->state == PROCESS_BLOCKED_POLL &&
                   it->waiting_poll_net) {
            it->state = PROCESS_RUNNABLE;
        }
        it = it->next;
    } while (it && it != process_list);
}

void process_wake_sleepers(void) {
    uint32_t now;
    process_t *it;

    if (!process_list) return;

    now = (uint32_t)(time_ms() & 0xFFFFFFFFULL);
    it = process_list;
    do {
        if (it->alive && it->state == PROCESS_SLEEPING &&
            it->sleep_until_ms != 0 && now >= it->sleep_until_ms) {
            it->state = PROCESS_RUNNABLE;
            it->sleep_until_ms = 0;
            it->frame.rax = 0;
        }
        it = it->next;
    } while (it && it != process_list);
}

int process_has_runnable_other_than(process_t *p) {
    if (!process_list) return 0;
    process_t *it = process_list;
    do {
        if (it != p && process_is_runnable(it)) {
            return 1;
        }
        it = it->next;
    } while (it && it != process_list);
    return 0;
}

/*
 * Highest-priority runnable process. Among equal priorities the scan starts
 * after 'prev' so equal-priority processes alternate; 'prev' itself only wins
 * on strictly higher priority. 'prev' may be 0 (already destroyed).
 */
static process_t *process_pick_next(process_t *prev) {
    process_t *best = 0;
    process_t *start;
    process_t *it;

    if (!process_list) {
        return 0;
    }
    start = (prev && prev->next) ? prev->next : process_list;
    it = start;
    do {
        if (it != prev && process_is_runnable(it) &&
            (!best || it->priority > best->priority)) {
            best = it;
        }
        it = it->next ? it->next : process_list;
    } while (it != start);

    if (prev && process_is_runnable(prev) &&
        (!best || prev->priority > best->priority)) {
        best = prev;
    }
    return best;
}

/*
 * Completes whatever 'p' was blocked on. Returns -1 if the wake was spurious
 * and 'p' is blocked again. p's address space must be active because pending
 * results are written to user memory.
 */
static int process_resume(process_t *p) {
    process_complete_wait(p);
    if (process_resume_blocked_io(p) != 0) {
        return -1;
    }
    if (process_resume_blocked_socket(p) != 0) {
        return -1;
    }
    if (process_resume_blocked_poll(p) != 0) {
        return -1;
    }
    return 0;
}

/*
 * Priority-aware scheduler, called after every syscall and on timer ticks
 * that interrupt user mode.
 *
 * Candidates are tried in priority order until one actually resumes; a
 * candidate whose wake was spurious re-blocks and drops out. If nothing can
 * run, the CPU idles here with the kernel as current process until a timer
 * deadline makes a process runnable. 'prev' is never resumed unless it is
 * runnable: returning its frame while it is blocked or dead would make a
 * pending syscall return early or run a dead process.
 *
 * Idling is only reachable from the syscall path: a timer tick interrupts a
 * running, hence runnable, process, so process_pick_next() finds at least
 * 'prev' and the IRQ is acknowledged normally.
 */
regs_t *process_schedule(regs_t *r) {
    process_t *prev = current_proc;
    if (!prev || prev == &kernel_proc || !process_list) {
        return r;
    }

    if (prev->alive) {
        process_set_frame(prev, r);
        if (prev->timeslice_ms > 0) {
            prev->timeslice_ms--;
        }
    }

    process_wake_sleepers();
    process_update_poll_timeouts();
    process_update_kernel_waits();
    process_update_socket_waits();

    uint64_t *active_pml4 = prev->pml4;
    for (;;) {
        process_t *next = process_pick_next(prev);
        if (next) {
            process_set_current(next);
            if (next->pml4 != active_pml4) {
                paging_switch(next->pml4);
                active_pml4 = next->pml4;
            }
            if (process_resume(next) != 0) {
                continue;
            }
            if (next->timeslice_ms == 0) {
                next->timeslice_ms = process_timeslice_for_prio(next->priority);
            }
            if (prev && prev != next && !prev->alive) {
                process_destroy(prev);
            }
            return &next->frame;
        }

        process_set_current(&kernel_proc);
        active_pml4 = paging_kernel_pml4();
        paging_switch(active_pml4);
        if (prev && !prev->alive) {
            process_destroy(prev);
            prev = 0;
        }
        __asm__ volatile("sti\n\thlt\n\tcli");
        process_wake_sleepers();
        process_update_poll_timeouts();
        process_update_socket_waits();
    }
}
