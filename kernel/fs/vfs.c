#include "vfs.h"
#include "ext2.h"
#include "ramfs.h"
#include "procfs.h"
#include "errno.h"
#include "kheap.h"

typedef enum vfs_fs_type {
    VFS_FS_NONE = 0,
    VFS_FS_EXT2 = 1,
    VFS_FS_RAMFS = 2,
    VFS_FS_PROCFS = 3,
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

static int mount_root(void) {
    if (ext2_mount_auto() == 0 && vfs_mount("/", "ext2") == 0)
        return 0;
    if (ramfs_init() == 0 && vfs_mount("/", "ramfs") == 0)
        return 0;
    return -1;
}

int vfs_init(void) {
    ready = 0;
    mounts_clear();
    if (mount_root() != 0)
        return -1;
    ready = 1;
    return vfs_mount("/proc", "procfs");
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
    else if (str_eq(fs, "procfs")) { type = VFS_FS_PROCFS; name = "procfs"; }
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
    if (m->fs == VFS_FS_PROCFS) return procfs_list_dir(sub, cb, ctx);
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
    if (m->fs == VFS_FS_PROCFS) return procfs_read_file_at(sub, offset, out, out_sz, out_len);
    return -1;
}

int vfs_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    return vfs_read_file_at(path, 0, out, out_sz, out_len);
}

/* ramfs and procfs store no attributes: everything belongs to root. The
   initrd is all executables and only root may modify ramfs, so its files are
   0755; procfs is read-only. */
