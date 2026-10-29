#include "errno.h"
#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

#define HELPER_PATH "/exechelper"
#define TRUNCATED_PATH "/exectest.trunc"
#define TRUNCATED_BYTES 120
#define CHAIN_LENGTH 6
#define HUGE_ARG_BYTES 4000
#define PATTERN_BYTES 4096
#define MAP_BYTES 8192
#define UNMAPPED_ADDRESS 0x0000100000000000ul

static volatile int usr1_count;
static char huge_arg[HUGE_ARG_BYTES + 1];
static char file_image[TRUNCATED_BYTES];

static int fail(const char *what, long value) {
    dprintf(2, "EXEC FAIL: %s (%ld)\n", what, value);
    return 1;
}

static void usr1_handler(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    usr1_count++;
}

static int check_fails(const char *what, long rc, long expected) {
    return rc == expected ? 0 : fail(what, rc);
}

/* Every failure below has to leave the old image running; the checks that
 * follow in main() prove that nothing was torn down. */
static int check_failures(void) {
    const char *const argv[] = {"exechelper", 0};
    const char *const bad_argv[] = {(const char *)UNMAPPED_ADDRESS, 0};
    const char *const huge_argv[] = {huge_arg, 0};
    long fd;
    long got;

    if (check_fails("missing file", execve("/exectest.missing", argv, 0), -ENOENT) ||
        check_fails("not an ELF", execve("/init.rc", argv, 0), -ENOEXEC) ||
        check_fails("null path", execve(0, argv, 0), -EFAULT) ||
        check_fails("bad argv entry", execve(HELPER_PATH, bad_argv, 0), -EFAULT) ||
        check_fails("bad argv vector",
                    execve(HELPER_PATH, (const char *const *)UNMAPPED_ADDRESS, 0),
                    -EFAULT))
        return 1;

    memset(huge_arg, 'x', HUGE_ARG_BYTES);
    if (check_fails("oversized argv", execve(HELPER_PATH, huge_argv, 0), -E2BIG))
        return 1;

    fd = open(HELPER_PATH, OPEN_READ);
    if (fd < 0)
        return fail("open helper", fd);
    got = read((int)fd, file_image, sizeof(file_image));
    close((int)fd);
    if (got != (long)sizeof(file_image))
        return fail("read helper", got);
    if (sys_writef(TRUNCATED_PATH, file_image, sizeof(file_image)) != 0)
        return fail("write truncated image", 0);
    got = execve(TRUNCATED_PATH, argv, 0);
    sys_unlink(TRUNCATED_PATH);
    return check_fails("truncated ELF", got, -ENOEXEC);
}

static int check_old_image_intact(const char *stack_pattern, int pipe_read,
                                  int pipe_write) {
    char byte = 0;
    long mapped = sys_mmap(0, MAP_BYTES, MMAP_PROT_READ | MMAP_PROT_WRITE, 0);

    if (mapped < 0)
        return fail("mmap", mapped);
    memset((void *)mapped, 0x5A, MAP_BYTES);
    if (((unsigned char *)mapped)[MAP_BYTES - 1] != 0x5A)
        return fail("mmap contents", 0);
    if (stack_pattern[0] != 'p' || stack_pattern[PATTERN_BYTES - 1] != 'q')
        return fail("stack contents", 0);
    if (write(pipe_write, "z", 1) != 1 || read(pipe_read, &byte, 1) != 1 ||
        byte != 'z')
        return fail("pipe after failed exec", 0);
    if (kill((int)sys_getpid(), SIGUSR1) != 0 || usr1_count != 1)
        return fail("handler after failed exec", usr1_count);
    return 0;
}

static void format_number(char *out, unsigned long value) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0)
        *out++ = digits[--count];
    *out = '\0';
}

/* The kernel does not preserve SSE registers across a system call, so the
 * argument vectors are built right before the call that consumes them
 * rather than somewhere above other system calls. */
static __attribute__((noinline)) long exec_helper(
    const char *pid_text, const char *read_text, const char *write_text,
    const char *cloexec_text, const char *pages_text, const char *chain_text) {
    const char *argv[8];
    const char *envp[3];

    argv[0] = "exechelper";
    argv[1] = pid_text;
    argv[2] = read_text;
    argv[3] = write_text;
    argv[4] = cloexec_text;
    argv[5] = pages_text;
    argv[6] = chain_text;
    argv[7] = 0;
    envp[0] = "EXEC_KEY=hydrogen";
    envp[1] = "EXEC_SECOND=2";
    envp[2] = 0;
    return execve(HELPER_PATH, argv, envp);
}

int main(void) {
    char pattern[PATTERN_BYTES];
    char pid_text[24], read_text[24], write_text[24], cloexec_text[24];
    char pages_text[24], chain_text[24];
    int fds[2];
    sysinfo_t info;
    sigaction_t ignore = {SIG_IGN, 0, 0, 0};
    long cloexec_fd;

    memset(pattern, 'm', sizeof(pattern));
    pattern[0] = 'p';
    pattern[PATTERN_BYTES - 1] = 'q';

    if (pipe(fds) != 0)
        return fail("pipe", 0);
    cloexec_fd = dup_cloexec(fds[1]);
    if (cloexec_fd < 0)
        return fail("dup_cloexec", cloexec_fd);
    if (signal_install(SIGUSR1, usr1_handler) != 0 ||
        sigaction(SIGUSR2, &ignore, 0) != 0)
        return fail("sigaction", 0);

    if (check_failures() != 0 ||
        check_old_image_intact(pattern, fds[0], fds[1]) != 0)
        return 1;

    if (sys_sysinfo(&info) != 0)
        return fail("sysinfo", 0);
    format_number(pid_text, (unsigned long)sys_getpid());
    format_number(read_text, (unsigned long)fds[0]);
    format_number(write_text, (unsigned long)fds[1]);
    format_number(cloexec_text, (unsigned long)cloexec_fd);
    format_number(pages_text, info.free_pages);
    format_number(chain_text, CHAIN_LENGTH);
    return fail("execve", exec_helper(pid_text, read_text, write_text,
                                      cloexec_text, pages_text, chain_text));
}
