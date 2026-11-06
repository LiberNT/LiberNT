/*
 * PROJECT:     LiberNT Color Converter DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pixel format conversion interface of the color converter
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#ifndef __COLORCNV_PRIVATE_H
#define __COLORCNV_PRIVATE_H

#define PIXELS_OPAQUE_ALPHA 0x1
#define PIXELS_NEAREST_CHROMA 0x2
#define PIXELS_ROUNDED_MATRIX 0x4

struct pixel_planes
{
    BYTE *data[3];
    LONG stride[3];
};

BOOL pixels_can_convert(const MFVIDEOFORMAT *input, const MFVIDEOFORMAT *output);
DWORD pixels_fill_planes(const MFVIDEOFORMAT *format, BYTE *data, DWORD size, LONG pitch, struct pixel_planes *planes);
void pixels_copy(const MFVIDEOFORMAT *format, const struct pixel_planes *src, const struct pixel_planes *dst);
void pixels_convert(const MFVIDEOFORMAT *input, const struct pixel_planes *src,
        const MFVIDEOFORMAT *output, const struct pixel_planes *dst, DWORD flags);

#endif /* __COLORCNV_PRIVATE_H */
