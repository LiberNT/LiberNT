/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry and WDDM DDI table
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx.h"

#define HDGFX_DEAD_DEVICE_MAGIC     0xDEAD4446UL
#define HDGFX_DEAD_PROCESS_MAGIC    0xDEAD5046UL

DRIVER_INITIALIZE HdgfxDriverEntry;

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
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ ULONG SegmentId,
    _Out_ PDXGK_SEGMENTDESCRIPTOR4 Descriptor)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    if (SegmentId == HDGFX_APERTURE_SEGMENT_ID)
    {
        Descriptor->BaseAddress.QuadPart = Adapter->ApertureBase;
        Descriptor->Size = Adapter->ApertureSize;
        Descriptor->CommitLimit = Descriptor->Size;
        Descriptor->Flags.Aperture = 1;
        Descriptor->Flags.NonLocalBudgetGroup = 1;
        return;
    }
    Descriptor->Flags.CpuVisible = 1;
    Descriptor->Flags.PopulatedFromSystemMemory = 1;
    Descriptor->Flags.LocalBudgetGroup = 1;
    if (SegmentId == HDGFX_SLAB_SEGMENT_ID)
    {
        Descriptor->BaseAddress = Adapter->SlabPhysical;
        Descriptor->CpuTranslatedAddress = Adapter->SlabPhysical;
        Descriptor->Size = Adapter->SlabSize;
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
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PDXGK_QUERYSEGMENTOUT4 Out;
    ULONG SegmentCount;
    ULONG SegmentId;

    if (QueryAdapterInfo->pOutputData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
        return STATUS_BUFFER_TOO_SMALL;
    if (Adapter->DmaMemory == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Out = QueryAdapterInfo->pOutputData;
    SegmentCount = Adapter->ApertureSize != 0 ? HDGFX_APERTURE_SEGMENT_ID : HDGFX_DMA_SEGMENT_ID;
    if (Out->pSegmentDescriptor == NULL)
    {
        Out->NbSegment = SegmentCount;
        Out->PagingBufferSegmentId = 0;
        Out->PagingBufferSize = 64 * 1024;
        Out->PagingBufferPrivateDataSize = 0;
        return STATUS_SUCCESS;
    }
    if (Out->NbSegment < SegmentCount || Out->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
        return STATUS_INVALID_PARAMETER;
    for (SegmentId = 1; SegmentId <= SegmentCount; SegmentId++)
    {
        HdgfxSegmentDescriptor(Adapter, SegmentId,
                               (PDXGK_SEGMENTDESCRIPTOR4)(Out->pSegmentDescriptor +
                                                          (SIZE_T)(SegmentId - 1) * Out->SegmentDescriptorStride));
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxQueryDriverCaps(
    _In_ PHDGFX_ADAPTER Adapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PDXGK_DRIVERCAPS Caps = QueryAdapterInfo->pOutputData;

    if (Caps == NULL)
        return STATUS_INVALID_PARAMETER;
    if (QueryAdapterInfo->OutputDataSize < sizeof(*Caps))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Caps, sizeof(*Caps));
    Caps->HighestAcceptableAddress.QuadPart = MAXULONG;
    if (Adapter->HardwarePointer)
    {
        Caps->MaxPointerWidth = HDGFX_CURSOR_SIZE;
        Caps->MaxPointerHeight = HDGFX_CURSOR_SIZE;
        Caps->PointerCaps.Color = 1;
    }
    Caps->MaxAllocationListSlotId = 255;
    Caps->ApertureSegmentCommitLimit = Adapter->ApertureSize;
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
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || QueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (QueryAdapterInfo->pOutputData != NULL && QueryAdapterInfo->OutputDataSize != 0)
                RtlZeroMemory(QueryAdapterInfo->pOutputData, QueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;

        case DXGKQAITYPE_DRIVERCAPS:
            return HdgfxQueryDriverCaps(Adapter, QueryAdapterInfo);

        case DXGKQAITYPE_QUERYSEGMENT4:
            return HdgfxQuerySegments(Adapter, QueryAdapterInfo);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
APIENTRY
HdgfxDdiCreateDevice(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_CREATEDEVICE *CreateDevice)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;
    PHDGFX_PROCESS Process;
    PHDGFX_DEVICE Device;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || CreateDevice == NULL)
        return STATUS_INVALID_PARAMETER;
    Process = CreateDevice->hKmdProcess;
    if ((!CreateDevice->Flags.SystemDevice && Process == NULL) ||
        (Process != NULL && (Process->Magic != HDGFX_PROCESS_MAGIC || Process->Adapter != Adapter)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Device), HDGFX_TAG);
    if (Device == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Device->Magic = HDGFX_DEVICE_MAGIC;
    Device->Adapter = Adapter;
    Device->Process = Process;
    CreateDevice->hDevice = Device;
    CreateDevice->pInfo = &HdgfxDeviceInfo;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiDestroyDevice(
    _In_ PVOID hDevice)
{
    PHDGFX_DEVICE Device = hDevice;

    if (Device == NULL)
        return STATUS_SUCCESS;
    if (Device->Magic != HDGFX_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Device->Magic = HDGFX_DEAD_DEVICE_MAGIC;
    ExFreePoolWithTag(Device, HDGFX_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiCreateProcess(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_CREATEPROCESS *CreateProcess)
{
    PHDGFX_ADAPTER Adapter = hAdapter;
    PHDGFX_PROCESS Process;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || CreateProcess == NULL)
        return STATUS_INVALID_PARAMETER;

    Process = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Process), HDGFX_TAG);
    if (Process == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Process->Magic = HDGFX_PROCESS_MAGIC;
    Process->hDxgkProcess = CreateProcess->hDxgkProcess;
    Process->Adapter = Adapter;
    CreateProcess->hKmdProcess = Process;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiDestroyProcess(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST HANDLE hKmdProcess)
{
    PHDGFX_ADAPTER Adapter = hAdapter;
    PHDGFX_PROCESS Process = hKmdProcess;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (Process == NULL)
        return STATUS_SUCCESS;
    if (Process->Magic != HDGFX_PROCESS_MAGIC || Process->Adapter != Adapter)
        return STATUS_INVALID_PARAMETER;
    Process->Magic = HDGFX_DEAD_PROCESS_MAGIC;
    Process->Adapter = NULL;
    ExFreePoolWithTag(Process, HDGFX_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiGetNodeMetadata(
    _In_ PVOID MiniportDeviceContext,
    _In_ UINT NodeOrdinalAndAdapterIndex,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    static const WCHAR FriendlyName[] = L"3D";
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC || GetNodeMetadata == NULL ||
        NodeOrdinalAndAdapterIndex != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(GetNodeMetadata, sizeof(*GetNodeMetadata));
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName, FriendlyName, sizeof(FriendlyName));
    return STATUS_SUCCESS;
}

static NTSTATUS
HdgfxControlInterrupt(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGK_INTERRUPT_TYPE InterruptType,
    _In_ DXGK_INTERRUPT_STATE InterruptState,
    _In_ DXGK_CRTC_VSYNC_STATE VsyncState)
{
    PHDGFX_ADAPTER Adapter = MiniportDeviceContext;

    if (Adapter == NULL || Adapter->Magic != HDGFX_ADAPTER_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (InterruptType == DXGK_INTERRUPT_CRTC_VSYNC)
        return HdgfxSetVsync(Adapter, VsyncState);
    if (InterruptState != DXGK_INTERRUPT_ENABLE && InterruptState != DXGK_INTERRUPT_DISABLE)
        return STATUS_INVALID_PARAMETER;
    if (InterruptType != DXGK_INTERRUPT_TYPE_DMA_COMPLETED && InterruptType != DXGK_INTERRUPT_TYPE_DMA_PREEMPTED)
        return STATUS_NOT_IMPLEMENTED;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
HdgfxDdiControlInterrupt(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN EnableInterrupt)
{
    return HdgfxControlInterrupt(MiniportDeviceContext, InterruptType,
                                 EnableInterrupt ? DXGK_INTERRUPT_ENABLE : DXGK_INTERRUPT_DISABLE,
                                 EnableInterrupt ? DXGK_VSYNC_ENABLE : DXGK_VSYNC_DISABLE_NO_PHASE);
}

NTSTATUS
APIENTRY
HdgfxDdiControlInterrupt2(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT2 InterruptControl)
{
    return HdgfxControlInterrupt(MiniportDeviceContext, InterruptControl.InterruptType,
                                 InterruptControl.InterruptState, InterruptControl.CrtcVsyncState);
}

NTSTATUS
APIENTRY
HdgfxDdiControlInterrupt3(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CONTROLINTERRUPT3 *InterruptControl)
{
    if (InterruptControl == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterruptControl->InterruptType == DXGK_INTERRUPT_CRTC_VSYNC && InterruptControl->VidPnSourceId != 0)
        return STATUS_INVALID_PARAMETER;
    return HdgfxControlInterrupt(MiniportDeviceContext, InterruptControl->InterruptType,
                                 InterruptControl->InterruptState, InterruptControl->CrtcVsyncState);
}

NTSTATUS
APIENTRY
HdgfxDdiCollectDiagnosticInfo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    INOUT_PDXGKARG_COLLECTDIAGNOSTICINFO CollectDiagnosticInfo)
{
    PHDGFX_ADAPTER Adapter;
    PCSTR Bucket;

    if (PhysicalDeviceObject == NULL || CollectDiagnosticInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = CollectDiagnosticInfo->hAdapter;
    if (Adapter != NULL &&
        (Adapter->Magic != HDGFX_ADAPTER_MAGIC || Adapter->PhysicalDeviceObject != PhysicalDeviceObject))
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (CollectDiagnosticInfo->Type)
    {
        case DXGK_DI_ADDDEVICE:
            Bucket = "hdgfx_adddevice";
            break;
        case DXGK_DI_STARTDEVICE:
            Bucket = "hdgfx_startdevice";
            break;
        case DXGK_DI_BLACKSCREEN:
            Bucket = "hdgfx_blackscreen";
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(CollectDiagnosticInfo->BucketingString, sizeof(CollectDiagnosticInfo->BucketingString));
    RtlZeroMemory(CollectDiagnosticInfo->DescriptionString, sizeof(CollectDiagnosticInfo->DescriptionString));
    RtlCopyMemory(CollectDiagnosticInfo->BucketingString, Bucket,
                  min(strlen(Bucket), sizeof(CollectDiagnosticInfo->BucketingString) - 1));
    RtlCopyMemory(CollectDiagnosticInfo->DescriptionString, Bucket,
                  min(strlen(Bucket), sizeof(CollectDiagnosticInfo->DescriptionString) - 1));
    CollectDiagnosticInfo->BufferSizeOut = 0;
    return STATUS_SUCCESS;
}

VOID
APIENTRY
HdgfxDdiResetDevice(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
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
    InitData.Version = DXGKDDI_INTERFACE_VERSION;

    InitData.DxgkDdiAddDevice = HdgfxDdiAddDevice;
    InitData.DxgkDdiStartDevice = HdgfxDdiStartDevice;
    InitData.DxgkDdiStopDevice = HdgfxDdiStopDevice;
    InitData.DxgkDdiRemoveDevice = HdgfxDdiRemoveDevice;
    InitData.DxgkDdiResetDevice = HdgfxDdiResetDevice;
    InitData.DxgkDdiSetPowerState = HdgfxDdiSetPowerState;
    InitData.DxgkDdiNotifyAcpiEvent = HdgfxDdiNotifyAcpiEvent;
    InitData.DxgkDdiQueryInterface = HdgfxDdiQueryInterface;

    InitData.DxgkDdiQueryAdapterInfo = HdgfxDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations = HdgfxDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = HdgfxDdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = HdgfxDdiQueryDeviceDescriptor;
    InitData.DxgkDdiGetNodeMetadata = HdgfxDdiGetNodeMetadata;

    InitData.DxgkDdiInterruptRoutine = HdgfxDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine = HdgfxDdiDpcRoutine;

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

    InitData.DxgkDdiIsSupportedVidPn = HdgfxDdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn = HdgfxDdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality = HdgfxDdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceAddress = HdgfxDdiSetVidPnSourceAddress;
    InitData.DxgkDdiSetVidPnSourceVisibility = HdgfxDdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn = HdgfxDdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath = HdgfxDdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes = HdgfxDdiRecommendMonitorModes;
    InitData.DxgkDdiRecommendVidPnTopology = HdgfxDdiRecommendVidPnTopology;

    InitData.DxgkDdiSetPointerPosition = HdgfxDdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape = HdgfxDdiSetPointerShape;
    InitData.DxgkDdiSetPalette = HdgfxDdiSetPalette;
    InitData.DxgkDdiGetScanLine = HdgfxDdiGetScanLine;

    InitData.DxgkDdiCreateDevice = HdgfxDdiCreateDevice;
    InitData.DxgkDdiDestroyDevice = HdgfxDdiDestroyDevice;
    InitData.DxgkDdiCreateContext = HdgfxDdiCreateContext;
    InitData.DxgkDdiDestroyContext = HdgfxDdiDestroyContext;

    InitData.DxgkDdiControlInterrupt = HdgfxDdiControlInterrupt;
    InitData.DxgkDdiControlInterrupt2 = HdgfxDdiControlInterrupt2;
    InitData.DxgkDdiCollectDiagnosticInfo = HdgfxDdiCollectDiagnosticInfo;
    InitData.DxgkDdiControlInterrupt3 = HdgfxDdiControlInterrupt3;

    InitData.DxgkDdiCreateProcess = HdgfxDdiCreateProcess;
    InitData.DxgkDdiDestroyProcess = HdgfxDdiDestroyProcess;
    InitData.DxgkDdiValidateUpdateAllocationProperty = HdgfxDdiValidateUpdateAllocationProperty;
    InitData.DxgkDdiSetTimingsFromVidPn = HdgfxDdiSetTimingsFromVidPn;

    Status = DxgkInitialize(DriverObject, RegistryPath, &InitData);
    if (!NT_SUCCESS(Status))
        DPRINT1("DxgkInitialize failed 0x%08lx\n", Status);
    return Status;
}
