#include "exec.h"

#include "builtins.h"
#include "env.h"
#include "expand.h"
#include "interrupt.h"
#include "jobs.h"
#include "lexer.h"
#include "mini_libc.h"
#include "output.h"
#include "parser.h"
#include "signal.h"
#include "shell_limits.h"

enum {
    STATUS_FAILURE = 1,
    STATUS_USAGE = 2,
    STATUS_CANNOT_EXECUTE = 126,
    STATUS_NOT_FOUND = 127,
    STATUS_INTERRUPTED = 130,
};

typedef struct PreparedRedirect {
    uint8_t kind;
    uint8_t fd;
    const char *target;
} PreparedRedirect;

/* A command after expansion: argv strings and redirection targets live in
 * storage. */
typedef struct PreparedCommand {
    char *argv[SHELL_MAX_ARGS + 1];
    int argc;
    int assignment_only;
    PreparedRedirect redirects[SHELL_MAX_REDIRECTS];
    int redirect_count;
    size_t used;
    char storage[SHELL_EXPAND_CAP];
} PreparedCommand;

/* The child's view of stdin/stdout/stderr as fds of this shell, plus the
 * files opened for it that must be closed once it is set up. */
typedef struct StdioPlan {
    int fds[3];
    int opened[SHELL_MAX_REDIRECTS];
    int opened_count;
} StdioPlan;

static int last_status = 0;
static int exit_requested = 0;
static int interactive = 0;
static int terminal_host = 0;

static ShellTokenList tokens;
static ShellProgram program;
static PreparedCommand prepared[SHELL_MAX_STAGES];
static char envp_storage[SHELL_EXPAND_CAP];
static const char *envp_vector[ENV_MAX_VARIABLES + 1];
/* One byte of slack for the terminator: the child shell rejects logical
 * lines longer than SHELL_LINE_MAX. */
static char subshell_command[SHELL_LINE_MAX + 1];
static char resolved_path[SHELL_PATH_MAX];

int exec_last_status(void) { return last_status; }
void exec_request_exit(void) { exit_requested = 1; }
int exec_exit_requested(void) { return exit_requested; }
void exec_set_interactive(int value) { interactive = value; }
void exec_set_terminal_host(int value) { terminal_host = value; }
int exec_terminal_host(void) { return terminal_host; }

static int expand_token(const char *line, const ShellToken *token,
                        PreparedCommand *cmd, char **out,
                        ShellExpansion *result) {
    const char *error = 0;
    char *dst = cmd->storage + cmd->used;
    if (expand_word(line + token->start, token->length, last_status, dst,
                    SHELL_EXPAND_CAP - cmd->used, result, &error) != 0) {
        out_error("shell", 0, error);
        return -1;
    }
    cmd->used += result->length + 1;
    *out = dst;
    return 0;
}

static int raw_is_assignment(const char *line, const ShellToken *token) {
    const char *raw = line + token->start;
    size_t name_len = 0;
    while (name_len < token->length && raw[name_len] != '=')
        name_len++;
    return name_len < token->length && env_name_valid(raw, name_len);
}

static int prepare_command(const char *line, const ShellCommand *src,
                           PreparedCommand *cmd) {
    int assignments = 0;
    cmd->argc = 0;
    cmd->redirect_count = 0;
    cmd->used = 0;
    for (int i = 0; i < src->word_count; ++i) {
        const ShellToken *token = &tokens.tokens[src->words[i]];
        ShellExpansion result;
        char *word;
        if (expand_token(line, token, cmd, &word, &result) != 0)
            return -1;
        /* An unquoted word that expands to nothing is dropped, so that
         * "echo $UNSET x" passes a single argument. */
        if (result.length == 0 && !result.quoted)
            continue;
        if (raw_is_assignment(line, token))
            assignments++;
        cmd->argv[cmd->argc++] = word;
    }
    cmd->argv[cmd->argc] = 0;
    cmd->assignment_only = cmd->argc > 0 && assignments == cmd->argc;

    for (int i = 0; i < src->redirect_count; ++i) {
        const ShellRedirect *redirect = &src->redirects[i];
        PreparedRedirect *dst = &cmd->redirects[cmd->redirect_count++];
        ShellExpansion result;
        char *target;
        if (expand_token(line, &tokens.tokens[redirect->target], cmd, &target,
                         &result) != 0)
            return -1;
        if (redirect->fd > 2) {
            out_error("shell", 0, "only file descriptors 0, 1 and 2 can be redirected");
            return -1;
        }
        if (result.length == 0) {
            out_error("shell", 0, "ambiguous redirect");
            return -1;
        }
        if (redirect->kind == REDIRECT_DUPLICATE &&
            (result.length != 1 || target[0] < '0' || target[0] > '2')) {
            out_error("shell", target, "bad file descriptor");
            return -1;
        }
        dst->kind = redirect->kind;
        dst->fd = redirect->fd;
        dst->target = target;
    }
    return 0;
}

