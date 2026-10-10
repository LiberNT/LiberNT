/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Render-only WDDM miniport for Imagination PowerVR Rogue GPUs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "powervr.h"

#define NDEBUG
#include <debug.h>

static KDEFERRED_ROUTINE PowerVrCompletionDpc;

static const POWERVR_PLATFORM *const PowerVrPlatforms[] =
{
    &PowerVrK1Platform,
};

static const DXGK_DRIVERCAPS PowerVrDriverCaps =
{
    .HighestAcceptableAddress.QuadPart = POWERVR_DMA_LIMIT,
    .MaxAllocationListSlotId = 255,
    .GpuEngineTopology.NbAsymetricProcessingNodes = 1,
    .WDDMVersion = POWERVR_DECLARED_WDDM_VERSION,
};

static PPOWERVR_ADAPTER
PowerVrAdapter(_In_ PVOID MiniportDeviceContext)
{
    PPOWERVR_ADAPTER Adapter = MiniportDeviceContext;

    return (Adapter && Adapter->Magic == POWERVR_ADAPTER_MAGIC) ? Adapter : NULL;
}

static const POWERVR_PLATFORM *
PowerVrSelectPlatform(_In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PWCHAR Ids, Id;
    ULONG Length = 0, Index;
    NTSTATUS Status;
    const POWERVR_PLATFORM *Platform = NULL;

    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, 0, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL || Length == 0)
        return NULL;
    Ids = ExAllocatePoolWithTag(PagedPool, Length + sizeof(WCHAR) * 2, POWERVR_TAG);
    if (!Ids)
        return NULL;
    RtlZeroMemory(Ids, Length + sizeof(WCHAR) * 2);
    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, Length, Ids, &Length);
    if (NT_SUCCESS(Status))
    {
        for (Id = Ids; *Id && !Platform; Id += wcslen(Id) + 1)
        {
            for (Index = 0; Index < RTL_NUMBER_OF(PowerVrPlatforms); ++Index)
            {
                if (!_wcsicmp(Id, PowerVrPlatforms[Index]->HardwareId))
                {
                    Platform = PowerVrPlatforms[Index];
                    break;
                }
            }
        }
    }
    ExFreePoolWithTag(Ids, POWERVR_TAG);
    return Platform;
}

static NTSTATUS
PowerVrFindResources(_Inout_ PPOWERVR_ADAPTER Adapter)
{
    PCM_RESOURCE_LIST List = Adapter->DeviceInfo.TranslatedResourceList;
    ULONG Index, Descriptor;

    if (!List)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < List->Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_LIST Partial = &List->List[Index].PartialResourceList;

        for (Descriptor = 0; Descriptor < Partial->Count; ++Descriptor)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = &Partial->PartialDescriptors[Descriptor];

            if (Resource->Type == CmResourceTypeMemory && !Adapter->RegistersLength)
            {
                Adapter->RegistersPhysical = Resource->u.Memory.Start;
                Adapter->RegistersLength = Resource->u.Memory.Length;
            }
            else if (Resource->Type == CmResourceTypeInterrupt && !Adapter->InterruptFound)
            {
                Adapter->InterruptVector = Resource->u.Interrupt.Vector;
                Adapter->InterruptIrql = (KIRQL)Resource->u.Interrupt.Level;
                Adapter->InterruptLevelSensitive = !(Resource->Flags & CM_RESOURCE_INTERRUPT_LATCHED);
                Adapter->InterruptFound = TRUE;
            }
        }
    }
    return Adapter->RegistersLength && Adapter->InterruptFound ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS
