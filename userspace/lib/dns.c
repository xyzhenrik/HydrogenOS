#include "dns.h"

#include "mini_libc.h"
#include "syscall.h"

#define DNS_HEADER_LEN     12
#define DNS_LABEL_MAX      63
#define DNS_TYPE_A         1
#define DNS_CLASS_IN       1
#define DNS_FLAG_QR        0x8000u
#define DNS_FLAG_RD        0x0100u
#define DNS_OPCODE_MASK    0x7800u
#define DNS_RCODE_MASK     0x000Fu
#define DNS_RCODE_NXDOMAIN 3
#define DNS_COMPRESSION    0xC0

#define DNS_ERRNO_EAGAIN       (-11)
#define DNS_ERRNO_ENODEV       (-19)
#define DNS_ERRNO_ENETDOWN     (-100)
#define DNS_ERRNO_ENETUNREACH  (-101)
#define DNS_ERRNO_EHOSTUNREACH (-113)

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static int label_char_valid(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Length of name without an optional trailing root dot. */
static size_t name_length(const char *name) {
    size_t len = strlen(name);
    return (len > 0 && name[len - 1] == '.') ? len - 1 : len;
}

/* Encodes name as length-prefixed labels followed by the root label. Returns
 * the encoded length, DNS_ERR_NAME, or -1 when cap is too small. */
static long encode_name(uint8_t *out, size_t cap, const char *name) {
    size_t len = name_length(name);
    size_t pos = 0;
    size_t label_start = 0;

    if (len == 0 || len > DNS_NAME_MAX)
        return DNS_ERR_NAME;
    if (len + 2 > cap)
        return -1;

    for (size_t i = 0; i <= len; ++i) {
        if (i == len || name[i] == '.') {
            size_t label_len = i - label_start;
            if (label_len == 0 || label_len > DNS_LABEL_MAX)
                return DNS_ERR_NAME;
            out[pos++] = (uint8_t)label_len;
            for (size_t j = label_start; j < i; ++j)
                out[pos++] = (uint8_t)name[j];
            label_start = i + 1;
        } else if (!label_char_valid(name[i])) {
            return DNS_ERR_NAME;
        }
    }
    out[pos++] = 0;
    return (long)pos;
}

long dns_build_query(uint8_t *msg, size_t cap, uint16_t id, const char *name) {
    if (cap < DNS_HEADER_LEN)
        return -1;
    long name_len = encode_name(msg + DNS_HEADER_LEN, cap - DNS_HEADER_LEN, name);
    if (name_len < 0)
        return name_len;
    size_t total = DNS_HEADER_LEN + (size_t)name_len + 4;
    if (total > cap)
        return -1;

    memset(msg, 0, DNS_HEADER_LEN);
    put16(msg + 0, id);
    put16(msg + 2, DNS_FLAG_RD);
    put16(msg + 4, 1);
    put16(msg + DNS_HEADER_LEN + name_len, DNS_TYPE_A);
    put16(msg + DNS_HEADER_LEN + name_len + 2, DNS_CLASS_IN);
    return (long)total;
}

/* Advances *pos past the (possibly compressed) name that starts there. */
static int skip_name(const uint8_t *msg, size_t len, size_t *pos) {
    size_t p = *pos;

    while (p < len) {
        uint8_t octet = msg[p];
        if ((octet & DNS_COMPRESSION) == DNS_COMPRESSION) {
            if (p + 2 > len)
                return -1;
            *pos = p + 2;
            return 0;
        }
        if ((octet & DNS_COMPRESSION) != 0)
            return -1;
        if (octet == 0) {
            *pos = p + 1;
            return 0;
        }
        p += 1u + octet;
    }
    return -1;
}

/* Compares the question name at *pos (never compressed) with the host name,
 * case-insensitively, and advances *pos past it. Returns 0, DNS_ERR_MISMATCH
 * when the names differ, or DNS_ERR_MALFORMED. */
static int question_matches(const uint8_t *msg, size_t len, size_t *pos,
                            const char *name) {
    size_t p = *pos;
    size_t n = 0;
    size_t name_len = name_length(name);

    for (;;) {
        if (p >= len)
            return DNS_ERR_MALFORMED;
        uint8_t label = msg[p++];
        if (label == 0)
            break;
        if (label > DNS_LABEL_MAX || p + label > len)
            return DNS_ERR_MALFORMED;
        if (n > 0) {
            if (n >= name_len || name[n] != '.')
                return DNS_ERR_MISMATCH;
            ++n;
        }
        for (uint8_t i = 0; i < label; ++i, ++n) {
            if (n >= name_len || lower(name[n]) != lower((char)msg[p + i]))
                return DNS_ERR_MISMATCH;
        }
        p += label;
    }
    if (n != name_len)
        return DNS_ERR_MISMATCH;
    *pos = p;
    return 0;
}

int dns_parse_response(const uint8_t *msg, size_t len, uint16_t id,
                       const char *name, unsigned int *out_ipv4) {
    size_t pos = DNS_HEADER_LEN;

    if (len < DNS_HEADER_LEN)
        return DNS_ERR_MALFORMED;
    uint16_t flags = get16(msg + 2);
    if (get16(msg + 0) != id || !(flags & DNS_FLAG_QR) ||
        (flags & DNS_OPCODE_MASK) != 0)
        return DNS_ERR_MISMATCH;
    if (get16(msg + 4) != 1)
        return DNS_ERR_MALFORMED;

    int rc = question_matches(msg, len, &pos, name);
    if (rc != 0)
        return rc;
    if (pos + 4 > len)
        return DNS_ERR_MALFORMED;
    if (get16(msg + pos) != DNS_TYPE_A || get16(msg + pos + 2) != DNS_CLASS_IN)
        return DNS_ERR_MISMATCH;
    pos += 4;

    unsigned int rcode = flags & DNS_RCODE_MASK;
    if (rcode == DNS_RCODE_NXDOMAIN)
        return DNS_ERR_NXDOMAIN;
    if (rcode != 0)
        return DNS_ERR_SERVFAIL;

    /* CNAME records precede the A record in the answer section; skip them. */
    for (unsigned int i = get16(msg + 6); i > 0; --i) {
        if (skip_name(msg, len, &pos) != 0 || pos + 10 > len)
            return DNS_ERR_MALFORMED;
        uint16_t type = get16(msg + pos);
        uint16_t class_in = get16(msg + pos + 2);
        size_t data_len = get16(msg + pos + 8);
        pos += 10;
        if (pos + data_len > len)
            return DNS_ERR_MALFORMED;
        if (type == DNS_TYPE_A && class_in == DNS_CLASS_IN && data_len == 4) {
            *out_ipv4 = ((unsigned int)msg[pos] << 24) |
                        ((unsigned int)msg[pos + 1] << 16) |
                        ((unsigned int)msg[pos + 2] << 8) |
                        (unsigned int)msg[pos + 3];
            return 0;
        }
        pos += data_len;
    }
    return DNS_ERR_NO_ADDRESS;
}

/* Waits up to timeout_ms for the response to the query with 'id'. Datagrams
 * from other peers or for other queries are ignored. */
static int await_response(int fd, unsigned int server, uint16_t id,
                          const char *name, unsigned long timeout_ms,
                          unsigned int *out_ipv4) {
    uint8_t msg[DNS_MESSAGE_MAX];
    unsigned long start = time_ms();

    for (;;) {
        unsigned long elapsed = time_ms() - start;
        if (elapsed >= timeout_ms)
            return DNS_ERR_TIMEOUT;

        long ready = poll_wait_fd(fd, POLL_READ, (long)(timeout_ms - elapsed));
        if (ready < 0)
            return (int)ready;
        if (ready == 0)
            return DNS_ERR_TIMEOUT;

        net_sockaddr_t from;
        long n = sys_recvfrom(fd, msg, sizeof(msg), NET_MSG_DONTWAIT, &from);
        if (n == DNS_ERRNO_EAGAIN)
            continue;
        if (n < 0)
            return (int)n;
        if (from.ipv4 != server || from.port != DNS_PORT)
            continue;

        int rc = dns_parse_response(msg, (size_t)n, id, name, out_ipv4);
        if (rc != DNS_ERR_MISMATCH && rc != DNS_ERR_MALFORMED)
            return rc;
    }
}

int dns_resolve_a(unsigned int server, const char *name, unsigned long timeout_ms,
                  unsigned int *out_ipv4) {
    uint8_t query[DNS_MESSAGE_MAX];
    uint16_t id = (uint16_t)(time_ms() * 40503u);
    long query_len = dns_build_query(query, sizeof(query), id, name);

    if (query_len < 0)
        return query_len == DNS_ERR_NAME ? DNS_ERR_NAME : DNS_ERR_MALFORMED;

    long fd = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    if (fd < 0)
        return (int)fd;

    int rc = DNS_ERR_TIMEOUT;
    for (int attempt = 0; attempt < 2 && rc == DNS_ERR_TIMEOUT; ++attempt) {
        long sent = sys_sendto((int)fd, query, (unsigned long)query_len, server,
                               DNS_PORT);
        if (sent < 0) {
            rc = (int)sent;
            break;
        }
        rc = await_response((int)fd, server, id, name, timeout_ms / 2, out_ipv4);
    }
    close((int)fd);
    return rc;
}

const char *dns_error_text(int error) {
    switch (error) {
    case DNS_ERR_TIMEOUT:         return "timed out";
    case DNS_ERR_NXDOMAIN:        return "non-existent domain";
    case DNS_ERR_SERVFAIL:        return "server failure";
    case DNS_ERR_NAME:            return "invalid host name";
    case DNS_ERR_MALFORMED:       return "malformed response";
    case DNS_ERR_NO_ADDRESS:      return "no address record";
    case DNS_ERR_MISMATCH:        return "unexpected response";
    case DNS_ERRNO_ENODEV:        return "no network device";
    case DNS_ERRNO_ENETDOWN:      return "link is down";
    case DNS_ERRNO_ENETUNREACH:   return "network unreachable";
    case DNS_ERRNO_EHOSTUNREACH:  return "host unreachable";
    default:                      return "network error";
    }
}
