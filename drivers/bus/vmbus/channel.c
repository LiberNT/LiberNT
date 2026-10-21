/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Channel buffers, GPADLs and ring buffers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vmbus.h"

#define NDEBUG
#include <debug.h>

static
ULONG
VmbusRingFree(
    _In_ ULONG Size,
    _In_ ULONG WriteIndex,
    _In_ ULONG ReadIndex)
{
    if (ReadIndex > WriteIndex)
        return ReadIndex - WriteIndex - 8;

    return Size - (WriteIndex - ReadIndex) - 8;
}

static
ULONG
VmbusRingWrite(
    _In_ PVMBUS_RING Ring,
    _In_ ULONG Offset,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    ULONG First = min(Length, Ring->Size - Offset);

    if (Data != NULL)
    {
        RtlCopyMemory(Ring->Data + Offset, Data, First);
        RtlCopyMemory(Ring->Data, (const UCHAR *)Data + First, Length - First);
    }
    else
    {
        RtlZeroMemory(Ring->Data + Offset, First);
        RtlZeroMemory(Ring->Data, Length - First);
    }

    return (Offset + Length) % Ring->Size;
}

static
VOID
VmbusRingRead(
    _In_ PVMBUS_RING Ring,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length)
{
    ULONG First = min(Length, Ring->Size - Offset);

    RtlCopyMemory(Data, Ring->Data + Offset, First);
    RtlCopyMemory((PUCHAR)Data + First, Ring->Data, Length - First);
}

static
VOID
VmbusRingInitialize(
    _Out_ PVMBUS_RING Ring,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size)
{
    Ring->Control = (PVMBUS_RING_CONTROL)Buffer;
    Ring->Data = Buffer + PAGE_SIZE;
    Ring->Size = Size - PAGE_SIZE;
    Ring->Control->FeatureBits = VMBUS_RING_FEATURE_PENDING_SEND_SIZE;
    KeInitializeSpinLock(&Ring->Lock);
}

PVMBUS_CHANNEL
NTAPI
VmbChannelFromDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PVMBUS_PDO_EXTENSION PdoExtension;

    if (PhysicalDeviceObject == NULL || PhysicalDeviceObject->DriverObject != VmbusDriverObject)
        return NULL;

    PdoExtension = PhysicalDeviceObject->DeviceExtension;
    if (PdoExtension->Common.IsFdo)
        return NULL;

    return PdoExtension->Channel;
}

ULONG
NTAPI
VmbChannelGetMmioMegabytes(
    _In_ PVMBUS_CHANNEL Channel)
{
    return Channel->MmioMegabytes;
}

VOID
NTAPI
VmbChannelGetInterfaceType(
    _In_ PVMBUS_CHANNEL Channel,
    _Out_ GUID *InterfaceType)
{
    *InterfaceType = Channel->InterfaceType;
}

