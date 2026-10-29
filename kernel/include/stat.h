#pragma once

#include <stdint.h>

#define S_IFMT   0170000
#define S_IFREG  0100000  /* regular file */
#define S_IFDIR  0040000  /* directory */
#define S_IFLNK  0120000  /* symlink */

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

/* SYS_STAT flags: describe a symlink itself instead of following it. */
#define STAT_NOFOLLOW (1u << 0)

typedef struct hydrogen_stat {
    uint32_t st_mode;    /* file type + permissions */
    uint32_t st_size;    /* file size in bytes */
    uint32_t st_nlink;   /* number of hard links */
    uint32_t st_ino;     /* inode number */
    uint64_t st_mtime;   /* modification time (ms since boot) */
} hydrogen_stat_t;
