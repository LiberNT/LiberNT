/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth extensible transport (BthX) miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "bthmini.h"

#define NDEBUG
#include <debug.h>

DEFINE_GUID(GUID_BLUETOOTH_RADIO_INTERFACE, 0x92383b0e, 0xf90e, 0x4ac9, 0x8d, 0x44, 0x8c, 0x2d, 0x0d, 0x0e, 0xbd, 0xa2);

typedef struct _BTHMINI_READ
{
    PBTHMINI_DEVICE Dev;
    WDFMEMORY Input;
    WDFMEMORY Output;
    ULONG Type;
} BTHMINI_READ, *PBTHMINI_READ;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHMINI_READ, BthMiniGetRead)

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD BthMiniEvtDeviceAdd;
static EVT_WDF_DEVICE_D0_ENTRY BthMiniEvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT BthMiniEvtD0Exit;
static EVT_WDF_IO_QUEUE_STATE BthMiniEvtQueueReady;
static EVT_WDF_REQUEST_COMPLETION_ROUTINE BthMiniReadComplete;

static
NTSTATUS
BthMiniIoctl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Code,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    WDF_MEMORY_DESCRIPTOR InDescriptor, OutDescriptor;
    WDF_REQUEST_SEND_OPTIONS Options;

    WDF_REQUEST_SEND_OPTIONS_INIT(&Options, 0);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&Options, WDF_REL_TIMEOUT_IN_SEC(5));
    if (Input)
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&InDescriptor, Input, InputLength);
    if (Output)
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&OutDescriptor, Output, OutputLength);
    return WdfIoTargetSendIoctlSynchronously(Dev->Target, NULL, Code,
                                             Input ? &InDescriptor : NULL,
                                             Output ? &OutDescriptor : NULL,
                                             &Options, NULL);
}

