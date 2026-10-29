#include "mini_libc.h"
#include "syscall.h"

#define NET_ERR_EFAULT     (-14)
#define NET_ERR_EINVAL     (-22)
#define NET_ERR_ETIMEDOUT  (-110)

#define NETTEST_IPV4      NET_IPV4_ADDR(10, 0, 2, 15)
#define NETTEST_NETMASK   NET_IPV4_ADDR(255, 255, 255, 0)
#define NETTEST_GATEWAY   NET_IPV4_ADDR(10, 0, 2, 2)
/* On the slirp subnet but never answered: slirp only ARPs for its own hosts. */
#define NETTEST_SILENT    NET_IPV4_ADDR(10, 0, 2, 77)

#define NETTEST_PING_TIMEOUT_MS   2000
#define NETTEST_PING_ATTEMPTS     3
#define NETTEST_SILENT_TIMEOUT_MS 300

static int fail(const char *what, long value) {
    fprintf(2, "nettest: FAIL %s (%ld)\n", what, value);
    return 1;
}

static int check_info(net_info_t *info) {
    long rc = sys_net_info(info);
    if (rc != 0)
        return fail("net_info", rc);
    if (!(info->flags & NET_INFO_PRESENT) || !(info->flags & NET_INFO_LINK_UP))
        return fail("link state", info->flags);
    if (info->ipv4 != NETTEST_IPV4 || info->netmask != NETTEST_NETMASK ||
        info->gateway != NETTEST_GATEWAY)
        return fail("static config", (long)info->ipv4);

    unsigned int mac_bits = 0;
    for (int i = 0; i < 6; ++i)
        mac_bits |= info->mac[i];
    if (mac_bits == 0 || (info->mac[0] & 0x01) != 0)
        return fail("mac address", info->mac[0]);
    return 0;
}

static int check_argument_errors(void) {
    long rc = sys_net_info((net_info_t *)0);
    if (rc != NET_ERR_EFAULT)
        return fail("net_info null pointer", rc);
    rc = sys_net_ping(NETTEST_GATEWAY, 0);
    if (rc != NET_ERR_EINVAL)
        return fail("ping zero timeout", rc);
    rc = sys_net_ping(NET_IPV4_ADDR(255, 255, 255, 255), NETTEST_PING_TIMEOUT_MS);
    if (rc != NET_ERR_EINVAL)
        return fail("ping broadcast", rc);
    return 0;
}

static int check_gateway_ping(void) {
    long rc = NET_ERR_ETIMEDOUT;
    for (int attempt = 0; attempt < NETTEST_PING_ATTEMPTS && rc < 0; ++attempt)
        rc = sys_net_ping(NETTEST_GATEWAY, NETTEST_PING_TIMEOUT_MS);
    if (rc < 0)
        return fail("ping gateway", rc);
    /* stdout is compared verbatim by init; diagnostics go to stderr. */
    fprintf(2, "nettest: gateway rtt %ld ms\n", rc);
    return 0;
}

int main(void) {
    net_info_t before;
    net_info_t after;

    if (check_info(&before) != 0 || check_argument_errors() != 0 ||
        check_gateway_ping() != 0)
        return 1;

    long rc = sys_net_ping(NETTEST_IPV4, NETTEST_PING_TIMEOUT_MS);
    if (rc != 0)
        return fail("ping self", rc);

    rc = sys_net_ping(NETTEST_SILENT, NETTEST_SILENT_TIMEOUT_MS);
    if (rc != NET_ERR_ETIMEDOUT)
        return fail("ping silent host", rc);

    if (check_info(&after) != 0)
        return 1;
    if (after.tx_frames <= before.tx_frames || after.rx_frames <= before.rx_frames)
        return fail("frame counters", (long)after.rx_frames);

    write(1, "NET PASS\n", 9);
    return 0;
}
