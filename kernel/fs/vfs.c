#include "vfs.h"
#include "ext2.h"
#include "ramfs.h"
#include "errno.h"

typedef enum vfs_fs_type {
    VFS_FS_NONE = 0,
    VFS_FS_EXT2 = 1,
    VFS_FS_RAMFS = 2,
} vfs_fs_type_t;

typedef struct vfs_mount {
    char path[16];
    vfs_fs_type_t fs;
    const char *fs_name;
} vfs_mount_t;

#define VFS_MAX_MOUNTS 4
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int ready = 0;

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int is_root_path(const char *path) {
    if (!path) return 1;
    if (path[0] == 0) return 1;
    if (str_eq(path, "/")) return 1;
    return 0;
}

static int path_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a == '/') a++;
    while (*b == '/') b++;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int path_is_prefix(const char *path, const char *mount, const char **out_sub) {
    if (!path || !mount) return 0;
    if (is_root_path(mount)) {
        if (out_sub) *out_sub = path;
        return 1;
    }
    const char *p = path;
    const char *m = mount;
    if (*p != '/') return 0;
    while (*m && *p && *m == *p) { m++; p++; }
    if (*m != 0) return 0;
    if (*p == 0) {
        if (out_sub) *out_sub = "/";
        return 1;
    }
    if (*p != '/') return 0;
    if (out_sub) *out_sub = p;
    return 1;
}

static int path_copy_mount(char *dst, const char *src, uint32_t max) {
    if (!dst || !src || max == 0) return -1;
    uint32_t i = 0;
    if (*src != '/') {
        if (i + 1 >= max) return -1;
        dst[i++] = '/';
    }
    while (*src && i < max - 1) {
        dst[i++] = *src++;
    }
    dst[i] = 0;
    return 0;
}


static void mounts_clear(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        mounts[i].path[0] = 0;
        mounts[i].fs = VFS_FS_NONE;
        mounts[i].fs_name = 0;
    }
}

int vfs_init(void) {
    ready = 0;
    mounts_clear();
    if (ext2_mount_auto() == 0) {
        if (vfs_mount("/", "ext2") == 0) {
            ready = 1;
            return 0;
        }
    }
    if (ramfs_init() == 0) {
        if (vfs_mount("/", "ramfs") == 0) {
            ready = 1;
            return 0;
        }
    }
    return -1;
}

int vfs_is_ready(void) { return ready; }

const char *vfs_root_name(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].fs != VFS_FS_NONE && is_root_path(mounts[i].path)) {
            return mounts[i].fs_name;
        }
    }
    return 0;
}

int vfs_mount(const char *path, const char *fs) {
    if (!path || !fs) return -1;
    vfs_fs_type_t type = VFS_FS_NONE;
    const char *name = 0;
    if (str_eq(fs, "ext2")) { type = VFS_FS_EXT2; name = "ext2"; }
    else if (str_eq(fs, "ramfs")) { type = VFS_FS_RAMFS; name = "ramfs"; }
    else return -1;
    char norm[16];
    if (path_copy_mount(norm, path, sizeof(norm)) != 0) return -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].fs != VFS_FS_NONE && path_eq(mounts[i].path, norm)) {
            return -1; // already mounted
        }
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].fs == VFS_FS_NONE) {
            for (int k = 0; k < (int)sizeof(mounts[i].path); ++k) mounts[i].path[k] = norm[k];
            mounts[i].fs = type;
            mounts[i].fs_name = name;
            return 0;
        }
    }
    return -1;
}

int vfs_umount(const char *path) {
    if (!path) return -1;
    char norm[16];
    if (path_copy_mount(norm, path, sizeof(norm)) != 0) return -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].fs != VFS_FS_NONE && path_eq(mounts[i].path, norm)) {
            mounts[i].fs = VFS_FS_NONE;
            mounts[i].path[0] = 0;
            mounts[i].fs_name = 0;
            return 0;
        }
    }
    return -1;
}

static const vfs_mount_t *mount_for_path(const char *path, const char **subpath) {
    if (!path) return 0;
    const vfs_mount_t *best = 0;
    uint32_t best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].fs == VFS_FS_NONE) continue;
        const char *sub = 0;
        if (path_is_prefix(path, mounts[i].path, &sub)) {
            uint32_t mlen = str_len(mounts[i].path);
            if (mlen > best_len) {
                best_len = mlen;
                best = &mounts[i];
                if (subpath) *subpath = sub;
            }
        }
    }
    return best;
}

int vfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx) {
    if (!ready) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_EXT2) return ext2_list_dir(sub, cb, ctx);
    if (m->fs == VFS_FS_RAMFS) {
        if (is_root_path(sub)) return ramfs_list_root(cb, ctx);
        return ramfs_list_dir(sub, cb, ctx);
    }
    return -1;
}

int vfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                     uint32_t out_sz, uint32_t *out_len) {
    if (!ready) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_EXT2) return ext2_read_file_at(sub, offset, out, out_sz, out_len);
    if (m->fs == VFS_FS_RAMFS) return ramfs_read_file_at(sub, offset, out, out_sz, out_len);
    return -1;
}

