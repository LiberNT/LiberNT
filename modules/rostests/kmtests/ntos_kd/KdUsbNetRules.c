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

START_TEST(KdUsbNetRules)
{
    TestNcmOffset();
}
