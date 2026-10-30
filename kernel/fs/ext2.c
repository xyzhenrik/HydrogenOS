#include "ext2.h"
#include "drivers/block.h"
#include "drivers/block_cache.h"
#include "drivers/mbr.h"
#include "fs/dcache.h"
#include "mm/kheap.h"
#include "errno.h"

#include <stddef.h>

/*
 * ext2 driver (revision 0 and 1, all block groups, 1-4 KiB blocks).
 *
 * Every public entry point runs to completion without being preempted
 * (syscalls execute with IF cleared), so the module-wide scratch buffers
 * below need no locking. Metadata updates are ordered so that an I/O error
 * in the middle of an operation can leak blocks or inodes but never leaves
 * a directory entry pointing at an unallocated or uninitialised inode.
 *
 * Namespace operations and whole-file writes return only after the block
 * cache has been synced. Positioned writes and truncation are write-back:
 * they are durable once the cache's periodic flush, ext2_sync() or an
 * unmount/power-off has run.
 */

#define EXT2_SUPER_MAGIC          0xEF53
#define EXT2_SUPERBLOCK_OFFSET    1024
#define EXT2_ROOT_INO             2
#define EXT2_GOOD_OLD_REV         0
#define EXT2_GOOD_OLD_INODE_SIZE  128
#define EXT2_GOOD_OLD_FIRST_INO   11
#define EXT2_MAX_BLOCK_SIZE       4096
#define EXT2_NDIR_BLOCKS          12
#define EXT2_NAME_LEN             255
#define EXT2_LINK_MAX             32000
/* i_size is 32 bit; the large-file high word in i_dir_acl is not used. */
#define EXT2_FILE_SIZE_MAX        0x7FFFFFFFu
/* Targets up to the size of i_block[] live in the inode itself. */
#define EXT2_FAST_SYMLINK_MAX     60

#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

#define EXT2_PERMISSION_MASK 0777

/* Where Linux keeps the upper halves of the 32-bit owner ids. */
#define EXT2_OSD2_UID_HIGH 4
#define EXT2_OSD2_GID_HIGH 6

#define EXT2_INDEX_FL 0x00001000

#define EXT2_FEATURE_INCOMPAT_FILETYPE      0x0002
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002
#define EXT2_SUPPORTED_INCOMPAT  EXT2_FEATURE_INCOMPAT_FILETYPE
#define EXT2_SUPPORTED_RO_COMPAT (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER | \
                                  EXT2_FEATURE_RO_COMPAT_LARGE_FILE)

#define EXT2_XATTR_MAGIC 0xEA020000u

#define DIR_ENTRY_HEADER_LEN 8
#define NO_PREV_ENTRY        0xFFFFFFFFu

typedef struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
} __attribute__((packed)) ext2_superblock_t;

typedef struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

typedef struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} __attribute__((packed)) ext2_dir_entry_t;

/* Logical block -> slot in i_block[] plus indices into each indirect level. */
typedef struct ext2_block_path {
    uint32_t depth;
    uint32_t slot;
    uint32_t index[3];
} ext2_block_path_t;

typedef struct ext2_dir_pos {
    uint32_t block;
    uint32_t offset;
    uint32_t prev_offset;
} ext2_dir_pos_t;

/* Returns 0 to continue the walk, >0 to stop it, <0 on error. */
typedef int (*ext2_dir_visit_fn)(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx);

static int mounted = 0;
static int writable = 0;
static uint32_t base_lba = 0;
static uint32_t block_size = 0;
static uint32_t sectors_per_block = 0;
static uint32_t inode_size = 0;
static uint32_t first_ino = 0;
static uint32_t group_count = 0;
static uint64_t gd_table_offset = 0;
static ext2_superblock_t sb;
static ext2_group_desc_t *groups = 0;

static uint8_t sector_buf[512];
/* File data and xattr blocks. */
static uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
/* Directory block currently being walked; owned by dir_walk and its visitors. */
static uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
static uint8_t bitmap_buf[EXT2_MAX_BLOCK_SIZE];
/* One buffer per indirection level so the block tree can be freed recursively. */
static uint32_t indirect_buf[3][EXT2_MAX_BLOCK_SIZE / 4];
static const uint8_t zero_block[EXT2_MAX_BLOCK_SIZE];

/* ------------------------------------------------------------------ */
/* Raw I/O                                                             */
/* ------------------------------------------------------------------ */

static void buf_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
}

static void buf_zero(void *dst, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; ++i) p[i] = 0;
}

static int sector_lba(uint64_t byte_off, uint32_t *out_lba) {
    uint64_t lba = (uint64_t)base_lba + byte_off / 512;
    if (lba > 0xFFFFFFFFu) return -EIO;
    *out_lba = (uint32_t)lba;
    return 0;
}

static int read_bytes(uint64_t byte_off, void *out, uint32_t len) {
    uint8_t *dst = (uint8_t *)out;
    uint32_t lba = 0;
    if (sector_lba(byte_off, &lba) != 0) return -EIO;
    uint32_t off = (uint32_t)(byte_off % 512);
    while (len > 0) {
        if (block_read_cached(lba, 1, sector_buf) != 0) return -EIO;
        uint32_t take = 512 - off;
        if (take > len) take = len;
        buf_copy(dst, sector_buf + off, take);
        dst += take;
        len -= take;
        lba++;
        off = 0;
    }
    return 0;
}

static int write_bytes(uint64_t byte_off, const void *src, uint32_t len) {
    const uint8_t *sp = (const uint8_t *)src;
    uint32_t lba = 0;
    if (sector_lba(byte_off, &lba) != 0) return -EIO;
    uint32_t off = (uint32_t)(byte_off % 512);
    while (len > 0) {
        uint32_t take = 512 - off;
        if (take > len) take = len;
        if (take < 512 && block_read_cached(lba, 1, sector_buf) != 0) return -EIO;
        buf_copy(sector_buf + off, sp, take);
        if (block_write_cached(lba, 1, sector_buf) != 0) return -EIO;
        sp += take;
        len -= take;
        lba++;
        off = 0;
    }
    return 0;
}

static int read_block(uint32_t blk, void *out) {
    if (blk == 0 || blk >= sb.s_blocks_count) return -EIO;
    return read_bytes((uint64_t)blk * block_size, out, block_size);
}

static int write_block(uint32_t blk, const void *buf) {
    if (blk == 0 || blk >= sb.s_blocks_count) return -EIO;
    return write_bytes((uint64_t)blk * block_size, buf, block_size);
}

static int write_superblock(void) {
    return write_bytes(EXT2_SUPERBLOCK_OFFSET, &sb, sizeof(sb));
}

static int write_group(uint32_t g) {
    return write_bytes(gd_table_offset + (uint64_t)g * sizeof(ext2_group_desc_t),
                       &groups[g], sizeof(ext2_group_desc_t));
}

static int sync_result(int rc) {
    int sync_rc = block_cache_sync() == 0 ? 0 : -EIO;
    return rc != 0 ? rc : sync_rc;
}

/* ------------------------------------------------------------------ */
/* Inodes                                                              */
/* ------------------------------------------------------------------ */

/* The kernel has no wall clock yet; the newest timestamp recorded in the
   superblock is the best available lower bound for "now". */
static uint32_t fs_timestamp(void) {
    uint32_t t = sb.s_wtime;
    if (sb.s_mtime > t) t = sb.s_mtime;
    if (sb.s_lastcheck > t) t = sb.s_lastcheck;
    return t ? t : 1;
}

