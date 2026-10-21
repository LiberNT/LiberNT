import os
import sys
from PIL import Image

PALETTES = {
    "NORMAL": ({0: (32, 32, 32), 1: (43, 43, 43)}, {0: (87, 87, 87), 1: (95, 95, 95)}, (0, 0, 0)),
    "LIGHT": ({0: (238, 238, 238), 1: (243, 243, 243)}, {0: (255, 255, 255), 1: (255, 255, 255)}, (160, 160, 160)),
}


def frame(w, h, states, left, right, top, bottom, palette):
    key, light_line, dark_line = palette
    im = Image.new("RGB", (w, h * states))
    px = im.load()
    for s in range(states):
        y0 = s * h
        for y in range(h):
            for x in range(w):
                dark = (left and x == 0) or (right and x == w - 1) or (top and y == 0) or (bottom and y == h - 1)
                light = (left and x == 1) or (right and x == w - 2) or (top and y == 1) or (bottom and y == h - 2)
                px[x, y0 + y] = dark_line if dark else light_line[s] if light else key[s]
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for prefix, palette in PALETTES.items():
        def save(im, name):
            im.save(os.path.join(out, prefix + "_" + name + ".bmp"))

        save(frame(8, 30, 2, True, True, True, False, palette), "FRAMECAPTION")
        save(frame(8, 22, 2, True, True, True, False, palette), "SMALLFRAMECAPTION")
        save(frame(8, 30, 2, True, True, True, True, palette), "FRAMECAPTIONMIN")
        save(frame(6, 8, 2, True, False, False, False, palette), "FRAMELEFT")
        save(frame(6, 8, 2, False, True, False, False, palette), "FRAMERIGHT")
        save(frame(8, 6, 2, True, True, False, True, palette), "FRAMEBOTTOM")
        save(frame(40, 20, 2, False, False, False, False, palette), "FRAMECAPTIONSIZING")
        save(frame(40, 20, 2, False, False, False, False, palette), "SMALLFRAMECAPTIONSIZING")
        save(frame(8, 30, 2, False, False, False, False, palette), "FRAMEMAXIMIZED")


if __name__ == "__main__":
    main()
