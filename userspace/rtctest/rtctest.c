#include "mini_libc.h"
#include "syscall.h"
#include "wallclock.h"

#define RTC_MIN_PLAUSIBLE_YEAR 2024
#define RTC_SLEEP_MS 250
/* The scheduler may wake us a tick early; allow one 10 ms PIT tick of slack. */
#define RTC_SLEEP_SLACK_MS 10
#define SECONDS_PER_DAY 86400

typedef struct EpochVector {
    int64_t epoch;
    int64_t year;
    uint8_t month, day, hour, minute, second, weekday;
    uint16_t yearday;
} EpochVector;

static const EpochVector epoch_vectors[] = {
    {0,             1970,  1,  1,  0,  0,  0, 4,   0},
    {-1,            1969, 12, 31, 23, 59, 59, 3, 364},
    {951782400,     2000,  2, 29,  0,  0,  0, 2,  59},
    {951868800,     2000,  3,  1,  0,  0,  0, 3,  60},
    {1709164800,    2024,  2, 29,  0,  0,  0, 4,  59},
    {4107456000,    2100,  2, 28,  0,  0,  0, 0,  58},
    {4107542400,    2100,  3,  1,  0,  0,  0, 1,  59},
    {-2203891200,   1900,  3,  1,  0,  0,  0, 4,  59},
    {253402300799,  9999, 12, 31, 23, 59, 59, 5, 364},
};

static const char *fail_reason;

static int fail(const char *reason) {
    if (!fail_reason)
        fail_reason = reason;
    return -1;
}

static int civil_matches(const CivilTime *t, const EpochVector *v) {
    return t->year == v->year && t->month == v->month && t->day == v->day &&
           t->hour == v->hour && t->minute == v->minute &&
           t->second == v->second && t->weekday == v->weekday &&
           t->yearday == v->yearday;
}

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int check_calendar_rules(void) {
    if (!time_is_leap_year(2000) || !time_is_leap_year(2024) ||
        time_is_leap_year(1900) || time_is_leap_year(2100) ||
        time_is_leap_year(2023))
        return fail("leap years");
    if (time_days_in_month(2023, 2) != 28 || time_days_in_month(2024, 2) != 29 ||
        time_days_in_month(2100, 2) != 28 || time_days_in_month(2024, 4) != 30 ||
        time_days_in_month(2024, 12) != 31 || time_days_in_month(2024, 0) != 0 ||
        time_days_in_month(2024, 13) != 0)
        return fail("days in month");
    return 0;
}

static int check_vectors(void) {
    for (size_t i = 0; i < sizeof(epoch_vectors) / sizeof(epoch_vectors[0]); ++i) {
        const EpochVector *v = &epoch_vectors[i];
        CivilTime t;
        int64_t back;
        if (time_civil_from_epoch(v->epoch, &t) != 0 || !civil_matches(&t, v))
            return fail("epoch to civil vector");
        if (time_epoch_from_civil(&t, &back) != 0 || back != v->epoch)
            return fail("civil to epoch vector");
    }
    return 0;
}

static int check_invalid_civil(void) {
    int64_t out;
    CivilTime t = {2023, 2, 29, 0, 0, 0, 0, 0};
    if (time_epoch_from_civil(&t, &out) == 0)
        return fail("accepted 2023-02-29");
    t.year = 2024;
    t.month = 13;
    t.day = 1;
    if (time_epoch_from_civil(&t, &out) == 0)
        return fail("accepted month 13");
    t.month = 1;
    t.hour = 24;
    if (time_epoch_from_civil(&t, &out) == 0)
        return fail("accepted hour 24");
    if (time_civil_from_epoch(0, 0) == 0)
        return fail("accepted NULL civil");
    return 0;
}

/* Walks every day 1899..2101 and checks the calendar advances by exactly one day. */
static int check_day_sweep(void) {
    int64_t start = time_days_from_civil(1899, 1, 1);
    int64_t end = time_days_from_civil(2101, 12, 31);
    CivilTime prev;
    if (time_civil_from_epoch(start * SECONDS_PER_DAY, &prev) != 0)
        return fail("sweep start");
    for (int64_t day = start + 1; day <= end; ++day) {
        CivilTime cur;
        int64_t back;
        int64_t epoch = day * SECONDS_PER_DAY + 43199;
        if (time_civil_from_epoch(epoch, &cur) != 0)
            return fail("sweep convert");
        if (time_epoch_from_civil(&cur, &back) != 0 || back != epoch)
            return fail("sweep round trip");
        if (cur.weekday != (prev.weekday + 1) % 7)
            return fail("sweep weekday");
        int new_month = cur.day == 1;
        int new_year = new_month && cur.month == 1;
        if (new_month) {
            if (prev.day != time_days_in_month(prev.year, prev.month))
                return fail("sweep month length");
            if (cur.month != (new_year ? 1 : prev.month + 1))
                return fail("sweep month step");
        } else if (cur.day != prev.day + 1 || cur.month != prev.month) {
            return fail("sweep day step");
        }
        if (new_year ? (cur.year != prev.year + 1 || cur.yearday != 0)
                     : (cur.year != prev.year || cur.yearday != prev.yearday + 1))
            return fail("sweep year step");
        prev = cur;
    }
    return 0;
}