int vfs_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    return vfs_read_file_at(path, 0, out, out_sz, out_len);
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    if (!ready || !out) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_EXT2) {
        if (ext2_stat(sub, &out->type, &out->size) != 0) return -1;
        return 0;
    }
    if (m->fs == VFS_FS_RAMFS) {
        return ramfs_stat(sub, out);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* vfs_stat_full - fill hydrogen_stat_t from VFS                       */
/* ------------------------------------------------------------------ */

int vfs_stat_full(const char *path, hydrogen_stat_t *out) {
    if (!ready || !out) return -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -1;
    if (st.type == 2) {
        out->st_mode  = S_IFDIR | 0755;
        out->st_nlink = 2;
    } else {
        out->st_mode  = S_IFREG | 0644;
        out->st_nlink = 1;
    }
    out->st_size  = st.size;
    out->st_ino   = 0;
    out->st_mtime = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* vfs_readdir - iterate directory entries                             */
/* ------------------------------------------------------------------ */

typedef struct readdir_ctx {
    int target;
    int current;
    int found;
    hydrogen_dirent_t *out;
} readdir_ctx_t;

static void readdir_cb(const char *name, uint8_t type, void *ctx) {
    readdir_ctx_t *r = (readdir_ctx_t *)ctx;
    if (r->found) return;
    if (r->current == r->target) {
        r->found = 1;
        r->out->d_ino  = 0;
        r->out->d_type = (type == 2) ? 4 : 8;
        uint32_t i = 0;
        while (name[i] && i < DIRENT_NAME_MAX) {
            r->out->d_name[i] = name[i];
            ++i;
        }
        r->out->d_name[i] = 0;
    }
    r->current++;
}

int vfs_readdir(const char *path, int *pos, hydrogen_dirent_t *out) {
    if (!ready || !pos || !out) return -1;
    if (*pos < 0) return -1;
    readdir_ctx_t ctx;
    ctx.target  = *pos;
    ctx.current = 0;
    ctx.found   = 0;
    ctx.out     = out;
    int rc = vfs_list_dir(path, readdir_cb, &ctx);
    if (!ctx.found) return rc != 0 ? -1 : 0;
    (*pos)++;
    return 1;
}

#define VFS_MAX_FD 8
typedef struct vfs_fd {
    int used;
    uint32_t offset;
    char path[128];
} vfs_fd_t;

static vfs_fd_t fds[VFS_MAX_FD];

static int path_copy(char *dst, const char *src, uint32_t max) {
    if (!dst || !src || max == 0) return -1;
    uint32_t i = 0;
    if (src[0] != '/') {
        if (i + 1 >= max) return -1;
        dst[i++] = '/';
    }
    for (; src[0] && i < max - 1; ++i, ++src) dst[i] = *src;
    dst[i] = 0;
    return 0;
}

int vfs_open(const char *path) {
    if (!ready || !path) return -1;
    char tmp[128];
    if (path_copy(tmp, path, sizeof(tmp)) != 0) return -1;
    vfs_stat_t st;
    if (vfs_stat(tmp, &st) != 0 || st.type != 1) return -1;
    for (int i = 0; i < VFS_MAX_FD; ++i) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].offset = 0;
            for (int k = 0; k < (int)sizeof(fds[i].path); ++k) fds[i].path[k] = tmp[k];
            return i;
        }
    }
    return -1;
}

int vfs_read(int fd, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    if (!ready || fd < 0 || fd >= VFS_MAX_FD || !fds[fd].used) return -1;
    int rc = vfs_read_file_at(fds[fd].path, fds[fd].offset, out, out_sz, out_len);
    if (rc == 0 && out_len) {
        fds[fd].offset += *out_len;
    }
    return rc;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    fds[fd].used = 0;
    fds[fd].offset = 0;
    fds[fd].path[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Write operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_mkdir(const char *path) {
    if (!ready || !path) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_RAMFS) return ramfs_mkdir(sub);
    if (m->fs == VFS_FS_EXT2) return ext2_mkdir(sub);
    return -1;
}

int vfs_rmdir(const char *path) {
    if (!ready || !path) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (is_root_path(sub)) return -EBUSY;
    if (m->fs == VFS_FS_RAMFS) return ramfs_rmdir(sub);
    if (m->fs == VFS_FS_EXT2) return ext2_rmdir(sub);
    return -1;
}

int vfs_unlink(const char *path) {
    if (!ready || !path) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_RAMFS) return ramfs_unlink(sub);
    if (m->fs == VFS_FS_EXT2) return ext2_unlink(sub);
    return -1;
}

int vfs_rename(const char *old_path, const char *new_path) {
    if (!ready || !old_path || !new_path) return -1;
    const char *old_sub = 0;
    const char *new_sub = 0;
    const vfs_mount_t *mo = mount_for_path(old_path, &old_sub);
    const vfs_mount_t *mn = mount_for_path(new_path, &new_sub);
    /* Both paths must be on the same filesystem */
    if (!mo || !mn || mo != mn) return -1;
    if (mo->fs == VFS_FS_RAMFS) return ramfs_rename(old_sub, new_sub);
    if (mo->fs == VFS_FS_EXT2) return ext2_rename(old_sub, new_sub);
    return -1;
}

int vfs_write_file(const char *path, const uint8_t *data, uint32_t size) {
    if (!ready || !path) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    if (m->fs == VFS_FS_RAMFS) return ramfs_write_file(sub, data, size);
    if (m->fs == VFS_FS_EXT2) return ext2_write_file(sub, data, size);
    return -1;
}
