#include "mini_libc.h"
#include "app_editor.h"
#include "app_terminal.h"
#include "compositor.h"
#include "filemanager.h"
#include "gfx_effects.h"
#include "iconset.h"
#include "icons.h"
#include "kbd_keys.h"
#include "settings.h"
#include "settings_ui.h"
#include "spotlight.h"
#include "syscall.h"
#include "wallclock.h"
#include "window_geometry.h"

#define TITLE_H   30
#define PAD       12
#define LINE_H    16
#define TASKBAR_H 56
/* Height of the top bar; windows maximize and drag below it. */
#define TOPBAR_H  28
/* Side margin of the top bar's contents. */
#define TOPBAR_PAD 16
#define MAX_APPS  8
/* Rows a wheel notch scrolls the terminal by. */
#define TERMINAL_WHEEL_ROWS 3
/* Width of TIME_FORMAT_SHORT ("Thu 29 Oct 14:05") in 8px glyphs. */
#define CLOCK_W   (16 * 8)

/* The user's preferences and the palette derived from them. g_applied is what
   the session currently shows, so a change is detected by diffing against it. */
static Settings g_settings;
static Settings g_applied;
static SettingsPalette g_th;
static int g_settings_save_failed;

#define COL_ACCENT    (g_th.accent)
#define COL_WIN       (g_th.terminal_bg)
#define COL_TITLE     (g_th.title)
#define COL_TITLE_F   (g_th.title_focus)
#define COL_TEXT      (g_th.title_text)
#define COL_DIM       (g_th.terminal_dim)
#define COL_MENU      (g_th.menu)
#define COL_CLOSE     (g_th.close)
#define COL_MIN       (g_th.minimize)
#define COL_MAX       (g_th.maximize)
#define COL_EDIT_BG   (g_th.document_bg)
#define COL_EDIT_FG   (g_th.document_text)
#define COL_SELECTION (g_th.document_selection)
#define COL_BG_TOP    (g_th.wallpaper_top)
#define COL_BG_BOTTOM (g_th.wallpaper_bottom)
#define COL_TASKBAR   (g_th.taskbar)
#define COL_CHROME_TEXT (g_th.chrome_text)
#define COL_CHROME_DIM  (g_th.chrome_dim)
#define COL_LOGO        (g_th.logo)
#define COL_TITLE_BORDER (g_th.title_border)

enum { APP_TERM = 0, APP_EDIT, APP_FILES, APP_SETTINGS, APP_KIND_COUNT };

typedef struct App {
    int used;
    int kind;
    Window *win;
    int minimized;
    int maximized;
    /* Order of the last focus; the highest stamp is the topmost window. */
    unsigned long focus_stamp;
    int dragging;
    int resizing;
    int drag_ox, drag_oy;
    int rx, ry, rw, rh;
    char title[64];
    /* terminal */
    Terminal *term;
    char start_command[4 * FM_PATH_CAP + 4];  /* typed at the first prompt; empty once done */
    /* editor */
    Editor *editor;
    /* files */
    FileManager files;
    /* settings */
    SettingsUi settings_ui;
} App;

static App g_apps[MAX_APPS];
static int g_focus = -1;
static int SW, SH;
static Window *g_bg;
static Window *g_topbar;
static Window *g_taskbar;
static Window *g_launcher;
static int g_mx = -1, g_my = -1, g_btn = 0;
static int g_prev_mx = -1, g_prev_my = -1;
static unsigned long g_last_sec = 0;
static unsigned long g_focus_clock = 0;
/* The previous title bar press, to recognise a double click. */
static int g_click_app = -1;
static int g_click_x, g_click_y;
static unsigned long g_click_ms;

static void draw_terminal(App *a);
static void draw_editor(App *a);
static void draw_files(App *a);
static void draw_settings(App *a);
static void draw_app_content(App *a);

/* ---------------- basic drawing ---------------- */

static void fill_rect(Window *win, int x, int y, int w, int h, uint32_t color) {
    if (!win) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > win->w) w = win->w - x;
    if (y + h > win->h) h = win->h - y;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            ((uint32_t *)win->surface)[yy * win->w + xx] = color;
}

static GfxCanvas window_canvas(Window *win) {
    GfxCanvas canvas = { (uint32_t *)win->surface, win->w, win->h, win->w,
                         win->style.material == COMP_MATERIAL_GLASS };
    return canvas;
}

/* Anti-aliased rounded rectangle; a square with radius = size / 2 is a disc. */
static void rounded_rect(Window *win, int x, int y, int w, int h, int r, uint32_t color) {
    if (!win || !win->surface) return;
    GfxCanvas canvas = window_canvas(win);
    gfx_fill_round_rect(&canvas, x, y, w, h, r, color & 0x00FFFFFFu, 255);
}

static void fill_circle(Window *win, int cx, int cy, int r, uint32_t color) {
    rounded_rect(win, cx - r, cy - r, 2 * r + 1, 2 * r + 1, r, color);
}

static void draw_cursor(uint32_t *buf, int w, int h, int mx, int my) {
    static const unsigned char arrow[16] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
        0xF8, 0xE8, 0xC8, 0x84, 0x04, 0x00, 0x00, 0x00
    };
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t color = pass == 0 ? 0x00000000u : 0x00FFFFFFu;
        int off = pass == 0 ? 1 : 0;
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 8; ++col)
                if (arrow[row] & (1u << (7 - col))) {
                    int x = mx + col + off, y = my + row + off;
                    if (x >= 0 && y >= 0 && x < w && y < h) buf[y * w + x] = color;
                }
    }
}

static void draw_resize_cursor(uint32_t *buf, int w, int h, int mx, int my) {
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t color = pass == 0 ? 0x00000000u : 0x00FFFFFFu;
        int off = pass == 0 ? 1 : 0;
        for (int i = 1; i < 11; ++i) {
            int x = mx + i + off, y = my + i + off;
            if (x >= 0 && y >= 0 && x < w && y < h) buf[y * w + x] = color;
            if (x + 1 < w) buf[y * w + x + 1] = color;
        }
        int pts[6][2] = { {0,0},{1,0},{0,1},{11,11},{10,11},{11,10} };
        for (int i = 0; i < 6; ++i) {
            int x = mx + pts[i][0] + off, y = my + pts[i][1] + off;
            if (x >= 0 && y >= 0 && x < w && y < h) buf[y * w + x] = color;
        }
    }
}

