#include "mini_libc.h"

int main(void) {
    yield_cpu();
    yield_cpu();
    if (write(1, "w", 1) != 1) {
        write(2, "pollwakewriter: write failed\n", 29);
        return 1;
    }
    return 0;
}
