#include "errno.h"
#include "mini_libc.h"
#include "sha256.h"
#include "syscall.h"

/*
 * Boot test for /bin/hpkg. Runs the real binary against a scratch root below
 * /tmp: installs, upgrades and removes hand-built packages (dependencies,
 * conflicts, corrupt and unsafe archives, a failure halfway through an
 * installation) and finally installs from the baked-in repository in
 * /var/lib/hpkg/repo. Prints HPKG PASS.
 */

#define SCRATCH "/tmp/hpkgtest"
#define ROOT SCRATCH "/root"
#define HPKG "/bin/hpkg"
#define SYSTEM_REPOSITORY "/var/lib/hpkg/repo"

enum {
    OUTPUT_CAP = 2048,
    ARCHIVE_CAP = 4096,
    FILE_CAP = 512,
};

typedef struct TestFile {
    const char *path;
    unsigned mode;
    const char *data;
} TestFile;

static void remove_tree(const char *path);

static void fail(int line, const char *condition) {
    remove_tree(SCRATCH);
    dprintf(2, "HPKG FAIL line %d: %s\n", line, condition);
    exit(1);
}

#define CHECK(condition) \
    do { \
        if (!(condition)) \
            fail(__LINE__, #condition); \
    } while (0)

static int exists(const char *path) {
    stat_t info;

    return lstat(path, &info) == 0;
}

static void remove_tree(const char *path) {
    stat_t info;

    if (lstat(path, &info) != 0)
        return;
    if (S_ISDIR(info.st_mode)) {
        dirent_t entry;

        /* Entries shift when one is removed, so always look at position 0
         * after skipping "." and "..". */
        for (;;) {
            char child[160];
            int position = 0;
            long status;

            do {
                status = readdir_pos(path, position++, &entry);
            } while (status == 1 && (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0));
            if (status != 1)
                break;
            snprintf(child, sizeof(child), "%s/%s", path, entry.d_name);
            remove_tree(child);
        }
        sys_rmdir(path);
    } else {
        sys_unlink(path);
    }
}

static void make_directory(const char *path) {
    long status = sys_mkdir(path);

    CHECK(status == 0 || status == -EEXIST);
}

static size_t read_text(const char *path, char *buffer, size_t capacity) {
    long fd = open(path, OPEN_READ);
    long got;

    if (fd < 0)
        return 0;
    got = read((int)fd, buffer, capacity - 1);
    close((int)fd);
    if (got < 0)
        got = 0;
    buffer[got] = 0;
    return (size_t)got;
}

static int file_has(const char *path, const char *expected) {
    char buffer[FILE_CAP];

    read_text(path, buffer, sizeof(buffer));
    return strcmp(buffer, expected) == 0;
}

static void write_text(const char *path, const char *text) {
    CHECK(sys_writef(path, text, strlen(text)) == 0);
}

static void put_u32(uint8_t **cursor, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        *(*cursor)++ = (uint8_t)(value >> (8 * i));
}

static void put_bytes(uint8_t **cursor, const void *bytes, size_t length) {
    memcpy(*cursor, bytes, length);
    *cursor += length;
}

/* Writes a package archive; files must already be in hpkg_path_compare order. */
static size_t build_package(const char *output, const char *manifest, const TestFile *files,
                            size_t count) {
    static uint8_t archive[ARCHIVE_CAP];
    uint8_t *cursor = archive;
    Sha256Context context;
    uint8_t digest[SHA256_DIGEST_SIZE];

    put_bytes(&cursor, "HPK1", 4);
    put_u32(&cursor, (uint32_t)strlen(manifest));
    put_bytes(&cursor, manifest, strlen(manifest));
    put_u32(&cursor, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) {
        size_t size = strlen(files[i].data);

        put_u32(&cursor, (uint32_t)strlen(files[i].path));
        put_bytes(&cursor, files[i].path, strlen(files[i].path));
        put_u32(&cursor, files[i].mode);
        put_u32(&cursor, (uint32_t)size);
        sha256_init(&context);
        sha256_update(&context, files[i].data, size);
        sha256_final(&context, digest);
        put_bytes(&cursor, digest, sizeof(digest));
        put_bytes(&cursor, files[i].data, size);
    }
    sha256_init(&context);
    sha256_update(&context, archive, (size_t)(cursor - archive));
    sha256_final(&context, digest);
    put_bytes(&cursor, digest, sizeof(digest));
    CHECK(sys_writef(output, archive, (size_t)(cursor - archive)) == 0);
    return (size_t)(cursor - archive);
}

/* Runs hpkg with output captured; returns its exit status. */
static int run(char *output, const char *const *arguments) {
    const char *argv[16] = {"hpkg"};
    size_t argc = 1;
    size_t used = 0;
    int fds[2];
    int status = -1;
    long pid;

    for (size_t i = 0; arguments[i]; ++i)
        argv[argc++] = arguments[i];
    argv[argc] = 0;
    CHECK(pipe(fds) == 0);
    pid = spawn_args(HPKG, argv, 0, SPAWN_STDIO_NULL, fds[1], fds[1]);
    close(fds[1]);
    CHECK(pid > 0);
    for (;;) {
        long got = read(fds[0], output + used, OUTPUT_CAP - 1 - used);

        if (got <= 0)
            break;
        used += (size_t)got;
    }
    output[used] = 0;
    close(fds[0]);
    CHECK(wait_pid((int)pid, &status) >= 0);
    return status;
}

static int hpkg(char *output, const char *a, const char *b, const char *c) {
    const char *arguments[8] = {"--root", ROOT};
    size_t count = 2;

    if (a)
        arguments[count++] = a;
    if (b)
        arguments[count++] = b;
    if (c)
        arguments[count++] = c;
    arguments[count] = 0;
    return run(output, arguments);
}

static int hpkg_with_repository(char *output, const char *repository, const char *a,
                                const char *b, const char *c) {
    const char *arguments[10] = {"--root", ROOT, "--repo", repository};
    size_t count = 4;

    if (a)
        arguments[count++] = a;
    if (b)
        arguments[count++] = b;
    if (c)
        arguments[count++] = c;
    arguments[count] = 0;
    return run(output, arguments);
}

static void test_sha256(void) {
    char hex[SHA256_HEX_LENGTH + 1];

    sha256_hex("", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    sha256_hex("abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
}

static void install_chain(char *output) {
    static const TestFile alpha1[] = {
        {"/usr/bin/alpha-tool", 0755, "tool\n"},
        {"/usr/share/alpha/one.txt", 0644, "one\n"},
        {"/usr/share/alpha/two.txt", 0644, "two\n"},
    };
    static const TestFile beta1[] = {{"/usr/share/beta/beta.txt", 0644, "beta\n"}};
    static const TestFile gamma1[] = {{"/usr/share/gamma/gamma.txt", 0644, "gamma\n"}};
    stat_t info;

    build_package(SCRATCH "/alpha-1.hpkg", "name alpha\nversion 1.0\ndescription first\n",
                  alpha1, 3);
    build_package(SCRATCH "/beta-1.hpkg", "name beta\nversion 1.0\ndepends alpha\n", beta1, 1);
    build_package(SCRATCH "/gamma-1.hpkg", "name gamma\nversion 1.0\ndepends missing\n",
                  gamma1, 1);

    CHECK(hpkg(output, "install", SCRATCH "/alpha-1.hpkg", 0) == 0);
    CHECK(strstr(output, "installed alpha 1.0 (3 files)"));
    CHECK(file_has(ROOT "/usr/share/alpha/one.txt", "one\n"));
    CHECK(stat(ROOT "/usr/bin/alpha-tool", &info) == 0 && (info.st_mode & S_IRWXUGO) == 0755);
    CHECK(hpkg(output, "list", 0, 0) == 0 && strcmp(output, "alpha 1.0\n") == 0);
    CHECK(hpkg(output, "verify", 0, 0) == 0);
    CHECK(strstr(output, "verified 1 package, 0 problems"));
    CHECK(hpkg(output, "install", SCRATCH "/alpha-1.hpkg", 0) == 0);
    CHECK(strstr(output, "already installed"));
    CHECK(hpkg(output, "info", "alpha", 0) == 0);
    CHECK(strstr(output, "Version        : 1.0") && strstr(output, "  /usr/share/alpha/two.txt"));

    CHECK(hpkg(output, "install", SCRATCH "/beta-1.hpkg", 0) == 0);
    CHECK(hpkg(output, "install", SCRATCH "/gamma-1.hpkg", 0) != 0);
    CHECK(strstr(output, "gamma needs missing"));
    CHECK(!exists(ROOT "/usr/share/gamma"));
    CHECK(hpkg(output, "list", 0, 0) == 0 && strcmp(output, "alpha 1.0\nbeta 1.0\n") == 0);
    CHECK(hpkg(output, "remove", "alpha", 0) != 0);
    CHECK(strstr(output, "beta depends on it"));
    CHECK(exists(ROOT "/usr/share/alpha/one.txt"));
}

static void rejected_archives(char *output) {
    static const TestFile conflict[] = {{"/usr/share/alpha/one.txt", 0644, "stolen\n"}};
    static const TestFile stray[] = {{"/usr/share/stray/s.txt", 0644, "package\n"}};
    static const TestFile unsafe[] = {{"/usr/share/../evil.txt", 0644, "evil\n"}};
    static uint8_t damaged[ARCHIVE_CAP];
    size_t length;
    long fd;

    build_package(SCRATCH "/conflict.hpkg", "name conflict\nversion 1.0\n", conflict, 1);
    CHECK(hpkg(output, "install", SCRATCH "/conflict.hpkg", 0) != 0);
    CHECK(strstr(output, "is owned by alpha"));
    CHECK(file_has(ROOT "/usr/share/alpha/one.txt", "one\n"));
    CHECK(!exists(ROOT "/usr/share/alpha/one.txt.hpkg-new"));

    build_package(SCRATCH "/stray.hpkg", "name stray\nversion 1.0\n", stray, 1);
    make_directory(ROOT "/usr/share/stray");
    write_text(ROOT "/usr/share/stray/s.txt", "mine\n");
    CHECK(hpkg(output, "install", SCRATCH "/stray.hpkg", 0) != 0);
    CHECK(strstr(output, "belongs to no package"));
    CHECK(file_has(ROOT "/usr/share/stray/s.txt", "mine\n"));
    remove_tree(ROOT "/usr/share/stray");

    build_package(SCRATCH "/unsafe.hpkg", "name unsafe\nversion 1.0\n", unsafe, 1);
    CHECK(hpkg(output, "install", SCRATCH "/unsafe.hpkg", 0) != 0);
    CHECK(strstr(output, "unsafe path"));
    CHECK(!exists(ROOT "/usr/evil.txt"));

    fd = open(SCRATCH "/alpha-1.hpkg", OPEN_READ);
    CHECK(fd >= 0);
    length = (size_t)read((int)fd, damaged, sizeof(damaged));
    close((int)fd);
    CHECK(length > 80);
    damaged[length - 40] ^= 0x01;
    CHECK(sys_writef(SCRATCH "/damaged.hpkg", damaged, length) == 0);
    CHECK(hpkg(output, "install", SCRATCH "/damaged.hpkg", 0) != 0);
    CHECK(strstr(output, "checksum mismatch"));
    CHECK(hpkg(output, "list", 0, 0) == 0 && strcmp(output, "alpha 1.0\nbeta 1.0\n") == 0);
}

static void upgrades(char *output) {
    static const TestFile blocked[] = {
        {"/usr/share/alpha/one.txt", 0644, "three\n"},
        {"/usr/share/alpha/zzz.txt", 0644, "z\n"},
    };
    static const TestFile alpha2[] = {
        {"/usr/share/alpha/one.txt", 0644, "one v2\n"},
        {"/usr/share/alpha/three.txt", 0644, "three\n"},
    };
    static const TestFile alpha_old[] = {{"/usr/share/alpha/one.txt", 0644, "old\n"}};

    /* The second staging file cannot be created because a directory has its
     * name; the first one has been staged by then and must be rolled back. */
    build_package(SCRATCH "/alpha-3.hpkg", "name alpha\nversion 3.0\n", blocked, 2);
    CHECK(sys_mkdir(ROOT "/usr/share/alpha/zzz.txt.hpkg-new") == 0);
    CHECK(hpkg(output, "install", SCRATCH "/alpha-3.hpkg", 0) != 0);
    CHECK(file_has(ROOT "/usr/share/alpha/one.txt", "one\n"));
    CHECK(file_has(ROOT "/usr/share/alpha/two.txt", "two\n"));
    CHECK(!exists(ROOT "/usr/share/alpha/one.txt.hpkg-new"));
    CHECK(!exists(ROOT "/usr/share/alpha/one.txt.hpkg-old"));
    CHECK(!exists(ROOT "/usr/share/alpha/zzz.txt"));
    CHECK(hpkg(output, "list", 0, 0) == 0 && strcmp(output, "alpha 1.0\nbeta 1.0\n") == 0);
    CHECK(hpkg(output, "verify", 0, 0) == 0);
    CHECK(sys_rmdir(ROOT "/usr/share/alpha/zzz.txt.hpkg-new") == 0);

    build_package(SCRATCH "/alpha-2.hpkg", "name alpha\nversion 2.0\n", alpha2, 2);
    CHECK(hpkg(output, "install", SCRATCH "/alpha-2.hpkg", 0) == 0);
    CHECK(strstr(output, "upgraded alpha 1.0 -> 2.0"));
    CHECK(file_has(ROOT "/usr/share/alpha/one.txt", "one v2\n"));
    CHECK(file_has(ROOT "/usr/share/alpha/three.txt", "three\n"));
    CHECK(!exists(ROOT "/usr/share/alpha/two.txt"));
    CHECK(!exists(ROOT "/usr/bin/alpha-tool"));
    CHECK(!exists(ROOT "/usr/share/alpha/one.txt.hpkg-old"));
    CHECK(hpkg(output, "verify", 0, 0) == 0);

    build_package(SCRATCH "/alpha-0.hpkg", "name alpha\nversion 0.9\n", alpha_old, 1);
    CHECK(hpkg(output, "install", SCRATCH "/alpha-0.hpkg", 0) != 0);
    CHECK(strstr(output, "refusing to downgrade"));
    CHECK(file_has(ROOT "/usr/share/alpha/one.txt", "one v2\n"));
}

static void removal_and_verify(char *output) {
    static const TestFile alpha1[] = {{"/usr/share/alpha/one.txt", 0644, "ONE\n"}};

    CHECK(hpkg(output, "remove", "beta", 0) == 0);
    CHECK(hpkg(output, "remove", "alpha", 0) == 0);
    CHECK(hpkg(output, "remove", "alpha", 0) != 0);
    CHECK(strstr(output, "alpha is not installed"));
    CHECK(!exists(ROOT "/usr"));
    CHECK(hpkg(output, "list", 0, 0) == 0 && output[0] == 0);

    build_package(SCRATCH "/alpha-t.hpkg", "name alpha\nversion 1.0\n", alpha1, 1);
    CHECK(hpkg(output, "install", SCRATCH "/alpha-t.hpkg", 0) == 0);
    write_text(ROOT "/usr/share/alpha/one.txt", "one\n");
    CHECK(hpkg(output, "verify", 0, 0) != 0);
    CHECK(strstr(output, "alpha: /usr/share/alpha/one.txt: checksum mismatch"));
    CHECK(strstr(output, "1 problem"));
    sys_unlink(ROOT "/usr/share/alpha/one.txt");
    CHECK(hpkg(output, "verify", "alpha", 0) != 0);
    CHECK(strstr(output, "missing"));
    CHECK(hpkg(output, "remove", "alpha", 0) == 0);
    CHECK(!exists(ROOT "/usr"));
}

static void copy_file(const char *from, const char *to, long flip_offset) {
    static uint8_t content[ARCHIVE_CAP];
    long fd = open(from, OPEN_READ);
    long length;

    CHECK(fd >= 0);
    length = read((int)fd, content, sizeof(content));
    close((int)fd);
    CHECK(length > 0);
    if (flip_offset >= 0)
        content[flip_offset] ^= 0x01;
    CHECK(sys_writef(to, content, (size_t)length) == 0);
}

static void repository(char *output) {
    stat_t info;

    CHECK(hpkg_with_repository(output, SYSTEM_REPOSITORY, "update", 0, 0) == 0);
    CHECK(strstr(output, "repository: 3 packages, 0 upgradable"));
    CHECK(hpkg_with_repository(output, SYSTEM_REPOSITORY, "info", "fortune-lite", 0) == 0);
    CHECK(strstr(output, "available in the repository"));
    CHECK(hpkg_with_repository(output, SYSTEM_REPOSITORY, "install", "no-such-package", 0) != 0);

    /* A repository whose greeter archive is damaged: the dependency that was
     * installed first must be taken out again. */
    make_directory(SCRATCH "/repo");
    copy_file(SYSTEM_REPOSITORY "/index", SCRATCH "/repo/index", -1);
    copy_file(SYSTEM_REPOSITORY "/libgreet-1.0.0.hpkg", SCRATCH "/repo/libgreet-1.0.0.hpkg", -1);
    copy_file(SYSTEM_REPOSITORY "/greeter-1.2.0.hpkg", SCRATCH "/repo/greeter-1.2.0.hpkg", 40);
    CHECK(hpkg_with_repository(output, SCRATCH "/repo", "install", "greeter", 0) != 0);
    CHECK(hpkg(output, "list", 0, 0) == 0 && output[0] == 0);
    CHECK(!exists(ROOT "/usr"));

    CHECK(hpkg_with_repository(output, SYSTEM_REPOSITORY, "install", "greeter", 0) == 0);
    CHECK(strstr(output, "installed libgreet 1.0.0") && strstr(output, "installed greeter 1.2.0"));
    CHECK(hpkg(output, "list", 0, 0) == 0 && strcmp(output, "greeter 1.2.0\nlibgreet 1.0.0\n") == 0);
    CHECK(stat(ROOT "/usr/share/libgreet/phrases.txt", &info) == 0);
    CHECK(hpkg(output, "verify", 0, 0) == 0);
    CHECK(hpkg(output, "info", "greeter", 0) == 0);
    CHECK(strstr(output, "Depends on     : libgreet"));
    CHECK(hpkg(output, "remove", "libgreet", 0) != 0);
    CHECK(hpkg(output, "remove", "greeter", "libgreet") == 0);
    CHECK(!exists(ROOT "/usr"));
}

int main(void) {
    char output[OUTPUT_CAP];

    remove_tree(SCRATCH);
    make_directory(SCRATCH);
    make_directory(ROOT);
    test_sha256();
    install_chain(output);
    rejected_archives(output);
    upgrades(output);
    removal_and_verify(output);
    repository(output);
    remove_tree(SCRATCH);
    printf("HPKG PASS\n");
    return 0;
}
