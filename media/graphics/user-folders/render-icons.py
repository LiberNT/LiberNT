#!/usr/bin/env python3
"""Render the Papirus icons in papirus/, the Fluent icons in fluent/, the
Adwaita icons in adwaita/, the MoreWaita icons in morewaita/ and the Oxygen
icons in oxygen/ as Windows icon resources.

Requires Pillow, NumPy and resvg-py. Each frame is rasterized from the
Papirus design size nearest above it. BADGED icons draw a Papirus emblem,
unmodified, over the bottom-right quarter of another Papirus icon; ON_SCREEN
icons draw the stroked lines of another Papirus icon, without its filled
background tile, centered on the screen of the Papirus computer at the screen
height. Oxygen frames use the Oxygen PNG of the frame size, or the scalable
Oxygen SVG where Oxygen has no PNG of that size.
"""

import gzip
import io
import json
import re
from pathlib import Path
import struct

import numpy as np
from PIL import Image
import resvg_py


SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
ICONS = (
    ("folder-yellow", ("dll/win32/shell32/res/icons/4.ico",)),
    ("folder-yellow-open", ("dll/win32/shell32/res/icons/5.ico",)),
    ("drive-harddisk", ("dll/win32/shell32/res/icons/9.ico",)),
    ("computer", ("dll/win32/shell32/res/icons/16.ico",)),
    ("network-workgroup", ("dll/win32/shell32/res/icons/18.ico",)),
    ("user-trash", ("dll/win32/shell32/res/icons/32.ico",)),
    ("user-trash-full", ("dll/win32/shell32/res/icons/33.ico",)),
    ("user-teal-home", ("dll/win32/shell32/res/icons/35.ico",)),
    ("folder-blue-development", ("dll/win32/shell32/res/icons/137.ico",)),
    ("text-x-readme", ("dll/win32/shell32/res/icons/152.ico",)),
    ("folder-blue-documents", ("dll/win32/shell32/res/icons/235.ico",)),
    ("folder-cyan-pictures", ("dll/win32/shell32/res/icons/236.ico",)),
    ("folder-deeporange-music", ("dll/win32/shell32/res/icons/237.ico",)),
    ("folder-violet-videos", ("dll/win32/shell32/res/icons/238.ico",)),
    ("web-browser", ("dll/win32/shell32/res/icons/512.ico", "base/applications/iexplore/iexplore.ico")),
    ("folder-green-download", ("dll/win32/shell32/res/icons/downloads.ico",)),
    ("folder-blue-code", ("dll/shellext/ntobjshex/resources/1.ico",)),
    ("folder-blue-private", ("dll/shellext/ntobjshex/resources/7.ico",)),
    ("system-software-install", ("base/applications/rapps/res/main.ico", "base/applications/cmdutils/lntget/res/lntget.ico")),
    ("utilities-terminal", ("base/shell/cmd/res/terminal.ico",)),
    ("preferences-system-windows", ("base/applications/dwmsettings/res/dwm-settings.ico",)),
    ("yast-vm-install", ("base/setup/libernt/res/libernt.ico",)),
)
SCREENS = {
    "computer": {16: (2, 3, 14, 11), 22: (1, 2, 21, 16), 24: (2, 3, 22, 17), 32: (3, 5, 29, 22), 48: (4, 6, 44, 34), 64: (6, 9, 58, 44)},
}
ON_SCREEN = (
    ("computer", "com.github.stsdc.monitor", ("base/applications/taskmgr/res/taskmgr.ico", "base/applications/taskmgr11/res/taskmgr11.ico")),
)
FLUENT = (
    ("input-mouse", ("dll/win32/setupapi/res/2.ico",)),
    ("input-keyboard", ("dll/win32/setupapi/res/3.ico",)),
    ("printer", ("dll/win32/setupapi/res/4.ico",)),
    ("network-card", ("dll/win32/setupapi/res/5.ico",)),
    ("disk-check", ("dll/win32/setupapi/res/9.ico",)),
    ("network-server", ("dll/win32/setupapi/res/12.ico",)),
    ("unknown", ("dll/win32/setupapi/res/18.ico",)),
    ("media-flash-memory-stick", ("dll/win32/setupapi/res/20.ico",)),
    ("drive-removable-media", ("dll/win32/setupapi/res/21.ico",)),
    ("input-touchscreen", ("dll/win32/setupapi/res/24.ico",)),
    ("computer", ("dll/win32/setupapi/res/27.ico",)),
    ("cpu", ("dll/win32/setupapi/res/28.ico",)),
    ("media-floppy", ("dll/win32/setupapi/res/34.ico", "dll/win32/setupapi/res/50.ico", "dll/win32/setupapi/res/105.ico")),
    ("desktop", ("dll/win32/setupapi/res/35.ico",)),
    ("media-flash", ("dll/win32/setupapi/res/39.ico",)),
    ("drive-optical", ("dll/win32/setupapi/res/51.ico",)),
    ("drive-harddisk", ("dll/win32/setupapi/res/53.ico",)),
)
ADWAITA = (
    ("computer", ("dll/win32/setupapi/res/13.ico",)),
)
MOREWAITA = (
    ("device_pci", ("dll/win32/setupapi/res/1.ico",)),
)
OXYGEN = (
    ("computer", ("dll/win32/devmgr/resources/computer.ico",)),
)
OXYGEN_BITMAPS = (
    ("computer", 16, "dll/win32/devmgr/resources/root.bmp"),
)
FLUENT_STRIPS = (
    (("application-info", "preferences-system-search", "emblem-checked", "emblem-remove", "emblem-downloads", "user-trash"),
     16, "dll/win32/devmgr/resources/toolbar.bmp"),
)
BADGED = (
    ("computer", "emblem-system", ("base/applications/mscutils/devmgmt/devmgmt.ico",)),
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


def write_ico(image, path, sizes=SIZES):
    """Write an RGBA image resized to each frame size as an ICO."""
    write_frames([image.resize((size, size), Image.Resampling.LANCZOS) for size in sizes], path)


def write_bitmap(image, path):
    """Write an RGBA image as a bottom-up 32-bit BI_RGB bitmap keeping its alpha."""
    rgba = np.array(image)
    pixels = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
    info = struct.pack("<IiiHHIIiiII", 40, image.width, image.height, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    header = struct.pack("<2sIHHI", b"BM", 14 + len(info) + len(pixels), 0, 0, 14 + len(info))
    path.write_bytes(header + info + pixels)


def render(svg, size, strokes_only=False):
    text = gzip.decompress(svg.read_bytes()).decode() if svg.suffix == ".svgz" else svg.read_text()
    if strokes_only:
        text = re.sub(r"<(?:path|rect)\b(?![^>]*stroke:)[^>]*/>", "", text)
    png = bytes(resvg_py.svg_to_bytes(svg_string=text, width=size, height=size))
    with Image.open(io.BytesIO(png)) as image:
        return image.convert("RGBA")


def main():
    assets = Path(__file__).resolve().parent
    root = assets.parents[2]
    designs = {}
    for source in json.loads((assets / "papirus/sources.json").read_text()):
        svg = assets / "papirus" / source["file"]
        if "currentColor" in svg.read_text():
            continue
        designs.setdefault(source["name"], {})[int(source["size"].split("x")[0])] = svg

    def pick(name, size):
        available = sorted(designs[name])
        return next((d for d in available if d >= size), available[-1])

    def draw(name, size, strokes_only=False):
        return render(designs[name][pick(name, size)], size, strokes_only)

    for name, targets in ICONS:
        frames = [draw(name, size) for size in SIZES]
        for target in targets:
            write_frames(frames, root / target)
            print(f"{name} -> {target}")
    for name, badge, targets in BADGED:
        frames = []
        for size in SIZES:
            frame = draw(name, size)
            corner = max(size // 2, 8)
            frame.alpha_composite(draw(badge, corner), (size - corner, size - corner))
            frames.append(frame)
        for target in targets:
            write_frames(frames, root / target)
            print(f"{name} + {badge} -> {target}")
    for name, glyph, targets in ON_SCREEN:
        frames = []
        for size in SIZES:
            frame = draw(name, size)
            design = pick(name, size)
            left, top, right, bottom = (edge * size / design for edge in SCREENS[name][design])
            extent = max(round(bottom - top), 6)
            frame.alpha_composite(draw(glyph, extent, True),
                                  (round((left + right - extent) / 2), round((top + bottom - extent) / 2)))
            frames.append(frame)
        for target in targets:
            write_frames(frames, root / target)
            print(f"{name} + {glyph} -> {target}")
    fluent = {source["name"]: assets / "fluent" / source["file"]
              for source in json.loads((assets / "fluent/sources.json").read_text())}
    for name, targets in FLUENT:
        frames = [render(fluent[name], size) for size in SIZES]
        for target in targets:
            write_frames(frames, root / target)
            print(f"fluent {name} -> {target}")
    adwaita = {source["name"]: assets / "adwaita" / source["file"]
               for source in json.loads((assets / "adwaita/sources.json").read_text())}
    for name, targets in ADWAITA:
        frames = [render(adwaita[name], size) for size in SIZES]
        for target in targets:
            write_frames(frames, root / target)
            print(f"adwaita {name} -> {target}")
    morewaita = {source["name"]: assets / "morewaita" / source["file"]
                 for source in json.loads((assets / "morewaita/sources.json").read_text())}
    for name, targets in MOREWAITA:
        frames = [render(morewaita[name], size) for size in SIZES]
        for target in targets:
            write_frames(frames, root / target)
            print(f"morewaita {name} -> {target}")
    oxygen = {}
    for source in json.loads((assets / "oxygen/sources.json").read_text()):
        oxygen.setdefault(source["name"], {})[source["size"]] = assets / "oxygen" / source["file"]

    def draw_oxygen(name, size):
        png = oxygen[name].get(f"{size}x{size}")
        if png is None:
            return render(oxygen[name]["scalable"], size)
        with Image.open(png) as image:
            return image.convert("RGBA")

    for name, targets in OXYGEN:
        frames = [draw_oxygen(name, size) for size in SIZES]
        for target in targets:
            write_frames(frames, root / target)
            print(f"oxygen {name} -> {target}")
    for name, size, target in OXYGEN_BITMAPS:
        write_bitmap(draw_oxygen(name, size), root / target)
        print(f"oxygen {name} -> {target}")
    for names, size, target in FLUENT_STRIPS:
        strip = Image.new("RGBA", (size * len(names), size))
        for index, name in enumerate(names):
            strip.alpha_composite(render(fluent[name], size), (index * size, 0))
        write_bitmap(strip, root / target)
        print(f"fluent {', '.join(names)} -> {target}")


if __name__ == "__main__":
    main()
