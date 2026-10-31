#include "spotlight.h"

#include "gfx_effects.h"
#include "iconset.h"
#include "kbd_keys.h"
#include "launcher_model.h"
#include "mini_libc.h"
#include "syscall.h"

/* ---- geometry (pixels, panel-local) ---- */

#define PAD             12
#define FIELD_Y         12
#define FIELD_H         36
#define FIELD_RADIUS    10
#define FIELD_TEXT_X    44
#define LIST_Y          56
#define HEADER_H        28
#define ROW_H           40
#define ROW_RADIUS      10
#define ICON_X          (PAD + 8)
#define NAME_X          (ICON_X + ICON_SIZE_APP + 12)
#define POWER_H         64
#define NOTICE_H        28
#define LIST_GAP        8
#define EMPTY_H         96
#define CONFIRM_H       280
#define BUTTON_H        36
#define BUTTON_GAP      8
#define BUTTON_W        ((SPOTLIGHT_WIDTH - 2 * PAD - BUTTON_GAP) / 2)
#define GLYPH_GAP       8
#define CHAR_W          8
#define CHAR_H          16

#define QUERY_MAX       ((SPOTLIGHT_WIDTH - FIELD_TEXT_X - PAD - 2 * CHAR_W) / CHAR_W)

/* ---- model limits ---- */

/* Programs beyond this are not searchable; the panel says so. */
#define MAX_PROGRAMS    256
#define MAX_CHILDREN    16

#define ERROR_NOT_SUPPORTED 38

/* Translucent overlays on the panel, as opacity of the ink colour. */
#define OPACITY_FIELD      28
#define OPACITY_FIELD_EDGE 36
#define OPACITY_BUTTON     22
#define OPACITY_SEPARATOR  30
#define OPACITY_BADGE      40

enum { POWER_NONE = 0, POWER_SHUTDOWN, POWER_RESTART };

static struct {
    Window *window;
    const SpotlightHost *host;
    const SettingsPalette *palette;
    const Settings *settings;
    int open;
    int height;                     /* of the panel; its top edge stays put */

    char query[QUERY_MAX + 1];
    int query_length;

    LauncherResults results;
    int row_y[LAUNCHER_MAX_ITEMS];
    int header_y[LAUNCHER_SECTION_COUNT];   /* -1: section has no header */
    int selected;                   /* items, then the two power buttons */

    int confirm;                    /* POWER_*: confirmation is showing */
    int confirm_accept;             /* focus is on the confirming button */
    char notice[64];

    LauncherEntry programs[MAX_PROGRAMS];
    int program_count;
    int programs_truncated;
    LauncherRecents recents;
    long children[MAX_CHILDREN];
    int child_count;
} g;

/* ---- text helpers ---- */

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static int has_text(const char *s) { return s && s[0]; }

/* A failure to report wins over the standing hint about the program list. */
static const char *panel_notice(void) {
    if (has_text(g.notice)) return g.notice;
    return g.programs_truncated ? "Some programs are not listed." : "";
}

static int button_y(void) {
    return g.height - POWER_H + (POWER_H - BUTTON_H) / 2;
}

/* ---- programs on disk ---- */

/* The power commands are not programs to start from a search result: they
   would end the session unasked. System services live in /sbin, which is not
   scanned. */
static int is_system_program(const char *name) {
    static const char *const hidden[] = { "poweroff", "reboot" };
    for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i)
        if (strcmp(name, hidden[i]) == 0) return 1;
    return 0;
}

