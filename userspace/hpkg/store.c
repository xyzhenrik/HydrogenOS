#include "store.h"

#include "errno.h"
#include "mini_libc.h"
#include "syscall.h"

#define DATABASE_DIRECTORY "/var/lib/hpkg/db"
#define STAGING_SUFFIX ".hpkg-new"
#define BACKUP_SUFFIX ".hpkg-old"
#define RECORD_LIMIT (4 * 1024 * 1024)
#define STORE_PATH_CAP (HPKG_ROOT_MAX + HPKG_PATH_MAX + 16)

int hpkg_store_open(HpkgStore *store, const char *root) {
    size_t length = strlen(root);

    if (length > HPKG_ROOT_MAX || (length > 0 && (root[0] != '/' || root[length - 1] == '/'))) {
        hpkg_report("invalid root '%s'", root);
        return HPKG_FAILED;
    }
    memcpy(store->root, root, length + 1);
    return 0;
}

int hpkg_store_path(const HpkgStore *store, const char *path, char *out, size_t capacity) {
    int length = snprintf(out, capacity, "%s%s", store->root, path);

    if (length < 0 || (size_t)length >= capacity) {
        hpkg_report("%s: path too long", path);
        return HPKG_FAILED;
    }
    return 0;
}

/* root + path + suffix, for the staging and backup names next to a file. */
static int suffixed_path(const HpkgStore *store, const char *path, const char *suffix,
                         char out[STORE_PATH_CAP]) {
    int length = snprintf(out, STORE_PATH_CAP, "%s%s%s", store->root, path, suffix);

    if (length < 0 || length >= STORE_PATH_CAP) {
        hpkg_report("%s: path too long", path);
        return HPKG_FAILED;
    }
    return 0;
}

static int record_path(const HpkgStore *store, const char *name, const char *suffix,
                       char out[STORE_PATH_CAP]) {
    char relative[HPKG_PATH_MAX + 1];

    snprintf(relative, sizeof(relative), "%s/%s", DATABASE_DIRECTORY, name);
    return suffixed_path(store, relative, suffix, out);
}

/* ---- directories -------------------------------------------------------- */

typedef struct DirectoryList {
    HpkgDirectory *items;
    size_t count;
    size_t capacity;
} DirectoryList;

static int directory_list_add(DirectoryList *list, const char *path) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 8;
        HpkgDirectory *grown = realloc(list->items, capacity * sizeof(HpkgDirectory));

        if (!grown) {
            hpkg_report("out of memory");
            return HPKG_FAILED;
        }
        list->items = grown;
        list->capacity = capacity;
    }
    strcpy(list->items[list->count++].path, path);
    return 0;
}

/* Creates every missing directory above path, outermost first, and records
 * the ones it created in created (when not NULL). */
static int make_parents(const HpkgStore *store, const char *path, DirectoryList *created) {
    char prefix[HPKG_PATH_MAX + 1];

    for (size_t i = 1; path[i]; ++i) {
        char full[STORE_PATH_CAP];
        long status;

        if (path[i] != '/')
            continue;
        memcpy(prefix, path, i);
        prefix[i] = 0;
        if (suffixed_path(store, prefix, "", full) != 0)
            return HPKG_FAILED;
        status = sys_mkdir(full);
        if (status == 0) {
            if (created && directory_list_add(created, prefix) != 0)
                return HPKG_FAILED;
        } else if (status == -EEXIST) {
            stat_t info;

            if (stat(full, &info) != 0 || !S_ISDIR(info.st_mode)) {
                hpkg_report("%s: exists and is not a directory", prefix);
                return HPKG_FAILED;
            }
        } else {
            hpkg_report("%s: cannot create directory (error %d)", prefix, (int)-status);
            return HPKG_FAILED;
        }
    }
    return 0;
}

/* ---- records ------------------------------------------------------------ */

static void package_release(HpkgPackage *package) {
    free(package->files);
    free(package->directories);
    memset(package, 0, sizeof(*package));
}

