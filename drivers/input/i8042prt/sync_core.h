/*
 * PROJECT:     LiberNT i8042 port driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Rules for the interrupt that synchronizes the keyboard and mouse ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

PKINTERRUPT
I8042CoreSyncInterrupt(
    _In_opt_ PKINTERRUPT Current,
    _In_ PKINTERRUPT Connected,
    _In_ KIRQL ConnectedDirql,
    _In_ KIRQL DirqlMax);

NTSTATUS
I8042CorePacketStatus(
    _In_ BOOLEAN Initialized,
    _In_opt_ PKINTERRUPT SyncInterrupt);

BOOLEAN
I8042CorePacketTimedOut(
    _In_ BOOLEAN TimerSignaled,
    _In_ BOOLEAN PacketActive);