NTSTATUS
BthMiniSendPacket(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHX_HCI_READ_WRITE_CONTEXT Context;
    ULONG Size = FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Length;
    ULONG Written = 0;
    NTSTATUS Status;

    Context = ExAllocatePoolWithTag(NonPagedPool, Size, BTHMINI_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    Context->DataLen = Length;
    Context->Type = Type;
    RtlCopyMemory(Context->Data, Data, Length);
    Status = BthMiniIoctl(Dev, IOCTL_BTHX_WRITE_HCI, Context, Size, &Written, sizeof(Written));
    ExFreePoolWithTag(Context, BTHMINI_TAG);
    return Status;
}

BOOLEAN
BthMiniWaitWork(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    if (Dev->Stopping)
        return FALSE;
    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeWaitForSingleObject(&Dev->WorkEvent, Executive, KernelMode, FALSE, &Timeout);
    return !Dev->Stopping;
}

static
VOID
BthMiniQueueRx(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHMINI_RX_PACKET Packet;
    KIRQL OldIrql;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHMINI_RX_PACKET, Data[Length]), BTHMINI_TAG);
    if (!Packet)
        return;
    Packet->Type = Type;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);

    KeAcquireSpinLock(&Dev->RxLock, &OldIrql);
    if (Dev->RxCount >= BTHMINI_MAX_RX_PACKETS)
    {
        KeReleaseSpinLock(&Dev->RxLock, OldIrql);
        ExFreePoolWithTag(Packet, BTHMINI_TAG);
        DPRINT1("BTHMINI: receive queue full, packet dropped\n");
        return;
    }
    InsertTailList(&Dev->RxPackets, &Packet->Link);
    Dev->RxCount++;
    KeReleaseSpinLock(&Dev->RxLock, OldIrql);
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
BthMiniSendRead(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHMINI_READ Read = BthMiniGetRead(Request);
    WDF_REQUEST_REUSE_PARAMS Reuse;
    NTSTATUS Status;

    WDF_REQUEST_REUSE_PARAMS_INIT(&Reuse, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
    WdfRequestReuse(Request, &Reuse);
    Status = WdfIoTargetFormatRequestForIoctl(Dev->Target, Request, IOCTL_BTHX_READ_HCI,
                                              Read->Input, NULL, Read->Output, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    WdfRequestSetCompletionRoutine(Request, BthMiniReadComplete, Read);
    InterlockedIncrement(&Dev->ReadsOutstanding);
    if (!WdfRequestSend(Request, Dev->Target, WDF_NO_SEND_OPTIONS))
    {
        Status = WdfRequestGetStatus(Request);
        if (InterlockedDecrement(&Dev->ReadsOutstanding) == 0)
            KeSetEvent(&Dev->ReadsDrained, IO_NO_INCREMENT, FALSE);
        return Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BthMiniReadComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context)
{
    PBTHMINI_READ Read = Context;
    PBTHMINI_DEVICE Dev = Read->Dev;
    NTSTATUS Status = Params->IoStatus.Status;
    ULONG_PTR Information = Params->IoStatus.Information;

    UNREFERENCED_PARAMETER(Target);

    if (NT_SUCCESS(Status) && Information >= FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data))
    {
        size_t Size;
        PBTHX_HCI_READ_WRITE_CONTEXT Packet = WdfMemoryGetBuffer(Read->Output, &Size);

        if (Packet->DataLen <= Information - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data))
            BthMiniQueueRx(Dev, Packet->Type, Packet->Data, Packet->DataLen);
    }

    if (InterlockedDecrement(&Dev->ReadsOutstanding) == 0 && Dev->Stopping)
        KeSetEvent(&Dev->ReadsDrained, IO_NO_INCREMENT, FALSE);
    if (Dev->Stopping || Status == STATUS_CANCELLED || Status == STATUS_DEVICE_REMOVED)
        return;
    if (!NT_SUCCESS(BthMiniSendRead(Dev, Request)))
        DPRINT1("BTHMINI: read of HCI type %lu not reposted\n", Read->Type);
}

static
NTSTATUS
BthMiniCreateReads(
    _In_ PBTHMINI_DEVICE Dev)
{
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(Dev->Reads); Index++)
    {
        WDF_OBJECT_ATTRIBUTES Attributes;
        PBTHMINI_READ Read;
        PVOID Buffer;
        size_t Size;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_READ);
        Attributes.ParentObject = Dev->Device;
        Status = WdfRequestCreate(&Attributes, Dev->Target, &Dev->Reads[Index]);
        if (!NT_SUCCESS(Status))
            return Status;
        Read = BthMiniGetRead(Dev->Reads[Index]);
        Read->Dev = Dev;
        Read->Type = Index < BTHMINI_EVENT_READS ? HciPacketEvent : HciPacketAclData;
        Size = FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) +
               (Read->Type == HciPacketEvent ? BTHMINI_MAX_EVENT : 4 + Dev->Capabilities.MaxAclTransferInSize);

        WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
        Attributes.ParentObject = Dev->Reads[Index];
        Status = WdfMemoryCreate(&Attributes, NonPagedPool, BTHMINI_TAG, sizeof(ULONG), &Read->Input, &Buffer);
        if (!NT_SUCCESS(Status))
            return Status;
        *(PULONG)Buffer = Read->Type;
        Status = WdfMemoryCreate(&Attributes, NonPagedPool, BTHMINI_TAG, Size, &Read->Output, &Buffer);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
BthMiniFlushRx(
    _In_ PBTHMINI_DEVICE Dev)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->RxLock, &OldIrql);
    while (!IsListEmpty(&Dev->RxPackets))
    {
        PBTHMINI_RX_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->RxPackets), BTHMINI_RX_PACKET, Link);

        ExFreePoolWithTag(Packet, BTHMINI_TAG);
    }
    Dev->RxCount = 0;
    KeReleaseSpinLock(&Dev->RxLock, OldIrql);
}

