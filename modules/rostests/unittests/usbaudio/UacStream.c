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

static
VOID
TestFeatureUnit2Controls(VOID)
{
    ok_hex(UacFeatureUnit2Controls(0), 0);
    /* mute read-only (01), mute programmable (11) */
    ok_hex(UacFeatureUnit2Controls(0x1), 0x1);
    ok_hex(UacFeatureUnit2Controls(0x3), 0x1);
    /* volume programmable alone is the Audio 1.0 volume bit, not mute or bass */
    ok_hex(UacFeatureUnit2Controls(0xC), 0x2);
    ok_hex(UacFeatureUnit2Controls(0x4), 0x2);
    /* mute and volume programmable */
    ok_hex(UacFeatureUnit2Controls(0xF), 0x3);
    /* bass (D5..4) and mid (D7..6) */
    ok_hex(UacFeatureUnit2Controls(0x30), 0x4);
    ok_hex(UacFeatureUnit2Controls(0xC0), 0x8);
    /* loudness (D19..18) is the tenth control, Audio 1.0 bit 9 */
    ok_hex(UacFeatureUnit2Controls(0xC0000), 0x200);
    ok_hex(UacFeatureUnit2Controls(0xFFFFFFFF), 0xFFFF);
}

static
VOID
TestNormalizeFeedback(VOID)
{
    /* 48 kHz: 6.0 frames per microframe, 48.0 per 1 ms frame */
    ok_hex(UacNormalizeFeedback(TRUE, 6 << 16, 6 << 16), 6 << 16);
    ok_hex(UacNormalizeFeedback(TRUE, (6 << 16) + 0x1000, 6 << 16), (6 << 16) + 0x1000);
    ok_hex(UacNormalizeFeedback(TRUE, 48 << 16, 6 << 16), 6 << 16);
    /* full speed and an unknown nominal rate are taken as they come */
    ok_hex(UacNormalizeFeedback(FALSE, 48 << 16, 48 << 16), 48 << 16);
    ok_hex(UacNormalizeFeedback(TRUE, 48 << 16, 0), 48 << 16);
    /* more than 16 times nominal is not a per-frame report */
    ok_hex(UacNormalizeFeedback(TRUE, 200 << 16, 6 << 16), 200 << 16);
}

START_TEST(UacStream)
{
    TestPacketRate();
    TestFeatureUnit2Controls();
    TestNormalizeFeedback();
}
