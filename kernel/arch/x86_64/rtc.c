#include "rtc.h"

#include "core/time.h"
#include "errno.h"
#include "higher_half.h"
#include "io.h"
#include "paging.h"

#include <stddef.h>
#include <stdint.h>

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

#define RTC_REG_SECONDS  0x00
#define RTC_REG_MINUTES  0x02
#define RTC_REG_HOURS    0x04
#define RTC_REG_DAY      0x07
#define RTC_REG_MONTH    0x08
#define RTC_REG_YEAR     0x09
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B

#define RTC_STATUS_A_UPDATE_IN_PROGRESS 0x80
#define RTC_STATUS_B_24_HOUR            0x02
#define RTC_STATUS_B_BINARY             0x04
#define RTC_HOUR_PM                     0x80

/* CMOS register indices 0x00..0x7F; bit 7 of the index port is the NMI mask. */
#define RTC_CENTURY_REG_MAX 0x7F

/* An update cycle lasts < 2 ms; each inb costs ~1 us, so this bounds a dead RTC. */
#define RTC_UIP_SPIN_LIMIT 1000000U
#define RTC_STABLE_READ_ATTEMPTS 8

#define RTC_DEFAULT_CENTURY 20

#define ACPI_EBDA_SEGMENT_PTR 0x40EULL
#define ACPI_EBDA_SCAN_LEN    0x400ULL
#define ACPI_BIOS_AREA_START  0xE0000ULL
#define ACPI_BIOS_AREA_END    0x100000ULL
#define ACPI_RSDP_V1_LEN      20U
#define ACPI_TABLE_MAX_LEN    0x10000U
#define ACPI_FADT_CENTURY_OFFSET 108U

#define PAGE_SIZE_4K 0x1000ULL

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

typedef struct RtcRawTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} RtcRawTime;

static int64_t rtc_epoch_base_ms;
static int rtc_ready;

/* Firmware tables are reached through the higher-half alias of low physical memory. */
static const volatile void *phys_view(uint64_t phys) {
    return (const volatile void *)(uintptr_t)(KERNEL_VMA_BASE + phys);
}

static int phys_range_mapped(uint64_t phys, uint64_t len) {
    if (len == 0 || phys + len < phys ||
        phys + len > UINT64_MAX - KERNEL_VMA_BASE)
        return 0;
    for (uint64_t page = phys & ~(PAGE_SIZE_4K - 1); page < phys + len;
         page += PAGE_SIZE_4K) {
        if (!paging_is_mapped((uintptr_t)(KERNEL_VMA_BASE + page)))
            return 0;
    }
    return 1;
}

static int acpi_checksum_ok(const volatile uint8_t *bytes, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

static int signature_equals(const volatile char *have, const char *want,
                            size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (have[i] != want[i])
            return 0;
    }
    return 1;
}

static const volatile AcpiRsdp *acpi_scan_rsdp(uint64_t start, uint64_t len) {
    if (!phys_range_mapped(start, len))
        return NULL;
    for (uint64_t addr = start; addr + ACPI_RSDP_V1_LEN <= start + len;
         addr += 16) {
        const volatile AcpiRsdp *rsdp = phys_view(addr);
        if (!signature_equals(rsdp->signature, "RSD PTR ", 8))
            continue;
        if (acpi_checksum_ok((const volatile uint8_t *)rsdp, ACPI_RSDP_V1_LEN))
            return rsdp;
    }
    return NULL;
}

static const volatile AcpiRsdp *acpi_find_rsdp(void) {
    if (phys_range_mapped(ACPI_EBDA_SEGMENT_PTR, sizeof(uint16_t))) {
        uint16_t segment =
            *(const volatile uint16_t *)phys_view(ACPI_EBDA_SEGMENT_PTR);
        uint64_t ebda = (uint64_t)segment << 4;
        if (ebda != 0) {
            const volatile AcpiRsdp *rsdp =
                acpi_scan_rsdp(ebda, ACPI_EBDA_SCAN_LEN);
            if (rsdp)
                return rsdp;
        }
    }
    return acpi_scan_rsdp(ACPI_BIOS_AREA_START,
                          ACPI_BIOS_AREA_END - ACPI_BIOS_AREA_START);
}

