#include "bootsplash.h"

#include "errno.h"
#include "fb_console.h"
#include "framebuffer.h"
#include "serial.h"
#include "logo_splash.h"

/* Everything is drawn with integer arithmetic only: the kernel keeps no FPU
   state, and the shapes are signed distance functions in 1/SUB pixel units
   that are turned into anti-aliased coverage. */
#define SUB 16
#define FAR (1 << 24)
#define PROGRESS_MAX 1000u
#define SERVICES_BASE 300u
#define REPORT_CAP 48
#define FADE_STEPS 12

#define BG_TOP 0x001B2530u
#define BG_BOTTOM 0x0005070Au
#define LETTER_COLOR 0x00EEF6FFu
#define BAR_FILL 0x00F0F7FFu
#define BAR_TRACK_WEIGHT 36u
#define SUFFIX_WEIGHT 150u

/* Cap-height relative sizes in per mille. */
#define STROKE 150
#define LETTER_GAP 190
#define DROP_HEIGHT 1300
#define DROP_OVERSHOOT 90

typedef enum glyph {
    GLYPH_H,
    GLYPH_Y,
    GLYPH_D,
    GLYPH_R,
    GLYPH_DROP,
    GLYPH_G,
    GLYPH_E,
    GLYPH_N,
    GLYPH_O,
    GLYPH_S,
} glyph_t;

/* weight is 0..256: how strongly the letter is drawn over the background.
   The "OS" suffix is dimmer than the name so that the drop stays the O of
   "Hydrogen". */
typedef struct wordmark_letter {
    glyph_t glyph;
    int32_t width;
    uint32_t weight;
} wordmark_letter_t;

static const wordmark_letter_t wordmark[] = {
    { GLYPH_H, 720, 256 },
    { GLYPH_Y, 780, 256 },
    { GLYPH_D, 740, 256 },
    { GLYPH_R, 700, 256 },
    { GLYPH_DROP, 980, 256 },
    { GLYPH_G, 1000, 256 },
    { GLYPH_E, 560, 256 },
    { GLYPH_N, 740, 256 },
    { GLYPH_O, 1000, SUFFIX_WEIGHT },
    { GLYPH_S, 575, SUFFIX_WEIGHT },
};

#define WORDMARK_LETTERS (sizeof(wordmark) / sizeof(wordmark[0]))

/* Progress after each kernel stage; init's services fill the rest. */
static const uint16_t stage_progress[] = { 80, 150, 230, SERVICES_BASE };

typedef struct splash_layout {
    uint32_t width;
    uint32_t height;
    int32_t cap;
    int32_t stroke;
    int32_t bar_x;
    int32_t bar_y;
    int32_t bar_w;
    int32_t bar_h;
} splash_layout_t;

static splash_layout_t layout;
static bootsplash_state_t state = BOOTSPLASH_DISABLED;
static uint32_t progress = 0;
static uint32_t *surface = NULL;
static uint32_t stride_words = 0;

static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

static uint32_t isqrt64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ull << 62;
    while (bit > value)
        bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static int32_t length(int32_t dx, int32_t dy) {
    return (int32_t)isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy));
}

static uint32_t channel(uint32_t color, int shift) {
    return (color >> shift) & 0xFFu;
}

/* weight is 0..256: how much of `color` replaces `base`. */
static uint32_t blend(uint32_t base, uint32_t color, uint32_t weight) {
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t b = channel(base, shift);
        uint32_t c = channel(color, shift);
        out |= ((b * (256u - weight) + c * weight) >> 8) << shift;
    }
    return out;
}

/* Coverage 0..SUB of a pixel whose center lies at signed distance d from the
   shape's edge (positive outside). */
static int32_t coverage(int32_t d) {
    int32_t c = SUB / 2 - d;
    if (c < 0)
        return 0;
    return c > SUB ? SUB : c;
}

