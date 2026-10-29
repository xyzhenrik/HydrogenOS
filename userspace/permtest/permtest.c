#include "errno.h"
#include "mini_libc.h"
#include "syscall.h"

/*
 * Runs as root under init. It lays out files with every kind of owner and
 * mode below TEST_DIR, then starts itself as "user", which drops to uid/gid
 * 1000 and checks what that account may and may not do. Programs started
 * from either verify that they inherited the credentials of their parent.
 */

#define TEST_DIR "/tmp/permtest"
#define PUBLIC_FILE TEST_DIR "/public.txt"
#define SECRET_FILE TEST_DIR "/secret.txt"
#define SHARED_FILE TEST_DIR "/shared.txt"
#define OWNED_FILE TEST_DIR "/owned.txt"
#define GROUP_FILE TEST_DIR "/group.txt"
#define LOCKED_OUT_FILE TEST_DIR "/lockedout.txt"
#define LOCKED_DIR TEST_DIR "/locked"
#define LOCKED_INNER LOCKED_DIR "/inner.txt"
#define OPEN_DIR TEST_DIR "/open"
#define OPEN_CREATED OPEN_DIR "/created.txt"
#define OPEN_RENAMED OPEN_DIR "/renamed.txt"
#define OPEN_SUBDIR OPEN_DIR "/sub"
#define READONLY_DIR TEST_DIR "/readonly"
#define READONLY_KEPT READONLY_DIR "/kept.txt"
#define SEARCH_ONLY_DIR TEST_DIR "/searchonly"
#define SEARCH_ONLY_INNER SEARCH_ONLY_DIR "/inner.txt"
#define RUNNABLE_PROGRAM TEST_DIR "/runnable"
#define PLAIN_PROGRAM TEST_DIR "/plain"

#define SELF_PROGRAM "/permtest"
#define LOGIN_PROGRAM "/login"
#define MODE_UNPRIVILEGED "unprivileged"
#define MODE_AS_ROOT "as-root"
#define MODE_AS_USER "as-user"

#define ROOT_ID 0u
#define USER_ID 1000u
#define OTHER_ID 7u
#define COPY_CHUNK 1024

static const char *const fixture_files[] = {
    PUBLIC_FILE, SECRET_FILE, SHARED_FILE, OWNED_FILE, GROUP_FILE, LOCKED_OUT_FILE,
    LOCKED_INNER, OPEN_CREATED, OPEN_RENAMED, READONLY_KEPT, SEARCH_ONLY_INNER,
    RUNNABLE_PROGRAM, PLAIN_PROGRAM,
};

/* Children before their parents. */
static const char *const fixture_dirs[] = {
    OPEN_SUBDIR, LOCKED_DIR, OPEN_DIR, READONLY_DIR, SEARCH_ONLY_DIR, TEST_DIR,
};

static int fail(const char *what) {
    dprintf(2, "PERM FAIL: %s\n", what);
    return 1;
}

/* Returns 0 when actual is what the test wanted. */
static int expect(const char *what, long actual, long wanted) {
    if (actual == wanted)
        return 0;
    dprintf(2, "PERM FAIL: %s: got %ld, want %ld\n", what, actual, wanted);
    return 1;
}

static int expect_symlink_owner(const char *path, uint32_t uid, uint32_t gid) {
    stat_t st = {0};
    long rc = lstat(path, &st);
    if (rc == 0 && S_ISLNK(st.st_mode) && st.st_uid == uid && st.st_gid == gid)
        return 0;
    dprintf(2, "PERM FAIL: link %s is %u:%u (rc %ld), want %u:%u\n", path,
            st.st_uid, st.st_gid, rc, uid, gid);
    return 1;
}

static int expect_attributes(const char *path, uint32_t uid, uint32_t gid,
                             uint32_t mode) {
    stat_t st = {0};
    long rc = stat(path, &st);
    if (rc == 0 && st.st_uid == uid && st.st_gid == gid &&
        (st.st_mode & S_IRWXUGO) == mode)
        return 0;
    dprintf(2, "PERM FAIL: %s is %u:%u mode %o (rc %ld), want %u:%u mode %o\n",
            path, st.st_uid, st.st_gid, st.st_mode & S_IRWXUGO, rc, uid, gid, mode);
    return 1;
}

