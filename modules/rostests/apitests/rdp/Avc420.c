/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test AVC420 (MS-RDPEGFX 2.2.4.4) encoding through the OpenH264 library
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <stdlib.h>
#include <freerdp/codec/h264.h>
#include <freerdp/codec/color.h>

#define WIDTH 320
#define HEIGHT 240

static UINT32 Source[WIDTH * HEIGHT];
static UINT32 Decoded[WIDTH * HEIGHT];

static void FillFrame(UINT32 frame)
{
    UINT32 x, y;
    for (y = 0; y < HEIGHT; ++y)
        for (x = 0; x < WIDTH; ++x)
        {
            UINT32 r = (x + frame * 4) & 0xFF, g = (y * 2 + frame) & 0xFF, b = ((x + y) / 2) & 0xFF;
            if (x >= 100 + frame * 6 && x < 160 + frame * 6 && y >= 80 && y < 140) r = g = b = 0xF0;
            Source[y * WIDTH + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
}

static double MeanError(void)
{
    ULONGLONG total = 0;
    UINT32 i, shift;
    for (i = 0; i < WIDTH * HEIGHT; ++i)
        for (shift = 0; shift < 24; shift += 8)
        {
            int a = (Source[i] >> shift) & 0xFF, b = (Decoded[i] >> shift) & 0xFF;
            total += a > b ? a - b : b - a;
        }
    return (double)total / (WIDTH * HEIGHT * 3);
}

START_TEST(Avc420)
{
    H264_CONTEXT *encoder = h264_context_new(TRUE), *decoder;
    RECTANGLE_16 whole = { 0, 0, WIDTH, HEIGHT };
    UINT32 frame;

    if (!encoder)
    {
        skip("openh264.dll is not available\n");
        return;
    }
    decoder = h264_context_new(FALSE);
    ok(decoder != NULL, "the decoder did not start\n");
    ok(h264_context_reset(encoder, WIDTH, HEIGHT), "h264_context_reset of the encoder failed\n");
    ok(decoder && h264_context_reset(decoder, WIDTH, HEIGHT), "h264_context_reset of the decoder failed\n");
    ok(h264_context_set_option(encoder, H264_CONTEXT_OPTION_USAGETYPE, H264_SCREEN_CONTENT_REAL_TIME) &&
       h264_context_set_option(encoder, H264_CONTEXT_OPTION_RATECONTROL, H264_RATECONTROL_CQP) &&
       h264_context_set_option(encoder, H264_CONTEXT_OPTION_QP, 22) &&
       h264_context_set_option(encoder, H264_CONTEXT_OPTION_FRAMERATE, 30),
       "setting the encoder options failed\n");
    if (!decoder) goto done;

    for (frame = 0; frame < 4; ++frame)
    {
        RDPGFX_H264_METABLOCK meta = {0};
        BYTE *stream = NULL;
        UINT32 size = 0;
        INT32 status;
        FillFrame(frame);
        status = avc420_compress(encoder, (const BYTE *)Source, PIXEL_FORMAT_BGRX32, WIDTH * 4, WIDTH, HEIGHT, &whole,
                                 &stream, &size, &meta);
        ok(status > 0, "frame %lu: avc420_compress returned %ld\n", (ULONG)frame, (long)status);
        ok(meta.numRegionRects > 0, "frame %lu: no region was reported as changed\n", (ULONG)frame);
        ok(size > 0 && size < WIDTH * HEIGHT, "frame %lu: %lu bytes of H.264\n", (ULONG)frame, (ULONG)size);
        if (status > 0 && size)
        {
            if (!frame) memset(Decoded, 0, sizeof(Decoded));
            status = avc420_decompress(decoder, stream, size, (BYTE *)Decoded, PIXEL_FORMAT_BGRX32, WIDTH * 4, WIDTH,
                                       HEIGHT, meta.regionRects, meta.numRegionRects);
            ok(status >= 0, "frame %lu: avc420_decompress returned %ld\n", (ULONG)frame, (long)status);
            ok(MeanError() < 6.0, "frame %lu: decoded pixels differ by %.2f on average\n", (ULONG)frame, MeanError());
        }
        free_h264_metablock(&meta);
    }

done:
    h264_context_free(decoder);
    h264_context_free(encoder);
}
