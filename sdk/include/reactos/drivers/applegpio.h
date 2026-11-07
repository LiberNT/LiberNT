/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon pin controller: pin functions and GPIO outputs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_GPIO
{
    PUCHAR Registers;
    ULONG Size;
    ULONG Pin;
    BOOLEAN ActiveLow;
} APPLE_GPIO, *PAPPLE_GPIO;

NTSTATUS
NTAPI
AppleGpioApplyPinctrl(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node);

NTSTATUS
NTAPI
AppleGpioOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Property,
    _Out_ PAPPLE_GPIO Gpio);

VOID
NTAPI
AppleGpioClose(
    _Inout_ PAPPLE_GPIO Gpio);

VOID
NTAPI
AppleGpioSet(
    _In_ PAPPLE_GPIO Gpio,
    _In_ BOOLEAN Asserted);
