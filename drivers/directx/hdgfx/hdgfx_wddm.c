/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and WDDM DDI table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

DRIVER_INITIALIZE HdgfxDriverEntry;

static const GUID HdgfxGuidShadowPresent = RXGK_SHADOW_PRESENT_INTERFACE_GUID_INIT;

static DXGK_DEVICEINFO HdgfxDeviceInfo =
{
    64 * 1024,
    1UL << (HDGFX_DMA_SEGMENT_ID - 1),
    0,
    256,
    256,
};

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

static NTSTATUS
HdgfxQuerySegments(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PHDGFX_ADAPTER Adapter = Device->PlatformServicesContext;
    PDXGK_QUERYSEGMENTOUT4 Out;
    ULONG SegmentId;

    if (QueryAdapterInfo->pOutputData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
        return STATUS_BUFFER_TOO_SMALL;
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

static NTSTATUS
HdgfxQueryDriverCaps(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PDXGK_DRIVERCAPS Caps = QueryAdapterInfo->pOutputData;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;
    if (QueryAdapterInfo->OutputDataSize < sizeof(*Caps))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Caps, sizeof(*Caps));
    Caps->HighestAcceptableAddress.QuadPart = Device->HighestDmaAddress.QuadPart != 0 ?
                                              Device->HighestDmaAddress.QuadPart : -1LL;
    if (Device->PlatformHardwarePointer)
    {
        Caps->MaxPointerWidth = SOFTGPU_POINTER_MAX_WIDTH;
        Caps->MaxPointerHeight = SOFTGPU_POINTER_MAX_HEIGHT;
        Caps->PointerCaps.Color = 1;
    }
    Caps->MaxAllocationListSlotId = 255;
    Caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
    Caps->WDDMVersion = DXGKDDI_WDDMv3_2_ENUM;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiQueryAdapterInfo(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PSOFTGPU_DEVICE Device = MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || QueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (QueryAdapterInfo->pOutputData != NULL && QueryAdapterInfo->OutputDataSize != 0)
                RtlZeroMemory(QueryAdapterInfo->pOutputData, QueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;

        case DXGKQAITYPE_DRIVERCAPS:
            return HdgfxQueryDriverCaps(Device, QueryAdapterInfo);

        case DXGKQAITYPE_QUERYSEGMENT4:
            return HdgfxQuerySegments(Device, QueryAdapterInfo);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
APIENTRY
HdgfxDdiQueryInterface(
    _In_ PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface)
{
    PAGED_CODE();

    if (QueryInterface != NULL && QueryInterface->InterfaceType != NULL &&
        IsEqualGUID(QueryInterface->InterfaceType, &HdgfxGuidShadowPresent))
    {
        return STATUS_NOT_SUPPORTED;
    }
    return SoftGpuDdiQueryInterface(MiniportDeviceContext, QueryInterface);
}

NTSTATUS
APIENTRY
HdgfxDdiCreateDevice(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_CREATEDEVICE *CreateDevice)
{
    NTSTATUS Status;

    Status = SoftGpuDdiCreateDevice(MiniportDeviceContext, CreateDevice);
    if (NT_SUCCESS(Status))
        CreateDevice->pInfo = &HdgfxDeviceInfo;
    return Status;
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
    InitData.DxgkDdiQueryInterface = HdgfxDdiQueryInterface;

    InitData.DxgkDdiQueryAdapterInfo = HdgfxDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations = SoftGpuDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = SoftGpuDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = SoftGpuDdiQueryDeviceDescriptor;
    InitData.DxgkDdiGetNodeMetadata = SoftGpuDdiGetNodeMetadata;

    InitData.DxgkDdiInterruptRoutine = SoftGpuDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine = SoftGpuDdiDpcRoutine;

    InitData.DxgkDdiCreateAllocation = HdgfxDdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation = HdgfxDdiDestroyAllocation;
    InitData.DxgkDdiOpenAllocation = HdgfxDdiOpenAllocation;
    InitData.DxgkDdiCloseAllocation = HdgfxDdiCloseAllocation;
    InitData.DxgkDdiGetStandardAllocationDriverData = HdgfxDdiGetStandardAllocationDriverData;
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

    InitData.DxgkDdiCreateDevice = HdgfxDdiCreateDevice;
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
    InitData.DxgkDdiValidateUpdateAllocationProperty = HdgfxDdiValidateUpdateAllocationProperty;
    InitData.DxgkDdiSetTimingsFromVidPn = SoftGpuDdiSetTimingsFromVidPn;
    InitData.DxgkDdiEscape = SoftGpuDdiEscape;
    InitData.DxgkDdiCreateCpuEvent = SoftGpuDdiCreateCpuEvent;
    InitData.DxgkDdiDestroyCpuEvent = SoftGpuDdiDestroyCpuEvent;

    Status = DxgkInitialize(DriverObject, RegistryPath, &InitData);
    if (!NT_SUCCESS(Status))
        DPRINT1("DxgkInitialize failed 0x%08lx\n", Status);
    return Status;
}
