/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 processor identification, caches and TLB maintenance
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern UCHAR KiArmVectorTable[];

ULONG KiArmDcacheLineSize = 64;
ULONG KiArmIcacheLineSize = 64;

#define KI_ARM_CACHE_LEVELS 7
#define KI_ARM_MPIDR_MP 0x80000000
#define KI_ARM_MPIDR_MT 0x01000000
#define KI_ARM_MPIDR_PACKAGE 0x00FF0000
#define KI_ARM_MPIDR_CLUSTER 0x00FFFF00
#define KI_ARM_PRIVATE_CACHE MAXULONG

typedef struct _KI_ARM_PROCESSOR_CACHES
{
    ULONG Package;
    ULONG Cluster;
    ULONG SharedFloor;
    ULONG Count;
    CACHE_DESCRIPTOR Cache[2 * KI_ARM_CACHE_LEVELS];
} KI_ARM_PROCESSOR_CACHES, *PKI_ARM_PROCESSOR_CACHES;

static KI_ARM_PROCESSOR_CACHES KiArmProcessorCaches[MAXIMUM_PROCESSORS];

VOID
NTAPI
KiArmInitializeVectors(VOID)
{
    KiArmWriteCp15(12, 0, 0, 0, (ULONG_PTR)KiArmVectorTable & ~(ULONG_PTR)1);
    KiArmIsb();
}

static
VOID
KiArmDescribeCache(
    _In_ ULONG Level,
    _In_ PROCESSOR_CACHE_TYPE Type,
    _Out_ PCACHE_DESCRIPTOR Cache)
{
    ULONG Ccsidr, LineSize, Ways, Sets;
    ULONGLONG Size;

    /* CSSELR (ARM ARM B4.1.41) selects the cache CCSIDR (B4.1.19) describes: level in bits 3:1, instruction side in bit 0. */
    KiArmWriteCp15(0, 2, 0, 0, ((Level - 1) << 1) | (Type == CacheInstruction));
    KiArmIsb();
    Ccsidr = KiArmReadCp15(0, 1, 0, 0);
    LineSize = 1UL << ((Ccsidr & 7) + 4);
    Ways = ((Ccsidr >> 3) & 0x3FF) + 1;
    Sets = ((Ccsidr >> 13) & 0x7FFF) + 1;
    Size = (ULONGLONG)LineSize * Ways * Sets;

    Cache->Level = (UCHAR)Level;
    Cache->Associativity = (Sets == 1) ? CACHE_FULLY_ASSOCIATIVE : (UCHAR)min(Ways, CACHE_FULLY_ASSOCIATIVE - 1);
    Cache->LineSize = (USHORT)LineSize;
    Cache->Size = (ULONG)min(Size, MAXULONG);
    Cache->Type = Type;
}

