/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V processor cache topology from the device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/riscv64/fdtlib.h>

#define NDEBUG
#include <debug.h>

#define KI_RISCV_HART_CACHES        4
#define KI_RISCV_CACHE_CHAIN_DEPTH  3

typedef struct _KI_RISCV_HART_TOPOLOGY
{
    ULONG64 HartId;
    ULONG CacheCount;
    CACHE_DESCRIPTOR Cache[KI_RISCV_HART_CACHES];
    ULONG Instance[KI_RISCV_HART_CACHES];
} KI_RISCV_HART_TOPOLOGY, *PKI_RISCV_HART_TOPOLOGY;

static KI_RISCV_HART_TOPOLOGY KiRiscvHartTopology[MAXIMUM_PROCESSORS];
static ULONG KiRiscvHartTopologyCount;

static ULONG
KiRiscvReadCacheValue(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name)
{
    ULONG Value;

    return RiscvFdtReadU32(Fdt, Node, Name, &Value) ? Value : 0;
}

static VOID
KiRiscvAddCache(
    _Inout_ PKI_RISCV_HART_TOPOLOGY Hart,
    _In_ UCHAR Level,
    _In_ PROCESSOR_CACHE_TYPE Type,
    _In_ ULONG Size,
    _In_ ULONG LineSize,
    _In_ ULONG Sets,
    _In_ ULONG Instance)
{
    PCACHE_DESCRIPTOR Cache;
    ULONG64 Ways = 0;

    if ((Size == 0) || (Hart->CacheCount >= KI_RISCV_HART_CACHES))
        return;
    if ((Sets != 0) && (LineSize != 0))
        Ways = Size / ((ULONG64)Sets * LineSize);

    Cache = &Hart->Cache[Hart->CacheCount];
    Cache->Level = Level;
    Cache->Associativity = (Ways < CACHE_FULLY_ASSOCIATIVE) ? (UCHAR)Ways : 0;
    Cache->LineSize = (USHORT)LineSize;
    Cache->Size = Size;
    Cache->Type = Type;
    Hart->Instance[Hart->CacheCount++] = Instance;
}

static VOID
KiRiscvAddNodeCaches(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ UCHAR Level,
    _Inout_ PKI_RISCV_HART_TOPOLOGY Hart)
{
    ULONG Line;
    ULONG Length;

    if (RiscvFdtGetProperty(Fdt, Node, "cache-unified", &Length) != NULL)
    {
        Line = KiRiscvReadCacheValue(Fdt, Node, "cache-line-size");
        if (Line == 0)
            Line = KiRiscvReadCacheValue(Fdt, Node, "cache-block-size");
        KiRiscvAddCache(Hart, Level, CacheUnified,
                        KiRiscvReadCacheValue(Fdt, Node, "cache-size"), Line,
                        KiRiscvReadCacheValue(Fdt, Node, "cache-sets"), Node);
        return;
    }

    Line = KiRiscvReadCacheValue(Fdt, Node, "i-cache-line-size");
    if (Line == 0)
        Line = KiRiscvReadCacheValue(Fdt, Node, "i-cache-block-size");
    KiRiscvAddCache(Hart, Level, CacheInstruction,
                    KiRiscvReadCacheValue(Fdt, Node, "i-cache-size"), Line,
                    KiRiscvReadCacheValue(Fdt, Node, "i-cache-sets"), Node);

    Line = KiRiscvReadCacheValue(Fdt, Node, "d-cache-line-size");
    if (Line == 0)
        Line = KiRiscvReadCacheValue(Fdt, Node, "d-cache-block-size");
    KiRiscvAddCache(Hart, Level, CacheData,
                    KiRiscvReadCacheValue(Fdt, Node, "d-cache-size"), Line,
                    KiRiscvReadCacheValue(Fdt, Node, "d-cache-sets"), Node);
}

static ULONG
KiRiscvFindPhandle(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Phandle)
{
    ULONG Offset = RiscvFdtRootNode(Fdt);
    ULONG Value;

    while (Offset != RISCV_FDT_NO_NODE)
    {
        ULONG Token = RiscvFdtToken(Fdt, Offset);

        if (Token == RISCV_FDT_END)
            break;
        if ((Token == RISCV_FDT_BEGIN_NODE) &&
            (RiscvFdtReadU32(Fdt, Offset, "phandle", &Value) ||
             RiscvFdtReadU32(Fdt, Offset, "linux,phandle", &Value)) &&
            (Value == Phandle))
        {
            return Offset;
        }
        Offset = RiscvFdtSkipToken(Fdt, Offset);
    }
    return RISCV_FDT_NO_NODE;
}

