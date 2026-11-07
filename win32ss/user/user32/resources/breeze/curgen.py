#!/usr/bin/env python3
# PROJECT:     LiberNT user32
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the system cursors from the Breeze Light SVG cursors at every Windows cursor size
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

CANVAS = 32
SIZES = (32, 40, 48, 56, 64, 72, 80, 96, 128)
FRAMES = 23
JIFFIES = 2
CURSORS = (
    ("ocr_normal.cur", "default"),
    ("ocr_ibeam.cur", "text"),
    ("ocr_wait.ani", "wait"),
    ("ocr_cross.cur", "crosshair"),
    ("ocr_up.cur", "up-arrow"),
    ("ocr_sizenwse.cur", "size_fdiag"),
    ("ocr_sizenesw.cur", "size_bdiag"),
    ("ocr_sizewe.cur", "size_hor"),
    ("ocr_sizens.cur", "size_ver"),
    ("ocr_sizeall.cur", "fleur"),
    ("ocr_size.cur", "fleur"),
    ("ocr_no.cur", "not-allowed"),
    ("ocr_appstarting.ani", "progress"),
    ("ocr_help.cur", "help"),
    ("ocr_pen.cur", "pencil"),
    ("ocr_hand.cur", "pointer"),
    ("ocr_icon.cur", "default"),
    ("ocr_cdautostart.cur", "default"),
)


def multiply(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def parse_transform(text):
    result = (1, 0, 0, 1, 0, 0)
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text or ""):
        v = [float(x) for x in re.split(r"[\s,]+", args.strip()) if x]
        if name == "matrix":
            m = tuple(v)
        elif name == "translate":
            m = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale":
            m = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate":
            a = math.radians(v[0])
            m = (math.cos(a), math.sin(a), -math.sin(a), math.cos(a), 0, 0)
            if len(v) == 3:
                m = multiply(multiply((1, 0, 0, 1, v[1], v[2]), m), (1, 0, 0, 1, -v[1], -v[2]))
        else:
            raise ValueError("unsupported transform %s" % name)
        result = multiply(result, m)
    return result


def hotspot(path):
    root = ET.parse(path).getroot()
    parent = {child: node for node in root.iter() for child in node}
    spot = next(e for e in root.iter() if e.get("id") == "hotspot")
    number = r"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"
    match = re.match(r"\s*[mM]\s*" + number + r"[\s,]*" + number + r"\s*h\s*" + number + r"\s*v\s*" + number +
                     r"\s*h\s*" + number + r"\s*z\s*$", spot.get("d", ""))
    if not match or float(match.group(5)) != -float(match.group(3)):
        raise ValueError("%s: unexpected hotspot shape" % path)
    x, y, w, h = (float(match.group(i)) for i in (1, 2, 3, 4))
    matrix, node = (1, 0, 0, 1, 0, 0), spot
    while node is not None:
        matrix = multiply(parse_transform(node.get("transform")), matrix)
        node = parent.get(node)
    corners = [(x + dx, y + dy) for dx in (0, w) for dy in (0, h)]
    points = [(matrix[0] * px + matrix[2] * py + matrix[4], matrix[1] * px + matrix[3] * py + matrix[5])
              for px, py in corners]
    return round(min(p[0] for p in points), 6), round(min(p[1] for p in points), 6)


def read_png(path):
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colortype = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    if depth != 8 or colortype != 6:
        raise ValueError("%s: expected 8-bit RGBA" % path)
    stride = width * 4
    raw = zlib.decompress(idat)
    prev, rows, offset = bytearray(stride), [], 0
    for _ in range(height):
        kind, line = raw[offset], bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, rows


def write_png(width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def render(svg, size, work):
    text = open(svg, encoding="utf-8").read()
    tag = re.search(r"<svg\b[^>]*>", text)
    head = re.sub(r'\b(width|height)="[^"]*"', lambda m: '%s="%d"' % (m.group(1), size), tag.group(0))
    if "viewBox" not in head:
        head = head.replace("<svg", '<svg viewBox="0 0 %d %d"' % (CANVAS, CANVAS), 1)
    scaled = os.path.join(work, "scaled.svg")
    target = os.path.join(work, "scaled.png")
    open(scaled, "w", encoding="utf-8").write(text[:tag.start()] + head + text[tag.end():])
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", target, scaled], check=True)
    else:
        subprocess.run(["sips", "-s", "format", "png", scaled, "--out", target], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    width, height, rows = read_png(target)
    if (width, height) != (size, size):
        raise ValueError("%s rendered at %dx%d instead of %d" % (svg, width, height, size))
    return write_png(width, height, rows)


def cursor_file(svg, work):
    hx, hy = hotspot(svg)
    images = [(size, int(math.floor(hx * size / CANVAS)), int(math.floor(hy * size / CANVAS)), render(svg, size, work))
              for size in SIZES]
    out = struct.pack("<HHH", 0, 2, len(images))
    offset = 6 + 16 * len(images)
    for size, x, y, png in images:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, x, y, len(png), offset)
        offset += len(png)
    return out + b"".join(png for _, _, _, png in images)


def riff_chunk(kind, body):
    return kind + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def animated_file(paths, work):
    frames = b"".join(riff_chunk(b"icon", cursor_file(path, work)) for path in paths)
    header = struct.pack("<9I", 36, len(paths), len(paths), 0, 0, 0, 0, JIFFIES, 1)
    body = b"ACON" + riff_chunk(b"anih", header) + riff_chunk(b"LIST", b"fram" + frames)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: curgen.py <Breeze Light svg folder> <output folder>")
    source, target = sys.argv[1], sys.argv[2]
    work = tempfile.mkdtemp()
    try:
        for name, breeze in CURSORS:
            if name.endswith(".ani"):
                data = animated_file([os.path.join(source, "%s-%02d.svg" % (breeze, i + 1)) for i in range(FRAMES)], work)
            else:
                data = cursor_file(os.path.join(source, breeze + ".svg"), work)
            open(os.path.join(target, name), "wb").write(data)
            print("%s <- %s (%d bytes)" % (name, breeze, len(data)))
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
