#include "mini_libc.h"
#include "syscall.h"

static int read_all(const char *path, char *buf, int cap) {
    int fd = (int)open(path, 0);
    if (fd < 0) return -1;
    long n = read(fd, buf, (size_t)(cap - 1));
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

int main(void) {
    char buf[512];
    if (read_all("/etc/passwd", buf, sizeof(buf)) <= 0) {
        write(2, "login: no /etc/passwd\n", 22);
        return 1;
    }
    char name[32];
    int i = 0;
    while (buf[i] && buf[i] != ':' && i < 31) { name[i] = buf[i]; ++i; }
    name[i] = 0;
    if (i == 0) return 1;
    write(2, "login: ", 7);
    write(2, name, strlen(name));
    write(2, "\n", 1);
    write(1, "LOGIN PASS\n", 11);
    return 0;
}
