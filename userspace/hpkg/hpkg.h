#pragma once

/*
 * hpkg: shared definitions. The package archive format, the database layout
 * and the repository index are specified in docs/HPKG.md.
 */

#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

enum {
    HPKG_NAME_MAX = 32,
    HPKG_VERSION_MAX = 32,
    HPKG_DESCRIPTION_MAX = 120,
    HPKG_DEPENDS_MAX = 16,
    /* The kernel limits a path to 255 characters; a package path plus the
     * root prefix plus the ".hpkg-new" staging suffix must stay below that. */
    HPKG_PATH_MAX = 200,
    HPKG_ROOT_MAX = 32,
    HPKG_FILES_MAX = 4096,
    HPKG_MANIFEST_MAX = 4096,
    HPKG_ARCHIVE_MAX = 32 * 1024 * 1024,
};

/* Return convention of every function below: 0 on success, HPKG_NOT_FOUND
 * where absence is a meaningful answer, HPKG_FAILED for everything else.
 * Failures have already been reported on stderr by the time they return. */
enum {
    HPKG_FAILED = -1,
    HPKG_NOT_FOUND = -2,
};

/* Identity and relations of a package, shared by archive, database and index. */
typedef struct HpkgMeta {
    char name[HPKG_NAME_MAX + 1];
    char version[HPKG_VERSION_MAX + 1];
    char description[HPKG_DESCRIPTION_MAX + 1];
    char depends[HPKG_DEPENDS_MAX][HPKG_NAME_MAX + 1];
    size_t depend_count;
} HpkgMeta;

/* Growing text buffer; always NUL-terminated once anything was appended. */
typedef struct HpkgText {
    char *data;
    size_t length;
    size_t capacity;
} HpkgText;

/* Prints "hpkg: <message>\n" to stderr. */
void hpkg_report(const char *format, ...);

/* Package names are lower-case [a-z0-9+_-] and start with a letter or digit;
 * the exclusion of '.' keeps them apart from the ".hpkg-*" working files. */
int hpkg_valid_name(const char *name);
int hpkg_valid_version(const char *version);
/* Absolute, canonical (no "//", "." or ".." components, no trailing slash),
 * printable without blanks, and not under the package database. */
int hpkg_valid_path(const char *path);

/* Orders paths so that every directory is followed directly by its contents
 * ('/' sorts below every other character). */
int hpkg_path_compare(const char *first, const char *second);
/* Compares dotted versions segment by segment: numbers numerically, other
 * text bytewise, a missing segment before a present one. */
int hpkg_version_compare(const char *first, const char *second);

/* Sets one manifest field. Unknown keys are ignored for forward
 * compatibility. */
int hpkg_meta_set(HpkgMeta *meta, const char *key, const char *value);
int hpkg_meta_depends_on(const HpkgMeta *meta, const char *name);

int hpkg_text_append(HpkgText *text, const char *format, ...);
void hpkg_text_free(HpkgText *text);

/* Reads a whole file into a new buffer (one spare NUL byte is appended past
 * length). HPKG_NOT_FOUND, without a report, when it does not exist. */
int hpkg_read_file(const char *path, size_t limit, uint8_t **data, size_t *length);
/* Creates or truncates path, writes data and applies the permission bits.
 * A file that could not be completed is removed again. */
int hpkg_write_file(const char *path, const void *data, size_t length, unsigned mode);
/* SHA-256 and size of a file's contents. */
int hpkg_hash_file(const char *path, char hex[SHA256_HEX_LENGTH + 1], uint64_t *size);

/* Splits a text into lines in place (replacing '\n' with NUL). Calls
 * handler for each non-empty line; stops at the first non-zero result. */
int hpkg_each_line(char *text, int (*handler)(char *line, void *context), void *context);
