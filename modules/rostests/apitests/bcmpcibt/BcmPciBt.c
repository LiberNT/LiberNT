/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for the Broadcom PCIe Bluetooth shared-memory protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>

#define UNIT_TEST
#include "../../../../drivers/bluetooth/bcmpcibt/proto.c"

static
USHORT
Get16(
    const UCHAR *Buffer)
{
    return (USHORT)(Buffer[0] | (Buffer[1] << 8));
}

static
ULONG
Get32(
    const UCHAR *Buffer)
{
    return Get16(Buffer) | ((ULONG)Get16(Buffer + 2) << 16);
}

static
ULONG64
Get64(
    const UCHAR *Buffer)
{
    return Get32(Buffer) | ((ULONG64)Get32(Buffer + 4) << 32);
}

static
ULONG
AppendRecord(
    PUCHAR Otp,
    ULONG Offset,
    UCHAR Type,
    const VOID *Data,
    UCHAR Length)
{
    Otp[Offset] = Type;
    Otp[Offset + 1] = Length;
    memcpy(Otp + Offset + 2, Data, Length);
    return Offset + 2 + Length;
}

static
UCHAR
BuildVendorRecord(
    PUCHAR Record,
    PCSTR Chip,
    PCSTR Board)
{
    ULONG Length = 4;

    Record[0] = 0x08;
    Record[1] = Record[2] = Record[3] = 0;
    memcpy(Record + Length, Chip, strlen(Chip) + 1);
    Length += (ULONG)strlen(Chip) + 1;
    memcpy(Record + Length, Board, strlen(Board) + 1);
    Length += (ULONG)strlen(Board) + 1;
    return (UCHAR)Length;
}

static
NTSTATUS
ParseVendor(
    PCSTR Chip,
    PCSTR Board,
    PCHAR Stepping,
    PCHAR Vendor)
{
    UCHAR Otp[BCMBT_OTP_SIZE], Record[96], Length;
    ULONG Offset;

    memset(Otp, 0, sizeof(Otp));
    Length = BuildVendorRecord(Record, Chip, Board);
    Offset = AppendRecord(Otp, 0, 0x80, "\x01\x02\x03", 3);
    AppendRecord(Otp, Offset, BCMBT_OTP_SYS_VENDOR, Record, Length);
    return BcmBtParseOtp(Otp, sizeof(Otp), Stepping, Vendor);
}

static
VOID
TestChips(VOID)
{
    const BCMBT_CHIP *Chip;

    Chip = BcmBtFindChip(0x5f72);
    ok(Chip != NULL, "BCM4388 missing\n");
    if (Chip)
    {
        ok_hex(Chip->ChipId, 0x4388);
        ok_hex(Chip->OtpOffset, 0x415c);
        ok_hex(Chip->Bar2Offset, 0x200000);
        ok_hex(Chip->Bar0Window1, 0x18002000);
        ok_hex(Chip->Bar0Window2, 0x18109000);
        ok_hex(Chip->Bar0Core2Window2, 0x18106000);
        ok_int(Chip->ClearSubsystemBit19, FALSE);
        ok_int(BcmBtUsesBeamforming(Chip, "c0"), TRUE);
        ok_int(BcmBtUsesBeamforming(Chip, "b0"), TRUE);
    }
    Chip = BcmBtFindChip(0x5f71);
    ok(Chip != NULL, "BCM4387 missing\n");
    if (Chip)
    {
        ok_hex(Chip->OtpOffset, 0x413c);
        ok_hex(Chip->Bar2Offset, 0);
        ok_int(Chip->ClearSubsystemBit19, TRUE);
        ok_int(BcmBtUsesBeamforming(Chip, "c2"), TRUE);
        ok_int(BcmBtUsesBeamforming(Chip, "c0"), FALSE);
    }
    Chip = BcmBtFindChip(0x5f69);
    ok(Chip != NULL, "BCM4378 missing\n");
    if (Chip)
    {
        ok_hex(Chip->OtpOffset, 0x4120);
        ok_hex(Chip->Bar0Window2, 0x1810a000);
        ok_hex(Chip->Bar0Core2Window2, 0x18107000);
        ok_int(BcmBtUsesBeamforming(Chip, "b1"), TRUE);
        ok_int(BcmBtUsesBeamforming(Chip, "b3"), TRUE);
        ok_int(BcmBtUsesBeamforming(Chip, "b2"), FALSE);
        ok_int(BcmBtUsesBeamforming(Chip, "b"), FALSE);
        ok_int(BcmBtUsesBeamforming(Chip, "b10"), FALSE);
    }
    ok(BcmBtFindChip(0x5fa0) == NULL, "BCM4377 is not an Apple silicon part\n");
    ok(BcmBtFindChip(0x4433) == NULL, "The Wi-Fi function must not match\n");
}

