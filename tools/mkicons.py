#!/usr/bin/env python3
"""Generates the HydrogenOS icon set.

The icons are vector drawings, authored on a 64 x 64 design grid in this
file, and rasterised by tools/vecraster.py at every pixel size the desktop
draws them at, so no icon is ever scaled at run time. The output is
premultiplied 0xAARRGGBB data in C:

    mkicons.py OUT_DIR                 writes OUT_DIR/iconset_data.{h,c}
    mkicons.py --preview FILE [ZOOM]   writes a PNG contact sheet of both themes

Two kinds of icons exist. Colour icons (application, file type, place and
settings tiles) are rendered once per theme: what differs is only how the
shape separates from the surface behind it. Glyphs are coverage masks that the
caller tints, so toolbar and button symbols follow any ink colour.
"""

import math
import os
import struct
import sys
import zlib

from vecraster import (Canvas, Linear, Path, Radial, Solid, circle, colour, ellipse, line,
                       polygon, polyline, rect, rotate, round_polygon, rrect, squircle, vertical)

# ---------------------------------------------------------------- sizes

# Pixel sizes the UI draws the icons at. They are emitted as ICON_SIZE_*
# macros, so layout code and icon data cannot disagree.
SIZES = {
    'APP': 32,          # taskbar and launcher rows
    'FILE': 48,         # file manager, icon view
    'FILE_SMALL': 24,   # file manager, list view
    'TILE': 24,         # sidebar tiles of the file manager and settings
    'TOOLBAR': 20,      # toolbar glyphs
    'GLYPH': 16,        # glyphs inline with text
    'GLYPH_LARGE': 32,  # glyphs in the confirmation badge
}

# Margin and upward shift of the artwork in pixels, leaving room for its shadow.
FRAME = {16: (0, 0), 20: (1, 0), 24: (1, 0), 32: (2, -1), 48: (3, -1)}
GLYPH_FRAME = {16: (0, 0), 20: (1, 0), 32: (0, 0)}

WHITE = colour('#FFFFFF')
BLACK = colour('#000000')


def solid(text, alpha=1.0):
    return Solid(colour(text, alpha))


def fade(text, top, bottom):
    """Vertical gradient of one colour between two opacities."""
    return vertical(colour(text, top), colour(text, bottom))


def gradient(top, bottom, y0=0.0, y1=64.0):
    return vertical(colour(top), colour(bottom), y0, y1)


# ---------------------------------------------------------------- shared parts

def plate(c, theme, top, bottom):
    """The app tile: a squircle with a vertical gradient, a soft sheen at the top,
    a one pixel edge that separates it from the surface, and a drop shadow."""
    shape = squircle(0, 0, 64, 64)
    if c.size >= 32:
        c.shadow(shape, 1.6, 1.8, opacity=0.40 if theme == 'dark' else 0.30)
    c.fill(shape, gradient(top, bottom))
    c.fill(shape, Linear((0, 0), (0, 40), [(0.0, colour('#FFFFFF', 0.20)), (1.0, colour('#FFFFFF', 0.0))]))
    edge = 1.0 / c.scale
    ring = Path().extend(shape).extend(squircle(edge, edge, 64 - 2 * edge, 64 - 2 * edge))
    if theme == 'dark':
        c.fill(ring, fade('#FFFFFF', 0.32, 0.04), even_odd=True)
    else:
        c.fill(ring, fade('#000000', 0.16, 0.26), even_odd=True)


def lift(c, path, strength=0.30):
    """A small shadow under a glyph or sheet drawn on a tile."""
    if c.size >= 32:
        c.shadow(path, 1.0, 1.2, opacity=strength)


def folder_shape(x, y, w, h, tab_width, tab_height, radius):
    return round_polygon([(x, y), (x + tab_width, y), (x + tab_width + 5, y + tab_height),
                          (x + w, y + tab_height), (x + w, y + h), (x, y + h)],
                         [radius, 2.0, 2.0, radius, radius, radius])


def sheet_shape(x, y, w, h, fold):
    return round_polygon([(x, y), (x + w - fold, y), (x + w, y + fold), (x + w, y + h), (x, y + h)],
                         [3, 1.5, 2, 3, 3])