PowerVrStartCore(_Inout_ PPOWERVR_ADAPTER Adapter)
{
    struct pvr_glue_platform Platform;
    int Error;

    RtlZeroMemory(&Platform, sizeof(Platform));
    Platform.registers_physical = (uint64_t)Adapter->RegistersPhysical.QuadPart;
    Platform.registers_length = Adapter->RegistersLength;
    Platform.interrupt_vector = Adapter->InterruptVector;
    Platform.interrupt_irql = Adapter->InterruptIrql;
    Platform.interrupt_level_sensitive = Adapter->InterruptLevelSensitive;
    Platform.core_clock_hz = Adapter->Platform->CoreClockHz;
    Platform.dma_limit = POWERVR_DMA_LIMIT;
    Platform.compatible = Adapter->Platform->Compatible;
    Platform.compatible_count = Adapter->Platform->CompatibleCount;

    Error = pvr_glue_probe(&Platform, &Adapter->Core);
    if (Error)
    {
        DPRINT1("POWERVR: PowerVR core probe failed (%d)\n", Error);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static VOID
PowerVrReadCoreId(_Inout_ PPOWERVR_ADAPTER Adapter)
{
    ULONG64 Value = PowerVrRead64(Adapter, ROGUE_CR_CORE_ID__PBVNC);

    Adapter->Branch = (USHORT)(Value >> 48);
    Adapter->Version = (USHORT)(Value >> 32);
    Adapter->NumberOfScalableUnits = (USHORT)(Value >> 16);
    Adapter->Config = (USHORT)Value;
    DPRINT1("POWERVR: %s GPU BVNC %u.%u.%u.%u (CORE_ID 0x%016I64x)\n",
            Adapter->Platform->Name, Adapter->Branch, Adapter->Version,
            Adapter->NumberOfScalableUnits, Adapter->Config,
            PowerVrRead64(Adapter, ROGUE_CR_CORE_ID));
}

static NTSTATUS
APIENTRY
PowerVrDdiAddDevice(
    _In_ CONST PDEVICE_OBJECT PhysicalDeviceObject,
    _Outptr_ PVOID *MiniportDeviceContext)
{
    PPOWERVR_ADAPTER Adapter;
    const POWERVR_PLATFORM *Platform;

    if (!PhysicalDeviceObject || !MiniportDeviceContext)
        return STATUS_INVALID_PARAMETER;
    Platform = PowerVrSelectPlatform(PhysicalDeviceObject);
    if (!Platform)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), POWERVR_TAG);
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->Magic = POWERVR_ADAPTER_MAGIC;
    KeInitializeSpinLock(&Adapter->FenceLock);
    KeInitializeDpc(&Adapter->CompletionDpc, PowerVrCompletionDpc, Adapter);
    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->Platform = Platform;
    *MiniportDeviceContext = Adapter;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiStartDevice(
    _In_ PVOID MiniportDeviceContext,
    _In_ PDXGK_START_INFO DxgkStartInfo,
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ PULONG NumberOfVideoPresentSources,
    _Out_ PULONG NumberOfChildren)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);
    PHYSICAL_ADDRESS Low, High, Skip;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DxgkStartInfo);

    if (!Adapter || !DxgkInterface || !NumberOfVideoPresentSources || !NumberOfChildren)
        return STATUS_INVALID_PARAMETER;
    Adapter->Dxgk = *DxgkInterface;
    Status = Adapter->Dxgk.DxgkCbGetDeviceInformation(Adapter->Dxgk.DeviceHandle, &Adapter->DeviceInfo);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = PowerVrFindResources(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Adapter->Registers = MmMapIoSpace(Adapter->RegistersPhysical, Adapter->RegistersLength, MmNonCached);
    if (!Adapter->Registers)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = Adapter->Platform->PowerUp(Adapter);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Adapter->Powered = TRUE;
    PowerVrReadCoreId(Adapter);

    Low.QuadPart = 0;
    High.QuadPart = POWERVR_DMA_LIMIT;
    Skip.QuadPart = 0;
    Adapter->Segment = MmAllocateContiguousMemorySpecifyCache(POWERVR_SEGMENT_SIZE, Low, High, Skip, MmWriteCombined);
    if (!Adapter->Segment)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Adapter->SegmentPhysical = MmGetPhysicalAddress(Adapter->Segment);

    Status = PowerVrStartCore(Adapter);
    if (!NT_SUCCESS(Status))
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->Segment, POWERVR_SEGMENT_SIZE, MmWriteCombined);
        Adapter->Segment = NULL;
        goto Fail;
    }

    *NumberOfVideoPresentSources = 0;
    *NumberOfChildren = 0;
    Adapter->Started = TRUE;
    return STATUS_SUCCESS;

