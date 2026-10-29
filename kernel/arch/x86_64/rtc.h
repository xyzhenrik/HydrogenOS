#pragma once

#include "rtc_time.h"

/*
 * Reads the CMOS real-time clock once and anchors wall-clock time to the
 * monotonic timer. Must run after time_init(). Returns 0 or -errno.
 */
int rtc_init(void);

/* Current UTC wall time (boot RTC value + monotonic ms). Returns 0 or -EIO. */
int rtc_now(hydrogen_rtc_time_t *out);