def fold_flap(x, y, w, fold):
    return round_polygon([(x + w - fold, y), (x + w, y + fold), (x + w - fold, y + fold)], 1.5)


def local_to_design(points, angle, origin):
    """Points of a drawing made along +x, rotated by angle degrees and moved to origin."""
    return [(origin[0] + x, origin[1] + y) for x, y in rotate(points, angle)]


def pencil(c, tip, angle, length, width, body_top, body_bottom):
    """A pencil whose tip is at `tip`, pointing back along `angle` (degrees)."""
    half = width / 2.0
    body_end = length - 0.22 * length
    ferrule_end = body_end + 0.07 * length

    def part(points):
        return polygon(local_to_design(points, angle, tip))

    def across(a, b):
        return Linear(local_to_design([(0, -half)], angle, tip)[0],
                      local_to_design([(0, half)], angle, tip)[0], [(0.0, a), (1.0, b)])

    cone = part([(0, 0), (0.26 * length, -half), (0.26 * length, half)])
    c.shadow(polygon(local_to_design([(0, 0), (0.26 * length, -half), (length, -half),
                                      (length, half), (0.26 * length, half)], angle, tip)),
             1.2, 1.4, opacity=0.32)
    c.fill(cone, solid('#F6DDB2'))
    c.fill(part([(0, 0), (0.11 * length, -0.4 * half), (0.11 * length, 0.4 * half)]), solid('#3A3A3E'))
    c.fill(part([(0.26 * length, -half), (body_end, -half), (body_end, half), (0.26 * length, half)]),
           across(colour(body_top), colour(body_bottom)))
    c.fill(part([(body_end, -half), (ferrule_end, -half), (ferrule_end, half), (body_end, half)]),
           across(colour('#E5E8EE'), colour('#AEB4C0')))
    c.fill(polygon(local_to_design([(ferrule_end, -half), (length - half * 0.6, -half),
                                    (length, -half * 0.5), (length, half * 0.5),
                                    (length - half * 0.6, half), (ferrule_end, half)], angle, tip)),
           across(colour('#FF8FA0'), colour('#E9566E')))


# ---------------------------------------------------------------- applications

def terminal(c, theme):
    plate(c, theme, '#42454E', '#16171B')
    prompt = polyline([(15, 22), (29, 32), (15, 42)])
    c.stroke(prompt, Linear((0, 20), (0, 44), [(0.0, colour('#7BF29A')), (1.0, colour('#2FCF5C'))]), 5.6)
    c.stroke(line(34, 45, 50, 45), solid('#FFFFFF', 0.92), 5.6)


def editor(c, theme):
    plate(c, theme, '#FFD94E', '#FF9A0A')
    sheet = sheet_shape(14, 8, 36, 48, 12)
    lift(c, sheet)
    c.fill(sheet, gradient('#FFFFFF', '#E9ECF2'))
    c.fill(fold_flap(14, 8, 36, 12), gradient('#BFC6D2', '#E2E6ED', 8, 20))
    ink = solid('#97A1B2')
    for y, end in ((29, 44), (36, 44), (43, 33)):
        c.stroke(line(21, y, end, y), ink, 3.4)
    pencil(c, (26, 55), -45, 38, 10, '#58AEFF', '#0A68DE')


def files(c, theme):
    plate(c, theme, '#5FC3FF', '#0A6FEA')
    back = folder_shape(10, 14, 44, 38, 17, 6, 4)
    front = rrect(10, 25, 44, 27, 4)
    lift(c, back)
    c.fill(back, solid('#FFFFFF', 0.72))
    lift(c, front, 0.22)
    c.fill(front, gradient('#FFFFFF', '#D3E8FF', 25, 52))


