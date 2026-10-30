#include "mini_libc.h"
#include "compositor.h"
#include "gfx_effects.h"
#include "icons.h"
#include "settings.h"
#include "settings_ui.h"
#include "syscall.h"
#include "wallclock.h"
#include "window_geometry.h"

#define TITLE_H   30
#define PAD       12
#define LINE_H    16
#define TASKBAR_H 56
/* Height a top bar reserves above the work area; windows maximize below it. */
#define TOPBAR_H  0
#define MAX_APPS  8
/* Width of TIME_FORMAT_SHORT ("Thu 29 Oct 14:05") in 8px glyphs. */
#define CLOCK_W   (16 * 8)

#define COL_INPUT     0x0080FF80u

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
#define COL_TERM_TEXT (g_th.terminal_text)
#define COL_PROMPT    (g_th.terminal_prompt)
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
#define COL_WALLPAPER_TEXT (g_th.wallpaper_text)
#define COL_CHROME_DIM  (g_th.chrome_dim)
#define COL_CHROME_ICON (g_th.chrome_icon)
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
    char title[24];
    /* terminal */
    char scroll[96][128];
    int scroll_count;
    int scroll_off;
    char input[256];
    int input_len;
    /* editor */
    char text[8192];
    int text_len;
    int cursor;
    int scroll_line;
    char path[128];
    /* files */
    char cwd[128];
    int sel;
    /* settings */
    SettingsUi settings_ui;
} App;

static App g_apps[MAX_APPS];
static int g_focus = -1;
static int SW, SH;
static Window *g_bg;
static Window *g_taskbar;
static Window *g_menu;
static int g_menu_open = 0;
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

/* Anti-aliased rounded rectangle; a square with radius = size / 2 is a disc. */
static void rounded_rect(Window *win, int x, int y, int w, int h, int r, uint32_t color) {
    if (!win || !win->surface) return;
    GfxCanvas canvas = { (uint32_t *)win->surface, win->w, win->h, win->w,
                         win->style.material == COMP_MATERIAL_GLASS };
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

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; ++a; ++b; }
    return *a == 0 && *b == 0;
}

static void str_copy_to(char *dst, const char *src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static void append_num(char *line, int *z, int v) {
    if (v < 0) { line[(*z)++] = '-'; v = -v; }
    char tmp[12]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) line[(*z)++] = tmp[--n];
}

/* ---------------- icons ---------------- */

/* An icon is drawn in its own colours (ICON_NATIVE) or as a silhouette in a
   single ink colour, given as ICON_INK(rgb). */
#define ICON_NATIVE 0u
#define ICON_INK(rgb) (0xFF000000u | ((rgb) & 0x00FFFFFFu))

static void blit_icon(Window *win, int x, int y, const uint32_t *icon, int size, uint32_t ink) {
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
            uint32_t a00 = (p00 >> 24) & 0xFF, a10 = (p10 >> 24) & 0xFF;
            uint32_t a01 = (p01 >> 24) & 0xFF, a11 = (p11 >> 24) & 0xFF;
            uint32_t a0 = (a00 * (256 - wx) + a10 * wx) >> 8;
            uint32_t a1 = (a01 * (256 - wx) + a11 * wx) >> 8;
            uint32_t a = (a0 * (256 - wy) + a1 * wy) >> 8;
            if (a == 0) continue;
            uint32_t r = ((((p00 >> 16) & 0xFF) * (256 - wx) + ((p10 >> 16) & 0xFF) * wx) >> 8);
            uint32_t r2 = ((((p01 >> 16) & 0xFF) * (256 - wx) + ((p11 >> 16) & 0xFF) * wx) >> 8);
            r = (r * (256 - wy) + r2 * wy) >> 8;
            uint32_t g = ((((p00 >> 8) & 0xFF) * (256 - wx) + ((p10 >> 8) & 0xFF) * wx) >> 8);
            uint32_t g2 = ((((p01 >> 8) & 0xFF) * (256 - wx) + ((p11 >> 8) & 0xFF) * wx) >> 8);
            g = (g * (256 - wy) + g2 * wy) >> 8;
            uint32_t b = (((p00 & 0xFF) * (256 - wx) + (p10 & 0xFF) * wx) >> 8);
            uint32_t b2 = (((p01 & 0xFF) * (256 - wx) + (p11 & 0xFF) * wx) >> 8);
            b = (b * (256 - wy) + b2 * wy) >> 8;
            int dx = x + px, dy = y + py;
            if (dx < 0 || dy < 0 || dx >= win->w || dy >= win->h) continue;
            uint32_t *d = &dst[dy * win->w + dx];
            uint32_t rgb = ink ? (ink & 0x00FFFFFFu) : (r << 16) | (g << 8) | b;
            if (win->style.material == COMP_MATERIAL_GLASS)
                *d = gfx_over(*d, 0xFF000000u | rgb, a);
            else
                *d = gfx_mix(*d, rgb, a) & 0x00FFFFFFu;
        }
    }
}

