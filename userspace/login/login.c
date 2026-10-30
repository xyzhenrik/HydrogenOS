#include "mini_libc.h"
#include "passwd.h"
#include "syscall.h"

#define DEFAULT_ACCOUNT "root"
#define DEFAULT_SHELL "/bin/shell"
#define ENV_ENTRY_CAP (PASSWD_PATH_MAX + 8)
#define SHELL_ARGS_MAX 8

/* Drops to the account's group and user; the order matters, because only
 * root may still change the group afterwards. */
static int become(const passwd_entry_t *account) {
    return sys_setgid(account->gid) == 0 && sys_setuid(account->uid) == 0 ? 0 : -1;
}

/* Runs the account's shell with the given arguments as the foreground job
 * and returns its status. */
static int run_shell(const passwd_entry_t *account, int argc, char **argv) {
    const char *shell = account->shell[0] ? account->shell : DEFAULT_SHELL;
    char home[ENV_ENTRY_CAP];
    char user[PASSWD_NAME_MAX + 8];
    const char *shell_argv[SHELL_ARGS_MAX + 2] = {shell};
    const char *envp[] = {home, user, 0};
    int status = 1;
    long pid;

    for (int i = 0; i < argc; ++i)
        shell_argv[i + 1] = argv[i];

    snprintf(home, sizeof(home), "HOME=%s", account->home);
    snprintf(user, sizeof(user), "USER=%s", account->name);
    pid = spawn_args(shell, shell_argv, envp, SPAWN_STDIO_INHERIT, SPAWN_STDIO_INHERIT,
                     SPAWN_STDIO_INHERIT);
    if (pid < 0) {
        dprintf(2, "login: cannot start %s\n", shell);
        return 1;
    }
    if (wait_pid((int)pid, &status) != pid)
        return 1;
    return status;
}

/* login [user [shell-argument...]]: without arguments the session of the boot
 * sequence is checked against the root account; with a user, that account's
 * shell runs, given the remaining arguments. */
int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : DEFAULT_ACCOUNT;
    passwd_entry_t account;

    if (argc - 2 > SHELL_ARGS_MAX) {
        dprintf(2, "login: too many arguments\n");
        return 1;
    }
    if (passwd_find_name(name, &account) != 0) {
        dprintf(2, "login: unknown user %s\n", name);
        return 1;
    }
    if (become(&account) != 0) {
        dprintf(2, "login: cannot become %s\n", name);
        return 1;
    }
    dprintf(2, "login: %s\n", account.name);
    if (argc > 1)
        return run_shell(&account, argc > 2 ? argc - 2 : 0, argv + 2);
    write(1, "LOGIN PASS\n", 11);
    return 0;
}
