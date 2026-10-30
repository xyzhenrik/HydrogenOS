#pragma once

/* Every command the shell implements itself, as X(name): the builtin table
 * expands it into {"name", builtin_name}, and the desktop terminal expands it
 * into the candidates for command completion. Keep it in alphabetical order;
 * the pipe and poll diagnostics are deliberately not listed. */
#define SHELL_BUILTIN_NAMES(X) \
    X(cat) X(cd) X(clear) X(cp) X(echo) X(edit) X(env) X(exec) X(exit) X(export) \
    X(false) X(files) X(help) X(jobs) X(kill) X(ln) X(ls) X(mkdir) X(mv) \
    X(pwd) X(readlink) X(rm) X(sleep) X(time) X(touch) X(true) X(unset) \
    X(uptime) X(version) X(wait)
