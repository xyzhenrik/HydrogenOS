#include "acpi.h"

#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "paging.h"
#include "serial.h"
#include "string.h"

/*
 * Firmware tables are reached through a dedicated kernel-half window instead
 * of the boot identity map: tables may live above the identity-mapped first
 * GiB, and kernel-half mappings created before the first process exists are
 * shared by every user address space (needed for the reset register, which is
 * written from syscall context).
 */
#define ACPI_WINDOW_BASE 0xFFFFA00000000000ULL
#define ACPI_WINDOW_SIZE (64ULL * 1024 * 1024)
#define ACPI_PAGE_SIZE 0x1000ULL
#define ACPI_TABLE_MAX_LENGTH (8U * 1024 * 1024)
#define ACPI_MAX_SSDTS 16

#define BIOS_DATA_AREA 0x400ULL
#define BIOS_DATA_AREA_SIZE 0x100U
#define BIOS_EBDA_SEGMENT_OFFSET 0x0E
#define BIOS_EBDA_SCAN_SIZE 1024U
#define BIOS_EBDA_LIMIT 0xA0000ULL
#define BIOS_ROM_START 0xE0000ULL
#define BIOS_ROM_END 0x100000ULL

#define RSDP_V1_LENGTH 20U
#define RSDP_MAX_LENGTH 256U

#define ACPI_SPACE_MEMORY 0
#define ACPI_SPACE_IO 1
#define ACPI_SPACE_PCI_CONFIG 2

/* CMOS register indices 0x00..0x7F; bit 7 of the index port is the NMI mask. */
#define CMOS_REGISTER_MAX 0x7F

/* FADT field offsets (ACPI 6.x, table 5.9); older revisions are shorter. */
#define FADT_DSDT 40
#define FADT_SMI_CMD 48
#define FADT_ACPI_ENABLE 52
#define FADT_PM1A_CNT_BLK 64
#define FADT_PM1B_CNT_BLK 68
#define FADT_FLAGS 112
#define FADT_CENTURY 108
#define FADT_RESET_REG 116
#define FADT_RESET_VALUE 128
#define FADT_X_DSDT 140
#define FADT_X_PM1A_CNT_BLK 172
#define FADT_X_PM1B_CNT_BLK 184
#define FADT_SLEEP_CONTROL_REG 244

#define FADT_FLAG_RESET_REG_SUP (1U << 10)
#define FADT_FLAG_HW_REDUCED_ACPI (1U << 20)

#define PM1_CNT_SCI_EN (1U << 0)
#define PM1_CNT_SLP_TYP_SHIFT 10
#define PM1_CNT_SLP_TYP_MASK (7U << PM1_CNT_SLP_TYP_SHIFT)
#define PM1_CNT_SLP_EN (1U << 13)

#define SLEEP_CONTROL_SLP_TYP_SHIFT 2
#define SLEEP_CONTROL_SLP_EN (1U << 5)

#define AML_ZERO_OP 0x00
#define AML_ONE_OP 0x01
#define AML_NAME_OP 0x08
#define AML_BYTE_PREFIX 0x0A
#define AML_WORD_PREFIX 0x0B
#define AML_DWORD_PREFIX 0x0C
#define AML_QWORD_PREFIX 0x0E
#define AML_PACKAGE_OP 0x12
#define AML_ROOT_CHAR '\\'
#define AML_PARENT_PREFIX_CHAR '^'

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* Generous upper bounds; port 0x80 writes take roughly a microsecond. */
#define ACPI_ENABLE_TIMEOUT_US 3000000U
#define ACPI_SETTLE_US 500000U

typedef struct __attribute__((packed)) AcpiRsdp {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} AcpiRsdp;

typedef struct __attribute__((packed)) AcpiSdtHeader {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} AcpiSdtHeader;

typedef struct __attribute__((packed)) AcpiGas {
  uint8_t space_id;
  uint8_t bit_width;
  uint8_t bit_offset;
  uint8_t access_size;
  uint64_t address;
} AcpiGas;

