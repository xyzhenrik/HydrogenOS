#pragma once

#include <stddef.h>

enum {
    ENV_MAX_VARIABLES = 64,
    ENV_NAME_MAX = 32,
    ENV_VALUE_MAX = 256,
};

typedef void (*EnvVisitor)(const char *name, const char *value, void *context);

/* A name is [A-Za-z_][A-Za-z0-9_]* and shorter than ENV_NAME_MAX. */
int env_name_valid(const char *name, size_t len);

/* Returns the value of a set variable, or 0. */
const char *env_get(const char *name, size_t len);

/* Sets a variable, keeping its export flag; new variables are local. */
int env_set(const char *name, size_t len, const char *value);

/* Marks a variable for export to child processes, creating it empty. */
int env_export(const char *name, size_t len);

void env_unset(const char *name, size_t len);

/* Imports "NAME=value" entries from a spawn envp as exported variables. */
void env_import(char **envp);

void env_for_each_exported(EnvVisitor visitor, void *context);

/* Packs the exported variables as a NULL-terminated envp in storage.
 * Returns 0, or -1 when they do not fit. */
int env_build_envp(char *storage, size_t storage_cap, const char **vector,
                   size_t vector_cap);
