#include "mini_libc.h"
#include "syscall.h"

static int str_eq(const char *a, const char *b, int len) {
    for (int i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void fail(const char *reason) {
    /* "FSTEST FAIL: " + reason + "\n" */
    write(1, "FSTEST FAIL: ", 13);
    /* compute length */
    int len = 0;
    while (reason[len]) {
        ++len;
    }
    write(1, reason, (size_t)len);
    write(1, "\n", 1);
}

int main(void) {
    stat_t st;
    char buf[16];
    long fd;
    long n;
    const char data[] = "hello fs\n";

    /* 1. mkdir accept 0 or -17 */
    n = sys_mkdir("/fstestdir");
    if (n != 0 && n != -17) {
        fail("mkdir");
        return 1;
    }

    /* 2. writef */
    if (sys_writef("/fstest.txt", data, 9) != 0) {
        fail("writef");
        return 1;
    }

    /* 3. stat reg size 9 */
    if (stat("/fstest.txt", &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != 9) {
        fail("stat new");
        return 1;
    }

    /* 4. open + read */
    fd = open("/fstest.txt", 0);
    if (fd < 0) {
        fail("open");
        return 1;
    }
    n = read((int)fd, buf, sizeof(buf));
    close((int)fd);
    if (n != 9 || !str_eq(buf, data, 9)) {
        fail("read");
        return 1;
    }

    /* 5. rename */
    if (sys_rename("/fstest.txt", "/fstest2.txt") != 0) {
        fail("rename");
        return 1;
    }
    if (stat("/fstest.txt", &st) == 0) {
        fail("old exists");
        return 1;
    }
    if (stat("/fstest2.txt", &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != 9) {
        fail("new stat");
        return 1;
    }

    /* 6. unlink */
    if (sys_unlink("/fstest2.txt") != 0) {
        fail("unlink");
        return 1;
    }
    if (stat("/fstest2.txt", &st) == 0) {
        fail("unlink incomplete");
        return 1;
    }

    /* 7. mkdir idempotent */
    n = sys_mkdir("/fstestdir");
    if (n != 0 && n != -17) {
        fail("mkdir2");
        return 1;
    }

    /* 8. unlink nonexistent -> -2 */
    if (sys_unlink("/fstest_no_such_file.txt") != -2) {
        fail("unlink enoent");
        return 1;
    }

    write(1, "FSTEST PASS\n", 12);
    return 0;
}