/* Dithered vertical gradient; the dither hides banding on dark ramps. */
static uint32_t background_pixel(uint32_t x, uint32_t y) {
    static const uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
    };
    uint32_t threshold = (uint32_t)bayer[y & 3][x & 3] * 16u + 8u;
    uint32_t span = layout.height > 1 ? layout.height - 1 : 1;
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t top = channel(BG_TOP, shift) << 8;
        uint32_t bottom = channel(BG_BOTTOM, shift) << 8;
        uint32_t value = (uint32_t)(((uint64_t)top * (span - y) +
                                     (uint64_t)bottom * y) / span);
        uint32_t c = (value + threshold) >> 8;
        out |= (c > 255u ? 255u : c) << shift;
    }
    return out;
}

static uint32_t *surface_pixel(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || (uint32_t)x >= layout.width ||
        (uint32_t)y >= layout.height)
        return NULL;
    return surface + (size_t)y * stride_words + (uint32_t)x;
}

static void present(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w > 0 && h > 0)
        fb_console_present_rect((uint32_t)x, (uint32_t)y, (uint32_t)w,
                                (uint32_t)h);
}

/* ---- Distance functions. Glyph space has its origin at the top-left of the
   cap box; positive distance is outside. ---- */

static int32_t box_distance(int32_t x, int32_t y, int32_t x0, int32_t y0,
                            int32_t x1, int32_t y1) {
    return imax(imax(x0 - x, x - x1), imax(y0 - y, y - y1));
}

/* Straight stroke along a-b with flat ends cut by the clip box instead of
   perpendicular to the stroke, so diagonals end flush with the cap line. */
static int32_t band_distance(int32_t x, int32_t y, int32_t ax, int32_t ay,
                             int32_t bx, int32_t by, int32_t half,
                             int32_t clip_x0, int32_t clip_y0, int32_t clip_x1,
                             int32_t clip_y1) {
    int64_t dx = bx - ax;
    int64_t dy = by - ay;
    int64_t cross = (int64_t)(x - ax) * dy - (int64_t)(y - ay) * dx;
    int32_t len = length((int32_t)dx, (int32_t)dy);
    int32_t perp = (int32_t)((cross < 0 ? -cross : cross) / len);
    return imax(perp - half,
                box_distance(x, y, clip_x0, clip_y0, clip_x1, clip_y1));
}

/* Right half of a ring, as the bowl of D and R. It reaches one pixel past the
   center line so that it overlaps the bars it joins; abutting shapes would
   leave a faint seam at the shared edge. */
static int32_t bowl_distance(int32_t x, int32_t y, int32_t cx, int32_t cy,
                             int32_t outer, int32_t inner) {
    if (x < cx - SUB)
        return FAR;
    int32_t r = length(x - cx, y - cy);
    return imax(r - outer, inner - r);
}

