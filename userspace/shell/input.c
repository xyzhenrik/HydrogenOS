#include "input.h"

#include "interrupt.h"
#include "mini_libc.h"
#include "output.h"

enum { ERRNO_INTERRUPTED = -4 };

/* One byte per read: whatever follows the newline belongs to the command that
 * is about to run, which reads the same pipe. */
int input_read_line(char *buf, int cap) {
    int len = 0;
    int overflow = 0;
    for (;;) {
        char c;
        long n = read(0, &c, 1);
        if (n == ERRNO_INTERRUPTED) {
            interrupt_clear();
            continue;
        }
        if (n <= 0) {
            if (len == 0 && !overflow)
                return -1;
            break;
        }
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (len + 1 < cap)
            buf[len++] = c;
        else
            overflow = 1;
    }
    if (overflow) {
        out_error("shell", 0, "line too long");
        len = 0;
    }
    buf[len] = 0;
    return len;
}
