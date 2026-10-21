/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VMBus connection, channel offers and child device enumeration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vmbus.h"

#define NDEBUG
#include <debug.h>

PDRIVER_OBJECT VmbusDriverObject;

static const GUID VmbusNetworkInterface =
    { 0xf8615163, 0xdf3e, 0x46c5, { 0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e } };

static const ULONG VmbusVersions[] =
{
    VMBUS_VERSION_WIN10_V5_2,
    VMBUS_VERSION_WIN10_V5_1,
    VMBUS_VERSION_WIN10_V5,
    VMBUS_VERSION_WIN10_RS3,
    VMBUS_VERSION_WIN10,
    VMBUS_VERSION_WIN8_1,
    VMBUS_VERSION_WIN8,
};

static
BOOLEAN
VmbusChannelIsSupported(
    _In_ PVMBUS_CHANNEL Channel)
{
    return IsEqualGUID(&Channel->InterfaceType, &VmbusNetworkInterface);
}

VOID
VmbusControlBegin(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_ ULONG ResponseType,
    _In_ ULONG ResponseKey)
{
    ExAcquireFastMutex(&Bus->ControlLock);
    KeClearEvent(&Bus->ResponseEvent);
    Bus->ExpectedKey = ResponseKey;
    InterlockedExchange((LONG volatile *)&Bus->ExpectedResponse, (LONG)ResponseType);
}

NTSTATUS
VmbusControlWait(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _Out_writes_bytes_opt_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -10000LL * 10000;
    Status = KeWaitForSingleObject(&Bus->ResponseEvent, Executive, KernelMode, FALSE, &Timeout);
    if (Status == STATUS_TIMEOUT)
    {
        DPRINT1("VMBUS: no response of type %lu (key %lu)\n", Bus->ExpectedResponse, Bus->ExpectedKey);
        return STATUS_IO_TIMEOUT;
    }

    if (Response != NULL)
        RtlCopyMemory(Response, Bus->Response, min(ResponseSize, (ULONG)sizeof(Bus->Response)));

    return STATUS_SUCCESS;
}

VOID
VmbusControlEnd(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    InterlockedExchange((LONG volatile *)&Bus->ExpectedResponse, 0);
    ExReleaseFastMutex(&Bus->ControlLock);
}

NTSTATUS
VmbusSendControl(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_reads_bytes_(Size) const VOID *Message,
    _In_ ULONG Size,
    _In_ ULONG ResponseType,
    _In_ ULONG ResponseKey,
    _Out_writes_bytes_opt_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize)
{
    NTSTATUS Status;

    VmbusControlBegin(Bus, ResponseType, ResponseKey);
    Status = HvPostMessage(Bus, Message, Size);
    if (NT_SUCCESS(Status) && ResponseType != 0)
        Status = VmbusControlWait(Bus, Response, ResponseSize);
    VmbusControlEnd(Bus);

    return Status;
}

static
PVMBUS_CHANNEL
VmbusFindChannel(
    _In_ PVMBUS_FDO_EXTENSION Bus,
    _In_ ULONG ChildRelId)
{
    PLIST_ENTRY Entry;
    PVMBUS_CHANNEL Channel;

    for (Entry = Bus->Channels.Flink; Entry != &Bus->Channels; Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry, VMBUS_CHANNEL, ListEntry);
        if (Channel->ChildRelId == ChildRelId)
            return Channel;
    }

    return NULL;
}

static
VOID
VmbusAddOffer(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_ PVMBUS_MSG_OFFER_CHANNEL Offer)
{
    PVMBUS_CHANNEL Channel;
    BOOLEAN Invalidate;

    if (Offer->SubchannelIndex != 0)
        return;

    KeAcquireSpinLockAtDpcLevel(&Bus->ChannelLock);
    Channel = VmbusFindChannel(Bus, Offer->ChildRelId);
    KeReleaseSpinLockFromDpcLevel(&Bus->ChannelLock);
    if (Channel != NULL)
        return;

    Channel = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Channel), VMBUS_TAG);
    if (Channel == NULL)
        return;

    RtlZeroMemory(Channel, sizeof(*Channel));
    Channel->Bus = Bus;
    Channel->InterfaceType = Offer->InterfaceType;
    Channel->InterfaceInstance = Offer->InterfaceInstance;
    Channel->ChildRelId = Offer->ChildRelId;
    Channel->ConnectionId = Offer->ConnectionId;

    KeAcquireSpinLockAtDpcLevel(&Bus->ChannelLock);
    InsertTailList(&Bus->Channels, &Channel->ListEntry);
    Invalidate = Bus->Connected && VmbusChannelIsSupported(Channel);
    KeReleaseSpinLockFromDpcLevel(&Bus->ChannelLock);

    if (Invalidate)
        IoInvalidateDeviceRelations(Bus->Pdo, BusRelations);
}

