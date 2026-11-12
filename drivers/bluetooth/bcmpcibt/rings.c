/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared memory, transfer rings and completion rings of the Broadcom PCIe Bluetooth function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bcmpcibt.h"

#define NDEBUG
#include <debug.h>

#define BCMBT_SHARED_ALIGN      0x1000
#define BCMBT_DART_RETRIES      100

typedef struct _BCMBT_TRANSFER_LAYOUT
{
    UCHAR Id;
    UCHAR Doorbell;
    UCHAR CompletionRing;
    USHORT FooterSize;
    USHORT MappedSize;
    BOOLEAN Virtual;
    BOOLEAN Sync;
    BOOLEAN BuffersOnly;
} BCMBT_TRANSFER_LAYOUT;

typedef struct _BCMBT_COMPLETION_LAYOUT
{
    UCHAR Id;
    USHORT Entries;
    USHORT FooterSize;
    USHORT Delay;
    ULONG TransferMask;
} BCMBT_COMPLETION_LAYOUT;

static const BCMBT_TRANSFER_LAYOUT BcmBtTransferLayout[] =
{
    { BCMBT_XFER_CONTROL, BCMBT_DOORBELL_CONTROL, BCMBT_COMPLETION_CONTROL_ACK, BCMBT_CONTROL_MESSAGE_SIZE, 0, FALSE, FALSE, FALSE },
    { BCMBT_XFER_HCI_H2D, BCMBT_DOORBELL_HCI_H2D, BCMBT_COMPLETION_HCI_ACL_ACK, BCMBT_EVENT_PAYLOAD_SIZE, 0, FALSE, FALSE, FALSE },
    { BCMBT_XFER_HCI_D2H, BCMBT_DOORBELL_HCI_D2H, BCMBT_COMPLETION_HCI_ACL_EVENT, 0, 0, TRUE, FALSE, FALSE },
    { BCMBT_XFER_SCO_H2D, BCMBT_DOORBELL_SCO, BCMBT_COMPLETION_SCO_ACK, BCMBT_SCO_PAYLOAD_SIZE, 0, FALSE, TRUE, FALSE },
    { BCMBT_XFER_SCO_D2H, BCMBT_DOORBELL_SCO, BCMBT_COMPLETION_SCO_EVENT, 0, 0, TRUE, TRUE, FALSE },
    { BCMBT_XFER_ACL_H2D, BCMBT_DOORBELL_ACL_H2D, BCMBT_COMPLETION_HCI_ACL_ACK, 0, BCMBT_ACL_PAYLOAD_SIZE, FALSE, FALSE, FALSE },
    { BCMBT_XFER_ACL_D2H, BCMBT_DOORBELL_ACL_D2H, BCMBT_COMPLETION_HCI_ACL_EVENT, 0, BCMBT_ACL_PAYLOAD_SIZE, FALSE, FALSE, TRUE },
};

static const BCMBT_COMPLETION_LAYOUT BcmBtCompletionLayout[] =
{
    { BCMBT_COMPLETION_CONTROL_ACK, 32, 0, 0, 1UL << BCMBT_XFER_CONTROL },
    { BCMBT_COMPLETION_HCI_ACL_ACK, 2 * BCMBT_RING_ENTRIES, 0, 1000, (1UL << BCMBT_XFER_HCI_H2D) | (1UL << BCMBT_XFER_ACL_H2D) },
    { BCMBT_COMPLETION_HCI_ACL_EVENT, 2 * BCMBT_RING_ENTRIES, BCMBT_EVENT_PAYLOAD_SIZE, 1000,
      (1UL << BCMBT_XFER_HCI_D2H) | (1UL << BCMBT_XFER_ACL_D2H) },
    { BCMBT_COMPLETION_SCO_ACK, BCMBT_RING_ENTRIES, 0, 0, 1UL << BCMBT_XFER_SCO_H2D },
    { BCMBT_COMPLETION_SCO_EVENT, BCMBT_RING_ENTRIES, BCMBT_SCO_PAYLOAD_SIZE, 0, 1UL << BCMBT_XFER_SCO_D2H },
};

