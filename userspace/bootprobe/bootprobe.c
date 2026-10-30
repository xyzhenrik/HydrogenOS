#include "mini_libc.h"

static void write_u64(unsigned long v) {
    char buf[24];
    int i = 0;
    if (v == 0) {
        write(1, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

int main(void) {
    static const char ok_prefix[] = "bootprobe: /bin/shell ok at ";
    long fd = open("/bin/shell", 0);
    if (fd < 0) {
        write(1, "bootprobe: open /bin/shell failed\n", 34);
        return 1;
    }

    close((int)fd);
    write(1, ok_prefix, sizeof(ok_prefix) - 1);
    write_u64(time_ms());
    write(1, " ms\n", 4);
    return 0;
}
