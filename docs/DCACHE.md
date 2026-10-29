# Directory entry cache (dcache)

`kernel/fs/dcache.{c,h}` caches the result of looking up one name in one
directory. It sits on the ext2 read path: `resolve_range` in `ext2.c` asks the
cache before walking a directory's blocks and stores what the walk finds.

## Model

- Key: `(filesystem id, parent inode number, name)`; value: the child inode
  number, or a negative marker meaning "no such name".
- Fixed pool of `DCACHE_CAPACITY` (256) entries, hashed into 128 buckets, with
  least-recently-used recycling when the pool is full. Names longer than
  `DCACHE_NAME_MAX` (47) bypass the cache.
- Symlinks: an entry maps the link's own name to the link's inode. Targets are
  never cached; `vfs_resolve` reads the target from the inode on every use, so
  retargeting a link by unlink + symlink is visible immediately.
- Mounts: entries carry a filesystem id (`DCACHE_FS_EXT2`). `ext2_mount_auto`
  and `vfs_umount` of an ext2 mount drop every entry of that filesystem.
- ramfs and procfs are not cached. ramfs finds a node by its whole path in a
  flat table and procfs entries (pids) come and go without any VFS mutation.

## Invalidation

All directory mutations in ext2 go through three primitives, each of which
drops the affected `(parent, name)` entry before touching the directory block:

| Primitive            | Used by                                              |
|----------------------|------------------------------------------------------|
| `dir_add_entry`      | file create, mkdir, link, symlink, rename            |
| `dir_remove_entry`   | unlink, rmdir, rename                                |
| `dir_retarget_entry` | rename over an existing name                         |

A moved directory's `..` entry is dropped explicitly in `ext2_rename`.
`inode_destroy` drops every entry that has the freed inode as parent or child,
because the number may be handed out again. Dropping first means a failed
operation can never leave a stale entry behind. The mutating operations look
names up with the uncached `dir_lookup`, so their decisions never rest on
cached state.

## /proc/dcache

Same `Label:` format as the other procfs files: `Entries`, `Capacity`, `Hits`
(positive and negative), `NegativeHits`, `Misses`, `Insertions`, `Evictions`
(live entries recycled) and `Invalidations` (live entries dropped on request).

## Test

`/dcachetest` (`DCACHE PASS`) checks repeated deep-path lookups against the
hit counter, negative entries turning positive after create, link and symlink
target creation, unlink/rename/rmdir invalidation (including a directory move
and inode reuse), symlink retargeting, over-long names and a flood of distinct
names that forces eviction while answers stay correct.
