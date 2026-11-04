/*
 * PROJECT:     LiberNT DirectShow
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Native MPEG-1 video decoder filter
 * COPYRIGHT:   Copyright 2022 Anton Baskanov
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "quartz_private.h"

#include "mpeg1video.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

static const struct
{
    const GUID *subtype;
    WORD bit_count;
    DWORD compression;
}
output_types[] =
{
    {&MEDIASUBTYPE_YV12, 12, MAKEFOURCC('Y','V','1','2')},
    {&MEDIASUBTYPE_Y41P, 12, MAKEFOURCC('Y','4','1','P')},
    {&MEDIASUBTYPE_YUY2, 16, MAKEFOURCC('Y','U','Y','2')},
    {&MEDIASUBTYPE_UYVY, 16, MAKEFOURCC('U','Y','V','Y')},
    {&MEDIASUBTYPE_RGB24, 24, BI_RGB},
    {&MEDIASUBTYPE_RGB32, 32, BI_RGB},
    {&MEDIASUBTYPE_RGB565, 16, BI_BITFIELDS},
    {&MEDIASUBTYPE_RGB555, 16, BI_RGB},
    {&MEDIASUBTYPE_RGB8, 8, BI_RGB},
};

struct mpeg_video_codec
{
    struct strmbase_filter filter;

    struct strmbase_sink sink;
    struct strmbase_source source;
    struct strmbase_passthrough passthrough;

    IQualityControl sink_IQualityControl_iface;
    IQualityControl source_IQualityControl_iface;
    IQualityControl *qc_sink;

    struct mpeg1video *decoder;
    REFERENCE_TIME next_time;
    BOOL discontinuity;

    AM_MEDIA_TYPE output_mt;
    RGBQUAD palette[256];
    unsigned int palette_size;
    BYTE *inverse_palette;
};

static inline struct mpeg_video_codec *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_video_codec, filter);
}

static struct strmbase_pin *mpeg_video_codec_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(iface);
    if (index == 0)
        return &filter->sink.pin;
    if (index == 1)
        return &filter->source.pin;
    return NULL;
}

static void mpeg_video_codec_destroy(struct strmbase_filter *iface)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(iface);

    strmbase_passthrough_cleanup(&filter->passthrough);
    strmbase_source_cleanup(&filter->source);
    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);

    free(filter);
}

static REFERENCE_TIME frame_duration(struct mpeg_video_codec *filter)
{
    const MPEG1VIDEOINFO *format = (const MPEG1VIDEOINFO *)filter->sink.pin.mt.pbFormat;

    return format->hdr.AvgTimePerFrame;
}

static unsigned int get_halftone_palette(RGBQUAD *palette)
{
    PALETTEENTRY entries[256];
    unsigned int count, i;
    HPALETTE hpal;
    HDC dc;

    /* FIXME: the palette Windows uses for RGB8 output is unknown; this uses the GDI halftone palette. */
    dc = GetDC(NULL);
    hpal = CreateHalftonePalette(dc);
    ReleaseDC(NULL, dc);
    count = GetPaletteEntries(hpal, 0, 256, entries);
    DeleteObject(hpal);
    for (i = 0; i < count; ++i)
    {
        palette[i].rgbRed = entries[i].peRed;
        palette[i].rgbGreen = entries[i].peGreen;
        palette[i].rgbBlue = entries[i].peBlue;
        palette[i].rgbReserved = 0;
    }
    return count;
}

