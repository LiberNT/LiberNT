/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 processor cache topology from CPUCFG
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

#define KI_LA64_TOPOLOGY_CACHES 4

/* CPUCFG10 presence bits and the per-level geometry word, [3A5000] Table 54:
 * word 0x10 is the presence word, 0x11-0x14 describe L1I, L1D, L2 and L3.
 * The fields are Way-1 in 15:0, Index-log2 in 23:16 and Linesize-log2 in
 * 30:24. */
typedef struct _KI_LA64_CACHE_TOPOLOGY
{
    BOOLEAN Present;
    ULONG Ways;
    ULONG SetLog2;
    ULONG LineLog2;
} KI_LA64_CACHE_TOPOLOGY, *PKI_LA64_CACHE_TOPOLOGY;

static
VOID
KiLa64ReadTopology(
    _In_ ULONG PresenceBit,
    _In_ ULONG GeometryWord,
    _Out_ PKI_LA64_CACHE_TOPOLOGY Topology)
{
    ULONG Presence = KiLa64ReadCpucfg(0x10);
    ULONG Word = KiLa64ReadCpucfg(GeometryWord);

    Topology->Present = (Presence & PresenceBit) != 0;
    Topology->Ways = (Word & 0xFFFF) + 1;
    Topology->SetLog2 = (Word >> 16) & 0xFF;
    Topology->LineLog2 = (Word >> LA64_CPUCFG_LINESIZE_SHIFT) & LA64_CPUCFG_LINESIZE_MASK;

    /* A geometry outside these bounds cannot describe a real cache on a
     * 4 KiB-page machine; treat it as absent rather than report nonsense. */
    if ((Topology->SetLog2 > 20) || (Topology->LineLog2 < 4) ||
        (Topology->LineLog2 > PAGE_SHIFT))
    {
        Topology->Present = FALSE;
    }
}

static
ULONG
KiLa64TopologyCacheSize(
    _In_ const KI_LA64_CACHE_TOPOLOGY *Topology)
{
    return Topology->Ways * (1u << Topology->SetLog2) * (1u << Topology->LineLog2);
}

VOID
NTAPI
KiQueryProcessorTopology(
    _Out_writes_to_opt_(MaxRecords, *RecordCount) PKI_CACHE_RECORD Records,
    _In_ ULONG MaxRecords,
    _Out_ PULONG RecordCount,
    _Out_writes_to_opt_(MaxSets, *SetCount) PKAFFINITY Sets,
    _In_ ULONG MaxSets,
    _Out_ PULONG SetCount)
{
    KI_LA64_CACHE_TOPOLOGY Caches[KI_LA64_TOPOLOGY_CACHES];
    CACHE_DESCRIPTOR Descriptors[KI_LA64_TOPOLOGY_CACHES];
    ULONG Types[KI_LA64_TOPOLOGY_CACHES] = { CacheInstruction, CacheData, CacheUnified, CacheUnified };
    ULONG Index, Number, Count = 0;

    UNREFERENCED_PARAMETER(Sets);
    UNREFERENCED_PARAMETER(MaxSets);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *RecordCount = 0;
    *SetCount = 0;
    if ((Records == NULL) || (MaxRecords == 0))
        return;
    MaxRecords = min(MaxRecords, KI_MAX_CACHE_RECORDS);

    /* Every processor on a homogeneous part reports the same geometry, so
     * the descriptors are built once and shared by every active processor. */
    KiLa64ReadTopology(LA64_CPUCFG10_L1IU_PRESENT, 0x11, &Caches[0]);
    KiLa64ReadTopology(LA64_CPUCFG10_L1D_PRESENT, 0x12, &Caches[1]);
    KiLa64ReadTopology(0x00000008, 0x13, &Caches[2]);
    KiLa64ReadTopology(0x00000400, 0x14, &Caches[3]);

    for (Index = 0; Index < KI_LA64_TOPOLOGY_CACHES; Index++)
    {
        if (!Caches[Index].Present)
            continue;
        RtlZeroMemory(&Descriptors[Count], sizeof(Descriptors[Count]));
        Descriptors[Count].Level = (UCHAR)((Index < 2) ? 1 : (Index == 2 ? 2 : 3));
        Descriptors[Count].Associativity = (UCHAR)min(Caches[Index].Ways, 0xFF);
        Descriptors[Count].LineSize = (USHORT)(1u << Caches[Index].LineLog2);
        Descriptors[Count].Size = KiLa64TopologyCacheSize(&Caches[Index]);
        Descriptors[Count].Type = Types[Index];
        Count++;
    }

    Count = min(Count, MaxRecords);
    for (Number = 0; Number < (ULONG)KeNumberProcessors; ++Number)
    {
        if (!(KeActiveProcessors & AFFINITY_MASK(Number)))
            continue;
        for (Index = 0; Index < Count; Index++)
        {
            if (Records[Index].Descriptor.Size == 0)
                Records[Index].Descriptor = Descriptors[Index];
            Records[Index].ProcessorSet |= AFFINITY_MASK(Number);
        }
    }
    *RecordCount = Count;
}

VOID
NTAPI
KiQueryProcessorDomains(
    _Out_ PKI_PROCESSOR_DOMAINS Domains)
{
    /* TODO: cores, modules, dies and packages; CPUCFG defines no topology word, so each processor falls back to its own core in one package */
    RtlZeroMemory(Domains, sizeof(*Domains));
}

CODE_SEG("INIT")
VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses)
{
    /* TODO: rank processors by a capacity value; CPUCFG carries no performance word on this part */
    RtlZeroMemory(EfficiencyClasses, MAXIMUM_PROCESSORS);
}