static App *app_at(int mx, int my);

static int cursor_over_resize(int mx, int my) {
    App *a = app_at(mx, my);
    if (!a || a->maximized) return 0;
    Window *w = a->win;
    return mx - w->x >= w->w - 16 && my - w->y >= w->h - 16;
}

static void str_copy_to(char *dst, const char *src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

/* ---------------- icons ---------------- */

/* The logo is a silhouette in a single ink colour, given as ICON_INK(rgb). */
#define ICON_INK(rgb) (0xFF000000u | ((rgb) & 0x00FFFFFFu))

/* The logo is a drop on a dark tile. Keyed out, only the drop remains, so it
   sits on any chrome in a theme-appropriate ink instead of as a dark square.
   Pixels at or below LOGO_KEY_LOW luma are tile, at or above LOGO_KEY_HIGH drop. */
#define LOGO_KEY_LOW  48
#define LOGO_KEY_HIGH 112

/* Bilinear sample of one 8-bit channel (at bit offset shift) of four pixels;
   wx and wy are 0..255 weights towards the right and lower neighbour. */
static uint32_t icon_channel(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11,
                             uint32_t wx, uint32_t wy, int shift) {
    uint32_t top = (((p00 >> shift) & 0xFF) * (256 - wx) + ((p10 >> shift) & 0xFF) * wx) >> 8;
    uint32_t bottom = (((p01 >> shift) & 0xFF) * (256 - wx) + ((p11 >> shift) & 0xFF) * wx) >> 8;
    return (top * (256 - wy) + bottom * wy) >> 8;
}

static void blit_icon(Window *win, int x, int y, const uint32_t *icon, int size, uint32_t ink, int key_dark) {
    if (!win || !win->surface || !icon || size <= 0) return;
    uint32_t *dst = (uint32_t *)win->surface;
    for (int py = 0; py < size; ++py) {
        int sy16 = ((2 * py + 1) * ICON_SIZE * 256) / (2 * size) - 128;
        int sy0 = sy16 >> 8;
        int wy = sy16 & 0xFF;
        if (sy0 < 0) { sy0 = 0; wy = 0; }
        int sy1 = sy0 + 1;
        if (sy1 >= ICON_SIZE) sy1 = ICON_SIZE - 1;
        for (int px = 0; px < size; ++px) {
            int sx16 = ((2 * px + 1) * ICON_SIZE * 256) / (2 * size) - 128;
            int sx0 = sx16 >> 8;
            int wx = sx16 & 0xFF;
            if (sx0 < 0) { sx0 = 0; wx = 0; }
            int sx1 = sx0 + 1;
            if (sx1 >= ICON_SIZE) sx1 = ICON_SIZE - 1;
            uint32_t p00 = icon[sy0 * ICON_SIZE + sx0];
            uint32_t p10 = icon[sy0 * ICON_SIZE + sx1];
            uint32_t p01 = icon[sy1 * ICON_SIZE + sx0];
            uint32_t p11 = icon[sy1 * ICON_SIZE + sx1];
            uint32_t a = icon_channel(p00, p10, p01, p11, (uint32_t)wx, (uint32_t)wy, 24);
            if (key_dark) {
                uint32_t r = icon_channel(p00, p10, p01, p11, (uint32_t)wx, (uint32_t)wy, 16);
                uint32_t g = icon_channel(p00, p10, p01, p11, (uint32_t)wx, (uint32_t)wy, 8);
                uint32_t b = icon_channel(p00, p10, p01, p11, (uint32_t)wx, (uint32_t)wy, 0);
                int luma = (int)((299 * r + 587 * g + 114 * b) / 1000);
                int key = (luma - LOGO_KEY_LOW) * 255 / (LOGO_KEY_HIGH - LOGO_KEY_LOW);
                a = a * (uint32_t)(key < 0 ? 0 : (key > 255 ? 255 : key)) / 255;
            }
            if (a == 0) continue;
            int dx = x + px, dy = y + py;
            if (dx < 0 || dy < 0 || dx >= win->w || dy >= win->h) continue;
            uint32_t *d = &dst[dy * win->w + dx];
            uint32_t rgb = ink & 0x00FFFFFFu;
            if (win->style.material == COMP_MATERIAL_GLASS)
                *d = gfx_over(*d, 0xFF000000u | rgb, a);
            else
                *d = gfx_mix(*d, rgb, a) & 0x00FFFFFFu;
        }
    }
}

/* The drop fills about three quarters of its tile, so it is drawn a third
   larger, centred on the slot, to match the visual weight of the other icons. */
static void draw_icon_os(Window *win, int x, int y, int s) {
    int grown = s + s / 3;
    blit_icon(win, x - (grown - s) / 2, y - (grown - s) / 2, icon_os, grown, ICON_INK(COL_LOGO), 1);
}

static void draw_clock(Window *win, int right_x, int y) {
    char buf[32];
    CivilTime now;
    if (wallclock_now(&now, 0, 0) != 0 ||
        time_format(buf, sizeof(buf), TIME_FORMAT_SHORT, &now) < 0) {
        unsigned long minutes = time_ms() / 60000;
        snprintf(buf, sizeof(buf), "up %02lu:%02lu", minutes / 60, minutes % 60);
    }
    comp_text_render(win, right_x - (int)strlen(buf) * 8, y, buf, COL_CHROME_TEXT);
}

/* ---------------- taskbar ---------------- */

static int tb_logo_x(void) { return 12; }
static int tb_logo_y(void) { return SH - TASKBAR_H + 12; }
static int tb_icon_x(int kind) { return 60 + kind * 44; }
static int tb_y(void) { return SH - TASKBAR_H + 12; }
static int tb_icon_s(void) { return ICON_SIZE_APP; }

static const IconId g_app_icons[APP_KIND_COUNT] = {
    [APP_TERM] = ICON_TERMINAL, [APP_EDIT] = ICON_EDITOR,
    [APP_FILES] = ICON_FILES, [APP_SETTINGS] = ICON_SETTINGS,
};

static void draw_kind_icon(Window *win, int kind, int x, int y, int s) {
    GfxCanvas canvas = window_canvas(win);
    icon_draw(&canvas, g_app_icons[kind], s, x, y, g_th.light, 255);
}

/* 0: no window of the app, 1: only minimized windows, 2: a window is open. */
static int app_presence(int kind) {
    int presence = 0;
    for (int i = 0; i < MAX_APPS; ++i) {
        if (!g_apps[i].used || g_apps[i].kind != kind) continue;
        if (!g_apps[i].minimized) return 2;
        presence = 1;
    }
    return presence;
}

/* With glass on, the taskbar and the menu are glass windows whose surface
   alpha 0 is bare glass; with glass off they are solid in their tint. */
#define GLASS_CLEAR 0x00000000u
#define GLASS_INK   0xFF000000u

static uint32_t chrome_clear(uint32_t solid_tint) {
    return g_settings.glass ? GLASS_CLEAR : solid_tint;
}

/* The text of a top bar row sits centred between its edges. */
#define TOPBAR_TEXT_Y ((TOPBAR_H - 16) / 2)

static const char *app_kind_name(int kind) {
    switch (kind) {
    case APP_TERM: return "Terminal";
    case APP_EDIT: return "Editor";
    case APP_FILES: return "Files";
    default: return "Settings";
    }
}

/* The name of the focused app on the left, the clock on the right. */
static void draw_topbar(void) {
    const char *name = g_focus >= 0 && g_apps[g_focus].used && !g_apps[g_focus].minimized
                           ? app_kind_name(g_apps[g_focus].kind)
                           : "HydrogenOS";
    fill_rect(g_topbar, 0, 0, SW, TOPBAR_H, chrome_clear(COL_TASKBAR));
    comp_text_render(g_topbar, TOPBAR_PAD, TOPBAR_TEXT_Y, name, COL_CHROME_TEXT);
    draw_clock(g_topbar, SW - TOPBAR_PAD, TOPBAR_TEXT_Y);
    fill_rect(g_topbar, 0, TOPBAR_H - 1, SW, 1, GLASS_INK | COL_TITLE_BORDER);
}

static void draw_taskbar(void) {
    int ty = SH - TASKBAR_H;
    fill_rect(g_taskbar, 0, 0, SW, TASKBAR_H, chrome_clear(COL_TASKBAR));
    draw_icon_os(g_taskbar, tb_logo_x(), tb_logo_y() - ty, 32);
    for (int k = 0; k < APP_KIND_COUNT; ++k) {
        draw_kind_icon(g_taskbar, k, tb_icon_x(k), tb_y() - ty, tb_icon_s());
        int presence = app_presence(k);
        if (presence)
            fill_rect(g_taskbar, tb_icon_x(k), TASKBAR_H - 3, tb_icon_s(), 3,
                      GLASS_INK | (presence == 2 ? COL_ACCENT : COL_CHROME_DIM));
    }
}

static void style_chrome_window(Window *win, uint32_t tint, unsigned int tint_alpha, int radius, int shadow) {
    CompWindowStyle style;
    if (g_settings.glass) {
        comp_style_glass(&style);
        style.glass_tint = tint;
        style.glass_tint_alpha = tint_alpha;
    } else {
        comp_style_plain(&style);
    }
    style.corner_radius = radius;
    style.shadow = shadow;
    comp_set_window_style(win, &style);
}

/* The launcher is read over whatever windows lie beneath it, so its glass is
   denser than the taskbar's. */
#define LAUNCHER_EXTRA_OPACITY 70
#define LAUNCHER_MAX_OPACITY   240

static void style_chrome(void) {
    unsigned int launcher_alpha = g_th.glass_alpha + LAUNCHER_EXTRA_OPACITY;
    if (launcher_alpha > LAUNCHER_MAX_OPACITY) launcher_alpha = LAUNCHER_MAX_OPACITY;
    style_chrome_window(g_topbar, COL_TASKBAR, g_th.glass_alpha, 0, 0);
    style_chrome_window(g_taskbar, COL_TASKBAR, g_th.glass_alpha, 0, 0);
    style_chrome_window(g_launcher, COL_MENU, launcher_alpha, 16, 1);
}

static void draw_background(void) {
    uint32_t *px = (uint32_t *)g_bg->surface;
    int top = (int)COL_BG_TOP;
    int bot = (int)COL_BG_BOTTOM;
    int rt = (top >> 16) & 0xFF, gt = (top >> 8) & 0xFF, bt = top & 0xFF;
    int rb = (bot >> 16) & 0xFF, gb = (bot >> 8) & 0xFF, bb = bot & 0xFF;
    for (int y = 0; y < SH; ++y) {
        int r = rt + ((rb - rt) * y) / (SH - 1);
        int g = gt + ((gb - gt) * y) / (SH - 1);
        int b = bt + ((bb - bt) * y) / (SH - 1);
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        for (int x = 0; x < SW; ++x) px[y * SW + x] = c;
    }
    style_chrome();
    draw_topbar();
    draw_taskbar();
}

/* ---------------- launcher ---------------- */

static int launcher_x(void) { return (SW - SPOTLIGHT_WIDTH) / 2; }

/* A sixth of the way down, but never into the taskbar. */
static int launcher_y(void) {
    int y = SH - TASKBAR_H - SPOTLIGHT_HEIGHT - 8;
    return SH / 6 < y ? SH / 6 : (y > 8 ? y : 8);
}

/* ---------------- terminal app ---------------- */

static int app_open_new(int kind);

/* Carries out what a program in a terminal asked the desktop for. */
static void handle_terminal_request(const TerminalRequest *request) {
    if (request->kind == TERMINAL_REQUEST_FILES) {
        app_open_new(APP_FILES);
        return;
    }
    int idx = app_open_new(APP_EDIT);
    if (idx < 0 || !request->argument[0]) return;
    App *e = &g_apps[idx];
    editor_open(e->editor, request->argument);
    draw_app_content(e);
    comp_damage_rect(g_bg, e->win->x, e->win->y, e->win->w, e->win->h);
}

/* ---------------- rendering per app ---------------- */

static void draw_titlebar(App *a) {
    Window *w = a->win;
    int focused = (g_focus >= 0 && &g_apps[g_focus] == a) && !a->minimized;
    fill_rect(w, 0, 0, w->w, TITLE_H, focused ? COL_TITLE_F : COL_TITLE);
    fill_rect(w, 0, TITLE_H, w->w, 1, COL_TITLE_BORDER);
    fill_circle(w, 18, 15, 6, COL_CLOSE);
    fill_circle(w, 38, 15, 6, COL_MIN);
    fill_circle(w, 58, 15, 6, COL_MAX);
    comp_text_render(w, 78, 7, a->title, COL_TEXT);
    if (a->kind == APP_EDIT && a->editor && editor_modified(a->editor))
        fill_circle(w, 78 + (int)strlen(a->title) * 8 + 12, 15, 4, COL_ACCENT);
}

static void draw_terminal(App *a) {
    Window *w = a->win;
    AppTerminalTheme theme;
    fill_rect(w, 0, TITLE_H, w->w, w->h - TITLE_H, COL_WIN);
    draw_titlebar(a);
    app_terminal_theme(&g_th, &theme);
    app_terminal_draw(a->term, w, TITLE_H, g_focus >= 0 && &g_apps[g_focus] == a, &theme);
}

static void draw_editor(App *a) {
    if (editor_draw(a->editor, a->win, &g_th, TITLE_H) & EDITOR_DRAW_TITLE) {
        editor_title(a->editor, a->title, sizeof(a->title));
        draw_titlebar(a);
        comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, TITLE_H);
    }
}

