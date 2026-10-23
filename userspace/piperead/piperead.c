#include "mini_libc.h"

int main(void) {
    char buf[128];
    int saw_eof = 0;
    for (int fd = 3; fd < 11; ++fd) {
        long got = read(fd, buf, sizeof(buf) - 1);
        if (got < 0) {
            continue;
        }
        if (got == 0) {
            saw_eof = 1;
            continue;
        }
        buf[got] = 0;
        write(1, "piperead: ", 10);
        write(1, buf, (size_t)got);
        write(1, "\n", 1);
        return 0;
    }
    if (saw_eof) {
        write(1, "piperead: eof\n", 14);
        return 0;
    }
    write(1, "piperead: no inherited pipe data\n", 33);
    return 1;
}
