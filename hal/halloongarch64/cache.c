/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Cache geometry and DMA cache maintenance
 *
 * There are no hardware A/D bits and, on a machine without CPUCFG3.CCDMA, no
 * hardware cache coherency for DMA: the CPU's dirty lines are invisible to a
 * device and a device's stores are invisible to the CPU until the lines are
 * written back and invalidated. CACOP [V1-4.2.3.1] is the only maintenance
 * primitive. op[2:0] selects the cache in CPUCFG10 order (op 1 = first-level
 * private data cache, 2 = second level, 3 = third level) and op[4:3] = 2
 * selects "hit invalidate / invalidate and writeback", which the manual
 * defines as one combined operation for a data cache. There is therefore no
 * invalidate-without-writeback form; a caller that lets a device write a
 * buffer must have written back (or never dirtied) the lines first, which is
 * why the HAL writes back before every hand-off and after every completion.
 *
 * All maintenance here addresses memory through the DMW1 cached window of the
 * page's physical address, so one call covers every mapping of the page.
 */

#include <ntifs.h>
#include "halp.h"

/* CPUCFG16 cache presence bits [V1-4.2.6.5]. */
#define HALP_LA64_CPUCGF16_L1D_PRESENT 0x0004ULL
#define HALP_LA64_CPUCGF16_L2_IU_PRESENT 0x0008ULL
#define HALP_LA64_CPUCGF16_L2D_PRESENT 0x0080ULL
#define HALP_LA64_CPUCGF16_L3_IU_PRESENT 0x0400ULL
#define HALP_LA64_CPUCGF16_L3D_PRESENT 0x4000ULL

/* CPUCFG17/18/19/20 layout [V1-4.2.6.5]: bits 30:24 are log2(line bytes). */
#define HALP_LA64_CPUCGF_LINE_SHIFT 24
#define HALP_LA64_CPUCGF_LINE_MASK  0x7FULL

/* CACOP op encoding [V1-4.2.3.1]: op[4:3] = 2 (hit invalidate/writeback),
 * op[2:0] = cache object in CPUCFG10 order. */
#define HALP_LA64_CACOP_HIT_INVALIDATE(Level) ((ULONG)((2UL << 3) | (Level)))

#define HALP_LA64_CACHE_MAX_LEVELS 3

typedef struct _HALP_LA64_CACHE_INFO
{
    ULONG LineSize;
    ULONG Levels;
    ULONG Level[HALP_LA64_CACHE_MAX_LEVELS];
} HALP_LA64_CACHE_INFO;

static HALP_LA64_CACHE_INFO HalpLa64Cache;

static
ULONG64
HalpLa64ReadCpucfg(
    _In_ ULONG Number)
{
    ULONG64 Value;

    __asm__ __volatile__("cpucfg %0, %1" : "=r"(Value) : "r"((ULONG)Number));
    return Value;
}

static
ULONG
HalpLa64LineFromCpucfg(
    _In_ ULONG Number)
{
    ULONG64 Value = HalpLa64ReadCpucfg(Number);
    ULONG Shift = (ULONG)((Value >> HALP_LA64_CPUCGF_LINE_SHIFT) &
                          HALP_LA64_CPUCGF_LINE_MASK);

    if (Shift < 4 || Shift > 12)
        return 0;
    return 1u << Shift;
}

/* Populate the geometry once, from the boot core. Every core of a
 * homogeneous machine reports the same values; a heterogeneous machine would
 * need per-core data, which this port does not attempt. */
static
VOID
HalpLa64InitializeCacheInfo(VOID)
{
    ULONG64 Present;
    ULONG Line;

    if (HalpLa64Cache.LineSize)
        return;

    Present = HalpLa64ReadCpucfg(16);
    Line = HalpLa64LineFromCpucfg(18);
    if (!Line)
        Line = 64; /* Every implementation the manual's CPUCFG17-20 tables
                    * describe reports a power-of-two line of at least 16
                    * bytes; 64 is the conservative step for a missing field
                    * and only affects how many redundant ops are issued. */

    HalpLa64Cache.LineSize = Line;
    HalpLa64Cache.Levels = 0;
    if (Present & HALP_LA64_CPUCGF16_L1D_PRESENT)
        HalpLa64Cache.Level[HalpLa64Cache.Levels++] = 1;
    if (Present & (HALP_LA64_CPUCGF16_L2_IU_PRESENT | HALP_LA64_CPUCGF16_L2D_PRESENT))
    {
        Line = HalpLa64LineFromCpucfg(19);
        if (Line && Line < HalpLa64Cache.LineSize)
            HalpLa64Cache.LineSize = Line;
        HalpLa64Cache.Level[HalpLa64Cache.Levels++] = 2;
    }
    if (Present & (HALP_LA64_CPUCGF16_L3_IU_PRESENT | HALP_LA64_CPUCGF16_L3D_PRESENT))
    {
        Line = HalpLa64LineFromCpucfg(20);
        if (Line && Line < HalpLa64Cache.LineSize)
            HalpLa64Cache.LineSize = Line;
        HalpLa64Cache.Level[HalpLa64Cache.Levels++] = 3;
    }
    if (!HalpLa64Cache.Levels)
    {
        /* A core that reports no data cache still executes CACOP for op 1 as
         * a no-op, which is cheaper than special-casing every caller. */
        HalpLa64Cache.Level[HalpLa64Cache.Levels++] = 1;
    }
}