static int is_dir(const ext2_inode_t *ino) { return (ino->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR; }
static int is_file(const ext2_inode_t *ino) { return (ino->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG; }
static int is_symlink(const ext2_inode_t *ino) { return (ino->i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK; }

static uint32_t osd2_u16(const ext2_inode_t *ino, uint32_t at) {
    return (uint32_t)ino->i_osd2[at] | (uint32_t)ino->i_osd2[at + 1] << 8;
}

static void osd2_set_u16(ext2_inode_t *ino, uint32_t at, uint32_t value) {
    ino->i_osd2[at] = (uint8_t)value;
    ino->i_osd2[at + 1] = (uint8_t)(value >> 8);
}

static uint32_t inode_uid(const ext2_inode_t *ino) {
    return ino->i_uid | osd2_u16(ino, EXT2_OSD2_UID_HIGH) << 16;
}

static uint32_t inode_gid(const ext2_inode_t *ino) {
    return ino->i_gid | osd2_u16(ino, EXT2_OSD2_GID_HIGH) << 16;
}

static void inode_set_owner(ext2_inode_t *ino, uint32_t uid, uint32_t gid) {
    ino->i_uid = (uint16_t)uid;
    ino->i_gid = (uint16_t)gid;
    osd2_set_u16(ino, EXT2_OSD2_UID_HIGH, uid >> 16);
    osd2_set_u16(ino, EXT2_OSD2_GID_HIGH, gid >> 16);
}

static uint32_t group_of_inode(uint32_t ino) { return (ino - 1) / sb.s_inodes_per_group; }

static int inode_offset(uint32_t ino, uint64_t *out) {
    if (ino == 0 || ino > sb.s_inodes_count) return -EIO;
    uint32_t g = group_of_inode(ino);
    uint32_t idx = (ino - 1) % sb.s_inodes_per_group;
    *out = (uint64_t)groups[g].bg_inode_table * block_size + (uint64_t)idx * inode_size;
    return 0;
}

static int read_inode(uint32_t ino, ext2_inode_t *out) {
    uint64_t off = 0;
    int rc = inode_offset(ino, &off);
    if (rc != 0) return rc;
    return read_bytes(off, out, sizeof(ext2_inode_t));
}

/* Writes only the revision-0 part so that the extended area of large
   inodes (extra timestamps, in-inode xattrs) is preserved. */
static int write_inode(uint32_t ino, const ext2_inode_t *in) {
    uint64_t off = 0;
    int rc = inode_offset(ino, &off);
    if (rc != 0) return rc;
    return write_bytes(off, in, sizeof(ext2_inode_t));
}

/* Writes a freshly allocated inode, clearing whatever a previous owner
   left in the extended area of the slot. */
static int init_inode(uint32_t ino, const ext2_inode_t *in) {
    uint64_t off = 0;
    int rc = inode_offset(ino, &off);
    if (rc != 0) return rc;
    if (inode_size > sizeof(ext2_inode_t)) {
        rc = write_bytes(off + sizeof(ext2_inode_t), zero_block,
                         inode_size - (uint32_t)sizeof(ext2_inode_t));
        if (rc != 0) return rc;
    }
    return write_bytes(off, in, sizeof(ext2_inode_t));
}

static uint8_t dirent_file_type(uint16_t mode) {
    if (!(sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE)) return EXT2_FT_UNKNOWN;
    switch (mode & EXT2_S_IFMT) {
    case EXT2_S_IFREG:  return EXT2_FT_REG_FILE;
    case EXT2_S_IFDIR:  return EXT2_FT_DIR;
    case EXT2_S_IFCHR:  return EXT2_FT_CHRDEV;
    case EXT2_S_IFBLK:  return EXT2_FT_BLKDEV;
    case EXT2_S_IFIFO:  return EXT2_FT_FIFO;
    case EXT2_S_IFSOCK: return EXT2_FT_SOCK;
    case EXT2_S_IFLNK:  return EXT2_FT_SYMLINK;
    default:            return EXT2_FT_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* Bitmap allocation                                                   */
/* ------------------------------------------------------------------ */

static uint32_t group_block_count(uint32_t g) {
    uint32_t first = sb.s_first_data_block + g * sb.s_blocks_per_group;
    uint32_t n = sb.s_blocks_count - first;
    return n < sb.s_blocks_per_group ? n : sb.s_blocks_per_group;
}

static int alloc_block(uint32_t goal_group, uint32_t *out_blk) {
    for (uint32_t k = 0; k < group_count; ++k) {
        uint32_t g = (goal_group + k) % group_count;
        if (groups[g].bg_free_blocks_count == 0) continue;
        int rc = read_block(groups[g].bg_block_bitmap, bitmap_buf);
        if (rc != 0) return rc;
        uint32_t n = group_block_count(g);
        for (uint32_t i = 0; i < n; ++i) {
            if (i % 8 == 0 && bitmap_buf[i / 8] == 0xFF) {
                i += 7;
                continue;
            }
            uint8_t bit = (uint8_t)(1u << (i % 8));
            if (bitmap_buf[i / 8] & bit) continue;
            bitmap_buf[i / 8] |= bit;
            rc = write_block(groups[g].bg_block_bitmap, bitmap_buf);
            if (rc != 0) return rc;
            groups[g].bg_free_blocks_count--;
            if (sb.s_free_blocks_count > 0) sb.s_free_blocks_count--;
            rc = write_group(g);
            if (rc == 0) rc = write_superblock();
            if (rc != 0) return rc;
            *out_blk = sb.s_first_data_block + g * sb.s_blocks_per_group + i;
            return 0;
        }
    }
    return -ENOSPC;
}

static int free_block(uint32_t blk) {
    if (blk < sb.s_first_data_block || blk >= sb.s_blocks_count) return -EIO;
    uint32_t rel = blk - sb.s_first_data_block;
    uint32_t g = rel / sb.s_blocks_per_group;
    uint32_t i = rel % sb.s_blocks_per_group;
    uint8_t bit = (uint8_t)(1u << (i % 8));
    int rc = read_block(groups[g].bg_block_bitmap, bitmap_buf);
    if (rc != 0) return rc;
    if (!(bitmap_buf[i / 8] & bit)) return -EIO;
    bitmap_buf[i / 8] &= (uint8_t)~bit;
    rc = write_block(groups[g].bg_block_bitmap, bitmap_buf);
    if (rc != 0) return rc;
    groups[g].bg_free_blocks_count++;
    sb.s_free_blocks_count++;
    rc = write_group(g);
    if (rc != 0) return rc;
    return write_superblock();
}

static int alloc_inode(uint32_t goal_group, int dir, uint32_t *out_ino) {
    for (uint32_t k = 0; k < group_count; ++k) {
        uint32_t g = (goal_group + k) % group_count;
        if (groups[g].bg_free_inodes_count == 0) continue;
        int rc = read_block(groups[g].bg_inode_bitmap, bitmap_buf);
        if (rc != 0) return rc;
        for (uint32_t i = 0; i < sb.s_inodes_per_group; ++i) {
            uint32_t ino = g * sb.s_inodes_per_group + i + 1;
            if (ino > sb.s_inodes_count) break;
            if (ino < first_ino) continue;
            uint8_t bit = (uint8_t)(1u << (i % 8));
            if (bitmap_buf[i / 8] & bit) continue;
            bitmap_buf[i / 8] |= bit;
            rc = write_block(groups[g].bg_inode_bitmap, bitmap_buf);
            if (rc != 0) return rc;
            groups[g].bg_free_inodes_count--;
            if (dir) groups[g].bg_used_dirs_count++;
            if (sb.s_free_inodes_count > 0) sb.s_free_inodes_count--;
            rc = write_group(g);
            if (rc == 0) rc = write_superblock();
            if (rc != 0) return rc;
            *out_ino = ino;
            return 0;
        }
    }
    return -ENOSPC;
}

static int free_inode(uint32_t ino, int dir) {
    if (ino < first_ino || ino > sb.s_inodes_count) return -EIO;
    uint32_t g = group_of_inode(ino);
    uint32_t i = (ino - 1) % sb.s_inodes_per_group;
    uint8_t bit = (uint8_t)(1u << (i % 8));
    int rc = read_block(groups[g].bg_inode_bitmap, bitmap_buf);
    if (rc != 0) return rc;
    if (!(bitmap_buf[i / 8] & bit)) return -EIO;
    bitmap_buf[i / 8] &= (uint8_t)~bit;
    rc = write_block(groups[g].bg_inode_bitmap, bitmap_buf);
    if (rc != 0) return rc;
    groups[g].bg_free_inodes_count++;
    if (dir && groups[g].bg_used_dirs_count > 0) groups[g].bg_used_dirs_count--;
    sb.s_free_inodes_count++;
    rc = write_group(g);
    if (rc != 0) return rc;
    return write_superblock();
}

/* ------------------------------------------------------------------ */
/* Block maps                                                          */
/* ------------------------------------------------------------------ */

static int block_path(uint32_t lb, ext2_block_path_t *p) {
    uint32_t ptrs = block_size / 4;
    uint32_t rem = lb;
    if (rem < EXT2_NDIR_BLOCKS) {
        p->depth = 0;
        p->slot = rem;
        return 0;
    }
    rem -= EXT2_NDIR_BLOCKS;
    if (rem < ptrs) {
        p->depth = 1;
        p->slot = 12;
        p->index[0] = rem;
        return 0;
    }
    rem -= ptrs;
    if (rem < ptrs * ptrs) {
        p->depth = 2;
        p->slot = 13;
        p->index[0] = rem / ptrs;
        p->index[1] = rem % ptrs;
        return 0;
    }
    rem -= ptrs * ptrs;
    if (rem >= ptrs * ptrs * ptrs) return -EIO;
    p->depth = 3;
    p->slot = 14;
    p->index[0] = rem / (ptrs * ptrs);
    p->index[1] = (rem / ptrs) % ptrs;
    p->index[2] = rem % ptrs;
    return 0;
}

/* Resolve a logical block to a physical one; *out_blk == 0 marks a hole. */
static int inode_get_block(const ext2_inode_t *ino, uint32_t lb, uint32_t *out_blk) {
    ext2_block_path_t p;
    int rc = block_path(lb, &p);
    if (rc != 0) return rc;
    uint32_t blk = ino->i_block[p.slot];
    for (uint32_t d = 0; d < p.depth && blk; ++d) {
        rc = read_block(blk, indirect_buf[0]);
        if (rc != 0) return rc;
        blk = indirect_buf[0][p.index[d]];
    }
    *out_blk = blk;
    return 0;
}

/* Map a fresh data block at logical index lb, which must be a hole,
   allocating any missing indirect blocks on the way. Free space is
   checked up front so that ENOSPC never strands a half-built path. */
static int inode_map_new_block(ext2_inode_t *ino, uint32_t goal_group, uint32_t lb,
                               uint32_t *out_blk) {
    ext2_block_path_t p;
    int rc = block_path(lb, &p);
    if (rc != 0) return rc;

    uint32_t needed = 1;
    uint32_t blk = ino->i_block[p.slot];
    for (uint32_t d = 0; d < p.depth; ++d) {
        if (!blk) {
            needed += p.depth - d;
            break;
        }
        rc = read_block(blk, indirect_buf[0]);
        if (rc != 0) return rc;
        blk = indirect_buf[0][p.index[d]];
    }
    if (blk && needed == 1) return -EIO;
    if (sb.s_free_blocks_count < needed) return -ENOSPC;

    uint32_t cur = ino->i_block[p.slot];
    if (!cur) {
        rc = alloc_block(goal_group, &cur);
        if (rc != 0) return rc;
        if (p.depth > 0) {
            rc = write_block(cur, zero_block);
            if (rc != 0) return rc;
        }
        ino->i_block[p.slot] = cur;
        ino->i_blocks += sectors_per_block;
        if (p.depth == 0) {
            *out_blk = cur;
            return 0;
        }
    }
    for (uint32_t d = 0; d < p.depth; ++d) {
        rc = read_block(cur, indirect_buf[0]);
        if (rc != 0) return rc;
        uint32_t child = indirect_buf[0][p.index[d]];
        int leaf = (d + 1 == p.depth);
        if (!child) {
            rc = alloc_block(goal_group, &child);
            if (rc != 0) return rc;
            if (!leaf) {
                rc = write_block(child, zero_block);
                if (rc != 0) return rc;
            }
            indirect_buf[0][p.index[d]] = child;
            rc = write_block(cur, indirect_buf[0]);
            if (rc != 0) return rc;
            ino->i_blocks += sectors_per_block;
        }
        cur = child;
    }
    *out_blk = cur;
    return 0;
}

static int free_indirect(uint32_t blk, uint32_t level) {
    uint32_t *table = indirect_buf[level - 1];
    int rc = read_block(blk, table);
    if (rc != 0) return rc;
    uint32_t ptrs = block_size / 4;
    for (uint32_t i = 0; i < ptrs; ++i) {
        uint32_t child = table[i];
        if (!child) continue;
        rc = (level > 1) ? free_indirect(child, level - 1) : free_block(child);
        if (rc != 0) return rc;
    }
    return free_block(blk);
}

/* Free the part of the tree below indirect block blk that maps logical
   blocks at or beyond keep. base is the logical index of the first data block
   the tree covers. *empty tells the caller that nothing is left below blk, so
   blk itself can go too. Every freed block, indirect ones included, is added
   to *freed. */
static int trim_indirect(uint32_t blk, uint32_t level, uint32_t base, uint32_t keep,
                         uint32_t *freed, int *empty) {
    uint32_t *table = indirect_buf[level - 1];
    int rc = read_block(blk, table);
    if (rc != 0) return rc;
    uint32_t ptrs = block_size / 4;
    uint32_t span = 1;
    for (uint32_t l = 1; l < level; ++l) span *= ptrs;

    int kept = 0;
    int changed = 0;
    for (uint32_t i = 0; i < ptrs && rc == 0; ++i) {
        uint32_t child = table[i];
        if (!child) continue;
        uint32_t child_base = base + i * span;
        if (child_base + span <= keep) {
            kept = 1;
            continue;
        }
        if (level > 1) {
            int child_empty = 0;
            rc = trim_indirect(child, level - 1, child_base, keep, freed, &child_empty);
            if (rc != 0) break;
            if (!child_empty) {
                kept = 1;
                continue;
            }
        }
        rc = free_block(child);
        if (rc != 0) break;
        (*freed)++;
        table[i] = 0;
        changed = 1;
    }
    /* A failed pass still persists the pointers it has already released. */
    if (changed && (kept || rc != 0)) {
        int wr = write_block(blk, table);
        if (rc == 0) rc = wr;
    }
    *empty = rc == 0 && !kept;
    return rc;
}

/* Release every data and indirect block that maps logical block keep or
   later and adjust i_blocks. The size is left to the caller. */
static int inode_trim_blocks(ext2_inode_t *ino, uint32_t keep) {
    uint32_t freed = 0;
    int rc = 0;
    for (uint32_t i = keep; i < EXT2_NDIR_BLOCKS && rc == 0; ++i) {
        if (!ino->i_block[i]) continue;
        rc = free_block(ino->i_block[i]);
        if (rc != 0) break;
        ino->i_block[i] = 0;
        freed++;
    }
    uint32_t ptrs = block_size / 4;
    uint32_t base = EXT2_NDIR_BLOCKS;
    uint32_t span = ptrs;
    for (uint32_t level = 1; level <= 3 && rc == 0; ++level) {
        uint32_t slot = EXT2_NDIR_BLOCKS - 1 + level;
        if (ino->i_block[slot] && base + span > keep) {
            int empty = 0;
            rc = trim_indirect(ino->i_block[slot], level, base, keep, &freed, &empty);
            if (rc == 0 && empty) {
                rc = free_block(ino->i_block[slot]);
                if (rc == 0) {
                    ino->i_block[slot] = 0;
                    freed++;
                }
            }
        }
        base += span;
        if (level < 3) span *= ptrs;
    }
    uint32_t sectors = freed * sectors_per_block;
    ino->i_blocks = ino->i_blocks > sectors ? ino->i_blocks - sectors : 0;
    return rc;
}

/* Device nodes and fast symlinks keep payload, not block numbers, in i_block. */
static int inode_has_block_map(const ext2_inode_t *ino) {
    uint16_t fmt = ino->i_mode & EXT2_S_IFMT;
    if (fmt == EXT2_S_IFREG || fmt == EXT2_S_IFDIR) return 1;
    if (fmt == EXT2_S_IFLNK) {
        uint32_t xattr_sectors = ino->i_file_acl ? sectors_per_block : 0;
        return ino->i_blocks > xattr_sectors;
    }
    return 0;
}

/* Release all data and indirect blocks; the xattr block is kept. */
static int inode_release_blocks(ext2_inode_t *ino) {
    if (!inode_has_block_map(ino)) return 0;
    int rc = 0;
    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS && rc == 0; ++i) {
        if (!ino->i_block[i]) continue;
        rc = free_block(ino->i_block[i]);
        ino->i_block[i] = 0;
    }
    for (uint32_t level = 1; level <= 3 && rc == 0; ++level) {
        uint32_t slot = EXT2_NDIR_BLOCKS - 1 + level;
        if (!ino->i_block[slot]) continue;
        rc = free_indirect(ino->i_block[slot], level);
        ino->i_block[slot] = 0;
    }
    if (rc != 0) return rc;
    ino->i_blocks = ino->i_file_acl ? sectors_per_block : 0;
    ino->i_size = 0;
    ino->i_dir_acl = 0;
    return 0;
}

static int inode_release_xattr(ext2_inode_t *ino) {
    if (!ino->i_file_acl) return 0;
    uint32_t blk = ino->i_file_acl;
    int rc = read_block(blk, block_buf);
    if (rc != 0) return rc;
    uint32_t *header = (uint32_t *)block_buf;
    if (header[0] != EXT2_XATTR_MAGIC || header[1] == 0) return -EIO;
    if (header[1] > 1) {
        header[1]--;
        rc = write_block(blk, block_buf);
    } else {
        rc = free_block(blk);
    }
    if (rc != 0) return rc;
    ino->i_file_acl = 0;
    if (ino->i_blocks >= sectors_per_block) ino->i_blocks -= sectors_per_block;
    return 0;
}

/* Free an inode whose last link has already been removed. */
static int inode_destroy(uint32_t ino_num, ext2_inode_t *ino) {
    int dir = is_dir(ino);
    /* The number is about to be reused; nothing cached may refer to it. */
    dcache_invalidate_inode(DCACHE_FS_EXT2, ino_num);
    int rc = inode_release_blocks(ino);
    if (rc == 0) rc = inode_release_xattr(ino);
    if (rc != 0) return rc;
    ino->i_links_count = 0;
    ino->i_dtime = fs_timestamp();
    rc = write_inode(ino_num, ino);
    if (rc != 0) return rc;
    return free_inode(ino_num, dir);
}

/* ------------------------------------------------------------------ */
/* Directories                                                         */
/* ------------------------------------------------------------------ */

static uint32_t dir_rec_len(uint32_t name_len) {
    return (DIR_ENTRY_HEADER_LEN + name_len + 3) & ~3u;
}

static int name_matches(const ext2_dir_entry_t *ent, const char *name, uint32_t len) {
    if (ent->inode == 0 || ent->name_len != len) return 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (ent->name[i] != name[i]) return 0;
    }
    return 1;
}

static int name_is_dot(const char *name, uint32_t len) {
    return len == 1 && name[0] == '.';
}

static int name_is_dotdot(const char *name, uint32_t len) {
    return len == 2 && name[0] == '.' && name[1] == '.';
}

static int dir_entry_valid(const ext2_dir_entry_t *ent, uint32_t off) {
    return ent->rec_len >= DIR_ENTRY_HEADER_LEN && (ent->rec_len & 3u) == 0 &&
           off + ent->rec_len <= block_size &&
           (uint32_t)ent->name_len + DIR_ENTRY_HEADER_LEN <= ent->rec_len;
}

/* Visit every record of a directory. A visitor that modifies dir_buf must
   write pos->block back itself and then stop the walk. */
static int dir_walk(const ext2_inode_t *dir, ext2_dir_visit_fn visit, void *ctx) {
    uint32_t nblocks = dir->i_size / block_size;
    for (uint32_t lb = 0; lb < nblocks; ++lb) {
        uint32_t blk = 0;
        int rc = inode_get_block(dir, lb, &blk);
        if (rc != 0) return rc;
        if (!blk) continue;
        rc = read_block(blk, dir_buf);
        if (rc != 0) return rc;
        ext2_dir_pos_t pos = { blk, 0, NO_PREV_ENTRY };
        while (pos.offset < block_size) {
            if (pos.offset + DIR_ENTRY_HEADER_LEN > block_size) return -EIO;
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(dir_buf + pos.offset);
            if (!dir_entry_valid(ent, pos.offset)) return -EIO;
            rc = visit(ent, &pos, ctx);
            if (rc != 0) return rc;
            pos.prev_offset = pos.offset;
            pos.offset += ent->rec_len;
        }
    }
    return 0;
}

typedef struct dir_name_ctx {
    const char *name;
    uint32_t len;
    uint32_t ino;
    uint8_t file_type;
} dir_name_ctx_t;

static int visit_lookup(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    (void)pos;
    dir_name_ctx_t *c = (dir_name_ctx_t *)ctx;
    if (!name_matches(ent, c->name, c->len)) return 0;
    c->ino = ent->inode;
    return 1;
}

static int dir_lookup(const ext2_inode_t *dir, const char *name, uint32_t len, uint32_t *out_ino) {
    if (!is_dir(dir)) return -ENOTDIR;
    if (len == 0 || len > EXT2_NAME_LEN) return -ENOENT;
    dir_name_ctx_t c = { name, len, 0, 0 };
    int rc = dir_walk(dir, visit_lookup, &c);
    if (rc < 0) return rc;
    if (rc == 0) return -ENOENT;
    *out_ino = c.ino;
    return 0;
}

static int visit_not_empty(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    (void)pos;
    (void)ctx;
    if (ent->inode == 0) return 0;
    if (name_is_dot(ent->name, ent->name_len) || name_is_dotdot(ent->name, ent->name_len)) return 0;
    return 1;
}

static int dir_is_empty(const ext2_inode_t *dir) {
    int rc = dir_walk(dir, visit_not_empty, 0);
    if (rc < 0) return rc;
    return rc == 0;
}

static void dir_fill_entry(ext2_dir_entry_t *ent, const dir_name_ctx_t *c) {
    ent->inode = c->ino;
    ent->name_len = (uint8_t)c->len;
    ent->file_type = c->file_type;
    for (uint32_t i = 0; i < c->len; ++i) ent->name[i] = c->name[i];
}

static int visit_insert(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    dir_name_ctx_t *c = (dir_name_ctx_t *)ctx;
    uint32_t need = dir_rec_len(c->len);
    if (ent->inode == 0) {
        if (ent->rec_len < need) return 0;
        dir_fill_entry(ent, c);
    } else {
        uint32_t used = dir_rec_len(ent->name_len);
        if (ent->rec_len < used + need) return 0;
        ext2_dir_entry_t *next = (ext2_dir_entry_t *)(dir_buf + pos->offset + used);
        next->rec_len = (uint16_t)(ent->rec_len - used);
        ent->rec_len = (uint16_t)used;
        dir_fill_entry(next, c);
    }
    int rc = write_block(pos->block, dir_buf);
    return rc != 0 ? rc : 1;
}

/* Insert name -> child into dir, growing it by one block if no record has
   room. Writes the directory inode whenever it changes. */
static int dir_add_entry(uint32_t dir_ino, ext2_inode_t *dir, const char *name, uint32_t len,
                         uint32_t child_ino, uint8_t file_type) {
    if (len == 0 || len > EXT2_NAME_LEN) return -EINVAL;
    dcache_invalidate_entry(DCACHE_FS_EXT2, dir_ino, name, len);
    int rc;
    /* Entries are inserted linearly, which would leave an htree index
       stale; dropping the flag turns the directory into a valid linear
       one (the index blocks read as empty records). */
    if (dir->i_flags & EXT2_INDEX_FL) {
        dir->i_flags &= ~(uint32_t)EXT2_INDEX_FL;
        rc = write_inode(dir_ino, dir);
        if (rc != 0) return rc;
    }
    dir_name_ctx_t c = { name, len, child_ino, file_type };
    rc = dir_walk(dir, visit_insert, &c);
    if (rc < 0) return rc;
    if (rc > 0) return 0;

    uint32_t blk = 0;
    rc = inode_map_new_block(dir, group_of_inode(dir_ino), dir->i_size / block_size, &blk);
    if (rc != 0) return rc;
    buf_zero(dir_buf, block_size);
    ext2_dir_entry_t *ent = (ext2_dir_entry_t *)dir_buf;
    ent->rec_len = (uint16_t)block_size;
    dir_fill_entry(ent, &c);
    rc = write_block(blk, dir_buf);
    if (rc != 0) return rc;
    dir->i_size += block_size;
    return write_inode(dir_ino, dir);
}

static int visit_remove(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    dir_name_ctx_t *c = (dir_name_ctx_t *)ctx;
    if (!name_matches(ent, c->name, c->len)) return 0;
    if (pos->prev_offset == NO_PREV_ENTRY) {
        ent->inode = 0;
    } else {
        ext2_dir_entry_t *prev = (ext2_dir_entry_t *)(dir_buf + pos->prev_offset);
        prev->rec_len = (uint16_t)(prev->rec_len + ent->rec_len);
    }
    int rc = write_block(pos->block, dir_buf);
    return rc != 0 ? rc : 1;
}

static int dir_remove_entry(uint32_t dir_ino, const ext2_inode_t *dir, const char *name,
                            uint32_t len) {
    dcache_invalidate_entry(DCACHE_FS_EXT2, dir_ino, name, len);
    dir_name_ctx_t c = { name, len, 0, 0 };
    int rc = dir_walk(dir, visit_remove, &c);
    if (rc < 0) return rc;
    return rc > 0 ? 0 : -ENOENT;
}

typedef struct dir_retarget_ctx {
    const char *name;
    uint32_t len;
    uint32_t new_ino;
    uint8_t file_type;
} dir_retarget_ctx_t;

static int visit_retarget(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    dir_retarget_ctx_t *c = (dir_retarget_ctx_t *)ctx;
    if (!name_matches(ent, c->name, c->len)) return 0;
    ent->inode = c->new_ino;
    ent->file_type = c->file_type;
    int rc = write_block(pos->block, dir_buf);
    return rc != 0 ? rc : 1;
}

/* Point an existing entry at a different inode. */
static int dir_retarget_entry(uint32_t dir_ino, const ext2_inode_t *dir, const char *name,
                              uint32_t len, uint32_t new_ino, uint8_t file_type) {
    dcache_invalidate_entry(DCACHE_FS_EXT2, dir_ino, name, len);
    dir_retarget_ctx_t c = { name, len, new_ino, file_type };
    int rc = dir_walk(dir, visit_retarget, &c);
    if (rc < 0) return rc;
    return rc > 0 ? 0 : -EIO;
}

typedef struct dir_list_ctx {
    vfs_dir_cb cb;
    void *ctx;
} dir_list_ctx_t;

static int visit_list(ext2_dir_entry_t *ent, const ext2_dir_pos_t *pos, void *ctx) {
    (void)pos;
    dir_list_ctx_t *c = (dir_list_ctx_t *)ctx;
    if (ent->inode == 0 || ent->name_len == 0) return 0;
    char name[EXT2_NAME_LEN + 1];
    for (uint32_t i = 0; i < ent->name_len; ++i) name[i] = ent->name[i];
    name[ent->name_len] = 0;
    c->cb(name, ent->file_type, c->ctx);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Path resolution                                                     */
/* ------------------------------------------------------------------ */

static int next_component(const char **cursor, const char *end, const char **name, uint32_t *len) {
    const char *p = *cursor;
    while (p < end && *p == '/') p++;
    if (p >= end) {
        *cursor = p;
        return 0;
    }
    const char *start = p;
    while (p < end && *p != '/') p++;
    *name = start;
    *len = (uint32_t)(p - start);
    *cursor = p;
    return 1;
}

static const char *path_end(const char *path) {
    while (*path) path++;
    return path;
}

/* Read-path lookup through the dentry cache. The mutating operations call
   dir_lookup directly so that their decisions never depend on cached state. */
static int dir_lookup_cached(uint32_t dir_ino, const ext2_inode_t *dir, const char *name,
                             uint32_t len, uint32_t *out_ino) {
    if (!is_dir(dir)) return -ENOTDIR;
    switch (dcache_lookup(DCACHE_FS_EXT2, dir_ino, name, len, out_ino)) {
    case DCACHE_HIT_POSITIVE: return 0;
    case DCACHE_HIT_NEGATIVE: return -ENOENT;
    case DCACHE_MISS: break;
    }
    int rc = dir_lookup(dir, name, len, out_ino);
    if (rc == 0) dcache_insert(DCACHE_FS_EXT2, dir_ino, name, len, *out_ino);
    else if (rc == -ENOENT) dcache_insert_negative(DCACHE_FS_EXT2, dir_ino, name, len);
    return rc;
}

static int resolve_range(const char *path, const char *end, uint32_t *out_ino, ext2_inode_t *out) {
    uint32_t ino = EXT2_ROOT_INO;
    int rc = read_inode(ino, out);
    if (rc != 0) return rc;
    const char *cursor = path;
    const char *name = 0;
    uint32_t len = 0;
    while (next_component(&cursor, end, &name, &len)) {
        uint32_t child = 0;
        rc = dir_lookup_cached(ino, out, name, len, &child);
        if (rc != 0) return rc;
        rc = read_inode(child, out);
        if (rc != 0) return rc;
        ino = child;
    }
    if (out_ino) *out_ino = ino;
    return 0;
}

static int resolve_path(const char *path, uint32_t *out_ino, ext2_inode_t *out) {
    return resolve_range(path, path_end(path), out_ino, out);
}

/* Split path into its parent directory and final component. Returns -EBUSY
   for the root itself, which has no parent entry. */
static int resolve_parent(const char *path, uint32_t *parent_ino, ext2_inode_t *parent,
                          const char **name, uint32_t *len) {
    const char *end = path_end(path);
    const char *cursor = path;
    const char *comp = 0;
    uint32_t comp_len = 0;
    const char *last = 0;
    uint32_t last_len = 0;
    while (next_component(&cursor, end, &comp, &comp_len)) {
        last = comp;
        last_len = comp_len;
    }
    if (!last) return -EBUSY;
    int rc = resolve_range(path, last, parent_ino, parent);
    if (rc != 0) return rc;
    if (!is_dir(parent)) return -ENOTDIR;
    *name = last;
    *len = last_len;
    return 0;
}

/* Reject moving a directory below itself by walking up from the new parent. */
static int check_not_ancestor(uint32_t ancestor, uint32_t start) {
    uint32_t cur = start;
    for (uint32_t steps = 0; steps < sb.s_inodes_count; ++steps) {
        if (cur == ancestor) return -EINVAL;
        if (cur == EXT2_ROOT_INO) return 0;
        ext2_inode_t node;
        int rc = read_inode(cur, &node);
        if (rc != 0) return rc;
        uint32_t up = 0;
        rc = dir_lookup(&node, "..", 2, &up);
        if (rc != 0) return rc == -ENOENT ? -EIO : rc;
        cur = up;
    }
    return -EIO;
}

/* ------------------------------------------------------------------ */
/* Mount and read API                                                  */
/* ------------------------------------------------------------------ */

int ext2_mount_auto(void) {
    mounted = 0;
    writable = 0;
    dcache_invalidate_fs(DCACHE_FS_EXT2);
    if (groups) {
        kfree(groups);
        groups = 0;
    }

    mbr_t m;
    base_lba = 0;
    if (mbr_read(&m) == 0 && mbr_valid(&m)) {
        for (int i = 0; i < 4; ++i) {
            if (m.parts[i].type == 0x83 && m.parts[i].sectors != 0) {
                base_lba = m.parts[i].lba_start;
                break;
            }
        }
    }

    if (read_bytes(EXT2_SUPERBLOCK_OFFSET, &sb, sizeof(sb)) != 0) return -1;
    if (sb.s_magic != EXT2_SUPER_MAGIC) return -1;
    if (sb.s_log_block_size > 2) return -1;
    block_size = 1024U << sb.s_log_block_size;
    sectors_per_block = block_size / 512;

    if (sb.s_rev_level == EXT2_GOOD_OLD_REV) {
        inode_size = EXT2_GOOD_OLD_INODE_SIZE;
        first_ino = EXT2_GOOD_OLD_FIRST_INO;
    } else {
        if (sb.s_feature_incompat & ~(uint32_t)EXT2_SUPPORTED_INCOMPAT) return -1;
        inode_size = sb.s_inode_size;
        first_ino = sb.s_first_ino;
    }
    if (inode_size < EXT2_GOOD_OLD_INODE_SIZE || inode_size > block_size ||
        (inode_size & (inode_size - 1)) != 0)
        return -1;
    if (sb.s_blocks_per_group == 0 || sb.s_blocks_per_group > block_size * 8 ||
        sb.s_inodes_per_group == 0 || sb.s_inodes_per_group > block_size * 8 ||
        sb.s_first_data_block >= sb.s_blocks_count || first_ino <= EXT2_ROOT_INO)
        return -1;

    group_count = (sb.s_blocks_count - sb.s_first_data_block + sb.s_blocks_per_group - 1) /
                  sb.s_blocks_per_group;
    if ((uint64_t)group_count * sb.s_inodes_per_group < sb.s_inodes_count) return -1;

    uint32_t gd_bytes = group_count * (uint32_t)sizeof(ext2_group_desc_t);
    groups = (ext2_group_desc_t *)kalloc(gd_bytes);
    if (!groups) return -1;
    gd_table_offset = (uint64_t)(sb.s_first_data_block + 1) * block_size;
    if (read_bytes(gd_table_offset, groups, gd_bytes) != 0) {
        kfree(groups);
        groups = 0;
        return -1;
    }

    writable = sb.s_rev_level == EXT2_GOOD_OLD_REV ||
               (sb.s_feature_ro_compat & ~(uint32_t)EXT2_SUPPORTED_RO_COMPAT) == 0;
    mounted = 1;
    return 0;
}

int ext2_is_mounted(void) { return mounted; }

int ext2_info(ext2_info_t *out) {
    if (!mounted || !out) return -1;
    out->base_lba = base_lba;
    out->block_size = block_size;
    out->inode_size = inode_size;
    out->blocks = sb.s_blocks_count;
    out->inodes = sb.s_inodes_count;
    out->free_blocks = sb.s_free_blocks_count;
    out->free_inodes = sb.s_free_inodes_count;
    return 0;
}

int ext2_list_dir(const char *path, vfs_dir_cb cb, void *ctx) {
    if (!mounted || !path || !cb) return -EINVAL;
    ext2_inode_t dir;
    int rc = resolve_path(path, 0, &dir);
    if (rc != 0) return rc;
    if (!is_dir(&dir)) return -ENOTDIR;
    dir_list_ctx_t c = { cb, ctx };
    rc = dir_walk(&dir, visit_list, &c);
    return rc < 0 ? rc : 0;
}

int ext2_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                      uint32_t out_sz, uint32_t *out_len) {
    if (!mounted || !path || !out) return -EINVAL;
    ext2_inode_t ino;
    int rc = resolve_path(path, 0, &ino);
    if (rc != 0) return rc;
    if (!is_file(&ino)) return -EISDIR;

    if (offset >= ino.i_size) {
        if (out_len) *out_len = 0;
        return 0;
    }

    uint32_t size = ino.i_size - offset;
    if (size > out_sz) size = out_sz;
    uint32_t remaining = size;
    uint8_t *dst = out;
    uint32_t block_index = offset / block_size;
    uint32_t block_off = offset % block_size;

    for (uint32_t i = block_index; remaining > 0; ++i) {
        uint32_t blk = 0;
        rc = inode_get_block(&ino, i, &blk);
        if (rc != 0) return rc;
        uint32_t take = block_size - block_off;
        if (take > remaining) take = remaining;
        if (blk) {
            rc = read_block(blk, block_buf);
            if (rc != 0) return rc;
            buf_copy(dst, block_buf + block_off, take);
        } else {
            buf_zero(dst, take);
        }
        dst += take;
        remaining -= take;
        block_off = 0;
    }
    if (out_len) *out_len = size;
    return 0;
}

/* A symlink is reported as such; the final component is never followed. */
int ext2_stat(const char *path, vfs_stat_t *out) {
    if (!mounted || !path || !out) return -EINVAL;
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    int rc = resolve_path(path, &ino_num, &ino);
    if (rc != 0) return rc;
    if (is_dir(&ino)) out->type = 2;
    else if (is_file(&ino)) out->type = 1;
    else if (is_symlink(&ino)) out->type = 3;
    else out->type = 0;
    out->size = ino.i_size;
    out->nlink = ino.i_links_count;
    out->ino = ino_num;
    out->mode = ino.i_mode & EXT2_PERMISSION_MASK;
    out->uid = inode_uid(&ino);
    out->gid = inode_gid(&ino);
    return 0;
}

/* Copies the target of a symlink inode to buf without a terminator. */
static int symlink_read_target(const ext2_inode_t *ino, char *buf) {
    uint32_t len = ino->i_size;
    if (len == 0 || len > block_size) return -EIO;
    if (!inode_has_block_map(ino)) {
        if (len > EXT2_FAST_SYMLINK_MAX) return -EIO;
        buf_copy((uint8_t *)buf, (const uint8_t *)ino->i_block, len);
        return 0;
    }
    uint32_t blk = 0;
    int rc = inode_get_block(ino, 0, &blk);
    if (rc != 0) return rc;
    if (!blk) return -EIO;
    rc = read_block(blk, block_buf);
    if (rc != 0) return rc;
    buf_copy((uint8_t *)buf, block_buf, len);
    return 0;
}

int ext2_readlink(const char *path, char *buf, uint32_t cap) {
    if (!mounted || !path || !buf || cap == 0) return -EINVAL;
    ext2_inode_t ino;
    int rc = resolve_path(path, 0, &ino);
    if (rc != 0) return rc;
    if (!is_symlink(&ino)) return -EINVAL;
    if (ino.i_size >= cap) return -ENAMETOOLONG;
    rc = symlink_read_target(&ino, buf);
    if (rc != 0) return rc;
    buf[ino.i_size] = 0;
    return (int)ino.i_size;
}

/* ------------------------------------------------------------------ */
/* Write API                                                           */
/* ------------------------------------------------------------------ */

static int write_check(const char *path) {
    if (!mounted || !path) return -EINVAL;
    if (!writable) return -EROFS;
    return 0;
}

static int write_file_data(uint32_t ino_num, ext2_inode_t *ino, const uint8_t *data, uint32_t size) {
    uint32_t goal = group_of_inode(ino_num);
    uint32_t blocks = size / block_size + (size % block_size != 0);
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t blk = 0;
        int rc = inode_map_new_block(ino, goal, i, &blk);
        if (rc != 0) return rc;
        uint32_t take = size - i * block_size;
        if (take > block_size) take = block_size;
        buf_copy(block_buf, data + (uint64_t)i * block_size, take);
        buf_zero(block_buf + take, block_size - take);
        rc = write_block(blk, block_buf);
        if (rc != 0) return rc;
    }
    ino->i_size = size;
    ino->i_mtime = ino->i_ctime = fs_timestamp();
    return 0;
}

int ext2_write_file(const char *path, const uint8_t *data, uint32_t size) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    if (size > 0 && !data) return -EINVAL;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(path, &parent_ino, &parent, &name, &len);
    if (rc == -EBUSY) return -EISDIR;
    if (rc != 0) return rc;
    if (name_is_dot(name, len) || name_is_dotdot(name, len)) return -EISDIR;
    if (len > EXT2_NAME_LEN) return -EINVAL;

    uint32_t ino_num = 0;
    ext2_inode_t ino;
    rc = dir_lookup(&parent, name, len, &ino_num);
    if (rc == 0) {
        rc = read_inode(ino_num, &ino);
        if (rc != 0) return rc;
        if (is_dir(&ino)) return -EISDIR;
        if (!is_file(&ino)) return -EINVAL;
        rc = inode_release_blocks(&ino);
        if (rc == 0) rc = write_inode(ino_num, &ino);
        if (rc != 0) return sync_result(rc);
    } else if (rc == -ENOENT) {
        rc = alloc_inode(group_of_inode(parent_ino), 0, &ino_num);
        if (rc != 0) return sync_result(rc);
        buf_zero(&ino, sizeof(ino));
        ino.i_mode = EXT2_S_IFREG | 0644;
        ino.i_links_count = 1;
        ino.i_atime = ino.i_ctime = ino.i_mtime = fs_timestamp();
        rc = init_inode(ino_num, &ino);
        if (rc == 0) rc = dir_add_entry(parent_ino, &parent, name, len, ino_num,
                                        dirent_file_type(ino.i_mode));
        if (rc != 0) {
            /* The slot may already hold a live-looking inode; destroying it
               writes links_count=0 and dtime before the bitmap bit is cleared. */
            inode_destroy(ino_num, &ino);
            return sync_result(rc);
        }
    } else {
        return rc;
    }

    rc = write_file_data(ino_num, &ino, data, size);
    if (rc != 0) {
        /* Leave a consistent empty file rather than a partial one. */
        inode_release_blocks(&ino);
        write_inode(ino_num, &ino);
        return sync_result(rc);
    }
    return sync_result(write_inode(ino_num, &ino));
}

int ext2_write_at(const char *path, uint32_t offset, const uint8_t *data, uint32_t size) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    if (size > 0 && !data) return -EINVAL;
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    rc = resolve_path(path, &ino_num, &ino);
    if (rc != 0) return rc;
    if (is_dir(&ino)) return -EISDIR;
    if (!is_file(&ino)) return -EINVAL;
    if (size == 0) return 0;
    if (offset > EXT2_FILE_SIZE_MAX || size > EXT2_FILE_SIZE_MAX - offset) return -EFBIG;

    uint32_t goal = group_of_inode(ino_num);
    uint32_t pos = offset;
    const uint8_t *src = data;
    uint32_t remaining = size;
    while (remaining > 0) {
        uint32_t lb = pos / block_size;
        uint32_t block_off = pos % block_size;
        uint32_t take = block_size - block_off;
        if (take > remaining) take = remaining;

        uint32_t blk = 0;
        rc = inode_get_block(&ino, lb, &blk);
        if (rc != 0) break;
        int fresh = (blk == 0);
        if (fresh) {
            rc = inode_map_new_block(&ino, goal, lb, &blk);
            if (rc != 0) break;
        }
        if (take == block_size) {
            rc = write_block(blk, src);
        } else {
            if (fresh) {
                buf_zero(block_buf, block_size);
            } else {
                rc = read_block(blk, block_buf);
                if (rc != 0) break;
            }
            buf_copy(block_buf + block_off, src, take);
            rc = write_block(blk, block_buf);
        }
        if (rc != 0) break;
        pos += take;
        src += take;
        remaining -= take;
    }

    /* Blocks mapped so far stay referenced by the inode even if the write
       stopped early, so the inode is written in every case. */
    if (pos > offset) {
        if (pos > ino.i_size) ino.i_size = pos;
        ino.i_mtime = ino.i_ctime = fs_timestamp();
    }
    int inode_rc = write_inode(ino_num, &ino);
    return rc != 0 ? rc : inode_rc;
}

