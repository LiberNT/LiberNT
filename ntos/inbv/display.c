/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Who owns the boot display and what gets drawn on it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntimage.h>
#include "inbvp.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

BOOLEAN InbvBootDriverInstalled = FALSE;

/* PRIVATE DATA ***************************************************************/

/* Who holds the display right now */
static INBV_DISPLAY_STATE InbvDisplayState = INBV_DISPLAY_STATE_LOST;

/* Whether InbvDisplayString is allowed to put anything on the screen */
static BOOLEAN InbvDisplayStringEnabled = FALSE;

static BOOLEAN InbvResetDisplayPending = FALSE;

/* What a display driver asked us to call when it takes the display away */
static INBV_RESET_DISPLAY_PARAMETERS InbvResetDisplayCallback = NULL;

/* What somebody asked to look at every string before it is drawn */
static INBV_DISPLAY_STRING_FILTER InbvDisplayFilter = NULL;

/* The lock around the display, and the level the holder came in at */
static KSPIN_LOCK InbvLock;
static KIRQL InbvOldIrql;

/* The screen as it was when the snapshot was taken, and the room for it */
static PUCHAR InbvSnapshotBuffer = NULL;
static ULONG InbvSnapshotWidth = 0;
static ULONG InbvSnapshotHeight = 0;
static ULONG InbvSnapshotStride = 0;
static BOOLEAN InbvSnapshotTaken = FALSE;

/* What the boot loader said about the firmware framebuffer */
static LOADER_PARAMETER_FRAMEBUFFER InbvGopFrameBuffer;
static BOOLEAN InbvGopFrameBufferValid = FALSE;
static LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM InbvGopTransform;
static BOOLEAN InbvGopTransformValid = FALSE;
static LOADER_PARAMETER_BGRT InbvBgrtInfo;
static BOOLEAN InbvBgrtInfoValid = FALSE;

/* The resource kind a bitmap is filed under, as a PE files it */
#define INBV_RESOURCE_TYPE_BITMAP 2

#define INBV_RESOURCE_LANGUAGE_NEUTRAL 0

#define INBV_TAG 'vbnI'

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
InbvCanDraw(VOID)
{
    return InbvBootDriverInstalled &&
           InbvDisplayState == INBV_DISPLAY_STATE_OWNED;
}

/* Puts the mode back the way the boot video driver wants it, once */
static
VOID
InbvApplyPendingReset(VOID)
{
    if (!InbvResetDisplayPending) return;

    InbvResetDisplayPending = FALSE;
    VidResetDisplay(TRUE);
}

static
PUCHAR
InbvFindResource(
    _In_ ULONG ResourceNumber)
{
    PIMAGE_RESOURCE_DATA_ENTRY DataEntry;
    LDR_RESOURCE_INFO Info;
    NTSTATUS Status;
    PVOID Data = NULL;
    ULONG Size = 0;

    if (PsNtosImageBase == 0) return NULL;

    Info.Type = INBV_RESOURCE_TYPE_BITMAP;
    Info.Name = ResourceNumber;
    Info.Language = INBV_RESOURCE_LANGUAGE_NEUTRAL;

    Status = LdrFindResource_U((PVOID)PsNtosImageBase, &Info, 3, &DataEntry);
    if (!NT_SUCCESS(Status))
    {
        Info.Language = 0;
        Status = LdrFindResource_U((PVOID)PsNtosImageBase, &Info, 2, &DataEntry);
    }

    if (!NT_SUCCESS(Status)) return NULL;

    Status = LdrAccessResource((PVOID)PsNtosImageBase, DataEntry, &Data, &Size);
    if (!NT_SUCCESS(Status) || Size == 0) return NULL;

    return (PUCHAR)Data;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvDriverInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Count)
{
    PLOADER_PARAMETER_EXTENSION Extension;
    ULONG Attempt;

    KeInitializeSpinLock(&InbvLock);

    if (LoaderBlock != NULL && LoaderBlock->Extension != NULL)
    {
        Extension = LoaderBlock->Extension;

        if (Extension->GopFramebuffer.FrameBufferBase.QuadPart != 0 &&
            Extension->GopFramebuffer.HorizontalResolution != 0 &&
            Extension->GopFramebuffer.VerticalResolution != 0)
        {
            InbvGopFrameBuffer = Extension->GopFramebuffer;
            InbvGopFrameBufferValid = TRUE;
        }

        if (Extension->GopFramebufferTransform.Size >=
                sizeof(LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM) &&
            Extension->GopFramebufferTransform.Version ==
                LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM_VERSION)
        {
            InbvGopTransform = Extension->GopFramebufferTransform;
            InbvGopTransformValid = TRUE;
        }

        if (Extension->BgrtInfo.Valid)
        {
            InbvBgrtInfo = Extension->BgrtInfo;
            InbvBgrtInfoValid = TRUE;
        }
    }

    for (Attempt = 0; Attempt <= Count; Attempt++)
    {
        if (!VidInitialize(FALSE)) continue;

        InbvBootDriverInstalled = TRUE;
        InbvDisplayState = INBV_DISPLAY_STATE_OWNED;
        InbvDisplayStringEnabled = TRUE;
        return TRUE;
    }

    InbvBootDriverInstalled = FALSE;
    InbvDisplayState = INBV_DISPLAY_STATE_LOST;
    return FALSE;
}

