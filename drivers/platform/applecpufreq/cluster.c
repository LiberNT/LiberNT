/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance-state table and requests of an Apple silicon CPU cluster
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "cluster.h"

NTSTATUS
CpufreqFindCpus(
    _In_ const FDT *Fdt,
    _In_ ULONG Cluster,
    _Out_ PCPUFREQ_CPUS Cpus)
{
    const VOID *Property;
    ULONG Phandle, Cells, Parent, Cpu, Length, Table, Capacity;
    ULONG64 Domain, Mpidr, Size;

    RtlZeroMemory(Cpus, sizeof(*Cpus));
    if (!FdtReadU32(Fdt, Cluster, "phandle", &Phandle))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (FdtReadU32(Fdt, Cluster, "#performance-domain-cells", &Cells) && Cells)
        return STATUS_NOT_SUPPORTED;
    if (!FdtResolvePath(Fdt, "/cpus", &Parent, NULL, NULL))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Cpu = FdtFirstChild(Fdt, Parent); Cpu != FDT_NO_NODE; Cpu = FdtNextSibling(Fdt, Cpu))
    {
        Property = FdtGetProperty(Fdt, Cpu, "device_type", &Length);
        if (!FdtStringListContains(Property, Length, "cpu"))
            continue;
        Property = FdtGetProperty(Fdt, Cpu, "performance-domains", &Length);
        if (!Property || !FdtReadCells(Property, Length, 0, 1, &Domain) || Domain != Phandle)
            continue;
        if (!FdtReadU32(Fdt, Cpu, "operating-points-v2", &Table) || !Table ||
            (Cpus->Count && Table != Cpus->TablePhandle) || !FdtReadReg(Fdt, Cpu, Parent, 0, &Mpidr, &Size) ||
            Cpus->Count == CPUFREQ_MAX_CPUS)
        {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if (!FdtReadU32(Fdt, Cpu, "capacity-dmips-mhz", &Capacity))
            Capacity = 0;
        if (!Cpus->Count)
        {
            Cpus->TablePhandle = Table;
            Cpus->First = Mpidr;
            Cpus->Capacity = Capacity;
        }
        else if (Capacity != Cpus->Capacity)
        {
            Cpus->Capacity = 0;
        }
        Cpus->Last = Mpidr;
        Cpus->Mpidr[Cpus->Count++] = Mpidr;
    }
    return Cpus->Count ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS
CpufreqFindEfficientCpus(
    _In_ const FDT *Fdt,
    _Out_ PCPUFREQ_CPUS Cpus)
{
    const VOID *Property;
    ULONG Parent, Cpu, Length, Capacity;
    ULONG64 Mpidr, Size;

    RtlZeroMemory(Cpus, sizeof(*Cpus));
    Cpus->Capacity = MAXULONG;
    if (!FdtResolvePath(Fdt, "/cpus", &Parent, NULL, NULL))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Cpu = FdtFirstChild(Fdt, Parent); Cpu != FDT_NO_NODE; Cpu = FdtNextSibling(Fdt, Cpu))
    {
        Property = FdtGetProperty(Fdt, Cpu, "device_type", &Length);
        if (!FdtStringListContains(Property, Length, "cpu"))
            continue;
        if (!FdtReadU32(Fdt, Cpu, "capacity-dmips-mhz", &Capacity) || !Capacity ||
            !FdtReadReg(Fdt, Cpu, Parent, 0, &Mpidr, &Size))
        {
            continue;
        }
        if (Capacity > Cpus->Capacity)
            continue;
        if (Capacity < Cpus->Capacity)
        {
            Cpus->Capacity = Capacity;
            Cpus->Count = 0;
        }
        if (Cpus->Count == CPUFREQ_MAX_CPUS)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (!Cpus->Count)
            Cpus->First = Mpidr;
        Cpus->Last = Mpidr;
        Cpus->Mpidr[Cpus->Count++] = Mpidr;
    }
    if (!Cpus->Count)
    {
        Cpus->Capacity = 0;
        return STATUS_NOT_FOUND;
    }
    ASSERT(Cpus->Capacity && Cpus->Capacity != MAXULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
CpufreqParseTable(
    _In_ const FDT *Fdt,
    _In_ ULONG TableNode,
    _Out_ PCPUFREQ_TABLE Table)
{
    const VOID *Property;
    CPUFREQ_STATE State;
    ULONG64 Hz;
    ULONG Node, Length, Index;

    RtlZeroMemory(Table, sizeof(*Table));
    Property = FdtGetProperty(Fdt, TableNode, "compatible", &Length);
    if (!FdtStringListContains(Property, Length, "operating-points-v2"))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Node = FdtFirstChild(Fdt, TableNode); Node != FDT_NO_NODE; Node = FdtNextSibling(Fdt, Node))
    {
        Property = FdtGetProperty(Fdt, Node, "opp-hz", &Length);
        if (!Property)
            continue;
        if (Length != sizeof(ULONG64) || !FdtReadCells(Property, Length, 0, 2, &Hz) || !Hz)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        RtlZeroMemory(&State, sizeof(State));
        State.Hz = Hz;
        if (!FdtReadU32(Fdt, Node, "opp-level", &State.Level) || !State.Level || State.Level > CPUFREQ_MAX_LEVEL)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (!FdtReadU32(Fdt, Node, "clock-latency-ns", &State.LatencyNs))
            State.LatencyNs = 0;
        if (Table->Count == CPUFREQ_MAX_STATES)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        for (Index = Table->Count; Index && Table->State[Index - 1].Level > State.Level; --Index)
            Table->State[Index] = Table->State[Index - 1];
        if (Index && Table->State[Index - 1].Level == State.Level)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Table->State[Index] = State;
        Table->Count++;
    }
    if (!Table->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 1; Index < Table->Count; ++Index)
    {
        ASSERT(Table->State[Index - 1].Level < Table->State[Index].Level);
        if (Table->State[Index - 1].Hz >= Table->State[Index].Hz)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

const CPUFREQ_STATE *
CpufreqFindLevel(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Level)
{
    ULONG Index;

    ASSERT(Table->Count <= CPUFREQ_MAX_STATES);
    for (Index = 0; Index < Table->Count; ++Index)
    {
        if (Table->State[Index].Level == Level)
            return &Table->State[Index];
    }
    return NULL;
}

VOID
CpufreqRead(
    _In_ PUCHAR Base,
    _Out_ PCPUFREQ_READING Reading)
{
    Reading->Command = READ_REGISTER_ULONG64((volatile ULONG64 *)(Base + CPUFREQ_COMMAND));
    Reading->Status = READ_REGISTER_ULONG((volatile ULONG *)(Base + CPUFREQ_STATUS));
    Reading->Current = CPUFREQ_STATUS_CURRENT(Reading->Status);
    Reading->Target = CPUFREQ_STATUS_TARGET(Reading->Status);
}

static
BOOLEAN
CpufreqWaitIdle(
    _In_ PUCHAR Base,
    _Out_ PULONG64 Command,
    _Inout_ PULONG Polls)
{
    ULONG Poll;

    for (Poll = 0; Poll <= CPUFREQ_POLL_COUNT; ++Poll)
    {
        *Command = READ_REGISTER_ULONG64((volatile ULONG64 *)(Base + CPUFREQ_COMMAND));
        if (!(*Command & CPUFREQ_COMMAND_BUSY))
            return TRUE;
        if (Poll == CPUFREQ_POLL_COUNT)
            break;
        KeStallExecutionProcessor(CPUFREQ_POLL_US);
        (*Polls)++;
    }
    return FALSE;
}

NTSTATUS
CpufreqRequest(
    _In_ PUCHAR Base,
    _In_ ULONG Level,
    _Out_ PCPUFREQ_READING Reading,
    _Out_ PULONG Polls)
{
    ULONG64 Command;
    BOOLEAN Idle;

    ASSERT(Level && Level <= CPUFREQ_MAX_LEVEL);
    *Polls = 0;
    if (!CpufreqWaitIdle(Base, &Command, Polls))
    {
        CpufreqRead(Base, Reading);
        return STATUS_DEVICE_BUSY;
    }
    ASSERT(!(Command & CPUFREQ_COMMAND_BUSY));
    Command = (Command & ~CPUFREQ_COMMAND_STATE) | Level | CPUFREQ_COMMAND_SET;
    WRITE_REGISTER_ULONG64((volatile ULONG64 *)(Base + CPUFREQ_COMMAND), Command);
    *Polls = 0;
    Idle = CpufreqWaitIdle(Base, &Command, Polls);
    CpufreqRead(Base, Reading);
    return Idle ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}