static void draw_files(App *a) {
    fm_draw(&a->files, a->win);
    draw_titlebar(a);
}

static void draw_settings(App *a) {
    a->settings_ui.save_failed = g_settings_save_failed;
    settings_ui_draw(&a->settings_ui, a->win);
    draw_titlebar(a);
}

static void draw_app_content(App *a) {
    switch (a->kind) {
    case APP_TERM: draw_terminal(a); break;
    case APP_EDIT: editor_invalidate(a->editor); draw_editor(a); break;
    case APP_FILES: draw_files(a); break;
    default: draw_settings(a); break;
    }
}

/* ---------------- window manager ---------------- */

static int point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int app_index_of(App *a) {
    for (int i = 0; i < MAX_APPS; ++i) if (&g_apps[i] == a) return i;
    return -1;
}

static CompRect work_area(void) {
    return wm_work_area(SW, SH, TOPBAR_H, TASKBAR_H);
}

static void refresh_taskbar(void) {
    draw_taskbar();
    comp_damage_rect(g_bg, 0, SH - TASKBAR_H, SW, TASKBAR_H);
}

static void refresh_topbar(void) {
    draw_topbar();
    comp_damage_rect(g_bg, 0, 0, SW, TOPBAR_H);
}

/* Repaints what shows which window has the focus: the top bar and the title bars. */
static void refresh_focus_chrome(void) {
    refresh_topbar();
    for (int i = 0; i < MAX_APPS; ++i) {
        App *o = &g_apps[i];
        if (o->used && o->win && o->win->visible && !o->minimized) {
            if (o->kind == APP_TERM) {
                draw_terminal(o);
                comp_damage_rect(g_bg, o->win->x, o->win->y, o->win->w, o->win->h);
            } else {
                draw_titlebar(o);
                comp_damage_rect(g_bg, o->win->x, o->win->y, o->win->w, TITLE_H);
            }
        }
    }
}