/* 0 if path opens with flags, else the negative errno. */
static long try_open(const char *path, int flags) {
    long fd = open(path, flags);
    if (fd < 0)
        return fd;
    close((int)fd);
    return 0;
}

/* Overwrites the start of an existing file; 0 or the negative errno. */
static long overwrite(const char *path, const char *text) {
    long fd = open(path, OPEN_WRITE);
    long wrote;
    if (fd < 0)
        return fd;
    wrote = write((int)fd, text, strlen(text));
    close((int)fd);
    return wrote == (long)strlen(text) ? 0 : -EIO;
}

static int starts_with(const char *path, const char *text) {
    char buf[16];
    size_t len = strlen(text);
    long fd = open(path, OPEN_READ);
    long n;
    if (fd < 0)
        return 0;
    n = read((int)fd, buf, sizeof(buf));
    close((int)fd);
    return n >= (long)len && memcmp(buf, text, len) == 0;
}

/* Starts path with one argument and waits for it: returns the exit status or
 * the negative errno of the spawn. */
static long run_program(const char *path, const char *mode) {
    const char *argv[] = {path, mode, 0};
    int status = -1;
    long pid = spawn_args(path, argv, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                          SPAWN_STDIO_INHERIT);
    if (pid < 0)
        return pid;
    if (wait_pid((int)pid, &status) != pid)
        return -EIO;
    return status;
}