static
VOID
NTAPI
BthMiniThread(
    _In_ PVOID Context)
{
    PBTHMINI_DEVICE Dev = Context;
    NTSTATUS Status;

    Status = BthHciInitialize(Dev);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHMINI: radio initialization failed 0x%08lx\n", Status);
        while (BthMiniWaitWork(Dev, 1000))
        {
            WDFREQUEST Request;

            BthHciProcessRx(Dev);
            while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Dev->Queue, &Request)))
                WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        }
        PsTerminateSystemThread(Status);
    }

    BthConnLoadPaired(Dev);
    BthSmpLoadKeys(Dev);
    BthGattLoad(Dev);
    BthEnumLoad(Dev);
    BthEnumLoadLocalServices(Dev);
    Dev->Ready = TRUE;
    BthTdiRadioReady(Dev);
    while (!Dev->Stopping)
    {
        WDFREQUEST Request;
        ULONG Wait;

        BthHciProcessRx(Dev);
        while (!Dev->Stopping && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Dev->Queue, &Request)))
        {
            BthRadioDeviceControl(Dev, Request);
            BthHciProcessRx(Dev);
        }
        BthConnTick(Dev);
        BthLeTick(Dev);
        BthL2capTick(Dev);
        BthRfcommTick(Dev);
        BthSdpClientTick(Dev);
        BthTdiProcess(Dev);
        Wait = BthComProcess(Dev);
        BthMiniWaitWork(Dev, max(Wait, 1));
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
BthMiniEvtQueueReady(
    _In_ WDFQUEUE Queue,
    _In_ WDFCONTEXT Context)
{
    PBTHMINI_DEVICE Dev = Context;

    UNREFERENCED_PARAMETER(Queue);
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
BthMiniEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PBTHMINI_DEVICE Dev = BthMiniGetDevice(Device);
    BTHX_VERSION Version;
    HANDLE Thread;
    ULONG Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);

    RtlZeroMemory(&Version, sizeof(Version));
    Status = BthMiniIoctl(Dev, IOCTL_BTHX_GET_VERSION, NULL, 0, &Version, sizeof(Version));
    if (!NT_SUCCESS(Status) || Version.Version < BTHX_DDI_VERSION_1)
    {
        DPRINT1("BTHMINI: transport version query failed 0x%08lx (version %lu)\n", Status, Version.Version);
        return NT_SUCCESS(Status) ? STATUS_NOT_SUPPORTED : Status;
    }
    Version.Version = BTHX_DDI_VERSION_1;
    Status = BthMiniIoctl(Dev, IOCTL_BTHX_SET_VERSION, &Version, sizeof(Version), NULL, 0);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Dev->Capabilities, sizeof(Dev->Capabilities));
        Status = BthMiniIoctl(Dev, IOCTL_BTHX_QUERY_CAPABILITIES, NULL, 0,
                              &Dev->Capabilities, sizeof(Dev->Capabilities));
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHMINI: transport negotiation failed 0x%08lx\n", Status);
        return Status;
    }
    if (!Dev->Capabilities.MaxAclTransferInSize)
        Dev->Capabilities.MaxAclTransferInSize = 1021;

    if (!Dev->Reads[0])
    {
        Status = BthMiniCreateReads(Dev);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Dev->Stopping = FALSE;
    Dev->Ready = FALSE;
    Dev->CommandCredits = 1;
    KeClearEvent(&Dev->ReadsDrained);
    for (Index = 0; Index < RTL_NUMBER_OF(Dev->Reads); Index++)
    {
        Status = BthMiniSendRead(Dev, Dev->Reads[Index]);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BTHMINI: HCI read not posted 0x%08lx\n", Status);
            return Status;
        }
    }

    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, BthMiniThread, Dev);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Dev->Thread, NULL);
    ZwClose(Thread);
    return Status;
}

static
NTSTATUS
NTAPI
BthMiniEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PBTHMINI_DEVICE Dev = BthMiniGetDevice(Device);
    ULONG Index;

    UNREFERENCED_PARAMETER(TargetState);

    Dev->Stopping = TRUE;
    Dev->Ready = FALSE;
    KeMemoryBarrier();
    KeSetEvent(&Dev->WorkEvent, IO_NO_INCREMENT, FALSE);
    if (Dev->Thread)
    {
        KeWaitForSingleObject(Dev->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Dev->Thread);
        Dev->Thread = NULL;
    }
    while (Dev->ReadsOutstanding)
    {
        LARGE_INTEGER Timeout;

        for (Index = 0; Index < RTL_NUMBER_OF(Dev->Reads); Index++)
        {
            if (Dev->Reads[Index])
                WdfRequestCancelSentRequest(Dev->Reads[Index]);
        }
        Timeout.QuadPart = -100 * 10000LL;
        KeWaitForSingleObject(&Dev->ReadsDrained, Executive, KernelMode, FALSE, &Timeout);
    }
    BthHciFlushCommands(Dev);
    BthMiniFlushRx(Dev);
    BthBrbFlush(Dev);
    BthL2capFlush(Dev);
    BthSdpClientFlush(Dev);
    BthTdiRadioStopping(Dev);
    BthRadioFreeRemotes(Dev);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BthMiniEvtCleanup(
    _In_ WDFOBJECT Object)
{
    PBTHMINI_DEVICE Dev = BthMiniGetDevice((WDFDEVICE)Object);

    BthEnumFlush(Dev);
    BthBrbCleanup(Dev);
    BthSdpCleanup(Dev);
    BthRadioCleanup(Dev);
}

