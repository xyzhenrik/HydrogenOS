#include "env.h"

#include "mini_libc.h"

typedef struct EnvVariable {
    char name[ENV_NAME_MAX];
    char value[ENV_VALUE_MAX];
    int used;
    int exported;
} EnvVariable;

static EnvVariable variables[ENV_MAX_VARIABLES];

static int is_name_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

int env_name_valid(const char *name, size_t len) {
    if (!name || len == 0 || len >= ENV_NAME_MAX || !is_name_start(name[0]))
        return 0;
    for (size_t i = 1; i < len; ++i) {
        if (!is_name_char(name[i]))
            return 0;
    }
    return 1;
}

static int name_matches(const EnvVariable *var, const char *name, size_t len) {
    size_t i = 0;
    for (; i < len; ++i) {
        if (var->name[i] != name[i])
            return 0;
    }
    return var->name[i] == 0;
}

static EnvVariable *find(const char *name, size_t len) {
    for (int i = 0; i < ENV_MAX_VARIABLES; ++i) {
        if (variables[i].used && name_matches(&variables[i], name, len))
            return &variables[i];
    }
    return 0;
}

static EnvVariable *find_or_create(const char *name, size_t len) {
    EnvVariable *var;
    if (!env_name_valid(name, len))
        return 0;
    var = find(name, len);
    if (var)
        return var;
    for (int i = 0; i < ENV_MAX_VARIABLES; ++i) {
        if (!variables[i].used) {
            var = &variables[i];
            memcpy(var->name, name, len);
            var->name[len] = 0;
            var->value[0] = 0;
            var->exported = 0;
            var->used = 1;
            return var;
        }
    }
    return 0;
}

const char *env_get(const char *name, size_t len) {
    EnvVariable *var = find(name, len);
    return var ? var->value : 0;
}

int env_set(const char *name, size_t len, const char *value) {
    size_t value_len = value ? strlen(value) : 0;
    EnvVariable *var;
    if (value_len >= ENV_VALUE_MAX)
        return -1;
    var = find_or_create(name, len);
    if (!var)
        return -1;
    if (value_len > 0)
        memcpy(var->value, value, value_len);
    var->value[value_len] = 0;
    return 0;
}

int env_export(const char *name, size_t len) {
    EnvVariable *var = find_or_create(name, len);
    if (!var)
        return -1;
    var->exported = 1;
    return 0;
}

void env_unset(const char *name, size_t len) {
    EnvVariable *var = find(name, len);
    if (var)
        var->used = 0;
}

void env_import(char **envp) {
    if (!envp)
        return;
    for (int i = 0; envp[i]; ++i) {
        const char *entry = envp[i];
        size_t name_len = 0;
        while (entry[name_len] && entry[name_len] != '=')
            name_len++;
        if (entry[name_len] != '=')
            continue;
        if (env_set(entry, name_len, entry + name_len + 1) == 0)
            env_export(entry, name_len);
    }
}

void env_for_each_exported(EnvVisitor visitor, void *context) {
    for (int i = 0; i < ENV_MAX_VARIABLES; ++i) {
        if (variables[i].used && variables[i].exported)
            visitor(variables[i].name, variables[i].value, context);
    }
}

int env_build_envp(char *storage, size_t storage_cap, const char **vector,
                   size_t vector_cap) {
    size_t used = 0;
    size_t count = 0;
    for (int i = 0; i < ENV_MAX_VARIABLES; ++i) {
        const EnvVariable *var = &variables[i];
        size_t name_len;
        size_t value_len;
        if (!var->used || !var->exported)
            continue;
        name_len = strlen(var->name);
        value_len = strlen(var->value);
        if (count + 1 >= vector_cap ||
            used + name_len + value_len + 2 > storage_cap)
            return -1;
        vector[count++] = storage + used;
        memcpy(storage + used, var->name, name_len);
        used += name_len;
        storage[used++] = '=';
        memcpy(storage + used, var->value, value_len);
        used += value_len;
        storage[used++] = 0;
    }
    vector[count] = 0;
    return 0;
}
