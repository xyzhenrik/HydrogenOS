#include "mini_libc.h"
#include "syscall.h"

enum {
    EEXIST_ERRNO = 17,
    ENOTDIR_ERRNO = 20,
};

#define CWDCHECK_DIR "/tmp/cwdcheck.d"
/* An installed binary: a directory must not be creatable or enterable there. */
#define CWDCHECK_BINARY "/bin/time"

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

static int bytes_eq(const char *a, const char *b, int len) {
    for (int i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static const char payload[] = "cwd ok\n";
    char cwd[64];
    char got[16];
    stat_t st;
    long fd;
    long n;
    long pid;
    int status = 0;

    if (!getcwd(cwd, sizeof(cwd)) || !str_eq(cwd, "/")) {
        write(1, "cwdcheck: root cwd failed\n", 26);
        return 1;
    }

    if (sys_mkdir(CWDCHECK_BINARY) != -EEXIST_ERRNO) {
        write(1, "cwdcheck: mkdir over binary not refused\n", 40);
        return 19;
    }
    if (chdir(CWDCHECK_BINARY) != -ENOTDIR_ERRNO) {
        write(1, "cwdcheck: chdir into binary not refused\n", 40);
        return 20;
    }

    sys_rmdir(CWDCHECK_DIR);
    if (sys_mkdir(CWDCHECK_DIR) != 0) {
        write(1, "cwdcheck: mkdir failed\n", 23);
        return 2;
    }
    if (stat(CWDCHECK_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        write(1, "cwdcheck: stat dir failed\n", 26);
        return 21;
    }

    if (chdir(CWDCHECK_DIR) != 0) {
        write(1, "cwdcheck: chdir abs failed\n", 27);
        return 3;
    }
    if (!getcwd(cwd, sizeof(cwd)) || !str_eq(cwd, CWDCHECK_DIR)) {
        write(1, "cwdcheck: cwd after abs failed\n", 31);
        return 4;
    }

    if (sys_writef("note.txt", payload, sizeof(payload) - 1) != 0) {
        write(1, "cwdcheck: writef rel failed\n", 28);
        return 5;
    }

    fd = open("note.txt", 0);
    if (fd < 0) {
        write(1, "cwdcheck: open rel failed\n", 26);
        return 6;
    }
    n = read((int)fd, got, sizeof(got));
    close((int)fd);
    if (n != (long)(sizeof(payload) - 1) ||
        !bytes_eq(got, payload, (int)(sizeof(payload) - 1))) {
        write(1, "cwdcheck: read rel failed\n", 26);
        return 7;
    }

    if (stat("note.txt", &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != (unsigned long)(sizeof(payload) - 1)) {
        write(1, "cwdcheck: stat rel failed\n", 26);
        return 8;
    }

    fd = open("../../bin/time", 0);
    if (fd < 0) {
        write(1, "cwdcheck: parent path failed\n", 29);
        return 9;
    }
    close((int)fd);

    if (sys_rename("note.txt", "renamed.txt") != 0) {
        write(1, "cwdcheck: rename rel failed\n", 28);
        return 10;
    }
    if (stat("note.txt", &st) == 0) {
        write(1, "cwdcheck: old name still exists\n", 32);
        return 11;
    }
    if (stat("renamed.txt", &st) != 0 || !S_ISREG(st.st_mode)) {
        write(1, "cwdcheck: renamed stat failed\n", 30);
        return 12;
    }

    if (sys_unlink("renamed.txt") != 0) {
        write(1, "cwdcheck: unlink rel failed\n", 28);
        return 13;
    }
    if (stat("renamed.txt", &st) == 0) {
        write(1, "cwdcheck: unlink incomplete\n", 28);
        return 14;
    }

    if (chdir("../..") != 0) {
        write(1, "cwdcheck: chdir parent failed\n", 30);
        return 15;
    }
    if (!getcwd(cwd, sizeof(cwd)) || !str_eq(cwd, "/")) {
        write(1, "cwdcheck: cwd after parent failed\n", 34);
        return 16;
    }

    if (sys_rmdir(CWDCHECK_DIR) != 0) {
        write(1, "cwdcheck: rmdir failed\n", 23);
        return 22;
    }

    pid = spawn_with_stdio("bin/time", SPAWN_STDIO_INHERIT, SPAWN_STDIO_NULL, SPAWN_STDIO_INHERIT);
    if (pid < 0) {
        write(1, "cwdcheck: spawn rel failed\n", 27);
        return 17;
    }
    if (wait_pid((int)pid, &status) != pid || status != 0) {
        write(1, "cwdcheck: spawn wait failed\n", 28);
        return 18;
    }

    write(1, "cwdcheck: cwd ok\n", 17);
    return 0;
}
