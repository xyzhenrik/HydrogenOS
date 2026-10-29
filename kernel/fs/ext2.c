#include "ext2.h"
#include "drivers/block.h"
#include "drivers/block_cache.h"
#include "drivers/mbr.h"

#include <stddef.h>

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

static int mounted = 0;
static uint32_t base_lba = 0;
static uint32_t gd_offset = 0;
static uint8_t aux_buf[4096];
static uint32_t block_size = 0;
static uint32_t sectors_per_block = 0;
static ext2_superblock_t sb;
static ext2_group_desc_t gd0;

static uint8_t sector_buf[512];
static uint8_t block_buf[4096];

static void buf_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
}

static int read_bytes(uint32_t byte_off, void *out, uint32_t len) {
    uint8_t *dst = (uint8_t *)out;
    uint32_t lba = base_lba + (byte_off / 512);
    uint32_t off = byte_off % 512;
    while (len > 0) {
        if (block_read_cached(lba, 1, sector_buf) != 0) return -1;
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

static int read_block(uint32_t block, void *out) {
    uint32_t byte_off = block * block_size;
    return read_bytes(byte_off, out, block_size);
}

static int read_inode(uint32_t ino, ext2_inode_t *out) {
    if (ino == 0 || !out) return -1;
    uint32_t idx = ino - 1;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    if (idx >= inodes_per_group) return -1; // nur Gruppe 0
    uint32_t inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    uint32_t table_block = gd0.bg_inode_table;
    uint32_t byte_off = idx * inode_size;
    uint32_t block = table_block + (byte_off / block_size);
    uint32_t off = byte_off % block_size;
    if (read_block(block, block_buf) != 0) return -1;
    buf_copy((uint8_t *)out, block_buf + off, sizeof(ext2_inode_t));
    return 0;
}

static int name_eq(const char *a, const char *b, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i) {
        if (!a[i] || a[i] != b[i]) return 0;
    }
    return b[len] == 0;
}

static int is_dir(const ext2_inode_t *ino) { return (ino->i_mode & 0xF000) == 0x4000; }
static int is_file(const ext2_inode_t *ino) { return (ino->i_mode & 0xF000) == 0x8000; }

static int find_in_dir(const ext2_inode_t *dir, const char *name, uint32_t *out_ino) {
    if (!dir || !name || !out_ino) return -1;
    for (int i = 0; i < 12; ++i) {
        uint32_t blk = dir->i_block[i];
        if (!blk) continue;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(block_buf + off);
            if (ent->rec_len == 0) break;
            if (ent->inode != 0 && ent->name_len > 0) {
                if (name_eq(ent->name, name, ent->name_len)) {
                    *out_ino = ent->inode;
                    return 0;
                }
            }
            off += ent->rec_len;
        }
    }
    return -1;
}

static int resolve_path(const char *path, ext2_inode_t *out) {
    if (!path || !out) return -1;
    ext2_inode_t cur;
    if (read_inode(2, &cur) != 0) return -1;
    if (!is_dir(&cur)) return -1;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == 0) {
        *out = cur;
        return 0;
    }
    while (*p) {
        char name[256];
        uint32_t n = 0;
        while (*p && *p != '/' && n < sizeof(name) - 1) {
            name[n++] = *p++;
        }
        name[n] = 0;
        while (*p == '/') p++;
        uint32_t child_ino = 0;
        if (find_in_dir(&cur, name, &child_ino) != 0) return -1;
        if (read_inode(child_ino, &cur) != 0) return -1;
        if (*p) {
            if (!is_dir(&cur)) return -1;
        }
    }
    *out = cur;
    return 0;
}

int ext2_mount_auto(void) {
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

    if (read_bytes(1024, &sb, sizeof(sb)) != 0) {
        mounted = 0;
        return -1;
    }
    if (sb.s_magic != 0xEF53) {
        mounted = 0;
        return -1;
    }

    block_size = 1024U << sb.s_log_block_size;
    if (block_size == 0 || block_size > sizeof(block_buf)) {
        mounted = 0;
        return -1;
    }
    sectors_per_block = block_size / 512;

    uint32_t gd_off = (block_size == 1024) ? (2 * block_size) : block_size;
    gd_offset = gd_off;
    if (read_bytes(gd_off, &gd0, sizeof(gd0)) != 0) {
        mounted = 0;
        return -1;
    }

    mounted = 1;
    return 0;
}

