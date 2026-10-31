#!/usr/bin/env python3
"""Builds the HydrogenOS boot-splash wordmark as pre-rasterised alpha bitmaps.

The letterforms are drawn here, as vector geometry, in a geometric monoline
sans: one stroke weight for every letter, circular bowls, a spine-driven S and
optically spaced pairs. They are rasterised with tools/vecraster.py at each
requested cap height and written as a C header of 8-bit coverage bitmaps, so
the kernel only blits alpha and needs no glyph code.

Each size is designed for its own pixel grid: the stroke is a whole number of
pixels, stems and bars sit on pixel boundaries, and the glyph origins are
rounded, so straight edges are crisp and only curves are anti-aliased. The
"OS" suffix is baked in at a lower coverage than "HYDROGEN"; the kernel
multiplies the whole bitmap by one colour.

    mkwordmark.py --caps 36,72 --out gen/wordmark.h
    mkwordmark.py --caps 36,72 --preview wordmark.png [ZOOM]
"""

import argparse
import math
import struct
import sys
import zlib

from vecraster import rasterize, stroke_polygons

TEXT = "HYDROGEN"
SUFFIX = "OS"

# Stroke weight and overshoot of round letters, as fractions of the cap height.
STROKE = 0.112
OVERSHOOT = 0.014
# Round strokes are drawn slightly heavier than stems: at equal width a curve
# looks lighter than a stem.
ROUND_BOOST = 1.04
# Horizontal and diagonal strokes are a little lighter than stems for the
# same reason in reverse.
DIAGONAL_WEIGHT = 0.94

# Suffix coverage relative to the name, and the word space before it, in caps.
SUFFIX_COVERAGE = 0.62
WORD_SPACE = 0.52

# Transparent margin around the ink in pixels, for filtering and for the
# kernel's bilinear scaling.
MARGIN = 2

# Optical side bearing of an outline's edge, in caps. Gaps add the right edge
# of the left letter to the left edge of the right one.
BEARING = {"stem": 0.150, "round": 0.088, "open": 0.095, "diagonal": 0.040}
EDGES = {
    # letter: (left edge, right edge)
    "H": ("stem", "stem"),
    "Y": ("diagonal", "diagonal"),
    "D": ("stem", "round"),
    "R": ("stem", "diagonal"),
    "O": ("round", "round"),
    "G": ("round", "round"),
    "E": ("stem", "open"),
    "N": ("stem", "stem"),
    "S": ("round", "round"),
}
# Hand-tuned corrections of single pairs, in caps, after the table above.
KERNING = {
    ("R", "O"): 0.022,
    ("Y", "D"): 0.016,
}

# Quarter-ellipse bezier handle length.
KAPPA = 0.5522847498


# ---------------------------------------------------------------- geometry

def area(points):
    return sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(points, points[1:] + points[:1]))


def winding(points):
    """The polygon wound so that a non-zero fill of many of them is their union."""
    return points if area(points) >= 0 else points[::-1]


