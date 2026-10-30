#include "compositor.h"
#include "gfx_effects.h"
#include "theme.h"

#include <stddef.h>

/* TrueType support lives in the kernel (kernel/video/ttf.h) and cannot be
   linked into userspace. Keep the include as a (guarded) placeholder and fall
   back to the built-in bitmap glyph renderer below. */
#if __has_include("../../kernel/video/ttf.h")
#include "../../kernel/video/ttf.h"
#endif
/* #include "ttf.h"  -- not available on the userspace include path */

void *malloc(size_t size);
void free(void *ptr);

static int comp_initialized = 0;
static int comp_ready_printed = 0;
static int comp_w = 0;
static int comp_h = 0;

static uint32_t *comp_front = 0;
static uint32_t *comp_back = 0;

static Window *comp_windows[COMP_MAX_WINDOWS];
static int comp_window_cnt = 0;
static Window *comp_z[COMP_MAX_WINDOWS];
static int comp_z_cnt = 0;

static int comp_next_id = 1;
static int comp_focused_id = 0;

typedef CompRect DamageRect;

struct CompWindowState {
    size_t surface_capacity;   /* pixels the surface can hold */
    int owns_surface;

    /* What the last comp_render() put on screen for this window. */
    int presented;
    int last_visible;
    CompRect last_rect;
    CompRect last_extent;
    CompWindowStyle last_style;

    /* Glass: the unblurred backdrop (everything beneath the window over
       glass_area) is kept so damage below can be checked for real changes;
       glass_out holds the blurred, tinted result over glass_view. */
    int glass_valid;
    int glass_z;               /* z index the backdrop was composed for */
    CompRect glass_area;
    CompRect glass_view;
    uint32_t *glass_raw;
    size_t glass_raw_cap;
    uint32_t *glass_out;
    size_t glass_out_cap;
};

typedef struct {
    uint32_t *data;
    size_t capacity;
} CompScratch;

static CompScratch comp_compose_scratch;
static CompScratch comp_blur_scratch;

static DamageRect comp_damages[COMP_MAX_DAMAGES];
static int comp_damage_cnt = 0;
static DamageRect comp_last_damages[COMP_MAX_DAMAGES];
static int comp_last_damage_cnt = 0;

static const unsigned int COMP_BG_COLOR = 0x00101020u;

/* Built-in 8x16 bitmap font (reused from kernel font data). */
static const unsigned char comp_font8x16[256][16] = {
#include "../../kernel/video/font8x16_data.inc"
};

static int rect_intersect(int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh,
                          int *ox, int *oy, int *ow, int *oh) {
    int x1 = ax > bx ? ax : bx;
    int y1 = ay > by ? ay : by;
    int x2 = (ax + aw) < (bx + bw) ? (ax + aw) : (bx + bw);
    int y2 = (ay + ah) < (by + bh) ? (ay + ah) : (by + bh);
    if (x2 <= x1 || y2 <= y1) return 0;
    *ox = x1; *oy = y1; *ow = x2 - x1; *oh = y2 - y1;
    return 1;
}

static int rect_equal(const CompRect *a, const CompRect *b) {
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static int rect_clip(const CompRect *a, const CompRect *b, CompRect *out) {
    return rect_intersect(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h,
                          &out->x, &out->y, &out->w, &out->h);
}

static void rect_union(CompRect *acc, const CompRect *r) {
    if (r->w <= 0 || r->h <= 0) return;
    if (acc->w <= 0 || acc->h <= 0) { *acc = *r; return; }
    int x1 = acc->x + acc->w > r->x + r->w ? acc->x + acc->w : r->x + r->w;
    int y1 = acc->y + acc->h > r->y + r->h ? acc->y + acc->h : r->y + r->h;
    if (r->x < acc->x) acc->x = r->x;
    if (r->y < acc->y) acc->y = r->y;
    acc->w = x1 - acc->x;
    acc->h = y1 - acc->y;
}

static void damage_add_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > comp_w) w = comp_w - x;
    if (y + h > comp_h) h = comp_h - y;
    if (w <= 0 || h <= 0) return;
    CompRect r = { x, y, w, h };
    if (comp_damage_cnt >= COMP_MAX_DAMAGES) {
        /* Never drop damage: widen the last slot to cover the new rect. */
        rect_union(&comp_damages[COMP_MAX_DAMAGES - 1], &r);
        return;
    }
    comp_damages[comp_damage_cnt++] = r;
}

