/*
 * PROJECT:     LiberNT Bluetooth USB transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HCI over USB: configuration, event and ACL reception, command and ACL transmission
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthusb.h"

#define NDEBUG
#include <debug.h>

typedef struct _BTHUSB_WRITE
{
    PBTHUSB_DEVICE Dev;
    PIRP Irp;
    WDFREQUEST Request;
    PULONG Written;
    ULONG Length;
    PKEVENT Done;
    NTSTATUS Status;
    URB Urb;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHUSB_WRITE, *PBTHUSB_WRITE;

static IO_COMPLETION_ROUTINE BthUsbReadComplete;
static IO_COMPLETION_ROUTINE BthUsbWriteComplete;

static
NTSTATUS
BthUsbSubmitUrbSync(
    _In_ PBTHUSB_DEVICE Dev,
    _Inout_ PURB Urb)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB, Dev->LowerDeviceObject,
                                        NULL, 0, NULL, 0, TRUE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;
    Status = IoCallDriver(Dev->LowerDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
BthUsbGetDescriptor(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ UCHAR Type,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Returned)
{
    NTSTATUS Status;
    URB Urb;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest(&Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 Type, 0, 0, Buffer, NULL, Length, NULL);
    Status = BthUsbSubmitUrbSync(Dev, &Urb);
    *Returned = Urb.UrbControlDescriptorRequest.TransferBufferLength;
    return Status;
}

NTSTATUS
BthUsbConfigure(
    _In_ PBTHUSB_DEVICE Dev)
{
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR Header;
    PUSBD_INTERFACE_LIST_ENTRY Interfaces;
    PUSB_CONFIGURATION_DESCRIPTOR Config;
    ULONG Returned, Index, Pipe, Count;
    NTSTATUS Status;
    PURB Urb;

    Status = BthUsbGetDescriptor(Dev, USB_DEVICE_DESCRIPTOR_TYPE, &DeviceDescriptor, sizeof(DeviceDescriptor), &Returned);
    if (!NT_SUCCESS(Status) || Returned < sizeof(DeviceDescriptor))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    Dev->VendorId = DeviceDescriptor.idVendor;
    Dev->ProductId = DeviceDescriptor.idProduct;

    Status = BthUsbGetDescriptor(Dev, USB_CONFIGURATION_DESCRIPTOR_TYPE, &Header, sizeof(Header), &Returned);
    if (!NT_SUCCESS(Status) || Returned < sizeof(Header) || Header.wTotalLength < sizeof(Header) ||
        !Header.bNumInterfaces)
    {
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }
    Config = ExAllocatePoolWithTag(NonPagedPool, Header.wTotalLength, BTHUSB_TAG);
    if (!Config)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = BthUsbGetDescriptor(Dev, USB_CONFIGURATION_DESCRIPTOR_TYPE, Config, Header.wTotalLength, &Returned);
    if (!NT_SUCCESS(Status) || Returned != Header.wTotalLength)
    {
        ExFreePoolWithTag(Config, BTHUSB_TAG);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }
    Dev->ConfigurationDescriptor = Config;

    Count = Config->bNumInterfaces;
    Interfaces = ExAllocatePoolZero(NonPagedPool, (Count + 1) * sizeof(*Interfaces), BTHUSB_TAG);
    if (!Interfaces)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < Count; Index++)
    {
        Interfaces[Index].InterfaceDescriptor =
            USBD_ParseConfigurationDescriptorEx(Config, Config, (LONG)Index, 0, -1, -1, -1);
        if (!Interfaces[Index].InterfaceDescriptor)
        {
            ExFreePoolWithTag(Interfaces, BTHUSB_TAG);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    Urb = USBD_CreateConfigurationRequestEx(Config, Interfaces);
    if (!Urb)
    {
        ExFreePoolWithTag(Interfaces, BTHUSB_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (Index = 0; Index < Count; Index++)
    {
        PUSBD_INTERFACE_INFORMATION Info = Interfaces[Index].Interface;

        for (Pipe = 0; Pipe < Info->NumberOfPipes; Pipe++)
            Info->Pipes[Pipe].MaximumTransferSize = BTHUSB_ACL_READ_SIZE;
    }

    Status = BthUsbSubmitUrbSync(Dev, Urb);
    if (NT_SUCCESS(Status))
    {
        Dev->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;
        for (Index = 0; Index < Count; Index++)
        {
            PUSBD_INTERFACE_INFORMATION Info = Interfaces[Index].Interface;

            for (Pipe = 0; Pipe < Info->NumberOfPipes; Pipe++)
            {
                PUSBD_PIPE_INFORMATION Endpoint = &Info->Pipes[Pipe];
                BOOLEAN In = USB_ENDPOINT_DIRECTION_IN(Endpoint->EndpointAddress);

                if (Endpoint->PipeType == UsbdPipeTypeInterrupt && In && !Dev->EventPipe)
                {
                    Dev->EventPipe = Endpoint->PipeHandle;
                    Dev->EventPacketSize = Endpoint->MaximumPacketSize;
                }
                else if (Endpoint->PipeType == UsbdPipeTypeBulk && In && !Dev->AclInPipe)
                {
                    Dev->AclInPipe = Endpoint->PipeHandle;
                }
                else if (Endpoint->PipeType == UsbdPipeTypeBulk && !In && !Dev->AclOutPipe)
                {
                    Dev->AclOutPipe = Endpoint->PipeHandle;
                }
            }
        }
    }
    ExFreePool(Urb);
    ExFreePoolWithTag(Interfaces, BTHUSB_TAG);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHUSB: select configuration failed 0x%08lx\n", Status);
        return Status;
    }
    if (!Dev->EventPipe || !Dev->AclInPipe || !Dev->AclOutPipe || !Dev->EventPacketSize)
    {
        DPRINT1("BTHUSB: %04x:%04x has no HCI event and ACL endpoints\n", Dev->VendorId, Dev->ProductId);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    DPRINT1("BTHUSB: %04x:%04x configured, event packet %lu bytes\n",
            Dev->VendorId, Dev->ProductId, Dev->EventPacketSize);
    return STATUS_SUCCESS;
}

VOID
BthUsbUnconfigure(
    _In_ PBTHUSB_DEVICE Dev)
{
    ULONG Kind, Index;

    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
        {
            PBTHUSB_READ Read = &Dev->Reads[Kind][Index];

            if (Read->Irp)
                IoFreeIrp(Read->Irp);
            if (Read->Buffer)
                ExFreePoolWithTag(Read->Buffer, BTHUSB_TAG);
            Read->Irp = NULL;
            Read->Buffer = NULL;
        }
    }
    if (Dev->ConfigurationDescriptor)
        ExFreePoolWithTag(Dev->ConfigurationDescriptor, BTHUSB_TAG);
    Dev->ConfigurationDescriptor = NULL;
    Dev->ConfigurationHandle = NULL;
    Dev->EventPipe = Dev->AclInPipe = Dev->AclOutPipe = NULL;
}

static
VOID
BthUsbIoDone(
    _In_ PBTHUSB_DEVICE Dev,
    _Inout_ volatile LONG *Counter)
{
    if (InterlockedDecrement(Counter) == 0 && Dev->Stopping)
        KeSetEvent(&Dev->IoDrained, IO_NO_INCREMENT, FALSE);
}

static
VOID
BthUsbQueuePacket(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHUSB_RX_STREAM Stream = &Dev->Rx[Kind];
    PBTHUSB_PACKET Packet;

    if (Kind == BthUsbRxAcl && !Dev->Ready)
        return;
    if (Stream->Count >= BTHUSB_MAX_QUEUED_PACKETS)
    {
        DPRINT1("BTHUSB: %s queue full, packet dropped\n", Kind == BthUsbRxEvent ? "event" : "ACL");
        return;
    }
    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BTHUSB_PACKET, Data) + Length, BTHUSB_TAG);
    if (!Packet)
        return;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);
    InsertTailList(&Stream->Packets, &Packet->Link);
    Stream->Count++;
}

static
VOID
BthUsbReceive(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBTHUSB_RX_STREAM Stream = &Dev->Rx[Kind];
    ULONG HeaderLength = Kind == BthUsbRxEvent ? 2 : 4;
    ULONG Received = Length;
    BOOLEAN Queued = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    while (Length)
    {
        ULONG Total, Take;

        if (Stream->Length < HeaderLength)
        {
            Take = min(Length, HeaderLength - Stream->Length);
            RtlCopyMemory(Stream->Data + Stream->Length, Data, Take);
            Stream->Length += Take;
            Data += Take;
            Length -= Take;
            if (Stream->Length < HeaderLength)
                break;
            Total = Kind == BthUsbRxEvent ? 2 + (ULONG)Stream->Data[1] :
                                            4 + (ULONG)(Stream->Data[2] | (Stream->Data[3] << 8));
            Stream->Discard = Total > sizeof(Stream->Data);
            if (Stream->Discard)
                DPRINT1("BTHUSB: oversized ACL packet of %lu bytes dropped\n", Total);
        }
        Total = Kind == BthUsbRxEvent ? 2 + (ULONG)Stream->Data[1] :
                                        4 + (ULONG)(Stream->Data[2] | (Stream->Data[3] << 8));
        Take = min(Length, Total - Stream->Length);
        if (!Stream->Discard)
            RtlCopyMemory(Stream->Data + Stream->Length, Data, Take);
        Stream->Length += Take;
        Data += Take;
        Length -= Take;
        if (Stream->Length == Total)
        {
            if (!Stream->Discard)
            {
                BthUsbQueuePacket(Dev, Kind, Stream->Data, Total);
                Queued = TRUE;
            }
            Stream->Length = 0;
            Stream->Discard = FALSE;
        }
    }
    if (Kind == BthUsbRxEvent && Received < Dev->EventPacketSize && Stream->Length)
    {
        DPRINT1("BTHUSB: %04x:%04x dropped %lu bytes of an event ended by a short packet\n",
                Dev->VendorId, Dev->ProductId, Stream->Length);
        Stream->Length = 0;
        Stream->Discard = FALSE;
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);

    if (Queued)
    {
        if (Kind == BthUsbRxEvent)
            KeSetEvent(&Dev->EventArrived, IO_NO_INCREMENT, FALSE);
        BthUsbDeliverPackets(Dev, Kind);
    }
}

static
VOID
BthUsbSubmitRead(
    _In_ PBTHUSB_READ Read)
{
    PBTHUSB_DEVICE Dev = Read->Dev;
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Read->Irp, STATUS_SUCCESS);
    RtlZeroMemory(&Read->Urb, sizeof(Read->Urb));
    UsbBuildInterruptOrBulkTransferRequest(&Read->Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Read->Kind == BthUsbRxEvent ? Dev->EventPipe : Dev->AclInPipe,
                                           Read->Buffer, NULL, Read->Size,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK, NULL);
    IoStack = IoGetNextIrpStackLocation(Read->Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = &Read->Urb;
    IoSetCompletionRoutine(Read->Irp, BthUsbReadComplete, Read, TRUE, TRUE, TRUE);
    IoCallDriver(Dev->LowerDeviceObject, Read->Irp);
}

static
NTSTATUS
NTAPI
BthUsbReadComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PBTHUSB_READ Read = Context;
    PBTHUSB_DEVICE Dev = Read->Dev;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (NT_SUCCESS(Status))
    {
        BthUsbReceive(Dev, Read->Kind, Read->Buffer, Read->Urb.UrbBulkOrInterruptTransfer.TransferBufferLength);
        Dev->ReadFailures = 0;
        if (!Dev->Stopping)
        {
            BthUsbSubmitRead(Read);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
    }
    else if (!Dev->Stopping && Status != STATUS_CANCELLED && Status != STATUS_DEVICE_NOT_CONNECTED &&
             Status != STATUS_NO_SUCH_DEVICE)
    {
        DPRINT1("BTHUSB: %s read failed 0x%08lx (USBD 0x%08lx)\n", Read->Kind == BthUsbRxEvent ? "event" : "ACL",
                Status, Read->Urb.UrbHeader.Status);
        if (InterlockedIncrement(&Dev->ReadFailures) <= BTHUSB_MAX_READ_FAILURES)
        {
            Read->Failed = TRUE;
            WdfWorkItemEnqueue(Dev->RecoverItem);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
    }
    BthUsbIoDone(Dev, &Dev->ReadsOutstanding);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
BthUsbStartReads(
    _In_ PBTHUSB_DEVICE Dev)
{
    ULONG Kind, Index;

    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
        {
            PBTHUSB_READ Read = &Dev->Reads[Kind][Index];

            Read->Dev = Dev;
            Read->Kind = (BTHUSB_RX_KIND)Kind;
            Read->Size = Kind == BthUsbRxEvent ? Dev->EventPacketSize : BTHUSB_ACL_READ_SIZE;
            if (!Read->Buffer)
                Read->Buffer = ExAllocatePoolWithTag(NonPagedPool, Read->Size, BTHUSB_TAG);
            if (!Read->Irp)
                Read->Irp = IoAllocateIrp(Dev->LowerDeviceObject->StackSize, FALSE);
            if (!Read->Buffer || !Read->Irp)
                return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Dev->Stopping = FALSE;
    Dev->ReadFailures = 0;
    KeClearEvent(&Dev->IoDrained);
    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
        {
            InterlockedIncrement(&Dev->ReadsOutstanding);
            BthUsbSubmitRead(&Dev->Reads[Kind][Index]);
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
BthUsbResetPipe(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ USBD_PIPE_HANDLE Pipe)
{
    URB Urb;

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
    Urb.UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    Urb.UrbPipeRequest.PipeHandle = Pipe;
    return BthUsbSubmitUrbSync(Dev, &Urb);
}

VOID
NTAPI
BthUsbRecoverReads(
    _In_ WDFWORKITEM WorkItem)
{
    PBTHUSB_DEVICE Dev = BthUsbGetDevice(WdfWorkItemGetParentObject(WorkItem));
    ULONG Kind, Index;

    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
        {
            PBTHUSB_READ Read = &Dev->Reads[Kind][Index];

            if (!Read->Failed)
                continue;
            Read->Failed = FALSE;
            if (Dev->Stopping)
            {
                BthUsbIoDone(Dev, &Dev->ReadsOutstanding);
                continue;
            }
            BthUsbResetPipe(Dev, Kind == BthUsbRxEvent ? Dev->EventPipe : Dev->AclInPipe);
            BthUsbSubmitRead(Read);
        }
    }
}

VOID
BthUsbStopIo(
    _In_ PBTHUSB_DEVICE Dev)
{
    ULONG Kind, Index;

    Dev->Stopping = TRUE;
    KeMemoryBarrier();
    WdfWorkItemFlush(Dev->RecoverItem);
    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
        {
            if (Dev->Reads[Kind][Index].Irp)
                IoCancelIrp(Dev->Reads[Kind][Index].Irp);
        }
    }
    while (Dev->ReadsOutstanding || Dev->WritesOutstanding)
    {
        LARGE_INTEGER Timeout;

        Timeout.QuadPart = -10 * 1000 * 100;
        KeWaitForSingleObject(&Dev->IoDrained, Executive, KernelMode, FALSE, &Timeout);
        KeClearEvent(&Dev->IoDrained);
        for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
        {
            for (Index = 0; Index < BTHUSB_READS_PER_PIPE; Index++)
            {
                if (Dev->Reads[Kind][Index].Irp)
                    IoCancelIrp(Dev->Reads[Kind][Index].Irp);
            }
        }
    }
    if (Dev->EventPipe)
        BthUsbResetPipe(Dev, Dev->EventPipe);
    if (Dev->AclInPipe)
        BthUsbResetPipe(Dev, Dev->AclInPipe);
    Dev->Rx[BthUsbRxEvent].Length = 0;
    Dev->Rx[BthUsbRxAcl].Length = 0;
}

static
NTSTATUS
NTAPI
BthUsbWriteComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PBTHUSB_WRITE Write = Context;
    PBTHUSB_DEVICE Dev = Write->Dev;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!NT_SUCCESS(Status))
        DPRINT1("BTHUSB: write of %lu bytes failed 0x%08lx (USBD 0x%08lx)\n",
                Write->Length, Status, Write->Urb.UrbHeader.Status);
    IoFreeIrp(Irp);
    if (Write->Request)
    {
        if (NT_SUCCESS(Status) && Write->Written)
        {
            *Write->Written = Write->Length;
            WdfRequestCompleteWithInformation(Write->Request, Status, sizeof(ULONG));
        }
        else
        {
            WdfRequestComplete(Write->Request, Status);
        }
        ExFreePoolWithTag(Write, BTHUSB_TAG);
    }
    else
    {
        Write->Status = Status;
        KeSetEvent(Write->Done, IO_NO_INCREMENT, FALSE);
    }
    BthUsbIoDone(Dev, &Dev->WritesOutstanding);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
BthUsbWrite(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHX_HCI_PACKET_TYPE Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_opt_ WDFREQUEST Request)
{
    PIO_STACK_LOCATION IoStack;
    PBTHUSB_WRITE Write;
    KEVENT Done;
    NTSTATUS Status;
    WDFMEMORY Output;

    if (Dev->Stopping)
        return STATUS_DEVICE_NOT_READY;
    Write = ExAllocatePoolZero(NonPagedPool, FIELD_OFFSET(BTHUSB_WRITE, Data) + Length, BTHUSB_TAG);
    if (!Write)
        return STATUS_INSUFFICIENT_RESOURCES;
    Write->Irp = IoAllocateIrp(Dev->LowerDeviceObject->StackSize, FALSE);
    if (!Write->Irp)
    {
        ExFreePoolWithTag(Write, BTHUSB_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Write->Dev = Dev;
    Write->Request = Request;
    Write->Length = Length;
    RtlCopyMemory(Write->Data, Data, Length);
    if (Request && NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Output)))
    {
        size_t Size;
        PVOID Buffer = WdfMemoryGetBuffer(Output, &Size);

        if (Size >= sizeof(ULONG))
            Write->Written = Buffer;
    }
    if (!Request)
    {
        KeInitializeEvent(&Done, NotificationEvent, FALSE);
        Write->Done = &Done;
    }

    if (Type == HciPacketCommand)
    {
        Write->Urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
        Write->Urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_DEVICE;
        Write->Urb.UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
        Write->Urb.UrbControlVendorClassRequest.TransferBuffer = Write->Data;
        Write->Urb.UrbControlVendorClassRequest.TransferBufferLength = Length;
    }
    else
    {
        UsbBuildInterruptOrBulkTransferRequest(&Write->Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                               Dev->AclOutPipe, Write->Data, NULL, Length,
                                               USBD_TRANSFER_DIRECTION_OUT, NULL);
    }

    IoStack = IoGetNextIrpStackLocation(Write->Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = &Write->Urb;
    IoSetCompletionRoutine(Write->Irp, BthUsbWriteComplete, Write, TRUE, TRUE, TRUE);
    InterlockedIncrement(&Dev->WritesOutstanding);
    IoCallDriver(Dev->LowerDeviceObject, Write->Irp);
    if (Request)
        return STATUS_PENDING;

    KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
    Status = Write->Status;
    ExFreePoolWithTag(Write, BTHUSB_TAG);
    return Status;
}

PBTHUSB_PACKET
BthUsbTakePacket(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind)
{
    PBTHUSB_RX_STREAM Stream = &Dev->Rx[Kind];
    PBTHUSB_PACKET Packet = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    if (!IsListEmpty(&Stream->Packets))
    {
        Packet = CONTAINING_RECORD(RemoveHeadList(&Stream->Packets), BTHUSB_PACKET, Link);
        Stream->Count--;
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    return Packet;
}

VOID
BthUsbFlushPackets(
    _In_ PBTHUSB_DEVICE Dev)
{
    ULONG Kind;
    PBTHUSB_PACKET Packet;

    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
    {
        while ((Packet = BthUsbTakePacket(Dev, (BTHUSB_RX_KIND)Kind)) != NULL)
            ExFreePoolWithTag(Packet, BTHUSB_TAG);
    }
}

VOID
BthUsbDeliverPackets(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind)
{
    PBTHUSB_RX_STREAM Stream = &Dev->Rx[Kind];
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    if (Stream->Delivering)
    {
        KeReleaseSpinLock(&Dev->Lock, OldIrql);
        return;
    }
    Stream->Delivering = TRUE;
    for (;;)
    {
        PBTHX_HCI_READ_WRITE_CONTEXT Context;
        PBTHUSB_PACKET Packet;
        WDFREQUEST Request;
        WDFMEMORY Memory;
        size_t Size;

        if (!Dev->Ready || !Stream->Queue || IsListEmpty(&Stream->Packets) ||
            !NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Stream->Queue, &Request)))
        {
            Stream->Delivering = FALSE;
            KeReleaseSpinLock(&Dev->Lock, OldIrql);
            return;
        }
        Packet = CONTAINING_RECORD(RemoveHeadList(&Stream->Packets), BTHUSB_PACKET, Link);
        Stream->Count--;
        KeReleaseSpinLock(&Dev->Lock, OldIrql);

        if (NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Memory)))
        {
            Context = WdfMemoryGetBuffer(Memory, &Size);
            if (Size >= FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length)
            {
                Context->DataLen = Packet->Length;
                Context->Type = Kind == BthUsbRxEvent ? HciPacketEvent : HciPacketAclData;
                RtlCopyMemory(Context->Data, Packet->Data, Packet->Length);
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                                  FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length);
            }
            else
            {
                WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
            }
        }
        else
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        }
        ExFreePoolWithTag(Packet, BTHUSB_TAG);
        KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    }
}