typedef struct AcpiRegister {
  uint8_t space_id;
  uint64_t address;
  volatile uint8_t *mmio;
} AcpiRegister;

typedef struct AcpiState {
  uint8_t rsdp_revision;
  char oem_id[7];
  int uses_xsdt;
  uint64_t root_table;
  uint8_t fadt_revision;
  uint8_t century_register;
  int hw_reduced;
  uint16_t pm1a_control;
  uint16_t pm1b_control;
  uint16_t smi_command;
  uint8_t acpi_enable_value;
  int sleep_control_valid;
  AcpiRegister sleep_control;
  int s5_valid;
  uint8_t slp_typ_a;
  uint8_t slp_typ_b;
  int reset_valid;
  AcpiRegister reset_register;
  uint8_t reset_value;
} AcpiState;

static AcpiState acpi_state;
static uintptr_t acpi_window_next = ACPI_WINDOW_BASE;

static void acpi_log_hex(uint64_t value) {
  static const char digits[] = "0123456789ABCDEF";
  char buf[19];
  int shift = 60;
  int pos = 0;
  buf[pos++] = '0';
  buf[pos++] = 'x';
  while (shift > 0 && ((value >> shift) & 0xF) == 0)
    shift -= 4;
  for (; shift >= 0; shift -= 4)
    buf[pos++] = digits[(value >> shift) & 0xF];
  buf[pos] = '\0';
  serial_write(buf);
}

static void acpi_log_dec(uint32_t value) {
  char buf[11];
  int pos = (int)sizeof(buf) - 1;
  buf[pos] = '\0';
  do {
    buf[--pos] = (char)('0' + value % 10);
    value /= 10;
  } while (value != 0);
  serial_write(&buf[pos]);
}

static void acpi_delay_us(uint32_t us) {
  while (us-- > 0)
    io_wait();
}

static uint8_t acpi_checksum(const void *data, size_t length) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint8_t sum = 0;
  for (size_t i = 0; i < length; ++i)
    sum = (uint8_t)(sum + bytes[i]);
  return sum;
}

static uint32_t read_u32(const uint8_t *base, size_t offset) {
  uint32_t value;
  memcpy(&value, base + offset, sizeof(value));
  return value;
}

static uint64_t read_u64(const uint8_t *base, size_t offset) {
  uint64_t value;
  memcpy(&value, base + offset, sizeof(value));
  return value;
}

static const void *acpi_map(uint64_t phys, size_t length) {
  if (length == 0)
    return NULL;
  uint64_t end = phys + length;
  if (end < phys)
    return NULL;
  uint64_t first = phys & ~(ACPI_PAGE_SIZE - 1);
  uint64_t last = (end + ACPI_PAGE_SIZE - 1) & ~(ACPI_PAGE_SIZE - 1);
  if (last < end)
    return NULL;
  uint64_t span = last - first;
  if (span > ACPI_WINDOW_BASE + ACPI_WINDOW_SIZE - acpi_window_next)
    return NULL;
  uintptr_t virt = acpi_window_next;
  acpi_window_next += span;
  paging_map_region((uintptr_t)first, virt, (size_t)span, PAGE_RW);
  /* paging_map_region maps in ascending order and stops at the first
   * page-table allocation failure, so the last page proves the whole span. */
  if (!paging_is_mapped(virt + span - ACPI_PAGE_SIZE))
    return NULL;
  return (const void *)(virt + (uintptr_t)(phys - first));
}

static const AcpiSdtHeader *acpi_map_table(uint64_t phys) {
  if (phys == 0)
    return NULL;
  const AcpiSdtHeader *header = acpi_map(phys, sizeof(AcpiSdtHeader));
  if (!header)
    return NULL;
  uint32_t length = header->length;
  if (length < sizeof(AcpiSdtHeader) || length > ACPI_TABLE_MAX_LENGTH)
    return NULL;
  const AcpiSdtHeader *table = acpi_map(phys, length);
  if (!table || acpi_checksum(table, length) != 0)
    return NULL;
  return table;
}

