#include "filemanager.h"
#include "mini_libc.h"
#include "syscall.h"

#define GLYPH_WIDTH  8
#define GLYPH_HEIGHT 16

/* Places sidebar. */
#define SIDEBAR_WIDE         164
#define SIDEBAR_NARROW       136
#define SIDEBAR_NARROW_BELOW 640
#define SIDEBAR_MARGIN       10
#define PLACES_TITLE_Y       16
#define PLACES_TOP           40
#define ITEM_HEIGHT          32
#define ITEM_GAP             2
#define ITEM_RADIUS          6
#define ITEM_ICON            20
#define ITEM_ICON_RADIUS     5
#define ITEM_PAD             8
#define PLACE_COUNT          4

/* Toolbar. */
#define TOOLBAR_HEIGHT  44
#define TOOLBAR_PAD     12
#define BUTTON_SIZE     28
#define BUTTON_RADIUS   6
#define BUTTON_GAP      2
#define GROUP_GAP       12
#define ACTIONS_WIDTH   (3 * BUTTON_SIZE + 2 * 4)
#define VIEW_BOX_WIDTH  60
#define PATH_RADIUS     7
#define CRUMB_PAD       6
#define CRUMB_SEPARATOR 12
#define CRUMB_MAX       128
#define CRUMB_LABEL_MAX 14

#define STATUS_HEIGHT 26
#define STATUS_PAD    14

/* Icon view. */
#define GRID_PAD     12
#define CELL_WIDTH   104
#define CELL_HEIGHT  92
#define ICON_LARGE   48
#define ICON_TOP     8
#define LABEL_CHARS  11
#define LABEL_HEIGHT 20
#define LABEL_RADIUS 6
#define EDIT_FIELD_WIDTH 132
#define BACKDROP_RADIUS 10

/* List view. */
#define ROW_HEIGHT    28
#define HEADER_HEIGHT 26
#define LIST_PAD      8
#define LIST_VPAD     4
#define ICON_SMALL    20
#define SIZE_COLUMN_WIDTH    72
#define KIND_COLUMN_WIDTH    104
#define COLUMN_GAP    16

#define SCROLL_STEP   48
#define SCROLLBAR_WIDTH 4
#define DOUBLE_CLICK_MS 500
#define MAX_ENTRIES   4096
#define DELETE_DEPTH  16
#define SNIFF_BYTES   32

#define DIALOG_WIDTH  360
#define DIALOG_HEIGHT 140
#define DIALOG_PAD    20
#define DIALOG_BUTTON_WIDTH 96
#define DIALOG_RADIUS 12

#define TRIANGLE_MAX_WIDTH 64
#define TOOLTIP_PAD   8

#define HOVER_OPACITY  24u
#define SELECT_OPACITY 64u

/* Hit identifiers; places, crumbs and items are base + index. */
#define HIT_NONE           (-1)
#define HIT_BACK           1
#define HIT_FORWARD        2
#define HIT_UP             3
#define HIT_ICONS          4
#define HIT_LIST           5
#define HIT_NEW_FOLDER     6
#define HIT_RENAME         7
#define HIT_DELETE         8
#define HIT_DIALOG_CANCEL  9
#define HIT_DIALOG_CONFIRM 10
#define HIT_DIALOG         11
#define HIT_PLACE          16
#define HIT_CRUMB          32
#define HIT_ITEM           1024

#define COLOR_WHITE 0xFFFFFFu
#define COLOR_BLACK 0x000000u

/* File-type colours identify the kind, not the theme. */
#define COLOR_FOLDER_BACK  0x3A8EE6u
#define COLOR_FOLDER_FRONT 0x67BDFFu
#define COLOR_APP_TILE     0x5E5CE6u
#define COLOR_BOX_BODY     0xD7A468u
#define COLOR_BOX_LID      0xB98450u
#define COLOR_BOX_TAPE     0xF1DDB9u
#define COLOR_SKY          0x74BFFFu
#define COLOR_SUN          0xFFD60Au
#define COLOR_HILL_BACK    0x2FAE55u
#define COLOR_HILL_FRONT   0x58D77Au

typedef struct Place {
    const char *name;
    const char *path;
    uint32_t tint;
} Place;

static const Place places[PLACE_COUNT] = {
    { "Root",     "/",     0x8E8E93u },
    { "Home",     "/home/user", 0x0A84FFu },
    { "Temp",     "/tmp",  0xFF9F0Au },
    { "Programs", "/bin",  0x5E5CE6u },
};

static const char *const kind_names[] = {
    "Folder", "Document", "Text file", "Application", "Package", "Image",
};

static const char *const text_extensions[] = {
    "txt", "md", "conf", "cfg", "ini", "rc", "log", "c", "h", "s", "json", "csv", "sh",
};
static const char *const archive_extensions[] = { "hpkg", "tar", "gz", "zip", "cpio" };
static const char *const image_extensions[] = { "png", "jpg", "jpeg", "gif", "bmp", "ppm" };

typedef struct Crumb {
    GfxRect rect;
    int end;                       /* length of the path prefix it leads to */
    char label[CRUMB_LABEL_MAX + 4];
} Crumb;

typedef struct FmLayout {
    GfxRect sidebar;
    GfxRect place[PLACE_COUNT];
    GfxRect column;
    GfxRect toolbar;
    GfxRect back, forward, up;
    GfxRect path;
    GfxRect view_box, view_icons, view_list;
    GfxRect new_folder, rename, delete;
    GfxRect header;                /* list view only */
    GfxRect viewport;
    GfxRect status;

    Crumb crumb[CRUMB_MAX + 1];
    int crumb_count;

    int columns;
    int content_height;
    int max_scroll;
    int name_x;
    int size_right;
    int kind_x;
} FmLayout;

typedef struct Painter {
    Window *window;
    GfxCanvas canvas;
    const SettingsPalette *palette;
} Painter;

/* ---------------- strings and paths ---------------- */

