/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the USB audio isochronous stream arithmetic
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <uac_stream.h>

static
VOID
TestPacketRate(VOID)
{
    /* full speed: one packet per 2^(bInterval-1) frames of 1 ms */
    ok_int(UacPacketRate(FALSE, 1), 1000);
    ok_int(UacPacketRate(FALSE, 0), 1000);
    ok_int(UacPacketRate(FALSE, 2), 500);
    ok_int(UacPacketRate(FALSE, 4), 125);
    ok_int(UacPacketRate(FALSE, 16), 125);

    /* high speed: one packet per 2^(bInterval-1) microframes of 125 us */
    ok_int(UacPacketRate(TRUE, 1), 8000);
    ok_int(UacPacketRate(TRUE, 0), 8000);
    ok_int(UacPacketRate(TRUE, 2), 4000);
    ok_int(UacPacketRate(TRUE, 4), 1000);
    ok_int(UacPacketRate(TRUE, 16), 1000);
}

START_TEST(UacStream)
{
    TestPacketRate();
}