static void stdio_plan_init(StdioPlan *plan, int in, int out, int err) {
    plan->fds[0] = in;
    plan->fds[1] = out;
    plan->fds[2] = err;
    plan->opened_count = 0;
}

static void stdio_plan_close(StdioPlan *plan) {
    for (int i = 0; i < plan->opened_count; ++i)
        close(plan->opened[i]);
    plan->opened_count = 0;
}

/* Applies redirections left to right, so "> f 2>&1" and "2>&1 > f" differ
 * exactly as in other shells. */
static int stdio_plan_apply(StdioPlan *plan, const PreparedCommand *cmd) {
    for (int i = 0; i < cmd->redirect_count; ++i) {
        const PreparedRedirect *redirect = &cmd->redirects[i];
        unsigned int flags;
        long fd;
        if (redirect->kind == REDIRECT_DUPLICATE) {
            plan->fds[redirect->fd] = plan->fds[redirect->target[0] - '0'];
            continue;
        }
        if (redirect->kind == REDIRECT_INPUT)
            flags = OPEN_READ;
        else if (redirect->kind == REDIRECT_APPEND)
            flags = OPEN_WRITE | OPEN_CREATE | OPEN_APPEND;
        else
            flags = OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE;
        fd = open(redirect->target, (int)flags);
        if (fd < 0) {
            out_error("shell", redirect->target, "cannot open");
            stdio_plan_close(plan);
            return -1;
        }
        plan->opened[plan->opened_count++] = (int)fd;
        plan->fds[redirect->fd] = (int)fd;
    }
    return 0;
}

static int assign_variables(int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        size_t name_len = 0;
        while (argv[i][name_len] != '=')
            name_len++;
        if (env_set(argv[i], name_len, argv[i] + name_len + 1) != 0) {
            out_error("shell", argv[i], "cannot set variable");
            return STATUS_FAILURE;
        }
    }
    return 0;
}

static void restore_stdio(const StdioPlan *plan, const int saved[3]) {
    for (int i = 0; i < 3; ++i) {
        if (saved[i] < 0)
            continue;
        if (plan->fds[i] != i)
            dup2(saved[i], i);
        close(saved[i]);
    }
}

/* Runs fn inside the shell with the command's redirections temporarily
 * installed on fds 0-2. fn may be null for a command without words. */
static int run_in_shell(BuiltinFn fn, PreparedCommand *cmd) {
    StdioPlan plan;
    int saved[3] = {-1, -1, -1};
    int redirected;
    int status = 0;
    stdio_plan_init(&plan, 0, 1, 2);
    if (stdio_plan_apply(&plan, cmd) != 0)
        return STATUS_FAILURE;
    redirected = plan.fds[0] != 0 || plan.fds[1] != 1 || plan.fds[2] != 2;
    if (redirected)
        out_pause_color(1);
    if (redirected && fn) {
        out_flush();
        /* Save all three first: a target such as 2>&1 refers to the fd as
         * it was before any slot was replaced. */
        for (int i = 0; i < 3; ++i) {
            long fd = dup(i);
            if (fd < 0) {
                out_error("shell", 0, "cannot save standard streams");
                restore_stdio(&plan, saved);
                stdio_plan_close(&plan);
                out_pause_color(0);
                return STATUS_FAILURE;
            }
            saved[i] = (int)fd;
        }
        for (int i = 0; i < 3; ++i) {
            int src = plan.fds[i];
            if (src == i)
                continue;
            if (src >= 0 && src < 3)
                src = saved[src];
            if (dup2(src, i) < 0) {
                restore_stdio(&plan, saved);
                stdio_plan_close(&plan);
                out_pause_color(0);
                out_error("shell", 0, "cannot redirect standard streams");
                return STATUS_FAILURE;
            }
        }
    }
    if (fn)
        status = fn(cmd->argc, cmd->argv);
    out_flush();
    restore_stdio(&plan, saved);
    stdio_plan_close(&plan);
    out_pause_color(0);
    /* A builtin cut short by Ctrl+C leaves the cursor mid-line, where a
     * killed child would have the shell end it. */
    if (fn && interrupt_pending()) {
        out_char(2, '\n');
        out_flush();
    }
    return status;
}

