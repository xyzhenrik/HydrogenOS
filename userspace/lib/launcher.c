#include "launcher.h"

#include <stddef.h>
#include <stdint.h>

static Launcher *g_launcher = 0;

static void launcher_copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int launcher_is_surface_ptr(void *p) {
    if (!p) return 0;
    Surface *s = (Surface *)p;
    return s->pixels != 0 && s->width > 0 && s->height > 0;
}

static Launcher *launcher_as_launcher(void *p) {
    if (!p) return 0;
    if (launcher_is_surface_ptr(p)) return 0;
    return (Launcher *)p;
}

static Launcher *launcher_ensure(Launcher *l) {
    if (!l) l = g_launcher;
    if (!l) {
        g_launcher = launcher_create();
        l = g_launcher;
    }
    if (!g_launcher && l) g_launcher = l;
    return l;
}

static int launcher_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!haystack) return 0;
    for (size_t i = 0; haystack[i]; ++i) {
        size_t j = 0;
        while (needle[j]) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b || a == '\0') break;
            ++j;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static void launcher_update_filter(Launcher *l) {
    if (!l) return;
    l->filtered_count = 0;
    for (int i = 0; i < l->app_count && l->filtered_count < LAUNCHER_RESULT_CAP; ++i) {
        LauncherApp *app = &l->apps[i];
        int match = 1;
        if (l->query[0]) {
            match = launcher_contains_ci(app->name, l->query) ||
                    launcher_contains_ci(app->path, l->query) ||
                    launcher_contains_ci(app->keywords, l->query);
        }
        if (match) {
            l->filtered[l->filtered_count++] = i;
        }
    }
    if (l->filtered_count == 0) {
        l->selected = 0;
    } else if (l->selected >= l->filtered_count) {
        l->selected = l->filtered_count - 1;
    }
    if (l->selected < 0) l->selected = 0;
}

static void launcher_reset_query(Launcher *l) {
    if (!l) return;
    l->query[0] = '\0';
    l->cursor = 0;
    l->selected = 0;
    launcher_update_filter(l);
}

Launcher *launcher_create(void) {
    Launcher *l = (Launcher *)malloc(sizeof(Launcher));
    if (!l) return 0;
    memset(l, 0, sizeof(Launcher));

    l->visible = 0;
    l->app_count = 0;
    l->filtered_count = 0;
    l->selected = 0;
    l->cursor = 0;
    l->query[0] = '\0';
    l->selected_path[0] = '\0';

    l->x = 0;
    l->y = 0;
    l->w = 0;
    l->h = 0;

    l->bg_color = 0x00181828u;
    l->fg_color = 0x00FFFFFFu;
    l->accent_color = 0x00303060u;
    l->input_bg = 0x00101020u;

    l->on_select = 0;
    l->userdata = 0;
    l->user_data = 0;
    l->context = 0;

    launcher_add_app(l, "Shell", "/shell", "terminal command line console");
    launcher_add_app(l, "Hello", "/hello", "hello world demo");
    launcher_add_app(l, "Echo", "/echo", "echo text");
    launcher_add_app(l, "Cat", "/cat", "cat file read");
    launcher_add_app(l, "Filesystem Test", "/fstest", "filesystem test");
    launcher_add_app(l, "Boot Probe", "/bootprobe", "boot probe");
    launcher_add_app(l, "Time", "/time", "time clock");
    launcher_add_app(l, "Sleep", "/sleep", "sleep delay");

    launcher_update_filter(l);
    return l;
}

void launcher_destroy(Launcher *launcher) {
    if (!launcher) return;
    if (g_launcher == launcher) g_launcher = 0;
    free(launcher);
}

Launcher *launcher_get(void) {
    if (!g_launcher) g_launcher = launcher_create();
    return g_launcher;
}

int launcher_is_visible(Launcher *launcher) {
    Launcher *l = launcher_ensure(launcher);
    return l ? l->visible : 0;
}

const char *launcher_selected_path(Launcher *launcher) {
    Launcher *l = launcher_ensure(launcher);
    return l ? l->selected_path : 0;
}

const char *launcher_get_selected_path(Launcher *launcher) {
    return launcher_selected_path(launcher);
}