ULONG
HalpLa64CacheLineSize(VOID)
{
    HalpLa64InitializeCacheInfo();
    return HalpLa64Cache.LineSize;
}

/* The op immediate must be a compile-time constant in the instruction, so the
 * three possible levels are spelled out rather than passed in a register. */
static
VOID
HalpLa64InvalidateLevel(
    _In_ ULONG Level,
    _In_ ULONG64 Address)
{
    switch (Level)
    {
        case 1:
            __asm__ __volatile__("cacop %0, %1, 0"
                                 :
                                 : "i"(0x11), "r"(Address)
                                 : "memory");
            break;
        case 2:
            __asm__ __volatile__("cacop %0, %1, 0"
                                 :
                                 : "i"(0x12), "r"(Address)
                                 : "memory");
            break;
        case 3:
            __asm__ __volatile__("cacop %0, %1, 0"
                                 :
                                 : "i"(0x13), "r"(Address)
                                 : "memory");
            break;
        default:
            break;
    }
}

/* Write back and invalidate every cache line that backs a physical range.
 * Called with the range already owned by the caller (a common buffer or a map
 * register window), so no locking is needed here. */
VOID
HalpLa64FlushDmaPhysical(
    _In_ ULONG64 Physical,
    _In_ SIZE_T Length)
{
    ULONG64 Address, End;
    ULONG Index, Line;

    if (!Length)
        return;
    if (HalpLa64Platform.DmaCoherent)
    {
        /* CPUCFG3.CCDMA: the hardware keeps the caches coherent with DMA, so
         * the only requirement is that the CPU's stores are ordered before
         * the device's read. DBAR 0 does that [V1-2.2.8.1]. */
        __asm__ __volatile__("dbar 0" ::: "memory");
        return;
    }

    HalpLa64InitializeCacheInfo();
    Line = HalpLa64Cache.LineSize;
    Address = Physical & ~(ULONG64)(Line - 1);
    End = Physical + Length;
    for (; Address < End; Address += Line)
    {
        ULONG64 Va = HalpLa64DirectMapBase + Address;

        for (Index = 0; Index < HalpLa64Cache.Levels; ++Index)
            HalpLa64InvalidateLevel(HalpLa64Cache.Level[Index], Va);
    }
    __asm__ __volatile__("dbar 0" ::: "memory");
}

/* Video/rectangle flush hook in the HAL private dispatch table. The buffer is
 * described by an MDL; each page's physical address is maintained through the
 * direct map so a process-relative user VA is never dereferenced here. */
static
VOID
NTAPI
HalpLa64FlushIoRectangleExternalCache(
    _In_ PMDL Mdl,
    _In_ ULONG StartOffset,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride,
    _In_ BOOLEAN ReadOperation)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG Row, Remaining, InPage, Chunk;
    ULONG_PTR Offset;

    UNREFERENCED_PARAMETER(ReadOperation);

    for (Row = 0; Row < Height; ++Row)
    {
        Offset = Mdl->ByteOffset + StartOffset + (ULONG_PTR)Row * Stride;
        for (Remaining = Width; Remaining; Remaining -= Chunk, Offset += Chunk)
        {
            InPage = (ULONG)(Offset & (PAGE_SIZE - 1));
            Chunk = min(Remaining, PAGE_SIZE - InPage);
            HalpLa64FlushDmaPhysical(((ULONG64)Pages[Offset >> PAGE_SHIFT] << PAGE_SHIFT) +
                                     InPage, Chunk);
        }
    }
}

VOID
HalpLa64InitializeCache(VOID)
{
    HalpLa64InitializeCacheInfo();
    HALPRIVATEDISPATCH->HalFlushIoRectangleExternalCache =
        HalpLa64FlushIoRectangleExternalCache;
}
