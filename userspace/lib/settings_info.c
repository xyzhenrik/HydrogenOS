#include "settings_info.h"
#include "mini_libc.h"

#define PROC_TEXT_CAP 1024
#define SYSINFO_FS_EXT2 1u

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Value of the "label:   123 unit" line, which must start at a line boundary. */
static int find_field_u64(const char *text, size_t text_size, const char *label,
                          unsigned long *value) {
    size_t label_size = strlen(label);
    size_t line = 0;

    while (line < text_size) {
        size_t pos = line;
        size_t matched = 0;

        while (matched < label_size && pos < text_size && text[pos] == label[matched]) {
            ++pos;
            ++matched;
        }
        if (matched == label_size && pos < text_size && text[pos] == ':') {
            unsigned long parsed = 0;
            int digits = 0;

            ++pos;
            while (pos < text_size && text[pos] == ' ')
                ++pos;
            while (pos < text_size && is_digit(text[pos])) {
                parsed = parsed * 10u + (unsigned long)(text[pos] - '0');
                ++pos;
                ++digits;
            }
            if (digits == 0)
                return -1;
            *value = parsed;
            return 0;
        }
        while (line < text_size && text[line] != '\n')
            ++line;
        ++line;
    }
    return -1;
}

int settings_info_parse_meminfo(const char *text, size_t text_size,
                                unsigned long *total_kib, unsigned long *used_kib) {
    unsigned long total;
    unsigned long used;

    if (!text || !total_kib || !used_kib)
        return -1;
    if (find_field_u64(text, text_size, "MemTotal", &total) != 0 ||
        find_field_u64(text, text_size, "MemUsed", &used) != 0)
        return -1;
    *total_kib = total;
    *used_kib = used;
    return 0;
}

int settings_info_parse_uptime(const char *text, size_t text_size, unsigned long *seconds) {
    unsigned long parsed = 0;
    size_t pos = 0;

    if (!text || !seconds)
        return -1;
    while (pos < text_size && is_digit(text[pos])) {
        parsed = parsed * 10u + (unsigned long)(text[pos] - '0');
        ++pos;
    }
    if (pos == 0)
        return -1;
    *seconds = parsed;
    return 0;
}

/* Copies the next whitespace-delimited word; returns the new position or 0. */
static size_t next_word(const char *text, size_t text_size, size_t pos,
                        char *word, size_t word_size) {
    size_t length_copied = 0;

    while (pos < text_size && is_space(text[pos]))
        ++pos;
    if (pos >= text_size)
        return 0;
    while (pos < text_size && !is_space(text[pos])) {
        if (length_copied + 1 < word_size)
            word[length_copied++] = text[pos];
        ++pos;
    }
    word[length_copied] = '\0';
    return pos;
}

int settings_info_parse_version(const char *text, size_t text_size, SettingsInfo *info) {
    char name[SETTINGS_INFO_TEXT_CAP];
    char version[SETTINGS_INFO_TEXT_CAP];
    char arch[SETTINGS_INFO_TEXT_CAP];
    size_t pos;

    if (!text || !info)
        return -1;
    pos = next_word(text, text_size, 0, name, sizeof(name));
    if (pos != 0)
        pos = next_word(text, text_size, pos, version, sizeof(version));
    if (pos != 0)
        pos = next_word(text, text_size, pos, arch, sizeof(arch));
    if (pos == 0)
        return -1;

    memcpy(info->os_name, name, sizeof(name));
    memcpy(info->os_version, version, sizeof(version));
    memcpy(info->os_arch, arch, sizeof(arch));
    return 0;
}

/* Reads a small text file into buffer (NUL-terminated); returns its size or -1. */
static long read_text_file(const char *path, char *buffer, size_t buffer_size) {
    size_t total = 0;
    long fd = open(path, OPEN_READ);

    if (fd < 0)
        return -1;
    while (total + 1 < buffer_size) {
        long got = read((int)fd, buffer + total, buffer_size - 1 - total);
        if (got < 0) {
            close((int)fd);
            return -1;
        }
        if (got == 0)
            break;
        total += (size_t)got;
    }
    close((int)fd);
    buffer[total] = '\0';
    return (long)total;
}

static void refresh_display(SettingsInfo *info) {
    unsigned int fb[4];

    info->display_valid = sys_fb_info(fb) == 0;
    if (!info->display_valid)
        return;
    info->display_width = fb[0];
    info->display_height = fb[1];
    info->display_bits_per_pixel = fb[3];
}

static void refresh_network(SettingsInfo *info) {
    info->network_valid = sys_net_info(&info->network) == 0 &&
                          (info->network.flags & NET_INFO_PRESENT) != 0;
}

