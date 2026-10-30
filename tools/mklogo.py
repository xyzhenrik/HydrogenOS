#!/usr/bin/env python3
"""Builds the HydrogenOS logo resources from the artwork in assets/.

The artwork (a gradient-stroked water drop on a transparent background) is the
single source of truth. The tool decodes the PNGs with the standard library,
crops to the drop, and emits a C header with:

  * alpha-coverage bitmaps of the drop at several square sizes, area-averaged
    from the full-resolution coverage with padding, and
  * the vertical stroke gradient of the artwork for light and dark
    backgrounds, sampled from the transparent artwork (teal, for light
    surfaces) and from the dark tile variant (light ink, for dark surfaces).

Only coverage is stored; the colour follows from the gradient at draw time, so
the one bitmap serves every chrome and theme.
"""

import argparse
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# The drop occupies the square minus this many per mille of the edge on every
# side.
PADDING_PERMILLE = 40

# Heights the gradient is sampled at, from the top to the bottom of the drop.
GRADIENT_STOPS = 8

# Pixels at least this opaque (0..255) count as solid stroke for colour
# sampling; pixels at least BOUNDS_ALPHA opaque count for the bounding box.
SOLID_ALPHA = 200
BOUNDS_ALPHA = 8

# Pixels of the dark variant brighter than this are stroke, darker are tile.
DARK_STROKE_LUMA = 96

# The dark variant may differ from the transparent artwork by this many rows.
MAX_HEIGHT_SKEW = 2

GRADIENT_LOOKUP = """\
/* Colour of the drop's stroke at destination row `row` of a `size` x `size`
   bitmap, interpolated between the gradient stops. `size` stays below 2000. */
static inline uint32_t logo_gradient_at(const uint32_t *gradient, int size, int row) {
    int span = size * (1000 - 2 * LOGO_PADDING_PERMILLE);
    int position = (2 * row + 1) * 500 - size * LOGO_PADDING_PERMILLE;
    int stop = position * LOGO_GRADIENT_STOPS * 128 / span - 64;
    if (stop < 0)
        stop = 0;
    if (stop > (LOGO_GRADIENT_STOPS - 1) * 128)
        stop = (LOGO_GRADIENT_STOPS - 1) * 128;
    int index = stop >> 7;
    int weight = stop & 127;
    int next = index + 1 < LOGO_GRADIENT_STOPS ? index + 1 : index;
    uint32_t color = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        int from = (int)((gradient[index] >> shift) & 0xFF);
        int to = (int)((gradient[next] >> shift) & 0xFF);
        color |= (uint32_t)((from * (128 - weight) + to * weight + 64) >> 7) << shift;
    }
    return color;
}
"""


class PngError(Exception):
    pass


def paeth(left, up, up_left):
    estimate = left + up - up_left
    d_left = abs(estimate - left)
    d_up = abs(estimate - up)
    d_up_left = abs(estimate - up_left)
    if d_left <= d_up and d_left <= d_up_left:
        return left
    return up if d_up <= d_up_left else up_left


