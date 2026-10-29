#pragma once

#include <stddef.h>
#include <stdint.h>

void kheap_init(uintptr_t start_phys, size_t size_bytes);
void *kalloc(size_t size);
void kfree(void *ptr);

typedef struct kheap_stats {
  size_t total_bytes; /* sum of block payloads; block headers are not counted */
  size_t used_bytes;
  size_t free_bytes;
  size_t largest_free_bytes;
} kheap_stats_t;

void kheap_get_stats(kheap_stats_t *out);