static void damage_add(const CompRect *r) {
    damage_add_rect(r->x, r->y, r->w, r->h);
}

static uint32_t *scratch_reserve(CompScratch *scratch, size_t count) {
    if (count <= scratch->capacity) return scratch->data;
    uint32_t *fresh = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!fresh) return 0;
    if (scratch->data) free(scratch->data);
    scratch->data = fresh;
    scratch->capacity = count;
    return fresh;
}

static int buffer_reserve(uint32_t **buf, size_t *cap, size_t count) {
    if (count <= *cap) return 0;
    uint32_t *fresh = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!fresh) return -1;
    if (*buf) free(*buf);
    *buf = fresh;
    *cap = count;
    return 0;
}

static const ThemeEffects *comp_effects(void) {
    return &theme_default()->effects;
}

static const GfxShadow *window_shadow(const Window *win) {
    const ThemeEffects *fx = comp_effects();
    if (!win->style.shadow || fx->shadow_alpha == 0) return 0;
    int radius = win->style.corner_radius;
    if (radius < 0) radius = 0;
    if (radius > GFX_MAX_CORNER_RADIUS) radius = GFX_MAX_CORNER_RADIUS;
    return gfx_shadow_get(radius, fx->shadow_blur_radius);
}

/* Screen area a window paints: its rectangle plus its shadow. */
static CompRect window_extent(const Window *win) {
    CompRect extent = { win->x, win->y, win->w, win->h };
    const GfxShadow *shadow = window_shadow(win);
    if (shadow) {
        const ThemeEffects *fx = comp_effects();
        int e = shadow->extent;
        CompRect cast = { win->x + fx->shadow_offset_x - e, win->y + fx->shadow_offset_y - e,
                          win->w + 2 * e, win->h + 2 * e };
        rect_union(&extent, &cast);
    }
    return extent;
}

static int style_equal(const CompWindowStyle *a, const CompWindowStyle *b) {
    return a->corner_radius == b->corner_radius && a->shadow == b->shadow &&
           a->material == b->material && a->glass_blur_radius == b->glass_blur_radius &&
           a->glass_tint == b->glass_tint && a->glass_tint_alpha == b->glass_tint_alpha;
}

int comp_init_default(void) {
    return comp_init_size(COMP_DEFAULT_WIDTH, COMP_DEFAULT_HEIGHT);
}

int comp_init_one(int width) {
    if (width <= 0) width = COMP_DEFAULT_WIDTH;
    return comp_init_size(width, width);
}

int comp_init_size(int width, int height, ...) {
    if (!comp_ready_printed) {
        printf("compositor: ready\n");
        printf("COMP READY\n");
        comp_ready_printed = 1;
    }
    (void)0;
    if (width <= 0 || height <= 0) return -1;
    if (comp_initialized) {
        /* allow re-init only if dimensions change */
    }
    size_t pixels = (size_t)width * (size_t)height;
    uint32_t *f = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    uint32_t *b = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    if (!f || !b) {
        if (f) free(f);
        if (b) free(b);
        return -1;
    }
    for (size_t i = 0; i < pixels; ++i) {
        f[i] = COMP_BG_COLOR;
        b[i] = COMP_BG_COLOR;
    }
    if (comp_front) free(comp_front);
    if (comp_back) free(comp_back);
    comp_front = f;
    comp_back = b;
    comp_w = width;
    comp_h = height;
    comp_initialized = 1;
    comp_window_cnt = 0;
    comp_z_cnt = 0;
    comp_damage_cnt = 0;
    comp_focused_id = 0;
    return 0;
}

Window *comp_create_window4(int x, int y, int w, int h) {
    return comp_create_window5(x, y, w, h, 0);
}

