#pragma once

#include <stdint.h>
#include "stat.h"
#include "dirent.h"

typedef void (*vfs_dir_cb)(const char *name, uint8_t type, void *ctx);

typedef struct vfs_stat {
    uint8_t type; // 1=file, 2=dir
    uint32_t size;
} vfs_stat_t;

int vfs_init(void);
int vfs_is_ready(void);
const char *vfs_root_name(void);
int vfs_mount(const char *path, const char *fs);
int vfs_umount(const char *path);
int vfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx);
int vfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                     uint32_t out_sz, uint32_t *out_len);
int vfs_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int vfs_stat(const char *path, vfs_stat_t *out);
int vfs_stat_full(const char *path, hydrogen_stat_t *out);
int vfs_readdir(const char *path, int *pos, hydrogen_dirent_t *out);

int vfs_open(const char *path);
int vfs_read(int fd, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int vfs_close(int fd);

/* Write operations */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_write_file(const char *path, const uint8_t *data, uint32_t size);
int vfs_write_file_at(const char *path, uint32_t offset, const uint8_t *data,
                      uint32_t size);
