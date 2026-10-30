#include "settings_ui.h"
#include "mini_libc.h"

#define GLYPH_WIDTH  8
#define GLYPH_HEIGHT 16

#define SIDEBAR_MARGIN      10
#define SIDEBAR_TOP         16
#define SIDEBAR_NARROW_BELOW 480
#define ITEM_HEIGHT         32
#define ITEM_GAP            2
#define ITEM_RADIUS         6
#define ITEM_ICON           20
#define ITEM_ICON_RADIUS    5
#define ITEM_PAD            8

#define CONTENT_PAD  24
#define TITLE_OFFSET 20
#define CARDS_OFFSET 56
#define CARD_GAP     20
#define CARD_RADIUS  10
#define ROW_PAD      16
#define ROW_TEXT_OFFSET ((SETTINGS_UI_ROW_HEIGHT - GLYPH_HEIGHT) / 2)

#define TILE_WIDTH     112
#define TILE_HEIGHT    72
#define TILE_RADIUS    8
#define TILE_INSET_Y   18
#define TILE_GAP       20
#define THEME_CARD_HEIGHT 130

#define ACCENT_CARD_HEIGHT 80
#define ACCENT_HIT         28
#define ACCENT_PITCH       32
#define ACCENT_DOT         20
#define ACCENT_TOP         38

#define TALL_ROW_HEIGHT 56
#define SWITCH_WIDTH    44
#define SWITCH_HEIGHT   26
#define BUTTON_WIDTH    96
#define BUTTON_HEIGHT   28
#define LOGO_SIZE       64
#define LIVE_REFRESH_MS 1000
#define FOOTNOTE_GAP    10
#define NOTICE_FROM_BOTTOM 26

#define NETWORK_ROWS 6
#define ABOUT_ROWS   6
#define LIST_ROWS    2

#define TEXT_CAP 64

static const char *const section_names[SETTINGS_SECTION_COUNT] = {
    "Appearance", "Keyboard", "Display", "Network", "About",
};

/* Icon tile colours are fixed: they identify the section, not the theme. */
static const uint32_t section_colors[SETTINGS_SECTION_COUNT] = {
    0x5E5CE6u, 0x8E8E93u, 0x0A84FFu, 0x30D158u, 0x636366u,
};

#define COLOR_WHITE        0xFFFFFFu
#define COLOR_LINK_UP      0x30D158u
#define COLOR_LINK_DOWN    0xFF9F0Au
#define HOVER_OPACITY        24u
#define BUTTON_HOVER_OPACITY 40u

typedef struct Painter {
    Window *window;
    GfxCanvas canvas;
    const SettingsPalette *palette;
} Painter;

/* ---------------- geometry ---------------- */

static GfxRect rect_make(int x, int y, int w, int h) {
    GfxRect rect = { x, y, w, h };
    return rect;
}