static
VOID
TestOtp(VOID)
{
    CHAR Stepping[BCMBT_OTP_PARAM_LENGTH], Vendor[BCMBT_OTP_PARAM_LENGTH];
    UCHAR Otp[BCMBT_OTP_SIZE], Record[96];
    UCHAR Length;

    ok_hex(ParseVendor("s=C0 M=Jura", "V=u m=1", Stepping, Vendor), STATUS_SUCCESS);
    ok_str(Stepping, "c0");
    ok_str(Vendor, "u");
    ok_hex(ParseVendor("  M=x  s=B1", " V=USI ", Stepping, Vendor), STATUS_SUCCESS);
    ok_str(Stepping, "b1");
    ok_str(Vendor, "USI");
    ok_hex(ParseVendor("s=c0", "m=1", Stepping, Vendor), STATUS_NOT_FOUND);
    ok_hex(ParseVendor("M=1", "V=u", Stepping, Vendor), STATUS_NOT_FOUND);
    ok_hex(ParseVendor("s=c0", "V=0123456789abcdef", Stepping, Vendor), STATUS_INVALID_PARAMETER);
    ok_hex(ParseVendor("s=c0", "V=0123456789abcde", Stepping, Vendor), STATUS_SUCCESS);
    ok_str(Vendor, "0123456789abcde");
    ok_hex(ParseVendor("sc0", "V=u", Stepping, Vendor), STATUS_INVALID_PARAMETER);
    ok_hex(ParseVendor("s", "V=u", Stepping, Vendor), STATUS_INVALID_PARAMETER);

    memset(Otp, 0, sizeof(Otp));
    Length = BuildVendorRecord(Record, "s=c0", "V=u");
    Record[0] = 0x09;
    AppendRecord(Otp, 0, BCMBT_OTP_SYS_VENDOR, Record, Length);
    ok_hex(BcmBtParseOtp(Otp, sizeof(Otp), Stepping, Vendor), STATUS_INVALID_PARAMETER);

    memset(Otp, 0, sizeof(Otp));
    Length = BuildVendorRecord(Record, "s=c0", "V=u");
    AppendRecord(Otp, 0, BCMBT_OTP_SYS_VENDOR, Record, (UCHAR)(Length - 1));
    ok_hex(BcmBtParseOtp(Otp, sizeof(Otp), Stepping, Vendor), STATUS_INVALID_PARAMETER);

    memset(Otp, 0, sizeof(Otp));
    Length = BuildVendorRecord(Record, "s=c0", "V=u");
    AppendRecord(Otp, sizeof(Otp) - 8, BCMBT_OTP_SYS_VENDOR, Record, Length);
    ok_hex(BcmBtParseOtp(Otp, sizeof(Otp), Stepping, Vendor), STATUS_NOT_FOUND);

    memset(Otp, 0, sizeof(Otp));
    Otp[0] = 0x80;
    Otp[1] = 4;
    Length = BuildVendorRecord(Record, "s=c0", "V=u");
    AppendRecord(Otp, 8, BCMBT_OTP_SYS_VENDOR, Record, Length);
    ok_hex(BcmBtParseOtp(Otp, sizeof(Otp), Stepping, Vendor), STATUS_NOT_FOUND);
}

