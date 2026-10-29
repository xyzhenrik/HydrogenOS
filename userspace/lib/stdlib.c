#include "stdlib.h"

#include "errno.h"
#include "stdio.h"
#include "syscall.h"

#include <limits.h>

_Static_assert(sizeof(long) == sizeof(long long), "strtol shares the 64-bit parser");

static int is_space(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/* Value of c as a digit in bases up to 36, or 36 when it is not one. */
static int digit_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'Z')
        return 10 + (c - 'A');
    return 36;
}

typedef struct ParsedInteger {
    unsigned long long magnitude;
    int negative;
    int overflow;
    int valid;
} ParsedInteger;

/* The sign and magnitude shared by all strto* variants. */
static ParsedInteger parse_integer(const char *nptr, char **end, int base) {
    ParsedInteger parsed = {0, 0, 0, 0};
    const char *p = nptr;

    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (end)
            *end = (char *)nptr;
        return parsed;
    }
    while (is_space(*p))
        ++p;
    if (*p == '+' || *p == '-')
        parsed.negative = *p++ == '-';

    /* A "0x" prefix only counts when a hex digit follows, otherwise the "0"
       is the whole number. */
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_value(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }

    for (;; ++p) {
        int digit = digit_value(*p);
        if (digit >= base)
            break;
        parsed.valid = 1;
        if (parsed.magnitude > (ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base)
            parsed.overflow = 1;
        else
            parsed.magnitude = parsed.magnitude * (unsigned long long)base + (unsigned long long)digit;
    }
    if (end)
        *end = (char *)(parsed.valid ? p : nptr);
    return parsed;
}

unsigned long long strtoull(const char *nptr, char **end, int base) {
    ParsedInteger parsed = parse_integer(nptr, end, base);
    if (parsed.overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return parsed.negative ? 0ULL - parsed.magnitude : parsed.magnitude;
}

long long strtoll(const char *nptr, char **end, int base) {
    ParsedInteger parsed = parse_integer(nptr, end, base);
    unsigned long long limit = parsed.negative ? (unsigned long long)LLONG_MAX + 1ULL
                                               : (unsigned long long)LLONG_MAX;
    if (parsed.overflow || parsed.magnitude > limit) {
        errno = ERANGE;
        return parsed.negative ? LLONG_MIN : LLONG_MAX;
    }
    return parsed.negative ? (long long)(0ULL - parsed.magnitude) : (long long)parsed.magnitude;
}

unsigned long strtoul(const char *nptr, char **end, int base) {
    return (unsigned long)strtoull(nptr, end, base);
}

long strtol(const char *nptr, char **end, int base) {
    return (long)strtoll(nptr, end, base);
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, (char **)0, 10);
}

long atol(const char *nptr) {
    return strtol(nptr, (char **)0, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, (char **)0, 10);
}

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0 ? -value : value;
}

long long llabs(long long value) {
    return value < 0 ? -value : value;
}

void exit(int status) {
    (void)fflush(0);
    sys_exit(status);
    __builtin_unreachable();
}