Window *comp_create_window5(int x, int y, int w, int h, void *surface) {
    if (!comp_initialized) {
        if (comp_init_default() != 0) return 0;
    }
    if (w <= 0 || h <= 0) return 0;
    if (comp_window_cnt >= COMP_MAX_WINDOWS) return 0;

    Window *win = (Window *)malloc(sizeof(Window));
    if (!win) return 0;
    memset(win, 0, sizeof(Window));
    win->state = (struct CompWindowState *)malloc(sizeof(struct CompWindowState));
    if (!win->state) {
        free(win);
        return 0;
    }
    memset(win->state, 0, sizeof(struct CompWindowState));
    comp_style_plain(&win->style);
    win->id = comp_next_id++;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->surface = surface;
    win->visible = 1;
    win->focused = 0;
    win->bg_color = 0x00CCCCCCu;
    win->damaged = 1;

    win->state->surface_capacity = (size_t)w * (size_t)h;
    if (!surface) {
        size_t sp = (size_t)w * (size_t)h;
        uint32_t *buf = (uint32_t *)malloc(sp * sizeof(uint32_t));
        if (buf) {
            for (size_t i = 0; i < sp; ++i) buf[i] = win->bg_color;
            win->surface = buf;
            win->state->owns_surface = 1;
        } else {
            win->state->surface_capacity = 0;
        }
    }

    comp_windows[comp_window_cnt++] = win;
    comp_z[comp_z_cnt++] = win;

    /* damage whole window */
    damage_add_rect(x, y, w, h);
    return win;
}

int comp_destroy_window(Window *win) {
    if (!win) return -1;

    /* remove from z */
    for (int i = 0; i < comp_z_cnt; ++i) {
        if (comp_z[i] == win) {
            for (int j = i; j + 1 < comp_z_cnt; ++j) comp_z[j] = comp_z[j + 1];
            comp_z_cnt--;
            break;
        }
    }
    /* remove from windows list */
    for (int i = 0; i < comp_window_cnt; ++i) {
        if (comp_windows[i] == win) {
            for (int j = i; j + 1 < comp_window_cnt; ++j) comp_windows[j] = comp_windows[j + 1];
            comp_window_cnt--;
            break;
        }
    }
    if (comp_focused_id == win->id) comp_focused_id = 0;
    CompRect extent = window_extent(win);
    damage_add(&extent);
    struct CompWindowState *st = win->state;
    if (st->presented && st->last_visible) damage_add(&st->last_extent);
    if (st->owns_surface) free(win->surface);
    if (st->glass_raw) free(st->glass_raw);
    if (st->glass_out) free(st->glass_out);
    free(st);
    free(win);
    return 0;
}

int comp_resize_window(Window *win, int w, int h) {
    if (!win || w <= 0 || h <= 0) return -1;
    struct CompWindowState *st = win->state;
    size_t need = (size_t)w * (size_t)h;
    if (need > st->surface_capacity) {
        if (!st->owns_surface) return -1;
        /* Interactive resizes grow a window a few pixels per frame. Reserving
           the whole screen on first growth keeps every later resize within the
           screen allocation-free instead of reallocating on each step. */
        size_t capacity = need;
        size_t screen = (size_t)comp_w * (size_t)comp_h;
        if (screen > capacity) capacity = screen;
        uint32_t *buf = (uint32_t *)malloc(capacity * sizeof(uint32_t));
        if (!buf) return -1;
        for (size_t i = 0; i < need; ++i) buf[i] = win->bg_color;
        if (win->surface) free(win->surface);
        win->surface = buf;
        st->surface_capacity = capacity;
    }
    damage_add_rect(win->x, win->y, win->w, win->h);
    win->w = w;
    win->h = h;
    win->damaged = 1;
    damage_add_rect(win->x, win->y, win->w, win->h);
    return 0;
}

void comp_style_plain(CompWindowStyle *style) {
    if (!style) return;
    memset(style, 0, sizeof(*style));
    style->material = COMP_MATERIAL_OPAQUE;
}

void comp_style_window(CompWindowStyle *style) {
    if (!style) return;
    comp_style_plain(style);
    style->corner_radius = comp_effects()->window_radius;
    style->shadow = 1;
}

void comp_style_glass(CompWindowStyle *style) {
    if (!style) return;
    const ThemeEffects *fx = comp_effects();
    comp_style_plain(style);
    style->material = COMP_MATERIAL_GLASS;
    style->glass_blur_radius = fx->glass_blur_radius;
    style->glass_tint = fx->glass_tint;
    style->glass_tint_alpha = fx->glass_tint_alpha;
}

