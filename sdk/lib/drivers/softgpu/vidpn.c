/*
 * PROJECT:     LiberNT WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidPN DDI callbacks for softgpu.sys.
 *              Implements the Video Present Network miniport interface.
 * COPYRIGHT:   Copyright 2024 LiberNT WDDM Team
 *
 * VidPN (Video Present Network) overview
 * =======================================
 * A VidPN describes the mapping of rendering sources (primary surfaces) to
 * display targets (monitor connectors).  dxgkrnl manages the VidPN topology
 * and calls the miniport to:
 *
 *   IsSupportedVidPn         — validate a proposed VidPN topology.
 *   RecommendFunctionalVidPn — propose a full VidPN if none is given.
 *   EnumVidPnCofuncModality  — enumerate modes compatible with constraints.
 *   CommitVidPn              — activate a VidPN topology.
 *   SetVidPnSourceAddress    — program a new framebuffer address.
 *   SetVidPnSourceVisibility — toggle source display on/off.
 *
 * softgpu exposes one source, one target, and one identity path at the
 * firmware framebuffer's native size. The software renderer can extend an
 * unpinned VidPN with that mode, but it must reject pinned modes, transforms,
 * or topologies that the scanout path cannot implement.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"
#include "softgpu_2d_core.h"
#include "vidpn_policy_core.h"

static NTSTATUS
SoftGpuScheduleScanout(
    _Inout_ PSOFTGPU_DEVICE Device);

/*
 * STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY and STATUS_MONITOR_NO_DESCRIPTOR
 * are defined in ntstatus.h (pulled in via ntddk.h / wdm.h).
 * No local defines needed.
 */

/* =========================================================================
 * DxgkDdiIsSupportedVidPn
 * =========================================================================
 */

static ULONG
SoftGpuDisplayModeCount(
    _In_ PSOFTGPU_DEVICE Device)
{
    return Device->ModeCount != 0 ? Device->ModeCount : 1;
}

static VOID
SoftGpuGetDisplayModeSize(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONG Index,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    if (Device->ModeCount != 0)
    {
        *Width = Device->Modes[Index].Width;
        *Height = Device->Modes[Index].Height;
    }
    else
    {
        *Width = Device->Width;
        *Height = Device->Height;
    }
}

static VOID
SoftGpuFillModeSignal(
    _In_opt_ const SOFTGPU_DISPLAY_MODE *Mode,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    RtlZeroMemory(Signal, sizeof(*Signal));
    Signal->VideoStandard = D3DKMDT_VSS_OTHER;
    Signal->ActiveSize.cx = Width;
    Signal->ActiveSize.cy = Height;
    Signal->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    if (Mode != NULL && Mode->PixelRate != 0)
    {
        Signal->TotalSize.cx = Mode->TotalWidth;
        Signal->TotalSize.cy = Mode->TotalHeight;
        Signal->VSyncFreq.Numerator = Mode->PixelRate;
        Signal->VSyncFreq.Denominator = Mode->TotalWidth * Mode->TotalHeight;
        Signal->HSyncFreq.Numerator = Mode->PixelRate;
        Signal->HSyncFreq.Denominator = Mode->TotalWidth;
        Signal->PixelRate = Mode->PixelRate;
    }
    else
    {
        Signal->TotalSize.cx = Width;
        Signal->TotalSize.cy = Height;
        Signal->VSyncFreq.Numerator = 60;
        Signal->VSyncFreq.Denominator = 1;
        Signal->HSyncFreq.Numerator = 60 * Height;
        Signal->HSyncFreq.Denominator = 1;
        Signal->PixelRate = (SIZE_T)Width * Height * 60;
    }
}

static VOID
SoftGpuGetDisplayModeSignal(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONG Index,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    ULONG Width;
    ULONG Height;

    SoftGpuGetDisplayModeSize(Device, Index, &Width, &Height);
    SoftGpuFillModeSignal(Device->ModeCount != 0 ? &Device->Modes[Index] : NULL, Width, Height, Signal);
}

PSOFTGPU_OUTPUT
SoftGpuFindOutput(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONG ChildUid)
{
    ULONG Index;

    if (ChildUid == SOFTGPU_CHILD_UID)
        return NULL;
    for (Index = 0; Index < Device->ExtraOutputCount; Index++)
    {
        if (Device->ExtraOutputs[Index].ChildUid == ChildUid)
            return &Device->ExtraOutputs[Index];
    }
    return NULL;
}

static BOOLEAN
SoftGpuTargetKnown(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    return TargetId == SOFTGPU_CHILD_UID || SoftGpuFindOutput(Device, TargetId) != NULL;
}

static ULONG
SoftGpuTargetModeCount(
    _In_ PSOFTGPU_DEVICE Device,
    _In_opt_ const SOFTGPU_OUTPUT *Output)
{
    return Output != NULL ? Output->ModeCount : SoftGpuDisplayModeCount(Device);
}

static VOID
SoftGpuGetTargetModeSignal(
    _In_ PSOFTGPU_DEVICE Device,
    _In_opt_ const SOFTGPU_OUTPUT *Output,
    _In_ ULONG Index,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    if (Output != NULL)
        SoftGpuFillModeSignal(&Output->Modes[Index], Output->Modes[Index].Width, Output->Modes[Index].Height, Signal);
    else
        SoftGpuGetDisplayModeSignal(Device, Index, Signal);
}

static BOOLEAN
SoftGpuTargetModePreferred(
    _In_ PSOFTGPU_DEVICE Device,
    _In_opt_ const SOFTGPU_OUTPUT *Output,
    _In_ ULONG Index)
{
    if (Output != NULL)
        return Index == Output->PreferredMode;
    return Device->ModeCount == 0 || Index == Device->PreferredMode;
}

static BOOLEAN
SoftGpuTargetOffersSize(
    _In_ PSOFTGPU_DEVICE Device,
    _In_opt_ const SOFTGPU_OUTPUT *Output,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    ULONG Index;

    for (Index = 0; Index < SoftGpuTargetModeCount(Device, Output); Index++)
    {
        SoftGpuGetTargetModeSignal(Device, Output, Index, &Signal);
        if (Signal.ActiveSize.cx == (LONG)Width && Signal.ActiveSize.cy == (LONG)Height)
            return TRUE;
    }
    return FALSE;
}

static VOID
SoftGpuLockOutputs(
    _In_ PSOFTGPU_DEVICE Device)
{
    KeWaitForSingleObject(&Device->OutputMutex, Executive, KernelMode, FALSE, NULL);
}

static VOID
SoftGpuUnlockOutputs(
    _In_ PSOFTGPU_DEVICE Device)
{
    KeReleaseMutex(&Device->OutputMutex, FALSE);
}

static BOOLEAN
SoftGpuFindDisplayMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ LONG Width,
    _In_ LONG Height,
    _Out_opt_ PULONG ModeIndex)
{
    ULONG ModeWidth;
    ULONG ModeHeight;
    ULONG Index;

    for (Index = 0; Index < SoftGpuDisplayModeCount(Device); Index++)
    {
        SoftGpuGetDisplayModeSize(Device, Index, &ModeWidth, &ModeHeight);
        if (Width == (LONG)ModeWidth && Height == (LONG)ModeHeight)
        {
            if (ModeIndex != NULL)
                *ModeIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
SoftGpuSourceModeMatchesDisplay(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const D3DKMDT_VIDPN_SOURCE_MODE *Mode,
    _Out_opt_ PULONG ModeIndex)
{
    return Mode->Type == D3DKMDT_RMT_GRAPHICS &&
           Mode->Format.Graphics.VisibleRegionSize.cx ==
               Mode->Format.Graphics.PrimSurfSize.cx &&
           Mode->Format.Graphics.VisibleRegionSize.cy ==
               Mode->Format.Graphics.PrimSurfSize.cy &&
           Mode->Format.Graphics.PrimSurfSize.cx > 0 &&
           Mode->Format.Graphics.Stride ==
               (ULONG)Mode->Format.Graphics.PrimSurfSize.cx *
                   SOFTGPU_DISPLAY_BYTES_PER_PIXEL &&
           (Mode->Format.Graphics.PixelFormat == D3DDDIFMT_X8R8G8B8 ||
            Mode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8) &&
           Mode->Format.Graphics.PixelValueAccessMode == D3DKMDT_PVAM_DIRECT &&
           SoftGpuFindDisplayMode(Device,
                                  Mode->Format.Graphics.PrimSurfSize.cx,
                                  Mode->Format.Graphics.PrimSurfSize.cy,
                                  ModeIndex);
}

static BOOLEAN
SoftGpuTargetModeMatchesDisplay(
    _In_ PSOFTGPU_DEVICE Device,
    _In_opt_ const SOFTGPU_OUTPUT *Output,
    _In_ const D3DKMDT_VIDPN_TARGET_MODE *Mode,
    _Out_opt_ PULONG ModeIndex)
{
    CONST D3DDDI_RATIONAL *VSync = &Mode->VideoSignalInfo.VSyncFreq;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    ULONG Index;

    if (Mode->VideoSignalInfo.ScanLineOrdering != D3DDDI_VSSLO_PROGRESSIVE)
        return FALSE;

    for (Index = 0; Index < SoftGpuTargetModeCount(Device, Output); Index++)
    {
        SoftGpuGetTargetModeSignal(Device, Output, Index, &Signal);
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

static NTSTATUS
SoftGpuReleasePathInfo(
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_ NTSTATUS Status)
{
    NTSTATUS ReleaseStatus;

    ReleaseStatus =
        TopologyInterface->pfnReleasePathInfo(hTopology, Path);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        return ReleaseStatus;

    return Status;
}

static NTSTATUS
SoftGpuCheckPinnedSourceMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PBOOLEAN Supported)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    const D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (NT_SUCCESS(Status))
    {
        *Supported = PinnedMode == NULL ||
                     SoftGpuSourceModeMatchesDisplay(Device, PinnedMode, NULL);
    }

    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuCheckPinnedTargetMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PBOOLEAN Supported)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    const D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    *Supported = FALSE;
    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                                                     TargetId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (NT_SUCCESS(Status))
    {
        *Supported = PinnedMode == NULL ||
                     SoftGpuTargetModeMatchesDisplay(Device, SoftGpuFindOutput(Device, TargetId), PinnedMode, NULL);
    }

    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuValidateCommittedSourceMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PULONG ModeIndex)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                                                     0,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_SOURCEMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (!NT_SUCCESS(Status))
        goto ReleaseModeInfo;
    if (PinnedMode == NULL)
    {
        Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        goto ReleaseModeInfo;
    }
    if (!SoftGpuSourceModeMatchesDisplay(Device, PinnedMode, ModeIndex))
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

ReleaseModeInfo:
    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

static NTSTATUS
SoftGpuValidateCommittedTargetMode(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _Out_ PULONG ModeIndex)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                                                     TargetId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSetInterface == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TARGETMODESET;
        goto ReleaseModeSet;
    }

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet,
                                                        &PinnedMode);
    if (!NT_SUCCESS(Status))
        goto ReleaseModeInfo;
    if (PinnedMode == NULL)
    {
        Status = STATUS_GRAPHICS_MODE_NOT_PINNED;
        goto ReleaseModeInfo;
    }
    if (!SoftGpuTargetModeMatchesDisplay(Device, SoftGpuFindOutput(Device, TargetId), PinnedMode, ModeIndex))
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;

