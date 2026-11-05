/*
 * PROJECT:     LiberNT Color Converter DSP
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     YUV and RGB pixel format conversion
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "mfobjects.h"
#include "mfapi.h"

#include "colorcnv_private.h"

enum pixel_format
{
    PIXEL_NONE,
    PIXEL_NV12,
    PIXEL_I420,
    PIXEL_YV12,
    PIXEL_YUY2,
    PIXEL_UYVY,
    PIXEL_YVYU,
    PIXEL_AYUV,
    PIXEL_RGB32,
    PIXEL_RGB24,
    PIXEL_RGB565,
    PIXEL_RGB555,
};

enum chroma_layout
{
    CHROMA_420,
    CHROMA_422,
    CHROMA_444,
    CHROMA_RGB,
};

struct frame
{
    enum pixel_format format;
    UINT width;
    UINT height;
    const struct pixel_planes *planes;
};

static enum pixel_format get_pixel_format(const GUID *subtype)
{
    switch (subtype->Data1)
    {
    case MAKEFOURCC('N','V','1','2'): return PIXEL_NV12;
    case MAKEFOURCC('I','4','2','0'):
    case MAKEFOURCC('I','Y','U','V'): return PIXEL_I420;
    case MAKEFOURCC('Y','V','1','2'): return PIXEL_YV12;
    case MAKEFOURCC('Y','U','Y','2'): return PIXEL_YUY2;
    case MAKEFOURCC('U','Y','V','Y'): return PIXEL_UYVY;
    case MAKEFOURCC('Y','V','Y','U'): return PIXEL_YVYU;
    case MAKEFOURCC('A','Y','U','V'): return PIXEL_AYUV;
    case 0xe436eb7e: case 22: return PIXEL_RGB32;
    case 0xe436eb7d: case 20: return PIXEL_RGB24;
    case 0xe436eb7b: case 23: return PIXEL_RGB565;
    case 0xe436eb7c: case 24: return PIXEL_RGB555;
    }
    return PIXEL_NONE;
}

static enum chroma_layout get_chroma_layout(enum pixel_format format)
{
    switch (format)
    {
    case PIXEL_NV12: case PIXEL_I420: case PIXEL_YV12: return CHROMA_420;
    case PIXEL_YUY2: case PIXEL_UYVY: case PIXEL_YVYU: return CHROMA_422;
    case PIXEL_AYUV: return CHROMA_444;
    default: return CHROMA_RGB;
    }
}

static UINT get_bytes_per_pixel(enum pixel_format format)
{
    switch (format)
    {
    case PIXEL_YUY2: case PIXEL_UYVY: case PIXEL_YVYU: case PIXEL_RGB565: case PIXEL_RGB555: return 2;
    case PIXEL_RGB24: return 3;
    case PIXEL_AYUV: case PIXEL_RGB32: return 4;
    default: return 1;
    }
}

BOOL pixels_can_convert(const MFVIDEOFORMAT *input, const MFVIDEOFORMAT *output)
{
    enum pixel_format in = get_pixel_format(&input->guidFormat), out = get_pixel_format(&output->guidFormat);

    if (in == PIXEL_NONE || out == PIXEL_NONE)
        return FALSE;
    if ((get_chroma_layout(in) == CHROMA_420 || get_chroma_layout(out) == CHROMA_420) && input->videoInfo.dwHeight < 2)
        return FALSE;
    return TRUE;
}

DWORD pixels_fill_planes(const MFVIDEOFORMAT *format, BYTE *data, DWORD size, LONG pitch, struct pixel_planes *planes)
{
    enum pixel_format pixel_format = get_pixel_format(&format->guidFormat);
    UINT width = format->videoInfo.dwWidth, height = format->videoInfo.dwHeight;
    UINT strides[3] = {0}, rows[3] = {0}, i;
    DWORD natural = 0, needed = 0;
    BYTE *plane;

    switch (get_chroma_layout(pixel_format))
    {
    case CHROMA_420:
        strides[0] = (width + 1) & ~1;
        rows[0] = height;
        rows[1] = height / 2;
        if (pixel_format == PIXEL_NV12)
            strides[1] = strides[0];
        else
        {
            strides[1] = strides[2] = strides[0] / 2;
            rows[2] = rows[1];
        }
        break;
    case CHROMA_422:
    case CHROMA_444:
        strides[0] = width * get_bytes_per_pixel(pixel_format);
        rows[0] = height;
        break;
    case CHROMA_RGB:
        strides[0] = (width * get_bytes_per_pixel(pixel_format) + 3) & ~3;
        rows[0] = height;
        break;
    }

    for (i = 0; i < 3; i++)
        natural += strides[i] * rows[i];

    if (pitch && (UINT)abs(pitch) != strides[0])
    {
        UINT new_strides[3];
        for (i = 0; i < 3; i++)
            new_strides[i] = abs(pitch) * strides[i] / strides[0];
        memcpy(strides, new_strides, sizeof(strides));
    }
    for (i = 0; i < 3; i++)
        needed += strides[i] * rows[i];
    if (needed > size)
        return 0;
    if (!planes)
        return natural;

    memset(planes, 0, sizeof(*planes));
    for (i = 0, plane = data; i < 3; plane += strides[i] * rows[i], i++)
    {
        planes->data[i] = rows[i] ? plane : NULL;
        planes->stride[i] = strides[i];
    }
    if (pixel_format == PIXEL_YV12)
    {
        BYTE *v = planes->data[1];
        planes->data[1] = planes->data[2];
        planes->data[2] = v;
    }
    if (format->videoInfo.VideoFlags & MFVideoFlag_BottomUpLinearRep)
    {
        planes->data[0] += planes->stride[0] * (height - 1);
        planes->stride[0] = -planes->stride[0];
    }
    return natural;
}

void pixels_copy(const MFVIDEOFORMAT *format, const struct pixel_planes *src, const struct pixel_planes *dst)
{
    enum pixel_format pixel_format = get_pixel_format(&format->guidFormat);
    UINT width = format->videoInfo.dwWidth, height = format->videoInfo.dwHeight;
    UINT bytes[3] = {0}, rows[3] = {0}, i, y;

    bytes[0] = width * get_bytes_per_pixel(pixel_format);
    rows[0] = height;
    if (get_chroma_layout(pixel_format) == CHROMA_420)
    {
        bytes[0] = (width + 1) & ~1;
        rows[1] = rows[2] = height / 2;
        bytes[1] = bytes[2] = bytes[0] / 2;
        if (pixel_format == PIXEL_NV12)
        {
            bytes[1] = bytes[0];
            rows[2] = 0;
        }
    }

    for (i = 0; i < 3; i++)
        for (y = 0; y < rows[i]; y++)
            memcpy(dst->data[i] + (INT_PTR)y * dst->stride[i], src->data[i] + (INT_PTR)y * src->stride[i], bytes[i]);
}

static inline BYTE clip_byte(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static inline BYTE *pixel_row(const struct pixel_planes *planes, UINT plane, UINT y)
{
    return planes->data[plane] + (INT_PTR)y * planes->stride[plane];
}

static BYTE get_y(const struct frame *frame, UINT x, UINT y)
{
    BYTE *row = pixel_row(frame->planes, 0, y);

    switch (frame->format)
    {
    case PIXEL_YUY2: case PIXEL_YVYU: return row[2 * x];
    case PIXEL_UYVY: return row[2 * x + 1];
    case PIXEL_AYUV: return row[4 * x + 2];
    default: return row[x];
    }
}

static void set_y(const struct frame *frame, UINT x, UINT y, BYTE value)
{
    BYTE *row = pixel_row(frame->planes, 0, y);

    switch (frame->format)
    {
    case PIXEL_YUY2: case PIXEL_YVYU: row[2 * x] = value; break;
    case PIXEL_UYVY: row[2 * x + 1] = value; break;
    case PIXEL_AYUV: row[4 * x + 2] = value; row[4 * x + 3] = 0xff; break;
    default: row[x] = value; break;
    }
}

static BYTE *chroma_sample(const struct frame *frame, UINT cx, UINT cy, BOOL v)
{
    switch (frame->format)
    {
    case PIXEL_NV12: return pixel_row(frame->planes, 1, cy) + 2 * cx + v;
    case PIXEL_I420: case PIXEL_YV12: return pixel_row(frame->planes, 1 + v, cy) + cx;
    case PIXEL_YUY2: return pixel_row(frame->planes, 0, cy) + 4 * cx + (v ? 3 : 1);
    case PIXEL_UYVY: return pixel_row(frame->planes, 0, cy) + 4 * cx + (v ? 2 : 0);
    case PIXEL_YVYU: return pixel_row(frame->planes, 0, cy) + 4 * cx + (v ? 1 : 3);
    case PIXEL_AYUV: return pixel_row(frame->planes, 0, cy) + 4 * cx + (v ? 0 : 1);
    default: return NULL;
    }
}

static UINT chroma_width(const struct frame *frame)
{
    return get_chroma_layout(frame->format) == CHROMA_444 ? frame->width : (frame->width + 1) / 2;
}

static UINT chroma_height(const struct frame *frame)
{
    return get_chroma_layout(frame->format) == CHROMA_420 ? frame->height / 2 : frame->height;
}

/* per field, chroma 1/4 (top) or 3/4 (bottom) of a field line below luma; NV12 in 8-line strips as on Windows */
static BYTE get_chroma_420(const struct frame *frame, UINT x, UINT y, BOOL v)
{
    UINT strip = frame->format == PIXEL_NV12 ? 8 : frame->height, start, lines, rows, field, pos, j, fraction;
    UINT cx = min(x / 2, chroma_width(frame) - 1), first, second, last;

    if (strip >= frame->height)
        start = 0;
    else if ((start = (y / strip) * strip) > (last = (frame->height / strip - 1) * strip))
        start = last;
    lines = start + strip > frame->height || frame->height - start < 2 * strip ? frame->height - start : strip;
    rows = (lines + 1) / 2;
    y -= start;
    field = y & 1;

    if ((int)(pos = field ? 4 * (y >> 1) - 3 : 4 * (y >> 1) - 1) < 0)
        pos = 0;
    j = pos >> 3;
    fraction = pos & 7;

    if ((first = 2 * j + field) >= rows)
        first = first >= 2 ? first - 2 : rows - 1;
    first = min(start / 2 + first, chroma_height(frame) - 1);
    if (!fraction || (second = 2 * (j + 1) + field) >= rows)
        return *chroma_sample(frame, cx, first, v);
    second = min(start / 2 + second, chroma_height(frame) - 1);
    return ((8 - fraction) * *chroma_sample(frame, cx, first, v) + fraction * *chroma_sample(frame, cx, second, v) + 4) >> 3;
}

