#!/usr/bin/env python3
"""
PROJECT:     LiberNT imageres.dll
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Render the imageres.dll icons from Fluent UI System Icons
COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>

Requires Pillow, NumPy and resvg-py. Each frame is rasterized from the Fluent
SVG of the frame size, or of the next larger size the icon has, or else of
its largest size. The SVGs in fluent/ are unmodified.
"""

import io
from pathlib import Path
import struct

import numpy as np
from PIL import Image
import resvg_py

SIZES = (16, 20, 24, 32, 40, 48, 64)
IMAGERES = "dll/win32/imageres/res/"
TARGETS = (
    ("document", (2,)),
    ("folder", (3,)),
    ("plug_disconnected", (31,)),
    ("shield_checkmark", (106,)),
    ("settings", (114,)),
    ("folder_zip", (174,)),
    ("shield", (1028,)),
)


def write_frames(frames, path):
    """Write RGBA frames as 32-bit DIBs with alpha and legacy AND transparency masks."""
    entries = []
    for frame in frames:
        size = frame.width
        rgba = np.array(frame)
        rgba[rgba[:, :, 3] == 0, :3] = 0
        pixels = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
        mask_stride = ((size + 31) // 32) * 4
        mask = bytearray(mask_stride * size)
        for y, row in enumerate(rgba[::-1, :, 3]):
            for x, alpha in enumerate(row):
                if alpha < 128:
                    mask[y * mask_stride + x // 8] |= 0x80 >> (x % 8)
        header = struct.pack("<IiiHHIIiiII", 40, size, size * 2,
                             1, 32, 0, len(pixels), 0, 0, 0, 0)
        entries.append((size, header + pixels + mask))

    offset = 6 + 16 * len(entries)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    for size, data in entries:
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256,
                                     0, 0, 1, 32, len(data), offset))
        offset += len(data)
    path.write_bytes(directory + b"".join(data for _, data in entries))


def pick(fluent, name, size):
    sizes = sorted(int(svg.stem.split("_")[-2]) for svg in fluent.glob("ic_fluent_%s_*_regular.svg" % name)
                   if svg.stem[len("ic_fluent_%s_" % name):].split("_")[0].isdigit())
    larger = [s for s in sizes if s >= size]
    return fluent / ("ic_fluent_%s_%d_regular.svg" % (name, larger[0] if larger else sizes[-1]))


def render(svg, size):
    png = bytes(resvg_py.svg_to_bytes(svg_path=str(svg), width=size, height=size))
    with Image.open(io.BytesIO(png)) as image:
        return image.convert("RGBA")


def main():
    assets = Path(__file__).resolve().parent
    root = assets.parents[2]
    fluent = assets / "fluent"
    for name, ids in TARGETS:
        frames = [render(pick(fluent, name, size), size) for size in SIZES]
        for icon_id in ids:
            write_frames(frames, root / IMAGERES / ("%d.ico" % icon_id))


if __name__ == "__main__":
    main()
