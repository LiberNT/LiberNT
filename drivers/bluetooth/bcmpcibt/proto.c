/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Encoding and decoding of the Broadcom PCIe Bluetooth shared-memory protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "proto.h"

static const BCMBT_CHIP BcmBtChips[] =
{
    { 0x5f69, 0x4378, 0x4120, 0x18002000, 0x1810a000, 0x18107000, 0x000000, FALSE, "b1 b3" },
    { 0x5f71, 0x4387, 0x413c, 0x18002000, 0x18109000, 0x18106000, 0x000000, TRUE, "c2" },
    { 0x5f72, 0x4388, 0x415c, 0x18002000, 0x18109000, 0x18106000, 0x200000, FALSE, "*" },
};

static
VOID
BcmBtPut16(
    _Out_writes_bytes_(2) PUCHAR Buffer,
    _In_ USHORT Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
}

static
VOID
BcmBtPut32(
    _Out_writes_bytes_(4) PUCHAR Buffer,
    _In_ ULONG Value)
{
    BcmBtPut16(Buffer, (USHORT)Value);
    BcmBtPut16(Buffer + 2, (USHORT)(Value >> 16));
}

static
VOID
BcmBtPut64(
    _Out_writes_bytes_(8) PUCHAR Buffer,
    _In_ ULONG64 Value)
{
    BcmBtPut32(Buffer, (ULONG)Value);
    BcmBtPut32(Buffer + 4, (ULONG)(Value >> 32));
}

static
USHORT
BcmBtGet16(
    _In_reads_bytes_(2) const UCHAR *Buffer)
{
    return (USHORT)(Buffer[0] | (Buffer[1] << 8));
}

static
ULONG
BcmBtGet32(
    _In_reads_bytes_(4) const UCHAR *Buffer)
{
    return BcmBtGet16(Buffer) | ((ULONG)BcmBtGet16(Buffer + 2) << 16);
}

const BCMBT_CHIP *
BcmBtFindChip(
    _In_ USHORT DeviceId)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(BcmBtChips); Index++)
    {
        if (BcmBtChips[Index].DeviceId == DeviceId)
            return &BcmBtChips[Index];
    }
    return NULL;
}

static
ULONG
BcmBtStringLength(
    _In_reads_bytes_(Size) const UCHAR *String,
    _In_ ULONG Size)
{
    ULONG Length = 0;

    while (Length < Size && String[Length])
        Length++;
    return Length;
}

static
NTSTATUS
BcmBtParseParameters(
    _In_reads_bytes_(Length) const UCHAR *Text,
    _In_ ULONG Length,
    _In_ CHAR WantedTag,
    _In_ BOOLEAN LowerCase,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Value)
{
    ULONG Position = 0;

    for (;;)
    {
        ULONG Start, End, Index;
        CHAR Tag;

        while (Position < Length && Text[Position] == ' ')
            Position++;
        if (Position == Length)
            return STATUS_SUCCESS;
        Tag = (CHAR)Text[Position++];
        if (Position == Length || Text[Position++] != '=')
            return STATUS_INVALID_PARAMETER;
        Start = Position;
        while (Position < Length && Text[Position] != ' ')
            Position++;
        End = Position;
        if (End - Start >= BCMBT_OTP_PARAM_LENGTH)
            return STATUS_INVALID_PARAMETER;
        if (Tag != WantedTag)
            continue;
        for (Index = 0; Index < End - Start; Index++)
        {
            CHAR Character = (CHAR)Text[Start + Index];

            if (LowerCase && Character >= 'A' && Character <= 'Z')
                Character = (CHAR)(Character - 'A' + 'a');
            Value[Index] = Character;
        }
        Value[End - Start] = ANSI_NULL;
    }
}

static
NTSTATUS
BcmBtParseVendorRecord(
    _In_reads_bytes_(Size) const UCHAR *Record,
    _In_ ULONG Size,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Stepping,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Vendor)
{
    ULONG ChipStart = 4, ChipLength, BoardStart, BoardLength;
    NTSTATUS Status;

    if (Size < 6 || BcmBtGet32(Record) != BCMBT_OTP_VENDOR_HEADER)
        return STATUS_INVALID_PARAMETER;
    ChipLength = BcmBtStringLength(Record + ChipStart, Size - ChipStart);
    BoardStart = ChipStart + ChipLength + 1;
    if (BoardStart >= Size)
        return STATUS_INVALID_PARAMETER;
    BoardLength = BcmBtStringLength(Record + BoardStart, Size - BoardStart);
    if (BoardStart + BoardLength >= Size)
        return STATUS_INVALID_PARAMETER;

    Stepping[0] = ANSI_NULL;
    Vendor[0] = ANSI_NULL;
    Status = BcmBtParseParameters(Record + ChipStart, ChipLength, 's', TRUE, Stepping);
    if (NT_SUCCESS(Status))
        Status = BcmBtParseParameters(Record + BoardStart, BoardLength, 'V', FALSE, Vendor);
    if (NT_SUCCESS(Status) && (!Stepping[0] || !Vendor[0]))
        Status = STATUS_NOT_FOUND;
    return Status;
}