ReleaseModeInfo:
    if (PinnedMode != NULL)
    {
        ReleaseStatus = ModeSetInterface->pfnReleaseModeInfo(hModeSet,
                                                             PinnedMode);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
            Status = ReleaseStatus;
    }

ReleaseModeSet:
    ReleaseStatus = VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus))
        Status = ReleaseStatus;
    return Status;
}

#define SOFTGPU_MAX_PATHS (1 + SOFTGPU_MAX_EXTRA_OUTPUTS)

typedef struct _SOFTGPU_PATH
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DDDI_GAMMARAMP_TYPE GammaRampType;
    ULONG PinnedWidth;
    ULONG PinnedHeight;
    ULONG ModeIndex;
} SOFTGPU_PATH, *PSOFTGPU_PATH;

typedef struct _SOFTGPU_PATH_SET
{
    ULONG Count;
    BOOLEAN Overflow;
    SOFTGPU_PATH Paths[SOFTGPU_MAX_PATHS];
} SOFTGPU_PATH_SET, *PSOFTGPU_PATH_SET;

static NTSTATUS
SoftGpuCollectPaths(
    _In_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface,
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology,
    _Out_ PSOFTGPU_PATH_SET Paths)
{
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath;
    NTSTATUS Status;

    RtlZeroMemory(Paths, sizeof(*Paths));
    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    while (Path != NULL)
    {
        if (Paths->Count == SOFTGPU_MAX_PATHS)
        {
            Paths->Overflow = TRUE;
            return SoftGpuReleasePathInfo(TopologyInterface, hTopology, Path, STATUS_SUCCESS);
        }
        Paths->Paths[Paths->Count].SourceId = Path->VidPnSourceId;
        Paths->Paths[Paths->Count].TargetId = Path->VidPnTargetId;
        Paths->Paths[Paths->Count].Scaling = Path->ContentTransformation.Scaling;
        Paths->Paths[Paths->Count].Rotation = Path->ContentTransformation.Rotation;
        Paths->Paths[Paths->Count].GammaRampType = Path->GammaRamp.Type;
        Paths->Count++;

        NextPath = NULL;
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            return SoftGpuReleasePathInfo(TopologyInterface, hTopology, Path, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return SoftGpuReleasePathInfo(TopologyInterface, hTopology, Path, Status);
        Status = TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(Status))
        {
            if (NextPath != NULL)
                TopologyInterface->pfnReleasePathInfo(hTopology, NextPath);
            return Status;
        }
        Path = NextPath;
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static BOOLEAN
SoftGpuPathTargetsValid(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PATH_SET *Paths)
{
    ULONG Index;
    ULONG Other;

    if (Paths->Overflow)
        return FALSE;
    for (Index = 0; Index < Paths->Count; Index++)
    {
        if (Paths->Paths[Index].SourceId != 0 ||
            !SoftGpuTargetKnown(Device, Paths->Paths[Index].TargetId))
        {
            return FALSE;
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (Paths->Paths[Other].TargetId == Paths->Paths[Index].TargetId)
                return FALSE;
        }
    }
    return TRUE;
}

static const SOFTGPU_PATH *
SoftGpuFindPath(
    _In_ const SOFTGPU_PATH_SET *Paths,
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

/*
 * Validate the desired VidPN without changing it. An empty VidPN is always
 * supported. A non-empty VidPN is extensible only when its sole 0 -> 0 path,
 * any pinned modes, and any pinned transforms match the native scanout.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiIsSupportedVidPn(
    _In_    PVOID                     MiniportDeviceContext,
    _Inout_ PDXGKARG_ISSUPPORTEDVIDPN IsSupportedVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    const DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    SOFTGPU_PATH_SET Paths;
    BOOLEAN SourceModeSupported;
    BOOLEAN TargetModeSupported;
    ULONG Index;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        IsSupportedVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: IsSupportedVidPn hDesiredVidPn=%p\n",
           (PVOID)IsSupportedVidPn->hDesiredVidPn);

    IsSupportedVidPn->IsVidPnSupported = FALSE;
    if (IsSupportedVidPn->hDesiredVidPn == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    if (Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 IsSupportedVidPn->hDesiredVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (VidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = VidPnInterface->pfnGetTopology(
                 IsSupportedVidPn->hDesiredVidPn,
                 &hTopology,
                 &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TopologyInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    SoftGpuLockOutputs(Device);
    Status = SoftGpuCollectPaths(TopologyInterface, hTopology, &Paths);
    if (!NT_SUCCESS(Status) || Paths.Count == 0)
    {
        IsSupportedVidPn->IsVidPnSupported = NT_SUCCESS(Status);
        goto Unlock;
    }
    if (!SoftGpuPathTargetsValid(Device, &Paths))
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

    Status = SoftGpuCheckPinnedSourceMode(Device,
                                          IsSupportedVidPn->hDesiredVidPn,
                                          VidPnInterface,
                                          &SourceModeSupported);
    if (!NT_SUCCESS(Status) || !SourceModeSupported)
        goto Unlock;

    for (Index = 0; Index < Paths.Count; Index++)
    {
        Status = SoftGpuCheckPinnedTargetMode(Device,
                                              IsSupportedVidPn->hDesiredVidPn,
                                              Paths.Paths[Index].TargetId,
                                              VidPnInterface,
                                              &TargetModeSupported);
        if (!NT_SUCCESS(Status) || !TargetModeSupported)
            goto Unlock;
    }

    IsSupportedVidPn->IsVidPnSupported = TRUE;

Unlock:
    SoftGpuUnlockOutputs(Device);
    return Status;
}


/* =========================================================================
 * DxgkDdiRecommendFunctionalVidPn
 * =========================================================================
 */

/*
 * SoftGpuDdiRecommendFunctionalVidPn
 *
 * Propose a functional VidPN when the OS has no prior state.
 * Since the ReactOS VidPN object model is not yet complete, we simply
 * return success without populating the VidPN object — dxgkrnl will
 * continue with whatever state it has.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRecommendFunctionalVidPn(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendFunctionalVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendFunctionalVidPn "
           "hRecommendedFunctionalVidPn=%p\n",
           (PVOID)RecommendFunctionalVidPn->hRecommendedFunctionalVidPn);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}


/* =========================================================================
 * DxgkDdiEnumVidPnCofuncModality
 * =========================================================================
 */

/* Ensure the source mode set for SourceId holds the current mode (no-op when
 * a mode is already pinned there). */
static NTSTATUS
SoftGpuUpdateSourceModeSet(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ const SOFTGPU_PATH_SET *Paths)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
    D3DKMDT_VIDPN_SOURCE_MODE *NewMode = NULL;
    ULONG Width;
    ULONG Height;
    ULONG FirstIndex;
    ULONG Index;
    ULONG PathIndex;
    NTSTATUS Status;

    Status = VidPnInterface->pfnAcquireSourceModeSet(hVidPn, SourceId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode);
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return Status;
    }
    if (PinnedMode != NULL)
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
        return STATUS_SUCCESS;
    }
    VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);

    Status = VidPnInterface->pfnCreateNewSourceModeSet(hVidPn, SourceId,
                                                       &hModeSet,
                                                       &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < SoftGpuDisplayModeCount(Device) && NT_SUCCESS(Status); Index++)
    {
        SoftGpuGetDisplayModeSize(Device, Index, &Width, &Height);
        if (SoftGpuFindDisplayMode(Device, Width, Height, &FirstIndex) && FirstIndex != Index)
            continue;
        for (PathIndex = 0; PathIndex < Paths->Count; PathIndex++)
        {
            const SOFTGPU_PATH *Path = &Paths->Paths[PathIndex];

            if (Path->SourceId != SourceId || !SoftGpuTargetKnown(Device, Path->TargetId))
                continue;
            if (Path->PinnedWidth != 0 ?
                    Path->PinnedWidth != Width || Path->PinnedHeight != Height :
                    !SoftGpuTargetOffersSize(Device, SoftGpuFindOutput(Device, Path->TargetId), Width, Height))
            {
                break;
            }
        }
        if (PathIndex != Paths->Count)
            continue;

        Status = ModeSetInterface->pfnCreateNewModeInfo(hModeSet, &NewMode);
        if (!NT_SUCCESS(Status))
            break;

        NewMode->Type = D3DKMDT_RMT_GRAPHICS;
        NewMode->Format.Graphics.PrimSurfSize.cx = Width;
        NewMode->Format.Graphics.PrimSurfSize.cy = Height;
        NewMode->Format.Graphics.VisibleRegionSize.cx = Width;
        NewMode->Format.Graphics.VisibleRegionSize.cy = Height;
        NewMode->Format.Graphics.Stride = Width * 4;
        NewMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        NewMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
        NewMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        Status = ModeSetInterface->pfnAddMode(hModeSet, NewMode);
        if (!NT_SUCCESS(Status))
            ModeSetInterface->pfnReleaseModeInfo(hModeSet, NewMode);
    }

    if (NT_SUCCESS(Status))
        Status = VidPnInterface->pfnAssignSourceModeSet(hVidPn, SourceId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);

    return Status;
}

