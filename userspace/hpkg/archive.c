#include "archive.h"

#include "mini_libc.h"

#define ARCHIVE_MAGIC "HPK1"
#define ARCHIVE_MAGIC_LENGTH 4

typedef struct Cursor {
    const uint8_t *position;
    size_t remaining;
} Cursor;

static int take_bytes(Cursor *cursor, size_t count, const uint8_t **bytes) {
    if (count > cursor->remaining)
        return -1;
    *bytes = cursor->position;
    cursor->position += count;
    cursor->remaining -= count;
    return 0;
}

static int take_u32(Cursor *cursor, uint32_t *value) {
    const uint8_t *bytes;

    if (take_bytes(cursor, 4, &bytes) != 0)
        return -1;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 0;
}

typedef struct ManifestState {
    HpkgMeta *meta;
    const char *archive_path;
} ManifestState;

static int read_manifest_line(char *line, void *context) {
    ManifestState *state = context;
    char *space = strchr(line, ' ');

    if (!space) {
        hpkg_report("%s: malformed manifest line", state->archive_path);
        return -1;
    }
    *space = 0;
    if (hpkg_meta_set(state->meta, line, space + 1) != 0) {
        hpkg_report("%s: invalid manifest field '%s'", state->archive_path, line);
        return -1;
    }
    return 0;
}

static int read_manifest(const char *archive_path, Cursor *cursor, HpkgMeta *meta) {
    uint32_t length;
    const uint8_t *bytes;
    char text[HPKG_MANIFEST_MAX + 1];
    ManifestState state = {meta, archive_path};

    if (take_u32(cursor, &length) != 0 || length > HPKG_MANIFEST_MAX ||
        take_bytes(cursor, length, &bytes) != 0) {
        hpkg_report("%s: truncated or oversized manifest", archive_path);
        return HPKG_FAILED;
    }
    memcpy(text, bytes, length);
    text[length] = 0;
    memset(meta, 0, sizeof(*meta));
    if (hpkg_each_line(text, read_manifest_line, &state) != 0)
        return HPKG_FAILED;
    if (!meta->name[0] || !meta->version[0]) {
        hpkg_report("%s: manifest lacks name or version", archive_path);
        return HPKG_FAILED;
    }
    return 0;
}

static int read_entry(const char *archive_path, Cursor *cursor, HpkgEntry *entry) {
    uint32_t path_length;
    const uint8_t *path_bytes;
    const uint8_t *digest_bytes;
    Sha256Context context;
    uint8_t computed[SHA256_DIGEST_SIZE];

    if (take_u32(cursor, &path_length) != 0 || path_length == 0 ||
        path_length > HPKG_PATH_MAX || take_bytes(cursor, path_length, &path_bytes) != 0 ||
        take_u32(cursor, &entry->mode) != 0 || take_u32(cursor, &entry->size) != 0 ||
        take_bytes(cursor, SHA256_DIGEST_SIZE, &digest_bytes) != 0 ||
        take_bytes(cursor, entry->size, &entry->data) != 0) {
        hpkg_report("%s: truncated file table", archive_path);
        return HPKG_FAILED;
    }
    memcpy(entry->path, path_bytes, path_length);
    entry->path[path_length] = 0;
    if (strlen(entry->path) != path_length || !hpkg_valid_path(entry->path)) {
        hpkg_report("%s: unsafe path in package", archive_path);
        return HPKG_FAILED;
    }
    if (entry->mode & ~(uint32_t)S_IRWXUGO) {
        hpkg_report("%s: %s: unsupported mode bits", archive_path, entry->path);
        return HPKG_FAILED;
    }
    sha256_init(&context);
    sha256_update(&context, entry->data, entry->size);
    sha256_final(&context, computed);
    if (memcmp(computed, digest_bytes, SHA256_DIGEST_SIZE) != 0) {
        hpkg_report("%s: checksum mismatch for %s", archive_path, entry->path);
        return HPKG_FAILED;
    }
    sha256_to_hex(computed, entry->digest);
    return 0;
}