void launcher_add_app(Launcher *launcher, const char *name, const char *path, const char *keywords) {
    Launcher *l = launcher_ensure(launcher);
    if (!l || l->app_count >= LAUNCHER_MAX_APPS) return;
    LauncherApp *app = &l->apps[l->app_count++];
    launcher_copy_str(app->name, sizeof(app->name), name ? name : "");
    launcher_copy_str(app->path, sizeof(app->path), path ? path : "");
    launcher_copy_str(app->keywords, sizeof(app->keywords), keywords ? keywords : "");
    launcher_update_filter(l);
}

void launcher_clear_apps(Launcher *launcher) {
    Launcher *l = launcher_ensure(launcher);
    if (!l) return;
    l->app_count = 0;
    launcher_update_filter(l);
}

void launcher_set_apps(Launcher *launcher, const LauncherApp *apps, int count) {
    Launcher *l = launcher_ensure(launcher);
    if (!l) return;
    l->app_count = 0;
    if (apps && count > 0) {
        for (int i = 0; i < count && l->app_count < LAUNCHER_MAX_APPS; ++i) {
            l->apps[l->app_count] = apps[i];
            ++l->app_count;
        }
    }
    launcher_update_filter(l);
}

void launcher_set_pos(Launcher *launcher, int x, int y) {
    Launcher *l = launcher_ensure(launcher);
    if (!l) return;
    l->x = x;
    l->y = y;
}

void launcher_set_size(Launcher *launcher, int w, int h) {
    Launcher *l = launcher_ensure(launcher);
    if (!l) return;
    l->w = w;
    l->h = h;
}

static int launcher_is_up_key(int key) {
    return key == 259 || key == 265 || key == 65362 || key == 1001 || key == 17;
}

static int launcher_is_down_key(int key) {
    return key == 260 || key == 264 || key == 65364 || key == 1002 || key == 18;
}

static int launcher_handle_key_impl(Launcher *l, int key) {
    if (!l) return -1;

    if (key == '\n' || key == '\r') {
        if (l->filtered_count > 0 && l->selected >= 0 && l->selected < l->filtered_count) {
            int app_index = l->filtered[l->selected];
            if (app_index >= 0 && app_index < l->app_count) {
                launcher_copy_str(l->selected_path, sizeof(l->selected_path), l->apps[app_index].path);
                if (l->on_select) l->on_select(l->selected_path);
                return app_index;
            }
        }
        return -1;
    }

    if (key == 8 || key == 127) {
        size_t len = strlen(l->query);
        if (len > 0) l->query[len - 1] = '\0';
        l->cursor = (int)strlen(l->query);
        l->selected = 0;
        launcher_update_filter(l);
        return -1;
    }

    if (key == 27) {
        launcher_reset_query(l);
        return -1;
    }

    if (launcher_is_up_key(key)) {
        if (l->selected > 0) --l->selected;
        return -1;
    }

    if (launcher_is_down_key(key)) {
        if (l->selected + 1 < l->filtered_count) ++l->selected;
        return -1;
    }

    if (key >= 32 && key <= 126) {
        size_t len = strlen(l->query);
        if (len + 1 < LAUNCHER_QUERY_CAP) {
            l->query[len] = (char)key;
            l->query[len + 1] = '\0';
            l->cursor = (int)(len + 1);
        }
        l->selected = 0;
        launcher_update_filter(l);
        return -1;
    }

    return -1;
}

