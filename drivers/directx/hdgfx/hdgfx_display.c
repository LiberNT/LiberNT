/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidPN, child device, pointer and scan-out DDIs
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

#define HDGFX_MAX_PATHS HDGFX_CHILD_COUNT

typedef struct _HDGFX_PATH
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DDDI_GAMMARAMP_TYPE GammaRampType;
    ULONG PinnedWidth;
    ULONG PinnedHeight;
    ULONG ModeIndex;
} HDGFX_PATH, *PHDGFX_PATH;

typedef struct _HDGFX_PATH_SET
{
    ULONG Count;
    BOOLEAN Overflow;
    HDGFX_PATH Paths[HDGFX_MAX_PATHS];
} HDGFX_PATH_SET, *PHDGFX_PATH_SET;

static PHDGFX_SCANOUT
HdgfxScanoutOf(
    _In_opt_ PVOID MiniportDeviceContext)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return NULL;
    return Adapter->Scanout;
}

static PHDGFX_SCANOUT
HdgfxScanoutAcquire(
    _In_opt_ PVOID MiniportDeviceContext,
    _Out_ PHDGFX_ADAPTER *Adapter)
{
    PHDGFX_ADAPTER Context = MiniportDeviceContext;

    *Adapter = NULL;
    if (Context == NULL || Context->Magic != HDGFX_ADAPTER_MAGIC || !ExAcquireRundownProtection(&Context->Rundown))
        return NULL;
    if (Context->Scanout == NULL)
    {
        ExReleaseRundownProtection(&Context->Rundown);
        return NULL;
    }
    *Adapter = Context;
    return Context->Scanout;
}

static VOID
HdgfxLockOutputs(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeWaitForSingleObject(&Scanout->OutputLock, Executive, KernelMode, FALSE, NULL);
}

static VOID
HdgfxUnlockOutputs(
    _In_ PHDGFX_SCANOUT Scanout)
{
    KeReleaseMutex(&Scanout->OutputLock, FALSE);
}

static BOOLEAN
HdgfxTargetKnown(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    return (TargetId == HDGFX_PANEL_CHILD_UID && Scanout->PanelPresent) ||
           (TargetId == HDGFX_CRT_CHILD_UID && Scanout->CrtSupported);
}

static ULONG
HdgfxTargetModes(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ const HDGFX_MODE **Modes)
{
    if (TargetId == HDGFX_CRT_CHILD_UID)
    {
        *Modes = Scanout->CrtModes;
        return Scanout->CrtModeCount;
    }
    *Modes = Scanout->Modes;
    return Scanout->ModeCount;
}

static VOID
HdgfxModeSignal(
    _In_ const HDGFX_MODE *Mode,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    RtlZeroMemory(Signal, sizeof(*Signal));
    Signal->VideoStandard = D3DKMDT_VSS_OTHER;
    Signal->TotalSize.cx = Mode->TotalWidth;
    Signal->TotalSize.cy = Mode->TotalHeight;
    Signal->ActiveSize.cx = Mode->Width;
    Signal->ActiveSize.cy = Mode->Height;
    Signal->VSyncFreq.Numerator = Mode->PixelRate;
    Signal->VSyncFreq.Denominator = Mode->TotalWidth * Mode->TotalHeight;
    Signal->HSyncFreq.Numerator = Mode->PixelRate;
    Signal->HSyncFreq.Denominator = Mode->TotalWidth;
    Signal->PixelRate = Mode->PixelRate;
    Signal->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

static BOOLEAN
HdgfxTargetOffersSize(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    const HDGFX_MODE *Modes;
    ULONG Count = HdgfxTargetModes(Scanout, TargetId, &Modes);
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Modes[Index].Width == Width && Modes[Index].Height == Height)
            return TRUE;
    }
    return FALSE;
}

