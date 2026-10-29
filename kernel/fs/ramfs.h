#pragma once

#include <stdint.h>

#include "fs/vfs.h"

int ramfs_init(void);
void ramfs_set_cpio(const void *data, uint32_t size);
int ramfs_is_ready(void);
int ramfs_list_root(vfs_dir_cb cb, void *ctx);
int ramfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx);
int ramfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                       uint32_t out_sz, uint32_t *out_len);
int ramfs_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int ramfs_stat(const char *path, vfs_stat_t *out);

/* Write operations */
int ramfs_mkdir(const char *path);
int ramfs_rmdir(const char *path);
int ramfs_create(const char *path, const uint8_t *data, uint32_t size);
int ramfs_unlink(const char *path);
int ramfs_rename(const char *old_path, const char *new_path);
int ramfs_write_file(const char *path, const uint8_t *data, uint32_t size);
