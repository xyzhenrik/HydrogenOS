#include "errno.h"
#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

#define HELPER_PATH "/exechelper"
#define TRUNCATED_PATH "/exectest.trunc"
#define TRUNCATED_BYTES 120
#define EMPTY_PATH "/exectest.empty"
#define BROKEN_PATH "/exectest.broken"
#define CHAIN_LENGTH 10
#define HUGE_ARG_BYTES 4000
#define PATTERN_BYTES 4096
#define MAP_BYTES 8192
#define UNMAPPED_ADDRESS 0x0000100000000000ul

/* Offsets into an ELF64 image, for building deliberately broken ones. */
#define ELF_PHOFF_OFFSET 32
#define ELF_PHNUM_OFFSET 56
#define ELF_PHDR_BYTES 56
#define PHDR_TYPE_OFFSET 0
#define PHDR_OFFSET_OFFSET 8
#define PHDR_VADDR_OFFSET 16
#define PHDR_FILESZ_OFFSET 32
#define PHDR_MEMSZ_OFFSET 40
#define PT_LOAD_TYPE 1
#define USER_IMAGE_BASE 0x80000000ul
#define KERNEL_IDENTITY_ADDRESS 0x100000ul

static volatile int usr1_count;
static char huge_arg[HUGE_ARG_BYTES + 1];
static char file_image[TRUNCATED_BYTES];
static char pid_text[24], read_text[24], write_text[24], cloexec_text[24];
static char pages_text[24], chain_text[24];
static int pipe_fds[2];
static long handler_exec_rc;

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

static void store_u64(char *image, unsigned long offset, unsigned long value) {
    for (int i = 0; i < 8; ++i)
        image[offset + (unsigned long)i] = (char)(value >> (8 * i));
}

static unsigned long load_u64(const char *image, unsigned long offset) {
    unsigned long value = 0;
    for (int i = 0; i < 8; ++i)
        value |= (unsigned long)(unsigned char)image[offset + (unsigned long)i] << (8 * i);
    return value;
}

/* A header plus a single PT_LOAD entry that claims `vaddr`/`memsz` and no
 * file bytes: complete enough to pass validation, wrong enough that the
 * loader has to refuse it. */
static long exec_broken_segment(unsigned long vaddr, unsigned long memsz) {
    const char *const argv[] = {"exechelper", 0};
    unsigned long phoff = load_u64(file_image, ELF_PHOFF_OFFSET);
    long rc;

    if (phoff + ELF_PHDR_BYTES > sizeof(file_image))
        return fail("helper layout", (long)phoff);
    file_image[ELF_PHNUM_OFFSET] = 1;
    file_image[ELF_PHNUM_OFFSET + 1] = 0;
    store_u64(file_image, phoff + PHDR_TYPE_OFFSET, PT_LOAD_TYPE);
    store_u64(file_image, phoff + PHDR_OFFSET_OFFSET, 0);
    store_u64(file_image, phoff + PHDR_VADDR_OFFSET, vaddr);
    store_u64(file_image, phoff + PHDR_FILESZ_OFFSET, 0);
    store_u64(file_image, phoff + PHDR_MEMSZ_OFFSET, memsz);
    if (sys_writef(BROKEN_PATH, file_image, sizeof(file_image)) != 0)
        return fail("write broken image", 0);
    rc = execve(BROKEN_PATH, argv, 0);
    sys_unlink(BROKEN_PATH);
    return rc;
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
    if (check_fails("truncated ELF", got, -ENOEXEC))
        return 1;

    if (check_fails("directory", execve("/", argv, 0), -EACCES))
        return 1;
    if (sys_writef(EMPTY_PATH, "", 0) != 0)
        return fail("write empty file", 0);
    got = execve(EMPTY_PATH, argv, 0);
    sys_unlink(EMPTY_PATH);
    if (check_fails("empty file", got, -ENOEXEC))
        return 1;

    return check_fails("segment size overflow",
                       exec_broken_segment(USER_IMAGE_BASE, ~0ul), -ENOEXEC) ||
           check_fails("segment in kernel identity map",
                       exec_broken_segment(KERNEL_IDENTITY_ADDRESS, 0x1000),
                       -ENOEXEC);
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
static __attribute__((noinline)) long exec_helper(void) {
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

/* Runs the exec from inside a signal handler: SIGTERM is blocked by the
 * handler's mask and made pending, so a kernel that cleared either across
 * the exec would let its default action kill the helper. */
static void exec_in_handler(int sig, const sigcontext_t *context) {
    (void)sig;
    (void)context;
    if (kill((int)sys_getpid(), SIGTERM) != 0)
        return;
    handler_exec_rc = exec_helper();
}

int main(void) {
    char pattern[PATTERN_BYTES];
    sysinfo_t info;
    sigaction_t ignore = {SIG_IGN, 0, 0, 0};
    sigaction_t exec_handler = {(unsigned long)exec_in_handler,
                                (unsigned long)sigreturn_trampoline,
                                SIGNAL_BIT(SIGTERM), 0};
    long cloexec_fd;
    long saved_stderr;

    memset(pattern, 'm', sizeof(pattern));
    pattern[0] = 'p';
    pattern[PATTERN_BYTES - 1] = 'q';

    if (pipe(pipe_fds) != 0)
        return fail("pipe", 0);
    cloexec_fd = dup_cloexec(pipe_fds[1]);
    if (cloexec_fd < 0)
        return fail("dup_cloexec", cloexec_fd);
    if (signal_install(SIGUSR1, usr1_handler) != 0 ||
        sigaction(SIGUSR2, &ignore, 0) != 0 ||
        sigaction(SIGINT, &exec_handler, 0) != 0)
        return fail("sigaction", 0);

    if (check_failures() != 0 ||
        check_old_image_intact(pattern, pipe_fds[0], pipe_fds[1]) != 0)
        return 1;

    if (sys_sysinfo(&info) != 0)
        return fail("sysinfo", 0);
    format_number(pid_text, (unsigned long)sys_getpid());
    format_number(read_text, (unsigned long)pipe_fds[0]);
    format_number(write_text, (unsigned long)pipe_fds[1]);
    format_number(cloexec_text, (unsigned long)cloexec_fd);
    format_number(pages_text, info.free_pages);
    format_number(chain_text, CHAIN_LENGTH);

    /* stderr becomes a close-on-exec pipe end for the exec; it is put back
     * if the exec returns so that the failure can still be reported. */
    saved_stderr = sys_dup(2);
    if (saved_stderr < 0)
        return fail("dup stderr", saved_stderr);
    if (sys_dup3(pipe_fds[1], 2, DUP3_CLOEXEC) != 2)
        return fail("cloexec stderr", 0);
    kill((int)sys_getpid(), SIGINT);
    sys_dup2((int)saved_stderr, 2);
    return fail("execve", handler_exec_rc);
}
