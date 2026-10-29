/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and WDDM DDI table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

DRIVER_INITIALIZE HdgfxDriverEntry;

NTSTATUS
NTAPI
HdgfxDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA InitData;
    NTSTATUS Status;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.Version = REACTOS_WDDM_TARGET_INTERFACE_VERSION;

    InitData.DxgkDdiAddDevice = SoftGpuDdiAddDevice;
    InitData.DxgkDdiStartDevice = SoftGpuDdiStartDevice;
    InitData.DxgkDdiStopDevice = SoftGpuDdiStopDevice;
    InitData.DxgkDdiRemoveDevice = SoftGpuDdiRemoveDevice;
    InitData.DxgkDdiResetDevice = SoftGpuDdiResetDevice;
    InitData.DxgkDdiSetPowerState = SoftGpuDdiSetPowerState;
    InitData.DxgkDdiNotifyAcpiEvent = SoftGpuDdiNotifyAcpiEvent;
    InitData.DxgkDdiQueryInterface = SoftGpuDdiQueryInterface;

    InitData.DxgkDdiQueryAdapterInfo = SoftGpuDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations = SoftGpuDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = SoftGpuDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = SoftGpuDdiQueryDeviceDescriptor;
    InitData.DxgkDdiGetNodeMetadata = SoftGpuDdiGetNodeMetadata;

    InitData.DxgkDdiInterruptRoutine = SoftGpuDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine = SoftGpuDdiDpcRoutine;

    InitData.DxgkDdiCreateAllocation = SoftGpuDdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation = SoftGpuDdiDestroyAllocation;
    InitData.DxgkDdiOpenAllocation = SoftGpuDdiOpenAllocation;
    InitData.DxgkDdiCloseAllocation = SoftGpuDdiCloseAllocation;
    InitData.DxgkDdiGetStandardAllocationDriverData = SoftGpuDdiGetStandardAllocationDriverData;
    InitData.DxgkDdiBuildPagingBuffer = HdgfxDdiBuildPagingBuffer;
    InitData.DxgkDdiPatch = HdgfxDdiPatch;
    InitData.DxgkDdiRender = HdgfxDdiRender;
    InitData.DxgkDdiPresent = HdgfxDdiPresent;

    InitData.DxgkDdiSubmitCommand = HdgfxDdiSubmitCommand;
    InitData.DxgkDdiQueryCurrentFence = HdgfxDdiQueryCurrentFence;
    InitData.DxgkDdiResetFromTimeout = HdgfxDdiResetFromTimeout;
    InitData.DxgkDdiRestartFromTimeout = HdgfxDdiRestartFromTimeout;

    InitData.DxgkDdiIsSupportedVidPn = SoftGpuDdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn = SoftGpuDdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality = SoftGpuDdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceAddress = SoftGpuDdiSetVidPnSourceAddress;
    InitData.DxgkDdiSetVidPnSourceVisibility = SoftGpuDdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn = SoftGpuDdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath = SoftGpuDdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes = SoftGpuDdiRecommendMonitorModes;
    InitData.DxgkDdiRecommendVidPnTopology = SoftGpuDdiRecommendVidPnTopology;

    InitData.DxgkDdiSetPointerPosition = SoftGpuDdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape = SoftGpuDdiSetPointerShape;
    InitData.DxgkDdiSetPalette = SoftGpuDdiSetPalette;
    InitData.DxgkDdiGetScanLine = SoftGpuDdiGetScanLine;

    InitData.DxgkDdiCreateDevice = SoftGpuDdiCreateDevice;
    InitData.DxgkDdiDestroyDevice = SoftGpuDdiDestroyDevice;
    InitData.DxgkDdiCreateContext = SoftGpuDdiCreateContext;
    InitData.DxgkDdiDestroyContext = SoftGpuDdiDestroyContext;

    InitData.DxgkDdiControlInterrupt = SoftGpuDdiControlInterrupt;
    InitData.DxgkDdiControlInterrupt2 = SoftGpuDdiControlInterrupt2;
    InitData.DxgkDdiCollectDiagnosticInfo = SoftGpuDdiCollectDiagnosticInfo;
    InitData.DxgkDdiControlInterrupt3 = SoftGpuDdiControlInterrupt3;

    InitData.DxgkDdiCreateProcess = SoftGpuDdiCreateProcess;
    InitData.DxgkDdiDestroyProcess = SoftGpuDdiDestroyProcess;
    InitData.DxgkDdiGetRootPageTableSize = SoftGpuDdiGetRootPageTableSize;
    InitData.DxgkDdiSetRootPageTable = SoftGpuDdiSetRootPageTable;
    InitData.DxgkDdiMapCpuHostAperture = SoftGpuDdiMapCpuHostAperture;
    InitData.DxgkDdiUnmapCpuHostAperture = SoftGpuDdiUnmapCpuHostAperture;
    InitData.DxgkDdiSubmitCommandVirtual = SoftGpuDdiSubmitCommandVirtual;
    InitData.DxgkDdiValidateUpdateAllocationProperty = SoftGpuDdiValidateUpdateAllocationProperty;
    InitData.DxgkDdiSetTimingsFromVidPn = SoftGpuDdiSetTimingsFromVidPn;
    InitData.DxgkDdiEscape = SoftGpuDdiEscape;
    InitData.DxgkDdiCreateCpuEvent = SoftGpuDdiCreateCpuEvent;
    InitData.DxgkDdiDestroyCpuEvent = SoftGpuDdiDestroyCpuEvent;

    Status = DxgkInitialize(DriverObject, RegistryPath, &InitData);
    if (!NT_SUCCESS(Status))
        DPRINT1("DxgkInitialize failed 0x%08lx\n", Status);
    return Status;
}
