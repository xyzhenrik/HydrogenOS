#include "stdio.h"

#include "errno.h"
#include "string.h"
#include "syscall.h"

#include <limits.h>
#include <stdint.h>

#define FORMAT_CHUNK 1024
#define PAD_CHUNK    16
/* Enough for a 64-bit value in base 8. */
#define DIGITS_MAX   24

/*
 * All printf variants render through a Sink: a window of characters that is
 * either the caller's bounded buffer (excess output is counted but dropped) or
 * a staging buffer that drain() empties into a stream or descriptor.
 */
typedef struct Sink {
    char *buf;
    size_t capacity;
    size_t used;
    size_t total; /* characters the format produced, delivered or not */
    int (*drain)(void *context, const char *data, size_t len);
    void *context;
    int failed;
} Sink;

static void sink_write(Sink *sink, const char *data, size_t len) {
    sink->total += len;
    while (len > 0) {
        size_t room = sink->capacity - sink->used;
        if (room == 0) {
            if (!sink->drain || sink->failed)
                return;
            if (sink->drain(sink->context, sink->buf, sink->used) != 0) {
                sink->failed = 1;
                return;
            }
            sink->used = 0;
            room = sink->capacity;
        }
        size_t n = len < room ? len : room;
        memcpy(sink->buf + sink->used, data, n);
        sink->used += n;
        data += n;
        len -= n;
    }
}

static void sink_repeat(Sink *sink, char c, size_t count) {
    char chunk[PAD_CHUNK];
    memset(chunk, c, sizeof(chunk));
    while (count > 0) {
        size_t n = count < sizeof(chunk) ? count : sizeof(chunk);
        sink_write(sink, chunk, n);
        count -= n;
    }
}

/* Hands the staged remainder to drain(). */
static void sink_finish(Sink *sink) {
    if (sink->drain && !sink->failed && sink->used > 0 &&
        sink->drain(sink->context, sink->buf, sink->used) != 0)
        sink->failed = 1;
    sink->used = 0;
}

typedef enum LengthModifier {
    LENGTH_INT,
    LENGTH_CHAR,
    LENGTH_SHORT,
    LENGTH_LONG, /* l, ll, j, z and t: every one is 64 bits wide here */
} LengthModifier;

typedef struct Spec {
    int left;
    int plus;
    int space;
    int alternate;
    int zero;
    int width;
    int precision; /* -1: not given */
    LengthModifier length;
} Spec;

static void pad_before(Sink *sink, const Spec *spec, size_t content_len) {
    if (!spec->left && (size_t)spec->width > content_len)
        sink_repeat(sink, ' ', (size_t)spec->width - content_len);
}

static void pad_after(Sink *sink, const Spec *spec, size_t content_len) {
    if (spec->left && (size_t)spec->width > content_len)
        sink_repeat(sink, ' ', (size_t)spec->width - content_len);
}

static void format_integer(Sink *sink, const Spec *spec, unsigned long long magnitude,
                           int negative, int is_signed, int base, int upper, int pointer) {
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digit_set = upper ? upper_digits : lower_digits;
    char digits[DIGITS_MAX];
    size_t digit_count = 0;
    char prefix[2] = {0, 0};
    size_t prefix_len = 0;
    size_t zeros = 0;

    for (unsigned long long v = magnitude; v > 0; v /= (unsigned)base)
        digits[digit_count++] = digit_set[v % (unsigned)base];
    if (magnitude == 0 && spec->precision != 0)
        digits[digit_count++] = '0';

    if (spec->precision >= 0 && (size_t)spec->precision > digit_count)
        zeros = (size_t)spec->precision - digit_count;
    if (spec->alternate && base == 8 && zeros == 0 &&
        (digit_count == 0 || digits[digit_count - 1] != '0'))
        zeros = 1;

    if (is_signed) {
        if (negative)
            prefix[prefix_len++] = '-';
        else if (spec->plus)
            prefix[prefix_len++] = '+';
        else if (spec->space)
            prefix[prefix_len++] = ' ';
    } else if (pointer || (spec->alternate && base == 16 && magnitude != 0)) {
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = upper ? 'X' : 'x';
    }

    /* A precision turns the 0 flag off, as does left alignment. */
    if (spec->zero && !spec->left && spec->precision < 0 &&
        (size_t)spec->width > prefix_len + zeros + digit_count)
        zeros = (size_t)spec->width - prefix_len - digit_count;

    size_t content = prefix_len + zeros + digit_count;
    pad_before(sink, spec, content);
    sink_write(sink, prefix, prefix_len);
    sink_repeat(sink, '0', zeros);
    while (digit_count > 0)
        sink_write(sink, &digits[--digit_count], 1);
    pad_after(sink, spec, content);
}

