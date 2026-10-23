#include "mini_libc.h"

int main(void) {
    char buf[128];
    long got = read(0, buf, sizeof(buf) - 1);
    if (got < 0) {
        write(1, "handoffcheck: stdin read failed\n", 32);
        return 1;
    }
    if (got == 0) {
        write(1, "handoffcheck: eof\n", 18);
        return 1;
    }
    buf[got] = 0;
    write(1, "handoffcheck: ", 14);
    write(1, buf, (size_t)got);
    return 0;
}