/* Puts the window on top of the other app windows. The top bar and the taskbar
   stay above them all, so a window's shadow never darkens them. */
static void raise_app_window(App *a) {
    a->focus_stamp = ++g_focus_clock;
    comp_raise(a->win);
    comp_raise(g_topbar);
    comp_raise(g_taskbar);
}

/* The visible window that was focused last, or -1. */
static int topmost_app(void) {
    int top = -1;
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->minimized || !a->win) continue;
        if (top < 0 || a->focus_stamp > g_apps[top].focus_stamp) top = i;
    }
    return top;
}

/* The window under the pointer: the topmost visible one, bars excluded. */
static App *app_at(int mx, int my) {
    App *hit = 0;
    if (my < TOPBAR_H || my >= SH - TASKBAR_H) return 0;
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->minimized || !a->win || !a->win->visible) continue;
        if (!point_in_rect(mx, my, a->win->x, a->win->y, a->win->w, a->win->h)) continue;
        if (!hit || a->focus_stamp > hit->focus_stamp) hit = a;
    }
    return hit;
}

static void bring_to_front(App *a, int idx) {
    g_focus = idx;
    if (a->win) raise_app_window(a);
    refresh_focus_chrome();
}

static void focus_app(App *a) {
    int idx = app_index_of(a);
    if (idx >= 0 && idx != g_focus) bring_to_front(a, idx);
}

static void app_minimize(App *a) {
    if (!a->used || a->minimized) return;
    a->minimized = 1;
    a->dragging = 0;
    a->resizing = 0;
    if (a->win) a->win->visible = 0;
    if (g_focus == app_index_of(a)) g_focus = topmost_app();
    refresh_focus_chrome();
    refresh_taskbar();
}

static void app_restore(App *a) {
    if (!a->used || !a->minimized) return;
    a->minimized = 0;
    if (a->win) a->win->visible = 1;
    bring_to_front(a, app_index_of(a));
    refresh_taskbar();
}

static void app_close(App *a) {
    if (!a->used) return;
    int idx = app_index_of(a);
    if (a->term) {
        terminal_destroy(a->term);
        a->term = 0;
    }
    if (a->win) {
        comp_destroy_window(a->win);
        a->win = 0;
    }
    editor_destroy(a->editor);
    a->editor = 0;
    if (a->kind == APP_FILES) fm_destroy(&a->files);
    a->used = 0;
    a->minimized = 0;
    a->dragging = 0;
    a->resizing = 0;
    if (g_click_app == idx) g_click_app = -1;
    if (g_focus == idx) g_focus = topmost_app();
    refresh_focus_chrome();
    refresh_taskbar();
}