void hpkg_package_set_free(HpkgPackageSet *set) {
    for (size_t i = 0; i < set->count; ++i)
        package_release(&set->packages[i]);
    free(set->packages);
    set->packages = 0;
    set->count = 0;
}

const HpkgPackage *hpkg_package_set_find(const HpkgPackageSet *set, const char *name) {
    for (size_t i = 0; i < set->count; ++i) {
        if (strcmp(set->packages[i].meta.name, name) == 0)
            return &set->packages[i];
    }
    return 0;
}

static int compare_path_to_file(const void *key, const void *element) {
    return hpkg_path_compare(key, ((const HpkgFileRecord *)element)->path);
}

static const HpkgFileRecord *find_file(const HpkgPackage *package, const char *path) {
    return bsearch(path, package->files, package->file_count, sizeof(HpkgFileRecord),
                   compare_path_to_file);
}

typedef struct RecordParser {
    HpkgPackage *package;
    size_t file_capacity;
    size_t directory_capacity;
} RecordParser;

static int parse_file_line(RecordParser *parser, char *fields) {
    HpkgPackage *package = parser->package;
    char *save = 0;
    char *digest = strtok_r(fields, " ", &save);
    char *size = strtok_r(0, " ", &save);
    char *mode = strtok_r(0, " ", &save);
    char *path = strtok_r(0, " ", &save);
    HpkgFileRecord *record;
    char *end;

    if (!path || strtok_r(0, " ", &save) || strlen(digest) != SHA256_HEX_LENGTH ||
        !hpkg_valid_path(path))
        return -1;
    if (package->file_count == parser->file_capacity) {
        size_t capacity = parser->file_capacity ? parser->file_capacity * 2 : 16;
        HpkgFileRecord *grown = realloc(package->files, capacity * sizeof(HpkgFileRecord));

        if (!grown)
            return -1;
        package->files = grown;
        parser->file_capacity = capacity;
    }
    record = &package->files[package->file_count];
    strcpy(record->digest, digest);
    strcpy(record->path, path);
    record->size = (uint32_t)strtoul(size, &end, 10);
    if (*size == 0 || *end)
        return -1;
    record->mode = (uint32_t)strtoul(mode, &end, 8);
    if (*mode == 0 || *end || record->mode & ~(uint32_t)S_IRWXUGO)
        return -1;
    if (package->file_count > 0 &&
        hpkg_path_compare(package->files[package->file_count - 1].path, path) >= 0)
        return -1;
    package->file_count++;
    return 0;
}

static int parse_directory_line(RecordParser *parser, const char *path) {
    HpkgPackage *package = parser->package;

    if (!hpkg_valid_path(path))
        return -1;
    if (package->directory_count == parser->directory_capacity) {
        size_t capacity = parser->directory_capacity ? parser->directory_capacity * 2 : 8;
        HpkgDirectory *grown =
            realloc(package->directories, capacity * sizeof(HpkgDirectory));

        if (!grown)
            return -1;
        package->directories = grown;
        parser->directory_capacity = capacity;
    }
    strcpy(package->directories[package->directory_count++].path, path);
    return 0;
}

static int parse_record_line(char *line, void *context) {
    RecordParser *parser = context;
    char *space = strchr(line, ' ');

    if (!space)
        return -1;
    *space = 0;
    if (strcmp(line, "file") == 0)
        return parse_file_line(parser, space + 1);
    if (strcmp(line, "dir") == 0)
        return parse_directory_line(parser, space + 1);
    return hpkg_meta_set(&parser->package->meta, line, space + 1);
}

