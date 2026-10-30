#include "hpkg.h"

#include "errno.h"
#include "mini_libc.h"
#include "syscall.h"

#define DATABASE_ROOT "/var/lib/hpkg"
#define HASH_CHUNK 4096

void hpkg_report(const char *format, ...) {
    va_list arguments;

    fprintf(stderr, "hpkg: ");
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fprintf(stderr, "\n");
}

static int is_lower_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static int is_alnum(char c) {
    return is_lower_alnum(c) || (c >= 'A' && c <= 'Z');
}

int hpkg_valid_name(const char *name) {
    size_t length = strlen(name);

    if (length == 0 || length > HPKG_NAME_MAX || !is_lower_alnum(name[0]))
        return 0;
    for (size_t i = 1; i < length; ++i) {
        if (!is_lower_alnum(name[i]) && name[i] != '+' && name[i] != '_' &&
            name[i] != '-')
            return 0;
    }
    return 1;
}

int hpkg_valid_version(const char *version) {
    size_t length = strlen(version);

    if (length == 0 || length > HPKG_VERSION_MAX || !is_alnum(version[0]))
        return 0;
    for (size_t i = 1; i < length; ++i) {
        char c = version[i];
        if (!is_alnum(c) && c != '.' && c != '+' && c != '~' && c != '_' && c != '-')
            return 0;
    }
    return 1;
}

static int has_suffix(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);

    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

static int is_inside(const char *path, const char *directory) {
    size_t length = strlen(directory);

    return strncmp(path, directory, length) == 0 &&
           (path[length] == 0 || path[length] == '/');
}

int hpkg_valid_path(const char *path) {
    size_t length = strlen(path);
    size_t component_start = 1;

    if (length < 2 || length > HPKG_PATH_MAX || path[0] != '/' || path[length - 1] == '/')
        return 0;
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char)path[i] <= ' ' || (unsigned char)path[i] > '~')
            return 0;
    }
    for (size_t i = 1; i <= length; ++i) {
        size_t component_length;

        if (path[i] != '/' && path[i] != 0)
            continue;
        component_length = i - component_start;
        if (component_length == 0)
            return 0;
        if (component_length <= 2 && path[component_start] == '.' &&
            (component_length == 1 || path[component_start + 1] == '.'))
            return 0;
        component_start = i + 1;
    }
    if (has_suffix(path, ".hpkg-new") || has_suffix(path, ".hpkg-old"))
        return 0;
    return !is_inside(path, DATABASE_ROOT);
}

int hpkg_path_compare(const char *first, const char *second) {
    for (;; ++first, ++second) {
        unsigned char a = (unsigned char)*first;
        unsigned char b = (unsigned char)*second;

        if (a == '/')
            a = 1;
        if (b == '/')
            b = 1;
        if (a != b)
            return a < b ? -1 : 1;
        if (a == 0)
            return 0;
    }
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_separator(char c) {
    return c == '.' || c == '-' || c == '+' || c == '~' || c == '_';
}

/* Takes the next segment off *text. Returns its length, 0 at the end. */
static size_t next_segment(const char **text, const char **start) {
    const char *cursor = *text;
    size_t length = 0;

    while (*cursor && is_separator(*cursor))
        ++cursor;
    *start = cursor;
    while (cursor[length] && !is_separator(cursor[length]))
        ++length;
    *text = cursor + length;
    return length;
}

static int all_digits(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (!is_digit(text[i]))
            return 0;
    }
    return 1;
}

static int compare_segment(const char *first, size_t first_length,
                           const char *second, size_t second_length) {
    if (all_digits(first, first_length) && all_digits(second, second_length)) {
        while (first_length > 1 && *first == '0') {
            ++first;
            --first_length;
        }
        while (second_length > 1 && *second == '0') {
            ++second;
            --second_length;
        }
        if (first_length != second_length)
            return first_length < second_length ? -1 : 1;
    }
    size_t common = first_length < second_length ? first_length : second_length;
    int order = memcmp(first, second, common);

    if (order != 0)
        return order < 0 ? -1 : 1;
    if (first_length == second_length)
        return 0;
    return first_length < second_length ? -1 : 1;
}

int hpkg_version_compare(const char *first, const char *second) {
    for (;;) {
        const char *first_start;
        const char *second_start;
        size_t first_length = next_segment(&first, &first_start);
        size_t second_length = next_segment(&second, &second_start);
        int order;

        if (first_length == 0 || second_length == 0) {
            if (first_length == second_length)
                return 0;
            return first_length == 0 ? -1 : 1;
        }
        order = compare_segment(first_start, first_length, second_start, second_length);
        if (order != 0)
            return order;
    }
}

static int parse_depends(HpkgMeta *meta, const char *value) {
    char word[HPKG_NAME_MAX + 2];

    meta->depend_count = 0;
    while (*value) {
        size_t length = 0;

        while (*value == ' ')
            ++value;
        if (!*value)
            break;
        while (value[length] && value[length] != ' ')
            ++length;
        if (length > HPKG_NAME_MAX || meta->depend_count == HPKG_DEPENDS_MAX)
            return -1;
        memcpy(word, value, length);
        word[length] = 0;
        if (!hpkg_valid_name(word) || hpkg_meta_depends_on(meta, word))
            return -1;
        strcpy(meta->depends[meta->depend_count++], word);
        value += length;
    }
    return 0;
}

