#include "passwd.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

enum {
    LINE_CAP = 256,
    FIELD_COUNT = 7,
    FIELD_NAME = 0,
    FIELD_UID = 2,
    FIELD_GID = 3,
    FIELD_HOME = 5,
    FIELD_SHELL = 6,
};

/* Reserved by the kernel to mean "no id". */
#define ID_RESERVED 0xFFFFFFFFul

typedef int (*MatchFunction)(const passwd_entry_t *entry, const void *key);

static int parse_id(const char *text, uint32_t *out) {
    char *end = 0;
    if (*text < '0' || *text > '9')
        return -1;
    unsigned long value = strtoul(text, &end, 10);
    if (*end != '\0' || value >= ID_RESERVED)
        return -1;
    *out = (uint32_t)value;
    return 0;
}

static int copy_field(char *dst, size_t cap, const char *src) {
    size_t len = strlen(src);
    if (len >= cap)
        return -1;
    memcpy(dst, src, len + 1);
    return 0;
}

/* Splits line at its colons, in place, into exactly FIELD_COUNT fields. */
static int parse_line(char *line, passwd_entry_t *out) {
    char *fields[FIELD_COUNT];
    char *cursor = line;
    for (int i = 0; i < FIELD_COUNT; ++i) {
        fields[i] = cursor;
        if (i == FIELD_COUNT - 1)
            break;
        cursor = strchr(cursor, ':');
        if (!cursor)
            return -1;
        *cursor++ = '\0';
    }
    if (strchr(fields[FIELD_SHELL], ':') || fields[FIELD_NAME][0] == '\0')
        return -1;
    if (parse_id(fields[FIELD_UID], &out->uid) != 0 ||
        parse_id(fields[FIELD_GID], &out->gid) != 0)
        return -1;
    if (copy_field(out->name, sizeof(out->name), fields[FIELD_NAME]) != 0 ||
        copy_field(out->home, sizeof(out->home), fields[FIELD_HOME]) != 0 ||
        copy_field(out->shell, sizeof(out->shell), fields[FIELD_SHELL]) != 0)
        return -1;
    return 0;
}

static int find(MatchFunction matches, const void *key, passwd_entry_t *out) {
    char line[LINE_CAP];
    int result = -1;
    FILE *file = fopen(PASSWD_FILE, "r");
    if (!file)
        return -1;
    while (result != 0 && fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        passwd_entry_t entry;
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        } else if (!feof(file)) {
            /* Overlong line: drop what is left of it. */
            int c;
            do {
                c = fgetc(file);
            } while (c != '\n' && c != EOF);
            continue;
        }
        if (parse_line(line, &entry) == 0 && matches(&entry, key)) {
            *out = entry;
            result = 0;
        }
    }
    fclose(file);
    return result;
}

static int matches_name(const passwd_entry_t *entry, const void *key) {
    return strcmp(entry->name, (const char *)key) == 0;
}

static int matches_uid(const passwd_entry_t *entry, const void *key) {
    return entry->uid == *(const uint32_t *)key;
}

int passwd_find_name(const char *name, passwd_entry_t *out) {
    return find(matches_name, name, out);
}

int passwd_find_uid(uint32_t uid, passwd_entry_t *out) {
    return find(matches_uid, &uid, out);
}
