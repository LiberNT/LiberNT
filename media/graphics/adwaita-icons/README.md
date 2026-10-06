# Adwaita icons

Unmodified icons from the Adwaita icon theme of the
[GNOME Project](http://www.gnome.org). They replace the Tango icons, the
earlier Shell, Control Panel and Explorer toolbar artwork, and the application,
dialog and status icons across the tree. Both source themes are licensed under the GNU LGPL v3
or the Creative Commons Attribution-Share Alike 3.0 United States License;
attribution is "GNOME Project".

| Directory | Source |
| --- | --- |
| `scalable`, `16x16` | <https://gitlab.gnome.org/GNOME/adwaita-icon-theme>, commit 82d3057; `status/folder-open.svg` is from an earlier release, as the current tree no longer ships it |
| `legacy` | <https://gitlab.gnome.org/GNOME/adwaita-icon-theme-legacy>, commit 700a078 |

The current theme is used wherever it has the icon. The legacy theme supplies
the rest; it ships PNGs only, up to 48 pixels.

`TARGETS` in `render-icons.py` maps each asset to the icon resources it
replaces, and the `Adwaita.txt` file in or beside each resource directory
lists the same mapping. Where Adwaita has no icon for a resource, the nearest
existing one is used as it is; nothing is composed or redrawn. The share and shortcut overlays are
the upstream emblems placed unscaled in the lower left corner of an empty
canvas. Other overlays, small state glyphs, logos and resources without a near
equivalent keep their existing artwork.

Rebuild using Python with Pillow and resvg-py:

```sh
python media/graphics/adwaita-icons/render-icons.py
```

Icons from the current theme hold 16, 20, 24, 32, 40, 48, 64, 96, 128, and
256 pixel frames, each rasterized directly from the SVG, except the 16-pixel
frame, which is the upstream PNG when the theme ships one. Icons from the
legacy theme hold the upstream 16, 24, 32, and 48 pixel PNGs. All frames are
32-bit DIBs with alpha and legacy AND masks.
