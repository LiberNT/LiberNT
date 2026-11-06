/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable KMT-to-miniport Present contract helpers
 */

#pragma once

#include <ntddk.h>
#include <windef.h>

#define DXGK_PRESENT_CORE_MAX_SUBRECTS 64U

typedef struct _DXGK_PRESENT_SCANOUT_RETIRE_POLICY
{
    BOOLEAN RefreshSharedPrimary;
    BOOLEAN ProgramSource;
    BOOLEAN HoldSharedSurfaceRundown;
} DXGK_PRESENT_SCANOUT_RETIRE_POLICY,
  *PDXGK_PRESENT_SCANOUT_RETIRE_POLICY;

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
/*
 * Sync refresh counts are public 32-bit serial numbers.  Targets are expected
 * to be within one signed 32-bit interval of the current count; this comparison
 * remains correct when the counter wraps through zero.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreRefreshTargetReached(
    _In_ ULONG CurrentRefreshCount,
    _In_ ULONG TargetRefreshCount)
{
    return (LONG)(CurrentRefreshCount - TargetRefreshCount) >= 0;
}

/*
 * A zero target is the native "next vertical blank" sentinel.  Even when the
 * current count is also zero it must remain pending until a vblank pulse
 * evaluates the target.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreRefreshTargetShouldSignalImmediately(
    _In_ ULONG CurrentRefreshCount,
    _In_ ULONG TargetRefreshCount)
{
    return TargetRefreshCount != 0 &&
           DxgkPresentCoreRefreshTargetReached(CurrentRefreshCount,
                                               TargetRefreshCount);
}
#endif

FORCEINLINE
NTSTATUS
DxgkPresentCoreEvaluateScanoutRetirement(
    _In_ BOOLEAN Flip,
    _In_ BOOLEAN WritesDestination,
    _In_ BOOLEAN HasSource,
    _In_ BOOLEAN HasDestination,
    _In_ BOOLEAN DestinationIsSharedPrimary,
    _Out_ PDXGK_PRESENT_SCANOUT_RETIRE_POLICY Policy)
{
    if (Policy == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Policy, sizeof(*Policy));
    if (Flip)
    {
        if (!HasSource || HasDestination)
            return STATUS_INVALID_PARAMETER;
        Policy->ProgramSource = TRUE;
        Policy->HoldSharedSurfaceRundown = TRUE;
    }
    else if (WritesDestination &&
             HasDestination &&
             DestinationIsSharedPrimary)
    {
        Policy->RefreshSharedPrimary = TRUE;
        Policy->HoldSharedSurfaceRundown = TRUE;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectValid(
    _In_ const RECT *Rect)
{
    return Rect != NULL &&
           Rect->left < Rect->right &&
           Rect->top < Rect->bottom;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectEqual(
    _In_ const RECT *Left,
    _In_ const RECT *Right)
{
    return Left != NULL &&
           Right != NULL &&
           Left->left == Right->left &&
           Left->top == Right->top &&
           Left->right == Right->right &&
           Left->bottom == Right->bottom;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectContained(
    _In_ const RECT *Outer,
    _In_ const RECT *Inner)
{
    return DxgkPresentCoreRectValid(Outer) &&
           DxgkPresentCoreRectValid(Inner) &&
           Inner->left >= Outer->left &&
           Inner->top >= Outer->top &&
           Inner->right <= Outer->right &&
           Inner->bottom <= Outer->bottom;
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreMapCoordinate(
    _In_ LONG SourceCoordinate,
    _In_ LONG SourceOrigin,
    _In_ ULONGLONG SourceExtent,
    _In_ LONG DestinationOrigin,
    _In_ ULONGLONG DestinationExtent,
    _Out_ PLONG DestinationCoordinate)
{
    ULONGLONG SourceOffset;
    ULONGLONG ScaledOffset;
    LONGLONG Result;

    if (DestinationCoordinate == NULL ||
        SourceCoordinate < SourceOrigin ||
        SourceExtent == 0 ||
        DestinationExtent == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SourceOffset =
        (ULONGLONG)((LONGLONG)SourceCoordinate - (LONGLONG)SourceOrigin);
    if (SourceOffset > SourceExtent ||
        (SourceOffset != 0 &&
         DestinationExtent > MAXULONGLONG / SourceOffset))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ScaledOffset = SourceOffset * DestinationExtent;
    if ((ScaledOffset % SourceExtent) != 0)
        return STATUS_NOT_SUPPORTED;

    ScaledOffset /= SourceExtent;
    if (ScaledOffset > (ULONGLONG)MAXLONGLONG)
        return STATUS_INVALID_PARAMETER;
    Result = (LONGLONG)DestinationOrigin + (LONGLONG)ScaledOffset;
    if (Result < (LONG)MINLONG || Result > MAXLONG)
        return STATUS_INVALID_PARAMETER;

    *DestinationCoordinate = (LONG)Result;
    return STATUS_SUCCESS;
}

/*
 * D3DKMT_PRESENT supplies source-space subrectangles, whereas
 * DXGKARG_PRESENT requires destination-space subrectangles.  Only mappings
 * whose four edges are integral can be represented exactly by RECT.
 */
