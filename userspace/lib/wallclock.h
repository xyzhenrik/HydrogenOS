#pragma once

#include <stddef.h>
#include <stdint.h>

/* Mirror of kernel/include/rtc_time.h; the layout is part of the SYS_RTC_TIME ABI. */
typedef struct hydrogen_rtc_time {
    int64_t  epoch_seconds;
    uint32_t millisecond;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
    uint8_t  reserved[4];
} hydrogen_rtc_time_t;

/* Broken-down UTC time in the proleptic Gregorian calendar. */
typedef struct CivilTime {
    int64_t  year;
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;     /* 0..23 */
    uint8_t  minute;   /* 0..59 */
    uint8_t  second;   /* 0..59 */
    uint8_t  weekday;  /* 0 = Sunday .. 6 = Saturday */
    uint16_t yearday;  /* 0..365 */
} CivilTime;

/* Fixed-width taskbar format, e.g. "Thu 29 Oct 14:05". */
#define TIME_FORMAT_SHORT "%a %d %b %H:%M"
/* e.g. "Thu 29 Oct 2026 14:05:09 UTC". */
#define TIME_FORMAT_LONG  "%a %d %b %Y %H:%M:%S UTC"

int time_is_leap_year(int64_t year);
/* Returns 0 for a month outside 1..12. */
int time_days_in_month(int64_t year, int month);
/* Days since 1970-01-01; the date must be valid (see time_epoch_from_civil). */
int64_t time_days_from_civil(int64_t year, int month, int day);

/* Both return 0, or -1 on a NULL argument or an out-of-range field. */
int time_civil_from_epoch(int64_t epoch_seconds, CivilTime *out);
int time_epoch_from_civil(const CivilTime *civil, int64_t *epoch_seconds);

/*
 * strftime-like formatting. Supported: %a %b %d %H %M %S %Y %m %j %F %T %%.
 * Returns the length written (without NUL), or -1 if the buffer is too small
 * or the format contains an unknown conversion.
 */
int time_format(char *buf, size_t size, const char *fmt, const CivilTime *civil);

/*
 * Current wall time from SYS_RTC_TIME; any output pointer may be NULL.
 * Returns 0, the syscall's -errno, or -1 if the epoch is not representable.
 */
long wallclock_now(CivilTime *out, int64_t *epoch_seconds, uint32_t *millisecond);
