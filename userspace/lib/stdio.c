#include "stdio.h"

#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

#include <stdint.h>

/*
 * A stream owns one buffer that holds either read-ahead or pending output
 * (StreamState), never both. Switching direction drops the read-ahead by
 * seeking the descriptor back, or flushes the pending output, so the
 * descriptor offset always matches the stream position the program sees.
 */

enum {
    FLAG_READ    = 1u << 0,
    FLAG_WRITE   = 1u << 1,
    FLAG_EOF     = 1u << 2,
    FLAG_ERROR   = 1u << 3,
    FLAG_STATIC  = 1u << 4, /* the FILE itself is not heap memory */
    FLAG_OWN_BUF = 1u << 5, /* buf was malloc'ed by this module */
};

typedef enum StreamState {
    STATE_IDLE,
    STATE_READING, /* buf[pos..len) is unread input */
    STATE_WRITING, /* buf[0..len) is unwritten output */
} StreamState;

#define NO_PUSHBACK (-1)

struct File {
    int fd;
    unsigned flags;
    int buffering; /* _IOFBF, _IOLBF or _IONBF */
    StreamState state;
    unsigned char *buf;
    size_t buf_size;
    size_t pos;
    size_t len;
    int pushback;
    int used; /* any read or write happened; setvbuf is no longer allowed */
    unsigned char unit; /* the one-byte read buffer of unbuffered streams */
    FILE *next;
};

static unsigned char stdin_buffer[BUFSIZ];

static FILE stderr_stream = {
    .fd = 2, .flags = FLAG_WRITE | FLAG_STATIC, .buffering = _IONBF,
    .buf = &stderr_stream.unit, .buf_size = 1, .pushback = NO_PUSHBACK,
};
static FILE stdout_stream = {
    .fd = 1, .flags = FLAG_WRITE | FLAG_STATIC, .buffering = _IONBF,
    .buf = &stdout_stream.unit, .buf_size = 1, .pushback = NO_PUSHBACK,
    .next = &stderr_stream,
};
static FILE stdin_stream = {
    .fd = 0, .flags = FLAG_READ | FLAG_STATIC, .buffering = _IOFBF,
    .buf = stdin_buffer, .buf_size = sizeof(stdin_buffer), .pushback = NO_PUSHBACK,
    .next = &stdout_stream,
};

FILE *stdin = &stdin_stream;
FILE *stdout = &stdout_stream;
FILE *stderr = &stderr_stream;

/* Every open stream, for fflush(NULL) and exit. */
static FILE *open_streams = &stdin_stream;

static int stream_error(FILE *f, long rc) {
    f->flags |= FLAG_ERROR;
    errno = (int)-rc;
    return -1;
}

/* Bytes the descriptor offset is ahead of the stream position. */
static size_t unread_count(const FILE *f) {
    size_t n = f->pushback != NO_PUSHBACK ? 1 : 0;
    return f->state == STATE_READING ? n + (f->len - f->pos) : n;
}

/* Writes everything or sets the error flag; returns the bytes delivered. */
static size_t write_all(FILE *f, const unsigned char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        long rc = sys_write(f->fd, data + done, len - done);
        if (rc < 0) {
            stream_error(f, rc);
            break;
        }
        if (rc == 0) {
            stream_error(f, -EIO);
            break;
        }
        done += (size_t)rc;
    }
    return done;
}

/* Pending output is discarded when it cannot be written, so a broken
   descriptor cannot wedge fclose or exit. */
static int flush_output(FILE *f) {
    if (f->state != STATE_WRITING)
        return 0;
    size_t pending = f->len;
    f->len = 0;
    return write_all(f, f->buf, pending) == pending ? 0 : -1;
}

/* Gives unread input back to the descriptor. Streams that cannot seek keep
   their buffer, since there is nowhere to put the data back. */
static void drop_read_ahead(FILE *f) {
    size_t unread = unread_count(f);
    if (unread > 0 && sys_lseek(f->fd, -(long)unread, SEEK_CUR) < 0)
        return;
    f->pos = 0;
    f->len = 0;
    f->pushback = NO_PUSHBACK;
    if (f->state == STATE_READING)
        f->state = STATE_IDLE;
}

