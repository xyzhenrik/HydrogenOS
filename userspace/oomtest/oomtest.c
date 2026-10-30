#include "meminfo.h"
#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

/*
 * Boot test for the out-of-memory policy. It starts memory hogs that map
 * memory until the machine runs dry and checks that the kernel answers by
 * killing hogs (and only hogs) with SIGKILL, that init and this process
 * survive, and that the memory comes back. Prints OOM PASS.
 */

enum {
    HOG_COUNT = 3,
    HOG_CHUNK_BYTES = 1024 * 1024,
    HOG_CHUNK_LIMIT = 4096,
    HOG_STATUS_EXHAUSTED = 3,
    INIT_PID = 1,
    /* Pages the kernel may keep for itself between the two snapshots. */
    LEAK_SLACK_PAGES = 256,
};

static int fail(const char *what) {
    dprintf(1, "OOM FAIL: %s\n", what);
    return 1;
}

static int str_equal(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* Maps memory until the kernel refuses. Under the OOM policy it normally ends
 * by being killed; reaching the refusal means that nothing else was killable. */
static int role_hog(void) {
    for (int chunk = 0; chunk < HOG_CHUNK_LIMIT; ++chunk) {
        if (sys_mmap(0, HOG_CHUNK_BYTES, MMAP_PROT_READ | MMAP_PROT_WRITE, 0) < 0)
            return HOG_STATUS_EXHAUSTED;
    }
    return HOG_STATUS_EXHAUSTED;
}

int main(int argc, char **argv) {
    if (argc > 1 && str_equal(argv[1], "hog"))
        return role_hog();

    hydrogen_meminfo_t before;
    if (meminfo_read(&before) != 0)
        return fail("meminfo interface");

    const char *hog_argv[] = {"/lib/tests/oomtest", "hog", 0};
    long pids[HOG_COUNT];
    for (int i = 0; i < HOG_COUNT; ++i) {
        pids[i] = spawn_args("/lib/tests/oomtest", hog_argv, 0, SPAWN_STDIO_NULL,
                             SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
        if (pids[i] <= 0)
            return fail("starting a hog");
    }

    unsigned killed = 0;
    for (int i = 0; i < HOG_COUNT; ++i) {
        int status;
        if (wait_pid((int)pids[i], &status) != pids[i])
            return fail("waiting for a hog");
        if (WIFSIGNALED(status)) {
            if (WTERMSIG(status) != SIGKILL)
                return fail("a hog died of another signal than SIGKILL");
            killed++;
        } else if (status != HOG_STATUS_EXHAUSTED) {
            return fail("a hog ended in an unexpected way");
        }
    }
    if (killed == 0)
        return fail("no hog was killed");

    hydrogen_meminfo_t after;
    if (meminfo_read(&after) != 0)
        return fail("meminfo after the run");
    if (after.oom_kills - before.oom_kills != killed)
        return fail("the kernel killed something besides the hogs");
    if (after.free_pages + LEAK_SLACK_PAGES < before.free_pages)
        return fail("memory of the killed hogs was not returned");
    if (kill(INIT_PID, 0) != 0)
        return fail("init did not survive");
    if (sys_mmap(0, HOG_CHUNK_BYTES, MMAP_PROT_READ | MMAP_PROT_WRITE, 0) < 0)
        return fail("no memory left for the survivor");

    write(1, "OOM PASS\n", 9);
    return 0;
}
