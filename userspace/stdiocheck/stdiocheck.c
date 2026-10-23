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

int main(void) {
    static const char expected[] = "init stdio ok\n";
    static const char ok_stdout[] = "stdiocheck: stdout ok\n";
    static const char ok_stderr[] = "stdiocheck: stderr ok\n";
    char buf[64];
    long got = read(0, buf, sizeof(buf) - 1);
    if (got < 0) {
        write(2, "stdiocheck: stdin read failed\n", 30);
        return 1;
    }
    if (got == 0) {
        write(2, "stdiocheck: stdin eof\n", 22);
        return 1;
    }
    buf[got] = 0;
    if (!str_eq(buf, expected)) {
        write(2, "stdiocheck: unexpected stdin\n", 29);
        return 2;
    }
    if (write(1, ok_stdout, sizeof(ok_stdout) - 1) != (long)(sizeof(ok_stdout) - 1)) {
        write(2, "stdiocheck: stdout write failed\n", 32);
        return 3;
    }
    if (write(2, ok_stderr, sizeof(ok_stderr) - 1) != (long)(sizeof(ok_stderr) - 1)) {
        return 4;
    }
    return 0;
}
