/*
 * PROJECT:     LiberNT Apple CPU Performance Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance-state table and requests of an Apple silicon CPU cluster
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/fdtlib.h>

#define CPUFREQ_COMMAND             0x20
#define CPUFREQ_COMMAND_BUSY        0x0000000080000000ULL
#define CPUFREQ_COMMAND_SET         0x0000000002000000ULL
#define CPUFREQ_COMMAND_STATE       0x000000000000001FULL
#define CPUFREQ_STATUS              0x50
#define CPUFREQ_STATUS_CURRENT(x)   (((x) >> 5) & 0x1F)
#define CPUFREQ_STATUS_TARGET(x)    ((x) & 0x1F)
#define CPUFREQ_REGISTER_SPAN       (CPUFREQ_STATUS + sizeof(ULONG))
#define CPUFREQ_MAX_LEVEL           31
#define CPUFREQ_MAX_STATES          32
#define CPUFREQ_POLL_US             2
#define CPUFREQ_POLL_COUNT          200
#define CPUFREQ_MAX_CPUS            64

typedef struct _CPUFREQ_STATE
{
    ULONG Level;
    ULONG64 Hz;
    ULONG LatencyNs;
} CPUFREQ_STATE, *PCPUFREQ_STATE;

typedef struct _CPUFREQ_TABLE
{
    ULONG Count;
    CPUFREQ_STATE State[CPUFREQ_MAX_STATES];
} CPUFREQ_TABLE, *PCPUFREQ_TABLE;

typedef struct _CPUFREQ_READING
{
    ULONG64 Command;
    ULONG Status;
    ULONG Current;
    ULONG Target;
} CPUFREQ_READING, *PCPUFREQ_READING;

typedef struct _CPUFREQ_CPUS
{
    ULONG Count;
    ULONG64 First;
    ULONG64 Last;
    ULONG TablePhandle;
    ULONG Capacity;
    ULONG64 Mpidr[CPUFREQ_MAX_CPUS];
} CPUFREQ_CPUS, *PCPUFREQ_CPUS;

NTSTATUS
CpufreqFindCpus(
    _In_ const FDT *Fdt,
    _In_ ULONG Cluster,
    _Out_ PCPUFREQ_CPUS Cpus);

NTSTATUS
CpufreqFindEfficientCpus(
    _In_ const FDT *Fdt,
    _Out_ PCPUFREQ_CPUS Cpus);

NTSTATUS
CpufreqParseTable(
    _In_ const FDT *Fdt,
    _In_ ULONG TableNode,
    _Out_ PCPUFREQ_TABLE Table);

const CPUFREQ_STATE *
CpufreqFindLevel(
    _In_ const CPUFREQ_TABLE *Table,
    _In_ ULONG Level);

VOID
CpufreqRead(
    _In_ PUCHAR Base,
    _Out_ PCPUFREQ_READING Reading);

NTSTATUS
CpufreqRequest(
    _In_ PUCHAR Base,
    _In_ ULONG Level,
    _Out_ PCPUFREQ_READING Reading,
    _Out_ PULONG Polls);
