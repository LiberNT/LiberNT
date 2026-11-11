/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Demand-based performance-state policy of an Apple silicon CPU cluster
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "cluster.h"

#define CPUFREQ_TARGET_PERMILLE     800
#define CPUFREQ_RACE_PERMILLE       900
#define CPUFREQ_IDLE_PERMILLE       50
#define CPUFREQ_DECREASE_SAMPLES    3
#define CPUFREQ_IDLE_SAMPLES        10
#define CPUFREQ_BUSY_PERIOD_MS      20
#define CPUFREQ_IDLE_PERIOD_MS      100
#define CPUFREQ_REPORT_PERIOD_MS    30000
#define CPUFREQ_MPIDR_AFFINITY      0x000000FF00FFFFFFULL
#define CPUFREQ_NO_INDEX            MAXULONG

typedef struct _CPUFREQ_GOVERNOR
{
    ULONG Index;
    ULONG DecreaseSamples;
    ULONG DecreaseIndex;
    ULONG IdleSamples;
} CPUFREQ_GOVERNOR, *PCPUFREQ_GOVERNOR;

ULONG
CpufreqBusyPermille(
    _In_ ULONG64 IdleDelta,
    _In_ ULONG64 TotalDelta);

ULONG
CpufreqFindIndex(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Level);

ULONG
CpufreqIdealIndex(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG RunningIndex,
    _In_ ULONG BusyPermille);

VOID
CpufreqGovernorInitialize(
    _Out_ PCPUFREQ_GOVERNOR Governor,
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Index);

ULONG
CpufreqGovernorUpdate(
    _Inout_ PCPUFREQ_GOVERNOR Governor,
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG RunningIndex,
    _In_ ULONG BusyPermille);

ULONG
CpufreqGovernorPeriodMs(
    _In_ const CPUFREQ_GOVERNOR *Governor);

ULONG64
CpufreqMapProcessors(
    _In_reads_(ProcessorCount) const ULONG64 *ProcessorMpidr,
    _In_ ULONG ProcessorCount,
    _In_ const CPUFREQ_CPUS *Cpus);
