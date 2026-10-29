#include "mini_libc.h"
#include "compositor.h"
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
    int bar_y;
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

    bar_y = 120 - TASKBAR_HEIGHT;
    if (surface->pixels[bar_y * surface->stride] != 0xFF3A3F4Au) {
        return "taskbar border";
    }
    if (surface->pixels[(bar_y - 1) * surface->stride] != 0) {
        return "taskbar bounds";
    }
    tile_pixel = surface->pixels[(bar_y + 5) * surface->stride + 8];
    if (tile_pixel == 0 || tile_pixel == 0xFF20242Au) {
        return "taskbar app tile";
    }
    /* Clock pill sits left of the "1500 ms" text, which starts at x=252. */
    clock_pixel = surface->pixels[(bar_y + 7) * surface->stride + 247];
    if (clock_pixel != 0xFF2A2F3Au) {
        return "taskbar clock";
    }

    surface_destroy(surface);
    return 0;
}

static const char *test_launcher(void) {
    Surface *surface;
    Launcher *launcher;
    const char *selected_path;
    int selected;

    surface = surface_create(TEST_SURFACE_W, TEST_SURFACE_H);
    if (!surface) {
        return "launcher surface";
    }
    surface_clear(surface, 0);

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
    /* (x, y) is the white panel border; one pixel inside is the panel fill. */
    if (surface->pixels[11 * surface->stride + 11] != launcher->bg_color) {
        return "launcher draw";
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

    if (failure) {
        report_fail(failure);
        return 1;
    }

    write(1, "DESKTOPTEST PASS\n", 17);
    return 0;
}