NTSTATUS
NTAPI
VmbChannelCreateGpadl(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Gpadl)
{
    PVMBUS_FDO_EXTENSION Bus = Channel->Bus;
    VMBUS_MSG_GPADL_CREATED Created;
    UCHAR Message[HV_MESSAGE_PAYLOAD_SIZE];
    ULONG Pages = Length >> PAGE_SHIFT;
    ULONG Index;
    ULONG Count;
    ULONG MessageNumber;
    ULONG Id;
    ULONG Value;
    USHORT Value16;
    ULONG64 Pfn;
    NTSTATUS Status;

    *Gpadl = 0;

    if (Pages == 0 || (Length & (PAGE_SIZE - 1)) != 0 || ((ULONG_PTR)Buffer & (PAGE_SIZE - 1)) != 0 ||
        8 + (ULONG64)Pages * sizeof(ULONG64) > MAXUSHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Id = (ULONG)InterlockedIncrement(&Bus->NextGpadl);

    RtlZeroMemory(Message, sizeof(Message));
    Value = VMBUS_MSGTYPE_GPADL_HEADER;
    RtlCopyMemory(&Message[0], &Value, sizeof(Value));
    RtlCopyMemory(&Message[8], &Channel->ChildRelId, sizeof(ULONG));
    RtlCopyMemory(&Message[12], &Id, sizeof(ULONG));
    Value16 = (USHORT)(8 + Pages * sizeof(ULONG64));
    RtlCopyMemory(&Message[16], &Value16, sizeof(Value16));
    Value16 = 1;
    RtlCopyMemory(&Message[18], &Value16, sizeof(Value16));
    RtlCopyMemory(&Message[20], &Length, sizeof(ULONG));

    Count = min(Pages, (ULONG)VMBUS_GPADL_HEADER_PFNS);
    for (Index = 0; Index < Count; Index++)
    {
        Pfn = (ULONG64)MmGetPhysicalAddress((PUCHAR)Buffer + Index * PAGE_SIZE).QuadPart >> PAGE_SHIFT;
        RtlCopyMemory(&Message[VMBUS_GPADL_HEADER_SIZE + 8 + Index * sizeof(ULONG64)], &Pfn, sizeof(Pfn));
    }

    VmbusControlBegin(Bus, VMBUS_MSGTYPE_GPADL_CREATED, Id);

    Status = HvPostMessage(Bus, Message, VMBUS_GPADL_HEADER_SIZE + 8 + Count * sizeof(ULONG64));

    MessageNumber = 0;
    while (NT_SUCCESS(Status) && Index < Pages)
    {
        Count = min(Pages - Index, (ULONG)VMBUS_GPADL_BODY_PFNS);

        RtlZeroMemory(Message, sizeof(Message));
        Value = VMBUS_MSGTYPE_GPADL_BODY;
        RtlCopyMemory(&Message[0], &Value, sizeof(Value));
        RtlCopyMemory(&Message[8], &MessageNumber, sizeof(ULONG));
        RtlCopyMemory(&Message[12], &Id, sizeof(ULONG));

        for (Value = 0; Value < Count; Value++)
        {
            Pfn = (ULONG64)MmGetPhysicalAddress((PUCHAR)Buffer + (Index + Value) * PAGE_SIZE).QuadPart >> PAGE_SHIFT;
            RtlCopyMemory(&Message[VMBUS_GPADL_BODY_SIZE + Value * sizeof(ULONG64)], &Pfn, sizeof(Pfn));
        }

        Status = HvPostMessage(Bus, Message, VMBUS_GPADL_BODY_SIZE + Count * sizeof(ULONG64));
        Index += Count;
        MessageNumber++;
    }

    if (NT_SUCCESS(Status))
        Status = VmbusControlWait(Bus, &Created, sizeof(Created));

    VmbusControlEnd(Bus);

    if (!NT_SUCCESS(Status))
        return Status;

    if (Created.CreationStatus != 0)
    {
        DPRINT1("VMBUS: GPADL %lu for channel %lu failed with 0x%lx\n",
                Id, Channel->ChildRelId, Created.CreationStatus);
        return STATUS_UNSUCCESSFUL;
    }

    *Gpadl = Id;
    return STATUS_SUCCESS;
}

VOID
NTAPI
VmbChannelDestroyGpadl(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ ULONG Gpadl)
{
    VMBUS_MSG_GPADL_TEARDOWN Teardown;
    VMBUS_MSG_GPADL_TORNDOWN Torndown;

    if (Gpadl == 0)
        return;

    RtlZeroMemory(&Teardown, sizeof(Teardown));
    Teardown.Header.MessageType = VMBUS_MSGTYPE_GPADL_TEARDOWN;
    Teardown.ChildRelId = Channel->ChildRelId;
    Teardown.Gpadl = Gpadl;

    VmbusSendControl(Channel->Bus, &Teardown, sizeof(Teardown),
                     VMBUS_MSGTYPE_GPADL_TORNDOWN, Gpadl, &Torndown, sizeof(Torndown));
}

NTSTATUS
NTAPI
VmbChannelOpen(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ ULONG RingBufferSize,
    _In_ PVMBUS_CHANNEL_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PHYSICAL_ADDRESS Highest;
    VMBUS_MSG_OPEN_CHANNEL Open;
    VMBUS_MSG_OPEN_RESULT Result;
    ULONG Size;
    PUCHAR Buffer;
    NTSTATUS Status;

    if (Channel->Open || Channel->Rescinded)
        return STATUS_INVALID_DEVICE_STATE;

    Size = (ULONG)ROUND_TO_PAGES(RingBufferSize);
    if (Size < 2 * PAGE_SIZE)
        Size = 2 * PAGE_SIZE;

    Highest.QuadPart = -1;
    Buffer = MmAllocateContiguousMemory(2 * Size, Highest);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, 2 * Size);
    VmbusRingInitialize(&Channel->Outbound, Buffer, Size);
    VmbusRingInitialize(&Channel->Inbound, Buffer + Size, Size);
    Channel->RingBuffer = Buffer;
    Channel->RingBufferSize = 2 * Size;
    Channel->Callback = Callback;
    Channel->CallbackContext = Context;

    Status = VmbChannelCreateGpadl(Channel, Buffer, 2 * Size, &Channel->RingGpadl);
    if (!NT_SUCCESS(Status))
        goto Failure;

    RtlZeroMemory(&Open, sizeof(Open));
    Open.Header.MessageType = VMBUS_MSGTYPE_OPEN_CHANNEL;
    Open.ChildRelId = Channel->ChildRelId;
    Open.OpenId = Channel->ChildRelId;
    Open.RingBufferGpadl = Channel->RingGpadl;
    Open.TargetVp = Channel->Bus->VpIndex;
    Open.DownstreamRingBufferPageOffset = Size >> PAGE_SHIFT;

    Status = VmbusSendControl(Channel->Bus, &Open, sizeof(Open),
                              VMBUS_MSGTYPE_OPEN_CHANNEL_RESULT, Channel->ChildRelId, &Result, sizeof(Result));
    if (NT_SUCCESS(Status) && Result.Status != 0)
    {
        DPRINT1("VMBUS: opening channel %lu failed with 0x%lx\n", Channel->ChildRelId, Result.Status);
        Status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(Status))
        goto Failure;

    KeMemoryBarrier();
    Channel->Open = TRUE;
    InterlockedExchange(&Channel->KickPending, 1);
    KeInsertQueueDpc(&Channel->Bus->Dpc, NULL, NULL);
    return STATUS_SUCCESS;

Failure:
    VmbChannelDestroyGpadl(Channel, Channel->RingGpadl);
    Channel->RingGpadl = 0;
    Channel->Callback = NULL;
    Channel->CallbackContext = NULL;
    Channel->RingBuffer = NULL;
    MmFreeContiguousMemory(Buffer);
    return Status;
}

