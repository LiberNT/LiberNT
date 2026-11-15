/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     Boot video and boot screen internals
 * COPYRIGHT:   Copyright 2007 Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2019-2022 Hermès Bélusca-Maïto
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#include "bootvid/bootvid.h"

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvDriverInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count
);

extern BOOLEAN InbvBootDriverInstalled;

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvQueryBgrtInfo(
    _Out_opt_ PLOADER_PARAMETER_BGRT BgrtInfo
);

VOID
NTAPI
InbvAcquireLock(VOID);

VOID
NTAPI
InbvBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y);

BOOLEAN
NTAPI
InbvDisplayNeedsDriverReset(VOID);

INBV_DISPLAY_STATE
NTAPI
InbvGetDisplayState(VOID);

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

BOOLEAN
NTAPI
InbvGetGopFrameBufferTransform(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER_TRANSFORM Transform);

PUCHAR
NTAPI
InbvGetResourceAddress(
    _In_ ULONG ResourceNumber);

BOOLEAN
NTAPI
InbvHasValidGopFrameBuffer(VOID);

BOOLEAN
NTAPI
InbvQueryDisplayInfo(
    _Out_ PVID_DISPLAY_INFO DisplayInfo);

VOID
NTAPI
InbvReleaseLock(VOID);

VOID
NTAPI
InbvReserveDisplaySnapshot(VOID);

VOID
NTAPI
InbvRestoreDisplaySnapshot(VOID);

VOID
NTAPI
InbvSaveDisplaySnapshot(VOID);

VOID
NTAPI
InbvUseCompactFont(VOID);

VOID
NTAPI
InbvIndicateProgress(VOID);

VOID
NTAPI
InbvPortEnableFifo(
    _In_ ULONG PortId,
    _In_ BOOLEAN Enable);

BOOLEAN
NTAPI
InbvPortGetByte(
    _In_ ULONG PortId,
    _Out_ PUCHAR Byte);

BOOLEAN
NTAPI
InbvPortInitialize(
    _In_ ULONG BaudRate,
    _In_ ULONG PortNumber,
    _In_ PUCHAR PortAddress,
    _Out_ PULONG PortId,
    _In_ BOOLEAN IsMMIODevice);

BOOLEAN
NTAPI
InbvPortPollOnly(
    _In_ ULONG PortId);

VOID
NTAPI
InbvPortPutByte(
    _In_ ULONG PortId,
    _In_ UCHAR Byte);

VOID
NTAPI
InbvPortTerminate(
    _In_ ULONG PortId);

VOID
NTAPI
InbvBufferToScreenBlt(
    _In_reads_bytes_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride);

VOID
NTAPI
InbvScreenToBufferBlt(
    _Out_writes_bytes_all_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride);