FORCEINLINE
NTSTATUS
DxgkPresentCoreMapSourceSubRects(
    _In_ const RECT *SourceRect,
    _In_ const RECT *DestinationRect,
    _In_reads_(SubRectCount) const RECT *SourceSubRects,
    _In_ UINT SubRectCount,
    _Out_writes_(SubRectCapacity) RECT *DestinationSubRects,
    _In_ UINT SubRectCapacity)
{
    ULONGLONG SourceWidth;
    ULONGLONG SourceHeight;
    ULONGLONG DestinationWidth;
    ULONGLONG DestinationHeight;
    NTSTATUS Status;
    UINT Index;

    if (!DxgkPresentCoreRectValid(SourceRect) ||
        !DxgkPresentCoreRectValid(DestinationRect) ||
        SourceSubRects == NULL ||
        DestinationSubRects == NULL ||
        SubRectCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SubRectCount > DXGK_PRESENT_CORE_MAX_SUBRECTS ||
        SubRectCount > SubRectCapacity)
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        if (!DxgkPresentCoreRectContained(SourceRect,
                                         &SourceSubRects[Index]))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    SourceWidth = (ULONGLONG)
        ((LONGLONG)SourceRect->right - (LONGLONG)SourceRect->left);
    SourceHeight = (ULONGLONG)
        ((LONGLONG)SourceRect->bottom - (LONGLONG)SourceRect->top);
    DestinationWidth = (ULONGLONG)
        ((LONGLONG)DestinationRect->right -
         (LONGLONG)DestinationRect->left);
    DestinationHeight = (ULONGLONG)
        ((LONGLONG)DestinationRect->bottom -
         (LONGLONG)DestinationRect->top);

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].left,
                     SourceRect->left,
                     SourceWidth,
                     DestinationRect->left,
                     DestinationWidth,
                     &DestinationSubRects[Index].left);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].top,
                     SourceRect->top,
                     SourceHeight,
                     DestinationRect->top,
                     DestinationHeight,
                     &DestinationSubRects[Index].top);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].right,
                     SourceRect->left,
                     SourceWidth,
                     DestinationRect->left,
                     DestinationWidth,
                     &DestinationSubRects[Index].right);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].bottom,
                     SourceRect->top,
                     SourceHeight,
                     DestinationRect->top,
                     DestinationHeight,
                     &DestinationSubRects[Index].bottom);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreCopyDestinationSubRects(
    _In_ const RECT *DestinationRect,
    _In_reads_(SubRectCount) const RECT *InputSubRects,
    _In_ UINT SubRectCount,
    _Out_writes_(SubRectCapacity) RECT *OutputSubRects,
    _In_ UINT SubRectCapacity)
{
    UINT Index;

    if (!DxgkPresentCoreRectValid(DestinationRect) ||
        InputSubRects == NULL ||
        OutputSubRects == NULL ||
        SubRectCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SubRectCount > DXGK_PRESENT_CORE_MAX_SUBRECTS ||
        SubRectCount > SubRectCapacity)
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        if (!DxgkPresentCoreRectContained(DestinationRect,
                                         &InputSubRects[Index]))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    RtlCopyMemory(OutputSubRects,
                  InputSubRects,
                  (SIZE_T)SubRectCount * sizeof(OutputSubRects[0]));
    return STATUS_SUCCESS;
}

