#include "syscall.h"

#include "console.h"
#include "framebuffer.h"
#include "mouse.h"
#include "keyboard.h"

int fb_console_blit(int x, int y, int w, int h, const unsigned int *src);
int fb_console_blit_strided(int x, int y, int w, int h, const unsigned int *src, int src_stride_bytes);
void fb_console_set_cursor_visible(int visible);
void fb_console_clear(void);
#include "core/time.h"
#include "fs/vfs.h"
#include "fs/ext2.h"
#include "higher_half.h"
#include "paging.h"
#include "pmm.h"
#include "mm/pmm.h"
#include "process.h"
#include "kheap.h"
#include "user_exec.h"
#include "vmm_user.h"
#include "tty.h"
#include "stat.h"
#include "dirent.h"
#include "rtc.h"
#include "power.h"
#include "net/net.h"
#include "net/socket.h"
#include "net/udp.h"

#include <stddef.h>
#include <stdint.h>

#include "errno.h"

static process_t *bootstrap_proc = 0;
static uintptr_t user_return_rip = 0;
static uintptr_t user_return_rsp = 0;

static int poll_events_valid(uint32_t events) {
    uint32_t known = PROCESS_POLL_READ | PROCESS_POLL_WRITE | PROCESS_POLL_HUP;
    return (events & ~known) == 0;
}

static int user_range_ok(const void *ptr, size_t len) {
    if (!ptr) return 0;
    uintptr_t start = (uintptr_t)ptr;
    if (start < 0x1000) return 0;
    if (len == 0) return 1;
    uintptr_t end = start + len - 1;
    if (end < start) return 0;
    if (end >= (uintptr_t)KERNEL_VMA_BASE) return 0;
    return 1;
}

/* Like user_range_ok, but also requires the pages to be mapped for user mode
 * (and writable when 'writable'), so the kernel can access them fault-free. */
static int user_buffer_ok(const process_t *proc, const void *ptr, size_t len,
                          int writable) {
    return user_range_ok(ptr, len) && proc && proc->pml4 &&
           paging_user_range_ok(proc->pml4, (uintptr_t)ptr, len, writable);
}

/* 0 and the socket behind fd, or -EBADF / -ENOTSOCK. */
static int socket_from_fd(process_t *proc, int fd, net_socket_t **out) {
    int rc = process_fd_socket(proc, fd, out);
    if (rc == -1)
        return -EBADF;
    if (rc == -2)
        return -ENOTSOCK;
    return 0;
}

static int user_str_copy(const char *uptr, char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return -1;
    if (!user_range_ok(uptr, 1)) return -1;
    for (size_t i = 0; i < dst_sz - 1; ++i) {
        if (!user_range_ok(uptr + i, 1)) return -1;
        char c = uptr[i];
        dst[i] = c;
        if (c == 0) {
            return 0;
        }
    }
    dst[dst_sz - 1] = 0;
    return -1;
}

/* Minimal string helpers for syscall.c (kernel has no string.h) */
static size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Appends a NULL-terminated user string vector to args; returns 0 or -errno. */
static int copy_user_string_vector(const char *const *uvec, spawn_args_t *args,
                                   uint32_t *out_count) {
    uint32_t count = 0;
    *out_count = 0;
    if (!uvec)
        return 0;
    for (;;) {
        if (!user_range_ok(&uvec[count], sizeof(uvec[count])))
            return -EFAULT;
        const char *ustr = uvec[count];
        if (!ustr)
            break;
        if (args->argc + args->envc + count >= SPAWN_ARGS_MAX_ENTRIES)
            return -E2BIG;
        for (size_t i = 0;; ++i) {
            if (!user_range_ok(ustr + i, 1))
                return -EFAULT;
            if (args->bytes >= SPAWN_ARGS_MAX_BYTES)
                return -E2BIG;
            char c = ustr[i];
            args->strings[args->bytes++] = c;
            if (c == 0)
                break;
        }
        count++;
    }
    *out_count = count;
    return 0;
}

static int copy_user_spawn_args(const char *const *uargv,
                                const char *const *uenvp, spawn_args_t *args) {
    uint32_t count = 0;
    args->argc = 0;
    args->envc = 0;
    args->bytes = 0;
    int rc = copy_user_string_vector(uargv, args, &count);
    if (rc != 0)
        return rc;
    args->argc = count;
    rc = copy_user_string_vector(uenvp, args, &count);
    if (rc != 0)
        return rc;
    args->envc = count;
    return 0;
}

