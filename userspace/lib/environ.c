#include "errno.h"
#include "stdlib.h"
#include "string.h"

/*
 * environ starts out pointing at the vector the kernel built on the initial
 * stack. The first modification moves it into a heap table so entries can be
 * added and removed; EnvTable.owned marks the strings this module allocated
 * (and therefore may free), as opposed to the ones living on the stack.
 */

#define ENV_TABLE_GROWTH 8

typedef struct EnvTable {
    char **entries;
    unsigned char *owned;
    size_t count;
    size_t capacity;
} EnvTable;

char **environ;

static EnvTable table;

/* Called by crt0 before main. */
void environ_init(char **envp) {
    environ = envp;
}

static int name_valid(const char *name) {
    return name && name[0] != '\0' && !strchr(name, '=');
}

/* Index of name in environ, or -1. */
static long find_entry(const char *name, size_t name_len) {
    if (!environ)
        return -1;
    for (size_t i = 0; environ[i]; ++i) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=')
            return (long)i;
    }
    return -1;
}

/* Makes table the live environment, with room for extra more entries. */
static int reserve(size_t extra) {
    int active = table.entries && environ == table.entries;
    size_t count = 0;
    if (active) {
        count = table.count;
        if (count + extra + 1 <= table.capacity)
            return 0;
    } else {
        while (environ && environ[count])
            ++count;
    }

    size_t capacity = count + extra + 1 + ENV_TABLE_GROWTH;
    char **entries = (char **)malloc(capacity * sizeof(char *));
    unsigned char *owned = (unsigned char *)malloc(capacity);
    if (!entries || !owned) {
        free(entries);
        free(owned);
        errno = ENOMEM;
        return -1;
    }
    memset(owned, 0, capacity);
    memcpy(entries, active ? table.entries : environ, count * sizeof(char *));
    if (active)
        memcpy(owned, table.owned, count);
    entries[count] = 0;
    free(table.entries);
    free(table.owned);
    table.entries = entries;
    table.owned = owned;
    table.count = count;
    table.capacity = capacity;
    environ = entries;
    return 0;
}

char *getenv(const char *name) {
    if (!name_valid(name))
        return 0;
    long index = find_entry(name, strlen(name));
    return index < 0 ? 0 : environ[index] + strlen(name) + 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name_valid(name) || !value) {
        errno = EINVAL;
        return -1;
    }
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    long index = find_entry(name, name_len);
    if (index >= 0 && !overwrite)
        return 0;

    char *entry = (char *)malloc(name_len + 1 + value_len + 1);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    memcpy(entry + name_len + 1, value, value_len + 1);

    if (reserve(1) != 0) {
        free(entry);
        return -1;
    }
    if (index < 0) {
        index = (long)table.count++;
        table.entries[table.count] = 0;
    } else if (table.owned[index]) {
        free(table.entries[index]);
    }
    table.entries[index] = entry;
    table.owned[index] = 1;
    return 0;
}

int unsetenv(const char *name) {
    if (!name_valid(name)) {
        errno = EINVAL;
        return -1;
    }
    long index = find_entry(name, strlen(name));
    if (index < 0)
        return 0;
    if (reserve(0) != 0)
        return -1;
    if (table.owned[index])
        free(table.entries[index]);
    for (size_t i = (size_t)index; i + 1 < table.count; ++i) {
        table.entries[i] = table.entries[i + 1];
        table.owned[i] = table.owned[i + 1];
    }
    --table.count;
    table.entries[table.count] = 0;
    return 0;
}
