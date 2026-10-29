#include "mini_libc.h"
#include "signal.h"
#include "syscall.h"

/*
 * Boot test for the kernel wait queues behind pipes, sockets and SYS_WAIT.
 * The coordinator spawns itself in different roles (argv[1]) and checks that
 *   - blocked readers of one pipe are served in arrival order, one per byte,
 *     and that a single large write is passed on from reader to reader
 *   - pollers watching many pipes all wake on an event on any of them and
 *     leave no registration behind on the pipes they did not wake on
 *   - processes killed while queued (pollers, readers, a parent in wait,
 *     socket receivers) neither stay on a queue nor swallow a wakeup, also
 *     when the kill hits a reader that was already woken but has not run
 *   - blocked socket receivers and pollers wake per datagram
 *   - SYS_WAIT completes when the child exits, whatever the exit order
 * A lost wakeup shows up as a timeout in the coordinator, which never blocks
 * without a deadline, instead of a hang.
 */

enum {
    PIPE_SET_SIZE = 8,
    POLLER_COUNT = 8,
    READER_COUNT = 6,
    STAGGER_MS = 40,
    SETTLE_MS = 60,
    IO_TIMEOUT_MS = 3000,
    NAPPER_MS = 400,
    SOCKET_PORT = 41500,
    REPORT_HANGUP = 0x80,
    REPORT_SOCKET = 'S',
    REPORT_PIPE = 'P',
    READ_EOF = -2,
    READ_TIMEOUT = -1,
    ROLE_ARGS_MAX = PIPE_SET_SIZE + 4,
};

static int fail(const char *what, long value) {
    dprintf(2, "waitqtest: FAIL %s (%ld)\n", what, value);
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

static int nothing_pending(int fd) {
    sleep_ms(STAGGER_MS);
    return poll_fd(fd, POLL_READ) == 0;
}

/*
 * Spawns this program in 'role' with up to ROLE_ARGS_MAX numeric arguments.
 * stdin/stdout are the given fds or one of the SPAWN_STDIO_* values.
 */
static long spawn_role(const char *role, const unsigned *args, unsigned count,
                       int in_fd, int out_fd) {
    char text[ROLE_ARGS_MAX][12];
    const char *argv[ROLE_ARGS_MAX + 2];
    unsigned argc = 0;

    argv[argc++] = "waitqtest";
    argv[argc++] = role;
    for (unsigned i = 0; i < count && i < ROLE_ARGS_MAX; ++i) {
        snprintf(text[i], sizeof(text[i]), "%u", args[i]);
        argv[argc++] = text[i];
    }
    argv[argc] = 0;
    return spawn_args("/waitqtest", argv, 0, in_fd, out_fd, SPAWN_STDIO_INHERIT);
}

static long spawn_one(const char *role, unsigned arg, int in_fd, int out_fd) {
    return spawn_role(role, &arg, 1, in_fd, out_fd);
}

/* Pipes whose read ends the pollers inherit; the write ends stay private. */
typedef struct pipe_set {
    int read_fd[PIPE_SET_SIZE];
    int write_fd[PIPE_SET_SIZE];
} pipe_set_t;

static int pipe_set_open(pipe_set_t *set) {
    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        int fds[2];
        if (pipe(fds) != 0)
            return -1;
        set->read_fd[i] = fds[0];
        set->write_fd[i] = fds[1];
        if (set_fd_inherit(fds[0], 1) != 0)
            return -1;
    }
    return 0;
}

static void pipe_set_close_writers(pipe_set_t *set) {
    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        if (set->write_fd[i] >= 0)
            close(set->write_fd[i]);
        set->write_fd[i] = -1;
    }
}

static void pipe_set_close(pipe_set_t *set) {
    pipe_set_close_writers(set);
    for (int i = 0; i < PIPE_SET_SIZE; ++i)
        close(set->read_fd[i]);
}

static long spawn_poller(unsigned id, const pipe_set_t *set, int done_fd) {
    unsigned args[1 + PIPE_SET_SIZE];
    args[0] = id;
    for (int i = 0; i < PIPE_SET_SIZE; ++i)
        args[1 + i] = (unsigned)set->read_fd[i];
    return spawn_role("poller", args, 1 + PIPE_SET_SIZE, SPAWN_STDIO_NULL,
                      done_fd);
}

