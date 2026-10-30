#include "mini_libc.h"
#include "syscall.h"

#define ERR_ENOENT 2

#define DCT_STATS_CAP 512
#define DCT_PATH_CAP 128
#define DCT_DEEP_REPEATS 16
#define DCT_DEEP_COMPONENTS 9
/* More distinct names than any plausible cache capacity. */
#define DCT_FLOOD_NAMES 600

#define LONG_NAME_LEN 60

typedef struct dcache_counters {
    uint64_t entries;
    uint64_t capacity;
    uint64_t hits;
    uint64_t negative_hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t invalidations;
} dcache_counters_t;

static const char *const cleanup_files[] = {
    "/dctree/a/b/c/d/e/f/g/leaf.txt", "/dctree/fresh.txt", "/dctree/gone.txt",
    "/dctree/old.txt", "/dctree/new.txt", "/dctree/r_src", "/dctree/r_dst",
    "/dctree/m1/dir/inner.txt", "/dctree/m2/dir/inner.txt", "/dctree/hl",
    "/dctree/orig", "/dctree/t1", "/dctree/t2", "/dctree/sl", "/dctree/dang",
    "/dctree/later", "/dctree/d1/kept.txt", "/dctree/d2/kept.txt",
};

static const char *const cleanup_dirs[] = {
    "/dctree/a/b/c/d/e/f/g", "/dctree/a/b/c/d/e/f", "/dctree/a/b/c/d/e",
    "/dctree/a/b/c/d", "/dctree/a/b/c", "/dctree/a/b", "/dctree/a",
    "/dctree/m1/dir", "/dctree/m2/dir", "/dctree/m1", "/dctree/m2",
    "/dctree/d1", "/dctree/d2", "/dctree",
};

static int fail(const char *reason) {
    dprintf(1, "DCACHE FAIL: %s\n", reason);
    return 1;
}

/* A name longer than anything the cache stores. */
static void long_path(char *out, char fill) {
    size_t base = strlen("/dctree/");
    memcpy(out, "/dctree/", base);
    for (size_t i = 0; i < LONG_NAME_LEN; ++i)
        out[base + i] = fill;
    out[base + LONG_NAME_LEN] = '\0';
}

static void remove_leftovers(void) {
    char path[DCT_PATH_CAP];
    for (size_t i = 0; i < sizeof(cleanup_files) / sizeof(cleanup_files[0]); ++i)
        sys_unlink(cleanup_files[i]);
    long_path(path, 'x');
    sys_unlink(path);
    long_path(path, 'y');
    sys_unlink(path);
    for (size_t i = 0; i < sizeof(cleanup_dirs) / sizeof(cleanup_dirs[0]); ++i)
        sys_rmdir(cleanup_dirs[i]);
}

static int field_u64(const char *text, const char *label, uint64_t *out) {
    size_t label_len = strlen(label);
    const char *line = text;
    while (*line) {
        size_t i = 0;
        while (i < label_len && line[i] == label[i])
            ++i;
        if (i == label_len && line[i] == ':') {
            const char *v = line + i + 1;
            while (*v == ' ')
                ++v;
            if (*v < '0' || *v > '9')
                return -1;
            uint64_t value = 0;
            while (*v >= '0' && *v <= '9')
                value = value * 10u + (uint64_t)(*v++ - '0');
            *out = value;
            return 0;
        }
        while (*line && *line != '\n')
            ++line;
        if (*line == '\n')
            ++line;
    }
    return -1;
}

static int read_counters(dcache_counters_t *out) {
    char text[DCT_STATS_CAP];
    long fd = open("/proc/dcache", OPEN_READ);
    if (fd < 0)
        return -1;
    long n = read((int)fd, text, sizeof(text) - 1);
    close((int)fd);
    if (n <= 0)
        return -1;
    text[n] = '\0';
    if (field_u64(text, "Entries", &out->entries) != 0 ||
        field_u64(text, "Capacity", &out->capacity) != 0 ||
        field_u64(text, "Hits", &out->hits) != 0 ||
        field_u64(text, "NegativeHits", &out->negative_hits) != 0 ||
        field_u64(text, "Misses", &out->misses) != 0 ||
        field_u64(text, "Evictions", &out->evictions) != 0 ||
        field_u64(text, "Invalidations", &out->invalidations) != 0)
        return -1;
    return 0;
}

