#include "pci.h"
#include "io.h"
#include "serial.h"
#include "paging.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

pci_device_t pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;

static uintptr_t pci_next_mmio_vaddr = 0xFFFF900000000000ULL;

/* -----------------------------------------------------------------------
 * Low-level config space helpers
 * ----------------------------------------------------------------------- */

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint8_t offset) {
    return (1u << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func <<  8)
         | (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    uint32_t val = inl(PCI_CONFIG_DATA);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func,
                          uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    uint32_t val = inl(PCI_CONFIG_DATA);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset, uint16_t val) {
    /* Read-modify-write to keep the other 16 bits intact */
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    uint32_t cur = inl(PCI_CONFIG_DATA);
    unsigned shift = (offset & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |= ((uint32_t)val << shift);
    outl(PCI_CONFIG_DATA, cur);
}

/* -----------------------------------------------------------------------
 * Small hex-print helpers (no malloc/printf)
 * ----------------------------------------------------------------------- */

static void serial_hex8(uint8_t v) {
    const char *h = "0123456789ABCDEF";
    char buf[3];
    buf[0] = h[(v >> 4) & 0xF];
    buf[1] = h[v & 0xF];
    buf[2] = '\0';
    serial_write(buf);
}

static void serial_hex16(uint16_t v) {
    serial_hex8((uint8_t)(v >> 8));
    serial_hex8((uint8_t)(v & 0xFF));
}

/* -----------------------------------------------------------------------
 * BAR size probing
 * ----------------------------------------------------------------------- */

static void probe_bar(uint8_t bus, uint8_t slot, uint8_t func, int i,
                      pci_device_t *dev) {
    uint8_t bar_off = (uint8_t)(PCI_BAR0 + i * 4);
    uint32_t orig = pci_config_read32(bus, slot, func, bar_off);

    dev->bar[i]       = orig;
    dev->bar_vaddr[i] = 0;

    if (orig == 0) {
        dev->bar_size[i]  = 0;
        dev->bar_is_io[i] = 0;
        return;
    }

    dev->bar_is_io[i] = (orig & PCI_BAR_TYPE_IO) ? 1 : 0;

    /* Write all-ones, read back size mask, restore */
    pci_config_write32(bus, slot, func, bar_off, 0xFFFFFFFF);
    uint32_t sz = pci_config_read32(bus, slot, func, bar_off);
    pci_config_write32(bus, slot, func, bar_off, orig);

    if (sz == 0 || sz == 0xFFFFFFFF) {
        dev->bar_size[i] = 0;
        return;
    }

    sz &= ~0xFu;          /* mask lower 4 attribute bits */
    sz  = ~sz + 1u;       /* two's complement -> size */
    dev->bar_size[i] = sz;
}

/* -----------------------------------------------------------------------
 * Enumeration
 * ----------------------------------------------------------------------- */

void pci_init(void) {
    pci_device_count = 0;

    for (int bus = 0; bus < 256 && pci_device_count < PCI_MAX_DEVICES; ++bus) {
        for (int slot = 0; slot < 32 && pci_device_count < PCI_MAX_DEVICES; ++slot) {
            for (int func = 0; func < 8 && pci_device_count < PCI_MAX_DEVICES; ++func) {
                uint16_t vendor = pci_config_read16((uint8_t)bus, (uint8_t)slot,
                                                     (uint8_t)func, PCI_VENDOR_ID);
                if (vendor == 0xFFFF)
                    continue; /* no device */

                pci_device_t *dev = &pci_devices[pci_device_count];
                dev->bus        = (uint8_t)bus;
                dev->slot       = (uint8_t)slot;
                dev->func       = (uint8_t)func;
                dev->vendor_id  = vendor;
                dev->device_id  = pci_config_read16((uint8_t)bus, (uint8_t)slot,
                                                     (uint8_t)func, PCI_DEVICE_ID);
                dev->class_code = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                    (uint8_t)func, PCI_CLASS);
                dev->subclass   = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                    (uint8_t)func, PCI_SUBCLASS);
                dev->prog_if    = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                    (uint8_t)func, PCI_PROG_IF);
                dev->revision   = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                    (uint8_t)func, PCI_REVISION_ID);
                dev->header_type = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                     (uint8_t)func, PCI_HEADER_TYPE);
                dev->interrupt_line = pci_config_read8((uint8_t)bus, (uint8_t)slot,
                                                        (uint8_t)func, PCI_INTERRUPT_LINE);

                /* Probe BARs (only for type-0 / normal endpoint headers) */
                if ((dev->header_type & 0x7F) == 0x00) {
                    for (int i = 0; i < 6; ++i)
                        probe_bar((uint8_t)bus, (uint8_t)slot, (uint8_t)func, i, dev);
                } else {
                    for (int i = 0; i < 6; ++i) {
                        dev->bar[i]       = 0;
                        dev->bar_size[i]  = 0;
                        dev->bar_is_io[i] = 0;
                        dev->bar_vaddr[i] = 0;
                    }
                }

                ++pci_device_count;

                /* If not a multi-function device, skip remaining functions */
                if (func == 0 && !(dev->header_type & 0x80))
                    break;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Lookup helpers
 * ----------------------------------------------------------------------- */

int pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out) {
    for (int i = 0; i < pci_device_count; ++i) {
        if (pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device) {
            if (out) *out = pci_devices[i];
            return 0;
        }
    }
    return -1;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out) {
    for (int i = 0; i < pci_device_count; ++i) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass   == subclass) {
            if (out) *out = pci_devices[i];
            return 0;
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Command register helpers
 * ----------------------------------------------------------------------- */

void pci_enable_busmaster(const pci_device_t *dev) {
    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_config_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

void pci_enable_mmio(const pci_device_t *dev) {
    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_config_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

/* -----------------------------------------------------------------------
 * BAR accessors
 * ----------------------------------------------------------------------- */

uint32_t pci_bar_iobase(const pci_device_t *dev, int bar_idx) {
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    if (!dev->bar_is_io[bar_idx])    return 0;
    return dev->bar[bar_idx] & ~0x3u;
}

uintptr_t pci_bar_mmiobase(const pci_device_t *dev, int bar_idx) {
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    if (dev->bar_is_io[bar_idx])     return 0;
    return (uintptr_t)(dev->bar[bar_idx] & ~0xFu);
}

void *pci_map_bar(pci_device_t *dev, int bar_idx) {
    if (bar_idx < 0 || bar_idx >= 6) return 0;
    if (dev->bar_is_io[bar_idx])     return 0;
    if (dev->bar_size[bar_idx] == 0) return 0;

    /* Already mapped? */
    if (dev->bar_vaddr[bar_idx] != 0) {
        return (void *)dev->bar_vaddr[bar_idx];
    }

    uintptr_t phys = pci_bar_mmiobase(dev, bar_idx);
    size_t size = dev->bar_size[bar_idx];

    /* Align size to page boundary */
    size_t aligned_size = (size + 4095) & ~4095ULL;

    uintptr_t virt = pci_next_mmio_vaddr;
    pci_next_mmio_vaddr += aligned_size;

    paging_map_region(phys, virt, aligned_size, PAGE_RW);

    dev->bar_vaddr[bar_idx] = virt;
    return (void *)virt;
}

uint8_t pci_find_capability(const pci_device_t *dev, uint8_t cap_id) {
    uint16_t status = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_STATUS);
    if (!(status & (1 << 4))) {
        return 0;
    }

    uint8_t cap_ptr = pci_config_read8(dev->bus, dev->slot, dev->func, PCI_CAP_PTR);
    while (cap_ptr != 0) {
        uint8_t id = pci_config_read8(dev->bus, dev->slot, dev->func, cap_ptr);
        if (id == cap_id) {
            return cap_ptr;
        }
        cap_ptr = pci_config_read8(dev->bus, dev->slot, dev->func, cap_ptr + 1);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Debug dump
 * ----------------------------------------------------------------------- */

void pci_dump_devices(void) {
    serial_write("[PCI] Enumerated devices: ");
    /* small decimal for count */
    {
        int n = pci_device_count;
        if (n == 0) {
            serial_write("0");
        } else {
            char buf[12];
            int i = 0;
            while (n > 0 && i < 11) { buf[i++] = (char)('0' + n % 10); n /= 10; }
            buf[i] = '\0';
            /* reverse */
            for (int a = 0, b = i - 1; a < b; ++a, --b) {
                char tmp = buf[a]; buf[a] = buf[b]; buf[b] = tmp;
            }
            serial_write(buf);
        }
    }
    serial_write("\n");

    for (int i = 0; i < pci_device_count; ++i) {
        const pci_device_t *d = &pci_devices[i];
        serial_write("[PCI]   ");
        serial_hex8(d->bus);
        serial_write(":");
        serial_hex8(d->slot);
        serial_write(".");
        serial_hex8(d->func);
        serial_write(" vendor=");
        serial_hex16(d->vendor_id);
        serial_write(" device=");
        serial_hex16(d->device_id);
        serial_write(" class=");
        serial_hex8(d->class_code);
        serial_write(":");
        serial_hex8(d->subclass);
        serial_write(" irq=");
        serial_hex8(d->interrupt_line);
        serial_write("\n");
    }
}
