#include "mini_libc.h"

int main(void) {
    static const char payload[] = "init channel payload\n";
    if (write(1, payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        return 1;
    }
    return 0;
}