static BYTE get_chroma(const struct frame *frame, UINT x, UINT y, BOOL v)
{
    switch (get_chroma_layout(frame->format))
    {
    case CHROMA_420: return get_chroma_420(frame, x, y, v);
    case CHROMA_422: return *chroma_sample(frame, x / 2, y, v);
    default: return *chroma_sample(frame, x, y, v);
    }
}

static void get_420_lines(const struct frame *frame, UINT cy, UINT *near_line, UINT *far_line)
{
    if (cy & 1)
    {
        *near_line = 2 * cy + 1;
        *far_line = 2 * cy - 1;
    }
    else
    {
        *near_line = 2 * cy;
        *far_line = 2 * cy + 2;
    }
    if (*near_line >= frame->height)
        *near_line -= 2;
    if (*far_line >= frame->height)
        *far_line -= 2;
}

static void get_rgb(const struct frame *frame, UINT x, UINT y, BYTE rgbx[4])
{
    BYTE *row = pixel_row(frame->planes, 0, y);
    WORD value;

    switch (frame->format)
    {
    case PIXEL_RGB32:
        memcpy(rgbx, row + 4 * x, 4);
        break;
    case PIXEL_RGB24:
        memcpy(rgbx, row + 3 * x, 3);
        rgbx[3] = 0xff;
        break;
    case PIXEL_RGB565:
        value = row[2 * x] | (row[2 * x + 1] << 8);
        rgbx[0] = (value & 0x1f) << 3;
        rgbx[1] = ((value >> 5) & 0x3f) << 2;
        rgbx[2] = (value >> 11) << 3;
        rgbx[3] = 0xff;
        break;
    case PIXEL_RGB555:
        value = row[2 * x] | (row[2 * x + 1] << 8);
        rgbx[0] = (value & 0x1f) << 3;
        rgbx[1] = ((value >> 5) & 0x1f) << 3;
        rgbx[2] = ((value >> 10) & 0x1f) << 3;
        rgbx[3] = 0xff;
        break;
    default:
        break;
    }
}