int ext2_is_mounted(void) { return mounted; }

int ext2_info(ext2_info_t *out) {
    if (!mounted || !out) return -1;
    out->base_lba = base_lba;
    out->block_size = block_size;
    out->inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    out->blocks = sb.s_blocks_count;
    out->inodes = sb.s_inodes_count;
    out->free_blocks = sb.s_free_blocks_count;
    out->free_inodes = sb.s_free_inodes_count;
    return 0;
}

int ext2_list_root(vfs_dir_cb cb, void *ctx) {
    if (!mounted || !cb) return -1;
    ext2_inode_t ino;
    if (read_inode(2, &ino) != 0) return -1;
    if ((ino.i_mode & 0xF000) != 0x4000) return -1;

    for (int i = 0; i < 12; ++i) {
        uint32_t blk = ino.i_block[i];
        if (!blk) continue;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(block_buf + off);
            if (ent->rec_len == 0) break;
            if (ent->inode != 0 && ent->name_len > 0) {
                char name[256];
                uint8_t n = ent->name_len;
                for (uint8_t k = 0; k < n; ++k) name[k] = ent->name[k];
                name[n] = 0;
                cb(name, ent->file_type, ctx);
            }
            off += ent->rec_len;
        }
    }
    return 0;
}

int ext2_read_file_root(const char *name, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    if (!mounted || !name || !out) return -1;
    ext2_inode_t root;
    if (read_inode(2, &root) != 0) return -1;
    if ((root.i_mode & 0xF000) != 0x4000) return -1;

    uint32_t target_ino = 0;
    for (int i = 0; i < 12 && !target_ino; ++i) {
        uint32_t blk = root.i_block[i];
        if (!blk) continue;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(block_buf + off);
            if (ent->rec_len == 0) break;
            if (ent->inode != 0 && ent->name_len > 0) {
                if (name_eq(ent->name, name, ent->name_len)) {
                    target_ino = ent->inode;
                    break;
                }
            }
            off += ent->rec_len;
        }
    }
    if (!target_ino) return -1;

    ext2_inode_t ino;
    if (read_inode(target_ino, &ino) != 0) return -1;
    if ((ino.i_mode & 0xF000) != 0x8000) return -1;

    uint32_t size = ino.i_size;
    if (size > out_sz) size = out_sz;
    uint32_t remaining = size;
    uint8_t *dst = out;

    for (int i = 0; i < 12 && remaining > 0; ++i) {
        uint32_t blk = ino.i_block[i];
        if (!blk) break;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t take = block_size;
        if (take > remaining) take = remaining;
        buf_copy(dst, block_buf, take);
        dst += take;
        remaining -= take;
    }
    if (out_len) *out_len = size - remaining;
    return 0;
}

int ext2_list_dir(const char *path, vfs_dir_cb cb, void *ctx) {
    if (!mounted || !cb) return -1;
    ext2_inode_t dir;
    if (resolve_path(path, &dir) != 0) return -1;
    if (!is_dir(&dir)) return -1;

    for (int i = 0; i < 12; ++i) {
        uint32_t blk = dir.i_block[i];
        if (!blk) continue;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(block_buf + off);
            if (ent->rec_len == 0) break;
            if (ent->inode != 0 && ent->name_len > 0) {
                char name[256];
                uint8_t n = ent->name_len;
                for (uint8_t k = 0; k < n; ++k) name[k] = ent->name[k];
                name[n] = 0;
                cb(name, ent->file_type, ctx);
            }
            off += ent->rec_len;
        }
    }
    return 0;
}

/* Resolve a logical block index to a physical block. Returns 0 with
   *out_blk == 0 for a sparse hole, and -1 only for out-of-range or I/O. */
static int inode_get_block(const ext2_inode_t *ino, uint32_t index, uint32_t *out_blk) {
    if (!ino || !out_blk) return -1;
    uint32_t ptrs = block_size / 4;
    if (index < 12) {
        *out_blk = ino->i_block[index];
        return 0;
    }
    index -= 12;
    if (index < ptrs) {
        uint32_t ind = ino->i_block[12];
        if (!ind) { *out_blk = 0; return 0; }
        if (read_block(ind, block_buf) != 0) return -1;
        *out_blk = ((uint32_t *)block_buf)[index];
        return 0;
    }
    index -= ptrs;
    if (index < ptrs * ptrs) {
        uint32_t dind = ino->i_block[13];
        if (!dind) { *out_blk = 0; return 0; }
        if (read_block(dind, block_buf) != 0) return -1;
        uint32_t l1 = ((uint32_t *)block_buf)[index / ptrs];
        if (!l1) { *out_blk = 0; return 0; }
        if (read_block(l1, block_buf) != 0) return -1;
        *out_blk = ((uint32_t *)block_buf)[index % ptrs];
        return 0;
    }
    return -1;
}

