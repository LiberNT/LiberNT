/*
 * PROJECT:     LiberNT Apple AOP Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AFK endpoints, rings and EPIC services of the Apple Always-On Processor
 * COPYRIGHT:   Copyright (C) The Asahi Linux Contributors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleaop.h"

#define AOP_FOURCC(a, b, c, d) (((ULONG)(UCHAR)(a) << 24) | ((ULONG)(UCHAR)(b) << 16) | \
                                ((ULONG)(UCHAR)(c) << 8) | (ULONG)(UCHAR)(d))

#define AFK_MAX_MESSAGES_PER_RECV       256
#define AFK_MAX_LOGGED_ERRORS           16
#define AFK_SHUTDOWN_TIMEOUT_MS         200
#define AOP_MAX_BOOTARGS                0x1000

static
ULONG
AfkAlignUp(
    _In_ ULONG Value,
    _In_ ULONG Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

static
BOOLEAN
AfkInShared(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return Endpoint->Shared && Offset <= Endpoint->Shared->Size && Length <= Endpoint->Shared->Size - Offset;
}

static
ULONG
AfkRead32(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Offset)
{
    ASSERT(AfkInShared(Endpoint, Offset, sizeof(ULONG)) && !(Offset & 3));
    return *(volatile ULONG *)((PUCHAR)Endpoint->Shared->Virtual + Offset);
}

static
VOID
AfkWrite32(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ASSERT(AfkInShared(Endpoint, Offset, sizeof(ULONG)) && !(Offset & 3));
    *(volatile ULONG *)((PUCHAR)Endpoint->Shared->Virtual + Offset) = Value;
}

static
VOID
AfkCopyFrom(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    volatile const UCHAR *Source;
    PUCHAR Target = Buffer;
    ULONG Index;

    ASSERT(AfkInShared(Endpoint, Offset, Length));
    Source = (volatile const UCHAR *)Endpoint->Shared->Virtual + Offset;
    for (Index = 0; Index < Length; ++Index)
        Target[Index] = Source[Index];
}

static
VOID
AfkCopyTo(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    volatile UCHAR *Target;
    const UCHAR *Source = Buffer;
    ULONG Index;

    ASSERT(AfkInShared(Endpoint, Offset, Length));
    Target = (volatile UCHAR *)Endpoint->Shared->Virtual + Offset;
    for (Index = 0; Index < Length; ++Index)
        Target[Index] = Source[Index];
}

static
BOOLEAN
AfkSend(
    _Inout_ PAOP_DEVICE Device,
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG64 Opcode,
    _In_ ULONG64 Payload)
{
    ASSERT(Device->LockOwner == KeGetCurrentThread());
    ASSERT(Payload < (1ULL << AFK_OPCODE_SHIFT));
    return AppleRtkitSend(&Device->Rtkit, Endpoint->Index, (Opcode << AFK_OPCODE_SHIFT) | Payload);
}

static
BOOLEAN
AfkLogError(
    _Inout_ PAFK_ENDPOINT Endpoint)
{
    return Endpoint->Errors++ < AFK_MAX_LOGGED_ERRORS;
}

VOID
AopPatchBootArguments(
    _Inout_ PAOP_DEVICE Device)
{
    const struct
    {
        ULONG Key;
        ULONG64 Value;
    } Patches[] =
    {
        { AOP_FOURCC('E', 'C', '0', 'p'), Device->Config->Ec0p },
        { AOP_FOURCC('n', 'C', 'a', 'l'), 0 },
        { AOP_FOURCC('a', 'l', 'i', 'g'), Device->Config->Alig },
        { AOP_FOURCC('A', 'O', 'P', 't'), Device->Config->Aopt },
    };
    ULONG Offset, Size, Index, Key, Length, Patch, Patched = 0;
    ULONG64 Old, New;
    PUCHAR Arguments;

    ASSERT(Device->Aop && Device->AopSize >= AOP_BOOTARGS_SIZE + sizeof(ULONG));
    Offset = READ_REGISTER_ULONG((volatile ULONG *)(Device->Aop + AOP_BOOTARGS_OFFSET));
    Size = READ_REGISTER_ULONG((volatile ULONG *)(Device->Aop + AOP_BOOTARGS_SIZE));
    if (!Size || Size > AOP_MAX_BOOTARGS || Offset >= Device->AopSize || Size > Device->AopSize - Offset)
    {
        DbgPrint("appleaop: boot arguments at %lx+%lx are outside the AOP memory, left as they are\n", Offset, Size);
        return;
    }
    Arguments = ExAllocatePoolWithTag(NonPagedPool, Size, AOP_TAG);
    if (!Arguments)
        return;
    for (Index = 0; Index < Size; ++Index)
        Arguments[Index] = READ_REGISTER_UCHAR(Device->Aop + Offset + Index);

    for (Index = 0; Index + 2 * sizeof(ULONG) <= Size; Index += Length)
    {
        RtlCopyMemory(&Key, Arguments + Index, sizeof(Key));
        RtlCopyMemory(&Length, Arguments + Index + sizeof(ULONG), sizeof(Length));
        Index += 2 * sizeof(ULONG);
        if (Length > Size - Index)
        {
            DbgPrint("appleaop: boot argument %08lx at %lx claims %lu bytes, %lu are left\n", Key, Index, Length,
                     Size - Index);
            break;
        }
        for (Patch = 0; Patch < RTL_NUMBER_OF(Patches); ++Patch)
        {
            if (Patches[Patch].Key != Key)
                continue;
            if (Length > sizeof(ULONG64))
            {
                DbgPrint("appleaop: boot argument %c%c%c%c has %lu bytes, not patched\n", (CHAR)(Key >> 24),
                         (CHAR)(Key >> 16), (CHAR)(Key >> 8), (CHAR)Key, Length);
                break;
            }
            Old = 0;
            New = Patches[Patch].Value;
            RtlCopyMemory(&Old, Arguments + Index, Length);
            RtlCopyMemory(Arguments + Index, &New, Length);
            DbgPrint("appleaop: boot argument %c%c%c%c (%lu bytes) %I64x -> %I64x\n", (CHAR)(Key >> 24),
                     (CHAR)(Key >> 16), (CHAR)(Key >> 8), (CHAR)Key, Length, Old, New);
            Patched++;
            break;
        }
    }
    for (Index = 0; Index < Size; ++Index)
        WRITE_REGISTER_UCHAR(Device->Aop + Offset + Index, Arguments[Index]);
    KeMemoryBarrier();
    ExFreePoolWithTag(Arguments, AOP_TAG);
    DbgPrint("appleaop: boot arguments at %lx+%lx, %lu of %lu patched\n", Offset, Size, Patched,
             (ULONG)RTL_NUMBER_OF(Patches));
}

VOID
AfkStartEndpoints(
    _Inout_ PAOP_DEVICE Device)
{
    PAFK_ENDPOINT Endpoint;
    ULONG Index, Started = 0;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    for (Index = 0; Index < AFK_ENDPOINT_COUNT; ++Index)
    {
        Endpoint = &Device->Endpoints[Index];
        ASSERT(Endpoint->Index == AFK_ENDPOINT_START + Index);
        Endpoint->Present = AppleRtkitHasEndpoint(&Device->Rtkit, Endpoint->Index);
        if (!Endpoint->Present)
            continue;
        AppleRtkitStartEndpoint(&Device->Rtkit, Endpoint->Index);
        if (AfkSend(Device, Endpoint, AFK_OPC_INIT, 0))
            Started++;
    }
    DbgPrint("appleaop: %lu AFK endpoints asked to start (map %08lx)\n", Started, Device->Rtkit.Endpoints[1]);
}

VOID
AfkStopEndpoints(
    _Inout_ PAOP_DEVICE Device)
{
    PAFK_ENDPOINT Endpoint;
    ULONG64 Deadline;
    ULONG Index;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    for (Index = 0; Index < AFK_ENDPOINT_COUNT; ++Index)
    {
        Endpoint = &Device->Endpoints[Index];
        if (!Endpoint->Present || !Endpoint->StartSent || Device->Rtkit.Failed)
            continue;
        Endpoint->ShutdownAcked = FALSE;
        if (!AfkSend(Device, Endpoint, AFK_OPC_SHUTDOWN, 0))
            continue;
        Deadline = AopNowMs() + AFK_SHUTDOWN_TIMEOUT_MS;
        while (!Endpoint->ShutdownAcked && !Device->Rtkit.Failed && AopNowMs() <= Deadline)
        {
            if (!AppleRtkitPoll(&Device->Rtkit))
                KeStallExecutionProcessor(100);
        }
        if (!Endpoint->ShutdownAcked)
            DbgPrint("appleaop: AFK endpoint %#x did not acknowledge its shutdown\n", Endpoint->Index);
    }
}

static
BOOLEAN
AfkParseRing(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG64 Message,
    _Out_ PAFK_RING Ring)
{
    ULONG Size = (ULONG)((Message >> 16) & 0xffff) * AFK_RB_BLOCK_STEP;
    ULONG Offset = (ULONG)((Message >> 32) & 0xffff) * AFK_RB_BLOCK_STEP;
    ULONG BufferSize;

    RtlZeroMemory(Ring, sizeof(*Ring));
    if (!Size || !AfkInShared(Endpoint, Offset, Size))
        return FALSE;
    BufferSize = AfkRead32(Endpoint, Offset);
    if (BufferSize >= Size || (Size - BufferSize) / 3 < sizeof(AFK_QE_HEADER) || BufferSize < sizeof(AFK_QE_HEADER))
        return FALSE;
    Ring->Offset = Offset;
    Ring->BufferSize = BufferSize;
    Ring->BlockSize = (Size - BufferSize) / 3;
    if (Ring->BlockSize & (Ring->BlockSize - 1))
        return FALSE;
    Ring->Valid = TRUE;
    return TRUE;
}

static
LONG
AopFindService(
    _In_ PAOP_DEVICE Device,
    _In_ UCHAR Endpoint,
    _In_ ULONG Channel)
{
    ULONG Index;

    for (Index = 0; Index < Device->ServiceCount; ++Index)
    {
        if (Device->Services[Index].Endpoint == Endpoint && Device->Services[Index].Channel == Channel)
            return (LONG)Index;
    }
    return -1;
}

static
VOID
AopRegisterService(
    _Inout_ PAOP_DEVICE Device,
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ const EPIC_SERVICE_ANNOUNCE *Announce)
{
    PAOP_SERVICE Service;
    ULONG Length;

    if (AopFindService(Device, Endpoint->Index, Announce->Channel) >= 0)
        return;
    if (Device->ServiceCount == AOP_MAX_SERVICES)
    {
        DbgPrint("appleaop: no room for the service on endpoint %#x channel %lu\n", Endpoint->Index,
                 Announce->Channel);
        return;
    }
    Service = &Device->Services[Device->ServiceCount];
    RtlZeroMemory(Service, sizeof(*Service));
    for (Length = 0; Length < AOP_SERVICE_NAME && Announce->Name[Length]; ++Length)
        Service->Name[Length] = (Announce->Name[Length] >= 0x20 && Announce->Name[Length] < 0x7f) ? Announce->Name[Length]
                                                                                                    : '?';
    Service->Name[Length] = '\0';
    Service->Endpoint = Endpoint->Index;
    Service->Channel = Announce->Channel;
    KeMemoryBarrier();
    Device->ServiceCount++;
    DbgPrint("appleaop: service %s on endpoint %#x channel %lu (code %lx)\n", Service->Name, Service->Endpoint,
             Service->Channel, Announce->ReturnCode);
    InterlockedExchange(&Device->ServicesPending, 1);
}

static
VOID
AfkCompleteCall(
    _Inout_ PAFK_ENDPOINT Endpoint,
    _In_ const EPIC_HEADER *Epic,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    PEPIC_CALL Call;
    ULONG ReturnCode;

    if (Length < sizeof(ULONG))
    {
        if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x reply with tag %u is too short\n", Endpoint->Index, Epic->Tag);
        return;
    }
    if (!Epic->Tag || Epic->Tag > EPIC_MAX_CALLS || !Endpoint->Calls[Epic->Tag - 1].InUse)
    {
        if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x reply with unknown tag %u\n", Endpoint->Index, Epic->Tag);
        return;
    }
    RtlCopyMemory(&ReturnCode, Payload, sizeof(ReturnCode));
    Call = &Endpoint->Calls[Epic->Tag - 1];
    if (Call->Abandoned)
    {
        DbgPrint("appleaop: endpoint %#x answered abandoned call %u late with %lx\n", Endpoint->Index, Epic->Tag,
                 ReturnCode);
        RtlZeroMemory(Call, sizeof(*Call));
        return;
    }
    Call->ReturnCode = ReturnCode;
    Call->Returned = min(Length - (ULONG)sizeof(ULONG), Call->ReplyLength);
    if (Call->Returned)
        RtlCopyMemory(Call->Reply, Payload + sizeof(ULONG), Call->Returned);
    Call->Done = TRUE;
}

static
VOID
AfkHandleMessage(
    _Inout_ PAOP_DEVICE Device,
    _Inout_ PAFK_ENDPOINT Endpoint,
    _In_ const AFK_QE_HEADER *Queue,
    _In_reads_bytes_(Length) const UCHAR *Message,
    _In_ ULONG Length)
{
    EPIC_HEADER Epic;
    EPIC_SERVICE_ANNOUNCE Announce;
    const UCHAR *Payload;
    ULONG PayloadLength;
    LONG Service;

    if (Length < sizeof(Epic))
    {
        if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x message of %lu bytes has no EPIC header\n", Endpoint->Index, Length);
        return;
    }
    RtlCopyMemory(&Epic, Message, sizeof(Epic));
    Payload = Message + sizeof(Epic);
    PayloadLength = Length - sizeof(Epic);

    if (Epic.Category == EPIC_CATEGORY_REPORT && Epic.Subtype == EPIC_SUBTYPE_STD_SERVICE)
    {
        if (PayloadLength < sizeof(Announce))
        {
            if (AfkLogError(Endpoint))
                DbgPrint("appleaop: endpoint %#x service announce of %lu bytes\n", Endpoint->Index, PayloadLength);
            return;
        }
        RtlCopyMemory(&Announce, Payload, sizeof(Announce));
        AopRegisterService(Device, Endpoint, &Announce);
        return;
    }
    if (Epic.Category == EPIC_CATEGORY_REPORT && Epic.Subtype == EPIC_SUBTYPE_FAKEHID_REPORT)
    {
        Service = AopFindService(Device, Endpoint->Index, Queue->Channel);
        if (Service >= 0)
            AopSensorReport(Device, (ULONG)Service, Payload, PayloadLength);
        else if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x report on unannounced channel %lu\n", Endpoint->Index, Queue->Channel);
        return;
    }
    if (Epic.Category == EPIC_CATEGORY_REPLY &&
        (Epic.Subtype == EPIC_SUBTYPE_RETCODE_PAYLOAD || Epic.Subtype == EPIC_SUBTYPE_RETCODE ||
         Epic.Subtype == EPIC_SUBTYPE_STRING))
    {
        AfkCompleteCall(Endpoint, &Epic, Payload, PayloadLength);
        return;
    }
    if (AfkLogError(Endpoint))
    {
        DbgPrint("appleaop: endpoint %#x unexpected EPIC category %02x subtype %04x on channel %lu\n", Endpoint->Index,
                 Epic.Category, Epic.Subtype, Queue->Channel);
    }
}

static
BOOLEAN
AfkReadQueueHeader(
    _In_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Base,
    _In_ ULONG Pointer,
    _Out_ PAFK_QE_HEADER Queue)
{
    AfkCopyFrom(Endpoint, Base + Pointer, Queue, sizeof(*Queue));
    if (Queue->Magic == AFK_QE_MAGIC_INPUT || Queue->Magic == AFK_QE_MAGIC_OUTPUT)
        return TRUE;
    if (AfkLogError(Endpoint))
        DbgPrint("appleaop: endpoint %#x queue entry at %lx has magic %08lx\n", Endpoint->Index, Pointer, Queue->Magic);
    return FALSE;
}

static
VOID
AfkReadRing(
    _Inout_ PAOP_DEVICE Device,
    _Inout_ PAFK_ENDPOINT Endpoint)
{
    PAFK_RING Ring = &Endpoint->Rx;
    AFK_QE_HEADER Queue;
    ULONG Base, Read, Write, Guard;
    PUCHAR Message;

    if (!Ring->Valid)
    {
        if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x has data but no receive ring\n", Endpoint->Index);
        return;
    }
    Base = Ring->Offset + 3 * Ring->BlockSize;
    Read = AfkRead32(Endpoint, Ring->Offset + Ring->BlockSize);
    Write = AfkRead32(Endpoint, Ring->Offset + 2 * Ring->BlockSize);
    KeMemoryBarrier();
    for (Guard = 0; Write != Read && Guard < AFK_MAX_MESSAGES_PER_RECV; ++Guard)
    {
        if (Read >= Ring->BufferSize || Write >= Ring->BufferSize || (Read & 3))
        {
            if (AfkLogError(Endpoint))
                DbgPrint("appleaop: endpoint %#x receive pointers %lx/%lx are outside %lx\n", Endpoint->Index, Read,
                         Write, Ring->BufferSize);
            return;
        }
        if (Ring->BufferSize - Read < sizeof(Queue))
        {
            Read = 0;
            if (Read == Write)
            {
                AfkWrite32(Endpoint, Ring->Offset + Ring->BlockSize, Read);
                break;
            }
        }
        if (!AfkReadQueueHeader(Endpoint, Base, Read, &Queue))
            return;
        if (Queue.Size > Ring->BufferSize - Read - sizeof(Queue))
        {
            Read = 0;
            if (!AfkReadQueueHeader(Endpoint, Base, Read, &Queue))
                return;
        }
        if (Queue.Size > Ring->BufferSize - sizeof(Queue))
        {
            if (AfkLogError(Endpoint))
                DbgPrint("appleaop: endpoint %#x queue entry of %lu bytes exceeds the ring\n", Endpoint->Index,
                         Queue.Size);
            return;
        }
        Message = Queue.Size ? ExAllocatePoolWithTag(NonPagedPool, Queue.Size, AOP_TAG) : NULL;
        if (Message)
        {
            AfkCopyFrom(Endpoint, Base + Read + sizeof(Queue), Message, Queue.Size);
            AfkHandleMessage(Device, Endpoint, &Queue, Message, Queue.Size);
            ExFreePoolWithTag(Message, AOP_TAG);
        }
        else if (Queue.Size && AfkLogError(Endpoint))
        {
            DbgPrint("appleaop: endpoint %#x dropped a %lu byte message\n", Endpoint->Index, Queue.Size);
        }
        Read = AfkAlignUp(Read + sizeof(Queue) + Queue.Size, Ring->BlockSize) % Ring->BufferSize;
        KeMemoryBarrier();
        AfkWrite32(Endpoint, Ring->Offset + Ring->BlockSize, Read);
        Write = AfkRead32(Endpoint, Ring->Offset + 2 * Ring->BlockSize);
        KeMemoryBarrier();
    }
}

static
NTSTATUS
AfkSendRing(
    _Inout_ PAOP_DEVICE Device,
    _Inout_ PAFK_ENDPOINT Endpoint,
    _In_ ULONG Channel,
    _In_ ULONG Type,
    _In_reads_bytes_(HeaderLength) const VOID *Header,
    _In_ ULONG HeaderLength,
    _In_reads_bytes_(DataLength) const VOID *Data,
    _In_ ULONG DataLength)
{
    PAFK_RING Ring = &Endpoint->Tx;
    AFK_QE_HEADER Queue;
    ULONG Base, Read, Write, Need, End, Next;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    if (!Ring->Valid)
        return STATUS_DEVICE_NOT_READY;
    if (DataLength > Ring->BufferSize || HeaderLength > Ring->BufferSize - DataLength ||
        sizeof(Queue) + HeaderLength + DataLength > Ring->BufferSize)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    Base = Ring->Offset + 3 * Ring->BlockSize;
    KeMemoryBarrier();
    Read = AfkRead32(Endpoint, Ring->Offset + Ring->BlockSize);
    Write = AfkRead32(Endpoint, Ring->Offset + 2 * Ring->BlockSize);
    if (Read >= Ring->BufferSize || Write >= Ring->BufferSize)
    {
        DbgPrint("appleaop: endpoint %#x transmit pointers %lx/%lx are outside %lx\n", Endpoint->Index, Read, Write,
                 Ring->BufferSize);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    Need = sizeof(Queue) + HeaderLength + DataLength;
    Queue.Magic = AFK_QE_MAGIC_INPUT;
    Queue.Size = HeaderLength + DataLength;
    Queue.Channel = Channel;
    Queue.Type = Type;

    End = AfkAlignUp(Write + Need, Ring->BlockSize);
    Next = (End >= Ring->BufferSize) ? End - Ring->BufferSize : End;
    if (Write >= Read && Need <= Ring->BufferSize - Write && (End < Ring->BufferSize || Next < Read))
    {
        ASSERT(Next < Ring->BufferSize);
    }
    else if (Write < Read && End < Read)
    {
        ASSERT(Next == End);
    }
    else if (Write >= Read && Need > Ring->BufferSize - Write && AfkAlignUp(Need, Ring->BlockSize) < Read)
    {
        if (Ring->BufferSize - Write >= sizeof(Queue))
            AfkCopyTo(Endpoint, Base + Write, &Queue, sizeof(Queue));
        Write = 0;
        Next = AfkAlignUp(Need, Ring->BlockSize);
    }
    else
    {
        DbgPrint("appleaop: endpoint %#x transmit ring is full (%lx/%lx, %lu bytes)\n", Endpoint->Index, Read, Write,
                 Need);
        return STATUS_DEVICE_BUSY;
    }
    AfkCopyTo(Endpoint, Base + Write, &Queue, sizeof(Queue));
    AfkCopyTo(Endpoint, Base + Write + sizeof(Queue), Header, HeaderLength);
    if (DataLength)
        AfkCopyTo(Endpoint, Base + Write + sizeof(Queue) + HeaderLength, Data, DataLength);
    KeMemoryBarrier();
    AfkWrite32(Endpoint, Ring->Offset + 2 * Ring->BlockSize, Next);
    KeMemoryBarrier();
    return AfkSend(Device, Endpoint, AFK_OPC_SEND, Next) ? STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED;
}

static
VOID
AfkRingReady(
    _Inout_ PAOP_DEVICE Device,
    _Inout_ PAFK_ENDPOINT Endpoint,
    _In_ BOOLEAN Receive,
    _In_ ULONG64 Message)
{
    PAFK_RING Ring = Receive ? &Endpoint->Rx : &Endpoint->Tx;

    if (Ring->Valid)
    {
        if (AfkLogError(Endpoint))
            DbgPrint("appleaop: endpoint %#x sent a second %s ring\n", Endpoint->Index, Receive ? "receive" : "transmit");
        return;
    }
    if (!AfkParseRing(Endpoint, Message, Ring))
    {
        DbgPrint("appleaop: endpoint %#x %s ring %I64x does not fit its %lu byte buffer\n", Endpoint->Index,
                 Receive ? "receive" : "transmit", Message, Endpoint->Shared ? Endpoint->Shared->Size : 0);
        return;
    }
    DbgPrint("appleaop: endpoint %#x %s ring at %lx, %lu bytes in %lu byte blocks\n", Endpoint->Index,
             Receive ? "receive" : "transmit", Ring->Offset, Ring->BufferSize, Ring->BlockSize);
    if (Endpoint->Rx.Valid && Endpoint->Tx.Valid && !Endpoint->StartSent)
        Endpoint->StartSent = AfkSend(Device, Endpoint, AFK_OPC_START, 0);
}

VOID
NTAPI
AfkReceive(
    _In_ PVOID Context,
    _In_ UCHAR Number,
    _In_ ULONG64 Message)
{
    PAOP_DEVICE Device = Context;
    PAFK_ENDPOINT Endpoint;
    ULONG64 Opcode = Message >> AFK_OPCODE_SHIFT;
    ULONG Size;

    ASSERT(Device->LockOwner == KeGetCurrentThread());
    if (Number < AFK_ENDPOINT_START || Number >= AFK_ENDPOINT_START + AFK_ENDPOINT_COUNT)
    {
        DbgPrint("appleaop: message %I64x on endpoint %#x outside the AFK range\n", Message, Number);
        return;
    }
    Endpoint = &Device->Endpoints[Number - AFK_ENDPOINT_START];
    ASSERT(Endpoint->Index == Number);
    Endpoint->Messages++;
    switch (Opcode)
    {
        case AFK_OPC_INIT:
            AfkSend(Device, Endpoint, AFK_OPC_INIT_ACK, 0);
            break;

        case AFK_OPC_GET_BUF:
            Size = (ULONG)((Message >> 16) & 0xffff) * AFK_RB_BLOCK_STEP;
            if (Endpoint->Shared)
            {
                DbgPrint("appleaop: endpoint %#x asked for a second shared buffer\n", Number);
                break;
            }
            Endpoint->Shared = Size ? AopAllocateShared(Device, Size) : NULL;
            if (!Endpoint->Shared || Endpoint->Shared->Iova >= (1ULL << AFK_OPCODE_SHIFT))
            {
                DbgPrint("appleaop: endpoint %#x shared buffer of %lu bytes not provided\n", Number, Size);
                Endpoint->Shared = NULL;
                break;
            }
            DbgPrint("appleaop: endpoint %#x shared buffer of %lu bytes at device address %I64x\n", Number, Size,
                     Endpoint->Shared->Iova);
            AfkSend(Device, Endpoint, AFK_OPC_GET_BUF_ACK, Endpoint->Shared->Iova);
            break;

        case AFK_OPC_INIT_UNK:
            break;

        case AFK_OPC_START_ACK:
            Endpoint->Started = TRUE;
            DbgPrint("appleaop: AFK endpoint %#x is running\n", Number);
            break;

        case AFK_OPC_INIT_RX:
            AfkRingReady(Device, Endpoint, TRUE, Message);
            break;

        case AFK_OPC_INIT_TX:
            AfkRingReady(Device, Endpoint, FALSE, Message);
            break;

        case AFK_OPC_RECV:
            AfkReadRing(Device, Endpoint);
            break;

        case AFK_OPC_SHUTDOWN_ACK:
            Endpoint->ShutdownAcked = TRUE;
            break;

        default:
            if (AfkLogError(Endpoint))
                DbgPrint("appleaop: endpoint %#x unknown message %I64x\n", Number, Message);
            break;
    }
}

NTSTATUS
AopEpicCall(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_ USHORT Subtype,
    _In_reads_bytes_(RequestLength) const VOID *Request,
    _In_ ULONG RequestLength,
    _Out_writes_bytes_opt_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _Out_opt_ PULONG Returned,
    _Out_ PULONG ReturnCode)
{
    PAOP_SERVICE Entry;
    PAFK_ENDPOINT Endpoint;
    PEPIC_CALL Call = NULL;
    EPIC_HEADER Epic;
    ULONG64 Deadline;
    ULONG Tag;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Reply || !ReplyLength);
    *ReturnCode = 0;
    if (Returned)
        *Returned = 0;
    AopLock(Device);
    if (!Device->Running || Device->Rtkit.Failed || Service >= Device->ServiceCount)
    {
        AopUnlock(Device);
        return Device->Rtkit.Failed ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_DEVICE_NOT_READY;
    }
    Entry = &Device->Services[Service];
    ASSERT(Entry->Endpoint >= AFK_ENDPOINT_START && Entry->Endpoint < AFK_ENDPOINT_START + AFK_ENDPOINT_COUNT);
    Endpoint = &Device->Endpoints[Entry->Endpoint - AFK_ENDPOINT_START];
    for (Tag = 0; Tag < EPIC_MAX_CALLS; ++Tag)
    {
        if (!Endpoint->Calls[Tag].InUse)
        {
            Call = &Endpoint->Calls[Tag];
            break;
        }
    }
    if (!Call)
    {
        AopUnlock(Device);
        DbgPrint("appleaop: too many calls in flight on endpoint %#x\n", Endpoint->Index);
        return STATUS_DEVICE_BUSY;
    }
    RtlZeroMemory(&Epic, sizeof(Epic));
    Epic.Version = 2;
    Epic.Sequence = Endpoint->Sequence;
    Epic.Length = RequestLength;
    Epic.SubVersion = 2;
    Epic.Category = EPIC_CATEGORY_NOTIFY;
    Epic.Subtype = Subtype;
    Epic.Tag = (USHORT)(Tag + 1);
    RtlZeroMemory(Call, sizeof(*Call));
    Call->InUse = TRUE;
    Call->Reply = Reply;
    Call->ReplyLength = ReplyLength;
    Status = AfkSendRing(Device, Endpoint, Entry->Channel, EPIC_TYPE_NOTIFY, &Epic, sizeof(Epic), Request,
                         RequestLength);
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(Call, sizeof(*Call));
        AopUnlock(Device);
        DbgPrint("appleaop: call %04x to %s not sent: %lx\n", Subtype, Entry->Name, Status);
        return Status;
    }
    Endpoint->Sequence++;
    Deadline = AopNowMs() + AOP_CALL_TIMEOUT_MS;
    while (!Call->Done && !Device->Rtkit.Failed)
    {
        if (AppleRtkitPoll(&Device->Rtkit))
            continue;
        if (AopNowMs() > Deadline)
            break;
        KeStallExecutionProcessor(50);
    }
    if (Call->Done)
    {
        *ReturnCode = Call->ReturnCode;
        if (Returned)
            *Returned = Call->Returned;
        RtlZeroMemory(Call, sizeof(*Call));
        Status = STATUS_SUCCESS;
    }
    else
    {
        Call->Abandoned = TRUE;
        Call->Reply = NULL;
        Call->ReplyLength = 0;
        Status = Device->Rtkit.Failed ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_IO_TIMEOUT;
        DbgPrint("appleaop: call %04x to %s got no reply: %lx\n", Subtype, Entry->Name, Status);
    }
    AopUnlock(Device);
    return Status;
}
