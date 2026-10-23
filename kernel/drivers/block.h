#pragma once

#include <stdint.h>

typedef struct block_dev {
    const char *name;
    const char *model;
    uint32_t sector_size;
    uint32_t sector_count;
    int (*read)(uint32_t lba, uint8_t count, void *buffer);
    int (*write)(uint32_t lba, uint8_t count, void *buffer);
} block_dev_t;

void block_init(void);
void block_cache_init(void);
const block_dev_t *block_primary(void);
int block_read(uint32_t lba, uint8_t count, void *buffer);
int block_write(uint32_t lba, uint8_t count, void *buffer);
int block_read_cached(uint32_t lba, uint8_t count, void *buffer);