static
VOID
TestAddressAndFixups(VOID)
{
    const BCMBT_CHIP *Chip = BcmBtFindChip(0x5f72);
    UCHAR Address[6] = { 0x93, 0x76, 0x00, 0x5a, 0x88, 0x43 };
    UCHAR Features[14] = { 0x0e, 0x0c, 0x01, 0x03, 0x20, 0x00, 0xff, 0xff, 0x01, 0, 0, 0, 0, 0 };
    UCHAR Report[4 + 2 * 26];
    ULONG Offset;

    ok_int(BcmBtIsDefaultAddress(Chip, Address), TRUE);
    Address[3] = 0x00;
    ok_int(BcmBtIsDefaultAddress(Chip, Address), TRUE);
    Address[5] = 0x44;
    ok_int(BcmBtIsDefaultAddress(Chip, Address), FALSE);
    Address[5] = 0x43;
    Address[0] = 0x94;
    ok_int(BcmBtIsDefaultAddress(Chip, Address), FALSE);

    BcmBtFixupEvent(Features, sizeof(Features));
    ok_hex(Features[6], 0xff);
    ok_hex(Features[7], 0xf7);
    ok_hex(Features[8], 0x01);
    Features[5] = 0x01;
    Features[7] = 0xff;
    BcmBtFixupEvent(Features, sizeof(Features));
    ok_hex(Features[7], 0xff);
    Features[5] = 0x00;
    Features[3] = 0x02;
    BcmBtFixupEvent(Features, sizeof(Features));
    ok_hex(Features[7], 0xff);
    Features[3] = 0x03;
    BcmBtFixupEvent(Features, 13);
    ok_hex(Features[7], 0xff);

    memset(Report, 0, sizeof(Report));
    Report[0] = 0x3e;
    Report[1] = sizeof(Report) - 2;
    Report[2] = 0x0d;
    Report[3] = 2;
    Report[4 + 9] = 0xe1;
    Report[4 + 10] = 0xe3;
    Report[4 + 23] = 2;
    Offset = 4 + 24 + 2;
    Report[Offset + 9] = 0xa1;
    Report[Offset + 10] = 0x42;
    Report[Offset + 11] = 0xee;
    BcmBtFixupEvent(Report, Offset + 24);
    ok_hex(Report[4 + 9], 0x01);
    ok_hex(Report[4 + 10], 0x03);
    ok_hex(Report[4 + 24], 0x00);
    ok_hex(Report[Offset + 9], 0x01);
    ok_hex(Report[Offset + 10], 0x02);
    ok_hex(Report[Offset + 11], 0xee);

    Report[4 + 9] = 0xe1;
    Report[Offset + 9] = 0xa1;
    BcmBtFixupEvent(Report, Offset + 23);
    ok_hex(Report[4 + 9], 0x01);
    ok_hex(Report[Offset + 9], 0xa1);
    Report[2] = 0x02;
    Report[4 + 9] = 0xe1;
    BcmBtFixupEvent(Report, sizeof(Report));
    ok_hex(Report[4 + 9], 0xe1);
}