static void launcher_draw_impl(Launcher *l, Surface *surface) {
    if (!l || !surface) return;
    if (!l->visible) return;

    if (l->w <= 0) l->w = surface->width * 2 / 3;
    if (l->h <= 0) l->h = surface->height / 2;
    if (l->w < 160) l->w = 160;
    if (l->h < 120) l->h = 120;
    if (l->x == 0 && l->y == 0) {
        l->x = (surface->width - l->w) / 2;
        l->y = (surface->height - l->h) / 3;
    }
    if (l->x < 0) l->x = 0;
    if (l->y < 0) l->y = 0;

    /* Panel. */
    surface_fill_rect(surface, l->x, l->y, l->w, l->h, l->bg_color);
    surface_draw_rect(surface, l->x, l->y, l->w, l->h, 0x00FFFFFFu);

    /* Input field. */
    int input_x = l->x + 8;
    int input_y = l->y + 8;
    int input_w = l->w - 16;
    int input_h = 20;
    surface_fill_rect(surface, input_x, input_y, input_w, input_h, l->input_bg);
    surface_draw_rect(surface, input_x, input_y, input_w, input_h, 0x00FFFFFFu);
    surface_draw_text(surface, input_x + 4, input_y + 2, l->query[0] ? l->query : "Search apps...", l->fg_color);

    int cx = input_x + 4 + (int)strlen(l->query) * 8;
    surface_fill_rect(surface, cx, input_y + 2, 1, 16, l->fg_color);

    /* Results. */
    int row_x = l->x + 8;
    int row_y = input_y + input_h + 8;
    int row_w = l->w - 16;
    int row_h = 18;
    int max_rows = (l->h - (row_y - l->y) - 8) / row_h;
    if (max_rows < 1) max_rows = 1;

    for (int r = 0; r < l->filtered_count && r < max_rows; ++r) {
        int app_index = l->filtered[r];
        if (app_index < 0 || app_index >= l->app_count) continue;
        LauncherApp *app = &l->apps[app_index];
        int y = row_y + r * row_h;

        if (r == l->selected) {
            surface_fill_rect(surface, row_x, y, row_w, row_h, l->accent_color);
        }

        char line[192];
        line[0] = '\0';
        snprintf(line, sizeof(line), "%s  %s", app->name, app->path);
        surface_draw_text(surface, row_x + 4, y + 1, line, l->fg_color);
    }

    if (l->filtered_count == 0) {
        surface_draw_text(surface, row_x + 4, row_y + 1, "No matching apps", l->fg_color);
    }
}

/* Instance-style init/show/hide/etc. */
Launcher *launcher_init_0(void) {
    if (!g_launcher) g_launcher = launcher_create();
    return g_launcher;
}

Launcher *launcher_init_1(uintptr_t a) {
    (void)a;
    return launcher_init_0();
}

Launcher *launcher_init_2(uintptr_t a, uintptr_t b) {
    (void)a; (void)b;
    return launcher_init_0();
}

Launcher *launcher_init_3(uintptr_t a, uintptr_t b, uintptr_t c) {
    (void)a; (void)b; (void)c;
    return launcher_init_0();
}

Launcher *launcher_init_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    (void)a; (void)b; (void)c; (void)d;
    return launcher_init_0();
}

static void launcher_show_impl(Launcher *l) {
    l = launcher_ensure(l);
    if (!l) return;
    l->visible = 1;
    launcher_reset_query(l);
}

void launcher_show_0(void) {
    launcher_show_impl(g_launcher);
}

void launcher_show_1(uintptr_t a) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = g_launcher;
    launcher_show_impl(l);
}

void launcher_show_2(uintptr_t a, uintptr_t b) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    launcher_show_impl(l);
}

void launcher_show_3(uintptr_t a, uintptr_t b, uintptr_t c) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    if (!l) l = launcher_as_launcher((void*)c);
    launcher_show_impl(l);
}

void launcher_show_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    if (!l) l = launcher_as_launcher((void*)c);
    if (!l) l = launcher_as_launcher((void*)d);
    launcher_show_impl(l);
}

static void launcher_hide_impl(Launcher *l) {
    l = launcher_ensure(l);
    if (l) l->visible = 0;
}

void launcher_hide_0(void) {
    launcher_hide_impl(g_launcher);
}

void launcher_hide_1(uintptr_t a) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = g_launcher;
    launcher_hide_impl(l);
}

void launcher_hide_2(uintptr_t a, uintptr_t b) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    launcher_hide_impl(l);
}

void launcher_hide_3(uintptr_t a, uintptr_t b, uintptr_t c) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    if (!l) l = launcher_as_launcher((void*)c);
    launcher_hide_impl(l);
}

void launcher_hide_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = launcher_as_launcher((void*)b);
    if (!l) l = launcher_as_launcher((void*)c);
    if (!l) l = launcher_as_launcher((void*)d);
    launcher_hide_impl(l);
}

int launcher_handle_key_0(void) {
    return launcher_handle_key_impl(g_launcher ? g_launcher : launcher_get(), 0);
}

int launcher_handle_key_1(uintptr_t a) {
    if ((uintptr_t)a < 0x100000) {
        Launcher *l = g_launcher ? g_launcher : launcher_get();
        return launcher_handle_key_impl(l, (int)(uintptr_t)a);
    }
    Launcher *l = launcher_as_launcher((void*)a);
    if (!l) l = g_launcher ? g_launcher : launcher_get();
    return launcher_handle_key_impl(l, 0);
}