/* Bytes past the end of file inside the last block must read as zero once
   the file grows again. */
static int zero_block_tail(const ext2_inode_t *ino, uint32_t size) {
    uint32_t tail = size % block_size;
    if (tail == 0) return 0;
    uint32_t blk = 0;
    int rc = inode_get_block(ino, size / block_size, &blk);
    if (rc != 0 || !blk) return rc;
    rc = read_block(blk, block_buf);
    if (rc != 0) return rc;
    buf_zero(block_buf + tail, block_size - tail);
    return write_block(blk, block_buf);
}

int ext2_truncate(const char *path, uint32_t size) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    if (size > EXT2_FILE_SIZE_MAX) return -EFBIG;
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    rc = resolve_path(path, &ino_num, &ino);
    if (rc != 0) return rc;
    if (is_dir(&ino)) return -EISDIR;
    if (!is_file(&ino)) return -EINVAL;
    if (size == ino.i_size) return 0;

    /* Growing only moves i_size: the new range is a hole that reads as zero. */
    if (size < ino.i_size) {
        rc = inode_trim_blocks(&ino, (size + block_size - 1) / block_size);
        if (rc == 0) {
            ino.i_size = size;
            rc = zero_block_tail(&ino, size);
        }
    } else {
        ino.i_size = size;
    }
    ino.i_mtime = ino.i_ctime = fs_timestamp();
    int inode_rc = write_inode(ino_num, &ino);
    return rc != 0 ? rc : inode_rc;
}