def unfilter(raw, width, height, bytes_per_pixel):
    """Reverses the per-scanline PNG filters; returns a list of row bytearrays."""
    stride = width * bytes_per_pixel
    if len(raw) != height * (stride + 1):
        raise PngError("image data has an unexpected length")
    rows = []
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        kind = raw[position]
        row = bytearray(raw[position + 1:position + 1 + stride])
        position += stride + 1
        if kind == 0:
            pass
        elif kind == 1:
            for i in range(bytes_per_pixel, stride):
                row[i] = (row[i] + row[i - bytes_per_pixel]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                up_left = previous[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                row[i] = (row[i] + paeth(left, previous[i], up_left)) & 0xFF
        else:
            raise PngError("unknown filter type %d" % kind)
        rows.append(row)
        previous = row
    return rows


def read_png(path):
    """Decodes an 8-bit, non-interlaced RGB or RGBA PNG.

    Returns (width, height, rows) with every row as RGBA bytes.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != PNG_SIGNATURE:
        raise PngError("%s is not a PNG file" % path)
    position = 8
    header = None
    compressed = bytearray()
    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        chunk = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            compressed += chunk
        elif kind == b"IEND":
            break
    if header is None:
        raise PngError("%s has no IHDR chunk" % path)
    width, height, depth, color_type, _, _, interlace = header
    if depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise PngError("%s: only 8-bit non-interlaced RGB/RGBA is supported" % path)
    channels = 4 if color_type == 6 else 3
    rows = unfilter(zlib.decompress(bytes(compressed)), width, height, channels)
    if channels == 3:
        rgba_rows = []
        for row in rows:
            rgba = bytearray(width * 4)
            for x in range(width):
                rgba[4 * x:4 * x + 3] = row[3 * x:3 * x + 3]
                rgba[4 * x + 3] = 255
            rgba_rows.append(rgba)
        rows = rgba_rows
    return width, height, rows


def bounding_box(rows, width, height):
    left, top, right, bottom = width, height, -1, -1
    for y, row in enumerate(rows):
        for x in range(width):
            if row[4 * x + 3] >= BOUNDS_ALPHA:
                left = min(left, x)
                right = max(right, x)
                top = min(top, y)
                bottom = max(bottom, y)
    if right < 0:
        raise PngError("the artwork has no visible pixels")
    return left, top, right + 1, bottom + 1


def box_resample(plane, src_w, src_h, dst_size, origin_x, origin_y, scale):
    """Area-averages a coverage plane into dst_size x dst_size pixels.

    Destination pixel (i, j) covers the source square of edge `scale` starting
    at (origin_x + i * scale, origin_y + j * scale). Fractional overlap is
    weighted, which is exact box filtering; source pixels outside the plane
    count as empty.
    """
    out = []
    for j in range(dst_size):
        y0 = origin_y + j * scale
        y1 = y0 + scale
        row = []
        for i in range(dst_size):
            x0 = origin_x + i * scale
            x1 = x0 + scale
            total = 0.0
            for sy in range(max(0, int(y0)), min(src_h, int(y1) + 1)):
                weight_y = min(y1, sy + 1) - max(y0, sy)
                if weight_y <= 0:
                    continue
                line = plane[sy]
                for sx in range(max(0, int(x0)), min(src_w, int(x1) + 1)):
                    weight_x = min(x1, sx + 1) - max(x0, sx)
                    if weight_x > 0:
                        total += line[sx] * weight_x * weight_y
            row.append(min(255, int(total / (scale * scale) + 0.5)))
        out.append(row)
    return out


def render_sizes(rows, width, height, sizes):
    """Returns ({size: coverage rows}, bounds) for every requested size."""
    bounds = bounding_box(rows, width, height)
    left, top, right, bottom = bounds
    extent = max(right - left, bottom - top)
    centre_x = (left + right) / 2.0
    centre_y = (top + bottom) / 2.0
    plane = [[row[4 * x + 3] for x in range(width)] for row in rows]
    rendered = {}
    for size in sizes:
        scale = extent / (size * (1000 - 2 * PADDING_PERMILLE) / 1000.0)
        rendered[size] = box_resample(plane, width, height, size,
                                      centre_x - scale * size / 2.0,
                                      centre_y - scale * size / 2.0, scale)
    return rendered, bounds


def sample_gradient(rows, bounds, stops):
    """Mean stroke colour at `stops` evenly spaced heights of the drop.

    Only fully inked pixels count, so the anti-aliased rim and any matte of
    the background do not tint the result.
    """
    left, top, right, bottom = bounds
    colours = []
    height = bottom - top
    for stop in range(stops):
        y0 = top + height * stop // stops
        y1 = max(y0 + 1, top + height * (stop + 1) // stops)
        sums = [0, 0, 0]
        count = 0
        for y in range(y0, y1):
            row = rows[y]
            for x in range(left, right):
                if row[4 * x + 3] >= SOLID_ALPHA:
                    for c in range(3):
                        sums[c] += row[4 * x + c]
                    count += 1
        if count == 0:
            raise PngError("no stroke pixels at gradient stop %d" % stop)
        colours.append(tuple((s + count // 2) // count for s in sums))
    return colours


def dark_stroke_pixels(rows, reference_rows):
    """The artwork's own stroke inside the dark tile variant, as RGBA rows.

    The dark variant is the same drop in light ink on a dark rounded tile on
    white. Its stroke is what is brighter than the tile, restricted to the
    footprint of the transparent artwork so the white margin never counts. The
    variants differ by a pixel or two in height; the sampled interior of the
    stroke is insensitive to that.
    """
    result = []
    for y, reference in enumerate(reference_rows[:len(rows)]):
        row = bytearray(len(reference))
        for x in range(len(reference) // 4):
            r, g, b = rows[y][4 * x:4 * x + 3]
            luma = (299 * r + 587 * g + 114 * b) // 1000
            if reference[4 * x + 3] >= SOLID_ALPHA and luma > DARK_STROKE_LUMA:
                row[4 * x:4 * x + 3] = bytes((r, g, b))
                row[4 * x + 3] = 255
        result.append(row)
    return result


def format_colour(rgb):
    return "0x%02X%02X%02Xu" % rgb


def emit_header(rendered, gradients, out):
    sizes = sorted(rendered)
    out.write("/* Generated by tools/mklogo.py from assets/. Do not edit. */\n")
    out.write("#pragma once\n#include <stdint.h>\n\n")
    out.write("#define LOGO_GRADIENT_STOPS %d\n" % GRADIENT_STOPS)
    out.write("#define LOGO_SIZE_COUNT %d\n" % len(sizes))
    out.write("/* Empty margin on each side of the drop, per mille of the bitmap edge. */\n")
    out.write("#define LOGO_PADDING_PERMILLE %d\n" % PADDING_PERMILLE)
    out.write("/* Stroke colour from the top to the bottom of the drop. */\n")
    for name, gradient in gradients:
        out.write("static const uint32_t logo_gradient_%s[LOGO_GRADIENT_STOPS] = { %s };\n" %
                  (name, ", ".join(format_colour(c) for c in gradient)))
    out.write("\n" + GRADIENT_LOOKUP + "\n")
    out.write("/* Edge lengths of the coverage bitmaps below, ascending. */\n")
    out.write("static const uint16_t logo_sizes[LOGO_SIZE_COUNT] = { %s };\n\n" %
              ", ".join(str(s) for s in sizes))
    for size in sizes:
        out.write("/* %dx%d coverage, 0..255. */\n" % (size, size))
        out.write("static const uint8_t logo_coverage_%d[%d] = {\n" % (size, size * size))
        for row in rendered[size]:
            out.write("    " + ",".join(str(v) for v in row) + ",\n")
        out.write("};\n\n")
    out.write("static const uint8_t *const logo_coverage[LOGO_SIZE_COUNT] = { %s };\n" %
              ", ".join("logo_coverage_%d" % s for s in sizes))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--light", required=True,
                        help="transparent artwork (teal gradient, for light surfaces)")
    parser.add_argument("--dark", required=True,
                        help="dark tile variant (light-ink gradient, for dark surfaces)")
    parser.add_argument("--sizes", required=True,
                        help="comma-separated square sizes in pixels")
    parser.add_argument("--gradients", choices=("light", "dark", "both"), default="both",
                        help="which stroke gradients to emit")
    parser.add_argument("--out", required=True, help="generated C header")
    args = parser.parse_args()

    try:
        sizes = sorted({int(size) for size in args.sizes.split(",")})
        if not sizes or sizes[0] < 8:
            raise PngError("sizes must be at least 8 pixels")
        width, height, rows = read_png(args.light)
        dark_width, dark_height, dark_rows = read_png(args.dark)
        if dark_width != width or abs(dark_height - height) > MAX_HEIGHT_SKEW:
            raise PngError("light and dark artwork differ in size")
        rendered, bounds = render_sizes(rows, width, height, sizes)
        gradients = []
        if args.gradients in ("light", "both"):
            gradients.append(("light", sample_gradient(rows, bounds, GRADIENT_STOPS)))
        if args.gradients in ("dark", "both"):
            dark_ink = dark_stroke_pixels(dark_rows, rows)
            gradients.append(("dark", sample_gradient(dark_ink, bounds, GRADIENT_STOPS)))
    except (OSError, ValueError, PngError, zlib.error) as error:
        sys.stderr.write("mklogo: %s\n" % error)
        return 1
    with open(args.out, "w") as out:
        emit_header(rendered, gradients, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