int ext2_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                      uint32_t out_sz, uint32_t *out_len) {
    if (!mounted || !path || !out) return -1;
    ext2_inode_t ino;
    if (resolve_path(path, &ino) != 0) return -1;
    if (!is_file(&ino)) return -1;

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
        if (inode_get_block(&ino, i, &blk) != 0) break;
        uint32_t take = block_size - block_off;
        if (take > remaining) take = remaining;
        if (blk) {
            if (read_block(blk, block_buf) != 0) return -1;
            buf_copy(dst, block_buf + block_off, take);
        } else {
            for (uint32_t k = 0; k < take; ++k) dst[k] = 0;
        }
        dst += take;
        remaining -= take;
        block_off = 0;
    }
    if (out_len) *out_len = size - remaining;
    return 0;
}

int ext2_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    return ext2_read_file_at(path, 0, out, out_sz, out_len);
}


/* ------------------------------------------------------------------ */
/* Write support (single block group, direct + single indirect)       */
/* ------------------------------------------------------------------ */

static uint32_t ext2_strlen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int write_bytes(uint32_t byte_off, const void *src, uint32_t len) {
    const uint8_t *sp = (const uint8_t *)src;
    uint32_t lba = base_lba + (byte_off / 512);
    uint32_t off = byte_off % 512;
    while (len > 0) {
        if (block_read_cached(lba, 1, sector_buf) != 0) return -1;
        uint32_t take = 512 - off;
        if (take > len) take = len;
        for (uint32_t i = 0; i < take; ++i) sector_buf[off + i] = sp[i];
        if (block_write_cached(lba, 1, sector_buf) != 0) return -1;
        sp += take;
        len -= take;
        lba++;
        off = 0;
    }
    return 0;
}

static int write_block(uint32_t block, const void *buf) {
    return write_bytes(block * block_size, buf, block_size);
}

static int write_inode(uint32_t ino, const ext2_inode_t *in) {
    if (!mounted || ino == 0 || !in) return -1;
    uint32_t idx = ino - 1;
    uint32_t inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    if (idx >= sb.s_inodes_per_group) return -1;
    uint32_t byte_off = gd0.bg_inode_table * block_size + idx * inode_size;
    if (inode_size >= sizeof(ext2_inode_t)) {
        uint8_t buf[256];
        uint32_t n = inode_size > sizeof(buf) ? (uint32_t)sizeof(buf) : inode_size;
        for (uint32_t i = 0; i < n; ++i) buf[i] = 0;
        uint32_t copy = (uint32_t)sizeof(ext2_inode_t);
        if (copy > n) copy = n;
        for (uint32_t i = 0; i < copy; ++i) buf[i] = ((const uint8_t *)in)[i];
        return write_bytes(byte_off, buf, n);
    }
    return write_bytes(byte_off, in, inode_size);
}

static int write_sb_gd(void) {
    if (write_bytes(1024, &sb, sizeof(sb)) != 0) return -1;
    if (write_bytes(gd_offset, &gd0, sizeof(gd0)) != 0) return -1;
    return block_cache_sync();
}

static int alloc_block(uint32_t *out_block) {
    if (!out_block || !mounted) return -1;
    if (read_block(gd0.bg_block_bitmap, aux_buf) != 0) return -1;
    uint32_t bits = block_size * 8;
    for (uint32_t i = 0; i < bits; ++i) {
        if (aux_buf[i / 8] & (1u << (i % 8))) continue;
        uint32_t blk = sb.s_first_data_block + i;
        if (blk >= sb.s_blocks_count) break;
        aux_buf[i / 8] |= (uint8_t)(1u << (i % 8));
        if (write_block(gd0.bg_block_bitmap, aux_buf) != 0) return -1;
        if (sb.s_free_blocks_count > 0) sb.s_free_blocks_count--;
        if (gd0.bg_free_blocks_count > 0) gd0.bg_free_blocks_count--;
        if (write_sb_gd() != 0) return -1;
        *out_block = blk;
        return 0;
    }
    return -1;
}

