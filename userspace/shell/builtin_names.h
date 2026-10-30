#pragma once

/* Every command the shell implements in builtins.c, as X(name): the builtin
 * table expands it into {"name", builtin_name}. Keep it in alphabetical order;
 * the pipe and poll diagnostics are deliberately not listed. */
#define SHELL_BUILTIN_NAMES(X) \
    X(cat) X(cd) X(clear) X(cp) X(echo) X(edit) X(env) X(exec) X(exit) X(export) \
    X(false) X(files) X(help) X(jobs) X(kill) X(ln) X(ls) X(mkdir) X(mv) \
    X(pwd) X(readlink) X(rm) X(sleep) X(sync) X(time) X(touch) X(true) X(unset) \
    X(uptime) X(version) X(wait)

/* The user and permission builtins that account.c implements. */
#define SHELL_ACCOUNT_NAMES(X) X(chmod) X(chown) X(id) X(whoami)

/* Every command name the shell resolves itself, for command completion. */
#define SHELL_COMPLETION_NAMES(X) SHELL_BUILTIN_NAMES(X) SHELL_ACCOUNT_NAMES(X)
