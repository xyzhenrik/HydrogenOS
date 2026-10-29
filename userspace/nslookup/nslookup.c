#include "dns.h"
#include "mini_libc.h"
#include "syscall.h"

/*
 * nslookup - resolve a host name to its IPv4 address.
 *
 * usage: nslookup <host> [server]
 *
 * The server defaults to the QEMU user-mode resolver (10.0.2.3).
 */

#define NSLOOKUP_TIMEOUT_MS 4000

static void format_ipv4(char *out, size_t size, unsigned int ip) {
    snprintf(out, size, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
}

/* Strict dotted quad. */
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
    if (*s != '\0')
        return -1;
    *out = ip;
    return 0;
}

int main(int argc, char **argv) {
    unsigned int server = DNS_DEFAULT_SERVER;
    unsigned int address = 0;
    char text[16];

    if (argc < 2 || argc > 3) {
        fprintf(2, "usage: nslookup <host> [server]\n");
        return 2;
    }
    if (argc == 3 && parse_ipv4(argv[2], &server) != 0) {
        fprintf(2, "nslookup: invalid server address '%s'\n", argv[2]);
        return 2;
    }

    int rc = dns_resolve_a(server, argv[1], NSLOOKUP_TIMEOUT_MS, &address);
    format_ipv4(text, sizeof(text), server);
    printf("Server:  %s\n", text);
    if (rc != 0) {
        fprintf(2, "nslookup: %s: %s\n", argv[1], dns_error_text(rc));
        return 1;
    }

    printf("Name:    %s\n", argv[1]);
    format_ipv4(text, sizeof(text), address);
    printf("Address: %s\n", text);
    return 0;
}