static
VOID
KiArmCaptureCaches(
    _In_ ULONG Number)
{
    PKI_ARM_PROCESSOR_CACHES Caches;
    ULONG Mpidr = KiArmReadCp15(0, 0, 0, 5);
    ULONG Clidr = KiArmReadCp15(0, 1, 0, 1);
    ULONG Level, Type, Unification, Deepest = 0, Count = 0;

    ASSERT(Number < MAXIMUM_PROCESSORS);
    Caches = &KiArmProcessorCaches[Number];

    /* MPIDR (ARM ARM B4.1.106) holds affinity fields only in the format bit 31 marks. */
    if (!(Mpidr & KI_ARM_MPIDR_MP))
        Mpidr = 0;
    Caches->Package = Mpidr & KI_ARM_MPIDR_PACKAGE;
    Caches->Cluster = Mpidr & ((Mpidr & KI_ARM_MPIDR_MT) ? KI_ARM_MPIDR_PACKAGE : KI_ARM_MPIDR_CLUSTER);

    for (Level = 1; Level <= KI_ARM_CACHE_LEVELS; Level++)
    {
        /* CLIDR Ctype (ARM ARM B4.1.20): 1 instruction, 2 data, 3 both, 4 unified; anything else ends the hierarchy. */
        Type = (Clidr >> ((Level - 1) * 3)) & 7;
        if ((Type == 0) || (Type > 4))
            break;

        if (Type == 4)
        {
            KiArmDescribeCache(Level, CacheUnified, &Caches->Cache[Count++]);
        }
        else
        {
            if (Type & 1)
                KiArmDescribeCache(Level, CacheInstruction, &Caches->Cache[Count++]);
            if (Type & 2)
                KiArmDescribeCache(Level, CacheData, &Caches->Cache[Count++]);
        }
        Deepest = Level;
    }
    ASSERT(Count <= RTL_NUMBER_OF(Caches->Cache));

    /* CLIDR LoUIS is the last level a processor keeps to itself; without it only the deepest level counts as shared. */
    Unification = (Clidr >> 21) & 7;
    Caches->SharedFloor = Unification ? (Unification + 1) : Deepest;
    Caches->Count = Count;
    KeMemoryBarrier();
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
    ULONG Clusters[KI_MAX_CACHE_RECORDS];
    ULONG Packages[MAXIMUM_PROCESSORS];
    KAFFINITY Active = KeActiveProcessors.Bitmap[0];
    KAFFINITY Core, Members;
    PKI_ARM_PROCESSOR_CACHES Caches;
    PCACHE_DESCRIPTOR Cache;
    ULONG Number, Index, Record, Set, Cluster, Count = 0, PackageCount = 0;

    KeMemoryBarrier();
    MaxRecords = Records ? min(MaxRecords, KI_MAX_CACHE_RECORDS) : 0;
    MaxSets = Sets ? min(MaxSets, MAXIMUM_PROCESSORS) : 0;

    for (Number = 0; Number < MAXIMUM_PROCESSORS; Number++)
    {
        if (!(Active & AFFINITY_MASK(Number)))
            continue;
        ASSERT(KiProcessorBlock[Number] != NULL);
        Caches = &KiArmProcessorCaches[Number];
        ASSERT(Caches->Count <= RTL_NUMBER_OF(Caches->Cache));

        for (Set = 0; (Set < PackageCount) && (Packages[Set] != Caches->Package); Set++)
            ;
        if (Set < MaxSets)
        {
            if (Set == PackageCount)
            {
                Packages[PackageCount] = Caches->Package;
                Sets[PackageCount++] = 0;
            }
            Sets[Set] |= AFFINITY_MASK(Number);
        }

        Core = ((KAFFINITY)KiProcessorBlock[Number]->MultiThreadProcessorSet | AFFINITY_MASK(Number)) & Active;
        for (Index = 0; Index < Caches->Count; Index++)
        {
            Cache = &Caches->Cache[Index];
            if (Cache->Level < Caches->SharedFloor)
            {
                /* The first processor of a core describes the caches its siblings share with it. */
                if (Core & (AFFINITY_MASK(Number) - 1))
                    continue;
                Cluster = KI_ARM_PRIVATE_CACHE;
                Members = Core;
                Record = Count;
            }
            else
            {
                Cluster = Caches->Cluster;
                Members = AFFINITY_MASK(Number);
                for (Record = 0; Record < Count; Record++)
                {
                    if ((Clusters[Record] == Cluster) &&
                        RtlEqualMemory(&Records[Record].Descriptor, Cache, sizeof(*Cache)))
                    {
                        break;
                    }
                }
            }

            if (Record == Count)
            {
                if (Count == MaxRecords)
                    continue;
                Records[Count].Descriptor = *Cache;
                Records[Count].ProcessorSet = 0;
                Clusters[Count++] = Cluster;
            }
            Records[Record].ProcessorSet |= Members;
        }
    }

    *RecordCount = Count;
    *SetCount = PackageCount;
}

VOID
NTAPI
KiArmIdentifyProcessor(
    _Inout_ PKPRCB Prcb)
{
    ULONG Midr = KiArmReadCp15(0, 0, 0, 0);
    ULONG Ctr = KiArmReadCp15(0, 0, 0, 1);
    ULONG Isar0 = KiArmReadCp15(0, 0, 2, 0);
    ULONG Features = 0;

    /* MIDR (ARM ARM B4.1.105): implementer, variant, part number, revision. */
    Prcb->CpuVendor = (UCHAR)(Midr >> 24);
    Prcb->ProcessorModel = (USHORT)((Midr >> 4) & 0xFFF);
    Prcb->ProcessorRevision = (USHORT)(((Midr >> 20) & 0xF) << 8 | (Midr & 0xF));

    /* CTR DminLine and IminLine are log2 of the line length in words. */
    KiArmDcacheLineSize = 4 << ((Ctr >> 16) & 0xF);
    KiArmIcacheLineSize = 4 << (Ctr & 0xF);

    /* ID_ISAR0 bits 27:24: hardware divide in the Thumb instruction set. */
    if ((Isar0 >> 24) & 0xF)
        Features |= KI_ARM_FEATURE_DIVIDE;
    Prcb->FeatureBits = Features;
    Prcb->MaxBreakpoints = 0;
    Prcb->MaxWatchpoints = 0;
    Prcb->MHz = 1000;
    Prcb->CycleCounterFrequency = KiArmReadCp15(14, 0, 0, 0);
    KiArmCaptureCaches(Prcb->Number);
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentPrcb()->Number;
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    KiArmDsbIsh();
    KiArmWriteCp15(8, 0, 3, 0, 0);
    KiArmWriteCp15(7, 0, 5, 6, 0);
    KiArmDsbIsh();
    KiArmIsb();
}

