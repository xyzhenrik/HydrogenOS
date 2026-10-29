#include "mini_libc.h"
#include "syscall.h"

#define PKG_MAGIC 0x48504B31u

static unsigned char pkg[4096];
static int pkg_len = 0;

static void put_u32(unsigned int v) {
    pkg[pkg_len++] = (unsigned char)(v & 0xFF);
    pkg[pkg_len++] = (unsigned char)((v >> 8) & 0xFF);
    pkg[pkg_len++] = (unsigned char)((v >> 16) & 0xFF);
    pkg[pkg_len++] = (unsigned char)((v >> 24) & 0xFF);
}

static void put_entry(const char *name, const char *data) {
    unsigned int nl = (unsigned int)strlen(name);
    unsigned int sz = (unsigned int)strlen(data);
    put_u32(nl);
    for (unsigned int i = 0; i < nl; ++i) pkg[pkg_len++] = (unsigned char)name[i];
    put_u32(sz);
    for (unsigned int i = 0; i < sz; ++i) pkg[pkg_len++] = (unsigned char)data[i];
}

static unsigned int get_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int main(void) {
    sys_mkdir("/installed");
    put_u32(PKG_MAGIC);
    put_u32(2);
    put_entry("hello.txt", "hello from a package\n");
    put_entry("notes.txt", "packaged notes\n");
    if (sys_writef("/demo.hpkg", pkg, (unsigned long)pkg_len) != 0) {
        write(1, "PKG FAIL write\n", 15);
        return 1;
    }
    unsigned char buf[4096];
    int fd = (int)open("/demo.hpkg", 0);
    if (fd < 0) { write(1, "PKG FAIL open\n", 14); return 1; }
    long n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0 || get_u32(buf) != PKG_MAGIC) {
        write(1, "PKG FAIL magic\n", 15);
        return 1;
    }
    unsigned int count = get_u32(buf + 4);
    int off = 8;
    for (unsigned int e = 0; e < count; ++e) {
        unsigned int nl = get_u32(buf + off); off += 4;
        char name[64];
        if (nl >= sizeof(name)) return 1;
        for (unsigned int i = 0; i < nl; ++i) name[i] = (char)buf[off + i];
        name[nl] = 0;
        off += (int)nl;
        unsigned int sz = get_u32(buf + off); off += 4;
        char path[96];
        const char *pre = "/installed/";
        int z = 0;
        while (pre[z]) { path[z] = pre[z]; ++z; }
        for (unsigned int i = 0; i < nl && z < 90; ++i) path[z++] = name[i];
        path[z] = 0;
        if (sys_writef(path, buf + off, sz) != 0) {
            write(1, "PKG FAIL install\n", 17);
            return 1;
        }
        off += (int)sz;
    }
    fd = (int)open("/installed/hello.txt", 0);
    if (fd < 0) { write(1, "PKG FAIL verify open\n", 21); return 1; }
    char vbuf[64];
    long vn = read(fd, vbuf, sizeof(vbuf) - 1);
    close(fd);
    if (vn <= 0) { write(1, "PKG FAIL verify read\n", 21); return 1; }
    vbuf[vn] = 0;
    if (strlen(vbuf) < 5 || vbuf[0] != 'h' || vbuf[1] != 'e') {
        write(1, "PKG FAIL verify data\n", 21);
        return 1;
    }
    /* package removal: unlink an installed entry and verify it is gone */
    if (sys_unlink("/installed/hello.txt") != 0) {
        write(1, "PKG FAIL remove\n", 16);
        return 1;
    }
    fd = (int)open("/installed/hello.txt", 0);
    if (fd >= 0) {
        close(fd);
        write(1, "PKG FAIL remove check\n", 22);
        return 1;
    }
    write(1, "PKG PASS\n", 9);
    return 0;
}