int ext2_sync(void) {
    if (!mounted) return -EINVAL;
    return block_cache_sync() == 0 ? 0 : -EIO;
}

int ext2_mkdir(const char *path) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(path, &parent_ino, &parent, &name, &len);
    if (rc == -EBUSY) return -EEXIST;
    if (rc != 0) return rc;
    if (name_is_dot(name, len) || name_is_dotdot(name, len)) return -EEXIST;
    if (len > EXT2_NAME_LEN) return -EINVAL;
    uint32_t existing = 0;
    rc = dir_lookup(&parent, name, len, &existing);
    if (rc == 0) return -EEXIST;
    if (rc != -ENOENT) return rc;
    if (parent.i_links_count >= EXT2_LINK_MAX) return -EMLINK;

    uint32_t ino_num = 0;
    rc = alloc_inode(group_of_inode(parent_ino), 1, &ino_num);
    if (rc != 0) return sync_result(rc);

    ext2_inode_t dir;
    buf_zero(&dir, sizeof(dir));
    dir.i_mode = EXT2_S_IFDIR | 0755;
    dir.i_links_count = 2;
    dir.i_atime = dir.i_ctime = dir.i_mtime = fs_timestamp();
    uint32_t blk = 0;
    rc = inode_map_new_block(&dir, group_of_inode(ino_num), 0, &blk);
    if (rc != 0) goto fail_inode;

    buf_zero(dir_buf, block_size);
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)dir_buf;
    dot->inode = ino_num;
    dot->rec_len = (uint16_t)dir_rec_len(1);
    dot->name_len = 1;
    dot->file_type = dirent_file_type(EXT2_S_IFDIR);
    dot->name[0] = '.';
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(dir_buf + dot->rec_len);
    dotdot->inode = parent_ino;
    dotdot->rec_len = (uint16_t)(block_size - dot->rec_len);
    dotdot->name_len = 2;
    dotdot->file_type = dirent_file_type(EXT2_S_IFDIR);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    rc = write_block(blk, dir_buf);
    if (rc != 0) goto fail_blocks;
    dir.i_size = block_size;
    rc = init_inode(ino_num, &dir);
    if (rc != 0) goto fail_blocks;

    rc = dir_add_entry(parent_ino, &parent, name, len, ino_num, dirent_file_type(EXT2_S_IFDIR));
    if (rc != 0) goto fail_blocks;
    parent.i_links_count++;
    return sync_result(write_inode(parent_ino, &parent));

