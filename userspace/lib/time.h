#pragma once

#include <stdint.h>

typedef int64_t time_t;
typedef int clockid_t;

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

/*
 * CLOCK_REALTIME is the UTC wall clock from SYS_RTC_TIME (millisecond
 * resolution); CLOCK_MONOTONIC counts from boot at timer-tick (10 ms)
 * resolution. Failures return -1 and set errno: EINVAL for an unknown clock or
 * a NULL result pointer, EIO when the wall clock is not available.
 */
int clock_gettime(clockid_t clock, struct timespec *out);
/* Seconds since 1970-01-01 UTC, or (time_t)-1 if the wall clock is unavailable. */
time_t time(time_t *out);
/* The time zone argument must be NULL. */
int gettimeofday(struct timeval *out, void *zone);
