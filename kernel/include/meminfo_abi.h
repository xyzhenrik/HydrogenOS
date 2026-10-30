#pragma once

#include <stdint.h>

/*
 * SYS_MEMINFO result. The layout is part of the syscall ABI: fields are only
 * ever appended and every change bumps HYDROGEN_MEMINFO_VERSION.
 * userspace/lib/meminfo.h mirrors this file.
 */
#define HYDROGEN_MEMINFO_VERSION 1u

typedef struct hydrogen_meminfo {
    uint32_t version;           /* HYDROGEN_MEMINFO_VERSION */
    uint32_t size;              /* sizeof(hydrogen_meminfo_t) of the kernel */
    uint64_t page_size;         /* bytes */
    uint64_t total_pages;       /* page frames the allocator manages */
    uint64_t free_pages;
    uint64_t kheap_total_bytes; /* block payloads; headers are not counted */
    uint64_t kheap_used_bytes;
    uint64_t oom_kills;         /* processes killed for memory since boot */
} hydrogen_meminfo_t;
