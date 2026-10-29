#include "mini_libc.h"

/*
 * Boot stress test for blocking through the scheduler. The coordinator spawns
 * itself in different roles (argv[1]) and checks that every blocked process
 * is woken exactly when its condition holds:
 *   - sleepers released together by a pipe EOF must wake in deadline order
 *   - request/reply ping-pong and a full-duplex bulk transfer over two pipes
 *   - eight producers filling a pipe past its capacity, one byte at a time
 *   - pollers parked on separate pipes, woken by targeted writes and by EOF
 * A lost wakeup shows up as a poll timeout in the coordinator instead of a
 * hang, because the coordinator never blocks without a deadline.
 */

enum {
    SLEEPER_COUNT = 10,
    SLEEPER_GAP_MS = 30,
    PINGPONG_ROUNDS = 200,
    BULK_BYTES = 3000,
    BULK_CHUNK = 97,
    PRODUCER_COUNT = 8,
    PRODUCER_RECORDS = 96,
    POLLER_DIRECT = 4,
    POLLER_GATED = 2,
    POLLER_COUNT = POLLER_DIRECT + POLLER_GATED,
    POLLER_GATED_FLAG = 0x80,
    SPINNER_MS = 500,
    IO_TIMEOUT_MS = 3000,
    READ_EOF = -2,
    READ_TIMEOUT = -1,
};

static int fail(const char *what, long value) {
    dprintf(2, "schedtest: FAIL %s (%ld)\n", what, value);
    return 1;
}

static unsigned parse_unsigned(const char *s) {
    unsigned value = 0;
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (unsigned)(*s++ - '0');
    return value;
}

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        long n = write(fd, p, len);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Returns the byte, READ_EOF once all writers are gone, or READ_TIMEOUT. */
static int read_byte_timeout(int fd) {
    unsigned char byte;
    if (poll_wait_fd(fd, POLL_READ, IO_TIMEOUT_MS) <= 0)
        return READ_TIMEOUT;
    long n = read(fd, &byte, 1);
    if (n == 0)
        return READ_EOF;
    return n == 1 ? byte : READ_TIMEOUT;
}

static int wait_status(long pid) {
    int status = -1;
    if (wait_pid((int)pid, &status) != pid)
        return -1;
    return status;
}

static long spawn_role(const char *role, unsigned first, unsigned second,
                       int in_fd, int out_fd) {
    char first_arg[12];
    char second_arg[12];
    snprintf(first_arg, sizeof(first_arg), "%u", first);
    snprintf(second_arg, sizeof(second_arg), "%u", second);
    const char *argv[] = {"schedtest", role, first_arg, second_arg, 0};
    return spawn_args("/schedtest", argv, 0, in_fd, out_fd,
                      SPAWN_STDIO_INHERIT);
}

static unsigned char bulk_pattern(unsigned index) {
    return (unsigned char)(index * 7 + 3);
}

/* ------------------------------------------------------------------ */
/* Worker roles                                                        */
/* ------------------------------------------------------------------ */

/* Blocks on stdin until EOF, sleeps, then reports its id on stdout. */
static int role_sleeper(unsigned id, unsigned delay_ms) {
    char byte;
    if (read(0, &byte, 1) != 0)
        return 10;
    unsigned long start = time_ms();
    sleep_ms(delay_ms);
    if (time_ms() - start < delay_ms)
        return 11;
    byte = (char)id;
    return write(1, &byte, 1) == 1 ? 0 : 12;
}

/* Replies to every stdin byte with that byte plus one. */
static int role_echo(void) {
    unsigned char buf[64];
    for (;;) {
        long n = read(0, buf, sizeof(buf));
        if (n < 0)
            return 20;
        if (n == 0)
            return 0;
        for (long i = 0; i < n; ++i)
            buf[i] = (unsigned char)(buf[i] + 1);
        if (write_all(1, buf, (size_t)n) != 0)
            return 21;
    }
}

static int role_producer(unsigned id, unsigned records) {
    for (unsigned seq = 0; seq < records; ++seq) {
        unsigned char record = (unsigned char)((id << 5) | (seq & 31));
        if (write(1, &record, 1) != 1)
            return 30;
        if (seq % 7 == 6)
            yield_cpu();
    }
    return 0;
}

/* Waits for a byte on stdin or for EOF on the gate pipe, then reports. */
static int role_poller(unsigned id, int gate_fd) {
    poll_fd_t fds[2];
    fds[0].fd = 0;
    fds[0].events = POLL_READ;
    fds[0].revents = 0;
    fds[1].fd = gate_fd;
    fds[1].events = POLL_READ | POLL_HUP;
    fds[1].revents = 0;
    if (poll_many_wait(fds, 2, POLL_WAIT_INFINITE) != 1)
        return 40;

    unsigned char report;
    if (id < POLLER_DIRECT) {
        char byte;
        if (!(fds[0].revents & POLL_READ) || fds[1].revents != 0 ||
            read(0, &byte, 1) != 1)
            return 41;
        report = (unsigned char)id;
    } else {
        if (fds[0].revents != 0 || !(fds[1].revents & POLL_HUP))
            return 42;
        report = (unsigned char)(POLLER_GATED_FLAG | id);
    }
    return write(1, &report, 1) == 1 ? 0 : 43;
}

