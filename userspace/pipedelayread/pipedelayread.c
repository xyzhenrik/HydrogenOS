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
    char fill[512];
    char tail[8];
    for (int fd = 3; fd < 11; ++fd) {
        long first = read(fd, fill, sizeof(fill));
        if (first < 0) {
            continue;
        }
        if (first != (long)sizeof(fill)) {
            write(1, "pipedelayread: first read ", 26);
            write_u64((unsigned long)first);
            write(1, "\n", 1);
            return 1;
        }
        long second = read(fd, tail, sizeof(tail));
        if (second < 0) {
            write(1, "pipedelayread: second read failed\n", 34);
            return 1;
        }
        if (second == 4 &&
            tail[0] == 'T' &&
            tail[1] == 'A' &&
            tail[2] == 'I' &&
            tail[3] == 'L') {
            write(1, "pipedelayread: tail ok, bytes ", 30);
            write_u64((unsigned long)(first + second));
            write(1, "\n", 1);
            return 0;
        }
        write(1, "pipedelayread: tail read ", 25);
        write_u64((unsigned long)second);
        write(1, "\n", 1);
        return 1;
    }
    write(1, "pipedelayread: no inherited pipe data\n", 38);
    return 1;
}
