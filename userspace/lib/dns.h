#pragma once

#include <stddef.h>
#include <stdint.h>

/* Stub resolver for A records over UDP (RFC 1035). IPv4 addresses have the
 * first octet in the most significant byte. */

#define DNS_PORT 53
/* Resolver of QEMU user-mode networking, 10.0.2.3. */
#define DNS_DEFAULT_SERVER ((10u << 24) | (0u << 16) | (2u << 8) | 3u)

#define DNS_NAME_MAX    253
#define DNS_MESSAGE_MAX 512

/* Failures of the lookup itself. Socket errors are passed through as the
 * kernel's -errno (magnitude below 1000). */
#define DNS_ERR_TIMEOUT    (-1001) /* no valid response in time */
#define DNS_ERR_NXDOMAIN   (-1002) /* the server says the name does not exist */
#define DNS_ERR_SERVFAIL   (-1003) /* the server failed or refused the query */
#define DNS_ERR_NAME       (-1004) /* not a valid host name */
#define DNS_ERR_MALFORMED  (-1005) /* response does not parse */
#define DNS_ERR_NO_ADDRESS (-1006) /* name exists but has no A record */
#define DNS_ERR_MISMATCH   (-1007) /* response belongs to another query */
#define DNS_ERR_TRUNCATED  (-1008) /* answer did not fit UDP; TCP is not supported */

/* Encodes a recursive A/IN query for name. Returns the message length,
 * DNS_ERR_NAME for an invalid name, or -1 when cap is too small. */
long dns_build_query(uint8_t *msg, size_t cap, uint16_t id, const char *name);

/* Validates a response to dns_build_query(id, name) and extracts the first
 * A record. Returns 0 or a DNS_ERR_* value. */
int dns_parse_response(const uint8_t *msg, size_t len, uint16_t id,
                       const char *name, unsigned int *out_ipv4);

/* Resolves name via server. The query is sent twice, each time waiting half of
 * timeout_ms. Returns 0 or a DNS_ERR_* / -errno value. */
int dns_resolve_a(unsigned int server, const char *name, unsigned long timeout_ms,
                  unsigned int *out_ipv4);

const char *dns_error_text(int error);
