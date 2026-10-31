#include "mini_libc.h"
#include "syscall.h"

#define ERR_ENOENT    2
#define ERR_EBUSY     16
#define ERR_EEXIST    17
#define ERR_ENOTDIR   20
#define ERR_EISDIR    21
#define ERR_EINVAL    22
#define ERR_ENOTEMPTY 39

#define DT_DIR_TYPE 4
#define DT_REG_TYPE 8

#define MANY_COUNT 64
#define FS_TYPE_EXT2 1

static const char *const cleanup_files[] = {
    "/tmp/dtree/a/b/c/leaf.txt",
    "/tmp/dtree/c/leaf.txt",
    "/tmp/dtree/a/note.txt",
    "/tmp/dtree/r1",
    "/tmp/dtree/r2",
};

static const char *const cleanup_dirs[] = {
    "/tmp/dtree/a/b/c",
    "/tmp/dtree/c",
    "/tmp/dtree/a/b",
    "/tmp/dtree/a",
    "/tmp/dtree/many",
    "/tmp/dtree",
};

static int fail(const char *reason) {
    dprintf(2, "DIRTEST FAIL: %s\n", reason);
    return 1;
}

static int str_same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void many_name(char *buf, size_t cap, int index, int short_form) {
    if (short_form) {
        snprintf(buf, cap, "/tmp/dtree/many/s%d", index);
    } else {
        snprintf(buf, cap, "/tmp/dtree/many/entry-with-a-rather-long-name-%d", index);
    }
}

/* Leftovers from an earlier, interrupted run on a persistent disk. */
static void remove_leftovers(void) {
    char path[96];
    for (int i = 0; i < MANY_COUNT; ++i) {
        many_name(path, sizeof(path), i, 0);
        sys_unlink(path);
        many_name(path, sizeof(path), i, 1);
        sys_unlink(path);
    }
    for (size_t i = 0; i < sizeof(cleanup_files) / sizeof(cleanup_files[0]); ++i)
        sys_unlink(cleanup_files[i]);
    for (size_t i = 0; i < sizeof(cleanup_dirs) / sizeof(cleanup_dirs[0]); ++i)
        sys_rmdir(cleanup_dirs[i]);
}

