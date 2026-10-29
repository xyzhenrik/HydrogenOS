#include "mini_libc.h"

/*
 * logread - print the system log written by syslogd.
 *
 * Prints the rotated log (system.log.1) followed by the current log;
 * "logread -c" prints only the current one.
 */

#define LOGREAD_PATH       "/var/log/system.log"
#define LOGREAD_PATH_OLD   "/var/log/system.log.1"
#define LOGREAD_ERR_ENOENT (-2)

enum {
    LOGREAD_CHUNK = 512,
};

/* Returns 0 on success, 1 if the file does not exist, -1 on other errors. */
static int dump_file(const char *path) {
    char chunk[LOGREAD_CHUNK];
    long fd = open(path, OPEN_READ);
    if (fd < 0) {
        return fd == LOGREAD_ERR_ENOENT ? 1 : -1;
    }
    for (;;) {
        long got = read((int)fd, chunk, sizeof(chunk));
        long done = 0;
        if (got < 0) {
            close((int)fd);
            return -1;
        }
        if (got == 0) {
            break;
        }
        while (done < got) {
            long put = write(1, chunk + done, (size_t)(got - done));
            if (put <= 0) {
                close((int)fd);
                return -1;
            }
            done += put;
        }
    }
    close((int)fd);
    return 0;
}

int main(int argc, char **argv) {
    int current_only = 0;
    if (argc == 2 && argv[1][0] == '-' && argv[1][1] == 'c' && argv[1][2] == 0) {
        current_only = 1;
    } else if (argc > 1) {
        fprintf(2, "usage: logread [-c]\n");
        return 2;
    }
    if (!current_only && dump_file(LOGREAD_PATH_OLD) < 0) {
        fprintf(2, "logread: cannot read %s\n", LOGREAD_PATH_OLD);
        return 1;
    }
    if (dump_file(LOGREAD_PATH) != 0) {
        fprintf(2, "logread: cannot read %s\n", LOGREAD_PATH);
        return 1;
    }
    return 0;
}