static int rect_contains(const GfxRect *rect, int x, int y) {
    return rect->w > 0 && rect->h > 0 && x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

static GfxRect row_rect(const GfxRect *card, int index) {
    return rect_make(card->x, card->y + index * SETTINGS_UI_ROW_HEIGHT, card->w,
                     SETTINGS_UI_ROW_HEIGHT);
}

static void add_card(SettingsUiLayout *layout, int *y, int height) {
    int inner = layout->content.w - 2 * CONTENT_PAD;
    layout->cards[layout->card_count++] =
        rect_make(layout->content.x + CONTENT_PAD, *y, inner > 0 ? inner : 0, height);
    *y += height + CARD_GAP;
}

static void layout_appearance(SettingsUiLayout *layout, int y) {
    const GfxRect *theme_card;
    const GfxRect *accent_card;
    const GfxRect *glass_card;

    add_card(layout, &y, THEME_CARD_HEIGHT);
    add_card(layout, &y, ACCENT_CARD_HEIGHT);
    add_card(layout, &y, TALL_ROW_HEIGHT);
    theme_card = &layout->cards[0];
    accent_card = &layout->cards[1];
    glass_card = &layout->cards[2];

    int tiles_x = theme_card->x + (theme_card->w - (2 * TILE_WIDTH + TILE_GAP)) / 2;
    for (int i = 0; i < 2; ++i) {
        layout->theme_tile[i] = rect_make(tiles_x + i * (TILE_WIDTH + TILE_GAP),
                                          theme_card->y + TILE_INSET_Y, TILE_WIDTH, TILE_HEIGHT);
    }
    for (int i = 0; i < SETTINGS_ACCENT_COUNT; ++i) {
        layout->accent_dot[i] = rect_make(accent_card->x + ROW_PAD + i * ACCENT_PITCH,
                                          accent_card->y + ACCENT_TOP, ACCENT_HIT, ACCENT_HIT);
    }
    layout->glass_switch = rect_make(glass_card->x + glass_card->w - ROW_PAD - SWITCH_WIDTH,
                                     glass_card->y + (glass_card->h - SWITCH_HEIGHT) / 2,
                                     SWITCH_WIDTH, SWITCH_HEIGHT);
}

void settings_ui_layout(int width, int height, int top, int section,
                        SettingsUiLayout *layout) {
    int sidebar_width;
    int y;

    if (!layout)
        return;
    memset(layout, 0, sizeof(*layout));
    if (width < 0)
        width = 0;
    if (height < top)
        height = top;
    layout->section = section;
    layout->body = rect_make(0, top, width, height - top);

    sidebar_width = width >= SIDEBAR_NARROW_BELOW ? SETTINGS_UI_SIDEBAR_WIDTH : width / 3;
    layout->sidebar = rect_make(0, top, sidebar_width, layout->body.h);
    layout->content = rect_make(sidebar_width, top, width - sidebar_width, layout->body.h);
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        layout->section_item[i] = rect_make(SIDEBAR_MARGIN,
                                            top + SIDEBAR_TOP + i * (ITEM_HEIGHT + ITEM_GAP),
                                            sidebar_width - 2 * SIDEBAR_MARGIN, ITEM_HEIGHT);
    }

    layout->title_x = layout->content.x + CONTENT_PAD;
    layout->title_y = top + TITLE_OFFSET;
    y = top + CARDS_OFFSET;

    switch (section) {
    case SETTINGS_SECTION_APPEARANCE:
        layout_appearance(layout, y);
        break;
    case SETTINGS_SECTION_KEYBOARD:
        add_card(layout, &y, LIST_ROWS * SETTINGS_UI_ROW_HEIGHT);
        for (int i = 0; i < LIST_ROWS; ++i)
            layout->layout_row[i] = row_rect(&layout->cards[0], i);
        layout->footnote_y = layout->cards[0].y + layout->cards[0].h + FOOTNOTE_GAP;
        break;
    case SETTINGS_SECTION_DISPLAY:
        add_card(layout, &y, LIST_ROWS * SETTINGS_UI_ROW_HEIGHT);
        layout->footnote_y = layout->cards[0].y + layout->cards[0].h + FOOTNOTE_GAP;
        break;
    case SETTINGS_SECTION_NETWORK:
        add_card(layout, &y, NETWORK_ROWS * SETTINGS_UI_ROW_HEIGHT);
        add_card(layout, &y, TALL_ROW_HEIGHT);
        layout->ping_button = rect_make(layout->cards[1].x + layout->cards[1].w - ROW_PAD - BUTTON_WIDTH,
                                        layout->cards[1].y + (TALL_ROW_HEIGHT - BUTTON_HEIGHT) / 2,
                                        BUTTON_WIDTH, BUTTON_HEIGHT);
        break;
    case SETTINGS_SECTION_ABOUT:
        layout->logo = rect_make(layout->title_x, y, LOGO_SIZE, LOGO_SIZE);
        y += LOGO_SIZE + CARD_GAP;
        add_card(layout, &y, ABOUT_ROWS * SETTINGS_UI_ROW_HEIGHT);
        break;
    default:
        break;
    }
}

int settings_ui_hit_test(const SettingsUiLayout *layout, int x, int y) {
    if (!layout)
        return SETTINGS_HIT_NONE;
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        if (rect_contains(&layout->section_item[i], x, y))
            return SETTINGS_HIT_SECTION + i;
    }

    switch (layout->section) {
    case SETTINGS_SECTION_APPEARANCE:
        for (int i = 0; i < 2; ++i) {
            if (rect_contains(&layout->theme_tile[i], x, y))
                return SETTINGS_HIT_THEME + i;
        }
        for (int i = 0; i < SETTINGS_ACCENT_COUNT; ++i) {
            if (rect_contains(&layout->accent_dot[i], x, y))
                return SETTINGS_HIT_ACCENT + i;
        }
        /* The whole row toggles, not just the switch. */
        if (rect_contains(&layout->cards[2], x, y))
            return SETTINGS_HIT_GLASS;
        break;
    case SETTINGS_SECTION_KEYBOARD:
        for (int i = 0; i < LIST_ROWS; ++i) {
            if (rect_contains(&layout->layout_row[i], x, y))
                return SETTINGS_HIT_LAYOUT + i;
        }
        break;
    case SETTINGS_SECTION_NETWORK:
        if (rect_contains(&layout->ping_button, x, y))
            return SETTINGS_HIT_PING;
        break;
    default:
        break;
    }
    return SETTINGS_HIT_NONE;
}

/* ---------------- painting primitives ---------------- */

static void fill_rect(const Painter *painter, const GfxRect *rect, uint32_t color) {
    int x0 = rect->x < 0 ? 0 : rect->x;
    int y0 = rect->y < 0 ? 0 : rect->y;
    int x1 = rect->x + rect->w > painter->canvas.width ? painter->canvas.width : rect->x + rect->w;
    int y1 = rect->y + rect->h > painter->canvas.height ? painter->canvas.height : rect->y + rect->h;

    for (int y = y0; y < y1; ++y) {
        uint32_t *row = painter->canvas.pixels + (long)y * painter->canvas.stride;
        for (int x = x0; x < x1; ++x)
            row[x] = color & 0x00FFFFFFu;
    }
}

