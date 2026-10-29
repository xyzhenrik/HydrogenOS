#pragma once

#include <stdint.h>

#define DIRENT_NAME_MAX 255

typedef struct hydrogen_dirent {
    uint32_t d_ino;
    uint8_t  d_type;    /* 4 = dir, 8 = regular file, 10 = symlink */
    char     d_name[DIRENT_NAME_MAX + 1];
} hydrogen_dirent_t;