static void draw_icon_os(Window *win, int x, int y, int s) { blit_icon(win, x, y, icon_os, s, ICON_NATIVE); }
static void draw_icon_terminal(Window *win, int x, int y, int s) { blit_icon(win, x, y, icon_terminal, s, ICON_INK(COL_CHROME_ICON)); }
static void draw_icon_editor(Window *win, int x, int y, int s) { blit_icon(win, x, y, icon_editor, s, ICON_INK(COL_CHROME_ICON)); }
static void draw_icon_files(Window *win, int x, int y, int s) { blit_icon(win, x, y, icon_files, s, ICON_INK(COL_CHROME_ICON)); }
static void draw_icon_settings(Window *win, int x, int y, int s) { blit_icon(win, x, y, icon_settings, s, ICON_INK(COL_CHROME_ICON)); }

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
static int tb_icon_s(void) { return 32; }

static void draw_kind_icon(Window *win, int kind, int x, int y, int s) {
    switch (kind) {
    case APP_TERM: draw_icon_terminal(win, x, y, s); break;
    case APP_EDIT: draw_icon_editor(win, x, y, s); break;
    case APP_FILES: draw_icon_files(win, x, y, s); break;
    default: draw_icon_settings(win, x, y, s); break;
    }
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
    draw_clock(g_taskbar, SW - 16, 20);
}

static void style_chrome_window(Window *win, uint32_t tint, int radius, int shadow) {
    CompWindowStyle style;
    if (g_settings.glass) {
        comp_style_glass(&style);
        style.glass_tint = tint;
        style.glass_tint_alpha = g_th.glass_alpha;
    } else {
        comp_style_plain(&style);
    }
    style.corner_radius = radius;
    style.shadow = shadow;
    comp_set_window_style(win, &style);
}

static void style_chrome(void) {
    style_chrome_window(g_taskbar, COL_TASKBAR, 0, 0);
    style_chrome_window(g_menu, COL_MENU, 12, 1);
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
    comp_text_render(g_bg, 24, 20, "HydrogenOS Desktop v0", COL_WALLPAPER_TEXT);
    fill_rect(g_bg, 24, 42, 160, 2, COL_ACCENT);
    style_chrome();
    draw_taskbar();
}

/* ---------------- start menu ---------------- */

#define MENU_W 300
#define MENU_H 300
#define MENU_ROW 44

static int menu_x(void) { return 12; }
static int menu_y(void) { return SH - TASKBAR_H - MENU_H - 8; }

static const char *menu_label(int i) {
    switch (i) {
    case 0: return "Terminal";
    case 1: return "Editor";
    case 2: return "Files";
    case 3: return "Settings";
    default: return "About HydrogenOS";
    }
}

static void draw_menu(void) {
    Window *w = g_menu;
    fill_rect(w, 0, 0, MENU_W, MENU_H, chrome_clear(COL_MENU));
    comp_text_render(w, 16, 10, "Applications", COL_CHROME_DIM);
    for (int i = 0; i < 5; ++i) {
        int y = 34 + i * MENU_ROW;
        if (i < 4)
            draw_kind_icon(w, i, 16, y + 4, 32);
        else
            draw_icon_os(w, 16, y + 4, 32);
        comp_text_render(w, 60, y + 12, menu_label(i), COL_CHROME_TEXT);
    }
}

static void open_menu(void) {
    g_menu_open = 1;
    g_menu->visible = 1;
    comp_raise(g_menu);
    draw_menu();
    comp_damage_rect(g_bg, g_menu->x, g_menu->y, g_menu->w, g_menu->h);
}

static void close_menu(void) {
    if (!g_menu_open) return;
    g_menu_open = 0;
    g_menu->visible = 0;
    comp_damage_rect(g_bg, g_menu->x, g_menu->y, g_menu->w, g_menu->h);
}

/* ---------------- terminal app ---------------- */