VOID
NTAPI
VmbChannelClose(
    _In_ PVMBUS_CHANNEL Channel)
{
    VMBUS_MSG_CHILD_RELID Close;

    if (!Channel->Open)
        return;

    Channel->Open = FALSE;
    KeMemoryBarrier();
    KeFlushQueuedDpcs();

    if (!Channel->Rescinded)
    {
        RtlZeroMemory(&Close, sizeof(Close));
        Close.Header.MessageType = VMBUS_MSGTYPE_CLOSE_CHANNEL;
        Close.ChildRelId = Channel->ChildRelId;
        VmbusSendControl(Channel->Bus, &Close, sizeof(Close), 0, 0, NULL, 0);
        VmbChannelDestroyGpadl(Channel, Channel->RingGpadl);
    }

    Channel->RingGpadl = 0;
    Channel->Callback = NULL;
    Channel->CallbackContext = NULL;
    MmFreeContiguousMemory(Channel->RingBuffer);
    Channel->RingBuffer = NULL;
}

typedef struct _VMBUS_WRITE_PART
{
    const VOID *Buffer;
    ULONG Length;
} VMBUS_WRITE_PART;

static
NTSTATUS
VmbusChannelWrite(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ USHORT Type,
    _In_ USHORT Flags,
    _In_ ULONG64 TransactionId,
    _In_ ULONG HeaderLength,
    _In_reads_(PartCount) const VMBUS_WRITE_PART *Parts,
    _In_ ULONG PartCount)
{
    PVMBUS_RING Ring = &Channel->Outbound;
    VMBUS_PACKET_DESCRIPTOR Descriptor;
    ULONG PacketLength;
    ULONG PayloadLength = 0;
    ULONG WriteIndex;
    ULONG ReadIndex;
    ULONG Offset;
    ULONG Index;
    ULONG64 Footer;
    BOOLEAN Signal;
    KIRQL Irql;

    if (!Channel->Open)
        return STATUS_INVALID_DEVICE_STATE;

    for (Index = 0; Index < PartCount; Index++)
        PayloadLength += Parts[Index].Length;

    PacketLength = ALIGN_UP_BY(sizeof(Descriptor) + PayloadLength, 8);
    if (PacketLength / 8 > MAXUSHORT || (HeaderLength & 7) != 0 || HeaderLength > PayloadLength)
        return STATUS_INVALID_PARAMETER;

    Descriptor.Type = Type;
    Descriptor.DataOffset8 = (USHORT)((sizeof(Descriptor) + HeaderLength) / 8);
    Descriptor.Length8 = (USHORT)(PacketLength / 8);
    Descriptor.Flags = Flags;
    Descriptor.TransactionId = TransactionId;

    KeAcquireSpinLock(&Ring->Lock, &Irql);

    WriteIndex = Ring->Control->WriteIndex;
    ReadIndex = ReadULongAcquire(&Ring->Control->ReadIndex);
    if (WriteIndex >= Ring->Size || ReadIndex >= Ring->Size ||
        VmbusRingFree(Ring->Size, WriteIndex, ReadIndex) < PacketLength + sizeof(Footer))
    {
        KeReleaseSpinLock(&Ring->Lock, Irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Offset = VmbusRingWrite(Ring, WriteIndex, &Descriptor, sizeof(Descriptor));
    for (Index = 0; Index < PartCount; Index++)
        Offset = VmbusRingWrite(Ring, Offset, Parts[Index].Buffer, Parts[Index].Length);
    Offset = VmbusRingWrite(Ring, Offset, NULL, PacketLength - sizeof(Descriptor) - PayloadLength);
    Footer = (ULONG64)WriteIndex << 32;
    Offset = VmbusRingWrite(Ring, Offset, &Footer, sizeof(Footer));

    KeMemoryBarrier();
    Ring->Control->WriteIndex = Offset;
    KeMemoryBarrier();

    Signal = Ring->Control->InterruptMask == 0 && Ring->Control->ReadIndex == WriteIndex;

    KeReleaseSpinLock(&Ring->Lock, Irql);

    if (Signal)
        HvSignalEvent(Channel->ConnectionId);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
VmbChannelSendPacket(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ USHORT Type,
    _In_ USHORT Flags,
    _In_ ULONG64 TransactionId,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    VMBUS_WRITE_PART Part;

    Part.Buffer = Data;
    Part.Length = Length;
    return VmbusChannelWrite(Channel, Type, Flags, TransactionId, 0, &Part, 1);
}

NTSTATUS
NTAPI
VmbChannelSendGpaDirect(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ ULONG64 TransactionId,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ ULONG ByteOffset,
    _In_ ULONG ByteCount,
    _In_reads_(PfnCount) const ULONG64 *Pfns,
    _In_ ULONG PfnCount)
{
    VMBUS_WRITE_PART Parts[4];
    ULONG Header[2];
    ULONG Range[2];

    if (PfnCount == 0 || PfnCount > VMBUS_MAX_GPA_PFNS ||
        ByteOffset >= PAGE_SIZE || ByteCount == 0 ||
        (ULONG64)ByteOffset + ByteCount > (ULONG64)PfnCount * PAGE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Header[0] = 0;
    Header[1] = 1;
    Range[0] = ByteCount;
    Range[1] = ByteOffset;

    Parts[0].Buffer = Header;
    Parts[0].Length = sizeof(Header);
    Parts[1].Buffer = Range;
    Parts[1].Length = sizeof(Range);
    Parts[2].Buffer = Pfns;
    Parts[2].Length = PfnCount * sizeof(ULONG64);
    Parts[3].Buffer = Data;
    Parts[3].Length = Length;

    return VmbusChannelWrite(Channel,
                             VMBUS_PACKET_TYPE_GPA_DIRECT,
                             VMBUS_PACKET_FLAG_COMPLETION_REQUESTED,
                             TransactionId,
                             sizeof(Header) + sizeof(Range) + PfnCount * sizeof(ULONG64),
                             Parts,
                             RTL_NUMBER_OF(Parts));
}

NTSTATUS
NTAPI
VmbChannelReceivePacket(
    _In_ PVMBUS_CHANNEL Channel,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG PacketLength)
{
    PVMBUS_RING Ring = &Channel->Inbound;
    VMBUS_PACKET_DESCRIPTOR Descriptor;
    ULONG WriteIndex;
    ULONG ReadIndex;
    ULONG NewReadIndex;
    ULONG Available;
    ULONG Length;
    ULONG Pending;
    BOOLEAN Signal;
    KIRQL Irql;

    *PacketLength = 0;

    if (!Channel->Open)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Ring->Lock, &Irql);

    ReadIndex = Ring->Control->ReadIndex;
    WriteIndex = ReadULongAcquire(&Ring->Control->WriteIndex);
    if (ReadIndex == WriteIndex)
    {
        KeReleaseSpinLock(&Ring->Lock, Irql);
        return STATUS_NO_MORE_ENTRIES;
    }

    if (ReadIndex >= Ring->Size || WriteIndex >= Ring->Size)
    {
        KeReleaseSpinLock(&Ring->Lock, Irql);
        return STATUS_DATA_ERROR;
    }

    Available = (WriteIndex > ReadIndex) ? WriteIndex - ReadIndex : Ring->Size - ReadIndex + WriteIndex;
    VmbusRingRead(Ring, ReadIndex, &Descriptor, sizeof(Descriptor));
    Length = (ULONG)Descriptor.Length8 * 8;
    if (Length < sizeof(Descriptor) || Length + sizeof(ULONG64) > Available)
    {
        KeReleaseSpinLock(&Ring->Lock, Irql);
        return STATUS_DATA_ERROR;
    }

    *PacketLength = Length;
    if (BufferLength < Length)
    {
        KeReleaseSpinLock(&Ring->Lock, Irql);
        return STATUS_BUFFER_TOO_SMALL;
    }

    VmbusRingRead(Ring, ReadIndex, Buffer, Length);
    NewReadIndex = (ReadIndex + Length + sizeof(ULONG64)) % Ring->Size;

    KeMemoryBarrier();
    Ring->Control->ReadIndex = NewReadIndex;
    KeMemoryBarrier();

    Pending = Ring->Control->PendingSendSize & ~7UL;
    WriteIndex = Ring->Control->WriteIndex;
    Signal = Pending != 0 && WriteIndex < Ring->Size &&
             VmbusRingFree(Ring->Size, WriteIndex, ReadIndex) < Pending &&
             VmbusRingFree(Ring->Size, WriteIndex, NewReadIndex) >= Pending;

    KeReleaseSpinLock(&Ring->Lock, Irql);

    if (Signal)
        HvSignalEvent(Channel->ConnectionId);

    return STATUS_SUCCESS;
}

VOID
VmbusChannelInterrupt(
    _In_ PVMBUS_CHANNEL Channel)
{
    PVMBUS_CHANNEL_CALLBACK Callback;

    if (!Channel->Open)
        return;

    Callback = Channel->Callback;
    if (Callback != NULL)
        Callback(Channel->CallbackContext);
}