int launcher_handle_key_2(uintptr_t a, uintptr_t b) {
    Launcher *l = 0;
    int key = 0;
    int have_key = 0;

    void *args[2] = { (void*)a, (void*)b };
    for (int i = 0; i < 2; ++i) {
        if (launcher_is_surface_ptr(args[i])) continue;
        if ((uintptr_t)args[i] < 0x100000) {
            key = (int)(uintptr_t)args[i];
            have_key = 1;
        } else {
            if (!l) l = (Launcher *)args[i];
        }
    }
    if (!l) l = g_launcher ? g_launcher : launcher_get();
    if (!have_key) key = 0;
    return launcher_handle_key_impl(l, key);
}

int launcher_handle_key_3(uintptr_t a, uintptr_t b, uintptr_t c) {
    void *args[3] = { (void*)a, (void*)b, (void*)c };
    Launcher *l = 0;
    int key = 0;
    int have_key = 0;
    for (int i = 0; i < 3; ++i) {
        if (launcher_is_surface_ptr(args[i])) continue;
        if ((uintptr_t)args[i] < 0x100000) {
            key = (int)(uintptr_t)args[i];
            have_key = 1;
        } else {
            if (!l) l = (Launcher *)args[i];
        }
    }
    if (!l) l = g_launcher ? g_launcher : launcher_get();
    if (!have_key) key = 0;
    return launcher_handle_key_impl(l, key);
}

int launcher_handle_key_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    void *args[4] = { (void*)a, (void*)b, (void*)c, (void*)d };
    Launcher *l = 0;
    int key = 0;
    int have_key = 0;
    for (int i = 0; i < 4; ++i) {
        if (launcher_is_surface_ptr(args[i])) continue;
        if ((uintptr_t)args[i] < 0x100000) {
            key = (int)(uintptr_t)args[i];
            have_key = 1;
        } else {
            if (!l) l = (Launcher *)args[i];
        }
    }
    if (!l) l = g_launcher ? g_launcher : launcher_get();
    if (!have_key) key = 0;
    return launcher_handle_key_impl(l, key);
}

static void launcher_draw_find(void *args[], int n, Launcher **out_l, Surface **out_s) {
    *out_l = 0;
    *out_s = 0;
    for (int i = 0; i < n; ++i) {
        if (launcher_is_surface_ptr(args[i])) {
            if (!*out_s) *out_s = (Surface *)args[i];
        } else if ((uintptr_t)args[i] >= 0x100000) {
            if (!*out_l) *out_l = (Launcher *)args[i];
        }
    }
    if (!*out_l) *out_l = g_launcher;
    if (!*out_l && *out_s) *out_l = launcher_get();
}

void launcher_draw_0(void) {
    /* Nothing to draw without a surface. */
}

void launcher_draw_1(uintptr_t a) {
    void *args[1] = { (void*)a };
    Launcher *l = 0;
    Surface *s = 0;
    launcher_draw_find(args, 1, &l, &s);
    if (s) launcher_draw_impl(launcher_ensure(l), s);
}

void launcher_draw_2(uintptr_t a, uintptr_t b) {
    void *args[2] = { (void*)a, (void*)b };
    Launcher *l = 0;
    Surface *s = 0;
    launcher_draw_find(args, 2, &l, &s);
    if (s) launcher_draw_impl(launcher_ensure(l), s);
}

void launcher_draw_3(uintptr_t a, uintptr_t b, uintptr_t c) {
    void *args[3] = { (void*)a, (void*)b, (void*)c };
    Launcher *l = 0;
    Surface *s = 0;
    launcher_draw_find(args, 3, &l, &s);
    if (s) launcher_draw_impl(launcher_ensure(l), s);
}

void launcher_draw_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    void *args[4] = { (void*)a, (void*)b, (void*)c, (void*)d };
    Launcher *l = 0;
    Surface *s = 0;
    launcher_draw_find(args, 4, &l, &s);
    if (s) launcher_draw_impl(launcher_ensure(l), s);
}
/* Simple global API wrappers. */
void launcher_init(void) { launcher_init_0(); }
void launcher_show(void) { launcher_show_0(); }
void launcher_hide(void) { launcher_hide_0(); }
int launcher_handle_key(int key) { return launcher_handle_key_1((uintptr_t)key); }
void launcher_draw(Surface *surface) { launcher_draw_1((uintptr_t)surface); }
