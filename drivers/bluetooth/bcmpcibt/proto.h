/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared-memory message formats and chip parameters of the Broadcom PCIe Bluetooth function
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BCMBT_PCI_BAR0_WINDOW1          0x80
#define BCMBT_PCI_BAR0_WINDOW2          0x70
#define BCMBT_PCI_BAR0_CORE2_WINDOW1    0x74
#define BCMBT_PCI_BAR0_CORE2_WINDOW2    0x78
#define BCMBT_PCI_BAR2_WINDOW           0x84
#define BCMBT_PCI_SUBSYSTEM_CONTROL     0x88
#define BCMBT_CORE2_WINDOW1_DEFAULT     0x18011000
#define BCMBT_BAR2_WINDOW_DEFAULT       0x19000000

#define BCMBT_BAR0_FW_DOORBELL          0x140
#define BCMBT_BAR0_RTI_CONTROL          0x144
#define BCMBT_BAR0_SLEEP_CONTROL        0x150
#define BCMBT_BAR0_DOORBELL             0x174
#define BCMBT_BAR0_HOST_WINDOW_LO       0x590
#define BCMBT_BAR0_HOST_WINDOW_HI       0x594
#define BCMBT_BAR0_HOST_WINDOW_SIZE     0x598

#define BCMBT_SLEEP_UNQUIESCE           0
#define BCMBT_SLEEP_AWAKE               2
#define BCMBT_SLEEP_QUIESCE             3

#define BCMBT_BAR2_CONTEXT_ADDR_HI      0x200450
#define BCMBT_BAR2_BOOTSTAGE            0x200454
#define BCMBT_BAR2_RTI_STATUS           0x20045c
#define BCMBT_BAR2_FW_LO                0x200478
#define BCMBT_BAR2_FW_HI                0x20047c
#define BCMBT_BAR2_FW_SIZE              0x200480
#define BCMBT_BAR2_CONTEXT_ADDR_LO      0x20048c
#define BCMBT_BAR2_RTI_WINDOW_LO        0x200494
#define BCMBT_BAR2_RTI_WINDOW_HI        0x200498
#define BCMBT_BAR2_RTI_WINDOW_SIZE      0x20049c

#define BCMBT_DMA_WINDOW_SIZE           0xfffffe00UL

#define BCMBT_OTP_SIZE                  0xe0
#define BCMBT_OTP_SYS_VENDOR            0x15
#define BCMBT_OTP_VENDOR_HEADER         0x00000008UL
#define BCMBT_OTP_PARAM_LENGTH          16

#define BCMBT_TRANSFER_RINGS            9
#define BCMBT_COMPLETION_RINGS          6
#define BCMBT_MAX_RING_ENTRIES          256
#define BCMBT_RING_ENTRIES              128
#define BCMBT_PAYLOAD_CHUNK_SLOTS       32
#define BCMBT_PAYLOAD_CHUNKS            (BCMBT_RING_ENTRIES / BCMBT_PAYLOAD_CHUNK_SLOTS)

#define BCMBT_ENTRY_SIZE                0x10
#define BCMBT_CONTROL_MESSAGE_SIZE      0x34
#define BCMBT_CONTEXT_SIZE              0x68
#define BCMBT_PERIPHERAL_INFO_SIZE      0x20
#define BCMBT_MAX_FOOTER_SIZE           (4 * 0xff)

#define BCMBT_EVENT_PAYLOAD_SIZE        264
#define BCMBT_SCO_PAYLOAD_SIZE          260
#define BCMBT_ACL_PAYLOAD_SIZE          1032

#define BCMBT_ENTRY_FLAG_MAPPED         0x01
#define BCMBT_ENTRY_FLAG_IN_FOOTER      0x02
#define BCMBT_RING_FLAG_VIRTUAL         0x0080
#define BCMBT_RING_FLAG_SYNC            0x0100

#define BCMBT_MSG_CREATE_TRANSFER_RING      1
#define BCMBT_MSG_CREATE_COMPLETION_RING    2
#define BCMBT_MSG_DESTROY_TRANSFER_RING     3
#define BCMBT_MSG_DESTROY_COMPLETION_RING   4

