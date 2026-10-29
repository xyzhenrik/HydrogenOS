#pragma once

/* Builtins write through output.h and return their exit status. They run
 * inside the shell process unless they are part of a pipeline or a
 * background job, in which case a child shell runs them. */
typedef int (*BuiltinFn)(int argc, char **argv);

BuiltinFn builtin_find(const char *name);
