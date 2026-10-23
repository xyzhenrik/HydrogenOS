#include "mini_libc.h"
#include "syscall.h"
#include <stdarg.h>

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

void yield_cpu(void) {
    (void)sys_yield();
}

void tty_set_color(unsigned char fg, unsigned char bg) {
    (void)sys_tty_set_color(fg, bg);
}

int puts(const char *s) {
    size_t len = strlen(s);
    return (int)write(1, s, len);
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) {
        d[i] = (unsigned char)c;
    }
    return dst;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

void sleep_ms(unsigned long ms) {
    (void)sys_sleep(ms);
}

unsigned long time_ms(void) {
    return sys_time_ms();
}

/* ------------------------------------------------------------------ */
/* Memory management (malloc/free) using sys_mmap                      */
/* ------------------------------------------------------------------ */

static void *heap_end = 0;

void *malloc(size_t size) {
    if (size == 0) return 0;
    /* Very simple: always allocate new pages via mmap */
    size_t aligned_size = (size + 4095) & ~4095ULL;
    void *ptr = (void *)sys_mmap(0, aligned_size, 0, 0, 0, 0);
    if ((long)ptr < 0) return 0;
    return ptr;
}

void free(void *ptr) {
    /* Simple malloc doesn't track size for munmap yet, so free is a stub
       or we would need a header to store the size. */
    (void)ptr;
}

/* ------------------------------------------------------------------ */
/* String utilities                                                    */
/* ------------------------------------------------------------------ */

char *strdup(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (!d) return 0;
    memcpy(d, s, len + 1);
    return d;
}

long strtol(const char *nptr, char **endptr, int base) {
    if (!nptr) return 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') nptr++;
    int neg = 0;
    if (*nptr == '-') { neg = 1; nptr++; }
    else if (*nptr == '+') { nptr++; }

    unsigned long res = 0;
    if (base == 0) {
        if (*nptr == '0') {
            if (nptr[1] == 'x' || nptr[1] == 'X') { base = 16; nptr += 2; }
            else { base = 8; nptr++; }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) nptr += 2;
    }

    while (*nptr) {
        int v = -1;
        if (*nptr >= '0' && *nptr <= '9') v = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'z') v = 10 + (*nptr - 'a');
        else if (*nptr >= 'A' && *nptr <= 'Z') v = 10 + (*nptr - 'A');
        if (v < 0 || v >= base) break;
        res = res * (unsigned long)base + (unsigned long)v;
        nptr++;
    }
    if (endptr) *endptr = (char *)nptr;
    return neg ? -(long)res : (long)res;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, (char **)0, 10);
}

long atol(const char *nptr) {
    return strtol(nptr, (char **)0, 10);
}

char *getcwd(char *buf, size_t size) {
    long r = syscall2(SYS_GETCWD, (long)buf, (long)size);
    return (r == 0) ? buf : (char *)0;
}

int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, (long)path);
}

/* ------------------------------------------------------------------ */
/* printf / sprintf / snprintf / vsnprintf / fprintf                   */
/* ------------------------------------------------------------------ */

/* Write a single character into the buffer, respecting the size cap.
   pos is the number of characters written so far (may exceed size-1
   if the buffer is full, in which case we still count but don't write). */
static void buf_putc(char *buf, size_t size, size_t *pos, char c) {
    if (buf && *pos + 1 < size) {
        buf[*pos] = c;
    }
    (*pos)++;
}

/* Write a decimal or hex representation of an unsigned 64-bit value. */
static void fmt_uint(char *buf, size_t size, size_t *pos,
                     unsigned long val, int base, int upper,
                     int width, int left_align, int zero_pad) {
    char tmp[24];
    int  len = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        tmp[len++] = '0';
    } else {
        unsigned long v = val;
        while (v > 0 && len < (int)sizeof(tmp)) {
            tmp[len++] = digits[v % (unsigned long)base];
            v /= (unsigned long)base;
        }
    }

    /* tmp holds digits in reverse order */
    int pad = width - len;
    char pad_char = (zero_pad && !left_align) ? '0' : ' ';

    if (!left_align) {
        for (int i = 0; i < pad; i++) {
            buf_putc(buf, size, pos, pad_char);
        }
    }
    for (int i = len - 1; i >= 0; i--) {
        buf_putc(buf, size, pos, tmp[i]);
    }
    if (left_align) {
        for (int i = 0; i < pad; i++) {
            buf_putc(buf, size, pos, ' ');
        }
    }
}

