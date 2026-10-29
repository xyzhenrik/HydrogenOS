#include "mini_libc.h"
#include "syscall.h"

#define ERR_ENOENT  2
#define ERR_EEXIST  17
#define ERR_ENOTDIR 20
#define ERR_EISDIR  21
#define ERR_EINVAL  22
#define ERR_ELOOP   40

#define DT_LNK_TYPE 10
#define CHAIN_MAX   9

static const char *const cleanup_names[] = {
    "/ltree/file.txt", "/ltree/hard.txt", "/ltree/hard2.txt", "/ltree/fast",
    "/ltree/abs", "/ltree/slow", "/ltree/dlink", "/ltree/sub/inner.txt",
    "/ltree/sub/up", "/ltree/loop_a", "/ltree/loop_b",
    "/ltree/dangling", "/ltree/nowhere", "/ltree/echolink", "/ltree/renamed",
    "/ltree/hardlink_to_fast", "/ltree/chain1", "/ltree/chain2",
    "/ltree/chain3", "/ltree/chain4", "/ltree/chain5", "/ltree/chain6",
    "/ltree/chain7", "/ltree/chain8", "/ltree/chain9",
};

static const char *const cleanup_dirs[] = {
    "/ltree/sub/made", "/ltree/sub", "/ltree",
};

static int fail(const char *reason) {
    fprintf(2, "LINK FAIL: %s\n", reason);
    return 1;
}

static int str_same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Leftovers from an earlier, interrupted run on a persistent disk. */
static void remove_leftovers(void) {
    for (size_t i = 0; i < sizeof(cleanup_names) / sizeof(cleanup_names[0]); ++i)
        sys_unlink(cleanup_names[i]);
    for (size_t i = 0; i < sizeof(cleanup_dirs) / sizeof(cleanup_dirs[0]); ++i)
        sys_rmdir(cleanup_dirs[i]);
}

static int file_has(const char *path, const char *text) {
    char buf[32];
    size_t len = strlen(text);
    long fd = open(path, OPEN_READ);
    if (fd < 0)
        return 0;
    long n = read((int)fd, buf, sizeof(buf));
    close((int)fd);
    if (n != (long)len)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != text[i])
            return 0;
    }
    return 1;
}

static int link_target_is(const char *path, const char *expected) {
    char buf[300];
    long n = sys_readlink(path, buf, sizeof(buf) - 1);
    if (n != (long)strlen(expected))
        return 0;
    buf[n] = 0;
    return str_same(buf, expected);
}