static int copy_resolved_user_path(process_t *proc, const char *uptr,
                                   char *dst, size_t dst_sz) {
    char raw[PROCESS_CWD_CAP];
    if (!proc || !uptr || !dst || dst_sz == 0)
        return -1;
    if (user_str_copy(uptr, raw, sizeof(raw)) != 0)
        return -1;
    if (process_resolve_path(proc, raw, dst, (uint32_t)dst_sz) != 0)
        return -1;
    return 0;
}

/* Filesystem drivers return negative errno values; anything outside the
   set a namespace operation can legitimately report is surfaced as EIO.
   EPERM is deliberately excluded: it equals the bare -1 the VFS layer
   still uses for generic failures. */
static uint64_t fs_status(int rc) {
    switch (rc) {
    case 0:
    case -ENOENT:
    case -EEXIST:
    case -ENOTDIR:
    case -EISDIR:
    case -ENOTEMPTY:
    case -ENOSPC:
    case -EINVAL:
    case -EBUSY:
    case -EROFS:
    case -EMLINK:
        return (uint64_t)(int64_t)rc;
    default:
        return (uint64_t)-EIO;
    }
}

void syscall_set_bootstrap(process_t *proc) {
    bootstrap_proc = proc;
}

void syscall_set_user_return(uintptr_t rip, uintptr_t rsp) {
    user_return_rip = rip;
    user_return_rsp = rsp;
}

static void user_return_to_kernel(regs_t *r, uint64_t status) {
    if (!r || user_return_rip == 0 || user_return_rsp == 0)
        return;
    process_set_current(process_kernel());
    paging_switch(paging_kernel_pml4());
    r->rdi = status;
    r->rip = user_return_rip;
    r->cs = 0x08;
    r->rflags = 0x202;
    r->rsp = user_return_rsp;
    r->ss = 0x10;
    user_return_rip = 0;
    user_return_rsp = 0;
}

