#pragma once

#include <stddef.h>

#include "syscall.h"

/* System facts shown by the Settings app. Gathering is separated from parsing
   and formatting so the latter can be tested with fixed input. */

#define SETTINGS_INFO_TEXT_CAP 32

/* sys_net_ping() result codes the UI distinguishes. */
#define SETTINGS_PING_ENODEV      (-19)
#define SETTINGS_PING_ENETDOWN    (-100)
#define SETTINGS_PING_ENETUNREACH (-101)
#define SETTINGS_PING_ETIMEDOUT   (-110)

typedef struct SettingsInfo {
    int display_valid;
    unsigned int display_width;
    unsigned int display_height;
    unsigned int display_bits_per_pixel;

    int network_valid;
    net_info_t network;

    int memory_valid;
    unsigned long memory_total_kib;
    unsigned long memory_used_kib;

    int storage_valid;
    unsigned long storage_total_kib;
    unsigned long storage_free_kib;

    int uptime_valid;
    unsigned long uptime_seconds;

    int version_valid;
    char os_name[SETTINGS_INFO_TEXT_CAP];
    char os_version[SETTINGS_INFO_TEXT_CAP];
    char os_arch[SETTINGS_INFO_TEXT_CAP];
} SettingsInfo;

/* Queries the kernel (SYS_FB_INFO, SYS_NET_INFO, SYS_SYSINFO, /proc). Each
   group that cannot be read is marked invalid; the others stay usable. */
void settings_info_refresh(SettingsInfo *info);

/* Pings the gateway. Returns the round trip in ms or a negative errno. */
long settings_info_ping_gateway(const SettingsInfo *info, long timeout_ms);

/* Parsers return 0 on success and -1 if the text lacks the data. */
int settings_info_parse_meminfo(const char *text, size_t text_size,
                                unsigned long *total_kib, unsigned long *used_kib);
int settings_info_parse_uptime(const char *text, size_t text_size, unsigned long *seconds);
int settings_info_parse_version(const char *text, size_t text_size, SettingsInfo *info);

/* Formatters always NUL-terminate (truncating if needed) when buffer_size > 0. */
void settings_info_format_ipv4(char *buffer, size_t buffer_size, unsigned int ipv4);
void settings_info_format_mac(char *buffer, size_t buffer_size, const unsigned char mac[6]);
/* "512 KB", "47.3 MB", "1.5 GB". */
void settings_info_format_size(char *buffer, size_t buffer_size, unsigned long kib);
/* "12s", "4m 05s", "2h 03m". */
void settings_info_format_duration(char *buffer, size_t buffer_size, unsigned long seconds);
void settings_info_format_ping(char *buffer, size_t buffer_size, long result);