static void fill_round(const Painter *painter, const GfxRect *rect, int radius,
                       uint32_t color, unsigned int opacity) {
    gfx_fill_round_rect(&painter->canvas, rect->x, rect->y, rect->w, rect->h, radius,
                        color, opacity);
}

static void stroke_round(const Painter *painter, const GfxRect *rect, int radius,
                         uint32_t color) {
    gfx_stroke_round_rect(&painter->canvas, rect->x, rect->y, rect->w, rect->h, radius,
                          color, 255);
}

static void fill_disc(const Painter *painter, int center_x, int center_y, int radius,
                      uint32_t color, unsigned int opacity) {
    GfxRect bounds = rect_make(center_x - radius, center_y - radius, 2 * radius, 2 * radius);
    fill_round(painter, &bounds, radius, color, opacity);
}

static void put_pixel(const Painter *painter, int x, int y, uint32_t color) {
    gfx_blend_pixel(&painter->canvas, x, y, color, 255);
}

static int text_width(const char *text) {
    return (int)strlen(text) * GLYPH_WIDTH;
}

static void draw_text(const Painter *painter, int x, int y, const char *text, uint32_t color) {
    comp_text_render(painter->window, x, y, text, color);
}

/* The bitmap font has a single weight; a one pixel smear stands in for bold. */
static void draw_text_bold(const Painter *painter, int x, int y, const char *text,
                           uint32_t color) {
    draw_text(painter, x, y, text, color);
    draw_text(painter, x + 1, y, text, color);
}

static void draw_text_right(const Painter *painter, int right, int y, const char *text,
                            uint32_t color) {
    draw_text(painter, right - text_width(text), y, text, color);
}

static void draw_text_centered(const Painter *painter, const GfxRect *rect, const char *text,
                               uint32_t color) {
    draw_text(painter, rect->x + (rect->w - text_width(text)) / 2,
              rect->y + (rect->h - GLYPH_HEIGHT) / 2, text, color);
}

static void draw_check(const Painter *painter, int x, int y, uint32_t color) {
    for (int step = 0; step < 5; ++step) {
        put_pixel(painter, x + step, y + 6 + step, color);
        put_pixel(painter, x + step, y + 7 + step, color);
    }
    for (int step = 0; step < 8; ++step) {
        put_pixel(painter, x + 4 + step, y + 10 - step, color);
        put_pixel(painter, x + 4 + step, y + 11 - step, color);
    }
}

/* ---------------- sidebar ---------------- */

static void draw_appearance_glyph(const Painter *painter, const GfxRect *tile, uint32_t tint) {
    GfxRect outer = rect_make(tile->x + 4, tile->y + 4, 12, 12);
    GfxRect inner = rect_make(tile->x + 6, tile->y + 6, 8, 8);

    fill_round(painter, &outer, 6, COLOR_WHITE, 255);
    fill_round(painter, &inner, 4, tint, 255);
    /* Left half of the inner disc, tested at doubled coordinates. */
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            int dx = 2 * x + 1 - 8;
            int dy = 2 * y + 1 - 8;
            if (dx * dx + dy * dy <= 64)
                put_pixel(painter, inner.x + x, inner.y + y, COLOR_WHITE);
        }
    }
}

static void draw_keyboard_glyph(const Painter *painter, const GfxRect *tile, uint32_t tint) {
    GfxRect body = rect_make(tile->x + 3, tile->y + 6, 14, 9);

    fill_round(painter, &body, 2, COLOR_WHITE, 255);
    for (int row = 0; row < 2; ++row) {
        for (int key = 0; key < 4; ++key) {
            GfxRect cap = rect_make(tile->x + 5 + key * 3, tile->y + 8 + row * 3, 2, 2);
            fill_rect(painter, &cap, tint);
        }
    }
}

static void draw_display_glyph(const Painter *painter, const GfxRect *tile) {
    GfxRect screen = rect_make(tile->x + 3, tile->y + 4, 14, 10);
    GfxRect neck = rect_make(tile->x + 8, tile->y + 14, 4, 2);
    GfxRect foot = rect_make(tile->x + 6, tile->y + 16, 8, 1);

    fill_round(painter, &screen, 2, COLOR_WHITE, 255);
    fill_rect(painter, &neck, COLOR_WHITE);
    fill_rect(painter, &foot, COLOR_WHITE);
}

static void draw_network_glyph(const Painter *painter, const GfxRect *tile) {
    for (int bar = 0; bar < 4; ++bar) {
        int height = 4 + 3 * bar;
        GfxRect column = rect_make(tile->x + 4 + 4 * bar, tile->y + 16 - height, 2, height);
        fill_rect(painter, &column, COLOR_WHITE);
    }
}