static int load_package(const HpkgStore *store, const char *name, HpkgPackage *package) {
    char path[STORE_PATH_CAP];
    uint8_t *text;
    size_t length;
    RecordParser parser = {package, 0, 0};
    int status;

    memset(package, 0, sizeof(*package));
    if (record_path(store, name, "", path) != 0)
        return HPKG_FAILED;
    status = hpkg_read_file(path, RECORD_LIMIT, &text, &length);
    if (status != 0)
        return status;
    if (hpkg_each_line((char *)text, parse_record_line, &parser) != 0 ||
        strcmp(package->meta.name, name) != 0 || !package->meta.version[0]) {
        hpkg_report("%s: corrupt package record", path);
        free(text);
        package_release(package);
        return HPKG_FAILED;
    }
    free(text);
    return 0;
}

static int compare_packages(const void *first, const void *second) {
    return strcmp(((const HpkgPackage *)first)->meta.name,
                  ((const HpkgPackage *)second)->meta.name);
}

int hpkg_store_load_all(const HpkgStore *store, HpkgPackageSet *set) {
    char directory[STORE_PATH_CAP];
    size_t capacity = 0;

    set->packages = 0;
    set->count = 0;
    if (suffixed_path(store, DATABASE_DIRECTORY, "", directory) != 0)
        return HPKG_FAILED;
    for (int position = 0;; ++position) {
        dirent_t entry;
        long status = readdir_pos(directory, position, &entry);

        if (status == -ENOENT && position == 0)
            return 0;
        if (status < 0) {
            hpkg_report("%s: cannot read package database (error %d)", directory,
                        (int)-status);
            hpkg_package_set_free(set);
            return HPKG_FAILED;
        }
        if (status == 0)
            break;
        if (!hpkg_valid_name(entry.d_name))
            continue;
        if (set->count == capacity) {
            size_t grown_capacity = capacity ? capacity * 2 : 8;
            HpkgPackage *grown = realloc(set->packages, grown_capacity * sizeof(HpkgPackage));

            if (!grown) {
                hpkg_report("out of memory");
                hpkg_package_set_free(set);
                return HPKG_FAILED;
            }
            set->packages = grown;
            capacity = grown_capacity;
        }
        if (load_package(store, entry.d_name, &set->packages[set->count]) != 0) {
            hpkg_package_set_free(set);
            return HPKG_FAILED;
        }
        set->count++;
    }
    qsort(set->packages, set->count, sizeof(HpkgPackage), compare_packages);
    return 0;
}

static int serialize_record(const HpkgMeta *meta, const HpkgFileRecord *files,
                            size_t file_count, const HpkgDirectory *directories,
                            size_t directory_count, HpkgText *text) {
    if (hpkg_text_append(text, "name %s\nversion %s\n", meta->name, meta->version) != 0)
        return HPKG_FAILED;
    if (meta->description[0] &&
        hpkg_text_append(text, "description %s\n", meta->description) != 0)
        return HPKG_FAILED;
    if (meta->depend_count > 0) {
        if (hpkg_text_append(text, "depends") != 0)
            return HPKG_FAILED;
        for (size_t i = 0; i < meta->depend_count; ++i) {
            if (hpkg_text_append(text, " %s", meta->depends[i]) != 0)
                return HPKG_FAILED;
        }
        if (hpkg_text_append(text, "\n") != 0)
            return HPKG_FAILED;
    }
    for (size_t i = 0; i < file_count; ++i) {
        if (hpkg_text_append(text, "file %s %u %o %s\n", files[i].digest,
                             (unsigned)files[i].size, (unsigned)files[i].mode,
                             files[i].path) != 0)
            return HPKG_FAILED;
    }
    for (size_t i = 0; i < directory_count; ++i) {
        if (hpkg_text_append(text, "dir %s\n", directories[i].path) != 0)
            return HPKG_FAILED;
    }
    return 0;
}