static const volatile AcpiSdtHeader *acpi_map_table(uint64_t phys) {
    if (phys == 0 || !phys_range_mapped(phys, sizeof(AcpiSdtHeader)))
        return NULL;
    const volatile AcpiSdtHeader *table = phys_view(phys);
    uint32_t len = table->length;
    if (len < sizeof(AcpiSdtHeader) || len > ACPI_TABLE_MAX_LEN)
        return NULL;
    if (!phys_range_mapped(phys, len))
        return NULL;
    if (!acpi_checksum_ok((const volatile uint8_t *)table, len))
        return NULL;
    return table;
}

/* Returns the FADT century CMOS register index, or 0 if ACPI does not name one. */
static uint8_t acpi_fadt_century_register(void) {
    const volatile AcpiRsdp *rsdp = acpi_find_rsdp();
    if (!rsdp)
        return 0;

    const volatile AcpiSdtHeader *root = NULL;
    uint32_t entry_size = sizeof(uint32_t);
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0 &&
        rsdp->length >= sizeof(AcpiRsdp) &&
        acpi_checksum_ok((const volatile uint8_t *)rsdp, sizeof(AcpiRsdp))) {
        root = acpi_map_table(rsdp->xsdt_address);
        entry_size = sizeof(uint64_t);
    }
    if (!root) {
        root = acpi_map_table(rsdp->rsdt_address);
        entry_size = sizeof(uint32_t);
    }
    if (!root)
        return 0;

    uint32_t count = (root->length - (uint32_t)sizeof(AcpiSdtHeader)) / entry_size;
    const volatile uint8_t *entries =
        (const volatile uint8_t *)root + sizeof(AcpiSdtHeader);
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t phys;
        if (entry_size == sizeof(uint64_t))
            phys = *(const volatile uint64_t *)(entries + (size_t)i * entry_size);
        else
            phys = *(const volatile uint32_t *)(entries + (size_t)i * entry_size);
        const volatile AcpiSdtHeader *table = acpi_map_table(phys);
        if (!table || !signature_equals(table->signature, "FACP", 4))
            continue;
        if (table->length <= ACPI_FADT_CENTURY_OFFSET)
            return 0;
        uint8_t reg = ((const volatile uint8_t *)table)[ACPI_FADT_CENTURY_OFFSET];
        return reg <= RTC_CENTURY_REG_MAX ? reg : 0;
    }
    return 0;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, reg);
    return inb(CMOS_DATA_PORT);
}

static int rtc_wait_update_done(void) {
    for (uint32_t i = 0; i < RTC_UIP_SPIN_LIMIT; ++i) {
        if (!(cmos_read(RTC_REG_STATUS_A) & RTC_STATUS_A_UPDATE_IN_PROGRESS))
            return 0;
    }
    return -EIO;
}

static int rtc_read_raw(uint8_t century_reg, RtcRawTime *out) {
    if (rtc_wait_update_done() != 0)
        return -EIO;
    out->second = cmos_read(RTC_REG_SECONDS);
    out->minute = cmos_read(RTC_REG_MINUTES);
    out->hour = cmos_read(RTC_REG_HOURS);
    out->day = cmos_read(RTC_REG_DAY);
    out->month = cmos_read(RTC_REG_MONTH);
    out->year = cmos_read(RTC_REG_YEAR);
    out->century = century_reg ? cmos_read(century_reg) : 0;
    return 0;
}

static int rtc_raw_equal(const RtcRawTime *a, const RtcRawTime *b) {
    return a->second == b->second && a->minute == b->minute &&
           a->hour == b->hour && a->day == b->day && a->month == b->month &&
           a->year == b->year && a->century == b->century;
}

/* The RTC may tick between register reads; accept only two identical snapshots. */
static int rtc_read_stable(uint8_t century_reg, RtcRawTime *out) {
    RtcRawTime prev;
    if (rtc_read_raw(century_reg, &prev) != 0)
        return -EIO;
    for (int attempt = 0; attempt < RTC_STABLE_READ_ATTEMPTS; ++attempt) {
        RtcRawTime cur;
        if (rtc_read_raw(century_reg, &cur) != 0)
            return -EIO;
        if (rtc_raw_equal(&prev, &cur)) {
            *out = cur;
            return 0;
        }
        prev = cur;
    }
    return -EIO;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + (value >> 4) * 10);
}