static void set_palette(struct mpeg_video_codec *filter, const AM_MEDIA_TYPE *mt)
{
    const VIDEOINFOHEADER *format = (const VIDEOINFOHEADER *)mt->pbFormat;
    unsigned int count = format->bmiHeader.biClrUsed ? format->bmiHeader.biClrUsed : 256;
    unsigned int r, g, b, i, best;

    free(filter->inverse_palette);
    filter->inverse_palette = NULL;

    if (count <= 256 && mt->cbFormat >= sizeof(VIDEOINFOHEADER) + count * sizeof(RGBQUAD))
    {
        memcpy(filter->palette, (const BYTE *)format + sizeof(VIDEOINFOHEADER), count * sizeof(RGBQUAD));
        filter->palette_size = count;
    }
    else
    {
        filter->palette_size = get_halftone_palette(filter->palette);
    }

    if (!filter->palette_size || !(filter->inverse_palette = malloc(32 * 32 * 32)))
        return;

    for (r = 0; r < 32; ++r)
        for (g = 0; g < 32; ++g)
            for (b = 0; b < 32; ++b)
            {
                unsigned int best_distance = ~0u;

                for (best = 0, i = 0; i < filter->palette_size; ++i)
                {
                    int dr = (int)(r * 8 + 4) - filter->palette[i].rgbRed;
                    int dg = (int)(g * 8 + 4) - filter->palette[i].rgbGreen;
                    int db = (int)(b * 8 + 4) - filter->palette[i].rgbBlue;
                    unsigned int distance = dr * dr + dg * dg + db * db;

                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best = i;
                    }
                }
                filter->inverse_palette[(r << 10) | (g << 5) | b] = best;
            }
}

static HRESULT mpeg_video_codec_init_stream(struct strmbase_filter *iface)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(iface);
    HRESULT hr;

    if (!filter->source.pin.peer)
        return S_OK;

    if (!(filter->decoder = mpeg1video_create()))
        return E_OUTOFMEMORY;

    if (FAILED(hr = CopyMediaType(&filter->output_mt, &filter->source.pin.mt)))
    {
        mpeg1video_destroy(filter->decoder);
        filter->decoder = NULL;
        return hr;
    }
    if (IsEqualGUID(&filter->output_mt.subtype, &MEDIASUBTYPE_RGB8))
        set_palette(filter, &filter->output_mt);

    filter->next_time = 0;
    filter->discontinuity = TRUE;

    if (FAILED(hr = IMemAllocator_Commit(filter->source.pAllocator)))
        ERR("Failed to commit allocator, hr %#lx.\n", hr);

    return S_OK;
}

static HRESULT mpeg_video_codec_cleanup_stream(struct strmbase_filter *iface)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(iface);

    if (filter->source.pin.peer)
    {
        IMemAllocator_Decommit(filter->source.pAllocator);

        EnterCriticalSection(&filter->filter.stream_cs);
        if (filter->decoder)
        {
            mpeg1video_destroy(filter->decoder);
            filter->decoder = NULL;
        }
        FreeMediaType(&filter->output_mt);
        memset(&filter->output_mt, 0, sizeof(filter->output_mt));
        free(filter->inverse_palette);
        filter->inverse_palette = NULL;
        LeaveCriticalSection(&filter->filter.stream_cs);
    }

    return S_OK;
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = mpeg_video_codec_get_pin,
    .filter_destroy = mpeg_video_codec_destroy,
    .filter_init_stream = mpeg_video_codec_init_stream,
    .filter_cleanup_stream = mpeg_video_codec_cleanup_stream,
};

static HRESULT mpeg_video_codec_sink_query_accept(struct strmbase_pin *pin, const AM_MEDIA_TYPE *mt)
{
    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Video)
            || (!IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Payload)
                && !IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Packet))
            || !IsEqualGUID(&mt->formattype, &FORMAT_MPEGVideo)
            || mt->cbFormat < sizeof(MPEG1VIDEOINFO))
        return S_FALSE;

    return S_OK;
}

