#include "bootsplash.h"

#include "errno.h"
#include "fb_console.h"
#include "framebuffer.h"
#include "serial.h"
#include "logo_splash.h"
#include "wordmark.h"

/* Everything is drawn with integer arithmetic only: the kernel keeps no FPU
   state. The wordmark and the drop are pre-rasterised coverage bitmaps; only
   the progress bar is a signed distance function in 1/SUB pixel units that is
   turned into anti-aliased coverage. */
#define SUB 16
#define PROGRESS_MAX 1000u
#define SERVICES_BASE 300u
#define REPORT_CAP 48
#define FADE_STEPS 12

#define BG_TOP 0x001B2530u
#define BG_BOTTOM 0x0005070Au
#define LETTER_COLOR 0x00EEF6FFu
#define BAR_FILL 0x00F0F7FFu
#define BAR_TRACK_WEIGHT 36u

/* Sizes relative to the screen height, in per mille: the cap height of the
   wordmark, the visible height of the drop above it, and the gaps. */
#define CAP_PER_MILLE 50
#define DROP_PER_MILLE 130
#define DROP_GAP_CAPS_PER_MILLE 1100
#define BAR_GAP_CAPS_PER_MILLE 2200
#define GROUP_CENTER_PER_MILLE 430

/* Progress after each kernel stage; init's services fill the rest. */
static const uint16_t stage_progress[] = { 80, 150, 230, SERVICES_BASE };

typedef struct splash_layout {
    uint32_t width;
    uint32_t height;
    int32_t wordmark_x;
    int32_t wordmark_y;
    int32_t wordmark_w;
    int32_t wordmark_h;
    int pick;
    int32_t drop_x;
    int32_t drop_y;
    int32_t drop_size;
    int drop_pick;
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

static uint32_t coverage_at(const uint8_t *bitmap, int32_t width, int32_t height,
                            int32_t x, int32_t y) {
    return bitmap[imin(y, height - 1) * width + imin(x, width - 1)];
}

/* Draws a coverage bitmap bilinearly resampled to dw x dh at (left, top). The
   colour is `color`, or the row's colour of `gradient` (see logo_gradient_at)
   when one is given. Resampling by one is exact, so a bitmap drawn at its own
   size keeps its hinted edges. */
static void draw_coverage(const uint8_t *bitmap, int32_t sw, int32_t sh, int32_t left,
                          int32_t top, int32_t dw, int32_t dh, uint32_t color,
                          const uint32_t *gradient) {
    for (int32_t py = 0; py < dh; ++py) {
        int32_t y0;
        uint32_t wy;
        sample_axis(py, dh, sh, &y0, &wy);
        uint32_t row_color = gradient ? logo_gradient_at(gradient, dh, py) : color;
        for (int32_t px = 0; px < dw; ++px) {
            uint32_t *pixel = surface_pixel(left + px, top + py);
            if (!pixel)
                continue;
            int32_t x0;
            uint32_t wx;
            sample_axis(px, dw, sw, &x0, &wx);
            uint32_t upper = coverage_at(bitmap, sw, sh, x0, y0) * (256u - wx) +
                             coverage_at(bitmap, sw, sh, x0 + 1, y0) * wx;
            uint32_t lower = coverage_at(bitmap, sw, sh, x0, y0 + 1) * (256u - wx) +
                             coverage_at(bitmap, sw, sh, x0 + 1, y0 + 1) * wx;
            uint32_t alpha = ((upper >> 8) * (256u - wy) + (lower >> 8) * wy) >> 8;
            if (alpha)
                *pixel = blend(*pixel, row_color, alpha + (alpha >> 7));
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

/* Index of the smallest bitmap of `sizes` that is not smaller than `wanted`,
   or the largest one. */
static int pick_size(const uint16_t *sizes, int count, int32_t wanted) {
    for (int i = 0; i < count; ++i) {
        if (sizes[i] >= wanted)
            return i;
    }
    return count - 1;
}

static int pick_nearest(const uint16_t *sizes, int count, int32_t wanted) {
    int best = 0;
    for (int i = 1; i < count; ++i) {
        if (imax(sizes[i], wanted) - imin(sizes[i], wanted) <
            imax(sizes[best], wanted) - imin(sizes[best], wanted))
            best = i;
    }
    return best;
}

/* The group of drop above wordmark is centred around GROUP_CENTER_PER_MILLE of
   the height, with the progress bar below it. */
static void compute_layout(void) {
    const int32_t width = (int32_t)framebuffer.width;
    const int32_t height = (int32_t)framebuffer.height;
    layout.width = framebuffer.width;
    layout.height = framebuffer.height;

    int32_t cap = imax(8, height * CAP_PER_MILLE / 1000);
    layout.pick = pick_nearest(wordmark_caps, WORDMARK_SIZE_COUNT, cap);
    layout.wordmark_w = (wordmark_widths[layout.pick] * cap + wordmark_caps[layout.pick] / 2) /
                        wordmark_caps[layout.pick];
    layout.wordmark_h = (wordmark_heights[layout.pick] * cap + wordmark_caps[layout.pick] / 2) /
                        wordmark_caps[layout.pick];

    int32_t drop_visible = imax(16, height * DROP_PER_MILLE / 1000);
    layout.drop_size = drop_visible * 1000 / (1000 - 2 * LOGO_PADDING_PERMILLE);
    layout.drop_pick = pick_size(logo_sizes, LOGO_SIZE_COUNT, layout.drop_size);
    int32_t gap = cap * DROP_GAP_CAPS_PER_MILLE / 1000;

    int32_t group = drop_visible + gap + cap;
    int32_t group_top = height * GROUP_CENTER_PER_MILLE / 1000 - group / 2;
    int32_t cap_top = group_top + drop_visible + gap;
    layout.drop_x = (width - layout.drop_size) / 2;
    layout.drop_y = group_top - layout.drop_size * LOGO_PADDING_PERMILLE / 1000;
    layout.wordmark_x = (width - layout.wordmark_w) / 2;
    layout.wordmark_y = cap_top + cap / 2 - layout.wordmark_h / 2;

    layout.bar_w = width * 26 / 100;
    layout.bar_h = imax(3, height / 190);
    layout.bar_x = (width - layout.bar_w) / 2;
    layout.bar_y = cap_top + cap + cap * BAR_GAP_CAPS_PER_MILLE / 1000;
}

static void paint(void) {
    for (uint32_t y = 0; y < layout.height; ++y) {
        uint32_t *row = surface + (size_t)y * stride_words;
        for (uint32_t x = 0; x < layout.width; ++x)
            row[x] = background_pixel(x, y);
    }

    /* The drop is the artwork's own coverage, coloured with its stroke gradient. */
    draw_coverage(logo_coverage[layout.drop_pick], logo_sizes[layout.drop_pick],
                  logo_sizes[layout.drop_pick], layout.drop_x, layout.drop_y,
                  layout.drop_size, layout.drop_size, 0, logo_gradient_dark);
    draw_coverage(wordmark_coverage[layout.pick], wordmark_widths[layout.pick],
                  wordmark_heights[layout.pick], layout.wordmark_x, layout.wordmark_y,
                  layout.wordmark_w, layout.wordmark_h, LETTER_COLOR, NULL);
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