/* Writes the record under a temporary name, then renames it into place. */
static int store_record(const HpkgStore *store, const HpkgMeta *meta,
                        const HpkgFileRecord *files, size_t file_count,
                        const HpkgDirectory *directories, size_t directory_count) {
    HpkgText text = {0, 0, 0};
    char temporary[STORE_PATH_CAP];
    char final_path[STORE_PATH_CAP];
    int status = HPKG_FAILED;

    if (record_path(store, meta->name, STAGING_SUFFIX, temporary) != 0 ||
        record_path(store, meta->name, "", final_path) != 0)
        return HPKG_FAILED;
    if (serialize_record(meta, files, file_count, directories, directory_count, &text) != 0)
        goto done;
    if (make_parents(store, DATABASE_DIRECTORY "/x", 0) != 0)
        goto done;
    if (hpkg_write_file(temporary, text.data, text.length, 0644) != 0)
        goto done;
    if (sys_rename(temporary, final_path) != 0) {
        sys_unlink(temporary);
        hpkg_report("%s: cannot store package record", final_path);
        goto done;
    }
    status = 0;
done:
    hpkg_text_free(&text);
    return status;
}

/* ---- install ------------------------------------------------------------ */

enum {
    STATE_STAGED = 1 << 0,
    STATE_ASIDE = 1 << 1,
    STATE_COMMITTED = 1 << 2,
    STATE_STALE = 1 << 3,
};

typedef struct Transaction {
    const HpkgStore *store;
    const HpkgArchive *archive;
    const HpkgPackage *previous;
    uint8_t *entry_state;
    uint8_t *stale_state;
    DirectoryList created;
} Transaction;

static int compare_path_to_entry(const void *key, const void *element) {
    return hpkg_path_compare(key, ((const HpkgEntry *)element)->path);
}

static const HpkgPackage *find_owner(const HpkgPackageSet *set, const HpkgPackage *ignored,
                                     const char *path) {
    for (size_t i = 0; i < set->count; ++i) {
        if (&set->packages[i] != ignored && find_file(&set->packages[i], path))
            return &set->packages[i];
    }
    return 0;
}

static int check_dependencies(const HpkgPackageSet *set, const HpkgMeta *meta) {
    int missing = 0;

    for (size_t i = 0; i < meta->depend_count; ++i) {
        if (strcmp(meta->depends[i], meta->name) == 0 ||
            !hpkg_package_set_find(set, meta->depends[i])) {
            hpkg_report("%s needs %s, which is not installed", meta->name, meta->depends[i]);
            missing = 1;
        }
    }
    return missing ? HPKG_FAILED : 0;
}

static int check_conflicts(const HpkgStore *store, const HpkgPackageSet *set,
                           const HpkgArchive *archive, const HpkgPackage *previous) {
    int conflicts = 0;

    for (size_t i = 0; i < archive->entry_count; ++i) {
        const char *path = archive->entries[i].path;
        const HpkgPackage *owner = find_owner(set, previous, path);
        char full[STORE_PATH_CAP];
        stat_t info;

        if (owner) {
            hpkg_report("%s is owned by %s", path, owner->meta.name);
            conflicts = 1;
        } else if (!(previous && find_file(previous, path)) &&
                   suffixed_path(store, path, "", full) == 0 && lstat(full, &info) == 0) {
            hpkg_report("%s exists and belongs to no package", path);
            conflicts = 1;
        }
    }
    return conflicts ? HPKG_FAILED : 0;
}

static void warn_leftover(const char *path) {
    hpkg_report("warning: could not remove %s", path);
}

/* Undoes a partly applied installation in reverse order. Returns non-zero
 * when something could not be restored. */
