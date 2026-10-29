#pragma once

#include <stdint.h>

#include "block.h"

void block_cache_init(void);
int block_read_cached(uint32_t lba, uint8_t count, void *buffer);
int block_write_cached(uint32_t lba, uint8_t count, const void *buffer);
int block_cache_sync(void);
int block_cache_invalidate(void);