static int free_block(uint32_t blk) {
    if (!mounted || blk < sb.s_first_data_block || blk >= sb.s_blocks_count) return -1;
    uint32_t i = blk - sb.s_first_data_block;
    if (read_block(gd0.bg_block_bitmap, aux_buf) != 0) return -1;
    if (!(aux_buf[i / 8] & (1u << (i % 8)))) return 0;
    aux_buf[i / 8] &= (uint8_t)~(1u << (i % 8));
    if (write_block(gd0.bg_block_bitmap, aux_buf) != 0) return -1;
    sb.s_free_blocks_count++;
    gd0.bg_free_blocks_count++;
    return write_sb_gd();
}

static int alloc_inode(uint32_t *out_ino) {
    if (!out_ino || !mounted) return -1;
    if (read_block(gd0.bg_inode_bitmap, aux_buf) != 0) return -1;
    uint32_t bits = sb.s_inodes_per_group;
    if (bits > block_size * 8) bits = block_size * 8;
    for (uint32_t i = 0; i < bits; ++i) {
        if (aux_buf[i / 8] & (1u << (i % 8))) continue;
        uint32_t ino = i + 1;
        if (ino < sb.s_first_ino) continue;
        aux_buf[i / 8] |= (uint8_t)(1u << (i % 8));
        if (write_block(gd0.bg_inode_bitmap, aux_buf) != 0) return -1;
        if (sb.s_free_inodes_count > 0) sb.s_free_inodes_count--;
        if (gd0.bg_free_inodes_count > 0) gd0.bg_free_inodes_count--;
        if (write_sb_gd() != 0) return -1;
        *out_ino = ino;
        return 0;
    }
    return -1;
}

static int free_inode(uint32_t ino) {
    if (!mounted || ino == 0) return -1;
    uint32_t i = ino - 1;
    if (read_block(gd0.bg_inode_bitmap, aux_buf) != 0) return -1;
    if (!(aux_buf[i / 8] & (1u << (i % 8)))) return 0;
    aux_buf[i / 8] &= (uint8_t)~(1u << (i % 8));
    if (write_block(gd0.bg_inode_bitmap, aux_buf) != 0) return -1;
    sb.s_free_inodes_count++;
    gd0.bg_free_inodes_count++;
    return write_sb_gd();
}

static void inode_zero(ext2_inode_t *in) {
    uint8_t *p = (uint8_t *)in;
    for (uint32_t i = 0; i < sizeof(*in); ++i) p[i] = 0;
}

static int inode_set_block(ext2_inode_t *ino, uint32_t index, uint32_t blk) {
    if (!ino) return -1;
    uint32_t ptrs = block_size / 4;
    if (index < 12) { ino->i_block[index] = blk; return 0; }
    index -= 12;
    if (index < ptrs) {
        if (ino->i_block[12] == 0) {
            uint32_t ind = 0;
            if (alloc_block(&ind) != 0) return -1;
            for (uint32_t i = 0; i < block_size; ++i) aux_buf[i] = 0;
            if (write_block(ind, aux_buf) != 0) return -1;
            ino->i_block[12] = ind;
        }
        uint32_t ind = ino->i_block[12];
        if (read_block(ind, aux_buf) != 0) return -1;
        ((uint32_t *)aux_buf)[index] = blk;
        return write_block(ind, aux_buf);
    }
    return -1; /* double indirect not supported */
}

static void inode_free_blocks(ext2_inode_t *ino) {
    if (!ino) return;
    for (uint32_t i = 0; i < 12; ++i) {
        if (ino->i_block[i]) { free_block(ino->i_block[i]); ino->i_block[i] = 0; }
    }
    if (ino->i_block[12]) {
        uint32_t ptrs = block_size / 4;
        if (read_block(ino->i_block[12], aux_buf) == 0) {
            for (uint32_t i = 0; i < ptrs; ++i) {
                uint32_t b = ((uint32_t *)aux_buf)[i];
                if (b) free_block(b);
            }
        }
        free_block(ino->i_block[12]);
        ino->i_block[12] = 0;
    }
    ino->i_size = 0;
    ino->i_blocks = 0;
}

static uint32_t dir_rec_len(uint32_t name_len) {
    uint32_t n = 8 + name_len;
    return (n + 3) & ~3u;
}

