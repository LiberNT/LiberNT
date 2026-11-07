#!/usr/bin/env python3
# PROJECT:     LiberNT user32
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Draw the system cursors at every Windows cursor size and write them as PNG cursor files
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

import math
import os
import struct
import sys
import zlib

CANVAS = 32
SIZES = (32, 40, 48, 56, 64, 72, 80, 96, 128)
OUTLINE = 1.0
WHITE = (1.0, 1.0, 1.0, 1.0)
BLACK = (0.0, 0.0, 0.0, 1.0)
BLUE = (0x00 / 255.0, 0x78 / 255.0, 0xD4 / 255.0, 1.0)
SHADOW_OFFSET = (0.8, 1.0)
SHADOW_BLUR = 1.0
SHADOW_OPACITY = 0.32
FRAMES = 30
JIFFIES = 2


class Shape:
    def __init__(self, sdf, box):
        self.sdf = sdf
        self.box = box


def polygon(points):
    pts = [(float(x), float(y)) for x, y in points]
    count = len(pts)

    def sdf(x, y):
        best = (x - pts[0][0]) ** 2 + (y - pts[0][1]) ** 2
        sign = 1.0
        j = count - 1
        for i in range(count):
            vix, viy = pts[i]
            vjx, vjy = pts[j]
            ex, ey = vjx - vix, vjy - viy
            wx, wy = x - vix, y - viy
            t = (wx * ex + wy * ey) / (ex * ex + ey * ey)
            t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
            bx, by = wx - ex * t, wy - ey * t
            best = min(best, bx * bx + by * by)
            c1, c2, c3 = y >= viy, y < vjy, ex * wy > ey * wx
            if (c1 and c2 and c3) or not (c1 or c2 or c3):
                sign = -sign
            j = i
        return sign * math.sqrt(best)

    return Shape(sdf, (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts),
                       max(p[1] for p in pts)))


def circle(cx, cy, r):
    return Shape(lambda x, y: math.hypot(x - cx, y - cy) - r, (cx - r, cy - r, cx + r, cy + r))


def capsule(ax, ay, bx, by, r):
    ex, ey = bx - ax, by - ay
    length = ex * ex + ey * ey

    def sdf(x, y):
        px, py = x - ax, y - ay
        h = (px * ex + py * ey) / length if length else 0.0
        h = 0.0 if h < 0.0 else 1.0 if h > 1.0 else h
        return math.hypot(px - ex * h, py - ey * h) - r

    return Shape(sdf, (min(ax, bx) - r, min(ay, by) - r, max(ax, bx) + r, max(ay, by) + r))


def box(x0, y0, x1, y1, r=0.0):
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    hx, hy = (x1 - x0) / 2.0 - r, (y1 - y0) / 2.0 - r

    def sdf(x, y):
        qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
        return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r

    return Shape(sdf, (x0, y0, x1, y1))


def ring(cx, cy, r, width):
    half = width / 2.0
    return Shape(lambda x, y: abs(math.hypot(x - cx, y - cy) - r) - half,
                 (cx - r - half, cy - r - half, cx + r + half, cy + r + half))


def arc(cx, cy, r, width, start, span):
    half = width / 2.0
    a0, a1 = math.radians(start), math.radians(start + span)
    e0 = (cx + r * math.sin(a0), cy - r * math.cos(a0))
    e1 = (cx + r * math.sin(a1), cy - r * math.cos(a1))

    def sdf(x, y):
        angle = math.degrees(math.atan2(x - cx, cy - y))
        if (angle - start) % 360.0 <= span:
            return abs(math.hypot(x - cx, y - cy) - r) - half
        return min(math.hypot(x - e0[0], y - e0[1]), math.hypot(x - e1[0], y - e1[1])) - half

    return Shape(sdf, (cx - r - half, cy - r - half, cx + r + half, cy + r + half))


def union(*shapes):
    def sdf(x, y):
        return min(shape.sdf(x, y) for shape in shapes)

    return Shape(sdf, (min(s.box[0] for s in shapes), min(s.box[1] for s in shapes),
                       max(s.box[2] for s in shapes), max(s.box[3] for s in shapes)))