static HRESULT mpeg_video_codec_sink_query_interface(struct strmbase_pin *pin, REFIID iid, void **out)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->filter);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->sink_IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static BYTE clip(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static void yuv_to_rgb(BYTE y, BYTE u, BYTE v, BYTE *r, BYTE *g, BYTE *b)
{
    int c = y - 16, d = u - 128, e = v - 128;

    *r = clip((298 * c + 409 * e + 128) >> 8);
    *g = clip((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clip((298 * c + 516 * d + 128) >> 8);
}

static LONG convert_frame(struct mpeg_video_codec *filter, const struct mpeg1video_frame *frame,
        const AM_MEDIA_TYPE *mt, BYTE *dst, LONG size)
{
    const VIDEOINFOHEADER *format = (const VIDEOINFOHEADER *)mt->pbFormat;
    const BITMAPINFOHEADER *bih = &format->bmiHeader;
    unsigned int width = min(frame->width, (unsigned int)bih->biWidth);
    unsigned int height = min(frame->height, (unsigned int)abs(bih->biHeight));
    unsigned int x, y, stride, image_size;
    BOOL top_down = TRUE;

    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_YV12))
    {
        unsigned int chroma_stride = bih->biWidth / 2, plane;
        BYTE *planes[3];

        image_size = bih->biWidth * abs(bih->biHeight) * 3 / 2;
        if ((LONG)image_size > size)
            return 0;
        planes[0] = dst;
        planes[2] = dst + bih->biWidth * abs(bih->biHeight);
        planes[1] = planes[2] + chroma_stride * (abs(bih->biHeight) / 2);
        for (y = 0; y < height; ++y)
            memcpy(planes[0] + y * bih->biWidth, frame->planes[0] + y * frame->strides[0], width);
        for (plane = 1; plane < 3; ++plane)
            for (y = 0; y < (height + 1) / 2; ++y)
                memcpy(planes[plane] + y * chroma_stride, frame->planes[plane] + y * frame->strides[plane], (width + 1) / 2);
        return image_size;
    }

    stride = ((bih->biWidth * bih->biBitCount + 31) & ~31) / 8;
    image_size = stride * abs(bih->biHeight);
    if ((LONG)image_size > size)
        image_size = size - size % stride;
    if (image_size < stride * height)
        height = image_size / stride;

    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB24) || IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB32)
            || IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB565) || IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB555)
            || IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB8))
        top_down = bih->biHeight < 0;

    for (y = 0; y < height; ++y)
    {
        const BYTE *luma = frame->planes[0] + y * frame->strides[0];
        const BYTE *cb = frame->planes[1] + (y / 2) * frame->strides[1];
        const BYTE *cr = frame->planes[2] + (y / 2) * frame->strides[2];
        BYTE *row = dst + (top_down ? y : abs(bih->biHeight) - 1 - y) * stride;
        BYTE r, g, b;

        if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_YUY2))
        {
            for (x = 0; x < width; x += 2)
            {
                row[x * 2] = luma[x];
                row[x * 2 + 1] = cb[x / 2];
                row[x * 2 + 2] = luma[x + 1];
                row[x * 2 + 3] = cr[x / 2];
            }
        }
        else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_UYVY))
        {
            for (x = 0; x < width; x += 2)
            {
                row[x * 2] = cb[x / 2];
                row[x * 2 + 1] = luma[x];
                row[x * 2 + 2] = cr[x / 2];
                row[x * 2 + 3] = luma[x + 1];
            }
        }
        else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_Y41P))
        {
            for (x = 0; x + 8 <= width; x += 8)
            {
                BYTE *block = row + x * 3 / 2;

                block[0] = cb[x / 2];
                block[1] = luma[x];
                block[2] = cr[x / 2];
                block[3] = luma[x + 1];
                block[4] = cb[x / 2 + 2];
                block[5] = luma[x + 2];
                block[6] = cr[x / 2 + 2];
                block[7] = luma[x + 3];
                block[8] = luma[x + 4];
                block[9] = luma[x + 5];
                block[10] = luma[x + 6];
                block[11] = luma[x + 7];
            }
        }
        else
        {
            for (x = 0; x < width; ++x)
            {
                yuv_to_rgb(luma[x], cb[x / 2], cr[x / 2], &r, &g, &b);

                if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB24))
                {
                    row[x * 3] = b;
                    row[x * 3 + 1] = g;
                    row[x * 3 + 2] = r;
                }
                else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB32))
                {
                    row[x * 4] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = 0;
                }
                else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB565))
                {
                    WORD value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    memcpy(row + x * 2, &value, sizeof(value));
                }
                else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB555))
                {
                    WORD value = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                    memcpy(row + x * 2, &value, sizeof(value));
                }
                else if (filter->inverse_palette)
                {
                    row[x] = filter->inverse_palette[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
                }
            }
        }
    }

    return image_size;
}

