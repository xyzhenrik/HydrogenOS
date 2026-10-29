#pragma once

#include "builtins.h"

/* Looks up a pipe/poll diagnostic command; returns 0 for unknown names. */
BuiltinFn diag_find(const char *name);