def settings(c, theme):
    plate(c, theme, '#A6A6B0', '#5B5B65')
    teeth = 8
    points = []
    for k in range(teeth):
        centre = 360.0 * k / teeth
        for angle, radius in ((-12.5, 17.5), (-8.0, 23.0), (8.0, 23.0), (12.5, 17.5)):
            a = math.radians(centre + angle)
            points.append((32 + radius * math.cos(a), 32 + radius * math.sin(a)))
    cog = round_polygon(points, [1.0, 1.6, 1.6, 1.0] * teeth)
    cog.extend(circle(32, 32, 7.5))
    c.shadow(cog, 1.0, 1.4, opacity=0.34, even_odd=True)
    c.fill(cog, gradient('#FFFFFF', '#CBCBD3', 9, 55), even_odd=True)


# ---------------------------------------------------------------- file types

def folder(c, theme):
    back = folder_shape(4, 9, 56, 46, 22, 7, 5)
    front = rrect(4, 22, 56, 33, 5)
    c.shadow(back, 1.6, 1.6, opacity=0.30)
    c.fill(back, gradient('#3F94F0', '#2A77D8', 9, 55))
    c.shadow(front, -0.6, 1.0, opacity=0.22)
    c.fill(front, gradient('#86CCFF', '#4BA4F4', 22, 55))
    c.stroke(line(8, 22.6, 56, 22.6), solid('#FFFFFF', 0.45), 1.0)


def _paper(c, theme, with_text):
    sheet = sheet_shape(12, 4, 40, 56, 14)
    c.shadow(sheet, 1.4, 1.5, opacity=0.34 if theme == 'dark' else 0.22)
    c.fill(sheet, gradient('#FFFFFF', '#E6EAF1', 4, 60))
    c.fill(fold_flap(12, 4, 40, 14), gradient('#B9C1CE', '#E0E4EB', 4, 18))
    edge = solid('#8D96A6', 0.55 if theme == 'dark' else 0.75)
    c.stroke(sheet, edge, 1.0 / c.scale)
    if with_text:
        ink = solid('#8F99AB')
        for y, end in ((29, 44), (36, 44), (43, 44), (50, 34)):
            c.stroke(line(20, y, end, y), ink, 3.4)


def text(c, theme):
    _paper(c, theme, True)


def page(c, theme):
    _paper(c, theme, False)


def program(c, theme):
    plate(c, theme, '#8C89FF', '#4B49D6')
    window = rrect(10, 14, 44, 36, 5)
    lift(c, window)
    c.fill(window, gradient('#FFFFFF', '#E7E7FF', 14, 50))
    c.fill(rect(10, 14, 44, 9), solid('#CFCEFF'), clip=c.mask(window))
    for x in (16, 21.5, 27):
        c.fill(circle(x, 18.5, 1.7), solid('#7F7CEE'))
    c.stroke(polyline([(17, 30), (24, 35.5), (17, 41)]), solid('#5A58E0'), 3.6)
    c.stroke(line(28, 41.5, 38, 41.5), solid('#5A58E0'), 3.6)


def archive(c, theme):
    body = rrect(8, 26, 48, 30, 4)
    lid = rrect(4, 14, 56, 15, 3.5)
    c.shadow(body, 1.6, 1.6, opacity=0.30)
    c.fill(body, gradient('#E9C28A', '#C79760', 26, 56))
    c.fill(rect(26, 29, 12, 27), gradient('#F6E8C6', '#E7D2A4', 29, 56))
    c.shadow(lid, 1.0, 1.0, opacity=0.25)
    c.fill(lid, gradient('#DDAE70', '#BC8852', 14, 29))
    c.fill(rect(26, 14, 12, 15), gradient('#F8EBCB', '#E9D6AA', 14, 29))


def image(c, theme):
    frame = rrect(4, 10, 56, 44, 5)
    sky_shape = rrect(7.5, 13.5, 49, 37, 3.5)
    c.shadow(frame, 1.4, 1.5, opacity=0.34 if theme == 'dark' else 0.24)
    c.fill(frame, solid('#FFFFFF'))
    c.fill(sky_shape, gradient('#A4DEFF', '#52AEF4', 13.5, 50.5))
    clip = c.mask(sky_shape)
    c.fill(circle(43, 25, 5.8), Radial((42, 23.5), 7, [(0.0, colour('#FFF08A')), (1.0, colour('#FFC21A'))]))
    far = (Path().move_to(6, 52).line_to(6, 43).cubic_to(14, 32, 25, 29, 34, 40)
           .cubic_to(38, 45, 42, 49, 46, 52).close())
    near = (Path().move_to(22, 52).cubic_to(30, 38, 45, 34, 58, 44).line_to(58, 52).close())
    c.fill(far, gradient('#3DB862', '#259A4C', 32, 52), clip=clip)
    c.fill(near, gradient('#78E394', '#34B85E', 36, 52), clip=clip)


