#include "rtc.h"

#include "acpi.h"
#include "core/time.h"
#include "errno.h"
#include "io.h"

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

/* An update cycle lasts < 2 ms; each inb costs ~1 us, so this bounds a dead RTC. */
#define RTC_UIP_SPIN_LIMIT 1000000U
#define RTC_STABLE_READ_ATTEMPTS 8

#define RTC_DEFAULT_CENTURY 20

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
    uint8_t century_reg = acpi_century_register();
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