fail_blocks:
    inode_destroy(ino_num, &dir);
    return sync_result(rc);
fail_inode:
    free_inode(ino_num, 1);
    return sync_result(rc);
}

int ext2_rmdir(const char *path) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(path, &parent_ino, &parent, &name, &len);
    if (rc != 0) return rc;
    if (name_is_dot(name, len)) return -EINVAL;
    if (name_is_dotdot(name, len)) return -ENOTEMPTY;

    uint32_t ino_num = 0;
    rc = dir_lookup(&parent, name, len, &ino_num);
    if (rc != 0) return rc;
    if (ino_num == EXT2_ROOT_INO) return -EBUSY;
    ext2_inode_t dir;
    rc = read_inode(ino_num, &dir);
    if (rc != 0) return rc;
    if (!is_dir(&dir)) return -ENOTDIR;
    rc = dir_is_empty(&dir);
    if (rc < 0) return rc;
    if (rc == 0) return -ENOTEMPTY;

    rc = dir_remove_entry(parent_ino, &parent, name, len);
    if (rc != 0) return sync_result(rc);
    if (parent.i_links_count > 1) parent.i_links_count--;
    rc = write_inode(parent_ino, &parent);
    if (rc == 0) rc = inode_destroy(ino_num, &dir);
    return sync_result(rc);
}