static void term_puts(App *a, const char *text) {
    if (!text) return;
    char line[128];
    int li = 0, i = 0;
    for (;;) {
        char c = text[i++];
        if (c == '\n' || c == 0) {
            line[li] = 0;
            if (a->scroll_count < 96) {
                int k = 0;
                while (line[k] && k < 127) { a->scroll[a->scroll_count][k] = line[k]; ++k; }
                a->scroll[a->scroll_count][k] = 0;
                a->scroll_count++;
            } else {
                for (int r = 1; r < 96; ++r) {
                    int k = 0;
                    while (a->scroll[r][k] && k < 127) { a->scroll[r-1][k] = a->scroll[r][k]; ++k; }
                    a->scroll[r-1][k] = 0;
                }
                int k = 0;
                while (line[k] && k < 127) { a->scroll[95][k] = line[k]; ++k; }
                a->scroll[95][k] = 0;
            }
            li = 0;
            if (c == 0) break;
            continue;
        }
        if (li < 127) line[li++] = c;
    }
}

static void editor_load_path(App *e, const char *path);

static int app_open_new(int kind);

static void term_execute(App *a) {
    char cmd[256];
    int i = 0;
    while (a->input[i] && i < 255) { cmd[i] = a->input[i]; ++i; }
    cmd[i] = 0;
    char *p = cmd;
    while (*p == ' ') ++p;
    char *arg = p;
    while (*arg && *arg != ' ') ++arg;
    if (*arg) { *arg = 0; arg++; while (*arg == ' ') ++arg; } else arg = (char *)"";

    if (!cmd[0]) return;
    if (str_eq(cmd, "help")) {
        term_puts(a, "Commands: help version uptime time date ls [path] cat <path> pwd cd <path>");
        term_puts(a, "  mkdir rm touch mv cp echo clear run <path> edit [path] files");
    } else if (str_eq(cmd, "version")) {
        term_puts(a, "HydrogenOS desktop v0");
    } else if (str_eq(cmd, "uptime")) {
        char line[48]; int z = 0;
        const char *pre = "uptime: ";
        while (pre[z]) { line[z] = pre[z]; ++z; }
        append_num(line, &z, (int)(time_ms() / 1000));
        line[z++] = 's'; line[z] = 0;
        term_puts(a, line);
    } else if (str_eq(cmd, "time")) {
        char line[48]; int z = 0;
        const char *pre = "time_ms: ";
        while (pre[z]) { line[z] = pre[z]; ++z; }
        append_num(line, &z, (int)time_ms());
        line[z] = 0;
        term_puts(a, line);
    } else if (str_eq(cmd, "date")) {
        char line[48];
        CivilTime now;
        if (wallclock_now(&now, 0, 0) == 0 &&
            time_format(line, sizeof(line), TIME_FORMAT_LONG, &now) > 0)
            term_puts(a, line);
        else
            term_puts(a, "date: wall clock unavailable");
    } else if (str_eq(cmd, "pwd")) {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd))) term_puts(a, cwd); else term_puts(a, "pwd: failed");
    } else if (str_eq(cmd, "cd")) {
        if (chdir(arg) != 0) term_puts(a, "cd: no such directory");
    } else if (str_eq(cmd, "clear")) {
        a->scroll_count = 0;
    } else if (str_eq(cmd, "ls")) {
        const char *target = (arg && *arg) ? arg : ".";
        int pos = 0, any = 0;
        dirent_t de;
        while (readdir_pos(target, pos, &de) == 1) { term_puts(a, de.d_name); any = 1; ++pos; }
        if (!any) term_puts(a, "ls: empty");
    } else if (str_eq(cmd, "cat")) {
        if (!*arg) { term_puts(a, "usage: cat <path>"); return; }
        int fd = (int)open(arg, 0);
        if (fd < 0) { term_puts(a, "cat: open failed"); return; }
        char buf[256];
        long n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n < 0) { term_puts(a, "cat: read failed"); return; }
        buf[n] = 0;
        term_puts(a, buf);
    } else if (str_eq(cmd, "mkdir")) {
        if (!*arg) { term_puts(a, "usage: mkdir <path>"); return; }
        term_puts(a, sys_mkdir(arg) == 0 ? "mkdir: ok" : "mkdir: failed");
    } else if (str_eq(cmd, "rm")) {
        if (!*arg) { term_puts(a, "usage: rm <path>"); return; }
        term_puts(a, sys_unlink(arg) == 0 ? "rm: ok" : "rm: failed");
    } else if (str_eq(cmd, "touch")) {
        if (!*arg) { term_puts(a, "usage: touch <path>"); return; }
        term_puts(a, sys_writef(arg, "", 0) == 0 ? "touch: ok" : "touch: failed");
    } else if (str_eq(cmd, "mv")) {
        char *dst = arg;
        while (*dst && *dst != ' ') ++dst;
        if (!*dst) { term_puts(a, "usage: mv <src> <dst>"); return; }
        *dst++ = 0; while (*dst == ' ') ++dst;
        term_puts(a, sys_rename(arg, dst) == 0 ? "mv: ok" : "mv: failed");
    } else if (str_eq(cmd, "cp")) {
        char *dst = arg;
        while (*dst && *dst != ' ') ++dst;
        if (!*dst) { term_puts(a, "usage: cp <src> <dst>"); return; }
        *dst++ = 0; while (*dst == ' ') ++dst;
        int fd = (int)open(arg, 0);
        if (fd < 0) { term_puts(a, "cp: open failed"); return; }
        static char data[4096];
        long n = read(fd, data, sizeof(data));
        close(fd);
        if (n < 0) { term_puts(a, "cp: read failed"); return; }
        term_puts(a, sys_writef(dst, data, (unsigned long)n) == 0 ? "cp: ok" : "cp: failed");
    } else if (str_eq(cmd, "echo")) {
        term_puts(a, arg);
    } else if (str_eq(cmd, "edit")) {
        int idx = app_open_new(APP_EDIT);
        if (idx >= 0 && *arg) editor_load_path(&g_apps[idx], arg);
    } else if (str_eq(cmd, "files")) {
        app_open_new(APP_FILES);
    } else if (str_eq(cmd, "run")) {
        if (!*arg) { term_puts(a, "usage: run <path>"); return; }
        long pid = spawn(arg);
        if (pid < 0) { term_puts(a, "run: spawn failed"); return; }
        int st = 0;
        wait_pid((int)pid, &st);
        char line[48]; int z = 0;
        const char *pre = "run: status ";
        while (pre[z]) { line[z] = pre[z]; ++z; }
        append_num(line, &z, st);
        line[z] = 0;
        term_puts(a, line);
    } else if (str_eq(cmd, "df")) {
        sysinfo_t si;
        if (sys_sysinfo(&si) != 0) { term_puts(a, "df: unavailable"); return; }
        char line[128]; int z = 0;
        if (si.fs_type == 1) { const char *p2 = "ext2 on /: "; while (*p2) line[z++] = *p2++; }
        else { const char *p2 = "ramfs on /: "; while (*p2) line[z++] = *p2++; }
        unsigned long total_mb = (unsigned long)si.total_blocks * si.block_size / (1024UL * 1024UL);
        unsigned long free_mb = (unsigned long)si.free_blocks * si.block_size / (1024UL * 1024UL);
        const char *t = "total "; while (*t) line[z++] = *t++;
        append_num(line, &z, (int)total_mb);
        const char *m = " MB, free "; while (*m) line[z++] = *m++;
        append_num(line, &z, (int)free_mb);
        const char *e = " MB"; while (*e) line[z++] = *e;
        line[z] = 0;
        term_puts(a, line);
    } else if (str_eq(cmd, "free")) {
        sysinfo_t si;
        if (sys_sysinfo(&si) != 0) { term_puts(a, "free: unavailable"); return; }
        unsigned long free_mb = (unsigned long)si.free_pages * si.page_size / (1024UL * 1024UL);
        unsigned long total_mb = (unsigned long)si.total_pages * si.page_size / (1024UL * 1024UL);
        char line[96]; int z = 0;
        const char *p2 = "free: "; while (*p2) line[z++] = *p2++;
        append_num(line, &z, (int)free_mb);
        const char *m = " MB of "; while (*m) line[z++] = *m++;
        append_num(line, &z, (int)total_mb);
        const char *e = " MB"; while (*e) line[z++] = *e;
        line[z] = 0;
        term_puts(a, line);
    } else if (str_eq(cmd, "mount")) {
        sysinfo_t si;
        if (sys_sysinfo(&si) != 0) { term_puts(a, "mount: unavailable"); return; }
        if (si.fs_type == 1) term_puts(a, "ext2 on / (disk, rw)");
        else term_puts(a, "ramfs on / (memory, fallback)");
    } else if (str_eq(cmd, "write")) {
        char *path = arg;
        char *text = path;
        while (*text && *text != ' ') ++text;
        if (!*text) { term_puts(a, "usage: write <path> <text>"); return; }
        *text++ = 0;
        unsigned long n = strlen(text);
        term_puts(a, sys_writef(path, text, n) == 0 ? "write: ok" : "write: failed");
    } else {
        term_puts(a, "unknown command; try 'help'");
    }
}