static void assign_buffer(FILE *f, unsigned char *buf, size_t size, int mode, int owned) {
    if (f->flags & FLAG_OWN_BUF)
        free(f->buf);
    f->flags = owned ? (f->flags | FLAG_OWN_BUF) : (f->flags & ~FLAG_OWN_BUF);
    f->buffering = mode;
    f->buf = buf;
    f->buf_size = size;
}

/* Falls back to unbuffered operation when memory is short. */
static void attach_default_buffer(FILE *f) {
    unsigned char *buf = (unsigned char *)malloc(BUFSIZ);
    if (buf)
        assign_buffer(f, buf, BUFSIZ, _IOFBF, 1);
    else
        assign_buffer(f, &f->unit, 1, _IONBF, 0);
}

static int parse_mode(const char *mode, unsigned *open_flags, unsigned *stream_flags) {
    int update = 0;
    if (!mode || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) {
        errno = EINVAL;
        return -1;
    }
    for (const char *p = mode + 1; *p; ++p) {
        if (*p == '+') {
            update = 1;
        } else if (*p != 'b') {
            errno = EINVAL;
            return -1;
        }
    }
    switch (mode[0]) {
    case 'r':
        *open_flags = OPEN_READ | (update ? OPEN_WRITE : 0);
        break;
    case 'w':
        *open_flags = OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE | (update ? OPEN_READ : 0);
        break;
    default:
        *open_flags = OPEN_WRITE | OPEN_CREATE | OPEN_APPEND | (update ? OPEN_READ : 0);
        break;
    }
    *stream_flags = ((*open_flags & OPEN_READ) ? FLAG_READ : 0) |
                    ((*open_flags & OPEN_WRITE) ? FLAG_WRITE : 0);
    return 0;
}

static FILE *create_stream(int fd, unsigned stream_flags) {
    FILE *f = (FILE *)calloc(1, sizeof(FILE));
    if (!f) {
        errno = ENOMEM;
        return 0;
    }
    f->fd = fd;
    f->flags = stream_flags;
    f->pushback = NO_PUSHBACK;
    attach_default_buffer(f);
    f->next = open_streams;
    open_streams = f;
    return f;
}

FILE *fopen(const char *path, const char *mode) {
    unsigned open_flags;
    unsigned stream_flags;
    if (!path) {
        errno = EINVAL;
        return 0;
    }
    if (parse_mode(mode, &open_flags, &stream_flags) != 0)
        return 0;
    long fd = sys_open(path, (int)open_flags);
    if (fd < 0) {
        errno = (int)-fd;
        return 0;
    }
    FILE *f = create_stream((int)fd, stream_flags);
    if (!f)
        sys_close((int)fd);
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    unsigned open_flags;
    unsigned stream_flags;
    if (fd < 0) {
        errno = EBADF;
        return 0;
    }
    if (parse_mode(mode, &open_flags, &stream_flags) != 0)
        return 0;
    return create_stream(fd, stream_flags);
}

int fclose(FILE *f) {
    int result = 0;
    if (flush_output(f) != 0)
        result = EOF;
    else
        drop_read_ahead(f);
    long rc = sys_close(f->fd);
    if (rc < 0) {
        errno = (int)-rc;
        result = EOF;
    }
    for (FILE **link = &open_streams; *link; link = &(*link)->next) {
        if (*link == f) {
            *link = f->next;
            break;
        }
    }
    if (f->flags & FLAG_OWN_BUF)
        free(f->buf);
    if (!(f->flags & FLAG_STATIC))
        free(f);
    return result;
}

