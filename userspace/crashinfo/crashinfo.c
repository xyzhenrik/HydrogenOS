#include "crash.h"
#include "mini_libc.h"

/*
 * crashinfo [N | --collect]
 *
 * Without arguments, lists the crash dumps in /var/crash. With a number,
 * prints /var/crash/crash-N.txt. --collect (root) first moves a dump the
 * kernel still holds into /var/crash, which init already does at boot.
 */

enum {
    PATH_CAP = 64,
    CHUNK = 512,
    STATUS_FAILURE = 1,
    STATUS_USAGE = 2,
};

static const char prefix[] = "crash-";
static const char suffix[] = ".txt";

static int is_dump_name(const char *name) {
    size_t length = strlen(name);
    size_t prefix_length = sizeof(prefix) - 1;
    size_t suffix_length = sizeof(suffix) - 1;
    return length > prefix_length + suffix_length &&
           strncmp(name, prefix, prefix_length) == 0 &&
           strcmp(name + length - suffix_length, suffix) == 0;
}

static int list_dumps(void) {
    dirent_t entry;
    int listed = 0;
    for (int pos = 0;; ++pos) {
        int rc = readdir_pos(CRASH_DIRECTORY, pos, &entry);
        if (rc < 0) {
            dprintf(2, "crashinfo: cannot read " CRASH_DIRECTORY "\n");
            return STATUS_FAILURE;
        }
        if (rc == 0)
            break;
        if (!is_dump_name(entry.d_name))
            continue;
        char path[PATH_CAP];
        stat_t info;
        snprintf(path, sizeof(path), CRASH_DIRECTORY "/%s", entry.d_name);
        if (stat(path, &info) != 0)
            continue;
        printf("%s  %u bytes\n", entry.d_name, (unsigned)info.st_size);
        listed++;
    }
    if (listed == 0)
        printf("no crash dumps\n");
    return 0;
}

static int show_dump(const char *number) {
    char *end;
    unsigned long index = strtoul(number, &end, 10);
    if (*number == '\0' || *end != '\0' || index == 0) {
        dprintf(2, "usage: crashinfo [N | --collect]\n");
        return STATUS_USAGE;
    }
    char path[PATH_CAP];
    snprintf(path, sizeof(path), CRASH_DIRECTORY "/crash-%lu.txt", index);
    long fd = open(path, OPEN_READ);
    if (fd < 0) {
        dprintf(2, "crashinfo: %s: no such dump\n", path);
        return STATUS_FAILURE;
    }
    char chunk[CHUNK];
    long got;
    while ((got = read((int)fd, chunk, sizeof(chunk))) > 0)
        write(1, chunk, (size_t)got);
    close((int)fd);
    return got < 0 ? STATUS_FAILURE : 0;
}

static int collect_dump(void) {
    char path[PATH_CAP];
    long rc = crash_collect(path, sizeof(path));
    if (rc < 0) {
        dprintf(2, "crashinfo: cannot collect the dump (error %ld)\n", -rc);
        return STATUS_FAILURE;
    }
    if (rc == 0)
        printf("no pending dump\n");
    else
        printf("saved %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1)
        return list_dumps();
    if (argc == 2 && strcmp(argv[1], "--collect") == 0)
        return collect_dump();
    if (argc == 2)
        return show_dump(argv[1]);
    dprintf(2, "usage: crashinfo [N | --collect]\n");
    return STATUS_USAGE;
}