static void draw_about_glyph(const Painter *painter, const GfxRect *tile, uint32_t tint) {
    GfxRect disc = rect_make(tile->x + 4, tile->y + 4, 12, 12);
    GfxRect dot = rect_make(tile->x + 9, tile->y + 7, 2, 2);
    GfxRect stem = rect_make(tile->x + 9, tile->y + 10, 2, 5);

    fill_round(painter, &disc, 6, COLOR_WHITE, 255);
    fill_rect(painter, &dot, tint);
    fill_rect(painter, &stem, tint);
}

static void draw_glyph(const Painter *painter, int section, const GfxRect *tile) {
    uint32_t tint = section_colors[section];

    switch (section) {
    case SETTINGS_SECTION_APPEARANCE: draw_appearance_glyph(painter, tile, tint); break;
    case SETTINGS_SECTION_KEYBOARD:   draw_keyboard_glyph(painter, tile, tint); break;
    case SETTINGS_SECTION_DISPLAY:    draw_display_glyph(painter, tile); break;
    case SETTINGS_SECTION_NETWORK:    draw_network_glyph(painter, tile); break;
    default:                          draw_about_glyph(painter, tile, tint); break;
    }
}

static void draw_sidebar(const Painter *painter, const SettingsUi *ui,
                         const SettingsUiLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    GfxRect edge = rect_make(layout->sidebar.x + layout->sidebar.w - 1, layout->sidebar.y,
                             1, layout->sidebar.h);

    fill_rect(painter, &layout->sidebar, palette->sidebar);
    fill_rect(painter, &edge, palette->separator);

    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        const GfxRect *item = &layout->section_item[i];
        int selected = i == ui->section;
        GfxRect icon = rect_make(item->x + ITEM_PAD, item->y + (item->h - ITEM_ICON) / 2,
                                 ITEM_ICON, ITEM_ICON);

        if (selected)
            fill_round(painter, item, ITEM_RADIUS, palette->accent, 255);
        else if (ui->hover == SETTINGS_HIT_SECTION + i)
            fill_round(painter, item, ITEM_RADIUS, palette->ink, HOVER_OPACITY);

        fill_round(painter, &icon, ITEM_ICON_RADIUS, section_colors[i], 255);
        draw_glyph(painter, i, &icon);
        draw_text(painter, icon.x + ITEM_ICON + ITEM_PAD, item->y + (item->h - GLYPH_HEIGHT) / 2,
                  section_names[i], selected ? palette->on_accent : palette->ink);
    }
}

/* ---------------- cards ---------------- */

static void draw_card(const Painter *painter, const GfxRect *card, int rows) {
    fill_round(painter, card, CARD_RADIUS, painter->palette->card, 255);
    stroke_round(painter, card, CARD_RADIUS, painter->palette->separator);
    for (int row = 1; row < rows; ++row) {
        GfxRect line = rect_make(card->x + ROW_PAD, card->y + row * SETTINGS_UI_ROW_HEIGHT,
                                 card->w - ROW_PAD, 1);
        fill_rect(painter, &line, painter->palette->separator);
    }
}

static void draw_row(const Painter *painter, const GfxRect *card, int row,
                     const char *label, const char *value) {
    int y = card->y + row * SETTINGS_UI_ROW_HEIGHT + ROW_TEXT_OFFSET;

    draw_text(painter, card->x + ROW_PAD, y, label, painter->palette->ink);
    if (value)
        draw_text_right(painter, card->x + card->w - ROW_PAD, y, value, painter->palette->ink_dim);
}

static void draw_title(const Painter *painter, const SettingsUiLayout *layout, const char *title) {
    draw_text_bold(painter, layout->title_x, layout->title_y, title, painter->palette->ink);
}

static void draw_footnote(const Painter *painter, const SettingsUiLayout *layout, const char *text) {
    draw_text(painter, layout->title_x + ROW_PAD / 2, layout->footnote_y, text,
              painter->palette->ink_dim);
}

/* ---------------- Appearance ---------------- */