static int term_key(App *a, char c) {
    if (c == '\r' || c == '\n') {
        a->input[a->input_len] = 0;
        if (a->input_len > 0) term_puts(a, a->input);
        term_execute(a);
        a->input_len = 0;
        a->input[0] = 0;
        return 1;
    }
    if (c == '\b' || c == 0x7F) {
        if (a->input_len > 0) a->input[--a->input_len] = 0;
        return 0;
    }
    if (c < 0x20 || c > 0x7E) return 0;
    if (a->input_len < 255) {
        a->input[a->input_len++] = c;
        a->input[a->input_len] = 0;
    }
    return 0;
}

/* ---------------- editor app ---------------- */

static void editor_save(App *a) {
    a->text[a->text_len] = 0;
    const char *path = a->path[0] ? a->path : "/notes.txt";
    if (sys_writef(path, a->text, (unsigned long)a->text_len) == 0) {
        /* status in the first terminal */
        for (int i = 0; i < MAX_APPS; ++i) {
            if (g_apps[i].used && g_apps[i].kind == APP_TERM) {
                term_puts(&g_apps[i], "editor: saved");
                if (!g_apps[i].minimized && g_apps[i].win && g_apps[i].win->visible) {
                    draw_terminal(&g_apps[i]);
                    comp_damage_rect(g_bg, g_apps[i].win->x, g_apps[i].win->y,
                                     g_apps[i].win->w, g_apps[i].win->h);
                }
                break;
            }
        }
    }
}

