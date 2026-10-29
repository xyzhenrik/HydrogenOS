#include "mini_libc.h"
#include "compositor.h"
#include "gfx_effects.h"
#include "widgets.h"
/* theme.h must precede layout.h: layout.h defines shorthand macros such as
   gap/space that collide with Theme field names. */
#include "theme.h"
#include "layout.h"
#include "taskbar.h"
#include "launcher.h"
#include "filemanager.h"

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
    taskbar_clock_update(1500UL);
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
    /* The clock pill spans x=246..313 around the "1500 ms" text. */
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

    if (failure) {
        report_fail(failure);
        return 1;
    }

    write(1, "DESKTOPTEST PASS\n", 17);
    return 0;
}
