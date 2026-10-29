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
/* Does not follow a symlink in the final component. */
int ext2_stat(const char *path, vfs_stat_t *out);
/* Returns the NUL-terminated target's length or a negative errno value;
   -EINVAL means the path is not a symlink. */
int ext2_readlink(const char *path, char *buf, uint32_t cap);
/* Mutating operations return 0 or a negative errno value. */
int ext2_write_file(const char *path, const uint8_t *data, uint32_t size);
int ext2_mkdir(const char *path);
int ext2_rmdir(const char *path);
int ext2_unlink(const char *path);
int ext2_rename(const char *old_path, const char *new_path);
int ext2_link(const char *old_path, const char *new_path);
int ext2_symlink(const char *target, const char *path);
/* Replace the permission bits (0777 at most; setuid/setgid/sticky are cleared)
   or the owner of the inode that path names, without following a final
   symlink. */
int ext2_chmod(const char *path, uint32_t mode);
int ext2_chown(const char *path, uint32_t uid, uint32_t gid);