Fail:
    if (Adapter->Powered)
    {
        Adapter->Platform->PowerDown(Adapter);
        Adapter->Powered = FALSE;
    }
    MmUnmapIoSpace(Adapter->Registers, Adapter->RegistersLength);
    Adapter->Registers = NULL;
    return Status;
}

static NTSTATUS
APIENTRY
PowerVrDdiStopDevice(_In_ PVOID MiniportDeviceContext)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);

    if (!Adapter)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->Core)
    {
        pvr_glue_remove(Adapter->Core);
        Adapter->Core = NULL;
    }
    if (Adapter->Segment)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->Segment, POWERVR_SEGMENT_SIZE, MmWriteCombined);
        Adapter->Segment = NULL;
    }
    if (Adapter->Powered)
    {
        Adapter->Platform->PowerDown(Adapter);
        Adapter->Powered = FALSE;
    }
    if (Adapter->Registers)
    {
        MmUnmapIoSpace(Adapter->Registers, Adapter->RegistersLength);
        Adapter->Registers = NULL;
    }
    Adapter->Started = FALSE;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiRemoveDevice(_In_ PVOID MiniportDeviceContext)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);

    if (!Adapter)
        return STATUS_INVALID_PARAMETER;
    Adapter->Magic = 0;
    ExFreePoolWithTag(Adapter, POWERVR_TAG);
    return STATUS_SUCCESS;
}

