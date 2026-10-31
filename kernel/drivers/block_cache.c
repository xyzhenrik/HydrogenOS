#include "block.h"
#include "block_cache.h"
#include "core/time.h"

#define CACHE_ENTRIES 256
#define CACHE_SECTOR_SIZE 512
#define CACHE_HASH_BUCKETS 256
#define CACHE_NO_ENTRY 0xFFFFu
/* Leaves a quarter of the cache clean at all times, so a victim for
   eviction never has to be written out first. */
#define CACHE_DIRTY_LIMIT (CACHE_ENTRIES * 3 / 4)
#define CACHE_FLUSH_INTERVAL_MS 1000
#define CACHE_WRITE_RUN_SECTORS 64

typedef struct cache_entry {
    uint32_t lba;
    uint32_t age;
    uint16_t hash_next;
    uint8_t valid;
    uint8_t dirty;
    uint8_t data[CACHE_SECTOR_SIZE];
} cache_entry_t;

static cache_entry_t cache[CACHE_ENTRIES];
static uint16_t buckets[CACHE_HASH_BUCKETS];
/* Write-back scratch: dirty entries in sector order and the staging buffer
   that makes a run of them contiguous for the device. */
static uint16_t write_order[CACHE_ENTRIES];
static uint8_t write_run[CACHE_WRITE_RUN_SECTORS * CACHE_SECTOR_SIZE];
static uint32_t age_counter = 1;
static uint32_t dirty_count = 0;
/* Set when sectors were written to the device without flushing it afterwards,
   so the periodic tick still makes them durable. */
static uint8_t device_unflushed = 0;
/* Valid while dirty_count > 0 or device_unflushed: when the oldest data not
   yet durable was written into the cache. */
static uint64_t dirty_since_ms = 0;

static void buf_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
}

static int buf_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint32_t bucket_of(uint32_t lba) { return lba % CACHE_HASH_BUCKETS; }

static uint32_t next_age(void) { return age_counter++; }

void block_cache_init(void) {
    for (uint32_t i = 0; i < CACHE_ENTRIES; ++i) {
        cache[i].valid = 0;
        cache[i].dirty = 0;
        cache[i].age = 0;
        cache[i].lba = 0;
        cache[i].hash_next = CACHE_NO_ENTRY;
    }
    for (uint32_t i = 0; i < CACHE_HASH_BUCKETS; ++i) buckets[i] = CACHE_NO_ENTRY;
    age_counter = 1;
    dirty_count = 0;
    device_unflushed = 0;
    dirty_since_ms = 0;
}

static cache_entry_t *cache_find(uint32_t lba) {
    uint16_t i = buckets[bucket_of(lba)];
    while (i != CACHE_NO_ENTRY) {
        if (cache[i].lba == lba) return &cache[i];
        i = cache[i].hash_next;
    }
    return 0;
}

static void cache_link(cache_entry_t *e) {
    uint32_t b = bucket_of(e->lba);
    e->hash_next = buckets[b];
    buckets[b] = (uint16_t)(e - cache);
}

static void cache_unlink(cache_entry_t *e) {
    uint16_t *slot = &buckets[bucket_of(e->lba)];
    uint16_t self = (uint16_t)(e - cache);
    while (*slot != CACHE_NO_ENTRY) {
        if (*slot == self) {
            *slot = e->hash_next;
            break;
        }
        slot = &cache[*slot].hash_next;
    }
    e->hash_next = CACHE_NO_ENTRY;
    e->valid = 0;
}

static void cache_mark_dirty(cache_entry_t *e) {
    if (e->dirty) return;
    if (dirty_count == 0 && !device_unflushed) dirty_since_ms = time_ms();
    e->dirty = 1;
    dirty_count++;
}

static void cache_mark_clean(cache_entry_t *e) {
    if (!e->dirty) return;
    e->dirty = 0;
    dirty_count--;
}

/* Returns an unlinked entry that may be overwritten: a free one, else the
   least recently used clean one. The dirty limit guarantees one exists. */
