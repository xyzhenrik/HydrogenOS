#pragma once

#include <stddef.h>
#include <stdint.h>

void kheap_init(uintptr_t start_phys, size_t size_bytes);
void *kalloc(size_t size);
void kfree(void *ptr);
