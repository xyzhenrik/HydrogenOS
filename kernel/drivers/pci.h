#pragma once
#include <stdint.h>

/* PCI Config Space Offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_CAP_PTR         0x34
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

/* PCI Command Register bits */
#define PCI_CMD_IO_SPACE    (1 << 0)
#define PCI_CMD_MEM_SPACE   (1 << 1)
#define PCI_CMD_BUS_MASTER  (1 << 2)

/* PCI Class Codes */
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_SIMPLE_COMM   0x07
#define PCI_SUBCLASS_IDE        0x01
#define PCI_SUBCLASS_SATA       0x06

/* BAR types */
#define PCI_BAR_TYPE_IO         0x01
#define PCI_BAR_TYPE_MEM32      0x00
#define PCI_BAR_TYPE_MEM64      0x04

#define PCI_MAX_DEVICES 64

typedef struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint32_t bar[6];       /* raw BAR values */
    uint32_t bar_size[6];  /* BAR sizes */
    uint8_t  bar_is_io[6]; /* 1 = I/O port, 0 = MMIO */
    uintptr_t bar_vaddr[6]; /* mapped virtual address */
} pci_device_t;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_config_read8 (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

void pci_init(void);
int  pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out);
int  pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out);
void pci_enable_busmaster(const pci_device_t *dev);
void pci_enable_mmio(const pci_device_t *dev);
uint32_t pci_bar_iobase(const pci_device_t *dev, int bar_idx);
uintptr_t pci_bar_mmiobase(const pci_device_t *dev, int bar_idx);
void *pci_map_bar(pci_device_t *dev, int bar_idx);
uint8_t pci_find_capability(const pci_device_t *dev, uint8_t cap_id);
void pci_dump_devices(void);  /* Debug: alle Geraete ausgeben */

extern pci_device_t pci_devices[PCI_MAX_DEVICES];
extern int pci_device_count;