static int is_executable(const char *path) {
    unsigned char magic[4];
    long fd = open(path, OPEN_READ);
    if (fd < 0) return 0;
    long n = read((int)fd, magic, sizeof(magic));
    close((int)fd);
    return n == 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static int program_known(const char *name) {
    for (int i = 0; i < g.program_count; ++i)
        if (strcmp(g.programs[i].name, name) == 0) return 1;
    return 0;
}

/* Stops at the first program that does not fit and records that the list is
   incomplete. */
static void scan_directory(const char *dir) {
    dirent_t entry;
    for (int pos = 0; readdir_pos(dir, pos, &entry) == 1; ++pos) {
        if (entry.d_type == 4 || strlen(entry.d_name) >= LAUNCHER_NAME_CAP) continue;
        if (is_system_program(entry.d_name) || program_known(entry.d_name)) continue;

        LauncherEntry program;
        program.app_id = LAUNCHER_ENTRY_PROGRAM;
        copy_text(program.name, sizeof(program.name), entry.d_name);
        if (snprintf(program.path, sizeof(program.path), "%s/%s", dir, entry.d_name) >=
            (int)sizeof(program.path))
            continue;
        if (!is_executable(program.path)) continue;
        if (g.program_count == MAX_PROGRAMS) {
            g.programs_truncated = 1;
            return;
        }
        g.programs[g.program_count++] = program;
    }
}

/* Directories in lookup order: the first one defining a name wins. */
static void scan_programs(void) {
    g.program_count = 0;
    g.programs_truncated = 0;
    scan_directory("/bin");
    if (!g.programs_truncated) scan_directory("/usr/bin");
}

static void program_directory(const LauncherEntry *program, char *out, size_t cap) {
    size_t length = strlen(program->path) - strlen(program->name);
    if (length > 1) --length;   /* drop the separator, but keep a lone "/" */
    if (length >= cap) length = cap - 1;
    memcpy(out, program->path, length);
    out[length] = '\0';
}

/* ---- search ---- */

/* Fills the item list for the current query and assigns each item its row. */
static void rebuild_items(void) {
    if (g.query_length > 0)
        launcher_search(g.query, g.host->apps, g.host->app_count, g.programs, g.program_count, &g.results);
    else
        launcher_suggest(g.host->apps, g.host->app_count, &g.recents, &g.results);

    int y = LIST_Y;
    int previous = -1;
    for (int s = 0; s < LAUNCHER_SECTION_COUNT; ++s) g.header_y[s] = -1;
    for (int i = 0; i < g.results.count; ++i) {
        int section = g.results.items[i].section;
        if (section != previous) {
            g.header_y[section] = y;
            y += HEADER_H;
            previous = section;
        }
        g.row_y[i] = y;
        y += ROW_H;
    }
}

static const char *section_title(int section) {
    if (g.query_length > 0) return section == LAUNCHER_SECTION_APPS ? "Applications" : "Programs";
    return section == LAUNCHER_SECTION_APPS ? "Pinned" : "Recent";
}

/* ---- drawing ---- */

static GfxCanvas canvas(void) {
    GfxCanvas c = { (uint32_t *)g.window->surface, g.window->w, g.window->h, g.window->w,
                    g.window->style.material == COMP_MATERIAL_GLASS };
    return c;
}

static void fill_round(int x, int y, int w, int h, int radius, uint32_t rgb, unsigned int opacity) {
    GfxCanvas c = canvas();
    gfx_fill_round_rect(&c, x, y, w, h, radius, rgb, opacity);
}

static void stroke_round(int x, int y, int w, int h, int radius, uint32_t rgb, unsigned int opacity) {
    GfxCanvas c = canvas();
    gfx_stroke_round_rect(&c, x, y, w, h, radius, rgb, opacity);
}

static void draw_text(int x, int y, const char *text, uint32_t rgb) {
    comp_text_render(g.window, x, y, text, rgb);
}

/* Draws text that is cut with an ellipsis when it exceeds max_chars. */
static void draw_text_fit(int x, int y, const char *text, int max_chars, uint32_t rgb) {
    char line[LAUNCHER_NAME_CAP + LAUNCHER_PATH_CAP];
    if (max_chars <= 0) return;
    if ((int)strlen(text) <= max_chars) {
        draw_text(x, y, text, rgb);
        return;
    }
    int keep = max_chars > 3 ? max_chars - 3 : max_chars;
    memcpy(line, text, (size_t)keep);
    strcpy(line + keep, max_chars > 3 ? "..." : "");
    draw_text(x, y, line, rgb);
}

static void draw_text_centered(int center_x, int y, const char *text, uint32_t rgb) {
    draw_text(center_x - (int)strlen(text) * CHAR_W / 2, y, text, rgb);
}

static void draw_glyph(IconId id, int x, int y, int size, uint32_t rgb) {
    GfxCanvas c = canvas();
    icon_draw_glyph(&c, id, size, x, y, rgb, 255);
}

static IconId power_glyph(int action) {
    return action == POWER_RESTART ? ICON_GLYPH_RESTART : ICON_GLYPH_POWER;
}

static void draw_program_badge(int x, int y, int size, const char *name, uint32_t ink, unsigned int opacity) {
    char letter[2] = { name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0], '\0' };
    fill_round(x, y, size, size, size / 4, ink, opacity);
    draw_text(x + (size - CHAR_W) / 2, y + (size - CHAR_H) / 2, letter, ink);
}