/* Floating windows get rounded corners and a shadow; maximized ones fill the
   work area edge to edge and cast none. */
static void style_app_window(Window *win, int maximized) {
    CompWindowStyle style;
    comp_style_window(&style);
    if (maximized) {
        style.corner_radius = 0;
        style.shadow = 0;
    }
    comp_set_window_style(win, &style);
}

/* Maximizes into the work area, or restores the geometry from before. */
static void app_toggle_maximize(App *a) {
    Window *w = a->win;
    if (!w) return;
    if (!a->maximized) {
        CompRect work = work_area();
        int rx = w->x, ry = w->y, rw = w->w, rh = w->h;
        if (comp_resize_window(w, work.w, work.h) != 0) return;
        a->rx = rx; a->ry = ry; a->rw = rw; a->rh = rh;
        w->x = work.x; w->y = work.y;
        a->maximized = 1;
    } else {
        if (comp_resize_window(w, a->rw, a->rh) != 0) return;
        w->x = a->rx; w->y = a->ry;
        a->maximized = 0;
    }
    style_app_window(w, a->maximized);
    draw_app_content(a);
}

/* Alt+Tab: focuses the next open window. */
static void cycle_focus(void) {
    for (int step = 1; step <= MAX_APPS; ++step) {
        int i = (g_focus + step + MAX_APPS) % MAX_APPS;
        if (!g_apps[i].used || g_apps[i].minimized) continue;
        focus_app(&g_apps[i]);
        return;
    }
}

/* What a click on a taskbar icon does to the windows of that app. */
static void dock_click(int kind) {
    int target = -1;
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->kind != kind) continue;
        /* Visible windows win over minimized ones, then the most recent. */
        if (target < 0 || (g_apps[target].minimized && !a->minimized) ||
            (g_apps[target].minimized == a->minimized && a->focus_stamp > g_apps[target].focus_stamp))
            target = i;
    }
    switch (wm_dock_action(target >= 0, target >= 0 && g_apps[target].minimized,
                           target >= 0 && target == g_focus)) {
    case WM_DOCK_LAUNCH: app_open_new(kind); break;
    case WM_DOCK_RESTORE: app_restore(&g_apps[target]); break;
    case WM_DOCK_RAISE: focus_app(&g_apps[target]); break;
    case WM_DOCK_MINIMIZE: app_minimize(&g_apps[target]); break;
    }
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_APPS; ++i) if (!g_apps[i].used) return i;
    return -1;
}

static uint32_t app_background(int kind) {
    switch (kind) {
    case APP_EDIT: return COL_EDIT_BG;
    case APP_FILES:
    case APP_SETTINGS: return g_th.surface;
    default: return COL_WIN;
    }
}

static int app_open_new(int kind) {
    int idx = find_free_slot();
    if (idx < 0) return -1;
    App *a = &g_apps[idx];
    for (int i = 0; i < (int)sizeof(App); ++i) ((char *)a)[i] = 0;

    CompRect work = work_area();
    int w = SW * 45 / 100, h = SH * 62 / 100;
    if (w < 720) w = 720;
    if (w > SW - 120) w = SW - 120;
    if (h < 420) h = 420;
    if (h > work.h - 80) h = work.h - 80;
    if (kind == APP_FILES) { w = 760; h = 500; }
    if (kind == APP_SETTINGS) { w = 700; h = 480; }
    if (w > SW) w = SW;
    if (h > work.h) h = work.h;
    int x = (SW - w) / 2 + (idx % 4) * 30 - 60;
    int y = work.y + (work.h - h) / 2 + (idx % 4) * 30 - 40;
    if (x < 0) x = 0;
    if (y < work.y) y = work.y;
    if (x + w > SW) x = SW - w;
    if (y + h > work.y + work.h) y = work.y + work.h - h;

    a->used = 1;
    a->kind = kind;
    a->win = comp_create_window(x, y, w, h);
    if (!a->win) { a->used = 0; return -1; }
    a->win->bg_color = app_background(kind);
    style_app_window(a->win, 0);

    str_copy_to(a->title, app_kind_name(kind), sizeof(a->title));

    if (kind == APP_TERM) {
        a->term = app_terminal_open(a->win, TITLE_H);
        if (!a->term) {
            comp_destroy_window(a->win);
            a->win = 0;
            a->used = 0;
            return -1;
        }
    } else if (kind == APP_EDIT) {
        a->editor = editor_create();
        if (!a->editor) {
            comp_destroy_window(a->win);
            a->win = 0;
            a->used = 0;
            return -1;
        }
    } else if (kind == APP_FILES) {
        char cwd[FM_PATH_CAP];
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
        if (fm_init(&a->files, &g_th, TITLE_H, cwd) != 0) {
            comp_destroy_window(a->win);
            a->win = 0;
            a->used = 0;
            return -1;
        }
    } else if (kind == APP_SETTINGS) {
        settings_ui_init(&a->settings_ui, &g_settings, &g_th, draw_icon_os, TITLE_H);
    }

    draw_app_content(a);
    bring_to_front(a, idx);
    refresh_taskbar();
    return idx;
}

/* Moves shell output into the terminal windows and serves their requests. A
   shell that ended cleanly closes its window; one that failed keeps it, with
   the status, until Enter starts a new shell. */
static int terminal_service_all(void) {
    int redrawn = 0;
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->kind != APP_TERM || !a->term) continue;
        unsigned events = terminal_pump(a->term);
        if (a->start_command[0]) {
            if (!terminal_alive(a->term)) a->start_command[0] = 0;
            else if (terminal_run_line(a->term, a->start_command) == 0) {
                a->start_command[0] = 0;
                events |= TERMINAL_EVENT_OUTPUT;
            }
        }
        TerminalRequest request;
        while (terminal_take_request(a->term, &request)) handle_terminal_request(&request);
        if ((events & TERMINAL_EVENT_EXITED) && terminal_exit_status(a->term) == 0) {
            app_close(a);
            redrawn = 1;
            continue;
        }
        if (events && !a->minimized && a->win->visible) {
            draw_terminal(a);
            comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
            redrawn = 1;
        }
    }
    return redrawn;
}

