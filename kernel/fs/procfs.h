#pragma once

#include <stdint.h>

#include "fs/vfs.h"

/* Synthetic, read-only filesystem exposing kernel and process state.
   Paths are relative to the mount point ("/", "/meminfo", "/<pid>/status").
   File contents are rendered on every read, so a reader that needs a
   consistent snapshot must fetch the whole file with a single read. */
int procfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx);
int procfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                        uint32_t out_sz, uint32_t *out_len);
int procfs_stat(const char *path, vfs_stat_t *out);
