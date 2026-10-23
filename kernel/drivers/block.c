#include "block.h"
#include "ata.h"
#include "virtio_blk.h"

static block_dev_t primary_dev;
static int has_primary = 0;

void block_init(void) {
    virtio_blk_init();
    if (virtio_blk_present()) {
        primary_dev.name = "vblk0";
        primary_dev.model = "Virtio Block Device";
        primary_dev.sector_size = 512;
        primary_dev.sector_count = (uint32_t)virtio_blk_capacity();
        primary_dev.read = virtio_blk_read;
        primary_dev.write = virtio_blk_write;
        has_primary = 1;
    } else if (ata_is_present()) {
        primary_dev.name = "ata0";
        primary_dev.model = ata_model();
        primary_dev.sector_size = 512;
        primary_dev.sector_count = ata_sector_count();
        primary_dev.read = ata_read28;
        primary_dev.write = 0; // ATA write not implemented
        has_primary = 1;
    } else {
        has_primary = 0;
    }
}

const block_dev_t *block_primary(void) {
    return has_primary ? &primary_dev : 0;
}

int block_read(uint32_t lba, uint8_t count, void *buffer) {
    if (!has_primary || !primary_dev.read) return -1;
    return primary_dev.read(lba, count, buffer);
}

int block_write(uint32_t lba, uint8_t count, void *buffer) {
    if (!has_primary || !primary_dev.write) return -1;
    return primary_dev.write(lba, count, buffer);
}
