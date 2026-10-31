#pragma once

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
/* Correct for overlapping ranges. */
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
/* Length of s, looking at no more than max bytes. */
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);

char *strcpy(char *dst, const char *src);
/* Pads dst with NULs up to n and does not terminate on truncation. */
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok_r(char *s, const char *delimiters, char **save);

/* Heap copies for free(); NULL when s is NULL or memory is exhausted. */
char *strdup(const char *s);
char *strndup(const char *s, size_t max);
