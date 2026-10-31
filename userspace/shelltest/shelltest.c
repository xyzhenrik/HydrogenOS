#include "mini_libc.h"

/* Boot test for /shell: runs scripts through "shell -c" with stdout and
 * stderr captured in one pipe and compares output and exit status. */

typedef struct ShellCase {
    const char *name;
    const char *script;
    const char *expected;
    int expected_status;
} ShellCase;

enum {
    OUTPUT_CAP = 768,
    REPORT_CAP = 160,
};

static const ShellCase cases[] = {
    {"redirect",
     "echo first > /tmp/shelltest.txt\n"
     "echo second >> /tmp/shelltest.txt\n"
     "cat < /tmp/shelltest.txt\n"
     "echo third 1>/tmp/shelltest.txt; cat /tmp/shelltest.txt\n"
     "> /tmp/shelltest.txt; cat /tmp/shelltest.txt\n"
     "rm /tmp/shelltest.txt\n",
     "first\nsecond\nthird\n", 0},
    {"pipeline",
     "echo piped through three | cat | cat\n"
     "echo external | /bin/echo\n",
     "piped through three\n"
     "echo: type a line and press Enter\nexternal\n", 0},
    {"env",
     "GREETING=hello\n"
     "echo \"$GREETING, ${GREETING}s\" '$GREETING' $SHELLTEST_IMPORTED $UNSET\n"
     "export GREETING\n"
     "env | cat\n"
     "unset GREETING\n"
     "echo \"[$GREETING]\"\n",
     "hello, hellos $GREETING yes\n"
     "SHELLTEST_IMPORTED=yes\nPATH=/bin:/sbin\nGREETING=hello\n"
     "[]\n", 0},
    {"status",
     "false; echo $?\n"
     "true; echo ${?}\n"
     "missing_command 2> /tmp/shelltest.err; echo $?\n"
     "cat /tmp/shelltest.err\n"
     "cat /tmp/shelltest.none 2>&1 | cat\n"
     "false && echo wrong || echo fallback\n"
     "rm /tmp/shelltest.err\n",
     "1\n0\n127\n"
     "shell: missing_command: command not found\n"
     "cat: /tmp/shelltest.none: cannot open\n"
     "fallback\n", 0},
    {"path",
     "hello\n"
     "PATH=/sbin\n"
     "hello 2>&1; echo $?\n"
     "PATH=/bin:/sbin\n"
     "hello\n",
     "Hello from user mode via sys_write!\n"
     "shell: hello: command not found\n127\n"
     "Hello from user mode via sys_write!\n", 0},
    {"jobs",
     "echo background > /tmp/shelltest.bg &\n"
     "wait\n"
     "cat /tmp/shelltest.bg\n"
     "jobs\n"
     "sleep 300 &\n"
     "jobs\n"
     "wait %1; echo $?\n"
     "jobs\n"
     "rm /tmp/shelltest.bg\n",
     "background\n[1] Running  sleep 300\n0\n", 0},
    {"joblist",
     "sleep 300 && echo listed > /tmp/shelltest.bgl &\n"
     "jobs\n"
     "wait\n"
     "cat /tmp/shelltest.bgl\n"
     "rm /tmp/shelltest.bgl\n",
     "[1] Running  sleep 300 && echo listed > /tmp/shelltest.bgl\nlisted\n", 0},
    {"continuation",
     "echo con\\\ntinued \\\n  two \"x\\\ny\"\n",
     "continued two xy\n", 0},
    {"copy",
     "echo data > /tmp/shelltest.cp\n"
     "cp /tmp/shelltest.cp /./tmp/shelltest.cp 2>&1\n"
     "cat /tmp/shelltest.cp\n"
     "rm /tmp/shelltest.cp\n",
     "cp: /./tmp/shelltest.cp: source and destination are the same file\ndata\n", 0},
    {"quoting",
     "echo a\\ b \"c \\\"d\\\" \\$e\" 'f  g' \"\" x # comment\n",
     "a b c \"d\" $e f  g  x\n", 0},
    {"braces",
     "echo {a,b}x pre{1,2}post x{a,b}{c,d} {a,b{c,d}}\n"
     "echo {a,b\n"
     "echo {a} {} a{,b} \\{x,y\\} '{x,y}' \"{x,y}\" ${SHELLTEST_IMPORTED}{1,2}\n"
     "GREETING={a,b}; echo $GREETING\n"
     "echo {a,b}{c,d}{e,f}{g,h}{i,j}{k,l}\n",
     "ax bx pre1post pre2post xac xad xbc xbd a bc bd\n"
     "{a,b\n"
     "{a} {} a ab {x,y} {x,y} {x,y} yes1 yes2\n"
     "{a,b}\n"
     "shell: too many arguments\n", 1},
    {"kill",
     "sleep 300 &\n"
     "kill %1\n"
     "wait %1; echo $?\n"
     "sleep 300 &\n"
     "kill -s KILL %1\n"
     "wait %1; echo $?\n"
     "kill 99999 2>&1; echo $?\n",
     "143\n137\nkill: 99999: no such process\n1\n", 0},
    {"exec",
     "echo via exec | exec /bin/echo\n"
     "exec /missing_program 2>&1; echo $?\n"
     "echo before\n"
     "exec /bin/echo\n"
     "echo never\n",
     "echo: type a line and press Enter\nvia exec\n"
     "exec: /missing_program: command not found\n127\n"
     "before\necho: type a line and press Enter\n", 0},
    {"accounts",
     "id; whoami\n"
     "echo data > /tmp/shelltest.acl\n"
     "chmod 640 /tmp/shelltest.acl; ls -l /tmp/shelltest.acl\n"
     "chmod u+x,go-r /tmp/shelltest.acl; ls -l /tmp/shelltest.acl\n"
     "chown user:user /tmp/shelltest.acl; ls -l /tmp/shelltest.acl\n"
     "chmod 1777 /tmp/shelltest.acl 2>&1; echo $?\n"
     "chown nobody /tmp/shelltest.acl 2>&1; echo $?\n"
     "rm /tmp/shelltest.acl\n",
     "uid=0(root) gid=0(root)\nroot\n"
     "-rw-r----- 1 root root 5 /tmp/shelltest.acl\n"
     "-rwx------ 1 root root 5 /tmp/shelltest.acl\n"
     "-rwx------ 1 user user 5 /tmp/shelltest.acl\n"
     "chmod: 1777: invalid mode\n2\n"
     "chown: nobody: invalid owner\n2\n", 0},
    {"exit",
     "echo before; exit 3; echo after\n"
     "echo never\n",
     "before\n", 3},
};