/* ---------------- settings ---------------- */

static void redraw_session(void) {
    draw_background();
    spotlight_redraw();
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || !a->win) continue;
        a->win->bg_color = app_background(a->kind);
        if (!a->minimized) draw_app_content(a);
    }
    comp_damage_all();
}

/* Makes the session show g_settings: keyboard layout, palette, materials and
   every window; then persists the model. */
static void apply_settings(void) {
    unsigned int changed = settings_diff(&g_applied, &g_settings);
    if (!changed) return;

    if (changed & SETTINGS_CHANGED_LAYOUT) {
        /* The kernel reports the layout it ended up with. */
        long active = sys_set_layout(g_settings.layout);
        if (active == SETTINGS_LAYOUT_DE || active == SETTINGS_LAYOUT_US)
            g_settings.layout = (int)active;
    }
    g_settings_save_failed = settings_save(SETTINGS_PATH, &g_settings) != 0;
    g_applied = g_settings;
    settings_palette(&g_settings, &g_th);
    redraw_session();
}

/* A missing or unreadable file keeps the defaults: that is the normal state
   of a fresh installation. */
static void settings_startup(void) {
    settings_defaults(&g_settings);
    settings_load(SETTINGS_PATH, &g_settings);
    sys_set_layout(g_settings.layout);
    g_applied = g_settings;
    settings_palette(&g_settings, &g_th);
}

/* ---------------- files ---------------- */

static int shell_word_safe(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '/' || c == '.' || c == '_' || c == '-' || c == '+' || c == ',' || c == '@';
}

/* Writes path as one shell word: as it is when nothing in it needs quoting,
   otherwise in single quotes with each embedded quote closed, escaped and
   reopened. Returns 0, or -1 if it does not fit. */
static int shell_quote(char *out, size_t capacity, const char *path) {
    size_t n = 0;
    int plain = path[0] != 0;
    for (const char *p = path; *p; ++p) if (!shell_word_safe(*p)) plain = 0;
    if (plain) {
        if (strlen(path) + 1 > capacity) return -1;
        memcpy(out, path, strlen(path) + 1);
        return 0;
    }
    if (capacity < 3) return -1;
    out[n++] = '\'';
    for (; *path; ++path) {
        size_t need = *path == '\'' ? 4 : 1;
        if (n + need + 2 > capacity) return -1;
        if (*path == '\'') {
            memcpy(out + n, "'\\''", 4);
            n += 4;
        } else {
            out[n++] = *path;
        }
    }
    out[n++] = '\'';
    out[n] = 0;
    return 0;
}

/* A program started from Files runs in a Terminal window of its own, so its
   output is visible and it can read the keyboard. The command is typed once
   the shell shows its prompt (see terminal_service_all). */
static int program_launch(const char *path) {
    int idx = app_open_new(APP_TERM);
    if (idx < 0) return -1;
    if (shell_quote(g_apps[idx].start_command, sizeof(g_apps[idx].start_command), path) != 0) {
        app_close(&g_apps[idx]);
        return -1;
    }
    return 0;
}

/* Carries out what a Files event asked for: opening a file and repainting. */
static void files_apply(App *a, unsigned int result) {
    if (result & FM_ACTION) {
        char path[FM_PATH_CAP];
        int action = fm_take_action(&a->files, path, sizeof(path));
        if (action == FM_ACTION_RUN) {
            if (program_launch(path) != 0) fm_report(&a->files, "The program could not be started.");
        } else if (action == FM_ACTION_OPEN_TEXT) {
            int e = app_open_new(APP_EDIT);
            if (e >= 0) {
                editor_open(g_apps[e].editor, path);
                draw_app_content(&g_apps[e]);
            }
        }
    }
    if (result & FM_REDRAW) {
        draw_files(a);
        comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
    }
}

/* ---------------- hit testing ---------------- */

enum { LAUNCH_ABOUT = APP_KIND_COUNT };

static const SpotlightApp g_launcher_apps[] = {
    { APP_TERM, "Terminal", "shell console command prompt" },
    { APP_EDIT, "Editor", "text notes write" },
    { APP_FILES, "Files", "file manager folder browser" },
    { APP_SETTINGS, "Settings", "preferences appearance theme keyboard layout" },
    { LAUNCH_ABOUT, "About HydrogenOS", "version system information" },
};

static void launcher_draw_icon(Window *win, int id, int x, int y, int size) {
    if (id == LAUNCH_ABOUT) draw_icon_os(win, x, y, size);
    else draw_kind_icon(win, id, x, y, size);
}

static void launcher_launch(int id) {
    int opened = app_open_new(id == LAUNCH_ABOUT ? APP_SETTINGS : id);
    if (id == LAUNCH_ABOUT && opened >= 0) {
        settings_ui_select(&g_apps[opened].settings_ui, SETTINGS_SECTION_ABOUT);
        draw_app_content(&g_apps[opened]);
    }
}

static const SpotlightHost g_launcher_host = {
    g_launcher_apps, (int)(sizeof(g_launcher_apps) / sizeof(g_launcher_apps[0])),
    launcher_draw_icon, launcher_launch,
};

