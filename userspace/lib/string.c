#include "string.h"

#include "stdlib.h"

void *memcpy(void *dst, const void *src, size_t n) {
    void *result = dst;
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return result;
}

/* Walking backwards by hand keeps the direction flag clear for interrupts. */
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d <= s || d >= s + n)
        return memcpy(dst, src, n);
    while (n > 0) {
        --n;
        d[n] = s[n];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    void *result = dst;
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return result;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n])
        ++n;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *result = dst;
    while ((*dst++ = *src++) != '\0') {
    }
    return result;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strchr(const char *s, int c) {
    for (;; ++s) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    for (;; ++s) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0)
        return (char *)haystack;
    for (; *haystack; ++haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, needle_len) == 0)
            return (char *)haystack;
    }
    return 0;
}

char *strpbrk(const char *s, const char *accept) {
    s += strcspn(s, accept);
    return *s ? (char *)s : 0;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n] && strchr(accept, s[n]))
        ++n;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n]))
        ++n;
    return n;
}

char *strtok_r(char *s, const char *delimiters, char **save) {
    if (!s)
        s = *save;
    s += strspn(s, delimiters);
    if (!*s) {
        *save = s;
        return 0;
    }
    char *token = s;
    s += strcspn(s, delimiters);
    if (*s)
        *s++ = '\0';
    *save = s;
    return token;
}

char *strdup(const char *s) {
    if (!s)
        return 0;
    return strndup(s, strlen(s));
}

char *strndup(const char *s, size_t max) {
    if (!s)
        return 0;
    size_t len = strnlen(s, max);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return 0;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}
