/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V instruction-stream synchronization
 */

#include <ntoskrnl.h>

/* Platform initialization owns this declaration. Zero means that coherent
 * DMA has not been established for the active machine. */
ULONG KiDmaIoCoherency;

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /* No baseline whole-cache maintenance operation is available. */
    return FALSE;
}

VOID
NTAPI
KeSetDmaIoCoherency(_In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

VOID
NTAPI
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(FlushSize);

    KIRQL OldIrql = KeGetCurrentIrql();
    KAFFINITY Targets;
    if (OldIrql < SYNCH_LEVEL) KfRaiseIrql(SYNCH_LEVEL);
    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    __asm__ __volatile__("fence rw, rw\n\tfence.i" ::: "memory");
    Targets = KiRiscvDeferSleepingFences(Targets, KI_RISCV_FENCE_ICACHE);
    if (Targets) HalpRiscvRemoteFence(Targets, NULL, 0, TRUE);
    KfLowerIrql(OldIrql);
}

ULONG
NTAPI
KiRiscvQueryCacheBlockSize(VOID)
{
    ULONG Block = KiRiscvProcessorFeatures.CbomBlockSize;

    if (!(KiRiscvProcessorFeatures.Flags & KI_RISCV_FEATURE_ZICBOM) ||
        Block < 16 || Block > PAGE_SIZE || (Block & (Block - 1)))
    {
        return 0;
    }
    return Block;
}

static
VOID
KiRiscvMaintainRange(
    _In_ ULONG_PTR Start,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    ULONG_PTR Block = KiRiscvQueryCacheBlockSize();
    ULONG_PTR Address, End = Start + Length;

    if (!Block)
        KiRiscvUnimplemented("KiRiscvMaintainRange/no-zicbom");
    __asm__ __volatile__("fence rw, rw" ::: "memory");
    for (Address = Start & ~(Block - 1); Address < End; Address += Block)
    {
        if (Invalidate)
            __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 2" :: "r"(Address) : "memory");
        else
            __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 1" :: "r"(Address) : "memory");
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

BOOLEAN
NTAPI
KiRiscvIsPhysicalCached(
    _In_ ULONG64 PhysicalAddress)
{
    MI_RISCV_PAGE_WALK Walk;

    if (PhysicalAddress >= RISCV64_LOADER_PHYSICAL_LIMIT ||
        !NT_SUCCESS(MiRiscvWalkCurrentPageTables((PVOID)(ULONG_PTR)(RISCV64_LOADER_DIRECT_MAP_BASE + PhysicalAddress),
                                                 &Walk)))
    {
        return TRUE;
    }
    return (Walk.Value.u.Long & MI_RISCV_PTE_PBMT_MASK) == 0;
}

BOOLEAN
NTAPI
KiRiscvFlushDmaRange(
    _In_ ULONG64 PhysicalAddress,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Invalidate)
{
    if (!Length)
        return TRUE;
    if (!KiRiscvQueryCacheBlockSize() ||
        PhysicalAddress >= RISCV64_LOADER_PHYSICAL_LIMIT ||
        Length > RISCV64_LOADER_PHYSICAL_LIMIT - PhysicalAddress)
    {
        return FALSE;
    }
    KiRiscvMaintainRange((ULONG_PTR)(RISCV64_LOADER_DIRECT_MAP_BASE + PhysicalAddress), Length, Invalidate);
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
    ASSERT(Mdl != NULL);

    /* The HAL sets coherency only for a firmware-declared coherent DMA host.
     * In that case a fence orders CPU and device accesses; no cache clean or
     * invalidate is needed. Noncoherent DMA still needs a board provider. */
    if (DmaOperation)
    {
        if (!KiDmaIoCoherency)
        {
            PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
            ULONG Offset = Mdl->ByteOffset, Remaining = Mdl->ByteCount;

            while (Remaining)
            {
                ULONG Chunk = min(Remaining, PAGE_SIZE - Offset);

                if (!KiRiscvFlushDmaRange(((ULONG64)*Pages << PAGE_SHIFT) + Offset, Chunk, ReadOperation))
                    KiRiscvUnimplemented("KeFlushIoBuffers/noncoherent-DMA");
                Remaining -= Chunk;
                Offset = 0;
                Pages++;
            }
        }
        __asm__ __volatile__("fence iorw, iorw" ::: "memory");
        return;
    }
    if (Mdl->ByteCount == 0)
        return;

    /* PIO and CPU copies need ordering, not cache maintenance: caches are
     * coherent with every alias of a page. A read may have supplied
     * executable bytes through a different virtual alias; synchronize this
     * hart's instruction stream. */
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
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

    /* The coherent platform does not require data-cache maintenance to make
     * RAM visible to CPUs or devices. Order those accesses and synchronize
     * instruction caches on every active hart. A noncoherent platform needs
     * cache-block operations supplied by its hardware provider. */
    if (!KiDmaIoCoherency)
        KiRiscvMaintainRange((ULONG_PTR)BaseAddress, Length, TRUE);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    KeSweepICache(BaseAddress, Length);
}