static int acpi_rsdp_valid(const AcpiRsdp *rsdp, uint64_t bytes_available) {
  if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
    return 0;
  if (acpi_checksum(rsdp, RSDP_V1_LENGTH) != 0)
    return 0;
  if (rsdp->revision < 2)
    return 1;
  if (bytes_available < sizeof(AcpiRsdp))
    return 0;
  if (rsdp->length < sizeof(AcpiRsdp) || rsdp->length > RSDP_MAX_LENGTH ||
      rsdp->length > bytes_available)
    return 0;
  return acpi_checksum(rsdp, rsdp->length) == 0;
}

static const AcpiRsdp *acpi_scan_rsdp(uint64_t start, uint64_t end) {
  /* Map RSDP_MAX_LENGTH past the end so a v2 RSDP in the last paragraph can
   * be checksummed; the BIOS area is followed by ordinary RAM. */
  const uint8_t *area = acpi_map(start, (size_t)(end - start) + RSDP_MAX_LENGTH);
  if (!area)
    return NULL;
  for (uint64_t offset = 0; offset + RSDP_V1_LENGTH <= end - start; offset += 16) {
    const AcpiRsdp *rsdp = (const AcpiRsdp *)(area + offset);
    if (acpi_rsdp_valid(rsdp, end - start - offset + RSDP_MAX_LENGTH))
      return rsdp;
  }
  return NULL;
}

static const AcpiRsdp *acpi_find_rsdp(void) {
  const uint8_t *bda = acpi_map(BIOS_DATA_AREA, BIOS_DATA_AREA_SIZE);
  if (bda) {
    uint16_t segment;
    memcpy(&segment, bda + BIOS_EBDA_SEGMENT_OFFSET, sizeof(segment));
    uint64_t ebda = (uint64_t)segment << 4;
    if (ebda >= BIOS_DATA_AREA + BIOS_DATA_AREA_SIZE &&
        ebda + BIOS_EBDA_SCAN_SIZE <= BIOS_EBDA_LIMIT) {
      const AcpiRsdp *rsdp = acpi_scan_rsdp(ebda, ebda + BIOS_EBDA_SCAN_SIZE);
      if (rsdp)
        return rsdp;
    }
  }
  return acpi_scan_rsdp(BIOS_ROM_START, BIOS_ROM_END);
}

static int aml_parse_pkg_length(const uint8_t *p, const uint8_t *end,
                                uint32_t *length, size_t *consumed) {
  if (p >= end)
    return -1;
  uint8_t lead = p[0];
  size_t extra = lead >> 6;
  if (extra == 0) {
    *length = lead & 0x3F;
    *consumed = 1;
    return 0;
  }
  if ((size_t)(end - p) < extra + 1)
    return -1;
  uint32_t value = lead & 0x0F;
  for (size_t i = 0; i < extra; ++i)
    value |= (uint32_t)p[1 + i] << (4 + 8 * i);
  *length = value;
  *consumed = extra + 1;
  return 0;
}

static int aml_parse_integer(const uint8_t *p, const uint8_t *end,
                             uint64_t *value, size_t *consumed) {
  if (p >= end)
    return -1;
  size_t width;
  switch (p[0]) {
  case AML_ZERO_OP:
    *value = 0;
    *consumed = 1;
    return 0;
  case AML_ONE_OP:
    *value = 1;
    *consumed = 1;
    return 0;
  case AML_BYTE_PREFIX:
    width = 1;
    break;
  case AML_WORD_PREFIX:
    width = 2;
    break;
  case AML_DWORD_PREFIX:
    width = 4;
    break;
  case AML_QWORD_PREFIX:
    width = 8;
    break;
  default:
    return -1;
  }
  if ((size_t)(end - p) < width + 1)
    return -1;
  uint64_t result = 0;
  for (size_t i = 0; i < width; ++i)
    result |= (uint64_t)p[1 + i] << (8 * i);
  *value = result;
  *consumed = width + 1;
  return 0;
}