C_ASSERT(RTL_NUMBER_OF(BcmBtTransferLayout) == RTL_NUMBER_OF(((PBCMBT_DEVICE)0)->TransferRings));
C_ASSERT(RTL_NUMBER_OF(BcmBtCompletionLayout) == RTL_NUMBER_OF(((PBCMBT_DEVICE)0)->CompletionRings));
C_ASSERT(sizeof(BCMBT_RING_STATE) == 2 * (2 * BCMBT_COMPLETION_RINGS + 2 * BCMBT_TRANSFER_RINGS));
C_ASSERT(BCMBT_PAYLOAD_CHUNKS * BCMBT_PAYLOAD_CHUNK_SLOTS == BCMBT_RING_ENTRIES);
C_ASSERT(BCMBT_CONTEXT_SIZE <= BCMBT_SHARED_ALIGN && sizeof(BCMBT_RING_STATE) <= BCMBT_SHARED_ALIGN);
C_ASSERT(BCMBT_PERIPHERAL_INFO_SIZE <= BCMBT_SHARED_ALIGN);

NTSTATUS
BcmBtMapDma(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova)
{
    ULONG64 Physical = (ULONG64)MmGetPhysicalAddress(Buffer).QuadPart;
    ULONG Attempt;
    NTSTATUS Status;

    *Iova = 0;
    if (Dev->DartNode == FDT_NO_NODE)
    {
        if (Physical + Length > BCMBT_DMA_WINDOW_SIZE)
            return STATUS_INSUFFICIENT_RESOURCES;
        *Iova = Physical;
        return STATUS_SUCCESS;
    }
    for (Attempt = 0; Attempt < BCMBT_DART_RETRIES; Attempt++)
    {
        Status = AppleDartMapLow(&Dev->Tree, Dev->DartNode, Dev->Sid, Physical, Length, Iova, "bcmpcibt");
        if (Status != STATUS_DEVICE_BUSY)
            break;
        BcmBtSleep(1);
    }
    if (NT_SUCCESS(Status) && *Iova + Length > BCMBT_DMA_WINDOW_SIZE)
    {
        BcmBtUnmapDma(Dev, *Iova, Length);
        *Iova = 0;
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    return Status;
}

VOID
BcmBtUnmapDma(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length)
{
    ULONG Attempt;

    if (Dev->DartNode == FDT_NO_NODE || !Iova)
        return;
    for (Attempt = 0; Attempt < BCMBT_DART_RETRIES; Attempt++)
    {
        if (AppleDartUnmapLow(&Dev->Tree, Dev->DartNode, Dev->Sid, Iova, Length, "bcmpcibt") != STATUS_DEVICE_BUSY)
            return;
        BcmBtSleep(1);
    }
}

static
NTSTATUS
BcmBtTakeBlock(
    _In_ PBCMBT_DEVICE Dev,
    _Inout_ PULONG Next,
    _In_ SIZE_T Size,
    _Out_ PUCHAR *Va,
    _Out_ PULONG64 Iova)
{
    PBCMBT_DMA_BLOCK Block;
    PHYSICAL_ADDRESS Low, High, Boundary;
    NTSTATUS Status;

    *Va = NULL;
    *Iova = 0;
    if (*Next >= RTL_NUMBER_OF(Dev->Blocks))
        return STATUS_INSUFFICIENT_RESOURCES;
    Block = &Dev->Blocks[*Next];
    Size = ALIGN_UP_BY(Size, BCMBT_SHARED_ALIGN);
    if (*Next == Dev->BlockCount)
    {
        Low.QuadPart = 0;
        High.QuadPart = BcmBtHighestDmaAddress(Dev->DartNode != FDT_NO_NODE);
        Boundary.QuadPart = 0;
        Block->Va = MmAllocateContiguousMemorySpecifyCache(Size, Low, High, Boundary, MmNonCached);
        if (!Block->Va)
        {
            DPRINT1("BCMBT: no %Iu contiguous bytes for ring block %lu\n", Size, *Next);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Status = BcmBtMapDma(Dev, Block->Va, Size, &Block->Iova);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("BCMBT: ring block %lu at %I64x+%Iu cannot be mapped for the device: %lx\n", *Next,
                    (ULONG64)MmGetPhysicalAddress(Block->Va).QuadPart, Size, Status);
            MmFreeContiguousMemorySpecifyCache(Block->Va, Size, MmNonCached);
            Block->Va = NULL;
            return Status;
        }
        Block->Size = Size;
        Dev->BlockCount++;
    }
    else if (Block->Size != Size)
    {
        return STATUS_INTERNAL_ERROR;
    }
    RtlZeroMemory(Block->Va, Size);
    *Va = Block->Va;
    *Iova = Block->Iova;
    (*Next)++;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BcmBtLayout(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Next = 0;
    ULONG Index, Chunk;
    NTSTATUS Status;

    Status = BcmBtTakeBlock(Dev, &Next, 3 * BCMBT_SHARED_ALIGN, &Dev->Context, &Dev->ContextIova);
    if (!NT_SUCCESS(Status))
        return Status;
    Dev->RingState = (PBCMBT_RING_STATE)(Dev->Context + BCMBT_SHARED_ALIGN);
    for (Index = 0; Index < RTL_NUMBER_OF(Dev->TransferRings); Index++)
    {
        PBCMBT_TRANSFER_RING Ring = &Dev->TransferRings[Index];

        if (!Ring->Virtual)
        {
            Status = BcmBtTakeBlock(Dev, &Next, (SIZE_T)Ring->Entries * BcmBtTransferEntrySize(Ring), &Ring->Ring,
                                    &Ring->RingIova);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        for (Chunk = 0; Ring->MappedSize && Chunk < BCMBT_PAYLOAD_CHUNKS; Chunk++)
        {
            Status = BcmBtTakeBlock(Dev, &Next, (SIZE_T)BCMBT_PAYLOAD_CHUNK_SLOTS * Ring->MappedSize,
                                    &Ring->PayloadChunk[Chunk], &Ring->PayloadChunkIova[Chunk]);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Dev->CompletionRings); Index++)
    {
        PBCMBT_COMPLETION_RING Ring = &Dev->CompletionRings[Index];

        Status = BcmBtTakeBlock(Dev, &Next, (SIZE_T)Ring->Entries * BcmBtCompletionEntrySize(Ring), &Ring->Ring,
                                &Ring->RingIova);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    BcmBtBuildContext(Dev->Context, Dev->ContextIova + BCMBT_SHARED_ALIGN, Dev->ContextIova + 2 * BCMBT_SHARED_ALIGN,
                      &Dev->TransferRings[BCMBT_XFER_CONTROL], &Dev->CompletionRings[BCMBT_COMPLETION_CONTROL_ACK]);
    return STATUS_SUCCESS;
}

static
VOID
BcmBtFreeBlocks(
    _In_ PBCMBT_DEVICE Dev)
{
    while (Dev->BlockCount)
    {
        PBCMBT_DMA_BLOCK Block = &Dev->Blocks[--Dev->BlockCount];

        BcmBtUnmapDma(Dev, Block->Iova, Block->Size);
        MmFreeContiguousMemorySpecifyCache(Block->Va, Block->Size, MmNonCached);
        RtlZeroMemory(Block, sizeof(*Block));
    }
    Dev->Context = NULL;
    Dev->ContextIova = 0;
    Dev->RingState = NULL;
}

static
VOID
BcmBtConfigureRings(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(BcmBtTransferLayout); Index++)
    {
        const BCMBT_TRANSFER_LAYOUT *Layout = &BcmBtTransferLayout[Index];
        PBCMBT_TRANSFER_RING Ring = &Dev->TransferRings[Layout->Id];

        RtlZeroMemory(Ring, sizeof(*Ring));
        Ring->Id = Layout->Id;
        Ring->Doorbell = Layout->Doorbell;
        Ring->CompletionRing = Layout->CompletionRing;
        Ring->Entries = BCMBT_RING_ENTRIES;
        Ring->FooterSize = Layout->FooterSize;
        Ring->MappedSize = Layout->MappedSize;
        Ring->Virtual = Layout->Virtual;
        Ring->Sync = Layout->Sync;
        Ring->BuffersOnly = Layout->BuffersOnly;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(BcmBtCompletionLayout); Index++)
    {
        const BCMBT_COMPLETION_LAYOUT *Layout = &BcmBtCompletionLayout[Index];
        PBCMBT_COMPLETION_RING Ring = &Dev->CompletionRings[Layout->Id];

        RtlZeroMemory(Ring, sizeof(*Ring));
        Ring->Id = Layout->Id;
        Ring->Entries = Layout->Entries;
        Ring->FooterSize = Layout->FooterSize;
        Ring->Delay = Layout->Delay;
        Ring->TransferMask = Layout->TransferMask;
    }
}

NTSTATUS
BcmBtAllocateShared(
    _In_ PBCMBT_DEVICE Dev)
{
    SIZE_T Total = 0;
    ULONG Index;
    NTSTATUS Status;

    if (Dev->BlockCount)
        return STATUS_SUCCESS;
    BcmBtConfigureRings(Dev);
    Status = BcmBtLayout(Dev);
    if (!NT_SUCCESS(Status))
    {
        BcmBtFreeBlocks(Dev);
        BcmBtConfigureRings(Dev);
        return Status;
    }
    for (Index = 0; Index < Dev->BlockCount; Index++)
        Total += Dev->Blocks[Index].Size;
    DPRINT1("BCMBT: %Iu bytes of rings in %lu blocks, context at device address %I64x\n", Total, Dev->BlockCount,
            Dev->ContextIova);
    return STATUS_SUCCESS;
}

NTSTATUS
BcmBtResetShared(
    _In_ PBCMBT_DEVICE Dev)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (!Dev->BlockCount)
        return STATUS_DEVICE_NOT_READY;
    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    BcmBtConfigureRings(Dev);
    Status = BcmBtLayout(Dev);
    Dev->RingsCreated = FALSE;
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
    return Status;
}

VOID
BcmBtFreeShared(
    _In_ PBCMBT_DEVICE Dev)
{
    if (Dev->Firmware)
    {
        BcmBtUnmapDma(Dev, Dev->FirmwareIova, Dev->FirmwareSize);
        MmFreeContiguousMemorySpecifyCache(Dev->Firmware, Dev->FirmwareSize, MmNonCached);
        Dev->Firmware = NULL;
        Dev->FirmwareSize = 0;
        Dev->FirmwareIova = 0;
    }
    BcmBtFreeBlocks(Dev);
}

static
VOID
BcmBtRingDoorbell(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR Doorbell,
    _In_ USHORT Value)
{
    KeMemoryBarrier();
    BcmBtWrite0(Dev, BCMBT_BAR0_DOORBELL, ((ULONG)Value << 16) | ((ULONG)Doorbell << 8) | 0x20);
}

NTSTATUS
BcmBtEnqueue(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR RingId,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait)
{
    PBCMBT_TRANSFER_RING Ring;
    LARGE_INTEGER Timeout;
    ULONGLONG Start;
    KEVENT Event;
    KIRQL OldIrql;
    USHORT Head, Next;
    LONG Slot = -1;
    NTSTATUS Status;

    if (RingId >= RTL_NUMBER_OF(Dev->TransferRings))
        return STATUS_INVALID_PARAMETER;
    Ring = &Dev->TransferRings[RingId];
    if (Ring->Virtual || Ring->BuffersOnly || (Wait && RingId != BCMBT_XFER_CONTROL))
        return STATUS_INVALID_PARAMETER;
    if (Wait)
        KeInitializeEvent(&Event, NotificationEvent, FALSE);

    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    Head = Dev->RingState->TransferHead[RingId];
    Next = (USHORT)((Head + 1) % Ring->Entries);
    if (!Ring->Enabled)
        Status = STATUS_DEVICE_NOT_READY;
    else if (Next == Dev->RingState->TransferTail[RingId] || (Slot = BcmBtAllocateSlot(Ring)) < 0)
        Status = STATUS_DEVICE_BUSY;
    else
        Status = BcmBtWriteTransferEntry(Ring, Head, (ULONG)Slot, Data, Length);
    if (NT_SUCCESS(Status))
    {
        if (Wait)
            Ring->Waiters[Slot] = &Event;
        BcmBtWrite0(Dev, BCMBT_BAR0_SLEEP_CONTROL, BCMBT_SLEEP_AWAKE);
        KeMemoryBarrier();
        Dev->RingState->TransferHead[RingId] = Next;
        if (!Ring->Sync)
            BcmBtRingDoorbell(Dev, Ring->Doorbell, Next);
    }
    else if (Slot >= 0)
    {
        BcmBtReleaseSlot(Ring, (ULONG)Slot);
    }
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BCMBT: %lu-byte message on ring %u not queued: %lx\n", Length, RingId, Status);
        return Status;
    }
    if (!Wait)
        return STATUS_SUCCESS;

    Start = KeQueryInterruptTime();
    Timeout.QuadPart = -10000LL * 10;
    do
    {
        BcmBtPollCompletions(Dev);
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &Timeout);
    } while (Status == STATUS_TIMEOUT && !Dev->StopThread &&
             KeQueryInterruptTime() - Start < 10000ULL * BCMBT_CONTROL_TIMEOUT_MS);
    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    Ring->Waiters[Slot] = NULL;
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
    if (Status == STATUS_TIMEOUT)
    {
        DPRINT1("BCMBT: control message %u was not acknowledged\n", ((const UCHAR *)Data)[0]);
        return STATUS_IO_TIMEOUT;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
BcmBtCreateTransferRing(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PBCMBT_TRANSFER_RING Ring)
{
    UCHAR Message[BCMBT_CONTROL_MESSAGE_SIZE];
    KIRQL OldIrql;
    NTSTATUS Status;

    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    Dev->RingState->TransferHead[Ring->Id] = 0;
    Dev->RingState->TransferTail[Ring->Id] = 0;
    Ring->Generation++;
    RtlZeroMemory(Ring->InUse, sizeof(Ring->InUse));
    BcmBtBuildCreateTransferRing(Ring, Message);
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);

    Status = BcmBtEnqueue(Dev, BCMBT_XFER_CONTROL, Message, sizeof(Message), TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    if (Ring->BuffersOnly)
        BcmBtWriteBufferEntries(Ring);
    if (Ring->Virtual || Ring->BuffersOnly)
    {
        KeMemoryBarrier();
        Dev->RingState->TransferHead[Ring->Id] = BCMBT_D2H_INITIAL_HEAD;
        BcmBtRingDoorbell(Dev, Ring->Doorbell, BCMBT_D2H_INITIAL_HEAD);
    }
    Ring->Enabled = TRUE;
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
BcmBtCreateCompletionRing(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PBCMBT_COMPLETION_RING Ring)
{
    UCHAR Message[BCMBT_CONTROL_MESSAGE_SIZE];
    NTSTATUS Status;

    RtlZeroMemory(Ring->Ring, (SIZE_T)Ring->Entries * BcmBtCompletionEntrySize(Ring));
    BcmBtBuildCreateCompletionRing(Ring, Message);
    Status = BcmBtEnqueue(Dev, BCMBT_XFER_CONTROL, Message, sizeof(Message), TRUE);
    if (NT_SUCCESS(Status))
        Ring->Enabled = TRUE;
    return Status;
}

static
VOID
BcmBtDestroyRing(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR Type,
    _In_ UCHAR Id,
    _Inout_ PBOOLEAN Enabled)
{
    UCHAR Message[BCMBT_CONTROL_MESSAGE_SIZE];
    KIRQL OldIrql;

    if (!*Enabled)
        return;
    BcmBtBuildDestroyRing(Type, Id, Message);
    if (!NT_SUCCESS(BcmBtEnqueue(Dev, BCMBT_XFER_CONTROL, Message, sizeof(Message), TRUE)))
        DPRINT1("BCMBT: ring %u (type %u) not destroyed\n", Id, Type);
    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    *Enabled = FALSE;
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
}

NTSTATUS
BcmBtCreateRings(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = BCMBT_COMPLETION_HCI_ACL_ACK; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Dev->CompletionRings); Index++)
        Status = BcmBtCreateCompletionRing(Dev, &Dev->CompletionRings[Index]);
    for (Index = BCMBT_XFER_HCI_H2D; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Dev->TransferRings); Index++)
        Status = BcmBtCreateTransferRing(Dev, &Dev->TransferRings[Index]);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BCMBT: creating rings failed: %lx\n", Status);
        BcmBtDestroyRings(Dev);
        return Status;
    }
    Dev->RingsCreated = TRUE;
    return STATUS_SUCCESS;
}

