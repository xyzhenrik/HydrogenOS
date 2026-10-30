"""A small resolution-independent 2D rasteriser for tools/mkicons.py.

Shapes are described in design units on a 64 x 64 grid and rendered at any
pixel size. Coverage is exact along x and sampled SUBSAMPLES times along y,
so edges are anti-aliased at every size without texture scaling. Layers are
composited in floating point into a premultiplied RGBA image.

Pure standard library; speed is adequate for icon-sized images.
"""

import math

SUBSAMPLES = 16
CIRCLE_STEPS = 128      # polygon steps of a full turn; chord error stays below 0.03 px at 96 px
DESIGN_SIZE = 64


def colour(text, alpha=1.0):
    """'#RRGGBB' or '#RGB' to (r, g, b, a) floats in 0..1."""
    digits = text.lstrip('#')
    if len(digits) == 3:
        digits = ''.join(ch * 2 for ch in digits)
    r, g, b = (int(digits[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    return (r, g, b, alpha)


# ---------------------------------------------------------------- paths

class Path:
    """Sequence of contours built from lines, quadratic/cubic curves and arcs."""

    def __init__(self):
        self.commands = []
        self.current = None

    def move_to(self, x, y):
        self.commands.append(('M', x, y))
        self.current = (x, y)
        return self

    def line_to(self, x, y):
        self.commands.append(('L', x, y))
        self.current = (x, y)
        return self

    def quad_to(self, cx, cy, x, y):
        self.commands.append(('Q', cx, cy, x, y))
        self.current = (x, y)
        return self

    def cubic_to(self, c1x, c1y, c2x, c2y, x, y):
        self.commands.append(('C', c1x, c1y, c2x, c2y, x, y))
        self.current = (x, y)
        return self

    def close(self):
        self.commands.append(('Z',))
        return self

    def arc(self, cx, cy, rx, ry, start_deg, end_deg):
        """Elliptical arc as short chords. Angles run clockwise on screen from +x;
        the first point joins the current point with a line (or starts the contour)."""
        sweep = end_deg - start_deg
        steps = max(2, int(math.ceil(abs(sweep) / 360.0 * CIRCLE_STEPS)))
        for i in range(steps + 1):
            a = math.radians(start_deg + sweep * i / steps)
            x, y = cx + rx * math.cos(a), cy + ry * math.sin(a)
            if i == 0 and self.current is None:
                self.move_to(x, y)
            else:
                self.line_to(x, y)
        return self

    def extend(self, other):
        self.commands.extend(other.commands)
        self.current = other.current
        return self

    def flatten(self, transform):
        """Pixel-space contours [(points, closed)] after applying transform(x, y)."""
        contours = []
        points = []
        start = None

        def flush(closed):
            nonlocal points
            if len(points) > 1:
                contours.append((points, closed))
            points = []

        for command in self.commands:
            kind = command[0]
            if kind == 'M':
                flush(False)
                start = transform(command[1], command[2])
                points = [start]
            elif kind == 'L':
                points.append(transform(command[1], command[2]))
            elif kind == 'Q':
                p0 = points[-1]
                c = transform(command[1], command[2])
                p1 = transform(command[3], command[4])
                steps = _curve_steps(p0, c, p1)
                for i in range(1, steps + 1):
                    t = i / steps
                    u = 1 - t
                    points.append((u * u * p0[0] + 2 * u * t * c[0] + t * t * p1[0],
                                   u * u * p0[1] + 2 * u * t * c[1] + t * t * p1[1]))
            elif kind == 'C':
                p0 = points[-1]
                c1 = transform(command[1], command[2])
                c2 = transform(command[3], command[4])
                p1 = transform(command[5], command[6])
                steps = _curve_steps(p0, c1, c2, p1)
                for i in range(1, steps + 1):
                    t = i / steps
                    u = 1 - t
                    points.append((u ** 3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t ** 3 * p1[0],
                                   u ** 3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t ** 3 * p1[1]))
            else:
                flush(True)
                points = [start] if start else []
        flush(False)
        return contours


def _curve_steps(*points):
    length = sum(math.dist(a, b) for a, b in zip(points, points[1:]))
    return max(6, int(length * 0.8))


def rect(x, y, w, h):
    return Path().move_to(x, y).line_to(x + w, y).line_to(x + w, y + h).line_to(x, y + h).close()


def rrect(x, y, w, h, r):
    r = min(r, w / 2.0, h / 2.0)
    path = Path()
    path.arc(x + w - r, y + r, r, r, -90, 0)
    path.arc(x + w - r, y + h - r, r, r, 0, 90)
    path.arc(x + r, y + h - r, r, r, 90, 180)
    path.arc(x + r, y + r, r, r, 180, 270)
    return path.close()


def circle(cx, cy, r):
    return ellipse(cx, cy, r, r)


def ellipse(cx, cy, rx, ry):
    return Path().arc(cx, cy, rx, ry, 0, 360).close()


def polygon(points):
    path = Path().move_to(*points[0])
    for point in points[1:]:
        path.line_to(*point)
    return path.close()


def polyline(points):
    path = Path().move_to(*points[0])
    for point in points[1:]:
        path.line_to(*point)
    return path


def line(x0, y0, x1, y1):
    return Path().move_to(x0, y0).line_to(x1, y1)


def squircle(x, y, w, h, exponent=5.0):
    """Superellipse |u|^n + |v|^n = 1: the continuous-curvature rounded square of
    modern app icons."""
    path = Path()
    for i in range(CIRCLE_STEPS * 2):
        t = 2 * math.pi * i / (CIRCLE_STEPS * 2)
        c, s = math.cos(t), math.sin(t)
        px = x + w / 2 + w / 2 * math.copysign(abs(c) ** (2 / exponent), c)
        py = y + h / 2 + h / 2 * math.copysign(abs(s) ** (2 / exponent), s)
        path.line_to(px, py) if path.current else path.move_to(px, py)
    return path.close()


def round_polygon(points, radius):
    """Polygon with every corner filleted by a quadratic curve. radius is a number or
    one value per vertex; it is limited to half the adjacent edge lengths."""
    n = len(points)
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * n
    corners = []
    for i in range(n):
        prev, cur, nxt = points[i - 1], points[i], points[(i + 1) % n]
        to_prev = _unit(prev[0] - cur[0], prev[1] - cur[1])
        to_next = _unit(nxt[0] - cur[0], nxt[1] - cur[1])
        cosine = max(-1.0, min(1.0, to_prev[0] * to_next[0] + to_prev[1] * to_next[1]))
        half = math.acos(cosine) / 2
        reach = radii[i] / math.tan(half) if half > 1e-6 else 0.0
        reach = min(reach, math.dist(prev, cur) / 2, math.dist(cur, nxt) / 2)
        corners.append(((cur[0] + to_prev[0] * reach, cur[1] + to_prev[1] * reach), cur,
                        (cur[0] + to_next[0] * reach, cur[1] + to_next[1] * reach)))
    path = Path().move_to(*corners[0][0])
    for i, (entry, vertex, leave) in enumerate(corners):
        if i:
            path.line_to(*entry)
        path.quad_to(vertex[0], vertex[1], leave[0], leave[1])
    return path.close()


def rotate(points, degrees, origin=(0.0, 0.0)):
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return [(origin[0] + (x - origin[0]) * c - (y - origin[1]) * s,
             origin[1] + (x - origin[0]) * s + (y - origin[1]) * c) for x, y in points]


def _unit(dx, dy):
    length = math.hypot(dx, dy)
    return (dx / length, dy / length) if length else (0.0, 0.0)


# ---------------------------------------------------------------- strokes

def _positive(points):
    """The polygon with counter-clockwise (positive area) winding, so that a nonzero
    fill of many of them is their union."""
    area = sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(points, points[1:] + points[:1]))
    return points if area >= 0 else points[::-1]


def _disc(centre, radius):
    steps = max(12, int(radius * 6))
    return [(centre[0] + radius * math.cos(2 * math.pi * i / steps),
             centre[1] + radius * math.sin(2 * math.pi * i / steps)) for i in range(steps)]


def stroke_polygons(contours, width, cap):
    """Outline of stroked pixel-space contours as polygons to be filled non-zero.
    Joins are round; cap is 'round' or 'butt'."""
    half = width / 2.0
    polygons = []
    for points, closed in contours:
        clean = [points[0]]
        for point in points[1:]:
            if math.dist(point, clean[-1]) > 1e-6:
                clean.append(point)
        if closed and len(clean) > 1 and math.dist(clean[0], clean[-1]) < 1e-6:
            clean.pop()
        if len(clean) < 2:
            continue
        count = len(clean)
        segments = count if closed else count - 1
        directions = []
        for i in range(segments):
            p, q = clean[i], clean[(i + 1) % count]
            d = _unit(q[0] - p[0], q[1] - p[1])
            n = (-d[1] * half, d[0] * half)
            polygons.append(_positive([(p[0] + n[0], p[1] + n[1]), (q[0] + n[0], q[1] + n[1]),
                                       (q[0] - n[0], q[1] - n[1]), (p[0] - n[0], p[1] - n[1])]))
            directions.append(d)
        joins = range(count) if closed else range(1, count - 1)
        for i in joins:
            d1, d2 = directions[i - 1], directions[i % segments]
            cross = d1[0] * d2[1] - d1[1] * d2[0]
            if abs(cross) < 1e-9 and d1[0] * d2[0] + d1[1] * d2[1] > 0:
                continue
            side = -1.0 if cross > 0 else 1.0
            a1 = math.atan2(side * d1[0], -side * d1[1])
            a2 = math.atan2(side * d2[0], -side * d2[1])
            delta = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
            steps = max(1, int(math.ceil(abs(delta) / 0.3)))
            fan = [clean[i]] + [(clean[i][0] + half * math.cos(a1 + delta * k / steps),
                                 clean[i][1] + half * math.sin(a1 + delta * k / steps))
                                for k in range(steps + 1)]
            polygons.append(_positive(fan))
        if not closed and cap == 'round':
            polygons.append(_positive(_disc(clean[0], half)))
            polygons.append(_positive(_disc(clean[-1], half)))
    return polygons


# ---------------------------------------------------------------- coverage

class Mask:
    """Coverage 0..1 of every pixel of a size x size image."""

    def __init__(self, width, height, rows=None):
        self.width = width
        self.height = height
        self.rows = rows or [[0.0] * width for _ in range(height)]


def rasterize(polygons, width, height, even_odd=False):
    edges = []
    for points in polygons:
        for p, q in zip(points, points[1:] + points[:1]):
            if p[1] == q[1]:
                continue
            if p[1] < q[1]:
                edges.append((p[1], q[1], p[0], (q[0] - p[0]) / (q[1] - p[1]), 1))
            else:
                edges.append((q[1], p[1], q[0], (p[0] - q[0]) / (p[1] - q[1]), -1))
    mask = Mask(width, height)
    if not edges:
        return mask
    weight = 1.0 / SUBSAMPLES
    first = max(0, int(math.floor(min(e[0] for e in edges))))
    last = min(height, int(math.ceil(max(e[1] for e in edges))))
    for row in range(first, last):
        active = [e for e in edges if e[0] < row + 1 and e[1] > row]
        if not active:
            continue
        acc = [0.0] * (width + 2)
        for sub in range(SUBSAMPLES):
            y = row + (sub + 0.5) * weight
            crossings = sorted((e[2] + (y - e[0]) * e[3], e[4]) for e in active if e[0] <= y < e[1])
            winding = 0
            left = 0.0
            for x, direction in crossings:
                inside = (winding & 1) if even_odd else winding != 0
                if inside:
                    _add_span(acc, left, x, width, weight)
                winding += direction
                left = x
        out = mask.rows[row]
        for x in range(width):
            out[x] = min(1.0, acc[x])
    return mask


def _add_span(acc, xa, xb, width, weight):
    xa = min(max(xa, 0.0), width)
    xb = min(max(xb, 0.0), width)
    if xb <= xa:
        return
    ia, ib = int(xa), int(xb)
    if ia == ib:
        acc[ia] += (xb - xa) * weight
        return
    acc[ia] += (ia + 1 - xa) * weight
    for k in range(ia + 1, ib):
        acc[k] += weight
    acc[ib] += (xb - ib) * weight


def blur_mask(mask, sigma):
    """Three box passes per axis approximate a gaussian of the given sigma (pixels)."""
    radius = max(1, int(round(sigma * 1.1)))
    rows = [row[:] for row in mask.rows]
    for _ in range(3):
        rows = [_box_line(row, radius) for row in rows]
        columns = [_box_line([rows[y][x] for y in range(mask.height)], radius) for x in range(mask.width)]
        rows = [[columns[x][y] for x in range(mask.width)] for y in range(mask.height)]
    return Mask(mask.width, mask.height, rows)


def _box_line(values, radius):
    n = len(values)
    total = sum(values[min(max(k, 0), n - 1)] for k in range(-radius, radius + 1))
    out = []
    span = 2 * radius + 1
    for i in range(n):
        out.append(total / span)
        total += values[min(i + radius + 1, n - 1)] - values[max(i - radius, 0)]
    return out


# ---------------------------------------------------------------- paints

def _lerp_stops(stops, t):
    if t <= stops[0][0]:
        return stops[0][1]
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return tuple(a + (b - a) * f for a, b in zip(c0, c1))
    return stops[-1][1]


class Solid:
    def __init__(self, rgba):
        self.rgba = rgba

    def bind(self, origin, scale):
        rgba = self.rgba
        return lambda x, y: rgba


class Linear:
    """Gradient between two design-space points; stops are (t, rgba)."""

    def __init__(self, start, end, stops):
        self.start, self.end, self.stops = start, end, stops

    def bind(self, origin, scale):
        x0 = origin[0] + self.start[0] * scale
        y0 = origin[1] + self.start[1] * scale
        dx = origin[0] + self.end[0] * scale - x0
        dy = origin[1] + self.end[1] * scale - y0
        norm = dx * dx + dy * dy
        stops = self.stops
        return lambda x, y: _lerp_stops(stops, ((x - x0) * dx + (y - y0) * dy) / norm)


class Radial:
    def __init__(self, centre, radius, stops):
        self.centre, self.radius, self.stops = centre, radius, stops

    def bind(self, origin, scale):
        cx = origin[0] + self.centre[0] * scale
        cy = origin[1] + self.centre[1] * scale
        radius = self.radius * scale
        stops = self.stops
        return lambda x, y: _lerp_stops(stops, math.hypot(x - cx, y - cy) / radius)


def vertical(top, bottom, y0=0.0, y1=DESIGN_SIZE):
    return Linear((0, y0), (0, y1), [(0.0, top), (1.0, bottom)])


# ---------------------------------------------------------------- canvas

class Canvas:
    """Draws design-space shapes into a size x size premultiplied RGBA image.

    The 64-unit design grid maps onto the square of side size - 2 * pad pixels
    whose top-left corner sits at (pad, pad + shift) pixels."""

    def __init__(self, size, pad=0, shift=0):
        self.size = size
        self.pad = pad
        self.scale = (size - 2 * pad) / DESIGN_SIZE
        self.origin = (float(pad), float(pad + shift))
        n = size * size
        self.r, self.g, self.b, self.a = [0.0] * n, [0.0] * n, [0.0] * n, [0.0] * n

    def transform(self, x, y):
        return (self.origin[0] + x * self.scale, self.origin[1] + y * self.scale)

    def mask(self, path, even_odd=False):
        polygons = [pts for pts, _ in path.flatten(self.transform)]
        return rasterize(polygons, self.size, self.size, even_odd)

    def stroke_mask(self, path, width, cap='round', min_px=1.0):
        width_px = max(width * self.scale, min_px)
        return rasterize(stroke_polygons(path.flatten(self.transform), width_px, cap),
                         self.size, self.size)

    def fill(self, path, paint, opacity=1.0, even_odd=False, clip=None):
        self._composite(self.mask(path, even_odd), paint, opacity, clip)

    def stroke(self, path, paint, width, cap='round', opacity=1.0, clip=None, min_px=1.0):
        self._composite(self.stroke_mask(path, width, cap, min_px), paint, opacity, clip)

    def shadow(self, path, dy, blur, rgba=(0.0, 0.0, 0.0, 1.0), opacity=0.3, even_odd=False):
        """Soft drop shadow of a filled path; dy and blur are in design units."""
        moved = self.mask(_shifted(path, 0.0, dy), even_odd)
        self._composite(blur_mask(moved, blur * self.scale), Solid(rgba), opacity, None)

    def _composite(self, mask, paint, opacity, clip):
        shade = paint.bind(self.origin, self.scale)
        size = self.size
        for y in range(size):
            row = mask.rows[y]
            clip_row = clip.rows[y] if clip else None
            for x in range(size):
                coverage = row[x]
                if coverage <= 0.0:
                    continue
                if clip_row is not None:
                    coverage *= clip_row[x]
                r, g, b, a = shade(x + 0.5, y + 0.5)
                alpha = coverage * a * opacity
                if alpha <= 0.0:
                    continue
                i = y * size + x
                keep = 1.0 - alpha
                self.r[i] = r * alpha + self.r[i] * keep
                self.g[i] = g * alpha + self.g[i] * keep
                self.b[i] = b * alpha + self.b[i] * keep
                self.a[i] = alpha + self.a[i] * keep

    def argb(self):
        """Premultiplied 0xAARRGGBB words, row by row."""
        words = []
        for r, g, b, a in zip(self.r, self.g, self.b, self.a):
            alpha = int(round(a * 255))
            words.append((alpha << 24) | (min(alpha, int(round(r * 255))) << 16) |
                         (min(alpha, int(round(g * 255))) << 8) | min(alpha, int(round(b * 255))))
        return words

    def argb_template(self):
        """Coverage only, as premultiplied white: tinted by the caller at draw time."""
        return [(a << 24) | (a << 16) | (a << 8) | a
                for a in (int(round(v * 255)) for v in self.a)]


def _shifted(path, dx, dy):
    moved = Path()
    for command in path.commands:
        if command[0] == 'Z':
            moved.commands.append(command)
            continue
        coords = list(command[1:])
        coords[0::2] = [c + dx for c in coords[0::2]]
        coords[1::2] = [c + dy for c in coords[1::2]]
        moved.commands.append((command[0], *coords))
    return moved