def subtract(shape, cut):
    return Shape(lambda x, y: max(shape.sdf(x, y), -cut.sdf(x, y)), shape.box)


def rotate(shape, degrees, cx=16.0, cy=16.0):
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))

    def sdf(x, y):
        dx, dy = x - cx, y - cy
        return shape.sdf(cx + dx * c + dy * s, cy - dx * s + dy * c)

    x0, y0, x1, y1 = shape.box
    corners = [(cx + (px - cx) * c - (py - cy) * s, cy + (px - cx) * s + (py - cy) * c)
               for px, py in ((x0, y0), (x1, y0), (x0, y1), (x1, y1))]
    return Shape(sdf, (min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners),
                       max(p[1] for p in corners)))


def translate(shape, dx, dy):
    x0, y0, x1, y1 = shape.box
    return Shape(lambda x, y: shape.sdf(x - dx, y - dy), (x0 + dx, y0 + dy, x1 + dx, y1 + dy))


def scaled(points, factor, ox, oy):
    return [(ox + x * factor, oy + y * factor) for x, y in points]


ARROW = polygon(scaled([(0, 0), (11.2, 11.2), (6.6, 11.2), (9.6, 17.6), (7.0, 18.8), (4.0, 12.4), (0, 16.0)],
                       0.92, 2.0, 2.0))


def double_arrow(length, head, half_head, half_shaft):
    left, right = 16.0 - length / 2.0, 16.0 + length / 2.0
    return polygon([(left, 16.0), (left + head, 16.0 - half_head), (left + head, 16.0 - half_shaft),
                    (right - head, 16.0 - half_shaft), (right - head, 16.0 - half_head), (right, 16.0),
                    (right - head, 16.0 + half_head), (right - head, 16.0 + half_shaft),
                    (left + head, 16.0 + half_shaft), (left + head, 16.0 + half_head)])


SIZE_WE = double_arrow(22.0, 6.0, 5.0, 1.1)
MOVE = union(double_arrow(24.0, 5.5, 4.0, 1.1), rotate(double_arrow(24.0, 5.5, 4.0, 1.1), 90))
UP = polygon([(16.0, 3.0), (23.0, 11.0), (18.2, 11.0), (18.2, 27.0), (13.8, 27.0), (13.8, 11.0), (9.0, 11.0)])
IBEAM = union(box(15.0, 7.0, 16.0, 25.0), box(12.0, 6.0, 15.0, 7.0), box(16.0, 6.0, 19.0, 7.0),
              box(12.0, 25.0, 15.0, 26.0), box(16.0, 25.0, 19.0, 26.0))
CROSS = union(box(15.0, 5.0, 16.0, 26.0), box(5.0, 15.0, 26.0, 16.0))
NO = union(ring(16.0, 16.0, 8.4, 3.0), capsule(10.4, 10.4, 21.6, 21.6, 1.5))
QUESTION = union(arc(22.0, 9.6, 3.0, 2.0, 270.0, 240.0), capsule(22.0 + 3.0 * math.sin(math.radians(150.0)),
                                                                   9.6 - 3.0 * math.cos(math.radians(150.0)),
                                                                   22.0, 15.0, 1.0),
                 circle(22.0, 18.4, 1.25))
HAND = union(capsule(11.0, 4.2, 11.0, 15.0, 2.1), capsule(15.3, 13.4, 15.3, 17.0, 2.0),
             capsule(19.5, 14.4, 19.5, 18.0, 1.95), capsule(23.4, 16.2, 23.4, 19.5, 1.75),
             box(9.0, 15.2, 25.15, 27.5, 3.6), capsule(9.8, 22.6, 5.2, 17.6, 2.0))
HAND_LINES = union(capsule(13.25, 14.0, 13.25, 17.6, 0.4), capsule(17.4, 14.6, 17.4, 18.4, 0.4),
                   capsule(21.5, 16.0, 21.5, 19.6, 0.4))
PEN_TIP = (3.6, 28.4)