int comp_set_window_style(Window *win, const CompWindowStyle *style) {
    if (!win || !style) return -1;
    if (style->material != COMP_MATERIAL_OPAQUE && style->material != COMP_MATERIAL_GLASS)
        return -1;
    if (style->corner_radius < 0 || style->glass_blur_radius < 0 ||
        style->glass_tint_alpha > 255)
        return -1;
    win->style = *style;
    return 0;
}

int comp_raise(Window *win) {
    if (!win) return -1;
    /* move to top of z */
    for (int i = 0; i < comp_z_cnt; ++i) {
        if (comp_z[i] == win) {
            for (int j = i; j + 1 < comp_z_cnt; ++j) comp_z[j] = comp_z[j + 1];
            comp_z[comp_z_cnt - 1] = win;
            break;
        }
    }
    /* The shadow now falls over windows it used to be beneath. */
    CompRect extent = window_extent(win);
    damage_add(&extent);
    return 0;
}

int comp_focus(Window *win) {
    if (!win) return -1;
    for (int i = 0; i < comp_window_cnt; ++i) {
        if (comp_windows[i]) comp_windows[i]->focused = 0;
    }
    win->focused = 1;
    comp_focused_id = win->id;
    comp_raise(win);
    return 0;
}

int comp_damage_window(Window *win) {
    if (!win) return -1;
    win->damaged = 1;
    damage_add_rect(win->x, win->y, win->w, win->h);
    return 0;
}

int comp_damage_rectp(Window *win, const CompRect *rect) {
    if (!rect) return comp_damage_window(win);
    /* win may be NULL? if so, treat rect as screen */
    int x = rect->x, y = rect->y, w = rect->w, h = rect->h;
    if (win) {
        /* clip to window */
        int ix, iy, iw, ih;
        if (rect_intersect(x, y, w, h, win->x, win->y, win->w, win->h, &ix, &iy, &iw, &ih)) {
            damage_add_rect(ix, iy, iw, ih);
        }
    } else {
        damage_add_rect(x, y, w, h);
    }
    return 0;
}

int comp_damage_rect(Window *win, int x, int y, int w, int h) {
    CompRect r = { x, y, w, h };
    return comp_damage_rectp(win, &r);
}

int comp_damage_all(void) {
    damage_add_rect(0, 0, comp_w, comp_h);
    /* A full repaint is a repaint from scratch: cached glass is rebuilt. */
    for (int i = 0; i < comp_window_cnt; ++i) {
        if (comp_windows[i]) {
            comp_windows[i]->damaged = 1;
            comp_windows[i]->state->glass_valid = 0;
        }
    }
    return 0;
}

void comp_flip(void) {
    if (!comp_initialized) return;
    uint32_t *tmp = comp_front;
    comp_front = comp_back;
    comp_back = tmp;
}

/* The surface backs the current geometry only if it is large enough; where
   it is not, the window's background colour is shown instead. */
static int window_surface_ok(const Window *win) {
    return win->surface && (size_t)win->w * (size_t)win->h <= win->state->surface_capacity;
}