static const char *const shell_env[] = {"SHELLTEST_IMPORTED=yes", 0};

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void report_failure(const ShellCase *c, const char *output, int status) {
    char report[REPORT_CAP];
    size_t len = 0;
    const char *prefix = "SHELL FAIL ";
    while (*prefix && len + 1 < sizeof(report))
        report[len++] = *prefix++;
    for (const char *p = c->name; *p && len + 1 < sizeof(report); ++p)
        report[len++] = *p;
    if (len + 1 < sizeof(report))
        report[len++] = ':';
    for (const char *p = output; *p && len + 3 < sizeof(report); ++p) {
        if (*p == '\n') {
            report[len++] = '\\';
            report[len++] = 'n';
        } else {
            report[len++] = *p;
        }
    }
    report[len] = 0;
    dprintf(2, "%s (status %d)\n", report, status);
}

static int run_case(const ShellCase *c) {
    static char output[OUTPUT_CAP];
    const char *argv[] = {"shell", "-c", c->script, 0};
    int fds[2] = {-1, -1};
    size_t used = 0;
    int status = -1;
    long pid;

    if (pipe(fds) != 0) {
        dprintf(2, "SHELL FAIL %s: pipe\n", c->name);
        return -1;
    }
    pid = spawn_args("/bin/shell", argv, shell_env, SPAWN_STDIO_NULL, fds[1], fds[1]);
    close(fds[1]);
    if (pid <= 0) {
        close(fds[0]);
        dprintf(2, "SHELL FAIL %s: spawn %d\n", c->name, (int)pid);
        return -1;
    }
    for (;;) {
        long n = read(fds[0], output + used, sizeof(output) - 1 - used);
        if (n <= 0)
            break;
        used += (size_t)n;
        if (used == sizeof(output) - 1)
            break;
    }
    output[used] = 0;
    close(fds[0]);
    if (wait_pid((int)pid, &status) < 0) {
        dprintf(2, "SHELL FAIL %s: wait\n", c->name);
        return -1;
    }
    if (status != c->expected_status || !str_eq(output, c->expected)) {
        report_failure(c, output, status);
        return -1;
    }
    return 0;
}

int main(void) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (run_case(&cases[i]) != 0)
            return 1;
    }
    printf("SHELL PASS\n");
    return 0;
}
