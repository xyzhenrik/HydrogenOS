#include "widgets.h"
#include "gfx_effects.h"
#include "theme.h"

static const uint8_t widget_font8x16[256][16] = {
    [32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [33] = {0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    [34] = {0x00,0x36,0x36,0x36,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [35] = {0x00,0x36,0x36,0x7F,0x36,0x36,0x36,0x7F,0x36,0x36,0x36,0x00,0x00,0x00,0x00,0x00},
    [36] = {0x18,0x18,0x7E,0xDB,0xD8,0x7E,0x1B,0x1B,0xDB,0x7E,0x18,0x18,0x00,0x00,0x00,0x00},
    [37] = {0x00,0x00,0x63,0x73,0x38,0x18,0x0C,0x0E,0x67,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [38] = {0x00,0x1C,0x36,0x36,0x1C,0x6E,0xDB,0xDB,0xDB,0x6E,0x00,0x00,0x00,0x00,0x00,0x00},
    [39] = {0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [40] = {0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    [41] = {0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    [42] = {0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [43] = {0x00,0x00,0x18,0x18,0x7E,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [44] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00},
    [45] = {0x00,0x00,0x00,0x00,0x00,0x7E,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [46] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    [47] = {0x00,0x03,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00},
    [48] = {0x00,0x3E,0x63,0x73,0x7B,0x6F,0x67,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [49] = {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},
    [50] = {0x00,0x3E,0x63,0x03,0x06,0x0C,0x18,0x30,0x60,0x7F,0x00,0x00,0x00,0x00,0x00,0x00},
    [51] = {0x00,0x7F,0x06,0x0C,0x18,0x0C,0x06,0x03,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [52] = {0x00,0x06,0x0E,0x1E,0x36,0x66,0x7F,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00},
    [53] = {0x00,0x7F,0x60,0x60,0x7E,0x03,0x03,0x03,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [54] = {0x00,0x1C,0x30,0x60,0x7E,0x63,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [55] = {0x00,0x7F,0x03,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00,0x00},
    [56] = {0x00,0x3E,0x63,0x63,0x3E,0x63,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [57] = {0x00,0x3E,0x63,0x63,0x63,0x3F,0x03,0x06,0x0C,0x38,0x00,0x00,0x00,0x00,0x00,0x00},
    [58] = {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    [59] = {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    [60] = {0x00,0x0C,0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00},
    [61] = {0x00,0x00,0x00,0x7E,0x7E,0x00,0x00,0x7E,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [62] = {0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00},
    [63] = {0x00,0x3E,0x63,0x63,0x06,0x0C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    [64] = {0x00,0x3E,0x63,0x63,0x7B,0x7B,0x7B,0x7B,0x60,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [65] = {0x00,0x3E,0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [66] = {0x00,0x7E,0x63,0x63,0x7E,0x63,0x63,0x63,0x63,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},
    [67] = {0x00,0x3E,0x63,0x60,0x60,0x60,0x60,0x60,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [68] = {0x00,0x7C,0x66,0x63,0x63,0x63,0x63,0x63,0x66,0x7C,0x00,0x00,0x00,0x00,0x00,0x00},
    [69] = {0x00,0x7F,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x7F,0x00,0x00,0x00,0x00,0x00,0x00},
    [70] = {0x00,0x7F,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00,0x00},
    [71] = {0x00,0x3E,0x63,0x60,0x60,0x67,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [72] = {0x00,0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [73] = {0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [74] = {0x00,0x1F,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [75] = {0x00,0x63,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [76] = {0x00,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7F,0x00,0x00,0x00,0x00,0x00,0x00},
    [77] = {0x00,0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [78] = {0x00,0x63,0x73,0x7B,0x7F,0x6F,0x67,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [79] = {0x00,0x3E,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [80] = {0x00,0x7E,0x63,0x63,0x7E,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00,0x00},
    [81] = {0x00,0x3E,0x63,0x63,0x63,0x63,0x63,0x73,0x6B,0x3E,0x06,0x07,0x00,0x00,0x00,0x00},
    [82] = {0x00,0x7E,0x63,0x63,0x7E,0x78,0x6C,0x66,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [83] = {0x00,0x3E,0x63,0x60,0x3C,0x06,0x03,0x03,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [84] = {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    [85] = {0x00,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [86] = {0x00,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x36,0x1C,0x00,0x00,0x00,0x00,0x00,0x00},
    [87] = {0x00,0x63,0x63,0x63,0x6B,0x6B,0x7F,0x7F,0x77,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [88] = {0x00,0x63,0x63,0x36,0x1C,0x1C,0x1C,0x36,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [89] = {0x00,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [90] = {0x00,0x7F,0x03,0x06,0x0C,0x18,0x30,0x60,0x60,0x7F,0x00,0x00,0x00,0x00,0x00,0x00},
    [91] = {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [92] = {0x00,0xC0,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00},
    [93] = {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [94] = {0x00,0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [95] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00},
    [96] = {0x00,0x18,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [97] = {0x00,0x00,0x00,0x00,0x3E,0x63,0x63,0x63,0x63,0x6E,0x00,0x00,0x00,0x00,0x00,0x00},
    [98] = {0x00,0x60,0x60,0x60,0x7C,0x66,0x63,0x63,0x63,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},
    [99] = {0x00,0x00,0x00,0x00,0x3E,0x63,0x60,0x60,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [100] = {0x00,0x03,0x03,0x03,0x3F,0x67,0x63,0x63,0x67,0x3F,0x00,0x00,0x00,0x00,0x00,0x00},
    [101] = {0x00,0x00,0x00,0x00,0x3E,0x63,0x7F,0x60,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [102] = {0x00,0x0F,0x18,0x18,0x7E,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    [103] = {0x00,0x00,0x00,0x00,0x3B,0x6E,0x66,0x66,0x3E,0x06,0x06,0x3C,0x00,0x00,0x00,0x00},
    [104] = {0x00,0x60,0x60,0x60,0x7C,0x66,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [105] = {0x00,0x18,0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [106] = {0x00,0x06,0x00,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00,0x00},
    [107] = {0x00,0x60,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [108] = {0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00},
    [109] = {0x00,0x00,0x00,0x00,0x76,0x7F,0x6B,0x6B,0x6B,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [110] = {0x00,0x00,0x00,0x00,0x7C,0x66,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [111] = {0x00,0x00,0x00,0x00,0x3E,0x63,0x63,0x63,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [112] = {0x00,0x00,0x00,0x00,0x7E,0x63,0x63,0x63,0x7E,0x60,0x60,0x60,0x00,0x00,0x00,0x00},
    [113] = {0x00,0x00,0x00,0x00,0x3F,0x67,0x63,0x63,0x3F,0x03,0x03,0x03,0x00,0x00,0x00,0x00},
    [114] = {0x00,0x00,0x00,0x00,0x6E,0x73,0x63,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00,0x00},
    [115] = {0x00,0x00,0x00,0x00,0x3E,0x63,0x38,0x0E,0x63,0x3E,0x00,0x00,0x00,0x00,0x00,0x00},
    [116] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00,0x00},
    [117] = {0x00,0x00,0x00,0x00,0x63,0x63,0x63,0x63,0x67,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    [118] = {0x00,0x00,0x00,0x00,0x63,0x63,0x63,0x63,0x36,0x1C,0x00,0x00,0x00,0x00,0x00,0x00},
    [119] = {0x00,0x00,0x00,0x00,0x63,0x63,0x6B,0x7F,0x7F,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    [120] = {0x00,0x00,0x00,0x00,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00,0x00,0x00,0x00,0x00,0x00},
    [121] = {0x00,0x00,0x00,0x00,0x63,0x63,0x63,0x63,0x3F,0x03,0x06,0x3C,0x00,0x00,0x00,0x00},
    [122] = {0x00,0x00,0x00,0x00,0x7F,0x06,0x0C,0x18,0x30,0x7F,0x00,0x00,0x00,0x00,0x00,0x00}
};


static char *widget_strdup(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (!d) return 0;
    memcpy(d, s, len + 1);
    return d;
}

/* ---------- Surface ---------- */

Surface *surface_create(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    Surface *s = (Surface *)malloc(sizeof(Surface));
    if (!s) return 0;
    memset(s, 0, sizeof(Surface));
    s->width = width;
    s->height = height;
    s->stride = width;
    s->pitch = width;
    s->w = width;
    s->h = height;
    s->pixels = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    s->buffer = s->pixels;
    s->data = s->pixels;
    if (!s->pixels) { free(s); return 0; }
    memset(s->pixels, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
    return s;
}

Surface *surface_new(int width, int height) { return surface_create(width, height); }

void surface_destroy(Surface *surface) {
    if (!surface) return;
    if (surface->pixels) free(surface->pixels);
    free(surface);
}

void surface_clear(Surface *surface, uint32_t color) {
    if (!surface || !surface->pixels) return;
    size_t n = (size_t)surface->width * (size_t)surface->height;
    for (size_t i = 0; i < n; ++i) surface->pixels[i] = color;
}

void surface_put_pixel(Surface *surface, int x, int y, uint32_t color) {
    if (!surface || !surface->pixels) return;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height) return;
    surface->pixels[y * surface->stride + x] = color;
}

void surface_fill_rect(Surface *surface, int x, int y, int w, int h, uint32_t color) {
    if (!surface || !surface->pixels || w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            surface_put_pixel(surface, x + xx, y + yy, color);
}

void surface_draw_rect(Surface *surface, int x, int y, int w, int h, uint32_t color) {
    if (!surface || w <= 0 || h <= 0) return;
    for (int xx = 0; xx < w; ++xx) {
        surface_put_pixel(surface, x + xx, y, color);
        surface_put_pixel(surface, x + xx, y + h - 1, color);
    }
    for (int yy = 0; yy < h; ++yy) {
        surface_put_pixel(surface, x, y + yy, color);
        surface_put_pixel(surface, x + w - 1, y + yy, color);
    }
}

int surface_text_width(const char *text) {
    if (!text) return 0;
    return (int)strlen(text) * 8;
}

int surface_text_height(void) { return 16; }

void surface_draw_text(Surface *surface, int x, int y, const char *text, uint32_t color) {
    if (!surface || !text) return;
    int cx = x;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\n') { cx = x; y += 16; continue; }
        if (ch < 32) continue;
        const uint8_t *glyph = widget_font8x16[ch];
        for (int row = 0; row < 16; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1 << (7 - col)))
                    surface_put_pixel(surface, cx + col, y + row, color);
            }
        }
        cx += 8;
    }
}

/* ---------- Label ---------- */

static void label_sync(Label *label) {
    if (!label) return;
    if (!label->type) label->type = WIDGET_LABEL;
    if (!label->text && label->string) label->text = label->string;
    if (!label->text && label->str) label->text = label->str;
    if (!label->text && label->caption) label->text = label->caption;
    if (!label->string) label->string = label->text;
    if (!label->str) label->str = label->text;
    if (!label->caption) label->caption = label->text;
    if (label->color == 0 && label->fg) label->color = label->fg;
    if (label->fg == 0) label->fg = label->color ? label->color : 0xFFFFFFFFu;
    if (label->color == 0) label->color = 0xFFFFFFFFu;
    if (label->fg_color == 0) label->fg_color = label->fg;
    if (label->foreground == 0) label->foreground = label->color;
    if (label->foreground_color == 0) label->foreground_color = label->foreground;
    if (label->text_color == 0) label->text_color = label->color;
    if (label->w == 0 && label->text) label->w = (int)strlen(label->text) * 8;
    if (label->h == 0) label->h = 16;
    label->width = label->w;
    label->height = label->h;
    if (!label->visible) label->visible = 1;
}

Label *label_create(const char *text, uint32_t color) {
    Label *label = (Label *)malloc(sizeof(Label));
    if (!label) return 0;
    memset(label, 0, sizeof(Label));
    label->type = WIDGET_LABEL;
    label->text = widget_strdup(text ? text : "");
    label->string = label->text;
    label->str = label->text;
    label->caption = label->text;
    label->color = color ? color : 0xFFFFFFFFu;
    label->fg = label->color;
    label->fg_color = label->color;
    label->foreground = label->color;
    label->foreground_color = label->color;
    label->text_color = label->color;
    label->w = (int)strlen(label->text) * 8;
    label->h = 16;
    label->width = label->w;
    label->height = label->h;
    label->visible = 1;
    return label;
}

Label *label_new(const char *text, uint32_t color) { return label_create(text, color); }

void label_set_text(Label *label, const char *text) {
    if (!label) return;
    if (label->text) free(label->text);
    label->text = widget_strdup(text ? text : "");
    label->string = label->text;
    label->str = label->text;
    label->caption = label->text;
    label_sync(label);
}

void label_set_color(Label *label, uint32_t color) {
    if (!label) return;
    label->color = color;
    label->fg = color;
    label->fg_color = color;
    label->foreground = color;
    label->foreground_color = color;
    label->text_color = color;
}

void label_set_pos(Label *label, int x, int y) {
    if (!label) return;
    label->x = x;
    label->y = y;
}

void label_render(Label *label, Surface *surface) {
    if (!label || !surface) return;
    label_sync(label);
    if (!label->visible) return;
    surface_draw_text(surface, label->x, label->y, label->text ? label->text : "", label->color);
}

void label_draw(Label *label, Surface *surface) { label_render(label, surface); }
void label_paint(Label *label, Surface *surface) { label_render(label, surface); }
void render_label(Label *label, Surface *surface) { label_render(label, surface); }
void draw_label(Label *label, Surface *surface) { label_render(label, surface); }
void paint_label(Label *label, Surface *surface) { label_render(label, surface); }

/* ---------- Button ---------- */

static void button_sync(Button *button) {
    if (!button) return;
    if (!button->type) button->type = WIDGET_BUTTON;
    if (!button->label && button->text) button->label = button->text;
    if (!button->label && button->caption) button->label = button->caption;
    if (!button->label && button->title) button->label = button->title;
    if (!button->text) button->text = button->label;
    if (!button->caption) button->caption = button->label;
    if (!button->title) button->title = button->label;
    if (button->color == 0 && button->fg) button->color = button->fg;
    if (button->fg == 0) button->fg = button->color ? button->color : 0xFFFFFFFFu;
    if (button->color == 0) button->color = 0xFFFFFFFFu;
    if (button->fg_color == 0) button->fg_color = button->fg;
    if (button->bg_color == 0 && button->background) button->bg_color = button->background;
    if (button->background == 0) button->background = button->bg_color ? button->bg_color : 0x404040u;
    if (button->bg_color == 0) button->bg_color = 0x404040u;
    if (button->background_color == 0) button->background_color = button->bg_color;
    if (!button->userdata) button->userdata = button->user_data;
    if (!button->user_data) button->user_data = button->userdata;
    if (!button->context) button->context = button->userdata;
    if (button->w == 0 && button->label) button->w = (int)strlen(button->label) * 8 + 16;
    if (button->h == 0) button->h = 24;
    button->width = button->w;
    button->height = button->h;
    if (!button->visible) button->visible = 1;
}

Button *button_create_impl(const char *label, void (*on_click)()) {
    Button *button = (Button *)malloc(sizeof(Button));
    if (!button) return 0;
    memset(button, 0, sizeof(Button));
    button->type = WIDGET_BUTTON;
    button->label = widget_strdup(label ? label : "");
    button->text = button->label;
    button->caption = button->label;
    button->title = button->label;
    button->color = 0xFFFFFFFFu;
    button->fg = button->color;
    button->fg_color = button->color;
    button->bg_color = 0x404040u;
    button->background = button->bg_color;
    button->background_color = button->bg_color;
    button->on_click = on_click;
    button->corner_radius = theme_default()->effects.button_radius;
    button->w = (int)strlen(button->label) * 8 + 16;
    button->h = 24;
    button->width = button->w;
    button->height = button->h;
    button->visible = 1;
    return button;
}

Button *button_new_impl(const char *label, void (*on_click)()) { return button_create_impl(label, on_click); }

void button_set_label(Button *button, const char *label) {
    if (!button) return;
    if (button->label) free(button->label);
    button->label = widget_strdup(label ? label : "");
    button->text = button->label;
    button->caption = button->label;
    button->title = button->label;
    button_sync(button);
}

void button_set_on_click_impl(Button *button, void (*on_click)()) {
    if (!button) return;
    button->on_click = on_click;
}

void button_click(Button *button) {
    if (!button) return;
    if (button->on_click) button->on_click();
}

void button_render(Button *button, Surface *surface) {
    if (!button || !surface) return;
    button_sync(button);
    if (!button->visible) return;
    uint32_t bg = button->pressed ? 0x202020u : button->bg_color;
    GfxCanvas canvas = { surface->pixels, surface->width, surface->height, surface->stride, 0 };
    gfx_fill_round_rect(&canvas, button->x, button->y, button->w, button->h,
                        button->corner_radius, bg, 255);
    /* A faint 1px edge reads as material; a hard outline would not. */
    gfx_stroke_round_rect(&canvas, button->x, button->y, button->w, button->h,
                          button->corner_radius, 0xFFFFFFu, 64);
    const char *txt = button->label ? button->label : "";
    int tw = (int)strlen(txt) * 8;
    int tx = button->x + (button->w - tw) / 2;
    int ty = button->y + (button->h - 16) / 2;
    surface_draw_text(surface, tx, ty, txt, button->color);
}

void button_draw(Button *button, Surface *surface) { button_render(button, surface); }
void button_paint(Button *button, Surface *surface) { button_render(button, surface); }
void render_button(Button *button, Surface *surface) { button_render(button, surface); }
void draw_button(Button *button, Surface *surface) { button_render(button, surface); }
void paint_button(Button *button, Surface *surface) { button_render(button, surface); }

/* ---------- TextInput ---------- */

static void textinput_sync(TextInput *input) {
    if (!input) return;
    if (!input->type) input->type = WIDGET_TEXTINPUT;
    if (!input->buffer) {
        size_t cap = input->capacity ? input->capacity : 64;
        input->buffer = (char *)malloc(cap + 1);
        if (input->buffer) { input->buffer[0] = '\0'; input->capacity = cap; }
    }
    if (!input->text) input->text = input->buffer;
    if (!input->str) input->str = input->buffer;
    if (!input->value) input->value = input->buffer;
    if (input->buffer) {
        input->text = input->buffer;
        input->str = input->buffer;
        input->value = input->buffer;
    }
    if (input->color == 0 && input->fg) input->color = input->fg;
    if (input->fg == 0) input->fg = input->color ? input->color : 0xFFFFFFFFu;
    if (input->color == 0) input->color = 0xFFFFFFFFu;
    if (input->fg_color == 0) input->fg_color = input->fg;
    if (input->text_color == 0) input->text_color = input->color;
    if (input->bg_color == 0 && input->background) input->bg_color = input->background;
    if (input->background == 0) input->background = input->bg_color ? input->bg_color : 0x202020u;
    if (input->bg_color == 0) input->bg_color = 0x202020u;
    if (input->background_color == 0) input->background_color = input->bg_color;
    if (input->capacity == 0) input->capacity = 64;
    if (input->cursor < 0) input->cursor = 0;
    size_t l = input->buffer ? strlen(input->buffer) : 0;
    input->size = l;
    input->len = l;
    input->length = l;
    if (input->cursor > (int)l) input->cursor = (int)l;
    input->cursor_pos = input->cursor;
    input->pos = input->cursor;
    input->caret = input->cursor;
    if (input->w == 0) input->w = (int)(input->capacity * 8);
    if (input->h == 0) input->h = 20;
    input->width = input->w;
    input->height = input->h;
    if (!input->visible) input->visible = 1;
}

TextInput *textinput_create(size_t capacity) {
    if (capacity == 0) capacity = 64;
    TextInput *input = (TextInput *)malloc(sizeof(TextInput));
    if (!input) return 0;
    memset(input, 0, sizeof(TextInput));
    input->type = WIDGET_TEXTINPUT;
    input->capacity = capacity;
    input->buffer = (char *)malloc(capacity + 1);
    if (!input->buffer) { free(input); return 0; }
    input->buffer[0] = '\0';
    input->text = input->buffer;
    input->str = input->buffer;
    input->value = input->buffer;
    input->color = 0xFFFFFFFFu;
    input->fg = input->color;
    input->fg_color = input->color;
    input->text_color = input->color;
    input->bg_color = 0x202020u;
    input->background = input->bg_color;
    input->background_color = input->bg_color;
    input->cursor = 0;
    input->cursor_pos = 0;
    input->pos = 0;
    input->caret = 0;
    input->w = (int)(capacity * 8);
    input->h = 20;
    input->width = input->w;
    input->height = input->h;
    input->visible = 1;
    input->focused = 1;
    return input;
}

TextInput *text_input_create(size_t capacity) { return textinput_create(capacity); }
TextInput *textinput_new(size_t capacity) { return textinput_create(capacity); }

void textinput_set_text(TextInput *input, const char *text) {
    if (!input) return;
    textinput_sync(input);
    if (!input->buffer) return;
    size_t i = 0;
    if (text) {
        while (text[i] && i < input->capacity) { input->buffer[i] = text[i]; ++i; }
    }
    input->buffer[i] = '\0';
    input->cursor = (int)i;
    textinput_sync(input);
}

void textinput_insert_char(TextInput *input, char c) {
    if (!input) return;
    textinput_sync(input);
    if (!input->buffer) return;
    size_t l = strlen(input->buffer);
    if (l >= input->capacity) return;
    int cur = input->cursor;
    if (cur < 0) cur = 0;
    if (cur > (int)l) cur = (int)l;
    for (size_t i = l; i >= (size_t)cur; --i) input->buffer[i + 1] = input->buffer[i];
    input->buffer[cur] = c;
    input->cursor = cur + 1;
    textinput_sync(input);
}

void textinput_backspace(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    if (!input->buffer) return;
    int cur = input->cursor;
    if (cur <= 0) return;
    size_t l = strlen(input->buffer);
    for (int i = cur - 1; i < (int)l; ++i) input->buffer[i] = input->buffer[i + 1];
    input->cursor = cur - 1;
    textinput_sync(input);
}

void textinput_delete(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    if (!input->buffer) return;
    int cur = input->cursor;
    size_t l = strlen(input->buffer);
    if (cur >= (int)l) return;
    for (size_t i = cur; i < l; ++i) input->buffer[i] = input->buffer[i + 1];
    textinput_sync(input);
}

void textinput_move_left(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    if (input->cursor > 0) --input->cursor;
    textinput_sync(input);
}

void textinput_move_right(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    size_t l = input->buffer ? strlen(input->buffer) : 0;
    if (input->cursor < (int)l) ++input->cursor;
    textinput_sync(input);
}

void textinput_move_home(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    input->cursor = 0;
    textinput_sync(input);
}

void textinput_move_end(TextInput *input) {
    if (!input) return;
    textinput_sync(input);
    input->cursor = (int)(input->buffer ? strlen(input->buffer) : 0);
    textinput_sync(input);
}

void textinput_render(TextInput *input, Surface *surface) {
    if (!input || !surface) return;
    textinput_sync(input);
    if (!input->visible) return;
    surface_fill_rect(surface, input->x, input->y, input->w, input->h, input->bg_color);
    surface_draw_rect(surface, input->x, input->y, input->w, input->h, 0xFFFFFFFFu);
    const char *txt = input->buffer ? input->buffer : "";
    surface_draw_text(surface, input->x + 4, input->y + 2, txt, input->color);
    int cx = input->x + 4 + input->cursor * 8;
    int cy = input->y + 2;
    surface_fill_rect(surface, cx, cy, 1, 16, input->color);
}

void textinput_draw(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void textinput_paint(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void render_textinput(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void render_text_input(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void draw_textinput(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void draw_text_input(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void paint_textinput(TextInput *input, Surface *surface) { textinput_render(input, surface); }
void paint_text_input(TextInput *input, Surface *surface) { textinput_render(input, surface); }

/* ---------- Container ---------- */

static void container_sync(Container *container) {
    if (!container) return;
    if (!container->type) container->type = WIDGET_CONTAINER;
    if (container->count < 0) container->count = 0;
    container->child_count = container->count;
    container->num_children = container->count;
    container->n = container->count;
    container->nchildren = container->count;
    container->size = container->count;
    if (container->capacity <= 0) container->capacity = 8;
    if (container->cap <= 0) container->cap = container->capacity;
    if (container->cap != container->capacity) {
        if (container->cap > container->capacity) container->capacity = container->cap;
        else container->cap = container->capacity;
    }
    if (container->w == 0) container->w = 320;
    if (container->h == 0) container->h = 240;
    container->width = container->w;
    container->height = container->h;
    if (!container->visible) container->visible = 1;
    if (!container->bg_color && !container->background) { container->bg_color = 0x101010u; container->background = container->bg_color; }
    if (!container->bg_color) container->bg_color = container->background;
    if (!container->background) container->background = container->bg_color;
    if (!container->background_color) container->background_color = container->bg_color;
}

Container *container_create(void) {
    Container *container = (Container *)malloc(sizeof(Container));
    if (!container) return 0;
    memset(container, 0, sizeof(Container));
    container->type = WIDGET_CONTAINER;
    container->capacity = 8;
    container->cap = 8;
    container->children = (void **)malloc(8 * sizeof(void *));
    if (!container->children) { free(container); return 0; }
    memset(container->children, 0, 8 * sizeof(void *));
    container->bg_color = 0x101010u;
    container->background = container->bg_color;
    container->background_color = container->bg_color;
    container->w = 320;
    container->h = 240;
    container->width = container->w;
    container->height = container->h;
    container->visible = 1;
    return container;
}

Container *container_new(void) { return container_create(); }

void container_add(Container *container, void *child) {
    if (!container || !child) return;
    container_sync(container);
    if (container->count >= container->capacity) {
        int newcap = container->capacity * 2;
        void **newarr = (void **)malloc((size_t)newcap * sizeof(void *));
        if (!newarr) return;
        memset(newarr, 0, (size_t)newcap * sizeof(void *));
        for (int i = 0; i < container->count; ++i) newarr[i] = container->children[i];
        container->children = newarr;
        container->capacity = newcap;
        container->cap = newcap;
    }
    container->children[container->count++] = child;
    container_sync(container);
}

void container_add_child(Container *container, void *child) { container_add(container, child); }
void container_append(Container *container, void *child) { container_add(container, child); }

void container_remove(Container *container, void *child) {
    if (!container || !child) return;
    container_sync(container);
    for (int i = 0; i < container->count; ++i) {
        if (container->children[i] == child) {
            for (int j = i; j < container->count - 1; ++j)
                container->children[j] = container->children[j + 1];
            container->children[--container->count] = 0;
            break;
        }
    }
    container_sync(container);
}

void container_clear(Container *container) {
    if (!container) return;
    container_sync(container);
    container->count = 0;
    container_sync(container);
}

static void widget_dispatch_render(Widget *w, Surface *surface);

void container_render(Container *container, Surface *surface) {
    if (!container || !surface) return;
    container_sync(container);
    if (!container->visible) return;
    surface_fill_rect(surface, container->x, container->y, container->w, container->h, container->bg_color);
    for (int i = 0; i < container->count; ++i)
        widget_dispatch_render((Widget *)container->children[i], surface);
}

void container_draw(Container *container, Surface *surface) { container_render(container, surface); }
void container_paint(Container *container, Surface *surface) { container_render(container, surface); }
void render_container(Container *container, Surface *surface) { container_render(container, surface); }
void draw_container(Container *container, Surface *surface) { container_render(container, surface); }
void paint_container(Container *container, Surface *surface) { container_render(container, surface); }

/* ---------- Generic Widget ---------- */

static void widget_dispatch_render(Widget *w, Surface *surface) {
    if (!w || !surface) return;
    switch (w->type) {
        case WIDGET_LABEL: label_render((Label *)w, surface); break;
        case WIDGET_BUTTON: button_render((Button *)w, surface); break;
        case WIDGET_TEXTINPUT: textinput_render((TextInput *)w, surface); break;
        case WIDGET_CONTAINER: container_render((Container *)w, surface); break;
        default: break;
    }
}

Widget *widget_create(int type, int x, int y, int w, int h) {
    size_t sz = sizeof(Widget);
    if (type == WIDGET_LABEL) sz = sizeof(Label);
    else if (type == WIDGET_BUTTON) sz = sizeof(Button);
    else if (type == WIDGET_TEXTINPUT) sz = sizeof(TextInput);
    else if (type == WIDGET_CONTAINER) sz = sizeof(Container);
    Widget *widget = (Widget *)malloc(sz);
    if (!widget) return 0;
    memset(widget, 0, sz);
    widget->type = type;
    widget->x = x;
    widget->y = y;
    widget->w = w > 0 ? w : 1;
    widget->h = h > 0 ? h : 1;
    widget->width = widget->w;
    widget->height = widget->h;
    widget->visible = 1;
    return widget;
}

Widget *widget_create_label(const char *text, uint32_t color) {
    return (Widget *)label_create(text, color);
}

Widget *widget_create_button(const char *label, void (*on_click)()) {
    return (Widget *)button_create(label, on_click);
}

Widget *widget_create_textinput(size_t capacity) {
    return (Widget *)textinput_create(capacity);
}

Widget *widget_create_container(void) {
    return (Widget *)container_create();
}

void widget_set_pos(Widget *widget, int x, int y) {
    if (!widget) return;
    widget->x = x;
    widget->y = y;
}

void widget_set_size(Widget *widget, int w, int h) {
    if (!widget) return;
    widget->w = w;
    widget->h = h;
    widget->width = w;
    widget->height = h;
}

void widget_render(Widget *widget, Surface *surface) {
    if (!widget || !surface) return;
    widget_dispatch_render(widget, surface);
}

void widget_draw(Widget *widget, Surface *surface) { widget_render(widget, surface); }
void widget_paint(Widget *widget, Surface *surface) { widget_render(widget, surface); }
void render_widget(Widget *widget, Surface *surface) { widget_render(widget, surface); }
void draw_widget(Widget *widget, Surface *surface) { widget_render(widget, surface); }
void paint_widget(Widget *widget, Surface *surface) { widget_render(widget, surface); }