static
VOID
NTAPI
BthMiniEvtIoInCallerContext(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request)
{
    WDF_REQUEST_PARAMETERS Parameters;
    NTSTATUS Status;

    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(Request, &Parameters);
    if (Parameters.Type == WdfRequestTypeDeviceControl &&
        Parameters.Parameters.DeviceIoControl.IoControlCode == IOCTL_BTHMINI_SET_LOCAL_SERVICE &&
        WdfRequestGetRequestorMode(Request) == UserMode &&
        !SeSinglePrivilegeCheck(RtlConvertLongToLuid(SE_LOAD_DRIVER_PRIVILEGE), UserMode))
    {
        WdfRequestComplete(Request, STATUS_PRIVILEGE_NOT_HELD);
        return;
    }
    Status = WdfDeviceEnqueueRequest(Device, Request);
    if (!NT_SUCCESS(Status))
        WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
BthMiniEvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject)
{
    BthSdpClientFileClosed(BthMiniGetDevice(WdfFileObjectGetDevice(FileObject)), FileObject);
}

static
NTSTATUS
NTAPI
BthMiniEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_FILEOBJECT_CONFIG FileConfig;
    PBTHMINI_DEVICE Dev;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BLUETOOTH);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDeviceD0Entry = BthMiniEvtD0Entry;
    Callbacks.EvtDeviceD0Exit = BthMiniEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Callbacks);
    WDF_FILEOBJECT_CONFIG_INIT(&FileConfig, NULL, NULL, BthMiniEvtFileCleanup);
    FileConfig.FileObjectClass = (WDF_FILEOBJECT_CLASS)(WdfFileObjectWdfCannotUseFsContexts | WdfFileObjectCanBeOptional);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &FileConfig, WDF_NO_OBJECT_ATTRIBUTES);
    WdfDeviceInitSetIoInCallerContextCallback(DeviceInit, BthMiniEvtIoInCallerContext);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHMINI_DEVICE);
    Attributes.EvtCleanupCallback = BthMiniEvtCleanup;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Dev = BthMiniGetDevice(Device);
    Dev->Device = Device;
    Dev->Target = WdfDeviceGetIoTarget(Device);
    KeInitializeEvent(&Dev->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Dev->ReadsDrained, NotificationEvent, FALSE);
    KeInitializeSpinLock(&Dev->RxLock);
    InitializeListHead(&Dev->RxPackets);
    InitializeListHead(&Dev->CommandsPending);
    InitializeListHead(&Dev->CommandsSent);
    InitializeListHead(&Dev->Remotes);
    InitializeListHead(&Dev->DiscoveryOwners);
    InitializeListHead(&Dev->Channels);
    InitializeListHead(&Dev->Sessions);
    InitializeListHead(&Dev->RfcommServers);
    InitializeListHead(&Dev->ComPorts);
    InitializeListHead(&Dev->AclQueue);
    InitializeListHead(&Dev->LeAclQueue);
    InitializeListHead(&Dev->Services);
    InitializeListHead(&Dev->LocalServices);
    BthSdpInitialize(Dev);
    InitializeListHead(&Dev->SdpConnections);
    KeInitializeSpinLock(&Dev->SdpClosedFilesLock);
    InitializeListHead(&Dev->SdpClosedFiles);
    InitializeListHead(&Dev->ProfileServers);
    InitializeListHead(&Dev->ProfilePsms);
    InitializeListHead(&Dev->ProfileChannels);
    Dev->NextCid = 0x0040;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchManual);
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Queue);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfIoQueueReadyNotify(Dev->Queue, BthMiniEvtQueueReady, Dev);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WdfDeviceCreateDeviceInterface(Device, &GUID_BTHPORT_DEVICE_INTERFACE, NULL);
    if (NT_SUCCESS(Status))
        Status = WdfDeviceCreateDeviceInterface(Device, &GUID_BLUETOOTH_RADIO_INTERFACE, NULL);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(BthAuthCreatePdo(Dev)))
        DPRINT1("BTHMINI: pairing request device not created\n");
    return Status;
}

static
VOID
NTAPI
BthMiniEvtDriverUnload(
    _In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
    BthTdiUnload();
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, BthMiniEvtDeviceAdd);
    Config.EvtDriverUnload = BthMiniEvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = BthTdiInitialize(Driver);
    if (!NT_SUCCESS(Status))
        DPRINT1("BTHMINI: RFCOMM socket transport not created 0x%08lx\n", Status);
    return STATUS_SUCCESS;
}
