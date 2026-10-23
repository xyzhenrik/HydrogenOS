#include "mini_libc.h"

int main(void) {
    static const char prompt[] = "echo: type a line and press Enter\n";
    char buf[128];

    write(1, prompt, sizeof(prompt) - 1);
    long n = read(0, buf, sizeof(buf));
    if (n > 0) {
        write(1, buf, (unsigned long)n);
    }
    return 0;
}