static BOOLEAN
HdgfxFindSourceMode(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_opt_ PULONG ModeIndex)
{
    ULONG Index;

    for (Index = 0; Index < Scanout->ModeCount; Index++)
    {
        if (Scanout->Modes[Index].Width == Width && Scanout->Modes[Index].Height == Height)
        {
            if (ModeIndex != NULL)
                *ModeIndex = Index;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN
HdgfxSourceModeMatches(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ const D3DKMDT_VIDPN_SOURCE_MODE *Mode,
    _Out_opt_ PULONG ModeIndex)
{
    const D3DKMDT_GRAPHICS_RENDERING_FORMAT *Format = &Mode->Format.Graphics;

    return Mode->Type == D3DKMDT_RMT_GRAPHICS &&
           Format->VisibleRegionSize.cx == Format->PrimSurfSize.cx &&
           Format->VisibleRegionSize.cy == Format->PrimSurfSize.cy &&
           Format->PrimSurfSize.cx > 0 &&
           Format->Stride == (ULONG)Format->PrimSurfSize.cx * HDGFX_BYTES_PER_PIXEL &&
           (Format->PixelFormat == D3DDDIFMT_X8R8G8B8 || Format->PixelFormat == D3DDDIFMT_A8R8G8B8) &&
           Format->PixelValueAccessMode == D3DKMDT_PVAM_DIRECT &&
           HdgfxFindSourceMode(Scanout, Format->PrimSurfSize.cx, Format->PrimSurfSize.cy, ModeIndex);
}

static BOOLEAN
HdgfxTargetModeMatches(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ const D3DKMDT_VIDPN_TARGET_MODE *Mode,
    _Out_opt_ PULONG ModeIndex)
{
    const D3DDDI_RATIONAL *VSync = &Mode->VideoSignalInfo.VSyncFreq;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    const HDGFX_MODE *Modes;
    ULONG Count = HdgfxTargetModes(Scanout, TargetId, &Modes);
    ULONG Index;

    if (Mode->VideoSignalInfo.ScanLineOrdering != D3DDDI_VSSLO_PROGRESSIVE)
        return FALSE;

    for (Index = 0; Index < Count; Index++)
    {
        HdgfxModeSignal(&Modes[Index], &Signal);
        if (Signal.ActiveSize.cx != Mode->VideoSignalInfo.ActiveSize.cx ||
            Signal.ActiveSize.cy != Mode->VideoSignalInfo.ActiveSize.cy)
        {
            continue;
        }
        if (VSync->Numerator != 0 && VSync->Denominator != 0 &&
            (ULONGLONG)VSync->Numerator * Signal.VSyncFreq.Denominator !=
                (ULONGLONG)Signal.VSyncFreq.Numerator * VSync->Denominator)
        {
            continue;
        }
        if (ModeIndex != NULL)
            *ModeIndex = Index;
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN
HdgfxScalingPinned(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling)
{
    return Scaling == D3DKMDT_VPPS_IDENTITY || Scaling == D3DKMDT_VPPS_CENTERED ||
           Scaling == D3DKMDT_VPPS_STRETCHED || Scaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX ||
           Scaling == D3DKMDT_VPPS_CUSTOM;
}

static BOOLEAN
HdgfxRotationPinned(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    return Rotation == D3DKMDT_VPPR_IDENTITY || Rotation == D3DKMDT_VPPR_ROTATE90 ||
           Rotation == D3DKMDT_VPPR_ROTATE180 || Rotation == D3DKMDT_VPPR_ROTATE270;
}

static BOOLEAN
HdgfxTransformSupported(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    return Scaling == D3DKMDT_VPPS_IDENTITY && Rotation == D3DKMDT_VPPR_IDENTITY;
}

static NTSTATUS
HdgfxReleasePath(
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_ NTSTATUS Status)
{
    NTSTATUS ReleaseStatus = Topology->pfnReleasePathInfo(hTopology, Path);

    return NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus) ? ReleaseStatus : Status;
}

static NTSTATUS
HdgfxCollectPaths(
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _Out_ PHDGFX_PATH_SET Paths)
{
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath;
    NTSTATUS Status;

    RtlZeroMemory(Paths, sizeof(*Paths));
    Status = Topology->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    while (Path != NULL)
    {
        if (Paths->Count == HDGFX_MAX_PATHS)
        {
            Paths->Overflow = TRUE;
            return HdgfxReleasePath(Topology, hTopology, Path, STATUS_SUCCESS);
        }
        Paths->Paths[Paths->Count].SourceId = Path->VidPnSourceId;
        Paths->Paths[Paths->Count].TargetId = Path->VidPnTargetId;
        Paths->Paths[Paths->Count].Scaling = Path->ContentTransformation.Scaling;
        Paths->Paths[Paths->Count].Rotation = Path->ContentTransformation.Rotation;
        Paths->Paths[Paths->Count].GammaRampType = Path->GammaRamp.Type;
        Paths->Count++;

        NextPath = NULL;
        Status = Topology->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            return HdgfxReleasePath(Topology, hTopology, Path, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return HdgfxReleasePath(Topology, hTopology, Path, Status);
        Status = Topology->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(Status))
        {
            if (NextPath != NULL)
                Topology->pfnReleasePathInfo(hTopology, NextPath);
            return Status;
        }
        Path = NextPath;
    }
    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static BOOLEAN
HdgfxPathTargetsValid(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ const HDGFX_PATH_SET *Paths)
{
    ULONG Index;
    ULONG Other;

    if (Paths->Overflow)
        return FALSE;
    for (Index = 0; Index < Paths->Count; Index++)
    {
        if (Paths->Paths[Index].SourceId != 0 || !HdgfxTargetKnown(Scanout, Paths->Paths[Index].TargetId))
            return FALSE;
        for (Other = 0; Other < Index; Other++)
        {
            if (Paths->Paths[Other].TargetId == Paths->Paths[Index].TargetId)
                return FALSE;
        }
    }
    return TRUE;
}

static const HDGFX_PATH *
HdgfxFindPath(
    _In_ const HDGFX_PATH_SET *Paths,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    ULONG Index;

    for (Index = 0; Index < Paths->Count; Index++)
    {
        if (Paths->Paths[Index].TargetId == TargetId)
            return &Paths->Paths[Index];
    }
    return NULL;
}

static NTSTATUS
HdgfxPinnedSourceMode(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ BOOLEAN Required,
    _Out_ PBOOLEAN Supported,
    _Out_opt_ PULONG ModeIndex)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *Pinned = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPn->pfnAcquireSourceModeSet(hVidPn, 0, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSet == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned);
    if (NT_SUCCESS(Status))
    {
        if (Pinned == NULL)
            *Supported = !Required;
        else
            *Supported = HdgfxSourceModeMatches(Scanout, Pinned, ModeIndex);
        if (Required && Pinned == NULL)
            Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
    }
    if (Pinned != NULL)
    {
        ReleaseStatus = ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPn->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
HdgfxPinnedTargetMode(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ BOOLEAN Required,
    _Out_ PBOOLEAN Supported,
    _Out_opt_ PULONG ModeIndex)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *Pinned = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPn->pfnAcquireTargetModeSet(hVidPn, TargetId, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSet == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned);
    if (NT_SUCCESS(Status))
    {
        if (Pinned == NULL)
            *Supported = !Required;
        else
            *Supported = HdgfxTargetModeMatches(Scanout, TargetId, Pinned, ModeIndex);
        if (Required && Pinned == NULL)
            Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
    }
    if (Pinned != NULL)
    {
        ReleaseStatus = ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPn->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
HdgfxQueryVidPn(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _Out_ CONST DXGK_VIDPN_INTERFACE **VidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY *hTopology,
    _Out_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE **Topology)
{
    NTSTATUS Status;

    *VidPn = NULL;
    *Topology = NULL;
    if (hVidPn == 0 || Adapter->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = Adapter->DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, VidPn);
    if (!NT_SUCCESS(Status))
        return Status;
    if (*VidPn == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;
    Status = (*VidPn)->pfnGetTopology(hVidPn, hTopology, Topology);
    if (!NT_SUCCESS(Status))
        return Status;
    return *Topology == NULL ? STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY : STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiIsSupportedVidPn(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    CONST DXGK_VIDPN_INTERFACE *VidPn;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology;
    HDGFX_PATH_SET Paths;
    BOOLEAN Supported;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || IsSupportedVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    IsSupportedVidPn->IsVidPnSupported = FALSE;
    if (IsSupportedVidPn->hDesiredVidPn == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }
    Status = HdgfxQueryVidPn(MiniportDeviceContext, IsSupportedVidPn->hDesiredVidPn, &VidPn, &hTopology, &Topology);
    if (!NT_SUCCESS(Status))
        return Status;

    HdgfxLockOutputs(Scanout);
    Status = HdgfxCollectPaths(Topology, hTopology, &Paths);
    if (!NT_SUCCESS(Status) || Paths.Count == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = NT_SUCCESS(Status);
        goto Unlock;
    }
    if (!HdgfxPathTargetsValid(Scanout, &Paths))
        goto Unlock;
    for (Index = 0; Index < Paths.Count; Index++)
    {
        if ((Paths.Paths[Index].Scaling != D3DKMDT_VPPS_IDENTITY &&
             Paths.Paths[Index].Scaling != D3DKMDT_VPPS_UNPINNED &&
             Paths.Paths[Index].Scaling != D3DKMDT_VPPS_NOTSPECIFIED) ||
            (Paths.Paths[Index].Rotation != D3DKMDT_VPPR_IDENTITY &&
             Paths.Paths[Index].Rotation != D3DKMDT_VPPR_UNPINNED &&
             Paths.Paths[Index].Rotation != D3DKMDT_VPPR_NOTSPECIFIED))
        {
            goto Unlock;
        }
    }

    Status = HdgfxPinnedSourceMode(Scanout, IsSupportedVidPn->hDesiredVidPn, VidPn, FALSE, &Supported, NULL);
    if (!NT_SUCCESS(Status) || !Supported)
        goto Unlock;
    for (Index = 0; Index < Paths.Count; Index++)
    {
        Status = HdgfxPinnedTargetMode(Scanout, IsSupportedVidPn->hDesiredVidPn, Paths.Paths[Index].TargetId, VidPn,
                                       FALSE, &Supported, NULL);
        if (!NT_SUCCESS(Status) || !Supported)
            goto Unlock;
    }
    IsSupportedVidPn->IsVidPnSupported = TRUE;

Unlock:
    HdgfxUnlockOutputs(Scanout);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiRecommendFunctionalVidPn(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn)
{
    PAGED_CODE();

    if (HdgfxScanoutOf(MiniportDeviceContext) == NULL || RecommendFunctionalVidPn == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

static NTSTATUS
HdgfxUpdateSourceModeSet(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ const HDGFX_PATH_SET *Paths)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *Pinned = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE *NewMode = NULL;
    const HDGFX_MODE *Mode;
    ULONG First;
    ULONG Index;
    ULONG PathIndex;
    NTSTATUS Status;

    Status = VidPn->pfnAcquireSourceModeSet(hVidPn, SourceId, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned);
    if (!NT_SUCCESS(Status) || Pinned != NULL)
    {
        if (Pinned != NULL)
            ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
        VidPn->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return Status;
    }
    VidPn->pfnReleaseSourceModeSet(hVidPn, hModeSet);

    Status = VidPn->pfnCreateNewSourceModeSet(hVidPn, SourceId, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Scanout->ModeCount && NT_SUCCESS(Status); Index++)
    {
        Mode = &Scanout->Modes[Index];
        if (HdgfxFindSourceMode(Scanout, Mode->Width, Mode->Height, &First) && First != Index)
            continue;
        for (PathIndex = 0; PathIndex < Paths->Count; PathIndex++)
        {
            const HDGFX_PATH *Path = &Paths->Paths[PathIndex];

            if (Path->SourceId != SourceId || !HdgfxTargetKnown(Scanout, Path->TargetId))
                continue;
            if (Path->PinnedWidth != 0 ?
                    Path->PinnedWidth != Mode->Width || Path->PinnedHeight != Mode->Height :
                    !HdgfxTargetOffersSize(Scanout, Path->TargetId, Mode->Width, Mode->Height))
            {
                break;
            }
        }
        if (PathIndex != Paths->Count)
            continue;

        Status = ModeSet->pfnCreateNewModeInfo(hModeSet, &NewMode);
        if (!NT_SUCCESS(Status))
            break;
        NewMode->Type = D3DKMDT_RMT_GRAPHICS;
        NewMode->Format.Graphics.PrimSurfSize.cx = Mode->Width;
        NewMode->Format.Graphics.PrimSurfSize.cy = Mode->Height;
        NewMode->Format.Graphics.VisibleRegionSize.cx = Mode->Width;
        NewMode->Format.Graphics.VisibleRegionSize.cy = Mode->Height;
        NewMode->Format.Graphics.Stride = Mode->Width * HDGFX_BYTES_PER_PIXEL;
        NewMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        NewMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
        NewMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
        Status = ModeSet->pfnAddMode(hModeSet, NewMode);
        if (!NT_SUCCESS(Status))
            ModeSet->pfnReleaseModeInfo(hModeSet, NewMode);
    }

    if (NT_SUCCESS(Status))
        Status = VidPn->pfnAssignSourceModeSet(hVidPn, SourceId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPn->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    return Status;
}

static NTSTATUS
HdgfxUpdateTargetModeSet(
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ ULONG RequiredWidth,
    _In_ ULONG RequiredHeight)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *Pinned = NULL;
    D3DKMDT_VIDPN_TARGET_MODE *NewMode = NULL;
    const HDGFX_MODE *Modes;
    ULONG Count = HdgfxTargetModes(Scanout, TargetId, &Modes);
    ULONG Index;
    NTSTATUS Status;

    Status = VidPn->pfnAcquireTargetModeSet(hVidPn, TargetId, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned);
    if (!NT_SUCCESS(Status) || Pinned != NULL)
    {
        if (Pinned != NULL)
            ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
        VidPn->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return Status;
    }
    VidPn->pfnReleaseTargetModeSet(hVidPn, hModeSet);

    Status = VidPn->pfnCreateNewTargetModeSet(hVidPn, TargetId, &hModeSet, &ModeSet);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        if (RequiredWidth != 0 && (Modes[Index].Width != RequiredWidth || Modes[Index].Height != RequiredHeight))
            continue;

        Status = ModeSet->pfnCreateNewModeInfo(hModeSet, &NewMode);
        if (!NT_SUCCESS(Status))
            break;
        HdgfxModeSignal(&Modes[Index], &NewMode->VideoSignalInfo);
        NewMode->WireFormatAndPreference.Value = 0;
        NewMode->WireFormatAndPreference.Preference = Index == 0 ? D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;
        NewMode->WireFormatAndPreference.Rgb = D3DKMDT_BITS_PER_COMPONENT_08;
        Status = ModeSet->pfnAddMode(hModeSet, NewMode);
        if (!NT_SUCCESS(Status))
            ModeSet->pfnReleaseModeInfo(hModeSet, NewMode);
    }

    if (NT_SUCCESS(Status))
        Status = VidPn->pfnAssignTargetModeSet(hVidPn, TargetId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPn->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    return Status;
}

static VOID
HdgfxPinnedSourceSize(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *Pinned = NULL;

    *Width = 0;
    *Height = 0;
    if (!NT_SUCCESS(VidPn->pfnAcquireSourceModeSet(hVidPn, SourceId, &hModeSet, &ModeSet)))
        return;
    if (ModeSet != NULL && NT_SUCCESS(ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned)) && Pinned != NULL)
    {
        if (Pinned->Type == D3DKMDT_RMT_GRAPHICS)
        {
            *Width = (ULONG)Pinned->Format.Graphics.PrimSurfSize.cx;
            *Height = (ULONG)Pinned->Format.Graphics.PrimSurfSize.cy;
        }
        ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
    }
    VidPn->pfnReleaseSourceModeSet(hVidPn, hModeSet);
}

static VOID
HdgfxPinnedTargetSize(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSet = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *Pinned = NULL;

    *Width = 0;
    *Height = 0;
    if (!NT_SUCCESS(VidPn->pfnAcquireTargetModeSet(hVidPn, TargetId, &hModeSet, &ModeSet)))
        return;
    if (ModeSet != NULL && NT_SUCCESS(ModeSet->pfnAcquirePinnedModeInfo(hModeSet, &Pinned)) && Pinned != NULL)
    {
        *Width = (ULONG)Pinned->VideoSignalInfo.ActiveSize.cx;
        *Height = (ULONG)Pinned->VideoSignalInfo.ActiveSize.cy;
        ModeSet->pfnReleaseModeInfo(hModeSet, Pinned);
    }
    VidPn->pfnReleaseTargetModeSet(hVidPn, hModeSet);
}

static NTSTATUS
HdgfxUpdatePathSupport(
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *Enum,
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path)
{
    D3DKMDT_VIDPN_PRESENT_PATH Updated = *Path;
    BOOLEAN PivotIsPath = Enum->EnumPivot.VidPnSourceId == Path->VidPnSourceId &&
                          Enum->EnumPivot.VidPnTargetId == Path->VidPnTargetId;
    BOOLEAN Scaling = !HdgfxScalingPinned(Path->ContentTransformation.Scaling) &&
                      !(Enum->EnumPivotType == D3DKMDT_EPT_SCALING && PivotIsPath);
    BOOLEAN Rotation = !HdgfxRotationPinned(Path->ContentTransformation.Rotation) &&
                       !(Enum->EnumPivotType == D3DKMDT_EPT_ROTATION && PivotIsPath);

    if (!Scaling && !Rotation)
        return STATUS_SUCCESS;
    if (Scaling)
    {
        RtlZeroMemory(&Updated.ContentTransformation.ScalingSupport,
                      sizeof(Updated.ContentTransformation.ScalingSupport));
        Updated.ContentTransformation.ScalingSupport.Identity = 1;
    }
    if (Rotation)
    {
        RtlZeroMemory(&Updated.ContentTransformation.RotationSupport,
                      sizeof(Updated.ContentTransformation.RotationSupport));
        Updated.ContentTransformation.RotationSupport.Identity = 1;
        Updated.ContentTransformation.RotationSupport.Offset0 = 1;
    }
    return Topology->pfnUpdatePathSupportInfo(hTopology, &Updated);
}

NTSTATUS
APIENTRY
HdgfxDdiEnumVidPnCofuncModality(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    CONST DXGK_VIDPN_INTERFACE *VidPn;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath;
    D3DKMDT_HVIDPN hVidPn;
    HDGFX_PATH_SET Paths;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || EnumCofuncModality == NULL)
        return STATUS_INVALID_PARAMETER;

    hVidPn = EnumCofuncModality->hConstrainingVidPn;
    Status = HdgfxQueryVidPn(MiniportDeviceContext, hVidPn, &VidPn, &hTopology, &Topology);
    if (!NT_SUCCESS(Status))
        return Status;

    HdgfxLockOutputs(Scanout);
    Status = HdgfxCollectPaths(Topology, hTopology, &Paths);
    if (!NT_SUCCESS(Status) || Paths.Count == 0)
        goto Unlock;
    if (Paths.Overflow)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }
    for (Index = 0; Index < Paths.Count; Index++)
    {
        HdgfxPinnedTargetSize(hVidPn, VidPn, Paths.Paths[Index].TargetId, &Paths.Paths[Index].PinnedWidth,
                              &Paths.Paths[Index].PinnedHeight);
    }

    Status = Topology->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status))
        goto Unlock;
    if (Path == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }

    while (Path != NULL)
    {
        HdgfxPinnedSourceSize(hVidPn, VidPn, Path->VidPnSourceId, &SourceWidth, &SourceHeight);
        if (!HdgfxTargetKnown(Scanout, Path->VidPnTargetId))
        {
            Status = HdgfxReleasePath(Topology, hTopology, Path, STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET);
            goto Unlock;
        }
        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              EnumCofuncModality->EnumPivot.VidPnSourceId == Path->VidPnSourceId))
        {
            Status = HdgfxUpdateSourceModeSet(Scanout, hVidPn, VidPn, Path->VidPnSourceId, &Paths);
            if (!NT_SUCCESS(Status))
            {
                Status = HdgfxReleasePath(Topology, hTopology, Path, Status);
                goto Unlock;
            }
        }
        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              EnumCofuncModality->EnumPivot.VidPnTargetId == Path->VidPnTargetId))
        {
            Status = HdgfxUpdateTargetModeSet(Scanout, hVidPn, VidPn, Path->VidPnTargetId, SourceWidth, SourceHeight);
            if (!NT_SUCCESS(Status))
            {
                Status = HdgfxReleasePath(Topology, hTopology, Path, Status);
                goto Unlock;
            }
        }
        Status = HdgfxUpdatePathSupport(EnumCofuncModality, Topology, hTopology, Path);
        if (!NT_SUCCESS(Status))
        {
            Status = HdgfxReleasePath(Topology, hTopology, Path, Status);
            goto Unlock;
        }

        NextPath = NULL;
        Status = Topology->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
        {
            Status = HdgfxReleasePath(Topology, hTopology, Path, STATUS_SUCCESS);
            goto Unlock;
        }
        if (!NT_SUCCESS(Status) || NextPath == NULL)
        {
            Status = HdgfxReleasePath(Topology, hTopology, Path,
                                      NT_SUCCESS(Status) ? STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY : Status);
            goto Unlock;
        }
        Status = Topology->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(Status))
        {
            Topology->pfnReleasePathInfo(hTopology, NextPath);
            goto Unlock;
        }
        Path = NextPath;
    }
    Status = STATUS_SUCCESS;

Unlock:
    HdgfxUnlockOutputs(Scanout);
    return Status;
}

static NTSTATUS
HdgfxValidateFunctionalVidPn(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _Out_ PHDGFX_PATH_SET Paths,
    _Out_ PULONG SourceMode)
{
    CONST DXGK_VIDPN_INTERFACE *VidPn;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *Topology;
    const HDGFX_MODE *Modes;
    BOOLEAN Supported;
    ULONG Index;
    NTSTATUS Status;

    RtlZeroMemory(Paths, sizeof(*Paths));
    *SourceMode = Scanout->Mode;
    Status = HdgfxQueryVidPn(Adapter, hVidPn, &VidPn, &hTopology, &Topology);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = HdgfxCollectPaths(Topology, hTopology, Paths);
    if (!NT_SUCCESS(Status) || Paths->Count == 0)
        return Status;
    if (!HdgfxPathTargetsValid(Scanout, Paths))
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    for (Index = 0; Index < Paths->Count; Index++)
    {
        if (!HdgfxTransformSupported(Paths->Paths[Index].Scaling, Paths->Paths[Index].Rotation))
            return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        if (Paths->Paths[Index].GammaRampType != D3DDDI_GAMMARAMP_DEFAULT)
            return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    }

    Status = HdgfxPinnedSourceMode(Scanout, hVidPn, VidPn, TRUE, &Supported, SourceMode);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Supported)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

    for (Index = 0; Index < Paths->Count; Index++)
    {
        PHDGFX_PATH Path = &Paths->Paths[Index];

        Status = HdgfxPinnedTargetMode(Scanout, hVidPn, Path->TargetId, VidPn, TRUE, &Supported, &Path->ModeIndex);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Supported)
            return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;
        HdgfxTargetModes(Scanout, Path->TargetId, &Modes);
        if (Modes[Path->ModeIndex].Width != Scanout->Modes[*SourceMode].Width ||
            Modes[Path->ModeIndex].Height != Scanout->Modes[*SourceMode].Height)
        {
            return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxApplyPaths(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ PHDGFX_SCANOUT Scanout,
    _In_ const HDGFX_PATH_SET *Paths,
    _In_ ULONG SourceMode)
{
    const HDGFX_PATH *PanelPath = HdgfxFindPath(Paths, HDGFX_PANEL_CHILD_UID);
    BOOLEAN Panel;
    BOOLEAN Crt;
    ULONG Mode;
    NTSTATUS Status;

    if (Paths->Count == 0)
    {
        Mode = Scanout->Mode;
        Panel = Scanout->LvdsActive;
    }
    else
    {
        Mode = PanelPath != NULL ? PanelPath->ModeIndex : SourceMode;
        Panel = PanelPath != NULL;
    }
    Crt = HdgfxFindPath(Paths, HDGFX_CRT_CHILD_UID) != NULL;
    if (Mode >= Scanout->ModeCount)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    if (Mode == Scanout->Mode && Panel == Scanout->LvdsActive && Crt == Scanout->CrtActive &&
        !(Crt && Scanout->CrtStale))
    {
        return STATUS_SUCCESS;
    }

    Status = HdgfxSetOutputs(Scanout, &Scanout->Modes[Mode], Panel, Crt);
    if (NT_SUCCESS(Status))
    {
        Scanout->Mode = Mode;
        if (Crt)
            Scanout->CrtStale = FALSE;
        Adapter->VsyncTargetId = !Panel && Crt ? HDGFX_CRT_CHILD_UID : HDGFX_PANEL_CHILD_UID;
    }
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiCommitVidPn(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    HDGFX_PATH_SET Paths;
    ULONG SourceMode;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || CommitVidPn == NULL ||
        (CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_IGNORE &&
         CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_ENFORCE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (CommitVidPn->AffectedVidPnSourceId != 0 && CommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    HdgfxLockOutputs(Scanout);
    Status = HdgfxValidateFunctionalVidPn(MiniportDeviceContext, Scanout, CommitVidPn->hFunctionalVidPn, &Paths,
                                          &SourceMode);
    if (NT_SUCCESS(Status))
        Status = HdgfxApplyPaths(MiniportDeviceContext, Scanout, &Paths, SourceMode);
    else
        DPRINT1("CommitVidPn rejected the functional VidPN: 0x%08lx\n", Status);
    HdgfxUnlockOutputs(Scanout);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetTimingsFromVidPn(
    _In_ PVOID MiniportDeviceContext,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN SetTimings)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    DXGK_SET_TIMING_PATH_INFO *PathInfo;
    HDGFX_PATH_SET Paths;
    ULONG SourceMode;
    ULONG Timed = 0;
    ULONG Index;
    ULONG Other;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || SetTimings == NULL || SetTimings->pResultsFlags == NULL ||
        SetTimings->SetFlags.Value != 0 || SetTimings->PathCount > HDGFX_CHILD_COUNT ||
        (SetTimings->PathCount != 0 && SetTimings->pSetTimingPathInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SetTimings->pResultsFlags->Value = 0;
    HdgfxLockOutputs(Scanout);
    Status = HdgfxValidateFunctionalVidPn(MiniportDeviceContext, Scanout, SetTimings->hFunctionalVidPn, &Paths,
                                          &SourceMode);
    if (!NT_SUCCESS(Status))
        goto Unlock;

    for (Index = 0; Index < SetTimings->PathCount; Index++)
    {
        BOOLEAN InTopology;

        PathInfo = &SetTimings->pSetTimingPathInfo[Index];
        if (!HdgfxTargetKnown(Scanout, PathInfo->VidPnTargetId) || (PathInfo->InputFlags & ~0x1FUL) != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Unlock;
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (SetTimings->pSetTimingPathInfo[Other].VidPnTargetId == PathInfo->VidPnTargetId)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Unlock;
            }
        }

        InTopology = HdgfxFindPath(&Paths, PathInfo->VidPnTargetId) != NULL;
        if (PathInfo->Input.VidPnPathUpdates == DXGK_PATH_UPDATE_REMOVED)
        {
            if (InTopology || PathInfo->Input.Active != 0)
            {
                Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
                goto Unlock;
            }
            continue;
        }
        if (!InTopology)
        {
            Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
            goto Unlock;
        }
        if (PathInfo->OutputWireColorSpace != D3DDDI_OUTPUT_WIRE_COLOR_SPACE_G22_P709 ||
            PathInfo->SelectedWireFormat.Preference != 0 ||
            PathInfo->SelectedWireFormat.Rgb != D3DKMDT_BITS_PER_COMPONENT_08 ||
            PathInfo->SelectedWireFormat.YCbCr444 != 0 || PathInfo->SelectedWireFormat.YCbCr422 != 0 ||
            PathInfo->SelectedWireFormat.YCbCr420 != 0 || PathInfo->SelectedWireFormat.Intensity != 0)
        {
            Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
            goto Unlock;
        }
        Timed++;
    }
    if (Timed != Paths.Count)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }

    Status = HdgfxApplyPaths(MiniportDeviceContext, Scanout, &Paths, SourceMode);
    if (!NT_SUCCESS(Status))
        goto Unlock;

    for (Index = 0; Index < SetTimings->PathCount; Index++)
    {
        PathInfo = &SetTimings->pSetTimingPathInfo[Index];
        PathInfo->OutputFlags = 0;
        RtlZeroMemory(&PathInfo->TargetState, sizeof(PathInfo->TargetState));
        PathInfo->TargetState.ConnectionChangeId = 1;
        PathInfo->TargetState.TargetId = PathInfo->VidPnTargetId;
        if (PathInfo->VidPnTargetId == HDGFX_CRT_CHILD_UID)
        {
            PathInfo->TargetState.ConnectionStatus =
                Scanout->CrtConnected ? MonitorStatusConnected : MonitorStatusDisconnected;
            PathInfo->TargetState.MonitorConnect.LinkTargetType = D3DKMDT_VOT_HD15;
        }
        else
        {
            PathInfo->TargetState.ConnectionStatus = MonitorStatusConnected;
            PathInfo->TargetState.MonitorConnect.LinkTargetType = D3DKMDT_VOT_INTERNAL;
        }
        PathInfo->DiagnosticInfo = 0;
        PathInfo->GlitchCause = DXGK_GLITCH_CAUSE_NONE;
        PathInfo->GlitchEffect = DXGK_GLITCH_EFFECT_SEAMLESS;
        PathInfo->GlitchDuration = DXGK_GLITCH_DURATION_NONE;
    }

Unlock:
    HdgfxUnlockOutputs(Scanout);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetVidPnSourceAddress(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress)
{
    PHDGFX_ADAPTER Adapter;
    PHDGFX_SCANOUT Scanout;
    ULONGLONG Address;
    ULONGLONG Offset;
    ULONGLONG Span;
    PHDGFX_ALLOCATION Allocation;
    BOOLEAN Valid;
    ULONG Surface;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    NTSTATUS Status = STATUS_SUCCESS;

    if (SetVidPnSourceAddress == NULL)
        return STATUS_INVALID_PARAMETER;
    if (SetVidPnSourceAddress->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (SetVidPnSourceAddress->PrimarySegment != 0 && SetVidPnSourceAddress->PrimarySegment != HDGFX_SLAB_SEGMENT_ID &&
        SetVidPnSourceAddress->PrimarySegment != HDGFX_APERTURE_SEGMENT_ID)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DPRINT1("SetVidPnSourceAddress at IRQL %u\n", KeGetCurrentIrql());
        return STATUS_INVALID_DEVICE_STATE;
    }

    Address = (ULONGLONG)SetVidPnSourceAddress->PrimaryAddress.QuadPart;
    if (Address == 0)
        return STATUS_SUCCESS;

    Allocation = (PHDGFX_ALLOCATION)SetVidPnSourceAddress->hAllocation;
    if (Allocation == NULL || Allocation->Magic != HDGFX_ALLOCATION_MAGIC)
        return STATUS_INVALID_HANDLE;

    Scanout = HdgfxScanoutAcquire(MiniportDeviceContext, &Adapter);
    if (Scanout == NULL)
        return STATUS_DEVICE_NOT_READY;
    HdgfxLock(Scanout);
    Width = Allocation->Width != 0 ? Allocation->Width : Scanout->Width;
    Height = Allocation->Height != 0 ? Allocation->Height : Scanout->Height;
    Pitch = Allocation->Pitch != 0 ? Allocation->Pitch : Width * HDGFX_BYTES_PER_PIXEL;
    Span = (ULONGLONG)Pitch * (Height - 1) + (ULONGLONG)Width * HDGFX_BYTES_PER_PIXEL;
    if (SetVidPnSourceAddress->PrimarySegment == HDGFX_APERTURE_SEGMENT_ID)
    {
        Offset = Address - Adapter->ApertureBase;
        Valid = Adapter->ApertureSize != 0 && Address >= Adapter->ApertureBase && Offset < Adapter->ApertureSize &&
                Span <= Adapter->ApertureSize - Offset;
        Surface = (ULONG)Address;
    }
    else
    {
        Offset = Address - Scanout->SlabPhysical;
        Valid = Scanout->SlabPhysical == (ULONGLONG)Adapter->SlabPhysical.QuadPart &&
                Address >= Scanout->SlabPhysical && Offset < Scanout->SlabSize && Span <= Scanout->SlabSize - Offset;
        Surface = Scanout->SlabAddress + (ULONG)Offset;
    }
    if (!Valid || Width != Scanout->Width || Height != Scanout->Height || Scanout->SlabGtt == NULL ||
        Pitch < Width * HDGFX_BYTES_PER_PIXEL || Pitch % HDGFX_STRIDE_ALIGNMENT != 0 ||
        Offset % PAGE_SIZE != 0 || Span > Allocation->Size)
    {
        DPRINT1("SetVidPnSourceAddress: %lux%lu pitch %lu at 0x%I64x does not fit the %lux%lu source\n",
                Width, Height, Pitch, Address, Scanout->Width, Scanout->Height);
        Status = STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }
    else
    {
        Scanout->Surface = Surface;
        Scanout->Pitch = Pitch;
        HdgfxPlaneCommit(Scanout);
        InterlockedExchange64(&Adapter->PrimaryAddress, (LONG64)Address);
    }
    HdgfxUnlock(Scanout);
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetVidPnSourceVisibility(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility)
{
    if (HdgfxScanoutOf(MiniportDeviceContext) == NULL || SetVidPnSourceVisibility == NULL)
        return STATUS_INVALID_PARAMETER;
    if (SetVidPnSourceVisibility->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiUpdateActiveVidPnPresentPath(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path;

    PAGED_CODE();

    if (Scanout == NULL || UpdateActiveVidPnPresentPath == NULL)
        return STATUS_INVALID_PARAMETER;
    Path = &UpdateActiveVidPnPresentPath->VidPnPresentPathInfo;
    if (Path->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (!HdgfxTargetKnown(Scanout, Path->VidPnTargetId))
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    if (!HdgfxTransformSupported(Path->ContentTransformation.Scaling, Path->ContentTransformation.Rotation))
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiRecommendMonitorModes(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);

    PAGED_CODE();

    if (Scanout == NULL || RecommendMonitorModes == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!HdgfxTargetKnown(Scanout, RecommendMonitorModes->VideoPresentTargetId))
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    return STATUS_GRAPHICS_NO_PREFERRED_MODE;
}

NTSTATUS
APIENTRY
HdgfxDdiRecommendVidPnTopology(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology)
{
    PAGED_CODE();

    if (HdgfxScanoutOf(MiniportDeviceContext) == NULL || RecommendVidPnTopology == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryChildRelations(
    _In_ PVOID MiniportDeviceContext,
    _Out_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    ULONG Count;
    ULONG Index;

    PAGED_CODE();

    if (Scanout == NULL || ChildRelations == NULL)
        return STATUS_INVALID_PARAMETER;
    Count = (Scanout->PanelPresent ? 1 : 0) + (Scanout->CrtSupported ? 1 : 0);
    if (ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR) * Count)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(ChildRelations, sizeof(DXGK_CHILD_DESCRIPTOR) * Count);
    Index = 0;
    if (Scanout->PanelPresent)
    {
        ChildRelations[Index].ChildDeviceType = TypeVideoOutput;
        ChildRelations[Index].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
        ChildRelations[Index].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = Adapter->ChildOutputTechnology;
        ChildRelations[Index].AcpiUid = Adapter->ChildAcpiUid;
        ChildRelations[Index].ChildUid = HDGFX_PANEL_CHILD_UID;
        Index++;
    }
    if (Scanout->CrtSupported)
    {
        ChildRelations[Index].ChildDeviceType = TypeVideoOutput;
        ChildRelations[Index].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
        ChildRelations[Index].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HD15;
        ChildRelations[Index].ChildUid = HDGFX_CRT_CHILD_UID;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryChildStatus(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);

    PAGED_CODE();
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (Scanout == NULL || ChildStatus == NULL || !HdgfxTargetKnown(Scanout, ChildStatus->ChildUid))
        return STATUS_INVALID_PARAMETER;
    if (ChildStatus->Type != StatusConnection)
        return STATUS_SUCCESS;

    if (ChildStatus->ChildUid == HDGFX_CRT_CHILD_UID)
    {
        HdgfxLockOutputs(Scanout);
        ChildStatus->HotPlug.Connected = Scanout->CrtConnected;
        HdgfxUnlockOutputs(Scanout);
    }
    else
    {
        ChildStatus->HotPlug.Connected = TRUE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryDeviceDescriptor(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PHDGFX_SCANOUT Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    const UCHAR *Edid;
    ULONG EdidLength;
    NTSTATUS Status;

    PAGED_CODE();

    if (Scanout == NULL || DeviceDescriptor == NULL || !HdgfxTargetKnown(Scanout, ChildUid))
        return STATUS_INVALID_PARAMETER;

    HdgfxLockOutputs(Scanout);
    if (ChildUid == HDGFX_CRT_CHILD_UID)
    {
        Edid = Scanout->CrtEdid;
        EdidLength = Scanout->CrtConnected ? Scanout->CrtEdidLength : 0;
    }
    else
    {
        Edid = Scanout->PanelEdid;
        EdidLength = Scanout->PanelEdidLength;
    }
    if (EdidLength == 0)
        Status = STATUS_MONITOR_NO_DESCRIPTOR;
    else if (DeviceDescriptor->DescriptorOffset >= EdidLength)
        Status = STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    else if (DeviceDescriptor->DescriptorBuffer == NULL)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        DeviceDescriptor->DescriptorLength = min(DeviceDescriptor->DescriptorLength,
                                                 EdidLength - DeviceDescriptor->DescriptorOffset);
        RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Edid + DeviceDescriptor->DescriptorOffset,
                      DeviceDescriptor->DescriptorLength);
        Status = STATUS_SUCCESS;
    }
    HdgfxUnlockOutputs(Scanout);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetPowerState(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType)
{
    PHDGFX_SCANOUT Scanout;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ActionType);

    if (DeviceUid == DISPLAY_ADAPTER_HW_ID)
        return DevicePowerState == PowerDeviceD0 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;

    Scanout = HdgfxScanoutOf(MiniportDeviceContext);
    if (Scanout == NULL || !HdgfxTargetKnown(Scanout, DeviceUid) || DevicePowerState > PowerDeviceD3)
        return STATUS_INVALID_PARAMETER;
    return HdgfxSetMonitorPower(Scanout, DeviceUid, DevicePowerState != PowerDeviceD0);
}

static NTSTATUS
HdgfxPointerCommit(
    _In_ PHDGFX_SCANOUT Scanout)
{
    if (Scanout->Cursor == NULL)
        return STATUS_NOT_SUPPORTED;
    Scanout->CursorControl = HdgfxIlkCursorControl(Scanout->PointerVisible && Scanout->PointerShapeValid);
    Scanout->CursorPosition = HdgfxIlkCursorPosition(Scanout->PointerX - (LONG)Scanout->PointerHotX,
                                                     Scanout->PointerY - (LONG)Scanout->PointerHotY);
    HdgfxCursorCommit(Scanout);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiSetPointerPosition(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    PHDGFX_ADAPTER Adapter;
    PHDGFX_SCANOUT Scanout;
    NTSTATUS Status;

    if (SetPointerPosition == NULL || SetPointerPosition->VidPnSourceId != 0 ||
        SetPointerPosition->Flags.Procedural || SetPointerPosition->Flags.Reserved)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Scanout = HdgfxScanoutAcquire(MiniportDeviceContext, &Adapter);
    if (Scanout == NULL)
        return STATUS_DEVICE_NOT_READY;
    HdgfxLock(Scanout);
    Scanout->PointerX = SetPointerPosition->X;
    Scanout->PointerY = SetPointerPosition->Y;
    Scanout->PointerVisible = SetPointerPosition->Flags.Visible ? TRUE : FALSE;
    Status = HdgfxPointerCommit(Scanout);
    HdgfxUnlock(Scanout);
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetPointerShape(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    PHDGFX_ADAPTER Adapter;
    PHDGFX_SCANOUT Scanout;
    const UCHAR *Pixels;
    PULONG Cursor;
    ULONG Y;
    NTSTATUS Status;

    if (SetPointerShape == NULL || SetPointerShape->VidPnSourceId != 0 ||
        SetPointerShape->Flags.Value != 2 || SetPointerShape->pPixels == NULL ||
        SetPointerShape->Width == 0 || SetPointerShape->Height == 0 ||
        SetPointerShape->Width > HDGFX_CURSOR_SIZE || SetPointerShape->Height > HDGFX_CURSOR_SIZE ||
        SetPointerShape->Pitch < SetPointerShape->Width * sizeof(ULONG) ||
        SetPointerShape->XHot >= SetPointerShape->Width || SetPointerShape->YHot >= SetPointerShape->Height)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Scanout = HdgfxScanoutAcquire(MiniportDeviceContext, &Adapter);
    if (Scanout == NULL)
        return STATUS_DEVICE_NOT_READY;
    HdgfxLock(Scanout);
    Cursor = Scanout->Cursor;
    if (Cursor != NULL)
    {
        Pixels = SetPointerShape->pPixels;
        RtlZeroMemory(Cursor, HDGFX_CURSOR_SIZE * HDGFX_CURSOR_SIZE * sizeof(ULONG));
        for (Y = 0; Y < SetPointerShape->Height; Y++)
        {
            RtlCopyMemory(&Cursor[Y * HDGFX_CURSOR_SIZE], Pixels + (SIZE_T)Y * SetPointerShape->Pitch,
                          SetPointerShape->Width * sizeof(ULONG));
        }
        KeMemoryBarrier();
        Scanout->PointerHotX = SetPointerShape->XHot;
        Scanout->PointerHotY = SetPointerShape->YHot;
        Scanout->PointerShapeValid = TRUE;
    }
    Status = HdgfxPointerCommit(Scanout);
    HdgfxUnlock(Scanout);
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}

NTSTATUS
APIENTRY
HdgfxDdiSetPalette(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SetPalette);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
HdgfxDdiGetScanLine(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    PHDGFX_ADAPTER Adapter;
    PHDGFX_SCANOUT Scanout;
    ULONG VActive;
    ULONG Pipe;
    ULONG Line;
    NTSTATUS Status = STATUS_SUCCESS;

    if (GetScanLine == NULL)
        return STATUS_INVALID_PARAMETER;
    Scanout = HdgfxScanoutAcquire(MiniportDeviceContext, &Adapter);
    if (Scanout == NULL)
        return STATUS_DEVICE_NOT_READY;

    if (!HdgfxTargetKnown(Scanout, GetScanLine->VidPnTargetId))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (GetScanLine->VidPnTargetId == HDGFX_CRT_CHILD_UID && !Scanout->CrtTimingValid)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        Pipe = GetScanLine->VidPnTargetId == HDGFX_CRT_CHILD_UID ? HDGFX_CRT_PIPE : 0;
        VActive = Pipe == HDGFX_CRT_PIPE ? Scanout->CrtTiming.Timings.VActive : Scanout->Panel.Timings.VActive;
        Line = HdgfxScanLine(Scanout, Pipe);
        GetScanLine->ScanLine = Line;
        GetScanLine->InVerticalBlank = Line >= VActive;
    }
    ExReleaseRundownProtection(&Adapter->Rundown);
    return Status;
}