static
VOID
TestTransferRing(VOID)
{
    static UCHAR RingMemory[BCMBT_RING_ENTRIES * (BCMBT_ENTRY_SIZE + BCMBT_EVENT_PAYLOAD_SIZE)];
    static UCHAR PayloadMemory[BCMBT_RING_ENTRIES * BCMBT_ACL_PAYLOAD_SIZE];
    UCHAR Data[BCMBT_ACL_PAYLOAD_SIZE + 1];
    BCMBT_TRANSFER_RING Ring;
    PUCHAR Entry;
    ULONG Slot, Index;
    LONG Allocated;

    memset(&Ring, 0, sizeof(Ring));
    Ring.Id = BCMBT_XFER_HCI_H2D;
    Ring.Entries = BCMBT_RING_ENTRIES;
    Ring.FooterSize = BCMBT_EVENT_PAYLOAD_SIZE;
    Ring.Generation = 3;
    Ring.Ring = RingMemory;
    for (Index = 0; Index < sizeof(Data); Index++)
        Data[Index] = (UCHAR)(Index * 7);

    ok_int(BcmBtTransferEntrySize(&Ring), 280);
    ok_hex(BcmBtWriteTransferEntry(&Ring, 5, 9, Data, 6), STATUS_SUCCESS);
    Entry = RingMemory + 5 * 280;
    ok_hex(Entry[0], BCMBT_ENTRY_FLAG_IN_FOOTER);
    ok_hex(Get16(Entry + 1), 6);
    ok_hex(Get64(Entry + 4), 0);
    ok_hex(Get16(Entry + 12), 0x0309);
    ok(memcmp(Entry + BCMBT_ENTRY_SIZE, Data, 6) == 0, "Footer payload not copied\n");
    ok_hex(BcmBtWriteTransferEntry(&Ring, 5, 9, Data, BCMBT_EVENT_PAYLOAD_SIZE + 1), STATUS_INVALID_PARAMETER);
    ok_hex(BcmBtWriteTransferEntry(&Ring, BCMBT_RING_ENTRIES, 9, Data, 6), STATUS_INVALID_PARAMETER);
    ok_hex(BcmBtWriteTransferEntry(&Ring, 5, BCMBT_RING_ENTRIES, Data, 6), STATUS_INVALID_PARAMETER);

    memset(&Ring, 0, sizeof(Ring));
    Ring.Id = BCMBT_XFER_ACL_H2D;
    Ring.Entries = BCMBT_RING_ENTRIES;
    Ring.MappedSize = BCMBT_ACL_PAYLOAD_SIZE;
    Ring.Generation = 1;
    Ring.Ring = RingMemory;
    for (Index = 0; Index < BCMBT_PAYLOAD_CHUNKS; Index++)
    {
        Ring.PayloadChunk[Index] = PayloadMemory + (BCMBT_PAYLOAD_CHUNKS - 1 - Index) * BCMBT_PAYLOAD_CHUNK_SLOTS *
                                                   BCMBT_ACL_PAYLOAD_SIZE;
        Ring.PayloadChunkIova[Index] = 0x01004000 + (ULONG64)Index * 0x10000;
    }
    ok_int(BcmBtTransferEntrySize(&Ring), BCMBT_ENTRY_SIZE);
    ok_hex(BcmBtWriteTransferEntry(&Ring, 127, 2, Data, BCMBT_ACL_PAYLOAD_SIZE), STATUS_SUCCESS);
    Entry = RingMemory + 127 * BCMBT_ENTRY_SIZE;
    ok_hex(Entry[0], BCMBT_ENTRY_FLAG_MAPPED);
    ok_hex(Get16(Entry + 1), BCMBT_ACL_PAYLOAD_SIZE);
    ok(Get64(Entry + 4) == 0x01004000 + 2 * BCMBT_ACL_PAYLOAD_SIZE, "Mapped payload at %I64x\n", Get64(Entry + 4));
    ok_hex(Get16(Entry + 12), 0x0102);
    ok(memcmp(PayloadMemory + (3 * BCMBT_PAYLOAD_CHUNK_SLOTS + 2) * BCMBT_ACL_PAYLOAD_SIZE, Data, BCMBT_ACL_PAYLOAD_SIZE) == 0,
       "Mapped payload not copied\n");
    ok_hex(BcmBtWriteTransferEntry(&Ring, 126, 40, Data, 16), STATUS_SUCCESS);
    Entry = RingMemory + 126 * BCMBT_ENTRY_SIZE;
    ok_hex(Get16(Entry + 1), 16);
    ok(Get64(Entry + 4) == 0x01014000 + 8 * BCMBT_ACL_PAYLOAD_SIZE, "Mapped payload of slot 40 at %I64x\n", Get64(Entry + 4));
    ok_hex(Get16(Entry + 12), 0x0128);
    ok(memcmp(PayloadMemory + (2 * BCMBT_PAYLOAD_CHUNK_SLOTS + 8) * BCMBT_ACL_PAYLOAD_SIZE, Data, 16) == 0,
       "Mapped payload of slot 40 not copied\n");
    ok(BcmBtPayload(&Ring, 127) == PayloadMemory + 31 * BCMBT_ACL_PAYLOAD_SIZE, "Slot 127 payload misplaced\n");
    ok(BcmBtPayloadIova(&Ring, 127) == 0x01034000 + 31 * BCMBT_ACL_PAYLOAD_SIZE, "Slot 127 payload at %I64x\n",
       BcmBtPayloadIova(&Ring, 127));
    ok_hex(BcmBtWriteTransferEntry(&Ring, 0, 2, Data, BCMBT_ACL_PAYLOAD_SIZE + 1), STATUS_INVALID_PARAMETER);

    for (Index = 0; Index < BCMBT_RING_ENTRIES; Index++)
    {
        Allocated = BcmBtAllocateSlot(&Ring);
        if (Allocated != (LONG)Index)
        {
            ok(FALSE, "Slot %lu allocated as %ld\n", Index, Allocated);
            break;
        }
    }
    ok_int(BcmBtAllocateSlot(&Ring), -1);
    ok_int(BcmBtReleaseSlot(&Ring, 77), TRUE);
    ok_int(BcmBtReleaseSlot(&Ring, 77), FALSE);
    ok_int(BcmBtReleaseSlot(&Ring, BCMBT_RING_ENTRIES), FALSE);
    ok_int(BcmBtAllocateSlot(&Ring), 77);

    ok_int(BcmBtMessageSlot(&Ring, 0x0142, &Slot), TRUE);
    ok_int(Slot, 0x42);
    ok_int(BcmBtMessageSlot(&Ring, 0x0242, &Slot), FALSE);
    ok_int(BcmBtMessageSlot(&Ring, 0x0180, &Slot), FALSE);

    memset(&Ring, 0, sizeof(Ring));
    Ring.Id = BCMBT_XFER_ACL_D2H;
    Ring.Entries = BCMBT_RING_ENTRIES;
    Ring.MappedSize = BCMBT_ACL_PAYLOAD_SIZE;
    Ring.BuffersOnly = TRUE;
    Ring.Generation = 1;
    Ring.Ring = RingMemory;
    for (Index = 0; Index < BCMBT_PAYLOAD_CHUNKS; Index++)
        Ring.PayloadChunkIova[Index] = 0x02000000 + (ULONG64)Index * 0x100000;
    BcmBtWriteBufferEntries(&Ring);
    for (Index = 0; Index < BCMBT_RING_ENTRIES; Index++)
    {
        ULONG64 Expected = 0x02000000 + (ULONG64)(Index / BCMBT_PAYLOAD_CHUNK_SLOTS) * 0x100000 +
                           (ULONG64)(Index % BCMBT_PAYLOAD_CHUNK_SLOTS) * BCMBT_ACL_PAYLOAD_SIZE;

        Entry = RingMemory + Index * BCMBT_ENTRY_SIZE;
        if (Entry[0] != BCMBT_ENTRY_FLAG_MAPPED || Get16(Entry + 1) != BCMBT_ACL_PAYLOAD_SIZE ||
            Get64(Entry + 4) != Expected || Get16(Entry + 12) != (0x100 | Index))
        {
            ok(FALSE, "Buffer entry %lu is %02x %04x %I64x %04x\n", Index, Entry[0], Get16(Entry + 1), Get64(Entry + 4),
               Get16(Entry + 12));
            break;
        }
    }
}

