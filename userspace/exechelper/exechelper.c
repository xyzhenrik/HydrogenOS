#include "errno.h"
#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

/* argv: name pid read_fd write_fd cloexec_fd free_pages_at_start chain.
 * Re-executes itself `chain` times, every image mapping memory first, and
 * then runs the checks; a leaked address space shows up as missing free
 * pages. */
enum {
    ARG_PID = 1,
    ARG_READ_FD,
    ARG_WRITE_FD,
    ARG_CLOEXEC_FD,
    ARG_FREE_PAGES,
    ARG_CHAIN,
    ARG_COUNT,
};

#define CHAIN_MAP_BYTES (1024ul * 1024ul)
/* The running image's page tables and stack are not part of the start value. */
#define FREE_PAGES_SLACK 128ul

static int fail(const char *what, long value) {
    dprintf(2, "EXEC FAIL: helper: %s (%ld)\n", what, value);
    return 1;
}

static int chain_next(int argc, char **argv, unsigned long remaining) {
    char chain_text[24];
    char digits[24];
    char *next_argv[ARG_COUNT + 1];
    int count = 0;
    long mapped = sys_mmap(0, CHAIN_MAP_BYTES, MMAP_PROT_READ | MMAP_PROT_WRITE, 0);

    if (mapped < 0)
        return fail("chain mmap", mapped);
    memset((void *)mapped, 1, CHAIN_MAP_BYTES);

    remaining--;
    do {
        digits[count++] = (char)('0' + remaining % 10);
        remaining /= 10;
    } while (remaining != 0);
    for (int i = 0; i < count; ++i)
        chain_text[i] = digits[count - 1 - i];
    chain_text[count] = '\0';

    for (int i = 0; i < argc; ++i)
        next_argv[i] = argv[i];
    next_argv[ARG_CHAIN] = chain_text;
    next_argv[argc] = 0;
    return fail("chain execv",
                execv("/exechelper", (const char *const *)next_argv));
}

int main(int argc, char **argv) {
    sigaction_t usr1, usr2;
    sysinfo_t info;
    char byte = 0;
    unsigned long remaining;
    unsigned long start_pages;
    int read_fd, write_fd, cloexec_fd;

    if (argc != ARG_COUNT || strcmp(argv[0], "exechelper") != 0)
        return fail("argc", argc);
    remaining = strtoul(argv[ARG_CHAIN], 0, 10);
    if (remaining > 0)
        return chain_next(argc, argv, remaining);

    if ((long)strtoul(argv[ARG_PID], 0, 10) != sys_getpid())
        return fail("pid changed", sys_getpid());
    if (!getenv("EXEC_KEY") || strcmp(getenv("EXEC_KEY"), "hydrogen") != 0 ||
        !getenv("EXEC_SECOND") || strcmp(getenv("EXEC_SECOND"), "2") != 0)
        return fail("environment", 0);

    read_fd = (int)strtoul(argv[ARG_READ_FD], 0, 10);
    write_fd = (int)strtoul(argv[ARG_WRITE_FD], 0, 10);
    cloexec_fd = (int)strtoul(argv[ARG_CLOEXEC_FD], 0, 10);
    /* stderr was a close-on-exec pipe end: the slot is closed now and falls
     * back to the console instead of feeding the pipe. */
    write(2, "\n", 1);
    if ((sys_poll(read_fd, POLL_READ) & (long)POLL_READ) != 0)
        return fail("close-on-exec stderr survived", 0);
    if (write(write_fd, "k", 1) != 1 || read(read_fd, &byte, 1) != 1 ||
        byte != 'k')
        return fail("inherited pipe", 0);
    if (write(cloexec_fd, "k", 1) != -EBADF)
        return fail("close-on-exec handle survived", 0);

    if (sigaction(SIGUSR1, 0, &usr1) != 0 || sigaction(SIGUSR2, 0, &usr2) != 0)
        return fail("sigaction", 0);
    if (usr1.handler != SIG_DFL)
        return fail("caught signal not reset", (long)usr1.handler);
    if (usr2.handler != SIG_IGN)
        return fail("ignored signal not kept", (long)usr2.handler);

    start_pages = strtoul(argv[ARG_FREE_PAGES], 0, 10);
    if (sys_sysinfo(&info) != 0)
        return fail("sysinfo", 0);
    if (info.free_pages + FREE_PAGES_SLACK < start_pages)
        return fail("old images leaked", (long)(start_pages - info.free_pages));

    write(1, "EXEC PASS\n", 10);
    return 0;
}