static void handle_press(int mx, int my) {
    if (spotlight_is_open()) {
        if (spotlight_press(mx, my)) return;
        spotlight_close();
        if (point_in_rect(mx, my, tb_logo_x(), tb_logo_y(), 32, 32)) return;
    }

    int ty = SH - TASKBAR_H;
    if (my >= ty) {
        if (point_in_rect(mx, my, tb_logo_x(), tb_logo_y(), 32, 32)) {
            spotlight_open();
            return;
        }
        for (int k = 0; k < APP_KIND_COUNT; ++k) {
            if (point_in_rect(mx, my, tb_icon_x(k), tb_y(), tb_icon_s(), tb_icon_s())) {
                dock_click(k);
                return;
            }
        }
        return;
    }

    {
        App *a = app_at(mx, my);
        if (!a) return;
        Window *w = a->win;
        focus_app(a);
        int lx = mx - w->x, ly = my - w->y;

        /* resize handle: bottom-right 16x16 */
        if (!a->maximized && lx >= w->w - 16 && ly >= w->h - 16) {
            a->resizing = 1;
            a->drag_ox = lx;
            a->drag_oy = ly;
            return;
        }
        if (ly < TITLE_H) {
            int dx = lx - 18, dy = ly - 15;
            if (dx * dx + dy * dy <= 36) {
                if (a->kind == APP_EDIT && !editor_close_allowed(a->editor)) draw_editor(a);
                else app_close(a);
                return;
            }
            dx = lx - 38;
            if (dx * dx + dy * dy <= 36) { app_minimize(a); return; }
            dx = lx - 58;
            if (dx * dx + dy * dy <= 36) { app_toggle_maximize(a); return; }
            int idx = app_index_of(a);
            unsigned long now = time_ms();
            int twice = g_click_app == idx &&
                        wm_is_double_click(g_click_ms, g_click_x, g_click_y, now, mx, my);
            g_click_app = twice ? -1 : idx;
            g_click_ms = now;
            g_click_x = mx;
            g_click_y = my;
            if (twice) {
                app_toggle_maximize(a);
            } else if (!a->maximized) {
                a->dragging = 1;
                a->drag_ox = lx;
                a->drag_oy = ly;
            }
            return;
        }

        /* app-specific clicks */
        if (a->kind == APP_FILES) {
            files_apply(a, fm_click(&a->files, w, lx, ly));
            return;
        } else if (a->kind == APP_EDIT) {
            editor_mouse_press(a->editor, lx, ly, time_ms());
            draw_editor(a);
            return;
        } else if (a->kind == APP_SETTINGS) {
            unsigned int result = settings_ui_click(&a->settings_ui, w, lx, ly);
            if (result & SETTINGS_UI_CHANGED) {
                apply_settings();
            } else if (result & SETTINGS_UI_REDRAW) {
                draw_app_content(a);
                comp_damage_rect(g_bg, w->x, w->y, w->w, w->h);
            }
            return;
        }
        return;
    }
}

static void handle_drag(int mx, int my) {
    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || !a->win) continue;
        Window *w = a->win;
        if (a->dragging) {
            int nx = mx - a->drag_ox;
            int ny = my - a->drag_oy;
            CompRect work = work_area();
            wm_clamp_position(&work, w->w, w->h, TITLE_H, &nx, &ny);
            if (nx == w->x && ny == w->y) continue;
            /* The compositor repaints where the window was and where it is,
               shadow included. */
            w->x = nx; w->y = ny;
        } else if (a->resizing) {
            int nw = mx - w->x;
            int nh = my - w->y;
            int min_w = 320, min_h = 200;
            if (a->kind == APP_SETTINGS) { min_w = SETTINGS_UI_MIN_WIDTH; min_h = SETTINGS_UI_MIN_HEIGHT; }
            if (a->kind == APP_FILES) { min_w = FM_MIN_WIDTH; min_h = FM_MIN_HEIGHT; }
            CompRect work = work_area();
            if (w->x + nw > work.x + work.w) nw = work.x + work.w - w->x;
            if (w->y + nh > work.y + work.h) nh = work.y + work.h - w->y;
            if (nw < min_w) nw = min_w;
            if (nh < min_h) nh = min_h;
            if (nw == w->w && nh == w->h) continue;
            if (comp_resize_window(w, nw, nh) != 0) continue;
            draw_app_content(a);
        }
    }
}

/* Hover highlights of Files and Settings follow the pointer over the topmost window only. */
static void settings_track_pointer(int mx, int my) {
    App *top = app_at(mx, my);
    int under = top ? app_index_of(top) : -1;

    if (spotlight_is_open() && point_in_rect(mx, my, g_launcher->x, g_launcher->y, g_launcher->w, g_launcher->h))
        under = -1;

    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->minimized || !a->win) continue;
        int lx = i == under ? mx - a->win->x : -1;
        int ly = i == under ? my - a->win->y : -1;
        if (a->kind == APP_FILES) {
            files_apply(a, fm_hover(&a->files, a->win, lx, ly));
            continue;
        }
        if (a->kind != APP_SETTINGS) continue;
        if (settings_ui_hover(&a->settings_ui, a->win, lx, ly) & SETTINGS_UI_REDRAW) {
            draw_app_content(a);
            comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
        }
    }
}

/* Runs pending pings and live-value refreshes of the Settings windows. */
static int settings_service_all(void) {
    int redrawn = 0;

    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->kind != APP_SETTINGS || a->minimized || !a->win) continue;
        if (settings_ui_service(&a->settings_ui, time_ms()) & SETTINGS_UI_REDRAW) {
            draw_app_content(a);
            comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
            redrawn = 1;
        }
    }
    return redrawn;
}

static void handle_release(void) {
    for (int i = 0; i < MAX_APPS; ++i) {
        g_apps[i].dragging = 0;
        g_apps[i].resizing = 0;
        if (g_apps[i].used && g_apps[i].kind == APP_EDIT) {
            editor_mouse_release(g_apps[i].editor);
            draw_editor(&g_apps[i]);
        }
    }
}

/* ---------------- main ---------------- */