static int32_t glyph_distance(glyph_t glyph, int32_t x, int32_t y, int32_t w) {
    const int32_t c = layout.cap;
    const int32_t t = layout.stroke;

    switch (glyph) {
    case GLYPH_H:
        return imin(imin(box_distance(x, y, 0, 0, t, c),
                         box_distance(x, y, w - t, 0, w, c)),
                    box_distance(x, y, 0, (c - t) / 2, w, (c + t) / 2));
    case GLYPH_Y: {
        int32_t mid = w / 2;
        int32_t join = c * 46 / 100;
        int32_t inset = t * 7 / 10;
        int32_t arms = imin(
            band_distance(x, y, inset, 0, mid, join, t / 2, 0, 0, w, join),
            band_distance(x, y, w - inset, 0, mid, join, t / 2, 0, 0, w, join));
        return imin(arms, box_distance(x, y, mid - t / 2, join - t, mid + t / 2, c));
    }
    case GLYPH_D: {
        int32_t radius = c / 2;
        int32_t bowl_x = w - radius;
        return imin(
            imin(box_distance(x, y, 0, 0, t, c),
                 imin(box_distance(x, y, 0, 0, bowl_x, t),
                      box_distance(x, y, 0, c - t, bowl_x, c))),
            bowl_distance(x, y, bowl_x, radius, radius, radius - t));
    }
    case GLYPH_R: {
        int32_t bowl_h = c * 56 / 100;
        int32_t radius = bowl_h / 2;
        int32_t bowl_x = w - radius;
        int32_t stem_and_bars = imin(
            box_distance(x, y, 0, 0, t, c),
            imin(box_distance(x, y, 0, 0, bowl_x, t),
                 box_distance(x, y, 0, bowl_h - t, bowl_x, bowl_h)));
        int32_t leg = band_distance(x, y, w * 42 / 100, bowl_h - t / 2,
                                    w - t * 6 / 10, c, t / 2, 0, bowl_h - t, w, c);
        return imin(imin(stem_and_bars, leg),
                    bowl_distance(x, y, bowl_x, radius, radius, radius - t));
    }
    case GLYPH_G: {
        int32_t radius = c / 2;
        int32_t dx = x - radius;
        int32_t dy = y - radius;
        int32_t r = length(dx, dy);
        int32_t ring = imax(r - radius, radius - t - r);
        /* The opening is the sector between 0 and 40 degrees above the
           horizontal on the right; (41, 49) / 64 is (sin, cos) of 40 degrees. */
        int32_t opening = imin(-dy, (dx * 41 + dy * 49) / 64);
        return imin(imax(ring, opening),
                    box_distance(x, y, radius, radius - t / 2, c, radius + t / 2));
    }
    case GLYPH_E:
        return imin(
            imin(box_distance(x, y, 0, 0, t, c), box_distance(x, y, 0, 0, w, t)),
            imin(box_distance(x, y, 0, (c - t) / 2, w * 90 / 100, (c + t) / 2),
                 box_distance(x, y, 0, c - t, w, c)));
    case GLYPH_N: {
        int32_t inset = t * 7 / 10;
        return imin(imin(box_distance(x, y, 0, 0, t, c),
                         box_distance(x, y, w - t, 0, w, c)),
                    band_distance(x, y, inset, 0, w - inset, c, t / 2, 0, 0, w, c));
    }
    case GLYPH_O: {
        int32_t radius = c / 2;
        int32_t r = length(x - radius, y - radius);
        return imax(r - radius, radius - t - r);
    }
    case GLYPH_S: {
        /* Two rings that overlap by one stroke, each missing the sector
           (35 degrees above the horizontal down to the vertical) that faces
           away from the other ring; (37, 52) / 64 is (sin, cos) of 35 degrees. */
        int32_t radius = (c + t) / 4;
        int32_t cx = x - w / 2;
        int32_t upper_dy = y - radius;
        int32_t lower_dy = y - (c - radius);
        int32_t upper_r = length(cx, upper_dy);
        int32_t lower_r = length(cx, lower_dy);
        int32_t upper = imax(imax(upper_r - radius, radius - t - upper_r),
                             imin(cx, (cx * 37 + upper_dy * 52) / 64));
        int32_t lower = imax(imax(lower_r - radius, radius - t - lower_r),
                             imin(-cx, (-cx * 37 - lower_dy * 52) / 64));
        return imin(upper, lower);
    }
    case GLYPH_DROP:
        break;
    }
    return FAR;
}

static void draw_glyph(glyph_t glyph, int32_t gx, int32_t gy, int32_t w,
                       uint32_t weight) {
    for (int32_t py = gy / SUB - 1; py <= (gy + layout.cap) / SUB + 1; ++py) {
        for (int32_t px = gx / SUB - 1; px <= (gx + w) / SUB + 1; ++px) {
            uint32_t *pixel = surface_pixel(px, py);
            if (!pixel)
                continue;
            int32_t d = glyph_distance(glyph, px * SUB + SUB / 2 - gx,
                                       py * SUB + SUB / 2 - gy, w);
            int32_t cov = coverage(d);
            if (cov)
                *pixel = blend(*pixel, LETTER_COLOR, (uint32_t)cov * weight / SUB);
        }
    }
}

