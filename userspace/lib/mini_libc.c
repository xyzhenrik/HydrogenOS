#include "mini_libc.h"
#include "syscall.h"

int stat(const char *path, stat_t *buf) {
    return (int)sys_stat(path, buf);
}

int fstat(int fd, stat_t *buf) {
    return (int)sys_fstat(fd, buf);
}

int readdir_pos(const char *path, int pos, dirent_t *out) {
    return (int)sys_readdir(path, pos, out);
}

long write(int fd, const void *buf, size_t len) {
    return sys_write(fd, buf, len);
}

long read(int fd, void *buf, size_t len) {
    return sys_read(fd, buf, len);
}

long open(const char *path, int flags) {
    return sys_open(path, flags);
}

long close(int fd) {
    return sys_close(fd);
}

long lseek(int fd, long off, int whence) {
    return sys_lseek(fd, off, whence);
}

long pipe(int fds[2]) {
    return sys_pipe(fds);
}

long poll_fd(int fd, unsigned int events) {
    return sys_poll(fd, events);
}

long poll_wait_fd(int fd, unsigned int events, long timeout_ms) {
    return sys_poll_wait(fd, events, timeout_ms);
}

long poll_many_wait(poll_fd_t *fds, unsigned int count, long timeout_ms) {
    return sys_poll_many((void *)fds, count, timeout_ms);
}

long dup(int fd) {
    return sys_dup(fd);
}

long dup2(int src_fd, int dst_fd) {
    return sys_dup2(src_fd, dst_fd);
}

long dup3(int src_fd, int dst_fd, unsigned int flags) {
    return sys_dup3(src_fd, dst_fd, flags);
}

long set_fd_inherit(int fd, int inherit) {
    return sys_handle_set_inherit(fd, inherit);
}

long set_handle_inherit(int fd, int inherit) {
    return set_fd_inherit(fd, inherit);
}

long spawn(const char *path) {
    return sys_spawn(path, 0);
}

long spawn_with_stdio(const char *path, int stdin_fd, int stdout_fd, int stderr_fd) {
    return sys_spawn_with_stdio(path, stdin_fd, stdout_fd, stderr_fd);
}

long wait_pid(int pid, int *status) {
    return sys_wait(pid, status);
}

long spawn_args(const char *path, const char *const *argv,
                const char *const *envp, int stdin_fd, int stdout_fd,
                int stderr_fd) {
    return sys_spawn_args(path, argv, envp, stdin_fd, stdout_fd, stderr_fd);
}

long try_wait_pid(int pid, int *status) {
    return sys_try_wait(pid, status);
}

void yield_cpu(void) {
    (void)sys_yield();
}

void tty_set_color(unsigned char fg, unsigned char bg) {
    (void)sys_tty_set_color(fg, bg);
}

void sleep_ms(unsigned long ms) {
    (void)sys_sleep(ms);
}

unsigned long time_ms(void) {
    return sys_time_ms();
}

char *getcwd(char *buf, size_t size) {
    long r = syscall2(SYS_GETCWD, (long)buf, (long)size);
    return (r == 0) ? buf : (char *)0;
}

int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, (long)path);
}