/* Find/add a directory entry in a directory inode. */
static int dir_add_entry(ext2_inode_t *dir, uint32_t dir_ino, const char *name,
                         uint32_t child_ino, uint8_t file_type) {
    (void)dir_ino;
    uint32_t name_len = ext2_strlen(name);
    if (name_len == 0 || name_len > 255) return -1;
    uint32_t need = dir_rec_len(name_len);
    uint32_t ptrs = block_size / 4;

    for (uint32_t bi = 0; bi < 12 + ptrs; ++bi) {
        uint32_t blk = 0;
        if (bi < 12) blk = dir->i_block[bi];
        else {
            if (!dir->i_block[12]) break;
            if (read_block(dir->i_block[12], aux_buf) != 0) return -1;
            blk = ((uint32_t *)aux_buf)[bi - 12];
        }
        if (!blk) continue;
        if (read_block(blk, aux_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(aux_buf + off);
            if (ent->rec_len < 8 || off + ent->rec_len > block_size) break;
            if (ent->inode == 0 && ent->rec_len >= need) {
                ent->inode = child_ino;
                ent->name_len = (uint8_t)name_len;
                ent->file_type = file_type;
                for (uint32_t i = 0; i < name_len; ++i) ent->name[i] = name[i];
                return write_block(blk, aux_buf);
            }
            uint32_t actual = dir_rec_len(ent->name_len);
            if (ent->inode != 0 && ent->rec_len >= actual + need) {
                uint32_t old_rec = ent->rec_len;
                ent->rec_len = (uint16_t)actual;
                ext2_dir_entry_t *ne = (ext2_dir_entry_t *)(aux_buf + off + actual);
                ne->inode = child_ino;
                ne->rec_len = (uint16_t)(old_rec - actual);
                ne->name_len = (uint8_t)name_len;
                ne->file_type = file_type;
                for (uint32_t i = 0; i < name_len; ++i) ne->name[i] = name[i];
                return write_block(blk, aux_buf);
            }
            off += ent->rec_len;
        }
    }

    /* No space: append a fresh block. */
    uint32_t new_blk = 0;
    if (alloc_block(&new_blk) != 0) return -1;
    for (uint32_t i = 0; i < block_size; ++i) aux_buf[i] = 0;
    ext2_dir_entry_t *ent = (ext2_dir_entry_t *)aux_buf;
    ent->inode = child_ino;
    ent->rec_len = (uint16_t)block_size;
    ent->name_len = (uint8_t)name_len;
    ent->file_type = file_type;
    for (uint32_t i = 0; i < name_len; ++i) ent->name[i] = name[i];
    if (write_block(new_blk, aux_buf) != 0) return -1;

    uint32_t bi = 0;
    while (bi < 12 && dir->i_block[bi]) bi++;
    if (bi < 12) {
        dir->i_block[bi] = new_blk;
    } else {
        if (inode_set_block(dir, bi, new_blk) != 0) return -1;
    }
    if (dir->i_size < (bi + 1) * block_size) dir->i_size = (bi + 1) * block_size;
    return 0;
}

static int dir_remove_entry(ext2_inode_t *dir, const char *name) {
    uint32_t ptrs = block_size / 4;
    for (uint32_t bi = 0; bi < 12 + ptrs; ++bi) {
        uint32_t blk = 0;
        if (bi < 12) blk = dir->i_block[bi];
        else {
            if (!dir->i_block[12]) break;
            if (read_block(dir->i_block[12], aux_buf) != 0) return -1;
            blk = ((uint32_t *)aux_buf)[bi - 12];
        }
        if (!blk) continue;
        if (read_block(blk, aux_buf) != 0) return -1;
        uint32_t off = 0;
        while (off + 8 <= block_size) {
            ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(aux_buf + off);
            if (ent->rec_len < 8) break;
            if (ent->inode != 0 && name_eq(ent->name, name, ent->name_len)) {
                ent->inode = 0;
                return write_block(blk, aux_buf);
            }
            off += ent->rec_len;
        }
    }
    return -1;
}


/* Return parent inode number for a path's parent directory. */
static int resolve_parent(const char *path, uint32_t *parent_ino, ext2_inode_t *parent,
                          char *name, uint32_t name_cap) {
    if (!path || !parent_ino || !parent || !name) return -1;
    const char *p = path;
    while (*p == '/') p++;
    if (*p == 0) return -1;
    const char *last = 0;
    for (const char *q = p; *q; ++q) if (*q == '/') last = q;
    if (!last) {
        if (read_inode(2, parent) != 0) return -1;
        *parent_ino = 2;
    } else {
        char dirpath[256];
        uint32_t n = 0;
        while (p < last && n + 1 < sizeof(dirpath)) dirpath[n++] = *p++;
        dirpath[n] = 0;
        if (dirpath[0] == 0) {
            if (read_inode(2, parent) != 0) return -1;
            *parent_ino = 2;
        } else {
            /* Resolve the parent inode number by walking from root manually. */
            uint32_t cur = 2;
            ext2_inode_t cur_inode;
            if (read_inode(cur, &cur_inode) != 0) return -1;
            const char *q = dirpath;
            while (*q) {
                while (*q == '/') q++;
                if (!*q) break;
                char comp[256];
                uint32_t k = 0;
                while (*q && *q != '/' && k + 1 < sizeof(comp)) comp[k++] = *q++;
                comp[k] = 0;
                uint32_t next = 0;
                if (find_in_dir(&cur_inode, comp, &next) != 0) return -1;
                if (read_inode(next, &cur_inode) != 0) return -1;
                cur = next;
            }
            *parent = cur_inode;
            *parent_ino = cur;
        }
    }
    const char *nm = last ? (last + 1) : p;
    uint32_t k = 0;
    while (nm[k] && k + 1 < name_cap) { name[k] = nm[k]; ++k; }
    name[k] = 0;
    return name[0] ? 0 : -1;
}

int ext2_write_file(const char *path, const uint8_t *data, uint32_t size) {
    if (!mounted || !path || !data) return -1;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    char name[256];
    if (resolve_parent(path, &parent_ino, &parent, name, sizeof(name)) != 0) return -1;

    uint32_t ino_num = 0;
    ext2_inode_t ino;
    int existed = (find_in_dir(&parent, name, &ino_num) == 0);
    if (existed) {
        if (read_inode(ino_num, &ino) != 0) return -1;
        if (!is_file(&ino)) return -1;
        inode_free_blocks(&ino);
    } else {
        if (alloc_inode(&ino_num) != 0) return -1;
        inode_zero(&ino);
        ino.i_mode = 0100644;
        ino.i_links_count = 1;
        if (dir_add_entry(&parent, parent_ino, name, ino_num, 1) != 0) {
            free_inode(ino_num);
            return -1;
        }
        if (write_inode(parent_ino, &parent) != 0) return -1;
    }

    uint32_t blocks = (size + block_size - 1) / block_size;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t blk = 0;
        if (alloc_block(&blk) != 0) return -1;
        for (uint32_t k = 0; k < block_size; ++k) aux_buf[k] = 0;
        uint32_t remaining = size - i * block_size;
        uint32_t take = remaining > block_size ? block_size : remaining;
        for (uint32_t k = 0; k < take; ++k) aux_buf[k] = data[i * block_size + k];
        if (write_block(blk, aux_buf) != 0) return -1;
        if (inode_set_block(&ino, i, blk) != 0) return -1;
    }
    ino.i_size = size;
    ino.i_blocks = blocks * (block_size / 512);
    if (write_inode(ino_num, &ino) != 0) return -1;
    return block_cache_sync();
}