static cache_entry_t *cache_take_victim(void) {
    cache_entry_t *best = 0;
    for (uint32_t i = 0; i < CACHE_ENTRIES; ++i) {
        cache_entry_t *e = &cache[i];
        if (!e->valid) return e;
        if (e->dirty) continue;
        if (!best || (int32_t)(e->age - best->age) < 0) best = e;
    }
    if (best) cache_unlink(best);
    return best;
}

/* Collects the dirty entries into write_order, ascending by sector number, and
   returns how many there are. */
static uint32_t cache_collect_dirty(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < CACHE_ENTRIES; ++i) {
        if (!cache[i].valid || !cache[i].dirty) continue;
        uint32_t pos = n++;
        while (pos > 0 && cache[write_order[pos - 1]].lba > cache[i].lba) {
            write_order[pos] = write_order[pos - 1];
            --pos;
        }
        write_order[pos] = (uint16_t)i;
    }
    return n;
}

/* Hands every dirty sector to the device without flushing the device's own
   cache: a device flush costs a host-side fsync, far too slow to pay for every
   bulk write-back during a large sequential write. Neighbouring sectors travel
   in one request because the per-request cost of the device dwarfs the
   transfer itself. */
static int cache_write_back(void) {
    uint32_t n = cache_collect_dirty();
    uint32_t first = 0;
    while (first < n) {
        uint32_t lba = cache[write_order[first]].lba;
        uint32_t run = 0;
        while (first + run < n && run < CACHE_WRITE_RUN_SECTORS &&
               cache[write_order[first + run]].lba == lba + run) {
            buf_copy(write_run + run * CACHE_SECTOR_SIZE,
                     cache[write_order[first + run]].data, CACHE_SECTOR_SIZE);
            ++run;
        }
        if (block_write(lba, (uint8_t)run, write_run) != 0) return -1;
        for (uint32_t i = 0; i < run; ++i) cache_mark_clean(&cache[write_order[first + i]]);
        device_unflushed = 1;
        first += run;
    }
    return 0;
}

int block_read_cached(uint32_t lba, uint8_t count, void *buffer) {
    uint8_t *dst = (uint8_t *)buffer;
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cur = lba + i;
        cache_entry_t *e = cache_find(cur);
        if (!e) {
            e = cache_take_victim();
            if (!e) return -1;
            if (block_read(cur, 1, e->data) != 0) return -1;
            e->valid = 1;
            e->lba = cur;
            cache_link(e);
        }
        e->age = next_age();
        buf_copy(dst, e->data, CACHE_SECTOR_SIZE);
        dst += CACHE_SECTOR_SIZE;
    }
    return 0;
}

int block_write_cached(uint32_t lba, uint8_t count, const void *buffer) {
    const uint8_t *src = (const uint8_t *)buffer;
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cur = lba + i;
        if (dirty_count >= CACHE_DIRTY_LIMIT && cache_write_back() != 0) return -1;
        cache_entry_t *e = cache_find(cur);
        if (!e) {
            /* The whole sector is replaced, so the old contents are not read. */
            e = cache_take_victim();
            if (!e) return -1;
            e->valid = 1;
            e->lba = cur;
            cache_link(e);
            buf_copy(e->data, src, CACHE_SECTOR_SIZE);
            cache_mark_dirty(e);
        } else if (!buf_equal(e->data, src, CACHE_SECTOR_SIZE)) {
            buf_copy(e->data, src, CACHE_SECTOR_SIZE);
            cache_mark_dirty(e);
        }
        e->age = next_age();
        src += CACHE_SECTOR_SIZE;
    }
    return 0;
}

int block_cache_sync(void) {
    if (cache_write_back() != 0) return -1;
    if (!device_unflushed) return 0;
    if (block_flush() != 0) return -1;
    device_unflushed = 0;
    return 0;
}

int block_cache_invalidate(void) {
    if (block_cache_sync() != 0) return -1;
    block_cache_init();
    return 0;
}

void block_cache_tick(void) {
    if (dirty_count == 0 && !device_unflushed) return;
    uint64_t now = time_ms();
    if (now - dirty_since_ms < CACHE_FLUSH_INTERVAL_MS) return;
    /* After a failure, wait a full interval before hitting the device again. */
    if (block_cache_sync() != 0) dirty_since_ms = now;
}
