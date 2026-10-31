#include "mini_libc.h"
#include "syscall.h"

/*
 * ping - send ICMP echo requests.
 *
 * The target is the first argument ("ping 10.0.2.2"); without one it is read
 * from stdin when that is a pipe or file that already holds data (e.g.
 * "echo 10.0.2.3 | ping"); otherwise the default gateway is used.
 */

#define PING_COUNT       4
#define PING_TIMEOUT_MS  2000
#define PING_INTERVAL_MS 1000

#define PING_ERR_ENODEV      (-19)
#define PING_ERR_EINVAL      (-22)
#define PING_ERR_ENETDOWN    (-100)
#define PING_ERR_ENETUNREACH (-101)
#define PING_ERR_ETIMEDOUT   (-110)

static const char *ping_error_text(long rc) {
    switch (rc) {
    case PING_ERR_ENODEV:      return "no network device";
    case PING_ERR_EINVAL:      return "invalid destination";
    case PING_ERR_ENETDOWN:    return "link is down";
    case PING_ERR_ENETUNREACH: return "network unreachable";
    case PING_ERR_ETIMEDOUT:   return "timed out";
    default:                   return "error";
    }
}

static void format_ipv4(char *out, size_t size, unsigned int ip) {
    snprintf(out, size, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
}

/* Strict dotted quad; trailing whitespace is accepted. */
static int parse_ipv4(const char *s, unsigned int *out) {
    unsigned int ip = 0;

    for (int part = 0; part < 4; ++part) {
        unsigned int value = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 3) {
            value = value * 10 + (unsigned int)(*s - '0');
            ++s;
            ++digits;
        }
        if (digits == 0 || value > 255)
            return -1;
        ip = (ip << 8) | value;
        if (part < 3) {
            if (*s != '.')
                return -1;
            ++s;
        }
    }
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        ++s;
    if (*s != '\0')
        return -1;
    *out = ip;
    return 0;
}

static int read_target(unsigned int *target) {
    char line[32];

    if (!(poll_fd(0, POLL_READ) & POLL_READ))
        return 0;
    long n = read(0, line, sizeof(line) - 1);
    if (n <= 0)
        return 0;
    line[n] = '\0';
    return parse_ipv4(line, target);
}

int main(int argc, char **argv) {
    net_info_t info;
    char addr[16];
    unsigned int target;
    int sent = 0;
    int received = 0;

    long rc = sys_net_info(&info);
    if (rc != 0) {
        dprintf(2, "ping: %s\n", ping_error_text(rc));
        return 1;
    }
    target = info.gateway;
    if (argc > 1 ? parse_ipv4(argv[1], &target) != 0 : read_target(&target) != 0) {
        dprintf(2, "ping: expected an IPv4 address like 10.0.2.2\n");
        return 1;
    }

    format_ipv4(addr, sizeof(addr), target);
    printf("PING %s\n", addr);
    for (int seq = 1; seq <= PING_COUNT; ++seq) {
        ++sent;
        rc = sys_net_ping(target, PING_TIMEOUT_MS);
        if (rc >= 0) {
            printf("reply from %s: seq=%d time=%ld ms\n", addr, seq, rc);
            ++received;
        } else {
            printf("seq=%d: %s\n", seq, ping_error_text(rc));
            if (rc != PING_ERR_ETIMEDOUT)
                break;
        }
        if (seq < PING_COUNT)
            sleep_ms(PING_INTERVAL_MS);
    }
    printf("--- %s: %d sent, %d received ---\n", addr, sent, received);
    return received > 0 ? 0 : 1;
}
