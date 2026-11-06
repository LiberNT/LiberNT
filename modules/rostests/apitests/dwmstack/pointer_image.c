/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the composited pointer's image and placement
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include "../../../../base/system/dwm/gpupointer.h"

START_TEST(pointer_image)
{
    const ULONG OnBlack[] = {0xFF000000, 0x00FFFFFF, 0xFF404040, 0x00123456};
    const ULONG OnWhite[] = {0xFFFFFFFF, 0xFFFFFFFF, 0x00BFBFBF, 0x00123456};
    const ULONG InvertBlack[] = {0xFFFFFFFF};
    const ULONG InvertWhite[] = {0xFF000000};
    ULONG Image[4], Invert[4];
    RECT Rect;

    ok_int(DwmPointerConvert(OnBlack, OnWhite, ARRAYSIZE(OnBlack), Image, Invert), FALSE);
    ok_hex(Image[0], 0x00000000);
    ok_hex(Image[1], 0xFFFFFFFF);
    ok_hex(Image[2], 0x80404040);
    ok_hex(Image[3], 0xFF123456);
    ok_hex(Invert[0], 0);
    ok_hex(Invert[1], 0);
    ok_hex(Invert[2], 0);
    ok_hex(Invert[3], 0);

    ok_int(DwmPointerConvert(InvertBlack, InvertWhite, ARRAYSIZE(InvertBlack), Image, Invert), TRUE);
    ok_hex(Image[0], 0);
    ok_hex(Invert[0], 0xFFFFFFFF);

    DwmPointerRect(&Rect, 100, 50, 3, 4, 32, 48);
    ok_long(Rect.left, 97);
    ok_long(Rect.top, 46);
    ok_long(Rect.right, 129);
    ok_long(Rect.bottom, 94);
}