/* Makes the stream ready to take output. */
static int begin_write(FILE *f) {
    if (!(f->flags & FLAG_WRITE)) {
        stream_error(f, -EBADF);
        return -1;
    }
    f->used = 1;
    if (f->state == STATE_READING)
        drop_read_ahead(f);
    f->pushback = NO_PUSHBACK;
    if (f->state != STATE_WRITING && f->buffering != _IONBF) {
        f->state = STATE_WRITING;
        f->len = 0;
    }
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f) {
    const unsigned char *data = (const unsigned char *)ptr;
    if (size == 0 || count == 0)
        return 0;
    if (count > SIZE_MAX / size) {
        errno = EINVAL;
        return 0;
    }
    size_t total = size * count;
    if (begin_write(f) != 0)
        return 0;
    if (f->buffering == _IONBF)
        return write_all(f, data, total) / size;

    size_t remaining = total;
    while (remaining > 0) {
        if (f->len == 0 && remaining >= f->buf_size) {
            remaining -= write_all(f, data + (total - remaining), remaining);
            break;
        }
        size_t room = f->buf_size - f->len;
        size_t chunk = remaining < room ? remaining : room;
        memcpy(f->buf + f->len, data + (total - remaining), chunk);
        f->len += chunk;
        remaining -= chunk;
        if (f->len == f->buf_size && flush_output(f) != 0) {
            remaining += chunk;
            break;
        }
    }
    if (remaining == 0 && f->buffering == _IOLBF && memchr(data, '\n', total) &&
        flush_output(f) != 0)
        return 0;
    return (total - remaining) / size;
}

/* Makes the stream ready to take input. Returns 0, or -1 at end of file or on
   error. */
static int begin_read(FILE *f) {
    if (!(f->flags & FLAG_READ)) {
        stream_error(f, -EBADF);
        return -1;
    }
    if (f->flags & FLAG_EOF)
        return -1;
    f->used = 1;
    if (f->state == STATE_WRITING && flush_output(f) != 0)
        return -1;
    f->state = STATE_READING;
    return 0;
}

/* Refills an empty read buffer. Returns 0 when data is available. */
static int fill_buffer(FILE *f) {
    if (f->pos < f->len)
        return 0;
    if (begin_read(f) != 0)
        return -1;
    long rc = sys_read(f->fd, f->buf, f->buf_size);
    f->pos = 0;
    f->len = 0;
    if (rc < 0)
        return stream_error(f, rc);
    if (rc == 0) {
        f->flags |= FLAG_EOF;
        return -1;
    }
    f->len = (size_t)rc;
    return 0;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *f) {
    unsigned char *out = (unsigned char *)ptr;
    if (size == 0 || count == 0)
        return 0;
    if (count > SIZE_MAX / size) {
        errno = EINVAL;
        return 0;
    }
    size_t total = size * count;
    size_t done = 0;
    if (begin_read(f) != 0)
        return 0;
    if (f->pushback != NO_PUSHBACK) {
        out[done++] = (unsigned char)f->pushback;
        f->pushback = NO_PUSHBACK;
    }
    while (done < total) {
        if (f->pos < f->len) {
            size_t avail = f->len - f->pos;
            size_t chunk = total - done < avail ? total - done : avail;
            memcpy(out + done, f->buf + f->pos, chunk);
            f->pos += chunk;
            done += chunk;
        } else if (total - done >= f->buf_size) {
            long rc = sys_read(f->fd, out + done, total - done);
            if (rc < 0) {
                stream_error(f, rc);
                break;
            }
            if (rc == 0) {
                f->flags |= FLAG_EOF;
                break;
            }
            done += (size_t)rc;
        } else if (fill_buffer(f) != 0) {
            break;
        }
    }
    return done / size;
}

int fgetc(FILE *f) {
    if (f->pos < f->len && f->state == STATE_READING && f->pushback == NO_PUSHBACK)
        return f->buf[f->pos++];
    if (begin_read(f) != 0)
        return EOF;
    if (f->pushback != NO_PUSHBACK) {
        int c = f->pushback;
        f->pushback = NO_PUSHBACK;
        return c;
    }
    if (fill_buffer(f) != 0)
        return EOF;
    return f->buf[f->pos++];
}

int getc(FILE *f) {
    return fgetc(f);
}

int getchar(void) {
    return fgetc(stdin);
}

int ungetc(int c, FILE *f) {
    if (c == EOF)
        return EOF;
    f->flags &= ~(unsigned)FLAG_EOF;
    if (begin_read(f) != 0 || f->pushback != NO_PUSHBACK)
        return EOF;
    f->pushback = (unsigned char)c;
    return (unsigned char)c;
}