VOID
NTAPI
KiRiscvCaptureCacheTopology(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Cpus)
{
    ULONG Child;

    KiRiscvHartTopologyCount = 0;
    for (Child = RiscvFdtFirstChild(Fdt, Cpus);
         (Child != RISCV_FDT_NO_NODE) && (KiRiscvHartTopologyCount < MAXIMUM_PROCESSORS);
         Child = RiscvFdtNextSibling(Fdt, Child))
    {
        PKI_RISCV_HART_TOPOLOGY Hart = &KiRiscvHartTopology[KiRiscvHartTopologyCount];
        ULONG NameLength, Node, Phandle, Depth, Level;
        const CHAR *Name = RiscvFdtNodeName(Fdt, Child, &NameLength);
        ULONG64 Address, Size;

        if ((Name == NULL) || !RiscvFdtNameMatches(Name, NameLength, "cpu", 3))
            continue;
        if (!RiscvFdtReadReg(Fdt, Child, Cpus, 0, &Address, &Size))
            continue;

        RtlZeroMemory(Hart, sizeof(*Hart));
        Hart->HartId = Address;
        KiRiscvAddNodeCaches(Fdt, Child, 1, Hart);

        Node = Child;
        Level = 1;
        for (Depth = 0; Depth < KI_RISCV_CACHE_CHAIN_DEPTH; ++Depth)
        {
            if (!RiscvFdtReadU32(Fdt, Node, "next-level-cache", &Phandle))
                break;
            Node = KiRiscvFindPhandle(Fdt, Phandle);
            if (Node == RISCV_FDT_NO_NODE)
                break;
            if (!RiscvFdtReadU32(Fdt, Node, "cache-level", &Level) || (Level < 2) || (Level > 0xFF))
                Level = Depth + 2;
            KiRiscvAddNodeCaches(Fdt, Node, (UCHAR)Level, Hart);
        }

        if (Hart->CacheCount != 0)
            KiRiscvHartTopologyCount++;
    }
}

static PKI_RISCV_HART_TOPOLOGY
KiRiscvLookupHart(
    _In_ ULONG64 HartId)
{
    ULONG Index;

    for (Index = 0; Index < KiRiscvHartTopologyCount; ++Index)
    {
        if (KiRiscvHartTopology[Index].HartId == HartId)
            return &KiRiscvHartTopology[Index];
    }
    return NULL;
}

ULONG
NTAPI
KiRiscvQueryLargestCacheLine(VOID)
{
    ULONG Hart, Index, Size, Line = 0;

    for (Hart = 0; Hart < KiRiscvHartTopologyCount; ++Hart)
    {
        for (Index = 0; Index < KiRiscvHartTopology[Hart].CacheCount; ++Index)
        {
            Size = KiRiscvHartTopology[Hart].Cache[Index].LineSize;
            if ((Size >= 16) && (Size <= PAGE_SIZE) && !(Size & (Size - 1)))
                Line = max(Line, Size);
        }
    }
    return Line;
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
    ULONG Keys[KI_MAX_CACHE_RECORDS];
    ULONG Count = 0;
    ULONG Number, Index, Record;

    UNREFERENCED_PARAMETER(Sets);
    UNREFERENCED_PARAMETER(MaxSets);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *RecordCount = 0;
    *SetCount = 0;
    if ((Records == NULL) || (MaxRecords == 0))
        return;
    MaxRecords = min(MaxRecords, KI_MAX_CACHE_RECORDS);

    for (Number = 0; Number < (ULONG)KeNumberProcessors; ++Number)
    {
        PKI_RISCV_HART_TOPOLOGY Hart;
        PKPRCB Prcb = KiProcessorBlock[Number];

        if ((Prcb == NULL) || !(KeActiveProcessors & AFFINITY_MASK(Number)))
            continue;
        Hart = KiRiscvLookupHart(CONTAINING_RECORD(Prcb, KPCR, Prcb)->HartId);
        if (Hart == NULL)
            continue;

        for (Index = 0; Index < Hart->CacheCount; ++Index)
        {
            for (Record = 0; Record < Count; ++Record)
            {
                if ((Keys[Record] == Hart->Instance[Index]) &&
                    (Records[Record].Descriptor.Level == Hart->Cache[Index].Level) &&
                    (Records[Record].Descriptor.Type == Hart->Cache[Index].Type))
                {
                    break;
                }
            }
            if (Record < Count)
            {
                Records[Record].ProcessorSet |= AFFINITY_MASK(Number);
            }
            else if (Count < MaxRecords)
            {
                Records[Count].Descriptor = Hart->Cache[Index];
                Records[Count].ProcessorSet = AFFINITY_MASK(Number);
                Keys[Count++] = Hart->Instance[Index];
            }
        }
    }
    *RecordCount = Count;
}