static int is_symlink(const char *path) {
    stat_t st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

static int check_hard_links(void) {
    stat_t a, b;
    if (sys_writef("/ltree/file.txt", "hello", 5) != 0)
        return fail("create file");
    if (sys_link("/ltree/file.txt", "/ltree/hard.txt") != 0)
        return fail("link");
    if (stat("/ltree/file.txt", &a) != 0 || stat("/ltree/hard.txt", &b) != 0)
        return fail("stat after link");
    if (a.st_nlink != 2 || b.st_nlink != 2 || a.st_ino == 0 || a.st_ino != b.st_ino)
        return fail("link count or inode");
    if (!file_has("/ltree/hard.txt", "hello"))
        return fail("hard link content");
    if (sys_link("/ltree/file.txt", "/ltree/hard.txt") != -ERR_EEXIST)
        return fail("link onto existing name");
    if (sys_link("/ltree/missing", "/ltree/hard2.txt") != -ERR_ENOENT)
        return fail("link of missing file");
    if (sys_link("/ltree/sub", "/ltree/hard2.txt") != -ERR_EISDIR)
        return fail("link of directory");

    if (sys_writef("/ltree/hard.txt", "world!", 6) != 0)
        return fail("write through hard link");
    if (!file_has("/ltree/file.txt", "world!"))
        return fail("hard link shares data");

    if (sys_unlink("/ltree/file.txt") != 0)
        return fail("unlink original");
    if (stat("/ltree/hard.txt", &b) != 0 || b.st_nlink != 1)
        return fail("link count after unlink");
    if (!file_has("/ltree/hard.txt", "world!"))
        return fail("data survives unlink of one name");
    if (sys_unlink("/ltree/hard.txt") != 0)
        return fail("unlink last link");
    if (stat("/ltree/hard.txt", &b) != -ERR_ENOENT)
        return fail("file gone after last unlink");
    return 0;
}

static int check_symlinks(void) {
    stat_t st;
    if (sys_writef("/ltree/file.txt", "hello", 5) != 0)
        return fail("recreate file");

    if (sys_symlink("file.txt", "/ltree/fast") != 0)
        return fail("fast symlink");
    if (!is_symlink("/ltree/fast") || !link_target_is("/ltree/fast", "file.txt"))
        return fail("fast symlink target");
    if (stat("/ltree/fast", &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 5)
        return fail("stat follows relative symlink");
    if (!file_has("/ltree/fast", "hello"))
        return fail("open through relative symlink");

    if (sys_symlink("/ltree/file.txt", "/ltree/abs") != 0)
        return fail("absolute symlink");
    if (!file_has("/ltree/abs", "hello"))
        return fail("open through absolute symlink");

    /* A target longer than i_block[] is stored in a data block. */
    char slow_target[96];
    size_t len = 0;
    memcpy(slow_target, "/ltree/", 7);
    len = 7;
    for (int i = 0; i < 26; ++i) {
        slow_target[len++] = '.';
        slow_target[len++] = '/';
    }
    memcpy(slow_target + len, "file.txt", 9);
    if (strlen(slow_target) <= 60)
        return fail("slow target too short");
    if (sys_symlink(slow_target, "/ltree/slow") != 0)
        return fail("slow symlink");
    if (!link_target_is("/ltree/slow", slow_target))
        return fail("slow symlink target");
    if (lstat("/ltree/slow", &st) != 0 || st.st_size != (uint32_t)strlen(slow_target))
        return fail("slow symlink size");
    if (!file_has("/ltree/slow", "hello"))
        return fail("open through slow symlink");

    char small[4];
    if (sys_readlink("/ltree/fast", small, sizeof(small)) != (long)sizeof(small))
        return fail("readlink truncation");
    if (sys_readlink("/ltree/file.txt", small, sizeof(small)) != -ERR_EINVAL)
        return fail("readlink of regular file");
    if (sys_readlink("/ltree/missing", small, sizeof(small)) != -ERR_ENOENT)
        return fail("readlink of missing path");
    if (sys_symlink("x", "/ltree/fast") != -ERR_EEXIST)
        return fail("symlink onto existing name");
    if (sys_symlink("", "/ltree/empty") != -ERR_ENOENT)
        return fail("symlink with empty target");
    if (syscall3(SYS_STAT, (long)"/ltree/fast", (long)&st, 2) != -ERR_EINVAL)
        return fail("unknown stat flag");

    /* Hard links to a symlink share the link, not its target. */
    if (sys_link("/ltree/fast", "/ltree/hardlink_to_fast") != 0)
        return fail("hard link to symlink");
    if (lstat("/ltree/hardlink_to_fast", &st) != 0 || !S_ISLNK(st.st_mode) ||
        st.st_nlink != 2)
        return fail("hard link to symlink type");
    if (sys_unlink("/ltree/hardlink_to_fast") != 0)
        return fail("unlink hard link to symlink");

    /* Unlinking a symlink removes the link, never the target. */
    if (sys_unlink("/ltree/abs") != 0 || sys_unlink("/ltree/slow") != 0)
        return fail("unlink symlinks");
    if (lstat("/ltree/abs", &st) != -ERR_ENOENT || !file_has("/ltree/file.txt", "hello"))
        return fail("unlink symlink kept target");

    if (sys_rename("/ltree/fast", "/ltree/renamed") != 0)
        return fail("rename symlink");
    if (!is_symlink("/ltree/renamed") || !file_has("/ltree/renamed", "hello"))
        return fail("renamed symlink");
    if (sys_unlink("/ltree/renamed") != 0)
        return fail("unlink renamed symlink");
    return 0;
}

static int check_directory_links(void) {
    char cwd[64];
    if (sys_writef("/ltree/sub/inner.txt", "inner", 5) != 0)
        return fail("create inner");
    if (sys_symlink("sub", "/ltree/dlink") != 0)
        return fail("directory symlink");
    if (!file_has("/ltree/dlink/inner.txt", "inner"))
        return fail("path through directory symlink");
    if (sys_symlink("../file.txt", "/ltree/sub/up") != 0)
        return fail("dot-dot symlink");
    if (!file_has("/ltree/sub/up", "hello"))
        return fail("follow dot-dot symlink");

    stat_t st;
    dirent_t ent;
    int seen = 0;
    for (int pos = 0; readdir_pos("/ltree/dlink", pos, &ent) == 1; ++pos) {
        if (str_same(ent.d_name, "up")) {
            if (ent.d_type != DT_LNK_TYPE)
                return fail("readdir type of symlink");
            seen++;
        } else if (str_same(ent.d_name, "inner.txt")) {
            seen++;
        }
    }
    if (seen != 2)
        return fail("readdir through directory symlink");

    if (sys_mkdir("/ltree/dlink/made") != 0 || !is_symlink("/ltree/dlink") ||
        stat("/ltree/sub/made", &st) != 0)
        return fail("mkdir through directory symlink");
    if (sys_rmdir("/ltree/dlink") != -ERR_ENOTDIR || !is_symlink("/ltree/dlink"))
        return fail("rmdir of symlink");
    if (sys_rmdir("/ltree/dlink/made") != 0)
        return fail("rmdir through directory symlink");

    if (chdir("/ltree/dlink") != 0 || !getcwd(cwd, sizeof(cwd)) ||
        !str_same(cwd, "/ltree/sub")) {
        chdir("/");
        return fail("chdir through directory symlink");
    }
    int cwd_ok = file_has("inner.txt", "inner");
    chdir("/");
    if (!cwd_ok)
        return fail("relative open in linked cwd");
    return 0;
}

static int check_loops_and_dangling(void) {
    stat_t st;
    char name[32];
    char prev[32];
    if (sys_symlink("loop_b", "/ltree/loop_a") != 0 ||
        sys_symlink("loop_a", "/ltree/loop_b") != 0)
        return fail("create loop");
    if (stat("/ltree/loop_a", &st) != -ERR_ELOOP)
        return fail("stat of loop");
    if (open("/ltree/loop_a", OPEN_READ) != -ERR_ELOOP)
        return fail("open of loop");
    if (!is_symlink("/ltree/loop_a"))
        return fail("lstat of loop");

    /* A chain of VFS_SYMLINK_DEPTH_MAX links resolves, one more does not. */
    snprintf(prev, sizeof(prev), "file.txt");
    for (int i = 1; i <= CHAIN_MAX; ++i) {
        snprintf(name, sizeof(name), "/ltree/chain%d", i);
        if (sys_symlink(prev, name) != 0)
            return fail("create chain");
        snprintf(prev, sizeof(prev), "chain%d", i);
        if (i == 8 && !file_has(name, "hello"))
            return fail("chain of eight");
    }
    if (stat("/ltree/chain9", &st) != -ERR_ELOOP)
        return fail("chain of nine");

    if (sys_symlink("nowhere", "/ltree/dangling") != 0)
        return fail("dangling symlink");
    if (stat("/ltree/dangling", &st) != -ERR_ENOENT || !is_symlink("/ltree/dangling"))
        return fail("dangling stat");
    if (open("/ltree/dangling", OPEN_READ) != -ERR_ENOENT)
        return fail("dangling open");
    long fd = open("/ltree/dangling", (int)(OPEN_WRITE | OPEN_CREATE));
    if (fd < 0)
        return fail("create through dangling symlink");
    close((int)fd);
    if (!is_symlink("/ltree/dangling") || stat("/ltree/nowhere", &st) != 0)
        return fail("create made target");
    return 0;
}

static int check_spawn(void) {
    static const char *const argv[] = { "echo", 0 };
    if (sys_symlink("/echo", "/ltree/echolink") != 0)
        return fail("symlink to program");
    long pid = sys_spawn_args("/ltree/echolink", argv, 0, SPAWN_STDIO_NULL,
                              SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    if (pid <= 0)
        return fail("spawn through symlink");
    int status = -1;
    if (sys_wait((int)pid, &status) != pid || status != 0)
        return fail("spawned program status");
    return 0;
}

int main(void) {
    remove_leftovers();
    if (sys_mkdir("/ltree") != 0)
        return fail("mkdir ltree");
    if (sys_mkdir("/ltree/sub") != 0)
        return fail("mkdir sub");
    int rc = check_hard_links();
    if (rc == 0)
        rc = check_symlinks();
    if (rc == 0)
        rc = check_directory_links();
    if (rc == 0)
        rc = check_loops_and_dangling();
    if (rc == 0)
        rc = check_spawn();
    remove_leftovers();
    if (rc != 0)
        return rc;
    stat_t st;
    if (stat("/ltree", &st) != -ERR_ENOENT)
        return fail("cleanup");
    printf("LINK PASS\n");
    return 0;
}
