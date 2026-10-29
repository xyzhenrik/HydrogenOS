#pragma once

#include <stdint.h>

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_LSEEK 5
#define SYS_SPAWN 8
#define SYS_WAIT 9
#define SYS_GETPID 10
#define SYS_SLEEP 11
#define SYS_YIELD 12
#define SYS_TIME_MS 13
#define SYS_TTY_SET_COLOR 14
#define SYS_PIPE 15
#define SYS_HANDLE_SET_INHERIT 16
#define SYS_SPAWN_WITH_STDIO 17
#define SYS_DUP 18
#define SYS_DUP2 19
#define SYS_DUP3 20
#define SYS_POLL 21
#define SYS_POLL_WAIT 22
#define SYS_POLL_MANY 23
#define SYS_SET_PRIORITY 24
#define SYS_STAT    25
#define SYS_FSTAT   26
#define SYS_READDIR 27
#define SYS_GETCWD  28
#define SYS_CHDIR   29
#define SYS_MKDIR   30
#define SYS_UNLINK  31
#define SYS_RENAME  32
#define SYS_WRITEF  33
#define SYS_FB_INFO 36
#define SYS_FB_BLIT 37
#define SYS_MOUSE_STATE 38
#define SYS_KBD_READ 39
#define SYS_CONSOLE_CURSOR 40
#define SYS_FB_BLIT_STRIDED 41
#define SYS_FB_CLEAR 42
#define SYS_SYSINFO 43
#define SYS_SET_LAYOUT 44
#define SYS_MMAP 45
#define SYS_MUNMAP 46
#define SYS_RTC_TIME 54
#define SYS_RMDIR 47

#define PRIO_IDLE   0
#define PRIO_NORMAL 1
#define PRIO_HIGH   2
#define PRIO_RT     3

#define SPAWN_STDIO_CLEAR   (-1)
#define SPAWN_STDIO_INHERIT (-2)
#define SPAWN_STDIO_NULL    (-3)

#define DUP3_INHERIT        (1u << 0)
#define DUP3_CLOSE_ON_SPAWN (1u << 1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define POLL_READ  (1u << 0)
#define POLL_WRITE (1u << 1)
#define POLL_HUP   (1u << 2)
#define POLL_NVAL  (1u << 3)
#define POLL_WAIT_INFINITE (-1)

#define MMAP_PROT_READ  (1u << 0)
#define MMAP_PROT_WRITE (1u << 1)
#define MMAP_PROT_EXEC  (1u << 2)

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2) {
    return syscall3(n, a1, a2, 0);
}

static inline long syscall1(long n, long a1) {
    return syscall3(n, a1, 0, 0);
}