static void set_rgb(const struct frame *frame, UINT x, UINT y, const BYTE rgbx[4])
{
    BYTE *row = pixel_row(frame->planes, 0, y);
    WORD value;

    switch (frame->format)
    {
    case PIXEL_RGB32:
        memcpy(row + 4 * x, rgbx, 4);
        break;
    case PIXEL_RGB24:
        memcpy(row + 3 * x, rgbx, 3);
        break;
    case PIXEL_RGB565:
        value = (rgbx[0] >> 3) | ((rgbx[1] >> 2) << 5) | ((rgbx[2] >> 3) << 11);
        row[2 * x] = value & 0xff;
        row[2 * x + 1] = value >> 8;
        break;
    case PIXEL_RGB555:
        value = (rgbx[0] >> 3) | ((rgbx[1] >> 3) << 5) | ((rgbx[2] >> 3) << 10);
        row[2 * x] = value & 0xff;
        row[2 * x + 1] = value >> 8;
        break;
    default:
        break;
    }
}

static void yuv_to_rgb(BYTE y, BYTE u, BYTE v, BYTE rgbx[4])
{
    int luma = (76309 * (y - 16)) >> 16;

    rgbx[0] = clip_byte(luma + ((132201 * (u - 128)) >> 16));
    rgbx[1] = clip_byte(luma - ((25675 * (u - 128)) >> 16) - ((53279 * (v - 128)) >> 16));
    rgbx[2] = clip_byte(luma + ((104597 * (v - 128)) >> 16));
    rgbx[3] = 0;
}

