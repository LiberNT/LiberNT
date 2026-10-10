#!/usr/bin/env python3
"""
PROJECT:     LiberNT shell
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Render the notification area status icons on the pixel grid
COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
"""

import math
import struct
import sys
import zlib
from pathlib import Path

SIZES = (16, 18, 20, 22, 24, 26, 28, 31, 32, 35, 40, 44, 48)
HALO = (0x00, 0x00, 0x00, 0x99)
DIM = (0x8C, 0x8C, 0x8C, 0xFF)
WHITE = (0xFF, 0xFF, 0xFF, 0xFF)
RED = (0xE8, 0x11, 0x23, 0xFF)
YELLOW = (0xFF, 0xC8, 0x00, 0xFF)
INK = (0x10, 0x10, 0x10, 0xFF)
BASE = ("white", "dim", "dark")
BADGE = ("red", "yellow")
MARK = ("bwhite", "ink")


def near(v, parity):
    lo = math.floor(v)
    if lo % 2 != parity:
        lo -= 1
    return lo if v - lo <= lo + 2 - v else lo + 2


class Canvas:
    def __init__(self, s):
        self.s = s
        self.m = 1 if s < 18 else 2 if s < 26 else 3 if s < 35 else 4 if s < 44 else 5
        self.w = 1 if s < 26 else 2 if s < 48 else 3
        self.h = 1 if s < 31 else 2
        self.mx = max(self.h, self.m - 1)
        self.yb = s - self.m
        self.H = s - 2 * self.m
        self.k = self.H / 14.0
        self.px = {name: set() for name in BASE + BADGE + MARK}

    def rect(self, name, x0, y0, x1, y1):
        for j in range(max(0, y0), min(self.s, y1)):
            for i in range(max(0, x0), min(self.s, x1)):
                self.px[name].add((i, j))

    def frame(self, name, x0, y0, x1, y1, w):
        self.rect(name, x0, y0, x1, y0 + w)
        self.rect(name, x0, y1 - w, x1, y1)
        self.rect(name, x0, y0 + w, x0 + w, y1 - w)
        self.rect(name, x1 - w, y0 + w, x1, y1 - w)

    def cells(self, name, pts):
        for i, j in pts:
            if 0 <= i < self.s and 0 <= j < self.s:
                self.px[name].add((i, j))

    def knock(self, pts, r):
        cut = grow(set(pts), r, self.s)
        for name in BASE:
            self.px[name] -= cut
        return cut


def grow(pts, r, s):
    out = set()
    for i, j in pts:
        for y in range(j - r, j + r + 1):
            for x in range(i - r, i + r + 1):
                if 0 <= x < s and 0 <= y < s:
                    out.add((x, y))
    return out


def prune(c, least, cut):
    edge = grow(cut, 1, c.s)
    glyph = c.px["white"] | c.px["dim"]
    seen = set()
    for p in sorted(glyph):
        if p in seen:
            continue
        part = {p}
        todo = [p]
        while todo:
            i, j = todo.pop()
            for q in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                if q in glyph and q not in part:
                    part.add(q)
                    todo.append(q)
        seen |= part
        if len(part) < least and part & edge:
            for name in BASE:
                c.px[name] -= part


def compose(c):
    s = c.s
    badge = c.px["red"] | c.px["yellow"]
    if badge:
        xs = [i for i, j in badge]
        ys = [j for i, j in badge]
        box = {(i, j) for i in range(min(xs), max(xs) + 1) for j in range(min(ys), max(ys) + 1)}
        prune(c, 6 * c.w * c.w, c.knock(box, c.h))
    glyph = c.px["white"] | c.px["dim"]
    halo = grow(glyph, c.h, s) | c.px["dark"]
    out = []
    for j in range(s):
        for i in range(s):
            p = (i, j)
            px = (0, 0, 0, 0)
            for name, rgba in (("halo", HALO), ("dim", DIM), ("white", WHITE), ("red", RED),
                               ("yellow", YELLOW), ("bwhite", WHITE), ("ink", INK)):
                if p in (halo if name == "halo" else c.px[name]):
                    px = rgba
            out.append(px)
    return out


def badge_box(c, top):
    bs = 2 * math.floor(3.5 * c.k - 0.5) + 1
    if top:
        return c.mx, c.m, bs
    return c.s - c.mx - bs, c.yb - bs, bs