static void editor_load_path(App *e, const char *path) {
    str_copy_to(e->path, path, sizeof(e->path));
    int fd = (int)open(path, 0);
    if (fd < 0) return;
    long n = read(fd, e->text, sizeof(e->text) - 1);
    close(fd);
    if (n < 0) return;
    e->text_len = (int)n;
    e->text[e->text_len] = 0;
    e->cursor = e->text_len;
}

static void editor_key(App *a, char c) {
    if (c == 0x13) { editor_save(a); return; }
    if (c == 0x0F) { editor_load_path(a, a->path[0] ? a->path : "/notes.txt"); return; }
    if (c == 0x0E) { a->text_len = 0; a->cursor = 0; a->text[0] = 0; return; }
    if (c == '\r' || c == '\n') {
        if (a->text_len < (int)sizeof(a->text) - 2) {
            for (int i = a->text_len; i > a->cursor; --i) a->text[i] = a->text[i-1];
            a->text[a->cursor] = '\n';
            a->text_len++; a->cursor++;
        }
        return;
    }
    if (c == '\b' || c == 0x7F) {
        if (a->cursor > 0) {
            for (int i = a->cursor - 1; i < a->text_len - 1; ++i) a->text[i] = a->text[i+1];
            a->text_len--; a->cursor--;
        }
        return;
    }
    if (c == '\t') c = ' ';
    if (c < 0x20 || c > 0x7E) return;
    if (a->text_len < (int)sizeof(a->text) - 2) {
        for (int i = a->text_len; i > a->cursor; --i) a->text[i] = a->text[i-1];
        a->text[a->cursor] = c;
        a->text_len++; a->cursor++;
    }
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
}

static void draw_terminal(App *a) {
    Window *w = a->win;
    fill_rect(w, 0, TITLE_H, w->w, w->h - TITLE_H, COL_WIN);
    draw_titlebar(a);
    int cols = (w->w - 2 * PAD) / 8; if (cols < 1) cols = 1;
    int rows = (w->h - TITLE_H - PAD) / LINE_H; if (rows < 1) rows = 1;

    /* Prompt pinned to the top; command output flows below it. */
    int prompt_y = TITLE_H + PAD / 2;
    fill_rect(w, 0, prompt_y, w->w, LINE_H, COL_WIN);
    comp_text_render(w, PAD, prompt_y, "hydrogen$ ", COL_PROMPT);
    char in[256];
    int ik = 0;
    for (int i = 0; i < a->input_len && ik < cols - 10 && ik < 255; ++i) in[ik++] = a->input[i];
    in[ik] = 0;
    comp_text_render(w, PAD + 10 * 8, prompt_y, in, COL_INPUT);
    int cx = PAD + (10 + a->input_len) * 8;
    if (cx < w->w - 4) fill_rect(w, cx, prompt_y, 2, LINE_H, COL_INPUT);

    int out_rows = rows - 1;
    int first = a->scroll_count - out_rows - a->scroll_off;
    if (first < 0) first = 0;
    int row = 0;
    for (int i = first; i < a->scroll_count && row < out_rows; ++i, ++row) {
        char tmp[128]; int k = 0;
        while (a->scroll[i][k] && k < cols && k < 127) { tmp[k] = a->scroll[i][k]; ++k; }
        tmp[k] = 0;
        comp_text_render(w, PAD, TITLE_H + PAD / 2 + (row + 1) * LINE_H, tmp, COL_TERM_TEXT);
    }
}

static void editor_cursor_line_col(App *a, int *out_line, int *out_col) {
    int line = 0, col = 0;
    for (int i = 0; i < a->cursor && i < a->text_len; ++i) {
        if (a->text[i] == '\n') { line++; col = 0; } else col++;
    }
    *out_line = line; *out_col = col;
}