static HRESULT deliver_frame(struct mpeg_video_codec *filter, const struct mpeg1video_frame *frame)
{
    REFERENCE_TIME start, stop;
    IMediaSample *sample;
    AM_MEDIA_TYPE *mt;
    LONG length;
    BYTE *ptr;
    HRESULT hr;

    if (frame->has_time)
        filter->next_time = frame->time;
    start = filter->next_time;
    stop = start;
    filter->next_time = start + frame_duration(filter);

    if (FAILED(hr = IMemAllocator_GetBuffer(filter->source.pAllocator, &sample, &start, &stop, 0)))
        return hr;

    if (IMediaSample_GetMediaType(sample, &mt) == S_OK)
    {
        if (IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo) && mt->cbFormat >= sizeof(VIDEOINFOHEADER))
        {
            FreeMediaType(&filter->output_mt);
            filter->output_mt = *mt;
            CoTaskMemFree(mt);
            if (IsEqualGUID(&filter->output_mt.subtype, &MEDIASUBTYPE_RGB8))
                set_palette(filter, &filter->output_mt);
        }
        else
        {
            DeleteMediaType(mt);
        }
    }

    IMediaSample_GetPointer(sample, &ptr);
    length = convert_frame(filter, frame, &filter->output_mt, ptr, IMediaSample_GetSize(sample));
    IMediaSample_SetActualDataLength(sample, length);
    IMediaSample_SetTime(sample, &start, &stop);
    IMediaSample_SetSyncPoint(sample, TRUE);
    IMediaSample_SetDiscontinuity(sample, filter->discontinuity);
    filter->discontinuity = FALSE;

    hr = IMemInputPin_Receive(filter->source.pMemInputPin, sample);
    IMediaSample_Release(sample);
    return hr;
}

static HRESULT deliver_frames(struct mpeg_video_codec *filter, BOOL eos)
{
    const struct mpeg1video_frame *frame;
    HRESULT hr;

    while ((frame = mpeg1video_next(filter->decoder, eos)))
    {
        if ((hr = deliver_frame(filter, frame)) != S_OK)
            return hr;
    }
    return S_OK;
}

static HRESULT push_packets(struct mpeg_video_codec *filter, const BYTE *data, LONG size, BOOL has_time, REFERENCE_TIME time)
{
    LONG pos = 0, end, header;

    while (pos + 6 <= size)
    {
        if (data[pos] || data[pos + 1] || data[pos + 2] != 1)
        {
            ++pos;
            continue;
        }
        end = min(size, pos + 6 + ((data[pos + 4] << 8) | data[pos + 5]));
        header = pos + 6;
        while (header < end && data[header] == 0xff)
            ++header;
        if (header < end && (data[header] & 0xc0) == 0x40)
            header += 2;
        if (header < end && (data[header] & 0xf0) == 0x20)
            header += 5;
        else if (header < end && (data[header] & 0xf0) == 0x30)
            header += 10;
        else if (header < end && data[header] == 0x0f)
            header += 1;
        if (header < end && !mpeg1video_push(filter->decoder, data + header, end - header, has_time, time))
            return E_OUTOFMEMORY;
        has_time = FALSE;
        pos = end;
    }
    return S_OK;
}