static void compose_window_body(uint32_t *dst, const CompRect *buffer,
                                const CompRect *region, const Window *win) {
    CompRect rect = { win->x, win->y, win->w, win->h };
    CompRect area;
    if (!rect_clip(&rect, region, &area)) return;

    const struct CompWindowState *st = win->state;
    const uint32_t *surf = window_surface_ok(win) ? (const uint32_t *)win->surface : 0;
    int glass = win->style.material == COMP_MATERIAL_GLASS;
    const uint32_t *backdrop = glass && st->glass_valid ? st->glass_out : 0;
    const ThemeEffects *fx = comp_effects();
    GfxRoundRect rr;
    gfx_round_rect_init(&rr, win->w, win->h, win->style.corner_radius);

    for (int sy = area.y; sy < area.y + area.h; ++sy) {
        int wy = sy - win->y;
        uint32_t *drow = dst + (size_t)(sy - buffer->y) * (size_t)buffer->w + (size_t)(area.x - buffer->x);
        const uint32_t *srow = surf ? surf + (size_t)wy * (size_t)win->w : 0;
        int corner_row = rr.radius > 0 && (wy < rr.radius || wy >= win->h - rr.radius);
        /* The glass highlight lights the upper half of the border only. */
        int edge_row = glass && 2 * wy < win->h && wy <= rr.radius;

        if (!glass && !corner_row) {
            if (srow) {
                memcpy(drow, srow + (area.x - win->x), (size_t)area.w * sizeof(uint32_t));
            } else {
                for (int i = 0; i < area.w; ++i) drow[i] = win->bg_color;
            }
            continue;
        }

        const uint32_t *brow = backdrop
            ? backdrop + (size_t)(sy - st->glass_view.y) * (size_t)st->glass_view.w
            : 0;
        for (int sx = area.x; sx < area.x + area.w; ++sx) {
            int wx = sx - win->x;
            unsigned int cover = corner_row ? gfx_round_rect_coverage(&rr, wx, wy) : 255u;
            if (cover == 0) continue;
            uint32_t src = srow ? srow[wx] : win->bg_color;
            if (glass) {
                uint32_t base = brow ? brow[sx - st->glass_view.x] : win->style.glass_tint;
                base = gfx_mix(base, src, srow ? src >> 24 : 0u);
                if (edge_row) {
                    unsigned int edge = gfx_round_rect_edge(&rr, wx, wy);
                    base = gfx_mix(base, fx->glass_highlight,
                                   (edge * fx->glass_highlight_alpha + 127u) / 255u);
                }
                src = base;
            }
            uint32_t *d = &drow[sx - area.x];
            *d = cover == 255u ? src : gfx_mix(*d, src, cover);
        }
    }
}

/* Paints background, shadows and the windows below z_limit into `region` of
   a buffer covering screen rectangle `buffer` (stride buffer->w). region
   must lie within buffer. Reads nothing but window state, so it is equally
   valid for the back buffer and for glass backdrops. */
static void compose_region(uint32_t *dst, const CompRect *buffer, const CompRect *region, int z_limit) {
    for (int y = region->y; y < region->y + region->h; ++y) {
        uint32_t *row = dst + (size_t)(y - buffer->y) * (size_t)buffer->w + (size_t)(region->x - buffer->x);
        for (int i = 0; i < region->w; ++i) row[i] = COMP_BG_COLOR;
    }

    const ThemeEffects *fx = comp_effects();
    GfxCanvas canvas = { dst, buffer->w, buffer->h, buffer->w, 0 };
    GfxRect clip = { region->x - buffer->x, region->y - buffer->y, region->w, region->h };
    for (int zi = 0; zi < z_limit && zi < comp_z_cnt; ++zi) {
        Window *win = comp_z[zi];
        if (!win || !win->visible) continue;
        const GfxShadow *shadow = window_shadow(win);
        if (shadow) {
            gfx_draw_shadow(&canvas, shadow, win->x - buffer->x, win->y - buffer->y, win->w, win->h,
                            fx->shadow_offset_x, fx->shadow_offset_y,
                            fx->shadow_color, fx->shadow_alpha, &clip);
        }
        compose_window_body(dst, buffer, region, win);
    }
}

/* Damages whatever moved, appeared, vanished or changed style or shadow
   since the previous frame, including the shadow it casts. */
static void reconcile_windows(void) {
    for (int i = 0; i < comp_window_cnt; ++i) {
        Window *win = comp_windows[i];
        if (!win) continue;
        struct CompWindowState *st = win->state;
        CompRect rect = { win->x, win->y, win->w, win->h };
        CompRect extent = window_extent(win);
        int visible = win->visible != 0;
        int restyled = !style_equal(&win->style, &st->last_style);
        /* The extent also depends on live theme shadow tokens, so compare it
           directly rather than inferring it from geometry and style. */
        if (!st->presented || restyled || visible != st->last_visible ||
            !rect_equal(&rect, &st->last_rect) || !rect_equal(&extent, &st->last_extent)) {
            if (st->presented && st->last_visible) damage_add(&st->last_extent);
            if (visible) damage_add(&extent);
        }
        if (restyled) st->glass_valid = 0;
        st->presented = 1;
        st->last_visible = visible;
        st->last_rect = rect;
        st->last_extent = extent;
        st->last_style = win->style;
    }
}

