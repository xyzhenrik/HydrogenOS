#include "time.h"

#include "errno.h"
#include "syscall.h"
#include "wallclock.h"

#define MILLISECONDS_PER_SECOND 1000
#define NANOSECONDS_PER_MILLISECOND 1000000L

int clock_gettime(clockid_t clock, struct timespec *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    switch (clock) {
    case CLOCK_MONOTONIC: {
        unsigned long ms = sys_time_ms();
        out->tv_sec = (time_t)(ms / MILLISECONDS_PER_SECOND);
        out->tv_nsec = (long)(ms % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
        return 0;
    }
    case CLOCK_REALTIME: {
        hydrogen_rtc_time_t now;
        long rc = sys_rtc_time(&now);
        if (rc != 0) {
            errno = (int)-rc;
            return -1;
        }
        out->tv_sec = now.epoch_seconds;
        out->tv_nsec = (long)now.millisecond * NANOSECONDS_PER_MILLISECOND;
        return 0;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

time_t time(time_t *out) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return (time_t)-1;
    if (out)
        *out = now.tv_sec;
    return now.tv_sec;
}

int gettimeofday(struct timeval *out, void *zone) {
    struct timespec now;
    if (zone || !out) {
        errno = EINVAL;
        return -1;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;
    out->tv_sec = now.tv_sec;
    out->tv_usec = now.tv_nsec / 1000;
    return 0;
}