static void draw_editor(App *a) {
    Window *w = a->win;
    fill_rect(w, 0, TITLE_H, w->w, w->h - TITLE_H, COL_EDIT_BG);
    draw_titlebar(a);
    int cols = (w->w - 2 * PAD) / 8; if (cols < 1) cols = 1;
    int rows = (w->h - TITLE_H - PAD) / LINE_H;
    int line = 0, start = 0, draw_row = 0;
    for (int i = 0; i <= a->text_len && draw_row < rows; ++i) {
        if (i == a->text_len || a->text[i] == '\n') {
            if (line >= a->scroll_line) {
                char tmp[160]; int k = 0, j = start;
                while (j < i && k < cols && k < 159) tmp[k++] = a->text[j++];
                tmp[k] = 0;
                comp_text_render(w, PAD, TITLE_H + PAD / 2 + draw_row * LINE_H, tmp, COL_EDIT_FG);
                draw_row++;
            }
            start = i + 1; line++;
        }
    }
    editor_cursor_line_col(a, &line, &cols);
    line -= a->scroll_line;
    if (line >= 0 && line < rows) {
        int cx = PAD + cols * 8;
        int cy = TITLE_H + PAD / 2 + line * LINE_H;
        fill_rect(w, cx, cy, 2, LINE_H, COL_EDIT_FG);
    }
}

static void draw_files(App *a) {
    Window *w = a->win;
    fill_rect(w, 0, TITLE_H, w->w, w->h - TITLE_H, COL_EDIT_BG);
    draw_titlebar(a);
    comp_text_render(w, PAD, TITLE_H + 6, a->cwd, COL_EDIT_FG);
    int row = 0;
    int y0 = TITLE_H + 28;
    int show_dotdot = !str_eq(a->cwd, "/");
    if (show_dotdot) {
        fill_rect(w, 0, y0 + row * 20, w->w, 20, a->sel == -2 ? COL_SELECTION : COL_EDIT_BG);
        comp_text_render(w, PAD, y0 + row * 20 + 2, "../", COL_EDIT_FG);
        row++;
    }
    int pos = 0, idx = 0;
    dirent_t de;
    while (readdir_pos(a->cwd, pos, &de) == 1) {
        uint32_t bgc = (a->sel == idx) ? COL_SELECTION : COL_EDIT_BG;
        fill_rect(w, 0, y0 + row * 20, w->w, 20, bgc);
        char label[300];
        int k = 0;
        if (de.d_type == 4) { label[k++] = '['; }
        for (int i = 0; de.d_name[i] && k < 250; ++i) label[k++] = de.d_name[i];
        if (de.d_type == 4) label[k++] = ']';
        label[k] = 0;
        comp_text_render(w, PAD, y0 + row * 20 + 2, label, COL_EDIT_FG);
        row++; idx++; pos++;
        if (y0 + row * 20 > w->h - 24) break;
    }
    if (idx == 0 && !show_dotdot)
        comp_text_render(w, PAD, y0 + 2, "(empty)", COL_DIM);
}

static void draw_settings(App *a) {
    a->settings_ui.save_failed = g_settings_save_failed;
    settings_ui_draw(&a->settings_ui, a->win);
    draw_titlebar(a);
}