VOID
NTAPI
InbvAcquireLock(VOID)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    if (OldIrql < DISPATCH_LEVEL) KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    KeAcquireSpinLockAtDpcLevel(&InbvLock);
    InbvOldIrql = OldIrql;
}

VOID
NTAPI
InbvReleaseLock(VOID)
{
    KIRQL OldIrql = InbvOldIrql;

    KeReleaseSpinLockFromDpcLevel(&InbvLock);

    if (OldIrql < DISPATCH_LEVEL) KeLowerIrql(OldIrql);
}

/* Says whether the boot video driver gets to draw at all */
VOID
NTAPI
InbvEnableBootDriver(
    _In_ BOOLEAN Enable)
{
    if (InbvDisplayState == INBV_DISPLAY_STATE_LOST) return;

    InbvAcquireLock();

    if (Enable)
    {
        InbvDisplayState = INBV_DISPLAY_STATE_OWNED;
        InbvResetDisplayPending = TRUE;
    }
    else
    {
        InbvDisplayState = INBV_DISPLAY_STATE_DISABLED;
        VidCleanUp();
    }

    InbvReleaseLock();
}

BOOLEAN
NTAPI
InbvIsBootDriverInstalled(VOID)
{
    return InbvBootDriverInstalled;
}

INBV_DISPLAY_STATE
NTAPI
InbvGetDisplayState(VOID)
{
    return InbvDisplayState;
}

BOOLEAN
NTAPI
InbvCheckDisplayOwnership(VOID)
{
    return InbvDisplayState != INBV_DISPLAY_STATE_LOST;
}

VOID
NTAPI
InbvSetDisplayOwnership(
    _In_ BOOLEAN DisplayOwned)
{
    if (DisplayOwned)
    {
        InbvDisplayState = INBV_DISPLAY_STATE_OWNED;
        InbvResetDisplayPending = TRUE;
        return;
    }

    InbvDisplayState = INBV_DISPLAY_STATE_LOST;
    InbvResetDisplayPending = FALSE;
}

VOID
NTAPI
InbvAcquireDisplayOwnership(VOID)
{
    INBV_RESET_DISPLAY_PARAMETERS Callback = InbvResetDisplayCallback;
    VID_DISPLAY_INFO DisplayInfo;

    if (Callback != NULL && !KeBugCheckActive)
    {
        if (VidQueryDisplayInfo(&DisplayInfo))
        {
            Callback(DisplayInfo.Width / DisplayInfo.CharacterWidth,
                     DisplayInfo.Height / DisplayInfo.CharacterHeight);
        }
    }

    InbvSetDisplayOwnership(TRUE);
}

VOID
NTAPI
InbvNotifyDisplayOwnershipLost(
    _In_ INBV_RESET_DISPLAY_PARAMETERS Callback)
{
    InbvAcquireLock();

    InbvResetDisplayCallback = Callback;
    InbvSetDisplayOwnership(FALSE);

    InbvReleaseLock();
}

VOID
NTAPI
InbvNotifyDisplayOwnershipChange(
    _In_ BOOLEAN DisplayOwned)
{
    InbvAcquireLock();
    InbvSetDisplayOwnership(DisplayOwned);
    InbvReleaseLock();
}

/* Whether a drawing call still has to program the mode before it draws */
BOOLEAN
NTAPI
InbvDisplayNeedsDriverReset(VOID)
{
    return InbvResetDisplayPending;
}

/* Programs the boot video mode again and tells the caller whether it ran */
BOOLEAN
NTAPI
InbvResetDisplay(VOID)
{
    if (!InbvCanDraw()) return FALSE;

    InbvResetDisplayPending = FALSE;
    VidResetDisplay(TRUE);
    return TRUE;
}

/* Says whether strings go to the screen, and what the answer was before */
BOOLEAN
NTAPI
InbvEnableDisplayString(
    _In_ BOOLEAN Enable)
{
    BOOLEAN Previous = InbvDisplayStringEnabled;

    InbvDisplayStringEnabled = Enable;
    return Previous;
}

