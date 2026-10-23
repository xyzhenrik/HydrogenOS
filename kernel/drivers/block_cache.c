#include "block.h"

#define CACHE_ENTRIES 16

typedef struct cache_entry {
    uint32_t lba;
    uint32_t age;
    uint8_t valid;
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
    return (best >= 0) ? &cache[best] : &cache[0];
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
        }
        e->age = age_counter++;
        buf_copy(dst, e->data, 512);
        dst += 512;
    }
    return 0;
}
