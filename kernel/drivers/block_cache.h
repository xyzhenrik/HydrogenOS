#pragma once

#include <stdint.h>

#include "block.h"

/*
 * Sector cache with a write-back policy.
 *
 * Writes only dirty the cached copy. Dirty sectors reach the device
 *   - on block_cache_sync(), which also flushes the device's own cache,
 *   - when block_cache_tick() finds the oldest dirty sector older than
 *     one second,
 *   - in bulk when the number of dirty sectors hits a fixed share of the
 *     cache, which bounds the data a crash can lose and guarantees that
 *     eviction always finds a clean sector.
 * Callers that need ordering or durability (unmount, power-off, metadata
 * operations) call block_cache_sync().
 */
void block_cache_init(void);
int block_read_cached(uint32_t lba, uint8_t count, void *buffer);
int block_write_cached(uint32_t lba, uint8_t count, const void *buffer);
/* Writes back every dirty sector and flushes the device. On failure the
   sectors that were not written stay dirty. Returns 0 or -1. */
int block_cache_sync(void);
/* Syncs, then drops every cached sector. Keeps the cache intact and returns
   -1 if the sync fails. */
int block_cache_invalidate(void);
/* Periodic write-back. Callers must be at a point where no cache or
   filesystem operation can be half done: the scheduler entry after a syscall
   or user-mode timer tick, or the idle loop. */
void block_cache_tick(void);