static int glass_rebuild(Window *win) {
    struct CompWindowState *st = win->state;
    const CompRect *area = &st->glass_area;
    const CompRect *view = &st->glass_view;
    size_t pixels = (size_t)area->w * (size_t)area->h;
    size_t line = (size_t)(area->w > area->h ? area->w : area->h);
    uint32_t *work = scratch_reserve(&comp_blur_scratch, pixels + line);
    if (!work) return -1;
    memcpy(work, st->glass_raw, pixels * sizeof(uint32_t));
    gfx_blur(work, area->w, area->h, area->w, win->style.glass_blur_radius, work + pixels);
    for (int y = 0; y < view->h; ++y) {
        const uint32_t *src = work + (size_t)(view->y - area->y + y) * (size_t)area->w + (size_t)(view->x - area->x);
        uint32_t *out = st->glass_out + (size_t)y * (size_t)view->w;
        for (int x = 0; x < view->w; ++x)
            out[x] = gfx_mix(src[x], win->style.glass_tint, win->style.glass_tint_alpha);
    }
    return 0;
}

/* Recomposes the backdrop of a glass window under this frame's damage and
   re-blurs only when its pixels actually changed, so updates on or above
   the glass (clock ticks, the cursor) reuse the cached blur. */
static void glass_refresh(Window *win, int z_index) {
    struct CompWindowState *st = win->state;
    CompRect screen = { 0, 0, comp_w, comp_h };
    CompRect rect = { win->x, win->y, win->w, win->h };
    CompRect view, area;
    if (!rect_clip(&rect, &screen, &view)) {
        st->glass_valid = 0;
        return;
    }
    int e = gfx_blur_extent(win->style.glass_blur_radius);
    CompRect reach = { rect.x - e, rect.y - e, rect.w + 2 * e, rect.h + 2 * e };
    rect_clip(&reach, &screen, &area);

    /* A new z position changes which windows count as backdrop even where
       nothing was damaged, so it needs a full rebuild like a new geometry. */
    if (!st->glass_valid || st->glass_z != z_index ||
        !rect_equal(&area, &st->glass_area) || !rect_equal(&view, &st->glass_view)) {
        st->glass_valid = 0;
        damage_add(&view);
        if (buffer_reserve(&st->glass_raw, &st->glass_raw_cap, (size_t)area.w * (size_t)area.h) != 0 ||
            buffer_reserve(&st->glass_out, &st->glass_out_cap, (size_t)view.w * (size_t)view.h) != 0)
            return;
        st->glass_area = area;
        st->glass_view = view;
        st->glass_z = z_index;
        compose_region(st->glass_raw, &area, &area, z_index);
        st->glass_valid = glass_rebuild(win) == 0;
        return;
    }

    CompRect changed = { 0, 0, 0, 0 };
    int pending = comp_damage_cnt;
    for (int d = 0; d < pending; ++d) {
        CompRect part;
        if (!rect_clip(&comp_damages[d], &area, &part)) continue;
        uint32_t *fresh = scratch_reserve(&comp_compose_scratch, (size_t)part.w * (size_t)part.h);
        if (!fresh) {
            st->glass_valid = 0;
            return;
        }
        compose_region(fresh, &part, &part, z_index);
        int differs = 0;
        for (int y = 0; y < part.h; ++y) {
            uint32_t *cached = st->glass_raw + (size_t)(part.y - area.y + y) * (size_t)area.w + (size_t)(part.x - area.x);
            const uint32_t *row = fresh + (size_t)y * (size_t)part.w;
            for (int x = 0; x < part.w; ++x) {
                if (cached[x] != row[x]) {
                    cached[x] = row[x];
                    differs = 1;
                }
            }
        }
        if (differs) rect_union(&changed, &part);
    }
    if (changed.w <= 0) return;

    if (glass_rebuild(win) != 0) {
        st->glass_valid = 0;
        damage_add(&view);
        return;
    }
    /* A changed backdrop pixel influences the blur up to e pixels away. */
    CompRect spread = { changed.x - e, changed.y - e, changed.w + 2 * e, changed.h + 2 * e };
    CompRect repaint;
    if (rect_clip(&spread, &view, &repaint)) damage_add(&repaint);
}

