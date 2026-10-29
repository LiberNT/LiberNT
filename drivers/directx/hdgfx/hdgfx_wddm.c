/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and WDDM DDI table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

DRIVER_INITIALIZE HdgfxDriverEntry;

static VOID
HdgfxSegmentDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG SegmentId,
    _Out_ PDXGK_SEGMENTDESCRIPTOR4 Descriptor)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Flags.CpuVisible = 1;
    Descriptor->Flags.PopulatedFromSystemMemory = 1;
    Descriptor->Flags.LocalBudgetGroup = 1;
    if (SegmentId == SOFTGPU_SEGMENT_ID)
    {
        Descriptor->Flags.CacheCoherent = Device->FrameBufferCacheType == MmCached;
        Descriptor->BaseAddress = Device->FrameBufferPhys;
        Descriptor->CpuTranslatedAddress = Device->FrameBufferPhys;
        Descriptor->Size = Device->FrameBufferSize;
    }
    else
    {
        Descriptor->BaseAddress.QuadPart = (LONGLONG)Adapter->DmaBase;
        Descriptor->CpuTranslatedAddress.QuadPart = (LONGLONG)Adapter->DmaPhysical;
        Descriptor->Size = HDGFX_DMA_SIZE;
    }
    Descriptor->CommitLimit = Descriptor->Size;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryAdapterInfo(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;
    PHDGFX_ADAPTER Adapter;
    PDXGK_QUERYSEGMENTOUT4 Out;
    ULONG SegmentId;

    if (QueryAdapterInfo == NULL || QueryAdapterInfo->Type != DXGKQAITYPE_QUERYSEGMENT4)
        return SoftGpuDdiQueryAdapterInfo(MiniportDeviceContext, QueryAdapterInfo);
    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || QueryAdapterInfo->pOutputData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
        return STATUS_BUFFER_TOO_SMALL;
    Adapter = Device->PlatformServicesContext;
    if (Adapter == NULL || Adapter->DmaMemory == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Out = QueryAdapterInfo->pOutputData;
    if (Out->pSegmentDescriptor == NULL)
    {
        Out->NbSegment = HDGFX_DMA_SEGMENT_ID;
        Out->PagingBufferSegmentId = 0;
        Out->PagingBufferSize = 64 * 1024;
        Out->PagingBufferPrivateDataSize = 0;
        return STATUS_SUCCESS;
    }
    if (Out->NbSegment < HDGFX_DMA_SEGMENT_ID || Out->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
        return STATUS_INVALID_PARAMETER;
    for (SegmentId = 1; SegmentId <= HDGFX_DMA_SEGMENT_ID; SegmentId++)
    {
        HdgfxSegmentDescriptor(Device, Adapter, SegmentId,
                               (PDXGK_SEGMENTDESCRIPTOR4)(Out->pSegmentDescriptor +
                                                          (SIZE_T)(SegmentId - 1) * Out->SegmentDescriptorStride));
    }
    return STATUS_SUCCESS;
}

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

    InitData.DxgkDdiQueryAdapterInfo = HdgfxDdiQueryAdapterInfo;
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
