/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt that synchronizes the i8042 keyboard and mouse ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <sync_core.h>

START_TEST(IoI8042Sync)
{
    PKINTERRUPT Keyboard = (PKINTERRUPT)(ULONG_PTR)0x1000;
    PKINTERRUPT Mouse = (PKINTERRUPT)(ULONG_PTR)0x2000;

    ok_eq_pointer(I8042CoreSyncInterrupt(NULL, Keyboard, 5, 8), Keyboard);
    ok_eq_pointer(I8042CoreSyncInterrupt(NULL, Keyboard, 8, 8), Keyboard);
    ok_eq_pointer(I8042CoreSyncInterrupt(NULL, Mouse, 5, 8), Mouse);
    ok_eq_pointer(I8042CoreSyncInterrupt(Keyboard, Mouse, 8, 8), Mouse);
    ok_eq_pointer(I8042CoreSyncInterrupt(Keyboard, Mouse, 5, 8), Keyboard);
    ok_eq_pointer(I8042CoreSyncInterrupt(Mouse, Keyboard, 8, 8), Keyboard);

    ok_eq_hex(I8042CorePacketStatus(FALSE, NULL), STATUS_DEVICE_NOT_READY);
    ok_eq_hex(I8042CorePacketStatus(FALSE, Keyboard), STATUS_DEVICE_NOT_READY);
    ok_eq_hex(I8042CorePacketStatus(TRUE, NULL), STATUS_DEVICE_NOT_READY);
    ok_eq_hex(I8042CorePacketStatus(TRUE, Keyboard), STATUS_SUCCESS);

    ok_eq_bool(I8042CorePacketTimedOut(TRUE, TRUE), TRUE);
    ok_eq_bool(I8042CorePacketTimedOut(TRUE, FALSE), FALSE);
    ok_eq_bool(I8042CorePacketTimedOut(FALSE, TRUE), FALSE);
    ok_eq_bool(I8042CorePacketTimedOut(FALSE, FALSE), FALSE);
}