int comp_render(void) {
    if (!comp_initialized) return -1;
    reconcile_windows();
    if (comp_damage_cnt == 0) return 0;

    /* Bottom to top, so damage added by a lower glass window is seen by the
       glass windows above it. */
    for (int zi = 0; zi < comp_z_cnt; ++zi) {
        Window *win = comp_z[zi];
        if (!win || win->style.material != COMP_MATERIAL_GLASS) continue;
        if (!win->visible) {
            /* Damage beneath a hidden window is not tracked against its
               backdrop; start over once it is shown again. */
            win->state->glass_valid = 0;
            continue;
        }
        glass_refresh(win, zi);
    }

    CompRect screen = { 0, 0, comp_w, comp_h };
    for (int d = 0; d < comp_damage_cnt; ++d)
        compose_region(comp_back, &screen, &comp_damages[d], comp_z_cnt);
    for (int wi = 0; wi < comp_z_cnt; ++wi)
        if (comp_z[wi] && comp_z[wi]->visible) comp_z[wi]->damaged = 0;

    /* Preserve the damage list so callers can present only what changed. */
    comp_last_damage_cnt = comp_damage_cnt;
    for (int i = 0; i < comp_damage_cnt && i < COMP_MAX_DAMAGES; ++i)
        comp_last_damages[i] = comp_damages[i];

    comp_flip();
    comp_damage_cnt = 0;
    return 0;
}

int comp_present(void) {
    if (!comp_initialized) return -1;
    unsigned int info[4];
    long rc = sys_fb_info(info);
    if (rc == SYS_FB_ERR_NODEV)
        return -1;
    if (rc != 0) {
        printf("comp_present: info rc=%d\n", (int)rc);
        return -1;
    }
    int fw = (int)info[0];
    int fh = (int)info[1];
    int w = comp_w < fw ? comp_w : fw;
    int h = comp_h < fh ? comp_h : fh;
    printf("comp_present: fb=%dx%d comp=%dx%d\n", fw, fh, comp_w, comp_h);
    if (w <= 0 || h <= 0) return -1;
    rc = sys_fb_blit(0, 0, w, h, comp_front);
    if (rc != 0) {
        printf("comp_present: blit rc=%d\n", (int)rc);
        return -1;
    }
    return 0;
}

int comp_present_damage(void) {
    if (!comp_front) return -1;
    /* Merge overlapping/adjacent damage rects to reduce blit count. */
    for (int i = 0; i < comp_last_damage_cnt; ++i) {
        DamageRect *a = &comp_last_damages[i];
        if (a->w <= 0 || a->h <= 0) continue;
        int ax1 = a->x + a->w, ay1 = a->y + a->h;
        for (int j = i + 1; j < comp_last_damage_cnt; ++j) {
            DamageRect *b = &comp_last_damages[j];
            if (b->w <= 0 || b->h <= 0) continue;
            int bx1 = b->x + b->w, by1 = b->y + b->h;
            if (b->x <= ax1 + 1 && bx1 + 1 >= a->x &&
                b->y <= ay1 + 1 && by1 + 1 >= a->y) {
                int nx = a->x < b->x ? a->x : b->x;
                int ny = a->y < b->y ? a->y : b->y;
                int nx1 = ax1 > bx1 ? ax1 : bx1;
                int ny1 = ay1 > by1 ? ay1 : by1;
                a->x = nx; a->y = ny; a->w = nx1 - nx; a->h = ny1 - ny;
                ax1 = nx1; ay1 = ny1;
                b->w = 0; b->h = 0;
            }
        }
    }
    for (int i = 0; i < comp_last_damage_cnt; ++i) {
        DamageRect *d = &comp_last_damages[i];
        if (d->w <= 0 || d->h <= 0) continue;
        const uint32_t *src = comp_front + (long)d->y * comp_w + d->x;
        if (sys_fb_blit_strided(d->x, d->y, d->w, d->h, src, comp_w * 4) != 0)
            return -1;
    }
    return 0;
}

int comp_present_rect(int x, int y, int w, int h) {
    if (!comp_front) return -1;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > comp_w) w = comp_w - x;
    if (y + h > comp_h) h = comp_h - y;
    if (w <= 0 || h <= 0) return 0;
    const uint32_t *src = comp_front + (long)y * comp_w + x;
    return (int)sys_fb_blit_strided(x, y, w, h, src, comp_w * 4);
}

uint32_t *comp_front_buffer(void) { return comp_front; }
uint32_t *comp_back_buffer(void) { return comp_back; }
int comp_width(void) { return comp_w; }
int comp_height(void) { return comp_h; }