static
VOID
VmbusRescindOffer(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_ ULONG ChildRelId)
{
    PVMBUS_CHANNEL Channel;

    KeAcquireSpinLockAtDpcLevel(&Bus->ChannelLock);
    Channel = VmbusFindChannel(Bus, ChildRelId);
    if (Channel != NULL)
        Channel->Rescinded = TRUE;
    KeReleaseSpinLockFromDpcLevel(&Bus->ChannelLock);

    if (Channel != NULL && Channel->Pdo != NULL)
        IoInvalidateDeviceRelations(Bus->Pdo, BusRelations);
}

static
VOID
VmbusHandleMessage(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_reads_bytes_(Size) PUCHAR Message,
    _In_ ULONG Size)
{
    PVMBUS_MSG_HEADER Header = (PVMBUS_MSG_HEADER)Message;
    ULONG Key = 0;

    if (Size < sizeof(VMBUS_MSG_HEADER))
        return;

    switch (Header->MessageType)
    {
        case VMBUS_MSGTYPE_OFFER_CHANNEL:
            if (Size >= sizeof(VMBUS_MSG_OFFER_CHANNEL))
                VmbusAddOffer(Bus, (PVMBUS_MSG_OFFER_CHANNEL)Message);
            return;

        case VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER:
            if (Size >= sizeof(VMBUS_MSG_CHILD_RELID))
                VmbusRescindOffer(Bus, ((VMBUS_MSG_CHILD_RELID *)Message)->ChildRelId);
            return;

        case VMBUS_MSGTYPE_ALL_OFFERS_DELIVERED:
            KeSetEvent(&Bus->OffersEvent, IO_NO_INCREMENT, FALSE);
            return;

        case VMBUS_MSGTYPE_GPADL_CREATED:
            if (Size < sizeof(VMBUS_MSG_GPADL_CREATED))
                return;
            Key = ((VMBUS_MSG_GPADL_CREATED *)Message)->Gpadl;
            break;

        case VMBUS_MSGTYPE_GPADL_TORNDOWN:
            if (Size < sizeof(VMBUS_MSG_GPADL_TORNDOWN))
                return;
            Key = ((VMBUS_MSG_GPADL_TORNDOWN *)Message)->Gpadl;
            break;

        case VMBUS_MSGTYPE_OPEN_CHANNEL_RESULT:
            if (Size < sizeof(VMBUS_MSG_OPEN_RESULT))
                return;
            Key = ((VMBUS_MSG_OPEN_RESULT *)Message)->ChildRelId;
            break;

        case VMBUS_MSGTYPE_VERSION_RESPONSE:
            if (Size < sizeof(VMBUS_MSG_VERSION_RESPONSE))
                return;
            break;

        default:
            return;
    }

    if ((ULONG)ReadAcquire((LONG volatile *)&Bus->ExpectedResponse) == Header->MessageType &&
        Bus->ExpectedKey == Key)
    {
        RtlZeroMemory(Bus->Response, sizeof(Bus->Response));
        RtlCopyMemory(Bus->Response, Message, min(Size, (ULONG)sizeof(Bus->Response)));
        InterlockedExchange((LONG volatile *)&Bus->ExpectedResponse, 0);
        KeSetEvent(&Bus->ResponseEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
VmbusProcessMessages(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    PHV_MESSAGE Slot = &Bus->MessagePage[VMBUS_MESSAGE_SINT];
    UCHAR Message[HV_MESSAGE_PAYLOAD_SIZE];
    ULONG Size;
    UCHAR Flags;

    while (ReadULongAcquire((ULONG volatile *)&Slot->MessageType) != 0)
    {
        Size = min((ULONG)Slot->PayloadSize, (ULONG)sizeof(Message));
        RtlCopyMemory(Message, Slot->Payload, Size);
        Flags = Slot->MessageFlags;

        WriteULongRelease((ULONG volatile *)&Slot->MessageType, 0);
        KeMemoryBarrier();
        if (Flags & HV_MESSAGE_PENDING)
            HvAcknowledgeMessage();

        VmbusHandleMessage(Bus, Message, Size);
    }
}

static
VOID
VmbusProcessEvents(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    PULONG Flags = Bus->EventPage + VMBUS_MESSAGE_SINT * (256 / sizeof(ULONG));
    PVMBUS_CHANNEL Channel;
    ULONG Index;
    ULONG Bits;
    ULONG Bit;

    for (Index = 0; Index < 256 / sizeof(ULONG); Index++)
    {
        if (ReadULongAcquire((ULONG volatile *)&Flags[Index]) == 0)
            continue;

        Bits = (ULONG)InterlockedExchange((LONG volatile *)&Flags[Index], 0);
        while (Bits != 0)
        {
            BitScanForward(&Bit, Bits);
            Bits &= Bits - 1;

            if (Index == 0 && Bit == 0)
                continue;

            KeAcquireSpinLockAtDpcLevel(&Bus->ChannelLock);
            Channel = VmbusFindChannel(Bus, Index * 32 + Bit);
            KeReleaseSpinLockFromDpcLevel(&Bus->ChannelLock);

            if (Channel != NULL)
                VmbusChannelInterrupt(Channel);
        }
    }
}

static
VOID
NTAPI
VmbusDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PVMBUS_FDO_EXTENSION Bus = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Bus == NULL || !Bus->HypervisorReady)
        return;

    VmbusProcessMessages(Bus);
    VmbusProcessEvents(Bus);
}

static
BOOLEAN
NTAPI
VmbusInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    PVMBUS_FDO_EXTENSION Bus = ServiceContext;

    UNREFERENCED_PARAMETER(Interrupt);

    KeInsertQueueDpc(&Bus->Dpc, NULL, NULL);
    return TRUE;
}

