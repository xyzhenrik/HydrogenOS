#pragma once

#include <stdint.h>

/*
 * Directory entry cache: remembers the result of looking up one name in one
 * directory, keyed by (filesystem, parent inode number, name). Both hits that
 * name an inode and hits that record "no such name" (negative entries) are
 * kept. The size is fixed; the least recently used entry is recycled when the
 * pool is full.
 *
 * The cache holds names, never file contents or the targets of symlinks: an
 * entry for a symlink maps the link's own name to the link's inode.
 *
 * The owning filesystem must drop an entry before it changes the directory
 * record it describes (dcache_invalidate_entry), drop everything about an
 * inode when that inode is freed (dcache_invalidate_inode) and drop the whole
 * filesystem when it is mounted again or unmounted (dcache_invalidate_fs).
 *
 * Callers run to completion without preemption, so there is no locking.
 */

/* Filesystem identifiers; inode numbers are only unique within one of them. */
#define DCACHE_FS_EXT2 1u

/* Longer names are never cached and always take the slow path. */
#define DCACHE_NAME_MAX 47u
#define DCACHE_CAPACITY 256u

typedef enum dcache_result {
    DCACHE_MISS = 0,
    DCACHE_HIT_POSITIVE,
    DCACHE_HIT_NEGATIVE,
} dcache_result_t;

typedef struct dcache_stats {
    uint32_t entries;
    uint32_t capacity;
    uint64_t hits;           /* positive and negative hits together */
    uint64_t negative_hits;  /* the subset of hits that answered "absent" */
    uint64_t misses;
    uint64_t insertions;
    uint64_t evictions;      /* live entries recycled to make room */
    uint64_t invalidations;  /* live entries dropped on request */
} dcache_stats_t;

/* On DCACHE_HIT_POSITIVE the child inode number is stored in *child. */
dcache_result_t dcache_lookup(uint32_t fs, uint32_t parent, const char *name,
                              uint32_t len, uint32_t *child);
void dcache_insert(uint32_t fs, uint32_t parent, const char *name, uint32_t len,
                   uint32_t child);
void dcache_insert_negative(uint32_t fs, uint32_t parent, const char *name,
                            uint32_t len);

void dcache_invalidate_entry(uint32_t fs, uint32_t parent, const char *name,
                             uint32_t len);
/* Drops every entry that has ino as its parent or as its child. */
void dcache_invalidate_inode(uint32_t fs, uint32_t ino);
void dcache_invalidate_fs(uint32_t fs);

void dcache_get_stats(dcache_stats_t *out);
