#include "compositor.h"

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

typedef struct {
    int x, y, w, h;
} DamageRect;

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

static void damage_add_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > comp_w) w = comp_w - x;
    if (y + h > comp_h) h = comp_h - y;
    if (w <= 0 || h <= 0) return;
    if (comp_damage_cnt >= COMP_MAX_DAMAGES) return;
    comp_damages[comp_damage_cnt].x = x;
    comp_damages[comp_damage_cnt].y = y;
    comp_damages[comp_damage_cnt].w = w;
    comp_damages[comp_damage_cnt].h = h;
    comp_damage_cnt++;
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

    if (!surface) {
        size_t sp = (size_t)w * (size_t)h;
        uint32_t *buf = (uint32_t *)malloc(sp * sizeof(uint32_t));
        if (buf) {
            for (size_t i = 0; i < sp; ++i) buf[i] = win->bg_color;
            win->surface = buf;
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
    damage_add_rect(win->x, win->y, win->w, win->h);
    /* do not free surface (may be caller-owned); free window struct */
    free(win);
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
    damage_add_rect(win->x, win->y, win->w, win->h);
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
    for (int i = 0; i < comp_window_cnt; ++i) {
        if (comp_windows[i]) comp_windows[i]->damaged = 1;
    }
    return 0;
}

void comp_flip(void) {
    if (!comp_initialized) return;
    uint32_t *tmp = comp_front;
    comp_front = comp_back;
    comp_back = tmp;
}

int comp_render(void) {
    if (!comp_initialized) return -1;
    if (comp_damage_cnt == 0) return 0;

    /* Clear damaged regions in back buffer to background */
    for (int d = 0; d < comp_damage_cnt; ++d) {
        DamageRect *dr = &comp_damages[d];
        for (int yy = dr->y; yy < dr->y + dr->h; ++yy) {
            if (yy < 0 || yy >= comp_h) continue;
            for (int xx = dr->x; xx < dr->x + dr->w; ++xx) {
                if (xx < 0 || xx >= comp_w) continue;
                comp_back[yy * comp_w + xx] = COMP_BG_COLOR;
            }
        }
    }

    /* Draw windows intersecting damage, sorted by Z (back to front) */
    for (int wi = 0; wi < comp_z_cnt; ++wi) {
        Window *win = comp_z[wi];
        if (!win || !win->visible) continue;
        for (int d = 0; d < comp_damage_cnt; ++d) {
            DamageRect *dr = &comp_damages[d];
            int ix, iy, iw, ih;
            if (!rect_intersect(win->x, win->y, win->w, win->h,
                               dr->x, dr->y, dr->w, dr->h,
                               &ix, &iy, &iw, &ih)) continue;
            /* also clip to screen */
            int sx, sy, sw, sh;
            if (!rect_intersect(ix, iy, iw, ih, 0, 0, comp_w, comp_h, &sx, &sy, &sw, &sh)) continue;
            for (int yy = sy; yy < sy + sh; ++yy) {
                for (int xx = sx; xx < sx + sw; ++xx) {
                    int wx = xx - win->x;
                    int wy = yy - win->y;
                    uint32_t color = win->bg_color;
                    if (win->surface) {
                        uint32_t *surf = (uint32_t *)win->surface;
                        if (wx >= 0 && wy >= 0 && wx < win->w && wy < win->h) {
                            color = surf[wy * win->w + wx];
                        }
                    }
                    comp_back[yy * comp_w + xx] = color;
                }
            }
        }
        win->damaged = 0;
    }

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

/* Simple bitmap glyph renderer (fallback for unavailable TrueType). */
static void glyph_render(uint32_t *buf, int stride, int buf_w, int buf_h,
                        int x, int y, unsigned char ch, uint32_t color) {
    if (!buf || ch < 32) return;
    const unsigned char *glyph = comp_font8x16[ch];
    for (int row = 0; row < 16; ++row) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << (7 - col))) {
                int px = x + col;
                int py = y + row;
                if (px < 0 || py < 0 || px >= buf_w || py >= buf_h) continue;
                buf[py * stride + px] = color & 0x00FFFFFFu;
            }
        }
    }
}

/* Render text into a window using the bitmap glyph renderer.
   TrueType equivalent would be: ttf_draw_text(x, y, text, size, color, win->surface); */
void text_render(Window *win, int x, int y, const char *text, uint32_t color) {
    if (!win || !text) return;
    uint32_t *surf = (uint32_t *)win->surface;
    if (!surf) return;
    int cx = x;
    int cy = y;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\n') { cx = x; cy += 16; continue; }
        if (ch == '\r') { cx = x; continue; }
        if (ch < 32) continue;
        glyph_render(surf, win->w, win->w, win->h, cx, cy, ch, color);
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