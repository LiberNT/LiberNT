/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon I2C controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_I2C
{
    PUCHAR Registers;
    ULONG Size;
    ULONG Control;
} APPLE_I2C, *PAPPLE_I2C;

NTSTATUS
NTAPI
AppleI2cOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_I2C I2c);

VOID
NTAPI
AppleI2cClose(
    _Inout_ PAPPLE_I2C I2c);

NTSTATUS
NTAPI
AppleI2cTransfer(
    _Inout_ PAPPLE_I2C I2c,
    _In_ UCHAR Address,
    _In_reads_bytes_opt_(WriteLength) const UCHAR *Write,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_opt_(ReadLength) PUCHAR Read,
    _In_ ULONG ReadLength);
