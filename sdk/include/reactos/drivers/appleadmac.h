/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon audio DMA controller (ADMAC)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/fdtdrv.h>

typedef struct _APPLE_ADMAC
{
    PUCHAR Registers;
    ULONG Size;
    ULONG ChannelCount;
    ULONG InterruptIndex;
    ULONG InterruptGsi;
    BOOLEAN InterruptEdge;
    BOOLEAN SetFifoLimits;
    ULONG SramUsed[2];
} APPLE_ADMAC, *PAPPLE_ADMAC;

typedef struct _APPLE_ADMAC_CHANNEL
{
    PAPPLE_ADMAC Admac;
    ULONG Number;
    ULONG Carveout;
    ULONG64 Buffer;
    ULONG BufferLength;
    ULONG PeriodLength;
    ULONG Submitted;
    ULONG Reclaimed;
} APPLE_ADMAC_CHANNEL, *PAPPLE_ADMAC_CHANNEL;

BOOLEAN
NTAPI
HalArm64TranslateDeviceTreeInterrupt(
    _In_reads_bytes_(CompatibleLength) const CHAR *Compatible,
    _In_ ULONG CompatibleLength,
    _In_reads_(CellCount) const ULONG *Cells,
    _In_ ULONG CellCount,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

NTSTATUS
NTAPI
AppleAdmacQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PULONG InterruptIndex,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

NTSTATUS
NTAPI
AppleAdmacOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_ADMAC Admac);

VOID
NTAPI
AppleAdmacClose(
    _Inout_ PAPPLE_ADMAC Admac);

NTSTATUS
NTAPI
AppleAdmacOpenChannel(
    _Inout_ PAPPLE_ADMAC Admac,
    _In_ ULONG Number,
    _In_ ULONG WordBytes,
    _In_ ULONG FrameWords,
    _Out_ PAPPLE_ADMAC_CHANNEL Channel);

VOID
NTAPI
AppleAdmacCloseChannel(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel);

NTSTATUS
NTAPI
AppleAdmacStartCyclic(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength);

VOID
NTAPI
AppleAdmacStop(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel);

BOOLEAN
NTAPI
AppleAdmacInterruptPending(
    _In_ PAPPLE_ADMAC Admac,
    _In_ ULONG Number,
    _Out_ PBOOLEAN Global);

ULONG
NTAPI
AppleAdmacService(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel);

ULONG
NTAPI
AppleAdmacPosition(
    _In_ PAPPLE_ADMAC_CHANNEL Channel);
