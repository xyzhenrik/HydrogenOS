#include "settings.h"
#include "mini_libc.h"
#include "syscall.h"

/* A settings file is a handful of short lines; anything larger is not ours. */
#define SETTINGS_FILE_CAP 1024

typedef struct AccentEntry {
    const char *name;
    uint32_t dark;
    uint32_t light;
} AccentEntry;

static const AccentEntry accents[SETTINGS_ACCENT_COUNT] = {
    { "blue",     0x0A84FFu, 0x007AFFu },
    { "purple",   0xBF5AF2u, 0xAF52DEu },
    { "pink",     0xFF375Fu, 0xFF2D55u },
    { "red",      0xFF453Au, 0xFF3B30u },
    { "orange",   0xFF9F0Au, 0xFF9500u },
    { "green",    0x30D158u, 0x28CD41u },
    { "graphite", 0x98989Du, 0x8E8E93u },
};

static const char *const theme_names[] = { "dark", "light" };
static const char *const glass_names[] = { "off", "on" };
static const char *const layout_names[] = { "de", "us" };

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive comparison of [text, text + text_size) with a name. */
static int token_is(const char *text, size_t text_size, const char *name) {
    size_t i = 0;
    for (; i < text_size; ++i) {
        if (name[i] == '\0' || lower(text[i]) != name[i])
            return 0;
    }
    return name[i] == '\0';
}

/* Index of the name matching the token, or -1. */
static int token_index(const char *text, size_t text_size,
                       const char *const *names, size_t name_count) {
    for (size_t i = 0; i < name_count; ++i) {
        if (token_is(text, text_size, names[i]))
            return (int)i;
    }
    return -1;
}

static int accent_index(const char *text, size_t text_size) {
    for (int i = 0; i < SETTINGS_ACCENT_COUNT; ++i) {
        if (token_is(text, text_size, accents[i].name))
            return i;
    }
    return -1;
}

void settings_defaults(Settings *settings) {
    if (!settings)
        return;
    settings->theme = SETTINGS_THEME_DARK;
    settings->accent = 0;
    settings->glass = 1;
    settings->layout = SETTINGS_LAYOUT_DE;
}

int settings_valid(const Settings *settings) {
    return settings &&
           (settings->theme == SETTINGS_THEME_DARK || settings->theme == SETTINGS_THEME_LIGHT) &&
           settings->accent >= 0 && settings->accent < SETTINGS_ACCENT_COUNT &&
           (settings->glass == 0 || settings->glass == 1) &&
           (settings->layout == SETTINGS_LAYOUT_DE || settings->layout == SETTINGS_LAYOUT_US);
}

unsigned int settings_diff(const Settings *before, const Settings *after) {
    unsigned int changed = 0;
    if (!before || !after)
        return 0;
    if (before->theme != after->theme)
        changed |= SETTINGS_CHANGED_THEME;
    if (before->accent != after->accent)
        changed |= SETTINGS_CHANGED_ACCENT;
    if ((before->glass != 0) != (after->glass != 0))
        changed |= SETTINGS_CHANGED_GLASS;
    if (before->layout != after->layout)
        changed |= SETTINGS_CHANGED_LAYOUT;
    return changed;
}

/* Applies one key/value pair; returns 1 if it changed the model. */
static int apply_assignment(Settings *settings, const char *key, size_t key_size,
                            const char *value, size_t value_size) {
    int index;

    if (token_is(key, key_size, "theme")) {
        index = token_index(value, value_size, theme_names, COUNT_OF(theme_names));
        if (index < 0)
            return 0;
        settings->theme = index;
    } else if (token_is(key, key_size, "accent")) {
        index = accent_index(value, value_size);
        if (index < 0)
            return 0;
        settings->accent = index;
    } else if (token_is(key, key_size, "glass")) {
        index = token_index(value, value_size, glass_names, COUNT_OF(glass_names));
        if (index < 0)
            return 0;
        settings->glass = index;
    } else if (token_is(key, key_size, "layout")) {
        index = token_index(value, value_size, layout_names, COUNT_OF(layout_names));
        if (index < 0)
            return 0;
        settings->layout = index;
    } else {
        return 0;
    }
    return 1;
}

int settings_parse(Settings *settings, const char *text, size_t text_size) {
    size_t pos = 0;
    int applied = 0;

    if (!settings || (!text && text_size > 0))
        return -1;

    while (pos < text_size) {
        size_t line_start = pos;
        size_t line_end;
        size_t equals;

        while (pos < text_size && text[pos] != '\n')
            ++pos;
        line_end = pos;
        if (pos < text_size)
            ++pos;

        while (line_start < line_end && is_blank(text[line_start]))
            ++line_start;
        while (line_end > line_start && is_blank(text[line_end - 1]))
            --line_end;
        if (line_start == line_end || text[line_start] == '#')
            continue;

        for (equals = line_start; equals < line_end && text[equals] != '='; ++equals)
            ;
        if (equals == line_end)
            continue;

        size_t key_end = equals;
        size_t value_start = equals + 1;
        while (key_end > line_start && is_blank(text[key_end - 1]))
            --key_end;
        while (value_start < line_end && is_blank(text[value_start]))
            ++value_start;

        applied += apply_assignment(settings, text + line_start, key_end - line_start,
                                    text + value_start, line_end - value_start);
    }
    return applied;
}