# ---------------------------------------------------------------- places and settings tiles

def drive(c, theme):
    plate(c, theme, '#A8A8B3', '#686872')
    body = rrect(9, 19, 46, 27, 6)
    lift(c, body)
    c.fill(body, gradient('#FFFFFF', '#E1E1E8', 19, 46))
    c.stroke(line(16, 39, 31, 39), solid('#7C7C88'), 3.4)
    c.fill(circle(47, 39, 2.4), solid('#34C759'))


def home(c, theme):
    plate(c, theme, '#5AB9FF', '#0A72EE')
    house = round_polygon([(32, 10), (55, 31), (48, 31), (48, 53), (16, 53), (16, 31), (9, 31)],
                          [2.5, 3, 0.8, 2.5, 2.5, 0.8, 3])
    lift(c, house)
    c.fill(house, gradient('#FFFFFF', '#DCEBFF', 10, 53))
    c.fill(rrect(27, 37, 10, 16, 2), solid('#2E86F0'))


def clock(c, theme):
    plate(c, theme, '#FFB547', '#FF8A00')
    face = circle(32, 32, 22)
    lift(c, face)
    c.fill(face, gradient('#FFFFFF', '#FFE8C7', 10, 54))
    hands = polyline([(32, 17), (32, 32), (42, 38)])
    c.stroke(hands, solid('#E67A00'), 4.2)


def programs(c, theme):
    plate(c, theme, '#8C89FF', '#4B49D6')
    for x in (13, 34):
        for y in (13, 34):
            cell = rrect(x, y, 17, 17, 4.5)
            lift(c, cell, 0.2)
            c.fill(cell, gradient('#FFFFFF', '#E0E0FF', y, y + 17))


def appearance(c, theme):
    plate(c, theme, '#8583FF', '#5654D6')
    disc = circle(32, 32, 19)
    lift(c, disc)
    c.stroke(circle(32, 32, 19), solid('#FFFFFF'), 4.4)
    half = Path().arc(32, 32, 14.5, 14.5, 90, 270).close()
    c.fill(half, solid('#FFFFFF'))


def keyboard(c, theme):
    plate(c, theme, '#A9A9B3', '#6A6A74')
    body = rrect(8, 18, 48, 28, 5)
    lift(c, body)
    c.fill(body, gradient('#FFFFFF', '#E3E3EA', 18, 46))
    key = solid('#72727E')
    for row_y, x0, count in ((24, 14, 5), (31, 14, 5)):
        for k in range(count):
            c.fill(rrect(x0 + k * 7.2, row_y, 4.8, 4.2, 1.2), key)
    c.fill(rrect(20, 38, 24, 4.2, 1.2), key)


def display(c, theme):
    plate(c, theme, '#59BAFF', '#0A74EE')
    screen = rrect(8, 12, 48, 32, 5)
    stand = Path().extend(rect(27, 44, 10, 5)).extend(rrect(21, 49, 22, 4, 2))
    lift(c, screen)
    c.fill(stand, gradient('#F3F8FF', '#C5DDFB', 44, 53))
    c.fill(screen, gradient('#FFFFFF', '#D6E8FF', 12, 44))
    c.fill(rrect(12.5, 16.5, 39, 23, 2.5), gradient('#4FB0FF', '#1F7BF0', 16, 40))


def network(c, theme):
    plate(c, theme, '#4FE07A', '#1DAE49')
    ink = solid('#FFFFFF')
    c.stroke(circle(32, 32, 20.5), ink, 4.0)
    c.stroke(ellipse(32, 32, 9, 20.5), ink, 3.4)
    c.stroke(line(11.5, 32, 52.5, 32), ink, 3.4)
    c.stroke(Path().arc(32, 7, 26, 26, 60, 120), ink, 3.2)
    c.stroke(Path().arc(32, 57, 26, 26, -120, -60), ink, 3.2)