static void draw_app_content(App *a) {
    switch (a->kind) {
    case APP_TERM: draw_terminal(a); break;
    case APP_EDIT: draw_editor(a); break;
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

static void refresh_titlebars(void) {
    for (int i = 0; i < MAX_APPS; ++i) {
        App *o = &g_apps[i];
        if (o->used && o->win && o->win->visible && !o->minimized) {
            draw_titlebar(o);
            comp_damage_rect(g_bg, o->win->x, o->win->y, o->win->w, TITLE_H);
        }
    }
}

/* Puts the window on top of the other app windows. The taskbar stays above
   them all, so a window's shadow never darkens it. */
static void raise_app_window(App *a) {
    a->focus_stamp = ++g_focus_clock;
    comp_raise(a->win);
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

/* The window under the pointer: the topmost visible one, taskbar excluded. */
static App *app_at(int mx, int my) {
    App *hit = 0;
    if (my >= SH - TASKBAR_H) return 0;
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
    refresh_titlebars();
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
    refresh_titlebars();
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
    if (a->win) {
        comp_destroy_window(a->win);
        a->win = 0;
    }
    a->used = 0;
    a->minimized = 0;
    a->dragging = 0;
    a->resizing = 0;
    if (g_click_app == idx) g_click_app = -1;
    if (g_focus == idx) g_focus = topmost_app();
    refresh_titlebars();
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
    case APP_EDIT:
    case APP_FILES: return COL_EDIT_BG;
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
    if (kind == APP_FILES) { w = 620; h = 460; }
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

    const char *t = "Terminal";
    if (kind == APP_EDIT) t = "Editor";
    else if (kind == APP_FILES) t = "Files";
    else if (kind == APP_SETTINGS) t = "Settings";
    str_copy_to(a->title, t, sizeof(a->title));

    if (kind == APP_TERM) {
        term_puts(a, "HydrogenOS shell app");
        term_puts(a, "Type 'help'. Drag the title bar to move.");
    } else if (kind == APP_EDIT) {
        a->path[0] = 0;
    } else if (kind == APP_FILES) {
        getcwd(a->cwd, sizeof(a->cwd));
        a->sel = 0;
    } else if (kind == APP_SETTINGS) {
        settings_ui_init(&a->settings_ui, &g_settings, &g_th, draw_icon_os, TITLE_H);
    }

    draw_app_content(a);
    bring_to_front(a, idx);
    refresh_taskbar();
    return idx;
}

/* ---------------- settings ---------------- */

static void redraw_session(void) {
    draw_background();
    draw_menu();
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

/* ---------------- hit testing ---------------- */

static void handle_menu_click(int mx, int my) {
    (void)mx;
    int rel = my - menu_y() - 34;
    int idx = rel / MENU_ROW;
    if (idx >= 0 && idx < 4) {
        close_menu();
        app_open_new(idx);
    } else if (idx == 4) {
        close_menu();
        int opened = app_open_new(APP_SETTINGS);
        if (opened >= 0) {
            settings_ui_select(&g_apps[opened].settings_ui, SETTINGS_SECTION_ABOUT);
            draw_app_content(&g_apps[opened]);
        }
    } else {
        close_menu();
    }
}

static void handle_press(int mx, int my) {
    if (g_menu_open) {
        if (point_in_rect(mx, my, menu_x(), menu_y(), MENU_W, MENU_H))
            handle_menu_click(mx, my);
        else
            close_menu();
        return;
    }

    int ty = SH - TASKBAR_H;
    if (my >= ty) {
        if (point_in_rect(mx, my, tb_logo_x(), tb_logo_y(), 32, 32)) {
            if (g_menu_open) close_menu(); else open_menu();
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
            if (dx * dx + dy * dy <= 36) { app_close(a); return; }
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
            int y0 = TITLE_H + 28;
            int row = (ly - y0) / 20;
            int show_dotdot = !str_eq(a->cwd, "/");
            int sel_idx = show_dotdot ? row - 1 : row;
            if (row >= 0) {
                a->sel = sel_idx;
                if (show_dotdot && row == 0) {
                    chdir("..");
                    getcwd(a->cwd, sizeof(a->cwd));
                    a->sel = 0;
                } else {
                    int pos = 0, idx = 0;
                    dirent_t de;
                    while (readdir_pos(a->cwd, pos, &de) == 1) {
                        if (idx == sel_idx) {
                            if (de.d_type == 4) {
                                chdir(de.d_name);
                                getcwd(a->cwd, sizeof(a->cwd));
                                a->sel = 0;
                            } else {
                                int e = app_open_new(APP_EDIT);
                                if (e >= 0) {
                                    char full[256];
                                    int k = 0;
                                    if (str_eq(a->cwd, "/")) full[k++] = '/';
                                    else { str_copy_to(full, a->cwd, sizeof(full)); k = 0; while (full[k]) k++; full[k++] = '/'; }
                                    str_copy_to(full + k, de.d_name, (int)sizeof(full) - k);
                                    editor_load_path(&g_apps[e], full);
                                    draw_app_content(&g_apps[e]);
                                }
                            }
                            break;
                        }
                        idx++; pos++;
                    }
                }
                draw_app_content(a);
                comp_damage_rect(g_bg, w->x, w->y, w->w, w->h);
                return;
            }
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
            wm_clamp_position(&work, w->w, TITLE_H, &nx, &ny);
            if (nx == w->x && ny == w->y) continue;
            /* The compositor repaints where the window was and where it is,
               shadow included. */
            w->x = nx; w->y = ny;
        } else if (a->resizing) {
            int nw = mx - w->x;
            int nh = my - w->y;
            int min_w = a->kind == APP_SETTINGS ? SETTINGS_UI_MIN_WIDTH : 320;
            int min_h = a->kind == APP_SETTINGS ? SETTINGS_UI_MIN_HEIGHT : 200;
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

/* Hover highlights follow the pointer over the topmost window only. */
static void settings_track_pointer(int mx, int my) {
    App *top = app_at(mx, my);
    int under = top ? app_index_of(top) : -1;

    if (g_menu_open && point_in_rect(mx, my, menu_x(), menu_y(), MENU_W, MENU_H)) under = -1;

    for (int i = 0; i < MAX_APPS; ++i) {
        App *a = &g_apps[i];
        if (!a->used || a->kind != APP_SETTINGS || a->minimized || !a->win) continue;
        int lx = i == under ? mx - a->win->x : -1;
        int ly = i == under ? my - a->win->y : -1;
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

    g_taskbar = comp_create_window(0, SH - TASKBAR_H, SW, TASKBAR_H);
    if (!g_taskbar || !g_taskbar->surface) return 1;

    g_menu = comp_create_window(menu_x(), menu_y(), MENU_W, MENU_H);
    if (!g_menu || !g_menu->surface) return 1;
    g_menu->visible = 0;
    style_chrome();
    draw_menu();

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

        for (int guard = 0; guard < 64; ++guard) {
            long k = sys_kbd_read();
            if (k < 0) break;
            if (k == KBD_KEY_ALT_TAB) {
                cycle_focus();
                dirty = 1;
                continue;
            }
            char c = (char)k;
            if (g_focus >= 0 && g_apps[g_focus].used && !g_apps[g_focus].minimized) {
                App *a = &g_apps[g_focus];
                if (a->kind == APP_TERM) {
                    int full = term_key(a, c);
                    Window *w = a->win;
                    if (full) {
                        draw_terminal(a);
                        comp_damage_rect(g_bg, w->x, w->y, w->w, w->h);
                    } else {
                        draw_terminal(a);
                        int py = TITLE_H + PAD / 2;
                        comp_damage_rect(g_bg, w->x, w->y + py, w->w, LINE_H);
                    }
                    dirty = 1;
                } else if (a->kind == APP_EDIT) {
                    editor_key(a, c);
                    draw_editor(a);
                    comp_damage_rect(g_bg, a->win->x, a->win->y, a->win->w, a->win->h);
                    dirty = 1;
                }
            }
        }

        int m[4] = { 0, 0, 0, 0 };
        if (sys_mouse_state(m) == 0) {
            int nmx = m[0], nmy = m[1], nbtn = m[2] & 7;
            if (nmx != g_mx || nmy != g_my) {
                g_mx = nmx;
                g_my = nmy;
                dirty = 1;
                settings_track_pointer(g_mx, g_my);
            }
            int wheel = m[3];
            int press = (nbtn & 1) && !(g_btn & 1);
            int release = !(nbtn & 1) && (g_btn & 1);
            g_btn = nbtn;
            if (wheel != 0 && g_focus >= 0 && g_apps[g_focus].used && !g_apps[g_focus].minimized) {
                App *fa = &g_apps[g_focus];
                if (fa->kind == APP_TERM) {
                    fa->scroll_off += wheel;
                    if (fa->scroll_off < 0) fa->scroll_off = 0;
                    if (fa->scroll_off > fa->scroll_count) fa->scroll_off = fa->scroll_count;
                    draw_terminal(fa);
                    comp_damage_rect(g_bg, fa->win->x, fa->win->y, fa->win->w, fa->win->h);
                    dirty = 1;
                } else if (fa->kind == APP_EDIT) {
                    fa->scroll_line -= wheel;
                    if (fa->scroll_line < 0) fa->scroll_line = 0;
                    if (fa->scroll_line > fa->text_len) fa->scroll_line = fa->text_len;
                    draw_editor(fa);
                    comp_damage_rect(g_bg, fa->win->x, fa->win->y, fa->win->w, fa->win->h);
                    dirty = 1;
                }
            }
            if (press) { handle_press(g_mx, g_my); dirty = 1; }
            int any_drag = 0;
            for (int i = 0; i < MAX_APPS; ++i) if (g_apps[i].dragging || g_apps[i].resizing) any_drag = 1;
            if (any_drag) { handle_drag(g_mx, g_my); dirty = 1; }
            if (release) { handle_release(); dirty = 1; }
        }

        unsigned long now_sec = time_ms() / 1000;
        if (now_sec != g_last_sec) {
            g_last_sec = now_sec;
            fill_rect(g_taskbar, SW - 16 - CLOCK_W - 2, 16, CLOCK_W + 4, 20, chrome_clear(COL_TASKBAR));
            draw_clock(g_taskbar, SW - 16, 20);
            comp_damage_rect(g_bg, SW - 16 - CLOCK_W - 2, SH - TASKBAR_H + 16, CLOCK_W + 4, 20);
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