static int is_dot_name(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int compare_names(const char *a, const char *b) {
    const char *x = a;
    const char *y = b;

    while (*x && lower((unsigned char)*x) == lower((unsigned char)*y)) {
        ++x;
        ++y;
    }
    int order = lower((unsigned char)*x) - lower((unsigned char)*y);
    return order != 0 ? order : strcmp(a, b);
}

static int has_extension(const char *name, const char *const *list, int count) {
    const char *dot = 0;

    for (const char *c = name; *c; ++c) {
        if (*c == '.')
            dot = c;
    }
    if (!dot || dot == name)
        return 0;
    for (int i = 0; i < count; ++i) {
        const char *want = list[i];
        const char *have = dot + 1;
        while (*want && lower((unsigned char)*have) == *want) {
            ++want;
            ++have;
        }
        if (!*want && !*have)
            return 1;
    }
    return 0;
}

/* Collapses "." and ".." and repeated slashes of an absolute path. Returns -1
   when the result does not fit. */
static int path_normalize(char *out, size_t capacity, const char *path) {
    size_t length = 0;

    out[0] = 0;
    while (*path) {
        const char *start;
        size_t part;

        while (*path == '/')
            ++path;
        start = path;
        while (*path && *path != '/')
            ++path;
        part = (size_t)(path - start);
        if (part == 0 || (part == 1 && start[0] == '.'))
            continue;
        if (part == 2 && start[0] == '.' && start[1] == '.') {
            while (length > 0 && out[length - 1] != '/')
                --length;
            if (length > 0)
                --length;
            out[length] = 0;
            continue;
        }
        if (length + 1 + part + 1 > capacity)
            return -1;
        out[length++] = '/';
        memcpy(out + length, start, part);
        length += part;
        out[length] = 0;
    }
    if (length == 0) {
        if (capacity < 2)
            return -1;
        out[0] = '/';
        out[1] = 0;
    }
    return 0;
}

static int path_join(char *out, size_t capacity, const char *base, const char *name) {
    char combined[2 * FM_PATH_CAP];

    if ((size_t)snprintf(combined, sizeof(combined), "%s/%s", base, name) >= sizeof(combined))
        return -1;
    return path_normalize(out, capacity, combined);
}

static void fit_text(char *out, size_t capacity, const char *text, int max_chars) {
    size_t length = strlen(text);
    size_t keep;

    if (capacity == 0)
        return;
    if (max_chars < 0)
        max_chars = 0;
    if (length <= (size_t)max_chars) {
        keep = length;
    } else if (max_chars <= 3) {
        keep = (size_t)max_chars;
    } else {
        keep = (size_t)max_chars - 3;
    }
    if (keep > capacity - 1)
        keep = capacity - 1;
    memcpy(out, text, keep);
    out[keep] = 0;
    if (length > (size_t)max_chars && max_chars > 3 && keep + 3 < capacity)
        memcpy(out + keep, "...", 4);
}

static void format_size(char *out, size_t capacity, unsigned long bytes) {
    static const char *const units[] = { "B", "KB", "MB", "GB" };
    unsigned long divisor = 1;
    int unit = 0;
    unsigned long whole;

    while (unit < 3 && bytes / divisor >= 1024) {
        divisor *= 1024;
        ++unit;
    }
    whole = bytes / divisor;
    if (unit == 0)
        snprintf(out, capacity, "%lu B", whole);
    else if (whole < 100)
        snprintf(out, capacity, "%lu.%lu %s", whole, (bytes % divisor) * 10 / divisor, units[unit]);
    else
        snprintf(out, capacity, "%lu %s", whole, units[unit]);
}

static void set_note(FileManager *fm, const char *text) {
    snprintf(fm->note, sizeof(fm->note), "%s", text);
}

static void set_name_note(FileManager *fm, const char *name, const char *tail) {
    char shown[28];

    fit_text(shown, sizeof(shown), name, 24);
    snprintf(fm->note, sizeof(fm->note), "\"%s\" %s", shown, tail);
}

/* ---------------- folder contents ---------------- */

static FmKind sniff_kind(const char *path) {
    unsigned char head[SNIFF_BYTES];
    long count;
    int fd = (int)open(path, 0);

    if (fd < 0)
        return FM_KIND_FILE;
    count = read(fd, head, sizeof(head));
    close(fd);
    if (count < 0)
        return FM_KIND_FILE;
    if (count >= 4 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F')
        return FM_KIND_EXECUTABLE;
    for (long i = 0; i < count; ++i) {
        unsigned char c = head[i];
        if ((c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c > 0x7E)
            return FM_KIND_FILE;
    }
    return FM_KIND_TEXT;
}

static FmKind classify(const char *path, const char *name, uint8_t d_type) {
    stat_t info;

    if (stat(path, &info) != 0)
        return d_type == 4 ? FM_KIND_FOLDER : FM_KIND_FILE;
    if (S_ISDIR(info.st_mode))
        return FM_KIND_FOLDER;
    if (has_extension(name, text_extensions, (int)(sizeof(text_extensions) / sizeof(text_extensions[0]))))
        return FM_KIND_TEXT;
    if (has_extension(name, archive_extensions, (int)(sizeof(archive_extensions) / sizeof(archive_extensions[0]))))
        return FM_KIND_ARCHIVE;
    if (has_extension(name, image_extensions, (int)(sizeof(image_extensions) / sizeof(image_extensions[0]))))
        return FM_KIND_IMAGE;
    return sniff_kind(path);
}

static int compare_entries(const void *left, const void *right) {
    const FmEntry *a = (const FmEntry *)left;
    const FmEntry *b = (const FmEntry *)right;
    int a_folder = a->kind == FM_KIND_FOLDER;
    int b_folder = b->kind == FM_KIND_FOLDER;

    if (a_folder != b_folder)
        return b_folder - a_folder;
    return compare_names(a->name, b->name);
}

static void refresh_free_space(FileManager *fm) {
    sysinfo_t info;

    fm->free_known = sys_sysinfo(&info) == 0;
    if (fm->free_known)
        fm->free_bytes = (unsigned long)info.free_blocks * info.block_size;
}

static int entry_find(const FileManager *fm, const char *name) {
    for (int i = 0; i < fm->count; ++i) {
        if (strcmp(fm->entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Re-reads the current folder. The entry called select (if any) stays selected. */
static int reload(FileManager *fm, const char *select) {
    dirent_t de;
    int total = 0;
    int rc;
    FmEntry *list = 0;
    int filled = 0;

    free(fm->entries);
    fm->entries = 0;
    fm->count = 0;
    fm->selected = -1;
    fm->load_failed = 0;
    refresh_free_space(fm);

    for (int pos = 0; total < MAX_ENTRIES; ++pos) {
        rc = readdir_pos(fm->path, pos, &de);
        if (rc != 1) {
            fm->load_failed = rc < 0;
            break;
        }
        if (!is_dot_name(de.d_name))
            ++total;
    }
    if (total == 0)
        return 0;

    list = (FmEntry *)malloc((size_t)total * sizeof(FmEntry));
    if (!list)
        return -1;
    for (int pos = 0; filled < total; ++pos) {
        char full[FM_PATH_CAP];

        if (readdir_pos(fm->path, pos, &de) != 1)
            break;
        if (is_dot_name(de.d_name) || path_join(full, sizeof(full), fm->path, de.d_name) != 0)
            continue;
        snprintf(list[filled].name, sizeof(list[filled].name), "%s", de.d_name);
        list[filled].kind = classify(full, de.d_name, de.d_type);
        list[filled].size = 0;
        if (list[filled].kind != FM_KIND_FOLDER) {
            stat_t info;

            if (stat(full, &info) == 0)
                list[filled].size = info.st_size;
        }
        ++filled;
    }
    qsort(list, (size_t)filled, sizeof(FmEntry), compare_entries);
    fm->entries = list;
    fm->count = filled;
    if (select)
        fm->selected = entry_find(fm, select);
    return 0;
}

/* ---------------- navigation ---------------- */

static void history_push(char (*stack)[FM_PATH_CAP], int *count, const char *path) {
    if (*count == FM_HISTORY) {
        memmove(stack[0], stack[1], (size_t)(FM_HISTORY - 1) * FM_PATH_CAP);
        --*count;
    }
    snprintf(stack[(*count)++], FM_PATH_CAP, "%s", path);
}

static void end_edit(FileManager *fm) {
    fm->editing = 0;
    fm->edit_created = 0;
}

/* Shows the folder without touching the history. */
static int enter_folder(FileManager *fm, const char *target) {
    char normalized[FM_PATH_CAP];
    char previous[FM_PATH_CAP];
    dirent_t probe;

    if (path_normalize(normalized, sizeof(normalized), target) != 0 ||
        readdir_pos(normalized, 0, &probe) < 0) {
        set_note(fm, "This folder can't be opened.");
        return -1;
    }
    snprintf(previous, sizeof(previous), "%s", fm->path);
    snprintf(fm->path, sizeof(fm->path), "%s", normalized);
    if (reload(fm, 0) != 0) {
        snprintf(fm->path, sizeof(fm->path), "%s", previous);
        reload(fm, 0);
        set_note(fm, "Not enough memory to show this folder.");
        return -1;
    }
    fm->scroll = 0;
    fm->last_click_item = -1;
    fm->note[0] = 0;
    end_edit(fm);
    return 0;
}

static unsigned int navigate(FileManager *fm, const char *target) {
    char previous[FM_PATH_CAP];

    snprintf(previous, sizeof(previous), "%s", fm->path);
    if (enter_folder(fm, target) == 0 && strcmp(previous, fm->path) != 0) {
        history_push(fm->back, &fm->back_count, previous);
        fm->forward_count = 0;
    }
    return FM_REDRAW;
}

static unsigned int go_back(FileManager *fm) {
    char previous[FM_PATH_CAP];

    if (fm->back_count == 0)
        return 0;
    snprintf(previous, sizeof(previous), "%s", fm->path);
    if (enter_folder(fm, fm->back[fm->back_count - 1]) == 0) {
        --fm->back_count;
        history_push(fm->forward, &fm->forward_count, previous);
    }
    return FM_REDRAW;
}

static unsigned int go_forward(FileManager *fm) {
    char previous[FM_PATH_CAP];

    if (fm->forward_count == 0)
        return 0;
    snprintf(previous, sizeof(previous), "%s", fm->path);
    if (enter_folder(fm, fm->forward[fm->forward_count - 1]) == 0) {
        --fm->forward_count;
        history_push(fm->back, &fm->back_count, previous);
    }
    return FM_REDRAW;
}

static unsigned int go_up(FileManager *fm) {
    char parent[FM_PATH_CAP];

    if (strcmp(fm->path, "/") == 0)
        return 0;
    if (path_join(parent, sizeof(parent), fm->path, "..") != 0)
        return 0;
    return navigate(fm, parent);
}

/* ---------------- geometry ---------------- */

static GfxRect rect_make(int x, int y, int w, int h) {
    GfxRect rect = { x, y, w, h };
    return rect;
}

static int rect_contains(const GfxRect *rect, int x, int y) {
    return rect->w > 0 && rect->h > 0 && x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

static int text_width(const char *text) {
    return (int)strlen(text) * GLYPH_WIDTH;
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static int min_int(int a, int b) {
    return a < b ? a : b;
}

/* Splits the path into clickable segments and fits them into the path bar,
   dropping the oldest ones behind a leading "..." when they do not fit. */
static void layout_crumbs(const FileManager *fm, FmLayout *layout) {
    Crumb all[CRUMB_MAX];
    int count = 0;
    int widths[CRUMB_MAX];
    int available = layout->path.w - 2 * CRUMB_PAD;
    int first = 0;
    int used;
    int x;
    int ellipsis;

    snprintf(all[0].label, sizeof(all[0].label), "Root");
    all[0].end = 1;
    count = 1;
    for (const char *c = fm->path; *c && count < CRUMB_MAX;) {
        const char *start;
        int length;

        while (*c == '/')
            ++c;
        start = c;
        while (*c && *c != '/')
            ++c;
        length = (int)(c - start);
        if (length == 0)
            continue;
        all[count].end = (int)(c - fm->path);
        {
            char name[FM_NAME_CAP];

            snprintf(name, sizeof(name), "%.*s", length, start);
            fit_text(all[count].label, sizeof(all[count].label), name, CRUMB_LABEL_MAX);
        }
        ++count;
    }
    for (int i = 0; i < count; ++i)
        widths[i] = text_width(all[i].label) + 2 * CRUMB_PAD;

    ellipsis = 3 * GLYPH_WIDTH + 2 * CRUMB_PAD + CRUMB_SEPARATOR;
    for (first = 0; first < count - 1; ++first) {
        int total = first > 0 ? ellipsis : 0;

        for (int i = first; i < count; ++i)
            total += widths[i] + (i > first ? CRUMB_SEPARATOR : 0);
        if (total <= available)
            break;
    }

    x = layout->path.x + CRUMB_PAD;
    used = 0;
    /* When even the last segment alone is too wide, it gets the whole bar. */
    if (first > 0 && (first < count - 1 || ellipsis + widths[first] <= available)) {
        Crumb *crumb = &layout->crumb[used++];

        snprintf(crumb->label, sizeof(crumb->label), "...");
        crumb->end = all[first - 1].end;
        crumb->rect = rect_make(x, layout->path.y + 3, 3 * GLYPH_WIDTH + 2 * CRUMB_PAD,
                                layout->path.h - 6);
        x += crumb->rect.w + CRUMB_SEPARATOR;
    }
    for (int i = first; i < count; ++i) {
        Crumb *crumb = &layout->crumb[used++];
        int room = layout->path.x + layout->path.w - CRUMB_PAD - x;
        int width = min_int(widths[i], max_int(room, 0));

        *crumb = all[i];
        if (width < widths[i])
            fit_text(crumb->label, sizeof(crumb->label), all[i].label,
                     (width - 2 * CRUMB_PAD) / GLYPH_WIDTH);
        crumb->rect = rect_make(x, layout->path.y + 3, width, layout->path.h - 6);
        x += width + CRUMB_SEPARATOR;
    }
    layout->crumb_count = used;
}

static void layout_make(const FileManager *fm, int width, int height, FmLayout *layout) {
    int sidebar_width = width >= SIDEBAR_NARROW_BELOW ? SIDEBAR_WIDE : SIDEBAR_NARROW;
    int top = fm->top;
    int column_x = sidebar_width;
    int column_width = width - sidebar_width;
    int button_y = top + (TOOLBAR_HEIGHT - BUTTON_SIZE) / 2;
    int body_top = top + TOOLBAR_HEIGHT;
    int right;
    int x;
    int viewport_top;

    memset(layout, 0, sizeof(*layout));
    if (column_width < 0)
        column_width = 0;
    if (height < body_top + STATUS_HEIGHT)
        height = body_top + STATUS_HEIGHT;

    layout->sidebar = rect_make(0, top, sidebar_width, height - top);
    for (int i = 0; i < PLACE_COUNT; ++i) {
        layout->place[i] = rect_make(SIDEBAR_MARGIN, top + PLACES_TOP + i * (ITEM_HEIGHT + ITEM_GAP),
                                     sidebar_width - 2 * SIDEBAR_MARGIN, ITEM_HEIGHT);
    }
    layout->column = rect_make(column_x, top, column_width, height - top);
    layout->toolbar = rect_make(column_x, top, column_width, TOOLBAR_HEIGHT);

    x = column_x + TOOLBAR_PAD;
    layout->back = rect_make(x, button_y, BUTTON_SIZE, BUTTON_SIZE);
    x += BUTTON_SIZE + BUTTON_GAP;
    layout->forward = rect_make(x, button_y, BUTTON_SIZE, BUTTON_SIZE);
    x += BUTTON_SIZE + BUTTON_GAP;
    layout->up = rect_make(x, button_y, BUTTON_SIZE, BUTTON_SIZE);
    x += BUTTON_SIZE + GROUP_GAP;

    right = column_x + column_width - TOOLBAR_PAD;
    layout->delete = rect_make(right - BUTTON_SIZE, button_y, BUTTON_SIZE, BUTTON_SIZE);
    layout->rename = rect_make(right - 2 * BUTTON_SIZE - 4, button_y, BUTTON_SIZE, BUTTON_SIZE);
    layout->new_folder = rect_make(right - ACTIONS_WIDTH, button_y, BUTTON_SIZE, BUTTON_SIZE);
    layout->view_box = rect_make(right - ACTIONS_WIDTH - GROUP_GAP - VIEW_BOX_WIDTH, button_y,
                                 VIEW_BOX_WIDTH, BUTTON_SIZE);
    layout->view_icons = rect_make(layout->view_box.x + 2, button_y + 2, 28, BUTTON_SIZE - 4);
    layout->view_list = rect_make(layout->view_box.x + 30, button_y + 2, 28, BUTTON_SIZE - 4);
    layout->path = rect_make(x, button_y, max_int(layout->view_box.x - GROUP_GAP - x, 0), BUTTON_SIZE);
    layout_crumbs(fm, layout);

    layout->status = rect_make(column_x, height - STATUS_HEIGHT, column_width, STATUS_HEIGHT);
    viewport_top = body_top;
    if (fm->view == FM_VIEW_LIST) {
        layout->header = rect_make(column_x, body_top, column_width, HEADER_HEIGHT);
        viewport_top += HEADER_HEIGHT;
    }
    layout->viewport = rect_make(column_x, viewport_top, column_width,
                                 max_int(layout->status.y - viewport_top, 0));

    if (fm->view == FM_VIEW_ICONS) {
        int usable = column_width - 2 * GRID_PAD;
        int rows;

        layout->columns = max_int(usable / CELL_WIDTH, 1);
        rows = (fm->count + layout->columns - 1) / layout->columns;
        layout->content_height = rows > 0 ? rows * CELL_HEIGHT + 2 * GRID_PAD : 0;
    } else {
        layout->columns = 1;
        layout->content_height = fm->count > 0 ? fm->count * ROW_HEIGHT + 2 * LIST_VPAD : 0;
        layout->kind_x = column_x + column_width - LIST_PAD - KIND_COLUMN_WIDTH;
        layout->size_right = layout->kind_x - COLUMN_GAP;
        layout->name_x = column_x + LIST_PAD + 8 + ICON_SMALL + 10;
    }
    layout->max_scroll = max_int(layout->content_height - layout->viewport.h, 0);
}

/* Builds the layout for the window and keeps the scroll offset inside it. */
static void layout_for(FileManager *fm, const Window *window, FmLayout *layout) {
    layout_make(fm, window->w, window->h, layout);
    if (fm->scroll > layout->max_scroll)
        fm->scroll = layout->max_scroll;
    if (fm->scroll < 0)
        fm->scroll = 0;
}

static GfxRect item_rect(const FileManager *fm, const FmLayout *layout, int index) {
    if (fm->view == FM_VIEW_ICONS) {
        int column = index % layout->columns;
        int row = index / layout->columns;

        return rect_make(layout->viewport.x + GRID_PAD + column * CELL_WIDTH,
                         layout->viewport.y + GRID_PAD + row * CELL_HEIGHT - fm->scroll,
                         CELL_WIDTH, CELL_HEIGHT);
    }
    return rect_make(layout->viewport.x + LIST_PAD,
                     layout->viewport.y + LIST_VPAD + index * ROW_HEIGHT - fm->scroll,
                     layout->viewport.w - 2 * LIST_PAD, ROW_HEIGHT);
}

static int item_at(const FileManager *fm, const FmLayout *layout, int x, int y) {
    int local_x;
    int local_y;
    int index;

    if (!rect_contains(&layout->viewport, x, y))
        return -1;
    local_x = x - layout->viewport.x;
    local_y = y - layout->viewport.y + fm->scroll;
    if (fm->view == FM_VIEW_ICONS) {
        int column;

        local_x -= GRID_PAD;
        local_y -= GRID_PAD;
        if (local_x < 0 || local_y < 0)
            return -1;
        column = local_x / CELL_WIDTH;
        if (column >= layout->columns)
            return -1;
        index = local_y / CELL_HEIGHT * layout->columns + column;
    } else {
        local_y -= LIST_VPAD;
        if (local_y < 0 || local_x < LIST_PAD || local_x >= layout->viewport.w - LIST_PAD)
            return -1;
        index = local_y / ROW_HEIGHT;
    }
    return index < fm->count ? index : -1;
}

static void ensure_visible(FileManager *fm, const FmLayout *layout, int index) {
    GfxRect rect;

    if (index < 0 || index >= fm->count)
        return;
    rect = item_rect(fm, layout, index);
    if (rect.y < layout->viewport.y)
        fm->scroll -= layout->viewport.y - rect.y;
    else if (rect.y + rect.h > layout->viewport.y + layout->viewport.h)
        fm->scroll += rect.y + rect.h - (layout->viewport.y + layout->viewport.h);
    if (fm->scroll > layout->max_scroll)
        fm->scroll = layout->max_scroll;
    if (fm->scroll < 0)
        fm->scroll = 0;
}

static void dialog_layout(const FmLayout *layout, GfxRect *card, GfxRect *cancel, GfxRect *confirm) {
    int x = layout->column.x + (layout->column.w - DIALOG_WIDTH) / 2;
    int y = layout->column.y + (layout->column.h - DIALOG_HEIGHT) / 2;

    if (x < layout->column.x + 8)
        x = layout->column.x + 8;
    *card = rect_make(x, y, DIALOG_WIDTH, DIALOG_HEIGHT);
    *confirm = rect_make(x + DIALOG_WIDTH - DIALOG_PAD - DIALOG_BUTTON_WIDTH,
                         y + DIALOG_HEIGHT - DIALOG_PAD - BUTTON_SIZE,
                         DIALOG_BUTTON_WIDTH, BUTTON_SIZE);
    *cancel = rect_make(confirm->x - 8 - DIALOG_BUTTON_WIDTH, confirm->y,
                        DIALOG_BUTTON_WIDTH, BUTTON_SIZE);
}

/* ---------------- editing names ---------------- */

static GfxRect edit_rect(const FileManager *fm, const FmLayout *layout, int index) {
    GfxRect item = item_rect(fm, layout, index);

    if (fm->view == FM_VIEW_ICONS) {
        /* Wider than the cell so a typical name fits; it is painted on top of its neighbours. */
        int x = item.x + (CELL_WIDTH - EDIT_FIELD_WIDTH) / 2;

        if (x < layout->viewport.x + 4)
            x = layout->viewport.x + 4;
        return rect_make(x, item.y + ICON_TOP + ICON_LARGE + 6, EDIT_FIELD_WIDTH, LABEL_HEIGHT + 2);
    }
    return rect_make(layout->name_x - 6, item.y + 2, layout->size_right - SIZE_COLUMN_WIDTH - 12 - layout->name_x + 6,
                     ROW_HEIGHT - 4);
}

static void edit_begin(FileManager *fm, int index) {
    fm->editing = 1;
    fm->edit_replaces_all = 1;
    snprintf(fm->edit_text, sizeof(fm->edit_text), "%s", fm->entries[index].name);
    fm->edit_len = (int)strlen(fm->edit_text);
}

static int path_exists(const char *path) {
    stat_t info;

    return stat(path, &info) == 0;
}

static void edit_commit(FileManager *fm) {
    char old_path[FM_PATH_CAP];
    char new_path[FM_PATH_CAP];
    char old_name[FM_NAME_CAP];
    char new_name[FM_EDIT_CAP];

    if (!fm->editing || fm->selected < 0 || fm->selected >= fm->count)
        return;
    snprintf(old_name, sizeof(old_name), "%s", fm->entries[fm->selected].name);
    snprintf(new_name, sizeof(new_name), "%s", fm->edit_text);
    end_edit(fm);
    if (new_name[0] == 0 || is_dot_name(new_name) || strcmp(new_name, old_name) == 0)
        return;
    if (path_join(old_path, sizeof(old_path), fm->path, old_name) != 0 ||
        path_join(new_path, sizeof(new_path), fm->path, new_name) != 0) {
        set_note(fm, "That name is too long.");
        return;
    }
    if (path_exists(new_path)) {
        set_name_note(fm, new_name, "already exists.");
        return;
    }
    if (sys_rename(old_path, new_path) != 0) {
        set_name_note(fm, old_name, "could not be renamed.");
        return;
    }
    reload(fm, new_name);
}

static unsigned int edit_key(FileManager *fm, int key) {
    if (key == FM_KEY_ESCAPE) {
        end_edit(fm);
        return FM_REDRAW;
    }
    if (key == '\n' || key == '\r') {
        edit_commit(fm);
        return FM_REDRAW;
    }
    if (key == '\b' || key == 0x7F) {
        if (fm->edit_replaces_all) {
            fm->edit_len = 0;
            fm->edit_replaces_all = 0;
        } else if (fm->edit_len > 0) {
            --fm->edit_len;
        }
        fm->edit_text[fm->edit_len] = 0;
        return FM_REDRAW;
    }
    if (key < 0x20 || key > 0x7E || key == '/')
        return 0;
    if (fm->edit_replaces_all) {
        fm->edit_len = 0;
        fm->edit_replaces_all = 0;
    }
    if (fm->edit_len >= FM_EDIT_CAP - 1)
        return 0;
    fm->edit_text[fm->edit_len++] = (char)key;
    fm->edit_text[fm->edit_len] = 0;
    return FM_REDRAW;
}

/* ---------------- actions on entries ---------------- */

static unsigned int create_folder(FileManager *fm, const FmLayout *layout) {
    char name[FM_NAME_CAP];
    char path[FM_PATH_CAP];
    int created = 0;

    for (int number = 1; number < 1000 && !created; ++number) {
        if (number == 1)
            snprintf(name, sizeof(name), "untitled folder");
        else
            snprintf(name, sizeof(name), "untitled folder %d", number);
        if (path_join(path, sizeof(path), fm->path, name) != 0)
            break;
        if (!path_exists(path))
            created = sys_mkdir(path) == 0 ? 1 : -1;
        if (created < 0)
            break;
    }
    if (created != 1) {
        set_note(fm, "The folder could not be created.");
        return FM_REDRAW;
    }
    fm->note[0] = 0;
    if (reload(fm, name) != 0 || fm->selected < 0) {
        set_note(fm, "The folder was created but could not be shown.");
        return FM_REDRAW;
    }
    ensure_visible(fm, layout, fm->selected);
    edit_begin(fm, fm->selected);
    fm->edit_created = 1;
    return FM_REDRAW;
}

static int remove_tree(const char *path, int depth) {
    if (depth > DELETE_DEPTH)
        return -1;
    for (int guard = 0; guard < MAX_ENTRIES; ++guard) {
        dirent_t de;
        char child[FM_PATH_CAP];
        stat_t info;
        int pos = 0;
        int rc;
        int removed;

        while ((rc = readdir_pos(path, pos, &de)) == 1 && is_dot_name(de.d_name))
            ++pos;
        if (rc != 1)
            break;
        if (path_join(child, sizeof(child), path, de.d_name) != 0 || stat(child, &info) != 0)
            return -1;
        removed = S_ISDIR(info.st_mode) ? remove_tree(child, depth + 1) : (int)sys_unlink(child);
        if (removed != 0)
            return -1;
    }
    return sys_rmdir(path) == 0 ? 0 : -1;
}

static void delete_selected(FileManager *fm) {
    const FmEntry *entry;
    char path[FM_PATH_CAP];
    char name[FM_NAME_CAP];
    int index = fm->selected;
    int failed;

    fm->confirm_delete = 0;
    if (index < 0 || index >= fm->count)
        return;
    entry = &fm->entries[index];
    snprintf(name, sizeof(name), "%s", entry->name);
    if (path_join(path, sizeof(path), fm->path, name) != 0) {
        set_note(fm, "That name is too long.");
        return;
    }
    failed = entry->kind == FM_KIND_FOLDER ? remove_tree(path, 0) != 0 : sys_unlink(path) != 0;
    reload(fm, 0);
    if (failed) {
        set_name_note(fm, name, "could not be deleted.");
        fm->selected = entry_find(fm, name);
        return;
    }
    fm->note[0] = 0;
    fm->selected = fm->count > 0 ? min_int(index, fm->count - 1) : -1;
}

static unsigned int activate(FileManager *fm, int index) {
    const FmEntry *entry = &fm->entries[index];
    char path[FM_PATH_CAP];

    if (path_join(path, sizeof(path), fm->path, entry->name) != 0) {
        set_note(fm, "That name is too long.");
        return FM_REDRAW;
    }
    switch (entry->kind) {
    case FM_KIND_FOLDER:
        return navigate(fm, path);
    case FM_KIND_EXECUTABLE:
        fm->action = FM_ACTION_RUN;
        break;
    case FM_KIND_TEXT:
        fm->action = FM_ACTION_OPEN_TEXT;
        break;
    default:
        set_note(fm, "No application can open this kind of file yet.");
        return FM_REDRAW;
    }
    snprintf(fm->action_path, sizeof(fm->action_path), "%s", path);
    return FM_REDRAW | FM_ACTION;
}

static void select_item(FileManager *fm, const FmLayout *layout, int index) {
    fm->selected = index;
    fm->note[0] = 0;
    ensure_visible(fm, layout, index);
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

static void tint_rect(const Painter *painter, const GfxRect *rect, uint32_t color,
                      unsigned int opacity) {
    int x0 = rect->x < 0 ? 0 : rect->x;
    int y0 = rect->y < 0 ? 0 : rect->y;
    int x1 = rect->x + rect->w > painter->canvas.width ? painter->canvas.width : rect->x + rect->w;
    int y1 = rect->y + rect->h > painter->canvas.height ? painter->canvas.height : rect->y + rect->h;

    for (int y = y0; y < y1; ++y) {
        uint32_t *row = painter->canvas.pixels + (long)y * painter->canvas.stride;
        for (int x = x0; x < x1; ++x)
            row[x] = gfx_mix(row[x], color, opacity);
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

static void fill_box(const Painter *painter, int x, int y, int w, int h, int radius,
                     uint32_t color) {
    GfxRect rect = rect_make(x, y, w, h);

    fill_round(painter, &rect, radius, color, 255);
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

/* A line of the given thickness, stamped with round dots. */
static void draw_stroke(const Painter *painter, int x0, int y0, int x1, int y1, int thickness,
                        uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int steps = max_int(dx, dy);

    if (steps == 0)
        steps = 1;
    for (int step = 0; step <= steps; ++step) {
        int x = x0 + (x1 - x0) * step / steps;
        int y = y0 + (y1 - y0) * step / steps;

        fill_box(painter, x - thickness / 2, y - thickness / 2, thickness, thickness,
                 thickness / 2, color);
    }
}

/* Anti-aliased triangle: four sample rows per pixel row, exact horizontal coverage. */
static void fill_triangle(const Painter *painter, int x0, int y0, int x1, int y1, int x2, int y2,
                          uint32_t color) {
    int xs[3] = { x0, x1, x2 };
    int ys[3] = { y0, y1, y2 };
    int min_x = min_int(x0, min_int(x1, x2));
    int max_x = max_int(x0, max_int(x1, x2));
    int min_y = min_int(y0, min_int(y1, y2));
    int max_y = max_int(y0, max_int(y1, y2));

    if (max_x - min_x + 1 > TRIANGLE_MAX_WIDTH)
        return;
    for (int y = min_y; y < max_y; ++y) {
        unsigned int cover[TRIANGLE_MAX_WIDTH] = { 0 };

        for (int sample = 0; sample < 4; ++sample) {
            long sample_y = (long)y * 256 + sample * 64 + 32;
            long low = 0x7FFFFFFFL;
            long high = -0x7FFFFFFFL;

            for (int edge = 0; edge < 3; ++edge) {
                int a = edge;
                int b = (edge + 1) % 3;
                long ya = (long)ys[a] * 256;
                long yb = (long)ys[b] * 256;
                long x;

                if (ya == yb || sample_y < (ya < yb ? ya : yb) || sample_y >= (ya < yb ? yb : ya))
                    continue;
                x = (long)xs[a] * 256 + (long)(xs[b] - xs[a]) * 256 * (sample_y - ya) / (yb - ya);
                if (x < low)
                    low = x;
                if (x > high)
                    high = x;
            }
            if (low >= high)
                continue;
            for (long px = low >> 8; px <= (high - 1) >> 8; ++px) {
                long left = low > px * 256 ? low : px * 256;
                long right = high < px * 256 + 256 ? high : px * 256 + 256;

                cover[px - min_x] += (unsigned int)(right - left);
            }
        }
        for (int px = 0; px < max_x - min_x + 1; ++px) {
            unsigned int alpha = cover[px] / 4;

            gfx_blend_pixel(&painter->canvas, min_x + px, y, color, alpha > 255 ? 255 : alpha);
        }
    }
}

/* ---------------- file-type icons ---------------- */

/* Icons are designed on a 48 unit grid and scaled to the requested size. */
#define U(v) (((v) * size + 24) / 48)

static void icon_box(const Painter *painter, int x, int y, int size, int left, int top,
                     int width, int height, int radius, uint32_t color, int grow) {
    int w = max_int(U(width), 1) + 2 * grow;
    int h = max_int(U(height), 1) + 2 * grow;

    fill_box(painter, x + U(left) - grow, y + U(top) - grow, w, h, max_int(U(radius), 1), color);
}

static void draw_folder_icon(const Painter *painter, int x, int y, int size) {
    icon_box(painter, x, y, size, 4, 7, 20, 14, 4, COLOR_FOLDER_BACK, 0);
    icon_box(painter, x, y, size, 4, 12, 40, 28, 5, COLOR_FOLDER_BACK, 0);
    icon_box(painter, x, y, size, 4, 18, 40, 22, 5, COLOR_FOLDER_FRONT, 0);
}

/* A page with a folded corner: two rounded boxes leave the corner open, the
   fold fills half of it. grow outlines the shape by that many pixels. */
static void draw_page_shape(const Painter *painter, int x, int y, int size, int grow,
                            uint32_t color) {
    icon_box(painter, x, y, size, 10, 4, 18, 40, 3, color, grow);
    icon_box(painter, x, y, size, 10, 14, 28, 30, 3, color, grow);
}

static void draw_page_icon(const Painter *painter, int x, int y, int size) {
    const SettingsPalette *palette = painter->palette;
    uint32_t outline = gfx_mix(palette->card, palette->ink_dim, 150) & 0x00FFFFFFu;
    uint32_t fold = gfx_mix(palette->card, palette->ink_dim, 90) & 0x00FFFFFFu;

    draw_page_shape(painter, x, y, size, 1, outline);
    draw_page_shape(painter, x, y, size, 0, palette->card);
    fill_triangle(painter, x + U(28), y + U(4), x + U(28), y + U(14), x + U(38), y + U(14), fold);
    draw_stroke(painter, x + U(28), y + U(4), x + U(38), y + U(14), 1, outline);
}

static void draw_text_icon(const Painter *painter, int x, int y, int size) {
    uint32_t ink = painter->palette->ink_dim;

    draw_page_icon(painter, x, y, size);
    for (int line = 0; line < 4; ++line) {
        int width = line == 3 ? 9 : 18;

        icon_box(painter, x, y, size, 15, 22 + line * 5, width, 2, 1, ink, 0);
    }
}

static void draw_executable_icon(const Painter *painter, int x, int y, int size) {
    int thick = size >= 32 ? 3 : 2;

    icon_box(painter, x, y, size, 5, 5, 38, 38, 10, COLOR_APP_TILE, 0);
    draw_stroke(painter, x + U(15), y + U(17), x + U(23), y + U(25), thick, COLOR_WHITE);
    draw_stroke(painter, x + U(23), y + U(25), x + U(15), y + U(33), thick, COLOR_WHITE);
    icon_box(painter, x, y, size, 26, 32, 10, 3, 1, COLOR_WHITE, 0);
}

static void draw_archive_icon(const Painter *painter, int x, int y, int size) {
    icon_box(painter, x, y, size, 7, 19, 34, 23, 3, COLOR_BOX_BODY, 0);
    icon_box(painter, x, y, size, 4, 9, 40, 12, 3, COLOR_BOX_LID, 0);
    icon_box(painter, x, y, size, 20, 9, 8, 33, 1, COLOR_BOX_TAPE, 0);
    icon_box(painter, x, y, size, 11, 33, 8, 3, 1, COLOR_BOX_LID, 0);
}

static void draw_image_icon(const Painter *painter, int x, int y, int size) {
    GfxRect frame = rect_make(x + U(5), y + U(8), U(38), U(32));

    fill_round(painter, &frame, max_int(U(5), 1), COLOR_SKY, 255);
    fill_box(painter, x + U(28), y + U(13), U(10), U(10), U(5), COLOR_SUN);
    fill_triangle(painter, x + U(9), y + U(39), x + U(21), y + U(22), x + U(33), y + U(39),
                  COLOR_HILL_BACK);
    fill_triangle(painter, x + U(22), y + U(39), x + U(32), y + U(28), x + U(41), y + U(39),
                  COLOR_HILL_FRONT);
    gfx_stroke_round_rect(&painter->canvas, frame.x, frame.y, frame.w, frame.h,
                          max_int(U(5), 1), COLOR_WHITE, 120);
}

static void draw_kind_icon(const Painter *painter, FmKind kind, int x, int y, int size) {
    switch (kind) {
    case FM_KIND_FOLDER:     draw_folder_icon(painter, x, y, size); break;
    case FM_KIND_TEXT:       draw_text_icon(painter, x, y, size); break;
    case FM_KIND_EXECUTABLE: draw_executable_icon(painter, x, y, size); break;
    case FM_KIND_ARCHIVE:    draw_archive_icon(painter, x, y, size); break;
    case FM_KIND_IMAGE:      draw_image_icon(painter, x, y, size); break;
    default:                 draw_page_icon(painter, x, y, size); break;
    }
}

#undef U

/* ---------------- toolbar glyphs ---------------- */

static void glyph_back(const Painter *painter, int cx, int cy, uint32_t color) {
    draw_stroke(painter, cx + 2, cy - 6, cx - 3, cy, 2, color);
    draw_stroke(painter, cx - 3, cy, cx + 2, cy + 6, 2, color);
}

static void glyph_forward(const Painter *painter, int cx, int cy, uint32_t color) {
    draw_stroke(painter, cx - 2, cy - 6, cx + 3, cy, 2, color);
    draw_stroke(painter, cx + 3, cy, cx - 2, cy + 6, 2, color);
}

static void glyph_up(const Painter *painter, int cx, int cy, uint32_t color) {
    draw_stroke(painter, cx, cy + 6, cx, cy - 6, 2, color);
    draw_stroke(painter, cx - 5, cy - 1, cx, cy - 6, 2, color);
    draw_stroke(painter, cx + 5, cy - 1, cx, cy - 6, 2, color);
}

static void glyph_icons(const Painter *painter, int cx, int cy, uint32_t color) {
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column)
            fill_box(painter, cx - 7 + column * 8, cy - 7 + row * 8, 6, 6, 1, color);
    }
}

static void glyph_list(const Painter *painter, int cx, int cy, uint32_t color) {
    for (int row = -1; row <= 1; ++row) {
        fill_box(painter, cx - 7, cy + row * 5 - 1, 2, 2, 1, color);
        fill_box(painter, cx - 3, cy + row * 5 - 1, 10, 2, 1, color);
    }
}

static void glyph_new_folder(const Painter *painter, int cx, int cy, uint32_t color) {
    GfxRect body = rect_make(cx - 8, cy - 4, 16, 12);

    stroke_round(painter, &body, 2, color);
    fill_box(painter, cx - 8, cy - 6, 6, 3, 1, color);
    draw_stroke(painter, cx - 3, cy + 2, cx + 3, cy + 2, 2, color);
    draw_stroke(painter, cx, cy - 1, cx, cy + 5, 2, color);
}

static void glyph_rename(const Painter *painter, int cx, int cy, uint32_t color) {
    draw_stroke(painter, cx + 5, cy - 6, cx - 2, cy + 1, 3, color);
    draw_stroke(painter, cx - 2, cy + 1, cx - 5, cy + 5, 2, color);
    fill_box(painter, cx, cy + 6, 8, 2, 1, color);
}

static void glyph_delete(const Painter *painter, int cx, int cy, uint32_t color) {
    GfxRect body = rect_make(cx - 6, cy - 3, 12, 11);

    fill_box(painter, cx - 3, cy - 8, 6, 2, 1, color);
    fill_box(painter, cx - 7, cy - 6, 14, 2, 1, color);
    stroke_round(painter, &body, 2, color);
    fill_box(painter, cx - 2, cy, 1, 5, 0, color);
    fill_box(painter, cx + 1, cy, 1, 5, 0, color);
}

typedef void (*GlyphFn)(const Painter *, int, int, uint32_t);

static const GfxRect *button_rect(const FmLayout *layout, int hit) {
    switch (hit) {
    case HIT_BACK:       return &layout->back;
    case HIT_FORWARD:    return &layout->forward;
    case HIT_UP:         return &layout->up;
    case HIT_ICONS:      return &layout->view_icons;
    case HIT_LIST:       return &layout->view_list;
    case HIT_NEW_FOLDER: return &layout->new_folder;
    case HIT_RENAME:     return &layout->rename;
    case HIT_DELETE:     return &layout->delete;
    default:             return 0;
    }
}

static int button_enabled(const FileManager *fm, int hit) {
    switch (hit) {
    case HIT_BACK:       return fm->back_count > 0;
    case HIT_FORWARD:    return fm->forward_count > 0;
    case HIT_UP:         return strcmp(fm->path, "/") != 0;
    case HIT_RENAME:
    case HIT_DELETE:     return !fm->editing && fm->selected >= 0;
    case HIT_NEW_FOLDER: return !fm->editing;
    default:             return 1;
    }
}

static void draw_button(const Painter *painter, const FileManager *fm, const FmLayout *layout,
                        int hit, int active, GlyphFn glyph) {
    const SettingsPalette *palette = painter->palette;
    const GfxRect *rect = button_rect(layout, hit);
    int enabled = button_enabled(fm, hit);
    uint32_t color = enabled ? palette->ink : palette->control_off;

    if (active) {
        fill_round(painter, rect, BUTTON_RADIUS, palette->accent, 255);
        color = palette->on_accent;
    } else if (enabled && fm->hover == hit) {
        fill_round(painter, rect, BUTTON_RADIUS, palette->ink, HOVER_OPACITY);
    }
    glyph(painter, rect->x + rect->w / 2, rect->y + rect->h / 2, color);
}

/* ---------------- sidebar ---------------- */

static void draw_place_glyph(const Painter *painter, int place, const GfxRect *tile) {
    uint32_t tint = places[place].tint;
    int x = tile->x;
    int y = tile->y;

    switch (place) {
    case 0:
        fill_box(painter, x + 3, y + 6, 14, 9, 2, COLOR_WHITE);
        fill_box(painter, x + 13, y + 10, 2, 2, 1, tint);
        fill_box(painter, x + 5, y + 10, 5, 1, 0, tint);
        break;
    case 1:
        fill_triangle(painter, x + 10, y + 3, x + 3, y + 10, x + 17, y + 10, COLOR_WHITE);
        fill_box(painter, x + 5, y + 10, 10, 7, 1, COLOR_WHITE);
        fill_box(painter, x + 9, y + 12, 3, 5, 0, tint);
        break;
    case 2:
        fill_box(painter, x + 4, y + 4, 12, 12, 6, COLOR_WHITE);
        fill_box(painter, x + 9, y + 6, 2, 5, 0, tint);
        fill_box(painter, x + 9, y + 9, 4, 2, 0, tint);
        break;
    default:
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column)
                fill_box(painter, x + 4 + column * 7, y + 4 + row * 7, 5, 5, 1, COLOR_WHITE);
        }
        break;
    }
}

/* The place whose folder contains the current one; the deepest wins. */
static int current_place(const FileManager *fm) {
    int best = -1;
    size_t best_length = 0;

    for (int i = 0; i < PLACE_COUNT; ++i) {
        size_t length = strlen(places[i].path);
        int inside = strncmp(fm->path, places[i].path, length) == 0 &&
                     (length == 1 || fm->path[length] == 0 || fm->path[length] == '/');

        if (inside && length >= best_length) {
            best = i;
            best_length = length;
        }
    }
    return best;
}

static void draw_sidebar(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    GfxRect edge = rect_make(layout->sidebar.x + layout->sidebar.w - 1, layout->sidebar.y, 1,
                             layout->sidebar.h);
    int current = current_place(fm);

    fill_rect(painter, &layout->sidebar, palette->sidebar);
    fill_rect(painter, &edge, palette->separator);
    draw_text(painter, SIDEBAR_MARGIN + ITEM_PAD, layout->sidebar.y + PLACES_TITLE_Y, "Places",
              palette->ink_dim);

    for (int i = 0; i < PLACE_COUNT; ++i) {
        const GfxRect *item = &layout->place[i];
        GfxRect icon = rect_make(item->x + ITEM_PAD, item->y + (item->h - ITEM_ICON) / 2,
                                 ITEM_ICON, ITEM_ICON);

        if (i == current)
            fill_round(painter, item, ITEM_RADIUS, palette->ink, 40);
        else if (fm->hover == HIT_PLACE + i)
            fill_round(painter, item, ITEM_RADIUS, palette->ink, HOVER_OPACITY);
        fill_round(painter, &icon, ITEM_ICON_RADIUS, places[i].tint, 255);
        draw_place_glyph(painter, i, &icon);
        draw_text(painter, icon.x + ITEM_ICON + ITEM_PAD, item->y + (item->h - GLYPH_HEIGHT) / 2,
                  places[i].name, palette->ink);
    }
}

/* ---------------- toolbar ---------------- */

static void draw_crumb_separator(const Painter *painter, int x, int cy, uint32_t color) {
    draw_stroke(painter, x - 1, cy - 4, x + 2, cy, 1, color);
    draw_stroke(painter, x + 2, cy, x - 1, cy + 4, 1, color);
}

static void draw_path_bar(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;

    fill_round(painter, &layout->path, PATH_RADIUS, palette->card, 255);
    stroke_round(painter, &layout->path, PATH_RADIUS, palette->separator);
    for (int i = 0; i < layout->crumb_count; ++i) {
        const Crumb *crumb = &layout->crumb[i];
        int last = i == layout->crumb_count - 1;
        int text_x = crumb->rect.x + CRUMB_PAD;
        int text_y = crumb->rect.y + (crumb->rect.h - GLYPH_HEIGHT) / 2;

        if (fm->hover == HIT_CRUMB + i)
            fill_round(painter, &crumb->rect, 5, palette->ink, HOVER_OPACITY);
        if (last)
            draw_text_bold(painter, text_x, text_y, crumb->label, palette->ink);
        else
            draw_text(painter, text_x, text_y, crumb->label, palette->ink_dim);
        if (!last) {
            draw_crumb_separator(painter, crumb->rect.x + crumb->rect.w + CRUMB_SEPARATOR / 2 - 1,
                                 crumb->rect.y + crumb->rect.h / 2, palette->ink_dim);
        }
    }
}

static void draw_toolbar(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    GfxRect line = rect_make(layout->toolbar.x, layout->toolbar.y + layout->toolbar.h - 1,
                             layout->toolbar.w, 1);

    fill_rect(painter, &layout->toolbar, palette->surface);
    fill_rect(painter, &line, palette->separator);

    draw_button(painter, fm, layout, HIT_BACK, 0, glyph_back);
    draw_button(painter, fm, layout, HIT_FORWARD, 0, glyph_forward);
    draw_button(painter, fm, layout, HIT_UP, 0, glyph_up);
    draw_path_bar(painter, fm, layout);

    fill_round(painter, &layout->view_box, BUTTON_RADIUS + 1, palette->sidebar, 255);
    stroke_round(painter, &layout->view_box, BUTTON_RADIUS + 1, palette->separator);
    draw_button(painter, fm, layout, HIT_ICONS, fm->view == FM_VIEW_ICONS, glyph_icons);
    draw_button(painter, fm, layout, HIT_LIST, fm->view == FM_VIEW_LIST, glyph_list);

    draw_button(painter, fm, layout, HIT_NEW_FOLDER, 0, glyph_new_folder);
    draw_button(painter, fm, layout, HIT_RENAME, 0, glyph_rename);
    draw_button(painter, fm, layout, HIT_DELETE, 0, glyph_delete);
}

static const char *tooltip_for(int hit) {
    switch (hit) {
    case HIT_BACK:       return "Back";
    case HIT_FORWARD:    return "Forward";
    case HIT_UP:         return "Enclosing folder";
    case HIT_ICONS:      return "As icons";
    case HIT_LIST:       return "As list";
    case HIT_NEW_FOLDER: return "New folder";
    case HIT_RENAME:     return "Rename";
    case HIT_DELETE:     return "Delete";
    default:             return 0;
    }
}

static void draw_tooltip(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const char *text = tooltip_for(fm->hover);
    const GfxRect *button = button_rect(layout, fm->hover);
    GfxRect pill;
    int width;
    int x;

    if (!text || !button || !button_enabled(fm, fm->hover))
        return;
    width = text_width(text) + 2 * TOOLTIP_PAD;
    x = button->x + (button->w - width) / 2;
    if (x + width > layout->column.x + layout->column.w - 4)
        x = layout->column.x + layout->column.w - 4 - width;
    pill = rect_make(x, button->y + button->h + 6, width, GLYPH_HEIGHT + 8);
    fill_round(painter, &pill, 6, palette->ink, 255);
    draw_text(painter, pill.x + TOOLTIP_PAD, pill.y + 4, text, palette->surface);
}

/* ---------------- items ---------------- */

static void draw_edit_field(const Painter *painter, const FileManager *fm, const GfxRect *field) {
    const SettingsPalette *palette = painter->palette;
    int visible = (field->w - 12) / GLYPH_WIDTH;
    const char *text = fm->edit_text;
    int length = fm->edit_len;
    int text_x = field->x + 6;
    int text_y = field->y + (field->h - GLYPH_HEIGHT) / 2;

    if (length > visible) {
        text += length - visible;
        length = visible;
    }
    fill_round(painter, field, 5, palette->card, 255);
    for (int ring = 0; ring < 2; ++ring) {
        gfx_stroke_round_rect(&painter->canvas, field->x + ring, field->y + ring,
                              field->w - 2 * ring, field->h - 2 * ring, 5 - ring,
                              palette->accent, 255);
    }
    if (fm->edit_replaces_all) {
        GfxRect selection = rect_make(text_x - 1, text_y, length * GLYPH_WIDTH + 2, GLYPH_HEIGHT);

        fill_round(painter, &selection, 2, palette->accent, 255);
        draw_text(painter, text_x, text_y, text, palette->on_accent);
    } else {
        draw_text(painter, text_x, text_y, text, palette->ink);
        fill_box(painter, text_x + length * GLYPH_WIDTH, text_y, 2, GLYPH_HEIGHT, 0, palette->accent);
    }
}

static void draw_icon_item(const Painter *painter, const FileManager *fm, const FmLayout *layout,
                           int index) {
    const SettingsPalette *palette = painter->palette;
    const FmEntry *entry = &fm->entries[index];
    GfxRect cell = item_rect(fm, layout, index);
    GfxRect backdrop = rect_make(cell.x + 12, cell.y + 2, CELL_WIDTH - 24, ICON_LARGE + 12);
    int selected = index == fm->selected;
    char label[LABEL_CHARS + 4];

    if (cell.y + cell.h <= layout->viewport.y || cell.y >= layout->viewport.y + layout->viewport.h)
        return;
    if (selected)
        fill_round(painter, &backdrop, BACKDROP_RADIUS, palette->accent, SELECT_OPACITY);
    else if (fm->hover == HIT_ITEM + index)
        fill_round(painter, &backdrop, BACKDROP_RADIUS, palette->ink, HOVER_OPACITY);
    draw_kind_icon(painter, entry->kind, cell.x + (CELL_WIDTH - ICON_LARGE) / 2,
                   cell.y + ICON_TOP, ICON_LARGE);

    if (selected && fm->editing)
        return;
    fit_text(label, sizeof(label), entry->name, LABEL_CHARS);
    {
        int width = text_width(label);
        GfxRect pill = rect_make(cell.x + (CELL_WIDTH - width) / 2 - 5,
                                 cell.y + ICON_TOP + ICON_LARGE + 8, width + 10, LABEL_HEIGHT);

        if (selected)
            fill_round(painter, &pill, LABEL_RADIUS, palette->accent, 255);
        draw_text(painter, pill.x + 5, pill.y + (pill.h - GLYPH_HEIGHT) / 2, label,
                  selected ? palette->on_accent : palette->ink);
    }
}

static void draw_row_item(const Painter *painter, const FileManager *fm, const FmLayout *layout,
                          int index) {
    const SettingsPalette *palette = painter->palette;
    const FmEntry *entry = &fm->entries[index];
    GfxRect row = item_rect(fm, layout, index);
    int selected = index == fm->selected;
    uint32_t primary = selected ? palette->on_accent : palette->ink;
    uint32_t secondary = selected ? palette->on_accent : palette->ink_dim;
    int text_y = row.y + (row.h - GLYPH_HEIGHT) / 2;
    char name[FM_NAME_CAP];
    char size[24];

    if (row.y + row.h <= layout->viewport.y || row.y >= layout->viewport.y + layout->viewport.h)
        return;
    if (selected)
        fill_round(painter, &row, ITEM_RADIUS, palette->accent, 255);
    else if (fm->hover == HIT_ITEM + index)
        fill_round(painter, &row, ITEM_RADIUS, palette->ink, HOVER_OPACITY);
    draw_kind_icon(painter, entry->kind, row.x + 8, row.y + (row.h - ICON_SMALL) / 2, ICON_SMALL);

    if (!(selected && fm->editing)) {
        fit_text(name, sizeof(name), entry->name,
                 (layout->size_right - SIZE_COLUMN_WIDTH - 12 - layout->name_x) / GLYPH_WIDTH);
        draw_text(painter, layout->name_x, text_y, name, primary);
    }
    if (entry->kind == FM_KIND_FOLDER)
        snprintf(size, sizeof(size), "--");
    else
        format_size(size, sizeof(size), entry->size);
    draw_text_right(painter, layout->size_right, text_y, size, secondary);
    draw_text(painter, layout->kind_x, text_y, kind_names[entry->kind], secondary);
}

static void draw_list_header(const Painter *painter, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    GfxRect line = rect_make(layout->header.x, layout->header.y + layout->header.h - 1,
                             layout->header.w, 1);
    int text_y = layout->header.y + (layout->header.h - GLYPH_HEIGHT) / 2;

    fill_rect(painter, &layout->header, palette->surface);
    fill_rect(painter, &line, palette->separator);
    draw_text(painter, layout->name_x, text_y, "Name", palette->ink_dim);
    draw_text_right(painter, layout->size_right, text_y, "Size", palette->ink_dim);
    draw_text(painter, layout->kind_x, text_y, "Kind", palette->ink_dim);
}

static void draw_empty_state(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const char *title = fm->load_failed ? "This folder can't be opened" : "This folder is empty";
    const char *hint = fm->load_failed ? "Check the path or go back." : "Items you add will show up here.";
    int center_x = layout->viewport.x + layout->viewport.w / 2;
    int top = layout->viewport.y + layout->viewport.h / 2 - 52;

    fill_box(painter, center_x - 28, top, 22, 14, 5, palette->control_off);
    fill_box(painter, center_x - 28, top + 8, 56, 40, 6, palette->control_off);
    draw_text_bold(painter, center_x - text_width(title) / 2, top + 64, title, palette->ink);
    draw_text(painter, center_x - text_width(hint) / 2, top + 86, hint, palette->ink_dim);
}

static void draw_scrollbar(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    int track = layout->viewport.h - 8;
    int thumb;
    int travel;
    GfxRect rect;

    if (layout->max_scroll <= 0 || track <= 0)
        return;
    thumb = max_int(track * layout->viewport.h / layout->content_height, 28);
    travel = track - thumb;
    rect = rect_make(layout->viewport.x + layout->viewport.w - SCROLLBAR_WIDTH - 4,
                     layout->viewport.y + 4 + travel * fm->scroll / layout->max_scroll,
                     SCROLLBAR_WIDTH, thumb);
    fill_round(painter, &rect, SCROLLBAR_WIDTH / 2, painter->palette->ink_dim, 150);
}

static void draw_status(const Painter *painter, const FileManager *fm, const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    GfxRect line = rect_make(layout->status.x, layout->status.y, layout->status.w, 1);
    int text_y = layout->status.y + (layout->status.h - GLYPH_HEIGHT) / 2 + 1;
    char left[160];
    char shown[160];
    char free_text[40];
    char size[24];
    int right_edge = layout->status.x + layout->status.w - STATUS_PAD;
    int left_room;
    uint32_t color = palette->ink_dim;

    fill_rect(painter, &layout->status, palette->surface);
    fill_rect(painter, &line, palette->separator);

    free_text[0] = 0;
    if (fm->free_known) {
        format_size(size, sizeof(size), fm->free_bytes);
        snprintf(free_text, sizeof(free_text), "%s free", size);
        draw_text_right(painter, right_edge, text_y, free_text, palette->ink_dim);
    }

    if (fm->note[0]) {
        snprintf(left, sizeof(left), "%s", fm->note);
        color = palette->danger;
    } else if (fm->selected >= 0 && fm->selected < fm->count) {
        const FmEntry *entry = &fm->entries[fm->selected];

        if (entry->kind == FM_KIND_FOLDER) {
            snprintf(left, sizeof(left), "%d %s  |  %s", fm->count, fm->count == 1 ? "item" : "items",
                     entry->name);
        } else {
            format_size(size, sizeof(size), entry->size);
            snprintf(left, sizeof(left), "%d %s  |  %s, %s", fm->count,
                     fm->count == 1 ? "item" : "items", entry->name, size);
        }
    } else {
        snprintf(left, sizeof(left), "%d %s", fm->count, fm->count == 1 ? "item" : "items");
    }
    left_room = right_edge - text_width(free_text) - 16 - (layout->status.x + STATUS_PAD);
    fit_text(shown, sizeof(shown), left, left_room / GLYPH_WIDTH);
    draw_text(painter, layout->status.x + STATUS_PAD, text_y, shown, color);
}

static void draw_delete_dialog(const Painter *painter, const FileManager *fm,
                               const FmLayout *layout) {
    const SettingsPalette *palette = painter->palette;
    const FmEntry *entry = &fm->entries[fm->selected];
    const GfxShadow *shadow = gfx_shadow_get(DIALOG_RADIUS, 14);
    GfxRect card;
    GfxRect cancel;
    GfxRect confirm;
    GfxRect dim = rect_make(0, layout->column.y, layout->column.x + layout->column.w, layout->column.h);
    char title[40];
    char shown[32];
    int folder = entry->kind == FM_KIND_FOLDER;

    dialog_layout(layout, &card, &cancel, &confirm);
    tint_rect(painter, &dim, COLOR_BLACK, 90);
    if (shadow)
        gfx_draw_shadow(&painter->canvas, shadow, card.x, card.y, card.w, card.h, 0, 6,
                        COLOR_BLACK, 110, 0);
    fill_round(painter, &card, DIALOG_RADIUS, palette->card, 255);
    stroke_round(painter, &card, DIALOG_RADIUS, palette->separator);

    fit_text(shown, sizeof(shown), entry->name, 24);
    snprintf(title, sizeof(title), "Delete \"%s\"?", shown);
    draw_text_bold(painter, card.x + DIALOG_PAD, card.y + DIALOG_PAD, title, palette->ink);
    draw_text(painter, card.x + DIALOG_PAD, card.y + DIALOG_PAD + 28,
              folder ? "This folder and everything in it" : "This file will be removed",
              palette->ink_dim);
    draw_text(painter, card.x + DIALOG_PAD, card.y + DIALOG_PAD + 46,
              folder ? "will be removed. This can't be undone." : "permanently. This can't be undone.",
              palette->ink_dim);

    fill_round(painter, &cancel, BUTTON_RADIUS, fm->hover == HIT_DIALOG_CANCEL ? palette->control_off : palette->sidebar, 255);
    stroke_round(painter, &cancel, BUTTON_RADIUS, palette->separator);
    draw_text(painter, cancel.x + (cancel.w - text_width("Cancel")) / 2,
              cancel.y + (cancel.h - GLYPH_HEIGHT) / 2, "Cancel", palette->ink);
    fill_round(painter, &confirm, BUTTON_RADIUS, palette->danger, fm->hover == HIT_DIALOG_CONFIRM ? 255 : 230);
    draw_text(painter, confirm.x + (confirm.w - text_width("Delete")) / 2,
              confirm.y + (confirm.h - GLYPH_HEIGHT) / 2, "Delete", COLOR_WHITE);
}

/* ---------------- public interface ---------------- */

int fm_init(FileManager *fm, const SettingsPalette *palette, int top, const char *path) {
    if (!fm || !palette)
        return -1;
    memset(fm, 0, sizeof(*fm));
    fm->palette = palette;
    fm->top = top;
    fm->selected = -1;
    fm->hover = HIT_NONE;
    fm->last_click_item = -1;
    snprintf(fm->path, sizeof(fm->path), "/");
    if (path && path[0] == '/' && enter_folder(fm, path) == 0)
        return 0;
    fm->note[0] = 0;
    return reload(fm, 0);
}

void fm_destroy(FileManager *fm) {
    if (!fm)
        return;
    free(fm->entries);
    fm->entries = 0;
    fm->count = 0;
}

void fm_draw(FileManager *fm, Window *window) {
    Painter painter;
    FmLayout layout;
    GfxRect body;

    if (!fm || !fm->palette || !window || !window->surface)
        return;
    painter.window = window;
    painter.canvas.pixels = (uint32_t *)window->surface;
    painter.canvas.width = window->w;
    painter.canvas.height = window->h;
    painter.canvas.stride = window->w;
    painter.canvas.has_alpha = 0;
    painter.palette = fm->palette;
    layout_for(fm, window, &layout);

    body = rect_make(0, fm->top, window->w, window->h - fm->top);
    fill_rect(&painter, &body, fm->palette->surface);

    if (fm->count == 0) {
        draw_empty_state(&painter, fm, &layout);
    } else {
        for (int i = 0; i < fm->count; ++i) {
            if (fm->view == FM_VIEW_ICONS)
                draw_icon_item(&painter, fm, &layout, i);
            else
                draw_row_item(&painter, fm, &layout, i);
        }
        draw_scrollbar(&painter, fm, &layout);
        if (fm->editing && fm->selected >= 0 && fm->selected < fm->count) {
            GfxRect field = edit_rect(fm, &layout, fm->selected);

            draw_edit_field(&painter, fm, &field);
        }
    }
    /* Items scrolled past the viewport edges are covered by the chrome. */
    if (fm->view == FM_VIEW_LIST)
        draw_list_header(&painter, &layout);
    draw_toolbar(&painter, fm, &layout);
    draw_status(&painter, fm, &layout);
    draw_sidebar(&painter, fm, &layout);
    draw_tooltip(&painter, fm, &layout);
    if (fm->confirm_delete && fm->selected >= 0 && fm->selected < fm->count)
        draw_delete_dialog(&painter, fm, &layout);
}

static int hit_test(const FileManager *fm, const FmLayout *layout, int x, int y) {
    if (fm->confirm_delete) {
        GfxRect card;
        GfxRect cancel;
        GfxRect confirm;

        dialog_layout(layout, &card, &cancel, &confirm);
        if (rect_contains(&confirm, x, y))
            return HIT_DIALOG_CONFIRM;
        if (rect_contains(&cancel, x, y))
            return HIT_DIALOG_CANCEL;
        return rect_contains(&card, x, y) ? HIT_DIALOG : HIT_NONE;
    }
    if (rect_contains(&layout->back, x, y))       return HIT_BACK;
    if (rect_contains(&layout->forward, x, y))    return HIT_FORWARD;
    if (rect_contains(&layout->up, x, y))         return HIT_UP;
    if (rect_contains(&layout->view_icons, x, y)) return HIT_ICONS;
    if (rect_contains(&layout->view_list, x, y))  return HIT_LIST;
    if (rect_contains(&layout->new_folder, x, y)) return HIT_NEW_FOLDER;
    if (rect_contains(&layout->rename, x, y))     return HIT_RENAME;
    if (rect_contains(&layout->delete, x, y))     return HIT_DELETE;
    for (int i = 0; i < layout->crumb_count; ++i) {
        if (rect_contains(&layout->crumb[i].rect, x, y))
            return HIT_CRUMB + i;
    }
    for (int i = 0; i < PLACE_COUNT; ++i) {
        if (rect_contains(&layout->place[i], x, y))
            return HIT_PLACE + i;
    }
    {
        int item = item_at(fm, layout, x, y);

        if (item >= 0)
            return HIT_ITEM + item;
    }
    return HIT_NONE;
}

static unsigned int set_view(FileManager *fm, int view) {
    if (fm->view == view)
        return 0;
    fm->view = view;
    fm->scroll = 0;
    return FM_REDRAW;
}

unsigned int fm_click(FileManager *fm, const Window *window, int x, int y) {
    FmLayout layout;
    unsigned int cleared;
    int hit;

    if (!fm || !window)
        return 0;
    layout_for(fm, window, &layout);
    if (fm->editing && fm->selected >= 0 && fm->selected < fm->count) {
        GfxRect field = edit_rect(fm, &layout, fm->selected);

        if (rect_contains(&field, x, y))
            return 0;
    }
    if (fm->editing) {
        edit_commit(fm);
        layout_for(fm, window, &layout);
    }
    hit = hit_test(fm, &layout, x, y);
    cleared = fm->note[0] ? FM_REDRAW : 0;
    fm->note[0] = 0;

    if (fm->confirm_delete) {
        if (hit == HIT_DIALOG_CONFIRM)
            delete_selected(fm);
        else if (hit != HIT_DIALOG)
            fm->confirm_delete = 0;
        return FM_REDRAW;
    }
    if (hit >= HIT_ITEM) {
        int index = hit - HIT_ITEM;
        unsigned long now = time_ms();
        int double_click = index == fm->last_click_item && now - fm->last_click_ms <= DOUBLE_CLICK_MS;

        fm->last_click_item = double_click ? -1 : index;
        fm->last_click_ms = now;
        select_item(fm, &layout, index);
        return double_click ? activate(fm, index) : FM_REDRAW;
    }
    fm->last_click_item = -1;
    if (hit >= HIT_CRUMB) {
        const Crumb *crumb = &layout.crumb[hit - HIT_CRUMB];
        char target[FM_PATH_CAP];

        snprintf(target, sizeof(target), "%.*s", crumb->end, fm->path);
        return navigate(fm, target);
    }
    if (hit >= HIT_PLACE)
        return navigate(fm, places[hit - HIT_PLACE].path);

    switch (hit) {
    case HIT_BACK:       return go_back(fm);
    case HIT_FORWARD:    return go_forward(fm);
    case HIT_UP:         return go_up(fm);
    case HIT_ICONS:      return set_view(fm, FM_VIEW_ICONS) | FM_REDRAW;
    case HIT_LIST:       return set_view(fm, FM_VIEW_LIST) | FM_REDRAW;
    case HIT_NEW_FOLDER: return create_folder(fm, &layout);
    case HIT_RENAME:
        if (fm->selected >= 0)
            edit_begin(fm, fm->selected);
        return FM_REDRAW;
    case HIT_DELETE:
        fm->confirm_delete = fm->selected >= 0;
        fm->hover = HIT_NONE;
        return FM_REDRAW;
    default:
        if (!rect_contains(&layout.viewport, x, y))
            return cleared;
        fm->selected = -1;
        return FM_REDRAW;
    }
}

unsigned int fm_hover(FileManager *fm, const Window *window, int x, int y) {
    FmLayout layout;
    int hit = HIT_NONE;

    if (!fm || !window)
        return 0;
    if (x >= 0 && y >= 0) {
        layout_for(fm, window, &layout);
        hit = hit_test(fm, &layout, x, y);
    }
    if (hit == fm->hover)
        return 0;
    fm->hover = hit;
    return FM_REDRAW;
}

unsigned int fm_wheel(FileManager *fm, const Window *window, int delta) {
    FmLayout layout;
    int before;

    if (!fm || !window || fm->confirm_delete)
        return 0;
    layout_for(fm, window, &layout);
    before = fm->scroll;
    fm->scroll -= delta * SCROLL_STEP;
    if (fm->scroll > layout.max_scroll)
        fm->scroll = layout.max_scroll;
    if (fm->scroll < 0)
        fm->scroll = 0;
    return fm->scroll != before ? FM_REDRAW : 0;
}

/* Index reached from the selection by an arrow key, or -1 to stay. */
static int step_selection(const FileManager *fm, const FmLayout *layout, int key) {
    int from = fm->selected;
    int columns = fm->view == FM_VIEW_ICONS ? layout->columns : 1;
    int target;

    if (fm->count == 0)
        return -1;
    if (from < 0)
        return key == FM_KEY_UP || key == FM_KEY_LEFT || key == FM_KEY_END ? fm->count - 1 : 0;
    switch (key) {
    case FM_KEY_LEFT:  target = from - 1; break;
    case FM_KEY_RIGHT: target = from + 1; break;
    case FM_KEY_UP:    target = from - columns; break;
    case FM_KEY_DOWN:
        target = from + columns;
        if (target >= fm->count && from / columns < (fm->count - 1) / columns)
            target = fm->count - 1;
        break;
    case FM_KEY_HOME:  target = 0; break;
    default:           target = fm->count - 1; break;
    }
    return target >= 0 && target < fm->count && target != from ? target : -1;
}

unsigned int fm_key(FileManager *fm, const Window *window, int key) {
    FmLayout layout;

    if (!fm || !window)
        return 0;
    if (fm->confirm_delete) {
        if (key == FM_KEY_ESCAPE) {
            fm->confirm_delete = 0;
            return FM_REDRAW;
        }
        if (key != '\n' && key != '\r')
            return 0;
        delete_selected(fm);
        return FM_REDRAW;
    }
    if (fm->editing)
        return edit_key(fm, key);
    layout_for(fm, window, &layout);

    switch (key) {
    case '\n':
    case '\r':
        fm->note[0] = 0;
        return fm->selected >= 0 ? activate(fm, fm->selected) : 0;
    case '\b':
    case 0x7F:
        fm->note[0] = 0;
        return go_up(fm);
    case FM_KEY_DELETE:
        if (fm->selected < 0)
            return 0;
        fm->confirm_delete = 1;
        fm->hover = HIT_NONE;
        return FM_REDRAW;
    case FM_KEY_LEFT:
    case FM_KEY_RIGHT:
    case FM_KEY_UP:
    case FM_KEY_DOWN:
    case FM_KEY_HOME:
    case FM_KEY_END: {
        int target = step_selection(fm, &layout, key);

        if (target < 0)
            return 0;
        select_item(fm, &layout, target);
        return FM_REDRAW;
    }
    default:
        return 0;
    }
}

void fm_report(FileManager *fm, const char *text) {
    if (fm && text)
        set_note(fm, text);
}

int fm_take_action(FileManager *fm, char *path, size_t capacity) {
    int action;

    if (!fm || fm->action == FM_ACTION_NONE)
        return FM_ACTION_NONE;
    action = fm->action;
    fm->action = FM_ACTION_NONE;
    if (path && capacity > 0)
        snprintf(path, capacity, "%s", fm->action_path);
    return action;
}