int ext2_unlink(const char *path) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(path, &parent_ino, &parent, &name, &len);
    if (rc == -EBUSY) return -EISDIR;
    if (rc != 0) return rc;
    if (name_is_dot(name, len) || name_is_dotdot(name, len)) return -EISDIR;

    uint32_t ino_num = 0;
    rc = dir_lookup(&parent, name, len, &ino_num);
    if (rc != 0) return rc;
    ext2_inode_t ino;
    rc = read_inode(ino_num, &ino);
    if (rc != 0) return rc;
    if (is_dir(&ino)) return -EISDIR;

    rc = dir_remove_entry(parent_ino, &parent, name, len);
    if (rc != 0) return sync_result(rc);
    if (ino.i_links_count > 1) {
        ino.i_links_count--;
        return sync_result(write_inode(ino_num, &ino));
    }
    return sync_result(inode_destroy(ino_num, &ino));
}

int ext2_rename(const char *old_path, const char *new_path) {
    int rc = write_check(old_path);
    if (rc != 0) return rc;
    if (!new_path) return -EINVAL;
    ext2_inode_t old_parent_node;
    ext2_inode_t new_parent_node;
    uint32_t old_parent = 0;
    uint32_t new_parent = 0;
    const char *old_name = 0;
    const char *new_name = 0;
    uint32_t old_len = 0;
    uint32_t new_len = 0;
    rc = resolve_parent(old_path, &old_parent, &old_parent_node, &old_name, &old_len);
    if (rc != 0) return rc;
    rc = resolve_parent(new_path, &new_parent, &new_parent_node, &new_name, &new_len);
    if (rc != 0) return rc;
    if (name_is_dot(old_name, old_len) || name_is_dotdot(old_name, old_len) ||
        name_is_dot(new_name, new_len) || name_is_dotdot(new_name, new_len))
        return -EINVAL;
    if (new_len > EXT2_NAME_LEN) return -EINVAL;

    /* Both handles must alias one copy when the parents coincide, or the
       second write_inode would discard changes made through the first. */
    ext2_inode_t *op = &old_parent_node;
    ext2_inode_t *np = (old_parent == new_parent) ? op : &new_parent_node;

    uint32_t src_ino = 0;
    rc = dir_lookup(op, old_name, old_len, &src_ino);
    if (rc != 0) return rc;
    ext2_inode_t src;
    rc = read_inode(src_ino, &src);
    if (rc != 0) return rc;
    int src_dir = is_dir(&src);
    int moves_dir = src_dir && old_parent != new_parent;
    if (moves_dir) {
        rc = check_not_ancestor(src_ino, new_parent);
        if (rc != 0) return rc;
    }

    uint32_t dst_ino = 0;
    ext2_inode_t dst;
    rc = dir_lookup(np, new_name, new_len, &dst_ino);
    int replace = (rc == 0);
    if (rc != 0 && rc != -ENOENT) return rc;
    if (replace) {
        if (dst_ino == src_ino) return 0;
        rc = read_inode(dst_ino, &dst);
        if (rc != 0) return rc;
        if (src_dir && !is_dir(&dst)) return -ENOTDIR;
        if (!src_dir && is_dir(&dst)) return -EISDIR;
        if (src_dir) {
            rc = dir_is_empty(&dst);
            if (rc < 0) return rc;
            if (rc == 0) return -ENOTEMPTY;
        }
    } else if (moves_dir && np->i_links_count >= EXT2_LINK_MAX) {
        return -EMLINK;
    }

    uint8_t ftype = dirent_file_type(src.i_mode);
    if (replace) {
        rc = dir_retarget_entry(new_parent, np, new_name, new_len, src_ino, ftype);
    } else {
        rc = dir_add_entry(new_parent, np, new_name, new_len, src_ino, ftype);
    }
    if (rc != 0) return sync_result(rc);

    rc = dir_remove_entry(old_parent, op, old_name, old_len);
    if (rc != 0) return sync_result(rc);

    if (moves_dir) {
        dir_retarget_ctx_t c = { "..", 2, new_parent, dirent_file_type(EXT2_S_IFDIR) };
        dcache_invalidate_entry(DCACHE_FS_EXT2, src_ino, "..", 2);
        rc = dir_walk(&src, visit_retarget, &c);
        if (rc < 0) return sync_result(rc);
        if (rc == 0) return sync_result(-EIO);
        if (op->i_links_count > 1) op->i_links_count--;
        if (!replace) np->i_links_count++;
    }
    if (replace && is_dir(&dst) && !moves_dir && np->i_links_count > 1) {
        /* dst's ".." pointed at np; src's ".." already does. */
        np->i_links_count--;
    }

    rc = write_inode(old_parent, op);
    if (rc == 0 && np != op) rc = write_inode(new_parent, np);
    if (rc != 0) return sync_result(rc);

    if (replace) {
        if (is_dir(&dst) || dst.i_links_count <= 1) {
            rc = inode_destroy(dst_ino, &dst);
        } else {
            dst.i_links_count--;
            rc = write_inode(dst_ino, &dst);
        }
    }
    return sync_result(rc);
}

