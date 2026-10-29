#include "filemanager.h"

#include "mini_libc.h"
#include "widgets.h"

#define FM_MARGIN 8
#define FM_HEADER_H 24
#define FM_CELL_W 84
#define FM_CELL_H 72
#define FM_ICON 32
#define FM_DBLCLICK_MS 500

static const uint32_t FM_BG = 0xFF1E1E2Eu;
static const uint32_t FM_FG = 0xFFFFFFFFu;
static const uint32_t FM_SEL = 0xFF3B82F6u;
static const uint32_t FM_FOLDER = 0xFFF6C177u;
static const uint32_t FM_FILE = 0xFF9AA0A6u;
static const uint32_t FM_DIM = 0xFF94A3B8u;

static char fm_path[FM_PATH_CAP];
static FmEntry fm_entries[FM_MAX_ENTRIES];
static int fm_count = 0;
static int fm_selected = -1;
static int fm_last_click_index = -1;
static unsigned long fm_last_click_time = 0;
static int fm_view_w = 800;
static int fm_view_h = 600;

static void fm_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int fm_strcmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t i = 0;
    while (a[i] && b[i]) {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        ++i;
    }
    if (a[i] == b[i]) return 0;
    if (a[i]) return 1;
    return -1;
}

static void fm_parent_path(const char *path, char *out, size_t cap) {
    if (!path || !*path) {
        fm_copy(out, cap, "/");
        return;
    }
    char tmp[FM_PATH_CAP];
    fm_copy(tmp, sizeof(tmp), path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = 0;
    }
    int idx = -1;
    for (size_t i = 0; i < len; ++i) {
        if (tmp[i] == '/') idx = (int)i;
    }
    if (idx <= 0) {
        if (tmp[0] == '/') fm_copy(out, cap, "/");
        else fm_copy(out, cap, ".");
        return;
    }
    tmp[idx] = 0;
    if (tmp[0] == 0) fm_copy(out, cap, "/");
    else fm_copy(out, cap, tmp);
}

static void fm_join_path(char *dst, size_t cap, const char *base, const char *name) {
    if (!name || !*name) {
        fm_copy(dst, cap, base && *base ? base : ".");
        return;
    }
    if (name[0] == '/') {
        fm_copy(dst, cap, name);
        return;
    }
    if (fm_strcmp(name, ".") == 0) {
        fm_copy(dst, cap, base && *base ? base : ".");
        return;
    }
    if (fm_strcmp(name, "..") == 0) {
        fm_parent_path(base, dst, cap);
        return;
    }

    char out[FM_PATH_CAP];
    out[0] = 0;
    if (base && *base && fm_strcmp(base, "/") != 0) {
        fm_copy(out, sizeof(out), base);
    } else if (base && fm_strcmp(base, "/") == 0) {
        fm_copy(out, sizeof(out), "/");
    } else {
        fm_copy(out, sizeof(out), ".");
    }

    if (fm_strcmp(out, ".") == 0) {
        fm_copy(out, sizeof(out), name);
    } else {
        size_t ll = strlen(out);
        if (ll > 0 && out[ll - 1] != '/') {
            out[ll] = '/';
            out[ll + 1] = 0;
        }
        size_t l2 = strlen(out);
        fm_copy(out + l2, sizeof(out) - l2, name);
    }
    fm_copy(dst, cap, out);
}

static void fm_set_path(const char *path) {
    if (!path || !*path) path = ".";
    fm_copy(fm_path, sizeof(fm_path), path);
}

static int fm_load(const char *path) {
    fm_count = 0;
    fm_selected = -1;
    fm_last_click_index = -1;
    fm_last_click_time = 0;

    dirent_t de;
    int pos = 0;
    while (fm_count < FM_MAX_ENTRIES) {
        int rc = readdir_pos(path, pos, &de);
        if (rc != 1) break;
        fm_copy(fm_entries[fm_count].name, sizeof(fm_entries[fm_count].name), de.d_name);
        fm_entries[fm_count].is_dir = (de.d_type == 4);
        ++fm_count;
        ++pos;
    }
    return 0;
}

int fm_navigate(const char *path) {
    if (!path || !*path) {
        path = (fm_path[0] ? fm_path : ".");
    }
    fm_set_path(path);
    chdir(fm_path);
    return fm_load(fm_path);
}

int fm_init(const char *path) {
    fm_view_w = 800;
    fm_view_h = 600;
    fm_count = 0;
    fm_selected = -1;
    fm_last_click_index = -1;
    fm_last_click_time = 0;
    fm_path[0] = 0;
    if (!path || !*path) path = ".";
    return fm_navigate(path);
}

