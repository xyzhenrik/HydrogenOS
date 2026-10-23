#pragma once

#include <stdint.h>
#include "fs/vfs.h"

typedef struct ext2_info {
    uint32_t base_lba;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks;
    uint32_t inodes;
} ext2_info_t;

int ext2_mount_auto(void);
int ext2_is_mounted(void);
int ext2_info(ext2_info_t *out);
int ext2_list_root(vfs_dir_cb cb, void *ctx);
int ext2_read_file_root(const char *name, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int ext2_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                      uint32_t out_sz, uint32_t *out_len);
int ext2_list_dir(const char *path, vfs_dir_cb cb, void *ctx);
int ext2_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int ext2_stat(const char *path, uint8_t *type, uint32_t *size);