static int is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint8_t days_in_month(int64_t year, uint8_t month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year))
        return 29;
    return days[month - 1];
}

/* Proleptic Gregorian calendar, after H. Hinnant's days_from_civil. */
static int64_t days_from_civil(int64_t year, uint8_t month, uint8_t day) {
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t yoe = year - era * 400;
    int64_t mp = month > 2 ? month - 3 : month + 9;
    int64_t doy = (153 * mp + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t days, int64_t *year, uint8_t *month,
                            uint8_t *day) {
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    *month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    *year = yoe + era * 400 + (*month <= 2);
}

static int rtc_decode(const RtcRawTime *raw, uint8_t status_b,
                      int64_t *epoch_seconds) {
    int binary = (status_b & RTC_STATUS_B_BINARY) != 0;
    int pm = (raw->hour & RTC_HOUR_PM) != 0;
    uint8_t second = raw->second;
    uint8_t minute = raw->minute;
    uint8_t hour = (uint8_t)(raw->hour & ~RTC_HOUR_PM);
    uint8_t day = raw->day;
    uint8_t month = raw->month;
    uint8_t year = raw->year;
    uint8_t century = raw->century;
    if (!binary) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        century = bcd_to_binary(century);
    }

    if (!(status_b & RTC_STATUS_B_24_HOUR)) {
        if (hour < 1 || hour > 12)
            return -EIO;
        hour = (uint8_t)(hour % 12 + (pm ? 12 : 0));
    }

    /* Firmware without an ACPI century register (or with garbage in it) gets 20xx. */
    if (century < 19 || century > 99)
        century = RTC_DEFAULT_CENTURY;
    int64_t full_year = (int64_t)century * 100 + year;

    /* rtc_now() relies on a non-negative epoch. */
    if (full_year < 1970)
        return -EIO;
    if (second > 59 || minute > 59 || hour > 23 || year > 99 || month < 1 ||
        month > 12 || day < 1 || day > days_in_month(full_year, month))
        return -EIO;

    int64_t days = days_from_civil(full_year, month, day);
    *epoch_seconds = days * 86400 + (int64_t)hour * 3600 +
                     (int64_t)minute * 60 + second;
    return 0;
}

int rtc_init(void) {
    uint8_t century_reg = acpi_fadt_century_register();
    RtcRawTime raw;
    if (rtc_read_stable(century_reg, &raw) != 0)
        return -EIO;
    int64_t epoch_seconds;
    if (rtc_decode(&raw, cmos_read(RTC_REG_STATUS_B), &epoch_seconds) != 0)
        return -EIO;
    rtc_epoch_base_ms = epoch_seconds * 1000 - (int64_t)time_ms();
    rtc_ready = 1;
    return 0;
}

int rtc_now(hydrogen_rtc_time_t *out) {
    if (!out || !rtc_ready)
        return -EIO;
    int64_t now_ms = rtc_epoch_base_ms + (int64_t)time_ms();
    int64_t seconds = now_ms / 1000;
    int64_t days = seconds / 86400;
    int64_t of_day = seconds % 86400;
    int64_t year;
    uint8_t month;
    uint8_t day;
    civil_from_days(days, &year, &month, &day);

    out->epoch_seconds = seconds;
    out->millisecond = (uint32_t)(now_ms % 1000);
    out->year = (uint16_t)year;
    out->month = month;
    out->day = day;
    out->hour = (uint8_t)(of_day / 3600);
    out->minute = (uint8_t)(of_day % 3600 / 60);
    out->second = (uint8_t)(of_day % 60);
    /* 1970-01-01 was a Thursday. */
    out->weekday = (uint8_t)((days + 4) % 7);
    for (size_t i = 0; i < sizeof(out->reserved); ++i)
        out->reserved[i] = 0;
    return 0;
}
