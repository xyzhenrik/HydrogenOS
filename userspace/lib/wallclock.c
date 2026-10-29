#include "wallclock.h"

#include "syscall.h"

#define SECONDS_PER_DAY 86400

static const char weekday_names[7][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char month_names[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* Keeps conversions far away from int64 overflow in the day arithmetic. */
#define TIME_YEAR_LIMIT 100000000LL

static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

int time_is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int time_days_in_month(int64_t year, int month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && time_is_leap_year(year))
        return 29;
    return days[month - 1];
}

/* After H. Hinnant, "chrono-Compatible Low-Level Date Algorithms". */
int64_t time_days_from_civil(int64_t year, int month, int day) {
    year -= month <= 2;
    int64_t era = floor_div(year, 400);
    int64_t yoe = year - era * 400;
    int64_t mp = month > 2 ? month - 3 : month + 9;
    int64_t doy = (153 * mp + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int time_civil_from_epoch(int64_t epoch_seconds, CivilTime *out) {
    if (!out)
        return -1;
    int64_t days = floor_div(epoch_seconds, SECONDS_PER_DAY);
    if (days > TIME_YEAR_LIMIT * 366 || days < -TIME_YEAR_LIMIT * 366)
        return -1;
    int64_t of_day = epoch_seconds - days * SECONDS_PER_DAY;

    int64_t z = days + 719468;
    int64_t era = floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1);
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);
    int64_t year = yoe + era * 400 + (month <= 2);

    out->year = year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)(of_day / 3600);
    out->minute = (uint8_t)(of_day % 3600 / 60);
    out->second = (uint8_t)(of_day % 60);
    /* 1970-01-01 was a Thursday. */
    int64_t weekday = (days + 4) % 7;
    out->weekday = (uint8_t)(weekday < 0 ? weekday + 7 : weekday);
    out->yearday = (uint16_t)(days - time_days_from_civil(year, 1, 1));
    return 0;
}

int time_epoch_from_civil(const CivilTime *civil, int64_t *epoch_seconds) {
    if (!civil || !epoch_seconds)
        return -1;
    if (civil->year > TIME_YEAR_LIMIT || civil->year < -TIME_YEAR_LIMIT)
        return -1;
    if (civil->month < 1 || civil->month > 12 || civil->day < 1 ||
        civil->day > time_days_in_month(civil->year, civil->month) ||
        civil->hour > 23 || civil->minute > 59 || civil->second > 59)
        return -1;
    int64_t days = time_days_from_civil(civil->year, civil->month, civil->day);
    *epoch_seconds = days * SECONDS_PER_DAY + (int64_t)civil->hour * 3600 +
                     (int64_t)civil->minute * 60 + civil->second;
    return 0;
}

typedef struct FormatCursor {
    char *buf;
    size_t size;
    size_t len;
    int overflow;
} FormatCursor;

static void emit_char(FormatCursor *c, char ch) {
    if (c->len + 1 >= c->size) {
        c->overflow = 1;
        return;
    }
    c->buf[c->len++] = ch;
}

static void emit_str(FormatCursor *c, const char *s) {
    while (*s)
        emit_char(c, *s++);
}

static void emit_number(FormatCursor *c, int64_t value, int min_digits) {
    char digits[20];
    int n = 0;
    uint64_t magnitude = value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
    if (value < 0)
        emit_char(c, '-');
    do {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude > 0);
    while (n < min_digits)
        digits[n++] = '0';
    while (n > 0)
        emit_char(c, digits[--n]);
}

static int emit_conversion(FormatCursor *c, char spec, const CivilTime *t) {
    switch (spec) {
    case 'a':
        if (t->weekday > 6)
            return -1;
        emit_str(c, weekday_names[t->weekday]);
        return 0;
    case 'b':
        if (t->month < 1 || t->month > 12)
            return -1;
        emit_str(c, month_names[t->month - 1]);
        return 0;
    case 'd': emit_number(c, t->day, 2); return 0;
    case 'H': emit_number(c, t->hour, 2); return 0;
    case 'M': emit_number(c, t->minute, 2); return 0;
    case 'S': emit_number(c, t->second, 2); return 0;
    case 'Y': emit_number(c, t->year, 4); return 0;
    case 'm': emit_number(c, t->month, 2); return 0;
    case 'j': emit_number(c, (int64_t)t->yearday + 1, 3); return 0;
    case 'F':
        emit_number(c, t->year, 4);
        emit_char(c, '-');
        emit_number(c, t->month, 2);
        emit_char(c, '-');
        emit_number(c, t->day, 2);
        return 0;
    case 'T':
        emit_number(c, t->hour, 2);
        emit_char(c, ':');
        emit_number(c, t->minute, 2);
        emit_char(c, ':');
        emit_number(c, t->second, 2);
        return 0;
    case '%': emit_char(c, '%'); return 0;
    default: return -1;
    }
}

int time_format(char *buf, size_t size, const char *fmt, const CivilTime *civil) {
    if (!buf || size == 0 || !fmt || !civil)
        return -1;
    FormatCursor c = {buf, size, 0, 0};
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            emit_char(&c, *p);
            continue;
        }
        ++p;
        if (!*p || emit_conversion(&c, *p, civil) != 0) {
            buf[0] = 0;
            return -1;
        }
    }
    if (c.overflow) {
        buf[0] = 0;
        return -1;
    }
    buf[c.len] = 0;
    return (int)c.len;
}

long wallclock_now(CivilTime *out, int64_t *epoch_seconds, uint32_t *millisecond) {
    hydrogen_rtc_time_t now;
    long rc = sys_rtc_time(&now);
    if (rc != 0)
        return rc;
    if (out && time_civil_from_epoch(now.epoch_seconds, out) != 0)
        return -1;
    if (epoch_seconds)
        *epoch_seconds = now.epoch_seconds;
    if (millisecond)
        *millisecond = now.millisecond;
    return 0;
}
