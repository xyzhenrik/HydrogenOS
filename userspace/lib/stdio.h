#pragma once

#include <stdarg.h>
#include <stddef.h>

/* Opaque; streams come from fopen/fdopen or are one of the three below. */
typedef struct File FILE;

#define EOF   (-1)
#define BUFSIZ 4096

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/*
 * stdout and stderr are unbuffered so output keeps its order relative to raw
 * write(2) calls and survives a process that ends without exit(); a program
 * may opt in to buffering with setvbuf before its first output. stdin is
 * fully buffered. Streams opened with fopen/fdopen are fully buffered and are
 * flushed by fflush, fclose and exit (returning from main counts).
 */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Modes: r, w, a, each optionally with + (read and write) and b (ignored). */
FILE *fopen(const char *path, const char *mode);
/* Takes ownership of fd: fclose closes it. */
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);

size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
/* NULL flushes every open output stream. */
int fflush(FILE *stream);
/* Only before the first read or write; buf may be NULL to allocate. */
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);

int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
/* One character of pushback. */
int ungetc(int c, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);
/* Reads at most size - 1 characters up to and including a newline. */
char *fgets(char *buf, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
/* Unlike ISO puts this appends no newline. */
int puts(const char *s);

/*
 * Conversions: %d %i %u %o %x %X %p %c %s %%, flags - + space # 0, width and
 * precision (digits or *), length hh h l ll j z t. No floating point. The
 * return value is the length the output has (or would have had, for the
 * bounded forms), or -1 on a write error.
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int printf(const char *fmt, ...);
/* Formats straight to a file descriptor, bypassing stdio buffers. */
int dprintf(int fd, const char *fmt, ...);