static int roll_back(Transaction *transaction) {
    const HpkgStore *store = transaction->store;
    const HpkgArchive *archive = transaction->archive;
    const HpkgPackage *previous = transaction->previous;
    int incomplete = 0;

    for (size_t i = previous ? previous->file_count : 0; i-- > 0;) {
        char full[STORE_PATH_CAP];
        char backup[STORE_PATH_CAP];

        if (!(transaction->stale_state[i] & STATE_ASIDE))
            continue;
        if (suffixed_path(store, previous->files[i].path, "", full) != 0 ||
            suffixed_path(store, previous->files[i].path, BACKUP_SUFFIX, backup) != 0 ||
            sys_rename(backup, full) != 0) {
            hpkg_report("rollback: cannot restore %s", previous->files[i].path);
            incomplete = 1;
        }
    }
    for (size_t i = archive->entry_count; i-- > 0;) {
        const char *path = archive->entries[i].path;
        uint8_t state = transaction->entry_state[i];
        char full[STORE_PATH_CAP];
        char staging[STORE_PATH_CAP];
        char backup[STORE_PATH_CAP];

        if (suffixed_path(store, path, "", full) != 0 ||
            suffixed_path(store, path, STAGING_SUFFIX, staging) != 0 ||
            suffixed_path(store, path, BACKUP_SUFFIX, backup) != 0) {
            incomplete = 1;
            continue;
        }
        if ((state & STATE_COMMITTED) && sys_unlink(full) != 0) {
            hpkg_report("rollback: cannot remove %s", path);
            incomplete = 1;
        }
        if ((state & STATE_ASIDE) && sys_rename(backup, full) != 0) {
            hpkg_report("rollback: cannot restore %s", path);
            incomplete = 1;
        }
        if ((state & STATE_STAGED) && !(state & STATE_COMMITTED) && sys_unlink(staging) != 0) {
            hpkg_report("rollback: cannot remove %s", staging);
            incomplete = 1;
        }
    }
    for (size_t i = transaction->created.count; i-- > 0;) {
        char full[STORE_PATH_CAP];

        if (suffixed_path(store, transaction->created.items[i].path, "", full) != 0 ||
            sys_rmdir(full) != 0)
            incomplete = 1;
    }
    if (incomplete)
        hpkg_report("rollback incomplete: leftovers named *" STAGING_SUFFIX
                    " or *" BACKUP_SUFFIX " may remain");
    return incomplete;
}

static int stage_files(Transaction *transaction) {
    const HpkgArchive *archive = transaction->archive;
    const HpkgStore *store = transaction->store;

    for (size_t i = 0; i < archive->entry_count; ++i) {
        const HpkgEntry *entry = &archive->entries[i];
        char staging[STORE_PATH_CAP];

        if (make_parents(store, entry->path, &transaction->created) != 0 ||
            suffixed_path(store, entry->path, STAGING_SUFFIX, staging) != 0)
            return HPKG_FAILED;
        if (hpkg_write_file(staging, entry->data, entry->size, entry->mode) != 0)
            return HPKG_FAILED;
        transaction->entry_state[i] |= STATE_STAGED;
    }
    return 0;
}

static int move_aside(const HpkgStore *store, const char *path, uint8_t *state) {
    char full[STORE_PATH_CAP];
    char backup[STORE_PATH_CAP];
    long status;

    if (suffixed_path(store, path, "", full) != 0 ||
        suffixed_path(store, path, BACKUP_SUFFIX, backup) != 0)
        return HPKG_FAILED;
    status = sys_rename(full, backup);
    if (status == 0)
        *state |= STATE_ASIDE;
    else if (status != -ENOENT) {
        hpkg_report("%s: cannot replace (error %d)", path, (int)-status);
        return HPKG_FAILED;
    }
    return 0;
}

static int commit_files(Transaction *transaction) {
    const HpkgArchive *archive = transaction->archive;
    const HpkgPackage *previous = transaction->previous;
    const HpkgStore *store = transaction->store;

    for (size_t i = 0; i < archive->entry_count; ++i) {
        const char *path = archive->entries[i].path;
        char full[STORE_PATH_CAP];
        char staging[STORE_PATH_CAP];
        long status;

        if (previous && find_file(previous, path) &&
            move_aside(store, path, &transaction->entry_state[i]) != 0)
            return HPKG_FAILED;
        if (suffixed_path(store, path, "", full) != 0 ||
            suffixed_path(store, path, STAGING_SUFFIX, staging) != 0)
            return HPKG_FAILED;
        status = sys_rename(staging, full);
        if (status != 0) {
            hpkg_report("%s: cannot move into place (error %d)", path, (int)-status);
            return HPKG_FAILED;
        }
        transaction->entry_state[i] |= STATE_COMMITTED;
    }
    for (size_t i = 0; previous && i < previous->file_count; ++i) {
        if ((transaction->stale_state[i] & STATE_STALE) &&
            move_aside(store, previous->files[i].path, &transaction->stale_state[i]) != 0)
            return HPKG_FAILED;
    }
    return 0;
}