static int copy_file(const char *from, const char *to) {
    char buf[COPY_CHUNK];
    long in = open(from, OPEN_READ);
    long out = open(to, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    int status = in >= 0 && out >= 0 ? 0 : -1;
    while (status == 0) {
        long n = read((int)in, buf, sizeof(buf));
        if (n < 0 || (n > 0 && write((int)out, buf, (size_t)n) != n))
            status = -1;
        else if (n == 0)
            break;
    }
    if (in >= 0)
        close((int)in);
    if (out >= 0)
        close((int)out);
    return status;
}

static int make_file(const char *path, const char *text, uint32_t mode,
                     uint32_t uid, uint32_t gid) {
    if (sys_writef(path, text, strlen(text)) != 0 || sys_chmod(path, mode) != 0 ||
        sys_chown(path, uid, gid) != 0)
        return fail(path);
    return 0;
}

static int make_directory(const char *path, uint32_t mode, uint32_t uid,
                          uint32_t gid) {
    if (sys_mkdir(path) != 0 || sys_chmod(path, mode) != 0 ||
        sys_chown(path, uid, gid) != 0)
        return fail(path);
    return 0;
}

/* Leftovers of an interrupted earlier run, or the end of this one. */
static void remove_fixtures(void) {
    for (size_t i = 0; i < sizeof(fixture_files) / sizeof(fixture_files[0]); ++i)
        sys_unlink(fixture_files[i]);
    for (size_t i = 0; i < sizeof(fixture_dirs) / sizeof(fixture_dirs[0]); ++i)
        sys_rmdir(fixture_dirs[i]);
}

static int create_fixtures(void) {
    if (make_directory(TEST_DIR, 0755, ROOT_ID, ROOT_ID) ||
        make_directory(OPEN_DIR, 0755, USER_ID, USER_ID) ||
        make_directory(READONLY_DIR, 0755, ROOT_ID, ROOT_ID) ||
        make_directory(LOCKED_DIR, 0700, ROOT_ID, ROOT_ID) ||
        make_directory(SEARCH_ONLY_DIR, 0711, ROOT_ID, ROOT_ID))
        return 1;
    if (make_file(PUBLIC_FILE, "public\n", 0644, ROOT_ID, ROOT_ID) ||
        make_file(SECRET_FILE, "secret\n", 0600, ROOT_ID, ROOT_ID) ||
        make_file(SHARED_FILE, "shared\n", 0666, ROOT_ID, ROOT_ID) ||
        make_file(OWNED_FILE, "owned\n", 0600, USER_ID, USER_ID) ||
        make_file(GROUP_FILE, "group\n", 0640, ROOT_ID, USER_ID) ||
        make_file(LOCKED_OUT_FILE, "locked out\n", 0006, USER_ID, USER_ID) ||
        make_file(LOCKED_INNER, "inner\n", 0644, ROOT_ID, ROOT_ID) ||
        make_file(READONLY_KEPT, "kept\n", 0644, ROOT_ID, ROOT_ID) ||
        make_file(SEARCH_ONLY_INNER, "inner\n", 0644, ROOT_ID, ROOT_ID))
        return 1;
    if (copy_file(SELF_PROGRAM, RUNNABLE_PROGRAM) != 0 ||
        copy_file(SELF_PROGRAM, PLAIN_PROGRAM) != 0)
        return fail("copy program");
    if (sys_chmod(RUNNABLE_PROGRAM, 0711) != 0 || sys_chmod(PLAIN_PROGRAM, 0644) != 0)
        return fail("program modes");
    return 0;
}

static int check_installed_files(void) {
    return expect_attributes(SELF_PROGRAM, ROOT_ID, ROOT_ID, 0755) ||
           expect_attributes("/etc/passwd", ROOT_ID, ROOT_ID, 0644) ||
           expect_attributes("/tmp", ROOT_ID, ROOT_ID, 0777) ||
           expect_attributes("/home/user", USER_ID, USER_ID, 0755) ||
           expect_attributes("/proc", ROOT_ID, ROOT_ID, 0555);
}

/* login drops to the account and runs its shell; the shell reports who it
 * has become. */
static int check_login(void) {
    static const char expected[] = "uid=1000(user) gid=1000(user)\n";
    const char *argv[] = {LOGIN_PROGRAM, "user", "-c", "id", 0};
    char output[sizeof(expected) + 8] = {0};
    int fds[2];
    int status = -1;
    long used = 0;
    long pid;

    if (expect("login unknown", run_program(LOGIN_PROGRAM, "nobody"), 1))
        return 1;
    if (pipe(fds) != 0)
        return fail("login pipe");
    pid = spawn_args(LOGIN_PROGRAM, argv, 0, SPAWN_STDIO_NULL, fds[1], SPAWN_STDIO_INHERIT);
    close(fds[1]);
    while (pid > 0 && used < (long)sizeof(output) - 1) {
        long n = read(fds[0], output + used, sizeof(output) - 1 - (size_t)used);
        if (n <= 0)
            break;
        used += n;
    }
    close(fds[0]);
    if (pid <= 0 || wait_pid((int)pid, &status) != pid)
        return fail("login spawn");
    if (status != 0 || strcmp(output, expected) != 0)
        return fail("login did not become user");
    return 0;
}

static int check_root(void) {
    if (expect("root uid", sys_getuid(), ROOT_ID) ||
        expect("root gid", sys_getgid(), ROOT_ID) ||
        expect("setuid reserved id", sys_setuid(ID_UNCHANGED), -EINVAL) ||
        expect("setgid reserved id", sys_setgid(ID_UNCHANGED), -EINVAL))
        return 1;
    if (check_installed_files())
        return 1;
    if (create_fixtures())
        return 1;
    /* Root ignores read and write bits, but not a missing execute bit. */
    if (expect("root reads 0600", try_open(SECRET_FILE, OPEN_READ), 0) ||
        expect("root writes 0644", try_open(PUBLIC_FILE, OPEN_WRITE), 0) ||
        expect("root searches 0700", try_open(LOCKED_INNER, OPEN_READ), 0) ||
        expect("root runs 0711", run_program(RUNNABLE_PROGRAM, MODE_AS_ROOT), 0) ||
        expect("root runs 0644", run_program(PLAIN_PROGRAM, MODE_AS_ROOT), -EACCES) ||
        check_login())
        return 1;
    /* chmod and chown report what they did. */
    if (expect("chmod special bits", sys_chmod(PUBLIC_FILE, 01644), -EINVAL) ||
        expect("chmod missing", sys_chmod(TEST_DIR "/missing", 0644), -ENOENT) ||
        expect("chown missing", sys_chown(TEST_DIR "/missing", 0, 0), -ENOENT) ||
        expect("chown owner only", sys_chown(PUBLIC_FILE, OTHER_ID, ID_UNCHANGED), 0) ||
        expect_attributes(PUBLIC_FILE, OTHER_ID, ROOT_ID, 0644) ||
        expect("chown group only", sys_chown(PUBLIC_FILE, ID_UNCHANGED, OTHER_ID), 0) ||
        expect_attributes(PUBLIC_FILE, OTHER_ID, OTHER_ID, 0644) ||
        expect("chown back", sys_chown(PUBLIC_FILE, ROOT_ID, ROOT_ID), 0) ||
        expect_attributes(PUBLIC_FILE, ROOT_ID, ROOT_ID, 0644))
        return 1;
    return 0;
}

static int drop_privileges(void) {
    /* The group first: afterwards setgid would be refused. */
    if (expect("setgid", sys_setgid(USER_ID), 0) ||
        expect("setuid", sys_setuid(USER_ID), 0) ||
        expect("getuid", sys_getuid(), USER_ID) ||
        expect("getgid", sys_getgid(), USER_ID) ||
        expect("regain root uid", sys_setuid(ROOT_ID), -EPERM) ||
        expect("regain root gid", sys_setgid(ROOT_ID), -EPERM) ||
        expect("setuid to self", sys_setuid(USER_ID), 0) ||
        expect("setuid reserved id", sys_setuid(ID_UNCHANGED), -EINVAL))
        return 1;
    return 0;
}

static int check_file_bits(void) {
    return expect("read 0644", try_open(PUBLIC_FILE, OPEN_READ), 0) ||
           expect("write 0644", try_open(PUBLIC_FILE, OPEN_WRITE), -EACCES) ||
           expect("read 0600", try_open(SECRET_FILE, OPEN_READ), -EACCES) ||
           expect_attributes(SECRET_FILE, ROOT_ID, ROOT_ID, 0600) ||
           expect("write 0666", overwrite(SHARED_FILE, "u"), 0) ||
           expect("owner reads", try_open(OWNED_FILE, OPEN_READ), 0) ||
           expect("owner writes", try_open(OWNED_FILE, OPEN_WRITE), 0) ||
           expect("group reads 0640", try_open(GROUP_FILE, OPEN_READ), 0) ||
           expect("group writes 0640", try_open(GROUP_FILE, OPEN_WRITE), -EACCES) ||
           /* The owner class applies even where "other" would allow it. */
           expect("owner read 0006", try_open(LOCKED_OUT_FILE, OPEN_READ), -EACCES) ||
           expect("owner write 0006", try_open(LOCKED_OUT_FILE, OPEN_WRITE), -EACCES);
}

static int check_attribute_calls(void) {
    return expect("owner chmod", sys_chmod(OWNED_FILE, 0640), 0) ||
           expect_attributes(OWNED_FILE, USER_ID, USER_ID, 0640) ||
           expect("foreign chmod", sys_chmod(PUBLIC_FILE, 0600), -EPERM) ||
           expect("chmod in locked dir", sys_chmod(LOCKED_INNER, 0600), -EACCES) ||
           expect("chown own file", sys_chown(OWNED_FILE, ROOT_ID, ROOT_ID), -EPERM) ||
           expect("chown to self", sys_chown(OWNED_FILE, USER_ID, USER_ID), -EPERM) ||
           expect_attributes(PUBLIC_FILE, ROOT_ID, ROOT_ID, 0644);
}

static int check_search_and_list(void) {
    stat_t st;
    dirent_t entry;
    return expect("stat below 0700", stat(LOCKED_INNER, &st), -EACCES) ||
           expect("open below 0700", try_open(LOCKED_INNER, OPEN_READ), -EACCES) ||
           expect("chdir into 0700", chdir(LOCKED_DIR), -EACCES) ||
           expect_attributes(LOCKED_DIR, ROOT_ID, ROOT_ID, 0700) ||
           expect("list 0711", sys_readdir(SEARCH_ONLY_DIR, 0, &entry), -EACCES) ||
           expect("open below 0711", try_open(SEARCH_ONLY_INNER, OPEN_READ), 0) ||
           expect("chdir into 0711", chdir(SEARCH_ONLY_DIR), 0) ||
           expect("chdir back", chdir("/"), 0) ||
           expect("list 0755", sys_readdir(TEST_DIR, 0, &entry), 1);
}

static int check_own_directory(void) {
    return expect("create in own dir", try_open(OPEN_CREATED, OPEN_WRITE | OPEN_CREATE), 0) ||
           expect_attributes(OPEN_CREATED, USER_ID, USER_ID, 0644) ||
           expect("rename in own dir", sys_rename(OPEN_CREATED, OPEN_RENAMED), 0) ||
           expect("mkdir in own dir", sys_mkdir(OPEN_SUBDIR), 0) ||
           expect_attributes(OPEN_SUBDIR, USER_ID, USER_ID, 0755) ||
           expect("rmdir in own dir", sys_rmdir(OPEN_SUBDIR), 0) ||
           expect("unlink in own dir", sys_unlink(OPEN_RENAMED), 0) ||
           expect("writef creates", sys_writef(OPEN_CREATED, "w", 1), 0) ||
           expect_attributes(OPEN_CREATED, USER_ID, USER_ID, 0644) ||
           expect("symlink in own dir", sys_symlink(PUBLIC_FILE, OPEN_RENAMED), 0) ||
           expect_symlink_owner(OPEN_RENAMED, USER_ID, USER_ID) ||
           expect("unlink symlink", sys_unlink(OPEN_RENAMED), 0);
}

static int check_foreign_directory(void) {
    return expect("create in 0755 dir", try_open(READONLY_DIR "/new.txt", OPEN_WRITE | OPEN_CREATE), -EACCES) ||
           expect("writef creates there", sys_writef(READONLY_DIR "/new.txt", "x", 1), -EACCES) ||
           expect("writef 0644 file", sys_writef(READONLY_KEPT, "x", 1), -EACCES) ||
           expect("mkdir in 0755 dir", sys_mkdir(READONLY_DIR "/sub"), -EACCES) ||
           expect("unlink in 0755 dir", sys_unlink(READONLY_KEPT), -EACCES) ||
           expect("rename out of 0755 dir", sys_rename(READONLY_KEPT, OPEN_RENAMED), -EACCES) ||
           expect("rename into 0755 dir", sys_rename(OPEN_CREATED, READONLY_DIR "/in.txt"), -EACCES) ||
           expect("symlink in 0755 dir", sys_symlink(PUBLIC_FILE, READONLY_DIR "/link"), -EACCES) ||
           expect("link into 0755 dir", sys_link(PUBLIC_FILE, READONLY_DIR "/hard"), -EACCES) ||
           expect("unlink own file", sys_unlink(OPEN_CREATED), 0);
}

static int check_execution(void) {
    return expect("run 0711 program", run_program(RUNNABLE_PROGRAM, MODE_AS_USER), 0) ||
           expect("run 0644 program", run_program(PLAIN_PROGRAM, MODE_AS_USER), -EACCES);
}

/* What the unprivileged child leaves behind for the root parent to verify. */
static int check_aftermath(void) {
    return expect("shared file written", starts_with(SHARED_FILE, "u"), 1) ||
           expect_attributes(OWNED_FILE, USER_ID, USER_ID, 0640) ||
           expect_attributes(PUBLIC_FILE, ROOT_ID, ROOT_ID, 0644);
}

static int run_unprivileged(void) {
    return drop_privileges() || check_file_bits() || check_attribute_calls() ||
           check_search_and_list() || check_own_directory() ||
           check_foreign_directory() || check_execution();
}

/* 0 when the process runs as the given account. */
static int run_as_account(uint32_t id) {
    return sys_getuid() == id && sys_getgid() == id ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], MODE_AS_ROOT) == 0)
        return run_as_account(ROOT_ID);
    if (argc >= 2 && strcmp(argv[1], MODE_AS_USER) == 0)
        return run_as_account(USER_ID);
    if (argc >= 2 && strcmp(argv[1], MODE_UNPRIVILEGED) == 0)
        return run_unprivileged();
    if (argc >= 2)
        return fail("unknown mode");

    remove_fixtures();
    if (check_root()) {
        remove_fixtures();
        return 1;
    }
    if (expect("unprivileged run", run_program(SELF_PROGRAM, MODE_UNPRIVILEGED), 0) ||
        check_aftermath()) {
        remove_fixtures();
        return 1;
    }
    remove_fixtures();
    dprintf(1, "PERM PASS\n");
    return 0;
}