/* Source position of destination pixel `index` of `size` in a source of
   `source` pixels: a base pixel and a 0..255 weight towards its successor. */
static void sample_axis(int32_t index, int32_t size, int32_t source,
                        int32_t *base, uint32_t *weight) {
    int32_t position = ((2 * index + 1) * source * 256) / (2 * size) - 128;
    if (position < 0)
        position = 0;
    *base = position >> 8;
    *weight = (uint32_t)position & 0xFFu;
}

static uint32_t coverage_at(const uint8_t *bitmap, int32_t source, int32_t x,
                            int32_t y) {
    return bitmap[imin(y, source - 1) * source + imin(x, source - 1)];
}

/* The drop is the artwork's coverage bitmap, bilinearly resampled from the
   smallest bitmap that is not smaller than the target and coloured with the
   artwork's stroke gradient. Its bottom sits on the baseline of the cap box
   at (gx, gy, w), centred in the slot; the apex reaches above the letters. */
static void draw_drop(int32_t gx, int32_t gy, int32_t w) {
    const int32_t cap_pixels = layout.cap / SUB;
    const int32_t size =
        cap_pixels * DROP_HEIGHT / (1000 - 2 * LOGO_PADDING_PERMILLE);
    int pick = LOGO_SIZE_COUNT - 1;
    for (int i = 0; i < LOGO_SIZE_COUNT; ++i) {
        if (logo_sizes[i] >= size) {
            pick = i;
            break;
        }
    }
    const uint8_t *bitmap = logo_coverage[pick];
    const int32_t source = logo_sizes[pick];
    const int32_t left = (gx + w / 2) / SUB - size / 2;
    const int32_t bottom = (gy + layout.cap) / SUB + cap_pixels * DROP_OVERSHOOT / 1000 +
                           size * LOGO_PADDING_PERMILLE / 1000;
    const int32_t top = bottom - size;

    for (int32_t py = 0; py < size; ++py) {
        int32_t y0;
        uint32_t wy;
        sample_axis(py, size, source, &y0, &wy);
        uint32_t color = logo_gradient_at(logo_gradient_dark, size, py);
        for (int32_t px = 0; px < size; ++px) {
            uint32_t *pixel = surface_pixel(left + px, top + py);
            if (!pixel)
                continue;
            int32_t x0;
            uint32_t wx;
            sample_axis(px, size, source, &x0, &wx);
            uint32_t upper = coverage_at(bitmap, source, x0, y0) * (256u - wx) +
                             coverage_at(bitmap, source, x0 + 1, y0) * wx;
            uint32_t lower = coverage_at(bitmap, source, x0, y0 + 1) * (256u - wx) +
                             coverage_at(bitmap, source, x0 + 1, y0 + 1) * wx;
            uint32_t alpha = ((upper >> 8) * (256u - wy) + (lower >> 8) * wy) >> 8;
            if (alpha)
                *pixel = blend(*pixel, color, alpha + (alpha >> 7));
        }
    }
}

/* ---- Progress bar ---- */

static uint32_t bar_fill_width(void) {
    if (progress == 0)
        return 0;
    uint32_t w = (uint32_t)layout.bar_w * progress / PROGRESS_MAX;
    return w < (uint32_t)layout.bar_h ? (uint32_t)layout.bar_h : w;
}