/* Accepts NameOp directly before the name segment, optionally separated by a
 * root or parent-prefix path ("Name (\_S5, ...)", "Name (^_S5, ...)"). */
static int aml_is_name_definition(const uint8_t *aml, size_t name_offset) {
  size_t i = name_offset;
  while (i > 0 && aml[i - 1] == AML_PARENT_PREFIX_CHAR)
    --i;
  if (i == name_offset && i > 0 && aml[i - 1] == AML_ROOT_CHAR)
    --i;
  return i > 0 && aml[i - 1] == AML_NAME_OP;
}

static int aml_parse_s5_package(const uint8_t *p, const uint8_t *end,
                                uint8_t *slp_typ_a, uint8_t *slp_typ_b) {
  if (p >= end || *p != AML_PACKAGE_OP)
    return -1;
  ++p;
  uint32_t pkg_length;
  size_t consumed;
  if (aml_parse_pkg_length(p, end, &pkg_length, &consumed) != 0)
    return -1;
  if (pkg_length < consumed + 1 || pkg_length > (size_t)(end - p))
    return -1;
  const uint8_t *pkg_end = p + pkg_length;
  p += consumed;
  uint8_t element_count = *p++;
  if (element_count == 0)
    return -1;

  uint64_t typ_a;
  if (aml_parse_integer(p, pkg_end, &typ_a, &consumed) != 0)
    return -1;
  p += consumed;
  uint64_t typ_b = typ_a;
  if (element_count >= 2 && aml_parse_integer(p, pkg_end, &typ_b, &consumed) != 0)
    return -1;
  if (typ_a > 7 || typ_b > 7)
    return -1;
  *slp_typ_a = (uint8_t)typ_a;
  *slp_typ_b = (uint8_t)typ_b;
  return 0;
}

static int acpi_find_s5(const AcpiSdtHeader *table, uint8_t *slp_typ_a,
                        uint8_t *slp_typ_b) {
  if (!table)
    return -1;
  const uint8_t *aml = (const uint8_t *)table;
  const uint8_t *end = aml + table->length;
  for (size_t i = sizeof(AcpiSdtHeader); i + 4 < table->length; ++i) {
    if (memcmp(aml + i, "_S5_", 4) != 0)
      continue;
    if (!aml_is_name_definition(aml, i))
      continue;
    if (aml_parse_s5_package(aml + i + 4, end, slp_typ_a, slp_typ_b) == 0)
      return 0;
  }
  return -1;
}

static int acpi_register_from_gas(const AcpiGas *gas, AcpiRegister *out) {
  if (gas->address == 0)
    return -1;
  out->space_id = gas->space_id;
  out->address = gas->address;
  out->mmio = NULL;
  switch (gas->space_id) {
  case ACPI_SPACE_MEMORY:
    out->mmio = (volatile uint8_t *)acpi_map(gas->address, 1);
    return out->mmio ? 0 : -1;
  case ACPI_SPACE_IO:
    return gas->address <= 0xFFFF ? 0 : -1;
  case ACPI_SPACE_PCI_CONFIG:
    /* Segment 0, bus 0 only: device in bits 47:32, function in 31:16. */
    if ((gas->address >> 48) != 0 || ((gas->address >> 32) & 0xFFFF) > 31 ||
        ((gas->address >> 16) & 0xFFFF) > 7 || (gas->address & 0xFFFF) > 0xFF)
      return -1;
    return 0;
  default:
    return -1;
  }
}

static void acpi_register_write8(const AcpiRegister *reg, uint8_t value) {
  switch (reg->space_id) {
  case ACPI_SPACE_MEMORY:
    *reg->mmio = value;
    break;
  case ACPI_SPACE_IO:
    outb((uint16_t)reg->address, value);
    break;
  case ACPI_SPACE_PCI_CONFIG: {
    uint32_t device = (uint32_t)((reg->address >> 32) & 0x1F);
    uint32_t function = (uint32_t)((reg->address >> 16) & 0x7);
    uint32_t offset = (uint32_t)(reg->address & 0xFF);
    outl(PCI_CONFIG_ADDRESS,
         (1U << 31) | (device << 11) | (function << 8) | (offset & 0xFC));
    outb((uint16_t)(PCI_CONFIG_DATA + (offset & 3)), value);
    break;
  }
  default:
    break;
  }
}