static void fm_draw_text_trunc(Surface *surface, int x, int y, const char *text, uint32_t color) {
    if (!surface || !text) return;
    char buf[32];
    size_t len = strlen(text);
    size_t maxc = 10;
    if (len <= maxc) {
        fm_copy(buf, sizeof(buf), text);
    } else {
        size_t i = 0;
        for (; i < maxc - 2 && i < len; ++i) buf[i] = text[i];
        buf[i] = '.';
        buf[i + 1] = '.';
        buf[i + 2] = 0;
    }
    surface_draw_text(surface, x, y, buf, color);
}

static void fm_draw_icon(Surface *surface, int x, int y, int is_dir) {
    if (!surface) return;
    if (is_dir) {
        surface_fill_rect(surface, x + 4, y, 24, 6, FM_FOLDER);
        surface_fill_rect(surface, x, y + 6, 32, 24, FM_FOLDER);
    } else {
        surface_fill_rect(surface, x + 6, y, 20, 32, FM_FILE);
        surface_fill_rect(surface, x + 20, y, 6, 8, FM_BG);
    }
}

int fm_draw(Surface *surface) {
    if (!surface) return -1;
    fm_view_w = surface->width > 0 ? surface->width : fm_view_w;
    fm_view_h = surface->height > 0 ? surface->height : fm_view_h;

    surface_clear(surface, FM_BG);

    surface_draw_text(surface, FM_MARGIN, 4, fm_path, FM_FG);
    char info[64];
    snprintf(info, sizeof(info), "  (%d entries)", fm_count);
    int px = FM_MARGIN + (int)strlen(fm_path) * 8 + 8;
    if (px < FM_MARGIN) px = FM_MARGIN;
    surface_draw_text(surface, px, 4, info, FM_DIM);

    int columns = (fm_view_w - 2 * FM_MARGIN) / FM_CELL_W;
    if (columns < 1) columns = 1;

    int y0 = FM_HEADER_H;
    for (int i = 0; i < fm_count; ++i) {
        int col = i % columns;
        int row = i / columns;
        int x = FM_MARGIN + col * FM_CELL_W;
        int y = y0 + row * FM_CELL_H;
        if (y + FM_CELL_H > fm_view_h) break;

        if (i == fm_selected) {
            surface_fill_rect(surface, x, y, FM_CELL_W - 2, FM_CELL_H - 2, FM_SEL);
        }

        fm_draw_icon(surface, x + (FM_CELL_W - FM_ICON) / 2, y + 4, fm_entries[i].is_dir);

        uint32_t tc = (i == fm_selected) ? FM_BG : FM_FG;
        fm_draw_text_trunc(surface, x + 4, y + FM_CELL_H - 18, fm_entries[i].name, tc);
    }

    return 0;
}

int fm_handle_click(int x, int y) {
    if (x < 0 || y < 0) return -1;
    int w = fm_view_w > 0 ? fm_view_w : 800;
    (void)fm_view_h;

    if (y < FM_HEADER_H) return -1;

    int columns = (w - 2 * FM_MARGIN) / FM_CELL_W;
    if (columns < 1) columns = 1;

    int col = (x - FM_MARGIN) / FM_CELL_W;
    int row = (y - FM_HEADER_H) / FM_CELL_H;
    if (col < 0 || col >= columns) return -1;
    if (row < 0) return -1;

    int index = row * columns + col;
    if (index < 0 || index >= fm_count) {
        fm_selected = -1;
        return -1;
    }

    unsigned long now = time_ms();
    int is_double = (index == fm_last_click_index) &&
                    (fm_last_click_time != 0) &&
                    (now - fm_last_click_time <= FM_DBLCLICK_MS);

    fm_last_click_index = index;
    fm_last_click_time = now;
    fm_selected = index;

    if (is_double && fm_entries[index].is_dir) {
        char target[FM_PATH_CAP];
        fm_join_path(target, sizeof(target), fm_path, fm_entries[index].name);
        fm_navigate(target);
    }

    return index;
}

const char *fm_get_path(void) { return fm_path; }

const char *fm_get_selected_name(void) {
    if (fm_selected < 0 || fm_selected >= fm_count) return 0;
    return fm_entries[fm_selected].name;
}

int fm_get_count(void) { return fm_count; }

const FmEntry *fm_get_entry(int index) {
    if (index < 0 || index >= fm_count) return 0;
    return &fm_entries[index];
}