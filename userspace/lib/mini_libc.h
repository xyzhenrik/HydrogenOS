#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#define SPAWN_STDIO_CLEAR   (-1)
#define SPAWN_STDIO_INHERIT (-2)
#define SPAWN_STDIO_NULL    (-3)

#define DUP3_INHERIT        (1u << 0)
#define DUP3_CLOSE_ON_SPAWN (1u << 1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define OPEN_READ     (1u << 0)
#define OPEN_WRITE    (1u << 1)
#define OPEN_CREATE   (1u << 2)
#define OPEN_TRUNCATE (1u << 3)
#define OPEN_APPEND   (1u << 4)

#define POLL_READ  (1u << 0)
#define POLL_WRITE (1u << 1)
#define POLL_HUP   (1u << 2)
#define POLL_NVAL  (1u << 3)
#define POLL_WAIT_INFINITE (-1)

typedef struct poll_fd {
    int fd;
    unsigned int events;
    unsigned int revents;
} poll_fd_t;

typedef struct stat {
    uint32_t st_mode;
    uint32_t st_size;
    uint32_t st_nlink;
    uint32_t st_ino;
    uint64_t st_mtime;
} stat_t;

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

#define DIRENT_NAME_MAX 255

typedef struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[DIRENT_NAME_MAX + 1];
} dirent_t;

long write(int fd, const void *buf, size_t len);
long read(int fd, void *buf, size_t len);
long open(const char *path, int flags);
long close(int fd);
long lseek(int fd, long off, int whence);
int stat(const char *path, stat_t *buf);
int lstat(const char *path, stat_t *buf);
int fstat(int fd, stat_t *buf);
int readdir_pos(const char *path, int pos, dirent_t *out);
long pipe(int fds[2]);
long poll_fd(int fd, unsigned int events);
long poll_wait_fd(int fd, unsigned int events, long timeout_ms);
long poll_many_wait(poll_fd_t *fds, unsigned int count, long timeout_ms);
long dup(int fd);
long dup2(int src_fd, int dst_fd);
long dup3(int src_fd, int dst_fd, unsigned int flags);
long set_fd_inherit(int fd, int inherit);
long set_handle_inherit(int fd, int inherit);
long spawn(const char *path);
long spawn_with_stdio(const char *path, int stdin_fd, int stdout_fd, int stderr_fd);
long spawn_args(const char *path, const char *const *argv,
                const char *const *envp, int stdin_fd, int stdout_fd,
                int stderr_fd);
long wait_pid(int pid, int *status);
long try_wait_pid(int pid, int *status);
void yield_cpu(void);
void tty_set_color(unsigned char fg, unsigned char bg);
int puts(const char *s);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void sleep_ms(unsigned long ms);
unsigned long time_ms(void);
char *getcwd(char *buf, size_t size);
int   chdir(const char *path);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