static void draw_theme_tile(const Painter *painter, const Settings *settings,
                            const GfxRect *tile, int theme) {
    const SettingsPalette *palette = painter->palette;
    Settings preview = *settings;
    SettingsPalette look;
    GfxRect window;
    GfxRect bar;
    GfxRect pill;
    GfxRect dock;
    int selected = settings->theme == theme;

    preview.theme = theme;
    settings_palette(&preview, &look);

    fill_round(painter, tile, TILE_RADIUS,
               gfx_mix(look.wallpaper_top, look.wallpaper_bottom, 128) & 0x00FFFFFFu, 255);
    window = rect_make(tile->x + 16, tile->y + 12, tile->w - 32, 40);
    fill_round(painter, &window, 4, look.surface, 255);
    fill_disc(painter, window.x + 7, window.y + 7, 2, look.close, 255);
    fill_disc(painter, window.x + 14, window.y + 7, 2, look.minimize, 255);
    fill_disc(painter, window.x + 21, window.y + 7, 2, look.maximize, 255);
    bar = rect_make(window.x + 6, window.y + 16, 44, 3);
    fill_round(painter, &bar, 1, look.ink_dim, 160);
    bar = rect_make(window.x + 6, window.y + 23, 30, 3);
    fill_round(painter, &bar, 1, look.ink_dim, 160);
    pill = rect_make(window.x + window.w - 24, window.y + window.h - 13, 18, 8);
    fill_round(painter, &pill, 4, look.accent, 255);
    dock = rect_make(tile->x + 18, tile->y + tile->h - 12, tile->w - 36, 6);
    fill_round(painter, &dock, 3, look.chrome_text, 70);

    if (selected) {
        for (int ring = 0; ring < 2; ++ring) {
            GfxRect outline = rect_make(tile->x - 3 + ring, tile->y - 3 + ring,
                                        tile->w + 6 - 2 * ring, tile->h + 6 - 2 * ring);
            stroke_round(painter, &outline, TILE_RADIUS + 3 - ring, palette->accent);
        }
    } else {
        stroke_round(painter, tile, TILE_RADIUS, palette->separator);
    }

    {
        const char *name = theme == SETTINGS_THEME_DARK ? "Dark" : "Light";
        int label_x = tile->x + (tile->w - text_width(name)) / 2;
        int label_y = tile->y + tile->h + 10;

        if (selected)
            draw_text_bold(painter, label_x, label_y, name, palette->ink);
        else
            draw_text(painter, label_x, label_y, name, palette->ink_dim);
    }
}

static void draw_accent_card(const Painter *painter, const Settings *settings,
                             const SettingsUiLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const GfxRect *card = &layout->cards[1];
    char name[TEXT_CAP];

    draw_card(painter, card, 1);
    snprintf(name, sizeof(name), "%s", settings_accent_name(settings->accent));
    if (name[0] >= 'a' && name[0] <= 'z')
        name[0] = (char)(name[0] - 'a' + 'A');
    draw_text(painter, card->x + ROW_PAD, card->y + 12, "Accent colour", palette->ink);
    draw_text_right(painter, card->x + card->w - ROW_PAD, card->y + 12, name, palette->ink_dim);

    for (int i = 0; i < SETTINGS_ACCENT_COUNT; ++i) {
        const GfxRect *hit = &layout->accent_dot[i];
        int center_x = hit->x + hit->w / 2;
        int center_y = hit->y + hit->h / 2;

        fill_disc(painter, center_x, center_y, ACCENT_DOT / 2,
                  settings_accent_color(i, settings->theme), 255);
        if (i == settings->accent) {
            for (int ring = 0; ring < 2; ++ring) {
                gfx_stroke_round_rect(&painter->canvas, hit->x + ring, hit->y + ring,
                                      hit->w - 2 * ring, hit->h - 2 * ring,
                                      hit->w / 2 - ring, palette->ink, 255);
            }
        }
    }
}

static void draw_glass_card(const Painter *painter, const Settings *settings,
                            const SettingsUiLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const GfxRect *card = &layout->cards[2];
    const GfxRect *track = &layout->glass_switch;
    int knob = SWITCH_HEIGHT - 4;
    int knob_x = settings->glass ? track->x + track->w - 2 - knob : track->x + 2;

    draw_card(painter, card, 1);
    draw_text(painter, card->x + ROW_PAD, card->y + 10, "Glass effects", palette->ink);
    draw_text(painter, card->x + ROW_PAD, card->y + 30, "Translucent taskbar and launcher",
              palette->ink_dim);

    fill_round(painter, track, SWITCH_HEIGHT / 2,
               settings->glass ? palette->accent : palette->control_off, 255);
    fill_disc(painter, knob_x + knob / 2, track->y + 2 + knob / 2 + 1, knob / 2, 0x000000u, 50);
    fill_disc(painter, knob_x + knob / 2, track->y + 2 + knob / 2, knob / 2, COLOR_WHITE, 255);
}

static void draw_appearance(const Painter *painter, const SettingsUi *ui,
                            const SettingsUiLayout *layout) {
    draw_title(painter, layout, section_names[SETTINGS_SECTION_APPEARANCE]);
    draw_card(painter, &layout->cards[0], 1);
    draw_theme_tile(painter, ui->settings, &layout->theme_tile[0], SETTINGS_THEME_DARK);
    draw_theme_tile(painter, ui->settings, &layout->theme_tile[1], SETTINGS_THEME_LIGHT);
    draw_accent_card(painter, ui->settings, layout);
    draw_glass_card(painter, ui->settings, layout);
}

/* ---------------- Keyboard ---------------- */

