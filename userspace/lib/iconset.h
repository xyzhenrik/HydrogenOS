#pragma once

/* The desktop's icons. They are vector drawings (tools/mkicons.py) rendered
   ahead of time at exactly the pixel sizes the UI uses, so drawing one is a
   plain blend, never a scaling step. Use the ICON_SIZE_* macros for sizes. */

#include "gfx_effects.h"
#include "iconset_data.h"

/* Blends a colour icon with its top-left corner at (x, y). light selects the
   artwork for the light theme. opacity is 0..255. Draws nothing for a size the
   icon does not exist at, or for a glyph (see icon_draw_glyph). */
void icon_draw(const GfxCanvas *canvas, IconId id, int size, int x, int y,
               int light, unsigned int opacity);

/* Blends a glyph in the colour rgb (0x00RRGGBB). Draws nothing for a size the
   glyph does not exist at, or for a colour icon. */
void icon_draw_glyph(const GfxCanvas *canvas, IconId id, int size, int x, int y,
                     uint32_t rgb, unsigned int opacity);

/* Nonzero when the icon exists at the size, as the kind icon_draw() or
   icon_draw_glyph() handles: glyph is nonzero for the latter. */
int icon_available(IconId id, int size, int glyph);