static void draw_bar(void) {
    const int32_t h = layout.bar_h;
    const int32_t radius = h * SUB / 2;
    const int32_t fill_end = layout.bar_x + (int32_t)bar_fill_width();

    for (int32_t py = layout.bar_y - 1; py <= layout.bar_y + h; ++py) {
        for (int32_t px = layout.bar_x - 1; px <= layout.bar_x + layout.bar_w; ++px) {
            uint32_t *pixel = surface_pixel(px, py);
            if (!pixel)
                continue;
            int32_t sx = px * SUB + SUB / 2;
            int32_t sy = py * SUB + SUB / 2 - layout.bar_y * SUB - radius;
            int32_t left = layout.bar_x * SUB + radius;
            int32_t track_right = (layout.bar_x + layout.bar_w) * SUB - radius;
            int32_t fill_right = fill_end * SUB - radius;
            int32_t track_d = length(imax(imax(left - sx, sx - track_right), 0), sy) - radius;
            uint32_t color = background_pixel((uint32_t)px, (uint32_t)py);
            uint32_t track = (uint32_t)coverage(track_d) * BAR_TRACK_WEIGHT / SUB;
            color = blend(color, BAR_FILL, track);
            if (progress != 0) {
                int32_t fill_d =
                    length(imax(imax(left - sx, sx - fill_right), 0), sy) - radius;
                color = blend(color, BAR_FILL, (uint32_t)coverage(fill_d) * 16u);
            }
            *pixel = color;
        }
    }
    present(layout.bar_x - 1, layout.bar_y - 1, layout.bar_w + 2, h + 2);
}

static void set_progress(uint32_t value) {
    if (state != BOOTSPLASH_ACTIVE)
        return;
    if (value > PROGRESS_MAX)
        value = PROGRESS_MAX;
    if (value <= progress)
        return;
    progress = value;
    draw_bar();
}

/* ---- Layout and first paint ---- */

static void compute_layout(void) {
    layout.width = framebuffer.width;
    layout.height = framebuffer.height;
    int32_t cap = (int32_t)imin((int32_t)(layout.height * 85 / 1000),
                                (int32_t)(layout.width * 8 / 100));
    if (cap < 8)
        cap = 8;
    layout.cap = cap * SUB;
    layout.stroke = layout.cap * STROKE / 1000;
    layout.bar_w = (int32_t)(layout.width * 26 / 100);
    layout.bar_h = imax(3, (int32_t)(layout.height / 190));
    layout.bar_x = ((int32_t)layout.width - layout.bar_w) / 2;
    layout.bar_y = (int32_t)(layout.height * 45 / 100) + cap * 23 / 10 + layout.bar_h;
}

static int32_t wordmark_width(void) {
    int32_t total = (int32_t)(WORDMARK_LETTERS - 1) * (layout.cap * LETTER_GAP / 1000);
    for (uint32_t i = 0; i < WORDMARK_LETTERS; ++i)
        total += layout.cap * wordmark[i].width / 1000;
    return total;
}

static void paint(void) {
    for (uint32_t y = 0; y < layout.height; ++y) {
        uint32_t *row = surface + (size_t)y * stride_words;
        for (uint32_t x = 0; x < layout.width; ++x)
            row[x] = background_pixel(x, y);
    }

    int32_t x = ((int32_t)layout.width * SUB - wordmark_width()) / 2;
    int32_t y = ((int32_t)layout.height * 45 / 100) * SUB - layout.cap / 2;
    for (uint32_t i = 0; i < WORDMARK_LETTERS; ++i) {
        int32_t w = layout.cap * wordmark[i].width / 1000;
        if (wordmark[i].glyph == GLYPH_DROP)
            draw_drop(x, y, w);
        else
            draw_glyph(wordmark[i].glyph, x, y, w, wordmark[i].weight);
        x += w + layout.cap * LETTER_GAP / 1000;
    }
    draw_bar();
    present(0, 0, (int32_t)layout.width, (int32_t)layout.height);
}

void bootsplash_start(bootsplash_stage_t stage, int enabled) {
    state = BOOTSPLASH_DISABLED;
    progress = 0;
    if (!enabled)
        return;
    surface = fb_console_surface();
    if (!surface || !framebuffer.addr || framebuffer.bpp != 32 ||
        framebuffer.width == 0 || framebuffer.height == 0)
        return;
    stride_words = framebuffer.pitch / 4;
    compute_layout();
    state = BOOTSPLASH_ACTIVE;
    fb_console_set_suspended(1);
    progress = stage_progress[stage];
    paint();
}

