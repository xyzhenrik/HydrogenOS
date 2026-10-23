#pragma once

#include <stdint.h>

void panic(const char *msg, const char *file, int line);

#define PANIC(msg) panic((msg), __FILE__, __LINE__)

#define ASSERT(cond)                                     \
    do {                                                 \
        if (!(cond))                                     \
            panic("ASSERT: " #cond, __FILE__, __LINE__); \
    } while (0)