/* Burns CPU without syscalls so timer ticks preempt it. */
static int role_spinner(unsigned duration_ms) {
    unsigned long start = time_ms();
    while (time_ms() - start < duration_ms) {
        for (unsigned i = 0; i < 20000; ++i)
            __asm__ volatile("" ::: "memory");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Coordinator phases                                                  */
/* ------------------------------------------------------------------ */

static int phase_sleepers(void) {
    int go[2];
    int done[2];
    long pids[SLEEPER_COUNT];

    if (pipe(go) != 0 || pipe(done) != 0)
        return fail("sleepers: pipe", 0);
    /* Spawned in reverse so deadline order differs from spawn order. */
    for (int i = SLEEPER_COUNT - 1; i >= 0; --i) {
        pids[i] = spawn_role("sleeper", (unsigned)i,
                             (unsigned)(i + 1) * SLEEPER_GAP_MS, go[0], done[1]);
        if (pids[i] <= 0)
            return fail("sleepers: spawn", pids[i]);
    }
    close(go[0]);
    close(done[1]);
    sleep_ms(50);
    close(go[1]);

    for (int i = 0; i < SLEEPER_COUNT; ++i) {
        int got = read_byte_timeout(done[0]);
        if (got != i)
            return fail("sleepers: wake order", got);
    }
    if (read_byte_timeout(done[0]) != READ_EOF)
        return fail("sleepers: eof", 0);
    for (int i = 0; i < SLEEPER_COUNT; ++i) {
        int status = wait_status(pids[i]);
        if (status != 0)
            return fail("sleepers: status", status);
    }
    close(done[0]);
    return 0;
}

static int phase_pingpong(void) {
    int request[2];
    int reply[2];

    if (pipe(request) != 0 || pipe(reply) != 0)
        return fail("pingpong: pipe", 0);
    long pid = spawn_role("echo", 0, 0, request[0], reply[1]);
    if (pid <= 0)
        return fail("pingpong: spawn", pid);
    close(request[0]);
    close(reply[1]);

    for (unsigned round = 0; round < PINGPONG_ROUNDS; ++round) {
        unsigned char sent = (unsigned char)(round * 5 + 1);
        if (write(request[1], &sent, 1) != 1)
            return fail("pingpong: write", round);
        int got = read_byte_timeout(reply[0]);
        if (got != (unsigned char)(sent + 1))
            return fail("pingpong: reply", got);
    }

    /*
     * Bulk transfer far beyond the pipe capacity in both directions: the
     * echo process blocks on a full reply pipe while this side is blocked in
     * poll, so every progress step depends on a pipe wakeup.
     */
    unsigned sent_total = 0;
    unsigned got_total = 0;
    while (got_total < BULK_BYTES) {
        poll_fd_t fds[2];
        fds[0].fd = reply[0];
        fds[0].events = POLL_READ;
        fds[0].revents = 0;
        fds[1].fd = sent_total < BULK_BYTES ? request[1] : -1;
        fds[1].events = POLL_WRITE;
        fds[1].revents = 0;
        if (poll_many_wait(fds, 2, IO_TIMEOUT_MS) <= 0)
            return fail("bulk: poll timeout", got_total);

        if (fds[1].revents & POLL_WRITE) {
            unsigned char chunk[BULK_CHUNK];
            unsigned len = BULK_BYTES - sent_total;
            if (len > BULK_CHUNK)
                len = BULK_CHUNK;
            for (unsigned i = 0; i < len; ++i)
                chunk[i] = bulk_pattern(sent_total + i);
            long n = write(request[1], chunk, len);
            if (n <= 0)
                return fail("bulk: write", n);
            sent_total += (unsigned)n;
        }
        if (fds[0].revents & POLL_READ) {
            unsigned char buf[128];
            long n = read(reply[0], buf, sizeof(buf));
            if (n <= 0)
                return fail("bulk: read", n);
            for (long i = 0; i < n; ++i) {
                if (buf[i] != (unsigned char)(bulk_pattern(got_total + (unsigned)i) + 1))
                    return fail("bulk: payload", (long)(got_total + (unsigned)i));
            }
            got_total += (unsigned)n;
        }
    }

    close(request[1]);
    if (read_byte_timeout(reply[0]) != READ_EOF)
        return fail("pingpong: eof", 0);
    int status = wait_status(pid);
    if (status != 0)
        return fail("pingpong: status", status);
    close(reply[0]);
    return 0;
}

static int phase_fanin(void) {
    int fan[2];
    long pids[PRODUCER_COUNT];
    unsigned next[PRODUCER_COUNT];
    unsigned total = 0;

    if (pipe(fan) != 0)
        return fail("fanin: pipe", 0);
    for (int i = 0; i < PRODUCER_COUNT; ++i) {
        next[i] = 0;
        pids[i] = spawn_role("producer", (unsigned)i, PRODUCER_RECORDS,
                             SPAWN_STDIO_NULL, fan[1]);
        if (pids[i] <= 0)
            return fail("fanin: spawn", pids[i]);
    }
    close(fan[1]);
    /* Let the producers overflow the pipe so they block as writers. */
    sleep_ms(100);

    for (;;) {
        unsigned char buf[64];
        if (poll_wait_fd(fan[0], POLL_READ, IO_TIMEOUT_MS) <= 0)
            return fail("fanin: poll timeout", total);
        long n = read(fan[0], buf, sizeof(buf));
        if (n < 0)
            return fail("fanin: read", n);
        if (n == 0)
            break;
        for (long i = 0; i < n; ++i) {
            unsigned id = buf[i] >> 5;
            unsigned seq = buf[i] & 31;
            if (id >= PRODUCER_COUNT || seq != (next[id] & 31))
                return fail("fanin: sequence", buf[i]);
            next[id]++;
            total++;
        }
    }
    for (int i = 0; i < PRODUCER_COUNT; ++i) {
        if (next[i] != PRODUCER_RECORDS)
            return fail("fanin: record count", next[i]);
        int status = wait_status(pids[i]);
        if (status != 0)
            return fail("fanin: status", status);
    }
    close(fan[0]);
    return 0;
}

static int phase_pollers(void) {
    static const int wake_order[POLLER_DIRECT] = {2, 0, 3, 1};
    int own[POLLER_COUNT][2];
    int gate[2];
    int done[2];
    long pids[POLLER_COUNT];

    if (pipe(gate) != 0 || pipe(done) != 0)
        return fail("pollers: pipe", 0);
    for (int i = 0; i < POLLER_COUNT; ++i) {
        if (pipe(own[i]) != 0)
            return fail("pollers: pipe", i);
    }
    /* The gate read end reaches the pollers as an inherited fd number. */
    if (set_fd_inherit(gate[0], 1) != 0)
        return fail("pollers: inherit", gate[0]);
    for (int i = 0; i < POLLER_COUNT; ++i) {
        pids[i] = spawn_role("poller", (unsigned)i, (unsigned)gate[0],
                             own[i][0], done[1]);
        if (pids[i] <= 0)
            return fail("pollers: spawn", pids[i]);
    }
    close(gate[0]);
    close(done[1]);
    for (int i = 0; i < POLLER_COUNT; ++i)
        close(own[i][0]);
    sleep_ms(50);

    for (int i = 0; i < POLLER_DIRECT; ++i) {
        int target = wake_order[i];
        if (write(own[target][1], "x", 1) != 1)
            return fail("pollers: write", target);
        int got = read_byte_timeout(done[0]);
        if (got != target)
            return fail("pollers: wake order", got);
        sleep_ms(30);
        if (poll_fd(done[0], POLL_READ) != 0)
            return fail("pollers: spurious report", i);
    }

    close(gate[1]);
    unsigned seen = 0;
    for (int i = 0; i < POLLER_GATED; ++i) {
        int got = read_byte_timeout(done[0]);
        unsigned id = (unsigned)got & ~(unsigned)POLLER_GATED_FLAG;
        if (got < 0 || !(got & POLLER_GATED_FLAG) || id < POLLER_DIRECT ||
            id >= POLLER_COUNT || (seen & (1u << id)))
            return fail("pollers: gated wake", got);
        seen |= 1u << id;
    }
    for (int i = 0; i < POLLER_COUNT; ++i) {
        int status = wait_status(pids[i]);
        if (status != 0)
            return fail("pollers: status", status);
    }
    close(done[0]);
    return 0;
}

static int run_coordinator(void) {
    if (phase_sleepers() != 0 || phase_pingpong() != 0)
        return 1;

    /* The spinner keeps one process permanently runnable and preempted by
     * the timer while the remaining phases block and wake processes. */
    long spinner = spawn_role("spinner", SPINNER_MS, 0, SPAWN_STDIO_NULL,
                              SPAWN_STDIO_NULL);
    if (spinner <= 0)
        return fail("spinner: spawn", spinner);
    if (phase_fanin() != 0 || phase_pollers() != 0)
        return 1;
    int status = wait_status(spinner);
    if (status != 0)
        return fail("spinner: status", status);

    write(1, "SCHED PASS\n", 11);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2)
        return run_coordinator();

    unsigned first = argc > 2 ? parse_unsigned(argv[2]) : 0;
    unsigned second = argc > 3 ? parse_unsigned(argv[3]) : 0;
    const char *role = argv[1];

    if (str_equal(role, "sleeper"))
        return role_sleeper(first, second);
    if (str_equal(role, "echo"))
        return role_echo();
    if (str_equal(role, "producer"))
        return role_producer(first, second);
    if (str_equal(role, "poller"))
        return role_poller(first, (int)second);
    if (str_equal(role, "spinner"))
        return role_spinner(first);
    return fail("unknown role", 0);
}