static
VOID
TestCompletionRing(VOID)
{
    static UCHAR RingMemory[2 * BCMBT_RING_ENTRIES * (BCMBT_ENTRY_SIZE + BCMBT_EVENT_PAYLOAD_SIZE)];
    BCMBT_COMPLETION_RING Ring;
    BCMBT_COMPLETION Completion;
    PUCHAR Entry;

    memset(&Ring, 0, sizeof(Ring));
    Ring.Id = BCMBT_COMPLETION_HCI_ACL_EVENT;
    Ring.Entries = 2 * BCMBT_RING_ENTRIES;
    Ring.FooterSize = BCMBT_EVENT_PAYLOAD_SIZE;
    Ring.TransferMask = (1UL << BCMBT_XFER_HCI_D2H) | (1UL << BCMBT_XFER_ACL_D2H);
    Ring.Ring = RingMemory;
    ok_int(BcmBtCompletionEntrySize(&Ring), 280);

    Entry = RingMemory + 255 * 280;
    Entry[0] = BCMBT_ENTRY_FLAG_IN_FOOTER;
    Entry[2] = BCMBT_XFER_HCI_D2H;
    Entry[4] = 0x07;
    Entry[5] = 0x01;
    Entry[6] = 0x0e;
    Entry[7] = 0x01;
    Entry[16] = 0x0e;
    ok_hex(BcmBtReadCompletion(&Ring, 255, &Completion), STATUS_SUCCESS);
    ok_hex(Completion.Flags, BCMBT_ENTRY_FLAG_IN_FOOTER);
    ok_int(Completion.TransferRing, BCMBT_XFER_HCI_D2H);
    ok_hex(Completion.MessageId, 0x0107);
    ok_hex(Completion.Length, 0x010e);
    ok(Completion.Footer == Entry + 16, "Footer at %p, expected %p\n", Completion.Footer, Entry + 16);

    Entry[2] = BCMBT_XFER_ACL_H2D;
    ok_hex(BcmBtReadCompletion(&Ring, 255, &Completion), STATUS_INVALID_PARAMETER);
    Entry[2] = 0;
    Entry[3] = 1;
    ok_hex(BcmBtReadCompletion(&Ring, 255, &Completion), STATUS_INVALID_PARAMETER);
    ok_hex(BcmBtReadCompletion(&Ring, 256, &Completion), STATUS_INVALID_PARAMETER);
}