void comp_event_loop(void) {
    if (!comp_ready_printed) {
        printf("compositor: ready\n");
        printf("COMP READY\n");
        comp_ready_printed = 1;
    }
    if (!comp_initialized) {
        if (comp_init_default() != 0) return;
    }
    for (;;) {
        if (comp_damage_cnt > 0) {
            comp_render();
        }
        sleep_ms(16);
    }
}

Window *comp_find_window(int id) {
    for (int i = 0; i < comp_window_cnt; ++i) {
        if (comp_windows[i] && comp_windows[i]->id == id) return comp_windows[i];
    }
    return 0;
}

Window *comp_get_window(int id) {
    return comp_find_window(id);
}

int comp_window_count(void) {
    return comp_window_cnt;
}

/* Simple bitmap glyph renderer (fallback for unavailable TrueType). Pixels
   outside the clip rectangle are left alone. */
static void glyph_render(uint32_t *buf, int stride, const CompRect *clip,
                        int x, int y, unsigned char ch, uint32_t color) {
    if (!buf || ch <= 32) return;
    const unsigned char *glyph = comp_font8x16[ch];
    for (int row = 0; row < 16; ++row) {
        int py = y + row;
        if (py < clip->y || py >= clip->y + clip->h) continue;
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << (7 - col))) {
                int px = x + col;
                if (px < clip->x || px >= clip->x + clip->w) continue;
                buf[py * stride + px] = color;
            }
        }
    }
}

void comp_text_draw(Window *win, int x, int y, const char *text, size_t length,
                    uint32_t color, const CompRect *clip) {
    if (!win || !text) return;
    uint32_t *surf = (uint32_t *)win->surface;
    if (!surf || !window_surface_ok(win)) return;
    CompRect bounds = { 0, 0, win->w, win->h };
    CompRect area;
    if (clip) {
        if (!rect_clip(&bounds, clip, &area)) return;
    } else {
        area = bounds;
    }
    /* Glass surfaces carry content opacity in the alpha byte. */
    uint32_t ink = win->style.material == COMP_MATERIAL_GLASS
        ? (color | 0xFF000000u) : (color & 0x00FFFFFFu);
    int cx = x;
    for (size_t i = 0; i < length && text[i]; ++i) {
        glyph_render(surf, win->w, &area, cx, y, (unsigned char)text[i], ink);
        cx += 8;
        if (cx >= area.x + area.w) break;
    }
}

/* Render text into a window using the bitmap glyph renderer.
   TrueType equivalent would be: ttf_draw_text(x, y, text, size, color, win->surface); */
void text_render(Window *win, int x, int y, const char *text, uint32_t color) {
    if (!win || !text) return;
    uint32_t *surf = (uint32_t *)win->surface;
    if (!surf || !window_surface_ok(win)) return;
    /* Glass surfaces carry content opacity in the alpha byte. */
    uint32_t ink = win->style.material == COMP_MATERIAL_GLASS
        ? (color | 0xFF000000u) : (color & 0x00FFFFFFu);
    CompRect all = { 0, 0, win->w, win->h };
    int cx = x;
    int cy = y;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\n') { cx = x; cy += 16; continue; }
        if (ch == '\r') { cx = x; continue; }
        if (ch < 32) continue;
        glyph_render(surf, win->w, &all, cx, cy, ch, ink);
        cx += 8;
    }
    win->damaged = 1;
    comp_damage_window(win);
}

void comp_text_render(Window *win, int x, int y, const char *text, uint32_t color) {
    text_render(win, x, y, text, color);
}

void render_text(Window *win, int x, int y, const char *text, uint32_t color) {
    text_render(win, x, y, text, color);
}

/* Dispatch helpers */
int _comp_damage_1(Window *win) {
    return comp_damage_window(win);
}
int _comp_damage_2(Window *win, const CompRect *rect) {
    return comp_damage_rectp(win, rect);
}
int _comp_damage_3(Window *win, int x, int y) {
    return comp_damage_rect(win, x, y, 0, 0);
}
int _comp_damage_4(Window *win, int x, int y, int w) {
    return comp_damage_rect(win, x, y, w, 0);
}
int _comp_damage_5(Window *win, int x, int y, int w, int h) {
    return comp_damage_rect(win, x, y, w, h);
}