#include "repo.h"

#include "mini_libc.h"

#define INDEX_HEADER "HPKG-INDEX 1"
#define REPOSITORY_PATH_CAP 256

enum {
    MARK_UNVISITED = 0,
    MARK_VISITING,
    MARK_PLANNED,
};

static int valid_file_name(const char *file) {
    size_t length = strlen(file);

    if (length < 6 || length > HPKG_FILE_NAME_MAX || file[0] == '.' ||
        strcmp(file + length - 5, ".hpkg") != 0)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        char c = file[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+'))
            return 0;
    }
    return 1;
}

typedef struct IndexParser {
    HpkgIndex *index;
    size_t capacity;
    int header_seen;
} IndexParser;

/* One line: name version size sha256 depends file [description...]. depends
 * is a comma separated list of names or "-". */
static int parse_entry(IndexParser *parser, char *line) {
    HpkgIndex *index = parser->index;
    HpkgIndexEntry *entry;
    char *save = 0;
    char *name = strtok_r(line, " ", &save);
    char *version = strtok_r(0, " ", &save);
    char *size = strtok_r(0, " ", &save);
    char *digest = strtok_r(0, " ", &save);
    char *depends = strtok_r(0, " ", &save);
    char *file = strtok_r(0, " ", &save);
    char *end;

    if (!file)
        return -1;
    if (index->count == parser->capacity) {
        size_t capacity = parser->capacity ? parser->capacity * 2 : 8;
        HpkgIndexEntry *grown = realloc(index->entries, capacity * sizeof(HpkgIndexEntry));

        if (!grown)
            return -1;
        index->entries = grown;
        parser->capacity = capacity;
    }
    entry = &index->entries[index->count];
    memset(entry, 0, sizeof(*entry));
    if (hpkg_meta_set(&entry->meta, "name", name) != 0 ||
        hpkg_meta_set(&entry->meta, "version", version) != 0)
        return -1;
    entry->size = (uint32_t)strtoul(size, &end, 10);
    if (*size == 0 || *end || strlen(digest) != SHA256_HEX_LENGTH || !valid_file_name(file))
        return -1;
    strcpy(entry->digest, digest);
    strcpy(entry->file, file);
    if (strcmp(depends, "-") != 0) {
        for (char *comma = depends; *comma; ++comma) {
            if (*comma == ',')
                *comma = ' ';
        }
        if (hpkg_meta_set(&entry->meta, "depends", depends) != 0)
            return -1;
    }
    if (save && hpkg_meta_set(&entry->meta, "description", save) != 0)
        return -1;
    for (size_t i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].meta.name, entry->meta.name) == 0)
            return -1;
    }
    index->count++;
    return 0;
}

static int parse_index_line(char *line, void *context) {
    IndexParser *parser = context;

    if (!parser->header_seen) {
        parser->header_seen = 1;
        return strcmp(line, INDEX_HEADER) == 0 ? 0 : -1;
    }
    if (line[0] == '#')
        return 0;
    return parse_entry(parser, line);
}

static int join_path(char *out, const char *repository, const char *file) {
    int length = snprintf(out, REPOSITORY_PATH_CAP, "%s/%s", repository, file);

    if (length < 0 || length >= REPOSITORY_PATH_CAP) {
        hpkg_report("%s: repository path too long", repository);
        return HPKG_FAILED;
    }
    return 0;
}

int hpkg_index_load(const char *repository, HpkgIndex *index) {
    char path[REPOSITORY_PATH_CAP];
    uint8_t *text;
    size_t length;
    IndexParser parser = {index, 0, 0};
    int status;

    index->entries = 0;
    index->count = 0;
    if (join_path(path, repository, "index") != 0)
        return HPKG_FAILED;
    status = hpkg_read_file(path, HPKG_INDEX_MAX, &text, &length);
    if (status == HPKG_NOT_FOUND) {
        hpkg_report("%s: no repository index", path);
        return HPKG_FAILED;
    }
    if (status != 0)
        return HPKG_FAILED;
    if (hpkg_each_line((char *)text, parse_index_line, &parser) != 0 || !parser.header_seen) {
        hpkg_report("%s: malformed repository index", path);
        free(text);
        hpkg_index_free(index);
        return HPKG_FAILED;
    }
    free(text);
    return 0;
}

