#pragma once

#include "hpkg.h"

typedef struct HpkgEntry {
    char path[HPKG_PATH_MAX + 1];
    uint32_t mode;
    uint32_t size;
    const uint8_t *data;
    char digest[SHA256_HEX_LENGTH + 1];
} HpkgEntry;

/* A package archive that was read completely and passed every check:
 * magic, archive and per-file SHA-256, manifest, path safety and ordering. */
typedef struct HpkgArchive {
    uint8_t *buffer;
    size_t length;
    /* SHA-256 of the archive file as a whole, as listed in a repository index. */
    char file_digest[SHA256_HEX_LENGTH + 1];
    HpkgMeta meta;
    HpkgEntry *entries;
    size_t entry_count;
} HpkgArchive;

/* Reads and validates path. Nothing is left allocated on failure. */
int hpkg_archive_open(const char *path, HpkgArchive *archive);
void hpkg_archive_close(HpkgArchive *archive);