void bootsplash_stage(bootsplash_stage_t stage) {
    set_progress(stage_progress[stage]);
}

/* ---- Reports from init ---- */

static int parse_number(const char **cursor, uint32_t *out) {
    const char *s = *cursor;
    if (*s < '0' || *s > '9')
        return -1;
    uint64_t value = 0;
    while (*s >= '0' && *s <= '9') {
        value = value * 10u + (uint64_t)(*s - '0');
        if (value > 0xFFFFFFFFu)
            return -1;
        ++s;
    }
    *cursor = s;
    *out = (uint32_t)value;
    return 0;
}

static int has_prefix(const char *s, const char *prefix, const char **rest) {
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    *rest = s;
    return 1;
}

int bootsplash_report(const uint8_t *data, uint32_t size, int from_init) {
    char line[REPORT_CAP];
    uint32_t len = 0;
    if (!data)
        return -EINVAL;
    while (len < size && data[len] != '\n' && data[len] != '\0') {
        if (len + 1 >= sizeof(line))
            return -EINVAL;
        line[len] = (char)data[len];
        ++len;
    }
    line[len] = '\0';

    const char *rest;
    if (has_prefix(line, "end", &rest) && *rest == '\0') {
        if (!from_init)
            return -EROFS;
        bootsplash_abort();
        return 0;
    }
    uint32_t done = 0;
    uint32_t total = 0;
    if (!has_prefix(line, "services ", &rest) || parse_number(&rest, &done) != 0 ||
        *rest++ != ' ' || parse_number(&rest, &total) != 0 || *rest != '\0' ||
        total == 0 || done > total)
        return -EINVAL;
    if (!from_init)
        return -EROFS;
    set_progress(SERVICES_BASE +
                 (uint32_t)((uint64_t)(PROGRESS_MAX - SERVICES_BASE) * done / total));
    return 0;
}

/* ---- Hand-off ---- */

static void finish(void) {
    fb_console_set_suspended(0);
    state = BOOTSPLASH_FINISHED;
}

void bootsplash_abort(void) {
    if (state != BOOTSPLASH_ACTIVE)
        return;
    finish();
    fb_console_clear();
}

/* Moves every pixel a quarter of the way to the new frame per step, which
   eases out, and leaves the exact frame to the caller's normal blit. */
static void fade_into(const uint32_t *src, size_t stride_bytes, int32_t x, int32_t y) {
    for (int step = 0; step < FADE_STEPS; ++step) {
        for (uint32_t py = 0; py < layout.height; ++py) {
            uint32_t *dst = surface + (size_t)py * stride_words;
            const uint32_t *from = (const uint32_t *)((const uint8_t *)src +
                                                      (size_t)((int32_t)py - y) * stride_bytes);
            for (uint32_t px = 0; px < layout.width; ++px) {
                uint32_t out = 0;
                for (int shift = 0; shift <= 16; shift += 8) {
                    int32_t now = (int32_t)channel(dst[px], shift);
                    int32_t target = (int32_t)channel(from[(int32_t)px - x], shift);
                    out |= (uint32_t)(now + ((target - now) >> 2)) << shift;
                }
                dst[px] = out;
            }
        }
        present(0, 0, (int32_t)layout.width, (int32_t)layout.height);
    }
}

int bootsplash_filter_blit(int x, int y, int w, int h, const uint32_t *src,
                           size_t stride_bytes) {
    if (state != BOOTSPLASH_ACTIVE)
        return 0;
    if (x > 0 || y > 0 || (int64_t)x + w < (int64_t)layout.width ||
        (int64_t)y + h < (int64_t)layout.height)
        return 1;
    set_progress(PROGRESS_MAX);
    fade_into(src, stride_bytes, x, y);
    finish();
    serial_write("[SPLASH] handoff to desktop\n");
    return 0;
}

bootsplash_state_t bootsplash_state(void) {
    return state;
}

uint32_t bootsplash_progress(void) {
    return progress;
}
