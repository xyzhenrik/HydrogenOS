#pragma once

#include <stdint.h>
#include <stddef.h>

/* Initialize TrueType font from in-memory data. Returns 0 on success. */
int ttf_init(const void *data, size_t size);

/* Render NUL-terminated text. `fb` may be a fb_t* or raw pixel buffer (see psf2). */
void ttf_draw_text(int x, int y, const char *text, float size, uint32_t color, void *fb);