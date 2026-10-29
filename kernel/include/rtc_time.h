#pragma once

#include <stdint.h>

/* SYS_RTC_TIME result. All fields are UTC; layout is part of the syscall ABI. */
typedef struct hydrogen_rtc_time {
    int64_t  epoch_seconds;  /* seconds since 1970-01-01 00:00:00 UTC */
    uint32_t millisecond;    /* 0..999 */
    uint16_t year;
    uint8_t  month;          /* 1..12 */
    uint8_t  day;            /* 1..31 */
    uint8_t  hour;           /* 0..23 */
    uint8_t  minute;         /* 0..59 */
    uint8_t  second;         /* 0..59 */
    uint8_t  weekday;        /* 0 = Sunday .. 6 = Saturday */
    uint8_t  reserved[4];    /* always zero */
} hydrogen_rtc_time_t;
