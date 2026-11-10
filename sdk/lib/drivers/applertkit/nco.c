/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon numerically controlled oscillator (audio clocks)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applenco.h>

#define NCO_CHANNEL_STRIDE      0x4000
#define NCO_CHANNEL_SPAN        0x14
#define NCO_CONTROL             0x00
#define NCO_CONTROL_ENABLE      0x80000000
#define NCO_DIVIDER             0x04
#define NCO_DIVIDER_COARSE_SHIFT 2
#define NCO_INCREMENT1          0x08
#define NCO_INCREMENT2          0x0c
#define NCO_ACCUMULATOR         0x10
#define NCO_ACCUMULATOR_START   0x80000000
#define NCO_LFSR_SEED           0x7ff
#define NCO_LFSR_TAPS           0x500
#define NCO_LFSR_PERIOD         0x7ff
#define NCO_COARSE_OFFSET       2
#define NCO_COARSE_COUNT        0x800

static
ULONG
AppleNcoCoarseCode(
    _In_ ULONG Index)
{
    ULONG State = NCO_LFSR_SEED, Step;

    if (!Index)
        return 0;
    for (Step = Index; Step <= NCO_LFSR_PERIOD; ++Step)
        State = (State & 1) ? ((State >> 1) ^ NCO_LFSR_TAPS) : (State >> 1);
    return State;
}

NTSTATUS
NTAPI
AppleNcoOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PAPPLE_NCO Nco)
{
    const VOID *Clocks, *Compatible;
    ULONG Length, Entry, Cell = 0, Provider = FDT_NO_NODE, Cells = 0, Parent, Size, Channel;
    ULONG64 Value, Base, RegSize;
    PHYSICAL_ADDRESS Physical;

    RtlZeroMemory(Nco, sizeof(*Nco));
    Clocks = FdtGetProperty(&Tree->Fdt, Node, "clocks", &Length);
    for (Entry = 0; Clocks && FdtReadCells(Clocks, Length, Cell, 1, &Value); ++Entry)
    {
        Provider = FdtDriverFindPhandle(Tree, (ULONG)Value);
        if (Provider == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Provider, "#clock-cells", &Cells))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (Entry == Index)
            break;
        Cell += 1 + Cells;
    }
    if (!Clocks || Entry != Index || Provider == FDT_NO_NODE || Cells != 1 ||
        !FdtReadCells(Clocks, Length, Cell + 1, 1, &Value))
        return STATUS_NOT_FOUND;
    Channel = (ULONG)Value;
    Compatible = FdtGetProperty(&Tree->Fdt, Provider, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,t8103-nco") &&
        !FdtStringListContains(Compatible, Length, "apple,nco"))
        return STATUS_NOT_SUPPORTED;
    Clocks = FdtGetProperty(&Tree->Fdt, Provider, "clocks", &Length);
    if (!Clocks || !FdtReadCells(Clocks, Length, 0, 1, &Value) ||
        (Parent = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, Parent, "clock-frequency", &Nco->ParentRate) || !Nco->ParentRate ||
        !FdtDriverReadReg(Tree, Provider, 0, &Base, &RegSize) ||
        RegSize < NCO_CHANNEL_SPAN || Channel > (RegSize - NCO_CHANNEL_SPAN) / NCO_CHANNEL_STRIDE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Size = NCO_CHANNEL_SPAN;
    Physical.QuadPart = (LONGLONG)(Base + (ULONG64)Channel * NCO_CHANNEL_STRIDE);
    Nco->Registers = MmMapIoSpace(Physical, Size, MmNonCached);
    return Nco->Registers ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NTAPI
AppleNcoClose(
    _Inout_ PAPPLE_NCO Nco)
{
    if (Nco->Registers)
        MmUnmapIoSpace(Nco->Registers, NCO_CHANNEL_SPAN);
    RtlZeroMemory(Nco, sizeof(*Nco));
}

NTSTATUS
NTAPI
AppleNcoSetRate(
    _In_ PAPPLE_NCO Nco,
    _In_ ULONG Rate)
{
    ULONG64 Twice;
    ULONG Divider, Coarse, Increment1, Increment2, Control;

    if (!Nco->Registers || !Rate)
        return STATUS_INVALID_PARAMETER;
    Twice = 2ULL * Nco->ParentRate;
    Divider = (ULONG)(Twice / Rate);
    Coarse = Divider / 4;
    if (Coarse < NCO_COARSE_OFFSET || Coarse >= NCO_COARSE_OFFSET + NCO_COARSE_COUNT)
        return STATUS_NOT_SUPPORTED;
    Increment1 = (ULONG)(Twice - (ULONG64)Divider * Rate);
    Increment2 = Increment1 - Rate;
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_CONTROL));
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_CONTROL), Control & ~NCO_CONTROL_ENABLE);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_DIVIDER),
                         (AppleNcoCoarseCode(Coarse - NCO_COARSE_OFFSET) << NCO_DIVIDER_COARSE_SHIFT) | (Divider & 3));
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_INCREMENT1), Increment1);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_INCREMENT2), Increment2);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_ACCUMULATOR), NCO_ACCUMULATOR_START);
    if (Control & NCO_CONTROL_ENABLE)
        WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_CONTROL), Control);
    return STATUS_SUCCESS;
}

VOID
NTAPI
AppleNcoEnable(
    _In_ PAPPLE_NCO Nco,
    _In_ BOOLEAN Enable)
{
    ULONG Control;

    if (!Nco->Registers)
        return;
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_CONTROL));
    Control = Enable ? (Control | NCO_CONTROL_ENABLE) : (Control & ~NCO_CONTROL_ENABLE);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Nco->Registers + NCO_CONTROL), Control);
}