/* The FADT's 64-bit X_ block takes precedence whenever it is populated;
 * only system-I/O control blocks are supported. */
static uint16_t acpi_fadt_pm1_port(const uint8_t *fadt, uint32_t length,
                                   size_t x_offset, size_t legacy_offset) {
  if (length >= x_offset + sizeof(AcpiGas)) {
    AcpiGas gas;
    memcpy(&gas, fadt + x_offset, sizeof(gas));
    if (gas.address != 0 && gas.space_id == ACPI_SPACE_IO && gas.address <= 0xFFFF)
      return (uint16_t)gas.address;
  }
  uint32_t legacy = read_u32(fadt, legacy_offset);
  return legacy <= 0xFFFF ? (uint16_t)legacy : 0;
}

static uint64_t acpi_parse_fadt(const AcpiSdtHeader *fadt_header) {
  const uint8_t *fadt = (const uint8_t *)fadt_header;
  uint32_t length = fadt_header->length;
  acpi_state.fadt_revision = fadt_header->revision;
  if (length < FADT_FLAGS + 4)
    return 0;

  if (length > FADT_CENTURY && fadt[FADT_CENTURY] <= CMOS_REGISTER_MAX)
    acpi_state.century_register = fadt[FADT_CENTURY];

  uint32_t flags = read_u32(fadt, FADT_FLAGS);
  acpi_state.hw_reduced = (flags & FADT_FLAG_HW_REDUCED_ACPI) != 0;
  if (acpi_state.hw_reduced) {
    if (length >= FADT_SLEEP_CONTROL_REG + sizeof(AcpiGas)) {
      AcpiGas gas;
      memcpy(&gas, fadt + FADT_SLEEP_CONTROL_REG, sizeof(gas));
      acpi_state.sleep_control_valid =
          acpi_register_from_gas(&gas, &acpi_state.sleep_control) == 0;
    }
  } else {
    uint32_t smi_command = read_u32(fadt, FADT_SMI_CMD);
    acpi_state.smi_command = smi_command <= 0xFFFF ? (uint16_t)smi_command : 0;
    acpi_state.acpi_enable_value = fadt[FADT_ACPI_ENABLE];
    acpi_state.pm1a_control =
        acpi_fadt_pm1_port(fadt, length, FADT_X_PM1A_CNT_BLK, FADT_PM1A_CNT_BLK);
    acpi_state.pm1b_control =
        acpi_fadt_pm1_port(fadt, length, FADT_X_PM1B_CNT_BLK, FADT_PM1B_CNT_BLK);
  }

  if ((flags & FADT_FLAG_RESET_REG_SUP) && length > FADT_RESET_VALUE) {
    AcpiGas gas;
    memcpy(&gas, fadt + FADT_RESET_REG, sizeof(gas));
    acpi_state.reset_value = fadt[FADT_RESET_VALUE];
    acpi_state.reset_valid =
        acpi_register_from_gas(&gas, &acpi_state.reset_register) == 0;
  }

  uint64_t dsdt = 0;
  if (length >= FADT_X_DSDT + 8)
    dsdt = read_u64(fadt, FADT_X_DSDT);
  if (dsdt == 0)
    dsdt = read_u32(fadt, FADT_DSDT);
  return dsdt;
}

static void acpi_log_register(const AcpiRegister *reg) {
  switch (reg->space_id) {
  case ACPI_SPACE_MEMORY:
    serial_write("mem:");
    break;
  case ACPI_SPACE_IO:
    serial_write("io:");
    break;
  default:
    serial_write("pci:");
    break;
  }
  acpi_log_hex(reg->address);
}

