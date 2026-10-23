#include "ata.h"
#include "io.h"

#define ATA_PRIMARY_IO 0x1F0
#define ATA_DATA       (ATA_PRIMARY_IO + 0)
#define ATA_ERROR      (ATA_PRIMARY_IO + 1)
#define ATA_SECCOUNT0  (ATA_PRIMARY_IO + 2)
#define ATA_LBA0       (ATA_PRIMARY_IO + 3)
#define ATA_LBA1       (ATA_PRIMARY_IO + 4)
#define ATA_LBA2       (ATA_PRIMARY_IO + 5)
#define ATA_HDDEVSEL   (ATA_PRIMARY_IO + 6)
#define ATA_COMMAND    (ATA_PRIMARY_IO + 7)
#define ATA_STATUS     (ATA_PRIMARY_IO + 7)

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static int ata_present = 0;
static char ata_model_str[41];
static uint32_t ata_sectors = 0;

static int ata_wait_bsy(void) {
    for (int i = 0; i < 100000; ++i) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; ++i) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static int ata_identify(uint16_t *out_words) {
    outb(ATA_HDDEVSEL, 0xA0); // master
    io_wait();
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xEC); // IDENTIFY

    uint8_t status = inb(ATA_STATUS);
    if (status == 0) return -1; // no device

    if (ata_wait_bsy() != 0) return -1;
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0) return -1; // ATAPI
    if (ata_wait_drq() != 0) return -1;

    for (int i = 0; i < 256; ++i) {
        out_words[i] = inw(ATA_DATA);
    }
    return 0;
}

int ata_init(void) {
    uint16_t id[256];
    if (ata_identify(id) != 0) {
        ata_present = 0;
        ata_sectors = 0;
        ata_model_str[0] = 0;
        return -1;
    }

    for (int i = 0; i < 20; ++i) {
        uint16_t w = id[27 + i];
        ata_model_str[i * 2] = (char)(w >> 8);
        ata_model_str[i * 2 + 1] = (char)(w & 0xFF);
    }
    ata_model_str[40] = 0;
    for (int i = 39; i >= 0; --i) {
        if (ata_model_str[i] == ' ') ata_model_str[i] = 0;
        else break;
    }
    ata_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    ata_present = 1;
    return 0;
}

const char *ata_model(void) {
    return ata_present ? ata_model_str : 0;
}

uint32_t ata_sector_count(void) {
    return ata_present ? ata_sectors : 0;
}

int ata_is_present(void) {
    return ata_present;
}

int ata_read28(uint32_t lba, uint8_t count, void *buffer) {
    if (!ata_present || count == 0) return -1;

    outb(ATA_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    io_wait();
    outb(ATA_SECCOUNT0, count);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, 0x20); // READ SECTORS

    uint8_t *buf = (uint8_t *)buffer;
    for (uint8_t s = 0; s < count; ++s) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; ++i) {
            uint16_t data = inw(ATA_DATA);
            *buf++ = (uint8_t)(data & 0xFF);
            *buf++ = (uint8_t)(data >> 8);
        }
    }
    return 0;
}
