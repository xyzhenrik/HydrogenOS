#include "procfs.h"

#include <stddef.h>

#include "core/process.h"
#include "core/time.h"
#include "errno.h"
#include "kheap.h"
#include "pmm.h"
#include "version.h"
#include "video/bootsplash.h"

/* Large enough for the biggest file (a status with a maximal cwd). */
#define PROCFS_RENDER_CAP 1024
#define PROCFS_LABEL_WIDTH 16
#define PROCFS_PAGE_SIZE 4096u

typedef enum procfs_node_kind {
    PROCFS_NODE_ROOT = 0,
    PROCFS_NODE_MEMINFO,
    PROCFS_NODE_UPTIME,
    PROCFS_NODE_VERSION,
    PROCFS_NODE_BOOT,
    PROCFS_NODE_PID_DIR,
    PROCFS_NODE_PID_STATUS,
} procfs_node_kind_t;

typedef struct procfs_node {
    procfs_node_kind_t kind;
    uint32_t pid;
} procfs_node_t;

typedef struct procfs_buffer {
    char data[PROCFS_RENDER_CAP];
    uint32_t len;
} procfs_buffer_t;

typedef struct procfs_static_file {
    const char *name;
    procfs_node_kind_t kind;
} procfs_static_file_t;

static const procfs_static_file_t static_files[] = {
    { "meminfo", PROCFS_NODE_MEMINFO },
    { "uptime", PROCFS_NODE_UPTIME },
    { "version", PROCFS_NODE_VERSION },
    { "boot", PROCFS_NODE_BOOT },
};

#define PROCFS_STATIC_FILE_COUNT \
    (sizeof(static_files) / sizeof(static_files[0]))

/* Compares the component [s, s+len) against a NUL-terminated name. */
static int component_eq(const char *s, uint32_t len, const char *name) {
    uint32_t i = 0;
    for (; i < len; ++i) {
        if (name[i] != s[i])
            return 0;
    }
    return name[i] == '\0';
}

