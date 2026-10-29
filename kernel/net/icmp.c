#include "icmp.h"

#include "core/time.h"
#include "errno.h"
#include "ipv4.h"
#include "net_bytes.h"
#include "netif.h"

#include <string.h>

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_ECHO_HDR_LEN      8
#define ICMP_PING_PAYLOAD_LEN  32

#define ICMP_PING_SLOTS      8
#define ICMP_PING_SLOT_BITS  3
#define ICMP_PING_IDENT_BASE 0x4800u
/* A ticket whose owner stopped polling is reclaimed this long after expiry. */
#define ICMP_PING_RECLAIM_MS 1000u

_Static_assert(ICMP_PING_SLOTS == (1 << ICMP_PING_SLOT_BITS),
               "tickets encode the slot in the low ICMP_PING_SLOT_BITS bits");

typedef enum icmp_ping_state {
    ICMP_PING_FREE = 0,
    ICMP_PING_RESOLVING,
    ICMP_PING_AWAITING_REPLY,
    ICMP_PING_DONE,
} icmp_ping_state_t;

typedef struct icmp_ping {
    icmp_ping_state_t state;
    uint16_t generation;
    uint32_t dst;
    uint16_t sequence;
    uint64_t sent_ms;
    uint64_t deadline_ms;
    int64_t result;
} icmp_ping_t;

static icmp_ping_t icmp_pings[ICMP_PING_SLOTS];
static uint16_t icmp_next_sequence = 1;

static uint16_t icmp_slot_ident(int slot) {
    return (uint16_t)(ICMP_PING_IDENT_BASE + (unsigned)slot);
}

static uint8_t icmp_payload_byte(uint16_t sequence, size_t i) {
    return (uint8_t)((sequence + i) * 0x1Du);
}

static void icmp_echo_reply(uint32_t dst, const uint8_t *request, size_t len) {
    uint8_t frame[NET_ETH_FRAME_MAX];
    uint8_t *icmp = frame + IPV4_PAYLOAD_OFFSET;

    if (len > IPV4_PAYLOAD_MAX)
        return;
    memcpy(icmp, request, len);
    icmp[0] = ICMP_TYPE_ECHO_REPLY;
    icmp[1] = 0;
    net_put16(icmp + 2, 0);
    net_put16(icmp + 2, ipv4_checksum(icmp, len));
    /* Unresolved peers simply retry; there is no queue to park the reply in. */
    (void)ipv4_output(frame, len, dst, IPV4_PROTO_ICMP);
}

static void icmp_match_reply(uint32_t src, const uint8_t *packet, size_t len) {
    uint16_t ident = net_get16(packet + 4);
    uint16_t sequence = net_get16(packet + 6);
    int slot = (int)ident - (int)ICMP_PING_IDENT_BASE;

    if (slot < 0 || slot >= ICMP_PING_SLOTS)
        return;
    icmp_ping_t *ping = &icmp_pings[slot];
    if (ping->state != ICMP_PING_AWAITING_REPLY || ping->dst != src ||
        ping->sequence != sequence ||
        len != ICMP_ECHO_HDR_LEN + ICMP_PING_PAYLOAD_LEN)
        return;
    for (size_t i = 0; i < ICMP_PING_PAYLOAD_LEN; ++i) {
        if (packet[ICMP_ECHO_HDR_LEN + i] != icmp_payload_byte(sequence, i))
            return;
    }
    ping->result = (int64_t)(time_ms() - ping->sent_ms);
    ping->state = ICMP_PING_DONE;
}

void icmp_input(uint32_t src, const uint8_t *packet, size_t len) {
    if (len < ICMP_ECHO_HDR_LEN || ipv4_checksum(packet, len) != 0) {
        net_count_rx_drop();
        return;
    }
    switch (packet[0]) {
    case ICMP_TYPE_ECHO_REQUEST:
        if (packet[1] == 0)
            icmp_echo_reply(src, packet, len);
        break;
    case ICMP_TYPE_ECHO_REPLY:
        if (packet[1] == 0)
            icmp_match_reply(src, packet, len);
        break;
    default:
        break;
    }
}