VOID
BcmBtDestroyRings(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Index;

    for (Index = RTL_NUMBER_OF(Dev->TransferRings) - 1; Index >= BCMBT_XFER_HCI_H2D; Index--)
        BcmBtDestroyRing(Dev, BCMBT_MSG_DESTROY_TRANSFER_RING, (UCHAR)Index, &Dev->TransferRings[Index].Enabled);
    for (Index = RTL_NUMBER_OF(Dev->CompletionRings) - 1; Index >= BCMBT_COMPLETION_HCI_ACL_ACK; Index--)
        BcmBtDestroyRing(Dev, BCMBT_MSG_DESTROY_COMPLETION_RING, (UCHAR)Index, &Dev->CompletionRings[Index].Enabled);
    Dev->RingsCreated = FALSE;
}

static
VOID
BcmBtHandleAck(
    _In_ PBCMBT_TRANSFER_RING Ring,
    _In_ USHORT MessageId)
{
    ULONG Slot;

    if (!BcmBtMessageSlot(Ring, MessageId, &Slot) || !BcmBtReleaseSlot(Ring, Slot))
    {
        DPRINT1("BCMBT: stale acknowledgement %04x on ring %u (generation %u)\n", MessageId, Ring->Id, Ring->Generation);
        return;
    }
    if (Slot < RTL_NUMBER_OF(Ring->Waiters) && Ring->Waiters[Slot])
    {
        KeSetEvent(Ring->Waiters[Slot], IO_NO_INCREMENT, FALSE);
        Ring->Waiters[Slot] = NULL;
    }
}