VOID
NTAPI
KeFlushProcessTb(VOID)
{
    KeFlushCurrentTb();
}

static
VOID
KiArmCleanInvalidateDcacheRange(
    _In_ ULONG_PTR Start,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    ULONG_PTR Line = KiArmDcacheLineSize;
    ULONG_PTR Address = Start & ~(Line - 1);
    ULONG_PTR End = Start + Length;

    KiArmDsbIsh();
    for (; Address < End; Address += Line)
    {
        if (Invalidate)
            KiArmWriteCp15(7, 0, 14, 1, Address);
        else
            KiArmWriteCp15(7, 0, 10, 1, Address);
    }
    KiArmDsb();
}

VOID
NTAPI
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG_PTR Line = KiArmIcacheLineSize;
    ULONG_PTR Address, End;

    if (!BaseAddress)
    {
        KiArmDsbIsh();
        KiArmWriteCp15(7, 0, 1, 0, 0);
        KiArmWriteCp15(7, 0, 5, 6, 0);
        KiArmDsbIsh();
        KiArmIsb();
        return;
    }

    /* Make the new instructions visible to the point of unification first. */
    KiArmCleanInvalidateDcacheRange((ULONG_PTR)BaseAddress, FlushSize, FALSE);
    Address = (ULONG_PTR)BaseAddress & ~(Line - 1);
    End = (ULONG_PTR)BaseAddress + FlushSize;
    for (; Address < End; Address += Line)
        KiArmWriteCp15(7, 0, 5, 1, Address);
    KiArmWriteCp15(7, 0, 5, 6, 0);
    KiArmDsbIsh();
    KiArmIsb();
}

VOID
NTAPI
KeSweepDcache(
    _In_ BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(AllProcessors);
    KiArmDsb();
}

VOID
NTAPI
KiArmFlushDmaRange(
    _In_ PVOID Address,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    KiArmCleanInvalidateDcacheRange((ULONG_PTR)Address, Length, Invalidate);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    PKARM_ARCH_STATE Arch = &ProcessorState->ArchState;

    Arch->Cp15_Cr0_CpuId = KiArmReadCp15(0, 0, 0, 0);
    Arch->Cp15_Cr1_Control = KiArmReadCp15(1, 0, 0, 0);
    Arch->Cp15_Cr1_AuxControl = KiArmReadCp15(1, 0, 0, 1);
    Arch->Cp15_Cr1_Cpacr = KiArmReadCp15(1, 0, 0, 2);
    Arch->Cp15_Cr2_TtbControl = KiArmReadCp15(2, 0, 0, 2);
    Arch->Cp15_Cr2_Ttb0 = (ULONG)KiArmReadCp15_64(0, 2);
    Arch->Cp15_Cr2_Ttb1 = (ULONG)KiArmReadCp15_64(1, 2);
    Arch->Cp15_Cr3_Dacr = KiArmReadCp15(3, 0, 0, 0);
    Arch->Cp15_Cr5_Dfsr = KiArmReadCp15(5, 0, 0, 0);
    Arch->Cp15_Cr5_Ifsr = KiArmReadCp15(5, 0, 0, 1);
    Arch->Cp15_Cr6_Dfar = KiArmReadCp15(6, 0, 0, 0);
    Arch->Cp15_Cr6_Ifar = KiArmReadCp15(6, 0, 0, 2);
    Arch->Cp15_Cr10_PrimaryMemoryRemap = KiArmReadCp15(10, 0, 2, 0);
    Arch->Cp15_Cr10_NormalMemoryRemap = KiArmReadCp15(10, 0, 2, 1);
    Arch->Cp15_Cr12_VBARns = KiArmReadCp15(12, 0, 0, 0);
    Arch->Cp15_Cr13_ContextId = KiArmReadCp15(13, 0, 0, 1);
    ProcessorState->SpecialRegisters.Cp15_Cr13_UsrRW = KiArmReadCp15(13, 0, 0, 2);
    ProcessorState->SpecialRegisters.Cp15_Cr13_UsrRO = KiArmReadCp15(13, 0, 0, 3);
    ProcessorState->SpecialRegisters.Cp15_Cr13_SvcRW = KiArmReadCp15(13, 0, 0, 4);
}

VOID
NTAPI
KiRestoreProcessorControlState(
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(ProcessorState);
}