/* ------------------------------------------------------------------ */
/* Worker roles                                                        */
/* ------------------------------------------------------------------ */

/* Takes one byte from stdin, then reports its id on stdout. */
static int role_reader(unsigned id) {
    char byte;
    if (read(0, &byte, 1) != 1)
        return 10;
    byte = (char)id;
    return write(1, &byte, 1) == 1 ? 0 : 11;
}

/*
 * Watches every given pipe without consuming anything and reports once:
 * (id << 4 | index) for the first readable pipe, REPORT_HANGUP | id when the
 * pipes were hung up.
 */
static int role_poller(unsigned id, const unsigned *fds) {
    poll_fd_t set[PIPE_SET_SIZE];
    unsigned char report;

    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        set[i].fd = (int)fds[i];
        set[i].events = POLL_READ | POLL_HUP;
        set[i].revents = 0;
    }
    if (poll_many_wait(set, PIPE_SET_SIZE, POLL_WAIT_INFINITE) <= 0)
        return 20;
    int index = -1;
    int hangup = 0;
    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        if (set[i].revents & POLL_HUP)
            hangup = 1;
        if (index < 0 && (set[i].revents & POLL_READ))
            index = i;
    }
    if (index < 0)
        return 21;
    report = hangup ? (unsigned char)(REPORT_HANGUP | id)
                    : (unsigned char)((id << 4) | (unsigned)index);
    return write(1, &report, 1) == 1 ? 0 : 22;
}

static int role_napper(unsigned ms, unsigned code) {
    sleep_ms(ms);
    return (int)code;
}

