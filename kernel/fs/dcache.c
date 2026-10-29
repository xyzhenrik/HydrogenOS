#include "dcache.h"

#define DCACHE_BUCKET_COUNT 128u
#define DCACHE_NIL 0xFFFFu

_Static_assert((DCACHE_BUCKET_COUNT & (DCACHE_BUCKET_COUNT - 1u)) == 0,
               "bucket count must be a power of two");
_Static_assert(DCACHE_CAPACITY < DCACHE_NIL, "entry indices must fit in 16 bits");

typedef struct dcache_entry {
    uint32_t fs;
    uint32_t parent;
    uint32_t child;
    uint16_t hash_next;  /* bucket chain, or free-list link while unused */
    uint16_t lru_prev;   /* towards the most recently used entry */
    uint16_t lru_next;   /* towards the least recently used entry */
    uint8_t name_len;
    uint8_t used;
    uint8_t negative;
    char name[DCACHE_NAME_MAX];
} dcache_entry_t;

static dcache_entry_t entries[DCACHE_CAPACITY];
static uint16_t buckets[DCACHE_BUCKET_COUNT];
static uint16_t free_head;
static uint16_t lru_head;
static uint16_t lru_tail;
static uint32_t live_count;
static dcache_stats_t counters;
static int initialised;

static uint32_t hash_key(uint32_t fs, uint32_t parent, const char *name,
                         uint32_t len) {
    uint32_t h = 2166136261u;
    h = (h ^ fs) * 16777619u;
    h = (h ^ parent) * 16777619u;
    for (uint32_t i = 0; i < len; ++i)
        h = (h ^ (uint8_t)name[i]) * 16777619u;
    return h & (DCACHE_BUCKET_COUNT - 1u);
}

static int entry_matches(const dcache_entry_t *e, uint32_t fs, uint32_t parent,
                         const char *name, uint32_t len) {
    if (e->fs != fs || e->parent != parent || e->name_len != len)
        return 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (e->name[i] != name[i])
            return 0;
    }
    return 1;
}

/* The bucket heads and the free list start out non-zero, so the pool cannot
   rely on zero-initialised statics. */
static void ensure_initialised(void) {
    if (initialised)
        return;
    for (uint32_t i = 0; i < DCACHE_BUCKET_COUNT; ++i)
        buckets[i] = DCACHE_NIL;
    for (uint32_t i = 0; i < DCACHE_CAPACITY; ++i)
        entries[i].hash_next = (uint16_t)(i + 1 < DCACHE_CAPACITY ? i + 1 : DCACHE_NIL);
    free_head = 0;
    lru_head = DCACHE_NIL;
    lru_tail = DCACHE_NIL;
    initialised = 1;
}

static void lru_unlink(uint16_t idx) {
    const dcache_entry_t *e = &entries[idx];
    if (e->lru_prev != DCACHE_NIL)
        entries[e->lru_prev].lru_next = e->lru_next;
    else
        lru_head = e->lru_next;
    if (e->lru_next != DCACHE_NIL)
        entries[e->lru_next].lru_prev = e->lru_prev;
    else
        lru_tail = e->lru_prev;
}

static void lru_push_front(uint16_t idx) {
    dcache_entry_t *e = &entries[idx];
    e->lru_prev = DCACHE_NIL;
    e->lru_next = lru_head;
    if (lru_head != DCACHE_NIL)
        entries[lru_head].lru_prev = idx;
    else
        lru_tail = idx;
    lru_head = idx;
}

static void bucket_unlink(uint16_t idx) {
    const dcache_entry_t *e = &entries[idx];
    uint16_t *link = &buckets[hash_key(e->fs, e->parent, e->name, e->name_len)];
    while (*link != idx)
        link = &entries[*link].hash_next;
    *link = e->hash_next;
}

/* Removes a live entry from every structure and returns it to the free list. */
static void entry_release(uint16_t idx) {
    bucket_unlink(idx);
    lru_unlink(idx);
    entries[idx].used = 0;
    entries[idx].hash_next = free_head;
    free_head = idx;
    live_count--;
}