/* Ensure the target mode set for TargetId holds the current mode. */
static NTSTATUS
SoftGpuUpdateTargetModeSet(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ ULONG RequiredWidth,
    _In_ ULONG RequiredHeight)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
    D3DKMDT_VIDPN_TARGET_MODE *NewMode = NULL;
    D3DKMDT_MODE_PREFERENCE Preference;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    const SOFTGPU_OUTPUT *Output = SoftGpuFindOutput(Device, TargetId);
    ULONG Index;
    NTSTATUS Status;

    Status = VidPnInterface->pfnAcquireTargetModeSet(hVidPn, TargetId,
                                                     &hModeSet,
                                                     &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode);
    if (!NT_SUCCESS(Status))
    {
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return Status;
    }
    if (PinnedMode != NULL)
    {
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
        return STATUS_SUCCESS;
    }
    VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);

    Status = VidPnInterface->pfnCreateNewTargetModeSet(hVidPn, TargetId,
                                                       &hModeSet,
                                                       &ModeSetInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < SoftGpuTargetModeCount(Device, Output) && NT_SUCCESS(Status); Index++)
    {
        SoftGpuGetTargetModeSignal(Device, Output, Index, &Signal);
        if (RequiredWidth != 0 &&
            (Signal.ActiveSize.cx != (LONG)RequiredWidth || Signal.ActiveSize.cy != (LONG)RequiredHeight))
        {
            continue;
        }

        Status = ModeSetInterface->pfnCreateNewModeInfo(hModeSet, &NewMode);
        if (!NT_SUCCESS(Status))
            break;

        Preference = SoftGpuTargetModePreferred(Device, Output, Index) ?
                         D3DKMDT_MP_PREFERRED : D3DKMDT_MP_NOTPREFERRED;
        NewMode->VideoSignalInfo = Signal;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        NewMode->WireFormatAndPreference.Value = 0;
        NewMode->WireFormatAndPreference.Preference = Preference;
        NewMode->WireFormatAndPreference.Rgb =
            D3DKMDT_BITS_PER_COMPONENT_08;
#else
        NewMode->Preference = Preference;
#endif

        Status = ModeSetInterface->pfnAddMode(hModeSet, NewMode);
        if (!NT_SUCCESS(Status))
            ModeSetInterface->pfnReleaseModeInfo(hModeSet, NewMode);
    }

    if (NT_SUCCESS(Status))
        Status = VidPnInterface->pfnAssignTargetModeSet(hVidPn, TargetId, hModeSet);
    if (!NT_SUCCESS(Status))
        VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);

    return Status;
}

static VOID
SoftGpuQueryPinnedSourceSize(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    D3DKMDT_HVIDPNSOURCEMODESET hModeSet = 0;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;

    *Width = 0;
    *Height = 0;
    if (!NT_SUCCESS(VidPnInterface->pfnAcquireSourceModeSet(hVidPn, SourceId, &hModeSet, &ModeSetInterface)))
        return;

    if (ModeSetInterface != NULL &&
        NT_SUCCESS(ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode)) &&
        PinnedMode != NULL)
    {
        if (PinnedMode->Type == D3DKMDT_RMT_GRAPHICS)
        {
            *Width = (ULONG)PinnedMode->Format.Graphics.PrimSurfSize.cx;
            *Height = (ULONG)PinnedMode->Format.Graphics.PrimSurfSize.cy;
        }
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
    }
    VidPnInterface->pfnReleaseSourceModeSet(hVidPn, hModeSet);
}

static VOID
SoftGpuQueryPinnedTargetSize(
    _In_ D3DKMDT_HVIDPN hVidPn,
    _In_ CONST DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    D3DKMDT_HVIDPNTARGETMODESET hModeSet = 0;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;

    *Width = 0;
    *Height = 0;
    if (!NT_SUCCESS(VidPnInterface->pfnAcquireTargetModeSet(hVidPn, TargetId, &hModeSet, &ModeSetInterface)))
        return;

    if (ModeSetInterface != NULL &&
        NT_SUCCESS(ModeSetInterface->pfnAcquirePinnedModeInfo(hModeSet, &PinnedMode)) &&
        PinnedMode != NULL)
    {
        *Width = (ULONG)PinnedMode->VideoSignalInfo.ActiveSize.cx;
        *Height = (ULONG)PinnedMode->VideoSignalInfo.ActiveSize.cy;
        ModeSetInterface->pfnReleaseModeInfo(hModeSet, PinnedMode);
    }
    VidPnInterface->pfnReleaseTargetModeSet(hVidPn, hModeSet);
}

