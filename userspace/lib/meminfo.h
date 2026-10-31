#pragma once

#include <stdint.h>

#include "syscall.h"

/* SYS_MEMINFO snapshot; mirrors kernel/include/meminfo_abi.h. */
#define HYDROGEN_MEMINFO_VERSION 1u

typedef struct hydrogen_meminfo {
    uint32_t version;
    uint32_t size;
    uint64_t page_size;         /* bytes */
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t kheap_total_bytes;
    uint64_t kheap_used_bytes;
    uint64_t oom_kills;         /* processes killed for memory since boot */
} hydrogen_meminfo_t;

/* Returns the number of bytes stored (at most size) or -errno. */
static inline long sys_meminfo(void *buffer, unsigned long size) {
    return syscall2(SYS_MEMINFO, (long)buffer, (long)size);
}

/* Reads a whole snapshot; 0 on success, -1 if the kernel speaks another
 * version or returns a short snapshot. */
static inline int meminfo_read(hydrogen_meminfo_t *out) {
    long n = sys_meminfo(out, sizeof(*out));
    if (n != (long)sizeof(*out) || out->version != HYDROGEN_MEMINFO_VERSION ||
        out->size != sizeof(*out))
        return -1;
    return 0;
}