/* The table must be strictly ascending in hpkg_path_compare order. That
 * rules out duplicates and, because directories sort right before their
 * contents, any path that is used both as a file and as a directory. */
static int check_table_order(const char *archive_path, const HpkgEntry *entries,
                             size_t count) {
    for (size_t i = 1; i < count; ++i) {
        const char *previous = entries[i - 1].path;
        size_t previous_length = strlen(previous);

        if (hpkg_path_compare(previous, entries[i].path) >= 0) {
            hpkg_report("%s: file table not sorted or duplicate: %s", archive_path,
                        entries[i].path);
            return HPKG_FAILED;
        }
        if (strncmp(previous, entries[i].path, previous_length) == 0 &&
            entries[i].path[previous_length] == '/') {
            hpkg_report("%s: %s is both a file and a directory", archive_path, previous);
            return HPKG_FAILED;
        }
    }
    return 0;
}

static int parse(const char *path, HpkgArchive *archive) {
    Cursor cursor = {archive->buffer, archive->length};
    const uint8_t *bytes;
    uint32_t count;
    Sha256Context context;
    uint8_t computed[SHA256_DIGEST_SIZE];

    if (archive->length < ARCHIVE_MAGIC_LENGTH + SHA256_DIGEST_SIZE ||
        memcmp(archive->buffer, ARCHIVE_MAGIC, ARCHIVE_MAGIC_LENGTH) != 0) {
        hpkg_report("%s: not a hpkg package", path);
        return HPKG_FAILED;
    }
    sha256_init(&context);
    sha256_update(&context, archive->buffer, archive->length - SHA256_DIGEST_SIZE);
    sha256_final(&context, computed);
    if (memcmp(computed, archive->buffer + archive->length - SHA256_DIGEST_SIZE,
               SHA256_DIGEST_SIZE) != 0) {
        hpkg_report("%s: archive checksum mismatch (corrupt or truncated)", path);
        return HPKG_FAILED;
    }
    cursor.remaining -= SHA256_DIGEST_SIZE;
    take_bytes(&cursor, ARCHIVE_MAGIC_LENGTH, &bytes);
    if (read_manifest(path, &cursor, &archive->meta) != 0)
        return HPKG_FAILED;
    if (take_u32(&cursor, &count) != 0 || count > HPKG_FILES_MAX) {
        hpkg_report("%s: bad file count", path);
        return HPKG_FAILED;
    }
    archive->entries = calloc(count ? count : 1, sizeof(HpkgEntry));
    if (!archive->entries) {
        hpkg_report("out of memory");
        return HPKG_FAILED;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (read_entry(path, &cursor, &archive->entries[i]) != 0)
            return HPKG_FAILED;
        archive->entry_count++;
    }
    if (cursor.remaining != 0) {
        hpkg_report("%s: trailing data after file table", path);
        return HPKG_FAILED;
    }
    return check_table_order(path, archive->entries, archive->entry_count);
}

int hpkg_archive_open(const char *path, HpkgArchive *archive) {
    int status;

    memset(archive, 0, sizeof(*archive));
    status = hpkg_read_file(path, HPKG_ARCHIVE_MAX, &archive->buffer, &archive->length);
    if (status == HPKG_NOT_FOUND) {
        hpkg_report("%s: no such package file", path);
        return HPKG_FAILED;
    }
    if (status != 0)
        return HPKG_FAILED;
    sha256_hex(archive->buffer, archive->length, archive->file_digest);
    if (parse(path, archive) != 0) {
        hpkg_archive_close(archive);
        return HPKG_FAILED;
    }
    return 0;
}

void hpkg_archive_close(HpkgArchive *archive) {
    free(archive->buffer);
    free(archive->entries);
    memset(archive, 0, sizeof(*archive));
}