/*
 * SoftGpuDdiEnumVidPnCofuncModality
 *
 * Populate unpinned source/target mode sets on every path of the
 * constraining VidPN with the current display mode (Basic Display model:
 * a single mode, the boot framebuffer's).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiEnumVidPnCofuncModality(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *NextPath = NULL;
    D3DKMDT_VIDPN_PRESENT_PATH UpdatedPath;
    SOFTGPU_VIDPN_TRANSFORM_POLICY TransformPolicy;
    SOFTGPU_PATH_SET Paths;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG Index;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        EnumCofuncModality == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL ||
        Device->Width == 0 || Device->Height == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 EnumCofuncModality->hConstrainingVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = VidPnInterface->pfnGetTopology(EnumCofuncModality->hConstrainingVidPn,
                                            &hTopology,
                                            &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;

    SoftGpuLockOutputs(Device);
    Status = SoftGpuCollectPaths(TopologyInterface, hTopology, &Paths);
    if (!NT_SUCCESS(Status) || Paths.Count == 0)
        goto Unlock;
    if (Paths.Overflow)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }
    for (Index = 0; Index < Paths.Count; Index++)
    {
        SoftGpuQueryPinnedTargetSize(EnumCofuncModality->hConstrainingVidPn,
                                     VidPnInterface,
                                     Paths.Paths[Index].TargetId,
                                     &Paths.Paths[Index].PinnedWidth,
                                     &Paths.Paths[Index].PinnedHeight);
    }

    Status = TopologyInterface->pfnAcquireFirstPathInfo(hTopology, &Path);
    if (!NT_SUCCESS(Status))
        goto Unlock;
    if (Path == NULL)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }

    while (Path != NULL)
    {
        SoftGpuQueryPinnedSourceSize(EnumCofuncModality->hConstrainingVidPn,
                                     VidPnInterface,
                                     Path->VidPnSourceId,
                                     &SourceWidth,
                                     &SourceHeight);

        if (!SoftGpuTargetKnown(Device, Path->VidPnTargetId))
        {
            Status = SoftGpuReleasePathInfo(TopologyInterface,
                                            hTopology,
                                            Path,
                                            STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET);
            goto Unlock;
        }

        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
              EnumCofuncModality->EnumPivot.VidPnSourceId == Path->VidPnSourceId))
        {
            Status = SoftGpuUpdateSourceModeSet(Device,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnSourceId,
                                                &Paths);
            if (!NT_SUCCESS(Status))
            {
                Status = SoftGpuReleasePathInfo(TopologyInterface,
                                                hTopology,
                                                Path,
                                                Status);
                goto Unlock;
            }
        }

        if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
              EnumCofuncModality->EnumPivot.VidPnTargetId == Path->VidPnTargetId))
        {
            Status = SoftGpuUpdateTargetModeSet(Device,
                                                EnumCofuncModality->hConstrainingVidPn,
                                                VidPnInterface,
                                                Path->VidPnTargetId,
                                                SourceWidth,
                                                SourceHeight);
            if (!NT_SUCCESS(Status))
            {
                Status = SoftGpuReleasePathInfo(TopologyInterface,
                                                hTopology,
                                                Path,
                                                Status);
                goto Unlock;
            }
        }

        UpdatedPath = *Path;
        SoftGpuVidPnEvaluateTransformPolicy(
            EnumCofuncModality->EnumPivotType,
            EnumCofuncModality->EnumPivot.VidPnSourceId,
            EnumCofuncModality->EnumPivot.VidPnTargetId,
            Path->VidPnSourceId,
            Path->VidPnTargetId,
            Path->ContentTransformation.Scaling,
            Path->ContentTransformation.Rotation,
            &TransformPolicy);

        if (TransformPolicy.UpdateScalingSupport)
        {
            RtlZeroMemory(
                &UpdatedPath.ContentTransformation.ScalingSupport,
                sizeof(UpdatedPath.ContentTransformation.ScalingSupport));
            UpdatedPath.ContentTransformation.ScalingSupport.Identity = 1;
        }

        if (TransformPolicy.UpdateRotationSupport)
        {
            RtlZeroMemory(
                &UpdatedPath.ContentTransformation.RotationSupport,
                sizeof(UpdatedPath.ContentTransformation.RotationSupport));
            UpdatedPath.ContentTransformation.RotationSupport.Identity = 1;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1300)
            UpdatedPath.ContentTransformation.RotationSupport.Offset0 = 1;
#endif
        }

        if (TransformPolicy.UpdateScalingSupport ||
            TransformPolicy.UpdateRotationSupport)
        {
            Status = TopologyInterface->pfnUpdatePathSupportInfo(
                         hTopology,
                         &UpdatedPath);
            if (!NT_SUCCESS(Status))
            {
                Status = SoftGpuReleasePathInfo(TopologyInterface,
                                                hTopology,
                                                Path,
                                                Status);
                goto Unlock;
            }
        }

        NextPath = NULL;
        Status = TopologyInterface->pfnAcquireNextPathInfo(hTopology, Path, &NextPath);
        if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
        {
            Status = SoftGpuReleasePathInfo(TopologyInterface,
                                            hTopology,
                                            Path,
                                            STATUS_SUCCESS);
            goto Unlock;
        }
        if (!NT_SUCCESS(Status))
        {
            Status = SoftGpuReleasePathInfo(TopologyInterface,
                                            hTopology,
                                            Path,
                                            Status);
            goto Unlock;
        }
        if (NextPath == NULL)
        {
            Status = SoftGpuReleasePathInfo(
                                            TopologyInterface,
                                            hTopology,
                                            Path,
                                            STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY);
            goto Unlock;
        }

        ReleaseStatus =
            TopologyInterface->pfnReleasePathInfo(hTopology, Path);
        if (!NT_SUCCESS(ReleaseStatus))
        {
            TopologyInterface->pfnReleasePathInfo(hTopology, NextPath);
            Status = ReleaseStatus;
            goto Unlock;
        }
        Path = NextPath;
    }

    Status = STATUS_SUCCESS;

Unlock:
    SoftGpuUnlockOutputs(Device);
    return Status;
}


static NTSTATUS
SoftGpuValidateFunctionalVidPn(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ D3DKMDT_HVIDPN hFunctionalVidPn,
    _Out_ PSOFTGPU_PATH_SET Paths,
    _Out_ PULONG SourceMode)
{
    CONST DXGK_VIDPN_INTERFACE *VidPnInterface = NULL;
    D3DKMDT_HVIDPNTOPOLOGY hTopology = 0;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface = NULL;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG Index;
    NTSTATUS Status;

    RtlZeroMemory(Paths, sizeof(*Paths));
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    *SourceMode = Device->CurrentMode;
    if (hFunctionalVidPn == 0 ||
        Device->DxgkInterface.DxgkCbQueryVidPnInterface == NULL)
    {
        return STATUS_GRAPHICS_INVALID_VIDPN;
    }

    Status = Device->DxgkInterface.DxgkCbQueryVidPnInterface(
                 hFunctionalVidPn,
                 DXGK_VIDPN_INTERFACE_VERSION_V1,
                 &VidPnInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (VidPnInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;

    Status = VidPnInterface->pfnGetTopology(hFunctionalVidPn,
                                            &hTopology,
                                            &TopologyInterface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TopologyInterface == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    Status = SoftGpuCollectPaths(TopologyInterface, hTopology, Paths);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Paths->Count == 0)
        return STATUS_SUCCESS;
    if (!SoftGpuPathTargetsValid(Device, Paths))
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    for (Index = 0; Index < Paths->Count; Index++)
    {
        if (!SoftGpuVidPnActiveTransformSupported(Paths->Paths[Index].Scaling,
                                                  Paths->Paths[Index].Rotation))
            return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
        if (Paths->Paths[Index].GammaRampType != D3DDDI_GAMMARAMP_DEFAULT)
            return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    }

    Status = SoftGpuValidateCommittedSourceMode(
                 Device,
                 hFunctionalVidPn,
                 VidPnInterface,
                 SourceMode);
    if (!NT_SUCCESS(Status))
        return Status;
    SoftGpuGetDisplayModeSize(Device, *SourceMode, &SourceWidth, &SourceHeight);

    for (Index = 0; Index < Paths->Count; Index++)
    {
        PSOFTGPU_PATH Path = &Paths->Paths[Index];

        Status = SoftGpuValidateCommittedTargetMode(
                     Device,
                     hFunctionalVidPn,
                     Path->TargetId,
                     VidPnInterface,
                     &Path->ModeIndex);
        if (!NT_SUCCESS(Status))
            return Status;

        SoftGpuGetTargetModeSignal(Device, SoftGpuFindOutput(Device, Path->TargetId), Path->ModeIndex, &Signal);
        if (Signal.ActiveSize.cx != (LONG)SourceWidth || Signal.ActiveSize.cy != (LONG)SourceHeight)
            return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
SoftGpuSetDisplayMode(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ ULONG ModeIndex)
{
    const SOFTGPU_DISPLAY_MODE *Mode;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Device->ModeCount == 0 || ModeIndex == Device->CurrentMode)
        return STATUS_SUCCESS;
    if (ModeIndex >= Device->ModeCount || Device->PlatformSetMode == NULL)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

    Mode = &Device->Modes[ModeIndex];
    Status = KeWaitForSingleObject(&Device->ScanoutMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Device->PlatformSetMode(Device, Mode);
    if (NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->Width = Mode->Width;
        Device->Height = Mode->Height;
        Device->ScanoutPitch = Mode->Pitch;
        Device->CurrentMode = ModeIndex;
        Device->CurrentPrimaryValid = FALSE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

        if (Device->Scanout != NULL)
            RtlZeroMemory(Device->Scanout, (SIZE_T)Mode->Pitch * Mode->Height);
    }

    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
    DPRINT("SOFTGPU: display mode %lux%lu pitch %lu: 0x%08lx\n",
            Mode->Width, Mode->Height, Mode->Pitch, Status);
    return Status;
}

static NTSTATUS
SoftGpuSetOutputs(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PATH_SET *Paths,
    _In_ ULONG ChildMode,
    _In_ BOOLEAN ChildActive)
{
    SOFTGPU_OUTPUT_TIMING Timings[SOFTGPU_MAX_PATHS];
    const SOFTGPU_DISPLAY_MODE *Mode;
    const SOFTGPU_PATH *Path;
    PSOFTGPU_OUTPUT Output;
    BOOLEAN ModeChanged;
    BOOLEAN Changed;
    KIRQL OldIrql;
    ULONG Index;
    NTSTATUS Status;

    if (ChildMode >= Device->ModeCount)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

    RtlZeroMemory(Timings, sizeof(Timings));
    Mode = &Device->Modes[ChildMode];
    Timings[0].ChildUid = SOFTGPU_CHILD_UID;
    Timings[0].Active = ChildActive;
    Timings[0].Mode = *Mode;
    ModeChanged = ChildMode != Device->CurrentMode;
    Changed = ModeChanged || ChildActive != Device->ChildActive;
    for (Index = 0; Index < Device->ExtraOutputCount; Index++)
    {
        Output = &Device->ExtraOutputs[Index];
        Path = SoftGpuFindPath(Paths, Output->ChildUid);
        Timings[1 + Index].ChildUid = Output->ChildUid;
        Timings[1 + Index].Active = Path != NULL;
        if (Path != NULL)
        {
            if (Path->ModeIndex >= Output->ModeCount)
                return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;
            Timings[1 + Index].Mode = Output->Modes[Path->ModeIndex];
            Changed |= !Output->Active || Path->ModeIndex != Output->CurrentMode;
        }
        else
        {
            Changed |= Output->Active;
        }
    }
    if (!Changed)
        return STATUS_SUCCESS;

    Status = KeWaitForSingleObject(&Device->ScanoutMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Device->PlatformSetOutputs(Device, Timings, 1 + Device->ExtraOutputCount);
    if (NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        if (ModeChanged)
        {
            Device->Width = Mode->Width;
            Device->Height = Mode->Height;
            Device->ScanoutPitch = Mode->Pitch;
            Device->CurrentMode = ChildMode;
            Device->CurrentPrimaryValid = FALSE;
        }
        Device->ChildActive = ChildActive;
        Device->VsyncTargetId = SOFTGPU_CHILD_UID;
        for (Index = Device->ExtraOutputCount; Index-- > 0;)
        {
            Output = &Device->ExtraOutputs[Index];
            Path = SoftGpuFindPath(Paths, Output->ChildUid);
            Output->Active = Path != NULL;
            Output->CurrentMode = Path != NULL ? Path->ModeIndex : MAXULONG;
            if (Output->Active && !ChildActive)
                Device->VsyncTargetId = Output->ChildUid;
        }
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

        if (ModeChanged && Device->Scanout != NULL)
            RtlZeroMemory(Device->Scanout, (SIZE_T)Mode->Pitch * Mode->Height);
    }

    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
    DPRINT("SOFTGPU: %lu path(s), source %lux%lu, child 0 %s: 0x%08lx\n",
           Paths->Count, Mode->Width, Mode->Height, ChildActive ? "on" : "off", Status);
    return Status;
}

static NTSTATUS
SoftGpuApplyPaths(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PATH_SET *Paths,
    _In_ ULONG SourceMode)
{
    const SOFTGPU_PATH *Child = SoftGpuFindPath(Paths, SOFTGPU_CHILD_UID);

    if (Device->PlatformSetOutputs == NULL)
    {
        if (Child == NULL)
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        return SoftGpuSetDisplayMode(Device, Child->ModeIndex);
    }
    return SoftGpuSetOutputs(Device,
                             Paths,
                             Child != NULL ? Child->ModeIndex : SourceMode,
                             Child != NULL);
}


/* =========================================================================
 * DxgkDdiCommitVidPn
 * =========================================================================
 */

