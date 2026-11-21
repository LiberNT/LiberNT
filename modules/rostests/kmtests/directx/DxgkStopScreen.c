/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pixel conversion and placement of the stop screen written through the display miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "stopscreen_core.h"

static
VOID
DxgkStopScreenTestFormats(VOID)
{
    DXGK_STOP_SCREEN_FORMAT Format;

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 32, 0x3FF00000, 0x000FFC00, 0x000003FF),
                 "a 10-bit-per-channel frame buffer is accepted");
    ok_eq_ulong(Format.BytesPerPixel, 4UL);
    ok_eq_ulong((ULONG)Format.Channel[DXGK_STOP_SCREEN_RED].Shift, 20UL);
    ok_eq_ulong((ULONG)Format.Channel[DXGK_STOP_SCREEN_RED].Bits, 10UL);
    ok_eq_ulong((ULONG)Format.Channel[DXGK_STOP_SCREEN_GREEN].Shift, 10UL);
    ok_eq_ulong((ULONG)Format.Channel[DXGK_STOP_SCREEN_BLUE].Shift, 0UL);

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 16, 0xF800, 0x07E0, 0x001F), "5:6:5 is accepted");
    ok_eq_ulong(Format.BytesPerPixel, 2UL);
    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 24, 0xFF0000, 0x00FF00, 0x0000FF), "24-bit is accepted");
    ok_eq_ulong(Format.BytesPerPixel, 3UL);

    ok_bool_false(DxgkStopScreenCoreInitFormat(&Format, 8, 0xE0, 0x1C, 0x03), "8-bit is refused");
    ok_eq_ulong(Format.BytesPerPixel, 0UL);
    ok_bool_false(DxgkStopScreenCoreInitFormat(&Format, 32, 0, 0x0000FF00, 0x000000FF), "a missing channel is refused");
    ok_bool_false(DxgkStopScreenCoreInitFormat(&Format, 32, 0x00FF0000, 0x00FFFF00, 0x000000FF),
                  "overlapping channels are refused");
    ok_bool_false(DxgkStopScreenCoreInitFormat(&Format, 32, 0x00F0F000, 0x00000F00, 0x000000FF),
                  "a split channel is refused");
    ok_bool_false(DxgkStopScreenCoreInitFormat(&Format, 16, 0x1F0000, 0x07E0, 0x001F),
                  "a channel outside the pixel is refused");
    ok_eq_ulong(Format.BytesPerPixel, 0UL);
}

static
VOID
DxgkStopScreenTestConversion(VOID)
{
    static const ULONG Wide[] = { 0x3FFFFFFF, 0x3FF00000, 0x000FFC00, 0x000003FF, 0x20080200, 0x00000000 };
    static const ULONG WideExpected[] = { 0x00FFFFFF, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00808080, 0x00000000 };
    static const USHORT Packed[] = { 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x8000 };
    static const ULONG PackedExpected[] = { 0x00FFFFFF, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00840000 };
    static const UCHAR Triple[] = { 0x56, 0x34, 0x12, 0xFF, 0x00, 0x80 };
    static const ULONG Plain[] = { 0xAB123456 };
    DXGK_STOP_SCREEN_FORMAT Format;
    ULONG Output[RTL_NUMBER_OF(Wide)];
    ULONG Index;

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 32, 0x3FF00000, 0x000FFC00, 0x000003FF), "10-bit format");
    RtlFillMemory(Output, sizeof(Output), 0xCC);
    DxgkStopScreenCoreConvertRow(&Format, (const UCHAR *)Wide, RTL_NUMBER_OF(Wide), Output);
    for (Index = 0; Index < RTL_NUMBER_OF(Wide); Index++)
        ok(Output[Index] == WideExpected[Index], "10-bit pixel %lu: %08lx, expected %08lx\n", Index, Output[Index], WideExpected[Index]);

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 16, 0xF800, 0x07E0, 0x001F), "5:6:5 format");
    RtlFillMemory(Output, sizeof(Output), 0xCC);
    DxgkStopScreenCoreConvertRow(&Format, (const UCHAR *)Packed, RTL_NUMBER_OF(Packed), Output);
    for (Index = 0; Index < RTL_NUMBER_OF(Packed); Index++)
        ok(Output[Index] == PackedExpected[Index], "5:6:5 pixel %lu: %08lx, expected %08lx\n", Index, Output[Index], PackedExpected[Index]);
    ok(Output[RTL_NUMBER_OF(Packed)] == 0xCCCCCCCC, "the conversion wrote past its count\n");

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 24, 0xFF0000, 0x00FF00, 0x0000FF), "24-bit format");
    DxgkStopScreenCoreConvertRow(&Format, Triple, 2, Output);
    ok_eq_hex(Output[0], 0x00123456UL);
    ok_eq_hex(Output[1], 0x008000FFUL);

    ok_bool_true(DxgkStopScreenCoreInitFormat(&Format, 32, 0x00FF0000, 0x0000FF00, 0x000000FF), "8:8:8 format");
    DxgkStopScreenCoreConvertRow(&Format, (const UCHAR *)Plain, 1, Output);
    ok_eq_hex(Output[0], 0x00123456UL);
}

static
VOID
DxgkStopScreenTestPlacement(VOID)
{
    ULONG Start;
    ULONG Length;
    ULONG Position;

    ok_eq_long(DxgkStopScreenCoreOrigin(3456, 3456), 0L);
    ok_eq_long(DxgkStopScreenCoreOrigin(2160, 2234), 37L);
    ok_eq_long(DxgkStopScreenCoreOrigin(4000, 3456), -272L);

    Start = 0;
    Length = 2160;
    ok_bool_true(DxgkStopScreenCoreClip(37, 2234, &Start, &Length, &Position), "a smaller screen is centred");
    ok_eq_ulong(Start, 0UL);
    ok_eq_ulong(Length, 2160UL);
    ok_eq_ulong(Position, 37UL);

    Start = 0;
    Length = 4000;
    ok_bool_true(DxgkStopScreenCoreClip(-272, 3456, &Start, &Length, &Position), "a larger screen is cut on both sides");
    ok_eq_ulong(Start, 272UL);
    ok_eq_ulong(Length, 3456UL);
    ok_eq_ulong(Position, 0UL);

    Start = 100;
    Length = 50;
    ok_bool_false(DxgkStopScreenCoreClip(-272, 3456, &Start, &Length, &Position), "a region left of the display is dropped");
    ok_eq_ulong(Position, 0UL);

    Start = 3700;
    Length = 300;
    ok_bool_true(DxgkStopScreenCoreClip(-272, 3456, &Start, &Length, &Position), "a region across the right edge is cut");
    ok_eq_ulong(Start, 3700UL);
    ok_eq_ulong(Length, 28UL);
    ok_eq_ulong(Position, 3428UL);

    Start = 4000;
    Length = 1;
    ok_bool_false(DxgkStopScreenCoreClip(0, 3456, &Start, &Length, &Position), "a region right of the display is dropped");
}

START_TEST(DxgkStopScreen)
{
    DxgkStopScreenTestFormats();
    DxgkStopScreenTestConversion();
    DxgkStopScreenTestPlacement();
}
