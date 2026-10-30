#pragma once

#include <stddef.h>
#include <stdint.h>

/* User-visible desktop preferences: the persisted model (/etc/settings.conf)
   and the colour palette derived from it. Everything here is pure except
   settings_load() and settings_save(). */

#define SETTINGS_PATH "/etc/settings.conf"

#define SETTINGS_THEME_DARK  0
#define SETTINGS_THEME_LIGHT 1

/* Values match SYS_SET_LAYOUT. */
#define SETTINGS_LAYOUT_DE 0
#define SETTINGS_LAYOUT_US 1

#define SETTINGS_ACCENT_COUNT 7

/* Upper bound of a serialized Settings, including the terminating NUL. */
#define SETTINGS_TEXT_CAP 128

/* Bits reported by settings_diff(). */
#define SETTINGS_CHANGED_THEME  (1u << 0)
#define SETTINGS_CHANGED_ACCENT (1u << 1)
#define SETTINGS_CHANGED_GLASS  (1u << 2)
#define SETTINGS_CHANGED_LAYOUT (1u << 3)

typedef struct Settings {
    int theme;   /* SETTINGS_THEME_* */
    int accent;  /* index into the accent table */
    int glass;   /* nonzero: blurred, translucent taskbar and launcher */
    int layout;  /* SETTINGS_LAYOUT_* */
} Settings;

/* Colour tokens of the desktop session, all 0x00RRGGBB. The wallpaper, the
   window title bars, the taskbar, the menu and the Settings surfaces follow
   the theme; the terminal and the document surface keep their own look. */
typedef struct SettingsPalette {
    uint32_t wallpaper_top;
    uint32_t wallpaper_bottom;
    uint32_t wallpaper_text;

    /* Taskbar and menu: tint of the glass (or the solid fill without glass),
       its opacity over the wallpaper, and the text on top. */
    uint32_t taskbar;
    uint32_t menu;
    unsigned int glass_alpha;
    uint32_t chrome_text;
    uint32_t chrome_dim;
    uint32_t chrome_icon;   /* ink of the monochrome app icons on chrome */

    uint32_t accent;
    uint32_t on_accent;

    uint32_t title;
    uint32_t title_focus;
    uint32_t title_text;
    uint32_t title_border;  /* hairline between title bar and content */
    uint32_t close;
    uint32_t minimize;
    uint32_t maximize;

    uint32_t terminal_bg;
    uint32_t terminal_text;
    uint32_t terminal_prompt;
    uint32_t terminal_dim;

    uint32_t document_bg;
    uint32_t document_text;
    uint32_t document_selection;

    /* Settings app: window body, sidebar, grouped cards and their text. */
    uint32_t surface;
    uint32_t sidebar;
    uint32_t card;
    uint32_t separator;
    uint32_t ink;
    uint32_t ink_dim;
    uint32_t control_off;
    uint32_t danger;
} SettingsPalette;

void settings_defaults(Settings *settings);

/* True when every field is within its documented range. */
int settings_valid(const Settings *settings);

/* Bitmask of SETTINGS_CHANGED_* for the fields that differ. */
unsigned int settings_diff(const Settings *before, const Settings *after);

/* Applies "key=value" lines on top of *settings. Blank lines and '#' comments
   are skipped; unknown keys and unusable values are ignored and leave the
   field unchanged, so a damaged file can never produce an invalid model.
   Returns the number of assignments applied, or -1 for a NULL argument. */
int settings_parse(Settings *settings, const char *text, size_t text_size);

/* Writes the canonical text form (NUL-terminated). Returns its length, or -1
   if *settings is invalid or the buffer is too small. */
int settings_serialize(const Settings *settings, char *buffer, size_t buffer_size);

/* Both return 0 or a negative errno. settings_load() leaves *settings
   untouched when the file cannot be read. */
long settings_load(const char *path, Settings *settings);
long settings_save(const char *path, const Settings *settings);

const char *settings_accent_name(int accent);
/* Accent colour for the theme; accents are slightly deeper on light surfaces. */
uint32_t settings_accent_color(int accent, int theme);

void settings_palette(const Settings *settings, SettingsPalette *palette);
