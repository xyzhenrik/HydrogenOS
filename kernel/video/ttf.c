#include "ttf.h"

#include "framebuffer.h"
#include "kheap.h"
#include "string.h"

#include <stddef.h>
#include <stdint.h>

/* Minimal freestanding math helpers (no libm). */
static double ttf_fabs(double x) { return x < 0 ? -x : x; }

static double ttf_floor(double x) {
    int i = (int)x;
    if ((double)i > x) {
        --i;
    }
    return (double)i;
}

static double ttf_ceil(double x) {
    int i = (int)x;
    if ((double)i < x) {
        ++i;
    }
    return (double)i;
}

static double ttf_sqrt(double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    double r = x;
    for (int i = 0; i < 64; ++i) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

static double ttf_fmod(double x, double y) {
    if (y == 0.0) {
        return 0.0;
    }
    double q = ttf_floor(x / y);
    return x - q * y;
}

static double ttf_atan(double t) {
    const double PI = 3.14159265358979323846;
    if (t > 1.0) {
        return PI * 0.5 - ttf_atan(1.0 / t);
    }
    if (t < -1.0) {
        return -PI * 0.5 - ttf_atan(1.0 / t);
    }
    double sum = t;
    double term = t;
    double x2 = t * t;
    for (int i = 1; i < 64; ++i) {
        term *= -x2;
        sum += term / (double)(2 * i + 1);
    }
    return sum;
}

static double ttf_atan2(double y, double x) {
    const double PI = 3.14159265358979323846;
    if (x > 0.0) {
        return ttf_atan(y / x);
    }
    if (x < 0.0) {
        if (y >= 0.0) {
            return ttf_atan(y / x) + PI;
        }
        return ttf_atan(y / x) - PI;
    }
    if (y > 0.0) {
        return PI * 0.5;
    }
    if (y < 0.0) {
        return -PI * 0.5;
    }
    return 0.0;
}

static double ttf_cos_series(double a) {
    double sum = 1.0;
    double term = 1.0;
    double x2 = a * a;
    for (int i = 1; i < 32; ++i) {
        term *= -x2 / (double)((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

static double ttf_cos(double x) {
    const double PI = 3.14159265358979323846;
    x = ttf_fmod(x, 2.0 * PI);
    if (x < 0.0) {
        x += 2.0 * PI;
    }
    double a;
    int sign = 1;
    if (x <= PI * 0.5) {
        a = x;
    } else if (x <= PI) {
        a = PI - x;
        sign = -1;
    } else if (x <= 1.5 * PI) {
        a = x - PI;
        sign = -1;
    } else {
        a = 2.0 * PI - x;
        sign = 1;
    }
    double c = ttf_cos_series(a);
    return sign > 0 ? c : -c;
}

static double ttf_acos(double x) {
    const double PI = 3.14159265358979323846;
    if (x >= 1.0) {
        return 0.0;
    }
    if (x <= -1.0) {
        return PI;
    }
    double s = ttf_sqrt(1.0 - x * x);
    return ttf_atan2(s, x);
}

static double ttf_log(double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    int n = 0;
    while (x > 2.0) {
        x *= 0.5;
        ++n;
    }
    while (x < 1.0) {
        x *= 2.0;
        --n;
    }
    double t = (x - 1.0) / (x + 1.0);
    double t2 = t * t;
    double term = t;
    double sum = t;
    for (int i = 1; i < 64; ++i) {
        term *= t2;
        sum += term / (double)(2 * i + 1);
    }
    const double LN2 = 0.6931471805599453;
    return 2.0 * sum + (double)n * LN2;
}

static double ttf_exp(double x) {
    if (x > 709.0) {
        x = 709.0;
    }
    if (x < -709.0) {
        return 0.0;
    }
    int n = (int)(x / 0.6931471805599453);
    x -= (double)n * 0.6931471805599453;
    double sum = 1.0;
    double term = 1.0;
    for (int i = 1; i < 32; ++i) {
        term *= x / (double)i;
        sum += term;
    }
    double p = 1.0;
    if (n >= 0) {
        for (int i = 0; i < n; ++i) {
            p *= 2.0;
        }
    } else {
        for (int i = 0; i < -n; ++i) {
            p *= 0.5;
        }
    }
    return sum * p;
}

static double ttf_pow(double x, double y) {
    if (y == 0.0) {
        return 1.0;
    }
    if (x == 0.0) {
        return 0.0;
    }
    /* Common case used by stb_truetype: cube root. */
    if (ttf_fabs(y - (1.0 / 3.0)) < 1e-6) {
        if (x >= 0.0) {
            double r = x > 0.0 ? x : 0.0;
            if (r == 0.0) {
                return 0.0;
            }
            for (int i = 0; i < 64; ++i) {
                r = (2.0 * r + x / (r * r)) / 3.0;
            }
            return r;
        }
        double r = -x;
        for (int i = 0; i < 64; ++i) {
            r = (2.0 * r + (-x) / (r * r)) / 3.0;
        }
        return -r;
    }
    if (x < 0.0) {
        /* Fallback for negative bases: only sensible for integer exponents. */
        double ip = ttf_floor(y);
        if (ttf_fabs(y - ip) > 1e-9) {
            x = ttf_fabs(x);
        }
    }
    double mag = ttf_exp(y * ttf_log(ttf_fabs(x)));
    if (x < 0.0 && ttf_fmod(y, 2.0) != 0.0) {
        mag = -mag;
    }
    return mag;
}

static size_t ttf_strlen(const char *s) {
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        ++n;
    }
    return n;
}

/* Freestanding-friendly stb_truetype configuration. */
#define STBTT_malloc(x, u) kalloc(x)
#define STBTT_free(x, u)   kfree(x)
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset
#define STBTT_strlen(x)    ttf_strlen(x)
#define STBTT_ifloor(x)    ((int)ttf_floor(x))
#define STBTT_iceil(x)     ((int)ttf_ceil(x))
#define STBTT_sqrt(x)      ttf_sqrt(x)
#define STBTT_pow(x, y)    ttf_pow(x, y)
#define STBTT_fmod(x, y)   ttf_fmod(x, y)
#define STBTT_cos(x)       ttf_cos(x)
#define STBTT_acos(x)      ttf_acos(x)
#define STBTT_fabs(x)      ttf_fabs(x)
#define STBTT_assert(x)    ((void)0)

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static stbtt_fontinfo ttf_font;
static int ttf_loaded = 0;
static const unsigned char *ttf_data = NULL;

int ttf_init(const void *data, size_t size) {
    ttf_loaded = 0;
    ttf_data = NULL;
    if (!data || size == 0) {
        return -1;
    }
    if (!stbtt_InitFont(&ttf_font, (const unsigned char *)data, 0)) {
        return -1;
    }
    (void)size;
    ttf_data = (const unsigned char *)data;
    ttf_loaded = 1;
    return 0;
}

static void resolve_fb(void *fb, uint8_t **out_addr, uint32_t *out_w,
                       uint32_t *out_h, uint32_t *out_pitch) {
    if (fb) {
        fb_t *f = (fb_t *)fb;
        if (f->addr && f->width > 0 && f->height > 0 &&
            f->pitch >= f->width * 4u) {
            *out_addr = f->addr;
            *out_w = f->width;
            *out_h = f->height;
            *out_pitch = f->pitch;
            return;
        }
        *out_addr = (uint8_t *)fb;
        if (framebuffer.width > 0 && framebuffer.height > 0 &&
            framebuffer.pitch >= framebuffer.width * 4u) {
            *out_w = framebuffer.width;
            *out_h = framebuffer.height;
            *out_pitch = framebuffer.pitch;
        } else {
            *out_w = 800;
            *out_h = 600;
            *out_pitch = 800 * 4u;
        }
        return;
    }

    *out_addr = framebuffer.addr;
    *out_w = framebuffer.width;
    *out_h = framebuffer.height;
    *out_pitch = framebuffer.pitch;
    if (*out_pitch == 0 && *out_w > 0) {
        *out_pitch = *out_w * 4u;
    }
}

void ttf_draw_text(int x, int y, const char *text, float size, uint32_t color, void *fb) {
    if (!ttf_loaded || !text) {
        return;
    }

    uint8_t *addr;
    uint32_t fw, fh, pitch;
    resolve_fb(fb, &addr, &fw, &fh, &pitch);
    if (!addr || fw == 0 || fh == 0 || pitch == 0) {
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&ttf_font, size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&ttf_font, &ascent, &descent, &line_gap);
    float baseline = (float)y + (float)ascent * scale;

    int prev_cp = 0;
    float pen_x = (float)x;
    for (const char *p = text; *p; ++p) {
        int cp = (unsigned char)*p;
        if (cp == '\n') {
            pen_x = (float)x;
            baseline += ((float)(ascent - descent + line_gap)) * scale;
            prev_cp = 0;
            continue;
        }
        if (cp == '\r') {
            pen_x = (float)x;
            prev_cp = 0;
            continue;
        }

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&ttf_font, cp, &advance, &lsb);
        float kern = (float)stbtt_GetCodepointKernAdvance(&ttf_font, prev_cp, cp) * scale;
        pen_x += kern;

        int w, h, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&ttf_font, scale, scale, cp, &w, &h, &xoff, &yoff);
        if (bitmap) {
            int bx = (int)(pen_x + (float)xoff);
            int by = (int)(baseline + (float)yoff);
            uint32_t sr = (color >> 16) & 0xFFu;
            uint32_t sg = (color >> 8) & 0xFFu;
            uint32_t sb = color & 0xFFu;

            for (int row = 0; row < h; ++row) {
                int py = by + row;
                if (py < 0 || (uint32_t)py >= fh) {
                    continue;
                }
                for (int col = 0; col < w; ++col) {
                    int px = bx + col;
                    if (px < 0 || (uint32_t)px >= fw) {
                        continue;
                    }
                    unsigned char cov = bitmap[row * w + col];
                    if (cov == 0) {
                        continue;
                    }
                    uint32_t *pixel = (uint32_t *)(addr + (uint32_t)py * pitch + (uint32_t)px * 4u);
                    if (cov == 255) {
                        *pixel = (color & 0x00FFFFFFu);
                    } else {
                        uint32_t dst = *pixel;
                        uint32_t dr = (dst >> 16) & 0xFFu;
                        uint32_t dg = (dst >> 8) & 0xFFu;
                        uint32_t db = dst & 0xFFu;
                        uint32_t a = cov;
                        uint32_t inv = 255u - a;
                        uint32_t nr = (sr * a + dr * inv) / 255u;
                        uint32_t ng = (sg * a + dg * inv) / 255u;
                        uint32_t nb = (sb * a + db * inv) / 255u;
                        *pixel = (nr << 16) | (ng << 8) | nb;
                    }
                }
            }
            stbtt_FreeBitmap(bitmap, NULL);
        }

        pen_x += (float)advance * scale;
        prev_cp = cp;
    }
}