static HRESULT WINAPI mpeg_video_codec_sink_receive(struct strmbase_sink *pin, IMediaSample *sample)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->pin.filter);
    REFERENCE_TIME start, stop;
    BOOL has_time;
    BYTE *data;
    HRESULT hr;

    if (!filter->source.pMemInputPin)
        return VFW_E_NOT_CONNECTED;

    if (filter->filter.state == State_Stopped)
        return VFW_E_WRONG_STATE;

    if (filter->sink.flushing)
        return S_FALSE;

    if (IMediaSample_IsDiscontinuity(sample) == S_OK)
        filter->discontinuity = TRUE;

    has_time = SUCCEEDED(IMediaSample_GetTime(sample, &start, &stop));
    IMediaSample_GetPointer(sample, &data);

    if (IsEqualGUID(&filter->sink.pin.mt.subtype, &MEDIASUBTYPE_MPEG1Packet))
    {
        if (FAILED(hr = push_packets(filter, data, IMediaSample_GetActualDataLength(sample), has_time, start)))
            return hr;
    }
    else if (!mpeg1video_push(filter->decoder, data, IMediaSample_GetActualDataLength(sample), has_time, start))
    {
        return E_OUTOFMEMORY;
    }

    return deliver_frames(filter, FALSE);
}

static HRESULT mpeg_video_codec_sink_eos(struct strmbase_sink *pin)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->pin.filter);
    IPin *peer = filter->source.pin.peer;
    HRESULT hr = S_OK;

    if (filter->sink.flushing)
        return S_FALSE;

    if (filter->decoder)
        deliver_frames(filter, TRUE);

    if (peer)
    {
        /* The renderer calls back into our source pin, which takes the filter lock. */
        LeaveCriticalSection(&filter->filter.stream_cs);
        hr = IPin_EndOfStream(peer);
        EnterCriticalSection(&filter->filter.stream_cs);
    }
    return hr;
}

static HRESULT mpeg_video_codec_sink_end_flush(struct strmbase_sink *pin)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->pin.filter);

    EnterCriticalSection(&filter->filter.stream_cs);
    if (filter->decoder)
    {
        mpeg1video_reset(filter->decoder);
        filter->discontinuity = TRUE;
    }
    LeaveCriticalSection(&filter->filter.stream_cs);

    if (filter->source.pin.peer)
        return IPin_EndFlush(filter->source.pin.peer);
    return S_OK;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = mpeg_video_codec_sink_query_accept,
    .base.pin_query_interface = mpeg_video_codec_sink_query_interface,
    .pfnReceive = mpeg_video_codec_sink_receive,
    .sink_eos = mpeg_video_codec_sink_eos,
    .sink_end_flush = mpeg_video_codec_sink_end_flush,
};

static int find_output_type(const GUID *subtype)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(output_types); ++i)
        if (IsEqualGUID(subtype, output_types[i].subtype))
            return i;
    return -1;
}

static HRESULT mpeg_video_codec_source_query_accept(struct strmbase_pin *pin, const AM_MEDIA_TYPE *mt)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->filter);
    int index;

    if (!filter->sink.pin.peer)
        return S_FALSE;

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Video)
            || !IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo)
            || mt->cbFormat < sizeof(VIDEOINFOHEADER))
        return S_FALSE;

    if ((index = find_output_type(&mt->subtype)) < 0)
        return S_FALSE;

    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_YV12))
        return S_FALSE;

    return S_OK;
}

