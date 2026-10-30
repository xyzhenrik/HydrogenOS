#include "mini_libc.h"
#include "syscall.h"
#include "wallclock.h"

#define ERR_ENOENT 2
#define ERR_EBADF  9
#define ERR_EINVAL 22
#define ERR_EISDIR 21
#define ERR_EFBIG  27
#define ERR_ENOSPC 28

#define TEST_PATH   "/tmp/iotest.bin"
#define CHUNK       4096u
#define MIB         (1024u * 1024u)
#define LARGE_WRITE (64u * 1024u)
#define FILE_LIMIT  0x7FFFFFFFul
#define BLOCK_BYTES 1024l
#define MTIME_TOLERANCE_S 120
/* Generous: TCG under load is slow, the old whole-file rewrite needs minutes. */
#define SEQUENTIAL_BUDGET_MS 30000ul

static unsigned char wbuf[LARGE_WRITE];
static unsigned char rbuf[CHUNK];

static int fail(const char *reason) {
    dprintf(2, "IO FAIL: %s\n", reason);
    return 1;
}

static unsigned char pattern(unsigned int offset, unsigned int seed) {
    return (unsigned char)(offset * 31u + (offset >> 9) + seed * 17u);
}

static void fill(unsigned char *buf, unsigned int len, unsigned int offset,
                 unsigned int seed) {
    for (unsigned int i = 0; i < len; ++i)
        buf[i] = pattern(offset + i, seed);
}

static int write_all(int fd, const unsigned char *buf, unsigned int len) {
    unsigned int done = 0;
    while (done < len) {
        long n = write(fd, buf + done, len - done);
        if (n <= 0)
            return -1;
        done += (unsigned int)n;
    }
    return 0;
}

static long file_size(const char *path) {
    stat_t st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (long)st.st_size;
}

static long free_blocks(void) {
    sysinfo_t info;
    if (sys_sysinfo(&info) != 0)
        return -1;
    return (long)info.free_blocks;
}

static int write_at(const char *path, unsigned int offset, unsigned int len,
                    unsigned int seed) {
    long fd = open(path, OPEN_READ | OPEN_WRITE);
    if (fd < 0)
        return -1;
    int rc = -1;
    if (lseek((int)fd, (long)offset, SEEK_SET) == (long)offset) {
        fill(wbuf, len, offset, seed);
        rc = write_all((int)fd, wbuf, len);
    }
    close((int)fd);
    return rc;
}

/* Seed 0 stands for "reads as zero". */
static int check_range(const char *path, unsigned int offset, unsigned int len,
                       unsigned int seed) {
    long fd = open(path, OPEN_READ);
    if (fd < 0)
        return -1;
    if (lseek((int)fd, (long)offset, SEEK_SET) != (long)offset) {
        close((int)fd);
        return -1;
    }
    unsigned int done = 0;
    while (done < len) {
        unsigned int want = len - done < CHUNK ? len - done : CHUNK;
        long n = read((int)fd, rbuf, want);
        if (n != (long)want) {
            close((int)fd);
            return -1;
        }
        for (unsigned int i = 0; i < want; ++i) {
            unsigned char expect = seed ? pattern(offset + done + i, seed) : 0;
            if (rbuf[i] != expect) {
                close((int)fd);
                return -1;
            }
        }
        done += want;
    }
    close((int)fd);
    return 0;
}

