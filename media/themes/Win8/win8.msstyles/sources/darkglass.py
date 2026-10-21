import os
import struct
import sys

CAPTION_TINT = (-4, 0, 8)
TASKBAR_TINT = (12, 16, 24)
CAPTION = ("HELPBUTTON", "MDICAPTIONBUTTON", "SMALLCLOSEBUTTON")
TASKBAR = (
    "TASKBARBACKGROUND",
    "TASKBARBACKGROUNDVERTICAL",
    "TASKBARTRAY",
    "TASKBARTRAYVERTICAL",
    "TASKBARSIZINGBARBOTTOM",
    "TASKBARSIZINGBARLEFT",
    "TASKBARSIZINGBARRIGHT",
    "TASKBARSIZINGBARTOP",
    "STARTBUTTON",
)


def tint(path, shift):
    data = bytearray(open(path, "rb").read())
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bpp = struct.unpack_from("<iiHH", data, 18)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    for y in range(abs(height)):
        for x in range(width):
            q = offset + y * stride + x * step
            b, g, r = data[q], data[q + 1], data[q + 2]
            if b == g == r and 1 <= r <= 127:
                data[q + 2] = max(0, min(255, r + shift[0]))
                data[q + 1] = max(0, min(255, g + shift[1]))
                data[q] = max(0, min(255, b + shift[2]))
    open(path, "wb").write(bytes(data))


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for name in CAPTION:
        tint(os.path.join(folder, "NORMAL_%s.bmp" % name), CAPTION_TINT)
    for name in TASKBAR:
        tint(os.path.join(folder, "NORMAL_%s.bmp" % name), TASKBAR_TINT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