void hpkg_index_free(HpkgIndex *index) {
    free(index->entries);
    index->entries = 0;
    index->count = 0;
}

const HpkgIndexEntry *hpkg_index_find(const HpkgIndex *index, const char *name) {
    for (size_t i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].meta.name, name) == 0)
            return &index->entries[i];
    }
    return 0;
}

int hpkg_repo_open_package(const char *repository, const HpkgIndexEntry *entry,
                           HpkgArchive *archive) {
    char path[REPOSITORY_PATH_CAP];

    if (join_path(path, repository, entry->file) != 0 ||
        hpkg_archive_open(path, archive) != 0)
        return HPKG_FAILED;
    if (archive->length != entry->size || strcmp(archive->file_digest, entry->digest) != 0) {
        hpkg_report("%s: does not match the repository index (size or checksum)", path);
        hpkg_archive_close(archive);
        return HPKG_FAILED;
    }
    if (strcmp(archive->meta.name, entry->meta.name) != 0 ||
        strcmp(archive->meta.version, entry->meta.version) != 0) {
        hpkg_report("%s: contains %s %s, the index promises %s %s", path, archive->meta.name,
                    archive->meta.version, entry->meta.name, entry->meta.version);
        hpkg_archive_close(archive);
        return HPKG_FAILED;
    }
    return 0;
}

int hpkg_repo_check_package(const char *repository, const HpkgIndexEntry *entry) {
    char path[REPOSITORY_PATH_CAP];
    char digest[SHA256_HEX_LENGTH + 1];
    uint64_t size;
    int status;

    if (join_path(path, repository, entry->file) != 0)
        return HPKG_FAILED;
    status = hpkg_hash_file(path, digest, &size);
    if (status != 0)
        return status;
    if (size != entry->size || strcmp(digest, entry->digest) != 0) {
        hpkg_report("%s: does not match the repository index (size or checksum)", path);
        return HPKG_FAILED;
    }
    return 0;
}

typedef struct Planner {
    const HpkgIndex *index;
    const HpkgPackageSet *installed;
    uint8_t *marks;
    const HpkgIndexEntry **order;
    size_t count;
} Planner;

static int visit(Planner *planner, const char *name, const char *requester) {
    const HpkgIndexEntry *entry = hpkg_index_find(planner->index, name);
    size_t position;

    if (!entry) {
        if (requester)
            hpkg_report("%s needs %s, which is not in the repository", requester, name);
        else
            hpkg_report("package '%s' not found in the repository", name);
        return HPKG_FAILED;
    }
    position = (size_t)(entry - planner->index->entries);
    if (planner->marks[position] == MARK_PLANNED)
        return 0;
    if (planner->marks[position] == MARK_VISITING) {
        hpkg_report("dependency cycle through %s", name);
        return HPKG_FAILED;
    }
    planner->marks[position] = MARK_VISITING;
    for (size_t i = 0; i < entry->meta.depend_count; ++i) {
        const char *dependency = entry->meta.depends[i];

        if (hpkg_package_set_find(planner->installed, dependency))
            continue;
        if (visit(planner, dependency, name) != 0)
            return HPKG_FAILED;
    }
    planner->marks[position] = MARK_PLANNED;
    planner->order[planner->count++] = entry;
    return 0;
}

int hpkg_repo_plan(const HpkgIndex *index, const HpkgPackageSet *installed,
                   const char *name, const HpkgIndexEntry ***order, size_t *count) {
    Planner planner = {index, installed, 0, 0, 0};

    planner.marks = calloc(index->count ? index->count : 1, 1);
    planner.order = calloc(index->count ? index->count : 1, sizeof(*planner.order));
    if (!planner.marks || !planner.order) {
        free(planner.marks);
        free(planner.order);
        hpkg_report("out of memory");
        return HPKG_FAILED;
    }
    if (visit(&planner, name, 0) != 0) {
        free(planner.marks);
        free(planner.order);
        return HPKG_FAILED;
    }
    free(planner.marks);
    *order = planner.order;
    *count = planner.count;
    return 0;
}