static void draw_item(int index) {
    const SettingsPalette *p = g.palette;
    const LauncherItem *item = &g.results.items[index];
    int row_y = g.row_y[index];
    int selected = index == g.selected;
    int row_w = SPOTLIGHT_WIDTH - 2 * PAD;
    uint32_t ink = selected ? p->on_accent : p->chrome_text;
    uint32_t dim = selected ? gfx_mix(p->accent, p->on_accent, 170) : p->chrome_dim;
    char hint[LAUNCHER_PATH_CAP];

    if (selected) fill_round(PAD, row_y, row_w, ROW_H, ROW_RADIUS, p->accent, 255);

    int icon_y = row_y + (ROW_H - ICON_SIZE_APP) / 2;
    if (item->entry.app_id == LAUNCHER_ENTRY_PROGRAM) {
        draw_program_badge(ICON_X, icon_y, ICON_SIZE_APP, item->entry.name, ink, OPACITY_BADGE);
        program_directory(&item->entry, hint, sizeof(hint));
        if (strcmp(hint, "/") == 0) copy_text(hint, sizeof(hint), "Program");
    } else {
        g.host->draw_icon(g.window, item->entry.app_id, ICON_X, icon_y, ICON_SIZE_APP);
        copy_text(hint, sizeof(hint), "Application");
    }

    int hint_chars = (int)strlen(hint);
    int hint_x = PAD + row_w - 12 - hint_chars * CHAR_W;
    int text_y = row_y + (ROW_H - CHAR_H) / 2;
    draw_text(hint_x, text_y, hint, dim);
    draw_text_fit(NAME_X, text_y, item->entry.name, (hint_x - 12 - NAME_X) / CHAR_W, ink);
}

static void draw_field(void) {
    const SettingsPalette *p = g.palette;
    int width = SPOTLIGHT_WIDTH - 2 * PAD;

    fill_round(PAD, FIELD_Y, width, FIELD_H, FIELD_RADIUS, p->chrome_text, OPACITY_FIELD);
    stroke_round(PAD, FIELD_Y, width, FIELD_H, FIELD_RADIUS, p->chrome_text, OPACITY_FIELD_EDGE);
    draw_glyph(ICON_GLYPH_SEARCH, PAD + 12, FIELD_Y + (FIELD_H - ICON_SIZE_GLYPH) / 2, ICON_SIZE_GLYPH, p->chrome_dim);

    int text_y = FIELD_Y + (FIELD_H - CHAR_H) / 2;
    if (g.query_length > 0) {
        draw_text(FIELD_TEXT_X, text_y, g.query, p->chrome_text);
        fill_round(FIELD_TEXT_X + g.query_length * CHAR_W + 1, text_y, 2, CHAR_H, 1, p->accent, 255);
    } else {
        fill_round(FIELD_TEXT_X, text_y, 2, CHAR_H, 1, p->accent, 255);
        draw_text(FIELD_TEXT_X + 8, text_y, "Search apps and programs", p->chrome_dim);
    }
}

static void draw_no_results(void) {
    const SettingsPalette *p = g.palette;
    int center = SPOTLIGHT_WIDTH / 2;
    char detail[QUERY_MAX + 16];

    draw_text_centered(center, LIST_Y + 26, "No results", p->chrome_text);
    snprintf(detail, sizeof(detail), "for \"%s\"", g.query);
    draw_text_fit(center - (int)strlen(detail) * CHAR_W / 2, LIST_Y + 48, detail, QUERY_MAX + 8, p->chrome_dim);
}

static int button_x(int which) {
    return PAD + (which == POWER_RESTART ? BUTTON_W + BUTTON_GAP : 0);
}

static const char *power_label(int action) {
    return action == POWER_RESTART ? "Restart" : "Shut Down";
}

static void draw_power_button(int action, int selected) {
    const SettingsPalette *p = g.palette;
    int x = button_x(action);
    const char *label = power_label(action);
    uint32_t ink = selected ? p->on_accent : p->chrome_text;

    if (selected) fill_round(x, button_y(), BUTTON_W, BUTTON_H, ROW_RADIUS, p->accent, 255);
    else fill_round(x, button_y(), BUTTON_W, BUTTON_H, ROW_RADIUS, p->chrome_text, OPACITY_BUTTON);

    int content = ICON_SIZE_GLYPH + GLYPH_GAP + (int)strlen(label) * CHAR_W;
    int left = x + (BUTTON_W - content) / 2;
    draw_glyph(power_glyph(action), left, button_y() + (BUTTON_H - ICON_SIZE_GLYPH) / 2, ICON_SIZE_GLYPH, ink);
    draw_text(left + ICON_SIZE_GLYPH + GLYPH_GAP, button_y() + (BUTTON_H - CHAR_H) / 2, label, ink);
}

