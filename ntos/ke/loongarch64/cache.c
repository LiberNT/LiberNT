/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 cache geometry, DMA maintenance and instruction
 *              stream synchronization
 *
 * Cache operations are CACOP ([V1-4.2.3.1]), whose 5-bit immediate splits
 * into op[2:0] = the cache object and op[4:3] = the operation:
 *
 *   0 store tag (initialization)
 *   1 index invalidate / invalidate-and-writeback
 *   2 hit invalidate / invalidate-and-writeback (the address is treated as a
 *     load, so it may raise a TLB exception - a direct-map address does not)
 *   3 implementation-defined
 *
 * "The Cache object indicated by op[2:0] is in the same order as the Cache
 * identified in CPUCFG10" and the per-level geometry (ways, sets, line size)
 * comes from CPUCFG11-CPUCFG14 ([3A5000] Table 54). Every sweep below is
 * derived from those words, so a part with different geometry needs no code
 * change.
 *
 * For instruction-stream coherency, CPUCFG3.ICACHET says the core keeps the
 * I-cache coherent with the D-cache in hardware ([3A5000] Table 54, bit 7).
 * That is the fast path of KeSweepICache; when it is clear the sweep falls
 * back to CACOP over the affected lines.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>

/* op[4:3] values, [V1-4.2.3.1]. */
#define LA64_CACOP_OP_SHIFT     3
#define LA64_CACOP_STORE_TAG    (0 << LA64_CACOP_OP_SHIFT)
#define LA64_CACOP_INDEX        (1 << LA64_CACOP_OP_SHIFT)
#define LA64_CACOP_HIT          (2 << LA64_CACOP_OP_SHIFT)

/* op[2:0] object indices: the order of the presence bits in CPUCFG10. */
#define LA64_CACHE_L1I          0
#define LA64_CACHE_L1D          1
#define LA64_CACHE_L2           2
#define LA64_CACHE_L3           3

/* Platform initialization owns this declaration. Zero means that coherent
 * DMA has not been established for the active machine. */
ULONG KiDmaIoCoherency;

/* Each cache object needs its own compile-time op immediate: CACOP's op is
 * an instruction field, not a register operand. */
FORCEINLINE VOID KiLa64CacheOpIndexL1I(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_INDEX | LA64_CACHE_L1I), "r"(Address) : "memory");
}
FORCEINLINE VOID KiLa64CacheOpIndexL1D(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_INDEX | LA64_CACHE_L1D), "r"(Address) : "memory");
}
FORCEINLINE VOID KiLa64CacheOpIndexL2(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_INDEX | LA64_CACHE_L2), "r"(Address) : "memory");
}
FORCEINLINE VOID KiLa64CacheOpIndexL3(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_INDEX | LA64_CACHE_L3), "r"(Address) : "memory");
}
FORCEINLINE VOID KiLa64CacheOpHitL1D(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_HIT | LA64_CACHE_L1D), "r"(Address) : "memory");
}
FORCEINLINE VOID KiLa64CacheOpHitL1I(_In_ ULONG_PTR Address)
{
    __asm__ __volatile__("cacop %0, %1, 0" :: "i"(LA64_CACOP_HIT | LA64_CACHE_L1I), "r"(Address) : "memory");
}

/* Ways, sets and line size of one cache level, from a CPUCFG11-14 word:
 * Way-1 at 15:0, Index-log2 at 23:16, Linesize-log2 at 30:24. */
typedef struct _KI_LA64_CACHE_GEOMETRY
{
    ULONG Ways;
    ULONG SetLog2;
    ULONG LineLog2;
    BOOLEAN Present;
} KI_LA64_CACHE_GEOMETRY, *PKI_LA64_CACHE_GEOMETRY;

static
VOID
KiLa64ReadCacheGeometry(
    _In_ ULONG PresenceBit,
    _In_ ULONG GeometryWord,
    _Out_ PKI_LA64_CACHE_GEOMETRY Geometry)
{
    ULONG Presence = KiLa64ReadCpucfg(0x10);
    ULONG Word = KiLa64ReadCpucfg(GeometryWord);

    Geometry->Present = (Presence & PresenceBit) != 0;
    Geometry->Ways = (Word & 0xFFFF) + 1;
    Geometry->SetLog2 = (Word >> 16) & 0xFF;
    Geometry->LineLog2 = (Word >> LA64_CPUCFG_LINESIZE_SHIFT) & LA64_CPUCFG_LINESIZE_MASK;

    /* A geometry outside these bounds cannot describe a real cache on a
     * 4 KiB-page machine; treat it as absent rather than sweeping with
     * nonsense numbers. */
    if ((Geometry->SetLog2 > 20) || (Geometry->LineLog2 < 4) ||
        (Geometry->LineLog2 > PAGE_SHIFT))
    {
        Geometry->Present = FALSE;
    }
}