static BYTE rgb_to_y(const BYTE rgbx[4])
{
    return clip_byte(((67371 * rgbx[2] + 132120 * rgbx[1] + 25690 * rgbx[0] + 131024) >> 18) + 16);
}

static int rgb_to_u_raw(const BYTE rgbx[4])
{
    return -9699 * rgbx[2] - 19071 * rgbx[1] + 28770 * rgbx[0];
}

static int rgb_to_v_raw(const BYTE rgbx[4])
{
    return 115082 * rgbx[2] - 96468 * rgbx[1] - 18613 * rgbx[0];
}

static BYTE rgb_to_u(const BYTE rgbx[4])
{
    return clip_byte(((rgb_to_u_raw(rgbx) + 32780) >> 16) + 128);
}

static BYTE rgb_to_v(const BYTE rgbx[4])
{
    return clip_byte(((rgb_to_v_raw(rgbx) + 130830) >> 18) + 128);
}

static BYTE get_source_chroma(const struct frame *frame, UINT x, UINT y, BOOL v)
{
    BYTE rgbx[4];

    if (get_chroma_layout(frame->format) != CHROMA_RGB)
        return get_chroma(frame, x, y, v);
    get_rgb(frame, x, y, rgbx);
    return v ? rgb_to_v(rgbx) : rgb_to_u(rgbx);
}

