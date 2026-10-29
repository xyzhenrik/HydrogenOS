#include "block.h"
#include "block_cache.h"

#define CACHE_ENTRIES 16

typedef struct cache_entry {
    uint32_t lba;
    uint32_t age;
    uint8_t valid;
    uint8_t dirty;
    uint8_t data[512];
} cache_entry_t;

static cache_entry_t cache[CACHE_ENTRIES];
static uint32_t age_counter = 1;

static void buf_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
}

void block_cache_init(void) {
    for (int i = 0; i < CACHE_ENTRIES; ++i) {
        cache[i].valid = 0;
        cache[i].dirty = 0;
        cache[i].age = 0;
        cache[i].lba = 0;
    }
    age_counter = 1;
}

static cache_entry_t *cache_find(uint32_t lba) {
    for (int i = 0; i < CACHE_ENTRIES; ++i) {
        if (cache[i].valid && cache[i].lba == lba) return &cache[i];
    }
    return 0;
}

static int cache_flush_entry(cache_entry_t *e) {
    if (!e || !e->valid || !e->dirty) return 0;
    if (block_write(e->lba, 1, e->data) != 0) return -1;
    e->dirty = 0;
    return 0;
}

static cache_entry_t *cache_evict(void) {
    int best = -1;
    uint32_t best_age = 0xFFFFFFFF;
    for (int i = 0; i < CACHE_ENTRIES; ++i) {
        if (!cache[i].valid) return &cache[i];
        if (cache[i].age < best_age) {
            best_age = cache[i].age;
            best = i;
        }
    }
    if (best < 0) best = 0;
    (void)cache_flush_entry(&cache[best]);
    cache[best].valid = 0;
    cache[best].dirty = 0;
    return &cache[best];
}

int block_read_cached(uint32_t lba, uint8_t count, void *buffer) {
    uint8_t *dst = (uint8_t *)buffer;
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cur = lba + i;
        cache_entry_t *e = cache_find(cur);
        if (!e) {
            e = cache_evict();
            if (block_read(cur, 1, e->data) != 0) return -1;
            e->valid = 1;
            e->lba = cur;
            e->dirty = 0;
        }
        e->age = age_counter++;
        buf_copy(dst, e->data, 512);
        dst += 512;
    }
    return 0;
}

int block_write_cached(uint32_t lba, uint8_t count, const void *buffer) {
    const uint8_t *src = (const uint8_t *)buffer;
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cur = lba + i;
        cache_entry_t *e = cache_find(cur);
        if (e) {
            buf_copy(e->data, src, 512);
            e->dirty = 1;
            e->age = age_counter++;
        } else {
            e = cache_evict();
            if (e->valid && e->dirty) {
                if (cache_flush_entry(e) != 0) {
                    if (block_write(cur, 1, (void *)src) != 0) return -1;
                    src += 512;
                    continue;
                }
            }
            e->valid = 1;
            e->lba = cur;
            e->dirty = 1;
            e->age = age_counter++;
            buf_copy(e->data, src, 512);
        }
        src += 512;
    }
    return 0;
}

int block_cache_sync(void) {
    int result = 0;
    for (int i = 0; i < CACHE_ENTRIES; ++i) {
        if (cache[i].valid && cache[i].dirty) {
            if (cache_flush_entry(&cache[i]) != 0) result = -1;
        }
    }
    return result;
}

int block_cache_invalidate(void) {
    (void)block_cache_sync();
    for (int i = 0; i < CACHE_ENTRIES; ++i) {
        cache[i].valid = 0;
        cache[i].dirty = 0;
        cache[i].age = 0;
        cache[i].lba = 0;
    }
    return 0;
}
