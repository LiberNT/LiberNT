/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the ClearCodec encoder of the RDP console server against the FreeRDP decoder
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <stdlib.h>
#include <freerdp/codec/clear.h>
#include <freerdp/codec/color.h>
#include "clearenc.h"

#define MAX_SIZE (320 * 320)

static UINT32 Source[MAX_SIZE + 64];
static UINT32 Decoded[MAX_SIZE];
static BYTE Encoded[MAX_SIZE * 4 + 1024];
static UINT32 Random = 0x12345678;

static UINT32 NextRandom(void)
{
    Random = Random * 1103515245u + 12345u;
    return Random >> 8;
}

static void FillPalette(UINT32 width, UINT32 height, UINT32 stride, UINT32 colors, UINT32 runMax)
{
    UINT32 x, y, color = 0, left = 0;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
        {
            if (!left)
            {
                color = (NextRandom() % colors) * 0x010203u + 0x00102030u;
                left = 1 + NextRandom() % runMax;
            }
            Source[y * stride + x] = 0xFF000000u | (color & 0x00FFFFFF);
            --left;
        }
}

static void FillGradient(UINT32 width, UINT32 height, UINT32 stride)
{
    UINT32 x, y;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            Source[y * stride + x] = 0xFF000000u | ((x * 7 + y * 3) & 0xFF) << 16 | (x & 0xFF) << 8 | ((x + y) & 0xFF);
}

static void FillSolid(UINT32 width, UINT32 height, UINT32 stride, UINT32 color)
{
    UINT32 x, y;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            Source[y * stride + x] = color;
}

static void RoundTrip(CLEAR_CONTEXT *decoder, BYTE *sequence, CLEAR_LAYER layer, UINT32 width, UINT32 height,
                      UINT32 stride, BOOL expectEncoded, const char *name)
{
    UINT32 size = 0, x, y, mismatches = 0;
    INT32 status;
    BOOL encoded = ClearEncode(*sequence, layer, (const BYTE *)Source, stride * 4, width, height,
                               Encoded, sizeof(Encoded), &size);
    ok(encoded == expectEncoded, "%s: encoded %d, expected %d\n", name, encoded, expectEncoded);
    if (!encoded) return;
    ++*sequence;
    memset(Decoded, 0xCC, sizeof(Decoded));
    status = clear_decompress(decoder, Encoded, size, width, height, (BYTE *)Decoded, PIXEL_FORMAT_BGRX32,
                              width * 4, 0, 0, width, height, NULL);
    ok(status == 0, "%s: clear_decompress returned %d\n", name, (int)status);
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            if ((Decoded[y * width + x] & 0x00FFFFFF) != (Source[y * stride + x] & 0x00FFFFFF)) ++mismatches;
    ok(mismatches == 0, "%s: %lu of %lu pixels differ\n", name, (ULONG)mismatches, (ULONG)(width * height));
}

START_TEST(ClearCodec)
{
    CLEAR_CONTEXT *decoder = clear_context_new(FALSE);
    BYTE sequence = 0;
    UINT32 i;
    ok(decoder != NULL, "clear_context_new failed\n");
    if (!decoder) return;

    FillPalette(64, 64, 64, 5, 40);
    ok(ClearPaletteSize((const BYTE *)Source, 64 * 4, 64, 64, CLEAR_MAX_PALETTE) == 5, "palette size of a 5-color tile\n");
    RoundTrip(decoder, &sequence, ClearRlex, 64, 64, 64, TRUE, "rlex 5 colors");
    RoundTrip(decoder, &sequence, ClearResidual, 64, 64, 64, TRUE, "residual 5 colors");

    FillPalette(37, 23, 51, 127, 3);
    ok(ClearPaletteSize((const BYTE *)Source, 51 * 4, 37, 23, CLEAR_MAX_PALETTE) <= 127, "palette size within 127\n");
    RoundTrip(decoder, &sequence, ClearRlex, 37, 23, 51, TRUE, "rlex odd size with a padded stride");

    FillGradient(64, 64, 64);
    ok(ClearPaletteSize((const BYTE *)Source, 64 * 4, 64, 64, CLEAR_MAX_PALETTE) > CLEAR_MAX_PALETTE,
       "a gradient has more than 127 colors\n");
    RoundTrip(decoder, &sequence, ClearRlex, 64, 64, 64, FALSE, "rlex refuses more than 127 colors");
    RoundTrip(decoder, &sequence, ClearResidual, 64, 64, 64, TRUE, "residual gradient");

    for (i = 0; i < 64; ++i)
        Source[i] = 0xFF000000u | ((i % 4) * 0x111111u);
    RoundTrip(decoder, &sequence, ClearRlex, 64, 1, 64, TRUE, "rlex suites of increasing indexes");
    for (i = 0; i < 4; ++i)
        Source[i] = 0xFF000000u | (i * 0x111111u);
    RoundTrip(decoder, &sequence, ClearRlex, 4, 1, 4, FALSE, "rlex larger than 3 bytes a pixel is refused");

    FillSolid(64, 64, 64, 0xFF336699u);
    ok(ClearPaletteSize((const BYTE *)Source, 64 * 4, 64, 64, CLEAR_MAX_PALETTE) == 1, "palette size of a solid tile\n");
    RoundTrip(decoder, &sequence, ClearRlex, 64, 64, 64, TRUE, "rlex single color");
    RoundTrip(decoder, &sequence, ClearResidual, 64, 64, 64, TRUE, "residual single color");

    FillSolid(300, 300, 300, 0xFF102030u);
    RoundTrip(decoder, &sequence, ClearResidual, 300, 300, 300, TRUE, "residual run longer than 65535");
    FillPalette(300, 300, 300, 2, 2000);
    RoundTrip(decoder, &sequence, ClearResidual, 300, 300, 300, TRUE, "residual runs longer than 255");
    RoundTrip(decoder, &sequence, ClearRlex, 300, 300, 300, TRUE, "rlex runs longer than 255");

    for (i = 0; i < 300; ++i)
    {
        FillPalette(16, 8, 16, 3, 4);
        RoundTrip(decoder, &sequence, (i & 1) ? ClearRlex : ClearResidual, 16, 8, 16, TRUE, "sequence number wrap");
    }

    FillPalette(64, 64, 64, 5, 40);
    ok(!ClearEncode(sequence, ClearResidual, (const BYTE *)Source, 64 * 4, 64, 64, Encoded, 8, &i),
       "encoding into a buffer that is too small fails\n");
    clear_context_free(decoder);
}