#define BCMBT_XFER_CONTROL              0
#define BCMBT_XFER_HCI_H2D              1
#define BCMBT_XFER_HCI_D2H              2
#define BCMBT_XFER_SCO_H2D              3
#define BCMBT_XFER_SCO_D2H              4
#define BCMBT_XFER_ACL_H2D              5
#define BCMBT_XFER_ACL_D2H              6

#define BCMBT_COMPLETION_CONTROL_ACK    0
#define BCMBT_COMPLETION_HCI_ACL_ACK    1
#define BCMBT_COMPLETION_HCI_ACL_EVENT  2
#define BCMBT_COMPLETION_SCO_ACK        3
#define BCMBT_COMPLETION_SCO_EVENT      4

#define BCMBT_DOORBELL_CONTROL          0
#define BCMBT_DOORBELL_HCI_H2D          1
#define BCMBT_DOORBELL_HCI_D2H          2
#define BCMBT_DOORBELL_ACL_H2D          3
#define BCMBT_DOORBELL_ACL_D2H          4
#define BCMBT_DOORBELL_SCO              6

#define BCMBT_D2H_INITIAL_HEAD          0xf

#define BCMBT_HCI_CALIBRATION           0xfd97
#define BCMBT_HCI_PTB                   0xfe0d
#define BCMBT_HCI_WRITE_BD_ADDR         0xfc01
#define BCMBT_HCI_READ_BD_ADDR          0x1009
#define BCMBT_HCI_LE_READ_FEATURES      0x2003
#define BCMBT_CALIBRATION_CHUNK         0xe6
#define BCMBT_PTB_CHUNK                 0xcf

#define BCMBT_CONTEXT_PERIPHERAL_INFO       0x08
#define BCMBT_CONTEXT_COMPLETION_HEADS      0x10
#define BCMBT_CONTEXT_TRANSFER_TAILS        0x18
#define BCMBT_CONTEXT_COMPLETION_TAILS      0x20
#define BCMBT_CONTEXT_TRANSFER_HEADS        0x28
#define BCMBT_CONTEXT_COMPLETION_COUNT      0x30
#define BCMBT_CONTEXT_TRANSFER_COUNT        0x32
#define BCMBT_CONTEXT_CONTROL_COMPLETION    0x34
#define BCMBT_CONTEXT_CONTROL_TRANSFER      0x3c
#define BCMBT_CONTEXT_CONTROL_LAYOUT        0x44

typedef struct _BCMBT_RING_STATE
{
    volatile USHORT CompletionHead[BCMBT_COMPLETION_RINGS];
    volatile USHORT CompletionTail[BCMBT_COMPLETION_RINGS];
    volatile USHORT TransferHead[BCMBT_TRANSFER_RINGS];
    volatile USHORT TransferTail[BCMBT_TRANSFER_RINGS];
} BCMBT_RING_STATE, *PBCMBT_RING_STATE;

typedef struct _BCMBT_CHIP
{
    USHORT DeviceId;
    USHORT ChipId;
    ULONG OtpOffset;
    ULONG Bar0Window1;
    ULONG Bar0Window2;
    ULONG Bar0Core2Window2;
    ULONG Bar2Offset;
    BOOLEAN ClearSubsystemBit19;
    PCSTR BeamformingSteppings;
} BCMBT_CHIP, *PBCMBT_CHIP;

typedef struct _BCMBT_TRANSFER_RING
{
    UCHAR Id;
    UCHAR Doorbell;
    UCHAR CompletionRing;
    UCHAR Generation;
    USHORT Entries;
    USHORT FooterSize;
    ULONG MappedSize;
    BOOLEAN Virtual;
    BOOLEAN Sync;
    BOOLEAN BuffersOnly;
    BOOLEAN Enabled;
    PUCHAR Ring;
    ULONG64 RingIova;
    PUCHAR PayloadChunk[BCMBT_PAYLOAD_CHUNKS];
    ULONG64 PayloadChunkIova[BCMBT_PAYLOAD_CHUNKS];
    ULONG InUse[BCMBT_MAX_RING_ENTRIES / 32];
    PVOID Waiters[BCMBT_RING_ENTRIES];
} BCMBT_TRANSFER_RING, *PBCMBT_TRANSFER_RING;

