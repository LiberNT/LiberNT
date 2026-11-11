/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon pin controller: pin functions, GPIO outputs and GPIO interrupts
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

typedef struct _APPLE_GPIO_INTERRUPT
{
    PUCHAR Registers;
    ULONG Size;
    ULONG Pin;
    ULONG Group;
    ULONG Mode;
    ULONG Gsi;
    BOOLEAN EdgeTriggered;
} APPLE_GPIO_INTERRUPT, *PAPPLE_GPIO_INTERRUPT;

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

NTSTATUS
NTAPI
AppleGpioQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

NTSTATUS
NTAPI
AppleGpioOpenInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _In_ ULONG Gsi,
    _Out_ PAPPLE_GPIO_INTERRUPT Interrupt);

VOID
NTAPI
AppleGpioCloseInterrupt(
    _Inout_ PAPPLE_GPIO_INTERRUPT Interrupt);

VOID
NTAPI
AppleGpioMaskInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt);

VOID
NTAPI
AppleGpioUnmaskInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt);

VOID
NTAPI
AppleGpioTestInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt);

BOOLEAN
NTAPI
AppleGpioClaimInterrupt(
    _In_ PAPPLE_GPIO_INTERRUPT Interrupt);