/*
 * Activate the new VidPN for pre-WDDM 2.3 callers. StartDevice has already
 * fixed the adapter geometry from the validated POST/loader framebuffer.
 * Commit validates the pinned source and target modes against that geometry;
 * it never substitutes a fallback mode.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCommitVidPn(
    _In_ PVOID                      MiniportDeviceContext,
    _In_ CONST DXGKARG_COMMITVIDPN *CommitVidPn)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    SOFTGPU_PATH_SET Paths;
    ULONG SourceMode;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        CommitVidPn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: CommitVidPn hFunctionalVidPn=%p\n",
           (PVOID)CommitVidPn->hFunctionalVidPn);

    if (CommitVidPn->AffectedVidPnSourceId != 0 &&
        CommitVidPn->AffectedVidPnSourceId != D3DDDI_ID_ALL)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_IGNORE &&
        CommitVidPn->MonitorConnectivityChecks != D3DKMDT_MCC_ENFORCE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SoftGpuLockOutputs(Device);
    Status = SoftGpuValidateFunctionalVidPn(
                 Device,
                 CommitVidPn->hFunctionalVidPn,
                 &Paths,
                 &SourceMode);
    if (!NT_SUCCESS(Status))
    {
        SoftGpuUnlockOutputs(Device);
        DPRINT1("SOFTGPU: CommitVidPn rejected functional VidPN 0x%08lx\n",
                Status);
        return Status;
    }

    if (Paths.Count != 0)
        Status = SoftGpuApplyPaths(Device, &Paths, SourceMode);
    else if (Device->PlatformSetOutputs != NULL)
        Status = SoftGpuSetOutputs(Device, &Paths, Device->CurrentMode, Device->ChildActive);
    SoftGpuUnlockOutputs(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT("SOFTGPU: CommitVidPn: committed %lux%lu format=%d\n",
           Device->Width, Device->Height, (int)Device->Format);

    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2300)
/*
 * WDDM 2.3 timing transaction. The platform binding exposes a single fixed
 * firmware scanout, so this callback accepts only that pinned mode, SDR
 * G22/P709 over 8-bit RGB, and target zero. No hardware queue, MPO, protected
 * session, or programmable-link capability is implied.
 */
NTSTATUS
APIENTRY
SoftGpuDdiSetTimingsFromVidPn(
    _In_ PVOID MiniportDeviceContext,
    IN_OUT_PDXGKARG_SETTIMINGSFROMVIDPN SetTimings)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    DXGK_SET_TIMING_PATH_INFO *PathInfo;
    const SOFTGPU_OUTPUT *Output;
    SOFTGPU_PATH_SET Paths;
    ULONG SourceMode;
    ULONG Timed = 0;
    ULONG Index;
    ULONG Other;
    BOOLEAN Active = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetTimings == NULL ||
        SetTimings->pResultsFlags == NULL ||
        SetTimings->SetFlags.Value != 0 ||
        SetTimings->PathCount > 1 + Device->ExtraOutputCount ||
        (SetTimings->PathCount != 0 &&
         SetTimings->pSetTimingPathInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SetTimings->pResultsFlags->Value = 0;
    SoftGpuLockOutputs(Device);
    Status = SoftGpuValidateFunctionalVidPn(
                 Device,
                 SetTimings->hFunctionalVidPn,
                 &Paths,
                 &SourceMode);
    if (!NT_SUCCESS(Status))
        goto Unlock;

    for (Index = 0; Index < SetTimings->PathCount; Index++)
    {
        BOOLEAN InTopology;

        PathInfo = &SetTimings->pSetTimingPathInfo[Index];
        if (!SoftGpuTargetKnown(Device, PathInfo->VidPnTargetId) ||
            (PathInfo->InputFlags & ~0x1FUL) != 0)
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

        InTopology = SoftGpuFindPath(&Paths, PathInfo->VidPnTargetId) != NULL;
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

        if (PathInfo->OutputWireColorSpace !=
                D3DDDI_OUTPUT_WIRE_COLOR_SPACE_G22_P709 ||
            PathInfo->SelectedWireFormat.Preference != 0 ||
            PathInfo->SelectedWireFormat.Rgb !=
                D3DKMDT_BITS_PER_COMPONENT_08 ||
            PathInfo->SelectedWireFormat.YCbCr444 != 0 ||
            PathInfo->SelectedWireFormat.YCbCr422 != 0 ||
            PathInfo->SelectedWireFormat.YCbCr420 != 0 ||
            PathInfo->SelectedWireFormat.Intensity != 0)
        {
            Status = STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
            goto Unlock;
        }
        if (PathInfo->Input.Active)
            Active = TRUE;
        Timed++;
    }
    if (Timed != Paths.Count)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        goto Unlock;
    }

    if (Paths.Count != 0)
        Status = SoftGpuApplyPaths(Device, &Paths, SourceMode);
    else if (Device->PlatformSetOutputs != NULL)
        Status = SoftGpuSetOutputs(Device, &Paths, Device->CurrentMode, Device->ChildActive);
    if (!NT_SUCCESS(Status))
        goto Unlock;

    for (Index = 0; Index < SetTimings->PathCount; Index++)
    {
        PathInfo = &SetTimings->pSetTimingPathInfo[Index];
        Output = SoftGpuFindOutput(Device, PathInfo->VidPnTargetId);
        PathInfo->OutputFlags = 0;
        RtlZeroMemory(&PathInfo->TargetState,
                      sizeof(PathInfo->TargetState));
        PathInfo->TargetState.ConnectionChangeId = 1;
        PathInfo->TargetState.TargetId = PathInfo->VidPnTargetId;
        PathInfo->TargetState.ConnectionStatus =
            Output == NULL || Output->Connected ? MonitorStatusConnected : MonitorStatusDisconnected;
        PathInfo->TargetState.MonitorConnect.LinkTargetType =
            Output != NULL ? Output->OutputTechnology : Device->ChildOutputTechnology;
        PathInfo->DiagnosticInfo = 0;
        PathInfo->GlitchCause = DXGK_GLITCH_CAUSE_NONE;
        PathInfo->GlitchEffect = DXGK_GLITCH_EFFECT_SEAMLESS;
        PathInfo->GlitchDuration = DXGK_GLITCH_DURATION_NONE;
    }
    SoftGpuUnlockOutputs(Device);

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Device->TimingActive = Active;
    if (++Device->ScanoutGeneration == 0)
        ++Device->ScanoutGeneration;
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    Status = SoftGpuScheduleScanout(Device);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT("SOFTGPU: SetTimingsFromVidPn path=%lu active=%u\n",
           Paths.Count,
           Active);
    return STATUS_SUCCESS;

Unlock:
    SoftGpuUnlockOutputs(Device);
    return Status;
}
#endif


/* =========================================================================
 * DxgkDdiSetVidPnSourceAddress
 * =========================================================================
 */

typedef struct _SOFTGPU_SCANOUT_SNAPSHOT
{
    PVOID Source;
    SIZE_T SourceSize;
    PVOID Destination;
    SIZE_T DestinationSize;
    ULONG SourcePitch;
    ULONG DestinationPitch;
    ULONG Width;
    ULONG Height;
    ULONG Generation;
    PHYSICAL_ADDRESS PrimaryPhysicalAddress;
    BOOLEAN Valid;
    BOOLEAN Visible;
    BOOLEAN Direct;
} SOFTGPU_SCANOUT_SNAPSHOT, *PSOFTGPU_SCANOUT_SNAPSHOT;

static NTSTATUS
SoftGpuCopyCurrentPrimaryToScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    SOFTGPU_SCANOUT_SNAPSHOT Snapshot;
    KIRQL OldIrql;
    RECT FrameRect;
    BOOLEAN PointerRestored = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    Status = KeWaitForSingleObject(&Device->ScanoutMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Snapshot, sizeof(Snapshot));
    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) == 0 &&
        Device->FrameBuffer != NULL &&
        Device->CurrentPrimaryOffset <= Device->FrameBufferSize)
    {
        Snapshot.Source =
            (PUCHAR)Device->FrameBuffer +
            Device->CurrentPrimaryOffset;
        Snapshot.SourceSize =
            Device->FrameBufferSize -
            (SIZE_T)Device->CurrentPrimaryOffset;
        Snapshot.Destination = Device->Scanout;
        Snapshot.DestinationSize = Device->ScanoutSize;
        Snapshot.SourcePitch = Device->CurrentPrimaryPitch;
        Snapshot.DestinationPitch = Device->ScanoutPitch;
        Snapshot.Width = Device->CurrentPrimaryWidth;
        Snapshot.Height = Device->CurrentPrimaryHeight;
        Snapshot.Generation = Device->ScanoutGeneration;
        Snapshot.Valid = Device->CurrentPrimaryValid;
        Snapshot.Visible =
            Device->ScanoutVisible && Device->TimingActive;
        Snapshot.Direct = Device->PlatformDirectScanout;
        Snapshot.PrimaryPhysicalAddress.QuadPart =
            Device->FrameBufferPhys.QuadPart +
            Device->CurrentPrimaryOffset;
    }
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (Snapshot.Direct)
    {
        if (Snapshot.Valid)
        {
            Status = SoftGpuPlatformSetPrimary(
                         Device,
                         Snapshot.PrimaryPhysicalAddress,
                         Snapshot.SourcePitch,
                         Snapshot.Width,
                         Snapshot.Height,
                         Snapshot.Visible);
        }
        else
        {
            Status = STATUS_SUCCESS;
        }
        goto Complete;
    }

    if (Snapshot.Destination == NULL)
    {
        /* A genuinely headless generic adapter has no firmware scanout. */
        Status = STATUS_SUCCESS;
        goto Complete;
    }
    if (!Snapshot.Valid)
    {
        /*
         * Initial timing and visibility transactions precede the first
         * SetVidPnSourceAddress call. There is no primary to copy yet; that
         * callback increments the generation and schedules the first present.
         */
        Status = STATUS_SUCCESS;
        goto Complete;
    }
    if (Snapshot.Source == NULL ||
        Snapshot.Width == 0 ||
        Snapshot.Height == 0 ||
        Snapshot.Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Complete;
    }

    FrameRect.left = 0;
    FrameRect.top = 0;
    FrameRect.right = (LONG)Snapshot.Width;
    FrameRect.bottom = (LONG)Snapshot.Height;
    if (Snapshot.Visible)
    {
        (VOID)SoftGpuPlatformWaitForVerticalBlank(Device);
        KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
        SoftGpuPointerRestoreLocked(Device);
        KeReleaseSpinLock(&Device->PointerLock, OldIrql);
        PointerRestored = TRUE;
        Status = SoftGpu2dCopyRect(
                     Snapshot.Source,
                     Snapshot.SourceSize,
                     Snapshot.SourcePitch,
                     &FrameRect,
                     Snapshot.Destination,
                     Snapshot.DestinationSize,
                     Snapshot.DestinationPitch,
                     &FrameRect);
    }
    else
    {
        (VOID)SoftGpuPlatformWaitForVerticalBlank(Device);
        KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
        SoftGpuPointerRestoreLocked(Device);
        KeReleaseSpinLock(&Device->PointerLock, OldIrql);
        PointerRestored = TRUE;
        Status = SoftGpu2dFillRect(
                     Snapshot.Destination,
                     Snapshot.DestinationSize,
                     Snapshot.DestinationPitch,
                     &FrameRect,
                     0);
    }
    if (!NT_SUCCESS(Status))
        goto Complete;
    SoftGpuPlatformScanoutWritten(Device, &FrameRect);

