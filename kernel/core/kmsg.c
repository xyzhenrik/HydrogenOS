#include "kmsg.h"

#include <stdint.h>

enum {
    KMSG_CAPACITY = 8192, /* power of two: positions wrap with a mask */
};

static char ring[KMSG_CAPACITY];
/* Total characters ever pushed; the oldest one still held is at
   total - KMSG_CAPACITY. */
static uint64_t total;

void kmsg_push(char c) {
    ring[total & (KMSG_CAPACITY - 1)] = c;
    total++;
}

size_t kmsg_copy_tail(char *out, size_t cap, unsigned max_lines) {
    if (cap == 0)
        return 0;
    uint64_t oldest = total > KMSG_CAPACITY ? total - KMSG_CAPACITY : 0;
    uint64_t start = total;
    unsigned lines = 0;
    size_t bytes = 0;

    /* Walk back from the end; a trailing newline ends the last line rather
       than starting an empty one. */
    while (start > oldest) {
        char c = ring[(start - 1) & (KMSG_CAPACITY - 1)];
        if (c == '\n' && start != total) {
            if (++lines == max_lines)
                break;
        }
        if (c != '\r')
            bytes++;
        if (bytes >= cap - 1)
            break;
        start--;
    }

    size_t length = 0;
    for (uint64_t pos = start; pos < total && length < cap - 1; ++pos) {
        char c = ring[pos & (KMSG_CAPACITY - 1)];
        if (c == '\r')
            continue;
        if (c != '\n' && c != '\t' && ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e))
            c = '?';
        out[length++] = c;
    }
    out[length] = '\0';
    return length;
}