static int check_format(void) {
    char buf[48];
    CivilTime t;
    if (time_civil_from_epoch(0, &t) != 0)
        return fail("format setup");
    if (time_format(buf, sizeof(buf), TIME_FORMAT_SHORT, &t) != 16 ||
        !str_equal(buf, "Thu 01 Jan 00:00"))
        return fail("format short");
    if (time_civil_from_epoch(951782400 + 14 * 3600 + 5 * 60 + 9, &t) != 0)
        return fail("format setup");
    if (time_format(buf, sizeof(buf), TIME_FORMAT_LONG, &t) < 0 ||
        !str_equal(buf, "Tue 29 Feb 2000 14:05:09 UTC"))
        return fail("format long");
    if (time_format(buf, sizeof(buf), "%F %T %j %m %%", &t) < 0 ||
        !str_equal(buf, "2000-02-29 14:05:09 060 02 %"))
        return fail("format iso");
    if (time_format(buf, 8, TIME_FORMAT_SHORT, &t) != -1 || buf[0] != 0)
        return fail("format overflow");
    if (time_format(buf, sizeof(buf), "%q", &t) != -1)
        return fail("format unknown conversion");
    return 0;
}

static int check_kernel_sample(const hydrogen_rtc_time_t *k) {
    CivilTime t;
    if (k->year < RTC_MIN_PLAUSIBLE_YEAR)
        return fail("year implausible");
    if (k->month < 1 || k->month > 12 || k->day < 1 ||
        k->day > time_days_in_month(k->year, k->month) || k->hour > 23 ||
        k->minute > 59 || k->second > 59 || k->weekday > 6 ||
        k->millisecond > 999)
        return fail("field out of range");
    for (size_t i = 0; i < sizeof(k->reserved); ++i) {
        if (k->reserved[i] != 0)
            return fail("reserved not zero");
    }
    if (time_civil_from_epoch(k->epoch_seconds, &t) != 0 ||
        t.year != k->year || t.month != k->month || t.day != k->day ||
        t.hour != k->hour || t.minute != k->minute || t.second != k->second ||
        t.weekday != k->weekday)
        return fail("kernel fields disagree with epoch");
    return 0;
}

static int64_t sample_ms(const hydrogen_rtc_time_t *k) {
    return k->epoch_seconds * 1000 + k->millisecond;
}

static int check_kernel_clock(void) {
    hydrogen_rtc_time_t first;
    hydrogen_rtc_time_t second;
    if (sys_rtc_time(0) >= 0)
        return fail("NULL pointer accepted");
    if (sys_rtc_time((struct hydrogen_rtc_time *)0xFFFF800000001000UL) >= 0)
        return fail("kernel pointer accepted");

    unsigned long mono_start = time_ms();
    if (sys_rtc_time(&first) != 0)
        return fail("sys_rtc_time");
    if (check_kernel_sample(&first) != 0)
        return -1;
    sleep_ms(RTC_SLEEP_MS);
    if (sys_rtc_time(&second) != 0)
        return fail("sys_rtc_time after sleep");
    unsigned long mono_end = time_ms();
    if (check_kernel_sample(&second) != 0)
        return -1;

    /* Wall time is boot RTC + the monotonic timer, so both clocks must agree. */
    int64_t wall_delta = sample_ms(&second) - sample_ms(&first);
    if (wall_delta < RTC_SLEEP_MS - RTC_SLEEP_SLACK_MS)
        return fail("wall clock did not advance across sleep");
    if (wall_delta > (int64_t)(mono_end - mono_start))
        return fail("wall clock outran monotonic clock");

    CivilTime now;
    char line[48];
    if (time_civil_from_epoch(second.epoch_seconds, &now) == 0 &&
        time_format(line, sizeof(line), TIME_FORMAT_LONG, &now) > 0)
        dprintf(2, "rtctest: wall clock %s\n", line);
    return 0;
}

int main(void) {
    if (check_calendar_rules() == 0 && check_vectors() == 0 &&
        check_invalid_civil() == 0 && check_day_sweep() == 0 &&
        check_format() == 0 && check_kernel_clock() == 0) {
        printf("RTC PASS\n");
        return 0;
    }
    printf("RTC FAIL %s\n", fail_reason ? fail_reason : "unknown");
    return 1;
}