def rectangle(x0, y0, x1, y1):
    return winding([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def clip(points, x0, y0, x1, y1):
    """Sutherland-Hodgman clip of a convex polygon to a box."""
    def against(polygon, inside, cut):
        out = []
        for i, current in enumerate(polygon):
            previous = polygon[i - 1]
            if inside(current):
                if not inside(previous):
                    out.append(cut(previous, current))
                out.append(current)
            elif inside(previous):
                out.append(cut(previous, current))
        return out

    def at_x(x):
        return lambda p, q: (x, p[1] + (q[1] - p[1]) * (x - p[0]) / (q[0] - p[0]))

    def at_y(y):
        return lambda p, q: (p[0] + (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]), y)

    points = against(points, lambda p: p[0] >= x0, at_x(x0))
    points = against(points, lambda p: p[0] <= x1, at_x(x1))
    points = against(points, lambda p: p[1] >= y0, at_y(y0))
    points = against(points, lambda p: p[1] <= y1, at_y(y1))
    return winding(points) if len(points) >= 3 else []


def band(p, q, width, reach):
    """Straight stroke along p-q, extended by `reach` at both ends."""
    dx, dy = q[0] - p[0], q[1] - p[1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    nx, ny = -uy * width / 2, ux * width / 2
    a = (p[0] - ux * reach, p[1] - uy * reach)
    b = (q[0] + ux * reach, q[1] + uy * reach)
    return winding([(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny),
                    (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)])


def arc_points(cx, cy, rx, ry, start, end):
    """Elliptical arc, angles in degrees clockwise on screen; may run either way."""
    steps = max(8, int(abs(end - start) / 360.0 * 360))
    return [(cx + rx * math.cos(math.radians(start + (end - start) * i / steps)),
             cy + ry * math.sin(math.radians(start + (end - start) * i / steps)))
            for i in range(steps + 1)]


def cubic(p0, p1, p2, p3, steps=48):
    out = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def joined(*pieces):
    """Concatenates polylines whose end and start points coincide."""
    out = list(pieces[0])
    for piece in pieces[1:]:
        out.extend(piece[1:])
    return out


def stroke_path(points, width):
    """Constant-width butt-capped stroke of an open polyline."""
    return stroke_polygons([(points, False)], width, "butt")


def modulated_stroke(points, width_at):
    """Stroke whose width follows width_at(s), s in 0..1 along the polyline.

    The outline is built from the smoothed normals of the centre line, so the
    edges stay clean where the width changes.
    """
    lengths = [0.0]
    for p, q in zip(points, points[1:]):
        lengths.append(lengths[-1] + math.dist(p, q))
    left, right = [], []
    for i, point in enumerate(points):
        a = points[max(0, i - 2)]
        b = points[min(len(points) - 1, i + 2)]
        length = math.dist(a, b)
        nx, ny = -(b[1] - a[1]) / length, (b[0] - a[0]) / length
        half = width_at(lengths[i] / lengths[-1]) / 2
        left.append((point[0] + nx * half, point[1] + ny * half))
        right.append((point[0] - nx * half, point[1] - ny * half))
    return winding(left + right[::-1])


# ---------------------------------------------------------------- letters

class Metrics:
    """Pixel dimensions of one size; everything the letters need."""

    def __init__(self, cap):
        self.cap = cap
        self.stem = max(2, round(cap * STROKE))
        self.round = self.stem * ROUND_BOOST
        self.light = self.stem * DIAGONAL_WEIGHT
        self.overshoot = cap * OVERSHOOT
        self.top = MARGIN + math.ceil(self.overshoot)
        self.bottom = self.top + cap


def round_width(m):
    """Outer diameter of the circular letters."""
    return round(m.cap + 2 * m.overshoot)


def letter_h(m):
    w = round(m.cap * 0.74)
    t = m.stem
    bar = round((m.cap - t) / 2)
    return w, [rectangle(0, 0, t, m.cap), rectangle(w - t, 0, w, m.cap),
               rectangle(t - 1, bar, w - t + 1, bar + t)]


def letter_e(m):
    w = round(m.cap * 0.56)
    t = m.stem
    bar = round((m.cap - t) / 2)
    return w, [rectangle(0, 0, t, m.cap), rectangle(0, 0, w, t),
               rectangle(0, bar, round(w * 0.9), bar + t),
               rectangle(0, m.cap - t, w, m.cap)]


def letter_n(m):
    w = round(m.cap * 0.76)
    t = m.stem
    a = m.light * math.hypot(m.cap, w) / m.cap
    return w, [rectangle(0, 0, t, m.cap), rectangle(w - t, 0, w, m.cap),
               winding([(0, 0), (a, 0), (w, m.cap), (w - a, m.cap)])]


def letter_y(m):
    w = round(m.cap * 0.78)
    t = m.stem
    junction = m.cap * 0.47
    half = w / 2
    # The arm's top edge is as wide as the stroke is, measured horizontally.
    top = t * 1.2
    for _ in range(8):
        slope = math.atan2(half - top / 2, junction)
        top = m.light / math.cos(slope)
    left = band((top / 2, 0), (half, junction), m.light, junction)
    right = band((w - top / 2, 0), (half, junction), m.light, junction)
    polygons = [clip(left, 0, 0, half, junction + m.light),
                clip(right, half, 0, w, junction + m.light),
                rectangle(half - t / 2, junction, half + t / 2, m.cap)]
    return w, [p for p in polygons if p]


def bowl(m, x_stem, y0, y1, right):
    """Right-hand bowl: top bar, half circle, bottom bar, as one stroke."""
    t = m.round
    radius = (y1 - y0 - t) / 2
    cy = (y0 + y1) / 2
    cx = right - t / 2 - radius
    arc = arc_points(cx, cy, radius, radius, -90, 90)
    return stroke_path(joined([(x_stem, y0 + t / 2)], arc[:1] + arc, arc[-1:] + [(x_stem, y1 - t / 2)]), t)


def letter_d(m):
    w = round(m.cap * 0.80)
    t = m.stem
    return w, [rectangle(0, 0, t, m.cap)] + bowl(m, t / 2, 0, m.cap, w)


def letter_r(m):
    w = round(m.cap * 0.66)
    t = m.stem
    height = round(m.cap * 0.56)
    a = m.light * math.hypot(m.cap - height, w * 0.5) / (m.cap - height)
    foot = (w - a / 2, m.cap)
    start = (w * 0.40, height - t / 2)
    leg = clip(band(start, foot, m.light, m.cap), 0, height - t, w, m.cap)
    return w, [rectangle(0, 0, t, m.cap)] + bowl(m, t / 2, 0, height, w - 1.0) + [leg]


def letter_o(m):
    d = round_width(m)
    t = m.round
    radius = (d - t) / 2
    return d, stroke_polygons(
        [(arc_points(d / 2, m.cap / 2, radius, radius, 0, 360)[:-1], True)], t, "butt")


def letter_g(m):
    d = round_width(m)
    t = m.round
    radius = (d - t) / 2
    cx, cy = d / 2, m.cap / 2
    arc = arc_points(cx, cy, radius, radius, -42, -360)
    bar = rectangle(cx + t * 0.05, cy - t / 2, cx + radius + t / 2, cy + t / 2)
    return d, stroke_path(arc, t) + [bar]


def letter_s(m):
    """Spine S: two bowls joined by a tangent-continuous diagonal spine.

    The centre line is built as a quarter-ellipse into each outer extreme and
    a cubic spine through the middle, then stroked with a width that is a
    touch lighter in the spine, where the stroke runs obliquely.
    """
    w = round(m.cap * 0.62)
    t = m.round
    h = t / 2
    ov = m.overshoot
    xl, xr = h, w - h
    yt, yb = h - ov, m.cap - h + ov
    mid = (w / 2, m.cap / 2)
    # Upper bowl: ellipse centre and radii.
    ux = w / 2 + 0.01 * m.cap
    upper_ry = (m.cap * 0.5 - yt) * 0.54
    uy = yt + upper_ry
    rx_r, rx_l = xr - ux, ux - xl
    terminal = -36.0
    first = arc_points(ux, uy, rx_r, upper_ry, terminal, -90)
    second = arc_points(ux, uy, rx_l, upper_ry, -90, -180)
    left = second[-1]
    slope = math.radians(30)
    reach_in = (mid[1] - left[1]) * 0.55
    spine = cubic(left, (left[0], left[1] + reach_in),
                  (mid[0] - math.cos(slope) * reach_in * 0.9,
                   mid[1] - math.sin(slope) * reach_in * 0.9), mid)
    upper = joined(first, second, spine)
    lower = [(w - x, m.cap - y) for x, y in upper[::-1]]
    path = joined(upper, lower)

    def width_at(s):
        spine_weight = 1.0 - 0.06 * math.exp(-((s - 0.5) / 0.16) ** 2)
        return t * spine_weight

    return w, [modulated_stroke(path, width_at)]


LETTERS = {"H": letter_h, "Y": letter_y, "D": letter_d, "R": letter_r, "O": letter_o,
           "G": letter_g, "E": letter_e, "N": letter_n, "S": letter_s}


# ---------------------------------------------------------------- layout

def pair_gap(m, left, right):
    gap = BEARING[EDGES[left][1]] + BEARING[EDGES[right][0]]
    gap += KERNING.get((left, right), 0.0)
    return gap * m.cap


def place(polygons, dx, dy):
    return [[(x + dx, y + dy) for x, y in p] for p in polygons]


def compose(m):
    """Returns (width, height, [(placed polygons, coverage factor)] per letter)."""
    glyphs = []
    x = float(MARGIN)
    previous = None
    for index, letter in enumerate(TEXT + SUFFIX):
        width, polygons = LETTERS[letter](m)
        if previous is not None:
            gap = pair_gap(m, previous, letter)
            if index == len(TEXT):
                gap = WORD_SPACE * m.cap
            x += gap
        factor = SUFFIX_COVERAGE if index >= len(TEXT) else 1.0
        glyphs.append((place(polygons, round(x), m.top), factor))
        x += width
        previous = letter
    return math.ceil(x) + MARGIN, m.bottom + math.ceil(m.overshoot) + MARGIN, glyphs


def render(cap):
    m = Metrics(cap)
    width, height, glyphs = compose(m)
    plane = [[0.0] * width for _ in range(height)]
    for polygons, factor in glyphs:
        mask = rasterize(polygons, width, height)
        for y in range(height):
            row = plane[y]
            for x in range(width):
                value = mask.rows[y][x] * factor
                if value > row[x]:
                    row[x] = value
    return width, height, [[min(255, int(v * 255 + 0.5)) for v in row] for row in plane]


# ---------------------------------------------------------------- output

def emit_header(sizes, out):
    out.write("/* Generated by tools/mkwordmark.py. Do not edit. */\n")
    out.write("#pragma once\n#include <stdint.h>\n\n")
    out.write("#define WORDMARK_SIZE_COUNT %d\n\n" % len(sizes))
    out.write("/* Cap height each bitmap was drawn for, ascending; the bitmaps are 8-bit\n"
              "   coverage with a transparent margin, vertically centred on the cap box. */\n")
    out.write("static const uint16_t wordmark_caps[WORDMARK_SIZE_COUNT] = { %s };\n" %
              ", ".join(str(cap) for cap, _, _, _ in sizes))
    out.write("static const uint16_t wordmark_widths[WORDMARK_SIZE_COUNT] = { %s };\n" %
              ", ".join(str(w) for _, w, _, _ in sizes))
    out.write("static const uint16_t wordmark_heights[WORDMARK_SIZE_COUNT] = { %s };\n\n" %
              ", ".join(str(h) for _, _, h, _ in sizes))
    for cap, w, h, rows in sizes:
        out.write("/* %dx%d, cap height %d. */\n" % (w, h, cap))
        out.write("static const uint8_t wordmark_coverage_%d[%d] = {\n" % (cap, w * h))
        for row in rows:
            out.write("    " + ",".join(str(v) for v in row) + ",\n")
        out.write("};\n\n")
    out.write("static const uint8_t *const wordmark_coverage[WORDMARK_SIZE_COUNT] = { %s };\n" %
              ", ".join("wordmark_coverage_%d" % cap for cap, _, _, _ in sizes))


def write_png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    raw = b"".join(b"\0" + bytes(row) for row in rows)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def preview(sizes, path, zoom):
    """Light letters on the splash background, one size below the other."""
    background = (0x10, 0x18, 0x22)
    ink = (0xEE, 0xF6, 0xFF)
    pad = 12
    width = max(w for _, w, _, _ in sizes) * zoom + 2 * pad
    height = sum(h * zoom + pad for _, _, h, _ in sizes) + pad
    canvas = [bytearray(bytes(background) * width) for _ in range(height)]
    y0 = pad
    for _, w, h, rows in sizes:
        for y in range(h * zoom):
            for x in range(w * zoom):
                alpha = rows[y // zoom][x // zoom]
                pixel = bytes((background[c] * (255 - alpha) + ink[c] * alpha + 127) // 255
                              for c in range(3))
                canvas[y0 + y][3 * (pad + x):3 * (pad + x) + 3] = pixel
        y0 += h * zoom + pad
    write_png(path, width, height, canvas)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--caps", required=True, help="comma-separated cap heights in pixels")
    parser.add_argument("--out", help="generated C header")
    parser.add_argument("--preview", help="write a PNG of the bitmaps instead")
    parser.add_argument("--zoom", type=int, default=1)
    args = parser.parse_args()
    try:
        caps = sorted({int(c) for c in args.caps.split(",")})
        if not caps or caps[0] < 12:
            raise ValueError("cap heights must be at least 12 pixels")
    except ValueError as error:
        sys.stderr.write("mkwordmark: %s\n" % error)
        return 1
    sizes = [(cap,) + render(cap) for cap in caps]
    if args.preview:
        preview(sizes, args.preview, args.zoom)
    if args.out:
        with open(args.out, "w") as out:
            emit_header(sizes, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
