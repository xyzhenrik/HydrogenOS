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
    static const char expected[] = "tee payload\n";
    static const char ok[] = "teereader_a: tee ok\n";
    char buf[64];
    long got = read(0, buf, sizeof(buf) - 1);
    if (got <= 0) {
        return 1;
    }
    buf[got] = 0;
    if (!str_eq(buf, expected)) {
        return 2;
    }
    if (write(1, ok, sizeof(ok) - 1) != (long)(sizeof(ok) - 1)) {
        return 3;
    }
    return 0;
}
