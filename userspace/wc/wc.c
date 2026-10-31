#include "mini_libc.h"

/*
 * wc - count lines, words and bytes.
 *
 * Reads the files given, or standard input without arguments. -l, -w and -c
 * select the counts; without options all three are printed. A word is a run
 * of bytes other than ASCII white space.
 */

enum {
    WC_CHUNK = 512,
    WC_STATUS_FAILURE = 1,
    WC_STATUS_USAGE = 2,
};

enum {
    SHOW_LINES = 1 << 0,
    SHOW_WORDS = 1 << 1,
    SHOW_BYTES = 1 << 2,
};

typedef struct Counts {
    unsigned long lines;
    unsigned long words;
    unsigned long bytes;
} Counts;

static int is_space(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/* Counts everything readable from fd. Returns 0, or -1 on a read error. */
static int count_fd(int fd, Counts *counts) {
    unsigned char chunk[WC_CHUNK];
    int in_word = 0;

    counts->lines = 0;
    counts->words = 0;
    counts->bytes = 0;
    for (;;) {
        long got = read(fd, chunk, sizeof(chunk));
        if (got < 0)
            return -1;
        if (got == 0)
            return 0;
        counts->bytes += (unsigned long)got;
        for (long i = 0; i < got; ++i) {
            if (chunk[i] == '\n')
                counts->lines++;
            if (is_space(chunk[i])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                counts->words++;
            }
        }
    }
}

/* One output line: the selected counts, then the name when there is one. */
static void print_counts(const Counts *counts, int show, const char *name) {
    const char *separator = "";

    if (show & SHOW_LINES) {
        dprintf(1, "%s%lu", separator, counts->lines);
        separator = " ";
    }
    if (show & SHOW_WORDS) {
        dprintf(1, "%s%lu", separator, counts->words);
        separator = " ";
    }
    if (show & SHOW_BYTES)
        dprintf(1, "%s%lu", separator, counts->bytes);
    if (name)
        dprintf(1, " %s", name);
    dprintf(1, "\n");
}

int main(int argc, char **argv) {
    int show = 0;
    int first = 1;
    int status = 0;
    Counts total = {0, 0, 0};

    for (; first < argc && argv[first][0] == '-' && argv[first][1]; ++first) {
        for (const char *option = argv[first] + 1; *option; ++option) {
            if (*option == 'l') {
                show |= SHOW_LINES;
            } else if (*option == 'w') {
                show |= SHOW_WORDS;
            } else if (*option == 'c') {
                show |= SHOW_BYTES;
            } else {
                dprintf(2, "usage: wc [-lwc] [file]...\n");
                return WC_STATUS_USAGE;
            }
        }
    }
    if (!show)
        show = SHOW_LINES | SHOW_WORDS | SHOW_BYTES;

    if (first == argc) {
        Counts counts;
        if (count_fd(0, &counts) != 0) {
            dprintf(2, "wc: read failed\n");
            return WC_STATUS_FAILURE;
        }
        print_counts(&counts, show, 0);
        return 0;
    }
    for (int i = first; i < argc; ++i) {
        Counts counts;
        long fd = open(argv[i], OPEN_READ);
        int failed;

        if (fd < 0) {
            dprintf(2, "wc: %s: cannot open\n", argv[i]);
            status = WC_STATUS_FAILURE;
            continue;
        }
        failed = count_fd((int)fd, &counts) != 0;
        close((int)fd);
        if (failed) {
            dprintf(2, "wc: %s: read failed\n", argv[i]);
            status = WC_STATUS_FAILURE;
            continue;
        }
        print_counts(&counts, show, argv[i]);
        total.lines += counts.lines;
        total.words += counts.words;
        total.bytes += counts.bytes;
    }
    if (argc - first > 1)
        print_counts(&total, show, "total");
    return status;
}
