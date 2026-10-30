#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Out-of-memory policy.
 *
 * Allocations made on behalf of a user process (user pages, page tables,
 * mapping descriptors, process objects) go through these wrappers. When the
 * page allocator or the kernel heap is exhausted they pick a victim process,
 * kill it with SIGKILL, release its memory on the spot and try the allocation
 * once more. See docs/CRASH_OOM.md for the scoring rules.
 */

/* Like pmm_alloc_page() / kalloc(), with one retry after an OOM kill.
 * Return 0 / NULL if memory is still exhausted: nothing was killable, or the
 * victim was the calling process itself, which is then already marked as
 * exited and must unwind and return. */
uintptr_t oom_alloc_page(void);
void *oom_kalloc(size_t size);

/* Processes killed for memory since boot. */
uint64_t oom_kill_count(void);