int settings_serialize(const Settings *settings, char *buffer, size_t buffer_size) {
    if (!settings_valid(settings) || !buffer || buffer_size == 0)
        return -1;

    int written = snprintf(buffer, buffer_size,
                           "# HydrogenOS desktop settings\n"
                           "theme=%s\n"
                           "accent=%s\n"
                           "glass=%s\n"
                           "layout=%s\n",
                           theme_names[settings->theme],
                           accents[settings->accent].name,
                           glass_names[settings->glass],
                           layout_names[settings->layout]);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[0] = '\0';
        return -1;
    }
    return written;
}

long settings_load(const char *path, Settings *settings) {
    char text[SETTINGS_FILE_CAP];
    size_t total = 0;
    long fd;

    if (!path || !settings)
        return -22;
    fd = open(path, OPEN_READ);
    if (fd < 0)
        return fd;

    for (;;) {
        long got = read((int)fd, text + total, sizeof(text) - total);
        if (got < 0) {
            close((int)fd);
            return got;
        }
        if (got == 0)
            break;
        total += (size_t)got;
        if (total == sizeof(text))
            break;
    }
    close((int)fd);

    if (settings_parse(settings, text, total) < 0)
        return -22;
    return 0;
}

long settings_save(const char *path, const Settings *settings) {
    char text[SETTINGS_TEXT_CAP];
    int length_written;

    if (!path)
        return -22;
    length_written = settings_serialize(settings, text, sizeof(text));
    if (length_written < 0)
        return -22;
    return sys_writef(path, text, (unsigned long)length_written);
}

const char *settings_accent_name(int accent) {
    if (accent < 0 || accent >= SETTINGS_ACCENT_COUNT)
        return "";
    return accents[accent].name;
}

uint32_t settings_accent_color(int accent, int theme) {
    if (accent < 0 || accent >= SETTINGS_ACCENT_COUNT)
        accent = 0;
    return theme == SETTINGS_THEME_LIGHT ? accents[accent].light : accents[accent].dark;
}

/* Text on an accent-coloured control: white unless the accent is light. */
static uint32_t on_accent_color(uint32_t accent) {
    uint32_t red = (accent >> 16) & 0xFFu;
    uint32_t green = (accent >> 8) & 0xFFu;
    uint32_t blue = accent & 0xFFu;
    uint32_t luma = (299u * red + 587u * green + 114u * blue) / 1000u;
    return luma > 160u ? 0x1C1C1Eu : 0xFFFFFFu;
}

void settings_palette(const Settings *settings, SettingsPalette *palette) {
    Settings effective;
    int light;

    if (!palette)
        return;
    if (settings_valid(settings)) {
        effective = *settings;
    } else {
        settings_defaults(&effective);
    }
    light = effective.theme == SETTINGS_THEME_LIGHT;

    palette->accent = settings_accent_color(effective.accent, effective.theme);
    palette->on_accent = on_accent_color(palette->accent);

    palette->wallpaper_top = light ? 0xE8EEF4u : 0x162030u;
    palette->wallpaper_bottom = light ? 0xC8D4E0u : 0x0C121Cu;

    /* Taskbar and menu are glass tinted with the surface colour of the
       theme, so their icons and text take the ink colour of that theme. */
    palette->taskbar = light ? 0xF4F4F8u : 0x1B222Cu;
    palette->menu = light ? 0xF8F8FBu : 0x202834u;
    palette->glass_alpha = light ? 190u : 150u;
    palette->chrome_text = light ? 0x1C1C1Eu : 0xD0D8E0u;
    palette->chrome_dim = light ? 0x6E6E73u : 0x8090A0u;
    palette->chrome_icon = light ? 0x3A3A3Eu : 0xFFFFFFu;
    palette->logo = light ? 0x1A7F9Fu : 0x56CCF2u;

    palette->title = light ? 0xE2E2E7u : 0x304050u;
    palette->title_focus = light ? 0xF6F6F9u : 0x425870u;
    palette->title_text = light ? 0x1C1C1Eu : 0xD0D8E0u;
    palette->title_border = light ? 0xC9C9CFu : 0x1E2A38u;
    palette->close = light ? 0xFF5F57u : 0xE06060u;
    palette->minimize = light ? 0xFEBC2Eu : 0xE0B050u;
    palette->maximize = light ? 0x28C840u : 0x60C060u;

    palette->terminal_bg = 0x101820u;
    palette->terminal_text = 0xD0D8E0u;
    palette->terminal_prompt = 0x60C060u;
    palette->terminal_dim = 0x8090A0u;

    palette->document_bg = light ? 0xF0F3F6u : 0x1E232Bu;
    palette->document_text = light ? 0x203040u : 0xD6DCE4u;
    palette->document_selection = light ? 0xC9DCF5u : 0x2F4B73u;

    palette->surface = light ? 0xF2F2F7u : 0x1C1C1Eu;
    palette->sidebar = light ? 0xE6E6EBu : 0x262629u;
    palette->card = light ? 0xFFFFFFu : 0x2C2C2Eu;
    palette->separator = light ? 0xD9D9DEu : 0x3D3D40u;
    palette->ink = light ? 0x1C1C1Eu : 0xF2F2F7u;
    palette->ink_dim = light ? 0x6E6E73u : 0x98989Fu;
    palette->control_off = light ? 0xD1D1D6u : 0x48484Au;
    palette->danger = light ? 0xFF3B30u : 0xFF453Au;
}
