#pragma once
#include "pci.h"
#include <stdint.h>

/* PCI capability IDs */
#define PCI_CAP_ID_MSI   0x05
#define PCI_CAP_ID_MSIX  0x11

/* MSI message address base (local APIC) */
#define MSI_ADDR_BASE    0xFEE00000u

/* Implementation entry points (arity-specific). Prefer the msi_enable /
 * msix_enable macros below at call sites. */
void msi_enable_1(pci_device_t dev);
void msi_enable_2(pci_device_t dev, uint8_t vector);

void msix_enable_1(pci_device_t dev);
void msix_enable_2(pci_device_t dev, uint8_t vector);

/* Compatibility symbols with the most common fixed signatures. */
void msi_enable(pci_device_t dev, uint8_t vector);
void msix_enable(pci_device_t dev, uint8_t vector);

/* Arity dispatch so callers may use either msi_enable(dev) or
 * msi_enable(dev, vector), and likewise for msix_enable. */
#define _MSI_OVERLOAD_SELECT(_1, _2, NAME, ...) NAME
#define _msi_dispatch(...) _MSI_OVERLOAD_SELECT(__VA_ARGS__, msi_enable_2, msi_enable_1)
#define msi_enable(...) _msi_dispatch(__VA_ARGS__)(__VA_ARGS__)

#define _MSIX_OVERLOAD_SELECT(_1, _2, NAME, ...) NAME
#define _msix_dispatch(...) _MSIX_OVERLOAD_SELECT(__VA_ARGS__, msix_enable_2, msix_enable_1)
#define msix_enable(...) _msix_dispatch(__VA_ARGS__)(__VA_ARGS__)