static void format_signed(Sink *sink, const Spec *spec, va_list *ap) {
    long long value;
    switch (spec->length) {
    case LENGTH_CHAR:  value = (signed char)va_arg(*ap, int); break;
    case LENGTH_SHORT: value = (short)va_arg(*ap, int); break;
    case LENGTH_LONG:  value = va_arg(*ap, long); break;
    default:           value = va_arg(*ap, int); break;
    }
    unsigned long long magnitude = value < 0 ? 0ULL - (unsigned long long)value
                                             : (unsigned long long)value;
    format_integer(sink, spec, magnitude, value < 0, 1, 10, 0, 0);
}

static unsigned long long read_unsigned(const Spec *spec, va_list *ap) {
    switch (spec->length) {
    case LENGTH_CHAR:  return (unsigned char)va_arg(*ap, unsigned int);
    case LENGTH_SHORT: return (unsigned short)va_arg(*ap, unsigned int);
    case LENGTH_LONG:  return va_arg(*ap, unsigned long);
    default:           return va_arg(*ap, unsigned int);
    }
}

static void format_string(Sink *sink, const Spec *spec, const char *s) {
    if (!s)
        s = "(null)";
    size_t len = spec->precision < 0 ? strlen(s) : strnlen(s, (size_t)spec->precision);
    pad_before(sink, spec, len);
    sink_write(sink, s, len);
    pad_after(sink, spec, len);
}

/* Reads a decimal number or '*' at *fmt; *given tells whether there was one. */
static int read_count(const char **fmt, va_list *ap, int *given) {
    *given = 1;
    if (**fmt == '*') {
        ++*fmt;
        return va_arg(*ap, int);
    }
    if (**fmt < '0' || **fmt > '9') {
        *given = 0;
        return 0;
    }
    int value = 0;
    while (**fmt >= '0' && **fmt <= '9') {
        int digit = **fmt - '0';
        value = value > (INT_MAX - digit) / 10 ? INT_MAX : value * 10 + digit;
        ++*fmt;
    }
    return value;
}

static void parse_flags(const char **fmt, Spec *spec) {
    for (;; ++*fmt) {
        switch (**fmt) {
        case '-': spec->left = 1; break;
        case '+': spec->plus = 1; break;
        case ' ': spec->space = 1; break;
        case '#': spec->alternate = 1; break;
        case '0': spec->zero = 1; break;
        default: return;
        }
    }
}

static void parse_length(const char **fmt, Spec *spec) {
    switch (**fmt) {
    case 'h':
        ++*fmt;
        if (**fmt == 'h') {
            ++*fmt;
            spec->length = LENGTH_CHAR;
        } else {
            spec->length = LENGTH_SHORT;
        }
        break;
    case 'l':
        ++*fmt;
        if (**fmt == 'l')
            ++*fmt;
        spec->length = LENGTH_LONG;
        break;
    case 'j':
    case 'z':
    case 't':
        ++*fmt;
        spec->length = LENGTH_LONG;
        break;
    default:
        break;
    }
}