int hpkg_meta_set(HpkgMeta *meta, const char *key, const char *value) {
    if (strcmp(key, "name") == 0) {
        if (!hpkg_valid_name(value))
            return -1;
        strcpy(meta->name, value);
    } else if (strcmp(key, "version") == 0) {
        if (!hpkg_valid_version(value))
            return -1;
        strcpy(meta->version, value);
    } else if (strcmp(key, "description") == 0) {
        size_t length = strlen(value);

        if (length > HPKG_DESCRIPTION_MAX)
            return -1;
        for (size_t i = 0; i < length; ++i) {
            if ((unsigned char)value[i] < ' ' || (unsigned char)value[i] > '~')
                return -1;
        }
        strcpy(meta->description, value);
    } else if (strcmp(key, "depends") == 0) {
        return parse_depends(meta, value);
    }
    return 0;
}

int hpkg_meta_depends_on(const HpkgMeta *meta, const char *name) {
    for (size_t i = 0; i < meta->depend_count; ++i) {
        if (strcmp(meta->depends[i], name) == 0)
            return 1;
    }
    return 0;
}

int hpkg_text_append(HpkgText *text, const char *format, ...) {
    va_list arguments;
    int needed;

    va_start(arguments, format);
    needed = vsnprintf(0, 0, format, arguments);
    va_end(arguments);
    if (needed < 0)
        return HPKG_FAILED;
    if (text->length + (size_t)needed + 1 > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 256;
        char *grown;

        while (capacity < text->length + (size_t)needed + 1)
            capacity *= 2;
        grown = realloc(text->data, capacity);
        if (!grown) {
            hpkg_report("out of memory");
            return HPKG_FAILED;
        }
        text->data = grown;
        text->capacity = capacity;
    }
    va_start(arguments, format);
    vsnprintf(text->data + text->length, (size_t)needed + 1, format, arguments);
    va_end(arguments);
    text->length += (size_t)needed;
    return 0;
}

void hpkg_text_free(HpkgText *text) {
    free(text->data);
    text->data = 0;
    text->length = 0;
    text->capacity = 0;
}

int hpkg_read_file(const char *path, size_t limit, uint8_t **data, size_t *length) {
    long fd = open(path, OPEN_READ);
    stat_t info;
    uint8_t *buffer;
    size_t total = 0;

    if (fd == -ENOENT)
        return HPKG_NOT_FOUND;
    if (fd < 0) {
        hpkg_report("%s: cannot open (error %d)", path, (int)-fd);
        return HPKG_FAILED;
    }
    if (fstat((int)fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        close((int)fd);
        hpkg_report("%s: not a regular file", path);
        return HPKG_FAILED;
    }
    if (info.st_size > limit) {
        close((int)fd);
        hpkg_report("%s: too large", path);
        return HPKG_FAILED;
    }
    buffer = malloc((size_t)info.st_size + 1);
    if (!buffer) {
        close((int)fd);
        hpkg_report("out of memory");
        return HPKG_FAILED;
    }
    while (total < info.st_size) {
        long got = read((int)fd, buffer + total, info.st_size - total);

        if (got <= 0)
            break;
        total += (size_t)got;
    }
    close((int)fd);
    if (total != info.st_size) {
        free(buffer);
        hpkg_report("%s: read failed", path);
        return HPKG_FAILED;
    }
    buffer[total] = 0;
    *data = buffer;
    *length = total;
    return 0;
}

int hpkg_write_file(const char *path, const void *data, size_t length, unsigned mode) {
    long fd = open(path, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    const uint8_t *bytes = data;
    size_t written = 0;

    if (fd < 0) {
        hpkg_report("%s: cannot create (error %d)", path, (int)-fd);
        return HPKG_FAILED;
    }
    while (written < length) {
        long put = write((int)fd, bytes + written, length - written);

        if (put <= 0) {
            close((int)fd);
            sys_unlink(path);
            hpkg_report("%s: write failed", path);
            return HPKG_FAILED;
        }
        written += (size_t)put;
    }
    close((int)fd);
    if (sys_chmod(path, mode) != 0) {
        sys_unlink(path);
        hpkg_report("%s: cannot set permissions", path);
        return HPKG_FAILED;
    }
    return 0;
}

int hpkg_hash_file(const char *path, char hex[SHA256_HEX_LENGTH + 1], uint64_t *size) {
    long fd = open(path, OPEN_READ);
    Sha256Context context;
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint8_t chunk[HASH_CHUNK];
    uint64_t total = 0;

    if (fd == -ENOENT)
        return HPKG_NOT_FOUND;
    if (fd < 0) {
        hpkg_report("%s: cannot open (error %d)", path, (int)-fd);
        return HPKG_FAILED;
    }
    sha256_init(&context);
    for (;;) {
        long got = read((int)fd, chunk, sizeof(chunk));

        if (got < 0) {
            close((int)fd);
            hpkg_report("%s: read failed", path);
            return HPKG_FAILED;
        }
        if (got == 0)
            break;
        sha256_update(&context, chunk, (size_t)got);
        total += (uint64_t)got;
    }
    close((int)fd);
    sha256_final(&context, digest);
    sha256_to_hex(digest, hex);
    *size = total;
    return 0;
}

int hpkg_each_line(char *text, int (*handler)(char *line, void *context), void *context) {
    char *line = text;

    while (*line) {
        char *end = line;
        int result;

        while (*end && *end != '\n')
            ++end;
        if (*end == '\n')
            *end++ = 0;
        if (*line) {
            result = handler(line, context);
            if (result != 0)
                return result;
        }
        line = end;
    }
    return 0;
}
