#include "mini_libc.h"

static int bytes_eq(const char *a, const char *b, int len) {
    for (int i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    char got[12];
    char expect[12];
    long fd;
    long alias_fd;
    long control_fd;
    long n;

    fd = open("/time", 0);
    if (fd < 0) {
        write(1, "fileoffsetcheck: open failed\n", 29);
        return 1;
    }
    alias_fd = dup((int)fd);
    if (alias_fd < 0) {
        close((int)fd);
        write(1, "fileoffsetcheck: dup failed\n", 28);
        return 2;
    }
    control_fd = open("/time", 0);
    if (control_fd < 0) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "fileoffsetcheck: reopen failed\n", 31);
        return 3;
    }

    n = read((int)fd, got, 4);
    if (n != 4) {
        close((int)control_fd);
        close((int)alias_fd);
        close((int)fd);
        write(1, "fileoffsetcheck: read 1 failed\n", 31);
        return 4;
    }
    n = read((int)alias_fd, got + 4, 4);
    if (n != 4) {
        close((int)control_fd);
        close((int)alias_fd);
        close((int)fd);
        write(1, "fileoffsetcheck: read 2 failed\n", 31);
        return 5;
    }
    n = read((int)fd, got + 8, 4);
    if (n != 4) {
        close((int)control_fd);
        close((int)alias_fd);
        close((int)fd);
        write(1, "fileoffsetcheck: read 3 failed\n", 31);
        return 6;
    }
    n = read((int)control_fd, expect, sizeof(expect));
    close((int)control_fd);
    close((int)alias_fd);
    close((int)fd);
    if (n != (long)sizeof(expect)) {
        write(1, "fileoffsetcheck: control read failed\n", 37);
        return 7;
    }
    if (!bytes_eq(got, expect, (int)sizeof(expect))) {
        write(1, "fileoffsetcheck: shared offset failed\n", 38);
        return 8;
    }

    write(1, "fileoffsetcheck: offset ok\n", 27);
    return 0;
}
