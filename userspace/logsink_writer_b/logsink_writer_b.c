#include "mini_libc.h"

int main(void) {
    static const char payload[] = "sink-b\n";
    if (write(1, payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        return 1;
    }
    return 0;
}
