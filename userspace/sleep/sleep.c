#include "mini_libc.h"

int main(void) {
    static const char start_msg[] = "sleeping 1000 ms...\n";
    static const char done_msg[] = "done\n";
    write(1, start_msg, sizeof(start_msg) - 1);
    sleep_ms(1000);
    write(1, done_msg, sizeof(done_msg) - 1);
    return 0;
}
