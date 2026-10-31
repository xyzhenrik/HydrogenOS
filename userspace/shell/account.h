#pragma once

#include <stddef.h>
#include <stdint.h>

#include "builtins.h"

/* The user and permission builtins: chmod, chown, id, whoami. Returns 0 for
 * any other name. */
BuiltinFn account_find(const char *name);

/* Formats the file type and permission bits of an st_mode the way ls -l
 * shows them, e.g. "drwxr-xr-x". */
void account_format_mode(uint32_t mode, char out[11]);

/* Writes the name of the account with this id, or the number if there is
 * none, into out. */
void account_format_owner(uint32_t id, char *out, size_t cap);
