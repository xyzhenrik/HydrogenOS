#include "vfs_access.h"

#include "errno.h"
#include "fs/vfs.h"

#define MODE_CLASS_BITS 07
#define MODE_OWNER_SHIFT 6
#define MODE_GROUP_SHIFT 3
#define MODE_ANY_EXECUTE 0111

static uint32_t class_bits(const credentials_t *cred, const vfs_stat_t *st) {
    if (cred->euid == st->uid)
        return (st->mode >> MODE_OWNER_SHIFT) & MODE_CLASS_BITS;
    if (cred->gid == st->gid)
        return (st->mode >> MODE_GROUP_SHIFT) & MODE_CLASS_BITS;
    return st->mode & MODE_CLASS_BITS;
}

static int mode_allows(const credentials_t *cred, const vfs_stat_t *st, uint32_t want) {
    if (credentials_is_root(cred)) {
        int runnable = st->type == 2 || (st->mode & MODE_ANY_EXECUTE) != 0;
        return (want & VFS_ACCESS_EXEC) && !runnable ? -EACCES : 0;
    }
    return (class_bits(cred, st) & want) == want ? 0 : -EACCES;
}

static uint32_t path_length(const char *path) {
    uint32_t n = 0;
    while (path[n]) n++;
    return n;
}

/* Checks search permission on every directory above the last component of
   path. A missing or non-directory prefix ends the walk successfully. */
static int check_search_path(const credentials_t *cred, const char *path) {
    char prefix[VFS_PATH_MAX];
    uint32_t len = path_length(path);
    if (len >= sizeof(prefix) || path[0] != '/') return -EINVAL;
    for (uint32_t i = 0; i < len; ++i) {
        if (path[i] != '/') continue;
        /* The directory holding the component that starts after this slash. */
        uint32_t end = i == 0 ? 1 : i;
        for (uint32_t k = 0; k < end; ++k) prefix[k] = path[k];
        prefix[end] = 0;
        vfs_stat_t st;
        if (vfs_stat(prefix, &st) != 0 || st.type != 2) return 0;
        int rc = mode_allows(cred, &st, VFS_ACCESS_EXEC);
        if (rc != 0) return rc;
    }
    return 0;
}

int vfs_access_check(const credentials_t *cred, const char *path, uint32_t want) {
    if (!credentials_is_root(cred)) {
        int rc = check_search_path(cred, path);
        if (rc != 0) return rc;
    } else if ((want & VFS_ACCESS_EXEC) == 0) {
        return 0;
    }
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return 0;
    return mode_allows(cred, &st, want);
}

int vfs_access_check_entry(const credentials_t *cred, const char *path) {
    if (credentials_is_root(cred)) return 0;
    int rc = check_search_path(cred, path);
    if (rc != 0) return rc;

    char parent[VFS_PATH_MAX];
    uint32_t len = path_length(path);
    if (len >= sizeof(parent)) return -EINVAL;
    for (uint32_t i = 0; i <= len; ++i) parent[i] = path[i];
    while (len > 1 && parent[len - 1] == '/') len--;
    while (len > 1 && parent[len - 1] != '/') len--;
    while (len > 1 && parent[len - 1] == '/') len--;
    parent[len] = 0;

    vfs_stat_t st;
    if (vfs_stat(parent, &st) != 0 || st.type != 2) return 0;
    return mode_allows(cred, &st, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
}

/* Shared by chmod and chown: the target must be reachable and exist. */
static int stat_reachable(const credentials_t *cred, const char *path, vfs_stat_t *st) {
    int rc = vfs_access_check(cred, path, 0);
    if (rc != 0) return rc;
    return vfs_stat(path, st) == 0 ? 0 : -ENOENT;
}

int vfs_access_chmod(const credentials_t *cred, const char *path, uint32_t mode) {
    if ((mode & ~(uint32_t)0777) != 0) return -EINVAL;
    vfs_stat_t st;
    int rc = stat_reachable(cred, path, &st);
    if (rc != 0) return rc;
    if (!credentials_is_root(cred) && cred->euid != st.uid) return -EPERM;
    return vfs_chmod(path, mode);
}

int vfs_access_chown(const credentials_t *cred, const char *path, uint32_t uid,
                     uint32_t gid) {
    vfs_stat_t st;
    int rc = stat_reachable(cred, path, &st);
    if (rc != 0) return rc;
    if (!credentials_is_root(cred)) return -EPERM;
    return vfs_chown(path, uid == CREDENTIALS_ID_NONE ? st.uid : uid,
                     gid == CREDENTIALS_ID_NONE ? st.gid : gid);
}

int vfs_access_claim(const credentials_t *cred, const char *path) {
    if (cred->euid == CREDENTIALS_ROOT_UID && cred->gid == 0) return 0;
    return vfs_chown(path, cred->euid, cred->gid);
}