typedef struct _BCMBT_COMPLETION_RING
{
    UCHAR Id;
    BOOLEAN Enabled;
    BOOLEAN Desynchronised;
    USHORT Entries;
    USHORT FooterSize;
    USHORT Delay;
    ULONG TransferMask;
    PUCHAR Ring;
    ULONG64 RingIova;
} BCMBT_COMPLETION_RING, *PBCMBT_COMPLETION_RING;

typedef struct _BCMBT_COMPLETION
{
    UCHAR Flags;
    USHORT TransferRing;
    USHORT MessageId;
    ULONG Length;
    const UCHAR *Footer;
} BCMBT_COMPLETION, *PBCMBT_COMPLETION;

const BCMBT_CHIP *
BcmBtFindChip(
    _In_ USHORT DeviceId);

NTSTATUS
BcmBtParseOtp(
    _In_reads_bytes_(Size) const UCHAR *Otp,
    _In_ ULONG Size,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Stepping,
    _Out_writes_z_(BCMBT_OTP_PARAM_LENGTH) PCHAR Vendor);

BOOLEAN
BcmBtUsesBeamforming(
    _In_ const BCMBT_CHIP *Chip,
    _In_z_ PCSTR Stepping);

BOOLEAN
BcmBtIsDefaultAddress(
    _In_ const BCMBT_CHIP *Chip,
    _In_reads_bytes_(6) const UCHAR *Address);

VOID
BcmBtFixupEvent(
    _Inout_updates_bytes_(Length) PUCHAR Event,
    _In_ ULONG Length);

ULONG
BcmBtTransferEntrySize(
    _In_ const BCMBT_TRANSFER_RING *Ring);

ULONG
BcmBtCompletionEntrySize(
    _In_ const BCMBT_COMPLETION_RING *Ring);

USHORT
BcmBtMessageId(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot);

BOOLEAN
BcmBtMessageSlot(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ USHORT MessageId,
    _Out_ PULONG Slot);

PUCHAR
BcmBtPayload(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot);

ULONG64
BcmBtPayloadIova(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _In_ ULONG Slot);

LONGLONG
BcmBtHighestDmaAddress(
    _In_ BOOLEAN Translated);

LONG
BcmBtAllocateSlot(
    _Inout_ PBCMBT_TRANSFER_RING Ring);

BOOLEAN
BcmBtReleaseSlot(
    _Inout_ PBCMBT_TRANSFER_RING Ring,
    _In_ ULONG Slot);

NTSTATUS
BcmBtWriteTransferEntry(
    _Inout_ PBCMBT_TRANSFER_RING Ring,
    _In_ USHORT Head,
    _In_ ULONG Slot,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length);

VOID
BcmBtWriteBufferEntries(
    _Inout_ PBCMBT_TRANSFER_RING Ring);

NTSTATUS
BcmBtReadCompletion(
    _In_ const BCMBT_COMPLETION_RING *Ring,
    _In_ USHORT Position,
    _Out_ PBCMBT_COMPLETION Completion);

VOID
BcmBtBuildCreateTransferRing(
    _In_ const BCMBT_TRANSFER_RING *Ring,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message);

VOID
BcmBtBuildCreateCompletionRing(
    _In_ const BCMBT_COMPLETION_RING *Ring,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message);

VOID
BcmBtBuildDestroyRing(
    _In_ UCHAR Type,
    _In_ USHORT RingId,
    _Out_writes_bytes_(BCMBT_CONTROL_MESSAGE_SIZE) PUCHAR Message);

VOID
BcmBtBuildContext(
    _Out_writes_bytes_(BCMBT_CONTEXT_SIZE) PUCHAR Context,
    _In_ ULONG64 RingStateIova,
    _In_ ULONG64 PeripheralInfoIova,
    _In_ const BCMBT_TRANSFER_RING *Control,
    _In_ const BCMBT_COMPLETION_RING *ControlAck);

ULONG
BcmBtChunkCount(
    _In_ ULONG Size,
    _In_ ULONG ChunkSize);