int ext2_link(const char *old_path, const char *new_path) {
    int rc = write_check(old_path);
    if (rc != 0) return rc;
    if (!new_path) return -EINVAL;
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    rc = resolve_path(old_path, &ino_num, &ino);
    if (rc != 0) return rc;
    if (is_dir(&ino)) return -EISDIR;
    if (ino.i_links_count >= EXT2_LINK_MAX) return -EMLINK;

    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(new_path, &parent_ino, &parent, &name, &len);
    if (rc == -EBUSY) return -EEXIST;
    if (rc != 0) return rc;
    if (name_is_dot(name, len) || name_is_dotdot(name, len)) return -EEXIST;
    if (len > EXT2_NAME_LEN) return -ENAMETOOLONG;
    uint32_t existing = 0;
    rc = dir_lookup(&parent, name, len, &existing);
    if (rc == 0) return -EEXIST;
    if (rc != -ENOENT) return rc;

    /* Count first: a failure below can then only leave the count too high,
       never a directory entry that outlives the last reference. */
    ino.i_links_count++;
    ino.i_ctime = fs_timestamp();
    rc = write_inode(ino_num, &ino);
    if (rc == 0) rc = dir_add_entry(parent_ino, &parent, name, len, ino_num,
                                    dirent_file_type(ino.i_mode));
    if (rc != 0) {
        ino.i_links_count--;
        write_inode(ino_num, &ino);
    }
    return sync_result(rc);
}