static HRESULT mpeg_video_codec_source_get_media_type(struct strmbase_pin *pin, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->filter);
    const MPEG1VIDEOINFO *input_format;
    VIDEOINFOHEADER *format;
    DWORD size;

    if (!filter->sink.pin.peer)
        return VFW_S_NO_MORE_ITEMS;

    if (index >= ARRAY_SIZE(output_types))
        return VFW_S_NO_MORE_ITEMS;

    input_format = (const MPEG1VIDEOINFO *)filter->sink.pin.mt.pbFormat;

    size = sizeof(VIDEOINFOHEADER);
    if (output_types[index].compression == BI_BITFIELDS)
        size += 3 * sizeof(DWORD);
    else if (output_types[index].bit_count == 8)
        size += 256 * sizeof(RGBQUAD);

    if (!(format = CoTaskMemAlloc(size)))
        return E_OUTOFMEMORY;
    memset(format, 0, size);

    SetRect(&format->rcSource, 0, 0, input_format->hdr.bmiHeader.biWidth, abs(input_format->hdr.bmiHeader.biHeight));
    format->AvgTimePerFrame = input_format->hdr.AvgTimePerFrame;
    format->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format->bmiHeader.biWidth = input_format->hdr.bmiHeader.biWidth;
    format->bmiHeader.biHeight = abs(input_format->hdr.bmiHeader.biHeight);
    format->bmiHeader.biPlanes = 1;
    format->bmiHeader.biBitCount = output_types[index].bit_count;
    format->bmiHeader.biCompression = output_types[index].compression;
    format->bmiHeader.biSizeImage = format->bmiHeader.biWidth * format->bmiHeader.biHeight
            * format->bmiHeader.biBitCount / 8;
    format->bmiHeader.biXPelsPerMeter = input_format->hdr.bmiHeader.biXPelsPerMeter;
    format->bmiHeader.biYPelsPerMeter = input_format->hdr.bmiHeader.biYPelsPerMeter;
    if (format->AvgTimePerFrame)
        format->dwBitRate = MulDiv(format->bmiHeader.biSizeImage * 8, 10000000, format->AvgTimePerFrame);

    if (output_types[index].compression == BI_BITFIELDS)
    {
        DWORD *masks = (DWORD *)(format + 1);

        masks[0] = 0xf800;
        masks[1] = 0x07e0;
        masks[2] = 0x001f;
    }
    else if (output_types[index].bit_count == 8)
    {
        format->bmiHeader.biClrUsed = 256;
        get_halftone_palette((RGBQUAD *)(format + 1));
    }

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->subtype = *output_types[index].subtype;
    mt->bFixedSizeSamples = TRUE;
    mt->lSampleSize = format->bmiHeader.biSizeImage;
    mt->formattype = FORMAT_VideoInfo;
    mt->cbFormat = size;
    mt->pbFormat = (BYTE *)format;

    return S_OK;
}

static HRESULT mpeg_video_codec_source_query_interface(struct strmbase_pin *pin, REFIID iid, void **out)
{
    struct mpeg_video_codec *filter = impl_from_strmbase_filter(pin->filter);

    if (IsEqualGUID(iid, &IID_IMediaPosition))
        *out = &filter->passthrough.IMediaPosition_iface;
    else if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &filter->passthrough.IMediaSeeking_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->source_IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT WINAPI mpeg_video_codec_source_DecideBufferSize(struct strmbase_source *pin,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    ALLOCATOR_PROPERTIES ret_props;

    props->cBuffers = 1;
    props->cbBuffer = pin->pin.mt.lSampleSize;
    props->cbAlign = 1;
    props->cbPrefix = 0;

    return IMemAllocator_SetProperties(allocator, props, &ret_props);
}

static const struct strmbase_source_ops source_ops =
{
    .base.pin_query_accept = mpeg_video_codec_source_query_accept,
    .base.pin_get_media_type = mpeg_video_codec_source_get_media_type,
    .base.pin_query_interface = mpeg_video_codec_source_query_interface,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = mpeg_video_codec_source_DecideBufferSize,
};

static struct mpeg_video_codec *impl_from_sink_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_video_codec, sink_IQualityControl_iface);
}

static HRESULT WINAPI sink_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct mpeg_video_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_QueryInterface(&filter->sink.pin.IPin_iface, iid, out);
}