static void refresh_memory(SettingsInfo *info, const sysinfo_t *system, int system_valid) {
    char text[PROC_TEXT_CAP];
    long got = read_text_file("/proc/meminfo", text, sizeof(text));

    info->memory_valid = got > 0 &&
        settings_info_parse_meminfo(text, (size_t)got, &info->memory_total_kib,
                                    &info->memory_used_kib) == 0;
    if (info->memory_valid || !system_valid || system->total_pages < system->free_pages)
        return;

    /* Without /proc the page counters of SYS_SYSINFO carry the same numbers. */
    info->memory_total_kib = (unsigned long)system->total_pages * system->page_size / 1024u;
    info->memory_used_kib = (unsigned long)(system->total_pages - system->free_pages) *
                            system->page_size / 1024u;
    info->memory_valid = 1;
}

static void refresh_storage(SettingsInfo *info, const sysinfo_t *system, int system_valid) {
    info->storage_valid = system_valid && system->fs_type == SYSINFO_FS_EXT2 &&
                          system->total_blocks > 0;
    if (!info->storage_valid)
        return;
    info->storage_total_kib = (unsigned long)system->total_blocks * system->block_size / 1024u;
    info->storage_free_kib = (unsigned long)system->free_blocks * system->block_size / 1024u;
}

static void refresh_uptime(SettingsInfo *info) {
    char text[PROC_TEXT_CAP];
    long got = read_text_file("/proc/uptime", text, sizeof(text));

    info->uptime_valid = got > 0 &&
        settings_info_parse_uptime(text, (size_t)got, &info->uptime_seconds) == 0;
    if (!info->uptime_valid) {
        info->uptime_seconds = time_ms() / 1000u;
        info->uptime_valid = 1;
    }
}

static void refresh_version(SettingsInfo *info) {
    char text[PROC_TEXT_CAP];
    long got = read_text_file("/proc/version", text, sizeof(text));

    info->version_valid = got > 0 &&
                          settings_info_parse_version(text, (size_t)got, info) == 0;
}

void settings_info_refresh(SettingsInfo *info) {
    sysinfo_t system;
    int system_valid;

    if (!info)
        return;
    system_valid = sys_sysinfo(&system) == 0;
    refresh_display(info);
    refresh_network(info);
    refresh_memory(info, &system, system_valid);
    refresh_storage(info, &system, system_valid);
    refresh_uptime(info);
    refresh_version(info);
}

long settings_info_ping_gateway(const SettingsInfo *info, long timeout_ms) {
    if (!info || !info->network_valid)
        return SETTINGS_PING_ENODEV;
    if (info->network.gateway == 0)
        return SETTINGS_PING_ENETUNREACH;
    return sys_net_ping(info->network.gateway, timeout_ms);
}

void settings_info_format_ipv4(char *buffer, size_t buffer_size, unsigned int ipv4) {
    snprintf(buffer, buffer_size, "%u.%u.%u.%u", (ipv4 >> 24) & 0xFFu,
             (ipv4 >> 16) & 0xFFu, (ipv4 >> 8) & 0xFFu, ipv4 & 0xFFu);
}

void settings_info_format_mac(char *buffer, size_t buffer_size, const unsigned char mac[6]) {
    snprintf(buffer, buffer_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void settings_info_format_size(char *buffer, size_t buffer_size, unsigned long kib) {
    if (kib < 1024ul) {
        snprintf(buffer, buffer_size, "%lu KB", kib);
    } else if (kib < 1024ul * 1024ul) {
        unsigned long tenths = kib * 10ul / 1024ul;
        snprintf(buffer, buffer_size, "%lu.%lu MB", tenths / 10ul, tenths % 10ul);
    } else {
        unsigned long tenths = kib * 10ul / (1024ul * 1024ul);
        snprintf(buffer, buffer_size, "%lu.%lu GB", tenths / 10ul, tenths % 10ul);
    }
}

void settings_info_format_duration(char *buffer, size_t buffer_size, unsigned long seconds) {
    if (seconds < 60ul)
        snprintf(buffer, buffer_size, "%lus", seconds);
    else if (seconds < 3600ul)
        snprintf(buffer, buffer_size, "%lum %02lus", seconds / 60ul, seconds % 60ul);
    else if (seconds < 86400ul)
        snprintf(buffer, buffer_size, "%luh %02lum", seconds / 3600ul, seconds % 3600ul / 60ul);
    else
        snprintf(buffer, buffer_size, "%lud %02luh", seconds / 86400ul, seconds % 86400ul / 3600ul);
}

void settings_info_format_ping(char *buffer, size_t buffer_size, long result) {
    switch (result) {
    case SETTINGS_PING_ETIMEDOUT:
        snprintf(buffer, buffer_size, "No reply (timed out)");
        break;
    case SETTINGS_PING_ENODEV:
        snprintf(buffer, buffer_size, "No network adapter");
        break;
    case SETTINGS_PING_ENETDOWN:
        snprintf(buffer, buffer_size, "Link is down");
        break;
    case SETTINGS_PING_ENETUNREACH:
        snprintf(buffer, buffer_size, "Network unreachable");
        break;
    default:
        if (result >= 0)
            snprintf(buffer, buffer_size, "Reply in %ld ms", result);
        else
            snprintf(buffer, buffer_size, "Failed (error %ld)", -result);
        break;
    }
}
