/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon numerically controlled oscillator (audio clocks)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_NCO
{
    PUCHAR Registers;
    ULONG ParentRate;
} APPLE_NCO, *PAPPLE_NCO;

NTSTATUS
NTAPI
AppleNcoOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PAPPLE_NCO Nco);

VOID
NTAPI
AppleNcoClose(
    _Inout_ PAPPLE_NCO Nco);

NTSTATUS
NTAPI
AppleNcoSetRate(
    _In_ PAPPLE_NCO Nco,
    _In_ ULONG Rate);

VOID
NTAPI
AppleNcoEnable(
    _In_ PAPPLE_NCO Nco,
    _In_ BOOLEAN Enable);