static void draw_power_section(void) {
    int separator_y = g.height - POWER_H;
    fill_round(PAD, separator_y, SPOTLIGHT_WIDTH - 2 * PAD, 1, 0, g.palette->chrome_text, OPACITY_SEPARATOR);
    draw_power_button(POWER_SHUTDOWN, g.selected == g.results.count);
    draw_power_button(POWER_RESTART, g.selected == g.results.count + 1);
}

/* Confirmation dialog buttons: Cancel on the left, the action on the right. */
#define DIALOG_BUTTON_W  140
#define DIALOG_BUTTON_Y  216

static int dialog_button_x(int accept) {
    int total = 2 * DIALOG_BUTTON_W + BUTTON_GAP;
    return (SPOTLIGHT_WIDTH - total) / 2 + (accept ? DIALOG_BUTTON_W + BUTTON_GAP : 0);
}

static void draw_dialog_button(int accept) {
    const SettingsPalette *p = g.palette;
    int focused = accept == g.confirm_accept;
    int x = dialog_button_x(accept);
    const char *label = accept ? power_label(g.confirm) : "Cancel";

    if (accept) fill_round(x, DIALOG_BUTTON_Y, DIALOG_BUTTON_W, BUTTON_H, ROW_RADIUS, p->danger, 255);
    else fill_round(x, DIALOG_BUTTON_Y, DIALOG_BUTTON_W, BUTTON_H, ROW_RADIUS, p->chrome_text, OPACITY_BUTTON);
    if (focused) stroke_round(x - 2, DIALOG_BUTTON_Y - 2, DIALOG_BUTTON_W + 4, BUTTON_H + 4,
                              ROW_RADIUS + 2, p->chrome_text, 200);
    draw_text_centered(x + DIALOG_BUTTON_W / 2, DIALOG_BUTTON_Y + (BUTTON_H - CHAR_H) / 2, label,
                       accept ? 0x00FFFFFFu : p->chrome_text);
}

static void draw_confirmation(void) {
    const SettingsPalette *p = g.palette;
    int center = SPOTLIGHT_WIDTH / 2;
    int badge = 64;
    int badge_x = center - badge / 2;
    int badge_y = 28;

    fill_round(badge_x, badge_y, badge, badge, badge / 2, p->chrome_text, OPACITY_FIELD);
    draw_glyph(power_glyph(g.confirm), center - ICON_SIZE_GLYPH_LARGE / 2, badge_y + (badge - ICON_SIZE_GLYPH_LARGE) / 2,
               ICON_SIZE_GLYPH_LARGE, p->chrome_text);

    draw_text_centered(center, 114, g.confirm == POWER_RESTART ? "Restart this computer?"
                                                              : "Shut down this computer?", p->chrome_text);
    draw_text_centered(center, 142, "Open windows are closed and unsaved", p->chrome_dim);
    draw_text_centered(center, 160, "changes are lost.", p->chrome_dim);
    if (has_text(g.notice)) draw_text_centered(center, 182, g.notice, p->danger);

    draw_dialog_button(0);
    draw_dialog_button(1);
}

/* The panel is as tall as what it shows. It grows downwards from a fixed top
   because the compositor leaves stale pixels behind when a visible glass
   window changes height and position together. */
static int wanted_height(void) {
    if (g.confirm != POWER_NONE) return CONFIRM_H;
    int list_end = g.results.count > 0 ? g.row_y[g.results.count - 1] + ROW_H : LIST_Y + EMPTY_H;
    return list_end + LIST_GAP + (has_text(panel_notice()) ? NOTICE_H : 0) + POWER_H;
}

static void fit_window(void) {
    int height = wanted_height();
    if (height != g.window->h && comp_resize_window(g.window, SPOTLIGHT_WIDTH, height) != 0)
        height = g.window->h;
    g.height = height;
}

