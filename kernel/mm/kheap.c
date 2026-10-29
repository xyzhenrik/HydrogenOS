#include "kheap.h"

#include <stddef.h>
#include <stdint.h>

typedef struct block {
  size_t size;
  uint8_t free;
  struct block *next;
} block_t;

#define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)
#define BLOCK_SIZE (sizeof(block_t))

static block_t *heap_head = NULL;
static uintptr_t heap_limit = 0;

void kheap_init(uintptr_t start_phys, size_t size_bytes) {
  start_phys = (uintptr_t)ALIGN8((size_t)start_phys);
  heap_limit = start_phys + (uintptr_t)size_bytes;
  heap_head = (block_t *)(uintptr_t)start_phys;
  heap_head->size = size_bytes - BLOCK_SIZE;
  heap_head->free = 1;
  heap_head->next = NULL;
}

static void split_block(block_t *blk, size_t size) {
  if (blk->size <= size + BLOCK_SIZE + 8)
    return;
  block_t *newblk = (block_t *)((uintptr_t)blk + BLOCK_SIZE + size);
  newblk->size = blk->size - size - BLOCK_SIZE;
  newblk->free = 1;
  newblk->next = blk->next;
  blk->size = size;
  blk->next = newblk;
}

void *kalloc(size_t size) {
  if (size == 0 || !heap_head)
    return NULL;
  size = ALIGN8(size);
  block_t *cur = heap_head;
  while (cur) {
    if (cur->free && cur->size >= size) {
      split_block(cur, size);
      cur->free = 0;
      return (void *)((uintptr_t)cur + BLOCK_SIZE);
    }
    cur = cur->next;
  }
  return NULL; // out of memory
}

static void coalesce(void) {
  block_t *cur = heap_head;
  while (cur && cur->next) {
    if (cur->free && cur->next->free) {
      cur->size += BLOCK_SIZE + cur->next->size;
      cur->next = cur->next->next;
    } else {
      cur = cur->next;
    }
  }
}

void kfree(void *ptr) {
  if (!ptr || ptr < (void *)heap_head ||
      (uintptr_t)ptr >= heap_limit)
    return;
  block_t *blk = (block_t *)((uintptr_t)ptr - BLOCK_SIZE);
  blk->free = 1;
  coalesce();
}

void kheap_get_stats(kheap_stats_t *out) {
  if (!out)
    return;
  out->total_bytes = 0;
  out->used_bytes = 0;
  out->free_bytes = 0;
  out->largest_free_bytes = 0;
  for (block_t *cur = heap_head; cur; cur = cur->next) {
    out->total_bytes += cur->size;
    if (cur->free) {
      out->free_bytes += cur->size;
      if (cur->size > out->largest_free_bytes)
        out->largest_free_bytes = cur->size;
    } else {
      out->used_bytes += cur->size;
    }
  }
}