/* Waits for a child that outlives it, so it is killed while blocked in wait. */
static int role_waiter(void) {
    long child = spawn_role("napper", (unsigned[]){NAPPER_MS, 0}, 2,
                            SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    int status = 0;
    if (child <= 0)
        return 30;
    wait_pid((int)child, &status);
    return 31;
}

/* Receives one datagram on the inherited socket, then reports its id. */
static int role_sockrecv(unsigned id, int sock_fd) {
    char buf[16];
    unsigned char report = (unsigned char)id;
    if (sys_recvfrom(sock_fd, buf, sizeof(buf), 0, 0) < 0)
        return 40;
    return write(1, &report, 1) == 1 ? 0 : 41;
}

/* Reports which of the inherited socket and pipe became readable. */
static int role_sockpoll(int sock_fd, int pipe_fd) {
    poll_fd_t set[2];
    unsigned char report;

    set[0].fd = sock_fd;
    set[0].events = POLL_READ;
    set[0].revents = 0;
    set[1].fd = pipe_fd;
    set[1].events = POLL_READ | POLL_HUP;
    set[1].revents = 0;
    if (poll_many_wait(set, 2, POLL_WAIT_INFINITE) != 1)
        return 50;
    report = (set[0].revents & POLL_READ) ? REPORT_SOCKET : REPORT_PIPE;
    return write(1, &report, 1) == 1 ? 0 : 51;
}

/* ------------------------------------------------------------------ */
/* Coordinator phases                                                  */
/* ------------------------------------------------------------------ */

/* Reads 'expected' reports; returns 0 or READ_TIMEOUT/READ_EOF. */
static int collect_reports(int done_fd, unsigned expected,
                           unsigned char *reports) {
    for (unsigned i = 0; i < expected; ++i) {
        int got = read_byte_timeout(done_fd);
        if (got < 0)
            return got;
        reports[i] = (unsigned char)got;
    }
    return 0;
}

/* Readers queue up in spawn order; each byte serves exactly the oldest. */
static int phase_reader_order(void) {
    int data[2];
    int done[2];
    long pids[READER_COUNT];

    if (pipe(data) != 0 || pipe(done) != 0)
        return fail("readers: pipe", 0);
    for (int i = 0; i < READER_COUNT; ++i) {
        pids[i] = spawn_one("reader", (unsigned)i, data[0], done[1]);
        if (pids[i] <= 0)
            return fail("readers: spawn", pids[i]);
        sleep_ms(STAGGER_MS);
    }
    for (int i = 0; i < READER_COUNT; ++i) {
        if (write(data[1], "x", 1) != 1)
            return fail("readers: write", i);
        int got = read_byte_timeout(done[0]);
        if (got != i)
            return fail("readers: serve order", got);
    }
    for (int i = 0; i < READER_COUNT; ++i) {
        if (wait_status(pids[i]) != 0)
            return fail("readers: status", i);
    }

    /* One write feeds every reader: each one passes the wakeup on. */
    for (int i = 0; i < READER_COUNT; ++i) {
        pids[i] = spawn_one("reader", (unsigned)i, data[0], done[1]);
        if (pids[i] <= 0)
            return fail("readers: respawn", pids[i]);
    }
    sleep_ms(SETTLE_MS);
    if (write(data[1], "123456", READER_COUNT) != READER_COUNT)
        return fail("readers: bulk write", 0);
    unsigned seen = 0;
    unsigned char reports[READER_COUNT];
    int rc = collect_reports(done[0], READER_COUNT, reports);
    if (rc != 0)
        return fail("readers: bulk wake lost", rc);
    for (int i = 0; i < READER_COUNT; ++i)
        seen |= 1u << (reports[i] & 0x0F);
    if (seen != (1u << READER_COUNT) - 1)
        return fail("readers: bulk wake set", seen);
    for (int i = 0; i < READER_COUNT; ++i) {
        if (wait_status(pids[i]) != 0)
            return fail("readers: bulk status", i);
    }

    /* Hangup wakes everybody left in the queue at once. */
    for (int i = 0; i < READER_COUNT; ++i) {
        pids[i] = spawn_one("reader", (unsigned)i, data[0], done[1]);
        if (pids[i] <= 0)
            return fail("readers: eof spawn", pids[i]);
    }
    sleep_ms(SETTLE_MS);
    close(data[1]);
    close(data[0]);
    close(done[1]);
    for (int i = 0; i < READER_COUNT; ++i) {
        if (wait_status(pids[i]) != 10)
            return fail("readers: eof status", i);
    }
    if (read_byte_timeout(done[0]) != READ_EOF)
        return fail("readers: eof report", 0);
    close(done[0]);
    return 0;
}

/* Every poller watches every pipe; any event wakes all of them. */
static int phase_many_pollers(void) {
    static const int rounds[] = {0, PIPE_SET_SIZE - 1, 3};
    pipe_set_t set;
    int done[2];
    long pids[POLLER_COUNT];

    if (pipe_set_open(&set) != 0 || pipe(done) != 0)
        return fail("pollers: pipe", 0);
    for (unsigned round = 0; round < sizeof(rounds) / sizeof(rounds[0]); ++round) {
        int target = rounds[round];
        for (unsigned i = 0; i < POLLER_COUNT; ++i) {
            pids[i] = spawn_poller(i, &set, done[1]);
            if (pids[i] <= 0)
                return fail("pollers: spawn", pids[i]);
        }
        sleep_ms(SETTLE_MS);
        if (write(set.write_fd[target], "x", 1) != 1)
            return fail("pollers: write", target);

        unsigned seen = 0;
        unsigned char reports[POLLER_COUNT];
        int rc = collect_reports(done[0], POLLER_COUNT, reports);
        if (rc != 0)
            return fail("pollers: lost wakeup", rc);
        for (unsigned i = 0; i < POLLER_COUNT; ++i) {
            if ((reports[i] & 0x0F) != target || (reports[i] >> 4) >= POLLER_COUNT)
                return fail("pollers: wrong pipe", reports[i]);
            seen |= 1u << (reports[i] >> 4);
        }
        if (seen != (1u << POLLER_COUNT) - 1)
            return fail("pollers: wake set", seen);
        if (!nothing_pending(done[0]))
            return fail("pollers: extra report", round);
        for (unsigned i = 0; i < POLLER_COUNT; ++i) {
            if (wait_status(pids[i]) != 0)
                return fail("pollers: status", i);
        }
        char byte;
        if (read(set.read_fd[target], &byte, 1) != 1)
            return fail("pollers: drain", target);
    }

    /* The pollers of the rounds above are gone: events on pipes they had
     * watched must not touch anything left behind. */
    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        char byte;
        if (write(set.write_fd[i], "x", 1) != 1 ||
            read(set.read_fd[i], &byte, 1) != 1)
            return fail("pollers: stale pipe", i);
    }

    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        pids[i] = spawn_poller(i, &set, done[1]);
        if (pids[i] <= 0)
            return fail("pollers: eof spawn", pids[i]);
    }
    sleep_ms(SETTLE_MS);
    pipe_set_close_writers(&set);
    unsigned seen = 0;
    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        int got = read_byte_timeout(done[0]);
        if (got < 0 || !(got & REPORT_HANGUP) ||
            (unsigned)(got & ~REPORT_HANGUP) >= POLLER_COUNT)
            return fail("pollers: hangup wake", got);
        seen |= 1u << (got & ~REPORT_HANGUP);
    }
    if (seen != (1u << POLLER_COUNT) - 1)
        return fail("pollers: hangup set", seen);
    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        if (wait_status(pids[i]) != 0)
            return fail("pollers: eof status", i);
    }
    pipe_set_close(&set);
    close(done[0]);
    close(done[1]);
    return 0;
}