def pen():
    ux, uy = math.sqrt(0.5), -math.sqrt(0.5)
    nx, ny = math.sqrt(0.5), math.sqrt(0.5)
    bx, by = PEN_TIP[0] + ux * 6.0, PEN_TIP[1] + uy * 6.0
    w, length = 2.6, 18.0
    body = polygon([PEN_TIP, (bx + nx * w, by + ny * w), (bx + nx * w + ux * length, by + ny * w + uy * length),
                    (bx - nx * w + ux * length, by - ny * w + uy * length), (bx - nx * w, by - ny * w)])
    line = capsule(bx + nx * w, by + ny * w, bx - nx * w, by - ny * w, 0.4)
    cap = capsule(bx + nx * w + ux * (length - 4.0), by + ny * w + uy * (length - 4.0),
                  bx - nx * w + ux * (length - 4.0), by - ny * w + uy * (length - 4.0), 0.4)
    return body, union(line, cap)


PEN, PEN_LINES = pen()


def spinner(cx, cy, r, width, frame):
    t = frame / float(FRAMES)
    span = 40.0 + 200.0 * math.sin(math.pi * t) ** 2
    start = 720.0 * t - span / 2.0 + 100.0 * math.sin(math.pi * t) ** 2
    return ring(cx, cy, r, width), arc(cx, cy, r, width - 0.2, start % 360.0, span)


def wait_layers(frame):
    track, head = spinner(16.0, 16.0, 8.0, 3.2, frame)
    return [(track, WHITE, BLACK), (head, BLUE, None)]


def working_layers(frame):
    track, head = spinner(20.6, 21.4, 4.6, 2.6, frame)
    return [(ARROW, WHITE, BLACK), (track, WHITE, BLACK), (head, BLUE, None)]


