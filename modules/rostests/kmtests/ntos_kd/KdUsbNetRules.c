/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CDC rules of the USB network debugger, with values captured from real adapters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <drivers/kdusbrules.h>

static
VOID
TestNcmOffset(VOID)
{
    /* AX88179: divisor 8, remainder 6, NDP ends at 32; offset 38 put two stray bytes in front of every frame */
    ok_eq_ulong(KdUsbNcmDatagramOffset(32, 8, 6), 32UL);
    ok_eq_ulong((KdUsbNcmDatagramOffset(32, 8, 6) + KD_USB_ETHERNET_HEADER) % 8, 6UL);
    ok_eq_ulong(KdUsbNcmDatagramOffset(28, 4, 0), 30UL);
    ok_eq_ulong(KdUsbNcmDatagramOffset(28, 4, 2), 28UL);
    ok_eq_ulong(KdUsbNcmDatagramOffset(28, 1, 0), 28UL);
}

static
VOID
TestNotifications(VOID)
{
    /* Speed change, then connection in the same transfer, as the AX88179 and the RTL8153 send them */
    static const UCHAR Connected[] = {0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                                      0x00, 0xCA, 0x9A, 0x3B, 0x00, 0xCA, 0x9A, 0x3B,
                                      0xA1, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
    static const UCHAR Disconnected[] = {0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0xA1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    static const UCHAR Alone[] = {0xA1, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
    static const UCHAR SpeedOnly[] = {0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                                      0x00, 0xCA, 0x9A, 0x3B, 0x00, 0xCA, 0x9A, 0x3B};
    static const UCHAR Oversized[] = {0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00, 0xFF, 0xFF,
                                      0xA1, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
    BOOLEAN State;

    ok_bool_true(KdUsbParseNotifications(Connected, sizeof(Connected), &State), "connection found");
    ok_bool_true(State, "connected");
    ok_bool_true(KdUsbParseNotifications(Disconnected, sizeof(Disconnected), &State), "connection found");
    ok_bool_false(State, "disconnected");
    ok_bool_true(KdUsbParseNotifications(Alone, sizeof(Alone), &State), "connection found");
    ok_bool_true(State, "connected");
    ok_bool_false(KdUsbParseNotifications(SpeedOnly, sizeof(SpeedOnly), &State), "no connection state");
    ok_bool_false(KdUsbParseNotifications(Alone, sizeof(Alone) - 1, &State), "truncated");
    ok_bool_false(KdUsbParseNotifications(Oversized, sizeof(Oversized), &State), "length past the transfer");
    ok_bool_false(KdUsbParseNotifications(Alone, 0, &State), "empty");
}

START_TEST(KdUsbNetRules)
{
    TestNcmOffset();
    TestNotifications();
}