static void draw_keyboard(const Painter *painter, const SettingsUi *ui,
                          const SettingsUiLayout *layout) {
    static const char *const names[LIST_ROWS] = { "German (QWERTZ)", "English US (QWERTY)" };
    const GfxRect *card = &layout->cards[0];

    draw_title(painter, layout, section_names[SETTINGS_SECTION_KEYBOARD]);
    draw_card(painter, card, LIST_ROWS);
    for (int i = 0; i < LIST_ROWS; ++i) {
        draw_row(painter, card, i, names[i], 0);
        if (ui->settings->layout == i)
            draw_check(painter, card->x + card->w - ROW_PAD - 12,
                       layout->layout_row[i].y + (SETTINGS_UI_ROW_HEIGHT - 12) / 2,
                       painter->palette->accent);
    }
    draw_footnote(painter, layout, "Applies to the whole desktop.");
}

/* ---------------- Display ---------------- */

static void draw_display(const Painter *painter, const SettingsUi *ui,
                         const SettingsUiLayout *layout) {
    const SettingsInfo *info = &ui->info;
    const GfxRect *card = &layout->cards[0];
    char resolution[TEXT_CAP] = "Unavailable";
    char depth[TEXT_CAP] = "Unavailable";

    if (info->display_valid) {
        snprintf(resolution, sizeof(resolution), "%u x %u", info->display_width,
                 info->display_height);
        snprintf(depth, sizeof(depth), "%u-bit", info->display_bits_per_pixel);
    }
    draw_title(painter, layout, section_names[SETTINGS_SECTION_DISPLAY]);
    draw_card(painter, card, LIST_ROWS);
    draw_row(painter, card, 0, "Resolution", resolution);
    draw_row(painter, card, 1, "Colour depth", depth);
    draw_footnote(painter, layout, "Chosen at boot by the display driver.");
}

/* ---------------- Network ---------------- */

static void draw_network_status(const Painter *painter, const SettingsInfo *info,
                                const GfxRect *card) {
    const char *text = "No adapter";
    uint32_t color = painter->palette->danger;
    int text_x;
    int y = card->y + ROW_TEXT_OFFSET;

    if (info->network_valid) {
        int up = (info->network.flags & NET_INFO_LINK_UP) != 0;
        text = up ? "Connected" : "Link down";
        color = up ? COLOR_LINK_UP : COLOR_LINK_DOWN;
    }
    text_x = card->x + card->w - ROW_PAD - text_width(text);
    draw_text(painter, text_x, y, text, painter->palette->ink_dim);
    fill_disc(painter, text_x - 12, y + GLYPH_HEIGHT / 2, 4, color, 255);
}

static void draw_ping_card(const Painter *painter, const SettingsUi *ui,
                           const SettingsUiLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const SettingsInfo *info = &ui->info;
    const GfxRect *card = &layout->cards[1];
    const GfxRect *button = &layout->ping_button;
    char status[TEXT_CAP];
    char gateway[TEXT_CAP];
    int enabled = info->network_valid && !ui->ping_pending;
    int hovered = ui->hover == SETTINGS_HIT_PING && enabled;

    settings_info_format_ipv4(gateway, sizeof(gateway), info->network.gateway);
    if (ui->ping_pending)
        snprintf(status, sizeof(status), "Pinging %s...", gateway);
    else if (ui->ping_done)
        settings_info_format_ping(status, sizeof(status), ui->ping_result);
    else if (info->network_valid)
        snprintf(status, sizeof(status), "Gateway %s", gateway);
    else
        snprintf(status, sizeof(status), "No network adapter");

    draw_card(painter, card, 1);
    draw_text(painter, card->x + ROW_PAD, card->y + 10, "Ping gateway", palette->ink);
    draw_text(painter, card->x + ROW_PAD, card->y + 30, status, palette->ink_dim);

    fill_round(painter, button, BUTTON_HEIGHT / 2,
               enabled ? palette->accent : palette->control_off, 255);
    if (hovered)
        fill_round(painter, button, BUTTON_HEIGHT / 2, COLOR_WHITE, BUTTON_HOVER_OPACITY);
    draw_text_centered(painter, button, ui->ping_pending ? "Pinging" : "Ping",
                       enabled ? palette->on_accent : palette->ink_dim);
}

static void draw_network(const Painter *painter, const SettingsUi *ui,
                         const SettingsUiLayout *layout) {
    const SettingsInfo *info = &ui->info;
    const GfxRect *card = &layout->cards[0];
    char mac[TEXT_CAP] = "-";
    char address[TEXT_CAP] = "-";
    char netmask[TEXT_CAP] = "-";
    char gateway[TEXT_CAP] = "-";
    char traffic[TEXT_CAP] = "-";

    if (info->network_valid) {
        settings_info_format_mac(mac, sizeof(mac), info->network.mac);
        settings_info_format_ipv4(address, sizeof(address), info->network.ipv4);
        settings_info_format_ipv4(netmask, sizeof(netmask), info->network.netmask);
        settings_info_format_ipv4(gateway, sizeof(gateway), info->network.gateway);
        snprintf(traffic, sizeof(traffic), "rx %u  tx %u  drop %u", info->network.rx_frames,
                 info->network.tx_frames, info->network.rx_dropped);
    }
    draw_title(painter, layout, section_names[SETTINGS_SECTION_NETWORK]);
    draw_card(painter, card, NETWORK_ROWS);
    draw_row(painter, card, 0, "Status", 0);
    draw_network_status(painter, info, card);
    draw_row(painter, card, 1, "MAC address", mac);
    draw_row(painter, card, 2, "IP address", address);
    draw_row(painter, card, 3, "Subnet mask", netmask);
    draw_row(painter, card, 4, "Gateway", gateway);
    draw_row(painter, card, 5, "Traffic", traffic);
    draw_ping_card(painter, ui, layout);
}

