/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon audio DMA controller (ADMAC)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appleadmac.h>

#define ADMAC_MAX_CHANNELS          64
#define ADMAC_INTERRUPT_OUTPUTS     4
#define ADMAC_SRAM_BLOCK            2048
#define ADMAC_RING_SLOTS            4
#define ADMAC_MAX_CELLS             4

#define ADMAC_TX_START              0x0000
#define ADMAC_TX_STOP               0x0004
#define ADMAC_RX_START              0x0008
#define ADMAC_RX_STOP               0x000c
#define ADMAC_FIFO_LIMIT_A          0x0028
#define ADMAC_FIFO_LIMIT_B          0x002c
#define ADMAC_FIFO_LIMIT_VALUE      0x00200000
#define ADMAC_TX_INTERRUPTS(i)      (0x0030 + (i) * 4)
#define ADMAC_RX_INTERRUPTS(i)      (0x0040 + (i) * 4)
#define ADMAC_GLOBAL_INTERRUPTS(i)  (0x0050 + (i) * 4)
#define ADMAC_TX_SRAM_SIZE          0x0094
#define ADMAC_RX_SRAM_SIZE          0x0098
#define ADMAC_CHANNEL(c)            (0x8000 + (c) * 0x200)
#define ADMAC_CHANNEL_CONTROL       0x00
#define ADMAC_CHANNEL_RESET_RINGS   0x00000001
#define ADMAC_CHANNEL_STATUS(i)     (0x10 + (i) * 4)
#define ADMAC_CHANNEL_MASK(i)       (0x20 + (i) * 4)
#define ADMAC_STATUS_DESC_DONE      0x00000001
#define ADMAC_STATUS_ERROR          0x00000040
#define ADMAC_CHANNEL_BUS_WIDTH     0x40
#define ADMAC_BUS_WIDTH_FIELDS      0x000000ff
#define ADMAC_BUS_WIDTH_16BIT       0x01
#define ADMAC_BUS_WIDTH_32BIT       0x02
#define ADMAC_BUS_WIDTH_FRAME_2     0x10
#define ADMAC_BUS_WIDTH_FRAME_4     0x20
#define ADMAC_CHANNEL_CARVEOUT      0x50
#define ADMAC_CHANNEL_FIFO          0x54
#define ADMAC_CHANNEL_RESIDUE       0x64
#define ADMAC_CHANNEL_DESC_RING     0x70
#define ADMAC_CHANNEL_REPORT_RING   0x74
#define ADMAC_RING_WRITE_SLOT(r)    ((r) & 3)
#define ADMAC_RING_READ_SLOT(r)     (((r) >> 4) & 3)
#define ADMAC_RING_EMPTY            0x00000100
#define ADMAC_RING_FULL             0x00000200
#define ADMAC_RING_ERROR            0x00000400
#define ADMAC_DESC_WRITE(c)         (0x10000 + ((c) / 2) * 4 + ((c) & 1) * 0x4000)
#define ADMAC_REPORT_READ(c)        (0x10100 + ((c) / 2) * 4 + ((c) & 1) * 0x4000)
#define ADMAC_DESC_NOTIFY           0x00010000
#define ADMAC_REGISTER_SPAN         0x14200

static
ULONG
AdmacRead(
    _In_ PAPPLE_ADMAC Admac,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Admac->Registers + Offset));
}

static
VOID
AdmacWrite(
    _In_ PAPPLE_ADMAC Admac,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Admac->Registers + Offset), Value);
}