static inline long sys_write(int fd, const void *buf, unsigned long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

static inline long sys_read(int fd, void *buf, unsigned long len) {
    return syscall3(SYS_READ, fd, (long)buf, (long)len);
}

static inline long sys_open(const char *path, int flags) {
    return syscall3(SYS_OPEN, (long)path, (long)flags, 0);
}

static inline long sys_close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

static inline long sys_lseek(int fd, long off, int whence) {
    return syscall3(SYS_LSEEK, (long)fd, off, (long)whence);
}

static inline long sys_spawn(const char *path, const char *const *argv) {
    return syscall3(SYS_SPAWN, (long)path, (long)argv, 0);
}

static inline long sys_wait(int pid, int *status) {
    return syscall3(SYS_WAIT, (long)pid, (long)status, 0);
}

static inline long sys_getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline long sys_yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline void sys_exit(int status) {
    (void)syscall3(SYS_EXIT, status, 0, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static inline long sys_sleep(unsigned long ms) {
    return syscall3(SYS_SLEEP, (long)ms, 0, 0);
}

static inline unsigned long sys_time_ms(void) {
    return (unsigned long)syscall3(SYS_TIME_MS, 0, 0, 0);
}

static inline long sys_tty_set_color(unsigned char fg, unsigned char bg) {
    return syscall2(SYS_TTY_SET_COLOR, (long)fg, (long)bg);
}

static inline long sys_pipe(int *fds) {
    return syscall3(SYS_PIPE, (long)fds, 0, 0);
}

static inline long sys_poll(int fd, unsigned int events) {
    return syscall3(SYS_POLL, (long)fd, (long)events, 0);
}

static inline long sys_poll_wait(int fd, unsigned int events, long timeout_ms) {
    return syscall3(SYS_POLL_WAIT, (long)fd, (long)events, timeout_ms);
}

static inline long sys_poll_many(void *fds, unsigned int count, long timeout_ms) {
    return syscall3(SYS_POLL_MANY, (long)fds, (long)count, timeout_ms);
}

static inline long sys_handle_set_inherit(int fd, int inherit) {
    return syscall3(SYS_HANDLE_SET_INHERIT, (long)fd, (long)inherit, 0);
}

static inline long sys_dup(int fd) {
    return syscall3(SYS_DUP, (long)fd, 0, 0);
}

static inline long sys_dup2(int src_fd, int dst_fd) {
    return syscall3(SYS_DUP2, (long)src_fd, (long)dst_fd, 0);
}

static inline long sys_dup3(int src_fd, int dst_fd, unsigned int flags) {
    return syscall3(SYS_DUP3, (long)src_fd, (long)dst_fd, (long)flags);
}

static inline long sys_spawn_with_stdio(const char *path, int stdin_fd,
                                        int stdout_fd, int stderr_fd) {
    return syscall4(SYS_SPAWN_WITH_STDIO, (long)path, (long)stdin_fd,
                    (long)stdout_fd, (long)stderr_fd);
}

static inline long sys_set_priority(int prio) {
    return syscall3(SYS_SET_PRIORITY, (long)prio, 0, 0);
}

static inline long sys_stat(const char *path, void *buf) {
    return syscall3(SYS_STAT, (long)path, (long)buf, 0);
}

static inline long sys_fstat(int fd, void *buf) {
    return syscall3(SYS_FSTAT, (long)fd, (long)buf, 0);
}

static inline long sys_readdir(const char *path, int pos, void *buf) {
    return syscall3(SYS_READDIR, (long)path, (long)pos, (long)buf);
}

static inline long sys_mkdir(const char *path) {
    return syscall1(SYS_MKDIR, (long)path);
}

static inline long sys_unlink(const char *path) {
    return syscall1(SYS_UNLINK, (long)path);
}

static inline long sys_rmdir(const char *path) {
    return syscall1(SYS_RMDIR, (long)path);
}

static inline long sys_rename(const char *old_path, const char *new_path) {
    return syscall2(SYS_RENAME, (long)old_path, (long)new_path);
}

static inline long sys_writef(const char *path, const void *data, unsigned long size) {
    return syscall3(SYS_WRITEF, (long)path, (long)data, (long)size);
}

static inline long sys_mmap(void *hint, unsigned long len, unsigned int prot,
                            unsigned int flags) {
    return syscall4(SYS_MMAP, (long)hint, (long)len, (long)prot, (long)flags);
}

static inline long sys_munmap(void *addr, unsigned long len) {
    return syscall2(SYS_MUNMAP, (long)addr, (long)len);
}

/* sys_fb_info() result when the boot path provided no linear framebuffer. */
#define SYS_FB_ERR_NODEV (-19)

static inline long sys_fb_info(unsigned int *info) {
    return syscall1(SYS_FB_INFO, (long)info);
}

static inline long sys_fb_blit(long x, long y, long w, long h, const void *buf) {
    return syscall5(SYS_FB_BLIT, x, y, w, h, (long)buf);
}

static inline long sys_mouse_state(int *out) {
    return syscall1(SYS_MOUSE_STATE, (long)out);
}

static inline long sys_kbd_read(void) {
    return syscall1(SYS_KBD_READ, 0);
}

static inline long sys_console_cursor(int visible) {
    return syscall1(SYS_CONSOLE_CURSOR, visible);
}

static inline long sys_fb_blit_strided(long x, long y, long w, long h, const void *buf, long stride) {
    return syscall6(SYS_FB_BLIT_STRIDED, x, y, w, h, (long)buf, stride);
}

static inline long sys_fb_clear(void) {
    return syscall1(SYS_FB_CLEAR, 0);
}

typedef struct sysinfo_t {
    unsigned int fs_type;
    unsigned int block_size;
    unsigned int total_blocks;
    unsigned int free_blocks;
    unsigned int total_inodes;
    unsigned int free_inodes;
    unsigned int free_pages;
    unsigned int page_size;
    unsigned int total_pages;
} sysinfo_t;

static inline long sys_sysinfo(sysinfo_t *out) {
    return syscall1(SYS_SYSINFO, (long)out);
}

static inline long sys_set_layout(int layout) {
    return syscall1(SYS_SET_LAYOUT, layout);
}

struct hydrogen_rtc_time;

static inline long sys_rtc_time(struct hydrogen_rtc_time *out) {
    return syscall1(SYS_RTC_TIME, (long)out);
}