/* Index-addressed sweep of one level: way = VA[Way-1:0], set =
 * VA[SetLog2+LineLog2-1:LineLog2], [V1-4.2.3.1]. The operation is always
 * invalidate-and-writeback; a cache row that is clean is unchanged by it. */
static
VOID
KiLa64SweepCacheByIndex(
    _In_ ULONG Object,
    _In_ const KI_LA64_CACHE_GEOMETRY *Geometry)
{
    ULONG Way, Set;
    ULONG_PTR Address;

    if (!Geometry->Present)
        return;

    for (Set = 0; Set < (1UL << Geometry->SetLog2); Set++)
    {
        for (Way = 0; Way < Geometry->Ways; Way++)
        {
            Address = ((ULONG_PTR)Set << Geometry->LineLog2) | Way;
            switch (Object)
            {
                case LA64_CACHE_L1I: KiLa64CacheOpIndexL1I(Address); break;
                case LA64_CACHE_L1D: KiLa64CacheOpIndexL1D(Address); break;
                case LA64_CACHE_L2:  KiLa64CacheOpIndexL2(Address);  break;
                default:             KiLa64CacheOpIndexL3(Address);  break;
            }
        }
    }
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    KI_LA64_CACHE_GEOMETRY L1I, L1D, L2, L3;

    KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1IU_PRESENT, 0x11, &L1I);
    KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1D_PRESENT, 0x12, &L1D);
    KiLa64ReadCacheGeometry(0x00000008, 0x13, &L2);
    KiLa64ReadCacheGeometry(0x00000400, 0x14, &L3);

    if (!L1I.Present && !L1D.Present)
        return FALSE;

    KiLa64MemoryBarrier();
    if (L1D.Present)
        KiLa64SweepCacheByIndex(LA64_CACHE_L1D, &L1D);
    if (L1I.Present)
        KiLa64SweepCacheByIndex(LA64_CACHE_L1I, &L1I);
    if (L2.Present)
        KiLa64SweepCacheByIndex(LA64_CACHE_L2, &L2);
    if (L3.Present)
        KiLa64SweepCacheByIndex(LA64_CACHE_L3, &L3);
    KiLa64InstructionBarrier();
    return TRUE;
}

VOID
NTAPI
KeSetDmaIoCoherency(_In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

/* The L1 data-cache line size, or 0 when the part does not report one. */
ULONG
NTAPI
KiLa64QueryCacheBlockSize(VOID)
{
    KI_LA64_CACHE_GEOMETRY L1D;

    KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1D_PRESENT, 0x12, &L1D);
    if (!L1D.Present)
        return 0;
    return 1u << L1D.LineLog2;
}

ULONG
NTAPI
KiLa64QueryLargestCacheLine(VOID)
{
    KI_LA64_CACHE_GEOMETRY Levels[4];
    ULONG Index, Line = 0;

    KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1IU_PRESENT, 0x11, &Levels[0]);
    KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1D_PRESENT, 0x12, &Levels[1]);
    KiLa64ReadCacheGeometry(0x00000008, 0x13, &Levels[2]);
    KiLa64ReadCacheGeometry(0x00000400, 0x14, &Levels[3]);

    for (Index = 0; Index < RTL_NUMBER_OF(Levels); Index++)
    {
        if (Levels[Index].Present)
            Line = max(Line, 1u << Levels[Index].LineLog2);
    }
    return Line;
}

/* Synchronize this core's instruction stream with the data it just wrote.
 * With CPUCFG3.ICACHET the hardware already keeps the two coherent, and the
 * barrier is the whole requirement; otherwise every line is hit-invalidated
 * in the L1 instruction cache. */
VOID
NTAPI
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    KI_LA64_CACHE_GEOMETRY L1I, L1D;
    ULONG CpuCfg3 = KiLa64ReadCpucfg(3);

    KiLa64MemoryBarrier();

    if ((CpuCfg3 & LA64_CPUCFG3_ICACHET) == 0)
    {
        ULONG LineSize;

        KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1IU_PRESENT, 0x11, &L1I);
        KiLa64ReadCacheGeometry(LA64_CPUCFG10_L1D_PRESENT, 0x12, &L1D);
        LineSize = L1D.Present ? (1u << L1D.LineLog2) :
                   (L1I.Present ? (1u << L1I.LineLog2) : 0);

        if (LineSize && (BaseAddress != NULL) && (FlushSize != 0))
        {
            ULONG_PTR Start = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(LineSize - 1);
            ULONG_PTR End = (ULONG_PTR)BaseAddress + FlushSize;

            for (; Start < End; Start += LineSize)
                KiLa64CacheOpHitL1I(Start);
        }
        else if (LineSize)
        {
            /* No range: drop the whole L1 instruction cache. */
            KiLa64SweepCacheByIndex(LA64_CACHE_L1I, &L1I);
        }
    }

    KiLa64InstructionBarrier();
}

