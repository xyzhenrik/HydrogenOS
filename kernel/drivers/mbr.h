#pragma once

#include <stdint.h>

typedef struct mbr_part {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t sectors;
} __attribute__((packed)) mbr_part_t;

typedef struct mbr {
    uint8_t boot_code[446];
    mbr_part_t parts[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

int mbr_read(mbr_t *out);
int mbr_valid(const mbr_t *m);
