/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transaction state for POST display ownership handoff rollback
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifndef _DXGK_POSTDISPLAY_CORE_H_
#define _DXGK_POSTDISPLAY_CORE_H_

#include <ntddk.h>

typedef enum _DXGK_POST_DISPLAY_COMPLETION_ACTION
{
    DxgkPostDisplayCompletionNone = 0,
    DxgkPostDisplayCompletionCommit,
    DxgkPostDisplayCompletionRollback,
    DxgkPostDisplayCompletionDefer
} DXGK_POST_DISPLAY_COMPLETION_ACTION;

typedef struct _DXGK_POST_DISPLAY_HANDOFF_CORE
{
    BOOLEAN FallbackStopped;
} DXGK_POST_DISPLAY_HANDOFF_CORE, *PDXGK_POST_DISPLAY_HANDOFF_CORE;

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreArm(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core)
{
    if (Core->FallbackStopped)
        return FALSE;
    Core->FallbackStopped = TRUE;
    return TRUE;
}

FORCEINLINE
DXGK_POST_DISPLAY_COMPLETION_ACTION
DxgkPostDisplayCoreComplete(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core,
    _In_ NTSTATUS StartStatus,
    _In_ BOOLEAN Restartable)
{
    if (!Core->FallbackStopped)
        return DxgkPostDisplayCompletionNone;
    if (!NT_SUCCESS(StartStatus) && !Restartable)
        return DxgkPostDisplayCompletionDefer;
    Core->FallbackStopped = FALSE;
    if (!NT_SUCCESS(StartStatus))
        return DxgkPostDisplayCompletionRollback;
    return DxgkPostDisplayCompletionCommit;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreFirmwareScanout(
    _In_ BOOLEAN DisplayOnlyDriver,
    _In_ BOOLEAN VidPnCommitted)
{
    return DisplayOnlyDriver || !VidPnCommitted;
}

/* A CPU host aperture run inside the window the firmware framebuffer is seen through makes that window show other memory. */
FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreApertureRunTakesBootWindow(
    _In_ ULONGLONG ApertureBase,
    _In_ ULONG AperturePageSize,
    _In_ ULONG FirstPage,
    _In_ ULONG PageCount,
    _In_ ULONGLONG BootWindowBase,
    _In_ ULONGLONG BootWindowBytes)
{
    ULONGLONG RunBase;
    ULONGLONG RunBytes;

    if (AperturePageSize == 0 || PageCount == 0 || BootWindowBytes == 0)
        return FALSE;
    RunBytes = (ULONGLONG)PageCount * AperturePageSize;
    ASSERT((ULONGLONG)FirstPage * AperturePageSize <= MAXULONGLONG - ApertureBase);
    RunBase = ApertureBase + (ULONGLONG)FirstPage * AperturePageSize;
    ASSERT(RunBytes <= MAXULONGLONG - RunBase);
    ASSERT(BootWindowBytes <= MAXULONGLONG - BootWindowBase);
    return RunBase < BootWindowBase + BootWindowBytes && BootWindowBase < RunBase + RunBytes;
}

/* The firmware framebuffer ranges name a segment by its zero-based position, not by the segment id the other DDIs use. */
FORCEINLINE
USHORT
DxgkPostDisplayCoreUefiRangeSegment(
    _In_ ULONG SegmentId)
{
    ASSERT(SegmentId != 0);
    ASSERT(SegmentId - 1 <= MAXUSHORT);
    return (USHORT)(SegmentId - 1);
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreUefiRangeSegmentIndex(
    _In_ USHORT DriverSegmentId,
    _In_ ULONG SegmentCount,
    _Out_ PULONG SegmentIndex)
{
    *SegmentIndex = 0;
    if (DriverSegmentId >= SegmentCount)
        return FALSE;
    *SegmentIndex = DriverSegmentId;
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreUefiUnblockDeferred(
    _In_ KIRQL Irql)
{
    ASSERT(Irql <= DISPATCH_LEVEL);
    return Irql > APC_LEVEL;
}

#endif /* _DXGK_POSTDISPLAY_CORE_H_ */