/* ---------------- About ---------------- */

static void draw_about(const Painter *painter, const SettingsUi *ui,
                       const SettingsUiLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const SettingsInfo *info = &ui->info;
    const GfxRect *card = &layout->cards[0];
    char version[TEXT_CAP] = "Version unknown";
    char uptime[TEXT_CAP] = "Unavailable";
    char memory[TEXT_CAP] = "Unavailable";
    char storage[TEXT_CAP] = "Unavailable";
    char total[TEXT_CAP];
    char part[TEXT_CAP];
    int text_x = layout->logo.x + layout->logo.w + ROW_PAD;

    if (info->version_valid)
        snprintf(version, sizeof(version), "Version %s", info->os_version);
    if (info->uptime_valid)
        settings_info_format_duration(uptime, sizeof(uptime), info->uptime_seconds);
    if (info->memory_valid) {
        settings_info_format_size(part, sizeof(part), info->memory_used_kib);
        settings_info_format_size(total, sizeof(total), info->memory_total_kib);
        snprintf(memory, sizeof(memory), "%s of %s used", part, total);
    }
    if (info->storage_valid) {
        settings_info_format_size(part, sizeof(part), info->storage_free_kib);
        settings_info_format_size(total, sizeof(total), info->storage_total_kib);
        snprintf(storage, sizeof(storage), "%s free of %s", part, total);
    }

    draw_title(painter, layout, section_names[SETTINGS_SECTION_ABOUT]);
    if (ui->draw_logo)
        ui->draw_logo(painter->window, layout->logo.x, layout->logo.y, layout->logo.w);
    draw_text_bold(painter, text_x, layout->logo.y + 12,
                   info->version_valid ? info->os_name : "HydrogenOS", palette->ink);
    draw_text(painter, text_x, layout->logo.y + 36, version, palette->ink_dim);

    draw_card(painter, card, ABOUT_ROWS);
    draw_row(painter, card, 0, "Name", info->version_valid ? info->os_name : "HydrogenOS");
    draw_row(painter, card, 1, "Version", info->version_valid ? info->os_version : "-");
    draw_row(painter, card, 2, "Architecture", info->version_valid ? info->os_arch : "-");
    draw_row(painter, card, 3, "Uptime", uptime);
    draw_row(painter, card, 4, "Memory", memory);
    draw_row(painter, card, 5, "Storage", storage);
}

/* ---------------- public API ---------------- */

void settings_ui_init(SettingsUi *ui, Settings *settings, const SettingsPalette *palette,
                      SettingsLogoFn draw_logo, int top) {
    if (!ui)
        return;
    memset(ui, 0, sizeof(*ui));
    ui->settings = settings;
    ui->palette = palette;
    ui->draw_logo = draw_logo;
    ui->top = top;
    ui->hover = SETTINGS_HIT_NONE;
    settings_info_refresh(&ui->info);
    ui->info_refreshed_ms = time_ms();
}

void settings_ui_select(SettingsUi *ui, int section) {
    if (!ui || section < 0 || section >= SETTINGS_SECTION_COUNT)
        return;
    ui->section = section;
    settings_info_refresh(&ui->info);
    ui->info_refreshed_ms = time_ms();
}

void settings_ui_draw(SettingsUi *ui, Window *window) {
    Painter painter;
    SettingsUiLayout layout;

    if (!ui || !ui->settings || !ui->palette || !window || !window->surface)
        return;
    painter.window = window;
    painter.canvas.pixels = (uint32_t *)window->surface;
    painter.canvas.width = window->w;
    painter.canvas.height = window->h;
    painter.canvas.stride = window->w;
    painter.canvas.has_alpha = 0;
    painter.palette = ui->palette;
    settings_ui_layout(window->w, window->h, ui->top, ui->section, &layout);

    fill_rect(&painter, &layout.body, ui->palette->surface);
    draw_sidebar(&painter, ui, &layout);
    switch (ui->section) {
    case SETTINGS_SECTION_APPEARANCE: draw_appearance(&painter, ui, &layout); break;
    case SETTINGS_SECTION_KEYBOARD:   draw_keyboard(&painter, ui, &layout); break;
    case SETTINGS_SECTION_DISPLAY:    draw_display(&painter, ui, &layout); break;
    case SETTINGS_SECTION_NETWORK:    draw_network(&painter, ui, &layout); break;
    default:                          draw_about(&painter, ui, &layout); break;
    }
    if (ui->save_failed) {
        draw_text(&painter, layout.title_x, layout.body.y + layout.body.h - NOTICE_FROM_BOTTOM,
                  "Settings could not be saved.", ui->palette->danger);
    }
}