/* Write a signed decimal integer. */
static void fmt_int(char *buf, size_t size, size_t *pos,
                    long val, int width, int left_align, int zero_pad) {
    if (val < 0) {
        buf_putc(buf, size, pos, '-');
        if (width > 0) {
            width--;
        }
        /* Avoid undefined behaviour for LONG_MIN */
        unsigned long uval = (val == (-__LONG_MAX__ - 1L))
                             ? ((unsigned long)__LONG_MAX__ + 1UL)
                             : (unsigned long)(-val);
        fmt_uint(buf, size, pos, uval, 10, 0, width, left_align, zero_pad);
    } else {
        fmt_uint(buf, size, pos, (unsigned long)val, 10, 0,
                 width, left_align, zero_pad);
    }
}

/* Write a null-terminated string with optional width/alignment. */
static void fmt_str(char *buf, size_t size, size_t *pos,
                    const char *s, int width, int left_align) {
    if (!s) {
        s = "(null)";
    }
    int slen = 0;
    const char *p = s;
    while (*p++) {
        slen++;
    }
    int pad = width - slen;

    if (!left_align) {
        for (int i = 0; i < pad; i++) {
            buf_putc(buf, size, pos, ' ');
        }
    }
    for (int i = 0; i < slen; i++) {
        buf_putc(buf, size, pos, s[i]);
    }
    if (left_align) {
        for (int i = 0; i < pad; i++) {
            buf_putc(buf, size, pos, ' ');
        }
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;

    if (!fmt) {
        if (buf && size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    while (*fmt) {
        if (*fmt != '%') {
            buf_putc(buf, size, &pos, *fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Flags */
        int left_align = 0;
        int zero_pad   = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') {
                left_align = 1;
            } else {
                zero_pad = 1;
            }
            fmt++;
        }

        /* Width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Length modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        /* Conversion specifier */
        char spec = *fmt;
        if (!spec) {
            break;
        }
        fmt++;

        switch (spec) {
        case 'd':
        case 'i': {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            fmt_int(buf, size, &pos, val, width, left_align, zero_pad);
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(buf, size, &pos, val, 10, 0, width, left_align, zero_pad);
            break;
        }
        case 'x': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(buf, size, &pos, val, 16, 0, width, left_align, zero_pad);
            break;
        }
        case 'X': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(buf, size, &pos, val, 16, 1, width, left_align, zero_pad);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)va_arg(ap, void *);
            buf_putc(buf, size, &pos, '0');
            buf_putc(buf, size, &pos, 'x');
            fmt_uint(buf, size, &pos, val, 16, 0, width, left_align, zero_pad);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            fmt_str(buf, size, &pos, s, width, left_align);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            buf_putc(buf, size, &pos, c);
            break;
        }
        case '%': {
            buf_putc(buf, size, &pos, '%');
            break;
        }
        default:
            /* Unknown specifier: emit literally */
            buf_putc(buf, size, &pos, '%');
            buf_putc(buf, size, &pos, spec);
            break;
        }
    }

    /* Null-terminate */
    if (buf && size > 0) {
        buf[pos < size ? pos : size - 1] = '\0';
    }

    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    /* Pass a large size so we don't truncate; caller must ensure buf is big enough */
    ret = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(int fd, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (ret > 0) {
        size_t to_write = (size_t)ret < sizeof(tmp) ? (size_t)ret : sizeof(tmp) - 1;
        write(fd, tmp, to_write);
    }
    return ret;
}

int printf(const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (ret > 0) {
        size_t to_write = (size_t)ret < sizeof(tmp) ? (size_t)ret : sizeof(tmp) - 1;
        write(1, tmp, to_write);
    }
    return ret;
}