def about(c, theme):
    plate(c, theme, '#9494A0', '#5E5E69')
    disc = circle(32, 32, 21)
    lift(c, disc)
    c.fill(disc, gradient('#FFFFFF', '#E2E2E9', 11, 53))
    c.fill(circle(32, 22, 3.3), solid('#6A6A76'))
    c.fill(rrect(29, 29, 6, 16, 2.6), solid('#6A6A76'))


# ---------------------------------------------------------------- glyphs

def mark(c, path, width):
    c.stroke(path, Solid(WHITE), width)


def glyph_search(c, theme):
    mark(c, circle(26, 26, 16.5), 7.0)
    mark(c, line(39, 39, 54, 54), 8.0)


def glyph_power(c, theme):
    mark(c, Path().arc(32, 34, 23, 23, -52, 232), 7.0)
    mark(c, line(32, 6, 32, 32), 7.0)


def glyph_restart(c, theme):
    centre, radius, end = (32, 32), 23, 70
    mark(c, Path().arc(centre[0], centre[1], radius, radius, 150, 360 + end - 12), 7.0)
    a = math.radians(end)
    tangent = (-math.sin(a), math.cos(a))
    normal = (math.cos(a), math.sin(a))
    base = (centre[0] + radius * normal[0], centre[1] + radius * normal[1])
    tip = (base[0] + tangent[0] * 11, base[1] + tangent[1] * 11)
    c.fill(polygon([(base[0] - normal[0] * 11, base[1] - normal[1] * 11),
                    (base[0] + normal[0] * 11, base[1] + normal[1] * 11), tip]), Solid(WHITE))


def glyph_back(c, theme):
    mark(c, polyline([(40, 12), (20, 32), (40, 52)]), 7.0)


def glyph_forward(c, theme):
    mark(c, polyline([(24, 12), (44, 32), (24, 52)]), 7.0)


def glyph_up(c, theme):
    mark(c, line(32, 54, 32, 14), 7.0)
    mark(c, polyline([(14, 32), (32, 14), (50, 32)]), 7.0)


def glyph_chevron(c, theme):
    mark(c, polyline([(24, 12), (44, 32), (24, 52)]), 5.5)


def glyph_view_grid(c, theme):
    for x in (9, 35):
        for y in (9, 35):
            c.fill(rrect(x, y, 20, 20, 5), Solid(WHITE))


def glyph_view_list(c, theme):
    for y in (14, 32, 50):
        c.fill(circle(12, y, 3.6), Solid(WHITE))
        c.fill(rrect(22, y - 3.2, 32, 6.4, 3.2), Solid(WHITE))


def glyph_new_folder(c, theme):
    outline = round_polygon([(6, 14), (23, 14), (29, 21), (58, 21), (58, 53), (6, 53)], 4)
    mark(c, outline, 5.0)
    mark(c, line(32, 31, 32, 45), 5.0)
    mark(c, line(25, 38, 39, 38), 5.0)


def glyph_rename(c, theme):
    body = polygon(local_to_design([(0, 0), (11, -6.5), (44, -6.5), (44, 6.5), (11, 6.5)], -45, (10, 54)))
    c.fill(body, Solid(WHITE))
    mark(c, line(34, 57, 56, 57), 5.0)


def glyph_delete(c, theme):
    mark(c, line(10, 17, 54, 17), 6.0)
    mark(c, polyline([(24, 17), (24, 9), (40, 9), (40, 17)]), 5.0)
    mark(c, round_polygon([(15, 25), (49, 25), (45, 56), (19, 56)], 3), 5.0)
    mark(c, line(26, 34, 26.8, 47), 4.4)
    mark(c, line(38, 34, 37.2, 47), 4.4)


# ---------------------------------------------------------------- registry

class Icon:
    def __init__(self, name, draw, sizes, mask=False):
        self.name, self.draw, self.sizes, self.mask = name, draw, sizes, mask


def colour_icons(*entries):
    return [Icon(name, draw, [SIZES[s] for s in sizes]) for name, draw, sizes in entries]


