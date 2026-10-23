#include "mini_libc.h"

int main(void) {
    static const char stdout_msg[] = "stdionullcheck: stdout discarded\n";
    static const char stderr_msg[] = "stdionullcheck: stderr discarded\n";
    char buf[8];
    long got = read(0, buf, sizeof(buf));
    if (got < 0) {
        write(1, "stdionullcheck: stdin read failed\n", 33);
        return 1;
    }
    if (got != 0) {
        write(1, "stdionullcheck: stdin not eof\n", 30);
        return 2;
    }
    if (write(1, stdout_msg, sizeof(stdout_msg) - 1) != (long)(sizeof(stdout_msg) - 1)) {
        write(1, "stdionullcheck: stdout write failed\n", 36);
        return 3;
    }
    if (write(2, stderr_msg, sizeof(stderr_msg) - 1) != (long)(sizeof(stderr_msg) - 1)) {
        write(1, "stdionullcheck: stderr write failed\n", 36);
        return 4;
    }
    return 0;
}
