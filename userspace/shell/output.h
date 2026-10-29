#pragma once

#include <stddef.h>

/* Buffered output for builtins. All fds share one buffer that is flushed
 * whenever the target fd changes, so interleaved stdout/stderr writes keep
 * their order even when both point at the same pipe or file. */
void out_write(int fd, const char *buf, size_t len);
void out_str(int fd, const char *s);
void out_char(int fd, char c);
void out_u64(int fd, unsigned long value);
void out_flush(void);

/* Writes "prefix: subject: message\n" to stderr; subject may be null. */
void out_error(const char *prefix, const char *subject, const char *message);

/* Same as out_error, but to fd; nothing is written when fd < 0. */
void out_error_to(int fd, const char *prefix, const char *subject,
                  const char *message);

/* Error messages are tinted on the console only in interactive mode. */
void out_set_color(int enabled);