static void draw(void) {
    fit_window();
    const SettingsPalette *p = g.palette;
    uint32_t *pixels = (uint32_t *)g.window->surface;
    uint32_t clear = g.settings->glass ? 0x00000000u : p->menu;
    for (int i = 0; i < g.window->w * g.window->h; ++i) pixels[i] = clear;

    if (g.confirm != POWER_NONE) {
        draw_confirmation();
    } else {
        draw_field();
        for (int s = 0; s < LAUNCHER_SECTION_COUNT; ++s)
            if (g.header_y[s] >= 0)
                draw_text(PAD + 8, g.header_y[s] + (HEADER_H - CHAR_H) / 2 + 2, section_title(s), p->chrome_dim);
        for (int i = 0; i < g.results.count; ++i) draw_item(i);
        if (g.results.count == 0) draw_no_results();
        if (has_text(panel_notice()))
            draw_text_centered(SPOTLIGHT_WIDTH / 2, g.height - POWER_H - NOTICE_H + (NOTICE_H - CHAR_H) / 2,
                               panel_notice(), has_text(g.notice) ? p->danger : p->chrome_dim);
        draw_power_section();
    }
    comp_damage_window(g.window);
}

/* ---- actions ---- */

static void set_notice(const char *text) {
    copy_text(g.notice, sizeof(g.notice), text);
}

/* Programs have no window of their own yet, so they run detached with their
   output discarded. */
static int start_program(const LauncherEntry *program) {
    if (g.child_count == MAX_CHILDREN) spotlight_service();
    if (g.child_count == MAX_CHILDREN) {
        set_notice("Too many programs are running.");
        return -1;
    }
    long pid = spawn_with_stdio(program->path, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL);
    if (pid <= 0) {
        char text[sizeof(g.notice)];
        snprintf(text, sizeof(text), "Could not start %s.", program->name);
        set_notice(text);
        return -1;
    }
    g.children[g.child_count++] = pid;
    return 0;
}

static void launch(int index) {
    LauncherEntry entry = g.results.items[index].entry;

    if (entry.app_id == LAUNCHER_ENTRY_PROGRAM && start_program(&entry) != 0) {
        draw();
        return;
    }
    launcher_recents_add(&g.recents, &entry);
    spotlight_close();
    if (entry.app_id != LAUNCHER_ENTRY_PROGRAM) g.host->launch(entry.app_id);
}

static void begin_confirmation(int action) {
    g.confirm = action;
    g.confirm_accept = 0;
    g.notice[0] = '\0';
    draw();
}

static void end_confirmation(void) {
    g.confirm = POWER_NONE;
    g.notice[0] = '\0';
    draw();
}

/* Does not return when the machine goes down. */
static void perform_power(void) {
    long result = g.confirm == POWER_RESTART ? sys_reboot() : sys_shutdown();
    if (result == 0) return;
    if (result == -ERROR_NOT_SUPPORTED)
        set_notice("Not supported on this machine.");
    else
        snprintf(g.notice, sizeof(g.notice), "%s failed (error %ld).", power_label(g.confirm), -result);
    draw();
}

static void select_item(int selected) {
    if (selected == g.selected) return;
    g.selected = selected;
    draw();
}

/* Selection indices: the items, then the two power buttons; -1 is nothing,
   which is what an empty result list starts with so Enter cannot power off. */
static int first_selection(void) {
    return g.results.count > 0 ? 0 : -1;
}

static void query_changed(void) {
    g.query[g.query_length] = '\0';
    g.notice[0] = '\0';
    rebuild_items();
    g.selected = first_selection();
    draw();
}

/* ---- public interface ---- */

void spotlight_init(Window *window, const SpotlightHost *host,
                    const SettingsPalette *palette, const Settings *settings) {
    memset(&g, 0, sizeof(g));
    g.window = window;
    g.height = window->h;
    g.host = host;
    g.palette = palette;
    g.settings = settings;
    window->visible = 0;
}

int spotlight_is_open(void) {
    return g.open;
}

void spotlight_open(void) {
    if (g.open) return;
    g.open = 1;
    g.query[0] = '\0';
    g.query_length = 0;
    g.confirm = POWER_NONE;
    g.notice[0] = '\0';
    scan_programs();
    rebuild_items();
    g.selected = first_selection();
    draw();
    g.window->visible = 1;
    comp_raise(g.window);
}

void spotlight_close(void) {
    if (!g.open) return;
    g.open = 0;
    g.window->visible = 0;
    comp_damage_window(g.window);
}

