#pragma once

#include <stdint.h>

#include "core/credentials.h"

/*
 * Permission policy for the VFS. Every path must be absolute and free of
 * symlinks (see vfs_resolve). The filesystem drivers stay
 * credential-agnostic: callers run these checks before an operation and
 * report their -EACCES or -EPERM unchanged.
 *
 * Root (euid 0) skips all of this except that a regular file needs at least
 * one execute bit before anyone, root included, may run it. A path that does
 * not exist passes: the operation itself reports -ENOENT, so the error does
 * not depend on who asks once the directories on the way are searchable.
 */

enum {
    VFS_ACCESS_EXEC = 1u << 0,  /* run a file, search a directory */
    VFS_ACCESS_WRITE = 1u << 1,
    VFS_ACCESS_READ = 1u << 2,
};

/* Search permission on every directory leading to path, then the requested
   rights on path itself (none: only the directories). 0 or -EACCES. */
int vfs_access_check(const credentials_t *cred, const char *path, uint32_t want);

/* Search permission on the directories leading to path, then write and search
   on its parent: what creating, removing or renaming an entry requires.
   0 or -EACCES. */
int vfs_access_check_entry(const credentials_t *cred, const char *path);

/* sys_chmod: only the owner or root may change the permission bits.
   0, -EPERM, -EACCES, -EINVAL or the filesystem's error. */
int vfs_access_chmod(const credentials_t *cred, const char *path, uint32_t mode);

/* sys_chown: root only. An id of CREDENTIALS_ID_NONE keeps the current value. */
int vfs_access_chown(const credentials_t *cred, const char *path, uint32_t uid,
                     uint32_t gid);

/* A newly created object belongs to its creator. Filesystems create objects
   as root, so this only has work to do for other users. */
int vfs_access_claim(const credentials_t *cred, const char *path);