/* Clip [*Low1, *High1] to [Low, High] and shrink [*Low2, *High2], which maps onto [Low, High], by the clipped share. */
FORCEINLINE
VOID
DxgkPresentCoreClipIntervals(
    _Inout_ LONG *Low1,
    _Inout_ LONG *High1,
    _Inout_ LONG *Low2,
    _Inout_ LONG *High2,
    _In_ LONG Low,
    _In_ LONG High)
{
    LONGLONG Length = (LONGLONG)High - Low;
    LONGLONG Length2;

    if (*Low1 < Low)
        *Low1 = Low;
    if (*High1 > High)
        *High1 = High;
    if ((LONGLONG)*High1 - *Low1 == Length || Length <= 0)
        return;
    Length2 = (LONGLONG)*High2 - *Low2;
    if (*Low1 > Low)
        *Low2 += (LONG)((Length + ((LONGLONG)*Low1 - Low) * Length2 - 1) / Length);
    if (*High1 < High)
        *High2 -= (LONG)((((LONGLONG)High - *High1) * Length2) / Length);
    if (*Low2 == *High2)
    {
        if ((LONGLONG)*Low1 - Low < (LONGLONG)High - *High1)
            *High2 += 1;
        else
            *Low2 -= 1;
    }
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreIntersectRect(
    _Out_ RECT *Result,
    _In_ const RECT *First,
    _In_ const RECT *Second)
{
    RECT Rect;

    Rect.left = max(First->left, Second->left);
    Rect.top = max(First->top, Second->top);
    Rect.right = min(First->right, Second->right);
    Rect.bottom = min(First->bottom, Second->bottom);
    if (Rect.left >= Rect.right || Rect.top >= Rect.bottom)
        return FALSE;
    *Result = Rect;
    return TRUE;
}

/*
 * Source and destination of a window Blt present.  The source starts as the
 * whole source allocation and the destination as the whole window client; a
 * valid SrcRect or DstRect (client relative) clips its side, and the clipped
 * part maps proportionally onto the other side.  FALSE: nothing to present.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreComputeBltRects(
    _In_ ULONG SourceWidth,
    _In_ ULONG SourceHeight,
    _In_ ULONG ClientWidth,
    _In_ ULONG ClientHeight,
    _In_opt_ const RECT *SrcRect,
    _In_opt_ const RECT *DstRect,
    _Out_ RECT *Source,
    _Out_ RECT *Destination)
{
    RECT Src = {0, 0, (LONG)SourceWidth, (LONG)SourceHeight};
    RECT Dst = {0, 0, (LONG)ClientWidth, (LONG)ClientHeight};

    if (SourceWidth == 0 || SourceHeight == 0 || ClientWidth == 0 || ClientHeight == 0 ||
        SourceWidth > MAXLONG || SourceHeight > MAXLONG || ClientWidth > MAXLONG || ClientHeight > MAXLONG)
        return FALSE;
    if (SrcRect != NULL &&
        (Src.bottom <= SrcRect->top || SrcRect->bottom <= Src.top ||
         Src.right <= SrcRect->left || SrcRect->right <= Src.left ||
         SrcRect->bottom <= SrcRect->top || SrcRect->right <= SrcRect->left))
        return FALSE;
    if (DstRect != NULL &&
        (Dst.bottom <= DstRect->top || DstRect->bottom < 1 ||
         Dst.right <= DstRect->left || DstRect->right < 1 ||
         DstRect->bottom <= DstRect->top || DstRect->right <= DstRect->left))
        return FALSE;

    if (SrcRect != NULL && DstRect != NULL)
    {
        LONGLONG ScaleX = (((LONGLONG)DstRect->right - DstRect->left) << 16) / ((LONGLONG)SrcRect->right - SrcRect->left);
        LONGLONG ScaleY = (((LONGLONG)DstRect->bottom - DstRect->top) << 16) / ((LONGLONG)SrcRect->bottom - SrcRect->top);
        LONGLONG MapLeft, MapTop, MapRight, MapBottom;
        RECT Clip, Mapped, Back;

        if (ScaleX == 0 || ScaleY == 0 || !DxgkPresentCoreIntersectRect(&Clip, SrcRect, &Src))
            return FALSE;
        MapLeft = ((LONGLONG)Clip.left - SrcRect->left) * ScaleX + ((LONGLONG)DstRect->left << 16);
        MapTop = ((LONGLONG)Clip.top - SrcRect->top) * ScaleY + ((LONGLONG)DstRect->top << 16);
        MapRight = ((LONGLONG)Clip.right - SrcRect->right) * ScaleX + ((LONGLONG)DstRect->right << 16);
        MapBottom = ((LONGLONG)Clip.bottom - SrcRect->bottom) * ScaleY + ((LONGLONG)DstRect->bottom << 16);
        Mapped.left = (LONG)((MapLeft + 0xffff) >> 16);
        Mapped.top = (LONG)((MapTop + 0xffff) >> 16);
        Mapped.right = (LONG)(MapRight >> 16);
        Mapped.bottom = (LONG)(MapBottom >> 16);
        if (!DxgkPresentCoreIntersectRect(&Dst, &Mapped, &Dst))
            return FALSE;
        Back.left = Clip.left + (LONG)((((LONGLONG)Dst.left << 16) - MapLeft + ScaleX - 1) / ScaleX);
        Back.top = Clip.top + (LONG)((((LONGLONG)Dst.top << 16) - MapTop + ScaleY - 1) / ScaleY);
        Back.right = Clip.right + (LONG)((((LONGLONG)Dst.right << 16) - MapRight) / ScaleX);
        Back.bottom = Clip.bottom + (LONG)((((LONGLONG)Dst.bottom << 16) - MapBottom) / ScaleY);
        if (!DxgkPresentCoreIntersectRect(&Src, &Back, &Src))
            return FALSE;
    }
    else if (SrcRect != NULL)
    {
        DxgkPresentCoreClipIntervals(&Src.top, &Src.bottom, &Dst.top, &Dst.bottom, SrcRect->top, SrcRect->bottom);
        DxgkPresentCoreClipIntervals(&Src.left, &Src.right, &Dst.left, &Dst.right, SrcRect->left, SrcRect->right);
    }
    else if (DstRect != NULL)
    {
        DxgkPresentCoreClipIntervals(&Dst.top, &Dst.bottom, &Src.top, &Src.bottom, DstRect->top, DstRect->bottom);
        DxgkPresentCoreClipIntervals(&Dst.left, &Dst.right, &Src.left, &Src.right, DstRect->left, DstRect->right);
    }
    *Source = Src;
    *Destination = Dst;
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreIsFullDestinationRegion(
    _In_ const RECT *DestinationRect,
    _In_reads_opt_(SubRectCount) const RECT *DestinationSubRects,
    _In_ UINT SubRectCount)
{
    return SubRectCount == 0 ||
           (SubRectCount == 1 &&
            DxgkPresentCoreRectEqual(DestinationRect,
                                     DestinationSubRects));
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreCompleteUnsupportedOverlayCreate(
    _In_ NTSTATUS DeviceValidationStatus,
    _Out_ PUINT OverlayHandle)
{
    if (OverlayHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *OverlayHandle = 0;
    return NT_SUCCESS(DeviceValidationStatus)
               ? STATUS_NOT_SUPPORTED
               : DeviceValidationStatus;
}

/*
 * The v1 MPO capability query may report TRUE only when both miniport DDIs
 * and both user-facing KMT execution paths exist.  Internal scan-out use of
 * the MPO DDI alone is not a user-mode MPO implementation.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreMpoV1Supported(
    _In_ ULONG ConfiguredLevel,
    _In_ BOOLEAN HasMiniportCheck,
    _In_ BOOLEAN HasMiniportPresent,
    _In_ BOOLEAN HasKmtCheck,
    _In_ BOOLEAN HasKmtPresent)
{
    return ConfiguredLevel >= 1300 &&
           HasMiniportCheck &&
           HasMiniportPresent &&
           HasKmtCheck &&
           HasKmtPresent;
}