/* Names what gets to look at every string before the screen sees it */
VOID
NTAPI
InbvInstallDisplayStringFilter(
    _In_ INBV_DISPLAY_STRING_FILTER DisplayFilter)
{
    InbvDisplayFilter = DisplayFilter;
}

BOOLEAN
NTAPI
InbvDisplayString(
    _In_ PCSTR String)
{
    INBV_DISPLAY_STRING_FILTER Filter = InbvDisplayFilter;

    if (Filter != NULL)
    {
        Filter(&String);
        if (String == NULL) return TRUE;
    }

    if (!InbvDisplayStringEnabled) return FALSE;
    if (!InbvCanDraw()) return FALSE;

    InbvAcquireLock();
    InbvApplyPendingReset();
    VidDisplayString(String);
    InbvReleaseLock();

    return TRUE;
}

/* What the boot video driver is showing: its size, its cell and its density */
BOOLEAN
NTAPI
InbvQueryDisplayInfo(
    _Out_ PVID_DISPLAY_INFO DisplayInfo)
{
    if (!InbvBootDriverInstalled) return FALSE;

    return VidQueryDisplayInfo(DisplayInfo);
}

/* Narrows the font so more of a line fits across the screen */
VOID
NTAPI
InbvUseCompactFont(VOID)
{
    if (!InbvBootDriverInstalled) return;

    VidUseCompactFont();
}

VOID
NTAPI
InbvSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom)
{
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    VidSetScrollRegion(Left, Top, Right, Bottom);
    InbvReleaseLock();
}

VOID
NTAPI
InbvSetTextColor(
    _In_ ULONG Color)
{
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    VidSetTextColor(Color);
    InbvReleaseLock();
}

VOID
NTAPI
InbvSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ ULONG Color)
{
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    InbvApplyPendingReset();
    VidSolidColorFill(Left, Top, Right, Bottom, (UCHAR)Color);
    InbvReleaseLock();
}

/* Draws a device-independent bitmap with its top left corner at X, Y */
VOID
NTAPI
InbvBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y)
{
    if (Buffer == NULL) return;
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    InbvApplyPendingReset();
    VidBitBlt(Buffer, X, Y);
    InbvReleaseLock();
}

/* Copies a block of screen-shaped pixels from the caller onto the screen */
VOID
NTAPI
InbvBufferToScreenBlt(
    _In_reads_bytes_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride)
{
    if (Buffer == NULL || Width == 0 || Height == 0) return;
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    InbvApplyPendingReset();
    VidBufferToScreenBlt(Buffer, X, Y, Width, Height, Stride);
    InbvReleaseLock();
}

/* Copies a block of the screen back out to the caller */
VOID
NTAPI
InbvScreenToBufferBlt(
    _Out_writes_bytes_all_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride)
{
    if (Buffer == NULL || Width == 0 || Height == 0) return;

    if (!InbvCanDraw())
    {
        RtlZeroMemory(Buffer, (SIZE_T)Height * Stride);
        return;
    }

    InbvAcquireLock();
    VidScreenToBufferBlt(Buffer, X, Y, Width, Height, Stride);
    InbvReleaseLock();
}

PUCHAR
NTAPI
InbvGetResourceAddress(
    _In_ ULONG ResourceNumber)
{
    return InbvFindResource(ResourceNumber);
}

VOID
NTAPI
InbvReserveDisplaySnapshot(VOID)
{
    VID_DISPLAY_INFO DisplayInfo;
    ULONG Stride;
    ULONG Size;
    PUCHAR Buffer;

    if (InbvSnapshotBuffer != NULL) return;
    if (!InbvBootDriverInstalled) return;
    if (KeGetCurrentIrql() > APC_LEVEL) return;

    if (!VidQueryDisplayInfo(&DisplayInfo)) return;
    if (DisplayInfo.Width == 0 || DisplayInfo.Height == 0) return;

    Stride = DisplayInfo.Width;
    Size = Stride * DisplayInfo.Height;

    Buffer = ExAllocatePoolZero(NonPagedPool, Size, INBV_TAG);
    if (Buffer == NULL) return;

    InbvSnapshotBuffer = Buffer;
    InbvSnapshotWidth = DisplayInfo.Width;
    InbvSnapshotHeight = DisplayInfo.Height;
    InbvSnapshotStride = Stride;
    InbvSnapshotTaken = FALSE;
}

/* Keeps what is on the screen right now in the room already put aside */
VOID
NTAPI
InbvSaveDisplaySnapshot(VOID)
{
    if (InbvSnapshotBuffer == NULL) return;
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    VidScreenToBufferBlt(InbvSnapshotBuffer,
                         0,
                         0,
                         InbvSnapshotWidth,
                         InbvSnapshotHeight,
                         InbvSnapshotStride);
    InbvReleaseLock();

    InbvSnapshotTaken = TRUE;
}