/* Accepts only canonical decimal pids: no sign, no leading zero, no overflow. */
static int parse_pid(const char *s, uint32_t len, uint32_t *out) {
    if (len == 0 || s[0] == '0')
        return -1;
    uint64_t value = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        value = value * 10u + (uint64_t)(s[i] - '0');
        if (value > 0xFFFFFFFFu)
            return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int resolve(const char *path, procfs_node_t *out) {
    const char *components[2];
    uint32_t lengths[2];
    uint32_t count = 0;
    const char *p = path ? path : "";

    while (*p) {
        while (*p == '/')
            ++p;
        if (!*p)
            break;
        if (count == 2)
            return -1;
        const char *start = p;
        while (*p && *p != '/')
            ++p;
        components[count] = start;
        lengths[count] = (uint32_t)(p - start);
        ++count;
    }

    if (count == 0) {
        out->kind = PROCFS_NODE_ROOT;
        out->pid = 0;
        return 0;
    }

    if (count == 1) {
        for (uint32_t i = 0; i < PROCFS_STATIC_FILE_COUNT; ++i) {
            if (component_eq(components[0], lengths[0], static_files[i].name)) {
                out->kind = static_files[i].kind;
                out->pid = 0;
                return 0;
            }
        }
    }

    uint32_t pid = 0;
    process_info_t info;
    if (parse_pid(components[0], lengths[0], &pid) != 0 ||
        process_get_info(pid, &info) != 0)
        return -1;
    out->pid = pid;
    if (count == 1) {
        out->kind = PROCFS_NODE_PID_DIR;
        return 0;
    }
    if (component_eq(components[1], lengths[1], "status")) {
        out->kind = PROCFS_NODE_PID_STATUS;
        return 0;
    }
    return -1;
}

static int node_is_dir(const procfs_node_t *node) {
    return node->kind == PROCFS_NODE_ROOT || node->kind == PROCFS_NODE_PID_DIR;
}

static void buf_put_str(procfs_buffer_t *b, const char *s) {
    while (*s && b->len < PROCFS_RENDER_CAP)
        b->data[b->len++] = *s++;
}

static void buf_put_u64(procfs_buffer_t *b, uint64_t value) {
    char digits[20];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (n && b->len < PROCFS_RENDER_CAP)
        b->data[b->len++] = digits[--n];
}

static void buf_put_label(procfs_buffer_t *b, const char *label) {
    uint32_t start = b->len;
    buf_put_str(b, label);
    buf_put_str(b, ":");
    while (b->len - start < PROCFS_LABEL_WIDTH && b->len < PROCFS_RENDER_CAP)
        b->data[b->len++] = ' ';
}

static void buf_put_field_str(procfs_buffer_t *b, const char *label,
                              const char *value) {
    buf_put_label(b, label);
    buf_put_str(b, value);
    buf_put_str(b, "\n");
}

static void buf_put_field_u64(procfs_buffer_t *b, const char *label,
                              uint64_t value, const char *unit) {
    buf_put_label(b, label);
    buf_put_u64(b, value);
    if (unit) {
        buf_put_str(b, " ");
        buf_put_str(b, unit);
    }
    buf_put_str(b, "\n");
}

static const char *state_name(const process_info_t *info) {
    if (info->is_current)
        return "running";
    switch ((process_state_t)info->state) {
    case PROCESS_RUNNABLE:
        return "runnable";
    case PROCESS_BLOCKED_WAIT:
        return "wait";
    case PROCESS_BLOCKED_PIPE_READ:
        return "pipe-read";
    case PROCESS_BLOCKED_PIPE_WRITE:
        return "pipe-write";
    case PROCESS_BLOCKED_POLL:
        return "poll";
    case PROCESS_SLEEPING:
        return "sleeping";
    case PROCESS_BLOCKED_KERNEL:
        return "kernel-wait";
    case PROCESS_BLOCKED_SOCKET:
        return "socket-recv";
    }
    return "unknown";
}

static const char *priority_name(uint8_t priority) {
    switch ((process_priority_t)priority) {
    case PROCESS_PRIO_IDLE:
        return "idle";
    case PROCESS_PRIO_NORMAL:
        return "normal";
    case PROCESS_PRIO_HIGH:
        return "high";
    case PROCESS_PRIO_RT:
        return "rt";
    }
    return "unknown";
}

static void render_meminfo(procfs_buffer_t *b) {
    uint64_t total_pages = pmm_total_page_count();
    uint64_t free_pages = pmm_free_page_count();
    uint64_t used_pages = total_pages > free_pages ? total_pages - free_pages : 0;
    kheap_stats_t heap;
    kheap_get_stats(&heap);

    buf_put_field_u64(b, "MemTotal", total_pages * (PROCFS_PAGE_SIZE / 1024u), "kB");
    buf_put_field_u64(b, "MemFree", free_pages * (PROCFS_PAGE_SIZE / 1024u), "kB");
    buf_put_field_u64(b, "MemUsed", used_pages * (PROCFS_PAGE_SIZE / 1024u), "kB");
    buf_put_field_u64(b, "PageSize", PROCFS_PAGE_SIZE, "B");
    buf_put_field_u64(b, "PagesTotal", total_pages, 0);
    buf_put_field_u64(b, "PagesFree", free_pages, 0);
    buf_put_field_u64(b, "HeapTotal", heap.total_bytes, "B");
    buf_put_field_u64(b, "HeapUsed", heap.used_bytes, "B");
    buf_put_field_u64(b, "HeapFree", heap.free_bytes, "B");
    buf_put_field_u64(b, "HeapLargestFree", heap.largest_free_bytes, "B");
}

static void render_uptime(procfs_buffer_t *b) {
    uint64_t ms = time_ms();
    uint64_t centis = (ms % 1000u) / 10u;
    buf_put_u64(b, ms / 1000u);
    buf_put_str(b, centis < 10u ? ".0" : ".");
    buf_put_u64(b, centis);
    buf_put_str(b, "\n");
}

static void render_version(procfs_buffer_t *b) {
    buf_put_str(b, HYDROGEN_OS_NAME " " HYDROGEN_VERSION " " HYDROGEN_ARCH "\n");
}

static void render_boot(procfs_buffer_t *b) {
    const char *splash = "disabled";
    switch (bootsplash_state()) {
    case BOOTSPLASH_ACTIVE:
        splash = "active";
        break;
    case BOOTSPLASH_FINISHED:
        splash = "finished";
        break;
    case BOOTSPLASH_DISABLED:
        break;
    }
    buf_put_field_str(b, "Splash", splash);
    buf_put_field_u64(b, "Progress", bootsplash_progress(), 0);
}

static int render_status(procfs_buffer_t *b, uint32_t pid) {
    process_info_t info;
    if (process_get_info(pid, &info) != 0)
        return -1;
    buf_put_field_str(b, "Name", info.name);
    buf_put_field_u64(b, "Pid", info.pid, 0);
    buf_put_field_u64(b, "PPid", info.parent_pid, 0);
    buf_put_field_str(b, "State", state_name(&info));
    buf_put_field_str(b, "Priority", priority_name(info.priority));
    buf_put_field_u64(b, "Handles", info.handle_count, 0);
    buf_put_field_u64(b, "Pages", info.user_pages, 0);
    buf_put_field_str(b, "Cwd", info.cwd);
    return 0;
}

static int render(const procfs_node_t *node, procfs_buffer_t *b) {
    b->len = 0;
    switch (node->kind) {
    case PROCFS_NODE_MEMINFO:
        render_meminfo(b);
        return 0;
    case PROCFS_NODE_UPTIME:
        render_uptime(b);
        return 0;
    case PROCFS_NODE_VERSION:
        render_version(b);
        return 0;
    case PROCFS_NODE_BOOT:
        render_boot(b);
        return 0;
    case PROCFS_NODE_PID_STATUS:
        return render_status(b, node->pid);
    case PROCFS_NODE_ROOT:
    case PROCFS_NODE_PID_DIR:
        return -1;
    }
    return -1;
}

static void format_pid(uint32_t pid, char *out) {
    char digits[10];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + pid % 10u);
        pid /= 10u;
    } while (pid);
    uint32_t i = 0;
    while (n)
        out[i++] = digits[--n];
    out[i] = '\0';
}

int procfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx) {
    procfs_node_t node;
    if (!cb || resolve(path, &node) != 0 || !node_is_dir(&node))
        return -1;
    if (node.kind == PROCFS_NODE_PID_DIR) {
        cb("status", 1, ctx);
        return 0;
    }
    for (uint32_t i = 0; i < PROCFS_STATIC_FILE_COUNT; ++i)
        cb(static_files[i].name, 1, ctx);
    process_info_t info;
    uint32_t after = 0;
    char name[11];
    while (process_next_info(after, &info) == 0) {
        format_pid(info.pid, name);
        cb(name, 2, ctx);
        after = info.pid;
    }
    return 0;
}

int procfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                        uint32_t out_sz, uint32_t *out_len) {
    procfs_node_t node;
    if (!out || !out_len || resolve(path, &node) != 0 || node_is_dir(&node))
        return -1;
    procfs_buffer_t b;
    if (render(&node, &b) != 0)
        return -1;
    if (offset >= b.len) {
        *out_len = 0;
        return 0;
    }
    uint32_t n = b.len - offset;
    if (n > out_sz)
        n = out_sz;
    for (uint32_t i = 0; i < n; ++i)
        out[i] = (uint8_t)b.data[offset + i];
    *out_len = n;
    return 0;
}

int procfs_stat(const char *path, vfs_stat_t *out) {
    procfs_node_t node;
    if (!out || resolve(path, &node) != 0)
        return -1;
    if (node_is_dir(&node)) {
        out->type = 2;
        out->size = 0;
        return 0;
    }
    procfs_buffer_t b;
    if (render(&node, &b) != 0)
        return -1;
    out->type = 1;
    out->size = b.len;
    return 0;
}

int procfs_write_file(const char *path, const uint8_t *data, uint32_t size) {
    procfs_node_t node;
    if (resolve(path, &node) != 0)
        return -ENOENT;
    if (node_is_dir(&node))
        return -EISDIR;
    if (node.kind != PROCFS_NODE_BOOT)
        return -EROFS;
    return bootsplash_report(data, size);
}