static void acpi_log_summary(void) {
  serial_write("[ACPI] RSDP rev=");
  acpi_log_dec(acpi_state.rsdp_revision);
  serial_write(" oem=");
  serial_write(acpi_state.oem_id);
  serial_write(acpi_state.uses_xsdt ? " XSDT=" : " RSDT=");
  acpi_log_hex(acpi_state.root_table);
  serial_write(" FADT rev=");
  acpi_log_dec(acpi_state.fadt_revision);
  serial_write("\n[ACPI] ");
  if (acpi_state.hw_reduced) {
    serial_write("hw-reduced SLEEP_CONTROL=");
    if (acpi_state.sleep_control_valid)
      acpi_log_register(&acpi_state.sleep_control);
    else
      serial_write("none");
  } else {
    serial_write("PM1a_CNT=");
    acpi_log_hex(acpi_state.pm1a_control);
    serial_write(" PM1b_CNT=");
    if (acpi_state.pm1b_control)
      acpi_log_hex(acpi_state.pm1b_control);
    else
      serial_write("none");
  }
  serial_write(" S5=");
  if (acpi_state.s5_valid) {
    acpi_log_dec(acpi_state.slp_typ_a);
    serial_write("/");
    acpi_log_dec(acpi_state.slp_typ_b);
  } else {
    serial_write("none");
  }
  serial_write(" reset=");
  if (acpi_state.reset_valid) {
    acpi_log_register(&acpi_state.reset_register);
    serial_write("/");
    acpi_log_hex(acpi_state.reset_value);
  } else {
    serial_write("none");
  }
  serial_write("\n");
}

static void acpi_log_failure(const char *reason) {
  serial_write("[ACPI] unavailable: ");
  serial_write(reason);
  serial_write("\n");
}