NTSTATUS
BcmBtParseOtp(
    _In_reads_bytes_(Size) const UCHAR *Otp,
    _In_ ULONG Size,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Stepping,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Vendor)
{
    NTSTATUS Status = STATUS_NOT_FOUND;
    ULONG Offset = 0;

    Stepping[0] = ANSI_NULL;
    Vendor[0] = ANSI_NULL;
    while (Offset + 1 < Size)
    {
        UCHAR Type = Otp[Offset], Length = Otp[Offset + 1];

        if (Type == 0 || Offset + 2 + Length > Size)
            break;
        if (Type == BCMBT_OTP_SYS_VENDOR)
            Status = BcmBtParseVendorRecord(Otp + Offset + 2, Length, Stepping, Vendor);
        Offset += 2 + Length;
    }
    return Status;
}

BOOLEAN
BcmBtUsesBeamforming(
    _In_ const BCMBT_CHIP *Chip,
    _In_z_ PCSTR Stepping)
{
    PCSTR List = Chip->BeamformingSteppings;

    while (*List)
    {
        ULONG Length = 0, Index;

        while (List[Length] && List[Length] != ' ')
            Length++;
        if (Length == 1 && List[0] == '*')
            return TRUE;
        for (Index = 0; Index < Length && Stepping[Index] == List[Index]; Index++)
            ;
        if (Index == Length && Stepping[Index] == ANSI_NULL)
            return TRUE;
        List += Length;
        while (*List == ' ')
            List++;
    }
    return FALSE;
}

BOOLEAN
BcmBtIsDefaultAddress(
    _In_ const BCMBT_CHIP *Chip,
    _In_reads_bytes_(6) const UCHAR *Address)
{
    return Address[0] == 0x93 && Address[1] == 0x76 && Address[2] == 0x00 &&
           Address[4] == (UCHAR)Chip->ChipId && Address[5] == (UCHAR)(Chip->ChipId >> 8);
}

VOID
BcmBtFixupEvent(
    _Inout_updates_bytes_(Length) PUCHAR Event,
    _In_ ULONG Length)
{
    ULONG Offset, Report;

    if (Length >= 14 && Event[0] == 0x0e && BcmBtGet16(Event + 3) == BCMBT_HCI_LE_READ_FEATURES && Event[5] == 0)
    {
        Event[7] &= (UCHAR)~0x08;
        return;
    }
    if (Length < 4 || Event[0] != 0x3e || Event[2] != 0x0d)
        return;
    Offset = 4;
    for (Report = 0; Report < Event[3] && Offset + 24 <= Length; Report++)
    {
        Event[Offset + 9] &= 0x1f;
        Event[Offset + 10] &= 0x1f;
        Offset += 24 + Event[Offset + 23];
    }
}

ULONG
BcmBtTransferEntrySize(
    _In_ const BCMBT_TRANSFER_RING *Ring)
{
    return BCMBT_ENTRY_SIZE + Ring->FooterSize;
}

ULONG
BcmBtCompletionEntrySize(
    _In_ const BCMBT_COMPLETION_RING *Ring)
{
    return BCMBT_ENTRY_SIZE + Ring->FooterSize;
}

USHORT
BcmBtMessageId(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot)
{
    return (USHORT)((Ring->Generation << 8) | (Slot & 0xff));
}

BOOLEAN
BcmBtMessageSlot(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ USHORT MessageId,
    _Out_ PULONG Slot)
{
    *Slot = MessageId & 0xff;
    return (MessageId >> 8) == Ring->Generation && *Slot < Ring->Entries;
}

PUCHAR
BcmBtPayload(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot)
{
    return Ring->PayloadChunk[Slot / BCMBT_PAYLOAD_CHUNK_SLOTS] +
           (Slot % BCMBT_PAYLOAD_CHUNK_SLOTS) * Ring->MappedSize;
}

ULONG64
BcmBtPayloadIova(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot)
{
    return Ring->PayloadChunkIova[Slot / BCMBT_PAYLOAD_CHUNK_SLOTS] +
           (ULONG64)(Slot % BCMBT_PAYLOAD_CHUNK_SLOTS) * Ring->MappedSize;
}