static
NTSTATUS
VmbusConnect(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    VMBUS_MSG_INITIATE_CONTACT Contact;
    VMBUS_MSG_VERSION_RESPONSE Response;
    VMBUS_MSG_HEADER Request;
    PHYSICAL_ADDRESS MonitorPa;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Index;

    MonitorPa = MmGetPhysicalAddress(Bus->MonitorPages);

    for (Index = 0; Index < RTL_NUMBER_OF(VmbusVersions); Index++)
    {
        RtlZeroMemory(&Contact, sizeof(Contact));
        Contact.Header.MessageType = VMBUS_MSGTYPE_INITIATE_CONTACT;
        Contact.VersionRequested = VmbusVersions[Index];
        Contact.TargetVcpu = Bus->VpIndex;
        Contact.MonitorPage1 = (ULONG64)MonitorPa.QuadPart;
        Contact.MonitorPage2 = (ULONG64)MonitorPa.QuadPart + PAGE_SIZE;

        if (VmbusVersions[Index] >= VMBUS_VERSION_WIN10_V5)
        {
            Contact.InterruptPageOrTarget = VMBUS_MESSAGE_SINT;
            Bus->MessageConnectionId = VMBUS_MESSAGE_CONNECTION_ID_4;
        }
        else
        {
            Contact.InterruptPageOrTarget = (ULONG64)MmGetPhysicalAddress(Bus->InterruptPage).QuadPart;
            Bus->MessageConnectionId = VMBUS_MESSAGE_CONNECTION_ID;
        }

        Status = VmbusSendControl(Bus, &Contact, sizeof(Contact), VMBUS_MSGTYPE_VERSION_RESPONSE, 0,
                                  &Response, sizeof(Response));
        if (!NT_SUCCESS(Status))
            return Status;

        if (Response.VersionSupported)
        {
            Bus->Version = VmbusVersions[Index];
            if (Bus->Version >= VMBUS_VERSION_WIN10_V5)
                Bus->MessageConnectionId = Response.SelectedVersionOrConnectionId;
            break;
        }
    }

    if (Bus->Version == 0)
    {
        DPRINT1("VMBUS: host accepted no protocol version\n");
        return STATUS_NOT_SUPPORTED;
    }

    KeClearEvent(&Bus->OffersEvent);
    RtlZeroMemory(&Request, sizeof(Request));
    Request.MessageType = VMBUS_MSGTYPE_REQUEST_OFFERS;
    Status = VmbusSendControl(Bus, &Request, sizeof(Request), 0, 0, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Timeout.QuadPart = -10000LL * 10000;
    if (KeWaitForSingleObject(&Bus->OffersEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        DPRINT1("VMBUS: offers not all delivered in time\n");

    Bus->Connected = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VmbusStartDevice(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = NULL;
    PCM_PARTIAL_RESOURCE_LIST List;
    LARGE_INTEGER Due;
    NTSTATUS Status;
    ULONG Index;

    if (!HvIsPresent())
    {
        DPRINT1("VMBUS: no Hyper-V synthetic interrupt controller\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (Resources != NULL && Resources->Count != 0)
    {
        List = &Resources->List[0].PartialResourceList;
        for (Index = 0; Index < List->Count; Index++)
        {
            if (List->PartialDescriptors[Index].Type == CmResourceTypeInterrupt)
            {
                Descriptor = &List->PartialDescriptors[Index];
                break;
            }
        }
    }

    Bus->Polling = TRUE;
    if (Descriptor != NULL && (Descriptor->u.Interrupt.Affinity & 1) != 0)
    {
        Status = IoConnectInterrupt(&Bus->Interrupt,
                                    VmbusInterruptService,
                                    Bus,
                                    NULL,
                                    Descriptor->u.Interrupt.Vector,
                                    (KIRQL)Descriptor->u.Interrupt.Level,
                                    (KIRQL)Descriptor->u.Interrupt.Level,
                                    (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                    Descriptor->ShareDisposition == CmResourceShareShared,
                                    Descriptor->u.Interrupt.Affinity,
                                    FALSE);
        if (NT_SUCCESS(Status))
        {
            Bus->Vector = Descriptor->u.Interrupt.Vector;
            Bus->Polling = FALSE;
        }
        else
        {
            DPRINT1("VMBUS: IoConnectInterrupt failed with 0x%lx\n", Status);
        }
    }

    Status = HvInitialize(Bus);
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (Bus->Polling)
    {
        Due.QuadPart = -10000LL * 10;
        KeSetTimerEx(&Bus->PollTimer, Due, 10, &Bus->PollDpc);
    }

    Status = VmbusConnect(Bus);
    if (!NT_SUCCESS(Status))
        goto Failure;

    return STATUS_SUCCESS;

Failure:
    KeCancelTimer(&Bus->PollTimer);
    HvShutdown(Bus);
    if (Bus->Interrupt != NULL)
    {
        IoDisconnectInterrupt(Bus->Interrupt);
        Bus->Interrupt = NULL;
    }
    KeFlushQueuedDpcs();
    return Status;
}

static
VOID
VmbusStopDevice(
    _Inout_ PVMBUS_FDO_EXTENSION Bus)
{
    KeCancelTimer(&Bus->PollTimer);
    if (Bus->Interrupt != NULL)
    {
        IoDisconnectInterrupt(Bus->Interrupt);
        Bus->Interrupt = NULL;
    }
    KeFlushQueuedDpcs();
    Bus->Connected = FALSE;
    HvShutdown(Bus);
}

static
PDEVICE_OBJECT
VmbusCreatePdo(
    _In_ PVMBUS_FDO_EXTENSION Bus,
    _In_ PVMBUS_CHANNEL Channel)
{
    PVMBUS_PDO_EXTENSION PdoExtension;
    PDEVICE_OBJECT Pdo;
    NTSTATUS Status;

    Status = IoCreateDevice(Bus->Fdo->DriverObject,
                            sizeof(VMBUS_PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
        return NULL;

    PdoExtension = Pdo->DeviceExtension;
    RtlZeroMemory(PdoExtension, sizeof(*PdoExtension));
    PdoExtension->Common.IsFdo = FALSE;
    PdoExtension->Channel = Channel;
    PdoExtension->Reported = TRUE;

    Pdo->Flags |= DO_POWER_PAGABLE;
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return Pdo;
}

static
NTSTATUS
VmbusQueryBusRelations(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _Inout_ PIRP Irp)
{
    PDEVICE_RELATIONS Relations;
    PVMBUS_CHANNEL *Snapshot;
    PVMBUS_PDO_EXTENSION PdoExtension;
    PLIST_ENTRY Entry;
    ULONG Capacity = 0;
    ULONG Count = 0;
    ULONG Reported = 0;
    ULONG Index;
    KIRQL Irql;

    KeAcquireSpinLock(&Bus->ChannelLock, &Irql);
    for (Entry = Bus->Channels.Flink; Entry != &Bus->Channels; Entry = Entry->Flink)
        Capacity++;
    KeReleaseSpinLock(&Bus->ChannelLock, Irql);

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, (Capacity + 1) * sizeof(PVMBUS_CHANNEL), VMBUS_TAG);
    if (Snapshot == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&Bus->ChannelLock, &Irql);
    for (Entry = Bus->Channels.Flink; Entry != &Bus->Channels && Count < Capacity; Entry = Entry->Flink)
        Snapshot[Count++] = CONTAINING_RECORD(Entry, VMBUS_CHANNEL, ListEntry);
    KeReleaseSpinLock(&Bus->ChannelLock, Irql);

    Relations = ExAllocatePoolWithTag(PagedPool,
                                      FIELD_OFFSET(DEVICE_RELATIONS, Objects) + (Count + 1) * sizeof(PDEVICE_OBJECT),
                                      VMBUS_TAG);
    if (Relations == NULL)
    {
        ExFreePoolWithTag(Snapshot, VMBUS_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < Count; Index++)
    {
        PVMBUS_CHANNEL Channel = Snapshot[Index];

        if (!VmbusChannelIsSupported(Channel))
            continue;

        if (Channel->Rescinded)
        {
            if (Channel->Pdo != NULL)
            {
                PdoExtension = Channel->Pdo->DeviceExtension;
                PdoExtension->Reported = FALSE;
            }
            continue;
        }

        if (Channel->Pdo == NULL)
            Channel->Pdo = VmbusCreatePdo(Bus, Channel);

        if (Channel->Pdo == NULL)
            continue;

        ObReferenceObject(Channel->Pdo);
        Relations->Objects[Reported++] = Channel->Pdo;
    }

    Relations->Count = Reported;
    ExFreePoolWithTag(Snapshot, VMBUS_TAG);

    if (Irp->IoStatus.Information != 0)
        ExFreePool((PVOID)Irp->IoStatus.Information);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VmbusFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMBUS_FDO_EXTENSION Bus = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Bus->LowerDevice, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = VmbusStartDevice(Bus, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                Status = VmbusQueryBusRelations(Bus, Irp);
                if (!NT_SUCCESS(Status))
                {
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;

        case IRP_MN_SURPRISE_REMOVAL:
            VmbusStopDevice(Bus);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            VmbusStopDevice(Bus);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Bus->LowerDevice, Irp);
            IoDetachDevice(Bus->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Bus->LowerDevice, Irp);
}

static
NTSTATUS
VmbusAllocateString(
    _Out_ PULONG_PTR Information,
    _In_ PCWSTR Format,
    _In_ BOOLEAN MultiString,
    _In_ const GUID *Guid)
{
    WCHAR Buffer[96];
    PWCHAR Result;
    SIZE_T Length;
    NTSTATUS Status;

    Status = RtlStringCchPrintfW(Buffer, RTL_NUMBER_OF(Buffer), Format,
                                 Guid->Data1, Guid->Data2, Guid->Data3,
                                 Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
                                 Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    if (!NT_SUCCESS(Status))
        return Status;

    Length = wcslen(Buffer) + (MultiString ? 2 : 1);
    Result = ExAllocatePoolWithTag(PagedPool, Length * sizeof(WCHAR), VMBUS_TAG);
    if (Result == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Result, Length * sizeof(WCHAR));
    RtlCopyMemory(Result, Buffer, wcslen(Buffer) * sizeof(WCHAR));
    *Information = (ULONG_PTR)Result;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VmbusPdoQueryId(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PULONG_PTR Information)
{
    static const WCHAR GuidFormat[] = L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}";
    static const WCHAR IdFormat[] = L"VMBUS\\{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}";

    switch (IdType)
    {
        case BusQueryDeviceID:
            return VmbusAllocateString(Information, IdFormat, FALSE, &Channel->InterfaceType);

        case BusQueryHardwareIDs:
            return VmbusAllocateString(Information, IdFormat, TRUE, &Channel->InterfaceType);

        case BusQueryInstanceID:
            return VmbusAllocateString(Information, GuidFormat, FALSE, &Channel->InterfaceInstance);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
VmbusPdoQueryText(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ DEVICE_TEXT_TYPE TextType,
    _Out_ PULONG_PTR Information)
{
    PCWSTR Text;
    PWCHAR Result;
    SIZE_T Size;

    if (TextType != DeviceTextDescription)
        return STATUS_NOT_SUPPORTED;

    if (VmbusChannelIsSupported(Channel))
        Text = L"Microsoft Hyper-V Network Adapter";
    else
        Text = L"Microsoft Hyper-V VMBus Device";

    Size = (wcslen(Text) + 1) * sizeof(WCHAR);
    Result = ExAllocatePoolWithTag(PagedPool, Size, VMBUS_TAG);
    if (Result == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Result, Text, Size);
    *Information = (ULONG_PTR)Result;
    return STATUS_SUCCESS;
}

static
NTSTATUS
VmbusPdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMBUS_PDO_EXTENSION PdoExtension = DeviceObject->DeviceExtension;
    PVMBUS_CHANNEL Channel = PdoExtension->Channel;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_CAPABILITIES Capabilities;
    PDEVICE_RELATIONS Relations;
    PPNP_BUS_INFORMATION BusInformation;
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            if (!PdoExtension->Reported)
            {
                Channel->Pdo = NULL;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                IoDeleteDevice(DeviceObject);
                return Status;
            }
            break;

        case IRP_MN_QUERY_ID:
            Status = VmbusPdoQueryId(Channel, Stack->Parameters.QueryId.IdType, &Irp->IoStatus.Information);
            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            Status = VmbusPdoQueryText(Channel, Stack->Parameters.QueryDeviceText.DeviceTextType,
                                       &Irp->IoStatus.Information);
            if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;
            Capabilities->UniqueID = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->SurpriseRemovalOK = TRUE;
            Capabilities->Address = Channel->ChildRelId;
            Capabilities->UINumber = Channel->ChildRelId;
            Capabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
            Capabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
            Capabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
            Capabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
            Capabilities->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
            Capabilities->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), VMBUS_TAG);
                if (Relations == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                ObReferenceObject(DeviceObject);
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            BusInformation = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), VMBUS_TAG);
            if (BusInformation == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            BusInformation->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
            BusInformation->LegacyBusType = PNPBus;
            BusInformation->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
VmbusDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMBUS_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;

    if (Common->IsFdo)
        return VmbusFdoPnp(DeviceObject, Irp);

    return VmbusPdoPnp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
VmbusDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMBUS_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);

    if (Common->IsFdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(((PVMBUS_FDO_EXTENSION)Common)->LowerDevice, Irp);
    }

    Status = Irp->IoStatus.Status;
    if (Stack->MinorFunction == IRP_MN_SET_POWER || Stack->MinorFunction == IRP_MN_QUERY_POWER)
        Status = STATUS_SUCCESS;

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
VmbusDispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PVMBUS_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Common->IsFdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(((PVMBUS_FDO_EXTENSION)Common)->LowerDevice, Irp);
    }

    Status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
VmbusAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PVMBUS_FDO_EXTENSION Bus;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(VMBUS_FDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;

    Bus = Fdo->DeviceExtension;
    RtlZeroMemory(Bus, sizeof(*Bus));
    Bus->Common.IsFdo = TRUE;
    Bus->Fdo = Fdo;
    Bus->Pdo = PhysicalDeviceObject;
    Bus->NextGpadl = 0xE1E10;
    ExInitializeFastMutex(&Bus->ControlLock);
    KeInitializeEvent(&Bus->ResponseEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Bus->OffersEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&Bus->ChannelLock);
    InitializeListHead(&Bus->Channels);
    KeInitializeDpc(&Bus->Dpc, VmbusDpcRoutine, Bus);
    KeSetTargetProcessorDpc(&Bus->Dpc, 0);
    KeInitializeDpc(&Bus->PollDpc, VmbusDpcRoutine, Bus);
    KeSetTargetProcessorDpc(&Bus->PollDpc, 0);
    KeInitializeTimerEx(&Bus->PollTimer, NotificationTimer);

    Bus->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (Bus->LowerDevice == NULL)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
VmbusUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    VmbusDriverObject = DriverObject;
    DriverObject->DriverExtension->AddDevice = VmbusAddDevice;
    DriverObject->DriverUnload = VmbusUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = VmbusDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = VmbusDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = VmbusDispatchSystemControl;
    return STATUS_SUCCESS;
}