int main(void) {
    unsigned int info[4];
    long fb_rc = sys_fb_info(info);
    if (fb_rc == SYS_FB_ERR_NODEV) {
        printf("desktop: no framebuffer, desktop session disabled\n");
        return 0;
    }
    if (fb_rc != 0) {
        printf("desktop: framebuffer query failed rc=%d\n", (int)fb_rc);
        return 1;
    }
    SW = (int)info[0];
    SH = (int)info[1];
    /* Prompts that start from the working directory, such as the editor's Save
       As, should open in the user's home. A missing home leaves the root. */
    (void)chdir("/home/user");
    if (SW <= 0 || SH <= 0) return 1;

    sys_console_cursor(0);
    sys_fb_clear();

    if (comp_init_size(SW, SH) != 0) {
        printf("desktop: comp_init failed\n");
        return 1;
    }
    settings_startup();
    g_bg = comp_create_window(0, 0, SW, SH);
    if (!g_bg || !g_bg->surface) return 1;
    g_bg->bg_color = COL_BG_TOP;

    g_topbar = comp_create_window(0, 0, SW, TOPBAR_H);
    if (!g_topbar || !g_topbar->surface) return 1;

    g_taskbar = comp_create_window(0, SH - TASKBAR_H, SW, TASKBAR_H);
    if (!g_taskbar || !g_taskbar->surface) return 1;

    g_launcher = comp_create_window(launcher_x(), launcher_y(), SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT);
    if (!g_launcher || !g_launcher->surface) return 1;
    style_chrome();
    spotlight_init(g_launcher, &g_launcher_host, &g_th, &g_settings);

    draw_background();
    app_open_new(APP_TERM);

    unsigned long checksum = 0;
    {
        uint32_t *px = (uint32_t *)g_bg->surface;
        for (int i = 0; i < 1024 && i < SW * SH; ++i) checksum += px[i];
    }
    printf("FRAME CHECKSUM: %lu\n", checksum);

    comp_damage_all();
    if (comp_render() != 0) return 1;
    comp_present_damage();

    for (;;) {
        int dirty = settings_service_all();
        spotlight_service();
        dirty |= terminal_service_all();

        for (int guard = 0; guard < 64; ++guard) {
            long k = sys_kbd_read();
            if (k < 0) break;
            if (spotlight_key(k)) { dirty = 1; continue; }
            if (k == KBD_KEY_ALT_TAB) {
                cycle_focus();
                dirty = 1;
                continue;
            }
            if (g_focus >= 0 && g_apps[g_focus].used && !g_apps[g_focus].minimized) {
                App *a = &g_apps[g_focus];
                if (a->kind == APP_TERM) {
                    terminal_key(a->term, (int)(k & KBD_CODE_MASK));
                    draw_terminal(a);
                    comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
                    dirty = 1;
                } else if (a->kind == APP_FILES) {
                    files_apply(a, fm_key(&a->files, a->win, (int)k));
                    dirty = 1;
                } else if (a->kind == APP_EDIT) {
                    editor_key(a->editor, (int)k);
                    draw_editor(a);
                    dirty = 1;
                }
            }
        }

        int m[4] = { 0, 0, 0, 0 };
        if (sys_mouse_state(m) == 0) {
            int nmx = m[0], nmy = m[1], nbtn = m[2] & 7;
            int moved = nmx != g_mx || nmy != g_my;
            if (moved) {
                g_mx = nmx;
                g_my = nmy;
                dirty = 1;
                settings_track_pointer(g_mx, g_my);
                spotlight_hover(g_mx, g_my);
            }
            int wheel = m[3];
            int press = (nbtn & 1) && !(g_btn & 1);
            int release = !(nbtn & 1) && (g_btn & 1);
            g_btn = nbtn;
            if (wheel != 0 && g_focus >= 0 && g_apps[g_focus].used && !g_apps[g_focus].minimized) {
                App *fa = &g_apps[g_focus];
                if (fa->kind == APP_TERM) {
                    terminal_scroll(fa->term, wheel * TERMINAL_WHEEL_ROWS);
                    draw_terminal(fa);
                    comp_damage_rect(g_bg, fa->win->x, fa->win->y, fa->win->w, fa->win->h);
                    dirty = 1;
                } else if (fa->kind == APP_FILES) {
                    files_apply(fa, fm_wheel(&fa->files, fa->win, wheel));
                    dirty = 1;
                } else if (fa->kind == APP_EDIT) {
                    editor_scroll(fa->editor, -wheel * EDITOR_WHEEL_LINES);
                    draw_editor(fa);
                    dirty = 1;
                }
            }
            if (press) { handle_press(g_mx, g_my); dirty = 1; }
            int any_drag = 0;
            for (int i = 0; i < MAX_APPS; ++i) if (g_apps[i].dragging || g_apps[i].resizing) any_drag = 1;
            if (any_drag) { handle_drag(g_mx, g_my); dirty = 1; }
            if (moved && (g_btn & 1) && g_focus >= 0 && g_apps[g_focus].used &&
                g_apps[g_focus].kind == APP_EDIT && !g_apps[g_focus].minimized) {
                App *ea = &g_apps[g_focus];
                editor_mouse_drag(ea->editor, g_mx - ea->win->x, g_my - ea->win->y);
                draw_editor(ea);
            }
            if (release) { handle_release(); dirty = 1; }
        }

        unsigned long now_sec = time_ms() / 1000;
        if (now_sec != g_last_sec) {
            g_last_sec = now_sec;
            int clock_x = SW - TOPBAR_PAD - CLOCK_W - 2;
            fill_rect(g_topbar, clock_x, TOPBAR_TEXT_Y, CLOCK_W + 4, 16, chrome_clear(COL_TASKBAR));
            draw_clock(g_topbar, SW - TOPBAR_PAD, TOPBAR_TEXT_Y);
            comp_damage_rect(g_bg, clock_x, TOPBAR_TEXT_Y, CLOCK_W + 4, 16);
            dirty = 1;
        }

        if (dirty) {
            if (g_prev_mx >= 0) comp_damage_rect(g_bg, g_prev_mx, g_prev_my, 14, 17);
            if (g_mx >= 0) comp_damage_rect(g_bg, g_mx, g_my, 14, 17);
            if (comp_render() == 0) {
                comp_present_damage();
                if (g_mx >= 0) {
                    uint32_t *front = comp_front_buffer();
                    if (cursor_over_resize(g_mx, g_my))
                        draw_resize_cursor(front, SW, SH, g_mx, g_my);
                    else
                        draw_cursor(front, SW, SH, g_mx, g_my);
                    sys_fb_blit_strided(g_mx, g_my, 14, 17, front + (long)g_my * SW + g_mx, SW * 4);
                }
            }
            g_prev_mx = g_mx;
            g_prev_my = g_my;
        }

        yield_cpu();
    }
}