static uint16_t find(uint32_t fs, uint32_t parent, const char *name,
                     uint32_t len) {
    uint16_t idx = buckets[hash_key(fs, parent, name, len)];
    while (idx != DCACHE_NIL) {
        if (entry_matches(&entries[idx], fs, parent, name, len))
            return idx;
        idx = entries[idx].hash_next;
    }
    return DCACHE_NIL;
}

static uint16_t entry_acquire(void) {
    if (free_head == DCACHE_NIL) {
        entry_release(lru_tail);
        counters.evictions++;
    }
    uint16_t idx = free_head;
    free_head = entries[idx].hash_next;
    return idx;
}

static void insert(uint32_t fs, uint32_t parent, const char *name, uint32_t len,
                   uint32_t child, int negative) {
    if (len == 0 || len > DCACHE_NAME_MAX)
        return;
    ensure_initialised();
    uint16_t idx = find(fs, parent, name, len);
    if (idx != DCACHE_NIL) {
        entries[idx].child = child;
        entries[idx].negative = (uint8_t)negative;
        lru_unlink(idx);
        lru_push_front(idx);
        return;
    }
    idx = entry_acquire();
    dcache_entry_t *e = &entries[idx];
    e->fs = fs;
    e->parent = parent;
    e->child = child;
    e->name_len = (uint8_t)len;
    e->negative = (uint8_t)negative;
    e->used = 1;
    for (uint32_t i = 0; i < len; ++i)
        e->name[i] = name[i];
    uint16_t *head = &buckets[hash_key(fs, parent, name, len)];
    e->hash_next = *head;
    *head = idx;
    lru_push_front(idx);
    live_count++;
    counters.insertions++;
}

dcache_result_t dcache_lookup(uint32_t fs, uint32_t parent, const char *name,
                              uint32_t len, uint32_t *child) {
    ensure_initialised();
    uint16_t idx = DCACHE_NIL;
    if (len != 0 && len <= DCACHE_NAME_MAX)
        idx = find(fs, parent, name, len);
    if (idx == DCACHE_NIL) {
        counters.misses++;
        return DCACHE_MISS;
    }
    lru_unlink(idx);
    lru_push_front(idx);
    counters.hits++;
    if (entries[idx].negative) {
        counters.negative_hits++;
        return DCACHE_HIT_NEGATIVE;
    }
    *child = entries[idx].child;
    return DCACHE_HIT_POSITIVE;
}

void dcache_insert(uint32_t fs, uint32_t parent, const char *name, uint32_t len,
                   uint32_t child) {
    insert(fs, parent, name, len, child, 0);
}

void dcache_insert_negative(uint32_t fs, uint32_t parent, const char *name,
                            uint32_t len) {
    insert(fs, parent, name, len, 0, 1);
}

void dcache_invalidate_entry(uint32_t fs, uint32_t parent, const char *name,
                             uint32_t len) {
    if (len == 0 || len > DCACHE_NAME_MAX)
        return;
    ensure_initialised();
    uint16_t idx = find(fs, parent, name, len);
    if (idx == DCACHE_NIL)
        return;
    entry_release(idx);
    counters.invalidations++;
}

void dcache_invalidate_inode(uint32_t fs, uint32_t ino) {
    ensure_initialised();
    for (uint32_t i = 0; i < DCACHE_CAPACITY; ++i) {
        const dcache_entry_t *e = &entries[i];
        if (!e->used || e->fs != fs)
            continue;
        if (e->parent == ino || (!e->negative && e->child == ino)) {
            entry_release((uint16_t)i);
            counters.invalidations++;
        }
    }
}

void dcache_invalidate_fs(uint32_t fs) {
    ensure_initialised();
    for (uint32_t i = 0; i < DCACHE_CAPACITY; ++i) {
        if (entries[i].used && entries[i].fs == fs) {
            entry_release((uint16_t)i);
            counters.invalidations++;
        }
    }
}

void dcache_get_stats(dcache_stats_t *out) {
    ensure_initialised();
    *out = counters;
    out->entries = live_count;
    out->capacity = DCACHE_CAPACITY;
}
