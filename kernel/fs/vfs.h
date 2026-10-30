#pragma once

#include <stdint.h>
#include "stat.h"
#include "dirent.h"

typedef void (*vfs_dir_cb)(const char *name, uint8_t type, void *ctx);

#define VFS_SYMLINK_DEPTH_MAX 8
#define VFS_PATH_MAX 256

typedef struct vfs_stat {
    uint8_t type; // 1=file, 2=dir, 3=symlink
    uint32_t size;
    uint32_t nlink; // 0 if the filesystem does not track link counts
    uint32_t ino;
    uint32_t mode; // permission bits only, 0777 at most
    uint32_t uid;
    uint32_t gid;
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
/* vfs_stat and vfs_stat_full describe the final component itself: a symlink
   there is reported as a symlink. Callers that want it followed resolve the
   path first with vfs_resolve. */
int vfs_stat(const char *path, vfs_stat_t *out);
int vfs_stat_full(const char *path, hydrogen_stat_t *out);
int vfs_readdir(const char *path, int *pos, hydrogen_dirent_t *out);
/* Attribute changes return 0 or a negative errno value; -EOPNOTSUPP means the
   filesystem has fixed ownership and permissions (ramfs, procfs). The caller
   decides who may change what, see vfs_access.h. */
int vfs_chmod(const char *path, uint32_t mode);
int vfs_chown(const char *path, uint32_t uid, uint32_t gid);

int vfs_open(const char *path);
int vfs_read(int fd, uint8_t *out, uint32_t out_sz, uint32_t *out_len);
int vfs_close(int fd);

/* Write operations */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
/* Hard link and symlink creation return 0 or a negative errno value. */
int vfs_link(const char *old_path, const char *new_path);
int vfs_symlink(const char *target, const char *link_path);
/* Stores the NUL-terminated target in buf and returns its length, or returns
   a negative errno value (-EINVAL if path is not a symlink). */
int vfs_readlink(const char *path, char *buf, uint32_t cap);
/* Rewrites an absolute, normalised path so that none of its components is a
   symlink; the final component is only expanded if follow_final is set.
   Returns 0 or a negative errno value (-ELOOP after VFS_SYMLINK_DEPTH_MAX
   expansions). */
int vfs_resolve(const char *path, int follow_final, char *out, uint32_t out_sz);
int vfs_write_file(const char *path, const uint8_t *data, uint32_t size);
/* Writes into the file in place, extending it if needed; a gap between the old
   end of file and offset reads back as zero. Returns 0 or a negative errno
   value. A failed write may have stored part of the data. */
int vfs_write_file_at(const char *path, uint32_t offset, const uint8_t *data,
                      uint32_t size);
/* Shrinks or grows a regular file; the new range reads as zero. Returns 0 or
   a negative errno value (-EFBIG if size exceeds what the filesystem holds). */
int vfs_truncate(const char *path, uint32_t size);
/* Makes all completed writes durable. Returns 0 or a negative errno value. */
int vfs_sync(void);
/* Periodic write-back of dirty cache data; cheap when nothing is pending. Call
   only where no filesystem operation can be half done. */
void vfs_writeback_tick(void);