static
VOID
KiLa64MaintainRange(
    _In_ ULONG_PTR Start,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    ULONG LineSize = KiLa64QueryCacheBlockSize();
    ULONG_PTR Address, End = Start + Length;

    if (!LineSize)
    {
        /* A part with no CPUCFG-reported L1 data cache: the barriers below
         * are the whole requirement. */
        KiLa64MemoryBarrier();
        return;
    }

    /* Hit invalidate (op type 2) on the L1 data cache. A dirty line is
     * written back towards memory before the invalidate, which is the
     * operation both a DMA read and a DMA write need; `Invalidate` only
     * documents the caller's intent. */
    (void)Invalidate;

    KiLa64MemoryBarrier();
    for (Address = Start & ~(ULONG_PTR)(LineSize - 1); Address < End; Address += LineSize)
        KiLa64CacheOpHitL1D(Address);
    KiLa64MemoryBarrier();
}

BOOLEAN
NTAPI
KiLa64IsPhysicalCached(
    _In_ ULONG64 PhysicalAddress)
{
    MI_LA64_PAGE_WALK Walk;

    /* The MAT field of the leaf that maps the direct-map alias decides
     * whether the page is cached ([V1-2.1.7]: 0 SUC, 1 CC, 2 WUC). A page
     * with no mapped alias is treated as cached, which is the conservative
     * answer for a DMA flush. */
    if ((PhysicalAddress >= LOONGARCH64_LOADER_PHYSICAL_LIMIT) ||
        !NT_SUCCESS(MiLa64WalkCurrentPageTables(MI_LA64_PFN_TO_VA(PhysicalAddress >> PAGE_SHIFT),
                                                &Walk)))
    {
        return TRUE;
    }
    return ((Walk.Value.u.Long & MI_LA64_PTE_MAT_MASK) != MI_LA64_PTE_MAT_SUC) &&
           ((Walk.Value.u.Long & MI_LA64_PTE_MAT_MASK) != MI_LA64_PTE_MAT_WUC);
}

BOOLEAN
NTAPI
KiLa64FlushDmaRange(
    _In_ ULONG64 PhysicalAddress,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    if (!Length)
        return TRUE;
    if (!KiLa64QueryCacheBlockSize() ||
        (PhysicalAddress >= LOONGARCH64_LOADER_PHYSICAL_LIMIT) ||
        (Length > LOONGARCH64_LOADER_PHYSICAL_LIMIT - PhysicalAddress))
    {
        return FALSE;
    }

    KiLa64MaintainRange((ULONG_PTR)(LOONGARCH64_LOADER_DIRECT_MAP_BASE + PhysicalAddress),
                        Length, Invalidate);
    return TRUE;
}

VOID
NTAPI
KeFlushIoBuffers(
    _In_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    /* Callers built for the architectures where this is an empty macro pass a
     * buffered request's NULL MDL. */
    if (Mdl == NULL)
        return;

    if (DmaOperation)
    {
        if (!KiDmaIoCoherency)
        {
            PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
            ULONG Offset = Mdl->ByteOffset, Remaining = Mdl->ByteCount;

            while (Remaining)
            {
                ULONG Chunk = min(Remaining, PAGE_SIZE - Offset);

                if (!KiLa64FlushDmaRange(((ULONG64)*Pages << PAGE_SHIFT) + Offset,
                                         Chunk, ReadOperation))
                {
                    KiLa64Unimplemented("KeFlushIoBuffers/noncoherent-DMA");
                }
                Remaining -= Chunk;
                Offset = 0;
                Pages++;
            }
        }
        KiLa64MemoryBarrier();
        return;
    }

    if (Mdl->ByteCount == 0)
        return;

    /* PIO and CPU copies need ordering, not cache maintenance: caches are
     * coherent with every alias of a page. A read may have supplied
     * executable bytes through another virtual alias, so synchronize this
     * core's instruction stream. */
    KiLa64MemoryBarrier();
    if (ReadOperation)
        KeSweepICache(NULL, 0);
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    if (!Length)
        return;

    if (!KiDmaIoCoherency)
        KiLa64MaintainRange((ULONG_PTR)BaseAddress, Length, TRUE);
    KeSweepICache(BaseAddress, Length);
}