int ext2_mkdir(const char *path) {
    if (!mounted || !path) return -1;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    char name[256];
    if (resolve_parent(path, &parent_ino, &parent, name, sizeof(name)) != 0) return -1;
    uint32_t existing = 0;
    if (find_in_dir(&parent, name, &existing) == 0) return -17;

    uint32_t ino_num = 0;
    if (alloc_inode(&ino_num) != 0) return -1;
    ext2_inode_t ino;
    inode_zero(&ino);
    ino.i_mode = 040755;
    ino.i_links_count = 2;
    ino.i_size = block_size;
    uint32_t blk = 0;
    if (alloc_block(&blk) != 0) return -1;
    for (uint32_t k = 0; k < block_size; ++k) aux_buf[k] = 0;
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)aux_buf;
    dot->inode = ino_num; dot->rec_len = 12; dot->name_len = 1; dot->file_type = 2; dot->name[0] = '.';
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(aux_buf + 12);
    dotdot->inode = parent_ino; dotdot->rec_len = (uint16_t)(block_size - 12);
    dotdot->name_len = 2; dotdot->file_type = 2; dotdot->name[0] = '.'; dotdot->name[1] = '.';
    if (write_block(blk, aux_buf) != 0) return -1;
    ino.i_block[0] = blk;
    ino.i_blocks = block_size / 512;
    if (write_inode(ino_num, &ino) != 0) return -1;
    if (dir_add_entry(&parent, parent_ino, name, ino_num, 2) != 0) {
        inode_free_blocks(&ino);
        write_inode(ino_num, &ino);
        free_inode(ino_num);
        return -1;
    }
    parent.i_links_count++;
    if (write_inode(parent_ino, &parent) != 0) return -1;
    gd0.bg_used_dirs_count++;
    if (write_sb_gd() != 0) return -1;
    return block_cache_sync();
}

