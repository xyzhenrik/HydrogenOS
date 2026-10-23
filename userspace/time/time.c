#include "mini_libc.h"

static unsigned long u64_to_dec(char *buf, unsigned long value) {
    char tmp[21];
    unsigned long i = 0;
    if (value == 0) {
        buf[0] = '0';
        return 1;
    }
    while (value > 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (unsigned long j = 0; j < i; ++j) {
        buf[j] = tmp[i - 1 - j];
    }
    return i;
}

int main(void) {
    char out[64];
    static const char prefix[] = "time_ms: ";
    unsigned long len = 0;
    for (unsigned long i = 0; i < sizeof(prefix) - 1; ++i) {
        out[len++] = prefix[i];
    }
    len += u64_to_dec(out + len, time_ms());
    out[len++] = '\n';
    write(1, out, len);
    return 0;
}