NTSTATUS
NTAPI
AppleAdmacQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PULONG InterruptIndex,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    const UCHAR *Property;
    const CHAR *Compatible;
    ULONG Length, CompatibleLength, Offset = 0, Index, Controller, CellCount, Cell;
    ULONG Cells[ADMAC_MAX_CELLS];
    ULONG64 Value;

    *InterruptIndex = 0;
    *Gsi = 0;
    *EdgeTriggered = FALSE;
    Property = FdtGetProperty(&Tree->Fdt, Node, "interrupts-extended", &Length);
    for (Index = 0; Property && Index < ADMAC_INTERRUPT_OUTPUTS && Offset + sizeof(ULONG) <= Length; ++Index)
    {
        if (!FdtReadCells(Property, Length, Offset / sizeof(ULONG), 1, &Value))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Offset += sizeof(ULONG);
        if (!Value)
            continue;
        if ((Controller = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
            !FdtReadU32(&Tree->Fdt, Controller, "#interrupt-cells", &CellCount) ||
            !CellCount || CellCount > ADMAC_MAX_CELLS || Offset + CellCount * sizeof(ULONG) > Length)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        for (Cell = 0; Cell < CellCount; ++Cell)
        {
            if (!FdtReadCells(Property, Length, Offset / sizeof(ULONG) + Cell, 1, &Value))
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Cells[Cell] = (ULONG)Value;
        }
        Compatible = FdtGetProperty(&Tree->Fdt, Controller, "compatible", &CompatibleLength);
        if (!Compatible ||
            !HalArm64TranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, CellCount,
                                                  Gsi, EdgeTriggered))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        *InterruptIndex = Index;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
AppleAdmacOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_ADMAC Admac)
{
    const VOID *Compatible;
    ULONG Length;
    NTSTATUS Status;

    RtlZeroMemory(Admac, sizeof(*Admac));
    Compatible = FdtGetProperty(&Tree->Fdt, Node, "compatible", &Length);
    if (FdtStringListContains(Compatible, Length, "apple,t8122-admac"))
        Admac->SetFifoLimits = TRUE;
    else if (!FdtStringListContains(Compatible, Length, "apple,t8103-admac") &&
             !FdtStringListContains(Compatible, Length, "apple,admac"))
        return STATUS_NOT_SUPPORTED;
    if (!FdtReadU32(&Tree->Fdt, Node, "dma-channels", &Admac->ChannelCount) ||
        !Admac->ChannelCount || Admac->ChannelCount > ADMAC_MAX_CHANNELS)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = AppleAdmacQueryInterrupt(Tree, Node, &Admac->InterruptIndex, &Admac->InterruptGsi, &Admac->InterruptEdge);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ApplePmgrPowerOnDevice(Tree, Node);
    if (!NT_SUCCESS(Status))
        return Status;
    Admac->Registers = FdtDriverMapReg(Tree, Node, 0, NULL, &Admac->Size);
    if (!Admac->Registers)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Admac->Size < ADMAC_REGISTER_SPAN || Admac->Size < ADMAC_CHANNEL(Admac->ChannelCount))
    {
        AppleAdmacClose(Admac);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAdmacClose(
    _Inout_ PAPPLE_ADMAC Admac)
{
    if (Admac->Registers)
        MmUnmapIoSpace(Admac->Registers, Admac->Size);
    RtlZeroMemory(Admac, sizeof(*Admac));
}

static
VOID
AdmacResetRings(
    _In_ PAPPLE_ADMAC_CHANNEL Channel)
{
    AdmacWrite(Channel->Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_CONTROL, ADMAC_CHANNEL_RESET_RINGS);
    AdmacWrite(Channel->Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_CONTROL, 0);
}

NTSTATUS
NTAPI
AppleAdmacOpenChannel(
    _Inout_ PAPPLE_ADMAC Admac,
    _In_ ULONG Number,
    _In_ ULONG WordBytes,
    _In_ ULONG FrameWords,
    _Out_ PAPPLE_ADMAC_CHANNEL Channel)
{
    ULONG Direction = Number & 1, SramSize, Blocks, Block, BusWidth;

    RtlZeroMemory(Channel, sizeof(*Channel));
    if (!Admac->Registers || Number >= Admac->ChannelCount)
        return STATUS_INVALID_PARAMETER;
    switch (WordBytes)
    {
        case 2:
            BusWidth = ADMAC_BUS_WIDTH_16BIT;
            break;
        case 4:
            BusWidth = ADMAC_BUS_WIDTH_32BIT;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }
    switch (FrameWords)
    {
        case 0:
        case 1:
            break;
        case 2:
            BusWidth |= ADMAC_BUS_WIDTH_FRAME_2;
            break;
        case 4:
            BusWidth |= ADMAC_BUS_WIDTH_FRAME_4;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    SramSize = AdmacRead(Admac, Direction ? ADMAC_RX_SRAM_SIZE : ADMAC_TX_SRAM_SIZE);
    Blocks = min(SramSize / ADMAC_SRAM_BLOCK, 32);
    for (Block = 0; Block < Blocks && (Admac->SramUsed[Direction] & (1UL << Block)); ++Block)
        ;
    if (Block >= Blocks)
        return STATUS_INSUFFICIENT_RESOURCES;
    Admac->SramUsed[Direction] |= 1UL << Block;

    Channel->Admac = Admac;
    Channel->Number = Number;
    Channel->Carveout = (ADMAC_SRAM_BLOCK << 16) | (Block * ADMAC_SRAM_BLOCK);
    AdmacWrite(Admac, ADMAC_CHANNEL(Number) + ADMAC_CHANNEL_CARVEOUT, Channel->Carveout);
    if (Admac->SetFifoLimits)
    {
        AdmacWrite(Admac, ADMAC_FIFO_LIMIT_A, ADMAC_FIFO_LIMIT_VALUE);
        AdmacWrite(Admac, ADMAC_FIFO_LIMIT_B, ADMAC_FIFO_LIMIT_VALUE);
    }
    AdmacWrite(Admac, ADMAC_CHANNEL(Number) + ADMAC_CHANNEL_BUS_WIDTH,
               (AdmacRead(Admac, ADMAC_CHANNEL(Number) + ADMAC_CHANNEL_BUS_WIDTH) & ~ADMAC_BUS_WIDTH_FIELDS) | BusWidth);
    AdmacWrite(Admac, ADMAC_CHANNEL(Number) + ADMAC_CHANNEL_FIFO, ((0x30 * WordBytes) << 16) | (0x18 * WordBytes));
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAdmacCloseChannel(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel)
{
    PAPPLE_ADMAC Admac = Channel->Admac;

    if (!Admac)
        return;
    AppleAdmacStop(Channel);
    Admac->SramUsed[Channel->Number & 1] &= ~(1UL << ((Channel->Carveout & 0xffff) / ADMAC_SRAM_BLOCK));
    RtlZeroMemory(Channel, sizeof(*Channel));
}

static
VOID
AdmacWriteDescriptor(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel)
{
    PAPPLE_ADMAC Admac = Channel->Admac;
    ULONG64 Address = Channel->Buffer + (Channel->Submitted % Channel->BufferLength);

    AdmacWrite(Admac, ADMAC_DESC_WRITE(Channel->Number), (ULONG)Address);
    AdmacWrite(Admac, ADMAC_DESC_WRITE(Channel->Number), (ULONG)(Address >> 32));
    AdmacWrite(Admac, ADMAC_DESC_WRITE(Channel->Number), Channel->PeriodLength);
    AdmacWrite(Admac, ADMAC_DESC_WRITE(Channel->Number), ADMAC_DESC_NOTIFY);
    Channel->Submitted = (Channel->Submitted + Channel->PeriodLength) % (2 * Channel->BufferLength);
}

static
VOID
AdmacFillRing(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel)
{
    ULONG Slot;

    for (Slot = 0; Slot < ADMAC_RING_SLOTS; ++Slot)
    {
        if (AdmacRead(Channel->Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_DESC_RING) & ADMAC_RING_FULL)
            break;
        AdmacWriteDescriptor(Channel);
    }
}

NTSTATUS
NTAPI
AppleAdmacStartCyclic(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength)
{
    PAPPLE_ADMAC Admac = Channel->Admac;
    ULONG Index;

    if (!Admac || !PeriodLength || !BufferLength || BufferLength % PeriodLength || BufferLength > MAXULONG / 2)
        return STATUS_INVALID_PARAMETER;
    Channel->Buffer = Buffer;
    Channel->BufferLength = BufferLength;
    Channel->PeriodLength = PeriodLength;
    Channel->Submitted = 0;
    Channel->Reclaimed = 0;
    Index = Admac->InterruptIndex;
    AdmacResetRings(Channel);
    AdmacWriteDescriptor(Channel);
    AdmacWrite(Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_STATUS(Index),
               ADMAC_STATUS_DESC_DONE | ADMAC_STATUS_ERROR);
    AdmacWrite(Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_MASK(Index),
               ADMAC_STATUS_DESC_DONE | ADMAC_STATUS_ERROR);
    AdmacWrite(Admac, (Channel->Number & 1) ? ADMAC_RX_START : ADMAC_TX_START, 1UL << (Channel->Number / 2));
    AdmacFillRing(Channel);
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleAdmacStop(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel)
{
    PAPPLE_ADMAC Admac = Channel->Admac;

    if (!Admac)
        return;
    AdmacWrite(Admac, (Channel->Number & 1) ? ADMAC_RX_STOP : ADMAC_TX_STOP, 1UL << (Channel->Number / 2));
    AdmacWrite(Admac, ADMAC_CHANNEL(Channel->Number) + ADMAC_CHANNEL_MASK(Admac->InterruptIndex), 0);
    AdmacResetRings(Channel);
    Channel->BufferLength = 0;
}

BOOLEAN
NTAPI
AppleAdmacInterruptPending(
    _In_ PAPPLE_ADMAC Admac,
    _In_ ULONG Number,
    _Out_ PBOOLEAN Global)
{
    ULONG Index;

    *Global = FALSE;
    if (!Admac->Registers)
        return FALSE;
    Index = Admac->InterruptIndex;
    if (AdmacRead(Admac, ADMAC_GLOBAL_INTERRUPTS(Index)))
    {
        AdmacWrite(Admac, ADMAC_GLOBAL_INTERRUPTS(Index), MAXULONG);
        *Global = TRUE;
    }
    return (AdmacRead(Admac, (Number & 1) ? ADMAC_RX_INTERRUPTS(Index) : ADMAC_TX_INTERRUPTS(Index)) &
            (1UL << (Number / 2))) != 0;
}

static
ULONG
AdmacOccupiedSlots(
    _In_ ULONG Ring)
{
    ULONG Write = ADMAC_RING_WRITE_SLOT(Ring), Read = ADMAC_RING_READ_SLOT(Ring);

    if (Write != Read)
        return (Write + ADMAC_RING_SLOTS - Read) % ADMAC_RING_SLOTS;
    return (Ring & ADMAC_RING_FULL) ? ADMAC_RING_SLOTS : 0;
}

ULONG
NTAPI
AppleAdmacService(
    _Inout_ PAPPLE_ADMAC_CHANNEL Channel)
{
    PAPPLE_ADMAC Admac = Channel->Admac;
    ULONG Base, Cause, Reports = 0, Word;
    BOOLEAN Cleared = FALSE;

    if (!Admac)
        return 0;
    Base = ADMAC_CHANNEL(Channel->Number);
    Cause = AdmacRead(Admac, Base + ADMAC_CHANNEL_STATUS(Admac->InterruptIndex));
    if (Cause & ADMAC_STATUS_ERROR)
    {
        if (AdmacRead(Admac, Base + ADMAC_CHANNEL_DESC_RING) & ADMAC_RING_ERROR)
        {
            AdmacWrite(Admac, Base + ADMAC_CHANNEL_DESC_RING, ADMAC_RING_ERROR);
            Cleared = TRUE;
        }
        if (AdmacRead(Admac, Base + ADMAC_CHANNEL_REPORT_RING) & ADMAC_RING_ERROR)
        {
            AdmacWrite(Admac, Base + ADMAC_CHANNEL_REPORT_RING, ADMAC_RING_ERROR);
            Cleared = TRUE;
        }
        if (!Cleared)
            AdmacWrite(Admac, Base + ADMAC_CHANNEL_MASK(Admac->InterruptIndex),
                       AdmacRead(Admac, Base + ADMAC_CHANNEL_MASK(Admac->InterruptIndex)) & ~ADMAC_STATUS_ERROR);
    }
    if (!(Cause & ADMAC_STATUS_DESC_DONE))
        return 0;
    AdmacWrite(Admac, Base + ADMAC_CHANNEL_STATUS(Admac->InterruptIndex), ADMAC_STATUS_DESC_DONE);
    while (Reports < ADMAC_RING_SLOTS && !(AdmacRead(Admac, Base + ADMAC_CHANNEL_REPORT_RING) & ADMAC_RING_EMPTY))
    {
        for (Word = 0; Word < 4; ++Word)
            (VOID)AdmacRead(Admac, ADMAC_REPORT_READ(Channel->Number));
        ++Reports;
    }
    if (!Channel->BufferLength)
        return Reports;
    Channel->Reclaimed = (Channel->Reclaimed + Reports * Channel->PeriodLength) % (2 * Channel->BufferLength);
    AdmacFillRing(Channel);
    return Reports;
}

ULONG
NTAPI
AppleAdmacPosition(
    _In_ PAPPLE_ADMAC_CHANNEL Channel)
{
    PAPPLE_ADMAC Admac = Channel->Admac;
    ULONG Base, Ring1, Ring2, Residue1, Residue2, Reports;
    ULONG64 Position;

    if (!Admac || !Channel->BufferLength)
        return 0;
    Base = ADMAC_CHANNEL(Channel->Number);
    Ring1 = AdmacRead(Admac, Base + ADMAC_CHANNEL_REPORT_RING);
    Residue1 = AdmacRead(Admac, Base + ADMAC_CHANNEL_RESIDUE);
    Ring2 = AdmacRead(Admac, Base + ADMAC_CHANNEL_REPORT_RING);
    Residue2 = AdmacRead(Admac, Base + ADMAC_CHANNEL_RESIDUE);
    Reports = (Residue2 > Residue1) ? AdmacOccupiedSlots(Ring1) + 1 : AdmacOccupiedSlots(Ring2);
    Position = (ULONG64)Channel->Reclaimed + (ULONG64)Channel->PeriodLength * (Reports + 1) - min(Residue2, Channel->PeriodLength);
    return (ULONG)(Position % Channel->BufferLength);
}
