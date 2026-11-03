/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pure adapter-start role policy shared with kernel tests
 */

#include <ntddk.h>
#include "adapter_start_core.h"

DXGK_ADAPTER_START_ROLE
DxgkAdapterStartClassifyRole(
    _In_ BOOLEAN IsDisplayOnlyDriver,
    _In_ ULONG NumberOfVideoPresentSources)
{
    if (IsDisplayOnlyDriver)
        return DxgkAdapterStartDisplayOnly;
    return NumberOfVideoPresentSources == 0 ? DxgkAdapterStartRenderOnly : DxgkAdapterStartFullDisplay;
}

BOOLEAN
DxgkAdapterStartRoleRequiresScheduler(
    _In_ DXGK_ADAPTER_START_ROLE Role)
{
    return Role == DxgkAdapterStartFullDisplay || Role == DxgkAdapterStartRenderOnly;
}

BOOLEAN
DxgkAdapterStartRoleRequiresDisplayPipeline(
    _In_ DXGK_ADAPTER_START_ROLE Role)
{
    return Role == DxgkAdapterStartDisplayOnly || Role == DxgkAdapterStartFullDisplay;
}

BOOLEAN
DxgkAdapterStartRoleHasValidCounts(
    _In_ DXGK_ADAPTER_START_ROLE Role,
    _In_ ULONG NumberOfVideoPresentSources,
    _In_ ULONG NodeCount,
    _In_ ULONG MaximumNodeCount)
{
    if (MaximumNodeCount == 0 || NodeCount > MaximumNodeCount)
        return FALSE;
    if (Role == DxgkAdapterStartDisplayOnly)
        return NumberOfVideoPresentSources != 0 && NodeCount == 0;
    if (Role == DxgkAdapterStartFullDisplay)
        return NumberOfVideoPresentSources != 0 && NodeCount != 0;
    if (Role == DxgkAdapterStartRenderOnly)
        return NumberOfVideoPresentSources == 0 && NodeCount != 0;
    return FALSE;
}

DXGK_VSYNC_CONTROL_DDI
DxgkAdapterStartVsyncControlDdi(
    _In_ BOOLEAN IndependentVidPnVSyncControl,
    _In_ BOOLEAN HasControlInterrupt3,
    _In_ BOOLEAN HasControlInterrupt2,
    _In_ BOOLEAN HasControlInterrupt)
{
    if (IndependentVidPnVSyncControl && HasControlInterrupt3)
        return DxgkVsyncControlInterrupt3;
    if (HasControlInterrupt2)
        return DxgkVsyncControlInterrupt2;
    if (HasControlInterrupt)
        return DxgkVsyncControlInterrupt;
    return DxgkVsyncControlNone;
}

NTSTATUS
DxgkAdapterStartVsyncControlStatus(
    _In_ NTSTATUS MiniportStatus)
{
    /* Windows VidSch records the new vsync state when the miniport answers STATUS_NOT_IMPLEMENTED. */
    return MiniportStatus == STATUS_NOT_IMPLEMENTED ? STATUS_SUCCESS : MiniportStatus;
}

/* Windows refuses the NativeGpuFence scheduling cap while its NativeFence feature is disabled. */
NTSTATUS
DxgkAdapterStartNativeFenceStatus(
    _In_ BOOLEAN NativeGpuFenceCap,
    _In_ BOOLEAN NativeFenceFeatureEnabled)
{
    return NativeGpuFenceCap && !NativeFenceFeatureEnabled ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
}

static BOOLEAN
DxgkAdapterStartKnownWddmVersion(
    _In_ ULONG WddmVersion)
{
    static const ULONG Known[] =
    {
        0x1000, 0x1200, 0x1300, 0x2000, 0x2100, 0x2200, 0x2300, 0x2400,
        0x2500, 0x2600, 0x2700, 0x2800, 0x2900, 0x3000, 0x3100, 0x3200
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Known); ++Index)
    {
        if (Known[Index] == WddmVersion)
            return TRUE;
    }
    return FALSE;
}

