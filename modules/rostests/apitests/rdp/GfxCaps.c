/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which graphics pipeline capability set the server confirms (MS-RDPEGFX 2.2.3)
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include "gfxcaps.h"

START_TEST(GfxCaps)
{
    RDPGFX_CAPSET Offered[3] = { { 0 } }, Confirm;

    Offered[0].version = RDPGFX_CAPVERSION_101;
    Offered[0].length = 0x10;
    ok(GfxChooseCaps(Offered, 1, &Confirm), "Version 101 alone was refused\n");
    ok(Confirm.version == RDPGFX_CAPVERSION_101, "Confirmed version 0x%08x\n", Confirm.version);
    ok(Confirm.length == 0x10, "Version 101 was confirmed with capsDataLength %u, not 0x10\n", Confirm.length);
    ok(Confirm.flags == 0, "Version 101 was confirmed with flags 0x%x\n", Confirm.flags);

    Offered[1].version = RDPGFX_CAPVERSION_107;
    Offered[1].length = 0xC;
    Offered[1].flags = RDPGFX_CAPS_FLAG_SMALL_CACHE;
    Offered[2].version = RDPGFX_CAPVERSION_81;
    Offered[2].length = 4;
    Offered[2].flags = RDPGFX_CAPS_FLAG_THINCLIENT;
    ok(GfxChooseCaps(Offered, 3, &Confirm), "Three versions were refused\n");
    ok(Confirm.version == RDPGFX_CAPVERSION_107, "Confirmed version 0x%08x, not the highest\n", Confirm.version);
    ok(Confirm.length == 4, "Version 107 was confirmed with capsDataLength %u\n", Confirm.length);
    ok(Confirm.flags == RDPGFX_CAPS_FLAG_SMALL_CACHE, "Version 107 lost its flags: 0x%x\n", Confirm.flags);

    Offered[0].version = 0x000A0700;
    ok(!GfxChooseCaps(Offered, 1, &Confirm), "An unknown version was confirmed\n");
    ok(!GfxChooseCaps(Offered, 0, &Confirm), "Nothing offered was confirmed\n");
}