static int killed_by_sigkill(long pid) {
    int status = wait_status(pid);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
}

/* Pollers die on several queues at once; the pipes must carry on. */
static int phase_kill_pollers(void) {
    pipe_set_t set;
    int done[2];
    long pids[POLLER_COUNT];

    if (pipe_set_open(&set) != 0 || pipe(done) != 0)
        return fail("killpoll: pipe", 0);
    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        pids[i] = spawn_poller(i, &set, done[1]);
        if (pids[i] <= 0)
            return fail("killpoll: spawn", pids[i]);
    }
    sleep_ms(SETTLE_MS);
    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        if (kill((int)pids[i], SIGKILL) != 0)
            return fail("killpoll: kill", i);
    }
    for (unsigned i = 0; i < POLLER_COUNT; ++i) {
        if (!killed_by_sigkill(pids[i]))
            return fail("killpoll: status", i);
    }

    for (int i = 0; i < PIPE_SET_SIZE; ++i) {
        char byte;
        if (write(set.write_fd[i], "x", 1) != 1 ||
            read(set.read_fd[i], &byte, 1) != 1)
            return fail("killpoll: stale pipe", i);
    }
    long survivor = spawn_poller(5, &set, done[1]);
    if (survivor <= 0)
        return fail("killpoll: respawn", survivor);
    sleep_ms(SETTLE_MS);
    if (write(set.write_fd[2], "x", 1) != 1)
        return fail("killpoll: write", 0);
    int got = read_byte_timeout(done[0]);
    if (got != ((5 << 4) | 2))
        return fail("killpoll: survivor report", got);
    if (wait_status(survivor) != 0)
        return fail("killpoll: survivor status", 0);

    pipe_set_close(&set);
    close(done[0]);
    close(done[1]);
    return 0;
}

/*
 * A reader that dies at the head of the queue must not eat the wakeup.
 *
 * The coordinator runs at raised priority while it writes and kills: the
 * scheduler then keeps running it after the write, so the woken reader is
 * still queued as runnable, byte unread, when the signal arrives. At equal
 * priority the reader would run first and the handoff path would go untested.
 */
static int phase_kill_readers(void) {
    int data[2];
    int done[2];
    long pids[3];
    char byte;

    if (pipe(data) != 0 || pipe(done) != 0)
        return fail("killread: pipe", 0);
    for (int i = 0; i < 3; ++i) {
        pids[i] = spawn_one("reader", (unsigned)i, data[0], done[1]);
        if (pids[i] <= 0)
            return fail("killread: spawn", pids[i]);
        sleep_ms(STAGGER_MS);
    }

    /* Killed while queued: the next reader is served instead. */
    if (kill((int)pids[0], SIGKILL) != 0 || !killed_by_sigkill(pids[0]))
        return fail("killread: kill head", 0);
    if (write(data[1], "x", 1) != 1)
        return fail("killread: write", 0);
    int got = read_byte_timeout(done[0]);
    if (got != 1)
        return fail("killread: served after kill", got);
    if (wait_status(pids[1]) != 0)
        return fail("killread: status", 0);

    /* Woken, then killed before it ran, with nobody left to pass the wakeup
     * to: the byte must stay in the pipe. */
    sys_set_priority(PRIO_HIGH);
    int wrote = write(data[1], "y", 1);
    int killed = kill((int)pids[2], SIGKILL);
    sys_set_priority(PRIO_NORMAL);
    if (wrote != 1 || killed != 0 || !killed_by_sigkill(pids[2]))
        return fail("killread: kill woken reader", killed);
    if (poll_fd(done[0], POLL_READ) != 0)
        return fail("killread: dead reader reported", 0);
    if (read(data[0], &byte, 1) != 1 || byte != 'y')
        return fail("killread: byte lost", 0);

    /* The handoff: the woken head dies unread, the next reader takes over. */
    long reader_a = spawn_one("reader", 7, data[0], done[1]);
    sleep_ms(STAGGER_MS);
    long reader_b = spawn_one("reader", 8, data[0], done[1]);
    sleep_ms(STAGGER_MS);
    if (reader_a <= 0 || reader_b <= 0)
        return fail("killread: respawn", reader_a < 0 ? reader_a : reader_b);
    sys_set_priority(PRIO_HIGH);
    wrote = write(data[1], "z", 1);
    killed = kill((int)reader_a, SIGKILL);
    sys_set_priority(PRIO_NORMAL);
    if (wrote != 1 || killed != 0 || !killed_by_sigkill(reader_a))
        return fail("killread: kill woken head", killed);
    got = read_byte_timeout(done[0]);
    if (got != 8)
        return fail("killread: handoff lost", got);
    if (wait_status(reader_b) != 0)
        return fail("killread: second status", 0);

    close(data[0]);
    close(data[1]);
    close(done[0]);
    close(done[1]);
    return 0;
}