static VOID
APIENTRY
PowerVrDdiResetDevice(_In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

static NTSTATUS
APIENTRY
PowerVrDdiSetPowerState(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(DevicePowerState);
    UNREFERENCED_PARAMETER(ActionType);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiQueryChildRelations(
    _In_ PVOID MiniportDeviceContext,
    _Inout_updates_bytes_(ChildRelationsSize) PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildRelations);
    UNREFERENCED_PARAMETER(ChildRelationsSize);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiQueryChildStatus(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    return STATUS_NOT_SUPPORTED;
}

#define POWERVR_FILL_SEGMENT_COUNTS(Out)            \
    do {                                            \
        (Out)->NbSegment = 1;                       \
        (Out)->PagingBufferSegmentId = 0;           \
        (Out)->PagingBufferSize = 64 * 1024;        \
        (Out)->PagingBufferPrivateDataSize = 0;     \
    } while (0)

#define POWERVR_FILL_SEGMENT(Desc, Adapter)                         \
    do {                                                            \
        RtlZeroMemory((Desc), sizeof(*(Desc)));                     \
        (Desc)->Flags.CpuVisible = 1;                               \
        (Desc)->Flags.PopulatedFromSystemMemory = 1;                \
        (Desc)->Flags.LocalBudgetGroup = 1;                         \
        (Desc)->BaseAddress = (Adapter)->SegmentPhysical;           \
        (Desc)->CpuTranslatedAddress = (Adapter)->SegmentPhysical;  \
        (Desc)->Size = POWERVR_SEGMENT_SIZE;                        \
        (Desc)->CommitLimit = POWERVR_SEGMENT_SIZE;                 \
    } while (0)

static NTSTATUS
APIENTRY
PowerVrDdiQueryAdapterInfo(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);

    if (!Adapter || !QueryAdapterInfo)
        return STATUS_INVALID_PARAMETER;

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (QueryAdapterInfo->pOutputData && QueryAdapterInfo->OutputDataSize)
                RtlZeroMemory(QueryAdapterInfo->pOutputData, QueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;

        case DXGKQAITYPE_DRIVERCAPS:
            if (!QueryAdapterInfo->pOutputData || QueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
                return STATUS_BUFFER_TOO_SMALL;
            RtlCopyMemory(QueryAdapterInfo->pOutputData, &PowerVrDriverCaps, sizeof(DXGK_DRIVERCAPS));
            return STATUS_SUCCESS;

        case DXGKQAITYPE_QUERYSEGMENT:
        {
            PDXGK_QUERYSEGMENTOUT Out = QueryAdapterInfo->pOutputData;

            if (!Out || QueryAdapterInfo->OutputDataSize < sizeof(*Out))
                return STATUS_BUFFER_TOO_SMALL;
            if (!Out->pSegmentDescriptor)
            {
                POWERVR_FILL_SEGMENT_COUNTS(Out);
                return STATUS_SUCCESS;
            }
            if (Out->NbSegment < 1)
                return STATUS_INVALID_PARAMETER;
            POWERVR_FILL_SEGMENT(&Out->pSegmentDescriptor[0], Adapter);
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT3:
        {
            PDXGK_QUERYSEGMENTOUT3 Out = QueryAdapterInfo->pOutputData;

            if (!Out || QueryAdapterInfo->OutputDataSize < sizeof(*Out))
                return STATUS_BUFFER_TOO_SMALL;
            if (!Out->pSegmentDescriptor)
            {
                POWERVR_FILL_SEGMENT_COUNTS(Out);
                return STATUS_SUCCESS;
            }
            if (Out->NbSegment < 1)
                return STATUS_INVALID_PARAMETER;
            POWERVR_FILL_SEGMENT(&Out->pSegmentDescriptor[0], Adapter);
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT4:
        {
            PDXGK_QUERYSEGMENTOUT4 Out = QueryAdapterInfo->pOutputData;

            if (!Out || QueryAdapterInfo->OutputDataSize < sizeof(*Out))
                return STATUS_BUFFER_TOO_SMALL;
            if (!Out->pSegmentDescriptor)
            {
                POWERVR_FILL_SEGMENT_COUNTS(Out);
                return STATUS_SUCCESS;
            }
            if (Out->NbSegment < 1 || Out->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
                return STATUS_INVALID_PARAMETER;
            POWERVR_FILL_SEGMENT((PDXGK_SEGMENTDESCRIPTOR4)Out->pSegmentDescriptor, Adapter);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static NTSTATUS
APIENTRY
PowerVrDdiCreateDevice(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_CREATEDEVICE *CreateDevice)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);
    PPOWERVR_DEVICE Device;

    if (!Adapter || !CreateDevice)
        return STATUS_INVALID_PARAMETER;
    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device), POWERVR_TAG);
    if (!Device)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Magic = POWERVR_DEVICE_MAGIC;
    Device->Adapter = Adapter;
    Device->hDevice = CreateDevice->hDevice;
    if (Adapter->Core && pvr_glue_open(Adapter->Core, &Device->File))
    {
        ExFreePoolWithTag(Device, POWERVR_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    CreateDevice->hDevice = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiDestroyDevice(_In_ HANDLE hDevice)
{
    PPOWERVR_DEVICE Device = hDevice;

    if (!Device || Device->Magic != POWERVR_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;
    if (Device->File)
    {
        pvr_glue_close(Device->File);
        Device->File = NULL;
    }
    Device->Magic = 0;
    ExFreePoolWithTag(Device, POWERVR_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiCreateContext(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ DXGKARG_CREATECONTEXT *CreateContext)
{
    PPOWERVR_DEVICE Device = MiniportDeviceContext;
    PPOWERVR_CONTEXT Context;

    if (!Device || Device->Magic != POWERVR_DEVICE_MAGIC || !CreateContext ||
        CreateContext->NodeOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), POWERVR_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Magic = POWERVR_CONTEXT_MAGIC;
    Context->Adapter = Device->Adapter;
    Context->NodeOrdinal = CreateContext->NodeOrdinal;
    CreateContext->ContextInfo.DmaBufferSize = 4096;
    CreateContext->ContextInfo.DmaBufferSegmentSet = 0;
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
    CreateContext->ContextInfo.AllocationListSize = 0;
    CreateContext->ContextInfo.PatchLocationListSize = 0;
    CreateContext->ContextInfo.Caps.Value = 0;
    CreateContext->hContext = Context;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiDestroyContext(_In_ HANDLE hContext)
{
    PPOWERVR_CONTEXT Context = hContext;

    if (!Context || Context->Magic != POWERVR_CONTEXT_MAGIC)
        return STATUS_INVALID_PARAMETER;
    Context->Magic = 0;
    ExFreePoolWithTag(Context, POWERVR_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiBuildPagingBuffer(
    _In_ PVOID MiniportDeviceContext,
    _In_ DXGKARG_BUILDPAGINGBUFFER *BuildPagingBuffer)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(BuildPagingBuffer);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiPatch(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PATCH *Patch)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(Patch);
    return STATUS_SUCCESS;
}

static VOID
NTAPI
PowerVrCompletionDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PPOWERVR_ADAPTER Adapter = DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA Notify;
    ULONG Fence;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Adapter->FenceLock);
    Fence = Adapter->SubmittedFence;
    Adapter->CompletedFence = Fence;
    KeReleaseSpinLockFromDpcLevel(&Adapter->FenceLock);

    RtlZeroMemory(&Notify, sizeof(Notify));
    Notify.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
    Notify.DmaCompleted.SubmissionFenceId = Fence;
    Notify.DmaCompleted.NodeOrdinal = 0;
    Notify.DmaCompleted.EngineOrdinal = 0;
    Adapter->Dxgk.DxgkCbNotifyInterrupt(Adapter->Dxgk.DeviceHandle, &Notify);
    Adapter->Dxgk.DxgkCbNotifyDpc(Adapter->Dxgk.DeviceHandle);
}

static NTSTATUS
APIENTRY
PowerVrDdiSubmitCommand(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(hAdapter);
    KIRQL OldIrql;

    if (!Adapter || !SubmitCommand || SubmitCommand->NodeOrdinal != 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Adapter->FenceLock, &OldIrql);
    Adapter->SubmittedFence = SubmitCommand->SubmissionFenceId;
    KeInsertQueueDpc(&Adapter->CompletionDpc, NULL, NULL);
    KeReleaseSpinLock(&Adapter->FenceLock, OldIrql);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiPreemptCommand(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(PreemptCommand);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
APIENTRY
PowerVrDdiQueryCurrentFence(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE *CurrentFence)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(hAdapter);
    KIRQL OldIrql;

    if (!Adapter || !CurrentFence || CurrentFence->NodeOrdinal != 0 || CurrentFence->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Adapter->FenceLock, &OldIrql);
    CurrentFence->CurrentFence = Adapter->CompletedFence;
    KeReleaseSpinLock(&Adapter->FenceLock, OldIrql);
    return STATUS_SUCCESS;
}

static NTSTATUS
PowerVrEscapeQueryInfo(
    _In_ PPOWERVR_ADAPTER Adapter,
    _Inout_ POWERVR_ESCAPE_INFO *Info)
{
    Info->Branch = Adapter->Branch;
    Info->Version = Adapter->Version;
    Info->NumberOfScalableUnits = Adapter->NumberOfScalableUnits;
    Info->Config = Adapter->Config;
    struct pvr_glue_info Core;

    Info->FirmwareState = 0;
    Info->FirmwareVersion = 0;
    if (Adapter->Core && !pvr_glue_query(Adapter->Core, &Core))
    {
        Info->FirmwareState = Core.firmware_booted;
        Info->FirmwareVersion = (Core.firmware_major << 16) | (Core.firmware_minor & 0xFFFF);
    }
    Info->CoreClockHz = Adapter->Platform->CoreClockHz;
    return STATUS_SUCCESS;
}

static PVOID
PowerVrEscapeFile(_In_ CONST DXGKARG_ESCAPE *Escape)
{
    PPOWERVR_DEVICE Device = Escape->hDevice;

    if (!Device || Device->Magic != POWERVR_DEVICE_MAGIC)
        return NULL;
    return Device->File;
}

static NTSTATUS
APIENTRY
PowerVrDdiEscape(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(hAdapter);
    POWERVR_ESCAPE_HEADER *Header;
    NTSTATUS Status;

    if (!Adapter || !Escape || !Escape->pPrivateDriverData ||
        Escape->PrivateDriverDataSize < sizeof(POWERVR_ESCAPE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Header = Escape->pPrivateDriverData;
    if (Header->Magic != POWERVR_ESCAPE_MAGIC || Header->AbiVersion != POWERVR_ESCAPE_ABI_VERSION ||
        Header->Size > Escape->PrivateDriverDataSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (Header->Command)
    {
        case POWERVR_ESCAPE_QUERY_INFO:
            if (Header->Size < sizeof(POWERVR_ESCAPE_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            Status = PowerVrEscapeQueryInfo(Adapter, (POWERVR_ESCAPE_INFO *)Header);
            break;

        case POWERVR_ESCAPE_DRM_IOCTL:
        {
            POWERVR_DRM_IOCTL_ESCAPE *Ioctl = (POWERVR_DRM_IOCTL_ESCAPE *)Header;
            PVOID File = PowerVrEscapeFile(Escape);

            if (Header->Size < sizeof(*Ioctl) || !File)
                return STATUS_INVALID_PARAMETER;
            Header->Status = (LONG)pvr_glue_ioctl(File, Ioctl->Command, (PVOID)(ULONG_PTR)Ioctl->Argument,
                                                  Ioctl->ArgumentSize);
            return STATUS_SUCCESS;
        }

        case POWERVR_ESCAPE_SYNC_MERGE:
        {
            POWERVR_SYNC_MERGE_ESCAPE *Merge = (POWERVR_SYNC_MERGE_ESCAPE *)Header;
            PVOID File = PowerVrEscapeFile(Escape);

            if (Header->Size < sizeof(*Merge) || Merge->Count > POWERVR_SYNC_MERGE_COUNT || !File)
                return STATUS_INVALID_PARAMETER;
            Header->Status = (LONG)pvr_glue_sync_merge(File, Merge->Destination, Merge->DestinationPoint, Merge->Count,
                                                       (const uint32_t *)Merge->Handles, Merge->Points);
            return STATUS_SUCCESS;
        }

        case POWERVR_ESCAPE_DRM_MMAP:
        case POWERVR_ESCAPE_DRM_MUNMAP:
        {
            POWERVR_DRM_MAP_ESCAPE *Map = (POWERVR_DRM_MAP_ESCAPE *)Header;
            PVOID File = PowerVrEscapeFile(Escape);
            uint64_t Address = 0;

            if (Header->Size < sizeof(*Map) || !File)
                return STATUS_INVALID_PARAMETER;
            if (Header->Command == POWERVR_ESCAPE_DRM_MMAP)
            {
                Header->Status = pvr_glue_mmap(File, Map->Offset, Map->Length, &Address);
                Map->Address = Address;
            }
            else
            {
                Header->Status = pvr_glue_munmap(File, Map->Address);
            }
            return STATUS_SUCCESS;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Header->Status = Status;
    return STATUS_SUCCESS;
}

static BOOLEAN
APIENTRY
PowerVrDdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber)
{
    PPOWERVR_ADAPTER Adapter = MiniportDeviceContext;
    int QueueDpc = 0;
    BOOLEAN Handled;

    UNREFERENCED_PARAMETER(MessageNumber);

    if (!Adapter || !Adapter->Core)
        return FALSE;
    Handled = pvr_glue_isr(Adapter->Core, &QueueDpc) != 0;
    if (QueueDpc)
        Adapter->Dxgk.DxgkCbQueueDpc(Adapter->Dxgk.DeviceHandle);
    return Handled;
}

static VOID
APIENTRY
PowerVrDdiDpcRoutine(_In_ PVOID MiniportDeviceContext)
{
    PPOWERVR_ADAPTER Adapter = PowerVrAdapter(MiniportDeviceContext);

    if (!Adapter)
        return;
    if (Adapter->Core)
        pvr_glue_dpc(Adapter->Core);
    Adapter->Dxgk.DxgkCbNotifyDpc(Adapter->Dxgk.DeviceHandle);
}

static NTSTATUS
APIENTRY
PowerVrDdiRender(
    _In_ CONST HANDLE hContext,
    _Inout_ DXGKARG_RENDER *Render)
{
    PPOWERVR_CONTEXT Context = (PPOWERVR_CONTEXT)hContext;

    if (!Context || Context->Magic != POWERVR_CONTEXT_MAGIC || !Render)
        return STATUS_INVALID_PARAMETER;
    if (Render->CommandLength != 0 || Render->PatchLocationListInSize != 0)
        return STATUS_INVALID_PARAMETER;
    Render->MultipassOffset = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiResetFromTimeout(_In_ CONST HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_SUCCESS;
}

static NTSTATUS
APIENTRY
PowerVrDdiRestartFromTimeout(_In_ CONST HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_SUCCESS;
}

static VOID
APIENTRY
PowerVrDdiUnload(VOID)
{
    pvr_glue_driver_exit();
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA InitData;
    NTSTATUS Status;

    if (pvr_glue_driver_init())
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.Version = POWERVR_DECLARED_INTERFACE_VERSION;
    InitData.DxgkDdiAddDevice = PowerVrDdiAddDevice;
    InitData.DxgkDdiStartDevice = PowerVrDdiStartDevice;
    InitData.DxgkDdiStopDevice = PowerVrDdiStopDevice;
    InitData.DxgkDdiRemoveDevice = PowerVrDdiRemoveDevice;
    InitData.DxgkDdiResetDevice = PowerVrDdiResetDevice;
    InitData.DxgkDdiSetPowerState = PowerVrDdiSetPowerState;
    InitData.DxgkDdiQueryAdapterInfo = PowerVrDdiQueryAdapterInfo;
    InitData.DxgkDdiQueryChildRelations = PowerVrDdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = PowerVrDdiQueryChildStatus;
    InitData.DxgkDdiInterruptRoutine = PowerVrDdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine = PowerVrDdiDpcRoutine;
    InitData.DxgkDdiCreateDevice = PowerVrDdiCreateDevice;
    InitData.DxgkDdiDestroyDevice = PowerVrDdiDestroyDevice;
    InitData.DxgkDdiCreateContext = PowerVrDdiCreateContext;
    InitData.DxgkDdiDestroyContext = PowerVrDdiDestroyContext;
    InitData.DxgkDdiBuildPagingBuffer = PowerVrDdiBuildPagingBuffer;
    InitData.DxgkDdiPatch = PowerVrDdiPatch;
    InitData.DxgkDdiSubmitCommand = PowerVrDdiSubmitCommand;
    InitData.DxgkDdiRender = PowerVrDdiRender;
    InitData.DxgkDdiPreemptCommand = PowerVrDdiPreemptCommand;
    InitData.DxgkDdiQueryCurrentFence = PowerVrDdiQueryCurrentFence;
    InitData.DxgkDdiEscape = PowerVrDdiEscape;
    InitData.DxgkDdiResetFromTimeout = PowerVrDdiResetFromTimeout;
    InitData.DxgkDdiRestartFromTimeout = PowerVrDdiRestartFromTimeout;
    InitData.DxgkDdiUnload = PowerVrDdiUnload;
    Status = DxgkInitialize(DriverObject, RegistryPath, &InitData);
    if (!NT_SUCCESS(Status))
        pvr_glue_driver_exit();
    return Status;
}