static BYTE get_downsampled_chroma(const struct frame *src, const struct frame *dst, UINT cx, UINT cy, BOOL v)
{
    UINT x = get_chroma_layout(dst->format) == CHROMA_444 ? cx : 2 * cx, near_line, far_line;
    BYTE near_rgbx[4], far_rgbx[4];
    int value;

    if (get_chroma_layout(dst->format) != CHROMA_420)
        return get_source_chroma(src, x, cy, v);

    get_420_lines(dst, cy, &near_line, &far_line);
    if (get_chroma_layout(src->format) != CHROMA_RGB || dst->format == PIXEL_NV12)
        return (3 * get_source_chroma(src, x, near_line, v) + get_source_chroma(src, x, far_line, v) + 2) >> 2;

    get_rgb(src, x, near_line, near_rgbx);
    get_rgb(src, x, far_line, far_rgbx);
    if (v)
        value = ((3 * rgb_to_v_raw(near_rgbx) + rgb_to_v_raw(far_rgbx) + 4 * 130830) >> 20) + 128;
    else
        value = ((3 * rgb_to_u_raw(near_rgbx) + rgb_to_u_raw(far_rgbx) + 4 * 32780) >> 18) + 128;
    return clip_byte(value);
}

void pixels_convert(const MFVIDEOFORMAT *input, const struct pixel_planes *src_planes,
        const MFVIDEOFORMAT *output, const struct pixel_planes *dst_planes)
{
    struct frame src = {get_pixel_format(&input->guidFormat), input->videoInfo.dwWidth, input->videoInfo.dwHeight, src_planes};
    struct frame dst = {get_pixel_format(&output->guidFormat), output->videoInfo.dwWidth, output->videoInfo.dwHeight, dst_planes};
    enum chroma_layout src_layout = get_chroma_layout(src.format), dst_layout = get_chroma_layout(dst.format);
    UINT width = min(src.width, dst.width), height = min(src.height, dst.height), x, y;
    BYTE rgbx[4];

    if (src.format == dst.format)
    {
        pixels_copy(input, src_planes, dst_planes);
        return;
    }

    if (dst_layout == CHROMA_RGB)
    {
        for (y = 0; y < height; y++)
        {
            for (x = 0; x < width; x++)
            {
                if (src_layout == CHROMA_RGB)
                    get_rgb(&src, x, y, rgbx);
                else
                {
                    yuv_to_rgb(get_y(&src, x, y), get_chroma(&src, x, y, FALSE), get_chroma(&src, x, y, TRUE), rgbx);
                    if (src_layout == CHROMA_422)
                        rgbx[3] = 0xff;
                }
                set_rgb(&dst, x, y, rgbx);
            }
        }
        return;
    }

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            if (src_layout == CHROMA_RGB)
            {
                get_rgb(&src, x, y, rgbx);
                set_y(&dst, x, y, rgb_to_y(rgbx));
            }
            else
                set_y(&dst, x, y, get_y(&src, x, y));
        }
    }

    for (y = 0; y < chroma_height(&dst); y++)
    {
        for (x = 0; x < chroma_width(&dst); x++)
        {
            if (src_layout == dst_layout)
            {
                *chroma_sample(&dst, x, y, FALSE) = *chroma_sample(&src, x, y, FALSE);
                *chroma_sample(&dst, x, y, TRUE) = *chroma_sample(&src, x, y, TRUE);
            }
            else if (dst_layout == CHROMA_420 || src_layout == CHROMA_RGB
                    || (src_layout == CHROMA_444 && dst_layout == CHROMA_422))
            {
                *chroma_sample(&dst, x, y, FALSE) = get_downsampled_chroma(&src, &dst, x, y, FALSE);
                *chroma_sample(&dst, x, y, TRUE) = get_downsampled_chroma(&src, &dst, x, y, TRUE);
            }
            else
            {
                UINT sx = dst_layout == CHROMA_444 ? x : 2 * x;
                *chroma_sample(&dst, x, y, FALSE) = get_chroma(&src, sx, y, FALSE);
                *chroma_sample(&dst, x, y, TRUE) = get_chroma(&src, sx, y, TRUE);
            }
        }
    }
}