static
VOID
BcmBtHandleIncoming(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PBCMBT_TRANSFER_RING Ring,
    _In_ const BCMBT_COMPLETION_RING *Completions,
    _In_ const BCMBT_COMPLETION *Completion)
{
    const UCHAR *Payload = Completion->Footer;
    USHORT Head;
    ULONG Slot;

    if (!Ring->Enabled)
    {
        DPRINT1("BCMBT: data on disabled ring %u\n", Ring->Id);
        Payload = NULL;
    }
    else if (Ring->BuffersOnly && (Completion->Flags & BCMBT_ENTRY_FLAG_MAPPED))
    {
        if (!BcmBtMessageSlot(Ring, Completion->MessageId, &Slot) || Completion->Length > Ring->MappedSize)
        {
            DPRINT1("BCMBT: bad buffer %04x (%lu bytes) on ring %u\n", Completion->MessageId, Completion->Length, Ring->Id);
            Payload = NULL;
        }
        else
        {
            Payload = BcmBtPayload(Ring, Slot);
        }
    }
    else if (Completion->Length > Completions->FooterSize)
    {
        DPRINT1("BCMBT: %lu-byte packet overruns the %u-byte footer on ring %u\n", Completion->Length,
                Completions->FooterSize, Ring->Id);
        Payload = NULL;
    }

    if (Payload && Ring->Id == BCMBT_XFER_HCI_D2H)
        BcmBtQueuePacket(Dev, HciPacketEvent, Payload, Completion->Length);
    else if (Payload && Ring->Id == BCMBT_XFER_ACL_D2H)
        BcmBtQueuePacket(Dev, HciPacketAclData, Payload, Completion->Length);

    Head = (USHORT)((Dev->RingState->TransferHead[Ring->Id] + 1) % Ring->Entries);
    Dev->RingState->TransferHead[Ring->Id] = Head;
    BcmBtRingDoorbell(Dev, Ring->Doorbell, Head);
}

