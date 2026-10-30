#pragma once

#include "store.h"

enum {
    HPKG_FILE_NAME_MAX = 64,
    HPKG_INDEX_MAX = 1024 * 1024,
};

typedef struct HpkgIndexEntry {
    HpkgMeta meta;
    uint32_t size;
    char digest[SHA256_HEX_LENGTH + 1];
    char file[HPKG_FILE_NAME_MAX + 1];
} HpkgIndexEntry;

/* The parsed <repository>/index. */
typedef struct HpkgIndex {
    HpkgIndexEntry *entries;
    size_t count;
} HpkgIndex;

int hpkg_index_load(const char *repository, HpkgIndex *index);
void hpkg_index_free(HpkgIndex *index);
const HpkgIndexEntry *hpkg_index_find(const HpkgIndex *index, const char *name);

/* Opens the archive an index entry points to and checks that it is the file
 * the index describes: size, SHA-256, name and version. */
int hpkg_repo_open_package(const char *repository, const HpkgIndexEntry *entry,
                           HpkgArchive *archive);

/* Checks the file of an entry against the index without parsing it. Returns
 * 0 when intact, HPKG_NOT_FOUND when missing, HPKG_FAILED when it differs. */
int hpkg_repo_check_package(const char *repository, const HpkgIndexEntry *entry);

/* Orders the packages needed to install name: its missing dependencies
 * first, name last. Dependencies already in installed are skipped. The
 * returned array (free() it) points into index. */
int hpkg_repo_plan(const HpkgIndex *index, const HpkgPackageSet *installed,
                   const char *name, const HpkgIndexEntry ***order, size_t *count);
