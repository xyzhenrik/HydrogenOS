#include "builtins.h"

#include "diag.h"
#include "env.h"
#include "exec.h"
#include "jobs.h"
#include "mini_libc.h"
#include "output.h"
#include "signal.h"
#include "syscall.h"

enum {
    STATUS_FAILURE = 1,
    STATUS_USAGE = 2,
    STATUS_NOT_FOUND = 127,
    COPY_CHUNK = 2048,
    COPY_PATH_MAX = 256,
};

static char io_buffer[COPY_CHUNK];

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int usage(const char *name, const char *text) {
    out_error(name, 0, text);
    return STATUS_USAGE;
}

/* Parses a non-negative decimal; returns -1 for anything else. */
static long parse_decimal(const char *s) {
    long value = 0;
    if (!*s)
        return -1;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || value > 100000000L)
            return -1;
        value = value * 10 + (*s - '0');
    }
    return value;
}

static int builtin_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    out_str(1,
            "Builtins: cat cd cp echo env exit export false help jobs kill ln ls\n"
            "          mkdir mv pwd readlink rm sleep time touch true unset wait\n"
            "Diagnostics: pipe piperun pipenoinherit pipeblock pipefull\n"
            "          pipebroken pipeeof pollpipe pollwaitpipe pollmanypipe\n"
            "Syntax:   a | b   a && b   a || b   a ; b   a &\n"
            "          < file   > file   >> file   2> file   2>&1\n"
            "          'single' \"double $VAR\" \\escape  $VAR ${VAR} $?  NAME=value\n"
            "Other commands are looked up along $PATH (default /).\n");
    return 0;
}

static int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int first = 1;
    if (first < argc && str_eq(argv[first], "-n")) {
        newline = 0;
        first++;
    }
    for (int i = first; i < argc; ++i) {
        if (i > first)
            out_char(1, ' ');
        out_str(1, argv[i]);
    }
    if (newline)
        out_char(1, '\n');
    return 0;
}

static int copy_fd_to_stdout(int fd) {
    for (;;) {
        long n = read(fd, io_buffer, sizeof(io_buffer));
        if (n < 0)
            return -1;
        if (n == 0)
            return 0;
        out_write(1, io_buffer, (size_t)n);
    }
}

static int builtin_cat(int argc, char **argv) {
    int status = 0;
    if (argc < 2)
        return copy_fd_to_stdout(0) == 0 ? 0 : STATUS_FAILURE;
    for (int i = 1; i < argc; ++i) {
        long fd;
        if (str_eq(argv[i], "-")) {
            if (copy_fd_to_stdout(0) != 0) {
                out_error("cat", "-", "read failed");
                status = STATUS_FAILURE;
            }
            continue;
        }
        fd = open(argv[i], OPEN_READ);
        if (fd < 0) {
            out_error("cat", argv[i], "cannot open");
            status = STATUS_FAILURE;
            continue;
        }
        if (copy_fd_to_stdout((int)fd) != 0) {
            out_error("cat", argv[i], "read failed");
            status = STATUS_FAILURE;
        }
        close((int)fd);
    }
    return status;
}

/* Appends " -> target" if path is a symlink. */
static void print_link_target(const char *path) {
    char target[COPY_PATH_MAX];
    long length = sys_readlink(path, target, sizeof(target) - 1);
    if (length <= 0)
        return;
    target[length] = 0;
    out_str(1, " -> ");
    out_str(1, target);
}

static void list_directory(const char *path) {
    dirent_t entry;
    char full[COPY_PATH_MAX + DIRENT_NAME_MAX + 2];
    for (int pos = 0; readdir_pos(path, pos, &entry) == 1; ++pos) {
        out_str(1, entry.d_name);
        if (entry.d_type == 4)
            out_char(1, '/');
        if (entry.d_type == 10 &&
            snprintf(full, sizeof(full), "%s/%s", path, entry.d_name) < (int)sizeof(full))
            print_link_target(full);
        out_char(1, '\n');
    }
}

