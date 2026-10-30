#pragma once

#include <stddef.h>

/* SYS_MEMINFO: copies the memory snapshot, truncated to size bytes, to user
 * space. Returns the byte count, -EINVAL if size cannot hold the version and
 * size fields, or -EFAULT for an invalid buffer. */
long meminfo_read_user(void *buffer, size_t size);
