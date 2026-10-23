#pragma once

#include <stdint.h>

int ata_init(void);
const char *ata_model(void);
uint32_t ata_sector_count(void);
int ata_is_present(void);
int ata_read28(uint32_t lba, uint8_t count, void *buffer);
