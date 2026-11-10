/*
 * PROJECT:     LiberNT USB CCID Smart Card Reader Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CCID messages and the smart card library reader functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "usbccid.h"

#define NDEBUG
#include <debug.h>

static IO_COMPLETION_ROUTINE CcidTransferCompletion;
static IO_COMPLETION_ROUTINE CcidInterruptCompletion;
static DRIVER_CANCEL CcidCancelTracking;

static
ULONG
CcidReadUlong(
    _In_reads_bytes_(4) const UCHAR *Bytes)
{
    return Bytes[0] | ((ULONG)Bytes[1] << 8) | ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
}

static
VOID
CcidSetState(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ ULONG State)
{
    KIRQL Irql;

    ASSERT(State <= SCARD_SPECIFIC);
    KeAcquireSpinLock(&Smartcard->OsData->SpinLock, &Irql);
    Smartcard->ReaderCapabilities.CurrentState = State;
    KeReleaseSpinLock(&Smartcard->OsData->SpinLock, Irql);
}

static
NTSTATUS
NTAPI
CcidTransferCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    ASSERT(Context);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
CcidSubmitPipeUrb(
    _In_ PREADER_EXTENSION Reader,
    _In_ PURB Urb,
    _In_ ULONG TimeoutMs)
{
    PIO_STACK_LOCATION Stack;
    LARGE_INTEGER Timeout;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Irp = IoAllocateIrp(Reader->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = Urb;
    IoSetCompletionRoutine(Irp, CcidTransferCompletion, &Event, TRUE, TRUE, TRUE);
    IoCallDriver(Reader->LowerDevice, Irp);
    Timeout.QuadPart = -10000LL * TimeoutMs;
    if (KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        IoCancelIrp(Irp);
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    if (Status == STATUS_CANCELLED)
        return STATUS_IO_TIMEOUT;
    if (NT_SUCCESS(Status) && !USBD_SUCCESS(Urb->UrbHeader.Status))
        Status = STATUS_UNSUCCESSFUL;
    return Status;
}

static
VOID
CcidResetPipe(
    _In_ PREADER_EXTENSION Reader,
    _In_ USBD_PIPE_HANDLE Pipe)
{
    URB Urb;

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
    Urb.UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    Urb.UrbPipeRequest.PipeHandle = Pipe;
    CcidSubmitPipeUrb(Reader, &Urb, CCID_TRANSFER_TIMEOUT_MS);
}

static
NTSTATUS
CcidBulk(
    _In_ PREADER_EXTENSION Reader,
    _In_ BOOLEAN In,
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Transferred)
{
    USBD_PIPE_HANDLE Pipe = In ? Reader->BulkIn : Reader->BulkOut;
    URB Urb;
    NTSTATUS Status;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildInterruptOrBulkTransferRequest(&Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER), Pipe, Buffer, NULL,
                                           Length,
                                           In ? USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK
                                              : USBD_TRANSFER_DIRECTION_OUT,
                                           NULL);
    Status = CcidSubmitPipeUrb(Reader, &Urb, CCID_TRANSFER_TIMEOUT_MS);
    *Transferred = NT_SUCCESS(Status) ? Urb.UrbBulkOrInterruptTransfer.TransferBufferLength : 0;
    ASSERT(*Transferred <= Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_IO_TIMEOUT)
        CcidResetPipe(Reader, Pipe);
    return Status;
}

static
NTSTATUS
CcidCommand(
    _In_ PREADER_EXTENSION Reader,
    _In_ UCHAR Type,
    _In_ UCHAR Parameter,
    _In_ USHORT Level,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ UCHAR Expected,
    _Outptr_opt_result_bytebuffer_(*ReplyLength) PUCHAR *Reply,
    _Out_opt_ PULONG ReplyLength)
{
    PSMARTCARD_EXTENSION Smartcard = Reader->Smartcard;
    PUCHAR Message = Reader->Command;
    PUCHAR Response = Reader->Response;
    ULONG Transferred, Payload;
    UCHAR Sequence;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Reply)
        *Reply = NULL;
    if (ReplyLength)
        *ReplyLength = 0;
    if (Length > Reader->MaxMessage - CCID_HEADER_SIZE)
        return STATUS_BUFFER_OVERFLOW;
    Sequence = Reader->Sequence++;
    Message[0] = Type;
    Message[1] = (UCHAR)Length;
    Message[2] = (UCHAR)(Length >> 8);
    Message[3] = (UCHAR)(Length >> 16);
    Message[4] = (UCHAR)(Length >> 24);
    Message[5] = 0;
    Message[6] = Sequence;
    Message[7] = Parameter;
    Message[8] = (UCHAR)Level;
    Message[9] = (UCHAR)(Level >> 8);
    if (Length)
        RtlCopyMemory(Message + CCID_HEADER_SIZE, Data, Length);
    Status = CcidBulk(Reader, FALSE, Message, CCID_HEADER_SIZE + Length, &Transferred);
    if (!NT_SUCCESS(Status))
        return Status;

    for (;;)
    {
        Status = CcidBulk(Reader, TRUE, Response, Reader->MaxMessage, &Transferred);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Transferred < CCID_HEADER_SIZE || Response[5] != 0)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        if (Response[6] != Sequence)
            continue;
        if (Response[0] != Expected)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        if (CCID_COMMAND_STATUS(Response[7]) != CCID_COMMAND_TIME_EXTENSION)
            break;
    }

    if (CCID_ICC_STATUS(Response[7]) == CCID_ICC_ABSENT)
        CcidSetState(Smartcard, SCARD_ABSENT);
    if (CCID_COMMAND_STATUS(Response[7]) == CCID_COMMAND_FAILED)
    {
        DPRINT("USBCCID: command %02x failed, status %02x error %02x\n", Type, Response[7], Response[8]);
        if (CCID_ICC_STATUS(Response[7]) == CCID_ICC_ABSENT)
            return STATUS_NO_MEDIA;
        if (Response[8] == CCID_ERROR_ICC_MUTE)
            return STATUS_IO_TIMEOUT;
        if (Response[8] == CCID_ERROR_COMMAND_NOT_SUPPORTED)
            return STATUS_NOT_SUPPORTED;
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (CCID_COMMAND_STATUS(Response[7]) != CCID_COMMAND_OK)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Payload = CcidReadUlong(Response + 1);
    if (Payload > Transferred - CCID_HEADER_SIZE)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (Reply)
        *Reply = Response + CCID_HEADER_SIZE;
    if (ReplyLength)
        *ReplyLength = Payload;
    return STATUS_SUCCESS;
}

static
NTSTATUS
CcidXfrBlock(
    _In_ PREADER_EXTENSION Reader,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ USHORT Level,
    _Out_writes_bytes_to_(Size, *Received) PUCHAR Out,
    _In_ ULONG Size,
    _Out_ PULONG Received,
    _Out_opt_ PUCHAR Chain)
{
    PUCHAR Reply;
    ULONG ReplyLength;
    NTSTATUS Status;

    *Received = 0;
    Status = CcidCommand(Reader, PC_TO_RDR_XFR_BLOCK, 0, Level, Data, Length, RDR_TO_PC_DATA_BLOCK, &Reply,
                         &ReplyLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ReplyLength > Size)
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(Out, Reply, ReplyLength);
    *Received = ReplyLength;
    if (Chain)
        *Chain = Reply[-1];
    return STATUS_SUCCESS;
}

static
NTSTATUS
CcidPowerOn(
    _In_ PREADER_EXTENSION Reader,
    _Outptr_result_bytebuffer_(*AtrLength) PUCHAR *Atr,
    _Out_ PULONG AtrLength)
{
    static const UCHAR Classes[] = { CCID_VOLTAGE_5V, CCID_VOLTAGE_3V, CCID_VOLTAGE_1V8 };
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    ULONG Index;

    if (Reader->Features & CCID_FEATURE_AUTO_VOLTAGE)
        return CcidCommand(Reader, PC_TO_RDR_ICC_POWER_ON, 0, 0, NULL, 0, RDR_TO_PC_DATA_BLOCK, Atr, AtrLength);
    for (Index = 0; Index < RTL_NUMBER_OF(Classes); ++Index)
    {
        if (!(Reader->VoltageSupport & Classes[Index]))
            continue;
        Status = CcidCommand(Reader, PC_TO_RDR_ICC_POWER_ON, (UCHAR)(Index + 1), 0, NULL, 0, RDR_TO_PC_DATA_BLOCK,
                             Atr, AtrLength);
        if (NT_SUCCESS(Status) || Status == STATUS_NO_MEDIA)
            return Status;
        CcidCommand(Reader, PC_TO_RDR_ICC_POWER_OFF, 0, 0, NULL, 0, RDR_TO_PC_SLOT_STATUS, NULL, NULL);
    }
    return Status;
}

static
NTSTATUS
CcidSetParameters(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ ULONG Protocol,
    _In_ UCHAR Fl,
    _In_ UCHAR Dl)
{
    PSCARD_CARD_CAPABILITIES Card = &Smartcard->CardCapabilities;
    UCHAR Parameters[7];
    ULONG Length;
    NTSTATUS Status;

    Parameters[0] = (UCHAR)(((Fl & 0x0F) << 4) | (Dl & 0x0F));
    Parameters[2] = Card->N;
    Parameters[4] = 0;
    if (Protocol == SCARD_PROTOCOL_T1)
    {
        Parameters[1] = 0x10 | ((Card->T1.EDC & T1_CRC_CHECK) ? 0x01 : 0x00) | (Card->InversConvention ? 0x02 : 0x00);
        Parameters[3] = (UCHAR)((Card->T1.BWI << 4) | (Card->T1.CWI & 0x0F));
        Parameters[5] = Card->T1.IFSC;
        Parameters[6] = 0;
        Length = 7;
    }
    else
    {
        Parameters[1] = Card->InversConvention ? 0x02 : 0x00;
        Parameters[3] = Card->T0.WI;
        Length = 5;
    }
    Status = CcidCommand(Smartcard->ReaderExtension, PC_TO_RDR_SET_PARAMETERS,
                         Protocol == SCARD_PROTOCOL_T1 ? 1 : 0, 0, Parameters, Length, RDR_TO_PC_PARAMETERS, NULL,
                         NULL);
    return Status == STATUS_NOT_SUPPORTED ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
CcidPps(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ ULONG Protocol,
    _Out_ PUCHAR Fl,
    _Out_ PUCHAR Dl)
{
    PPTS_DATA Pts = &Smartcard->CardCapabilities.PtsData;
    UCHAR Request[4], Reply[4];
    ULONG Length = 0, Received;
    NTSTATUS Status;

    *Fl = 1;
    *Dl = 1;
    Request[Length++] = 0xFF;
    Request[Length++] = Protocol == SCARD_PROTOCOL_T1 ? 0x01 : 0x00;
    if (Pts->Type != PTS_TYPE_DEFAULT && (Pts->Fl != 1 || Pts->Dl != 1))
    {
        Request[1] |= 0x10;
        Request[Length++] = (UCHAR)((Pts->Fl << 4) | (Pts->Dl & 0x0F));
    }
    Request[Length] = Request[0] ^ Request[1] ^ (Length == 3 ? Request[2] : 0);
    ++Length;
    Status = CcidXfrBlock(Smartcard->ReaderExtension, Request, Length, (USHORT)Length, Reply, sizeof(Reply),
                          &Received, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Received != Length || RtlCompareMemory(Request, Reply, Length) != Length)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (Request[1] & 0x10)
    {
        *Fl = Pts->Fl;
        *Dl = Pts->Dl;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
CcidPower(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    PREADER_EXTENSION Reader = Smartcard->ReaderExtension;
    PSCARD_CARD_CAPABILITIES Card = &Smartcard->CardCapabilities;
    PUCHAR Atr;
    ULONG AtrLength;
    NTSTATUS Status;

    *Smartcard->IoRequest.Information = 0;
    Status = CcidCommand(Reader, PC_TO_RDR_ICC_POWER_OFF, 0, 0, NULL, 0, RDR_TO_PC_SLOT_STATUS, NULL, NULL);
    if (Status == STATUS_NO_MEDIA)
        return Status;
    if (Smartcard->MinorIoControlCode == SCARD_POWER_DOWN)
    {
        if (NT_SUCCESS(Status))
            CcidSetState(Smartcard, SCARD_SWALLOWED);
        return Status;
    }

    /* FIXME: a warm reset powers the card off and on like a cold one. */
    CcidSetState(Smartcard, SCARD_SWALLOWED);
    Status = CcidPowerOn(Reader, &Atr, &AtrLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (AtrLength == 0 || AtrLength > MAXIMUM_ATR_LENGTH || AtrLength > Smartcard->IoRequest.ReplyBufferLength)
        return STATUS_UNRECOGNIZED_MEDIA;
    RtlCopyMemory(Card->ATR.Buffer, Atr, AtrLength);
    Card->ATR.Length = (UCHAR)AtrLength;
    Status = SmartcardUpdateCardCapabilities(Smartcard);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Smartcard->ReaderCapabilities.CurrentState == SCARD_SPECIFIC &&
        !(Reader->Features & CCID_FEATURE_AUTO_PARAMETERS))
    {
        Status = CcidSetParameters(Smartcard, Card->Protocol.Selected, Card->Fl, Card->Dl);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    RtlCopyMemory(Smartcard->IoRequest.ReplyBuffer, Card->ATR.Buffer, Card->ATR.Length);
    *Smartcard->IoRequest.Information = Card->ATR.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
CcidSetProtocol(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    PREADER_EXTENSION Reader = Smartcard->ReaderExtension;
    PSCARD_CARD_CAPABILITIES Card = &Smartcard->CardCapabilities;
    ULONG Allowed, Protocol;
    UCHAR Fl = Card->Fl, Dl = Card->Dl;
    NTSTATUS Status;

    *Smartcard->IoRequest.Information = 0;
    if (Smartcard->ReaderCapabilities.CurrentState == SCARD_SPECIFIC)
    {
        Protocol = Card->Protocol.Selected;
    }
    else
    {
        Allowed = Smartcard->MinorIoControlCode & Card->Protocol.Supported &
                  Smartcard->ReaderCapabilities.SupportedProtocols;
        Protocol = (Allowed & SCARD_PROTOCOL_T1) ? SCARD_PROTOCOL_T1 :
                   (Allowed & SCARD_PROTOCOL_T0) ? SCARD_PROTOCOL_T0 : SCARD_PROTOCOL_UNDEFINED;
        if (Protocol == SCARD_PROTOCOL_UNDEFINED)
            return STATUS_INVALID_DEVICE_REQUEST;
        if (!(Reader->Features & (CCID_FEATURE_AUTO_PPS_PROP | CCID_FEATURE_AUTO_PPS_CUR)))
        {
            Status = CcidPps(Smartcard, Protocol, &Fl, &Dl);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    ASSERT(Protocol == SCARD_PROTOCOL_T0 || Protocol == SCARD_PROTOCOL_T1);
    if (!(Reader->Features & CCID_FEATURE_AUTO_PARAMETERS))
    {
        Status = CcidSetParameters(Smartcard, Protocol, Fl, Dl);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Card->Protocol.Selected = Protocol;
    CcidSetState(Smartcard, SCARD_SPECIFIC);
    *(PULONG)Smartcard->IoRequest.ReplyBuffer = Protocol;
    *Smartcard->IoRequest.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CcidTransmitTpdu(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    PREADER_EXTENSION Reader = Smartcard->ReaderExtension;
    PSMARTCARD_REQUEST Request = &Smartcard->SmartcardRequest;
    PSMARTCARD_REPLY Reply = &Smartcard->SmartcardReply;
    NTSTATUS Status;

    if (Smartcard->CardCapabilities.Protocol.Selected == SCARD_PROTOCOL_T0)
    {
        Request->BufferLength = 0;
        Status = SmartcardT0Request(Smartcard);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = CcidXfrBlock(Reader, Request->Buffer, Request->BufferLength, 0, Reply->Buffer, Reply->BufferSize,
                              &Reply->BufferLength, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        return SmartcardT0Reply(Smartcard);
    }

    do
    {
        Request->BufferLength = 0;
        Status = SmartcardT1Request(Smartcard);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = CcidXfrBlock(Reader, Request->Buffer, Request->BufferLength, 0, Reply->Buffer, Reply->BufferSize,
                              &Reply->BufferLength, NULL);
        if (Status == STATUS_IO_TIMEOUT)
            Reply->BufferLength = 0;
        else if (!NT_SUCCESS(Status))
            return Status;
        Status = SmartcardT1Reply(Smartcard);
    } while (Status == STATUS_MORE_PROCESSING_REQUIRED);
    return Status;
}

static
NTSTATUS
CcidTransmitApdu(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    PREADER_EXTENSION Reader = Smartcard->ReaderExtension;
    PSCARD_IO_REQUEST Header = (PSCARD_IO_REQUEST)Smartcard->IoRequest.RequestBuffer;
    PSCARD_IO_REQUEST Out = (PSCARD_IO_REQUEST)Smartcard->IoRequest.ReplyBuffer;
    ULONG Chunk = Reader->MaxMessage - CCID_HEADER_SIZE;
    ULONG Offset, Length, Sent, Size, Received, Total = 0;
    PUCHAR Data;
    USHORT Level;
    UCHAR Chain = 0;
    NTSTATUS Status;

    if (Header->cbPciLength < sizeof(SCARD_IO_REQUEST) ||
        Header->cbPciLength > Smartcard->IoRequest.RequestBufferLength ||
        Smartcard->IoRequest.ReplyBufferLength < sizeof(SCARD_IO_REQUEST))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Data = Smartcard->IoRequest.RequestBuffer + Header->cbPciLength;
    Length = Smartcard->IoRequest.RequestBufferLength - Header->cbPciLength;
    Size = Smartcard->IoRequest.ReplyBufferLength - sizeof(SCARD_IO_REQUEST);

    for (Offset = 0;; Offset += Sent)
    {
        Sent = min(Length - Offset, Chunk);
        if (Length <= Chunk)
            Level = 0x00;
        else if (Offset == 0)
            Level = 0x01;
        else if (Offset + Sent == Length)
            Level = 0x02;
        else
            Level = 0x03;
        Status = CcidXfrBlock(Reader, Data + Offset, Sent, Level, (PUCHAR)(Out + 1), Size, &Received, &Chain);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Offset + Sent == Length)
            break;
        if (Chain != 0x10)
            return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    Total = Received;
    while (Chain == 0x01 || Chain == 0x03)
    {
        Status = CcidXfrBlock(Reader, NULL, 0, 0x10, (PUCHAR)(Out + 1) + Total, Size - Total, &Received, &Chain);
        if (!NT_SUCCESS(Status))
            return Status;
        Total += Received;
    }
    Out->dwProtocol = Smartcard->CardCapabilities.Protocol.Selected;
    Out->cbPciLength = sizeof(SCARD_IO_REQUEST);
    *Smartcard->IoRequest.Information = sizeof(SCARD_IO_REQUEST) + Total;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
CcidTransmit(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    PREADER_EXTENSION Reader = Smartcard->ReaderExtension;

    *Smartcard->IoRequest.Information = 0;
    switch (Reader->Features & CCID_FEATURE_EXCHANGE_MASK)
    {
        case CCID_FEATURE_TPDU:
            return CcidTransmitTpdu(Smartcard);

        case CCID_FEATURE_SHORT_APDU:
        case CCID_FEATURE_EXTENDED_APDU:
            return CcidTransmitApdu(Smartcard);

        default:
            /* TODO: character level readers exchange the T=0 procedure bytes themselves. */
            return STATUS_NOT_SUPPORTED;
    }
}

static
VOID
NTAPI
CcidCancelTracking(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCCID_DEVICE Device = DeviceObject->DeviceExtension;
    POS_DEP_DATA OsData = Device->Smartcard.OsData;
    KIRQL Irql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&OsData->SpinLock, &Irql);
    if (OsData->NotificationIrp == Irp)
        OsData->NotificationIrp = NULL;
    KeReleaseSpinLock(&OsData->SpinLock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
NTAPI
CcidTrack(
    _In_ PSMARTCARD_EXTENSION Smartcard)
{
    POS_DEP_DATA OsData = Smartcard->OsData;
    PIRP Irp, Complete = NULL;
    BOOLEAN Present;
    KIRQL Irql;

    KeAcquireSpinLock(&OsData->SpinLock, &Irql);
    Irp = OsData->NotificationIrp;
    ASSERT(Irp);
    IoMarkIrpPending(Irp);
    IoSetCancelRoutine(Irp, CcidCancelTracking);
    Present = Smartcard->ReaderCapabilities.CurrentState > SCARD_ABSENT;
    if (Irp->Cancel || (Smartcard->MajorIoControlCode == IOCTL_SMARTCARD_IS_PRESENT) == Present)
    {
        if (IoSetCancelRoutine(Irp, NULL))
        {
            OsData->NotificationIrp = NULL;
            Complete = Irp;
        }
    }
    KeReleaseSpinLock(&OsData->SpinLock, Irql);
    if (Complete)
    {
        Complete->IoStatus.Status = Complete->Cancel ? STATUS_CANCELLED : STATUS_SUCCESS;
        Complete->IoStatus.Information = 0;
        IoCompleteRequest(Complete, IO_NO_INCREMENT);
    }
    return STATUS_PENDING;
}

VOID
CcidCompleteTracking(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ NTSTATUS Status)
{
    POS_DEP_DATA OsData = Smartcard->OsData;
    PIRP Irp;
    KIRQL Irql;

    if (!OsData)
        return;
    KeAcquireSpinLock(&OsData->SpinLock, &Irql);
    Irp = OsData->NotificationIrp;
    if (Irp && IoSetCancelRoutine(Irp, NULL))
        OsData->NotificationIrp = NULL;
    else
        Irp = NULL;
    KeReleaseSpinLock(&OsData->SpinLock, Irql);
    if (Irp)
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
CcidSlotChange(
    _In_ PSMARTCARD_EXTENSION Smartcard,
    _In_ BOOLEAN Present,
    _In_ BOOLEAN Changed)
{
    POS_DEP_DATA OsData = Smartcard->OsData;
    PIRP Irp, Complete = NULL;
    ULONG Code;
    KIRQL Irql;

    KeAcquireSpinLock(&OsData->SpinLock, &Irql);
    if (!Present)
        Smartcard->ReaderCapabilities.CurrentState = SCARD_ABSENT;
    else if (Changed || Smartcard->ReaderCapabilities.CurrentState <= SCARD_ABSENT)
        Smartcard->ReaderCapabilities.CurrentState = SCARD_SWALLOWED;
    Irp = OsData->NotificationIrp;
    if (Irp)
    {
        Code = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.IoControlCode;
        if ((Changed || (Code == IOCTL_SMARTCARD_IS_PRESENT) == Present) && IoSetCancelRoutine(Irp, NULL))
        {
            OsData->NotificationIrp = NULL;
            Complete = Irp;
        }
    }
    KeReleaseSpinLock(&OsData->SpinLock, Irql);
    DPRINT("USBCCID: card %s%s\n", Present ? "present" : "absent", Changed ? ", changed" : "");
    if (Complete)
    {
        Complete->IoStatus.Status = STATUS_SUCCESS;
        Complete->IoStatus.Information = 0;
        IoCompleteRequest(Complete, IO_NO_INCREMENT);
    }
}

static
VOID
CcidSubmitInterrupt(
    _In_ PREADER_EXTENSION Reader)
{
    PIRP Irp = Reader->InterruptIrp;
    PIO_STACK_LOCATION Stack;

    ASSERT(Irp);
    IoReuseIrp(Irp, STATUS_SUCCESS);
    RtlZeroMemory(&Reader->InterruptUrb, sizeof(Reader->InterruptUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Reader->InterruptUrb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Reader->Interrupt, Reader->InterruptBuffer, NULL, Reader->InterruptLength,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK, NULL);
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = &Reader->InterruptUrb;
    IoSetCompletionRoutine(Irp, CcidInterruptCompletion, Reader, TRUE, TRUE, TRUE);
    IoCallDriver(Reader->LowerDevice, Irp);
    if (InterlockedCompareExchange(&Reader->InterruptStopping, 1, 1))
        IoCancelIrp(Irp);
}

static
NTSTATUS
NTAPI
CcidInterruptCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PREADER_EXTENSION Reader = Context;
    PUCHAR Message = Reader->InterruptBuffer;
    ULONG Length = Reader->InterruptUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Irp == Reader->InterruptIrp);
    if (NT_SUCCESS(Irp->IoStatus.Status) && Length >= 2 && Message[0] == RDR_TO_PC_NOTIFY_SLOT_CHANGE)
        CcidSlotChange(Reader->Smartcard, (Message[1] & 0x01) != 0, (Message[1] & 0x02) != 0);
    else if (NT_SUCCESS(Irp->IoStatus.Status) && Length >= 4 && Message[0] == RDR_TO_PC_HARDWARE_ERROR)
        DPRINT1("USBCCID: reader hardware error %02x\n", Message[3]);
    if (InterlockedCompareExchange(&Reader->InterruptStopping, 1, 1) || !NT_SUCCESS(Irp->IoStatus.Status))
    {
        if (!NT_SUCCESS(Irp->IoStatus.Status) && Irp->IoStatus.Status != STATUS_CANCELLED)
        {
            /* FIXME: card tracking ends on a failed interrupt transfer instead of resetting the pipe. */
            DPRINT1("USBCCID: interrupt transfer failed %x\n", Irp->IoStatus.Status);
        }
        KeSetEvent(&Reader->InterruptStopped, IO_NO_INCREMENT, FALSE);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    CcidSubmitInterrupt(Reader);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
CcidReadSlotStatus(
    _In_ PREADER_EXTENSION Reader)
{
    NTSTATUS Status;

    Status = CcidCommand(Reader, PC_TO_RDR_GET_SLOT_STATUS, 0, 0, NULL, 0, RDR_TO_PC_SLOT_STATUS, NULL, NULL);
    if (Status == STATUS_NO_MEDIA || CCID_ICC_STATUS(Reader->Response[7]) == CCID_ICC_ABSENT)
    {
        CcidSetState(Reader->Smartcard, SCARD_ABSENT);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (CCID_ICC_STATUS(Reader->Response[7]) == CCID_ICC_ACTIVE)
        CcidCommand(Reader, PC_TO_RDR_ICC_POWER_OFF, 0, 0, NULL, 0, RDR_TO_PC_SLOT_STATUS, NULL, NULL);
    CcidSetState(Reader->Smartcard, SCARD_SWALLOWED);
    return STATUS_SUCCESS;
}

static
VOID
CcidReadString(
    _In_ PCCID_DEVICE Device,
    _In_ UCHAR Index,
    _Out_writes_(MAXIMUM_ATTR_STRING_LENGTH) PUCHAR Buffer,
    _Out_ PUSHORT Length)
{
    if (!NT_SUCCESS(CcidGetString(Device, Index, Buffer, MAXIMUM_ATTR_STRING_LENGTH, Length)))
        *Length = 0;
}

NTSTATUS
CcidStartReader(
    _In_ PCCID_DEVICE Device,
    _In_ PUSB_DEVICE_DESCRIPTOR DeviceDescriptor,
    _In_ PUCHAR ClassDescriptor)
{
    PSMARTCARD_EXTENSION Smartcard = &Device->Smartcard;
    PREADER_EXTENSION Reader = &Device->Reader;
    PVENDOR_ATTR Vendor = &Smartcard->VendorAttr;
    PSCARD_READER_CAPABILITIES Capabilities = &Smartcard->ReaderCapabilities;
    ULONG MaxMessage;
    NTSTATUS Status;

    PAGED_CODE();
    CcidReleaseReader(Device);
    ASSERT(!Device->SmartcardInitialized && !Reader->InterruptIrp);
    ASSERT(ClassDescriptor[1] == CCID_CLASS_DESCRIPTOR_TYPE && ClassDescriptor[0] >= CCID_CLASS_DESCRIPTOR_LENGTH);
    RtlZeroMemory(Smartcard, sizeof(*Smartcard));
    Reader->Smartcard = Smartcard;
    Reader->VoltageSupport = ClassDescriptor[5];
    Reader->Features = CcidReadUlong(ClassDescriptor + 40);
    MaxMessage = CcidReadUlong(ClassDescriptor + 44);
    Reader->MaxMessage = min(max(MaxMessage, CCID_MIN_MESSAGE), CCID_MAX_MESSAGE);
    Reader->Sequence = 0;
    InterlockedExchange(&Reader->InterruptStopping, 0);
    KeInitializeEvent(&Reader->InterruptStopped, NotificationEvent, TRUE);

    Smartcard->Version = SMCLIB_VERSION;
    Smartcard->SmartcardRequest.BufferSize = MIN_BUFFER_SIZE;
    Smartcard->SmartcardReply.BufferSize = MIN_BUFFER_SIZE;
    Smartcard->ReaderExtension = Reader;
    Smartcard->ReaderFunction[RDF_CARD_POWER] = CcidPower;
    Smartcard->ReaderFunction[RDF_TRANSMIT] = CcidTransmit;
    Smartcard->ReaderFunction[RDF_CARD_TRACKING] = Reader->Interrupt ? CcidTrack : NULL;
    Smartcard->ReaderFunction[RDF_SET_PROTOCOL] = CcidSetProtocol;

    Capabilities->SupportedProtocols = CcidReadUlong(ClassDescriptor + 6) & SCARD_PROTOCOL_Tx;
    Capabilities->ReaderType = SCARD_READER_TYPE_USB;
    Capabilities->MechProperties = CcidReadUlong(ClassDescriptor + 36);
    Capabilities->Channel = SCARD_READER_TYPE_USB << 16;
    Capabilities->CLKFrequency.Default = CcidReadUlong(ClassDescriptor + 10);
    Capabilities->CLKFrequency.Max = CcidReadUlong(ClassDescriptor + 14);
    Capabilities->DataRate.Default = CcidReadUlong(ClassDescriptor + 19);
    Capabilities->DataRate.Max = CcidReadUlong(ClassDescriptor + 23);
    Capabilities->MaxIFSD = CcidReadUlong(ClassDescriptor + 28);
    Capabilities->CurrentState = SCARD_UNKNOWN;

    CcidReadString(Device, DeviceDescriptor->iManufacturer, Vendor->VendorName.Buffer, &Vendor->VendorName.Length);
    CcidReadString(Device, DeviceDescriptor->iProduct, Vendor->IfdType.Buffer, &Vendor->IfdType.Length);
    CcidReadString(Device, DeviceDescriptor->iSerialNumber, Vendor->IfdSerialNo.Buffer, &Vendor->IfdSerialNo.Length);
    if (!Vendor->VendorName.Length)
    {
        /* FIXME: a reader without strings is named after its IDs; the names Windows gives such readers are unmeasured. */
        RtlStringCbPrintfA((PCHAR)Vendor->VendorName.Buffer, MAXIMUM_ATTR_STRING_LENGTH, "%04X",
                           DeviceDescriptor->idVendor);
        Vendor->VendorName.Length = 4;
    }
    if (!Vendor->IfdType.Length)
    {
        RtlStringCbPrintfA((PCHAR)Vendor->IfdType.Buffer, MAXIMUM_ATTR_STRING_LENGTH, "%04X",
                           DeviceDescriptor->idProduct);
        Vendor->IfdType.Length = 4;
    }

    Reader->Command = ExAllocatePoolZero(NonPagedPool, Reader->MaxMessage, CCID_TAG);
    Reader->Response = ExAllocatePoolZero(NonPagedPool, Reader->MaxMessage, CCID_TAG);
    if (!Reader->Command || !Reader->Response)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Status = SmartcardInitialize(Smartcard);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Smartcard->OsData->DeviceObject = Device->DeviceObject;
    Device->SmartcardInitialized = TRUE;

    Status = CcidReadSlotStatus(Reader);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBCCID: reading the slot status failed %x\n", Status);
        goto Fail;
    }
    if (Reader->Interrupt)
    {
        Reader->InterruptIrp = IoAllocateIrp(Reader->LowerDevice->StackSize, FALSE);
        if (!Reader->InterruptIrp)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Fail;
        }
        KeClearEvent(&Reader->InterruptStopped);
        CcidSubmitInterrupt(Reader);
    }
    else
    {
        DPRINT1("USBCCID: the reader has no interrupt pipe, card changes go unnoticed\n");
    }
    DPRINT1("USBCCID: '%.*s %.*s' features %08lx protocols %lx message %lu\n", Vendor->VendorName.Length,
            Vendor->VendorName.Buffer, Vendor->IfdType.Length, Vendor->IfdType.Buffer, Reader->Features,
            Capabilities->SupportedProtocols, Reader->MaxMessage);
    return STATUS_SUCCESS;

Fail:
    CcidStopReader(Device);
    CcidReleaseReader(Device);
    return Status;
}

VOID
CcidStopReader(
    _In_ PCCID_DEVICE Device)
{
    PREADER_EXTENSION Reader = &Device->Reader;

    PAGED_CODE();
    if (Reader->InterruptIrp)
    {
        InterlockedExchange(&Reader->InterruptStopping, 1);
        IoCancelIrp(Reader->InterruptIrp);
        KeWaitForSingleObject(&Reader->InterruptStopped, Executive, KernelMode, FALSE, NULL);
        IoFreeIrp(Reader->InterruptIrp);
        Reader->InterruptIrp = NULL;
    }
    if (Device->SmartcardInitialized)
        CcidCompleteTracking(&Device->Smartcard, STATUS_DEVICE_REMOVED);
}

VOID
CcidReleaseReader(
    _In_ PCCID_DEVICE Device)
{
    PREADER_EXTENSION Reader = &Device->Reader;

    PAGED_CODE();
    ASSERT(!Reader->InterruptIrp);
    if (Device->SmartcardInitialized)
    {
        ASSERT(!Device->Smartcard.OsData->NotificationIrp);
        SmartcardExit(&Device->Smartcard);
        Device->SmartcardInitialized = FALSE;
    }
    if (Reader->Command)
        ExFreePoolWithTag(Reader->Command, CCID_TAG);
    if (Reader->Response)
        ExFreePoolWithTag(Reader->Response, CCID_TAG);
    Reader->Command = NULL;
    Reader->Response = NULL;
}