Complete:
    if (PointerRestored)
    {
        KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
        SoftGpuPointerDrawLocked(Device);
        KeMemoryBarrier();
        KeReleaseSpinLock(&Device->PointerLock, OldIrql);
    }
    if (NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        if (Device->ScanoutGeneration == Snapshot.Generation)
        {
            Device->ScanoutPresentedGeneration =
                Snapshot.Generation;
        }
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }
    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
    return Status;
}

static VOID
NTAPI
SoftGpuScanoutWorker(
    _In_ PVOID Context)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)Context;

    for (;;)
    {
        NTSTATUS Status =
            SoftGpuCopyCurrentPrimaryToScanout(Device);
        KIRQL OldIrql;
        BOOLEAN Repeat;

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Repeat =
            NT_SUCCESS(Status) &&
            InterlockedCompareExchange(&Device->Stopped, 0, 0) == 0 &&
            Device->ScanoutPresentedGeneration !=
                Device->ScanoutGeneration;
        if (!Repeat)
            InterlockedExchange(&Device->ScanoutWorkQueued, 0);
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

        if (!Repeat)
            break;
    }

    ExReleaseRundownProtection(&Device->ScanoutRundown);
}

VOID
SoftGpuScanoutInitializeDevice(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KeInitializeSpinLock(&Device->ScanoutLock);
    KeInitializeMutex(&Device->ScanoutMutex, 0);
    KeInitializeMutex(&Device->PointerMutex, 0);
    KeInitializeSpinLock(&Device->PointerLock);
    KeInitializeMutex(&Device->OutputMutex, 0);
    ExInitializeRundownProtection(&Device->ScanoutRundown);
    ExInitializeWorkItem(&Device->ScanoutWorkItem,
                         SoftGpuScanoutWorker,
                         Device);
    Device->ScanoutRundownCompleted = FALSE;
    Device->ScanoutWorkQueued = 0;
}

static BOOLEAN
SoftGpuValidateOutputModes(
    _In_reads_opt_(ModeCount) const SOFTGPU_DISPLAY_MODE *Modes,
    _In_ ULONG ModeCount,
    _In_ ULONG PreferredMode)
{
    ULONG Index;

    if (ModeCount > SOFTGPU_MAX_DISPLAY_MODES ||
        (ModeCount != 0 && (Modes == NULL || PreferredMode >= ModeCount)))
    {
        return FALSE;
    }
    for (Index = 0; Index < ModeCount; Index++)
    {
        if (Modes[Index].Width == 0 ||
            Modes[Index].Height == 0 ||
            Modes[Index].Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
            Modes[Index].Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
            (Modes[Index].PixelRate != 0 &&
             (Modes[Index].TotalWidth < Modes[Index].Width ||
              Modes[Index].TotalHeight < Modes[Index].Height ||
              (ULONGLONG)Modes[Index].TotalWidth * Modes[Index].TotalHeight > MAXULONG)))
        {
            return FALSE;
        }
    }
    return TRUE;
}

BOOLEAN
SoftGpuValidateExtraOutputs(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PLATFORM_CONFIG *Config)
{
    PSOFTGPU_OUTPUT Output;
    ULONG Index;
    ULONG Other;

    if (Config->ExtraOutputCount == 0)
        return TRUE;
    if (Config->ExtraOutputCount > SOFTGPU_MAX_EXTRA_OUTPUTS ||
        Config->SetOutputs == NULL ||
        Config->ModeCount == 0)
    {
        return FALSE;
    }

    for (Index = 0; Index < Config->ExtraOutputCount; Index++)
    {
        Output = &Device->ExtraOutputs[Index];
        if (Output->ChildUid == SOFTGPU_CHILD_UID ||
            Output->ChildUid == DISPLAY_ADAPTER_HW_ID ||
            Output->EdidLength > SOFTGPU_EDID_SIZE ||
            !SoftGpuValidateOutputModes(Output->Modes, Output->ModeCount, Output->PreferredMode))
        {
            return FALSE;
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (Device->ExtraOutputs[Other].ChildUid == Output->ChildUid)
                return FALSE;
        }
        Output->Active = FALSE;
        Output->CurrentMode = MAXULONG;
    }
    return TRUE;
}

NTSTATUS
SoftGpuUpdateOutput(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ ULONG ChildUid,
    _In_ BOOLEAN Connected,
    _In_reads_opt_(ModeCount) const SOFTGPU_DISPLAY_MODE *Modes,
    _In_ ULONG ModeCount,
    _In_ ULONG PreferredMode,
    _In_reads_bytes_opt_(EdidLength) const UCHAR *Edid,
    _In_ ULONG EdidLength,
    _In_ BOOLEAN Indicate)
{
    PSOFTGPU_OUTPUT Output;
    DXGK_CHILD_STATUS ChildStatus;

    PAGED_CODE();

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Output = SoftGpuFindOutput(Device, ChildUid);
    if (Output == NULL ||
        !SoftGpuValidateOutputModes(Modes, ModeCount, PreferredMode) ||
        EdidLength > SOFTGPU_EDID_SIZE ||
        (EdidLength != 0 && Edid == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    SoftGpuLockOutputs(Device);
    Output->Connected = Connected;
    Output->ModeCount = ModeCount;
    Output->PreferredMode = ModeCount != 0 ? PreferredMode : 0;
    Output->CurrentMode = MAXULONG;
    RtlZeroMemory(Output->Modes, sizeof(Output->Modes));
    if (ModeCount != 0)
        RtlCopyMemory(Output->Modes, Modes, ModeCount * sizeof(*Modes));
    RtlZeroMemory(Output->Edid, sizeof(Output->Edid));
    if (EdidLength != 0)
        RtlCopyMemory(Output->Edid, Edid, EdidLength);
    Output->EdidLength = EdidLength;
    SoftGpuUnlockOutputs(Device);

    DPRINT1("SOFTGPU: output %lu %s, %lu mode(s), %lu-byte EDID\n",
            ChildUid, Connected ? "connected" : "disconnected", ModeCount, EdidLength);
    if (!Indicate || Device->DxgkInterface.DxgkCbIndicateChildStatus == NULL)
        return STATUS_SUCCESS;

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));
    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = ChildUid;
    ChildStatus.HotPlug.Connected = Connected;
    return Device->DxgkInterface.DxgkCbIndicateChildStatus(Device->DxgkInterface.DeviceHandle, &ChildStatus);
}

NTSTATUS
SoftGpuScanoutStart(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_PLATFORM_CONFIG *Config)
{
    PVOID Mapping;
    SIZE_T MappingSize;
    ULONGLONG RequiredSize;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Device == NULL || Config == NULL ||
        Device->Magic != SOFTGPU_DEVICE_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->ScanoutRundownCompleted)
    {
        ExReInitializeRundownProtection(&Device->ScanoutRundown);
        Device->ScanoutRundownCompleted = FALSE;
    }

    if (Config->ScanoutPhysicalAddress.QuadPart == 0)
        return STATUS_SUCCESS;

    if (Device->Scanout != NULL ||
        Config->Width == 0 ||
        Config->Height == 0 ||
        Config->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Config->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Config->Width >
            MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Config->ScanoutPitch <
            Config->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Config->ScanoutPitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        Config->ScanoutSize == 0 ||
        Config->ScanoutSize > SOFTGPU_MAX_SURFACE_SIZE ||
        Config->ScanoutSize > MAXULONG_PTR)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RequiredSize =
        (ULONGLONG)Config->ScanoutPitch * Config->Height;
    if (RequiredSize == 0 || RequiredSize > Config->ScanoutSize)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    MappingSize = (SIZE_T)Config->ScanoutSize;
    Mapping = Config->ScanoutShadow;
    if (Mapping == NULL)
    {
        Mapping = MmMapIoSpace(Config->ScanoutPhysicalAddress,
                               MappingSize,
                               MmWriteCombined);
    }
    if (Mapping == NULL)
    {
        Mapping = MmMapIoSpace(Config->ScanoutPhysicalAddress,
                               MappingSize,
                               MmNonCached);
    }
    if (Mapping == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Device->Scanout = Mapping;
    Device->ScanoutPhys = Config->ScanoutPhysicalAddress;
    Device->ScanoutSize = MappingSize;
    Device->ScanoutPitch = Config->ScanoutPitch;
    Device->ScanoutShadowed = Config->ScanoutShadow != NULL;
    Device->CurrentPrimaryOffset = 0;
    Device->CurrentPrimaryPitch = 0;
    Device->CurrentPrimaryWidth = 0;
    Device->CurrentPrimaryHeight = 0;
    Device->CurrentPrimaryValid = FALSE;
    Device->ScanoutVisible = TRUE;
    Device->TimingActive = TRUE;
    Device->ScanoutGeneration = 1;
    Device->ScanoutPresentedGeneration = 0;
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
    Device->PointerBackingValid = FALSE;
    KeReleaseSpinLock(&Device->PointerLock, OldIrql);
    SoftGpuPlatformInitializeTiming(Device);
    return STATUS_SUCCESS;
}