/* Puts the kept screen back */
VOID
NTAPI
InbvRestoreDisplaySnapshot(VOID)
{
    if (InbvSnapshotBuffer == NULL || !InbvSnapshotTaken) return;
    if (!InbvCanDraw()) return;

    InbvAcquireLock();
    InbvApplyPendingReset();
    VidBufferToScreenBlt(InbvSnapshotBuffer,
                         0,
                         0,
                         InbvSnapshotWidth,
                         InbvSnapshotHeight,
                         InbvSnapshotStride);
    InbvReleaseLock();
}

/* What the loader found as the firmware framebuffer, if it found one */
BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo)
{
    if (FrameBufferInfo == NULL) return FALSE;

    if (InbvBootDriverInstalled && VidQueryFrameBufferInfo(FrameBufferInfo))
    {
        return TRUE;
    }

    if (!InbvGopFrameBufferValid) return FALSE;

    *FrameBufferInfo = InbvGopFrameBuffer;
    return TRUE;
}

/* How logical display coordinates sit on that framebuffer, if it is turned */
BOOLEAN
NTAPI
InbvGetGopFrameBufferTransform(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER_TRANSFORM Transform)
{
    if (Transform == NULL || !InbvGopTransformValid) return FALSE;

    *Transform = InbvGopTransform;
    return TRUE;
}

/* Whether there is a firmware framebuffer worth drawing into */
BOOLEAN
NTAPI
InbvHasValidGopFrameBuffer(VOID)
{
    LOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo;

    if (!InbvGetGopFrameBufferInfo(&FrameBufferInfo)) return FALSE;

    return FrameBufferInfo.FrameBufferBase.QuadPart != 0 &&
           FrameBufferInfo.HorizontalResolution != 0 &&
           FrameBufferInfo.VerticalResolution != 0;
}

/* What the firmware said about the logo it drew before the kernel started */
CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvQueryBgrtInfo(
    _Out_opt_ PLOADER_PARAMETER_BGRT BgrtInfo)
{
    if (!InbvBgrtInfoValid) return FALSE;

    if (BgrtInfo != NULL) *BgrtInfo = InbvBgrtInfo;

    return TRUE;
}

NTSTATUS
NTAPI
InbvSetVirtualFrameBuffer(
    _In_opt_ PVOID VirtualFrameBuffer)
{
    if (!InbvBootDriverInstalled) return STATUS_DEVICE_NOT_READY;

    return VidSetVirtualFrameBuffer(VirtualFrameBuffer);
}

NTSTATUS
NTAPI
NtDisplayString(
    _In_ PUNICODE_STRING DisplayString)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING Captured;
    OEM_STRING Oem;
    NTSTATUS Status;
    PWSTR Copy = NULL;
    ULONG Length;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(DisplayString, sizeof(*DisplayString), sizeof(ULONG));
            Captured = *DisplayString;
            ProbeForRead(Captured.Buffer, Captured.Length, sizeof(WCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        if (Captured.Length == 0) return STATUS_SUCCESS;

        Copy = ExAllocatePoolZero(PagedPool, Captured.Length, INBV_TAG);
        if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            RtlCopyMemory(Copy, Captured.Buffer, Captured.Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Copy, INBV_TAG);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        Captured.Buffer = Copy;
        Captured.MaximumLength = Captured.Length;
    }
    else
    {
        if (DisplayString == NULL) return STATUS_INVALID_PARAMETER;
        Captured = *DisplayString;
        if (Captured.Length == 0) return STATUS_SUCCESS;
    }

    Length = RtlxUnicodeStringToOemSize(&Captured);

    Oem.Length = 0;
    Oem.MaximumLength = 0;
    Oem.Buffer = ExAllocatePoolZero(PagedPool, Length, INBV_TAG);

    if (Oem.Buffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        Oem.MaximumLength = (USHORT)Length;
        Status = RtlUnicodeStringToOemString(&Oem, &Captured, FALSE);
    }

    if (NT_SUCCESS(Status))
    {
        InbvDisplayString(Oem.Buffer);
        HeadlessDispatch(HeadlessCmdPutString,
                         Oem.Buffer,
                         Oem.Length + sizeof(CHAR),
                         NULL,
                         NULL);
    }

    if (Oem.Buffer != NULL) ExFreePoolWithTag(Oem.Buffer, INBV_TAG);
    if (Copy != NULL) ExFreePoolWithTag(Copy, INBV_TAG);

    return Status;
}

/* EOF */