LONGLONG
BcmBtHighestDmaAddress(
    _In_ BOOLEAN Translated)
{
    return Translated ? MAXLONGLONG : (LONGLONG)BCMBT_DMA_WINDOW_SIZE - 1;
}

LONG
BcmBtAllocateSlot(
    _Inout_ PBCMBT_TRANSFER_RING Ring)
{
    ULONG Slot;

    for (Slot = 0; Slot < Ring->Entries; Slot++)
    {
        if (!(Ring->InUse[Slot / 32] & (1UL << (Slot % 32))))
        {
            Ring->InUse[Slot / 32] |= 1UL << (Slot % 32);
            return (LONG)Slot;
        }
    }
    return -1;
}

BOOLEAN
BcmBtReleaseSlot(
    _Inout_ PBCMBT_TRANSFER_RING Ring,
    _In_ ULONG Slot)
{
    if (Slot >= Ring->Entries || !(Ring->InUse[Slot / 32] & (1UL << (Slot % 32))))
        return FALSE;
    Ring->InUse[Slot / 32] &= ~(1UL << (Slot % 32));
    return TRUE;
}

NTSTATUS
BcmBtWriteTransferEntry(
    _Inout_ PBCMBT_TRANSFER_RING Ring,
    _In_ USHORT Head,
    _In_ ULONG Slot,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PUCHAR Entry;

    if (Head >= Ring->Entries || Slot >= Ring->Entries || (Length > Ring->FooterSize && Length > Ring->MappedSize))
        return STATUS_INVALID_PARAMETER;
    Entry = Ring->Ring + (ULONG)Head * BcmBtTransferEntrySize(Ring);
    RtlZeroMemory(Entry, BCMBT_ENTRY_SIZE);
    BcmBtPut16(Entry + 1, (USHORT)Length);
    BcmBtPut16(Entry + 12, BcmBtMessageId(Ring, Slot));
    if (Length <= Ring->FooterSize)
    {
        Entry[0] = BCMBT_ENTRY_FLAG_IN_FOOTER;
        RtlCopyMemory(Entry + BCMBT_ENTRY_SIZE, Data, Length);
    }
    else
    {
        Entry[0] = BCMBT_ENTRY_FLAG_MAPPED;
        BcmBtPut64(Entry + 4, BcmBtPayloadIova(Ring, Slot));
        RtlCopyMemory(BcmBtPayload(Ring, Slot), Data, Length);
    }
    return STATUS_SUCCESS;
}

VOID
BcmBtWriteBufferEntries(
    _Inout_ PBCMBT_TRANSFER_RING Ring)
{
    ULONG Slot;

    for (Slot = 0; Slot < Ring->Entries; Slot++)
    {
        PUCHAR Entry = Ring->Ring + Slot * BcmBtTransferEntrySize(Ring);

        RtlZeroMemory(Entry, BCMBT_ENTRY_SIZE);
        Entry[0] = BCMBT_ENTRY_FLAG_MAPPED;
        BcmBtPut16(Entry + 1, (USHORT)Ring->MappedSize);
        BcmBtPut64(Entry + 4, BcmBtPayloadIova(Ring, Slot));
        BcmBtPut16(Entry + 12, BcmBtMessageId(Ring, Slot));
    }
}