CURSORS = (
    ("ocr_normal.cur", [(ARROW, WHITE, BLACK)], (1.5, 1.5)),
    ("ocr_ibeam.cur", [(IBEAM, BLACK, WHITE)], (15.5, 15.5)),
    ("ocr_wait.ani", wait_layers, (15.5, 15.5)),
    ("ocr_cross.cur", [(CROSS, BLACK, WHITE)], (15.5, 15.5)),
    ("ocr_up.cur", [(UP, WHITE, BLACK)], (16.0, 2.5)),
    ("ocr_sizenwse.cur", [(rotate(SIZE_WE, 45), WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_sizenesw.cur", [(rotate(SIZE_WE, -45), WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_sizewe.cur", [(SIZE_WE, WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_sizens.cur", [(rotate(SIZE_WE, 90), WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_sizeall.cur", [(MOVE, WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_size.cur", [(MOVE, WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_no.cur", [(NO, WHITE, BLACK)], (15.5, 15.5)),
    ("ocr_appstarting.ani", working_layers, (1.5, 1.5)),
    ("ocr_help.cur", [(ARROW, WHITE, BLACK), (QUESTION, BLACK, WHITE)], (1.5, 1.5)),
    ("ocr_pen.cur", [(PEN, WHITE, BLACK), (PEN_LINES, BLACK, None)], (3.2, 28.8)),
    ("ocr_hand.cur", [(HAND, WHITE, BLACK), (HAND_LINES, BLACK, None)], (11.0, 1.6)),
    ("ocr_icon.cur", [(ARROW, WHITE, BLACK)], (1.5, 1.5)),
    ("ocr_cdautostart.cur", [(ARROW, WHITE, BLACK)], (1.5, 1.5)),
)

COVERAGE = {}


def coverage(shape, size, grow):
    key = (shape, size, grow)
    if key in COVERAGE:
        return COVERAGE[key]
    scale = size / float(CANVAS)
    x0, y0, x1, y1 = shape.box
    pad = grow + 1.0 / scale
    i0, i1 = max(0, int(math.floor((x0 - pad) * scale))), min(size, int(math.ceil((x1 + pad) * scale)))
    j0, j1 = max(0, int(math.floor((y0 - pad) * scale))), min(size, int(math.ceil((y1 + pad) * scale)))
    result = {}
    for j in range(j0, j1):
        y = (j + 0.5) / scale
        for i in range(i0, i1):
            d = (shape.sdf((i + 0.5) / scale, y) - grow) * scale
            if d < 0.5:
                result[j * size + i] = 1.0 if d <= -0.5 else 0.5 - d
    COVERAGE[key] = result
    return result


def box_blur(values, size, radius):
    out = [0.0] * (size * size)
    span = 2 * radius + 1
    for j in range(size):
        row = j * size
        for i in range(size):
            total = 0.0
            for k in range(i - radius, i + radius + 1):
                if 0 <= k < size:
                    total += values[row + k]
            out[row + i] = total / span
    final = [0.0] * (size * size)
    for i in range(size):
        for j in range(size):
            total = 0.0
            for k in range(j - radius, j + radius + 1):
                if 0 <= k < size:
                    total += out[k * size + i]
            final[j * size + i] = total / span
    return final


def composite(pixels, cover, color):
    r, g, b, a = color
    for index, amount in cover.items():
        alpha = a * amount
        if alpha <= 0.0:
            continue
        dst = pixels[index]
        keep = 1.0 - alpha
        pixels[index] = [r * alpha + dst[0] * keep, g * alpha + dst[1] * keep, b * alpha + dst[2] * keep,
                         alpha + dst[3] * keep]


def render_rows(layers, size):
    scale = size / float(CANVAS)
    count = size * size
    silhouette = [0.0] * count
    plan = []
    for shape, fill, line in layers:
        inner = coverage(shape, size, 0.0)
        outer = coverage(shape, size, OUTLINE) if line else inner
        plan.append((inner, fill, outer, line))
        for index, amount in outer.items():
            if amount > silhouette[index]:
                silhouette[index] = amount
    dx, dy = int(round(SHADOW_OFFSET[0] * scale)), int(round(SHADOW_OFFSET[1] * scale))
    shifted = [0.0] * count
    for j in range(size - dy):
        for i in range(size - dx):
            shifted[(j + dy) * size + i + dx] = silhouette[j * size + i]
    radius = max(1, int(round(SHADOW_BLUR * scale)))
    shadow = box_blur(box_blur(shifted, size, radius), size, radius)
    pixels = [[0.0, 0.0, 0.0, value * SHADOW_OPACITY] for value in shadow]
    for inner, fill, outer, line in plan:
        if line:
            composite(pixels, outer, line)
        composite(pixels, inner, fill)
    rows = []
    for j in range(size):
        row = bytearray()
        for i in range(size):
            r, g, b, a = pixels[j * size + i]
            if a <= 0.0:
                row += b"\0\0\0\0"
                continue
            row += bytes((min(255, int(round(r / a * 255))), min(255, int(round(g / a * 255))),
                          min(255, int(round(b / a * 255))), min(255, int(round(a * 255)))))
        rows.append(bytes(row))
    return rows


def render(layers, size):
    return write_png(size, size, render_rows(layers, size))


def write_png(width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def cursor_file(layers, hotspot):
    images = []
    for size in SIZES:
        scale = size / float(CANVAS)
        x = min(size - 1, int(math.floor(hotspot[0] * scale)))
        y = min(size - 1, int(math.floor(hotspot[1] * scale)))
        images.append((size, x, y, render(layers, size)))
    out = struct.pack("<HHH", 0, 2, len(images))
    offset = 6 + 16 * len(images)
    for size, x, y, png in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, x, y, len(png), offset)
        offset += len(png)
    return out + b"".join(png for _, _, _, png in images)


def riff_chunk(kind, body):
    return kind + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def animated_file(frames, hotspot):
    icons = b"".join(riff_chunk(b"icon", cursor_file(frames(frame), hotspot)) for frame in range(FRAMES))
    header = struct.pack("<9I", 36, FRAMES, FRAMES, 0, 0, 0, 0, JIFFIES, 1)
    body = b"ACON" + riff_chunk(b"anih", header) + riff_chunk(b"LIST", b"fram" + icons)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: curgen.py <output folder> [cursor file name]...")
    target, wanted = sys.argv[1], set(sys.argv[2:])
    for name, layers, hotspot in CURSORS:
        if wanted and name not in wanted:
            continue
        data = animated_file(layers, hotspot) if callable(layers) else cursor_file(layers, hotspot)
        open(os.path.join(target, name), "wb").write(data)
        print("%s (%d bytes)" % (name, len(data)))


if __name__ == "__main__":
    main()
