/*
 * PROJECT:     LiberNT Realtek Bluetooth UART transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Three-wire UART (H5) link layer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rtkh5bt.h"

#define NDEBUG
#include <debug.h>

#define RTKH5_SLIP_END      0xC0
#define RTKH5_SLIP_ESC      0xDB
#define RTKH5_SLIP_ESC_END  0xDC
#define RTKH5_SLIP_ESC_ESC  0xDD

static const UCHAR RtkH5Sync[] = { 0x01, 0x7E };
static const UCHAR RtkH5SyncResponse[] = { 0x02, 0x7D };
static const UCHAR RtkH5ConfigHeader[] = { 0x03, 0xFC };
static const UCHAR RtkH5ConfigResponseHeader[] = { 0x04, 0x7B };

ULONGLONG
RtkH5NowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
RtkH5FreeList(
    _Inout_ PLIST_ENTRY List)
{
    while (!IsListEmpty(List))
    {
        PRTKH5_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(List), RTKH5_PACKET, Link);

        ExFreePoolWithTag(Packet, RTKH5_TAG);
    }
}

VOID
RtkH5LinkReset(
    _In_ PRTKH5_DEVICE Dev)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    Dev->LinkState = RtkH5LinkUninitialized;
    Dev->TxSequence = 0;
    Dev->RxSequence = 0;
    Dev->AckPending = FALSE;
    Dev->PeerWindow = 1;
    RtkH5FreeList(&Dev->Unacked);
    Dev->UnackedCount = 0;
    Dev->RxLength = 0;
    Dev->RxEscape = FALSE;
    Dev->RxInFrame = FALSE;
    Dev->NextLinkMessage = 0;
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
}

static
NTSTATUS
RtkH5WriteRaw(
    _In_ PRTKH5_DEVICE Dev,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    WDF_MEMORY_DESCRIPTOR Descriptor;
    WDF_REQUEST_SEND_OPTIONS Options;
    ULONG_PTR Written = 0;
    NTSTATUS Status;

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&Descriptor, Data, Length);
    WDF_REQUEST_SEND_OPTIONS_INIT(&Options, 0);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&Options, WDF_REL_TIMEOUT_IN_MS(1000));
    Status = WdfIoTargetSendWriteSynchronously(Dev->Uart, NULL, &Descriptor, NULL, &Options, &Written);
    if (NT_SUCCESS(Status) && Written != Length)
        Status = STATUS_IO_TIMEOUT;
    return Status;
}

static
ULONG
RtkH5SlipPut(
    _Out_writes_(2) PUCHAR Out,
    _In_ UCHAR Byte)
{
    if (Byte == RTKH5_SLIP_END)
    {
        Out[0] = RTKH5_SLIP_ESC;
        Out[1] = RTKH5_SLIP_ESC_END;
        return 2;
    }
    if (Byte == RTKH5_SLIP_ESC)
    {
        Out[0] = RTKH5_SLIP_ESC;
        Out[1] = RTKH5_SLIP_ESC_ESC;
        return 2;
    }
    Out[0] = Byte;
    return 1;
}

static
NTSTATUS
RtkH5SendFrame(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type,
    _In_ BOOLEAN Reliable,
    _In_ UCHAR Sequence,
    _In_ UCHAR Ack,
    _In_reads_bytes_opt_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    UCHAR Header[4];
    PUCHAR Frame;
    ULONG Index, Used = 0;
    NTSTATUS Status;

    if (Length > RTKH5_MAX_PAYLOAD)
        return STATUS_INVALID_BUFFER_SIZE;

    Header[0] = (UCHAR)((Sequence & 7) | ((Ack & 7) << 3) | (Reliable ? 0x80 : 0));
    Header[1] = (UCHAR)((Type & 0x0F) | ((Length & 0x0F) << 4));
    Header[2] = (UCHAR)(Length >> 4);
    Header[3] = (UCHAR)~(Header[0] + Header[1] + Header[2]);

    Frame = ExAllocatePoolWithTag(NonPagedPool, 2 + 2 * (sizeof(Header) + Length), RTKH5_TAG);
    if (!Frame)
        return STATUS_INSUFFICIENT_RESOURCES;

    Frame[Used++] = RTKH5_SLIP_END;
    for (Index = 0; Index < sizeof(Header); Index++)
        Used += RtkH5SlipPut(Frame + Used, Header[Index]);
    for (Index = 0; Index < Length; Index++)
        Used += RtkH5SlipPut(Frame + Used, Payload[Index]);
    Frame[Used++] = RTKH5_SLIP_END;

    Status = RtkH5WriteRaw(Dev, Frame, Used);
    ExFreePoolWithTag(Frame, RTKH5_TAG);
    return Status;
}

static
NTSTATUS
RtkH5SendUnreliable(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_opt_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    UCHAR Ack;
    NTSTATUS Status;

    KeWaitForSingleObject(&Dev->WriteLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    Ack = Dev->RxSequence;
    if (Type == RTKH5_PKT_ACK)
        Dev->AckPending = FALSE;
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    Status = RtkH5SendFrame(Dev, Type, FALSE, 0, Ack, Payload, Length);
    KeReleaseMutex(&Dev->WriteLock, FALSE);
    return Status;
}

static
NTSTATUS
RtkH5TransmitQueued(
    _In_ PRTKH5_DEVICE Dev)
{
    PRTKH5_PACKET Packet;
    KIRQL OldIrql;
    UCHAR Ack;
    NTSTATUS Status;

    for (;;)
    {
        KeAcquireSpinLock(&Dev->Lock, &OldIrql);
        if (Dev->LinkState != RtkH5LinkActive ||
            Dev->UnackedCount >= Dev->PeerWindow ||
            IsListEmpty(&Dev->TxQueue))
        {
            KeReleaseSpinLock(&Dev->Lock, OldIrql);
            return STATUS_SUCCESS;
        }
        Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->TxQueue), RTKH5_PACKET, Link);
        Packet->Sequence = Dev->TxSequence;
        Dev->TxSequence = (Dev->TxSequence + 1) & 7;
        Ack = Dev->RxSequence;
        Dev->AckPending = FALSE;
        InsertTailList(&Dev->Unacked, &Packet->Link);
        Dev->UnackedCount++;
        Dev->LastTransmit = RtkH5NowMs();
        KeReleaseSpinLock(&Dev->Lock, OldIrql);

        Status = RtkH5SendFrame(Dev, Packet->Type, TRUE, Packet->Sequence, Ack, Packet->Data, Packet->Length);
        if (!NT_SUCCESS(Status))
            return Status;
    }
}

static
NTSTATUS
RtkH5Retransmit(
    _In_ PRTKH5_DEVICE Dev)
{
    PRTKH5_PACKET *Packets;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG Count = 0, Index;
    UCHAR Ack;
    NTSTATUS Status = STATUS_SUCCESS;

    Packets = ExAllocatePoolWithTag(NonPagedPool, sizeof(PRTKH5_PACKET) * 8, RTKH5_TAG);
    if (!Packets)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeWaitForSingleObject(&Dev->WriteLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    for (Entry = Dev->Unacked.Flink; Entry != &Dev->Unacked && Count < 8; Entry = Entry->Flink)
        Packets[Count++] = CONTAINING_RECORD(Entry, RTKH5_PACKET, Link);
    Ack = Dev->RxSequence;
    Dev->AckPending = FALSE;
    Dev->LastTransmit = RtkH5NowMs();
    KeReleaseSpinLock(&Dev->Lock, OldIrql);

    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
        Status = RtkH5SendFrame(Dev, Packets[Index]->Type, TRUE, Packets[Index]->Sequence, Ack,
                                Packets[Index]->Data, Packets[Index]->Length);
    KeReleaseMutex(&Dev->WriteLock, FALSE);
    ExFreePoolWithTag(Packets, RTKH5_TAG);
    return Status;
}

NTSTATUS
RtkH5SendReliable(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PRTKH5_PACKET Packet;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Length > RTKH5_MAX_PAYLOAD)
        return STATUS_INVALID_BUFFER_SIZE;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(RTKH5_PACKET, Data[Length]), RTKH5_TAG);
    if (!Packet)
        return STATUS_INSUFFICIENT_RESOURCES;
    Packet->Type = Type;
    Packet->Sequence = 0;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);

    KeWaitForSingleObject(&Dev->WriteLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    InsertTailList(&Dev->TxQueue, &Packet->Link);
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    Status = RtkH5TransmitQueued(Dev);
    KeReleaseMutex(&Dev->WriteLock, FALSE);
    return Status;
}

static
VOID
RtkH5ProcessAck(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Ack)
{
    LIST_ENTRY Done;
    KIRQL OldIrql;

    InitializeListHead(&Done);
    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    while (!IsListEmpty(&Dev->Unacked))
    {
        PRTKH5_PACKET Oldest = CONTAINING_RECORD(Dev->Unacked.Flink, RTKH5_PACKET, Link);
        UCHAR Distance = (Ack - Oldest->Sequence) & 7;

        if (Distance == 0 || Distance > Dev->UnackedCount)
            break;
        RemoveHeadList(&Dev->Unacked);
        Dev->UnackedCount--;
        InsertTailList(&Done, &Oldest->Link);
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    RtkH5FreeList(&Done);
}

static
VOID
RtkH5QueueReceived(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PRTKH5_PACKET Packet;
    KIRQL OldIrql;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(RTKH5_PACKET, Data[Length]), RTKH5_TAG);
    if (!Packet)
        return;
    Packet->Type = Type;
    Packet->Sequence = 0;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    if (Dev->RxPacketCount >= RTKH5_MAX_QUEUED_PACKETS)
    {
        PRTKH5_PACKET Oldest = CONTAINING_RECORD(RemoveHeadList(&Dev->RxPackets), RTKH5_PACKET, Link);

        Dev->RxPacketCount--;
        ExFreePoolWithTag(Oldest, RTKH5_TAG);
    }
    InsertTailList(&Dev->RxPackets, &Packet->Link);
    Dev->RxPacketCount++;
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    KeSetEvent(&Dev->RxEvent, IO_NO_INCREMENT, FALSE);
}

PRTKH5_PACKET
RtkH5TakePacket(
    _In_ PRTKH5_DEVICE Dev)
{
    PRTKH5_PACKET Packet = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->Lock, &OldIrql);
    if (!IsListEmpty(&Dev->RxPackets))
    {
        Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->RxPackets), RTKH5_PACKET, Link);
        Dev->RxPacketCount--;
    }
    KeReleaseSpinLock(&Dev->Lock, OldIrql);
    return Packet;
}

static
VOID
RtkH5SendConfig(
    _In_ PRTKH5_DEVICE Dev,
    _In_reads_(2) const UCHAR *Header)
{
    UCHAR Message[3];

    Message[0] = Header[0];
    Message[1] = Header[1];
    Message[2] = RTKH5_TX_WINDOW & 7;
    RtkH5SendUnreliable(Dev, RTKH5_PKT_LINK, Message, sizeof(Message));
}

static
VOID
RtkH5ProcessLink(
    _In_ PRTKH5_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    KIRQL OldIrql;

    if (Length < 2)
        return;

    if (RtlEqualMemory(Payload, RtkH5Sync, sizeof(RtkH5Sync)))
    {
        if (Dev->LinkState == RtkH5LinkActive)
            DPRINT1("RTKH5BT: controller restarted its H5 link\n");
        RtkH5SendUnreliable(Dev, RTKH5_PKT_LINK, RtkH5SyncResponse, sizeof(RtkH5SyncResponse));
    }
    else if (RtlEqualMemory(Payload, RtkH5SyncResponse, sizeof(RtkH5SyncResponse)))
    {
        if (Dev->LinkState == RtkH5LinkUninitialized)
        {
            Dev->LinkState = RtkH5LinkInitialized;
            Dev->NextLinkMessage = 0;
        }
    }
    else if (RtlEqualMemory(Payload, RtkH5ConfigHeader, sizeof(RtkH5ConfigHeader)))
    {
        RtkH5SendConfig(Dev, RtkH5ConfigResponseHeader);
    }
    else if (RtlEqualMemory(Payload, RtkH5ConfigResponseHeader, sizeof(RtkH5ConfigResponseHeader)))
    {
        if (Dev->LinkState == RtkH5LinkInitialized)
        {
            UCHAR Window = Length >= 3 ? (Payload[2] & 7) : 1;

            KeAcquireSpinLock(&Dev->Lock, &OldIrql);
            Dev->PeerWindow = (UCHAR)max(1, min(Window, RTKH5_TX_WINDOW));
            Dev->LinkState = RtkH5LinkActive;
            KeReleaseSpinLock(&Dev->Lock, OldIrql);
            DPRINT1("RTKH5BT: H5 link active, window %u\n", Dev->PeerWindow);
        }
    }
}

static
VOID
RtkH5ProcessFrame(
    _In_ PRTKH5_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    UCHAR Sequence, Ack, Type;
    BOOLEAN Reliable, Integrity, Accept = TRUE;
    ULONG PayloadLength;
    KIRQL OldIrql;

    if (Length < 4 || (UCHAR)(Frame[0] + Frame[1] + Frame[2] + Frame[3]) != 0xFF)
        return;

    Sequence = Frame[0] & 7;
    Ack = (Frame[0] >> 3) & 7;
    Integrity = (Frame[0] & 0x40) != 0;
    Reliable = (Frame[0] & 0x80) != 0;
    Type = Frame[1] & 0x0F;
    PayloadLength = (Frame[1] >> 4) | ((ULONG)Frame[2] << 4);
    if (4 + PayloadLength + (Integrity ? 2 : 0) != Length)
        return;

    if (Type == RTKH5_PKT_LINK)
    {
        RtkH5ProcessLink(Dev, Frame + 4, PayloadLength);
        return;
    }
    if (Dev->LinkState != RtkH5LinkActive)
        return;

    RtkH5ProcessAck(Dev, Ack);
    if (Reliable)
    {
        KeAcquireSpinLock(&Dev->Lock, &OldIrql);
        Accept = Sequence == Dev->RxSequence;
        if (Accept)
            Dev->RxSequence = (Dev->RxSequence + 1) & 7;
        Dev->AckPending = TRUE;
        KeReleaseSpinLock(&Dev->Lock, OldIrql);
    }
    if (!Accept)
        return;

    if (Type == RTKH5_PKT_EVENT || Type == RTKH5_PKT_ACL || Type == RTKH5_PKT_SCO)
        RtkH5QueueReceived(Dev, Type, Frame + 4, PayloadLength);
}

static
VOID
RtkH5ProcessBytes(
    _In_ PRTKH5_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        UCHAR Byte = Data[Index];

        if (Byte == RTKH5_SLIP_END)
        {
            if (Dev->RxInFrame && Dev->RxLength)
                RtkH5ProcessFrame(Dev, Dev->RxFrame, Dev->RxLength);
            Dev->RxInFrame = TRUE;
            Dev->RxLength = 0;
            Dev->RxEscape = FALSE;
            continue;
        }
        if (!Dev->RxInFrame)
            continue;
        if (Dev->RxEscape)
        {
            Dev->RxEscape = FALSE;
            if (Byte == RTKH5_SLIP_ESC_END)
                Byte = RTKH5_SLIP_END;
            else if (Byte == RTKH5_SLIP_ESC_ESC)
                Byte = RTKH5_SLIP_ESC;
            else
            {
                Dev->RxInFrame = FALSE;
                continue;
            }
        }
        else if (Byte == RTKH5_SLIP_ESC)
        {
            Dev->RxEscape = TRUE;
            continue;
        }
        if (Dev->RxLength >= sizeof(Dev->RxFrame))
        {
            Dev->RxInFrame = FALSE;
            continue;
        }
        Dev->RxFrame[Dev->RxLength++] = Byte;
    }
}

NTSTATUS
RtkH5Pump(
    _In_ PRTKH5_DEVICE Dev)
{
    WDF_MEMORY_DESCRIPTOR Descriptor;
    WDF_REQUEST_SEND_OPTIONS Options;
    UCHAR Buffer[512];
    ULONG_PTR Read = 0;
    ULONGLONG Now;
    NTSTATUS Status;

    Now = RtkH5NowMs();
    if (Dev->LinkState != RtkH5LinkActive && Now >= Dev->NextLinkMessage)
    {
        Dev->NextLinkMessage = Now + RTKH5_LINK_RETRY_MS;
        if (Dev->LinkState == RtkH5LinkUninitialized)
            RtkH5SendUnreliable(Dev, RTKH5_PKT_LINK, RtkH5Sync, sizeof(RtkH5Sync));
        else
            RtkH5SendConfig(Dev, RtkH5ConfigHeader);
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&Descriptor, Buffer, sizeof(Buffer));
    WDF_REQUEST_SEND_OPTIONS_INIT(&Options, 0);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&Options, WDF_REL_TIMEOUT_IN_MS(1000));
    Status = WdfIoTargetSendReadSynchronously(Dev->Uart, NULL, &Descriptor, NULL, &Options, &Read);
    if (NT_SUCCESS(Status) && Read)
        RtkH5ProcessBytes(Dev, Buffer, (ULONG)Read);
    else if (Status == STATUS_TIMEOUT || Status == STATUS_IO_TIMEOUT)
        Status = STATUS_SUCCESS;

    if (Dev->AckPending)
        RtkH5SendUnreliable(Dev, RTKH5_PKT_ACK, NULL, 0);

    KeWaitForSingleObject(&Dev->WriteLock, Executive, KernelMode, FALSE, NULL);
    RtkH5TransmitQueued(Dev);
    KeReleaseMutex(&Dev->WriteLock, FALSE);

    if (Dev->UnackedCount && RtkH5NowMs() - Dev->LastTransmit >= RTKH5_RETRANSMIT_MS)
        RtkH5Retransmit(Dev);

    if (Dev->Ready)
        RtkH5DeliverPackets(Dev);
    return Status;
}

NTSTATUS
RtkH5WaitLink(
    _In_ PRTKH5_DEVICE Dev)
{
    ULONGLONG Deadline = RtkH5NowMs() + RTKH5_LINK_TIMEOUT_MS;

    while (Dev->LinkState != RtkH5LinkActive)
    {
        if (Dev->StopThread)
            return STATUS_CANCELLED;
        if (RtkH5NowMs() >= Deadline)
            return STATUS_IO_TIMEOUT;
        RtkH5Pump(Dev);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RtkH5HciCommand(
    _In_ PRTKH5_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(ParameterLength) const UCHAR *Parameters,
    _In_ UCHAR ParameterLength,
    _Out_writes_bytes_to_opt_(ResultSize, *ResultLength) PUCHAR Result,
    _In_ ULONG ResultSize,
    _Out_opt_ PULONG ResultLength)
{
    UCHAR Command[3 + 255];
    ULONGLONG Deadline;
    NTSTATUS Status;

    if (ResultLength)
        *ResultLength = 0;
    Command[0] = (UCHAR)Opcode;
    Command[1] = (UCHAR)(Opcode >> 8);
    Command[2] = ParameterLength;
    if (ParameterLength)
        RtlCopyMemory(Command + 3, Parameters, ParameterLength);

    Status = RtkH5SendReliable(Dev, RTKH5_PKT_COMMAND, Command, 3 + ParameterLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Deadline = RtkH5NowMs() + RTKH5_COMMAND_TIMEOUT_MS;
    while (RtkH5NowMs() < Deadline && !Dev->StopThread)
    {
        PRTKH5_PACKET Packet;

        RtkH5Pump(Dev);
        while ((Packet = RtkH5TakePacket(Dev)) != NULL)
        {
            const UCHAR *Event = Packet->Data;

            if (Packet->Type == RTKH5_PKT_EVENT && Packet->Length >= 6 && Event[0] == 0x0E &&
                (Event[3] | (Event[4] << 8)) == Opcode)
            {
                ULONG Length = min(ResultSize, Packet->Length - 5);

                if (Result && Length)
                    RtlCopyMemory(Result, Event + 5, Length);
                if (ResultLength)
                    *ResultLength = Length;
                Status = Event[5] == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                ExFreePoolWithTag(Packet, RTKH5_TAG);
                return Status;
            }
            if (Packet->Type == RTKH5_PKT_EVENT && Packet->Length >= 6 && Event[0] == 0x0F &&
                (Event[4] | (Event[5] << 8)) == Opcode && Event[2] != 0)
            {
                Status = STATUS_UNSUCCESSFUL;
                ExFreePoolWithTag(Packet, RTKH5_TAG);
                return Status;
            }
            ExFreePoolWithTag(Packet, RTKH5_TAG);
        }
    }
    return Dev->StopThread ? STATUS_CANCELLED : STATUS_IO_TIMEOUT;
}