unsigned int settings_ui_click(SettingsUi *ui, const Window *window, int x, int y) {
    SettingsUiLayout layout;
    Settings *settings;
    int hit;

    if (!ui || !ui->settings || !window)
        return 0;
    settings = ui->settings;
    settings_ui_layout(window->w, window->h, ui->top, ui->section, &layout);
    hit = settings_ui_hit_test(&layout, x, y);

    if (hit >= SETTINGS_HIT_SECTION && hit < SETTINGS_HIT_SECTION + SETTINGS_SECTION_COUNT) {
        if (hit - SETTINGS_HIT_SECTION == ui->section)
            return 0;
        settings_ui_select(ui, hit - SETTINGS_HIT_SECTION);
        return SETTINGS_UI_REDRAW;
    }
    if (hit >= SETTINGS_HIT_THEME && hit < SETTINGS_HIT_THEME + 2) {
        if (settings->theme == hit - SETTINGS_HIT_THEME)
            return 0;
        settings->theme = hit - SETTINGS_HIT_THEME;
        return SETTINGS_UI_REDRAW | SETTINGS_UI_CHANGED;
    }
    if (hit >= SETTINGS_HIT_ACCENT && hit < SETTINGS_HIT_ACCENT + SETTINGS_ACCENT_COUNT) {
        if (settings->accent == hit - SETTINGS_HIT_ACCENT)
            return 0;
        settings->accent = hit - SETTINGS_HIT_ACCENT;
        return SETTINGS_UI_REDRAW | SETTINGS_UI_CHANGED;
    }
    if (hit == SETTINGS_HIT_GLASS) {
        settings->glass = !settings->glass;
        return SETTINGS_UI_REDRAW | SETTINGS_UI_CHANGED;
    }
    if (hit >= SETTINGS_HIT_LAYOUT && hit < SETTINGS_HIT_LAYOUT + 2) {
        if (settings->layout == hit - SETTINGS_HIT_LAYOUT)
            return 0;
        settings->layout = hit - SETTINGS_HIT_LAYOUT;
        return SETTINGS_UI_REDRAW | SETTINGS_UI_CHANGED;
    }
    if (hit == SETTINGS_HIT_PING) {
        if (ui->ping_pending || !ui->info.network_valid)
            return 0;
        ui->ping_pending = 1;
        ui->ping_done = 0;
        return SETTINGS_UI_REDRAW;
    }
    return 0;
}

unsigned int settings_ui_hover(SettingsUi *ui, const Window *window, int x, int y) {
    SettingsUiLayout layout;
    int hit;

    if (!ui || !window)
        return 0;
    settings_ui_layout(window->w, window->h, ui->top, ui->section, &layout);
    hit = settings_ui_hit_test(&layout, x, y);
    /* Only sidebar items and the button react to the pointer. */
    if (!((hit >= SETTINGS_HIT_SECTION && hit < SETTINGS_HIT_SECTION + SETTINGS_SECTION_COUNT) ||
          hit == SETTINGS_HIT_PING))
        hit = SETTINGS_HIT_NONE;
    if (hit == ui->hover)
        return 0;
    ui->hover = hit;
    return SETTINGS_UI_REDRAW;
}

/* Fields that change while a page is open; the rest is constant per boot. */
static int info_changed(const SettingsInfo *before, const SettingsInfo *after) {
    return before->uptime_seconds != after->uptime_seconds ||
           before->memory_used_kib != after->memory_used_kib ||
           before->storage_free_kib != after->storage_free_kib ||
           before->network_valid != after->network_valid ||
           before->network.flags != after->network.flags ||
           before->network.ipv4 != after->network.ipv4 ||
           before->network.rx_frames != after->network.rx_frames ||
           before->network.tx_frames != after->network.tx_frames ||
           before->network.rx_dropped != after->network.rx_dropped;
}

unsigned int settings_ui_service(SettingsUi *ui, unsigned long now_ms) {
    SettingsInfo before;

    if (!ui)
        return 0;
    if (ui->ping_pending) {
        ui->ping_result = settings_info_ping_gateway(&ui->info, SETTINGS_UI_PING_TIMEOUT_MS);
        ui->ping_pending = 0;
        ui->ping_done = 1;
        settings_info_refresh(&ui->info);
        ui->info_refreshed_ms = time_ms();
        return SETTINGS_UI_REDRAW;
    }
    if (ui->section == SETTINGS_SECTION_APPEARANCE || ui->section == SETTINGS_SECTION_KEYBOARD ||
        now_ms - ui->info_refreshed_ms < LIVE_REFRESH_MS)
        return 0;

    before = ui->info;
    settings_info_refresh(&ui->info);
    ui->info_refreshed_ms = now_ms;
    return info_changed(&before, &ui->info) ? SETTINGS_UI_REDRAW : 0;
}