static void set_fixed_attrs(vfs_stat_t *out, uint32_t dir_mode, uint32_t file_mode) {
    out->mode = out->type == 2 ? dir_mode : file_mode;
    out->uid = 0;
    out->gid = 0;
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    if (!ready || !out) return -1;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -1;
    out->nlink = 0;
    out->ino = 0;
    if (m->fs == VFS_FS_EXT2) {
        if (ext2_stat(sub, out) != 0) return -1;
        return 0;
    }
    if (m->fs == VFS_FS_RAMFS) {
        if (ramfs_stat(sub, out) != 0) return -1;
        set_fixed_attrs(out, 0755, 0755);
        return 0;
    }
    if (m->fs == VFS_FS_PROCFS) {
        if (procfs_stat(sub, out) != 0) return -1;
        set_fixed_attrs(out, 0555, 0444);
        return 0;
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
        out->st_mode  = S_IFDIR | st.mode;
        out->st_nlink = st.nlink ? st.nlink : 2;
    } else if (st.type == 3) {
        out->st_mode  = S_IFLNK | st.mode;
        out->st_nlink = st.nlink ? st.nlink : 1;
    } else {
        out->st_mode  = S_IFREG | st.mode;
        out->st_nlink = st.nlink ? st.nlink : 1;
    }
    out->st_size  = st.size;
    out->st_ino   = st.ino;
    out->st_uid   = st.uid;
    out->st_gid   = st.gid;
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
        r->out->d_type = (type == 2) ? 4 : (type == 7) ? 10 : 8;
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
/* Symlink resolution                                                  */
/* ------------------------------------------------------------------ */

/* Overlapping move within one buffer; the kernel has no memmove. */
static void buf_move(char *dst, const char *src, uint32_t n) {
    if (dst < src) {
        for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
    } else {
        for (uint32_t i = n; i > 0; --i) dst[i - 1] = src[i - 1];
    }
}

/* Drops the last component of an absolute path; the root stays the root. */
static void resolved_pop(char *path, uint32_t *len) {
    while (*len > 1 && path[*len - 1] != '/') (*len)--;
    if (*len > 1) (*len)--;
    path[*len] = 0;
}

static int resolved_push(char *path, uint32_t *len, uint32_t cap,
                         const char *name, uint32_t name_len) {
    uint32_t need = *len + name_len + (*len > 1 ? 1 : 0);
    if (need >= cap) return -ENAMETOOLONG;
    if (*len > 1) path[(*len)++] = '/';
    for (uint32_t i = 0; i < name_len; ++i) path[(*len)++] = name[i];
    path[*len] = 0;
    return 0;
}

/* Returns the length of the target if path is a symlink, 0 if it is not one
   (including components that do not exist yet; the operation itself reports
   those) and a negative errno value on failure. */
static int symlink_target(const char *path, char *target, uint32_t cap) {
    int n = vfs_readlink(path, target, cap);
    if (n == -ENOENT || n == -EINVAL || n == -ENOTDIR) return 0;
    return n;
}

int vfs_resolve(const char *path, int follow_final, char *out, uint32_t out_sz) {
    if (!ready || !path || !out || out_sz < 2 || path[0] != '/') return -EINVAL;
    char work[VFS_PATH_MAX];
    char target[VFS_PATH_MAX];
    uint32_t work_len = str_len(path);
    if (work_len >= sizeof(work)) return -ENAMETOOLONG;
    for (uint32_t i = 0; i <= work_len; ++i) work[i] = path[i];

    uint32_t out_len = 1;
    out[0] = '/';
    out[1] = 0;
    uint32_t pos = 0;
    int expansions = 0;
    for (;;) {
        while (work[pos] == '/') pos++;
        if (!work[pos]) return 0;
        uint32_t start = pos;
        while (work[pos] && work[pos] != '/') pos++;
        uint32_t seg_len = pos - start;
        if (seg_len == 1 && work[start] == '.') continue;
        if (seg_len == 2 && work[start] == '.' && work[start + 1] == '.') {
            resolved_pop(out, &out_len);
            continue;
        }

        uint32_t parent_len = out_len;
        int rc = resolved_push(out, &out_len, out_sz, work + start, seg_len);
        if (rc != 0) return rc;
        uint32_t rest = pos;
        while (work[rest] == '/') rest++;
        if (!work[rest] && !follow_final) return 0;

        int target_len = symlink_target(out, target, sizeof(target));
        if (target_len < 0) return target_len;
        if (target_len == 0) continue;
        if (++expansions > VFS_SYMLINK_DEPTH_MAX) return -ELOOP;

        /* Continue with target + "/" + remainder; a relative target starts
           from the directory that held the link. */
        out_len = parent_len;
        out[out_len] = 0;
        if (target[0] == '/') {
            out_len = 1;
            out[1] = 0;
        }
        uint32_t rest_len = str_len(work + rest);
        uint32_t joined = (uint32_t)target_len + (rest_len ? 1 + rest_len : 0);
        if (joined >= sizeof(work)) return -ENAMETOOLONG;
        if (rest_len) {
            buf_move(work + target_len + 1, work + rest, rest_len + 1);
            work[target_len] = '/';
        } else {
            work[target_len] = 0;
        }
        for (int i = 0; i < target_len; ++i) work[i] = target[i];
        pos = 0;
    }
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

int vfs_link(const char *old_path, const char *new_path) {
    if (!ready || !old_path || !new_path) return -EINVAL;
    const char *old_sub = 0;
    const char *new_sub = 0;
    const vfs_mount_t *mo = mount_for_path(old_path, &old_sub);
    const vfs_mount_t *mn = mount_for_path(new_path, &new_sub);
    if (!mo || !mn) return -ENOENT;
    if (mo != mn) return -EXDEV;
    if (mo->fs == VFS_FS_EXT2) return ext2_link(old_sub, new_sub);
    return -EOPNOTSUPP;
}

int vfs_symlink(const char *target, const char *link_path) {
    if (!ready || !target || !link_path) return -EINVAL;
    /* Longer targets could never be followed. */
    if (str_len(target) >= VFS_PATH_MAX) return -ENAMETOOLONG;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(link_path, &sub);
    if (!m) return -ENOENT;
    if (m->fs == VFS_FS_EXT2) return ext2_symlink(target, sub);
    return -EOPNOTSUPP;
}

int vfs_readlink(const char *path, char *buf, uint32_t cap) {
    if (!ready || !path || !buf || cap == 0) return -EINVAL;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -ENOENT;
    if (m->fs == VFS_FS_EXT2) return ext2_readlink(sub, buf, cap);
    return -EINVAL;
}

int vfs_chmod(const char *path, uint32_t mode) {
    if (!ready || !path) return -EINVAL;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -ENOENT;
    if (m->fs == VFS_FS_EXT2) return ext2_chmod(sub, mode);
    return -EOPNOTSUPP;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!ready || !path) return -EINVAL;
    const char *sub = 0;
    const vfs_mount_t *m = mount_for_path(path, &sub);
    if (!m) return -ENOENT;
    if (m->fs == VFS_FS_EXT2) return ext2_chown(sub, uid, gid);
    return -EOPNOTSUPP;
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

/* The backends only replace whole files, so a positioned write is a
 * read-modify-write of the complete file. Bytes between the old end of file
 * and offset read back as zero. */
int vfs_write_file_at(const char *path, uint32_t offset, const uint8_t *data,
                      uint32_t size) {
    if (!ready || !path || (size > 0 && !data)) return -1;
    if (size > UINT32_MAX - offset) return -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0 || st.type != 1) return -1;
    if (size == 0) return 0;
    uint32_t end = offset + size;
    uint32_t new_size = end > st.size ? end : st.size;
    uint8_t *buf = (uint8_t *)kalloc(new_size);
    if (!buf) return -1;
    uint32_t have = 0;
    while (have < st.size) {
        uint32_t got = 0;
        if (vfs_read_file_at(path, have, buf + have, st.size - have, &got) != 0 ||
            got == 0) {
            kfree(buf);
            return -1;
        }
        have += got;
    }
    for (uint32_t i = st.size; i < offset; ++i) buf[i] = 0;
    for (uint32_t i = 0; i < size; ++i) buf[offset + i] = data[i];
    int rc = vfs_write_file(path, buf, new_size);
    kfree(buf);
    return rc;
}
