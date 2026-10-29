#include "msi.h"
#include "pci.h"

#undef msi_enable
#undef msix_enable

static void msi_program(pci_device_t dev, uint8_t vector) {
    pci_enable_busmaster(&dev);

    uint8_t cap = pci_find_capability(&dev, PCI_CAP_ID_MSI);
    if (cap == 0) {
        return;
    }

    uint16_t ctrl = pci_config_read16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2));

    /* Disable MSI while programming. */
    ctrl &= (uint16_t)~(1u << 0);
    pci_config_write16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2), ctrl);

    int is_64 = (ctrl & (1u << 7)) != 0;
    int has_mask = (ctrl & (1u << 8)) != 0;

    /* Message address */
    pci_config_write32(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 4), MSI_ADDR_BASE);

    uint8_t data_off;
    if (is_64) {
        pci_config_write32(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 8), 0u);
        data_off = (uint8_t)(cap + 12);
    } else {
        data_off = (uint8_t)(cap + 8);
    }

    /* Message data: fixed delivery, edge-triggered. */
    pci_config_write16(dev.bus, dev.slot, dev.func, data_off, vector);

    if (has_mask) {
        uint8_t mask_off;
        if (is_64) {
            mask_off = (uint8_t)(cap + 16);
        } else {
            mask_off = (uint8_t)(cap + 12);
        }
        pci_config_write32(dev.bus, dev.slot, dev.func, mask_off, 0u);
    }

    /* Re-enable, single message. */
    ctrl |= (1u << 0);
    ctrl &= (uint16_t)~((7u) << 4);
    pci_config_write16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2), ctrl);
}

void msi_enable_1(pci_device_t dev) {
    msi_program(dev, 0);
}

void msi_enable_2(pci_device_t dev, uint8_t vector) {
    msi_program(dev, vector);
}

void msi_enable(pci_device_t dev, uint8_t vector) {
    msi_program(dev, vector);
}

static void msix_program(pci_device_t dev, uint8_t vector) {
    pci_enable_busmaster(&dev);

    uint8_t cap = pci_find_capability(&dev, PCI_CAP_ID_MSIX);
    if (cap == 0) {
        return;
    }

    /* Mask all (function mask) while updating table. */
    uint16_t ctrl = pci_config_read16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2));
    ctrl |= (1u << 14);
    pci_config_write16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2), ctrl);

    uint32_t table_reg = pci_config_read32(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 4));
    uint8_t table_bir = (uint8_t)(table_reg & 0x7u);
    uint32_t table_off = table_reg & ~0x7u;

    void *bar = pci_map_bar(&dev, table_bir);
    if (bar != 0) {
        volatile uint32_t *entry = (volatile uint32_t *)((unsigned char *)bar + table_off);
        entry[0] = MSI_ADDR_BASE;
        entry[1] = 0u;
        entry[2] = vector;
        entry[3] = 0u; /* unmasked */
    }

    /* Enable MSI-X, clear function mask. */
    ctrl = pci_config_read16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2));
    ctrl &= (uint16_t)~(1u << 14);
    ctrl |= (1u << 15);
    pci_config_write16(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 2), ctrl);
}

void msix_enable_1(pci_device_t dev) {
    msix_program(dev, 0);
}

void msix_enable_2(pci_device_t dev, uint8_t vector) {
    msix_program(dev, vector);
}

void msix_enable(pci_device_t dev, uint8_t vector) {
    msix_program(dev, vector);
}