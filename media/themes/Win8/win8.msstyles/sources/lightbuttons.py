import collections
import os
import struct
import sys

DARK_BASE = (32, 43)
LIGHT_BASE = (238, 243)
STROKE = 67
OUTLINE = 107
GLYPH_EDGE = 128
TOP = 14
GLOSS = 42
SCALE = 123
KEYS = ((255, 0, 255), (0, 0, 0))

BUTTONS = (
    ("CLOSEBUTTON", 8, "alpha", ()),
    ("MINBUTTON", 8, "alpha", ()),
    ("MAXBUTTON", 8, "alpha", ()),
    ("RESTOREBUTTON", 8, "alpha", ()),
    ("SMALLCLOSEBUTTON", 8, "flat", ()),
    ("HELPBUTTON", 8, "flat", ()),
    ("MDICAPTIONBUTTON", 6, "flat", ()),
    ("FLUENT_SMALLCLOSEGLYPH12", 8, "glyph", (1, 2, 5, 6)),
    ("HELPGLYPH9", 8, "glyph", ()),
    ("MDIGLYPHCLOSE", 4, "glyph", ()),
    ("MDIGLYPHMINIMIZE", 4, "glyph", ()),
    ("MDIGLYPHRESTORE", 4, "glyph", ()),
)


def red(r, g, b):
    return r >= g + 40


def luma(r, g, b):
    return (r * 299 + g * 587 + b * 114) // 1000


class Bitmap:
    def __init__(self, path):
        self.data = bytearray(open(path, "rb").read())
        self.offset, = struct.unpack_from("<I", self.data, 10)
        self.width, height, _, bpp = struct.unpack_from("<iiHH", self.data, 18)
        self.rows = abs(height)
        self.top_down = height < 0
        self.step = bpp // 8
        self.stride = (self.width * self.step + 3) & ~3

    def index(self, x, y):
        row = y if self.top_down else self.rows - 1 - y
        return self.offset + row * self.stride + x * self.step

    def get(self, x, y):
        q = self.index(x, y)
        b, g, r = self.data[q], self.data[q + 1], self.data[q + 2]
        a = self.data[q + 3] if self.step == 4 else 255
        return r, g, b, a

    def set(self, x, y, r, g, b, a=255):
        q = self.index(x, y)
        self.data[q], self.data[q + 1], self.data[q + 2] = b, g, r
        if self.step == 4:
            self.data[q + 3] = a


def neighbours(x, y):
    return ((x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy)


def geometry(bmp, cells):
    height = bmp.rows // cells
    reds = [any(red(*bmp.get(x, c * height + y)[:3]) for y in range(height) for x in range(bmp.width)) for c in range(cells)]
    y0 = reds.index(False) * height
    dark = {(x, y) for y in range(2, height) for x in range(bmp.width)
            if 0 < bmp.get(x, y0 + y)[3] < 255 and luma(*bmp.get(x, y0 + y)[:3]) < 0x60}
    outline = set()
    for start in dark:
        if start in outline:
            continue
        group, stack = {start}, [start]
        while stack:
            for q in neighbours(*stack.pop()):
                if q in dark and q not in group:
                    group.add(q)
                    stack.append(q)
        if len(group) >= 8:
            outline |= group
    seed = (bmp.width // 2, height // 2)
    inside, stack = {seed}, [seed]
    while stack:
        x, y = stack.pop()
        for q in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if (0 <= q[0] < bmp.width and 2 <= q[1] < height and q not in inside and q not in outline
                    and bmp.get(q[0], y0 + q[1])[3]):
                inside.add(q)
                stack.append(q)
    bevel = {(x, y) for x, y in inside
             if (x - 1, y) not in inside or (x + 1, y) not in inside or (x, y + 1) in outline}
    return height, reds, outline, inside, bevel


def lift(p, level):
    r, g, b, a = p
    delta = (luma(r, g, b) - DARK_BASE[level]) * a // 255
    key = LIGHT_BASE[level]
    v = max(0, min(255, key + TOP + (delta - GLOSS) * SCALE // 100))
    if v >= key:
        return (255, 255, 255, (v - key) * 255 // (255 - key))
    return (0, 0, 0, (key - v) * 255 // key)


def convert_alpha(bmp, cells):
    height, reds, outline, inside, bevel = geometry(bmp, cells)
    for cell in range(cells):
        y0 = cell * height
        level = cell * 2 // cells
        px = {(x, y): bmp.get(x, y0 + y) for y in range(height) for x in range(bmp.width)}
        body = {q for q in inside if px[q][3] == 255 and not red(*px[q][:3]) and luma(*px[q][:3]) >= GLYPH_EDGE}
        if body:
            gx = range(min(x for x, y in body) - 1, max(x for x, y in body) + 2)
            gy = range(min(y for x, y in body) - 1, max(y for x, y in body) + 2)
        else:
            gx = gy = range(0)
        glyph = {(x, y) for x, y in inside if x in gx and y in gy}
        faces = {}
        for y in range(2, height):
            row = collections.Counter(px[x, y] for x in range(bmp.width)
                                      if (x, y) in inside and (x, y) not in bevel and (x, y) not in glyph)
            faces[y] = row.most_common(1)[0][0] if row else faces.get(y - 1)
        for (x, y), p in px.items():
            if not p[3]:
                continue
            q = (x, max(y, 2))
            if q in outline:
                out = (0, 0, 0, min(p[3], OUTLINE) * STROKE // OUTLINE)
            elif q not in inside:
                out = (0, 0, 0, 0)
            elif reds[cell]:
                out = px[x, max(y, 1)]
            elif q in glyph and q[1] == y and p != faces[y]:
                out = p
            elif q in glyph or q[1] != y:
                out = lift(faces[q[1]], level)
            else:
                out = lift(p, level)
            bmp.set(x, y0 + y, *out)


def convert_flat(bmp, cells):
    height = bmp.rows // cells
    for y in range(bmp.rows):
        level = 0 if y // height < cells // 2 else 1
        for x in range(bmp.width):
            r, g, b, a = bmp.get(x, y)
            if red(r, g, b):
                continue
            v = max(0, min(255, LIGHT_BASE[level] - ((r * 299 + g * 587 + b * 114 + 500) // 1000 - DARK_BASE[level])))
            bmp.set(x, y, v, v, v, a)


def convert_glyph(bmp, cells, keep):
    height = bmp.rows // cells
    for y in range(bmp.rows):
        if y // height in keep:
            continue
        for x in range(bmp.width):
            r, g, b, a = bmp.get(x, y)
            if (r, g, b) not in KEYS:
                bmp.set(x, y, 255 - r, 255 - g, 255 - b, a)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for name, cells, kind, keep in BUTTONS:
        bmp = Bitmap(os.path.join(folder, "NORMAL_%s.bmp" % name))
        if kind == "alpha":
            convert_alpha(bmp, cells)
        elif kind == "flat":
            convert_flat(bmp, cells)
        else:
            convert_glyph(bmp, cells, keep)
        open(os.path.join(folder, "LIGHT_%s.bmp" % name), "wb").write(bytes(bmp.data))


if __name__ == "__main__":
    main()
