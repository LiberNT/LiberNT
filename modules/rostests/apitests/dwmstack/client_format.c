/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the conversion of client swap chain formats for software composition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include <apitest.h>
#include <windows.h>
#include <reactos/dwmframe.h>
#include "../../../../base/system/dwm/dxformat.h"

static void
TestFloatRow(void)
{
    /* 0.5, 0.25, 1.0, 1.0 / 2.0, -1.0, 0.0, 0.5 */
    static const unsigned short Row[8] = {0x3800, 0x3400, 0x3C00, 0x3C00, 0x4000, 0xBC00, 0x0000, 0x3800};
    BYTE Out[8];

    DwmDxConvertRow(DWM_DX_FORMAT_R16G16B16A16_FLOAT, (const BYTE *)Row, Out, 2);
    ok_int(Out[0], 255);
    ok_int(Out[1], 137);
    ok_int(Out[2], 188);
    ok_int(Out[3], 255);
    ok_int(Out[4], 0);
    ok_int(Out[5], 0);
    ok_int(Out[6], 255);
    ok_int(Out[7], 128);
}

static void
TestTenBitRow(void)
{
    /* R 0x200, G 0x100, B 0x3FF, A 3 / R 0, G 0x3FF, B 0, A 1 */
    static const unsigned int Row[2] = {0xFFF40200u, 0x400FFC00u};
    BYTE Out[8];

    DwmDxConvertRow(DWM_DX_FORMAT_R10G10B10A2_UNORM, (const BYTE *)Row, Out, 2);
    ok_int(Out[0], 255);
    ok_int(Out[1], 64);
    ok_int(Out[2], 128);
    ok_int(Out[3], 255);
    ok_int(Out[4], 0);
    ok_int(Out[5], 255);
    ok_int(Out[6], 0);
    ok_int(Out[7], 85);
}

static void
TestByteRows(void)
{
    static const BYTE Row[4] = {1, 2, 3, 4};
    BYTE Out[4];

    DwmDxConvertRow(DWM_DX_FORMAT_R8G8B8A8_UNORM, Row, Out, 1);
    ok_int(Out[0], 3);
    ok_int(Out[1], 2);
    ok_int(Out[2], 1);
    ok_int(Out[3], 4);
    DwmDxConvertRow(DWM_DX_FORMAT_B8G8R8A8_UNORM, Row, Out, 1);
    ok_int(Out[0], 1);
    ok_int(Out[1], 2);
    ok_int(Out[2], 3);
    ok_int(Out[3], 4);
}

START_TEST(client_format)
{
    TestFloatRow();
    TestTenBitRow();
    TestByteRows();
}