VOID
SoftGpuScanoutStop(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PVOID Mapping;
    SIZE_T MappingSize;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Device == NULL)
        return;

    if (!Device->ScanoutRundownCompleted)
    {
        ExWaitForRundownProtectionRelease(&Device->ScanoutRundown);
        Device->ScanoutRundownCompleted = TRUE;
    }

    KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
    SoftGpuPointerRestoreLocked(Device);
    KeMemoryBarrier();
    KeReleaseSpinLock(&Device->PointerLock, OldIrql);
    if (Device->PresentStage != NULL)
    {
        ExFreePoolWithTag(Device->PresentStage, SOFTGPU_POOL_TAG);
        Device->PresentStage = NULL;
        Device->PresentStageWidth = 0;
    }
    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    Mapping = Device->ScanoutShadowed ? NULL : Device->Scanout;
    MappingSize = Device->ScanoutSize;
    Device->ScanoutShadowed = FALSE;
    Device->Scanout = NULL;
    Device->ScanoutPhys.QuadPart = 0;
    Device->ScanoutSize = 0;
    Device->ScanoutPitch = 0;
    Device->CurrentPrimaryValid = FALSE;
    Device->ScanoutVisible = FALSE;
    Device->TimingActive = FALSE;
    Device->ScanoutGeneration++;
    Device->ScanoutPresentedGeneration =
        Device->ScanoutGeneration;
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (Mapping != NULL && MappingSize != 0)
        MmUnmapIoSpace(Mapping, MappingSize);
}

static BOOLEAN
SoftGpuClipPresentRect(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Input,
    _Out_ RECT *Output)
{
    *Output = *Input;
    if (Output->left < 0)
        Output->left = 0;
    if (Output->top < 0)
        Output->top = 0;
    if (Output->right > (LONG)Device->Width)
        Output->right = (LONG)Device->Width;
    if (Output->bottom > (LONG)Device->Height)
        Output->bottom = (LONG)Device->Height;

    return Output->left < Output->right && Output->top < Output->bottom;
}

static NTSTATUS
SoftGpuPresentRectStaged(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const VOID *Source,
    _In_ SIZE_T SourceSize,
    _In_ ULONG SourcePitch,
    _Inout_ PVOID Destination,
    _In_ SIZE_T DestinationSize,
    _In_ ULONG DestinationPitch,
    _In_ BOOLEAN Composite,
    _In_ const RECT *Rect)
{
    ULONG Width = (ULONG)(Rect->right - Rect->left);
    SIZE_T RowBytes = (SIZE_T)Width * sizeof(ULONG);
    LONG Top;
    KIRQL OldIrql;

    if (Device->PresentStage == NULL || Device->PresentStageWidth < Width)
    {
        PULONG Stage;

        if (Device->Width < Width)
            return STATUS_INVALID_PARAMETER;
        Stage = ExAllocatePoolWithTag(NonPagedPool,
                                      (SIZE_T)Device->Width * sizeof(ULONG) * SOFTGPU_PRESENT_CHUNK_ROWS,
                                      SOFTGPU_POOL_TAG);
        if (Stage == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Device->PresentStage != NULL)
            ExFreePoolWithTag(Device->PresentStage, SOFTGPU_POOL_TAG);
        Device->PresentStage = Stage;
        Device->PresentStageWidth = Device->Width;
    }
    if ((SIZE_T)(Rect->bottom - 1) * SourcePitch + (SIZE_T)Rect->right * sizeof(ULONG) > SourceSize ||
        (SIZE_T)(Rect->bottom - 1) * DestinationPitch + (SIZE_T)Rect->right * sizeof(ULONG) > DestinationSize)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    for (Top = Rect->top; Top < Rect->bottom; Top += SOFTGPU_PRESENT_CHUNK_ROWS)
    {
        RECT Chunk;
        LONG Rows;
        LONG Y;
        const UCHAR *Src;
        PUCHAR Dst;

        Chunk.left = Rect->left;
        Chunk.top = Top;
        Chunk.right = Rect->right;
        Chunk.bottom = min(Top + (LONG)SOFTGPU_PRESENT_CHUNK_ROWS, Rect->bottom);
        Rows = Chunk.bottom - Chunk.top;
        Src = (const UCHAR *)Source + (SIZE_T)Chunk.top * SourcePitch + (SIZE_T)Chunk.left * sizeof(ULONG);
        Dst = (PUCHAR)Destination + (SIZE_T)Chunk.top * DestinationPitch + (SIZE_T)Chunk.left * sizeof(ULONG);
        for (Y = 0; Y < Rows; ++Y)
            RtlCopyMemory(Device->PresentStage + (SIZE_T)Y * Width, Src + (SIZE_T)Y * SourcePitch, RowBytes);
        KeAcquireSpinLock(&Device->PointerLock, &OldIrql);
        if (Composite)
            SoftGpuPointerCompositeStage(Device, Device->PresentStage, Width, &Chunk);
        for (Y = 0; Y < Rows; ++Y)
            RtlCopyMemory(Dst + (SIZE_T)Y * DestinationPitch, Device->PresentStage + (SIZE_T)Y * Width, RowBytes);
        KeReleaseSpinLock(&Device->PointerLock, OldIrql);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
SoftGpuDdiPresentDisplayOnly(
    _In_ PVOID MiniportDeviceContext,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PVOID Destination;
    SIZE_T DestinationSize;
    ULONG DestinationPitch;
    ULONGLONG PrimaryOffset = 0;
    RECT Rect;
    SIZE_T SourceSize;
    ULONG Index;
    BOOLEAN DirectPrimaryValid = FALSE;
    BOOLEAN Composite;
    NTSTATUS Status;

    PAGED_CODE();

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        PresentDisplayOnly == NULL || PresentDisplayOnly->pSource == NULL ||
        PresentDisplayOnly->VidPnSourceId != 0 ||
        PresentDisplayOnly->BytesPerPixel !=
            SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        PresentDisplayOnly->Pitch <= 0 ||
        PresentDisplayOnly->Flags.Value != 0 ||
        (PresentDisplayOnly->NumMoves != 0 &&
         PresentDisplayOnly->pMoves == NULL) ||
        (PresentDisplayOnly->NumDirtyRects != 0 &&
         PresentDisplayOnly->pDirtyRect == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((ULONG)PresentDisplayOnly->Pitch <
            Device->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Device->Height >
            MAXULONG_PTR / (ULONG)PresentDisplayOnly->Pitch)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    SourceSize =
        (SIZE_T)(ULONG)PresentDisplayOnly->Pitch * Device->Height;

    if (PresentDisplayOnly->NumMoves == 0 &&
        PresentDisplayOnly->NumDirtyRects == 0)
    {
        return STATUS_SUCCESS;
    }
    if (!ExAcquireRundownProtection(&Device->ScanoutRundown))
        return STATUS_DELETE_PENDING;

    Status = KeWaitForSingleObject(&Device->ScanoutMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupRundown;

    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
        goto CleanupMutex;
    }
    if (Device->PlatformDirectScanout)
    {
        Status = SoftGpuPlatformPresentDisplayOnly(Device,
                                                   PresentDisplayOnly);
        if (Status != STATUS_NOT_SUPPORTED)
            goto CleanupMutex;
    }
    if (Device->PlatformDirectScanout)
    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        DirectPrimaryValid = Device->CurrentPrimaryValid;
        if (DirectPrimaryValid)
        {
            PrimaryOffset = Device->CurrentPrimaryOffset;
            DestinationPitch = Device->CurrentPrimaryPitch;
        }
        else
        {
            PrimaryOffset = 0;
            DestinationPitch = Device->ScanoutPitch;
        }
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

        if (DirectPrimaryValid)
        {
            if (PrimaryOffset > Device->FrameBufferSize)
            {
                Status = STATUS_DEVICE_NOT_READY;
                goto CleanupMutex;
            }

            Destination = (PUCHAR)Device->FrameBuffer + PrimaryOffset;
            DestinationSize =
                Device->FrameBufferSize - (SIZE_T)PrimaryOffset;
        }
        else
        {
            Destination = Device->Scanout;
            DestinationSize = Device->ScanoutSize;
        }
    }
    else
    {
        Destination = Device->Scanout;
        DestinationSize = Device->ScanoutSize;
        DestinationPitch = Device->ScanoutPitch;
    }

    if (Destination == NULL)
    {
        Status = STATUS_SUCCESS;
        goto CleanupMutex;
    }

    Composite = !Device->PlatformHardwarePointer && Destination == Device->Scanout;
    Status = STATUS_SUCCESS;
    for (Index = 0; Index < PresentDisplayOnly->NumMoves; ++Index)
    {
        if (!SoftGpuClipPresentRect(
                Device,
                &PresentDisplayOnly->pMoves[Index].DestRect,
                &Rect))
        {
            continue;
        }
        Status = SoftGpuPresentRectStaged(Device,
                                          PresentDisplayOnly->pSource,
                                          SourceSize,
                                          (ULONG)PresentDisplayOnly->Pitch,
                                          Destination,
                                          DestinationSize,
                                          DestinationPitch,
                                          Composite,
                                          &Rect);
        if (!NT_SUCCESS(Status))
            break;
        if (Destination == Device->Scanout)
            SoftGpuPlatformScanoutWritten(Device, &Rect);
    }
    for (Index = 0;
         NT_SUCCESS(Status) && Index < PresentDisplayOnly->NumDirtyRects;
         ++Index)
    {
        if (!SoftGpuClipPresentRect(Device,
                                    &PresentDisplayOnly->pDirtyRect[Index],
                                    &Rect))
        {
            continue;
        }
        Status = SoftGpuPresentRectStaged(Device,
                                          PresentDisplayOnly->pSource,
                                          SourceSize,
                                          (ULONG)PresentDisplayOnly->Pitch,
                                          Destination,
                                          DestinationSize,
                                          DestinationPitch,
                                          Composite,
                                          &Rect);
        if (NT_SUCCESS(Status) && Destination == Device->Scanout)
            SoftGpuPlatformScanoutWritten(Device, &Rect);
    }
    KeMemoryBarrier();

CleanupMutex:
    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
CleanupRundown:
    ExReleaseRundownProtection(&Device->ScanoutRundown);
    return Status;
}

static NTSTATUS
SoftGpuScheduleScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    KIRQL CurrentIrql;
    KIRQL OldIrql;
    BOOLEAN QueueWork;
    NTSTATUS Status;

    if (!ExAcquireRundownProtection(&Device->ScanoutRundown))
        return STATUS_DELETE_PENDING;

    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql == PASSIVE_LEVEL)
    {
        Status = SoftGpuCopyCurrentPrimaryToScanout(Device);
        ExReleaseRundownProtection(&Device->ScanoutRundown);
        return Status;
    }
    if (CurrentIrql > DISPATCH_LEVEL)
    {
        ExReleaseRundownProtection(&Device->ScanoutRundown);
        return STATUS_INVALID_DEVICE_STATE;
    }

    QueueWork = FALSE;
    KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else if (InterlockedCompareExchange(
                 &Device->ScanoutWorkQueued, 1, 0) == 0)
    {
        QueueWork = TRUE;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);

    if (QueueWork)
    {
        ExQueueWorkItem(&Device->ScanoutWorkItem,
                        DelayedWorkQueue);
        /* The queued worker owns this rundown reference. */
        return STATUS_SUCCESS;
    }

    ExReleaseRundownProtection(&Device->ScanoutRundown);
    return Status;
}