static int is_dir(const char *path) {
    stat_t st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int exists(const char *path) {
    stat_t st;
    return stat(path, &st) == 0;
}

static int file_has(const char *path, const char *text) {
    char buf[32];
    size_t len = strlen(text);
    long fd = open(path, 0);
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

static int count_entries(const char *path) {
    dirent_t ent;
    int count = 0;
    for (;;) {
        int rc = readdir_pos(path, count, &ent);
        if (rc < 0)
            return -1;
        if (rc == 0)
            return count;
        count++;
    }
}

/* /dtree/a must list exactly ".", "..", "b" (dir) and "note.txt" (file). */
static int check_listing(void) {
    dirent_t ent;
    int seen_dot = 0, seen_dotdot = 0, seen_b = 0, seen_note = 0;
    int pos = 0;
    for (;;) {
        int rc = readdir_pos("/tmp/dtree/a", pos, &ent);
        if (rc < 0)
            return fail("readdir error");
        if (rc == 0)
            break;
        pos++;
        if (str_same(ent.d_name, ".")) {
            seen_dot++;
        } else if (str_same(ent.d_name, "..")) {
            seen_dotdot++;
        } else if (str_same(ent.d_name, "b")) {
            if (ent.d_type != DT_DIR_TYPE)
                return fail("readdir type of b");
            seen_b++;
        } else if (str_same(ent.d_name, "note.txt")) {
            if (ent.d_type != DT_REG_TYPE)
                return fail("readdir type of note.txt");
            seen_note++;
        } else {
            return fail("readdir unexpected entry");
        }
    }
    if (seen_dot != 1 || seen_dotdot != 1 || seen_b != 1 || seen_note != 1)
        return fail("readdir listing");
    return 0;
}

static int check_many(void) {
    char path[96];
    sysinfo_t info;
    if (sys_mkdir("/tmp/dtree/many") != 0)
        return fail("mkdir many");
    for (int i = 0; i < MANY_COUNT; ++i) {
        many_name(path, sizeof(path), i, 0);
        if (sys_writef(path, "x", 1) != 0)
            return fail("create many entry");
    }
    if (count_entries("/tmp/dtree/many") != MANY_COUNT + 2)
        return fail("many count after create");

    stat_t st;
    if (sys_sysinfo(&info) != 0 || stat("/tmp/dtree/many", &st) != 0)
        return fail("many stat");
    if (st.st_size <= info.block_size || st.st_size % info.block_size != 0)
        return fail("many did not grow to several blocks");

    for (int i = 0; i < MANY_COUNT; i += 2) {
        many_name(path, sizeof(path), i, 0);
        if (sys_unlink(path) != 0)
            return fail("unlink even entry");
    }
    if (count_entries("/tmp/dtree/many") != MANY_COUNT / 2 + 2)
        return fail("many count after unlink");
    for (int i = 0; i < MANY_COUNT; i += 2) {
        many_name(path, sizeof(path), i, 1);
        if (sys_writef(path, "y", 1) != 0)
            return fail("reuse freed records");
    }
    if (count_entries("/tmp/dtree/many") != MANY_COUNT + 2)
        return fail("many count after reuse");
    stat_t st2;
    if (stat("/tmp/dtree/many", &st2) != 0 || st2.st_size != st.st_size)
        return fail("freed records not reused");
    if (sys_rmdir("/tmp/dtree/many") != -ERR_ENOTEMPTY)
        return fail("rmdir many not empty");

    for (int i = 0; i < MANY_COUNT; ++i) {
        many_name(path, sizeof(path), i, (i % 2) == 0);
        if (sys_unlink(path) != 0)
            return fail("unlink many entry");
    }
    if (count_entries("/tmp/dtree/many") != 2)
        return fail("many not empty after unlink");
    if (sys_rmdir("/tmp/dtree/many") != 0)
        return fail("rmdir many");
    if (exists("/tmp/dtree/many"))
        return fail("many still exists");
    return 0;
}

static int check_rename(void) {
    if (sys_rename("/tmp/dtree/a/b/c", "/tmp/dtree/c") != 0)
        return fail("rename dir across parents");
    if (exists("/tmp/dtree/a/b/c") || !is_dir("/tmp/dtree/c"))
        return fail("rename dir result");
    if (!file_has("/tmp/dtree/c/leaf.txt", "leaf\n"))
        return fail("moved dir lost its file");
    if (sys_rename("/tmp/dtree/a", "/tmp/dtree/a/b/inside") != -ERR_EINVAL)
        return fail("rename dir into itself");
    if (sys_rename("/tmp/dtree/c", "/tmp/dtree/a") != -ERR_ENOTEMPTY)
        return fail("rename onto non-empty dir");
    if (sys_rename("/tmp/dtree/a/note.txt", "/tmp/dtree/c") != -ERR_EISDIR)
        return fail("rename file onto dir");

    if (sys_writef("/tmp/dtree/r1", "one", 3) != 0 || sys_writef("/tmp/dtree/r2", "two", 3) != 0)
        return fail("create rename files");
    if (sys_rename("/tmp/dtree/r1", "/tmp/dtree/r2") != 0)
        return fail("rename replace");
    if (exists("/tmp/dtree/r1") || !file_has("/tmp/dtree/r2", "one"))
        return fail("rename replace result");
    if (sys_unlink("/tmp/dtree/r2") != 0)
        return fail("unlink r2");
    return 0;
}

int main(void) {
    sysinfo_t info;
    if (sys_sysinfo(&info) != 0)
        return fail("sysinfo");
    if (info.fs_type != FS_TYPE_EXT2) {
        dprintf(2, "dirtest: root is not ext2, skipping\n");
        printf("DIRTEST PASS\n");
        return 0;
    }

    remove_leftovers();

    if (sys_mkdir("/tmp/dtree") != 0)
        return fail("mkdir /dtree");
    if (sys_mkdir("/tmp/dtree") != -ERR_EEXIST)
        return fail("mkdir existing");
    if (sys_mkdir("/tmp/dtree/a") != 0 || sys_mkdir("/tmp/dtree/a/b") != 0 ||
        sys_mkdir("/tmp/dtree/a/b/c") != 0)
        return fail("mkdir nested");
    if (sys_mkdir("/tmp/dtree/missing/child") != -ERR_ENOENT)
        return fail("mkdir without parent");
    if (!is_dir("/tmp/dtree/a") || !is_dir("/tmp/dtree/a/b") || !is_dir("/tmp/dtree/a/b/c"))
        return fail("stat nested");

    if (sys_writef("/tmp/dtree/a/b/c/leaf.txt", "leaf\n", 5) != 0 ||
        sys_writef("/tmp/dtree/a/note.txt", "note\n", 5) != 0)
        return fail("writef nested");
    if (!file_has("/tmp/dtree/a/b/c/leaf.txt", "leaf\n") || !file_has("/tmp/dtree/a/note.txt", "note\n"))
        return fail("read nested");
    if (sys_mkdir("/tmp/dtree/a/note.txt/sub") != -ERR_ENOTDIR)
        return fail("mkdir below file");
    if (check_listing() != 0)
        return 1;
    if (count_entries("/tmp/dtree/absent") != -1)
        return fail("readdir missing dir");

    if (sys_rmdir("/tmp/dtree/a") != -ERR_ENOTEMPTY)
        return fail("rmdir non-empty");
    if (sys_rmdir("/tmp/dtree/a/note.txt") != -ERR_ENOTDIR)
        return fail("rmdir file");
    if (sys_unlink("/tmp/dtree/a/b") != -ERR_EISDIR)
        return fail("unlink dir");
    if (sys_rmdir("/tmp/dtree/absent") != -ERR_ENOENT)
        return fail("rmdir missing");
    if (sys_rmdir("/") != -ERR_EBUSY)
        return fail("rmdir root");
    if (!is_dir("/tmp/dtree/a/b"))
        return fail("unlink damaged dir");

    if (check_many() != 0)
        return 1;
    if (check_rename() != 0)
        return 1;

    if (sys_unlink("/tmp/dtree/c/leaf.txt") != 0 || sys_rmdir("/tmp/dtree/c") != 0)
        return fail("cleanup c");
    if (sys_unlink("/tmp/dtree/a/note.txt") != 0 || sys_rmdir("/tmp/dtree/a/b") != 0 ||
        sys_rmdir("/tmp/dtree/a") != 0)
        return fail("cleanup a");
    if (count_entries("/tmp/dtree") != 2)
        return fail("dtree not empty");
    if (sys_rmdir("/tmp/dtree") != 0)
        return fail("rmdir /dtree");
    if (exists("/tmp/dtree"))
        return fail("dtree still exists");
    if (sys_rmdir("/tmp/dtree") != -ERR_ENOENT)
        return fail("rmdir twice");

    printf("DIRTEST PASS\n");
    return 0;
}
