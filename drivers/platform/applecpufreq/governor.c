/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Demand-based performance-state policy of an Apple silicon CPU cluster
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "governor.h"

ULONG
CpufreqBusyPermille(
    _In_ ULONG64 IdleDelta,
    _In_ ULONG64 TotalDelta)
{
    ULONG Busy;

    if (!TotalDelta || IdleDelta >= TotalDelta)
        return 0;
    Busy = (ULONG)(((TotalDelta - IdleDelta) * 1000) / TotalDelta);
    ASSERT(Busy <= 1000);
    return Busy;
}

ULONG
CpufreqFindIndex(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Level)
{
    ULONG Index;

    ASSERT(Table->Count <= CPUFREQ_MAX_STATES);
    for (Index = 0; Index < Table->Count; ++Index)
    {
        if (Table->State[Index].Level == Level)
            return Index;
    }
    return CPUFREQ_NO_INDEX;
}

ULONG
CpufreqIdealIndex(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG RunningIndex,
    _In_ ULONG BusyPermille)
{
    ULONG64 Required;
    ULONG Index;

    ASSERT(Table->Count && Table->Count <= CPUFREQ_MAX_STATES);
    ASSERT(RunningIndex < Table->Count);
    ASSERT(BusyPermille <= 1000);

    if (BusyPermille >= CPUFREQ_RACE_PERMILLE)
        return Table->Count - 1;
    Required = (Table->State[RunningIndex].Hz * BusyPermille) / CPUFREQ_TARGET_PERMILLE;
    for (Index = 0; Index < Table->Count - 1; ++Index)
    {
        if (Table->State[Index].Hz >= Required)
            break;
    }
    ASSERT(Index < Table->Count);
    ASSERT(!Index || Table->State[Index - 1].Hz < Required);
    return Index;
}

VOID
CpufreqGovernorInitialize(
    _Out_ PCPUFREQ_GOVERNOR Governor,
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Index)
{
    ASSERT(Index < Table->Count);
    RtlZeroMemory(Governor, sizeof(*Governor));
    Governor->Index = Index;
}

ULONG
CpufreqGovernorUpdate(
    _Inout_ PCPUFREQ_GOVERNOR Governor,
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG RunningIndex,
    _In_ ULONG BusyPermille)
{
    ULONG Ideal;

    ASSERT(Governor->Index < Table->Count);
    ASSERT(Governor->DecreaseSamples < CPUFREQ_DECREASE_SAMPLES);
    ASSERT(!Governor->DecreaseSamples || Governor->DecreaseIndex < Governor->Index);

    Ideal = CpufreqIdealIndex(Table, RunningIndex, BusyPermille);
    if (Ideal > Governor->Index)
    {
        Governor->Index = Ideal;
        Governor->DecreaseSamples = 0;
    }
    else if (Ideal < Governor->Index)
    {
        if (!Governor->DecreaseSamples || Ideal > Governor->DecreaseIndex)
            Governor->DecreaseIndex = Ideal;
        if (++Governor->DecreaseSamples == CPUFREQ_DECREASE_SAMPLES)
        {
            Governor->Index = Governor->DecreaseIndex;
            Governor->DecreaseSamples = 0;
        }
    }
    else
    {
        Governor->DecreaseSamples = 0;
    }

    if (!Governor->Index && BusyPermille < CPUFREQ_IDLE_PERMILLE)
    {
        if (Governor->IdleSamples < CPUFREQ_IDLE_SAMPLES)
            Governor->IdleSamples++;
    }
    else
    {
        Governor->IdleSamples = 0;
    }

    ASSERT(Governor->Index < Table->Count);
    ASSERT(Governor->IdleSamples <= CPUFREQ_IDLE_SAMPLES);
    return Governor->Index;
}

ULONG
CpufreqGovernorPeriodMs(
    _In_ const CPUFREQ_GOVERNOR *Governor)
{
    return (Governor->IdleSamples == CPUFREQ_IDLE_SAMPLES) ? CPUFREQ_IDLE_PERIOD_MS : CPUFREQ_BUSY_PERIOD_MS;
}

ULONG64
CpufreqMapProcessors(
    _In_reads_(ProcessorCount) const ULONG64 *ProcessorMpidr,
    _In_ ULONG ProcessorCount,
    _In_ const CPUFREQ_CPUS *Cpus)
{
    ULONG64 Mask = 0;
    ULONG Processor, Cpu;

    ASSERT(ProcessorCount <= CPUFREQ_MAX_CPUS);
    ASSERT(Cpus->Count <= CPUFREQ_MAX_CPUS);
    for (Processor = 0; Processor < ProcessorCount; ++Processor)
    {
        for (Cpu = 0; Cpu < Cpus->Count; ++Cpu)
        {
            if ((ProcessorMpidr[Processor] & CPUFREQ_MPIDR_AFFINITY) == (Cpus->Mpidr[Cpu] & CPUFREQ_MPIDR_AFFINITY))
            {
                Mask |= 1ULL << Processor;
                break;
            }
        }
    }
    return Mask;
}