static int is_regular_file(const char *path) {
    stat_t st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int contains_slash(const char *s) {
    for (; *s; ++s) {
        if (*s == '/')
            return 1;
    }
    return 0;
}

/* Looks name up along $PATH (default "/") into resolved_path. */
static int resolve_external(const char *name) {
    size_t name_len = strlen(name);
    const char *dirs;
    if (contains_slash(name)) {
        if (name_len >= sizeof(resolved_path))
            return -1;
        memcpy(resolved_path, name, name_len + 1);
        return is_regular_file(resolved_path) ? 0 : -1;
    }
    dirs = env_get("PATH", 4);
    if (!dirs || !*dirs)
        dirs = "/";
    while (*dirs) {
        size_t dir_len = 0;
        size_t len;
        while (dirs[dir_len] && dirs[dir_len] != ':')
            dir_len++;
        len = dir_len;
        if (dir_len == 0) {
            resolved_path[len++] = '.';
        } else if (dir_len < sizeof(resolved_path)) {
            memcpy(resolved_path, dirs, dir_len);
        }
        if (len + 1 + name_len < sizeof(resolved_path)) {
            if (resolved_path[len - 1] != '/')
                resolved_path[len++] = '/';
            memcpy(resolved_path + len, name, name_len + 1);
            if (is_regular_file(resolved_path))
                return 0;
        }
        dirs += dir_len;
        if (*dirs == ':')
            dirs++;
    }
    return -1;
}

/* Appends text to subshell_command in single quotes, so that a child
 * shell's "-c" sees it as one word again; embedded quotes become '\''.
 * Fails when the result would exceed what the child shell accepts. */
static int append_quoted(size_t *len, const char *text, size_t text_len) {
    if (*len + 2 > SHELL_LINE_MAX)
        return -1;
    subshell_command[(*len)++] = '\'';
    for (size_t i = 0; i < text_len; ++i) {
        if (text[i] == '\'') {
            if (*len + 4 + 1 > SHELL_LINE_MAX)
                return -1;
            memcpy(subshell_command + *len, "'\\''", 4);
            *len += 4;
        } else {
            if (*len + 1 + 1 > SHELL_LINE_MAX)
                return -1;
            subshell_command[(*len)++] = text[i];
        }
    }
    subshell_command[(*len)++] = '\'';
    subshell_command[*len] = 0;
    return 0;
}

/* Runs a builtin in a child by re-quoting its argv as a "shell -c" line. */
static int build_subshell_command(const PreparedCommand *cmd) {
    size_t len = 0;
    for (int i = 0; i < cmd->argc; ++i) {
        if (i > 0) {
            if (len + 1 > SHELL_LINE_MAX)
                return -1;
            subshell_command[len++] = ' ';
        }
        if (append_quoted(&len, cmd->argv[i], strlen(cmd->argv[i])) != 0)
            return -1;
    }
    subshell_command[len] = 0;
    return 0;
}

/* Starts path with the plan's stdio and the exported environment, closing the
 * plan's files either way. Returns the pid, or 0 with the status in *status;
 * failures are reported on the child's stderr under name. */
static long spawn_process(const char *name, const char *path,
                          const char *const *argv, StdioPlan *plan,
                          int *status) {
    long pid;
    if (env_build_envp(envp_storage, sizeof(envp_storage), envp_vector,
                       sizeof(envp_vector) / sizeof(envp_vector[0])) != 0) {
        out_error("shell", name, "environment too large");
        stdio_plan_close(plan);
        *status = STATUS_FAILURE;
        return 0;
    }
    pid = spawn_args(path, argv, envp_vector, plan->fds[0], plan->fds[1],
                     plan->fds[2]);
    if (pid <= 0)
        out_error_to(plan->fds[2], "shell", name, "cannot execute");
    stdio_plan_close(plan);
    if (pid <= 0) {
        *status = STATUS_CANNOT_EXECUTE;
        return 0;
    }
    return pid;
}

/* Starts one pipeline stage as a child process. Returns its pid, or 0 when
 * no process was started; *status then holds the stage's exit status. */
static long spawn_command(PreparedCommand *cmd, int in, int out, int err,
                          int *status) {
    static const char *subshell_argv[4];
    const char *path;
    const char *const *argv;
    StdioPlan plan;

    *status = 0;
    stdio_plan_init(&plan, in, out, err);
    if (stdio_plan_apply(&plan, cmd) != 0) {
        *status = STATUS_FAILURE;
        return 0;
    }
    /* A redirect-only command just creates its files; assignments in a
     * child would be lost, so they are not run at all. */
    if (cmd->argc == 0 || cmd->assignment_only) {
        stdio_plan_close(&plan);
        return 0;
    }
    if (builtin_find(cmd->argv[0])) {
        if (build_subshell_command(cmd) != 0) {
            out_error("shell", cmd->argv[0], "argument list too long");
            stdio_plan_close(&plan);
            *status = STATUS_FAILURE;
            return 0;
        }
        subshell_argv[0] = "shell";
        subshell_argv[1] = "-c";
        subshell_argv[2] = subshell_command;
        subshell_argv[3] = 0;
        path = SHELL_SELF_PATH;
        argv = subshell_argv;
    } else if (resolve_external(cmd->argv[0]) == 0) {
        path = resolved_path;
        argv = (const char *const *)cmd->argv;
    } else {
        /* Reported on the command's stderr, as its own exec would. */
        out_error_to(plan.fds[2], "shell", cmd->argv[0], "command not found");
        stdio_plan_close(&plan);
        *status = STATUS_NOT_FOUND;
        return 0;
    }
    return spawn_process(cmd->argv[0], path, argv, &plan, status);
}

static void announce_job(int id, const int *pids, int count) {
    out_char(2, '[');
    out_u64(2, (unsigned long)id);
    out_char(2, ']');
    for (int i = 0; i < count; ++i) {
        out_char(2, ' ');
        out_u64(2, (unsigned long)pids[i]);
    }
    out_char(2, '\n');
    out_flush();
}

/* Tells the user how a foreground process died, as other shells do. */
static void report_signal_death(int status) {
    int sig = WTERMSIG(status);
    const char *description = signal_description(sig);
    if (sig != SIGINT)
        out_str(2, description);
    out_char(2, '\n');
    out_flush();
}

static int wait_all(const int *pids, int count, long status_pid) {
    int result = 0;
    interrupt_set_targets(pids, count);
    for (int i = 0; i < count; ++i) {
        int status = 0;
        if (wait_pid(pids[i], &status) < 0) {
            status = STATUS_NOT_FOUND;
        } else if (WIFSIGNALED(status)) {
            report_signal_death(status);
        }
        interrupt_target_reaped(pids[i]);
        if (pids[i] == status_pid)
            result = jobs_exit_code(status);
    }
    interrupt_set_targets(0, 0);
    return result;
}

/* Records pids as a job, or waits for them when the job table is full.
 * Returns the status the shell reports for the command line. */
static int register_background(const char *line, const ShellPipeline *pipeline,
                               const int *pids, int count, long status_pid,
                               int fallback_status) {
    int id = jobs_add(pids, count, line + pipeline->source_start,
                      (size_t)(pipeline->source_end - pipeline->source_start));
    if (id < 0) {
        out_error("shell", 0, "too many jobs; waiting in the foreground");
        return status_pid > 0 ? wait_all(pids, count, status_pid)
                              : fallback_status;
    }
    if (interactive)
        announce_job(id, pids, count);
    return 0;
}

/* Runs "a && b || c &" as a single job. The list executes in a child shell,
 * so it sees the exported environment and starts with $? at 0; shell-local
 * variables are not carried over. */
static int run_background_list(const char *line, const ShellPipeline *first) {
    static const char *subshell_argv[4];
    size_t len = (size_t)(first->source_end - first->source_start);
    StdioPlan plan;
    int status = 0;
    int pid;
    memcpy(subshell_command, line + first->source_start, len);
    subshell_command[len] = 0;
    subshell_argv[0] = "shell";
    subshell_argv[1] = "-c";
    subshell_argv[2] = subshell_command;
    subshell_argv[3] = 0;

    out_flush();
    stdio_plan_init(&plan, SPAWN_STDIO_NULL, 1, 2);
    pid = (int)spawn_process("background list", SHELL_SELF_PATH, subshell_argv,
                             &plan, &status);
    if (pid <= 0)
        return status;
    return register_background(line, first, &pid, 1, pid, 0);
}

static int run_pipeline(const char *line, const ShellPipeline *pipeline) {
    int pids[SHELL_MAX_STAGES];
    int pid_count = 0;
    int count = pipeline->command_count;
    int input;
    long last_pid = 0;
    int last_spawn_status = 0;

    for (int i = 0; i < count; ++i) {
        if (prepare_command(line, &pipeline->commands[i], &prepared[i]) != 0)
            return STATUS_FAILURE;
    }

    if (!pipeline->background && count == 1) {
        PreparedCommand *cmd = &prepared[0];
        BuiltinFn fn;
        if (cmd->argc == 0)
            return run_in_shell(0, cmd);
        if (cmd->assignment_only)
            return run_in_shell(assign_variables, cmd);
        fn = builtin_find(cmd->argv[0]);
        if (fn)
            return run_in_shell(fn, cmd);
    }

    out_flush();
    /* Background jobs must not compete with the shell for the console. */
    input = pipeline->background ? SPAWN_STDIO_NULL : 0;
    for (int i = 0; i < count; ++i) {
        int pipe_fds[2] = {-1, -1};
        int last = i == count - 1;
        int status = 0;
        long pid;
        if (!last && pipe(pipe_fds) != 0) {
            out_error("shell", 0, "cannot create pipe");
            if (input >= 3)
                close(input);
            wait_all(pids, pid_count, 0);
            return STATUS_FAILURE;
        }
        pid = spawn_command(&prepared[i], input, last ? 1 : pipe_fds[1], 2,
                            &status);
        /* Drop the shell's copies right away: readers only see EOF once
         * every write end is closed. */
        if (input >= 3)
            close(input);
        if (!last)
            close(pipe_fds[1]);
        input = pipe_fds[0];
        if (pid > 0)
            pids[pid_count++] = (int)pid;
        if (last) {
            last_pid = pid;
            last_spawn_status = status;
        }
    }

    if (pipeline->background && pid_count > 0)
        return register_background(line, pipeline, pids, pid_count, last_pid,
                                   last_spawn_status);
    if (last_pid <= 0) {
        wait_all(pids, pid_count, 0);
        return last_spawn_status;
    }
    return wait_all(pids, pid_count, last_pid);
}

int exec_line(const char *line) {
    const char *error = 0;
    if (lexer_tokenize(line, &tokens, &error) != 0 ||
        parser_parse(&tokens, &program, &error) != 0) {
        out_error("shell", 0, error);
        last_status = STATUS_USAGE;
        return last_status;
    }
    for (int i = 0; i < program.pipeline_count; ++i) {
        const ShellPipeline *pipeline = &program.pipelines[i];
        if (interrupt_pending()) {
            last_status = STATUS_INTERRUPTED;
            break;
        }
        if (pipeline->connector == CONNECT_IF_SUCCESS && last_status != 0)
            continue;
        if (pipeline->connector == CONNECT_IF_FAILURE && last_status == 0)
            continue;
        if (pipeline->background && pipeline->list_length > 1) {
            last_status = run_background_list(line, pipeline);
            i += pipeline->list_length - 1;
        } else {
            last_status = run_pipeline(line, pipeline);
        }
        if (exit_requested)
            break;
    }
    if (interrupt_pending())
        last_status = STATUS_INTERRUPTED;
    return last_status;
}