def cross(cx, cy, n, t):
    pts = []
    for d in range(-n, n + 1):
        for e in range(t):
            pts.append((cx + d + e - (t - 1) // 2, cy + d))
            pts.append((cx + d + e - (t - 1) // 2, cy - d))
    return pts


def badge_x(c, top=False):
    x0, y0, bs = badge_box(c, top)
    cut = round(bs / 4.0)
    for r in range(bs):
        inset = max(0, cut - r, cut - (bs - 1 - r))
        c.rect("red", x0 + inset, y0 + r, x0 + bs - inset, y0 + r + 1)
    c.cells("bwhite", cross(x0 + bs // 2, y0 + bs // 2, (bs - 1) // 4, 1 if bs < 13 else 2))


def badge_warn(c, top=False):
    x0, y0, bs = badge_box(c, top)
    for r in range(bs):
        half = r // 2
        c.rect("yellow", x0 + bs // 2 - half, y0 + r, x0 + bs // 2 + half + 1, y0 + r + 1)
    t = 1 if bs < 15 else 3
    d = 1 if bs < 15 else 2
    ex = x0 + bs // 2 - (t - 1) // 2
    dot = y0 + bs - 1 - d
    c.rect("ink", ex, y0 + round(bs / 3.0), ex + t, dot - d)
    c.rect("ink", ex, dot - d + 1, ex + t, dot + 1)


def battery_shape(c, x0, bw, level):
    w = c.w
    nh = w + 1
    nw = bw // 2
    if nw % 2 != bw % 2:
        nw -= 1
    by0 = c.m + nh
    xn = x0 + (bw - nw) // 2
    c.rect("white", xn, c.m, xn + nw, by0)
    c.frame("white", x0, by0, x0 + bw, c.yb, w)
    c.rect("dark", x0 + w, by0 + w, x0 + bw - w, c.yb - w)
    fx0 = x0 + 2 * w
    fx1 = x0 + bw - 2 * w
    fy1 = c.yb - 2 * w
    rows = round(level * (fy1 - (by0 + 2 * w)))
    if rows:
        c.rect("white", fx0, fy1 - rows, fx1, fy1)


def plug_shape(c, x0, pwid, top):
    pw = max(1, round(pwid / 6.0))
    ph = max(2, round(pwid / 3.0))
    c.rect("white", x0 + pw, top, x0 + 2 * pw, top + ph)
    c.rect("white", x0 + pwid - 2 * pw, top, x0 + pwid - pw, top + ph)
    bt = top + ph
    bh = max(3, round(pwid * 0.8))
    c.rect("white", x0, bt, x0 + pwid, bt + bh)
    th = max(1, round(pwid / 6.0))
    c.rect("white", x0 + pw, bt + bh, x0 + pwid - pw, bt + bh + th)
    cw = max(1, pwid // 3)
    if cw % 2 != pwid % 2:
        cw += 1
    cx0 = x0 + (pwid - cw) // 2
    c.rect("white", cx0, bt + bh + th, cx0 + cw, c.yb)


def battery(level):
    def draw(c):
        bw = near(0.57 * c.H, c.s % 2)
        battery_shape(c, (c.s - bw) // 2, bw, level)
    return draw


def battery_charge(level):
    def draw(c):
        bw = near(0.57 * c.H, 0)
        gap = 2 if c.s < 31 else 3
        pwid = min(near(0.43 * c.H, 0), c.s - 2 * c.mx - bw - gap)
        pwid -= pwid % 2
        x0 = (c.s - (pwid + gap + bw)) // 2
        battery_shape(c, x0 + pwid + gap, bw, level)
        plug_shape(c, x0, pwid, c.m + round(c.H * 0.25))
    return draw


def battery_error(c):
    battery(0)(c)
    badge_x(c)


def power_ac(c):
    pwid = near(0.57 * c.H, c.s % 2)
    plug_shape(c, (c.s - pwid) // 2, pwid, c.m)


BARS = {
    16: (2, 1, 2, 2), 18: (2, 1, 2, 2), 20: (2, 1, 3, 2), 22: (3, 1, 2, 3),
    24: (3, 1, 3, 3), 26: (3, 1, 3, 3), 28: (4, 1, 4, 3), 31: (4, 1, 3, 4),
    32: (4, 2, 4, 4), 35: (4, 2, 4, 4), 40: (5, 2, 4, 5), 44: (5, 2, 5, 5),
    48: (6, 2, 6, 6),
}


def bars(lit, badge=None):
    def draw(c):
        bwid, gap, h0, step = BARS[c.s]
        x0 = (c.s - (5 * bwid + 4 * gap)) // 2
        for n in range(5):
            x = x0 + n * (bwid + gap)
            c.rect("white" if n < lit else "dim", x, c.yb - h0 - step * n, x + bwid, c.yb)
        if badge:
            badge(c, True)
    return draw


def bracket(c, apex, cy, half, steps):
    pts = []
    run = half - steps
    for dy in range(-half, half + 1):
        back = max(0, abs(dy) - run)
        for e in range(c.w):
            pts.append((apex - back - e, cy + dy))
    c.cells("white", pts)


def speaker(waves, mute=False):
    def draw(c):
        w = c.w
        T = near(15 * c.H / 14.0, 1)
        if c.yb - T < c.h:
            T = c.yb - c.h - (c.yb - c.h + 1) % 2
        u = T / 15.0
        f = max(1, round(2 * u))
        lip = max(1, round(2 * u))
        box = T - 2 * f - 2 * lip
        bw = round(7 * u)
        lw = max(2, round(2 * u))
        inner = max(2, round(3 * u))
        half_in = max(2, round(2 * u))
        step_in = max(1, round(u))
        half_out = max(3, round(4 * u))
        step_out = max(2, round(2 * u))
        n = max(2, round(2 * u))
        while True:
            outer = inner + max(2, round(2 * u))
            width = bw + lw + max(outer + w, inner + 1 + n + w)
            if width <= c.s - 2 * c.mx or inner <= 2:
                break
            inner -= 1
        x0 = (c.s - width) // 2
        yt = c.yb - T
        cy = yt + T // 2
        xl = x0 + bw
        xr = xl + lw
        c.rect("white", xl, yt, xr, yt + T)
        c.rect("white", x0, cy - box // 2, xl, cy + box // 2 + 1)
        for i in range(1, f + 1):
            xs = xl - f - 1 + i
            c.rect("white", xs, cy - box // 2 - i, xl, cy - box // 2 - i + 1)
            c.rect("white", xs, cy + box // 2 + i, xl, cy + box // 2 + i + 1)
        if waves >= 1:
            bracket(c, xr + inner + w - 1, cy, half_in, step_in)
        if waves >= 2:
            bracket(c, xr + outer + w - 1, cy, half_out, step_out)
        if mute:
            c.cells("white", cross(xr + inner + 1, cy, n, w))
    return draw


def wired(badge=None):
    def draw(c):
        k = c.k
        w = c.w
        x1 = c.s - c.mx
        sx0 = c.mx + round(2 * k)
        sw = x1 - sx0
        sh = round(10 * k)
        sy0 = c.m
        c.frame("white", sx0, sy0, x1, sy0 + sh, w)
        c.rect("dark", sx0 + w, sy0 + w, x1 - w, sy0 + sh - w)
        if not badge:
            nw = near(2 * k, sw % 2)
            nx = sx0 + (sw - nw) // 2
            c.rect("white", nx, sy0 + sh, nx + nw, c.yb - w)
            bwid = near(8 * k, sw % 2)
            bx = sx0 + (sw - bwid) // 2
            c.rect("white", bx, c.yb - w, bx + bwid, c.yb)
        pw = near(4 * k, 0)
        ph = round(4 * k)
        pt = c.yb - round(3 * k) - ph
        cw = near(2 * k, 0)
        plug = set()
        for i in range(c.mx, c.mx + pw):
            for j in range(pt, pt + ph):
                plug.add((i, j))
        cx0 = c.mx + (pw - cw) // 2
        for i in range(cx0, cx0 + cw):
            for j in range(pt + ph, c.yb):
                plug.add((i, j))
        c.knock(plug, c.h)
        c.cells("white", plug)
        c.px["white"] -= {(i, j) for i in range(cx0, cx0 + cw) for j in range(pt + w, pt + 2 * w)}
        c.rect("dark", cx0, pt + w, cx0 + cw, pt + 2 * w)
        if badge:
            badge(c)
    return draw


def usb(c):
    k = c.k
    w = c.w
    bw = near(10 * k, c.s % 2)
    x0 = (c.s - bw) // 2
    cw = near(8 * k, c.s % 2)
    cx0 = (c.s - cw) // 2
    ch = round(4 * k)
    c.rect("white", cx0, c.m, cx0 + cw, c.m + ch)
    hw = max(1, round(2 * k))
    holes = set()
    for i in list(range(cx0 + w, cx0 + w + hw)) + list(range(cx0 + cw - w - hw, cx0 + cw - w)):
        for j in range(c.m + w, c.m + ch - w):
            holes.add((i, j))
    c.px["white"] -= holes
    c.cells("dark", holes)
    by0 = c.m + ch
    c.frame("white", x0, by0, x0 + bw, c.yb, w)
    c.rect("dark", x0 + w, by0 + w, x0 + bw - w, c.yb - w)


NETSHELL = "dll/shellext/netshell/res/tray/"
STOBJECT = "dll/shellext/stobject/resources/tray/"

ICONS = (
    (NETSHELL + "wired.ico", wired()),
    (NETSHELL + "wired_warn.ico", wired(badge_warn)),
    (NETSHELL + "wired_x.ico", wired(badge_x)),
    (NETSHELL + "wifi_1.ico", bars(5)),
    (NETSHELL + "wifi_2.ico", bars(4)),
    (NETSHELL + "wifi_3.ico", bars(2)),
    (NETSHELL + "wifi_4.ico", bars(1)),
    (NETSHELL + "wifi_warn.ico", bars(5, badge_warn)),
    (NETSHELL + "wifi_x.ico", bars(0, badge_x)),
    (STOBJECT + "battery_0.ico", battery(0)),
    (STOBJECT + "battery_1.ico", battery(0.25)),
    (STOBJECT + "battery_2.ico", battery(0.5)),
    (STOBJECT + "battery_3.ico", battery(0.75)),
    (STOBJECT + "battery_4.ico", battery(1.0)),
    (STOBJECT + "battery_charge_0.ico", battery_charge(0)),
    (STOBJECT + "battery_charge_1.ico", battery_charge(0.25)),
    (STOBJECT + "battery_charge_2.ico", battery_charge(0.5)),
    (STOBJECT + "battery_charge_3.ico", battery_charge(0.75)),
    (STOBJECT + "battery_charge_4.ico", battery_charge(1.0)),
    (STOBJECT + "battery_error.ico", battery_error),
    (STOBJECT + "power_ac.ico", power_ac),
    (STOBJECT + "speaker_0.ico", speaker(0)),
    (STOBJECT + "speaker_1.ico", speaker(1)),
    (STOBJECT + "speaker_2.ico", speaker(2)),
    (STOBJECT + "speaker_mute.ico", speaker(0, True)),
    (STOBJECT + "usb.ico", usb),
)


def render(draw, s):
    c = Canvas(s)
    draw(c)
    for name, pts in c.px.items():
        for i, j in pts:
            if not (c.h <= i < s - c.h and c.h <= j < s - c.h):
                raise ValueError("%s at %d px leaves the frame at %d,%d" % (name, s, i, j))
    return compose(c)


def ico(frames):
    head = struct.pack("<HHH", 0, 1, len(frames))
    dirs = b""
    blobs = b""
    off = 6 + 16 * len(frames)
    for s, px in frames:
        xor = b"".join(bytes((px[j * s + i][2], px[j * s + i][1], px[j * s + i][0], px[j * s + i][3]))
                       for j in reversed(range(s)) for i in range(s))
        stride = ((s + 31) // 32) * 4
        mask = b""
        for j in reversed(range(s)):
            row = bytearray(stride)
            for i in range(s):
                if px[j * s + i][3] == 0:
                    row[i // 8] |= 0x80 >> (i % 8)
            mask += bytes(row)
        bih = struct.pack("<IiiHHIIiiII", 40, s, 2 * s, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
        blob = bih + xor + mask
        dirs += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(blob), off + len(blobs))
        blobs += blob
    return head + dirs + blobs


def png(w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def preview(path, sizes, zoom, only):
    bgs = ((0x20, 0x20, 0x20), (0xF3, 0xF3, 0xF3), (0x1F, 0x4E, 0x8C))
    icons = [e for e in ICONS if not only or any(o in e[0] for o in only.split(","))]
    cell = max(sizes) * zoom + 8
    w = len(sizes) * len(bgs) * cell
    h = len(icons) * cell
    img = [[0x30, 0x30, 0x30] * w for _ in range(h)]
    for r, (name, draw) in enumerate(icons):
        for si, s in enumerate(sizes):
            px = render(draw, s)
            for bi, bg in enumerate(bgs):
                ox = (si * len(bgs) + bi) * cell + 4
                oy = r * cell + 4
                for j in range(s * zoom):
                    row = img[oy + j]
                    for i in range(s * zoom):
                        p = px[(j // zoom) * s + i // zoom]
                        a = p[3] / 255.0
                        for q in range(3):
                            row[(ox + i) * 3 + q] = int(round(p[q] * a + bg[q] * (1 - a)))
    Path(path).write_bytes(png(w, h, img))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--preview":
        sizes = tuple(int(v) for v in sys.argv[3].split(","))
        preview(sys.argv[2], sizes, int(sys.argv[4]), sys.argv[5] if len(sys.argv) > 5 else None)
        return
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3]
    for name, draw in ICONS:
        out = root / name
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(ico([(s, render(draw, s)) for s in SIZES]))


if __name__ == "__main__":
    main()