/* Windows DXGADAPTER::Initialize capability checks, in its order. */
NTSTATUS
DxgkAdapterStartValidateCaps(
    _In_ const DXGK_ADAPTER_START_CAPS *Caps)
{
    const ULONG CrossAdapterResource = 1UL << 4;
    const ULONG CrossAdapterResourceTexture = 1UL << 15;
    const ULONG CrossAdapterResourceScanout = 1UL << 16;

    if (Caps->WddmVersion >= 0x2400 && Caps->MiscCaps > 0x1FF)
        return STATUS_INVALID_PARAMETER;
    if ((Caps->MiscCaps & (1UL << 4)) != 0 && !Caps->HasControlInterrupt3)
        return STATUS_INVALID_PARAMETER;
    if (Caps->InterfaceVersion > 0x3000 && !DxgkAdapterStartKnownWddmVersion(Caps->WddmVersion))
        return STATUS_REVISION_MISMATCH;
    if ((Caps->MemoryManagementCaps & CrossAdapterResourceScanout) != 0)
    {
        if ((Caps->MemoryManagementCaps & CrossAdapterResourceTexture) == 0 ||
            (Caps->MemoryManagementCaps & CrossAdapterResource) == 0)
            return STATUS_INVALID_PARAMETER;
    }
    else if ((Caps->MemoryManagementCaps & CrossAdapterResourceTexture) != 0 &&
             (Caps->MemoryManagementCaps & CrossAdapterResource) == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    {
        BOOLEAN HybridDdi = Caps->InterfaceVersion >= 0x4000;
        BOOLEAN CrossAdapter = HybridDdi && (Caps->MemoryManagementCaps & CrossAdapterResource) != 0;
        BOOLEAN Discrete = HybridDdi && Caps->HybridDiscrete;

        if (Discrete && Caps->InterfaceVersion < 0x5005 &&
            (Caps->NumberOfVideoPresentSources != 0 || Caps->NumberOfChildren != 0))
            return STATUS_INVALID_PARAMETER;
        if ((Discrete || Caps->HybridIntegrated) && !CrossAdapter)
            return STATUS_INVALID_PARAMETER;
        if (Caps->HybridIntegrated && (Discrete || Caps->NumberOfChildren == 0))
            return STATUS_INVALID_PARAMETER;
    }
    if (Caps->SupportPerEngineTdr && !Caps->HasEngineResetDdis)
        return STATUS_INVALID_PARAMETER;
    if (Caps->SupportKernelModeCommandBuffer && !Caps->HasRenderKm)
        return STATUS_INVALID_PARAMETER;
    if (Caps->SupportRuntimePowerManagement && !Caps->HasRuntimePowerDdis)
        return STATUS_INVALID_PARAMETER;
    if (Caps->SupportMultiPlaneOverlay)
    {
        if (Caps->InterfaceVersion < 0x300B)
            return STATUS_INVALID_PARAMETER;
        if (Caps->InterfaceVersion > 0x4000)
        {
            if (Caps->MaxOverlayPlanes == 0)
                return STATUS_INVALID_PARAMETER;
            if (Caps->MaxOverlayPlanes > 8 &&
                (Caps->WddmVersion < 0x2200 ? Caps->WddmVersion < 0x2000 || Caps->MaxOverlayPlanes != 10
                                            : Caps->MaxOverlayPlanes > 10))
                return STATUS_INVALID_PARAMETER;
        }
        if (!Caps->HasMpoSetAddress)
            return STATUS_INVALID_PARAMETER;
        if (Caps->InterfaceVersion > 0x4002 && !Caps->HasMpoCheck)
            return STATUS_INVALID_PARAMETER;
        if (!Caps->SupportDirectFlip)
            return STATUS_INVALID_PARAMETER;
    }
    if (Caps->WddmVersion >= 0x2300)
    {
        ULONG Transform3x4 = (Caps->ColorTransformCaps >> 2) & 1;
        ULONG WideColor = (Caps->ColorTransformCaps >> 3) & 1;
        ULONG HighColor = (Caps->ColorTransformCaps >> 4) & 1;
        BOOLEAN TargetGamma = Caps->InterfaceVersion > 0x7009 && Caps->WddmVersion >= 0x2200 &&
                              (!Caps->HasUpdateActiveVidPnPresentPath || Caps->HasSetTargetGamma);

        if (Transform3x4 < WideColor || WideColor < HighColor)
            return STATUS_INVALID_PARAMETER;
        if (Transform3x4 && !TargetGamma)
            return STATUS_INVALID_PARAMETER;
    }
    if (Caps->WddmVersion >= 0x2400 && Caps->HasQueryDiagnosticTypesSupport != Caps->HasControlDiagnosticReporting)
        return STATUS_INVALID_PARAMETER;
    /* TODO: WDDM 1.2/1.3 mandatory feature, stable power and MultiEngineAware rules */
    return STATUS_SUCCESS;
}

