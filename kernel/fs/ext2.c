#include "ext2.h"
#include "drivers/block.h"
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

    for (uint32_t i = block_index; i < 12 && remaining > 0; ++i) {
        uint32_t blk = ino.i_block[i];
        if (!blk) break;
        if (read_block(blk, block_buf) != 0) return -1;
        uint32_t take = block_size - block_off;
        if (take > remaining) take = remaining;
        buf_copy(dst, block_buf + block_off, take);
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