void acpi_init(void) {
  const AcpiRsdp *rsdp = acpi_find_rsdp();
  if (!rsdp) {
    acpi_log_failure("no RSDP");
    return;
  }

  acpi_state.rsdp_revision = rsdp->revision;
  for (int i = 0; i < 6; ++i) {
    char c = rsdp->oem_id[i];
    acpi_state.oem_id[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  acpi_state.oem_id[6] = '\0';

  const AcpiSdtHeader *root = NULL;
  if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
    root = acpi_map_table(rsdp->xsdt_address);
    if (root && memcmp(root->signature, "XSDT", 4) == 0) {
      acpi_state.uses_xsdt = 1;
      acpi_state.root_table = rsdp->xsdt_address;
    } else {
      root = NULL;
    }
  }
  if (!root) {
    root = acpi_map_table(rsdp->rsdt_address);
    if (!root || memcmp(root->signature, "RSDT", 4) != 0) {
      acpi_log_failure("invalid RSDT/XSDT");
      return;
    }
    acpi_state.root_table = rsdp->rsdt_address;
  }

  size_t entry_size = acpi_state.uses_xsdt ? 8 : 4;
  size_t entry_count = (root->length - sizeof(AcpiSdtHeader)) / entry_size;
  const uint8_t *entries = (const uint8_t *)root + sizeof(AcpiSdtHeader);
  const AcpiSdtHeader *fadt = NULL;
  const AcpiSdtHeader *ssdts[ACPI_MAX_SSDTS];
  size_t ssdt_count = 0;
  for (size_t i = 0; i < entry_count; ++i) {
    uint64_t phys = acpi_state.uses_xsdt ? read_u64(entries, i * entry_size)
                                         : read_u32(entries, i * entry_size);
    const AcpiSdtHeader *table = acpi_map_table(phys);
    if (!table)
      continue;
    if (!fadt && memcmp(table->signature, "FACP", 4) == 0)
      fadt = table;
    else if (ssdt_count < ACPI_MAX_SSDTS && memcmp(table->signature, "SSDT", 4) == 0)
      ssdts[ssdt_count++] = table;
  }
  if (!fadt) {
    acpi_log_failure("no FADT");
    return;
  }

  uint64_t dsdt_phys = acpi_parse_fadt(fadt);
  const AcpiSdtHeader *dsdt = acpi_map_table(dsdt_phys);
  if (dsdt && memcmp(dsdt->signature, "DSDT", 4) != 0)
    dsdt = NULL;

  int s5_found = acpi_find_s5(dsdt, &acpi_state.slp_typ_a, &acpi_state.slp_typ_b) == 0;
  for (size_t i = 0; !s5_found && i < ssdt_count; ++i)
    s5_found = acpi_find_s5(ssdts[i], &acpi_state.slp_typ_a, &acpi_state.slp_typ_b) == 0;

  int control_ok = acpi_state.hw_reduced ? acpi_state.sleep_control_valid
                                         : acpi_state.pm1a_control != 0;
  acpi_state.s5_valid = s5_found && control_ok;

  acpi_log_summary();
  if (acpi_state.s5_valid)
    serial_write("ACPI OK\n");
  else if (!s5_found)
    acpi_log_failure(dsdt ? "no \\_S5 package" : "invalid DSDT");
  else
    acpi_log_failure("no sleep control register");
}

int acpi_s5_available(void) { return acpi_state.s5_valid; }

uint8_t acpi_century_register(void) { return acpi_state.century_register; }

static void acpi_enable_mode(void) {
  if ((inw(acpi_state.pm1a_control) & PM1_CNT_SCI_EN) != 0)
    return;
  if (acpi_state.smi_command == 0 || acpi_state.acpi_enable_value == 0)
    return;
  outb(acpi_state.smi_command, acpi_state.acpi_enable_value);
  for (uint32_t waited = 0; waited < ACPI_ENABLE_TIMEOUT_US; waited += 1000) {
    if ((inw(acpi_state.pm1a_control) & PM1_CNT_SCI_EN) != 0)
      return;
    acpi_delay_us(1000);
  }
}

static uint16_t pm1_control_value(uint16_t port, uint8_t slp_typ) {
  uint16_t value = inw(port);
  value &= (uint16_t)~(PM1_CNT_SLP_TYP_MASK | PM1_CNT_SLP_EN);
  return (uint16_t)(value | ((uint16_t)slp_typ << PM1_CNT_SLP_TYP_SHIFT));
}

void acpi_enter_s5(void) {
  if (!acpi_state.s5_valid)
    return;
  if (acpi_state.hw_reduced) {
    acpi_register_write8(&acpi_state.sleep_control,
                         (uint8_t)((acpi_state.slp_typ_a << SLEEP_CONTROL_SLP_TYP_SHIFT) |
                                   SLEEP_CONTROL_SLP_EN));
  } else {
    /* Proceed even if SCI_EN never latches: some firmware still honours
     * SLP_EN in that state, and failing here would gain nothing. */
    acpi_enable_mode();
    uint16_t value_a = pm1_control_value(acpi_state.pm1a_control, acpi_state.slp_typ_a);
    uint16_t value_b = 0;
    if (acpi_state.pm1b_control)
      value_b = pm1_control_value(acpi_state.pm1b_control, acpi_state.slp_typ_b);
    /* SLP_TYP is latched before SLP_EN is set, in both blocks. */
    outw(acpi_state.pm1a_control, value_a);
    if (acpi_state.pm1b_control)
      outw(acpi_state.pm1b_control, value_b);
    outw(acpi_state.pm1a_control, (uint16_t)(value_a | PM1_CNT_SLP_EN));
    if (acpi_state.pm1b_control)
      outw(acpi_state.pm1b_control, (uint16_t)(value_b | PM1_CNT_SLP_EN));
  }
  acpi_delay_us(ACPI_SETTLE_US);
}

void acpi_reset(void) {
  if (!acpi_state.reset_valid)
    return;
  acpi_register_write8(&acpi_state.reset_register, acpi_state.reset_value);
  acpi_delay_us(ACPI_SETTLE_US);
}