static ULONG WINAPI sink_quality_control_AddRef(IQualityControl *iface)
{
    struct mpeg_video_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_AddRef(&filter->sink.pin.IPin_iface);
}

static ULONG WINAPI sink_quality_control_Release(IQualityControl *iface)
{
    struct mpeg_video_codec *filter = impl_from_sink_IQualityControl(iface);
    return IPin_Release(&filter->sink.pin.IPin_iface);
}

static HRESULT WINAPI sink_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct mpeg_video_codec *filter = impl_from_sink_IQualityControl(iface);

    TRACE("filter %p, sender %p, type %#x, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type, q.Proportion, debugstr_time(q.Late), debugstr_time(q.TimeStamp));

    return S_OK;
}

static HRESULT WINAPI sink_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct mpeg_video_codec *filter = impl_from_sink_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

    filter->qc_sink = sink;

    return S_OK;
}

static const IQualityControlVtbl sink_quality_control_vtbl =
{
    sink_quality_control_QueryInterface,
    sink_quality_control_AddRef,
    sink_quality_control_Release,
    sink_quality_control_Notify,
    sink_quality_control_SetSink,
};

static struct mpeg_video_codec *impl_from_source_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_video_codec, source_IQualityControl_iface);
}

static HRESULT WINAPI source_quality_control_QueryInterface(IQualityControl *iface, REFIID iid, void **out)
{
    struct mpeg_video_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_QueryInterface(&filter->source.pin.IPin_iface, iid, out);
}

static ULONG WINAPI source_quality_control_AddRef(IQualityControl *iface)
{
    struct mpeg_video_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_AddRef(&filter->source.pin.IPin_iface);
}

static ULONG WINAPI source_quality_control_Release(IQualityControl *iface)
{
    struct mpeg_video_codec *filter = impl_from_source_IQualityControl(iface);
    return IPin_Release(&filter->source.pin.IPin_iface);
}

static HRESULT WINAPI source_quality_control_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    struct mpeg_video_codec *filter = impl_from_source_IQualityControl(iface);

    TRACE("filter %p, sender %p, type %s, proportion %ld, late %s, timestamp %s.\n",
            filter, sender, q.Type == Famine ? "Famine" : "Flood", q.Proportion,
            debugstr_time(q.Late), debugstr_time(q.TimeStamp));

    /* TODO: drop B pictures when the renderer reports late frames. */
    return S_OK;
}

static HRESULT WINAPI source_quality_control_SetSink(IQualityControl *iface, IQualityControl *sink)
{
    struct mpeg_video_codec *filter = impl_from_source_IQualityControl(iface);

    TRACE("filter %p, sink %p.\n", filter, sink);

    return S_OK;
}

static const IQualityControlVtbl source_quality_control_vtbl =
{
    source_quality_control_QueryInterface,
    source_quality_control_AddRef,
    source_quality_control_Release,
    source_quality_control_Notify,
    source_quality_control_SetSink,
};

HRESULT mpeg_video_codec_create(IUnknown *outer, IUnknown **out)
{
    struct mpeg_video_codec *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_CMpegVideoCodec, &filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"In", &sink_ops, NULL);
    strmbase_source_init(&object->source, &object->filter, L"Out", &source_ops);
    wcscpy(object->sink.pin.name, L"Input");
    wcscpy(object->source.pin.name, L"Output");

    strmbase_passthrough_init(&object->passthrough, (IUnknown *)&object->source.pin.IPin_iface);
    ISeekingPassThru_Init(&object->passthrough.ISeekingPassThru_iface, FALSE,
            &object->sink.pin.IPin_iface);

    object->sink_IQualityControl_iface.lpVtbl = &sink_quality_control_vtbl;
    object->source_IQualityControl_iface.lpVtbl = &source_quality_control_vtbl;

    TRACE("Created MPEG video decoder %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