static int test_append_and_overwrite(void) {
    long fd = open(TEST_PATH, OPEN_READ | OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (fd < 0)
        return fail("create");
    fill(wbuf, 3000, 0, 1);
    if (write_all((int)fd, wbuf, 3000) != 0)
        return fail("initial write");
    close((int)fd);
    if (file_size(TEST_PATH) != 3000)
        return fail("size after initial write");

    fd = open(TEST_PATH, OPEN_WRITE | OPEN_APPEND);
    if (fd < 0)
        return fail("open append");
    fill(wbuf, 2000, 3000, 1);
    if (write_all((int)fd, wbuf, 2000) != 0)
        return fail("append");
    close((int)fd);
    if (file_size(TEST_PATH) != 5000)
        return fail("size after append");
    if (check_range(TEST_PATH, 0, 5000, 1) != 0)
        return fail("content after append");

    /* Starts mid-block and crosses two block boundaries. */
    if (write_at(TEST_PATH, 1500, 1000, 2) != 0)
        return fail("overwrite middle");
    if (file_size(TEST_PATH) != 5000)
        return fail("overwrite changed the size");
    if (check_range(TEST_PATH, 0, 1500, 1) != 0 ||
        check_range(TEST_PATH, 1500, 1000, 2) != 0 ||
        check_range(TEST_PATH, 2500, 2500, 1) != 0)
        return fail("content after overwrite");
    return 0;
}

static int test_truncate(void) {
    if (sys_truncate(TEST_PATH, 2500) != 0)
        return fail("truncate shrink");
    if (file_size(TEST_PATH) != 2500)
        return fail("size after shrink");
    if (check_range(TEST_PATH, 0, 1500, 1) != 0 ||
        check_range(TEST_PATH, 1500, 1000, 2) != 0)
        return fail("kept data after shrink");

    if (sys_truncate(TEST_PATH, 4000) != 0)
        return fail("truncate grow");
    if (file_size(TEST_PATH) != 4000)
        return fail("size after grow");
    if (check_range(TEST_PATH, 0, 1500, 1) != 0 ||
        check_range(TEST_PATH, 1500, 1000, 2) != 0)
        return fail("kept data after grow");
    if (check_range(TEST_PATH, 2500, 1500, 0) != 0)
        return fail("grown range is not zero");

    if (sys_truncate(TEST_PATH, 0) != 0 || file_size(TEST_PATH) != 0)
        return fail("truncate to zero");

    if (sys_truncate("/tmp/iotest.missing", 0) != -ERR_ENOENT)
        return fail("truncate of missing file");
    if (sys_truncate("/tmp", 0) != -ERR_EISDIR)
        return fail("truncate of directory");
    if (sys_truncate(TEST_PATH, FILE_LIMIT + 1) != -ERR_EFBIG ||
        sys_truncate(TEST_PATH, 1ul << 32) != -ERR_EFBIG)
        return fail("truncate beyond the size limit");
    if (file_size(TEST_PATH) != 0)
        return fail("failed truncate changed the file");
    return 0;
}

static int test_sparse(void) {
    long before = free_blocks();
    if (before < 0)
        return fail("sysinfo");

    if (write_at(TEST_PATH, MIB, 100, 3) != 0)
        return fail("write at 1 MiB");
    if (file_size(TEST_PATH) != (long)(MIB + 100))
        return fail("size after sparse write");
    /* Lands inside a hole: the rest of that block must still read as zero. */
    if (write_at(TEST_PATH, MIB / 2 + 5, 10, 4) != 0)
        return fail("write into hole");

    if (check_range(TEST_PATH, 0, MIB / 2 + 5, 0) != 0 ||
        check_range(TEST_PATH, MIB / 2 + 5, 10, 4) != 0 ||
        check_range(TEST_PATH, MIB / 2 + 15, MIB / 2 - 15, 0) != 0)
        return fail("hole does not read as zero");
    if (check_range(TEST_PATH, MIB, 100, 3) != 0)
        return fail("data behind the hole");

    long after = free_blocks();
    if (after < 0 || before - after > 256)
        return fail("sparse write allocated the hole");

    if (sys_truncate(TEST_PATH, MIB / 2) != 0 || file_size(TEST_PATH) != (long)(MIB / 2))
        return fail("shrink sparse file");
    if (sys_truncate(TEST_PATH, 0) != 0)
        return fail("truncate sparse file");

    long fd = open(TEST_PATH, OPEN_WRITE);
    if (fd < 0)
        return fail("open for limit check");
    if (lseek((int)fd, (long)(FILE_LIMIT - 15), SEEK_SET) < 0 ||
        write((int)fd, wbuf, 100) >= 0)
        return fail("write beyond the size limit");
    close((int)fd);
    if (file_size(TEST_PATH) != 0)
        return fail("rejected write changed the file");
    return 0;
}

static int test_sequential(void) {
    long before = free_blocks();
    long fd = open(TEST_PATH, OPEN_WRITE | OPEN_TRUNCATE);
    if (before < 0 || fd < 0)
        return fail("open sequential");

    unsigned long start = time_ms();
    for (unsigned int off = 0; off < MIB; off += CHUNK) {
        fill(wbuf, CHUNK, off, 5);
        if (write_all((int)fd, wbuf, CHUNK) != 0)
            return fail("sequential write");
    }
    unsigned long elapsed = time_ms() - start;
    close((int)fd);
    if (elapsed > SEQUENTIAL_BUDGET_MS)
        return fail("sequential write too slow");
    if (file_size(TEST_PATH) != (long)MIB)
        return fail("size after sequential write");
    if (check_range(TEST_PATH, 0, MIB, 5) != 0)
        return fail("content after sequential write");

    long used = free_blocks();
    if (used < 0 || before - used < 1000)
        return fail("blocks not accounted");

    /* One write spanning the direct/indirect boundary of a fresh file. The
       reopen with OPEN_TRUNCATE releases the 1 MiB file's blocks. */
    fd = open(TEST_PATH, OPEN_WRITE | OPEN_TRUNCATE);
    if (fd < 0)
        return fail("reopen");
    fill(wbuf, LARGE_WRITE, 0, 6);
    if (write_all((int)fd, wbuf, LARGE_WRITE) != 0)
        return fail("large single write");
    close((int)fd);
    if (file_size(TEST_PATH) != (long)LARGE_WRITE ||
        check_range(TEST_PATH, 0, LARGE_WRITE, 6) != 0)
        return fail("large single write content");

    long now = free_blocks();
    if (now < 0 || now - used < 900)
        return fail("truncating reopen did not free blocks");
    return 0;
}

static int test_ftruncate_and_sync(void) {
    long fd = open(TEST_PATH, OPEN_READ | OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (fd < 0)
        return fail("create for ftruncate");
    fill(wbuf, 3000, 0, 1);
    if (write_all((int)fd, wbuf, 3000) != 0)
        return fail("write for ftruncate");

    if (ftruncate((int)fd, 1000) != 0 || file_size(TEST_PATH) != 1000)
        return fail("ftruncate shrink");
    if (lseek((int)fd, 0, SEEK_CUR) != 3000)
        return fail("ftruncate moved the file offset");
    if (ftruncate((int)fd, 2500) != 0 || file_size(TEST_PATH) != 2500)
        return fail("ftruncate grow");
    if (check_range(TEST_PATH, 0, 1000, 1) != 0 ||
        check_range(TEST_PATH, 1000, 1500, 0) != 0)
        return fail("content after ftruncate");
    if (ftruncate((int)fd, FILE_LIMIT + 1) != -ERR_EFBIG)
        return fail("ftruncate beyond the size limit");
    if (sync() != 0)
        return fail("sync");

    long readonly = open(TEST_PATH, OPEN_READ);
    int fds[2];
    if (readonly < 0 || pipe(fds) != 0)
        return fail("open for ftruncate errors");
    if (ftruncate((int)readonly, 0) != -ERR_EBADF)
        return fail("ftruncate on a read-only fd");
    if (ftruncate(fds[1], 0) != -ERR_EINVAL)
        return fail("ftruncate on a pipe");
    if (ftruncate(60, 0) != -ERR_EBADF)
        return fail("ftruncate on a closed fd");
    close(fds[0]);
    close(fds[1]);
    close((int)readonly);
    close((int)fd);
    return 0;
}

static int test_mtime(void) {
    hydrogen_rtc_time_t now;
    stat_t st;
    long fd = open(TEST_PATH, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (fd < 0 || write_all((int)fd, wbuf, 10) != 0)
        return fail("create for mtime");
    close((int)fd);
    if (sys_rtc_time((struct hydrogen_rtc_time *)&now) != 0 || stat(TEST_PATH, &st) != 0)
        return fail("clock or stat");
    long long skew = (long long)st.st_mtime - now.epoch_seconds;
    if (skew < -MTIME_TOLERANCE_S || skew > MTIME_TOLERANCE_S)
        return fail("mtime does not follow the wall clock");
    return 0;
}

/* Fills the disk with one file: the write that hits the end must report the
   bytes that fit, the next one ENOSPC, and deleting the file frees the space. */
static int test_disk_full(void) {
    long free_before = free_blocks();
    long fd = open(TEST_PATH, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (free_before < 0 || fd < 0)
        return fail("open for disk full");

    fill(wbuf, LARGE_WRITE, 0, 7);
    long total = 0;
    int short_write = 0;
    long rc = 0;
    for (;;) {
        rc = write((int)fd, wbuf, LARGE_WRITE);
        if (rc < 0)
            break;
        total += rc;
        if (rc < (long)LARGE_WRITE)
            short_write = 1;
        if (total > free_before * BLOCK_BYTES + (long)LARGE_WRITE)
            break;
    }
    if (rc != -ERR_ENOSPC) {
        close((int)fd);
        sys_unlink(TEST_PATH);
        return fail("full disk does not report ENOSPC");
    }
    if (!short_write || file_size(TEST_PATH) != total) {
        close((int)fd);
        sys_unlink(TEST_PATH);
        return fail("partial write count on a full disk");
    }
    close((int)fd);
    if (sys_unlink(TEST_PATH) != 0)
        return fail("unlink after disk full");
    long free_after = free_blocks();
    if (free_after < free_before - 8)
        return fail("space not returned after disk full");
    return 0;
}

int main(void) {
    sys_unlink(TEST_PATH);
    int failed = test_append_and_overwrite() || test_truncate() ||
                 test_sparse() || test_sequential() || test_ftruncate_and_sync() ||
                 test_mtime() || test_disk_full();
    sys_unlink(TEST_PATH);
    if (failed)
        return 1;
    dprintf(1, "IO PASS\n");
    return 0;
}
