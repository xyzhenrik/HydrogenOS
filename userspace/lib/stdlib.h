#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

/*
 * Number parsing follows ISO C: leading white space, an optional sign, base 0
 * detects 0x and 0 prefixes. Without digits nothing is consumed (*end = nptr)
 * and 0 is returned; out-of-range values saturate and set errno to ERANGE; an
 * unsupported base sets EINVAL.
 */
long strtol(const char *nptr, char **end, int base);
unsigned long strtoul(const char *nptr, char **end, int base);
long long strtoll(const char *nptr, char **end, int base);
unsigned long long strtoull(const char *nptr, char **end, int base);
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

int abs(int value);
long labs(long value);
long long llabs(long long value);

typedef int (*CompareFunction)(const void *, const void *);

/* In-place, not stable. Introsort: O(n log n) worst case, O(log n) stack. */
void qsort(void *base, size_t count, size_t size, CompareFunction compare);
/* Needs base sorted by compare; any one of several equal elements may match. */
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              CompareFunction compare);

/*
 * The environment starts as the envp vector handed over by sys_spawn_args
 * (NULL when the parent passed none). Changes are private to this process; to
 * pass them on, give environ to spawn_args.
 */
extern char **environ;
char *getenv(const char *name);
/* Names must be non-empty and free of '='; fails with EINVAL or ENOMEM. */
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

/* Flushes all stdio streams, then ends the process. */
__attribute__((noreturn)) void exit(int status);