char *fgets(char *s, int size, FILE *f) {
    if (size <= 0)
        return 0;
    size_t limit = (size_t)size - 1;
    size_t n = 0;
    int line_done = 0;
    if (limit > 0 && begin_read(f) != 0)
        return 0;
    if (limit > 0 && f->pushback != NO_PUSHBACK) {
        s[n++] = (char)f->pushback;
        line_done = f->pushback == '\n';
        f->pushback = NO_PUSHBACK;
    }
    while (n < limit && !line_done) {
        if (fill_buffer(f) != 0)
            break;
        size_t avail = f->len - f->pos;
        size_t chunk = limit - n < avail ? limit - n : avail;
        const unsigned char *newline = memchr(f->buf + f->pos, '\n', chunk);
        if (newline) {
            chunk = (size_t)(newline - (f->buf + f->pos)) + 1;
            line_done = 1;
        }
        memcpy(s + n, f->buf + f->pos, chunk);
        f->pos += chunk;
        n += chunk;
    }
    if (n == 0 && limit > 0)
        return 0;
    s[n] = '\0';
    return s;
}

int fputc(int c, FILE *f) {
    unsigned char byte = (unsigned char)c;
    return fwrite(&byte, 1, 1, f) == 1 ? byte : EOF;
}

int putc(int c, FILE *f) {
    return fputc(c, f);
}

int putchar(int c) {
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *f) {
    size_t len = strlen(s);
    return fwrite(s, 1, len, f) == len ? 0 : EOF;
}

int puts(const char *s) {
    return fputs(s, stdout);
}

int fflush(FILE *f) {
    int result = 0;
    if (!f) {
        for (FILE *stream = open_streams; stream; stream = stream->next) {
            if (flush_output(stream) != 0)
                result = EOF;
        }
        return result;
    }
    if (f->state == STATE_READING)
        drop_read_ahead(f);
    return flush_output(f) == 0 ? 0 : EOF;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    if ((mode != _IOFBF && mode != _IOLBF && mode != _IONBF) || f->used) {
        errno = EINVAL;
        return -1;
    }
    if (mode == _IONBF) {
        assign_buffer(f, &f->unit, 1, _IONBF, 0);
        return 0;
    }
    if (buf) {
        if (size == 0) {
            errno = EINVAL;
            return -1;
        }
        assign_buffer(f, (unsigned char *)buf, size, mode, 0);
        return 0;
    }
    size_t capacity = size ? size : BUFSIZ;
    unsigned char *owned = (unsigned char *)malloc(capacity);
    if (!owned) {
        errno = ENOMEM;
        return -1;
    }
    assign_buffer(f, owned, capacity, mode, 1);
    return 0;
}

int fseek(FILE *f, long offset, int whence) {
    if (flush_output(f) != 0)
        return -1;
    if (whence == SEEK_CUR)
        offset -= (long)unread_count(f);
    long rc = sys_lseek(f->fd, offset, whence);
    if (rc < 0) {
        errno = (int)-rc;
        return -1;
    }
    f->pos = 0;
    f->len = 0;
    f->pushback = NO_PUSHBACK;
    f->state = STATE_IDLE;
    f->flags &= ~FLAG_EOF;
    return 0;
}

long ftell(FILE *f) {
    if (flush_output(f) != 0)
        return -1;
    long rc = sys_lseek(f->fd, 0, SEEK_CUR);
    if (rc < 0) {
        errno = (int)-rc;
        return -1;
    }
    return rc - (long)unread_count(f);
}

void rewind(FILE *f) {
    (void)fseek(f, 0, SEEK_SET);
    clearerr(f);
}

int feof(FILE *f) {
    return (f->flags & FLAG_EOF) != 0;
}

int ferror(FILE *f) {
    return (f->flags & FLAG_ERROR) != 0;
}

void clearerr(FILE *f) {
    f->flags &= ~(unsigned)(FLAG_EOF | FLAG_ERROR);
}

int fileno(FILE *f) {
    return f->fd;
}