static int write_new_record(Transaction *transaction) {
    const HpkgArchive *archive = transaction->archive;
    const HpkgPackage *previous = transaction->previous;
    HpkgFileRecord *files = calloc(archive->entry_count ? archive->entry_count : 1,
                                   sizeof(HpkgFileRecord));
    DirectoryList directories = {0, 0, 0};
    int status = HPKG_FAILED;

    if (!files) {
        hpkg_report("out of memory");
        return HPKG_FAILED;
    }
    for (size_t i = 0; i < archive->entry_count; ++i) {
        strcpy(files[i].path, archive->entries[i].path);
        strcpy(files[i].digest, archive->entries[i].digest);
        files[i].size = archive->entries[i].size;
        files[i].mode = archive->entries[i].mode;
    }
    for (size_t i = 0; previous && i < previous->directory_count; ++i) {
        if (directory_list_add(&directories, previous->directories[i].path) != 0)
            goto done;
    }
    for (size_t i = 0; i < transaction->created.count; ++i) {
        if (directory_list_add(&directories, transaction->created.items[i].path) != 0)
            goto done;
    }
    status = store_record(transaction->store, &archive->meta, files, archive->entry_count,
                          directories.items, directories.count);
done:
    free(files);
    free(directories.items);
    return status;
}

static void discard_backups(const Transaction *transaction) {
    const HpkgArchive *archive = transaction->archive;
    const HpkgPackage *previous = transaction->previous;
    char backup[STORE_PATH_CAP];

    for (size_t i = 0; i < archive->entry_count; ++i) {
        if ((transaction->entry_state[i] & STATE_ASIDE) &&
            (suffixed_path(transaction->store, archive->entries[i].path, BACKUP_SUFFIX,
                           backup) != 0 ||
             sys_unlink(backup) != 0))
            warn_leftover(archive->entries[i].path);
    }
    for (size_t i = 0; previous && i < previous->file_count; ++i) {
        if ((transaction->stale_state[i] & STATE_ASIDE) &&
            (suffixed_path(transaction->store, previous->files[i].path, BACKUP_SUFFIX,
                           backup) != 0 ||
             sys_unlink(backup) != 0))
            warn_leftover(previous->files[i].path);
    }
}

int hpkg_store_install(const HpkgStore *store, const HpkgArchive *archive) {
    HpkgPackageSet set;
    Transaction transaction = {store, archive, 0, 0, 0, {0, 0, 0}};
    int status = HPKG_FAILED;

    if (hpkg_store_load_all(store, &set) != 0)
        return HPKG_FAILED;
    transaction.previous = hpkg_package_set_find(&set, archive->meta.name);
    if (check_dependencies(&set, &archive->meta) != 0 ||
        check_conflicts(store, &set, archive, transaction.previous) != 0)
        goto done;

    transaction.entry_state = calloc(archive->entry_count ? archive->entry_count : 1, 1);
    transaction.stale_state =
        calloc(transaction.previous && transaction.previous->file_count
                   ? transaction.previous->file_count : 1, 1);
    if (!transaction.entry_state || !transaction.stale_state) {
        hpkg_report("out of memory");
        goto done;
    }
    for (size_t i = 0; transaction.previous && i < transaction.previous->file_count; ++i) {
        if (!bsearch(transaction.previous->files[i].path, archive->entries,
                     archive->entry_count, sizeof(HpkgEntry), compare_path_to_entry))
            transaction.stale_state[i] |= STATE_STALE;
    }

    if (stage_files(&transaction) != 0 || commit_files(&transaction) != 0 ||
        write_new_record(&transaction) != 0) {
        roll_back(&transaction);
        goto done;
    }
    discard_backups(&transaction);
    status = 0;
done:
    free(transaction.entry_state);
    free(transaction.stale_state);
    free(transaction.created.items);
    hpkg_package_set_free(&set);
    return status;
}

