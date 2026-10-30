#include "mini_libc.h"

static int str_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int expect_dup_alias(void) {
    static const char payload[] = "dup alias payload\n";
    int fds[2] = { -1, -1 };
    char buf[64];
    long alias_fd;
    long got;

    if (pipe(fds) != 0) {
        return 1;
    }
    alias_fd = dup(fds[0]);
    if (alias_fd < 0) {
        close(fds[0]);
        close(fds[1]);
        return 2;
    }
    close(fds[0]);
    if (write(fds[1], payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close((int)alias_fd);
        close(fds[1]);
        return 3;
    }
    close(fds[1]);
    got = read((int)alias_fd, buf, sizeof(buf) - 1);
    close((int)alias_fd);
    if (got != (long)(sizeof(payload) - 1)) {
        return 4;
    }
    buf[got] = 0;
    return str_eq(buf, payload) ? 0 : 5;
}

static int expect_dup2_stdin(void) {
    static const char payload[] = "dup2 stdin payload\n";
    int fds[2] = { -1, -1 };
    char buf[64];
    long got;

    if (pipe(fds) != 0) {
        return 10;
    }
    if (dup2(fds[0], 0) != 0) {
        close(fds[0]);
        close(fds[1]);
        return 11;
    }
    close(fds[0]);
    if (write(fds[1], payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close(fds[1]);
        return 12;
    }
    close(fds[1]);
    got = read(0, buf, sizeof(buf) - 1);
    if (got != (long)(sizeof(payload) - 1)) {
        return 13;
    }
    buf[got] = 0;
    return str_eq(buf, payload) ? 0 : 14;
}

static int expect_dup2_stdout_restore(void) {
    static const char payload[] = "dup2 stdout payload\n";
    int fds[2] = { -1, -1 };
    char buf[64];
    long saved_stdout;
    long got;

    if (pipe(fds) != 0) {
        return 20;
    }
    saved_stdout = dup(1);
    if (saved_stdout < 0) {
        close(fds[0]);
        close(fds[1]);
        return 21;
    }
    if (dup2(fds[1], 1) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        close(fds[1]);
        return 22;
    }
    close(fds[1]);
    if (write(1, payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close((int)saved_stdout);
        close(fds[0]);
        return 23;
    }
    if (dup2((int)saved_stdout, 1) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        return 24;
    }
    close((int)saved_stdout);
    got = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (got != (long)(sizeof(payload) - 1)) {
        return 25;
    }
    buf[got] = 0;
    return str_eq(buf, payload) ? 0 : 26;
}

static int expect_dup_implicit_stdout(void) {
    long pid;
    int status = -1;

    pid = spawn_with_stdio("/lib/tests/dupfallbackcheck",
                           SPAWN_STDIO_INHERIT,
                           SPAWN_STDIO_CLEAR,
                           SPAWN_STDIO_CLEAR);
    if (pid < 0) {
        return 30;
    }
    if (wait_pid((int)pid, &status) != pid) {
        return 31;
    }
    return status == 0 ? 0 : 32;
}

static int expect_dup3_inherit_handle(void) {
    static const char payload[] = "dup3 inherit payload\n";
    int fds[2] = { -1, -1 };
    int status = -1;
    long pid;

    if (pipe(fds) != 0) {
        return 40;
    }
    if (dup3(fds[0], 5, DUP3_INHERIT) != 5) {
        close(fds[0]);
        close(fds[1]);
        return 41;
    }
    close(fds[0]);
    if (write(fds[1], payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close(5);
        close(fds[1]);
        return 42;
    }
    close(fds[1]);
    pid = spawn_with_stdio("/lib/tests/piperead",
                           SPAWN_STDIO_INHERIT,
                           SPAWN_STDIO_NULL,
                           SPAWN_STDIO_NULL);
    if (pid < 0) {
        close(5);
        return 43;
    }
    if (wait_pid((int)pid, &status) != pid) {
        close(5);
        return 44;
    }
    close(5);
    return status == 0 ? 0 : 45;
}

static int expect_dup3_close_on_spawn_handle(void) {
    static const char payload[] = "dup3 hidden payload\n";
    int fds[2] = { -1, -1 };
    int status = -1;
    long pid;

    if (pipe(fds) != 0) {
        return 50;
    }
    if (dup3(fds[0], 5, DUP3_CLOSE_ON_SPAWN) != 5) {
        close(fds[0]);
        close(fds[1]);
        return 51;
    }
    close(fds[0]);
    if (write(fds[1], payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close(5);
        close(fds[1]);
        return 52;
    }
    close(fds[1]);
    pid = spawn_with_stdio("/lib/tests/piperead",
                           SPAWN_STDIO_INHERIT,
                           SPAWN_STDIO_NULL,
                           SPAWN_STDIO_NULL);
    if (pid < 0) {
        close(5);
        return 53;
    }
    if (wait_pid((int)pid, &status) != pid) {
        close(5);
        return 54;
    }
    close(5);
    return status == 1 ? 0 : 55;
}

static int expect_dup3_close_on_spawn_stdout(void) {
    int fds[2] = { -1, -1 };
    char buf[64];
    long saved_stdout;
    long pid;
    int status = -1;
    long got;

    if (pipe(fds) != 0) {
        return 60;
    }
    saved_stdout = dup(1);
    if (saved_stdout < 0) {
        close(fds[0]);
        close(fds[1]);
        return 61;
    }
    if (dup3(fds[1], 1, DUP3_CLOSE_ON_SPAWN) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        close(fds[1]);
        return 62;
    }
    close(fds[1]);
    pid = spawn("/bin/time");
    if (pid < 0) {
        dup2((int)saved_stdout, 1);
        close((int)saved_stdout);
        close(fds[0]);
        return 63;
    }
    if (wait_pid((int)pid, &status) != pid || status != 0) {
        dup2((int)saved_stdout, 1);
        close((int)saved_stdout);
        close(fds[0]);
        return 64;
    }
    if (dup2((int)saved_stdout, 1) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        return 65;
    }
    close((int)saved_stdout);
    got = read(fds[0], buf, sizeof(buf));
    close(fds[0]);
    return got == 0 ? 0 : 66;
}

int main(void) {
    if (expect_dup_alias() != 0) {
        write(1, "dupcheck: dup alias failed\n", 27);
        return 1;
    }
    if (expect_dup2_stdin() != 0) {
        write(1, "dupcheck: dup2 stdin failed\n", 28);
        return 2;
    }
    if (expect_dup2_stdout_restore() != 0) {
        write(1, "dupcheck: dup2 stdout failed\n", 29);
        return 3;
    }
    if (expect_dup_implicit_stdout() != 0) {
        write(1, "dupcheck: implicit stdout failed\n", 33);
        return 4;
    }
    if (expect_dup3_inherit_handle() != 0) {
        write(1, "dupcheck: dup3 inherit failed\n", 30);
        return 5;
    }
    if (expect_dup3_close_on_spawn_handle() != 0) {
        write(1, "dupcheck: dup3 close handle failed\n", 35);
        return 6;
    }
    if (expect_dup3_close_on_spawn_stdout() != 0) {
        write(1, "dupcheck: dup3 close stdout failed\n", 35);
        return 7;
    }
    write(1, "dupcheck: dup ok\n", 17);
    return 0;
}