uint64_t syscall_handle(regs_t *r) {
    switch (r->rax) {
    case SYS_EXIT: {
        process_t *exiting = process_current();
        int is_bootstrap = (exiting == bootstrap_proc);
        process_mark_exited(exiting, (int)r->rdi);
        if (is_bootstrap && !process_has_runnable_other_than(exiting)) {
            bootstrap_proc = 0;
            user_return_to_kernel(r, r->rdi);
            process_destroy(exiting);
        }
        return 0;
    }
    case SYS_SET_LAYOUT: {
        keyboard_set_layout((int)r->rdi);
        return (uint64_t)keyboard_get_layout();
    }
    case SYS_RTC_TIME: {
        hydrogen_rtc_time_t *out = (hydrogen_rtc_time_t *)(uintptr_t)r->rdi;
        if (!user_range_ok(out, sizeof(*out)))
            return (uint64_t)-EFAULT;
        hydrogen_rtc_time_t now;
        int rc = rtc_now(&now);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        *out = now;
        return 0;
    }
    case SYS_SHUTDOWN: {
        if (r->rdi != 0)
            return (uint64_t)-EINVAL;
        return (uint64_t)(int64_t)power_shutdown();
    }
    case SYS_REBOOT: {
        if (r->rdi != 0)
            return (uint64_t)-EINVAL;
        return (uint64_t)(int64_t)power_reboot();
    }
    case SYS_SYSINFO: {
        uint32_t *o = (uint32_t *)(uintptr_t)r->rdi;
        if (!o || !user_range_ok(o, sizeof(uint32_t) * 9))
            return (uint64_t)-EFAULT;
        ext2_info_t ei;
        int ext2_root = (ext2_is_mounted() && ext2_info(&ei) == 0);
        o[0] = ext2_root ? 1u : 2u;
        o[1] = ext2_root ? ei.block_size : 4096u;
        o[2] = ext2_root ? ei.blocks : 0u;
        o[3] = ext2_root ? ei.free_blocks : 0u;
        o[4] = ext2_root ? ei.inodes : 0u;
        o[5] = ext2_root ? ei.free_inodes : 0u;
        o[6] = (uint32_t)pmm_free_page_count();
        o[7] = 4096u;
        o[8] = (uint32_t)pmm_total_page_count();
        return 0;
    }
    case SYS_FB_CLEAR: {
        fb_console_clear();
        return 0;
    }
    case SYS_CONSOLE_CURSOR: {
        fb_console_set_cursor_visible((int)r->rdi);
        return 0;
    }
    case SYS_FB_BLIT_STRIDED: {
        int x = (int)r->rdi;
        int y = (int)r->rsi;
        int w = (int)r->rdx;
        int h = (int)r->r10;
        const uint32_t *src = (const uint32_t *)(uintptr_t)r->r8;
        int stride = (int)r->r9;
        if (w <= 0 || h <= 0 || stride <= 0)
            return (uint64_t)-EINVAL;
        if (!src || !user_range_ok(src, (size_t)stride * (size_t)h))
            return (uint64_t)-EFAULT;
        if (!framebuffer.addr || framebuffer.bpp != 32)
            return (uint64_t)-EIO;
        if (fb_console_blit_strided(x, y, w, h, src, stride) != 0)
            return (uint64_t)-EIO;
        return 0;
    }
    case SYS_KBD_READ: {
        int c = keyboard_read_char();
        return (uint64_t)(int64_t)c;
    }
    case SYS_MOUSE_STATE: {
        int *out = (int *)(uintptr_t)r->rdi;
        if (!out || !user_range_ok(out, sizeof(int) * 4))
            return (uint64_t)-EFAULT;
        mouse_poll();
        int mx = 0, my = 0;
        mouse_get_pos(&mx, &my);
        out[0] = mx;
        out[1] = my;
        out[2] = (int)mouse_get_buttons();
        out[3] = mouse_take_wheel();
        return 0;
    }
    case SYS_FB_INFO: {
        uint32_t *info = (uint32_t *)(uintptr_t)r->rdi;
        if (!info || !user_range_ok(info, sizeof(uint32_t) * 4))
            return (uint64_t)-EFAULT;
        if (!framebuffer.addr || framebuffer.width == 0 ||
            framebuffer.height == 0)
            return (uint64_t)-ENODEV;
        info[0] = framebuffer.width;
        info[1] = framebuffer.height;
        info[2] = framebuffer.pitch;
        info[3] = framebuffer.bpp;
        return 0;
    }
    case SYS_FB_BLIT: {
        int x = (int)r->rdi;
        int y = (int)r->rsi;
        int w = (int)r->rdx;
        int h = (int)r->r10;
        const uint32_t *src = (const uint32_t *)(uintptr_t)r->r8;
        if (w <= 0 || h <= 0)
            return (uint64_t)-EINVAL;
        if (!src || !user_range_ok(src, (size_t)w * (size_t)h * sizeof(uint32_t)))
            return (uint64_t)-EFAULT;
        if (!framebuffer.addr || framebuffer.bpp != 32)
            return (uint64_t)-EIO;
        if (fb_console_blit(x, y, w, h, src) != 0)
            return (uint64_t)-EIO;
        return 0;
    }
    case SYS_PUTCHAR:
        tty_putc((char)r->rdi);
        return 0;
    case SYS_WRITE: {
        int fd = (int)r->rdi;
        const char *buf = (const char *)(uintptr_t)r->rsi;
        size_t len = (size_t)r->rdx;
        process_t *proc = process_current();
        if (!buf || len == 0)
            return 0;
        if (!user_range_ok(buf, len))
            return (uint64_t)-EFAULT;
        if (fd < 0 || fd == 0)
            return (uint64_t)-EBADF;
        uint32_t wrote = 0;
        int rc = process_fd_write(proc, fd, (const uint8_t *)buf,
                                  (uint32_t)len, &wrote);
        if (rc == 1) {
            if (!process_has_runnable_other_than(proc)) {
                do {
                    __asm__ volatile("sti\n\thlt\n\tcli");
                    rc = process_fd_write(proc, fd, (const uint8_t *)buf,
                                          (uint32_t)len, &wrote);
                } while (rc == 1);
            } else if (process_begin_fd_write_wait(proc, fd) == 0) {
                return 0;
            } else {
                return (uint64_t)-EIO;
            }
        }
        if (rc == -2)
            return (uint64_t)-EPIPE;
        if (rc == PROCESS_RESOURCE_IO_ERROR)
            return (uint64_t)-EIO;
        if (rc != 0)
            return (uint64_t)-EBADF;
        return wrote;
    }
    case SYS_READ: {
        int fd = (int)r->rdi;
        char *buf = (char *)(uintptr_t)r->rsi;
        size_t len = (size_t)r->rdx;
        process_t *proc = process_current();
        if (!buf || len == 0)
            return 0;
        if (!user_range_ok(buf, len))
            return (uint64_t)-EFAULT;
        if (fd < 0 || fd == 1 || fd == 2)
            return (uint64_t)-EBADF;
        uint32_t got = 0;
        int rc = process_fd_read(proc, fd, (uint8_t *)buf, (uint32_t)len, &got);
        if (rc == 1) {
            if (!process_has_runnable_other_than(proc)) {
                do {
                    __asm__ volatile("sti\n\thlt\n\tcli");
                    rc = process_fd_read(proc, fd, (uint8_t *)buf,
                                         (uint32_t)len, &got);
                } while (rc == 1);
                if (rc != 0)
                    return (uint64_t)-EIO;
                return got;
            }
            if (process_begin_fd_read_wait(proc, fd) != 0)
                return (uint64_t)-EIO;
            return 0;
        }
        if (rc != 0)
            return (uint64_t)-EIO;
        return got;
    }
    case SYS_OPEN: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        uint32_t flags = (uint32_t)r->rsi;
        /* Flags 0 predates the OPEN_* flags and keeps meaning read-only. */
        if (flags == 0)
            flags = PROCESS_OPEN_READ;
        if (!path)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-ENOENT;
        process_t *proc = process_current();
        char kpath[PROCESS_HANDLE_PATH_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        int handle = process_handle_open_file(proc, kpath, flags);
        if (handle == PROCESS_RESOURCE_NO_HANDLE_SLOT)
            return (uint64_t)-EMFILE;
        if (handle == PROCESS_RESOURCE_INVALID)
            return (uint64_t)-EINVAL;
        if (handle == PROCESS_RESOURCE_IO_ERROR)
            return (uint64_t)-EIO;
        if (handle == PROCESS_RESOURCE_NO_KERNEL_RESOURCE)
            return (uint64_t)-ENOMEM;
        if (handle < 0)
            return (uint64_t)-ENOENT;
        return (uint64_t)(handle + 3);
    }
    case SYS_CLOSE: {
        int fd = (int)r->rdi;
        process_t *proc = process_current();
        if (fd >= 0 && fd < 3) {
            if (process_stdio_close(proc, fd) == 0)
                return 0;
            return (uint64_t)-EBADF;
        }
        if (fd < 3)
            return (uint64_t)-EBADF;
        int handle = fd - 3;
        if (process_handle_close(proc, handle) != 0)
            return (uint64_t)-EBADF;
        return 0;
    }
    case SYS_LSEEK: {
        int fd = (int)r->rdi;
        int64_t off = (int64_t)r->rsi;
        int whence = (int)r->rdx;
        process_t *proc = process_current();
        uint64_t pos = 0;
        int rc = process_fd_seek(proc, fd, off, whence, &pos);
        if (rc == -1)
            return (uint64_t)-EBADF;
        if (rc == -2)
            return (uint64_t)-ESPIPE;
        if (rc == -3)
            return (uint64_t)-EINVAL;
        return pos;
    }
    case SYS_POLL: {
        int fd = (int)r->rdi;
        uint32_t events = (uint32_t)r->rsi;
        process_t *proc = process_current();
        uint32_t ready = 0;
        if (!poll_events_valid(events)) {
            return (uint64_t)-EINVAL;
        }
        if (process_fd_poll(proc, fd, events, &ready) != 0)
            return (uint64_t)-EBADF;
        return ready;
    }
    case SYS_POLL_WAIT: {
        int fd = (int)r->rdi;
        uint32_t events = (uint32_t)r->rsi;
        int64_t timeout_ms = (int64_t)r->rdx;
        process_t *proc = process_current();
        uint32_t ready = 0;
        uint64_t start = 0;

        if (!poll_events_valid(events)) {
            return (uint64_t)-EINVAL;
        }
        if (timeout_ms < -1) {
            return (uint64_t)-EINVAL;
        }
        if (process_fd_poll(proc, fd, events, &ready) != 0) {
            return (uint64_t)-EBADF;
        }
        if (ready != 0 || timeout_ms == 0) {
            return ready;
        }

        if (!process_has_runnable_other_than(proc)) {
            start = time_ms();
            do {
                __asm__ volatile("sti\n\thlt\n\tcli");
                if (process_fd_poll(proc, fd, events, &ready) != 0) {
                    return (uint64_t)-EBADF;
                }
                if (ready != 0) {
                    return ready;
                }
            } while (timeout_ms == -1 ||
                     (time_ms() - start) < (uint64_t)timeout_ms);
            return 0;
        }

        if (process_begin_fd_poll_wait(proc, fd, events, timeout_ms) != 0) {
            return (uint64_t)-EIO;
        }
        return 0;
    }
    case SYS_POLL_MANY: {
        process_poll_fd_t *fds = (process_poll_fd_t *)(uintptr_t)r->rdi;
        uint32_t count = (uint32_t)r->rsi;
        int64_t timeout_ms = (int64_t)r->rdx;
        process_t *proc = process_current();
        uint32_t ready_count = 0;
        uint64_t start = 0;
        int rc;

        if (timeout_ms < -1) {
            return (uint64_t)-EINVAL;
        }
        if (count == 0 || count > PROCESS_POLL_MAX_FDS) {
            return (uint64_t)-EINVAL;
        }
        if (!user_range_ok(fds, sizeof(process_poll_fd_t) * count)) {
            return (uint64_t)-EFAULT;
        }

        rc = process_poll_many(proc, fds, count, &ready_count);
        if (rc == -2) {
            return (uint64_t)-EINVAL;
        }
        if (rc != 0) {
            return (uint64_t)-EBADF;
        }
        if (ready_count != 0 || timeout_ms == 0) {
            return ready_count;
        }

        if (!process_has_runnable_other_than(proc)) {
            start = time_ms();
            do {
                __asm__ volatile("sti\n\thlt\n\tcli");
                rc = process_poll_many(proc, fds, count, &ready_count);
                if (rc == -2) {
                    return (uint64_t)-EINVAL;
                }
                if (rc != 0) {
                    return (uint64_t)-EBADF;
                }
                if (ready_count != 0) {
                    return ready_count;
                }
            } while (timeout_ms == -1 ||
                     (time_ms() - start) < (uint64_t)timeout_ms);
            return 0;
        }

        if (process_begin_poll_many_wait(proc, (uintptr_t)fds, count, timeout_ms) != 0) {
            return (uint64_t)-EIO;
        }
        return 0;
    }
    case SYS_SPAWN: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        uint64_t pid = 0;
        int rc = user_spawn_from_vfs(kpath, r, &pid);
        if (rc != 0)
            return (uint64_t)-ENOENT;
        return pid;
    }
    case SYS_SPAWN_WITH_STDIO: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        int stdin_fd = (int)r->rsi;
        int stdout_fd = (int)r->rdx;
        int stderr_fd = (int)r->r10;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        uint64_t pid = 0;
        int rc = user_spawn_with_stdio_from_vfs(kpath, r, &pid,
                                                stdin_fd, stdout_fd, stderr_fd);
        if (rc != 0)
            return (uint64_t)-EBADF;
        return pid;
    }
    case SYS_SPAWN_ARGS: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        const char *const *argv = (const char *const *)(uintptr_t)r->rsi;
        const char *const *envp = (const char *const *)(uintptr_t)r->rdx;
        int stdin_fd = (int)r->r10;
        int stdout_fd = (int)r->r8;
        int stderr_fd = (int)r->r9;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        spawn_args_t *args = (spawn_args_t *)kalloc(sizeof(spawn_args_t));
        if (!args)
            return (uint64_t)-ENOMEM;
        int rc = copy_user_spawn_args(argv, envp, args);
        if (rc != 0) {
            kfree(args);
            return (uint64_t)(int64_t)rc;
        }
        uint64_t pid = 0;
        rc = user_spawn_args_from_vfs(kpath, args, &pid, stdin_fd, stdout_fd,
                                      stderr_fd);
        kfree(args);
        if (rc == USER_SPAWN_ERR_STDIO)
            return (uint64_t)-EBADF;
        if (rc != 0)
            return (uint64_t)-ENOENT;
        return pid;
    }
    case SYS_TRY_WAIT: {
        uint32_t pid = (uint32_t)r->rdi;
        int *status_ptr = (int *)(uintptr_t)r->rsi;
        int status = 0;
        process_t *proc = process_current();
        if (pid == 0)
            return (uint64_t)-EINVAL;
        if (status_ptr && !user_range_ok(status_ptr, sizeof(int)))
            return (uint64_t)-EFAULT;
        int rc = process_wait(proc, pid, &status);
        if (rc < 0)
            return (uint64_t)-ENOENT;
        if (rc > 0)
            return 0;
        if (status_ptr)
            *status_ptr = status;
        return pid;
    }
    case SYS_WAIT: {
        uint32_t pid = (uint32_t)r->rdi;
        int *status_ptr = (int *)(uintptr_t)r->rsi;
        int status = 0;
        process_t *proc = process_current();
        if (pid == 0)
            return (uint64_t)-EINVAL;
        if (status_ptr && !user_range_ok(status_ptr, sizeof(int)))
            return (uint64_t)-EFAULT;
        int rc = process_wait(proc, pid, &status);
        if (rc < 0)
            return (uint64_t)-ENOENT;
        if (rc == 0) {
            if (status_ptr)
                *status_ptr = status;
            return pid;
        }
        if (process_begin_wait(proc, pid, (uintptr_t)status_ptr) != 0)
            return (uint64_t)-EINVAL;
        return 0;
    }
    case SYS_GETPID: {
        process_t *proc = process_current();
        return proc ? proc->pid : 0;
    }
    case SYS_YIELD:
        return 0;
    case SYS_SLEEP: {
        uint32_t ms = (uint32_t)r->rdi;
        process_t *proc = process_current();
        if (ms == 0)
            return 0;
        /* If we are the only runnable process, busy-wait with hlt */
        if (!process_has_runnable_other_than(proc)) {
            uint64_t start = time_ms();
            while ((time_ms() - start) < (uint64_t)ms) {
                __asm__ volatile("sti\n\thlt\n\tcli");
            }
            return 0;
        }
        /* Otherwise put the process to sleep; the scheduler will wake it */
        process_begin_sleep(proc, ms);
        return 0;
    }
    case SYS_TIME_MS:
        return (uint64_t)time_ms();
    case SYS_TTY_SET_COLOR:
        tty_set_color((unsigned char)(r->rdi & 0xFF), (unsigned char)(r->rsi & 0xFF));
        return 0;
    case SYS_PIPE: {
        int *fds = (int *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        int read_handle = -1;
        int write_handle = -1;
        if (!fds || !user_range_ok(fds, sizeof(int) * 2))
            return (uint64_t)-EFAULT;
        int rc = process_handle_create_pipe(proc, &read_handle, &write_handle);
        if (rc == PROCESS_RESOURCE_NO_HANDLE_SLOT)
            return (uint64_t)-EMFILE;
        if (rc != 0)
            return (uint64_t)-EIO;
        fds[0] = read_handle + 3;
        fds[1] = write_handle + 3;
        return 0;
    }
    case SYS_HANDLE_SET_INHERIT: {
        int fd = (int)r->rdi;
        int inherit = (int)r->rsi;
        process_t *proc = process_current();
        if (process_fd_set_inherit(proc, fd, inherit != 0) != 0)
            return (uint64_t)-EBADF;
        return 0;
    }
    case SYS_DUP: {
        process_t *proc = process_current();
        int fd = (int)r->rdi;
        int rc = process_dup_fd(proc, fd);
        if (rc == -1)
            return (uint64_t)-EBADF;
        if (rc == -2)
            return (uint64_t)-EMFILE;
        return (uint64_t)rc;
    }
    case SYS_DUP2: {
        process_t *proc = process_current();
        int src_fd = (int)r->rdi;
        int dst_fd = (int)r->rsi;
        int rc = process_dup2_fd(proc, src_fd, dst_fd);
        if (rc < 0)
            return (uint64_t)-EBADF;
        return (uint64_t)rc;
    }
    case SYS_DUP3: {
        process_t *proc = process_current();
        int src_fd = (int)r->rdi;
        int dst_fd = (int)r->rsi;
        uint32_t flags = (uint32_t)r->rdx;
        int rc = process_dup3_fd(proc, src_fd, dst_fd, flags);
        if (rc == -1)
            return (uint64_t)-EBADF;
        if (rc == -2)
            return (uint64_t)-EINVAL;
        return (uint64_t)rc;
    }
    case SYS_SET_PRIORITY: {
        int prio = (int)r->rdi;
        process_t *proc = process_current();
        if (prio < PROCESS_PRIO_IDLE || prio > PROCESS_PRIO_RT)
            return (uint64_t)-EINVAL;
        process_set_priority(proc, (uint8_t)prio);
        return 0;
    }
    case SYS_STAT: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        hydrogen_stat_t *buf = (hydrogen_stat_t *)(uintptr_t)r->rsi;
        process_t *proc = process_current();
        char kpath[PROCESS_CWD_CAP];
        if (!path)
            return (uint64_t)-EFAULT;
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (!buf || !user_range_ok(buf, sizeof(hydrogen_stat_t)))
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-ENOENT;
        hydrogen_stat_t kst;
        if (vfs_stat_full(kpath, &kst) != 0)
            return (uint64_t)-ENOENT;
        *buf = kst;
        return 0;
    }
    case SYS_FSTAT: {
        int fd = (int)r->rdi;
        hydrogen_stat_t *buf = (hydrogen_stat_t *)(uintptr_t)r->rsi;
        process_t *proc = process_current();
        if (!buf || !user_range_ok(buf, sizeof(hydrogen_stat_t)))
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-ENOENT;
        if (fd < 3)
            return (uint64_t)-EBADF;
        int handle = fd - 3;
        if (handle < 0 || handle >= PROCESS_MAX_HANDLE)
            return (uint64_t)-EBADF;
        process_handle_entry_t *he = &proc->handles[handle];
        if (!he->used || he->type != PROCESS_HANDLE_FILE)
            return (uint64_t)-EBADF;
        const char *fpath = he->resource.file->path;
        hydrogen_stat_t kst;
        if (vfs_stat_full(fpath, &kst) != 0)
            return (uint64_t)-ENOENT;
        *buf = kst;
        return 0;
    }
    case SYS_READDIR: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        int pos = (int)r->rsi;
        hydrogen_dirent_t *buf = (hydrogen_dirent_t *)(uintptr_t)r->rdx;
        process_t *proc = process_current();
        char kpath[PROCESS_CWD_CAP];
        if (!path)
            return (uint64_t)-EFAULT;
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (!buf || !user_range_ok(buf, sizeof(hydrogen_dirent_t)))
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-ENOENT;
        hydrogen_dirent_t kd;
        int rc = vfs_readdir(kpath, &pos, &kd);
        if (rc < 0)
            return (uint64_t)-ENOENT;
        if (rc == 0)
            return 0; /* end of directory */
        *buf = kd;
        return 1;
    }
    case SYS_GETCWD: {
        char *buf = (char *)(uintptr_t)r->rdi;
        size_t size = (size_t)r->rsi;
        if (!buf || size == 0)
            return (uint64_t)-EINVAL;
        if (!user_range_ok(buf, size))
            return (uint64_t)-EFAULT;
        process_t *proc = process_current();
        size_t cwd_len = k_strlen(proc->cwd) + 1;
        if (cwd_len > size)
            return (uint64_t)-ERANGE;
        for (size_t i = 0; i < cwd_len; i++)
            buf[i] = proc->cwd[i];
        return 0;
    }
    case SYS_CHDIR: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char newcwd[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, newcwd, sizeof(newcwd)) != 0)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-ENOENT;
        vfs_stat_t st;
        if (vfs_stat(newcwd, &st) != 0)
            return (uint64_t)-ENOENT;
        if (st.type != 2)
            return (uint64_t)-ENOTDIR;
        for (size_t i = 0; i < sizeof(proc->cwd); ++i) {
            proc->cwd[i] = newcwd[i];
            if (newcwd[i] == 0) {
                break;
            }
        }
        proc->cwd[sizeof(proc->cwd) - 1] = 0;
        return 0;
    }
    case SYS_MKDIR: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-EIO;
        return fs_status(vfs_mkdir(kpath));
    }
    case SYS_RMDIR: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-EIO;
        return fs_status(vfs_rmdir(kpath));
    }
    case SYS_UNLINK: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-EIO;
        return fs_status(vfs_unlink(kpath));
    }
    case SYS_RENAME: {
        const char *old_path = (const char *)(uintptr_t)r->rdi;
        const char *new_path = (const char *)(uintptr_t)r->rsi;
        process_t *proc = process_current();
        if (!old_path || !new_path)
            return (uint64_t)-EFAULT;
        char kold[PROCESS_CWD_CAP];
        char knew[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, old_path, kold, sizeof(kold)) != 0)
            return (uint64_t)-EFAULT;
        if (copy_resolved_user_path(proc, new_path, knew, sizeof(knew)) != 0)
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-EIO;
        return fs_status(vfs_rename(kold, knew));
    }
    case SYS_WRITEF: {
        const char *path = (const char *)(uintptr_t)r->rdi;
        const uint8_t *data = (const uint8_t *)(uintptr_t)r->rsi;
        uint32_t size = (uint32_t)r->rdx;
        process_t *proc = process_current();
        if (!path)
            return (uint64_t)-EFAULT;
        char kpath[PROCESS_CWD_CAP];
        if (copy_resolved_user_path(proc, path, kpath, sizeof(kpath)) != 0)
            return (uint64_t)-EFAULT;
        if (size > 0 && !user_range_ok(data, size))
            return (uint64_t)-EFAULT;
        if (!vfs_is_ready())
            return (uint64_t)-EIO;
        return fs_status(vfs_write_file(kpath, data, size));
    }
    case SYS_MMAP: {
        process_t *proc = process_current();
        if (!proc || !proc->pml4 || proc->pml4 == paging_kernel_pml4())
            return (uint64_t)-EPERM;
        uintptr_t addr = 0;
        int rc = vmm_user_map(&proc->vm, proc->pml4, (uintptr_t)r->rdi,
                              (size_t)r->rsi, (uint32_t)r->rdx,
                              (uint32_t)r->r10, &addr);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        return (uint64_t)addr;
    }
    case SYS_MUNMAP: {
        process_t *proc = process_current();
        if (!proc || !proc->pml4 || proc->pml4 == paging_kernel_pml4())
            return (uint64_t)-EPERM;
        return (uint64_t)(int64_t)vmm_user_unmap(&proc->vm, proc->pml4,
                                                 (uintptr_t)r->rdi,
                                                 (size_t)r->rsi);
    }
    case SYS_NET_INFO: {
        net_info_t *out = (net_info_t *)(uintptr_t)r->rdi;
        net_info_t info;
        if (!user_range_ok(out, sizeof(*out)))
            return (uint64_t)-EFAULT;
        int rc = net_get_info(&info);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        *out = info;
        return 0;
    }
    case SYS_NET_PING: {
        uint32_t dst = (uint32_t)r->rdi;
        int64_t timeout_ms = (int64_t)r->rsi;
        process_t *proc = process_current();
        uint64_t result = 0;

        if (timeout_ms <= 0 || timeout_ms > (int64_t)NET_PING_TIMEOUT_MAX_MS)
            return (uint64_t)-EINVAL;
        int ticket = net_ping_begin(dst, (uint32_t)timeout_ms);
        if (ticket < 0)
            return (uint64_t)(int64_t)ticket;
        if (net_ping_wait_check((uintptr_t)ticket, &result))
            return result;

        /* Block like poll_wait; with nothing else runnable wait in place. */
        if (process_has_runnable_other_than(proc) &&
            process_begin_kernel_wait(proc, net_ping_wait_check,
                                      (uintptr_t)ticket) == 0)
            return 0;
        while (!net_ping_wait_check((uintptr_t)ticket, &result))
            __asm__ volatile("sti\n\thlt\n\tcli");
        return result;
    }
    case SYS_SOCKET: {
        process_t *proc = process_current();
        net_socket_t *socket = 0;

        int rc = net_socket_create((int)r->rdi, (int)r->rsi, (int)r->rdx, &socket);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        int fd = process_handle_attach_socket(proc, socket);
        if (fd < 0) {
            net_socket_release(socket);
            return (uint64_t)-EMFILE;
        }
        return (uint64_t)fd;
    }
    case SYS_BIND: {
        net_socket_t *socket = 0;
        uint64_t port = r->rdx;

        int rc = socket_from_fd(process_current(), (int)r->rdi, &socket);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        if (port > NET_PORT_MAX)
            return (uint64_t)-EINVAL;
        return (uint64_t)(int64_t)net_socket_bind(socket, (uint32_t)r->rsi,
                                                  (uint16_t)port);
    }
    case SYS_SENDTO: {
        process_t *proc = process_current();
        net_socket_t *socket = 0;
        const uint8_t *buf = (const uint8_t *)(uintptr_t)r->rsi;
        size_t len = (size_t)r->rdx;
        uint64_t port = r->r8;

        int rc = socket_from_fd(proc, (int)r->rdi, &socket);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        if (len > UDP_PAYLOAD_MAX)
            return (uint64_t)-EMSGSIZE;
        if (len > 0 && !user_buffer_ok(proc, buf, len, 0))
            return (uint64_t)-EFAULT;
        if (port > NET_PORT_MAX)
            return (uint64_t)-EINVAL;
        rc = net_socket_sendto(socket, buf, len, (uint32_t)r->r10, (uint16_t)port);
        if (rc < 0)
            return (uint64_t)(int64_t)rc;
        if (rc == 0)
            return len;

        /* The datagram waits for neighbour resolution; the scheduler
         * completes the syscall with the datagram length or an error. */
        if (process_begin_kernel_wait(proc, udp_send_wait_check, (uintptr_t)rc) != 0) {
            udp_send_cancel(rc);
            return (uint64_t)-EINVAL;
        }
        return 0;
    }
    case SYS_RECVFROM: {
        process_t *proc = process_current();
        net_socket_t *socket = 0;
        uint8_t *buf = (uint8_t *)(uintptr_t)r->rsi;
        size_t len = (size_t)r->rdx;
        uint32_t flags = (uint32_t)r->r10;
        net_sockaddr_t *from = (net_sockaddr_t *)(uintptr_t)r->r8;
        uint32_t received = 0;

        int rc = socket_from_fd(proc, (int)r->rdi, &socket);
        if (rc != 0)
            return (uint64_t)(int64_t)rc;
        if ((r->r10 & ~(uint64_t)NET_MSG_DONTWAIT) != 0)
            return (uint64_t)-EINVAL;
        if (len > 0 && !user_buffer_ok(proc, buf, len, 1))
            return (uint64_t)-EFAULT;
        if (from && !user_buffer_ok(proc, from, sizeof(*from), 1))
            return (uint64_t)-EFAULT;
        rc = net_socket_recvfrom(socket, buf, len, from, &received);
        if (rc == 0)
            return received;
        if ((flags & NET_MSG_DONTWAIT) != 0)
            return (uint64_t)-EAGAIN;
        if (process_begin_socket_recv_wait(proc, (int)r->rdi) != 0)
            return (uint64_t)-EBADF;
        return 0;
    }
    default:
        return (uint64_t)-1;
    }
}