ICONS = colour_icons(
    ('TERMINAL', terminal, ['APP']),
    ('EDITOR', editor, ['APP']),
    ('FILES', files, ['APP']),
    ('SETTINGS', settings, ['APP']),
    ('FOLDER', folder, ['FILE', 'FILE_SMALL']),
    ('TEXT', text, ['FILE', 'FILE_SMALL']),
    ('PAGE', page, ['FILE', 'FILE_SMALL']),
    ('PROGRAM', program, ['FILE', 'FILE_SMALL']),
    ('ARCHIVE', archive, ['FILE', 'FILE_SMALL']),
    ('IMAGE', image, ['FILE', 'FILE_SMALL']),
    ('PLACE_ROOT', drive, ['TILE']),
    ('PLACE_HOME', home, ['TILE']),
    ('PLACE_TEMP', clock, ['TILE']),
    ('PLACE_PROGRAMS', programs, ['TILE']),
    ('SECTION_APPEARANCE', appearance, ['TILE']),
    ('SECTION_KEYBOARD', keyboard, ['TILE']),
    ('SECTION_DISPLAY', display, ['TILE']),
    ('SECTION_NETWORK', network, ['TILE']),
    ('SECTION_ABOUT', about, ['TILE']),
) + [Icon('GLYPH_' + name, draw, [SIZES[s] for s in sizes], mask=True) for name, draw, sizes in (
    ('SEARCH', glyph_search, ['GLYPH']),
    ('POWER', glyph_power, ['GLYPH', 'GLYPH_LARGE']),
    ('RESTART', glyph_restart, ['GLYPH', 'GLYPH_LARGE']),
    ('CHEVRON', glyph_chevron, ['GLYPH']),
    ('BACK', glyph_back, ['TOOLBAR']),
    ('FORWARD', glyph_forward, ['TOOLBAR']),
    ('UP', glyph_up, ['TOOLBAR']),
    ('VIEW_GRID', glyph_view_grid, ['TOOLBAR']),
    ('VIEW_LIST', glyph_view_list, ['TOOLBAR']),
    ('NEW_FOLDER', glyph_new_folder, ['TOOLBAR']),
    ('RENAME', glyph_rename, ['TOOLBAR']),
    ('DELETE', glyph_delete, ['TOOLBAR']),
)]

THEMES = ('dark', 'light')


def render(icon, size, theme):
    frame = (GLYPH_FRAME if icon.mask else FRAME)[size]
    canvas = Canvas(size, frame[0], frame[1])
    icon.draw(canvas, theme)
    return canvas.argb_template() if icon.mask else canvas.argb()


def render_all():
    """{(icon name, size): (dark words, light words)}."""
    rendered = {}
    for icon in ICONS:
        for size in icon.sizes:
            dark = render(icon, size, 'dark')
            light = dark if icon.mask else render(icon, size, 'light')
            rendered[(icon.name, size)] = (dark, light)
    return rendered


# ---------------------------------------------------------------- C output

HEADER_HEAD = """\
/* Generated by tools/mkicons.py; do not edit. */
#pragma once

#include <stdint.h>

"""

HEADER_TAIL = """\

/* One icon at one pixel size. Pixels are premultiplied 0xAARRGGBB, row by row.
   A mask holds coverage only (as premultiplied white) and is tinted when drawn. */
typedef struct IconBitmap {
    uint16_t id;
    uint16_t size;
    uint8_t mask;
    const uint32_t *dark;   /* for the dark theme */
    const uint32_t *light;  /* for the light theme; the same array if the themes agree */
} IconBitmap;

extern const IconBitmap icon_bitmaps[];
extern const int icon_bitmap_count;
"""


def write_header(path):
    lines = [HEADER_HEAD]
    for name, size in SIZES.items():
        lines.append('#define ICON_SIZE_%s %d\n' % (name, size))
    lines.append('\ntypedef enum IconId {\n')
    for icon in ICONS:
        lines.append('    ICON_%s,\n' % icon.name)
    lines.append('    ICON_COUNT\n} IconId;\n')
    lines.append(HEADER_TAIL)
    with open(path, 'w') as out:
        out.write(''.join(lines))