static int content_is(const char *path, const char *text) {
    char buf[32];
    size_t len = strlen(text);
    long fd = open(path, OPEN_READ);
    if (fd < 0)
        return 0;
    long n = read((int)fd, buf, sizeof(buf));
    close((int)fd);
    if (n != (long)len)
        return 0;
    return memcmp(buf, text, len) == 0;
}

static int is_absent(const char *path) {
    stat_t st;
    return lstat(path, &st) == -ERR_ENOENT;
}

static int is_dir(const char *path) {
    stat_t st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int make_dirs(const char *const *paths, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (sys_mkdir(paths[i]) != 0)
            return -1;
    }
    return 0;
}

static int check_repeated_lookups(void) {
    static const char *const dirs[] = {
        "/dctree/a", "/dctree/a/b", "/dctree/a/b/c",
        "/dctree/a/b/c/d", "/dctree/a/b/c/d/e", "/dctree/a/b/c/d/e/f",
        "/dctree/a/b/c/d/e/f/g",
    };
    const char *deep = "/dctree/a/b/c/d/e/f/g/leaf.txt";
    if (make_dirs(dirs, sizeof(dirs) / sizeof(dirs[0])) != 0)
        return fail("create deep directories");
    if (sys_writef(deep, "deep", 4) != 0)
        return fail("create deep file");

    stat_t st;
    if (stat(deep, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 4)
        return fail("stat deep file");
    uint32_t ino = st.st_ino;

    dcache_counters_t before, after;
    if (read_counters(&before) != 0)
        return fail("read /proc/dcache");
    if (before.capacity == 0 || before.entries > before.capacity)
        return fail("dcache size fields");
    for (int i = 0; i < DCT_DEEP_REPEATS; ++i) {
        if (stat(deep, &st) != 0 || st.st_ino != ino)
            return fail("repeated deep stat");
    }
    if (read_counters(&after) != 0)
        return fail("read /proc/dcache again");
    uint64_t hits = after.hits - before.hits;
    uint64_t misses = after.misses - before.misses;
    if (hits < (uint64_t)DCT_DEEP_REPEATS * DCT_DEEP_COMPONENTS)
        return fail("repeated lookups did not hit");
    if (misses >= hits)
        return fail("repeated lookups kept missing");
    if (!content_is(deep, "deep"))
        return fail("deep file content");
    return 0;
}

static int check_negative_entries(void) {
    dcache_counters_t before, after;
    if (!is_absent("/dctree/fresh.txt"))
        return fail("fresh.txt exists early");
    if (read_counters(&before) != 0)
        return fail("read counters");
    if (!is_absent("/dctree/fresh.txt"))
        return fail("fresh.txt exists early on repeat");
    if (read_counters(&after) != 0)
        return fail("read counters again");
    if (after.negative_hits <= before.negative_hits)
        return fail("absent name was not served from the cache");

    if (sys_writef("/dctree/fresh.txt", "fresh", 5) != 0)
        return fail("create fresh.txt");
    stat_t st;
    if (stat("/dctree/fresh.txt", &st) != 0 || st.st_size != 5)
        return fail("negative entry survived create");
    if (!content_is("/dctree/fresh.txt", "fresh"))
        return fail("fresh.txt content");

    /* A dangling symlink turns valid once its target appears. */
    if (sys_symlink("later", "/dctree/dang") != 0)
        return fail("dangling symlink");
    if (stat("/dctree/dang", &st) != -ERR_ENOENT)
        return fail("dangling symlink resolved");
    if (sys_writef("/dctree/later", "later", 5) != 0)
        return fail("create symlink target");
    if (!content_is("/dctree/dang", "later"))
        return fail("symlink did not see its new target");
    return 0;
}

static int check_unlink(void) {
    stat_t st;
    if (sys_writef("/dctree/gone.txt", "first", 5) != 0)
        return fail("create gone.txt");
    if (stat("/dctree/gone.txt", &st) != 0)
        return fail("stat gone.txt");
    if (sys_unlink("/dctree/gone.txt") != 0)
        return fail("unlink gone.txt");
    if (!is_absent("/dctree/gone.txt"))
        return fail("unlinked name still resolves");
    if (sys_writef("/dctree/gone.txt", "second", 6) != 0)
        return fail("recreate gone.txt");
    if (!content_is("/dctree/gone.txt", "second"))
        return fail("recreated file shows stale content");
    return 0;
}

static int check_rename(void) {
    stat_t before, after, st;
    if (sys_writef("/dctree/old.txt", "one", 3) != 0)
        return fail("create old.txt");
    if (stat("/dctree/old.txt", &before) != 0 || !is_absent("/dctree/new.txt"))
        return fail("warm rename names");
    if (sys_rename("/dctree/old.txt", "/dctree/new.txt") != 0)
        return fail("rename");
    if (!is_absent("/dctree/old.txt"))
        return fail("old name survives rename");
    if (stat("/dctree/new.txt", &after) != 0 || after.st_ino != before.st_ino)
        return fail("new name after rename");
    if (!content_is("/dctree/new.txt", "one"))
        return fail("renamed content");

    if (sys_writef("/dctree/r_src", "src", 3) != 0 ||
        sys_writef("/dctree/r_dst", "dst", 3) != 0)
        return fail("create replace pair");
    if (stat("/dctree/r_src", &st) != 0 || stat("/dctree/r_dst", &st) != 0)
        return fail("warm replace pair");
    if (sys_rename("/dctree/r_src", "/dctree/r_dst") != 0)
        return fail("rename over existing");
    if (!is_absent("/dctree/r_src"))
        return fail("source survives replacing rename");
    if (!content_is("/dctree/r_dst", "src"))
        return fail("replaced target shows old inode");

    /* A directory moves together with everything below it. */
    if (sys_mkdir("/dctree/m1") != 0 || sys_mkdir("/dctree/m2") != 0 ||
        sys_mkdir("/dctree/m1/dir") != 0)
        return fail("create move directories");
    if (sys_writef("/dctree/m1/dir/inner.txt", "inner", 5) != 0)
        return fail("create inner file");
    if (!content_is("/dctree/m1/dir/inner.txt", "inner") ||
        !is_absent("/dctree/m2/dir"))
        return fail("warm directory move");
    if (sys_rename("/dctree/m1/dir", "/dctree/m2/dir") != 0)
        return fail("rename directory");
    if (!is_absent("/dctree/m1/dir") || !is_absent("/dctree/m1/dir/inner.txt"))
        return fail("old directory path survives rename");
    if (!content_is("/dctree/m2/dir/inner.txt", "inner"))
        return fail("moved directory contents");

    dcache_counters_t counters;
    if (read_counters(&counters) != 0 || counters.invalidations == 0)
        return fail("no invalidations recorded");
    return 0;
}

static int check_rmdir(void) {
    if (sys_mkdir("/dctree/d1") != 0)
        return fail("mkdir d1");
    if (sys_writef("/dctree/d1/kept.txt", "kept", 4) != 0)
        return fail("create d1 file");
    if (!content_is("/dctree/d1/kept.txt", "kept") || !is_absent("/dctree/d1/ghost"))
        return fail("warm d1");
    if (sys_unlink("/dctree/d1/kept.txt") != 0 || sys_rmdir("/dctree/d1") != 0)
        return fail("remove d1");
    if (!is_absent("/dctree/d1") || !is_absent("/dctree/d1/kept.txt"))
        return fail("removed directory still resolves");

    /* The next directory probably reuses d1's inode number. */
    if (sys_mkdir("/dctree/d2") != 0)
        return fail("mkdir d2");
    if (!is_dir("/dctree/d2") || !is_absent("/dctree/d2/kept.txt") ||
        !is_absent("/dctree/d2/ghost"))
        return fail("new directory inherited stale entries");
    if (sys_writef("/dctree/d2/kept.txt", "again", 5) != 0 ||
        !content_is("/dctree/d2/kept.txt", "again"))
        return fail("file in reused directory");
    if (sys_mkdir("/dctree/d1") != 0 || !is_dir("/dctree/d1"))
        return fail("recreate d1");
    return 0;
}

static int check_links(void) {
    stat_t st;
    if (sys_writef("/dctree/orig", "linked", 6) != 0)
        return fail("create orig");
    if (!is_absent("/dctree/hl"))
        return fail("hl exists early");
    if (sys_link("/dctree/orig", "/dctree/hl") != 0)
        return fail("hard link");
    if (stat("/dctree/hl", &st) != 0 || st.st_nlink != 2)
        return fail("negative entry survived link");
    if (sys_unlink("/dctree/orig") != 0)
        return fail("unlink orig");
    if (!is_absent("/dctree/orig"))
        return fail("unlinked name resolves");
    if (!content_is("/dctree/hl", "linked") || stat("/dctree/hl", &st) != 0 ||
        st.st_nlink != 1)
        return fail("surviving link name");

    /* An entry names the link itself; the target is looked up on every use. */
    if (sys_writef("/dctree/t1", "first", 5) != 0 ||
        sys_writef("/dctree/t2", "other", 5) != 0)
        return fail("create symlink targets");
    if (sys_symlink("t1", "/dctree/sl") != 0)
        return fail("create symlink");
    if (!content_is("/dctree/sl", "first") || !content_is("/dctree/sl", "first"))
        return fail("read through symlink");
    if (sys_unlink("/dctree/sl") != 0 || sys_symlink("t2", "/dctree/sl") != 0)
        return fail("retarget symlink");
    if (!content_is("/dctree/sl", "other"))
        return fail("symlink followed a cached target");
    if (sys_unlink("/dctree/t2") != 0 || sys_writef("/dctree/t2", "third", 5) != 0)
        return fail("replace symlink target");
    if (!content_is("/dctree/sl", "third"))
        return fail("symlink followed a stale inode");
    return 0;
}

static int check_long_names(void) {
    char path_a[DCT_PATH_CAP];
    char path_b[DCT_PATH_CAP];
    long_path(path_a, 'x');
    long_path(path_b, 'y');
    if (sys_writef(path_a, "long", 4) != 0)
        return fail("create long name");
    for (int i = 0; i < 3; ++i) {
        if (!content_is(path_a, "long"))
            return fail("read long name");
    }
    if (sys_rename(path_a, path_b) != 0)
        return fail("rename long name");
    if (!is_absent(path_a) || !content_is(path_b, "long"))
        return fail("long name after rename");
    if (sys_unlink(path_b) != 0 || !is_absent(path_b))
        return fail("unlink long name");
    return 0;
}

static int check_bounded_size(void) {
    char path[DCT_PATH_CAP];
    dcache_counters_t before, after;
    if (read_counters(&before) != 0)
        return fail("read counters");
    for (int i = 0; i < DCT_FLOOD_NAMES; ++i) {
        snprintf(path, sizeof(path), "/dctree/flood%d", i);
        if (!is_absent(path))
            return fail("flood name exists");
    }
    if (read_counters(&after) != 0)
        return fail("read counters again");
    if (after.entries > after.capacity)
        return fail("cache grew past its capacity");
    if (after.evictions <= before.evictions)
        return fail("full cache did not evict");

    /* Evicted or not, answers must stay correct. */
    if (!content_is("/dctree/a/b/c/d/e/f/g/leaf.txt", "deep"))
        return fail("deep file after flood");
    if (!content_is("/dctree/fresh.txt", "fresh"))
        return fail("fresh.txt after flood");
    return 0;
}

int main(void) {
    remove_leftovers();
    if (sys_mkdir("/dctree") != 0)
        return fail("mkdir dctree");
    int rc = check_repeated_lookups();
    if (rc == 0)
        rc = check_negative_entries();
    if (rc == 0)
        rc = check_unlink();
    if (rc == 0)
        rc = check_rename();
    if (rc == 0)
        rc = check_rmdir();
    if (rc == 0)
        rc = check_links();
    if (rc == 0)
        rc = check_long_names();
    if (rc == 0)
        rc = check_bounded_size();
    remove_leftovers();
    if (rc != 0)
        return rc;
    if (!is_absent("/dctree"))
        return fail("cleanup");
    printf("DCACHE PASS\n");
    return 0;
}
