/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon SPMI controller and PMU NVMEM cell reads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_SPMI
{
    PUCHAR Registers;
    ULONG Size;
    BOOLEAN Failed;
} APPLE_SPMI, *PAPPLE_SPMI;

typedef struct _APPLE_NVMEM_CELL
{
    UCHAR Data[16];
    ULONG Length;
    ULONG Address;
    ULONG BitOffset;
    ULONG BitCount;
} APPLE_NVMEM_CELL, *PAPPLE_NVMEM_CELL;

NTSTATUS
NTAPI
AppleSpmiOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_SPMI Spmi);

VOID
NTAPI
AppleSpmiClose(
    _Inout_ PAPPLE_SPMI Spmi);

NTSTATUS
NTAPI
AppleSpmiRead(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ USHORT Address,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length);

NTSTATUS
NTAPI
AppleSpmiRegisterRead(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ UCHAR Address,
    _Out_ PUCHAR Value);

NTSTATUS
NTAPI
AppleSpmiZeroWrite(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ UCHAR Data);

NTSTATUS
NTAPI
AppleSpmiWakeup(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid);

BOOLEAN
NTAPI
AppleSpmiInterruptPending(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt);

VOID
NTAPI
AppleSpmiEnableInterrupt(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt);

VOID
NTAPI
AppleSpmiAcknowledgeInterrupt(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ ULONG Interrupt);

NTSTATUS
NTAPI
AppleNvmemReadCell(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle,
    _Out_ PAPPLE_NVMEM_CELL Cell);

NTSTATUS
NTAPI
AppleSpmiWrite(
    _Inout_ PAPPLE_SPMI Spmi,
    _In_ UCHAR Sid,
    _In_ USHORT Address,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length);

NTSTATUS
NTAPI
AppleNvmemWriteCell(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle,
    _In_ ULONG Value);

ULONG
NTAPI
AppleNvmemCellValue(
    _In_ PAPPLE_NVMEM_CELL Cell);