static
VOID
TestControlMessages(VOID)
{
    UCHAR Message[BCMBT_CONTROL_MESSAGE_SIZE], Context[BCMBT_CONTEXT_SIZE];
    BCMBT_TRANSFER_RING Transfer, Control;
    BCMBT_COMPLETION_RING Completion, ControlAck;
    ULONG Index;

    ok_int(BCMBT_CONTROL_MESSAGE_SIZE, 0x34);
    ok_int(BCMBT_CONTEXT_SIZE, 0x68);
    ok_int(sizeof(BCMBT_RING_STATE), 60);
    ok_int(FIELD_OFFSET(BCMBT_RING_STATE, CompletionTail), 12);
    ok_int(FIELD_OFFSET(BCMBT_RING_STATE, TransferHead), 24);
    ok_int(FIELD_OFFSET(BCMBT_RING_STATE, TransferTail), 42);

    memset(&Transfer, 0, sizeof(Transfer));
    Transfer.Id = BCMBT_XFER_SCO_D2H;
    Transfer.Doorbell = BCMBT_DOORBELL_SCO;
    Transfer.CompletionRing = BCMBT_COMPLETION_SCO_EVENT;
    Transfer.Entries = BCMBT_RING_ENTRIES;
    Transfer.FooterSize = BCMBT_SCO_PAYLOAD_SIZE;
    Transfer.Virtual = TRUE;
    Transfer.Sync = TRUE;
    Transfer.RingIova = 0x0100c000;
    memset(Message, 0xcc, sizeof(Message));
    BcmBtBuildCreateTransferRing(&Transfer, Message);
    ok_hex(Message[0], BCMBT_MSG_CREATE_TRANSFER_RING);
    ok_hex(Message[1], 0);
    ok_hex(Message[2], BCMBT_SCO_PAYLOAD_SIZE / 4);
    ok_hex(Get16(Message + 4), BCMBT_XFER_SCO_D2H);
    ok_hex(Get16(Message + 6), BCMBT_XFER_SCO_D2H);
    ok(Get64(Message + 8) == 0x0100c000, "Ring address %I64x\n", Get64(Message + 8));
    ok_hex(Get16(Message + 24), BCMBT_RING_ENTRIES);
    ok_hex(Get16(Message + 26), BCMBT_COMPLETION_SCO_EVENT);
    ok_hex(Get16(Message + 28), BCMBT_DOORBELL_SCO);
    ok_hex(Get16(Message + 30), 0x0180);
    for (Index = 32; Index < sizeof(Message) && !Message[Index]; Index++)
        ;
    ok_int(Index, sizeof(Message));

    memset(&Completion, 0, sizeof(Completion));
    Completion.Id = BCMBT_COMPLETION_HCI_ACL_EVENT;
    Completion.Entries = 2 * BCMBT_RING_ENTRIES;
    Completion.FooterSize = BCMBT_EVENT_PAYLOAD_SIZE;
    Completion.Delay = 1000;
    Completion.RingIova = 0x01010000;
    memset(Message, 0xcc, sizeof(Message));
    BcmBtBuildCreateCompletionRing(&Completion, Message);
    ok_hex(Message[0], BCMBT_MSG_CREATE_COMPLETION_RING);
    ok_hex(Message[2], BCMBT_EVENT_PAYLOAD_SIZE / 4);
    ok_hex(Get16(Message + 4), BCMBT_COMPLETION_HCI_ACL_EVENT);
    ok_hex(Get16(Message + 6), BCMBT_COMPLETION_HCI_ACL_EVENT);
    ok(Get64(Message + 8) == 0x01010000, "Ring address %I64x\n", Get64(Message + 8));
    ok_hex(Get16(Message + 16), 2 * BCMBT_RING_ENTRIES);
    ok_hex(Get32(Message + 18), 0xffffffff);
    ok_hex(Get16(Message + 28), 0);
    ok_hex(Get16(Message + 30), 1000);
    ok_hex(Get32(Message + 32), 0xffffffff);
    ok_hex(Get16(Message + 36), 0);

    memset(Message, 0xcc, sizeof(Message));
    BcmBtBuildDestroyRing(BCMBT_MSG_DESTROY_COMPLETION_RING, BCMBT_COMPLETION_SCO_ACK, Message);
    ok_hex(Message[0], BCMBT_MSG_DESTROY_COMPLETION_RING);
    ok_hex(Message[1], 0);
    ok_hex(Get16(Message + 2), BCMBT_COMPLETION_SCO_ACK);
    ok_hex(Message[sizeof(Message) - 1], 0);

    memset(&Control, 0, sizeof(Control));
    Control.Entries = BCMBT_RING_ENTRIES;
    Control.Doorbell = BCMBT_DOORBELL_CONTROL;
    Control.FooterSize = BCMBT_CONTROL_MESSAGE_SIZE;
    Control.RingIova = 0x01020000;
    memset(&ControlAck, 0, sizeof(ControlAck));
    ControlAck.Entries = 32;
    ControlAck.RingIova = 0x01030000;
    memset(Context, 0xcc, sizeof(Context));
    BcmBtBuildContext(Context, 0x01001000, 0x01002000, &Control, &ControlAck);
    ok_hex(Get16(Context + 0x00), 1);
    ok_hex(Get16(Context + 0x02), 0x68);
    ok_hex(Get32(Context + 0x04), 2);
    ok(Get64(Context + 0x08) == 0x01002000, "Peripheral info at %I64x\n", Get64(Context + 0x08));
    ok(Get64(Context + 0x10) == 0x01001000, "Completion heads at %I64x\n", Get64(Context + 0x10));
    ok(Get64(Context + 0x18) == 0x01001000 + 42, "Transfer tails at %I64x\n", Get64(Context + 0x18));
    ok(Get64(Context + 0x20) == 0x01001000 + 12, "Completion tails at %I64x\n", Get64(Context + 0x20));
    ok(Get64(Context + 0x28) == 0x01001000 + 24, "Transfer heads at %I64x\n", Get64(Context + 0x28));
    ok_hex(Get16(Context + 0x30), 6);
    ok_hex(Get16(Context + 0x32), 9);
    ok(Get64(Context + 0x34) == 0x01030000, "Control completion ring at %I64x\n", Get64(Context + 0x34));
    ok(Get64(Context + 0x3c) == 0x01020000, "Control transfer ring at %I64x\n", Get64(Context + 0x3c));
    ok_hex(Get16(Context + 0x44), BCMBT_RING_ENTRIES);
    ok_hex(Get16(Context + 0x46), 32);
    ok_hex(Get16(Context + 0x48), BCMBT_DOORBELL_CONTROL);
    ok_hex(Get16(Context + 0x4a), 0xffff);
    ok_hex(Get16(Context + 0x4c), 0);
    ok_hex(Get16(Context + 0x4e), 0);
    ok_hex(Context[0x50], 0);
    ok_hex(Context[0x51], BCMBT_CONTROL_MESSAGE_SIZE / 4);
    ok_hex(Context[0x52], 0);
    ok_hex(Context[0x53], 0);
    for (Index = 0x54; Index < sizeof(Context) && !Context[Index]; Index++)
        ;
    ok_int(Index, sizeof(Context));

    ok_int(BcmBtChunkCount(0, BCMBT_CALIBRATION_CHUNK), 0);
    ok_int(BcmBtChunkCount(BCMBT_CALIBRATION_CHUNK, BCMBT_CALIBRATION_CHUNK), 1);
    ok_int(BcmBtChunkCount(BCMBT_CALIBRATION_CHUNK + 1, BCMBT_CALIBRATION_CHUNK), 2);
    ok_int(BcmBtChunkCount(1000, BCMBT_PTB_CHUNK), 5);
}

static
VOID
TestDmaLimit(VOID)
{
    ok(BcmBtHighestDmaAddress(TRUE) == MAXLONGLONG, "Translated limit is %I64x\n", BcmBtHighestDmaAddress(TRUE));
    ok(BcmBtHighestDmaAddress(TRUE) > 0x108d9a17fffLL, "Translated limit %I64x excludes RAM above 1 TB\n",
       BcmBtHighestDmaAddress(TRUE));
    ok(BcmBtHighestDmaAddress(FALSE) == 0xfffffdffLL, "Direct limit is %I64x\n", BcmBtHighestDmaAddress(FALSE));
}

START_TEST(BcmPciBt)
{
    TestDmaLimit();
    TestChips();
    TestOtp();
    TestAddressAndFixups();
    TestTransferRing();
    TestCompletionRing();
    TestControlMessages();
}