/* One conversion; fmt points just after the '%'. Returns the new position. */
static const char *format_conversion(Sink *sink, const char *fmt, va_list *ap) {
    Spec spec = {0, 0, 0, 0, 0, 0, -1, LENGTH_INT};

    parse_flags(&fmt, &spec);
    int given;
    int width = read_count(&fmt, ap, &given);
    if (width < 0) {
        spec.left = 1;
        width = width == INT_MIN ? INT_MAX : -width;
    }
    spec.width = width;
    if (*fmt == '.') {
        ++fmt;
        int precision = read_count(&fmt, ap, &given);
        spec.precision = !given ? 0 : (precision < 0 ? -1 : precision);
    }
    parse_length(&fmt, &spec);

    char conversion = *fmt;
    if (!conversion)
        return fmt;
    ++fmt;

    switch (conversion) {
    case 'd':
    case 'i':
        format_signed(sink, &spec, ap);
        break;
    case 'u':
        format_integer(sink, &spec, read_unsigned(&spec, ap), 0, 0, 10, 0, 0);
        break;
    case 'o':
        format_integer(sink, &spec, read_unsigned(&spec, ap), 0, 0, 8, 0, 0);
        break;
    case 'x':
        format_integer(sink, &spec, read_unsigned(&spec, ap), 0, 0, 16, 0, 0);
        break;
    case 'X':
        format_integer(sink, &spec, read_unsigned(&spec, ap), 0, 0, 16, 1, 0);
        break;
    case 'p':
        spec.precision = -1;
        format_integer(sink, &spec, (uintptr_t)va_arg(*ap, void *), 0, 0, 16, 0, 1);
        break;
    case 's':
        format_string(sink, &spec, va_arg(*ap, const char *));
        break;
    case 'c': {
        char c = (char)va_arg(*ap, int);
        pad_before(sink, &spec, 1);
        sink_write(sink, &c, 1);
        pad_after(sink, &spec, 1);
        break;
    }
    case '%':
        sink_write(sink, "%", 1);
        break;
    default:
        sink_write(sink, "%", 1);
        sink_write(sink, &conversion, 1);
        break;
    }
    return fmt;
}

static void format_all(Sink *sink, const char *fmt, va_list ap) {
    va_list args;
    va_copy(args, ap);
    while (*fmt) {
        const char *literal = fmt;
        while (*fmt && *fmt != '%')
            ++fmt;
        sink_write(sink, literal, (size_t)(fmt - literal));
        if (*fmt == '%')
            fmt = format_conversion(sink, fmt + 1, &args);
    }
    va_end(args);
}

static int result_length(const Sink *sink) {
    if (sink->failed)
        return -1;
    if (sink->total > (size_t)INT_MAX) {
        errno = ERANGE;
        return -1;
    }
    return (int)sink->total;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    Sink sink = {buf, size > 0 ? size - 1 : 0, 0, 0, 0, 0, 0};
    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!buf)
        sink.capacity = 0;
    format_all(&sink, fmt, ap);
    if (buf && size > 0)
        buf[sink.used] = '\0';
    return result_length(&sink);
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, SIZE_MAX, fmt, ap);
}

static int drain_stream(void *context, const char *data, size_t len) {
    return fwrite(data, 1, len, (FILE *)context) == len ? 0 : -1;
}

static int drain_descriptor(void *context, const char *data, size_t len) {
    int fd = *(int *)context;
    while (len > 0) {
        long written = sys_write(fd, data, len);
        if (written <= 0) {
            errno = written < 0 ? (int)-written : EIO;
            return -1;
        }
        data += written;
        len -= (size_t)written;
    }
    return 0;
}

static int format_drained(int (*drain)(void *, const char *, size_t), void *context,
                          const char *fmt, va_list ap) {
    char chunk[FORMAT_CHUNK];
    Sink sink = {chunk, sizeof(chunk), 0, 0, drain, context, 0};
    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    format_all(&sink, fmt, ap);
    sink_finish(&sink);
    return result_length(&sink);
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    return format_drained(drain_stream, stream, fmt, ap);
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return result;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vsprintf(buf, fmt, ap);
    va_end(ap);
    return result;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vfprintf(stream, fmt, ap);
    va_end(ap);
    return result;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return result;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = format_drained(drain_descriptor, &fd, fmt, ap);
    va_end(ap);
    return result;
}
