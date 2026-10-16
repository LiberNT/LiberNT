/*
 * PROJECT:     LiberNT Bluetooth HID miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID over Bluetooth BR/EDR (HIDP) miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "hidbth.h"

#define NDEBUG
#include <debug.h>

typedef struct _HIDBTH_OPERATION
{
    PHIDBTH_DEVICE_EXTENSION Extension;
    HIDBTH_CHANNEL Channel;
    PIRP Irp;
    BRB Brb;
} HIDBTH_OPERATION, *PHIDBTH_OPERATION;

static IO_COMPLETION_ROUTINE HidBthReadComplete;
static IO_COMPLETION_ROUTINE HidBthOpenComplete;
static IO_COMPLETION_ROUTINE HidBthOperationComplete;
static VOID HidBthServerIndication(_In_ PVOID Context, _In_ INDICATION_CODE Indication, _In_ PINDICATION_PARAMETERS Parameters);
static VOID HidBthChannelIndication(_In_ PVOID Context, _In_ INDICATION_CODE Indication, _In_ PINDICATION_PARAMETERS Parameters);

static
PHIDBTH_DEVICE_EXTENSION
HidBthGetExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
USHORT
HidBthPsm(
    _In_ HIDBTH_CHANNEL Channel)
{
    return Channel == HidBthControl ? HIDBTH_PSM_CONTROL : HIDBTH_PSM_INTERRUPT;
}

static
VOID
HidBthIoDone(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    if (InterlockedDecrement(&Extension->IoOutstanding) == 0)
        KeSetEvent(&Extension->IoDrained, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
HidBthCallLower(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, Extension->LowerDevice, NULL, 0, NULL, 0, TRUE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Argument1;
    IoStack->Parameters.Others.Argument2 = Argument2;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
HidBthSubmitSync(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _Inout_ PBRB Brb)
{
    return HidBthCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, Brb, NULL);
}

static
VOID
HidBthSubmitAsync(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ PIRP Irp,
    _Inout_ PBRB Brb,
    _In_ PIO_COMPLETION_ROUTINE Completion,
    _In_ PVOID Context)
{
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Irp, STATUS_SUCCESS);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_BTH_SUBMIT_BRB;
    IoStack->Parameters.Others.Argument1 = Brb;
    IoSetCompletionRoutine(Irp, Completion, Context, TRUE, TRUE, TRUE);
    InterlockedIncrement(&Extension->IoOutstanding);
    IoCallDriver(Extension->LowerDevice, Irp);
}

static
PHIDBTH_OPERATION
HidBthAllocateOperation(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ HIDBTH_CHANNEL Channel,
    _In_ BRB_TYPE Type)
{
    PHIDBTH_OPERATION Operation = ExAllocatePoolZero(NonPagedPool, sizeof(*Operation), HIDBTH_TAG);

    if (!Operation)
        return NULL;
    Operation->Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
    if (!Operation->Irp)
    {
        ExFreePoolWithTag(Operation, HIDBTH_TAG);
        return NULL;
    }
    Operation->Extension = Extension;
    Operation->Channel = Channel;
    Extension->Profile.BthInitializeBrb(&Operation->Brb, Type);
    return Operation;
}

static
VOID
HidBthFreeOperation(
    _In_ PHIDBTH_OPERATION Operation)
{
    IoFreeIrp(Operation->Irp);
    ExFreePoolWithTag(Operation, HIDBTH_TAG);
}

static
NTSTATUS
NTAPI
HidBthOperationComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDBTH_OPERATION Operation = Context;
    PHIDBTH_DEVICE_EXTENSION Extension = Operation->Extension;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    HidBthFreeOperation(Operation);
    HidBthIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidBthCloseHandle(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ L2CAP_CHANNEL_HANDLE Handle)
{
    PHIDBTH_OPERATION Operation = HidBthAllocateOperation(Extension, HidBthControl, BRB_L2CA_CLOSE_CHANNEL);

    if (!Operation)
        return;
    Operation->Brb.BrbL2caCloseChannel.BtAddress = Extension->Address;
    Operation->Brb.BrbL2caCloseChannel.ChannelHandle = Handle;
    HidBthSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidBthOperationComplete, Operation);
}

static
VOID
HidBthPrepareOpen(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _Inout_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    Open->BtAddress = Extension->Address;
    Open->ChannelFlags = CF_ROLE_EITHER | CF_LINK_AUTHENTICATED | CF_LINK_ENCRYPTED;
    Open->CallbackFlags = CALLBACK_DISCONNECT;
    Open->Callback = HidBthChannelIndication;
    Open->CallbackContext = Extension;
    Open->ReferenceObject = Extension->DeviceObject;
    Open->IncomingQueueDepth = HIDBTH_QUEUED_REPORTS;
}

static
VOID
HidBthReportControl(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    KIRQL OldIrql;

    if (!Length)
        return;
    if ((Data[0] & HIDP_TYPE_MASK) == HIDP_CONTROL && (Data[0] & 0x0F) == HIDP_CONTROL_UNPLUG)
    {
        DPRINT1("HIDBTH: %012I64x sent virtual cable unplug\n", Extension->Address);
        return;
    }
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (Extension->ControlWaiting)
    {
        if ((Data[0] & HIDP_TYPE_MASK) == HIDP_HANDSHAKE)
        {
            Extension->ControlResult = Data[0] & 0x0F;
            Extension->ControlLength = 0;
            Extension->ControlWaiting = FALSE;
            KeSetEvent(&Extension->ControlDone, IO_NO_INCREMENT, FALSE);
        }
        else if ((Data[0] & HIDP_TYPE_MASK) == HIDP_DATA)
        {
            ULONG Copy = min(Length - 1, Extension->ControlLength);

            if (Extension->ControlData && Copy)
                RtlCopyMemory(Extension->ControlData, Data + 1, Copy);
            Extension->ControlLength = Copy;
            Extension->ControlResult = 0;
            Extension->ControlWaiting = FALSE;
            KeSetEvent(&Extension->ControlDone, IO_NO_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static
VOID
NTAPI
HidBthCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTH_DEVICE_EXTENSION Extension = HidBthGetExtension(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
HidBthRemovePendingRead(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    while (!IsListEmpty(&Extension->PendingReads))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Extension->PendingReads), IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
ULONG
HidBthCopyReport(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    Length = min(Length, IoStack->Parameters.DeviceIoControl.OutputBufferLength);
    RtlCopyMemory(Irp->UserBuffer, Report, Length);
    return Length;
}

static
VOID
HidBthDeliverReport(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    PIRP Irp;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Irp = HidBthRemovePendingRead(Extension);
    if (!Irp)
    {
        PUCHAR Slot;

        if (Extension->ReportQueueCount == HIDBTH_QUEUED_REPORTS)
        {
            Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDBTH_QUEUED_REPORTS;
            Extension->ReportQueueCount--;
        }
        Slot = Extension->ReportQueue + ((Extension->ReportQueueHead + Extension->ReportQueueCount) %
                                         HIDBTH_QUEUED_REPORTS) * Extension->ReportSlotLength;
        Slot[0] = (UCHAR)Length;
        Slot[1] = (UCHAR)(Length >> 8);
        RtlCopyMemory(Slot + sizeof(USHORT), Report, Length);
        Extension->ReportQueueCount++;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (Irp)
    {
        Irp->IoStatus.Information = HidBthCopyReport(Irp, Report, Length);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
HidBthFailPendingReads(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ NTSTATUS Status)
{
    KIRQL OldIrql;
    PIRP Irp;

    for (;;)
    {
        KeAcquireSpinLock(&Extension->Lock, &OldIrql);
        Irp = HidBthRemovePendingRead(Extension);
        Extension->ReportQueueCount = 0;
        Extension->ReportQueueHead = 0;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        if (!Irp)
            break;
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
BOOLEAN
HidBthPostRead(
    _In_ PHIDBTH_TRANSFER Transfer)
{
    PHIDBTH_DEVICE_EXTENSION Extension = Transfer->Extension;
    struct _BRB_L2CA_ACL_TRANSFER *Brb = &Transfer->Brb.BrbL2caAclTransfer;
    L2CAP_CHANNEL_HANDLE Handle;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Handle = Extension->Started ? Extension->Channels[Transfer->Channel] : NULL;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Handle)
        return FALSE;
    Extension->Profile.BthReuseBrb(&Transfer->Brb, BRB_L2CA_ACL_TRANSFER);
    Brb->BtAddress = Extension->Address;
    Brb->ChannelHandle = Handle;
    Brb->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    Brb->BufferSize = sizeof(Transfer->Buffer);
    Brb->Buffer = Transfer->Buffer;
    HidBthSubmitAsync(Extension, Transfer->Irp, &Transfer->Brb, HidBthReadComplete, Transfer);
    return TRUE;
}

static
NTSTATUS
NTAPI
HidBthReadComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDBTH_TRANSFER Transfer = Context;
    PHIDBTH_DEVICE_EXTENSION Extension = Transfer->Extension;
    ULONG Length = Transfer->Brb.BrbL2caAclTransfer.BufferSize;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (NT_SUCCESS(Irp->IoStatus.Status) && Length)
    {
        if (Transfer->Channel == HidBthControl)
            HidBthReportControl(Extension, Transfer->Buffer, Length);
        else if (Transfer->Buffer[0] == (HIDP_DATA | HIDP_REPORT_INPUT) && Length > 1)
            HidBthDeliverReport(Extension, Transfer->Buffer + 1, Length - 1);
    }
    if (!NT_SUCCESS(Irp->IoStatus.Status) || !HidBthPostRead(Transfer))
        InterlockedExchange(&Transfer->Active, 0);
    HidBthIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidBthStartRead(
    _In_ PHIDBTH_TRANSFER Transfer)
{
    if (InterlockedCompareExchange(&Transfer->Active, 1, 0) == 0 && !HidBthPostRead(Transfer))
        InterlockedExchange(&Transfer->Active, 0);
}

static
VOID
HidBthStartReads(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    ULONG Index;

    DPRINT1("HIDBTH: %012I64x connected\n", Extension->Address);
    HidBthStartRead(&Extension->ControlRead);
    for (Index = 0; Index < HIDBTH_INTERRUPT_READS; Index++)
        HidBthStartRead(&Extension->InterruptReads[Index]);
}

static
VOID
HidBthOpenChannel(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ HIDBTH_CHANNEL Channel)
{
    PHIDBTH_OPERATION Operation;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started || Extension->Channels[Channel] || Extension->Opening[Channel])
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Extension->Opening[Channel] = TRUE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    Operation = HidBthAllocateOperation(Extension, Channel, BRB_L2CA_OPEN_CHANNEL);
    if (!Operation)
    {
        Extension->Opening[Channel] = FALSE;
        return;
    }
    HidBthPrepareOpen(Extension, &Operation->Brb.BrbL2caOpenChannel);
    Operation->Brb.BrbL2caOpenChannel.Psm = HidBthPsm(Channel);
    HidBthSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidBthOpenComplete, Operation);
}

static
NTSTATUS
NTAPI
HidBthOpenComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDBTH_OPERATION Operation = Context;
    PHIDBTH_DEVICE_EXTENSION Extension = Operation->Extension;
    struct _BRB_L2CA_OPEN_CHANNEL *Open = &Operation->Brb.BrbL2caOpenChannel;
    HIDBTH_CHANNEL Channel = Operation->Channel;
    L2CAP_CHANNEL_HANDLE Stale = NULL;
    BOOLEAN Ready = FALSE, OpenInterrupt = FALSE;
    NTSTATUS Status = Irp->IoStatus.Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->Opening[Channel] = FALSE;
    if (NT_SUCCESS(Status) && Open->ChannelHandle)
    {
        if (!Extension->Started || Extension->Channels[Channel])
        {
            Stale = Open->ChannelHandle;
        }
        else
        {
            Extension->Channels[Channel] = Open->ChannelHandle;
            Ready = Extension->Channels[HidBthControl] && Extension->Channels[HidBthInterrupt];
            OpenInterrupt = Channel == HidBthControl && !Extension->Channels[HidBthInterrupt] &&
                            Open->Hdr.Type == BRB_L2CA_OPEN_CHANNEL;
        }
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    if (!NT_SUCCESS(Status))
        DPRINT1("HIDBTH: PSM 0x%04x to %012I64x not opened 0x%08lx\n", HidBthPsm(Channel), Extension->Address, Status);
    HidBthFreeOperation(Operation);
    if (Stale)
        HidBthCloseHandle(Extension, Stale);
    if (OpenInterrupt)
        HidBthOpenChannel(Extension, HidBthInterrupt);
    if (Ready)
        HidBthStartReads(Extension);
    HidBthIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidBthServerIndication(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PHIDBTH_DEVICE_EXTENSION Extension = Context;
    struct _BRB_L2CA_OPEN_CHANNEL *Open;
    PHIDBTH_OPERATION Operation;
    HIDBTH_CHANNEL Channel;
    BOOLEAN Accept;
    KIRQL OldIrql;
    USHORT Psm;

    if (Indication != IndicationRemoteConnect || Parameters->BtAddress != Extension->Address)
        return;
    Psm = Parameters->Parameters.Connect.Request.PSM;
    if (Psm != HIDBTH_PSM_CONTROL && Psm != HIDBTH_PSM_INTERRUPT)
        return;
    Channel = Psm == HIDBTH_PSM_CONTROL ? HidBthControl : HidBthInterrupt;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Accept = Extension->Started && (Psm == HIDBTH_PSM_CONTROL || Psm == HIDBTH_PSM_INTERRUPT) &&
             !Extension->Channels[Channel] && !Extension->Opening[Channel];
    if (Accept)
        Extension->Opening[Channel] = TRUE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    Operation = HidBthAllocateOperation(Extension, Channel, BRB_L2CA_OPEN_CHANNEL_RESPONSE);
    if (!Operation)
    {
        if (Accept)
            Extension->Opening[Channel] = FALSE;
        return;
    }
    Open = &Operation->Brb.BrbL2caOpenChannel;
    HidBthPrepareOpen(Extension, Open);
    Open->ChannelHandle = Parameters->ConnectionHandle;
    Open->Response = Accept ? CONNECT_RSP_RESULT_SUCCESS : CONNECT_RSP_RESULT_NO_RESOURCES;
    Open->ResponseStatus = CONNECT_RSP_STATUS_NO_INFORMATION;
    HidBthSubmitAsync(Extension, Operation->Irp, &Operation->Brb,
                      Accept ? HidBthOpenComplete : HidBthOperationComplete, Operation);
}

static
VOID
HidBthChannelIndication(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PHIDBTH_DEVICE_EXTENSION Extension = Context;
    L2CAP_CHANNEL_HANDLE Handle = NULL;
    KIRQL OldIrql;
    ULONG Index;

    if (Indication != IndicationRemoteDisconnect)
        return;
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    for (Index = 0; Index < HidBthChannels; Index++)
    {
        if (Extension->Channels[Index] == Parameters->ConnectionHandle)
        {
            Handle = Extension->Channels[Index];
            Extension->Channels[Index] = NULL;
        }
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Handle)
        return;
    DPRINT1("HIDBTH: %012I64x disconnected a channel\n", Extension->Address);
    HidBthCloseHandle(Extension, Handle);
}

static
NTSTATUS
HidBthSend(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ HIDBTH_CHANNEL Channel,
    _In_ UCHAR Header,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    struct _BRB_L2CA_ACL_TRANSFER *Transfer;
    L2CAP_CHANNEL_HANDLE Handle;
    PUCHAR Packet;
    KIRQL OldIrql;
    BRB Brb;
    NTSTATUS Status;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Handle = Extension->Started ? Extension->Channels[Channel] : NULL;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Handle)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (Length >= HIDBTH_BUFFER_SIZE)
        return STATUS_INVALID_BUFFER_SIZE;
    Packet = ExAllocatePoolWithTag(NonPagedPool, 1 + Length, HIDBTH_TAG);
    if (!Packet)
        return STATUS_INSUFFICIENT_RESOURCES;
    Packet[0] = Header;
    if (Length)
        RtlCopyMemory(Packet + 1, Data, Length);

    Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_ACL_TRANSFER);
    Transfer = &Brb.BrbL2caAclTransfer;
    Transfer->BtAddress = Extension->Address;
    Transfer->ChannelHandle = Handle;
    Transfer->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    Transfer->BufferSize = 1 + Length;
    Transfer->Buffer = Packet;
    Status = HidBthSubmitSync(Extension, &Brb);
    ExFreePoolWithTag(Packet, HIDBTH_TAG);
    return Status;
}

static
NTSTATUS
HidBthHandshakeStatus(
    _In_ UCHAR Result)
{
    switch (Result)
    {
        case 0x0:
            return STATUS_SUCCESS;
        case 0x1:
            return STATUS_DEVICE_NOT_READY;
        case 0x2:
        case 0x4:
            return STATUS_INVALID_PARAMETER;
        case 0x3:
            return STATUS_NOT_SUPPORTED;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static
NTSTATUS
HidBthControlTransaction(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ UCHAR Header,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(*ResponseLength) PUCHAR Response,
    _Inout_opt_ PULONG ResponseLength)
{
    LARGE_INTEGER Timeout;
    KIRQL OldIrql;
    NTSTATUS Status;

    KeWaitForSingleObject(&Extension->ControlLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->ControlData = Response;
    Extension->ControlLength = ResponseLength ? *ResponseLength : 0;
    Extension->ControlResult = 0x0F;
    Extension->ControlWaiting = TRUE;
    KeClearEvent(&Extension->ControlDone);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    Status = HidBthSend(Extension, HidBthControl, Header, Data, Length);
    if (NT_SUCCESS(Status))
    {
        Timeout.QuadPart = -(LONGLONG)HIDBTH_CONTROL_TIMEOUT_MS * 10000;
        Status = KeWaitForSingleObject(&Extension->ControlDone, Executive, KernelMode, FALSE, &Timeout);
        Status = Status == STATUS_TIMEOUT ? STATUS_IO_TIMEOUT : STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->ControlWaiting = FALSE;
    Extension->ControlData = NULL;
    if (NT_SUCCESS(Status))
    {
        Status = HidBthHandshakeStatus(Extension->ControlResult);
        if (ResponseLength)
            *ResponseLength = Extension->ControlLength;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    KeReleaseMutex(&Extension->ControlLock, FALSE);
    return Status;
}

static
VOID
HidBthReportPayload(
    _In_ PHID_XFER_PACKET Packet,
    _Out_ const UCHAR **Data,
    _Out_ PULONG Length)
{
    *Data = Packet->reportBuffer;
    *Length = Packet->reportBufferLen;
    if (!Packet->reportId && *Length && !Packet->reportBuffer[0])
    {
        (*Data)++;
        (*Length)--;
    }
}

static
NTSTATUS
HidBthGetReport(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ UCHAR Type,
    _Inout_ PHID_XFER_PACKET Packet)
{
    ULONG Length = Packet->reportBufferLen;
    UCHAR ReportId = Packet->reportId;
    NTSTATUS Status;

    Status = HidBthControlTransaction(Extension, HIDP_GET_REPORT | Type, ReportId ? &ReportId : NULL,
                                      ReportId ? 1 : 0, Packet->reportBuffer, &Length);
    if (NT_SUCCESS(Status))
        Packet->reportBufferLen = Length;
    return Status;
}

static
NTSTATUS
HidBthSetReport(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ UCHAR Type,
    _In_ PHID_XFER_PACKET Packet)
{
    const UCHAR *Data;
    ULONG Length;

    HidBthReportPayload(Packet, &Data, &Length);
    return HidBthControlTransaction(Extension, HIDP_SET_REPORT | Type, Data, Length, NULL, NULL);
}

static
NTSTATUS
HidBthWriteReport(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_ PHID_XFER_PACKET Packet)
{
    const UCHAR *Data;
    ULONG Length;

    HidBthReportPayload(Packet, &Data, &Length);
    return HidBthSend(Extension, HidBthInterrupt, HIDP_DATA | HIDP_REPORT_OUTPUT, Data, Length);
}

static
ULONG
HidBthSdpElement(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PUCHAR Type,
    _Out_ PULONG ValueOffset,
    _Out_ PULONG ValueLength)
{
    static const UCHAR FixedSizes[] = { 1, 2, 4, 8, 16 };
    UCHAR Index;

    if (!Length)
        return 0;
    *Type = Data[0] >> 3;
    Index = Data[0] & 7;
    if (*Type == 0)
    {
        *ValueOffset = 1;
        *ValueLength = 0;
    }
    else if (Index < 5)
    {
        *ValueOffset = 1;
        *ValueLength = FixedSizes[Index];
    }
    else if (Index == 5 && Length >= 2)
    {
        *ValueOffset = 2;
        *ValueLength = Data[1];
    }
    else if (Index == 6 && Length >= 3)
    {
        *ValueOffset = 3;
        *ValueLength = (Data[1] << 8) | Data[2];
    }
    else if (Index == 7 && Length >= 5)
    {
        *ValueOffset = 5;
        *ValueLength = ((ULONG)Data[1] << 24) | (Data[2] << 16) | (Data[3] << 8) | Data[4];
    }
    else
    {
        return 0;
    }
    if (*ValueOffset + *ValueLength > Length)
        return 0;
    return *ValueOffset + *ValueLength;
}

static
BOOLEAN
HidBthFindAttribute(
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length,
    _In_ USHORT Attribute,
    _Out_ const UCHAR **Value,
    _Out_ PULONG ValueLength)
{
    ULONG ListOffset, ListLength, Offset;
    UCHAR Type;

    if (!HidBthSdpElement(Record, Length, &Type, &ListOffset, &ListLength) || Type != 6)
        return FALSE;
    Record += ListOffset;
    for (Offset = 0; Offset < ListLength;)
    {
        ULONG IdOffset, IdLength, IdSize, ItemOffset, ItemLength, ItemSize;
        UCHAR IdType, ItemType;

        IdSize = HidBthSdpElement(Record + Offset, ListLength - Offset, &IdType, &IdOffset, &IdLength);
        if (!IdSize || IdType != 1 || IdLength != 2)
            return FALSE;
        ItemSize = HidBthSdpElement(Record + Offset + IdSize, ListLength - Offset - IdSize, &ItemType, &ItemOffset, &ItemLength);
        if (!ItemSize)
            return FALSE;
        if (((Record[Offset + IdOffset] << 8) | Record[Offset + IdOffset + 1]) == Attribute)
        {
            *Value = Record + Offset + IdSize;
            *ValueLength = ItemSize;
            return TRUE;
        }
        Offset += IdSize + ItemSize;
    }
    return FALSE;
}

static
NTSTATUS
HidBthParseRecord(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Record,
    _In_ ULONG Length)
{
    const UCHAR *List;
    ULONG ListLength, ListOffset, ListSize, Offset;
    UCHAR Type;

    if (!HidBthFindAttribute(Record, Length, SDP_ATTRIB_HID_DESCRIPTOR_LIST, &List, &ListLength) ||
        !HidBthSdpElement(List, ListLength, &Type, &ListOffset, &ListSize) || Type != 6)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    List += ListOffset;
    for (Offset = 0; Offset < ListSize;)
    {
        ULONG EntryOffset, EntryLength, EntrySize, KindOffset, KindLength, KindSize, TextOffset, TextLength;
        const UCHAR *Entry;
        UCHAR KindType, TextType;

        EntrySize = HidBthSdpElement(List + Offset, ListSize - Offset, &Type, &EntryOffset, &EntryLength);
        if (!EntrySize || Type != 6)
            break;
        Entry = List + Offset + EntryOffset;
        KindSize = HidBthSdpElement(Entry, EntryLength, &KindType, &KindOffset, &KindLength);
        if (KindSize && KindType == 1 && KindLength == 1 && Entry[KindOffset] == HID_REPORT_DESCRIPTOR_TYPE &&
            HidBthSdpElement(Entry + KindSize, EntryLength - KindSize, &TextType, &TextOffset, &TextLength) &&
            TextType == 4 && TextLength && TextLength <= MAXUSHORT)
        {
            Extension->ReportDescriptor = ExAllocatePoolWithTag(NonPagedPool, TextLength, HIDBTH_TAG);
            if (!Extension->ReportDescriptor)
                return STATUS_INSUFFICIENT_RESOURCES;
            RtlCopyMemory(Extension->ReportDescriptor, Entry + KindSize + TextOffset, TextLength);
            Extension->ReportDescriptorLength = (USHORT)TextLength;
            return STATUS_SUCCESS;
        }
        Offset += EntrySize;
    }
    return STATUS_INVALID_DEVICE_STATE;
}

static
NTSTATUS
HidBthQueryProfileInterface(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Extension->LowerDevice, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType = &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE;
    IoStack->Parameters.QueryInterface.Size = sizeof(Extension->Profile);
    IoStack->Parameters.QueryInterface.Version = BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)&Extension->Profile;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
HidBthRegister(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    struct _BRB_L2CA_REGISTER_SERVER *Server;
    ULONG Index;
    NTSTATUS Status;
    BRB Brb;

    for (Index = 0; Index < HidBthChannels; Index++)
    {
        Extension->Profile.BthInitializeBrb(&Brb, BRB_REGISTER_PSM);
        Brb.BrbPsm.Psm = HidBthPsm((HIDBTH_CHANNEL)Index);
        Status = HidBthSubmitSync(Extension, &Brb);
        if (!NT_SUCCESS(Status))
            return Status;
        Extension->PsmRegistered[Index] = TRUE;
    }

    Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_REGISTER_SERVER);
    Server = &Brb.BrbL2caRegisterServer;
    Server->BtAddress = Extension->Address;
    Server->IndicationCallback = HidBthServerIndication;
    Server->IndicationCallbackContext = Extension;
    Server->ReferenceObject = Extension->DeviceObject;
    Status = HidBthSubmitSync(Extension, &Brb);
    if (NT_SUCCESS(Status))
        Extension->Server = Server->ServerHandle;
    return Status;
}

static
VOID
HidBthUnregister(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    ULONG Index;
    BRB Brb;

    if (Extension->Server)
    {
        Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_UNREGISTER_SERVER);
        Brb.BrbL2caUnregisterServer.BtAddress = Extension->Address;
        Brb.BrbL2caUnregisterServer.ServerHandle = Extension->Server;
        HidBthSubmitSync(Extension, &Brb);
        Extension->Server = NULL;
    }
    for (Index = 0; Index < HidBthChannels; Index++)
    {
        if (!Extension->PsmRegistered[Index])
            continue;
        Extension->Profile.BthInitializeBrb(&Brb, BRB_UNREGISTER_PSM);
        Brb.BrbPsm.Psm = HidBthPsm((HIDBTH_CHANNEL)Index);
        HidBthSubmitSync(Extension, &Brb);
        Extension->PsmRegistered[Index] = FALSE;
    }
}

static
VOID
HidBthStop(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    L2CAP_CHANNEL_HANDLE Handles[HidBthChannels];
    LARGE_INTEGER Timeout;
    KIRQL OldIrql;
    ULONG Index;
    BRB Brb;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Extension->Started = FALSE;
    for (Index = 0; Index < HidBthChannels; Index++)
    {
        Handles[Index] = Extension->Channels[Index];
        Extension->Channels[Index] = NULL;
    }
    if (Extension->ControlWaiting)
    {
        Extension->ControlWaiting = FALSE;
        KeSetEvent(&Extension->ControlDone, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    HidBthFailPendingReads(Extension, STATUS_DEVICE_NOT_CONNECTED);
    for (Index = 0; Index < HidBthChannels; Index++)
    {
        if (!Handles[Index])
            continue;
        Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_CLOSE_CHANNEL);
        Brb.BrbL2caCloseChannel.BtAddress = Extension->Address;
        Brb.BrbL2caCloseChannel.ChannelHandle = Handles[Index];
        HidBthSubmitSync(Extension, &Brb);
    }
    HidBthUnregister(Extension);

    Timeout.QuadPart = -(LONGLONG)HIDBTH_CLOSE_TIMEOUT_MS * 10000;
    while (Extension->IoOutstanding)
    {
        IoCancelIrp(Extension->ControlRead.Irp);
        for (Index = 0; Index < HIDBTH_INTERRUPT_READS; Index++)
            IoCancelIrp(Extension->InterruptReads[Index].Irp);
        KeClearEvent(&Extension->IoDrained);
        if (Extension->IoOutstanding &&
            KeWaitForSingleObject(&Extension->IoDrained, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        {
            DPRINT1("HIDBTH: %ld requests still outstanding\n", Extension->IoOutstanding);
            break;
        }
    }
}

static
VOID
HidBthFreeResources(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    ULONG Index;

    if (Extension->ControlRead.Irp)
        IoFreeIrp(Extension->ControlRead.Irp);
    Extension->ControlRead.Irp = NULL;
    for (Index = 0; Index < HIDBTH_INTERRUPT_READS; Index++)
    {
        if (Extension->InterruptReads[Index].Irp)
            IoFreeIrp(Extension->InterruptReads[Index].Irp);
        Extension->InterruptReads[Index].Irp = NULL;
    }
    if (Extension->ReportDescriptor)
        ExFreePoolWithTag(Extension->ReportDescriptor, HIDBTH_TAG);
    Extension->ReportDescriptor = NULL;
    if (Extension->ReportQueue)
        ExFreePoolWithTag(Extension->ReportQueue, HIDBTH_TAG);
    Extension->ReportQueue = NULL;
    if (Extension->HaveProfile && Extension->Profile.Interface.InterfaceDereference)
        Extension->Profile.Interface.InterfaceDereference(Extension->Profile.Interface.Context);
    Extension->HaveProfile = FALSE;
}

static
NTSTATUS
HidBthStart(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension)
{
    BTH_ENUMERATOR_INFO EnumInfo;
    BTH_DEVICE_INFO DeviceInfo;
    PUCHAR Record = NULL;
    ULONG RecordLength = 0, Index;
    NTSTATUS Status;

    Status = HidBthQueryProfileInterface(Extension);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->HaveProfile = TRUE;

    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    Status = HidBthCallLower(Extension, IOCTL_INTERNAL_BTHENUM_GET_DEVINFO, &DeviceInfo, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->Address = DeviceInfo.address;

    RtlZeroMemory(&EnumInfo, sizeof(EnumInfo));
    if (NT_SUCCESS(HidBthCallLower(Extension, IOCTL_INTERNAL_BTHENUM_GET_ENUMINFO, &EnumInfo, NULL)))
    {
        Extension->VendorId = EnumInfo.Vid;
        Extension->ProductId = EnumInfo.Pid;
    }

    Status = HidBthCallLower(Extension, IOCTL_INTERNAL_BTHMINI_GET_SERVICE_RECORD, NULL, &RecordLength);
    if (Status != STATUS_BUFFER_TOO_SMALL || !RecordLength)
        return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
    Record = ExAllocatePoolWithTag(PagedPool, RecordLength, HIDBTH_TAG);
    if (!Record)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = HidBthCallLower(Extension, IOCTL_INTERNAL_BTHMINI_GET_SERVICE_RECORD, Record, &RecordLength);
    if (NT_SUCCESS(Status))
        Status = HidBthParseRecord(Extension, Record, RecordLength);
    ExFreePoolWithTag(Record, HIDBTH_TAG);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDBTH: %012I64x has no usable HID descriptor 0x%08lx\n", Extension->Address, Status);
        return Status;
    }

    Extension->ReportSlotLength = sizeof(USHORT) + HIDBTH_BUFFER_SIZE;
    Extension->ReportQueue = ExAllocatePoolWithTag(NonPagedPool, Extension->ReportSlotLength * HIDBTH_QUEUED_REPORTS,
                                                   HIDBTH_TAG);
    Extension->ControlRead.Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
    for (Index = 0; Index < HIDBTH_INTERRUPT_READS; Index++)
        Extension->InterruptReads[Index].Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
    if (!Extension->ReportQueue || !Extension->ControlRead.Irp ||
        !Extension->InterruptReads[0].Irp || !Extension->InterruptReads[HIDBTH_INTERRUPT_READS - 1].Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Extension->ControlRead.Extension = Extension;
    Extension->ControlRead.Channel = HidBthControl;
    for (Index = 0; Index < HIDBTH_INTERRUPT_READS; Index++)
    {
        Extension->InterruptReads[Index].Extension = Extension;
        Extension->InterruptReads[Index].Channel = HidBthInterrupt;
    }

    Extension->Started = TRUE;
    Status = HidBthRegister(Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDBTH: HID PSMs not registered 0x%08lx\n", Status);
        HidBthStop(Extension);
        return Status;
    }
    DPRINT1("HIDBTH: %012I64x started, report descriptor %u bytes, VID %04x PID %04x\n",
            Extension->Address, Extension->ReportDescriptorLength, Extension->VendorId, Extension->ProductId);
    HidBthOpenChannel(Extension, HidBthControl);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidBthReadReport(
    _In_ PHIDBTH_DEVICE_EXTENSION Extension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KIRQL OldIrql;
    ULONG Length;

    if (!Irp->UserBuffer || !IoStack->Parameters.DeviceIoControl.OutputBufferLength)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Extension->ReportQueueCount)
    {
        PUCHAR Slot = Extension->ReportQueue + Extension->ReportQueueHead * Extension->ReportSlotLength;

        Length = HidBthCopyReport(Irp, Slot + sizeof(USHORT), Slot[0] | ((ULONG)Slot[1] << 8));
        Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDBTH_QUEUED_REPORTS;
        Extension->ReportQueueCount--;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Information = Length;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IoMarkIrpPending(Irp);
    InsertTailList(&Extension->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, HidBthCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
HidBthCompleteWithBuffer(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Irp->UserBuffer || IoStack->Parameters.DeviceIoControl.OutputBufferLength < Length)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        RtlCopyMemory(Irp->UserBuffer, Data, Length);
        Irp->IoStatus.Information = Length;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HidBthInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTH_DEVICE_EXTENSION Extension = HidBthGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    PHID_XFER_PACKET Packet = Irp->UserBuffer;
    NTSTATUS Status;

    if (IoControlCode == IOCTL_HID_READ_REPORT)
        return HidBthReadReport(Extension, Irp);

    Irp->IoStatus.Information = 0;
    if (!Extension->Started)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }

    switch (IoControlCode)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR HidDescriptor;

            RtlZeroMemory(&HidDescriptor, sizeof(HidDescriptor));
            HidDescriptor.bLength = sizeof(HidDescriptor);
            HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            HidDescriptor.bcdHID = 0x0111;
            HidDescriptor.bNumDescriptors = 1;
            HidDescriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            HidDescriptor.DescriptorList[0].wReportLength = Extension->ReportDescriptorLength;
            return HidBthCompleteWithBuffer(Irp, &HidDescriptor, sizeof(HidDescriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return HidBthCompleteWithBuffer(Irp, Extension->ReportDescriptor, Extension->ReportDescriptorLength);

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            Attributes.VendorID = Extension->VendorId;
            Attributes.ProductID = Extension->ProductId;
            Attributes.VersionNumber = Extension->Version;
            return HidBthCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_GET_INPUT_REPORT:
            Status = Packet ? HidBthGetReport(Extension, IoControlCode == IOCTL_HID_GET_FEATURE ? HIDP_REPORT_FEATURE :
                                                                                                  HIDP_REPORT_INPUT, Packet) :
                              STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_SET_OUTPUT_REPORT:
            Status = Packet ? HidBthSetReport(Extension, IoControlCode == IOCTL_HID_SET_FEATURE ? HIDP_REPORT_FEATURE :
                                                                                                  HIDP_REPORT_OUTPUT, Packet) :
                              STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_WRITE_REPORT:
            Status = Packet ? HidBthWriteReport(Extension, Packet) : STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HidBthPnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
HidBthPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDBTH_DEVICE_EXTENSION Extension = HidBthGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, HidBthPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = HidBthStart(Extension);
            if (!NT_SUCCESS(Status))
                HidBthFreeResources(Extension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            HidBthStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            HidBthStop(Extension);
            HidBthFreeResources(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(HidBthGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidBthGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidBthCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HidBthAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDBTH_DEVICE_EXTENSION Extension = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->DeviceObject = DeviceObject;
    Extension->LowerDevice = HidExtension->NextDeviceObject;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeEvent(&Extension->IoDrained, NotificationEvent, FALSE);
    KeInitializeMutex(&Extension->ControlLock, 0);
    KeInitializeEvent(&Extension->ControlDone, NotificationEvent, FALSE);
    InitializeListHead(&Extension->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HidBthUnload(
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
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HidBthCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidBthCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidBthInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HidBthPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HidBthPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HidBthPassThrough;
    DriverObject->DriverExtension->AddDevice = HidBthAddDevice;
    DriverObject->DriverUnload = HidBthUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(HIDBTH_DEVICE_EXTENSION);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
