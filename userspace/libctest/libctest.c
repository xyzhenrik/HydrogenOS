#include "mini_libc.h"
#include "errno.h"
#include "syscall.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

#define SCRATCH_FILE "/tmp/libctest.tmp"

#define CHECK(cond)                                    \
    do {                                               \
        if (!(cond))                                   \
            return fail(#cond, __LINE__);              \
    } while (0)

static int fail(const char *expression, int line) {
    dprintf(2, "LIBC FAIL: %s (line %d)\n", expression, line);
    return 1;
}

static int same(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* ------------------------------------------------------------------ */
/* string.h                                                            */
/* ------------------------------------------------------------------ */

static int check_string(void) {
    char buf[32];
    char *save = 0;
    char text[] = "  alpha,beta;;gamma ";

    CHECK(strlen("") == 0 && strlen("hydrogen") == 8);
    CHECK(strnlen("hydrogen", 3) == 3 && strnlen("hi", 10) == 2);
    CHECK(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 && strcmp("b", "a") > 0);
    CHECK(strcmp("ab", "abc") < 0 && strcmp("abc", "ab") > 0);
    CHECK(strcmp("\xff", "a") > 0);
    CHECK(strncmp("abcdef", "abcxyz", 3) == 0 && strncmp("abcdef", "abcxyz", 4) < 0);
    CHECK(strncmp("ab", "abc", 2) == 0 && strncmp("a", "b", 0) == 0);
    CHECK(memcmp("abc", "abd", 3) < 0 && memcmp("abc", "abc", 3) == 0 && memcmp("\xff", "\x01", 1) > 0);

    {
        const char *hay = "hello";
        CHECK(memchr(hay, 'l', 5) == hay + 2);
        CHECK(memchr(hay, 'l', 2) == 0);
        CHECK(memchr(hay, 0, 6) == hay + 5);
    }
    {
        const char *s = "a/b/c";
        CHECK(strchr(s, '/') == s + 1 && strrchr(s, '/') == s + 3);
        CHECK(strchr(s, 'z') == 0 && strrchr(s, 'z') == 0);
        CHECK(strchr(s, '\0') == s + 5 && strrchr(s, '\0') == s + 5);
        CHECK(strstr(s, "b/c") == s + 2 && strstr(s, "") == s && strstr(s, "c/") == 0);
        CHECK(strstr("aaab", "aab") != 0 && strstr("abc", "abcd") == 0);
        CHECK(strpbrk(s, "bc") == s + 2 && strpbrk(s, "xyz") == 0);
        CHECK(strspn("aabbc", "ab") == 4 && strspn("xyz", "ab") == 0);
        CHECK(strcspn("hello world", " ") == 5 && strcspn("abc", "xyz") == 3);
    }

    memset(buf, 'x', sizeof(buf));
    strcpy(buf, "ab");
    strcat(buf, "cd");
    CHECK(same(buf, "abcd") && buf[5] == 'x');
    memset(buf, 'x', sizeof(buf));
    strncpy(buf, "ab", 5);
    CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 0 && buf[4] == 0 && buf[5] == 'x');
    strncpy(buf, "abcdef", 3);
    CHECK(buf[2] == 'c' && buf[3] == 0);

    CHECK(same(strtok_r(text, ",; ", &save), "alpha"));
    CHECK(same(strtok_r(0, ",; ", &save), "beta"));
    CHECK(same(strtok_r(0, ",; ", &save), "gamma"));
    CHECK(strtok_r(0, ",; ", &save) == 0);

    memcpy(buf, "0123456789", 11);
    memmove(buf + 2, buf, 6);
    CHECK(same(buf, "0101234589"));
    memcpy(buf, "0123456789", 11);
    memmove(buf, buf + 3, 7);
    CHECK(same(buf, "3456789789"));
    memcpy(buf, "0123456789", 11);
    memmove(buf + 1, buf, 0);
    CHECK(same(buf, "0123456789"));
    memset(buf, 0x5a, 7);
    CHECK((unsigned char)buf[0] == 0x5a && (unsigned char)buf[6] == 0x5a && buf[7] == '7');

    char *copy = strdup("duplicate");
    CHECK(copy && same(copy, "duplicate"));
    free(copy);
    copy = strndup("truncate me", 8);
    CHECK(copy && same(copy, "truncate") && strlen(copy) == 8);
    free(copy);
    copy = strndup("short", 50);
    CHECK(copy && same(copy, "short"));
    free(copy);
    CHECK(strdup(0) == 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* stdlib.h: number parsing                                            */
/* ------------------------------------------------------------------ */

static int check_numbers(void) {
    char *end;

    CHECK(strtol("  -42abc", &end, 10) == -42 && same(end, "abc"));
    CHECK(strtol("+7", &end, 10) == 7 && *end == 0);
    CHECK(strtol("0x1F", &end, 0) == 31 && *end == 0);
    CHECK(strtol("0X1f", &end, 16) == 31 && *end == 0);
    CHECK(strtol("017", &end, 0) == 15 && *end == 0);
    CHECK(strtol("017", &end, 10) == 17);
    CHECK(strtol("0", &end, 0) == 0 && *end == 0);
    CHECK(strtol("0x", &end, 16) == 0 && same(end, "x"));
    CHECK(strtol("0xg", &end, 0) == 0 && same(end, "xg"));
    CHECK(strtol("zz", &end, 36) == 1295 && *end == 0);
    CHECK(strtol("101", &end, 2) == 5 && *end == 0);
    CHECK(strtol("12", &end, 2) == 1 && same(end, "2"));
    {
        const char *none = "  xyz";
        CHECK(strtol(none, &end, 10) == 0 && end == none);
        none = "";
        CHECK(strtol(none, &end, 10) == 0 && end == none);
        none = "-";
        CHECK(strtol(none, &end, 10) == 0 && end == none);
    }
    CHECK(strtol("12", 0, 10) == 12);

    errno = 0;
    CHECK(strtol("9223372036854775807", &end, 10) == LONG_MAX && errno == 0);
    CHECK(strtol("-9223372036854775808", &end, 10) == LONG_MIN && errno == 0);
    errno = 0;
    CHECK(strtol("9223372036854775808", &end, 10) == LONG_MAX && errno == ERANGE && *end == 0);
    errno = 0;
    CHECK(strtol("-9223372036854775809", &end, 10) == LONG_MIN && errno == ERANGE);
    errno = 0;
    CHECK(strtol("99999999999999999999999", &end, 10) == LONG_MAX && errno == ERANGE && *end == 0);
    errno = 0;
    CHECK(strtol("5", &end, 1) == 0 && errno == EINVAL);
    errno = 0;
    CHECK(strtol("5", &end, 37) == 0 && errno == EINVAL);

    errno = 0;
    CHECK(strtoul("18446744073709551615", &end, 10) == ULONG_MAX && errno == 0);
    CHECK(strtoul("0xffffffffffffffff", &end, 0) == ULONG_MAX && errno == 0);
    CHECK(strtoul("-1", &end, 10) == ULONG_MAX && errno == 0);
    errno = 0;
    CHECK(strtoul("18446744073709551616", &end, 10) == ULONG_MAX && errno == ERANGE);
    CHECK(strtoul("4294967296", &end, 10) == 4294967296UL);
    CHECK(strtoull("777", &end, 8) == 511);
    CHECK(strtoll("-123456789012", &end, 10) == -123456789012LL);
    CHECK(strtoll("0x7fffffffffffffff", &end, 16) == LLONG_MAX);

    CHECK(atoi("123xyz") == 123 && atoi("  -9") == -9 && atoi("abc") == 0 && atoi("") == 0);
    CHECK(atol("-5000000000") == -5000000000L);
    CHECK(atoll("9000000000000") == 9000000000000LL);
    CHECK(abs(-5) == 5 && abs(5) == 5 && labs(-5000000000L) == 5000000000L);
    CHECK(llabs(-7LL) == 7LL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* stdio.h: formatted output                                           */
/* ------------------------------------------------------------------ */

static int format_is(const char *expected, const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int length = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return length == (int)strlen(expected) && same(buf, expected);
}

static int check_format(void) {
    char buf[16];

    CHECK(format_is("-42", "%d", -42));
    CHECK(format_is("   42|42   |00042", "%5d|%-5d|%05d", 42, 42, 42));
    CHECK(format_is("+5  5", "%+d % d", 5, 5));
    CHECK(format_is("-0042", "%05d", -42));
    CHECK(format_is("+0042|42   |", "%+05d|%-05d|", 42, 42));
    CHECK(format_is("4294967295", "%u", 4294967295u));
    CHECK(format_is("ff FF 0xff 0XFF 010 10", "%x %X %#x %#X %#o %o", 255, 255, 255, 255, 8, 8));
    CHECK(format_is("0 0 0 0", "%x %#x %o %#o", 0, 0, 0, 0));
    CHECK(format_is("-9223372036854775808 18446744073709551615", "%ld %lu", LONG_MIN, ULONG_MAX));
    CHECK(format_is("-9223372036854775808", "%lld", LLONG_MIN));
    CHECK(format_is("12345 -7", "%zu %zd", (size_t)12345, (long)-7));
    CHECK(format_is("44 4464 200", "%hhd %hd %hhu", 300, 70000, 200 + 256));
    CHECK(format_is("deadbeefcafe", "%lx", 0xdeadbeefcafeUL));

    CHECK(format_is("abc|       abc|abc       |abc", "%s|%10s|%-10s|%.3s", "abc", "abc", "abc", "abcdef"));
    CHECK(format_is("    ab|", "%6.2s|", "abc"));
    CHECK(format_is("(null)", "%s", (const char *)0));
    CHECK(format_is("ab", "%.*s", 2, "abcdef"));
    CHECK(format_is("ok", "%c%c", 'o', 'k'));
    CHECK(format_is("    x|x    |", "%5c|%-5c|", 'x', 'x'));
    CHECK(format_is("100%", "%d%%", 100));

    CHECK(format_is("0x1234", "%p", (void *)0x1234));
    CHECK(format_is("0x0", "%p", (void *)0));
    CHECK(format_is("  0xabc|0xabc  ", "%7p|%-7p", (void *)0xabc, (void *)0xabc));

    CHECK(format_is("00042", "%.5d", 42));
    CHECK(format_is("   00042|00042   |", "%8.5d|%-8.5d|", 42, 42));
    CHECK(format_is("-0000000042", "%.10d", -42));
    CHECK(format_is("", "%.0d", 0));
    CHECK(format_is("   |", "%3.0d|", 0));
    CHECK(format_is("   42", "%05.0d", 42));
    CHECK(format_is("   00042", "%08.5d", 42));
    CHECK(format_is("0x0000ff", "%#08x", 255));
    CHECK(format_is("0x000000ff", "%#.8x", 255));
    CHECK(format_is("    42|42    |", "%*d|%-*d|", 6, 42, 6, 42));
    CHECK(format_is("42    |", "%*d|", -6, 42));
    CHECK(format_is("42", "%.*d", -1, 42));

    CHECK(format_is("%q", "%q"));
    CHECK(format_is("100", "%d", 100));
    CHECK(format_is("a", "a%"));

    CHECK(snprintf(buf, 5, "%s", "abcdefgh") == 8 && same(buf, "abcd"));
    CHECK(snprintf(buf, 1, "%s", "abc") == 3 && buf[0] == 0);
    buf[0] = 'Q';
    CHECK(snprintf(buf, 0, "%s", "abc") == 3 && buf[0] == 'Q');
    CHECK(snprintf(0, 0, "%d", 12345) == 5);
    CHECK(snprintf(buf, sizeof(buf), "%s", "") == 0 && buf[0] == 0);
    CHECK(sprintf(buf, "%d-%d", 12, 34) == 5 && same(buf, "12-34"));
    return 0;
}

/* ------------------------------------------------------------------ */
/* qsort / bsearch                                                     */
/* ------------------------------------------------------------------ */

#define SORT_MAX 4096

typedef struct Small {
    uint8_t key;
    uint8_t check_a;
    uint8_t check_b;
} Small;

typedef struct Big {
    uint32_t key;
    uint8_t payload[44];
} Big;

static unsigned long compare_calls;
static uint32_t random_state = 0x2545F491u;

static uint32_t random_next(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state >> 8;
}

static int compare_int(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    ++compare_calls;
    return (x > y) - (x < y);
}

static int compare_small(const void *a, const void *b) {
    return (int)((const Small *)a)->key - (int)((const Small *)b)->key;
}

static int compare_big(const void *a, const void *b) {
    uint32_t x = ((const Big *)a)->key;
    uint32_t y = ((const Big *)b)->key;
    return (x > y) - (x < y);
}

static void fill_pattern(int *a, size_t n, int pattern) {
    size_t k = n / 2;
    for (size_t i = 0; i < n; ++i) {
        switch (pattern) {
        case 0: a[i] = (int)(random_next() % 1000u); break;
        case 1: a[i] = (int)i; break;
        case 2: a[i] = (int)(n - i); break;
        case 3: a[i] = 7; break;
        case 4: a[i] = (int)(i % 4); break;
        case 5: a[i] = (int)(i < k ? i : n - i); break;
        case 6: a[i] = (int)(i % 17); break;
        default: a[i] = 0; break;
        }
    }
    if (pattern == 7) {
        /* Musser's sequence that drives median-of-three quicksort quadratic. */
        for (size_t i = 1; i <= k; ++i) {
            if (i & 1) {
                a[i - 1] = (int)i;
                a[i] = (int)(k + i);
            }
            a[k + i - 1] = (int)(2 * i);
        }
    }
}

static int check_int_sort(void) {
    static int values[SORT_MAX];
    static const size_t sizes[] = {0, 1, 2, 3, 15, 16, 17, 18, 33, 100, 257, 1000, SORT_MAX};

    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        size_t n = sizes[s];
        for (int pattern = 0; pattern < 8; ++pattern) {
            long sum = 0;
            long squares = 0;
            fill_pattern(values, n, pattern);
            for (size_t i = 0; i < n; ++i) {
                sum += values[i];
                squares += (long)values[i] * values[i];
            }
            compare_calls = 0;
            qsort(values, n, sizeof(values[0]), compare_int);
            for (size_t i = 1; i < n; ++i)
                CHECK(values[i - 1] <= values[i]);
            for (size_t i = 0; i < n; ++i) {
                sum -= values[i];
                squares -= (long)values[i] * values[i];
            }
            CHECK(sum == 0 && squares == 0);
            CHECK(compare_calls < 64 * (n + 1));
        }
    }
    return 0;
}

static int check_record_sort(void) {
    static Small small[300];
    static Big big[200];

    for (size_t i = 0; i < 300; ++i) {
        small[i].key = (uint8_t)random_next();
        small[i].check_a = (uint8_t)(small[i].key * 3u + 1u);
        small[i].check_b = (uint8_t)(small[i].key ^ 0x5au);
    }
    qsort(small, 300, sizeof(Small), compare_small);
    for (size_t i = 0; i < 300; ++i) {
        CHECK(i == 0 || small[i - 1].key <= small[i].key);
        CHECK(small[i].check_a == (uint8_t)(small[i].key * 3u + 1u));
        CHECK(small[i].check_b == (uint8_t)(small[i].key ^ 0x5au));
    }

    for (size_t i = 0; i < 200; ++i) {
        big[i].key = random_next() % 50u;
        for (size_t b = 0; b < sizeof(big[i].payload); ++b)
            big[i].payload[b] = (uint8_t)(big[i].key + b);
    }
    qsort(big, 200, sizeof(Big), compare_big);
    for (size_t i = 0; i < 200; ++i) {
        CHECK(i == 0 || big[i - 1].key <= big[i].key);
        for (size_t b = 0; b < sizeof(big[i].payload); ++b)
            CHECK(big[i].payload[b] == (uint8_t)(big[i].key + b));
    }

    qsort(big, 0, sizeof(Big), compare_big);
    qsort(big, 5, 0, compare_big);
    return 0;
}

static int check_bsearch(void) {
    int values[100];
    for (int i = 0; i < 100; ++i)
        values[i] = i * 3;
    for (int i = 0; i < 100; ++i) {
        int key = i * 3;
        int *hit = bsearch(&key, values, 100, sizeof(int), compare_int);
        CHECK(hit == &values[i]);
    }
    for (int key = -2; key < 300; ++key) {
        int *hit = bsearch(&key, values, 100, sizeof(int), compare_int);
        CHECK((hit != 0) == (key >= 0 && key % 3 == 0 && key <= 297));
    }
    int key = 5;
    CHECK(bsearch(&key, values, 0, sizeof(int), compare_int) == 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */

static size_t environment_size(void) {
    size_t count = 0;
    while (environ && environ[count])
        ++count;
    return count;
}

static int check_environment(void) {
    char name[24];
    char value[24];
    size_t base = environment_size();

    CHECK(getenv("LIBCTEST_UNSET") == 0);
    CHECK(setenv("LIBCTEST_A", "one", 1) == 0);
    CHECK(same(getenv("LIBCTEST_A"), "one"));
    CHECK(setenv("LIBCTEST_A", "two", 0) == 0 && same(getenv("LIBCTEST_A"), "one"));
    CHECK(setenv("LIBCTEST_A", "two", 1) == 0 && same(getenv("LIBCTEST_A"), "two"));
    CHECK(setenv("LIBCTEST_B", "", 1) == 0 && same(getenv("LIBCTEST_B"), ""));
    CHECK(getenv("LIBCTEST") == 0 && getenv("LIBCTEST_AB") == 0);

    errno = 0;
    CHECK(setenv("", "x", 1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(setenv("A=B", "x", 1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(setenv(0, "x", 1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(unsetenv("A=B") == -1 && errno == EINVAL);
    CHECK(getenv("") == 0);

    CHECK(unsetenv("LIBCTEST_A") == 0 && getenv("LIBCTEST_A") == 0);
    CHECK(unsetenv("LIBCTEST_A") == 0);
    CHECK(same(getenv("LIBCTEST_B"), ""));

    for (int i = 0; i < 60; ++i) {
        snprintf(name, sizeof(name), "LIBCTEST_V%d", i);
        snprintf(value, sizeof(value), "value-%d", i * i);
        CHECK(setenv(name, value, 1) == 0);
    }
    for (int i = 0; i < 60; i += 2) {
        snprintf(name, sizeof(name), "LIBCTEST_V%d", i);
        CHECK(unsetenv(name) == 0);
    }
    for (int i = 0; i < 60; ++i) {
        snprintf(name, sizeof(name), "LIBCTEST_V%d", i);
        snprintf(value, sizeof(value), "value-%d", i * i);
        const char *found = getenv(name);
        CHECK(i % 2 == 0 ? found == 0 : (found && same(found, value)));
    }
    CHECK(environment_size() == base + 1 + 30);
    return 0;
}

#define CHILD_ENV_FAIL_BASE 20

/* Runs in the spawned copy: the environment must arrive through crt0. */
static int environment_child(int argc, char **argv) {
    if (argc != 2 || !same(argv[1], "--env"))
        return CHILD_ENV_FAIL_BASE;
    if (!environ)
        return CHILD_ENV_FAIL_BASE + 1;
    if (!getenv("LIBCTEST_FROM_PARENT") || !same(getenv("LIBCTEST_FROM_PARENT"), "inherited"))
        return CHILD_ENV_FAIL_BASE + 2;
    if (!getenv("LIBCTEST_EMPTY") || !same(getenv("LIBCTEST_EMPTY"), ""))
        return CHILD_ENV_FAIL_BASE + 3;
    if (getenv("LIBCTEST_A"))
        return CHILD_ENV_FAIL_BASE + 4;
    if (setenv("LIBCTEST_FROM_PARENT", "changed", 1) != 0 ||
        !same(getenv("LIBCTEST_FROM_PARENT"), "changed"))
        return CHILD_ENV_FAIL_BASE + 5;
    if (unsetenv("LIBCTEST_EMPTY") != 0 || getenv("LIBCTEST_EMPTY"))
        return CHILD_ENV_FAIL_BASE + 6;
    if (setenv("LIBCTEST_NEW", "added", 0) != 0 || !same(getenv("LIBCTEST_NEW"), "added"))
        return CHILD_ENV_FAIL_BASE + 7;
    return 0;
}

static int run_child(const char *mode, const char *const *envp) {
    const char *argv[] = {"libctest", mode, 0};
    int status = -1;
    long pid = spawn_args("/lib/tests/libctest", argv, envp, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                          SPAWN_STDIO_NULL);
    if (pid <= 0)
        return -1000;
    if (wait_pid((int)pid, &status) < 0)
        return -1001;
    return status;
}

static int check_inherited_environment(void) {
    const char *envp[] = {"LIBCTEST_FROM_PARENT=inherited", "LIBCTEST_EMPTY=", 0};
    CHECK(run_child("--env", envp) == 0);
    return 0;
}

/* Runs in the spawned copy: leaves a stream unflushed and returns from main. */
static int exit_flush_child(void) {
    FILE *f = fopen(SCRATCH_FILE, "w");
    if (!f || fputs("flushed at exit\n", f) == EOF)
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* time.h                                                              */
/* ------------------------------------------------------------------ */

#define NSEC_PER_SEC 1000000000L
#define PLAUSIBLE_EPOCH 1700000000

static int64_t nanoseconds(const struct timespec *t) {
    return t->tv_sec * NSEC_PER_SEC + t->tv_nsec;
}

static int check_clock(void) {
    struct timespec mono_a;
    struct timespec mono_b;
    struct timespec real_a;
    struct timespec real_b;
    struct timeval tv;
    time_t seconds = 0;

    CHECK(clock_gettime(CLOCK_MONOTONIC, &mono_a) == 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &real_a) == 0);
    sleep_ms(120);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &mono_b) == 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &real_b) == 0);

    CHECK(mono_a.tv_nsec >= 0 && mono_a.tv_nsec < NSEC_PER_SEC);
    CHECK(mono_b.tv_nsec >= 0 && mono_b.tv_nsec < NSEC_PER_SEC);
    int64_t mono_delta = nanoseconds(&mono_b) - nanoseconds(&mono_a);
    int64_t real_delta = nanoseconds(&real_b) - nanoseconds(&real_a);
    CHECK(mono_delta >= 100000000 && mono_delta < 10 * NSEC_PER_SEC);
    CHECK(real_delta >= 100000000 && real_delta < 10 * NSEC_PER_SEC);
    CHECK(real_a.tv_sec > PLAUSIBLE_EPOCH);
    CHECK(real_a.tv_nsec >= 0 && real_a.tv_nsec < NSEC_PER_SEC);

    time_t now = time(&seconds);
    CHECK(now == seconds && now >= real_b.tv_sec && now - real_b.tv_sec <= 10);
    CHECK(time(0) >= now);

    CHECK(gettimeofday(&tv, 0) == 0);
    CHECK(tv.tv_sec >= now && tv.tv_usec >= 0 && tv.tv_usec < 1000000);

    errno = 0;
    CHECK(clock_gettime(42, &mono_a) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(clock_gettime(CLOCK_MONOTONIC, 0) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(gettimeofday(&tv, &tv) == -1 && errno == EINVAL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* FILE streams                                                        */
/* ------------------------------------------------------------------ */

#define BIG_BYTES 10000

static unsigned char big_data[BIG_BYTES];

static long scratch_size(void) {
    stat_t st;
    return stat(SCRATCH_FILE, &st) == 0 ? (long)st.st_size : -1;
}

static int check_stream_write_read(void) {
    char line[64];
    static unsigned char back[BIG_BYTES];

    FILE *f = fopen(SCRATCH_FILE, "w");
    CHECK(f != 0);
    CHECK(fputs("first line\n", f) >= 0);
    CHECK(fprintf(f, "%s=%05d\n", "number", 42) == 13);
    CHECK(fputc('x', f) == 'x' && putc('y', f) == 'y');
    CHECK(fwrite("\nend\n", 1, 5, f) == 5);
    CHECK(scratch_size() == 0);
    CHECK(fflush(f) == 0);
    CHECK(scratch_size() == 11 + 13 + 7);
    CHECK(ftell(f) == 31);
    for (size_t i = 0; i < BIG_BYTES; ++i)
        big_data[i] = (unsigned char)(i * 7 + (i >> 8));
    CHECK(fwrite(big_data, 1, BIG_BYTES, f) == BIG_BYTES);
    CHECK(fwrite(big_data, 100, 100, f) == 100);
    CHECK(!ferror(f) && fclose(f) == 0);
    CHECK(scratch_size() == 31 + 2 * BIG_BYTES);

    f = fopen(SCRATCH_FILE, "r");
    CHECK(f != 0);
    CHECK(fgets(line, sizeof(line), f) == line && same(line, "first line\n"));
    CHECK(ftell(f) == 11);
    CHECK(fgets(line, sizeof(line), f) == line && same(line, "number=00042\n"));
    CHECK(fgets(line, 4, f) == line && same(line, "xy\n"));
    CHECK(fgets(line, 1, f) == line && same(line, ""));
    CHECK(fgets(line, sizeof(line), f) == line && same(line, "end\n"));
    CHECK(ftell(f) == 31);
    CHECK(fread(back, 1, BIG_BYTES, f) == BIG_BYTES && memcmp(back, big_data, BIG_BYTES) == 0);
    CHECK(fread(back, 100, 100, f) == 100 && memcmp(back, big_data, BIG_BYTES) == 0);
    CHECK(!feof(f));
    CHECK(fgetc(f) == EOF && feof(f) && !ferror(f));
    CHECK(fgets(line, sizeof(line), f) == 0);
    clearerr(f);
    CHECK(!feof(f));
    CHECK(fgetc(f) == EOF && feof(f));
    CHECK(fclose(f) == 0);
    return 0;
}

static int check_stream_seek(void) {
    char buf[16];
    FILE *f = fopen(SCRATCH_FILE, "w+");
    CHECK(f != 0);
    CHECK(fputs("0123456789abcdefghij", f) >= 0);
    CHECK(ftell(f) == 20);
    CHECK(fseek(f, 5, SEEK_SET) == 0 && ftell(f) == 5);
    CHECK(fread(buf, 1, 3, f) == 3 && memcmp(buf, "567", 3) == 0 && ftell(f) == 8);
    CHECK(fseek(f, 2, SEEK_CUR) == 0 && ftell(f) == 10);
    CHECK(fgetc(f) == 'a' && ftell(f) == 11);
    CHECK(fseek(f, -3, SEEK_CUR) == 0 && fgetc(f) == '8');
    CHECK(fseek(f, -4, SEEK_END) == 0 && fgetc(f) == 'g' && ftell(f) == 17);
    CHECK(fseek(f, 0, SEEK_END) == 0 && ftell(f) == 20 && fgetc(f) == EOF && feof(f));
    CHECK(fseek(f, 0, SEEK_SET) == 0 && !feof(f));
    CHECK(fgetc(f) == '0');
    CHECK(fseek(f, -1, SEEK_SET) == -1);

    /* A write after a read continues at the logical position. */
    rewind(f);
    CHECK(fread(buf, 1, 4, f) == 4);
    CHECK(fputs("XY", f) >= 0);
    CHECK(ftell(f) == 6);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fread(buf, 1, 8, f) == 8 && memcmp(buf, "0123XY67", 8) == 0);
    CHECK(fclose(f) == 0);

    f = fopen(SCRATCH_FILE, "r+");
    CHECK(f != 0);
    CHECK(fseek(f, 18, SEEK_SET) == 0 && fputs("!!", f) >= 0 && fflush(f) == 0);
    CHECK(fseek(f, 16, SEEK_SET) == 0 && fread(buf, 1, 8, f) == 4 && memcmp(buf, "gh!!", 4) == 0);
    CHECK(feof(f));
    CHECK(fclose(f) == 0);

    f = fopen(SCRATCH_FILE, "a");
    CHECK(f != 0);
    CHECK(fputs("TAIL", f) >= 0 && fclose(f) == 0);
    CHECK(scratch_size() == 24);
    f = fopen(SCRATCH_FILE, "a+");
    CHECK(f != 0);
    CHECK(fseek(f, 20, SEEK_SET) == 0 && fread(buf, 1, 8, f) == 4 && memcmp(buf, "TAIL", 4) == 0);
    CHECK(fputs("!", f) >= 0 && fclose(f) == 0);
    CHECK(scratch_size() == 25);
    return 0;
}

static int check_stream_edges(void) {
    char buf[32];
    FILE *f;

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && fputs("abcdefgh\n", f) >= 0 && fclose(f) == 0);
    f = fopen(SCRATCH_FILE, "r");
    CHECK(f);
    CHECK(fgets(buf, 4, f) == buf && same(buf, "abc"));
    CHECK(fgets(buf, 4, f) == buf && same(buf, "def"));
    CHECK(fgets(buf, 4, f) == buf && same(buf, "gh\n"));
    CHECK(fgets(buf, 4, f) == 0 && feof(f));
    CHECK(fclose(f) == 0);

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && fputs("no newline", f) >= 0 && fclose(f) == 0);
    f = fopen(SCRATCH_FILE, "r");
    CHECK(f && fgets(buf, sizeof(buf), f) == buf && same(buf, "no newline"));
    CHECK(fgets(buf, sizeof(buf), f) == 0);
    CHECK(fclose(f) == 0);

    f = fopen(SCRATCH_FILE, "r");
    CHECK(f);
    CHECK(fgetc(f) == 'n' && ungetc('n', f) == 'n' && ungetc('z', f) == EOF);
    CHECK(ftell(f) == 0);
    CHECK(fread(buf, 1, 4, f) == 4 && memcmp(buf, "no n", 4) == 0);
    CHECK(ungetc('Q', f) == 'Q' && fgets(buf, sizeof(buf), f) == buf && same(buf, "Qewline"));
    CHECK(fgetc(f) == EOF && ungetc('R', f) == 'R' && fgetc(f) == 'R');
    CHECK(fread(buf, SIZE_MAX, 2, f) == 0);
    CHECK(fread(buf, 0, 4, f) == 0);

    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fwrite("x", 1, 1, f) == 0 && ferror(f));
    clearerr(f);
    CHECK(!ferror(f));
    CHECK(fclose(f) == 0);

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f);
    CHECK(fgetc(f) == EOF && ferror(f));
    CHECK(fclose(f) == 0);

    errno = 0;
    CHECK(fopen("/tmp/libctest.missing", "r") == 0 && errno == ENOENT);
    errno = 0;
    CHECK(fopen(SCRATCH_FILE, "q") == 0 && errno == EINVAL);
    errno = 0;
    CHECK(fopen(SCRATCH_FILE, "rw") == 0 && errno == EINVAL);
    errno = 0;
    CHECK(fdopen(-1, "r") == 0 && errno == EBADF);

    /* Opening and closing must not leak descriptors or memory. */
    for (int i = 0; i < 300; ++i) {
        f = fopen(SCRATCH_FILE, i % 2 ? "rb" : "r+b");
        CHECK(f);
        CHECK(fclose(f) == 0);
    }
    return 0;
}

static int check_stream_buffering(void) {
    char custom[8];
    char buf[32];
    FILE *f;

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && setvbuf(f, 0, _IOLBF, 0) == 0);
    CHECK(fputs("partial", f) >= 0 && scratch_size() == 0);
    CHECK(fputs(" line\nrest", f) >= 0 && scratch_size() == 17);
    CHECK(setvbuf(f, 0, _IONBF, 0) == -1);
    CHECK(fclose(f) == 0 && scratch_size() == 17);

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && setvbuf(f, custom, _IOFBF, sizeof(custom)) == 0);
    CHECK(fputs("12345", f) >= 0 && scratch_size() == 0);
    CHECK(fputs("6789", f) >= 0 && scratch_size() == 8);
    CHECK(fclose(f) == 0 && scratch_size() == 9);

    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && setvbuf(f, 0, _IONBF, 0) == 0);
    CHECK(fputs("now", f) >= 0 && scratch_size() == 3);
    CHECK(fclose(f) == 0);

    /* A child that returns from main without fflush still delivers its data. */
    CHECK(run_child("--exit-flush", 0) == 0);
    f = fopen(SCRATCH_FILE, "r");
    CHECK(f && fgets(buf, sizeof(buf), f) == buf && same(buf, "flushed at exit\n"));
    CHECK(fclose(f) == 0);

    /* Streams on pipes: no seeking, but buffered line IO works. */
    int fds[2];
    CHECK(pipe(fds) == 0);
    FILE *reader = fdopen(fds[0], "r");
    FILE *writer = fdopen(fds[1], "w");
    CHECK(reader && writer);
    CHECK(fputs("pipe one\npipe two\n", writer) >= 0 && fflush(writer) == 0);
    CHECK(fgets(buf, sizeof(buf), reader) == buf && same(buf, "pipe one\n"));
    CHECK(fgets(buf, sizeof(buf), reader) == buf && same(buf, "pipe two\n"));
    errno = 0;
    CHECK(fseek(reader, 0, SEEK_SET) == -1 && errno == ESPIPE);
    CHECK(fclose(writer) == 0);
    CHECK(fgetc(reader) == EOF && feof(reader));
    CHECK(fclose(reader) == 0);
    CHECK(close(fds[0]) < 0);

    /* Output longer than the formatting chunk goes out unabridged. */
    static char long_text[3000];
    memset(long_text, 'L', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = 0;
    f = fopen(SCRATCH_FILE, "w");
    CHECK(f && fprintf(f, "<%s>", long_text) == (int)sizeof(long_text) + 1);
    CHECK(fclose(f) == 0 && scratch_size() == (long)sizeof(long_text) + 1);

    CHECK(fprintf(stderr, "%s", "") == 0);
    CHECK(fputs("", stdout) == 0);
    CHECK(sys_unlink(SCRATCH_FILE) == 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (same(argv[1], "--env"))
            return environment_child(argc, argv);
        if (same(argv[1], "--exit-flush"))
            return exit_flush_child();
        return 2;
    }
    if (check_string() || check_numbers() || check_format() || check_int_sort() ||
        check_record_sort() || check_bsearch() || check_environment() ||
        check_inherited_environment() || check_clock() || check_stream_write_read() ||
        check_stream_seek() || check_stream_edges() || check_stream_buffering())
        return 1;
    printf("LIBC PASS\n");
    return 0;
}