/* ---- remove and verify -------------------------------------------------- */

static int compare_directories_descending(const void *first, const void *second) {
    return hpkg_path_compare(((const HpkgDirectory *)second)->path,
                             ((const HpkgDirectory *)first)->path);
}

int hpkg_store_remove(const HpkgStore *store, const char *name) {
    HpkgPackageSet set;
    HpkgPackage *package;
    char path[STORE_PATH_CAP];
    int status = HPKG_FAILED;

    if (hpkg_store_load_all(store, &set) != 0)
        return HPKG_FAILED;
    package = (HpkgPackage *)hpkg_package_set_find(&set, name);
    if (!package) {
        hpkg_package_set_free(&set);
        return HPKG_NOT_FOUND;
    }
    for (size_t i = 0; i < set.count; ++i) {
        if (&set.packages[i] != package && hpkg_meta_depends_on(&set.packages[i].meta, name)) {
            hpkg_report("cannot remove %s: %s depends on it", name, set.packages[i].meta.name);
            goto done;
        }
    }
    for (size_t i = 0; i < package->file_count; ++i) {
        long result;

        if (suffixed_path(store, package->files[i].path, "", path) != 0)
            goto done;
        result = sys_unlink(path);
        if (result != 0 && result != -ENOENT) {
            hpkg_report("%s: cannot remove (error %d); package kept in database",
                        package->files[i].path, (int)-result);
            goto done;
        }
    }
    /* Directories left non-empty belong to someone else as well; they stay. */
    qsort(package->directories, package->directory_count, sizeof(HpkgDirectory),
          compare_directories_descending);
    for (size_t i = 0; i < package->directory_count; ++i) {
        long result;

        if (suffixed_path(store, package->directories[i].path, "", path) != 0)
            goto done;
        result = sys_rmdir(path);
        if (result != 0 && result != -ENOENT && result != -ENOTEMPTY)
            hpkg_report("warning: cannot remove directory %s (error %d)",
                        package->directories[i].path, (int)-result);
    }
    if (record_path(store, name, "", path) != 0)
        goto done;
    if (sys_unlink(path) != 0) {
        hpkg_report("%s: cannot remove package record", name);
        goto done;
    }
    status = 0;
done:
    hpkg_package_set_free(&set);
    return status;
}

int hpkg_store_verify(const HpkgStore *store, const HpkgPackage *package) {
    int problems = 0;

    for (size_t i = 0; i < package->file_count; ++i) {
        const HpkgFileRecord *record = &package->files[i];
        char path[STORE_PATH_CAP];
        char digest[SHA256_HEX_LENGTH + 1];
        uint64_t size;
        stat_t info;
        int status;

        if (suffixed_path(store, record->path, "", path) != 0)
            return HPKG_FAILED;
        status = hpkg_hash_file(path, digest, &size);
        if (status == HPKG_NOT_FOUND) {
            printf("%s: %s: missing\n", package->meta.name, record->path);
            ++problems;
        } else if (status != 0) {
            return HPKG_FAILED;
        } else if (size != record->size) {
            printf("%s: %s: size changed\n", package->meta.name, record->path);
            ++problems;
        } else if (strcmp(digest, record->digest) != 0) {
            printf("%s: %s: checksum mismatch\n", package->meta.name, record->path);
            ++problems;
        } else if (stat(path, &info) == 0 &&
                   (info.st_mode & S_IRWXUGO) != record->mode) {
            printf("%s: %s: permissions changed\n", package->meta.name, record->path);
            ++problems;
        }
    }
    return problems;
}
