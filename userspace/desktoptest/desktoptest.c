#include "mini_libc.h"
#include "compositor.h"
#include "gfx_effects.h"
#include "settings.h"
#include "settings_ui.h"
#include "widgets.h"
/* theme.h must precede layout.h: layout.h defines shorthand macros such as
   gap/space that collide with Theme field names. */
#include "theme.h"
#include "layout.h"
#include "taskbar.h"
#include "launcher.h"
#include "filemanager.h"
#include "window_geometry.h"

#define TEST_SURFACE_W 320
#define TEST_SURFACE_H 200

static int quiet_stdout_saved = -1;
static int quiet_pipe_read = -1;

/* compositor init prints its banner on fd 1; the test must keep stdout clean
   because init's stdout=capture compares the exact byte stream. */
static int quiet_stdout_begin(void) {
    int pipe_fds[2];

    if (pipe(pipe_fds) != 0) {
        return -1;
    }
    quiet_stdout_saved = (int)dup(1);
    if (quiet_stdout_saved < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (dup2(pipe_fds[1], 1) < 0) {
        close(quiet_stdout_saved);
        quiet_stdout_saved = -1;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    close(pipe_fds[1]);
    quiet_pipe_read = pipe_fds[0];
    return 0;
}

static int quiet_stdout_end(void) {
    int restored = 0;

    if (quiet_stdout_saved >= 0) {
        restored = dup2(quiet_stdout_saved, 1) >= 0;
        close(quiet_stdout_saved);
        quiet_stdout_saved = -1;
    }
    if (quiet_pipe_read >= 0) {
        close(quiet_pipe_read);
        quiet_pipe_read = -1;
    }
    return restored ? 0 : -1;
}

static const char *test_compositor(void) {
    Window *win;
    uint32_t *front;

    if (quiet_stdout_begin() != 0) {
        return "stdout guard";
    }
    if (comp_init_size(TEST_SURFACE_W, TEST_SURFACE_H) != 0) {
        quiet_stdout_end();
        return "compositor init";
    }
    if (comp_width() != TEST_SURFACE_W || comp_height() != TEST_SURFACE_H) {
        quiet_stdout_end();
        return "compositor size";
    }

    win = comp_create_window(8, 8, 200, 120);
    if (!win || !win->surface) {
        quiet_stdout_end();
        return "compositor window";
    }
    if (win->w != 200 || win->h != 120) {
        quiet_stdout_end();
        return "compositor window size";
    }
    if (comp_window_count() != 1) {
        quiet_stdout_end();
        return "compositor window count";
    }
    if (comp_focus(win) != 0) {
        quiet_stdout_end();
        return "compositor focus";
    }
    if (comp_damage_window(win) != 0) {
        quiet_stdout_end();
        return "compositor damage";
    }
    if (comp_render() != 0) {
        quiet_stdout_end();
        return "compositor render";
    }

    front = comp_front_buffer();
    if (!front) {
        quiet_stdout_end();
        return "compositor front buffer";
    }
    if (front[8 * TEST_SURFACE_W + 8] != win->bg_color) {
        quiet_stdout_end();
        return "compositor pixel";
    }

    if (comp_destroy_window(win) != 0) {
        quiet_stdout_end();
        return "compositor destroy";
    }
    if (comp_window_count() != 0) {
        quiet_stdout_end();
        return "compositor destroy count";
    }
    if (quiet_stdout_end() != 0) {
        return "stdout restore";
    }
    return 0;
}

static int surface_has_ink(Surface *surface, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (surface->pixels[y * surface->stride + x] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

static const char *test_widgets(void) {
    Surface *surface;
    Label *label;
    Button *button;
    TextInput *input;
    Container *box;

    surface = surface_create(160, 96);
    if (!surface) {
        return "surface create";
    }

    surface_clear(surface, 0x00112233u);
    if (surface->pixels[0] != 0x00112233u) {
        return "surface clear";
    }
    surface_fill_rect(surface, 2, 3, 4, 5, 0xFF00FF00u);
    if (surface->pixels[3 * surface->stride + 2] != 0xFF00FF00u ||
        surface->pixels[7 * surface->stride + 5] != 0xFF00FF00u) {
        return "surface fill";
    }
    if (surface->pixels[3 * surface->stride + 1] != 0x00112233u ||
        surface->pixels[8 * surface->stride + 6] != 0x00112233u) {
        return "surface fill bounds";
    }

    label = label_create("Desk", 0xFFFFFFFFu);
    if (!label) {
        return "label create";
    }
    if (strlen(label->text) != 4 || label->w != 32 || label->h != 16) {
        return "label geometry";
    }
    label_set_pos(label, 0, 0);
    surface_clear(surface, 0);
    label_render(label, surface);
    if (!surface_has_ink(surface, 32, 16)) {
        return "label render";
    }

    button = button_create("OK", 0);
    if (!button) {
        return "button create";
    }
    if (button->w != 32 || button->h != 24) {
        return "button geometry";
    }
    button_render(button, surface);
    button_click(button);

    input = textinput_create(16);
    if (!input) {
        return "textinput create";
    }
    textinput_set_text(input, "7");
    if (strlen(input->text) != 1 || input->text[0] != '7') {
        return "textinput set";
    }
    textinput_render(input, surface);

    box = container_create();
    if (!box) {
        return "container create";
    }
    container_add(box, label);
    if (box->count != 1 || box->children[0] != (void *)label) {
        return "container add";
    }
    container_render(box, surface);

    widget_draw((Widget *)button, surface);

    surface_destroy(surface);
    return 0;
}

static const char *test_theme(void) {
    Theme *theme;
    Label *label;
    Label *child;
    Button *button;
    TextInput *input;
    Container *box;

    theme = theme_default();
    if (!theme) {
        return "theme default";
    }
    if (theme->primary == 0 || theme->fg == 0 || theme->bg == 0 ||
        theme->border == 0 || theme->accent == 0) {
        return "theme colors";
    }
    if (theme->spacing_xs <= 0 || theme->spacing_xs >= theme->spacing_md ||
        theme->spacing_md >= theme->spacing_xl) {
        return "theme spacing";
    }
    if (theme->font_size <= 0) {
        return "theme font";
    }

    label = label_create("Theme", 0x00000001u);
    if (!label) {
        return "theme label";
    }
    theme_apply(theme, label);
    if (label->color != theme->fg || label->fg != theme->fg) {
        return "theme apply label";
    }

    button = button_create("Go", 0);
    if (!button) {
        return "theme button";
    }
    theme_apply(theme, button);
    if (button->bg_color != theme->primary || button->color != theme->fg) {
        return "theme apply button";
    }

    input = textinput_create(8);
    if (!input) {
        return "theme textinput";
    }
    theme_apply(theme, input);
    if (input->bg_color != theme->secondary || input->color != theme->fg) {
        return "theme apply textinput";
    }

    box = container_create();
    child = label_create("x", 0x00000002u);
    if (!box || !child) {
        return "theme container";
    }
    container_add(box, child);
    if (theme_apply(theme, box) != 0) {
        return "theme apply container";
    }
    if (box->bg_color != theme->bg || child->color != theme->fg) {
        return "theme apply subtree";
    }
    return 0;
}

static const char *test_layout(void) {
    LayoutContainer *row;
    LayoutContainer *column;
    LayoutItem first;
    LayoutItem second;
    LayoutItem top;
    LayoutItem bottom;

    row = layout_create(LAYOUT_HORIZONTAL);
    if (!row) {
        return "layout row create";
    }
    row->w = 320;
    row->h = 100;
    row->width = 320;
    row->height = 100;
    layout_set_padding(row, 8);
    layout_set_spacing(row, 4);

    memset(&first, 0, sizeof(first));
    first.w = 50;
    first.width = 50;
    first.h = 20;
    first.height = 20;
    memset(&second, 0, sizeof(second));
    second.w = 100;
    second.width = 100;
    second.h = 20;
    second.height = 20;
    row->grow[0] = 1;

    layout_add(row, &first);
    layout_add(row, &second);
    if (row->count != 2) {
        return "layout add";
    }
    layout_compute(row);
    if (first.x != 8 || first.y != 8 || first.w != 200 || first.h != 20) {
        return "layout row first";
    }
    if (second.x != 212 || second.y != 8 || second.w != 100 || second.h != 20) {
        return "layout row second";
    }

    column = layout_create(LAYOUT_VERTICAL);
    if (!column) {
        return "layout column create";
    }
    column->w = 120;
    column->h = 200;
    column->width = 120;
    column->height = 200;
    layout_set_padding(column, 4);
    layout_set_gap(column, 2);

    memset(&top, 0, sizeof(top));
    top.w = 50;
    top.width = 50;
    top.h = 20;
    top.height = 20;
    memset(&bottom, 0, sizeof(bottom));
    bottom.w = 50;
    bottom.width = 50;
    bottom.h = 30;
    bottom.height = 30;

    layout_append(column, &top);
    layout_append(column, &bottom);
    compute_layout(column);
    if (top.x != 4 || top.y != 4 || top.h != 20) {
        return "layout column first";
    }
    if (bottom.x != 4 || bottom.y != 26 || bottom.h != 30) {
        return "layout column second";
    }
    return 0;
}

static const char *test_taskbar(void) {
    Surface *surface;
    const ThemeEffects *fx;
    int bar_y;
    uint32_t glass_pixel;
    uint32_t tile_pixel;
    uint32_t clock_pixel;

    surface = surface_create(TEST_SURFACE_W, 120);
    if (!surface) {
        return "taskbar surface";
    }
    surface_clear(surface, 0);

    taskbar_init();
    taskbar_add_app("Files");
    taskbar_add_app("Shell");
    taskbar_clock_update();
    taskbar_draw(surface);

    /* Over a uniform backdrop the blur is the identity, so the glass is
       exactly the tinted backdrop, lit by the highlight on its top edge. */
    fx = &theme_default()->effects;
    bar_y = 120 - TASKBAR_HEIGHT;
    glass_pixel = gfx_mix(0, fx->glass_tint, fx->glass_tint_alpha);
    if (surface->pixels[(bar_y + 20) * surface->stride + 2] != glass_pixel) {
        return "taskbar glass";
    }
    if (surface->pixels[bar_y * surface->stride + 2] !=
        gfx_mix(glass_pixel, fx->glass_highlight, fx->glass_highlight_alpha)) {
        return "taskbar highlight";
    }
    if (surface->pixels[(bar_y - 1) * surface->stride] != 0) {
        return "taskbar bounds";
    }
    tile_pixel = surface->pixels[(bar_y + 20) * surface->stride + 12];
    if (tile_pixel == 0 || tile_pixel == glass_pixel) {
        return "taskbar app tile";
    }
    /* The pill spans at least x=238..313 even for the shortest label ("up 00:00"). */
    clock_pixel = surface->pixels[(bar_y + 8) * surface->stride + 266];
    if (clock_pixel != gfx_mix(glass_pixel, fx->glass_highlight, fx->glass_highlight_alpha / 2)) {
        return "taskbar clock";
    }

    surface_destroy(surface);
    return 0;
}

static const char *test_launcher(void) {
    Surface *surface;
    Launcher *launcher;
    const ThemeEffects *fx;
    const char *selected_path;
    int selected;

    surface = surface_create(TEST_SURFACE_W, TEST_SURFACE_H);
    if (!surface) {
        return "launcher surface";
    }
    surface_clear(surface, 0x00808080u);

    launcher_init();
    launcher = launcher_get();
    if (!launcher) {
        return "launcher get";
    }
    if (launcher->app_count <= 0 || launcher->filtered_count != launcher->app_count) {
        return "launcher apps";
    }

    launcher_set_pos(launcher, 10, 10);
    launcher_set_size(launcher, 240, 160);
    launcher_show();
    if (!launcher_is_visible(launcher)) {
        return "launcher show";
    }
    launcher_draw_2((uintptr_t)launcher, (uintptr_t)surface);
    /* Glass panel at (10, 10) 240x160 over a uniform backdrop: tinted glass
       inside, a rounded corner, a shadow below and nothing far away. */
    fx = &theme_default()->effects;
    if (surface->pixels[110 * surface->stride + 12] !=
        gfx_mix(0x00808080u, launcher->bg_color, fx->glass_tint_alpha)) {
        return "launcher glass";
    }
    if (surface->pixels[10 * surface->stride + 10] == surface->pixels[110 * surface->stride + 12]) {
        return "launcher corner";
    }
    if ((surface->pixels[172 * surface->stride + 130] & 0xFFu) >= 0x80u) {
        return "launcher shadow";
    }
    if (surface->pixels[195 * surface->stride + 315] != 0x00808080u) {
        return "launcher bounds";
    }

    launcher_handle_key('s');
    if (launcher->query[0] != 's') {
        return "launcher query";
    }
    if (launcher->filtered_count <= 0 ||
        launcher->filtered_count >= launcher->app_count) {
        return "launcher filter";
    }

    selected = launcher_handle_key('\n');
    if (selected < 0) {
        return "launcher select";
    }
    selected_path = launcher_selected_path(launcher);
    if (!selected_path || selected_path[0] != '/') {
        return "launcher selected path";
    }

    launcher_hide();
    if (launcher_is_visible(launcher)) {
        return "launcher hide";
    }

    surface_destroy(surface);
    return 0;
}

static const char *test_filemanager(void) {
    Surface *surface;
    const char *path;
    const FmEntry *entry;
    int index;

    surface = surface_create(TEST_SURFACE_W, TEST_SURFACE_H);
    if (!surface) {
        return "filemanager surface";
    }

    if (fm_init(".") != 0) {
        return "filemanager init";
    }
    if (fm_get_count() <= 0) {
        return "filemanager entries";
    }
    path = fm_get_path();
    if (!path || path[0] == '\0') {
        return "filemanager path";
    }
    entry = fm_get_entry(0);
    if (!entry || entry->name[0] == '\0') {
        return "filemanager entry";
    }
    if (fm_get_entry(-1) != 0 || fm_get_entry(fm_get_count()) != 0) {
        return "filemanager bounds";
    }

    if (fm_draw(surface) != 0) {
        return "filemanager draw";
    }
    if (surface->pixels[0] != 0xFF1E1E2Eu) {
        return "filemanager background";
    }

    /* First icon cell starts at (8, 24); clicking it selects entry 0. */
    index = fm_handle_click(9, 25);
    if (index != 0) {
        return "filemanager click";
    }
    if (!fm_get_selected_name()) {
        return "filemanager selected";
    }

    surface_destroy(surface);
    return 0;
}

/* Deterministic, non-uniform input for the material primitives. */
static uint32_t fx_pattern_pixel(int x, int y) {
    uint32_t r = (uint32_t)(x * 37 + y * 91) & 0xFFu;
    uint32_t g = (uint32_t)(x * y * 13) & 0xFFu;
    uint32_t b = (uint32_t)((x ^ y) * 29) & 0xFFu;
    return (r << 16) | (g << 8) | b;
}

static void fx_fill_pattern(uint32_t *pixels, int w, int h) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            pixels[y * w + x] = fx_pattern_pixel(x, y);
}

static void fx_fill_solid(uint32_t *pixels, int count, uint32_t color) {
    for (int i = 0; i < count; ++i) pixels[i] = color;
}

#define FX_BLUR_W 16
#define FX_BLUR_H 12
#define FX_ROUND_W 24
#define FX_ROUND_H 16
#define FX_SHADOW_W 40
#define FX_SHADOW_H 32
#define FX_GLASS_W 32
#define FX_GLASS_H 24

/* Reference checksums of the primitives' exact integer output. */
#define FX_SUM_BLUR        0x67797633u
#define FX_SUM_ROUND       0xF45FD7FDu
#define FX_SUM_SHADOW_MASK 0x3FB639B6u
#define FX_SUM_SHADOW      0xB57B8775u
#define FX_SUM_GLASS       0x54492FEDu

static const char *test_effects(void) {
    static uint32_t pixels[FX_SHADOW_W * FX_SHADOW_H];
    uint32_t line[FX_SHADOW_W];
    GfxCanvas canvas;

    if (gfx_mix(0x11000000u, 0x00FFFFFFu, 255) != 0x11FFFFFFu ||
        gfx_mix(0x00000000u, 0x00FFFFFFu, 128) != 0x00808080u ||
        gfx_mix(0x00336699u, 0x00FFFFFFu, 0) != 0x00336699u) {
        return "effects mix";
    }

    fx_fill_solid(pixels, FX_BLUR_W * FX_BLUR_H, 0x80406080u);
    gfx_blur(pixels, FX_BLUR_W, FX_BLUR_H, FX_BLUR_W, 3, line);
    for (int i = 0; i < FX_BLUR_W * FX_BLUR_H; ++i) {
        if (pixels[i] != 0x80406080u) {
            return "effects blur uniform";
        }
    }
    fx_fill_pattern(pixels, FX_BLUR_W, FX_BLUR_H);
    gfx_blur(pixels, FX_BLUR_W, FX_BLUR_H, FX_BLUR_W, 2, line);
    if (gfx_checksum(pixels, FX_BLUR_W, FX_BLUR_H, FX_BLUR_W) != FX_SUM_BLUR) {
        return "effects blur checksum";
    }

    canvas.pixels = pixels;
    canvas.width = FX_ROUND_W;
    canvas.height = FX_ROUND_H;
    canvas.stride = FX_ROUND_W;
    canvas.has_alpha = 0;
    fx_fill_solid(pixels, FX_ROUND_W * FX_ROUND_H, 0x00102030u);
    gfx_fill_round_rect(&canvas, 2, 2, 20, 12, 5, 0x00F0E0D0u, 255);
    if (pixels[2 * FX_ROUND_W + 2] != 0x00102030u ||
        pixels[8 * FX_ROUND_W + 12] != 0x00F0E0D0u) {
        return "effects round coverage";
    }
    for (int y = 2; y < 14; ++y) {
        for (int x = 2; x < 22; ++x) {
            uint32_t p = pixels[y * FX_ROUND_W + x];
            if (p != pixels[y * FX_ROUND_W + (23 - x)] ||
                p != pixels[(15 - y) * FX_ROUND_W + x]) {
                return "effects round symmetry";
            }
        }
    }
    gfx_stroke_round_rect(&canvas, 2, 2, 20, 12, 5, 0x00FFFFFFu, 128);
    if (gfx_checksum(pixels, FX_ROUND_W, FX_ROUND_H, FX_ROUND_W) != FX_SUM_ROUND) {
        return "effects round checksum";
    }

    const GfxShadow *shadow = gfx_shadow_get(4, 2);
    if (!shadow || shadow->extent != 6 || shadow->size != 2 * (4 + 12) + 1) {
        return "effects shadow template";
    }
    if (gfx_shadow_get(4, 2) != shadow || gfx_shadow_get(4, 0) != 0) {
        return "effects shadow cache";
    }
    if (gfx_checksum_a8(shadow->mask, shadow->size, shadow->size, shadow->size) != FX_SUM_SHADOW_MASK) {
        return "effects shadow mask checksum";
    }
    canvas.width = FX_SHADOW_W;
    canvas.height = FX_SHADOW_H;
    canvas.stride = FX_SHADOW_W;
    fx_fill_solid(pixels, FX_SHADOW_W * FX_SHADOW_H, 0x00C0C0C0u);
    gfx_draw_shadow(&canvas, shadow, 8, 6, 24, 16, 0, 2, 0x00000000u, 160, 0);
    /* The shape is knocked out; the shadow darkens towards the shape. */
    if (pixels[14 * FX_SHADOW_W + 20] != 0x00C0C0C0u ||
        pixels[0] != 0x00C0C0C0u) {
        return "effects shadow knockout";
    }
    for (int y = 23; y < 30; ++y) {
        if ((pixels[y * FX_SHADOW_W + 20] & 0xFFu) > (pixels[(y + 1) * FX_SHADOW_W + 20] & 0xFFu)) {
            return "effects shadow falloff";
        }
    }
    if ((pixels[22 * FX_SHADOW_W + 20] & 0xFFu) >= 0xC0u) {
        return "effects shadow visible";
    }
    if (gfx_checksum(pixels, FX_SHADOW_W, FX_SHADOW_H, FX_SHADOW_W) != FX_SUM_SHADOW) {
        return "effects shadow checksum";
    }

    GfxGlass glass = { 2, 4, 0x00202830u, 140, 0x00FFFFFFu, 60 };
    canvas.width = FX_GLASS_W;
    canvas.height = FX_GLASS_H;
    canvas.stride = FX_GLASS_W;
    fx_fill_pattern(pixels, FX_GLASS_W, FX_GLASS_H);
    if (gfx_glass_apply(&canvas, 4, 4, 24, 14, &glass) != 0) {
        return "effects glass scratch";
    }
    if (pixels[1 * FX_GLASS_W + 1] != fx_pattern_pixel(1, 1) ||
        pixels[20 * FX_GLASS_W + 30] != fx_pattern_pixel(30, 20)) {
        return "effects glass bounds";
    }
    if (gfx_checksum(pixels, FX_GLASS_W, FX_GLASS_H, FX_GLASS_W) != FX_SUM_GLASS) {
        return "effects glass checksum";
    }
    return 0;
}

#define SCENE_PIXELS (TEST_SURFACE_W * TEST_SURFACE_H)

static void scene_fill(Window *win, int x, int y, int w, int h, uint32_t color) {
    uint32_t *px = (uint32_t *)win->surface;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            px[yy * win->w + xx] = color;
}

/* Renders the pending damage, then a full frame, and checks both agree: the
   damage the compositor derived (shadows, glass spread) covered every pixel
   that changed. Both buffers hold the previous full frame beforehand. */
static const char *scene_check(uint32_t *saved, const char *what) {
    if (comp_render() != 0) {
        return what;
    }
    memcpy(saved, comp_front_buffer(), SCENE_PIXELS * sizeof(uint32_t));
    for (int pass = 0; pass < 2; ++pass) {
        comp_damage_all();
        if (comp_render() != 0) {
            return what;
        }
    }
    const uint32_t *front = comp_front_buffer();
    for (int i = 0; i < SCENE_PIXELS; ++i) {
        if (saved[i] != front[i]) {
            return what;
        }
    }
    return 0;
}

static const char *test_compositor_effects(void) {
    Window *bg;
    Window *card;
    Window *glass;
    CompWindowStyle style;
    uint32_t *saved;
    const uint32_t *front;
    const char *failure;

    saved = (uint32_t *)malloc(SCENE_PIXELS * sizeof(uint32_t));
    if (!saved) {
        return "scene memory";
    }
    if (quiet_stdout_begin() != 0) {
        return "stdout guard";
    }
    if (comp_init_size(TEST_SURFACE_W, TEST_SURFACE_H) != 0) {
        quiet_stdout_end();
        return "scene init";
    }
    if (quiet_stdout_end() != 0) {
        return "stdout restore";
    }

    bg = comp_create_window(0, 0, TEST_SURFACE_W, TEST_SURFACE_H);
    card = comp_create_window(40, 30, 120, 80);
    glass = comp_create_window(0, TEST_SURFACE_H - 40, TEST_SURFACE_W, 40);
    if (!bg || !card || !glass) {
        return "scene windows";
    }
    for (int y = 0; y < TEST_SURFACE_H; ++y)
        for (int x = 0; x < TEST_SURFACE_W; ++x)
            ((uint32_t *)bg->surface)[y * TEST_SURFACE_W + x] = fx_pattern_pixel(x, y);
    scene_fill(card, 0, 0, 120, 80, 0x00E8ECF0u);
    scene_fill(glass, 0, 0, TEST_SURFACE_W, 40, 0x00000000u);
    scene_fill(glass, 10, 10, 20, 20, 0xFFFF8040u);

    comp_style_window(&style);
    if (comp_set_window_style(card, &style) != 0) {
        return "scene card style";
    }
    comp_style_glass(&style);
    if (comp_set_window_style(glass, &style) != 0) {
        return "scene glass style";
    }
    style.material = 7;
    if (comp_set_window_style(glass, &style) == 0) {
        return "scene style validation";
    }

    for (int pass = 0; pass < 2; ++pass) {
        comp_damage_all();
        if (comp_render() != 0) {
            return "scene render";
        }
    }
    front = comp_front_buffer();
    /* Rounded corner shows the backdrop, the card body is opaque, glass is
       neither its tint nor the raw backdrop, opaque glass content wins. */
    if (front[30 * TEST_SURFACE_W + 40] == 0x00E8ECF0u ||
        front[70 * TEST_SURFACE_W + 100] != 0x00E8ECF0u) {
        return "scene rounded card";
    }
    if (front[(TEST_SURFACE_H - 20) * TEST_SURFACE_W + 200] ==
        fx_pattern_pixel(200, TEST_SURFACE_H - 20)) {
        return "scene glass material";
    }
    if ((front[(TEST_SURFACE_H - 20) * TEST_SURFACE_W + 20] & 0x00FFFFFFu) != 0x00FF8040u) {
        return "scene glass content";
    }
    if (front[115 * TEST_SURFACE_W + 100] == fx_pattern_pixel(100, 115)) {
        return "scene card shadow";
    }

    /* Backdrop change under the glass, damaged only where it was drawn. */
    scene_fill(bg, 100, TEST_SURFACE_H - 30, 30, 10, 0x00FFFFFFu);
    comp_damage_rect(bg, 100, TEST_SURFACE_H - 30, 30, 10);
    failure = scene_check(saved, "scene glass backdrop damage");
    if (failure) {
        return failure;
    }

    /* Move the card the way the desktop does: damage old and new rect only. */
    comp_damage_rect(bg, card->x, card->y, card->w, card->h);
    card->x += 7;
    card->y += 45;
    comp_damage_rect(bg, card->x, card->y, card->w, card->h);
    failure = scene_check(saved, "scene card move damage");
    if (failure) {
        return failure;
    }

    /* Damage over the glass without a change (a cursor) keeps the frame. */
    memcpy(saved, comp_front_buffer(), SCENE_PIXELS * sizeof(uint32_t));
    comp_damage_rect(bg, 150, TEST_SURFACE_H - 25, 14, 17);
    if (comp_render() != 0) {
        return "scene cursor render";
    }
    front = comp_front_buffer();
    for (int i = 0; i < SCENE_PIXELS; ++i) {
        if (front[i] != saved[i]) {
            return "scene cursor damage";
        }
    }

    /* Hiding, raising and resizing need no explicit damage at all. */
    card->visible = 0;
    failure = scene_check(saved, "scene hide damage");
    if (failure) {
        return failure;
    }
    card->visible = 1;
    if (comp_raise(card) != 0) {
        return "scene raise";
    }
    failure = scene_check(saved, "scene raise damage");
    if (failure) {
        return failure;
    }
    /* Raising the glass over the card turns the card into backdrop, also
       in the blur margin outside the glass where nothing was damaged. */
    if (comp_raise(glass) != 0) {
        return "scene raise glass";
    }
    failure = scene_check(saved, "scene glass raise damage");
    if (failure) {
        return failure;
    }
    if (comp_resize_window(card, 200, 120) != 0) {
        return "scene resize";
    }
    scene_fill(card, 0, 0, 200, 120, 0x00203040u);
    failure = scene_check(saved, "scene resize damage");
    if (failure) {
        return failure;
    }

    if (comp_destroy_window(glass) != 0 || comp_destroy_window(card) != 0 ||
        comp_destroy_window(bg) != 0 || comp_window_count() != 0) {
        return "scene destroy";
    }
    return 0;
}

/* ---------------- window manager ---------------- */

static int rect_is(CompRect r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static const char *test_window_geometry(void) {
    CompRect work = wm_work_area(1280, 720, 24, 56);
    int x;
    int y;

    if (!rect_is(work, 0, 24, 1280, 640) ||
        wm_work_area(100, 40, 30, 30).h != 0) {
        return "wm work area";
    }

    /* Title bar stays reachable: never above the work area, never below it,
       and WM_MIN_TITLE_VISIBLE pixels remain at either side. */
    work = wm_work_area(1000, 700, 0, 100);
    x = -5000; y = -5000;
    wm_clamp_position(&work, 400, 30, &x, &y);
    if (x != WM_MIN_TITLE_VISIBLE - 400 || y != 0) {
        return "wm clamp top left";
    }
    x = 5000; y = 5000;
    wm_clamp_position(&work, 400, 30, &x, &y);
    if (x != 1000 - WM_MIN_TITLE_VISIBLE || y != 600 - 30) {
        return "wm clamp bottom right";
    }
    x = 300; y = 200;
    wm_clamp_position(&work, 400, 30, &x, &y);
    if (x != 300 || y != 200) {
        return "wm clamp inside";
    }
    x = -100; y = 0;
    wm_clamp_position(&work, 40, 30, &x, &y);
    if (x != 0) {
        return "wm clamp narrow window";
    }
    work = wm_work_area(1000, 700, 40, 100);
    x = 0; y = 0;
    wm_clamp_position(&work, 400, 30, &x, &y);
    if (y != 40) {
        return "wm clamp below top bar";
    }

    if (wm_dock_action(0, 0, 0) != WM_DOCK_LAUNCH ||
        wm_dock_action(1, 1, 0) != WM_DOCK_RESTORE ||
        wm_dock_action(1, 1, 1) != WM_DOCK_RESTORE ||
        wm_dock_action(1, 0, 0) != WM_DOCK_RAISE ||
        wm_dock_action(1, 0, 1) != WM_DOCK_MINIMIZE) {
        return "wm dock action";
    }

    if (!wm_is_double_click(1000, 50, 60, 1000 + WM_DOUBLE_CLICK_MS, 52, 58) ||
        wm_is_double_click(1000, 50, 60, 1001 + WM_DOUBLE_CLICK_MS, 50, 60) ||
        wm_is_double_click(1000, 50, 60, 1100, 50 + WM_DOUBLE_CLICK_SLOP + 1, 60)) {
        return "wm double click";
    }
    return 0;
}

/* Renders the pending damage and checks that both buffers then hold the
   complete frame a from-scratch repaint produces. */
static const char *frame_check_buffers(uint32_t *front_copy, uint32_t *back_copy, const char *what) {
    if (comp_render() != 0) {
        return what;
    }
    memcpy(front_copy, comp_front_buffer(), SCENE_PIXELS * sizeof(uint32_t));
    memcpy(back_copy, comp_back_buffer(), SCENE_PIXELS * sizeof(uint32_t));
    comp_damage_all();
    if (comp_render() != 0) {
        return what;
    }
    const uint32_t *full = comp_front_buffer();
    for (int i = 0; i < SCENE_PIXELS; ++i) {
        if (front_copy[i] != full[i] || back_copy[i] != full[i]) {
            return what;
        }
    }
    return 0;
}

static int min_int(int a, int b) {
    return a < b ? a : b;
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static const char *test_window_damage(void) {
    const ThemeEffects *fx = &theme_default()->effects;
    const GfxShadow *shadow = gfx_shadow_get(fx->window_radius, fx->shadow_blur_radius);
    Window *bg;
    Window *card;
    CompWindowStyle style;
    uint32_t *front_copy;
    uint32_t *back_copy;
    const char *failure;

    front_copy = (uint32_t *)malloc(SCENE_PIXELS * sizeof(uint32_t));
    back_copy = (uint32_t *)malloc(SCENE_PIXELS * sizeof(uint32_t));
    if (!front_copy || !back_copy || !shadow) {
        return "window damage memory";
    }
    if (quiet_stdout_begin() != 0) {
        return "stdout guard";
    }
    if (comp_init_size(TEST_SURFACE_W, TEST_SURFACE_H) != 0) {
        quiet_stdout_end();
        return "window damage init";
    }
    if (quiet_stdout_end() != 0) {
        return "stdout restore";
    }
    bg = comp_create_window(0, 0, TEST_SURFACE_W, TEST_SURFACE_H);
    card = comp_create_window(90, 40, 120, 80);
    if (!bg || !card) {
        return "window damage windows";
    }
    for (int y = 0; y < TEST_SURFACE_H; ++y)
        for (int x = 0; x < TEST_SURFACE_W; ++x)
            ((uint32_t *)bg->surface)[y * TEST_SURFACE_W + x] = fx_pattern_pixel(x, y);
    scene_fill(card, 0, 0, 120, 80, 0x00E8ECF0u);

    /* The extent is the window plus the shadow the theme tokens cast. */
    if (!rect_is(comp_window_extent(card), 90, 40, 120, 80)) {
        return "window extent plain";
    }
    comp_style_window(&style);
    style.corner_radius = fx->window_radius;
    if (comp_set_window_style(card, &style) != 0) {
        return "window extent style";
    }
    int e = shadow->extent;
    int left = min_int(90, 90 + fx->shadow_offset_x - e);
    int top = min_int(40, 40 + fx->shadow_offset_y - e);
    int right = max_int(90 + 120, 90 + 120 + fx->shadow_offset_x + e);
    int bottom = max_int(40 + 80, 40 + 80 + fx->shadow_offset_y + e);
    if (e <= 0 || !rect_is(comp_window_extent(card), left, top, right - left, bottom - top)) {
        return "window extent shadow";
    }
    style.shadow = 0;
    if (comp_set_window_style(card, &style) != 0 ||
        !rect_is(comp_window_extent(card), 90, 40, 120, 80)) {
        return "window extent no shadow";
    }
    style.shadow = 1;
    if (comp_set_window_style(card, &style) != 0) {
        return "window extent restyle";
    }

    comp_damage_all();
    failure = frame_check_buffers(front_copy, back_copy, "window damage baseline");
    if (failure) {
        return failure;
    }

    /* Moving, hiding, showing, resizing and destroying need no damage from
       the caller: the corners of the shadow are repainted too. */
    card->x += 33;
    card->y += 61;
    failure = frame_check_buffers(front_copy, back_copy, "window damage move");
    if (failure) {
        return failure;
    }
    card->visible = 0;
    failure = frame_check_buffers(front_copy, back_copy, "window damage hide");
    if (failure) {
        return failure;
    }
    card->visible = 1;
    failure = frame_check_buffers(front_copy, back_copy, "window damage show");
    if (failure) {
        return failure;
    }
    if (comp_resize_window(card, 60, 50) != 0) {
        return "window damage resize";
    }
    scene_fill(card, 0, 0, 60, 50, 0x00203040u);
    failure = frame_check_buffers(front_copy, back_copy, "window damage resize");
    if (failure) {
        return failure;
    }

    /* Consecutive frames over different areas leave no stale pixels behind in
       either buffer, which presenting a merged damage box relies on. */
    scene_fill(bg, 5, 5, 30, 30, 0x00FF0000u);
    comp_damage_rect(bg, 5, 5, 30, 30);
    failure = frame_check_buffers(front_copy, back_copy, "window damage first frame");
    if (failure) {
        return failure;
    }
    scene_fill(bg, 250, 150, 40, 30, 0x0000FF00u);
    comp_damage_rect(bg, 250, 150, 40, 30);
    failure = frame_check_buffers(front_copy, back_copy, "window damage second frame");
    if (failure) {
        return failure;
    }

    if (comp_destroy_window(card) != 0) {
        return "window damage destroy";
    }
    failure = frame_check_buffers(front_copy, back_copy, "window damage destroyed");
    if (failure) {
        return failure;
    }
    if (comp_destroy_window(bg) != 0 || comp_window_count() != 0) {
        return "window damage cleanup";
    }
    free(front_copy);
    free(back_copy);
    return 0;
}

/* ---------------- settings model, palette and Settings app ---------------- */

#define SETTINGS_TEST_PATH "/tmp/settings_test.conf"
#define SETTINGS_TEST_CANONICAL \
    "# HydrogenOS desktop settings\ntheme=dark\naccent=blue\nglass=on\nlayout=de\n"

static int text_equals(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int settings_equal(const Settings *a, const Settings *b) {
    return a->theme == b->theme && a->accent == b->accent &&
           a->glass == b->glass && a->layout == b->layout;
}

static int luma(uint32_t rgb) {
    return (int)((299u * ((rgb >> 16) & 0xFFu) + 587u * ((rgb >> 8) & 0xFFu) +
                  114u * (rgb & 0xFFu)) / 1000u);
}

static int luma_distance(uint32_t a, uint32_t b) {
    int distance = luma(a) - luma(b);
    return distance < 0 ? -distance : distance;
}

static const char *test_settings_model(void) {
    Settings settings;
    Settings parsed;
    char text[SETTINGS_TEXT_CAP];
    int written;

    settings_defaults(&settings);
    if (!settings_valid(&settings) || settings.theme != SETTINGS_THEME_DARK ||
        settings.glass != 1 || settings.layout != SETTINGS_LAYOUT_DE) {
        return "settings defaults";
    }
    written = settings_serialize(&settings, text, sizeof(text));
    if (written != (int)strlen(SETTINGS_TEST_CANONICAL) ||
        !text_equals(text, SETTINGS_TEST_CANONICAL)) {
        return "settings serialize";
    }

    for (int theme = 0; theme < 2; ++theme) {
        for (int accent = 0; accent < SETTINGS_ACCENT_COUNT; ++accent) {
            for (int glass = 0; glass < 2; ++glass) {
                for (int layout = 0; layout < 2; ++layout) {
                    settings.theme = theme;
                    settings.accent = accent;
                    settings.glass = glass;
                    settings.layout = layout;
                    written = settings_serialize(&settings, text, sizeof(text));
                    if (written <= 0 || written >= (int)sizeof(text)) {
                        return "settings serialize combo";
                    }
                    /* Start from the opposite of every field so a missing
                       assignment cannot pass as a match. */
                    parsed.theme = !theme;
                    parsed.accent = (accent + 1) % SETTINGS_ACCENT_COUNT;
                    parsed.glass = !glass;
                    parsed.layout = !layout;
                    if (settings_parse(&parsed, text, (size_t)written) != 4 ||
                        !settings_equal(&settings, &parsed)) {
                        return "settings round trip";
                    }
                }
            }
        }
    }

    settings.theme = SETTINGS_THEME_LIGHT;
    settings.accent = 2;
    settings.glass = 0;
    settings.layout = SETTINGS_LAYOUT_US;
    written = settings_serialize(&settings, text, sizeof(text));
    if (written != (int)strlen(text) || settings_serialize(&settings, text, (size_t)written) != -1 ||
        settings_serialize(&settings, text, (size_t)written + 1) != written) {
        return "settings serialize bounds";
    }
    settings.accent = SETTINGS_ACCENT_COUNT;
    if (settings_valid(&settings) || settings_serialize(&settings, text, sizeof(text)) != -1) {
        return "settings serialize invalid";
    }
    return 0;
}

static const char *test_settings_parse(void) {
    static const char messy[] =
        "  THEME = Light \r\n# comment\n\nunknown=1\naccent=nonexistent\n"
        "no equals sign\naccent=PINK\nglass=maybe\n\tlayout=us";
    static const char duplicate[] = "theme=light\ntheme=dark\n";
    Settings settings;

    settings_defaults(&settings);
    if (settings_parse(&settings, messy, strlen(messy)) != 3) {
        return "settings parse count";
    }
    if (settings.theme != SETTINGS_THEME_LIGHT || settings.accent != 2 ||
        settings.glass != 1 || settings.layout != SETTINGS_LAYOUT_US) {
        return "settings parse tolerant";
    }
    settings_defaults(&settings);
    if (settings_parse(&settings, duplicate, strlen(duplicate)) != 2 ||
        settings.theme != SETTINGS_THEME_DARK) {
        return "settings parse last wins";
    }
    settings_defaults(&settings);
    if (settings_parse(&settings, "theme=light", 5) != 0 || settings.theme != SETTINGS_THEME_DARK) {
        return "settings parse truncated";
    }
    if (settings_parse(&settings, "", 0) != 0 || settings_parse(0, "x", 1) != -1 ||
        settings_parse(&settings, 0, 4) != -1) {
        return "settings parse arguments";
    }
    if (!settings_valid(&settings)) {
        return "settings parse stays valid";
    }
    return 0;
}

static const char *test_settings_diff(void) {
    Settings before;
    Settings after;

    settings_defaults(&before);
    after = before;
    if (settings_diff(&before, &after) != 0) {
        return "settings diff none";
    }
    after.theme = SETTINGS_THEME_LIGHT;
    after.accent = 3;
    if (settings_diff(&before, &after) != (SETTINGS_CHANGED_THEME | SETTINGS_CHANGED_ACCENT)) {
        return "settings diff theme accent";
    }
    after = before;
    after.glass = 0;
    after.layout = SETTINGS_LAYOUT_US;
    if (settings_diff(&before, &after) != (SETTINGS_CHANGED_GLASS | SETTINGS_CHANGED_LAYOUT)) {
        return "settings diff glass layout";
    }
    return 0;
}

static const char *test_settings_palette(void) {
    Settings settings;
    SettingsPalette dark;
    SettingsPalette light;
    SettingsPalette other;

    settings_defaults(&settings);
    settings_palette(&settings, &dark);
    settings.theme = SETTINGS_THEME_LIGHT;
    settings_palette(&settings, &light);

    if (dark.accent != settings_accent_color(0, SETTINGS_THEME_DARK) ||
        light.accent != settings_accent_color(0, SETTINGS_THEME_LIGHT)) {
        return "palette accent tokens";
    }
    if (luma(light.surface) <= luma(dark.surface) || luma(light.card) <= luma(dark.card) ||
        luma(light.wallpaper_top) <= luma(dark.wallpaper_top) ||
        luma(light.ink) >= luma(dark.ink) || luma(light.wallpaper_text) >= luma(dark.wallpaper_text)) {
        return "palette theme switch";
    }
    if (luma_distance(dark.ink, dark.surface) < 150 || luma_distance(light.ink, light.surface) < 150 ||
        luma_distance(dark.wallpaper_text, dark.wallpaper_bottom) < 100 ||
        luma_distance(light.wallpaper_text, light.wallpaper_bottom) < 100 ||
        luma_distance(dark.chrome_text, dark.taskbar) < 100 ||
        luma_distance(light.chrome_text, light.taskbar) < 100) {
        return "palette contrast";
    }
    if (luma(light.taskbar) <= luma(dark.taskbar) || luma(light.menu) <= luma(dark.menu) ||
        luma(light.chrome_text) >= luma(dark.chrome_text)) {
        return "palette chrome follows theme";
    }
    if (luma_distance(dark.chrome_icon, dark.taskbar) < 100 ||
        luma_distance(light.chrome_icon, light.taskbar) < 100 ||
        luma_distance(dark.chrome_icon, dark.menu) < 100 ||
        luma_distance(light.chrome_icon, light.menu) < 100) {
        return "palette chrome icon contrast";
    }
    if (luma(light.title) <= luma(dark.title) || luma(light.title_focus) <= luma(dark.title_focus) ||
        luma(light.title_text) >= luma(dark.title_text) ||
        luma(light.title_focus) < luma(light.title)) {
        return "palette title bar follows theme";
    }
    if (luma_distance(dark.title_text, dark.title) < 100 ||
        luma_distance(light.title_text, light.title) < 100 ||
        luma_distance(dark.title_text, dark.title_focus) < 100 ||
        luma_distance(light.title_text, light.title_focus) < 100 ||
        dark.title_border == dark.title || light.title_border == light.title) {
        return "palette title bar contrast";
    }
    if (dark.terminal_bg != light.terminal_bg || dark.document_bg != light.document_bg) {
        return "palette theme independent tokens";
    }

    for (int theme = 0; theme < 2; ++theme) {
        for (int accent = 0; accent < SETTINGS_ACCENT_COUNT; ++accent) {
            uint32_t color = settings_accent_color(accent, theme);

            settings.theme = theme;
            settings.accent = accent;
            settings_palette(&settings, &other);
            if (other.accent != color || luma_distance(other.on_accent, color) < 90) {
                return "palette accent contrast";
            }
            for (int later = accent + 1; later < SETTINGS_ACCENT_COUNT; ++later) {
                if (settings_accent_color(later, theme) == color) {
                    return "palette accent unique";
                }
            }
        }
    }
    if (settings_accent_name(0)[0] == '\0' || settings_accent_name(SETTINGS_ACCENT_COUNT)[0] != '\0') {
        return "palette accent names";
    }

    settings.theme = 9;
    settings_palette(&settings, &other);
    if (other.surface != dark.surface || other.accent != dark.accent) {
        return "palette invalid settings";
    }
    return 0;
}

static const char *test_settings_persistence(void) {
    Settings saved;
    Settings loaded;
    char text[SETTINGS_TEXT_CAP];
    long fd;
    long got;
    const char *failure = 0;

    settings_defaults(&loaded);
    if (settings_load("/tmp/settings_missing.conf", &loaded) >= 0) {
        return "settings load missing";
    }
    settings_defaults(&saved);
    if (!settings_equal(&saved, &loaded)) {
        return "settings load missing untouched";
    }

    saved.theme = SETTINGS_THEME_LIGHT;
    saved.accent = 4;
    saved.glass = 0;
    saved.layout = SETTINGS_LAYOUT_US;
    if (settings_save(SETTINGS_TEST_PATH, &saved) != 0) {
        return "settings save";
    }
    fd = open(SETTINGS_TEST_PATH, OPEN_READ);
    got = fd < 0 ? -1 : read((int)fd, text, sizeof(text) - 1);
    if (fd >= 0) {
        close((int)fd);
    }
    if (got <= 0) {
        failure = "settings file read";
    } else {
        text[got] = '\0';
        if (!text_equals(text, "# HydrogenOS desktop settings\ntheme=light\naccent=orange\n"
                               "glass=off\nlayout=us\n")) {
            failure = "settings file content";
        }
    }

    if (!failure) {
        settings_defaults(&loaded);
        if (settings_load(SETTINGS_TEST_PATH, &loaded) != 0 || !settings_equal(&saved, &loaded)) {
            failure = "settings load";
        }
    }
    if (!failure) {
        static const char damaged[] = "garbage\nlayout=us\naccent=\n";

        if (sys_writef(SETTINGS_TEST_PATH, damaged, sizeof(damaged) - 1) != 0) {
            failure = "settings damaged write";
        } else {
            settings_defaults(&loaded);
            if (settings_load(SETTINGS_TEST_PATH, &loaded) != 0 || loaded.layout != SETTINGS_LAYOUT_US ||
                loaded.accent != 0 || !settings_valid(&loaded)) {
                failure = "settings damaged load";
            }
        }
    }
    sys_unlink(SETTINGS_TEST_PATH);
    return failure;
}

static const char *test_settings_info(void) {
    static const char meminfo[] =
        "MemTotal:       262144 kB\nMemFree:        200000 kB\nMemUsed:         62144 kB\n"
        "PageSize:         4096 B\n";
    static const unsigned char mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    unsigned long total;
    unsigned long used;
    unsigned long seconds;
    SettingsInfo info;
    char text[SETTINGS_INFO_TEXT_CAP];
    char tiny[4];
    char version_line[3 * 100];
    size_t line_size = 0;

    memset(&info, 0, sizeof(info));
    if (settings_info_parse_meminfo(meminfo, strlen(meminfo), &total, &used) != 0 ||
        total != 262144ul || used != 62144ul) {
        return "info meminfo";
    }
    if (settings_info_parse_meminfo("MemTotal: 1 kB\n", 15, &total, &used) == 0 ||
        settings_info_parse_meminfo("MemTotal:\nMemUsed: 5 kB\n", 24, &total, &used) == 0) {
        return "info meminfo incomplete";
    }
    if (settings_info_parse_uptime("123.45\n", 7, &seconds) != 0 || seconds != 123ul ||
        settings_info_parse_uptime("0.05\n", 5, &seconds) != 0 || seconds != 0ul ||
        settings_info_parse_uptime("abc", 3, &seconds) == 0) {
        return "info uptime";
    }
    if (settings_info_parse_version("HydrogenOS 0.3.0-dev x86_64\n", 28, &info) != 0 ||
        !text_equals(info.os_name, "HydrogenOS") || !text_equals(info.os_version, "0.3.0-dev") ||
        !text_equals(info.os_arch, "x86_64")) {
        return "info version";
    }
    if (settings_info_parse_version("HydrogenOS\n", 11, &info) == 0) {
        return "info version incomplete";
    }
    for (int word = 0; word < 3; ++word) {
        memset(version_line + line_size, 'x', 99);
        line_size += 99;
        version_line[line_size++] = ' ';
    }
    if (settings_info_parse_version(version_line, line_size, &info) != 0 ||
        strlen(info.os_name) != SETTINGS_INFO_TEXT_CAP - 1 ||
        strlen(info.os_arch) != SETTINGS_INFO_TEXT_CAP - 1) {
        return "info version truncation";
    }

    settings_info_format_ipv4(text, sizeof(text), NET_IPV4_ADDR(10, 0, 2, 15));
    if (!text_equals(text, "10.0.2.15")) {
        return "info ipv4";
    }
    settings_info_format_mac(text, sizeof(text), mac);
    if (!text_equals(text, "52:54:00:12:34:56")) {
        return "info mac";
    }
    settings_info_format_size(text, sizeof(text), 512ul);
    if (!text_equals(text, "512 KB")) {
        return "info size kb";
    }
    settings_info_format_size(text, sizeof(text), 48433ul);
    if (!text_equals(text, "47.2 MB")) {
        return "info size mb";
    }
    settings_info_format_size(text, sizeof(text), 1572864ul);
    if (!text_equals(text, "1.5 GB")) {
        return "info size gb";
    }
    settings_info_format_duration(text, sizeof(text), 5ul);
    if (!text_equals(text, "5s")) {
        return "info duration seconds";
    }
    settings_info_format_duration(text, sizeof(text), 245ul);
    if (!text_equals(text, "4m 05s")) {
        return "info duration minutes";
    }
    settings_info_format_duration(text, sizeof(text), 7380ul);
    if (!text_equals(text, "2h 03m")) {
        return "info duration hours";
    }
    settings_info_format_duration(text, sizeof(text), 90000ul);
    if (!text_equals(text, "1d 01h")) {
        return "info duration days";
    }
    settings_info_format_ping(text, sizeof(text), 3);
    if (!text_equals(text, "Reply in 3 ms")) {
        return "info ping reply";
    }
    settings_info_format_ping(text, sizeof(text), SETTINGS_PING_ETIMEDOUT);
    if (!text_equals(text, "No reply (timed out)")) {
        return "info ping timeout";
    }
    settings_info_format_ping(text, sizeof(text), SETTINGS_PING_ENODEV);
    if (!text_equals(text, "No network adapter")) {
        return "info ping nodev";
    }
    settings_info_format_ping(text, sizeof(text), -5);
    if (!text_equals(text, "Failed (error 5)")) {
        return "info ping error";
    }
    settings_info_format_ipv4(tiny, sizeof(tiny), NET_IPV4_ADDR(255, 255, 255, 255));
    if (strlen(tiny) != sizeof(tiny) - 1) {
        return "info format truncation";
    }
    info.network_valid = 0;
    if (settings_info_ping_gateway(&info, 10) != SETTINGS_PING_ENODEV) {
        return "info ping without adapter";
    }
    return 0;
}

static int rect_inside(const GfxRect *outer, const GfxRect *inner) {
    return inner->w > 0 && inner->h > 0 && inner->x >= outer->x && inner->y >= outer->y &&
           inner->x + inner->w <= outer->x + outer->w && inner->y + inner->h <= outer->y + outer->h;
}

static int rects_overlap(const GfxRect *a, const GfxRect *b) {
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static int rects_disjoint(const GfxRect *rects, int rect_count) {
    for (int i = 0; i < rect_count; ++i) {
        for (int j = i + 1; j < rect_count; ++j) {
            if (rects_overlap(&rects[i], &rects[j])) {
                return 0;
            }
        }
    }
    return 1;
}

static int rect_center_hit(const SettingsUiLayout *layout, const GfxRect *rect) {
    return settings_ui_hit_test(layout, rect->x + rect->w / 2, rect->y + rect->h / 2);
}

static const char *check_section_layout(const SettingsUiLayout *layout, int section) {
    const GfxRect *content = &layout->content;

    if (!rect_inside(&layout->body, &layout->sidebar) || !rect_inside(&layout->body, content) ||
        layout->sidebar.x + layout->sidebar.w != content->x) {
        return "settings layout panes";
    }
    if (!rects_disjoint(layout->section_item, SETTINGS_SECTION_COUNT)) {
        return "settings layout sidebar overlap";
    }
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        if (!rect_inside(&layout->sidebar, &layout->section_item[i]) ||
            rect_center_hit(layout, &layout->section_item[i]) != SETTINGS_HIT_SECTION + i) {
            return "settings layout sidebar item";
        }
    }
    if (layout->card_count < 1 || layout->card_count > SETTINGS_UI_MAX_CARDS ||
        !rects_disjoint(layout->cards, layout->card_count)) {
        return "settings layout cards";
    }
    for (int i = 0; i < layout->card_count; ++i) {
        if (!rect_inside(content, &layout->cards[i])) {
            return "settings layout card bounds";
        }
    }

    switch (section) {
    case SETTINGS_SECTION_APPEARANCE:
        if (layout->card_count != 3 || !rects_disjoint(layout->theme_tile, 2) ||
            !rects_disjoint(layout->accent_dot, SETTINGS_ACCENT_COUNT)) {
            return "settings layout appearance";
        }
        for (int i = 0; i < 2; ++i) {
            if (!rect_inside(&layout->cards[0], &layout->theme_tile[i]) ||
                rect_center_hit(layout, &layout->theme_tile[i]) != SETTINGS_HIT_THEME + i) {
                return "settings layout theme tile";
            }
        }
        for (int i = 0; i < SETTINGS_ACCENT_COUNT; ++i) {
            if (!rect_inside(&layout->cards[1], &layout->accent_dot[i]) ||
                rect_center_hit(layout, &layout->accent_dot[i]) != SETTINGS_HIT_ACCENT + i) {
                return "settings layout accent dot";
            }
        }
        if (!rect_inside(&layout->cards[2], &layout->glass_switch) ||
            rect_center_hit(layout, &layout->glass_switch) != SETTINGS_HIT_GLASS) {
            return "settings layout glass switch";
        }
        break;
    case SETTINGS_SECTION_KEYBOARD:
        if (layout->card_count != 1 || !rects_disjoint(layout->layout_row, 2)) {
            return "settings layout keyboard";
        }
        for (int i = 0; i < 2; ++i) {
            if (!rect_inside(&layout->cards[0], &layout->layout_row[i]) ||
                rect_center_hit(layout, &layout->layout_row[i]) != SETTINGS_HIT_LAYOUT + i) {
                return "settings layout keyboard row";
            }
        }
        break;
    case SETTINGS_SECTION_NETWORK:
        if (layout->card_count != 2 || !rect_inside(&layout->cards[1], &layout->ping_button) ||
            rect_center_hit(layout, &layout->ping_button) != SETTINGS_HIT_PING) {
            return "settings layout network";
        }
        break;
    case SETTINGS_SECTION_ABOUT:
        if (layout->card_count != 1 || !rect_inside(content, &layout->logo) ||
            layout->logo.y + layout->logo.h > layout->cards[0].y) {
            return "settings layout about";
        }
        break;
    default:
        break;
    }

    if (settings_ui_hit_test(layout, content->x + content->w - 1, content->y + content->h - 1) !=
            SETTINGS_HIT_NONE ||
        settings_ui_hit_test(layout, -5, -5) != SETTINGS_HIT_NONE) {
        return "settings layout empty space";
    }
    return 0;
}

static const char *test_settings_layout(void) {
    static const int sizes[][2] = {
        { SETTINGS_UI_MIN_WIDTH, SETTINGS_UI_MIN_HEIGHT }, { 700, 480 }, { 1000, 700 },
    };
    SettingsUiLayout layout;
    const char *failure;

    for (int size = 0; size < 3; ++size) {
        for (int section = 0; section < SETTINGS_SECTION_COUNT; ++section) {
            settings_ui_layout(sizes[size][0], sizes[size][1], 30, section, &layout);
            failure = check_section_layout(&layout, section);
            if (failure) {
                return failure;
            }
            /* Everything must fit above the bottom edge, with room for a notice. */
            for (int card = 0; card < layout.card_count; ++card) {
                if (layout.cards[card].y + layout.cards[card].h > sizes[size][1] - 16) {
                    return "settings layout fits";
                }
            }
        }
    }

    /* A window far below the minimum must still produce sane rectangles. */
    settings_ui_layout(120, 60, 30, SETTINGS_SECTION_NETWORK, &layout);
    if (layout.sidebar.w < 0 || layout.content.w < 0 || layout.content.h < 0 ||
        layout.cards[0].w < 0 || layout.ping_button.w < 0) {
        return "settings layout tiny";
    }
    settings_ui_layout(700, 480, 30, SETTINGS_SECTION_COUNT, &layout);
    if (layout.card_count != 0) {
        return "settings layout unknown section";
    }
    return 0;
}

static const char *test_settings_ui_events(void) {
    Settings settings;
    SettingsPalette palette;
    SettingsUi ui;
    SettingsUiLayout layout;
    Window win;
    unsigned int result;

    settings_defaults(&settings);
    settings_palette(&settings, &palette);
    settings_ui_init(&ui, &settings, &palette, 0, 30);
    memset(&win, 0, sizeof(win));
    win.w = 700;
    win.h = 480;

    settings_ui_layout(win.w, win.h, 30, SETTINGS_SECTION_APPEARANCE, &layout);
    result = settings_ui_click(&ui, &win, layout.theme_tile[1].x + 5, layout.theme_tile[1].y + 5);
    if (result != (SETTINGS_UI_REDRAW | SETTINGS_UI_CHANGED) || settings.theme != SETTINGS_THEME_LIGHT) {
        return "settings ui theme click";
    }
    if (settings_ui_click(&ui, &win, layout.theme_tile[1].x + 5, layout.theme_tile[1].y + 5) != 0) {
        return "settings ui theme click repeat";
    }
    result = settings_ui_click(&ui, &win, layout.accent_dot[3].x + 14, layout.accent_dot[3].y + 14);
    if (!(result & SETTINGS_UI_CHANGED) || settings.accent != 3) {
        return "settings ui accent click";
    }
    result = settings_ui_click(&ui, &win, layout.cards[2].x + 20, layout.cards[2].y + 20);
    if (!(result & SETTINGS_UI_CHANGED) || settings.glass != 0) {
        return "settings ui glass off";
    }
    result = settings_ui_click(&ui, &win, layout.glass_switch.x + 3, layout.glass_switch.y + 3);
    if (!(result & SETTINGS_UI_CHANGED) || settings.glass != 1) {
        return "settings ui glass on";
    }
    if (settings_ui_click(&ui, &win, layout.content.x + layout.content.w - 2, layout.title_y) != 0) {
        return "settings ui empty click";
    }

    result = settings_ui_click(&ui, &win, layout.section_item[SETTINGS_SECTION_KEYBOARD].x + 4,
                               layout.section_item[SETTINGS_SECTION_KEYBOARD].y + 4);
    if (result != SETTINGS_UI_REDRAW || ui.section != SETTINGS_SECTION_KEYBOARD) {
        return "settings ui section click";
    }
    if (settings_ui_click(&ui, &win, layout.section_item[SETTINGS_SECTION_KEYBOARD].x + 4,
                          layout.section_item[SETTINGS_SECTION_KEYBOARD].y + 4) != 0) {
        return "settings ui section repeat";
    }
    settings_ui_layout(win.w, win.h, 30, SETTINGS_SECTION_KEYBOARD, &layout);
    result = settings_ui_click(&ui, &win, layout.layout_row[1].x + 10, layout.layout_row[1].y + 10);
    if (!(result & SETTINGS_UI_CHANGED) || settings.layout != SETTINGS_LAYOUT_US) {
        return "settings ui layout click";
    }
    if (settings_ui_click(&ui, &win, layout.layout_row[1].x + 10, layout.layout_row[1].y + 10) != 0) {
        return "settings ui layout repeat";
    }
    if (!settings_valid(&settings)) {
        return "settings ui keeps model valid";
    }
    /* An accent dot is not clickable on another page. */
    settings_ui_layout(win.w, win.h, 30, SETTINGS_SECTION_APPEARANCE, &layout);
    if (settings_ui_click(&ui, &win, layout.accent_dot[0].x + 14, layout.accent_dot[0].y + 14) != 0 ||
        settings.accent != 3) {
        return "settings ui foreign click";
    }

    result = settings_ui_hover(&ui, &win, layout.section_item[0].x + 4, layout.section_item[0].y + 4);
    if (result != SETTINGS_UI_REDRAW || ui.hover != SETTINGS_HIT_SECTION) {
        return "settings ui hover";
    }
    if (settings_ui_hover(&ui, &win, layout.section_item[0].x + 6, layout.section_item[0].y + 6) != 0) {
        return "settings ui hover repeat";
    }
    if (settings_ui_hover(&ui, &win, -1, -1) != SETTINGS_UI_REDRAW || ui.hover != SETTINGS_HIT_NONE) {
        return "settings ui hover leave";
    }

    ui.section = SETTINGS_SECTION_NETWORK;
    ui.info.network_valid = 0;
    settings_ui_layout(win.w, win.h, 30, SETTINGS_SECTION_NETWORK, &layout);
    if (settings_ui_click(&ui, &win, layout.ping_button.x + 5, layout.ping_button.y + 5) != 0 ||
        ui.ping_pending) {
        return "settings ui ping without adapter";
    }
    ui.section = SETTINGS_SECTION_APPEARANCE;
    if (settings_ui_service(&ui, ui.info_refreshed_ms + 60000ul) != 0) {
        return "settings ui service idle";
    }
    settings_ui_select(&ui, SETTINGS_SECTION_COUNT);
    if (ui.section != SETTINGS_SECTION_APPEARANCE) {
        return "settings ui select invalid";
    }
    return 0;
}

static void settings_test_info(SettingsInfo *info) {
    memset(info, 0, sizeof(*info));
    info->display_valid = 1;
    info->display_width = 1280;
    info->display_height = 720;
    info->display_bits_per_pixel = 32;
    info->network_valid = 1;
    info->network.mac[0] = 0x52;
    info->network.mac[1] = 0x54;
    info->network.mac[5] = 0x56;
    info->network.flags = NET_INFO_PRESENT | NET_INFO_LINK_UP;
    info->network.ipv4 = NET_IPV4_ADDR(10, 0, 2, 15);
    info->network.netmask = NET_IPV4_ADDR(255, 255, 255, 0);
    info->network.gateway = NET_IPV4_ADDR(10, 0, 2, 2);
    info->memory_valid = 1;
    info->memory_total_kib = 262144;
    info->memory_used_kib = 62144;
    info->storage_valid = 1;
    info->storage_total_kib = 16000;
    info->storage_free_kib = 12000;
    info->uptime_valid = 1;
    info->uptime_seconds = 245;
    info->version_valid = 1;
    memcpy(info->os_name, "HydrogenOS", 11);
    memcpy(info->os_version, "0.3.0-dev", 10);
    memcpy(info->os_arch, "x86_64", 7);
}

static uint32_t window_pixel(const Window *win, int x, int y) {
    return ((const uint32_t *)win->surface)[y * win->w + x] & 0x00FFFFFFu;
}

static uint32_t window_checksum(const Window *win) {
    return gfx_checksum((const uint32_t *)win->surface, win->w, win->h, win->w);
}

/* A pixel inside a sidebar item, right of its label. */
static uint32_t selected_item_pixel(const Window *win, const SettingsUiLayout *layout, int section) {
    const GfxRect *item = &layout->section_item[section];

    return window_pixel(win, item->x + item->w - 5, item->y + item->h / 2);
}

static const char *check_ui_section_paint(SettingsUi *ui, Window *win, const SettingsPalette *palette,
                                          uint32_t *sums) {
    SettingsUiLayout layout;

    for (int section = 0; section < SETTINGS_SECTION_COUNT; ++section) {
        ui->section = section;
        settings_ui_draw(ui, win);
        settings_ui_layout(win->w, win->h, 30, section, &layout);
        sums[section] = window_checksum(win);

        if (window_pixel(win, win->w - 1, win->h - 1) != palette->surface ||
            window_pixel(win, 3, win->h - 3) != palette->sidebar ||
            selected_item_pixel(win, &layout, section) != palette->accent) {
            return "settings ui paints tokens";
        }
        settings_ui_draw(ui, win);
        if (window_checksum(win) != sums[section]) {
            return "settings ui deterministic";
        }
    }
    for (int section = 1; section < SETTINGS_SECTION_COUNT; ++section) {
        if (sums[section] == sums[section - 1]) {
            return "settings ui sections differ";
        }
    }
    return 0;
}

static const char *check_ui_live_preview(SettingsUi *ui, Window *win, Settings *settings,
                                         SettingsPalette *palette) {
    SettingsUiLayout layout;
    uint32_t dark_sum;

    ui->section = SETTINGS_SECTION_APPEARANCE;
    settings_ui_layout(win->w, win->h, 30, SETTINGS_SECTION_APPEARANCE, &layout);
    settings_ui_draw(ui, win);
    dark_sum = window_checksum(win);

    /* Theme switching repaints with the light tokens. */
    settings->theme = SETTINGS_THEME_LIGHT;
    settings_palette(settings, palette);
    settings_ui_draw(ui, win);
    if (window_pixel(win, win->w - 1, win->h - 1) != palette->surface ||
        window_pixel(win, 3, win->h - 3) != palette->sidebar ||
        window_checksum(win) == dark_sum) {
        return "settings ui light theme";
    }

    /* The accent shows up in the selected sidebar item and the selected dot. */
    settings->accent = 5;
    settings_palette(settings, palette);
    settings_ui_draw(ui, win);
    if (palette->accent != settings_accent_color(5, SETTINGS_THEME_LIGHT) ||
        selected_item_pixel(win, &layout, SETTINGS_SECTION_APPEARANCE) != palette->accent ||
        window_pixel(win, layout.accent_dot[5].x + 14, layout.accent_dot[5].y + 14) != palette->accent ||
        window_pixel(win, layout.accent_dot[2].x + 14, layout.accent_dot[2].y + 14) == palette->accent) {
        return "settings ui accent paint";
    }

    /* The glass switch is accent coloured when on and grey when off. */
    settings->glass = 0;
    settings_ui_draw(ui, win);
    if (window_pixel(win, layout.glass_switch.x + layout.glass_switch.w - 6,
                     layout.glass_switch.y + 13) != palette->control_off) {
        return "settings ui switch off";
    }
    settings->glass = 1;
    settings_ui_draw(ui, win);
    if (window_pixel(win, layout.glass_switch.x + 6, layout.glass_switch.y + 13) != palette->accent) {
        return "settings ui switch on";
    }

    ui->save_failed = 1;
    settings_ui_draw(ui, win);
    ui->save_failed = 0;
    return 0;
}

static const char *test_settings_ui_draw(void) {
    static const int sizes[][2] = { { SETTINGS_UI_MIN_WIDTH, SETTINGS_UI_MIN_HEIGHT }, { 700, 480 } };
    Settings settings;
    SettingsPalette palette;
    SettingsUi ui;
    uint32_t sums[SETTINGS_SECTION_COUNT];
    const char *failure = 0;

    if (quiet_stdout_begin() != 0) {
        return "stdout guard";
    }
    if (comp_init_size(TEST_SURFACE_W, TEST_SURFACE_H) != 0) {
        quiet_stdout_end();
        return "settings ui compositor";
    }
    if (quiet_stdout_end() != 0) {
        return "stdout restore";
    }

    for (int size = 0; size < 2 && !failure; ++size) {
        Window *win = comp_create_window(0, 0, sizes[size][0], sizes[size][1]);

        if (!win || !win->surface) {
            return "settings ui window";
        }
        settings_defaults(&settings);
        settings_palette(&settings, &palette);
        settings_ui_init(&ui, &settings, &palette, 0, 30);
        settings_test_info(&ui.info);

        failure = check_ui_section_paint(&ui, win, &palette, sums);
        if (!failure) {
            failure = check_ui_live_preview(&ui, win, &settings, &palette);
        }
        if (comp_destroy_window(win) != 0 && !failure) {
            failure = "settings ui window destroy";
        }
    }
    return failure;
}

static void report_fail(const char *reason) {
    write(1, "DESKTOPTEST FAIL: ", 18);
    write(1, reason, strlen(reason));
    write(1, "\n", 1);
}

int main(void) {
    const char *failure;

    failure = test_compositor();
    if (!failure) {
        failure = test_widgets();
    }
    if (!failure) {
        failure = test_theme();
    }
    if (!failure) {
        failure = test_layout();
    }
    if (!failure) {
        failure = test_taskbar();
    }
    if (!failure) {
        failure = test_launcher();
    }
    if (!failure) {
        failure = test_filemanager();
    }
    if (!failure) {
        failure = test_effects();
    }
    if (!failure) {
        failure = test_compositor_effects();
    }
    if (!failure) {
        failure = test_window_geometry();
    }
    if (!failure) {
        failure = test_window_damage();
    }
    if (!failure) {
        failure = test_settings_model();
    }
    if (!failure) {
        failure = test_settings_parse();
    }
    if (!failure) {
        failure = test_settings_diff();
    }
    if (!failure) {
        failure = test_settings_palette();
    }
    if (!failure) {
        failure = test_settings_persistence();
    }
    if (!failure) {
        failure = test_settings_info();
    }
    if (!failure) {
        failure = test_settings_layout();
    }
    if (!failure) {
        failure = test_settings_ui_events();
    }
    if (!failure) {
        failure = test_settings_ui_draw();
    }

    if (failure) {
        report_fail(failure);
        return 1;
    }

    write(1, "DESKTOPTEST PASS\n", 17);
    return 0;
}
