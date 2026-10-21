import os
import struct
import sys

NAMES = (
    "SCROLLARROWS",
    "SCROLLARROWGLYPHS",
    "SCROLLARROWGLYPHSSMALL",
    "SCROLLSHAFTHORIZONTAL",
    "SCROLLSHAFTVERTICAL",
    "SCROLLTHUMBGRIPPERHORIZONTAL",
    "SCROLLTHUMBGRIPPERVERTICAL",
    "SCROLLTHUMBHORIZONTAL",
    "SCROLLTHUMBVERTICAL",
    "RESIZEGRIP2",
)
MIRROR = {32: 238, 43: 243, 255: 0, 160: 95}


def mirror(v):
    return MIRROR.get(v, max(0, min(255, 270 - v)))


def convert(src, dst):
    data = bytearray(open(src, "rb").read())
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bpp = struct.unpack_from("<iiHH", data, 18)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    for y in range(abs(height)):
        for x in range(width):
            q = offset + y * stride + x * step
            b, g, r = data[q], data[q + 1], data[q + 2]
            if b == g == r:
                data[q] = data[q + 1] = data[q + 2] = mirror(b)
    open(dst, "wb").write(data)


def main():
    bitmaps = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bitmaps")
    for name in NAMES:
        convert(os.path.join(bitmaps, "NORMAL_" + name + ".bmp"), os.path.join(bitmaps, "LIGHT_" + name + ".bmp"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