void spotlight_redraw(void) {
    if (g.open) draw();
}

static int step_selection(int step) {
    int last = g.results.count + 1;
    int next = g.selected + step;
    return next < 0 ? g.selected : next > last ? last : next;
}

static void key_in_confirmation(long key) {
    switch (key) {
    case KBD_KEY_ESCAPE:
        end_confirmation();
        break;
    case '\n':
        if (g.confirm_accept) perform_power(); else end_confirmation();
        break;
    case KBD_KEY_LEFT:
    case KBD_KEY_RIGHT:
    case KBD_KEY_UP:
    case KBD_KEY_DOWN:
    case '\t':
        g.confirm_accept = !g.confirm_accept;
        draw();
        break;
    default:
        break;
    }
}

int spotlight_key(long key) {
    if (!g.open) {
        if (key != KBD_KEY_LAUNCHER) return 0;
        spotlight_open();
        return 1;
    }
    if (key == KBD_KEY_LAUNCHER || (key == KBD_KEY_ESCAPE && g.confirm == POWER_NONE)) {
        spotlight_close();
        return 1;
    }
    if (g.confirm != POWER_NONE) {
        key_in_confirmation(key);
        return 1;
    }

    int power_zone = g.selected >= g.results.count;
    switch (key) {
    case '\n':
        if (g.selected < 0) break;
        if (g.selected < g.results.count) launch(g.selected);
        else begin_confirmation(g.selected == g.results.count ? POWER_SHUTDOWN : POWER_RESTART);
        break;
    case '\b':
    case 0x7F:
        if (g.query_length > 0) {
            --g.query_length;
            query_changed();
        }
        break;
    case KBD_KEY_UP:
        select_item(step_selection(-1));
        break;
    case KBD_KEY_DOWN:
        select_item(step_selection(1));
        break;
    case '\t':
        select_item(g.selected == g.results.count + 1 ? 0 : g.selected + 1);
        break;
    case KBD_KEY_LEFT:
        if (power_zone) select_item(g.results.count);
        break;
    case KBD_KEY_RIGHT:
        if (power_zone) select_item(g.results.count + 1);
        break;
    default:
        if (key >= 0x20 && key <= 0x7E && g.query_length < QUERY_MAX) {
            g.query[g.query_length++] = (char)key;
            query_changed();
        }
        break;
    }
    return 1;
}

static int inside(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}

/* The selection index under a panel-local point, or -1. */
static int target_at(int x, int y) {
    if (g.confirm != POWER_NONE) {
        for (int accept = 0; accept < 2; ++accept)
            if (inside(x, y, dialog_button_x(accept), DIALOG_BUTTON_Y, DIALOG_BUTTON_W, BUTTON_H))
                return accept;
        return -1;
    }
    for (int i = 0; i < g.results.count; ++i)
        if (inside(x, y, PAD, g.row_y[i], SPOTLIGHT_WIDTH - 2 * PAD, ROW_H)) return i;
    for (int action = POWER_SHUTDOWN; action <= POWER_RESTART; ++action)
        if (inside(x, y, button_x(action), button_y(), BUTTON_W, BUTTON_H))
            return g.results.count + action - POWER_SHUTDOWN;
    return -1;
}

int spotlight_press(int x, int y) {
    if (!g.open) return 0;
    if (!inside(x, y, g.window->x, g.window->y, g.window->w, g.window->h)) return 0;

    int target = target_at(x - g.window->x, y - g.window->y);
    if (target < 0) return 1;
    if (g.confirm != POWER_NONE) {
        if (target == 1) perform_power(); else end_confirmation();
    } else if (target < g.results.count) {
        launch(target);
    } else {
        begin_confirmation(target == g.results.count ? POWER_SHUTDOWN : POWER_RESTART);
    }
    return 1;
}

void spotlight_hover(int x, int y) {
    if (!g.open) return;
    int target = target_at(x - g.window->x, y - g.window->y);
    if (target < 0) return;
    if (g.confirm != POWER_NONE) {
        if (g.confirm_accept != target) {
            g.confirm_accept = target;
            draw();
        }
    } else {
        select_item(target);
    }
}

void spotlight_service(void) {
    int kept = 0;
    for (int i = 0; i < g.child_count; ++i) {
        int status;
        if (try_wait_pid((int)g.children[i], &status) == 0)
            g.children[kept++] = g.children[i];
    }
    g.child_count = kept;
}