static int icmp_ping_transmit(int slot) {
    icmp_ping_t *ping = &icmp_pings[slot];
    uint8_t frame[IPV4_PAYLOAD_OFFSET + ICMP_ECHO_HDR_LEN + ICMP_PING_PAYLOAD_LEN];
    uint8_t *icmp = frame + IPV4_PAYLOAD_OFFSET;
    size_t len = ICMP_ECHO_HDR_LEN + ICMP_PING_PAYLOAD_LEN;

    icmp[0] = ICMP_TYPE_ECHO_REQUEST;
    icmp[1] = 0;
    net_put16(icmp + 2, 0);
    net_put16(icmp + 4, icmp_slot_ident(slot));
    net_put16(icmp + 6, ping->sequence);
    for (size_t i = 0; i < ICMP_PING_PAYLOAD_LEN; ++i)
        icmp[ICMP_ECHO_HDR_LEN + i] = icmp_payload_byte(ping->sequence, i);
    net_put16(icmp + 2, ipv4_checksum(icmp, len));

    ping->sent_ms = time_ms();
    int rc = ipv4_output(frame, len, ping->dst, IPV4_PROTO_ICMP);
    if (rc == 0) {
        ping->state = ICMP_PING_AWAITING_REPLY;
    } else if (rc == -EAGAIN) {
        ping->state = ICMP_PING_RESOLVING;
    }
    return rc == -EAGAIN ? 0 : rc;
}

static int icmp_ping_ticket(int slot) {
    return (int)(((uint32_t)icmp_pings[slot].generation << ICMP_PING_SLOT_BITS) |
                 (uint32_t)slot);
}

static int icmp_ping_alloc(void) {
    uint64_t now = time_ms();
    for (int slot = 0; slot < ICMP_PING_SLOTS; ++slot) {
        icmp_ping_t *ping = &icmp_pings[slot];
        if (ping->state != ICMP_PING_FREE &&
            now >= ping->deadline_ms + ICMP_PING_RECLAIM_MS)
            ping->state = ICMP_PING_FREE;
        if (ping->state == ICMP_PING_FREE) {
            ping->generation++;
            return slot;
        }
    }
    return -EBUSY;
}

int icmp_ping_begin(uint32_t dst, uint32_t timeout_ms) {
    int slot = icmp_ping_alloc();
    if (slot < 0)
        return slot;

    icmp_ping_t *ping = &icmp_pings[slot];
    ping->dst = dst;
    ping->sequence = icmp_next_sequence++;
    ping->deadline_ms = time_ms() + timeout_ms;
    ping->result = 0;

    if (dst == net_interface()->ipv4) {
        ping->state = ICMP_PING_DONE;
        return icmp_ping_ticket(slot);
    }

    int rc = icmp_ping_transmit(slot);
    if (rc != 0) {
        ping->state = ICMP_PING_FREE;
        return rc;
    }
    return icmp_ping_ticket(slot);
}

int icmp_ping_poll(int ticket, int64_t *out_result) {
    int slot = ticket & (ICMP_PING_SLOTS - 1);
    icmp_ping_t *ping = &icmp_pings[slot];

    if (ticket < 0 || ping->state == ICMP_PING_FREE ||
        icmp_ping_ticket(slot) != ticket) {
        *out_result = -ETIMEDOUT;
        return 1;
    }

    if (ping->state == ICMP_PING_RESOLVING && time_ms() < ping->deadline_ms) {
        int rc = icmp_ping_transmit(slot);
        if (rc != 0) {
            ping->result = rc;
            ping->state = ICMP_PING_DONE;
        }
    }

    if (ping->state != ICMP_PING_DONE) {
        if (time_ms() < ping->deadline_ms)
            return 0;
        ping->result = -ETIMEDOUT;
    }
    *out_result = ping->result;
    ping->state = ICMP_PING_FREE;
    return 1;
}
