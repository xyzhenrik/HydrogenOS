#pragma once

#include "compositor.h"
#include "gfx_effects.h"
#include "settings.h"
#include "settings_info.h"

/* The Settings app: a sidebar of sections next to grouped cards. It paints
   into a compositor window below the caller's title bar and reports what the
   user changed; applying and persisting the model is the caller's job. */

enum {
    SETTINGS_SECTION_APPEARANCE = 0,
    SETTINGS_SECTION_KEYBOARD,
    SETTINGS_SECTION_DISPLAY,
    SETTINGS_SECTION_NETWORK,
    SETTINGS_SECTION_ABOUT,
    SETTINGS_SECTION_COUNT
};

/* Metrics, in pixels. A window of at least SETTINGS_UI_MIN_WIDTH x
   SETTINGS_UI_MIN_HEIGHT (including the caller's title bar) shows every
   section without clipping. */
#define SETTINGS_UI_MIN_WIDTH     560
#define SETTINGS_UI_MIN_HEIGHT    440
#define SETTINGS_UI_SIDEBAR_WIDTH 176
#define SETTINGS_UI_ROW_HEIGHT    40
#define SETTINGS_UI_PING_TIMEOUT_MS 1000

/* Hit identifiers; the section, theme, accent and layout ids are base + index. */
#define SETTINGS_HIT_NONE    (-1)
#define SETTINGS_HIT_SECTION 0
#define SETTINGS_HIT_THEME   8
#define SETTINGS_HIT_ACCENT  16
#define SETTINGS_HIT_GLASS   24
#define SETTINGS_HIT_LAYOUT  32
#define SETTINGS_HIT_PING    40
#define SETTINGS_HIT_MOTION  48

/* Result bits of the event functions. */
#define SETTINGS_UI_REDRAW  (1u << 0)  /* repaint the Settings window */
#define SETTINGS_UI_CHANGED (1u << 1)  /* the model changed: apply and persist it */

#define SETTINGS_UI_MAX_CARDS 3

/* Geometry of the visible section in window coordinates. Rectangles that do
   not belong to the section are empty (w == 0). */
typedef struct SettingsUiLayout {
    int section;
    GfxRect body;
    GfxRect sidebar;
    GfxRect content;
    int title_x;
    int title_y;

    GfxRect section_item[SETTINGS_SECTION_COUNT];

    GfxRect cards[SETTINGS_UI_MAX_CARDS];
    int card_count;

    GfxRect theme_tile[2];
    GfxRect accent_dot[SETTINGS_ACCENT_COUNT];
    GfxRect glass_switch;
    GfxRect motion_switch;
    GfxRect layout_row[2];
    GfxRect ping_button;
    GfxRect logo;
    int footnote_y;
} SettingsUiLayout;

typedef void (*SettingsLogoFn)(Window *window, int x, int y, int size);

typedef struct SettingsUi {
    Settings *settings;                /* shared model, owned by the caller */
    const SettingsPalette *palette;    /* colours, owned by the caller */
    SettingsLogoFn draw_logo;          /* optional, paints the About logo */
    int top;                           /* height of the caller's title bar */

    int section;
    int hover;                         /* SETTINGS_HIT_* under the pointer */
    int save_failed;                   /* the last persist attempt failed */

    SettingsInfo info;
    unsigned long info_refreshed_ms;
    int ping_pending;
    int ping_done;
    long ping_result;
} SettingsUi;

void settings_ui_layout(int width, int height, int top, int section,
                        SettingsUiLayout *layout);
/* Returns a SETTINGS_HIT_* id, or SETTINGS_HIT_NONE. */
int settings_ui_hit_test(const SettingsUiLayout *layout, int x, int y);

void settings_ui_init(SettingsUi *ui, Settings *settings, const SettingsPalette *palette,
                      SettingsLogoFn draw_logo, int top);
void settings_ui_select(SettingsUi *ui, int section);

/* Repaints everything below the title bar. */
void settings_ui_draw(SettingsUi *ui, Window *window);

/* x and y are window coordinates; all return SETTINGS_UI_* bits. */
unsigned int settings_ui_click(SettingsUi *ui, const Window *window, int x, int y);
unsigned int settings_ui_hover(SettingsUi *ui, const Window *window, int x, int y);
/* Runs a pending ping and refreshes live values; call it once per frame. */
unsigned int settings_ui_service(SettingsUi *ui, unsigned long now_ms);