int ext2_unlink(const char *path) {
    if (!mounted || !path) return -1;
    ext2_inode_t parent;
    uint32_t parent_ino = 0;
    char name[256];
    if (resolve_parent(path, &parent_ino, &parent, name, sizeof(name)) != 0) return -1;
    uint32_t ino_num = 0;
    if (find_in_dir(&parent, name, &ino_num) != 0) return -2;
    ext2_inode_t ino;
    if (read_inode(ino_num, &ino) != 0) return -1;
    if (is_dir(&ino)) return -1;
    inode_free_blocks(&ino);
    if (write_inode(ino_num, &ino) != 0) return -1;
    if (free_inode(ino_num) != 0) return -1;
    if (dir_remove_entry(&parent, name) != 0) return -1;
    if (write_inode(parent_ino, &parent) != 0) return -1;
    return block_cache_sync();
}

int ext2_rename(const char *old_path, const char *new_path) {
    if (!mounted || !old_path || !new_path) return -1;
    ext2_inode_t oparent, nparent;
    uint32_t opino = 0, npino = 0;
    char oname[256], nname[256];
    if (resolve_parent(old_path, &opino, &oparent, oname, sizeof(oname)) != 0) return -1;
    if (resolve_parent(new_path, &npino, &nparent, nname, sizeof(nname)) != 0) return -1;
    uint32_t ino_num = 0;
    if (find_in_dir(&oparent, oname, &ino_num) != 0) return -2;

    /* Same directory and the new name fits in the existing record? */
    if (opino == npino) {
        uint32_t ptrs = block_size / 4;
        for (uint32_t bi = 0; bi < 12 + ptrs; ++bi) {
            uint32_t blk = 0;
            if (bi < 12) blk = oparent.i_block[bi];
            else {
                if (!oparent.i_block[12]) break;
                if (read_block(oparent.i_block[12], aux_buf) != 0) return -1;
                blk = ((uint32_t *)aux_buf)[bi - 12];
            }
            if (!blk) continue;
            if (read_block(blk, aux_buf) != 0) return -1;
            uint32_t off = 0;
            while (off + 8 <= block_size) {
                ext2_dir_entry_t *ent = (ext2_dir_entry_t *)(aux_buf + off);
                if (ent->rec_len < 8) break;
                if (ent->inode == ino_num && name_eq(ent->name, oname, ent->name_len)) {
                    uint32_t nl = ext2_strlen(nname);
                    if (dir_rec_len(nl) <= ent->rec_len) {
                        ent->name_len = (uint8_t)nl;
                        for (uint32_t i = 0; i < nl; ++i) ent->name[i] = nname[i];
                        return write_block(blk, aux_buf);
                    }
                }
                off += ent->rec_len;
            }
        }
    }

    uint8_t ftype = 1;
    ext2_inode_t ino;
    if (read_inode(ino_num, &ino) == 0 && is_dir(&ino)) ftype = 2;
    if (dir_add_entry(&nparent, npino, nname, ino_num, ftype) != 0) return -1;
    if (write_inode(npino, &nparent) != 0) return -1;
    if (dir_remove_entry(&oparent, oname) != 0) return -1;
    if (write_inode(opino, &oparent) != 0) return -1;
    return block_cache_sync();
}

int ext2_stat(const char *path, uint8_t *type, uint32_t *size) {
    if (!mounted || !path) return -1;
    ext2_inode_t ino;
    if (resolve_path(path, &ino) != 0) return -1;
    if (type) {
        if (is_dir(&ino)) *type = 2;
        else if (is_file(&ino)) *type = 1;
        else *type = 0;
    }
    if (size) *size = ino.i_size;
    return 0;
}