NTSTATUS
BcmBtReadCompletion(
    _In_ const BCMBT_COMPLETION_RING *Ring,
    _In_ USHORT Position,
    _Out_ PBCMBT_COMPLETION Completion)
{
    const UCHAR *Entry;

    RtlZeroMemory(Completion, sizeof(*Completion));
    if (Position >= Ring->Entries)
        return STATUS_INVALID_PARAMETER;
    Entry = Ring->Ring + (ULONG)Position * BcmBtCompletionEntrySize(Ring);
    Completion->Flags = Entry[0];
    Completion->TransferRing = BcmBtGet16(Entry + 2);
    Completion->MessageId = BcmBtGet16(Entry + 4);
    Completion->Length = BcmBtGet32(Entry + 6);
    Completion->Footer = Entry + BCMBT_ENTRY_SIZE;
    if (Completion->TransferRing >= 32 || !(Ring->TransferMask & (1UL << Completion->TransferRing)))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

VOID
BcmBtBuildCreateTransferRing(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message)
{
    USHORT Flags = 0;

    if (Ring->Virtual)
        Flags |= BCMBT_RING_FLAG_VIRTUAL;
    if (Ring->Sync)
        Flags |= BCMBT_RING_FLAG_SYNC;
    RtlZeroMemory(Message, BCMBT_CONTROL_MESSAGE_SIZE);
    Message[0] = BCMBT_MSG_CREATE_TRANSFER_RING;
    Message[2] = (UCHAR)(Ring->FooterSize / 4);
    BcmBtPut16(Message + 4, Ring->Id);
    BcmBtPut16(Message + 6, Ring->Id);
    BcmBtPut64(Message + 8, Ring->RingIova);
    BcmBtPut16(Message + 24, Ring->Entries);
    BcmBtPut16(Message + 26, Ring->CompletionRing);
    BcmBtPut16(Message + 28, Ring->Doorbell);
    BcmBtPut16(Message + 30, Flags);
}

VOID
BcmBtBuildCreateCompletionRing(
    _In_ const BCMBT_COMPLETION_RING *Ring,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message)
{
    RtlZeroMemory(Message, BCMBT_CONTROL_MESSAGE_SIZE);
    Message[0] = BCMBT_MSG_CREATE_COMPLETION_RING;
    Message[2] = (UCHAR)(Ring->FooterSize / 4);
    BcmBtPut16(Message + 4, Ring->Id);
    BcmBtPut16(Message + 6, Ring->Id);
    BcmBtPut64(Message + 8, Ring->RingIova);
    BcmBtPut16(Message + 16, Ring->Entries);
    BcmBtPut32(Message + 18, 0xffffffff);
    BcmBtPut16(Message + 30, Ring->Delay);
    BcmBtPut32(Message + 32, 0xffffffff);
}

VOID
BcmBtBuildDestroyRing(
    _In_ UCHAR Type,
    _In_ USHORT RingId,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message)
{
    RtlZeroMemory(Message, BCMBT_CONTROL_MESSAGE_SIZE);
    Message[0] = Type;
    BcmBtPut16(Message + 2, RingId);
}

VOID
BcmBtBuildContext(
    _Out_writes_bytes_(BCMBT_CONTEXT_SIZE) PUCHAR Context,
    _In_ ULONG64 RingStateIova,
    _In_ ULONG64 PeripheralInfoIova,
    _In_ const BCMBT_TRANSFER_RING *Control,
    _In_ const BCMBT_COMPLETION_RING *ControlAck)
{
    RtlZeroMemory(Context, BCMBT_CONTEXT_SIZE);
    BcmBtPut16(Context + 0, 1);
    BcmBtPut16(Context + 2, BCMBT_CONTEXT_SIZE);
    BcmBtPut32(Context + 4, 2);
    BcmBtPut64(Context + BCMBT_CONTEXT_PERIPHERAL_INFO, PeripheralInfoIova);
    BcmBtPut64(Context + BCMBT_CONTEXT_COMPLETION_HEADS,
               RingStateIova + FIELD_OFFSET(BCMBT_RING_STATE, CompletionHead));
    BcmBtPut64(Context + BCMBT_CONTEXT_TRANSFER_TAILS,
               RingStateIova + FIELD_OFFSET(BCMBT_RING_STATE, TransferTail));
    BcmBtPut64(Context + BCMBT_CONTEXT_COMPLETION_TAILS,
               RingStateIova + FIELD_OFFSET(BCMBT_RING_STATE, CompletionTail));
    BcmBtPut64(Context + BCMBT_CONTEXT_TRANSFER_HEADS,
               RingStateIova + FIELD_OFFSET(BCMBT_RING_STATE, TransferHead));
    BcmBtPut16(Context + BCMBT_CONTEXT_COMPLETION_COUNT, BCMBT_COMPLETION_RINGS);
    BcmBtPut16(Context + BCMBT_CONTEXT_TRANSFER_COUNT, BCMBT_TRANSFER_RINGS);
    BcmBtPut64(Context + BCMBT_CONTEXT_CONTROL_COMPLETION, ControlAck->RingIova);
    BcmBtPut64(Context + BCMBT_CONTEXT_CONTROL_TRANSFER, Control->RingIova);
    BcmBtPut16(Context + BCMBT_CONTEXT_CONTROL_LAYOUT + 0, Control->Entries);
    BcmBtPut16(Context + BCMBT_CONTEXT_CONTROL_LAYOUT + 2, ControlAck->Entries);
    BcmBtPut16(Context + BCMBT_CONTEXT_CONTROL_LAYOUT + 4, Control->Doorbell);
    BcmBtPut16(Context + BCMBT_CONTEXT_CONTROL_LAYOUT + 6, 0xffff);
    Context[BCMBT_CONTEXT_CONTROL_LAYOUT + 13] = (UCHAR)(Control->FooterSize / 4);
    Context[BCMBT_CONTEXT_CONTROL_LAYOUT + 15] = (UCHAR)(ControlAck->FooterSize / 4);
}

ULONG
BcmBtChunkCount(
    _In_ ULONG Size,
    _In_ ULONG ChunkSize)
{
    return (Size + ChunkSize - 1) / ChunkSize;
}
