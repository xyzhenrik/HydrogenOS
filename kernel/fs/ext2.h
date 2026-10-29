#pragma once

#include <stdint.h>
#include "fs/vfs.h"

typedef struct ext2_info {
    uint32_t base_lba;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks;
    uint32_t inodes;
    uint32_t free_blocks;
    uint32_t free_inodes;
} ext2_info_t;

int ext2_mount_auto(void);
int ext2_is_mounted(void);
int ext2_info(ext2_info_t *out);
int ext2_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                      uint32_t out_sz, uint32_t *out_len);
int ext2_list_dir(const char *path, vfs_dir_cb cb, void *ctx);
int ext2_stat(const char *path, uint8_t *type, uint32_t *size);
/* Mutating operations return 0 or a negative errno value. */
int ext2_write_file(const char *path, const uint8_t *data, uint32_t size);
int ext2_mkdir(const char *path);
int ext2_rmdir(const char *path);
int ext2_unlink(const char *path);
int ext2_rename(const char *old_path, const char *new_path);