int ext2_symlink(const char *target, const char *path) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    if (!target) return -EINVAL;
    uint32_t target_len = 0;
    while (target[target_len]) target_len++;
    if (target_len == 0) return -ENOENT;
    if (target_len >= block_size) return -ENAMETOOLONG;

    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    const char *name = 0;
    uint32_t len = 0;
    rc = resolve_parent(path, &parent_ino, &parent, &name, &len);
    if (rc == -EBUSY) return -EEXIST;
    if (rc != 0) return rc;
    if (name_is_dot(name, len) || name_is_dotdot(name, len)) return -EEXIST;
    if (len > EXT2_NAME_LEN) return -ENAMETOOLONG;
    uint32_t existing = 0;
    rc = dir_lookup(&parent, name, len, &existing);
    if (rc == 0) return -EEXIST;
    if (rc != -ENOENT) return rc;

    uint32_t ino_num = 0;
    rc = alloc_inode(group_of_inode(parent_ino), 0, &ino_num);
    if (rc != 0) return sync_result(rc);

    ext2_inode_t ino;
    buf_zero(&ino, sizeof(ino));
    ino.i_mode = EXT2_S_IFLNK | 0777;
    ino.i_links_count = 1;
    ino.i_size = target_len;
    ino.i_atime = ino.i_ctime = ino.i_mtime = fs_timestamp();
    if (target_len <= EXT2_FAST_SYMLINK_MAX) {
        buf_copy((uint8_t *)ino.i_block, (const uint8_t *)target, target_len);
    } else {
        uint32_t blk = 0;
        rc = inode_map_new_block(&ino, group_of_inode(ino_num), 0, &blk);
        if (rc == 0) {
            buf_zero(block_buf, block_size);
            buf_copy(block_buf, (const uint8_t *)target, target_len);
            rc = write_block(blk, block_buf);
        }
    }
    if (rc == 0) rc = init_inode(ino_num, &ino);
    if (rc == 0) rc = dir_add_entry(parent_ino, &parent, name, len, ino_num,
                                    dirent_file_type(ino.i_mode));
    if (rc != 0) {
        inode_destroy(ino_num, &ino);
        return sync_result(rc);
    }
    return sync_result(0);
}

/* Attribute changes act on the inode that path itself names; callers expand
   a final symlink first. */
static int attributes_load(const char *path, uint32_t *ino_num, ext2_inode_t *ino) {
    int rc = write_check(path);
    if (rc != 0) return rc;
    return resolve_path(path, ino_num, ino);
}

static int attributes_store(uint32_t ino_num, ext2_inode_t *ino) {
    ino->i_ctime = fs_timestamp();
    return sync_result(write_inode(ino_num, ino));
}

int ext2_chmod(const char *path, uint32_t mode) {
    if ((mode & ~(uint32_t)EXT2_PERMISSION_MASK) != 0) return -EINVAL;
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    int rc = attributes_load(path, &ino_num, &ino);
    if (rc != 0) return rc;
    ino.i_mode = (uint16_t)((ino.i_mode & EXT2_S_IFMT) | mode);
    return attributes_store(ino_num, &ino);
}

int ext2_chown(const char *path, uint32_t uid, uint32_t gid) {
    uint32_t ino_num = 0;
    ext2_inode_t ino;
    int rc = attributes_load(path, &ino_num, &ino);
    if (rc != 0) return rc;
    inode_set_owner(&ino, uid, gid);
    return attributes_store(ino_num, &ino);
}
