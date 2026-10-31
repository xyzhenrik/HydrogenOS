#pragma once

#include "archive.h"

/* The package database below <root>/var/lib/hpkg/db and the installed files
 * below <root>. The default root is the empty string, the real file system. */
typedef struct HpkgStore {
    char root[HPKG_ROOT_MAX + 1];
} HpkgStore;

typedef struct HpkgFileRecord {
    char path[HPKG_PATH_MAX + 1];
    uint32_t mode;
    uint32_t size;
    char digest[SHA256_HEX_LENGTH + 1];
} HpkgFileRecord;

typedef struct HpkgDirectory {
    char path[HPKG_PATH_MAX + 1];
} HpkgDirectory;

/* An installed package: its manifest, every file it owns (ascending in
 * hpkg_path_compare order) and the directories its installation created. */
typedef struct HpkgPackage {
    HpkgMeta meta;
    HpkgFileRecord *files;
    size_t file_count;
    HpkgDirectory *directories;
    size_t directory_count;
} HpkgPackage;

/* All installed packages, ascending by name. */
typedef struct HpkgPackageSet {
    HpkgPackage *packages;
    size_t count;
} HpkgPackageSet;

int hpkg_store_open(HpkgStore *store, const char *root);
/* Writes root + path to out. */
int hpkg_store_path(const HpkgStore *store, const char *path, char *out, size_t capacity);

int hpkg_store_load_all(const HpkgStore *store, HpkgPackageSet *set);
void hpkg_package_set_free(HpkgPackageSet *set);
const HpkgPackage *hpkg_package_set_find(const HpkgPackageSet *set, const char *name);

/*
 * Installs the archive, or upgrades the package of the same name. The
 * operation is all or nothing: every file is first written under a temporary
 * name, then moved into place, and the database record is written last. Any
 * failure undoes what was done and leaves the previous state intact.
 * Refuses on missing dependencies and on files that exist already without
 * belonging to the package being replaced.
 */
int hpkg_store_install(const HpkgStore *store, const HpkgArchive *archive);

/* Removes the package's files and the directories its installation created.
 * Refuses while an installed package depends on it. */
int hpkg_store_remove(const HpkgStore *store, const char *name);

/* Compares the installed files with the record. Returns the number of
 * problems found (each reported on stdout), or HPKG_FAILED. */
int hpkg_store_verify(const HpkgStore *store, const HpkgPackage *package);
