#!/usr/bin/env python3
# PROJECT:     LiberNT uxtheme
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the caption button glyph icons from rasterized Fluent UI System Icons
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

import os
import struct
import sys
import zlib

GLYPHS = (
    ("close", "dismiss", "regular"),
    ("max", "square", "regular"),
    ("restore", "square_multiple", "regular"),
    ("min", "subtract", "filled"),
    ("help", "question", "regular"),
)
SIZES = (12, 16, 20, 24, 28)
SHADES = (
    ("w", (255, 255, 255)),
    ("i", (160, 160, 160)),
    ("d", (105, 105, 105)),
    ("l", (0, 0, 0)),
    ("li", (146, 146, 146)),
    ("ld", (224, 224, 224)),
)


def read_alpha(path):
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
    if depth != 8 or colortype not in (4, 6):
        raise ValueError("%s: expected 8-bit gray+alpha or RGBA" % path)
    step = 4 if colortype == 6 else 2
    stride = width * step
    raw = zlib.decompress(idat)
    prev, rows, offset = bytearray(stride), [], 0
    for _ in range(height):
        kind, line = raw[offset], bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for x in range(stride):
            a = line[x - step] if x >= step else 0
            b = prev[x]
            c = prev[x - step] if x >= step else 0
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
        rows.append([line[x * step + step - 1] for x in range(width)])
        prev = line
    if width != height:
        raise ValueError("%s: not square" % path)
    return width, rows


def dib(size, rows, color):
    r, g, b = color
    pixels = bytearray()
    for row in reversed(rows):
        for a in row:
            pixels += bytes((b, g, r, a))
    mask_stride = ((size + 31) // 32) * 4
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels) + mask_stride * size, 0, 0, 0, 0)
    return header + bytes(pixels) + bytes(mask_stride * size)


def write_ico(path, frames):
    out = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    blobs = []
    for size, blob in frames:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        blobs.append(blob)
    open(path, "wb").write(out + b"".join(blobs))


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: capglyphs.py <rasterized png folder> <output folder>")
    source, target = sys.argv[1], sys.argv[2]
    for name, icon, style in GLYPHS:
        masks = []
        for size in SIZES:
            path = os.path.join(source, "ic_fluent_%s_%d_%s.png" % (icon, size, style))
            if os.path.exists(path):
                width, rows = read_alpha(path)
                if width != size:
                    sys.exit("%s: rasterized at %d pixels" % (path, width))
                masks.append((size, rows))
        for suffix, color in SHADES:
            write_ico(os.path.join(target, "cap_%s_%s.ico" % (name, suffix)),
                      [(size, dib(size, rows, color)) for size, rows in masks])


if __name__ == "__main__":
    main()
