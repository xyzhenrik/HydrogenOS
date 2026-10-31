#include "mini_libc.h"

static void write_hex_byte(unsigned char v) {
    static const char digits[] = "0123456789ABCDEF";
    char out[3];
    out[0] = digits[(v >> 4) & 0xF];
    out[1] = digits[v & 0xF];
    out[2] = ' ';
    write(1, out, sizeof(out));
}

static void write_hex_dump(const unsigned char *buf, unsigned long len) {
    for (unsigned long i = 0; i < len; ++i) {
        write_hex_byte(buf[i]);
        if ((i & 15) == 15) {
            write(1, "\n", 1);
        }
    }
    if ((len & 15) != 0) {
        write(1, "\n", 1);
    }
}

int main(void) {
    const char *path = "/bin/hello";
    unsigned char buf[64];

    long fd = open(path, 0);
    if (fd < 0) {
        write(1, "cat: open failed\n", 17);
        return 1;
    }

    long n = read((int)fd, buf, sizeof(buf));
    if (n <= 0) {
        write(1, "cat: read failed\n", 17);
        close((int)fd);
        return 1;
    }

    write(1, "cat: first bytes of /bin/hello:\n", 32);
    write_hex_dump(buf, (unsigned long)n);
    close((int)fd);
    return 0;
}