def write_source(path, rendered):
    arrays = {}
    body = []
    for icon in ICONS:
        for size in icon.sizes:
            for theme, words in zip(THEMES, rendered[(icon.name, size)]):
                key = tuple(words)
                if key in arrays:
                    continue
                label = 'icon_%s_%d_%s' % (icon.name.lower(), size, theme)
                arrays[key] = label
                body.append('static const uint32_t %s[%d] = {\n' % (label, len(words)))
                for i in range(0, len(words), 8):
                    body.append('    ' + ','.join('0x%08Xu' % w for w in words[i:i + 8]) + ',\n')
                body.append('};\n\n')
    table = ['const IconBitmap icon_bitmaps[] = {\n']
    count = 0
    for icon in ICONS:
        for size in icon.sizes:
            dark, light = rendered[(icon.name, size)]
            table.append('    { ICON_%s, %d, %d, %s, %s },\n' % (
                icon.name, size, 1 if icon.mask else 0, arrays[tuple(dark)], arrays[tuple(light)]))
            count += 1
    table.append('};\n\nconst int icon_bitmap_count = %d;\n' % count)
    with open(path, 'w') as out:
        out.write('/* Generated by tools/mkicons.py; do not edit. */\n#include "iconset_data.h"\n\n')
        out.write(''.join(body) + ''.join(table))


# ---------------------------------------------------------------- preview

SURFACES = {'dark': (0x1C, 0x1C, 0x1E), 'light': (0xF2, 0xF2, 0xF7)}
GLYPH_INK = {'dark': (0xF2, 0xF2, 0xF7), 'light': (0x1C, 0x1C, 0x1E)}


def write_png(path, width, height, rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))

    with open(path, 'wb') as out:
        out.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                  chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def write_preview(path, rendered, zoom):
    """Every icon over the surface of each theme: 1:1 and enlarged, side by side."""
    cell_pad = 8
    columns = 6
    panel_w = 0
    items = [(icon, size) for icon in ICONS for size in icon.sizes]
    cell_w = max(size for _, size in items) * (1 + zoom) + 3 * cell_pad
    cell_h = max(size for _, size in items) * zoom + 2 * cell_pad
    rows_count = (len(items) + columns - 1) // columns
    panel_w = columns * cell_w
    width = panel_w * 2
    height = rows_count * cell_h
    image = [[0] * (width * 3) for _ in range(height)]
    for t, theme in enumerate(THEMES):
        surface = SURFACES[theme]
        for y in range(height):
            image[y][t * panel_w * 3:(t + 1) * panel_w * 3] = list(surface) * panel_w
        for n, (icon, size) in enumerate(items):
            words = rendered[(icon.name, size)][t]
            x0 = t * panel_w + (n % columns) * cell_w + cell_pad
            y0 = (n // columns) * cell_h + cell_pad
            for py in range(size):
                for px in range(size):
                    word = words[py * size + px]
                    a = word >> 24
                    if icon.mask:
                        src = tuple(GLYPH_INK[theme][k] * a // 255 for k in range(3))
                    else:
                        src = ((word >> 16) & 255, (word >> 8) & 255, word & 255)
                    for dx, dy, scale in ((0, 0, 1), (size + cell_pad, 0, zoom)):
                        for sy in range(scale):
                            for sx in range(scale):
                                ox = x0 + dx + px * scale + sx
                                oy = y0 + dy + py * scale + sy
                                for k in range(3):
                                    old = image[oy][ox * 3 + k]
                                    image[oy][ox * 3 + k] = min(255, src[k] + old * (255 - a) // 255)
    write_png(path, width, height, image)


def main(argv):
    if len(argv) >= 3 and argv[1] == '--preview':
        write_preview(argv[2], render_all(), int(argv[3]) if len(argv) > 3 else 4)
    elif len(argv) == 2:
        os.makedirs(argv[1], exist_ok=True)
        rendered = render_all()
        write_header(os.path.join(argv[1], 'iconset_data.h'))
        write_source(os.path.join(argv[1], 'iconset_data.c'), rendered)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