/* SYS_WAIT completes on exit, in any order; a waiter that dies leaves no trace. */
static int phase_wait(void) {
    static const unsigned delays[3] = {60, 120, 180};
    long pids[3];

    for (int i = 0; i < 3; ++i) {
        unsigned args[2] = {delays[i], (unsigned)(3 + i)};
        pids[i] = spawn_role("napper", args, 2, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
        if (pids[i] <= 0)
            return fail("wait: spawn", pids[i]);
    }
    unsigned long start = time_ms();
    if (wait_status(pids[2]) != 5)
        return fail("wait: longest child", 0);
    if (time_ms() - start < delays[2] / 2)
        return fail("wait: returned early", (long)(time_ms() - start));
    if (wait_status(pids[0]) != 3 || wait_status(pids[1]) != 4)
        return fail("wait: earlier exits", 0);

    long waiter = spawn_role("waiter", 0, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    if (waiter <= 0)
        return fail("wait: spawn waiter", waiter);
    sleep_ms(SETTLE_MS);
    if (kill((int)waiter, SIGKILL) != 0 || !killed_by_sigkill(waiter))
        return fail("wait: kill waiter", 0);
    /* Its child exits into the queue the dead waiter was removed from. */
    sleep_ms(NAPPER_MS);

    long after = spawn_role("napper", (unsigned[]){20, 9}, 2, SPAWN_STDIO_NULL,
                            SPAWN_STDIO_NULL);
    if (after <= 0 || wait_status(after) != 9)
        return fail("wait: after dead waiter", after);
    return 0;
}

static int send_datagram(int sock, const char *text) {
    return sys_sendto(sock, text, 1, NET_IPV4_ADDR(127, 0, 0, 1), SOCKET_PORT) == 1
               ? 0
               : -1;
}

static int phase_sockets(void) {
    int done[2];
    int idle[2];
    long rx = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    long tx = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    long pids[3];
    char buf[16];

    if (rx < 0 || tx < 0 || pipe(done) != 0 || pipe(idle) != 0)
        return fail("sockets: setup", rx < 0 ? rx : tx);
    if (sys_bind((int)rx, 0, SOCKET_PORT) != 0 ||
        set_fd_inherit((int)rx, 1) != 0 || set_fd_inherit(idle[0], 1) != 0)
        return fail("sockets: bind", 0);

    for (int i = 0; i < 3; ++i) {
        unsigned args[2] = {(unsigned)i, (unsigned)rx};
        pids[i] = spawn_role("sockrecv", args, 2, SPAWN_STDIO_NULL, done[1]);
        if (pids[i] <= 0)
            return fail("sockets: spawn", pids[i]);
        sleep_ms(STAGGER_MS);
    }
    for (int i = 0; i < 3; ++i) {
        if (send_datagram((int)tx, "d") != 0)
            return fail("sockets: send", i);
        int got = read_byte_timeout(done[0]);
        if (got != i)
            return fail("sockets: receive order", got);
    }
    for (int i = 0; i < 3; ++i) {
        if (wait_status(pids[i]) != 0)
            return fail("sockets: status", i);
    }

    /* A receiver killed while queued does not take the datagram with it. */
    for (int i = 0; i < 2; ++i) {
        unsigned args[2] = {(unsigned)i, (unsigned)rx};
        pids[i] = spawn_role("sockrecv", args, 2, SPAWN_STDIO_NULL, done[1]);
        if (pids[i] <= 0)
            return fail("sockets: respawn", pids[i]);
        sleep_ms(STAGGER_MS);
    }
    if (kill((int)pids[0], SIGKILL) != 0 || !killed_by_sigkill(pids[0]))
        return fail("sockets: kill receiver", 0);
    if (send_datagram((int)tx, "d") != 0)
        return fail("sockets: send after kill", 0);
    if (read_byte_timeout(done[0]) != 1 || wait_status(pids[1]) != 0)
        return fail("sockets: receiver after kill", 0);

    /* Pollers watch the socket without consuming the datagram. */
    unsigned poll_args[2] = {(unsigned)rx, (unsigned)idle[0]};
    long poller = spawn_role("sockpoll", poll_args, 2, SPAWN_STDIO_NULL, done[1]);
    if (poller <= 0)
        return fail("sockets: spawn poller", poller);
    sleep_ms(STAGGER_MS);
    if (send_datagram((int)tx, "d") != 0)
        return fail("sockets: send to poller", 0);
    if (read_byte_timeout(done[0]) != REPORT_SOCKET || wait_status(poller) != 0)
        return fail("sockets: poller socket wake", 0);
    if (sys_recvfrom((int)rx, buf, sizeof(buf), NET_MSG_DONTWAIT, 0) != 1)
        return fail("sockets: datagram consumed", 0);

    poller = spawn_role("sockpoll", poll_args, 2, SPAWN_STDIO_NULL, done[1]);
    if (poller <= 0)
        return fail("sockets: respawn poller", poller);
    sleep_ms(STAGGER_MS);
    if (write(idle[1], "x", 1) != 1)
        return fail("sockets: pipe write", 0);
    if (read_byte_timeout(done[0]) != REPORT_PIPE || wait_status(poller) != 0)
        return fail("sockets: poller pipe wake", 0);
    if (read(idle[0], buf, 1) != 1)
        return fail("sockets: pipe drain", 0);

    /* A poller killed on two queues; both objects keep working. */
    poller = spawn_role("sockpoll", poll_args, 2, SPAWN_STDIO_NULL, done[1]);
    if (poller <= 0)
        return fail("sockets: third poller", poller);
    sleep_ms(STAGGER_MS);
    if (kill((int)poller, SIGKILL) != 0 || !killed_by_sigkill(poller))
        return fail("sockets: kill poller", 0);
    if (send_datagram((int)tx, "d") != 0 ||
        sys_recvfrom((int)rx, buf, sizeof(buf), NET_MSG_DONTWAIT, 0) != 1)
        return fail("sockets: socket after kill", 0);
    if (write(idle[1], "x", 1) != 1 || read(idle[0], buf, 1) != 1)
        return fail("sockets: pipe after kill", 0);

    close((int)rx);
    close((int)tx);
    close(done[0]);
    close(done[1]);
    close(idle[0]);
    close(idle[1]);
    return 0;
}

static int run_coordinator(void) {
    if (phase_reader_order() != 0 || phase_many_pollers() != 0 ||
        phase_kill_pollers() != 0 || phase_kill_readers() != 0 ||
        phase_wait() != 0 || phase_sockets() != 0)
        return 1;
    write(1, "WAITQ PASS\n", 11);
    return 0;
}

int main(int argc, char **argv) {
    unsigned args[ROLE_ARGS_MAX] = {0};

    if (argc < 2)
        return run_coordinator();
    for (int i = 2; i < argc && i - 2 < ROLE_ARGS_MAX; ++i)
        args[i - 2] = parse_unsigned(argv[i]);

    const char *role = argv[1];
    if (str_equal(role, "reader"))
        return role_reader(args[0]);
    if (str_equal(role, "poller"))
        return role_poller(args[0], &args[1]);
    if (str_equal(role, "napper"))
        return role_napper(args[0], args[1]);
    if (str_equal(role, "waiter"))
        return role_waiter();
    if (str_equal(role, "sockrecv"))
        return role_sockrecv(args[0], (int)args[1]);
    if (str_equal(role, "sockpoll"))
        return role_sockpoll((int)args[0], (int)args[1]);
    return fail("unknown role", 0);
}
