#include "signal.h"

#include "syscall.h"

#define STRINGIFY_VALUE(x) #x
#define STRINGIFY(x) STRINGIFY_VALUE(x)

__asm__(".text\n"
        ".globl sigreturn_trampoline\n"
        "sigreturn_trampoline:\n"
        "    mov $" STRINGIFY(SYS_SIGRETURN) ", %eax\n"
        "    syscall\n"
        "    ud2\n");

typedef struct signal_info {
    int number;
    const char *name;
    const char *description;
} signal_info_t;

static const signal_info_t signal_table[] = {
    {SIGINT, "INT", ""},
    {SIGILL, "ILL", "Illegal instruction"},
    {SIGTRAP, "TRAP", "Trace/breakpoint trap"},
    {SIGABRT, "ABRT", "Aborted"},
    {SIGBUS, "BUS", "Bus error"},
    {SIGFPE, "FPE", "Arithmetic exception"},
    {SIGKILL, "KILL", "Killed"},
    {SIGUSR1, "USR1", "User defined signal 1"},
    {SIGSEGV, "SEGV", "Segmentation fault"},
    {SIGUSR2, "USR2", "User defined signal 2"},
    {SIGTERM, "TERM", "Terminated"},
    {SIGCHLD, "CHLD", "Child exited"},
};

#define SIGNAL_TABLE_LENGTH (sizeof(signal_table) / sizeof(signal_table[0]))

long sigaction(int sig, const sigaction_t *act, sigaction_t *old) {
    return sys_sigaction(sig, act, old);
}

long kill(int pid, int sig) {
    return sys_kill(pid, sig);
}

long sigprocmask(int how, unsigned int set, unsigned int *old) {
    return sys_sigprocmask(how, set, old);
}

long signal_install(int sig, signal_handler_t handler) {
    sigaction_t act = {
        .handler = (unsigned long)handler,
        .restorer = (unsigned long)sigreturn_trampoline,
        .mask = 0,
        .flags = 0,
    };
    return sigaction(sig, &act, 0);
}

const char *signal_name(int sig) {
    for (unsigned long i = 0; i < SIGNAL_TABLE_LENGTH; ++i) {
        if (signal_table[i].number == sig)
            return signal_table[i].name;
    }
    return 0;
}

const char *signal_description(int sig) {
    for (unsigned long i = 0; i < SIGNAL_TABLE_LENGTH; ++i) {
        if (signal_table[i].number == sig)
            return signal_table[i].description;
    }
    return "Unknown signal";
}

static int names_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

int signal_number(const char *name) {
    if (name[0] >= '0' && name[0] <= '9') {
        int number = 0;
        for (; *name >= '0' && *name <= '9'; ++name) {
            number = number * 10 + (*name - '0');
            if (number >= 32)
                return -1;
        }
        if (*name != 0)
            return -1;
        return number == 0 || signal_name(number) ? number : -1;
    }
    if (name[0] == 'S' && name[1] == 'I' && name[2] == 'G')
        name += 3;
    for (unsigned long i = 0; i < SIGNAL_TABLE_LENGTH; ++i) {
        if (names_equal(name, signal_table[i].name))
            return signal_table[i].number;
    }
    return -1;
}