static int builtin_ls(int argc, char **argv) {
    int status = 0;
    if (argc < 2) {
        list_directory(".");
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        stat_t st;
        if (stat(argv[i], &st) != 0) {
            out_error("ls", argv[i], "no such file or directory");
            status = STATUS_FAILURE;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            out_str(1, argv[i]);
            print_link_target(argv[i]);
            out_char(1, '\n');
            continue;
        }
        if (argc > 2) {
            out_str(1, argv[i]);
            out_str(1, ":\n");
        }
        list_directory(argv[i]);
    }
    return status;
}

static int builtin_pwd(int argc, char **argv) {
    char buf[256];
    (void)argc;
    (void)argv;
    if (!getcwd(buf, sizeof(buf))) {
        out_error("pwd", 0, "cannot determine working directory");
        return STATUS_FAILURE;
    }
    out_str(1, buf);
    out_char(1, '\n');
    return 0;
}

static int builtin_cd(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : env_get("HOME", 4);
    if (argc > 2)
        return usage("cd", "usage: cd [directory]");
    if (!target || !*target)
        target = "/";
    if (chdir(target) != 0) {
        out_error("cd", target, "no such directory");
        return STATUS_FAILURE;
    }
    return 0;
}

static int builtin_mkdir(int argc, char **argv) {
    int status = 0;
    if (argc < 2)
        return usage("mkdir", "usage: mkdir <directory>...");
    for (int i = 1; i < argc; ++i) {
        if (sys_mkdir(argv[i]) != 0) {
            out_error("mkdir", argv[i], "cannot create directory");
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int builtin_rm(int argc, char **argv) {
    int status = 0;
    if (argc < 2)
        return usage("rm", "usage: rm <file>...");
    for (int i = 1; i < argc; ++i) {
        if (sys_unlink(argv[i]) != 0) {
            out_error("rm", argv[i], "cannot remove");
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int builtin_touch(int argc, char **argv) {
    int status = 0;
    if (argc < 2)
        return usage("touch", "usage: touch <file>...");
    for (int i = 1; i < argc; ++i) {
        long fd = open(argv[i], (int)(OPEN_WRITE | OPEN_CREATE));
        if (fd < 0) {
            out_error("touch", argv[i], "cannot create");
            status = STATUS_FAILURE;
            continue;
        }
        close((int)fd);
    }
    return status;
}

static int builtin_mv(int argc, char **argv) {
    if (argc != 3)
        return usage("mv", "usage: mv <source> <destination>");
    if (sys_rename(argv[1], argv[2]) != 0) {
        out_error("mv", argv[1], "cannot rename");
        return STATUS_FAILURE;
    }
    return 0;
}

static int builtin_ln(int argc, char **argv) {
    int symbolic = argc > 1 && str_eq(argv[1], "-s");
    int first = symbolic ? 2 : 1;
    if (argc - first != 2)
        return usage("ln", "usage: ln [-s] <target> <link>");
    long rc = symbolic ? sys_symlink(argv[first], argv[first + 1])
                       : sys_link(argv[first], argv[first + 1]);
    if (rc != 0) {
        out_error("ln", argv[first + 1],
                  symbolic ? "cannot create symbolic link" : "cannot create hard link");
        return STATUS_FAILURE;
    }
    return 0;
}

static int builtin_readlink(int argc, char **argv) {
    int status = 0;
    if (argc < 2)
        return usage("readlink", "usage: readlink <link>...");
    for (int i = 1; i < argc; ++i) {
        char target[COPY_PATH_MAX];
        long length = sys_readlink(argv[i], target, sizeof(target) - 1);
        if (length <= 0) {
            out_error("readlink", argv[i], "not a symbolic link");
            status = STATUS_FAILURE;
            continue;
        }
        target[length] = 0;
        out_str(1, target);
        out_char(1, '\n');
    }
    return status;
}

static int write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        long wrote = write(fd, buf, len);
        if (wrote <= 0)
            return -1;
        buf += wrote;
        len -= (size_t)wrote;
    }
    return 0;
}

/* Resolves path against the working directory and collapses ".", ".." and
 * repeated slashes into out. Returns -1 when it does not fit. */
static int canonical_path(const char *path, char *out, size_t cap) {
    char joined[2 * COPY_PATH_MAX];
    size_t len = 0;
    size_t out_len = 0;
    if (path[0] != '/') {
        if (!getcwd(joined, COPY_PATH_MAX))
            return -1;
        len = strlen(joined);
        if (len == 0 || joined[len - 1] != '/')
            joined[len++] = '/';
    }
    if (len + strlen(path) >= sizeof(joined))
        return -1;
    memcpy(joined + len, path, strlen(path) + 1);

    for (const char *p = joined; *p;) {
        size_t seg = 0;
        while (*p == '/')
            p++;
        while (p[seg] && p[seg] != '/')
            seg++;
        if (seg == 0)
            break;
        if (seg == 1 && p[0] == '.') {
            /* current directory: nothing to append */
        } else if (seg == 2 && p[0] == '.' && p[1] == '.') {
            while (out_len > 0 && out[out_len - 1] != '/')
                out_len--;
            if (out_len > 0)
                out_len--;
        } else {
            if (out_len + 1 + seg >= cap)
                return -1;
            out[out_len++] = '/';
            memcpy(out + out_len, p, seg);
            out_len += seg;
        }
        p += seg;
    }
    if (out_len == 0)
        out[out_len++] = '/';
    out[out_len] = 0;
    return 0;
}

static int same_path(const char *a, const char *b) {
    char canonical_a[COPY_PATH_MAX];
    char canonical_b[COPY_PATH_MAX];
    if (canonical_path(a, canonical_a, sizeof(canonical_a)) != 0 ||
        canonical_path(b, canonical_b, sizeof(canonical_b)) != 0)
        return 0;
    return str_eq(canonical_a, canonical_b);
}

static int builtin_cp(int argc, char **argv) {
    long src;
    long dst;
    int status = 0;
    if (argc != 3)
        return usage("cp", "usage: cp <source> <destination>");
    /* Opening the destination truncates it, which would empty the source. */
    if (same_path(argv[1], argv[2])) {
        out_error("cp", argv[2], "source and destination are the same file");
        return STATUS_FAILURE;
    }
    src = open(argv[1], OPEN_READ);
    if (src < 0) {
        out_error("cp", argv[1], "cannot open");
        return STATUS_FAILURE;
    }
    dst = open(argv[2], (int)(OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE));
    if (dst < 0) {
        out_error("cp", argv[2], "cannot create");
        close((int)src);
        return STATUS_FAILURE;
    }
    for (;;) {
        long n = read((int)src, io_buffer, sizeof(io_buffer));
        if (n == 0)
            break;
        if (n < 0) {
            out_error("cp", argv[1], "read failed");
            status = STATUS_FAILURE;
            break;
        }
        if (write_all((int)dst, io_buffer, (size_t)n) != 0) {
            out_error("cp", argv[2], "write failed");
            status = STATUS_FAILURE;
            break;
        }
    }
    close((int)dst);
    close((int)src);
    return status;
}

static int builtin_time(int argc, char **argv) {
    (void)argc;
    (void)argv;
    out_str(1, "time_ms: ");
    out_u64(1, time_ms());
    out_char(1, '\n');
    return 0;
}

static int builtin_sleep(int argc, char **argv) {
    long ms = 1000;
    if (argc > 2)
        return usage("sleep", "usage: sleep [milliseconds]");
    if (argc == 2) {
        ms = parse_decimal(argv[1]);
        if (ms < 0)
            return usage("sleep", "usage: sleep [milliseconds]");
    }
    out_flush();
    sleep_ms((unsigned long)ms);
    return 0;
}

static void print_export(const char *name, const char *value, void *context) {
    const char *prefix = (const char *)context;
    out_str(1, prefix);
    out_str(1, name);
    out_char(1, '=');
    out_str(1, value);
    out_char(1, '\n');
}

static int builtin_export(int argc, char **argv) {
    int status = 0;
    if (argc < 2) {
        env_for_each_exported(print_export, (void *)"export ");
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        size_t name_len = 0;
        while (argv[i][name_len] && argv[i][name_len] != '=')
            name_len++;
        if (!env_name_valid(argv[i], name_len)) {
            out_error("export", argv[i], "not a valid identifier");
            status = STATUS_FAILURE;
            continue;
        }
        if ((argv[i][name_len] == '=' &&
             env_set(argv[i], name_len, argv[i] + name_len + 1) != 0) ||
            env_export(argv[i], name_len) != 0) {
            out_error("export", argv[i], "cannot set variable");
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int builtin_unset(int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        size_t len = strlen(argv[i]);
        if (!env_name_valid(argv[i], len)) {
            out_error("unset", argv[i], "not a valid identifier");
            status = STATUS_FAILURE;
            continue;
        }
        env_unset(argv[i], len);
    }
    return status;
}

static int builtin_env(int argc, char **argv) {
    (void)argv;
    if (argc > 1)
        return usage("env", "usage: env");
    env_for_each_exported(print_export, (void *)"");
    return 0;
}

static int builtin_jobs(int argc, char **argv) {
    (void)argc;
    (void)argv;
    jobs_print(0);
    return 0;
}

static int builtin_wait(int argc, char **argv) {
    int status = 0;
    if (argc < 2) {
        jobs_wait(0, &status);
        return status;
    }
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        int is_job = arg[0] == '%';
        long id = parse_decimal(is_job ? arg + 1 : arg);
        int rc;
        if (id <= 0)
            return usage("wait", "usage: wait [%job | pid]...");
        rc = is_job ? jobs_wait((int)id, &status) : jobs_wait_pid((int)id, &status);
        if (rc != 0) {
            out_error("wait", arg, is_job ? "no such job" : "not a child of this shell");
            status = STATUS_NOT_FOUND;
        }
    }
    return status;
}

/* kill [-SIGNAL | -s SIGNAL] {pid | %job}... ; SIGNAL is a name or number,
   0 only checks that the process exists. */
static int builtin_kill(int argc, char **argv) {
    static const char kill_usage[] = "usage: kill [-SIGNAL | -s SIGNAL] {pid | %job}...";
    int sig = SIGTERM;
    int first = 1;
    int status = 0;

    if (first < argc && str_eq(argv[first], "-s")) {
        if (first + 1 >= argc)
            return usage("kill", kill_usage);
        sig = signal_number(argv[first + 1]);
        first += 2;
    } else if (first < argc && argv[first][0] == '-' && argv[first][1]) {
        sig = signal_number(argv[first] + 1);
        first++;
    }
    if (sig < 0) {
        out_error("kill", 0, "unknown signal");
        return STATUS_USAGE;
    }
    if (first >= argc)
        return usage("kill", kill_usage);

    for (int i = first; i < argc; ++i) {
        const char *arg = argv[i];
        int is_job = arg[0] == '%';
        long id = parse_decimal(is_job ? arg + 1 : arg);
        long rc;
        if (id <= 0) {
            out_error("kill", arg, "not a pid or job");
            status = STATUS_FAILURE;
            continue;
        }
        rc = is_job ? jobs_signal((int)id, sig) : kill((int)id, sig);
        if (rc != 0) {
            out_error("kill", arg, is_job ? "no such job" : "no such process");
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int builtin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

static int builtin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return STATUS_FAILURE;
}

static int builtin_exit(int argc, char **argv) {
    long status = exec_last_status();
    if (argc > 2)
        return usage("exit", "usage: exit [status]");
    if (argc == 2) {
        status = parse_decimal(argv[1]);
        if (status < 0 || status > 255)
            return usage("exit", "usage: exit [status 0-255]");
    }
    exec_request_exit();
    return (int)status;
}

typedef struct BuiltinEntry {
    const char *name;
    BuiltinFn fn;
} BuiltinEntry;

static const BuiltinEntry builtins[] = {
    {"cat", builtin_cat},
    {"cd", builtin_cd},
    {"cp", builtin_cp},
    {"echo", builtin_echo},
    {"env", builtin_env},
    {"exit", builtin_exit},
    {"export", builtin_export},
    {"false", builtin_false},
    {"help", builtin_help},
    {"jobs", builtin_jobs},
    {"kill", builtin_kill},
    {"ln", builtin_ln},
    {"ls", builtin_ls},
    {"mkdir", builtin_mkdir},
    {"mv", builtin_mv},
    {"pwd", builtin_pwd},
    {"readlink", builtin_readlink},
    {"rm", builtin_rm},
    {"sleep", builtin_sleep},
    {"time", builtin_time},
    {"touch", builtin_touch},
    {"true", builtin_true},
    {"unset", builtin_unset},
    {"wait", builtin_wait},
};

BuiltinFn builtin_find(const char *name) {
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (str_eq(builtins[i].name, name))
            return builtins[i].fn;
    }
    return diag_find(name);
}
