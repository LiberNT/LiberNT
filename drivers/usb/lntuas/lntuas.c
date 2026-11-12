/*
 * PROJECT:     LiberNT USB Attached SCSI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StorPort virtual miniport entry points and USB configuration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuas.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

static
NTSTATUS
UasReadConfiguration(
    _In_ PUAS_ADAPTER Adapter)
{
    USB_CONFIGURATION_DESCRIPTOR Header;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration;
    URB Urb;
    NTSTATUS Status;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, &Header, NULL, sizeof(Header), NULL);
    Status = UasSyncUrb(Adapter, &Urb);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Urb.UrbControlDescriptorRequest.TransferBufferLength < sizeof(Header) ||
        Header.wTotalLength < sizeof(Header))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Configuration = ExAllocatePoolWithTag(NonPagedPool, Header.wTotalLength, UAS_POOL_TAG);
    if (!Configuration)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, Configuration, NULL, Header.wTotalLength,
                                 NULL);
    Status = UasSyncUrb(Adapter, &Urb);
    if (NT_SUCCESS(Status) && (Urb.UrbControlDescriptorRequest.TransferBufferLength != Header.wTotalLength ||
                               Configuration->wTotalLength != Header.wTotalLength))
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Configuration, UAS_POOL_TAG);
        return Status;
    }
    Adapter->Configuration = Configuration;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UasSelectConfiguration(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PUSB_INTERFACE_DESCRIPTOR Interface)
{
    USBD_INTERFACE_LIST_ENTRY List[2];
    PUSBD_INTERFACE_INFORMATION Information;
    ULONG Role, Index;
    NTSTATUS Status;
    PURB Urb;

    RtlZeroMemory(List, sizeof(List));
    List[0].InterfaceDescriptor = Interface;
    Urb = USBD_CreateConfigurationRequestEx(Adapter->Configuration, List);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = UasSyncUrb(Adapter, Urb);
    if (NT_SUCCESS(Status))
    {
        Information = &Urb->UrbSelectConfiguration.Interface;
        Adapter->Interface = ExAllocatePoolWithTag(NonPagedPool, Information->Length, UAS_POOL_TAG);
        if (Adapter->Interface)
            RtlCopyMemory(Adapter->Interface, Information, Information->Length);
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    ExFreePool(Urb);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Role = 0; Role < UAS_PIPES; Role++)
    {
        for (Index = 0; Index < Adapter->Interface->NumberOfPipes; Index++)
        {
            if (Adapter->Interface->Pipes[Index].EndpointAddress == Adapter->Endpoint[Role])
                break;
        }
        if (Index == Adapter->Interface->NumberOfPipes)
        {
            DPRINT1("lntuas: endpoint %x has no pipe\n", Adapter->Endpoint[Role]);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Adapter->Pipe[Role] = Adapter->Interface->Pipes[Index].PipeHandle;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
UasOpenStreams(
    _In_ PUAS_ADAPTER Adapter)
{
    URB Urb;
    ULONG Role, Index;
    NTSTATUS Status;

    ASSERT(Adapter->Streams);
    ASSERT(Adapter->StreamCount >= 2 && Adapter->StreamCount <= UAS_MAX_STREAMS);
    ASSERT((Adapter->StreamCount & (Adapter->StreamCount - 1)) == 0);

    for (Role = UAS_PIPE_STATUS; Role < UAS_PIPES; Role++)
    {
        RtlZeroMemory(&Urb, sizeof(Urb));
        Urb.UrbHeader.Length = sizeof(struct _URB_OPEN_STATIC_STREAMS);
        Urb.UrbHeader.Function = URB_FUNCTION_OPEN_STATIC_STREAMS;
        Urb.UrbOpenStaticStreams.PipeHandle = Adapter->Pipe[Role];
        Urb.UrbOpenStaticStreams.NumberOfStreams = Adapter->StreamCount;
        Urb.UrbOpenStaticStreams.StreamInfoVersion = URB_OPEN_STATIC_STREAMS_VERSION_100;
        Urb.UrbOpenStaticStreams.StreamInfoSize = sizeof(USBD_STREAM_INFORMATION);
        Urb.UrbOpenStaticStreams.Streams = Adapter->StreamInfo[Role];
        Status = UasSyncUrb(Adapter, &Urb);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("lntuas: pipe %lu opened no streams: %lx\n", Role, Status);
            return Status;
        }
        for (Index = 0; Index < Adapter->StreamCount; Index++)
        {
            if (Adapter->StreamInfo[Role][Index].StreamID != Index + 1 ||
                !Adapter->StreamInfo[Role][Index].PipeHandle)
            {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
UasInitializeTask(
    _In_ PUAS_ADAPTER Adapter,
    _Out_ PUAS_TASK Task,
    _In_ USHORT Tag)
{
    ULONG StatusOffset = ALIGN_UP_BY(UAS_COMMAND_IU_SIZE, SYSTEM_CACHE_ALIGNMENT_SIZE);
    ULONG Size = StatusOffset + ALIGN_UP_BY(UAS_STATUS_IU_SIZE, SYSTEM_CACHE_ALIGNMENT_SIZE);
    ULONG_PTR Base;
    ULONG Phase;

    RtlZeroMemory(Task, sizeof(*Task));
    Task->Adapter = Adapter;
    Task->Tag = Tag;
    Task->State = UasTaskFree;
    Task->Flags = UAS_TASK_ALL_DONE;
    for (Phase = 0; Phase < UAS_PHASES; Phase++)
    {
        Task->Irp[Phase] = IoAllocateIrp(Adapter->LowerDevice->StackSize, FALSE);
        if (!Task->Irp[Phase])
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    Task->IuAllocation = ExAllocatePoolWithTag(NonPagedPool, Size + SYSTEM_CACHE_ALIGNMENT_SIZE, UAS_POOL_TAG);
    if (!Task->IuAllocation)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Task->IuAllocation, Size + SYSTEM_CACHE_ALIGNMENT_SIZE);
    Base = (ULONG_PTR)ALIGN_UP_POINTER_BY(Task->IuAllocation, SYSTEM_CACHE_ALIGNMENT_SIZE);
    Task->CommandIu = (PUCHAR)Base;
    Task->StatusIu = (PUCHAR)Base + StatusOffset;
    return STATUS_SUCCESS;
}

static
VOID
UasFreeTask(
    _Inout_ PUAS_TASK Task)
{
    ULONG Phase;

    ASSERT(Task->State == UasTaskFree);
    ASSERT(Task->Pending == 0);
    for (Phase = 0; Phase < UAS_PHASES; Phase++)
    {
        if (Task->Irp[Phase])
            IoFreeIrp(Task->Irp[Phase]);
        Task->Irp[Phase] = NULL;
    }
    if (Task->IuAllocation)
        ExFreePoolWithTag(Task->IuAllocation, UAS_POOL_TAG);
    Task->IuAllocation = NULL;
}

static
NTSTATUS
UasConfigure(
    _In_ PUAS_ADAPTER Adapter)
{
    PUSB_INTERFACE_DESCRIPTOR Interface;
    ULONG DeviceStreams, Index;
    NTSTATUS Status;

    Status = UasReadConfiguration(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!UasScanConfiguration(Adapter->Configuration, &Interface, Adapter->Endpoint, &DeviceStreams))
    {
        DPRINT1("lntuas: the configuration has no USB Attached SCSI interface\n");
        return STATUS_NOT_SUPPORTED;
    }
    Adapter->Streams = (DeviceStreams >= 2);
    Adapter->StreamCount = Adapter->Streams ? min(DeviceStreams, UAS_MAX_STREAMS) : 0;
    Adapter->TaskCount = Adapter->Streams ? Adapter->StreamCount - 1 : 1;
    DPRINT1("lntuas: interface %u setting %u, %lu streams, %lu commands\n", Interface->bInterfaceNumber,
            Interface->bAlternateSetting, Adapter->StreamCount, Adapter->TaskCount);

    Status = UasSelectConfiguration(Adapter, Interface);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Adapter->Streams)
    {
        Status = UasOpenStreams(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    ASSERT(Adapter->TaskCount >= 1 && Adapter->TaskCount <= UAS_MAX_TASKS);
    for (Index = 0; Index < Adapter->TaskCount; Index++)
    {
        Status = UasInitializeTask(Adapter, &Adapter->Task[Index], (USHORT)(Index + 1));
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return UasInitializeTask(Adapter, &Adapter->ManagementTask, (USHORT)(Adapter->TaskCount + 1));
}

static
ULONG
NTAPI
UasHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_z_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _In_ PBOOLEAN Reserved3)
{
    PUAS_ADAPTER Adapter = DeviceExtension;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Reserved3);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (!Adapter->Configuration)
    {
        if (StorPortGetDeviceObjects(Adapter, (PVOID *)&Adapter->AdapterDevice, (PVOID *)&Adapter->PhysicalDevice,
                                     (PVOID *)&Adapter->LowerDevice) != STOR_STATUS_SUCCESS)
        {
            return SP_RETURN_ERROR;
        }
        KeInitializeSpinLock(&Adapter->Lock);
        KeInitializeEvent(&Adapter->Quiesced, NotificationEvent, FALSE);
        KeInitializeEvent(&Adapter->PortResetIdle, NotificationEvent, TRUE);
        KeInitializeTimer(&Adapter->TmfTimer);
        KeInitializeDpc(&Adapter->TmfDpc, UasTmfTimeoutDpc, Adapter);
        Adapter->PortResetWorkItem = IoAllocateWorkItem(Adapter->AdapterDevice);
        if (!Adapter->PortResetWorkItem)
            return SP_RETURN_ERROR;
        Status = UasConfigure(Adapter);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("lntuas: the device could not be configured: %lx\n", Status);
            return (Status == STATUS_NOT_SUPPORTED) ? SP_RETURN_NOT_FOUND : SP_RETURN_ERROR;
        }
    }

    ConfigInfo->VirtualDevice = TRUE;
    ConfigInfo->MaximumTransferLength = UAS_MAX_TRANSFER_LENGTH;
    ConfigInfo->NumberOfPhysicalBreaks = SP_UNINITIALIZED_VALUE;
    ConfigInfo->AlignmentMask = UAS_ALIGNMENT_MASK;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = SCSI_MAXIMUM_LOGICAL_UNITS;
    ConfigInfo->MaxNumberOfIO = Adapter->TaskCount;
    ConfigInfo->CachesData = FALSE;
    ConfigInfo->MapBuffers = STOR_MAP_NO_BUFFERS;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->WmiDataProvider = FALSE;
    return SP_RETURN_FOUND;
}

static
BOOLEAN
NTAPI
UasHwInitialize(
    _In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return TRUE;
}

static
VOID
UasCompleteSrb(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    StorPortNotification(RequestComplete, Adapter, Srb);
}

static
BOOLEAN
NTAPI
UasHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUAS_ADAPTER Adapter = DeviceExtension;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    switch (Srb->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            if (Srb->PathId != 0)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_PATH_ID);
            else if (Srb->TargetId != 0)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_SELECTION_TIMEOUT);
            else if (Srb->Lun >= SCSI_MAXIMUM_LOGICAL_UNITS)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_LUN);
            else if (Srb->CdbLength == 0 || Srb->CdbLength > 16 ||
                     Srb->DataTransferLength > UAS_MAX_TRANSFER_LENGTH)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
            else
                UasStartSrb(Adapter, Srb);
            break;

        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun >= SCSI_MAXIMUM_LOGICAL_UNITS)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_LUN);
            else
                UasResetLogicalUnit(Adapter, Srb);
            break;

        case SRB_FUNCTION_PNP:
            if (((PSCSI_PNP_REQUEST_BLOCK)Srb)->PnPAction == StorQueryCapabilities)
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
            else
                UasCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_POWER:
            UasCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        default:
            UasCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
            break;
    }
    return TRUE;
}

static
BOOLEAN
NTAPI
UasHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PUAS_ADAPTER Adapter = DeviceExtension;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    DPRINT1("lntuas: bus %lu reset requested\n", PathId);
    UasQueuePortReset(Adapter);
    return TRUE;
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
UasHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PUAS_ADAPTER Adapter = DeviceExtension;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST List;
    KIRQL OldIrql;

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
            List = Parameters;
            if (ScsiQuerySupportedControlTypes < List->MaxControlType)
                List->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            if (ScsiStopAdapter < List->MaxControlType)
                List->SupportedTypeList[ScsiStopAdapter] = TRUE;
            if (ScsiRestartAdapter < List->MaxControlType)
                List->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            return ScsiAdapterControlSuccess;

        case ScsiStopAdapter:
            UasStopAdapter(Adapter);
            return ScsiAdapterControlSuccess;

        case ScsiRestartAdapter:
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            ASSERT(Adapter->ActiveTasks == 0);
            Adapter->Stopped = FALSE;
            if (!Adapter->PortResetQueued && !Adapter->ManagementStarted && Adapter->ResetSrb == NULL)
            {
                Adapter->NexusPending = FALSE;
                Adapter->Resetting = FALSE;
            }
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return ScsiAdapterControlSuccess;

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

static
VOID
NTAPI
UasHwFreeAdapterResources(
    _In_ PVOID DeviceExtension)
{
    PUAS_ADAPTER Adapter = DeviceExtension;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (!Adapter->PortResetWorkItem)
        return;
    UasStopAdapter(Adapter);
    UasWaitQuiesced(Adapter);
    KeCancelTimer(&Adapter->TmfTimer);
    KeFlushQueuedDpcs();
    ASSERT(Adapter->ManagementTask.State == UasTaskFree && !Adapter->ManagementStarted);
    for (Index = 0; Index < UAS_MAX_TASKS; Index++)
        UasFreeTask(&Adapter->Task[Index]);
    UasFreeTask(&Adapter->ManagementTask);
    if (Adapter->Interface)
        ExFreePoolWithTag(Adapter->Interface, UAS_POOL_TAG);
    if (Adapter->Configuration)
        ExFreePoolWithTag(Adapter->Configuration, UAS_POOL_TAG);
    IoFreeWorkItem(Adapter->PortResetWorkItem);
    Adapter->Interface = NULL;
    Adapter->Configuration = NULL;
    Adapter->PortResetWorkItem = NULL;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = Internal;
    InitData.HwInitialize = UasHwInitialize;
    InitData.HwStartIo = UasHwStartIo;
    InitData.HwFindAdapter = UasHwFindAdapter;
    InitData.HwResetBus = UasHwResetBus;
    InitData.HwAdapterControl = UasHwAdapterControl;
    InitData.HwFreeAdapterResources = UasHwFreeAdapterResources;
    InitData.DeviceExtensionSize = sizeof(UAS_ADAPTER);
    InitData.MapBuffers = STOR_MAP_NO_BUFFERS;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;
    InitData.FeatureSupport = STOR_FEATURE_VIRTUAL_MINIPORT;
    InitData.SrbTypeFlags = SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK;
    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