/*
 * Validate a primary placement inside the software allocation slab, remember
 * its exact linear geometry, and copy it to the fixed firmware framebuffer.
 * Platform providers remain responsible only for validating and describing
 * the firmware scanout; the shared engine contains no bus or vendor policy.
 */
NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceAddress(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress)
{
    PSOFTGPU_DEVICE  Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    ULONGLONG        PrimaryAddress;
    ULONGLONG        FbBase;
    ULONGLONG        FbEnd;
    ULONGLONG        SurfaceSize;
    ULONGLONG        SurfaceSpan;
    PSOFTGPU_ALLOC   Allocation;
    ULONG            Width;
    ULONG            Height;
    ULONG            Pitch;
    KIRQL            OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetVidPnSourceAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: SetVidPnSourceAddress Source=%u PA=0x%I64x\n",
           SetVidPnSourceAddress->VidPnSourceId,
           SetVidPnSourceAddress->PrimaryAddress.QuadPart);

    if (SetVidPnSourceAddress->VidPnSourceId != 0)
    {
        DPRINT1("SOFTGPU: SetVidPnSourceAddress: invalid source %u\n",
                SetVidPnSourceAddress->VidPnSourceId);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (SetVidPnSourceAddress->PrimarySegment != 0 &&
        SetVidPnSourceAddress->PrimarySegment != SOFTGPU_SEGMENT_ID)
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    FbBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    if (Device->FrameBufferSize > (SIZE_T)(((ULONGLONG)-1) - FbBase))
        return STATUS_INTEGER_OVERFLOW;

    FbEnd = FbBase + (ULONGLONG)Device->FrameBufferSize;
    PrimaryAddress = (ULONGLONG)SetVidPnSourceAddress->PrimaryAddress.QuadPart;

    if (PrimaryAddress != 0)
    {
        Allocation =
            (PSOFTGPU_ALLOC)SetVidPnSourceAddress->hAllocation;
        if (Allocation == NULL ||
            Allocation->Magic != SOFTGPU_ALLOC_MAGIC)
        {
            return STATUS_INVALID_HANDLE;
        }

        Width = Allocation->Width != 0 ?
                    Allocation->Width :
                    Device->Width;
        Height = Allocation->Height != 0 ?
                     Allocation->Height :
                     Device->Height;
        Pitch = Allocation->Pitch != 0 ?
                    Allocation->Pitch :
                    Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;

        if (Width != Device->Width ||
            Height != Device->Height ||
            Width == 0 ||
            Height == 0 ||
            Width >
                MAXULONG / SOFTGPU_DISPLAY_BYTES_PER_PIXEL)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        SurfaceSize =
            (ULONGLONG)Width *
            SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
        if (Pitch < SurfaceSize ||
            Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
            Height > (~(ULONGLONG)0) / Pitch)
        {
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
        SurfaceSpan =
            (ULONGLONG)Pitch * (Height - 1) + SurfaceSize;

        if (PrimaryAddress < FbBase ||
            PrimaryAddress >= FbEnd ||
            SurfaceSpan > FbEnd - PrimaryAddress ||
            SurfaceSpan > Allocation->Size)
        {
            DPRINT1("SOFTGPU: SetVidPnSourceAddress: PA 0x%I64x outside "
                    "framebuffer [0x%I64x, 0x%I64x) for %I64u-byte surface\n",
                    PrimaryAddress,
                    FbBase,
                    FbEnd,
                    SurfaceSpan);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->CurrentPrimaryOffset =
            PrimaryAddress - FbBase;
        Device->CurrentPrimaryPitch = Pitch;
        Device->CurrentPrimaryWidth = Width;
        Device->CurrentPrimaryHeight = Height;
        Device->CurrentPrimaryValid = TRUE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }
    else
    {
        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->CurrentPrimaryValid = FALSE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }

    return SoftGpuScheduleScanout(Device);
}


/* =========================================================================
 * DxgkDdiSetVidPnSourceVisibility
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiSetVidPnSourceVisibility(
    _In_ PVOID                                 MiniportDeviceContext,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetVidPnSourceVisibility == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: SetVidPnSourceVisibility Source=%u Visible=%d\n",
           SetVidPnSourceVisibility->VidPnSourceId,
           (int)SetVidPnSourceVisibility->Visible);

    if (SetVidPnSourceVisibility->VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Device->ScanoutLock, &OldIrql);
        Device->ScanoutVisible =
            SetVidPnSourceVisibility->Visible ? TRUE : FALSE;
        if (++Device->ScanoutGeneration == 0)
            ++Device->ScanoutGeneration;
        KeReleaseSpinLock(&Device->ScanoutLock, OldIrql);
    }

    return SoftGpuScheduleScanout(Device);
}


/* =========================================================================
 * DxgkDdiUpdateActiveVidPnPresentPath
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiUpdateActiveVidPnPresentPath(
    _In_ PVOID                                      MiniportDeviceContext,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        UpdateActiveVidPnPresentPath == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId != 0)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    if (!SoftGpuTargetKnown(Device, UpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnTargetId))
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    if (!SoftGpuVidPnActiveTransformSupported(
            UpdateActiveVidPnPresentPath->
                VidPnPresentPathInfo.ContentTransformation.Scaling,
            UpdateActiveVidPnPresentPath->
                VidPnPresentPathInfo.ContentTransformation.Rotation))
    {
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiRecommendMonitorModes
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiRecommendMonitorModes(
    _In_ PVOID                              MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendMonitorModes == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendMonitorModes TargetId=%u\n",
           RecommendMonitorModes->VideoPresentTargetId);

    if (!SoftGpuTargetKnown(Device, RecommendMonitorModes->VideoPresentTargetId))
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    return STATUS_GRAPHICS_NO_PREFERRED_MODE;
}


/* =========================================================================
 * DxgkDdiRecommendVidPnTopology
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiRecommendVidPnTopology(
    _In_ PVOID                                  MiniportDeviceContext,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *RecommendVidPnTopology)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        RecommendVidPnTopology == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: RecommendVidPnTopology\n");

    /*
     * Returning STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY tells
     * dxgkrnl that the miniport has no topology recommendation; the OS
     * will use its own heuristics.
     */
    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}


/* =========================================================================
 * DxgkDdiQueryDeviceDescriptor
 * =========================================================================
 */

/*
 * SoftGpuDdiQueryDeviceDescriptor
 *
 * Returns STATUS_MONITOR_NO_DESCRIPTOR to indicate that no EDID or other
 * device descriptor is available for the virtual monitor.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiQueryDeviceDescriptor(
    _In_    PVOID                   MiniportDeviceContext,
    _In_    ULONG                   ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    const SOFTGPU_OUTPUT *Output;
    ULONG Length;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        DeviceDescriptor == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT("SOFTGPU: QueryDeviceDescriptor ChildUid=%lu\n", ChildUid);
    if (ChildUid == SOFTGPU_CHILD_UID)
        return SoftGpuPlatformQueryDescriptor(Device, DeviceDescriptor);

    Output = SoftGpuFindOutput(Device, ChildUid);
    if (Output == NULL)
        return STATUS_INVALID_PARAMETER;

    SoftGpuLockOutputs(Device);
    if (!Output->Connected || Output->EdidLength == 0)
        Status = STATUS_MONITOR_NO_DESCRIPTOR;
    else if (DeviceDescriptor->DescriptorOffset >= Output->EdidLength)
        Status = STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    else if (DeviceDescriptor->DescriptorBuffer == NULL)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        Length = min(DeviceDescriptor->DescriptorLength, Output->EdidLength - DeviceDescriptor->DescriptorOffset);
        RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Output->Edid + DeviceDescriptor->DescriptorOffset, Length);
        DeviceDescriptor->DescriptorLength = Length;
        Status = STATUS_SUCCESS;
    }
    SoftGpuUnlockOutputs(Device);
    return Status;
}

/* EOF */