static
VOID
BcmBtHandleCompletion(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PBCMBT_COMPLETION_RING Completions,
    _In_ USHORT Position)
{
    BCMBT_COMPLETION Completion;
    PBCMBT_TRANSFER_RING Ring;

    if (!NT_SUCCESS(BcmBtReadCompletion(Completions, Position, &Completion)))
    {
        DPRINT1("BCMBT: entry %u of completion ring %u names transfer ring %u\n", Position, Completions->Id,
                Completion.TransferRing);
        return;
    }
    Ring = &Dev->TransferRings[Completion.TransferRing];
    switch (Completion.TransferRing)
    {
        case BCMBT_XFER_CONTROL:
        case BCMBT_XFER_HCI_H2D:
        case BCMBT_XFER_SCO_H2D:
        case BCMBT_XFER_ACL_H2D:
            BcmBtHandleAck(Ring, Completion.MessageId);
            break;
        default:
            BcmBtHandleIncoming(Dev, Ring, Completions, &Completion);
            break;
    }
}

VOID
BcmBtPollCompletions(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Index;
    KIRQL OldIrql;

    if (!Dev->RingState)
        return;
    KeAcquireSpinLock(&Dev->RingLock, &OldIrql);
    for (Index = 0; Index < RTL_NUMBER_OF(Dev->CompletionRings); Index++)
    {
        PBCMBT_COMPLETION_RING Ring = &Dev->CompletionRings[Index];
        USHORT Tail;

        USHORT Head, Drained;

        if (!Ring->Enabled)
            continue;
        Tail = Dev->RingState->CompletionTail[Ring->Id];
        Head = Dev->RingState->CompletionHead[Ring->Id];
        if (Head >= Ring->Entries || Tail >= Ring->Entries)
        {
            if (!Ring->Desynchronised)
            {
                Ring->Desynchronised = TRUE;
                DPRINT1("BCMBT: completion ring %u reports %u of %u entries consumed up to %u; it is left alone\n",
                        Ring->Id, Head, Ring->Entries, Tail);
            }
            continue;
        }
        /* A ring holds at most its own length, so a longer run means the controller is still filling
         * it; the rest waits for the next notification rather than holding the processor. */
        for (Drained = 0; Tail != Head && Drained < Ring->Entries; ++Drained)
        {
            KeMemoryBarrier();
            BcmBtHandleCompletion(Dev, Ring, Tail);
            Tail = (USHORT)((Tail + 1) % Ring->Entries);
            Dev->RingState->CompletionTail[Ring->Id] = Tail;
        }
    }
    KeReleaseSpinLock(&Dev->RingLock, OldIrql);
}

BOOLEAN
BcmBtUpdateState(
    _In_ PBCMBT_DEVICE Dev)
{
    LONG Bootstage, Rti;
    BOOLEAN Changed;

    if (!Dev->Bar2)
        return FALSE;
    Bootstage = (LONG)BcmBtRead2(Dev, BCMBT_BAR2_BOOTSTAGE);
    Rti = (LONG)BcmBtRead2(Dev, BCMBT_BAR2_RTI_STATUS);
    Changed = InterlockedExchange(&Dev->Bootstage, Bootstage) != Bootstage;
    if (InterlockedExchange(&Dev->RtiStatus, Rti) != Rti)
        Changed = TRUE;
    return Changed;
}
