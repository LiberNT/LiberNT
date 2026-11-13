/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/tlb.c
 * PURPOSE:     LoongArch64 translation cache maintenance and TLB access
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mienv.h>
#include "hardware.h"

/*
 * invtlb operation codes (op is a 5-bit immediate, operands are (op, rj, rk)).
 * Vol1 sec. 4.2.4.7, Table 13:
 *   0x0  clear all page table entries
 *   0x1  same effect as 0x0
 *   0x2  clear all G=1 entries
 *   0x3  clear all G=0 entries
 *   0x4  clear G=0 entries whose ASID equals the register-specified ASID
 *   0x5  clear G=0 entries with that ASID and the register-specified VA
 *   0x6  clear entries whose G=1, or whose ASID matches the register ASID and
 *        whose VA matches the register VA
 * rj holds the ASID (bits 9:0, rest zero) and rk the VA; an operand the op
 * does not need must be r0.  Note that 0x2/0x3 are not ASID-scoped and that
 * 0x1 is not a separate "current ASID" operation.
 */
#define MI_LA64_INVTLB_ALL              0x0
#define MI_LA64_INVTLB_ADDR_GTRUE_OR_ASID 0x6

/* A TLB entry covers an adjacent even/odd pair of pages at the entry's own
 * page size -- 4 KiB here, because STLBPS.PS is 12 -- and its VPPN is the
 * virtual page number with the lowest bit dropped (Vol1 sec. 5.4.2); Loongson's
 * kernel masks with PAGE_MASK << 1 before invalidating a single address. */
#define MI_LA64_TLB_PAIR_SHIFT          (MI_ARCH_PAGE_SHIFT + 1)
#define MI_LA64_TLB_PAIR_SIZE           (1ULL << MI_LA64_TLB_PAIR_SHIFT)
#define MI_LA64_TLB_PAIR_MASK           (MI_LA64_TLB_PAIR_SIZE - 1)

/* Ranges above this many pairs flush the whole translation cache instead. */
#define MI_LA64_TLB_RANGE_LIMIT         64

VOID
MiLa64TlbFill(
    _In_ ULONG64 Vppn,
    _In_ ULONG64 EntryLo0,
    _In_ ULONG64 EntryLo1,
    _In_ ULONG PageShift)
{
    /* NE = 0 (valid entry) and PS = PageShift; the rest of TLBIDX is the index
     * TLBFILL does not use (Vol1 sec. 7.5.1, sec. 4.2.4.4).  PS is six bits
     * holding log2(page size), so 4 KiB is 12 (Table 44 for STLBPS). */
    ULONG64 TlbIdx = ((ULONG64)PageShift << MI_LA64_TLBIDX_PS_SHIFT) & MI_LA64_TLBIDX_PS_MASK;

    MI_ASSERT(PageShift >= MI_ARCH_PAGE_SHIFT && PageShift < 64);

    /* TLBEHI holds the VPPN of the pair; TLBELO0/1 the even and odd page
     * (Vol1 sec. 7.5.2, 7.5.3, 5.4.2).  TLBFILL takes no register operand:
     * the slot is implementation-chosen and the ASID comes from CSR.ASID. */
    MiLa64WriteCsrTlbEhi(Vppn);
    MiLa64WriteCsrTlbLo0(EntryLo0);
    MiLa64WriteCsrTlbLo1(EntryLo1);
    MiLa64WriteCsrTlbIdx(TlbIdx);

    __asm__ __volatile__("tlbfill" ::: "memory");
}

VOID
MiLa64TlbWriteIndex(
    _In_ ULONG Index)
{
    ULONG64 TlbIdx;

    MI_ASSERT(Index <= MI_LA64_TLBIDX_INDEX_MASK);

    /* TLBWR writes the slot named by TLBIDX.Index, taking PS from TLBIDX.PS
     * and E from TLBIDX.NE inverted (Table 33, sec. 4.2.4.3).  Rebuilding the
     * register from PS and Index also clears NE, so the entry is valid. */
    TlbIdx = MiLa64ReadCsrTlbIdx();
    TlbIdx = (TlbIdx & MI_LA64_TLBIDX_PS_MASK) | ((ULONG64)Index & MI_LA64_TLBIDX_INDEX_MASK);
    MiLa64WriteCsrTlbIdx(TlbIdx);

    __asm__ __volatile__("tlbwr" ::: "memory");
}

VOID
MiLa64TlbReadIndex(
    _In_ ULONG Index,
    _Out_ PMI_LA64_TLB_ENTRY Entry)
{
    ULONG64 TlbIdx;

    MI_ASSERT(Index <= MI_LA64_TLBIDX_INDEX_MASK);

    TlbIdx = MiLa64ReadCsrTlbIdx();
    TlbIdx = (TlbIdx & MI_LA64_TLBIDX_PS_MASK) | ((ULONG64)Index & MI_LA64_TLBIDX_INDEX_MASK);
    MiLa64WriteCsrTlbIdx(TlbIdx);

    __asm__ __volatile__("tlbrd" ::: "memory");

    /* TLBRD writes TLBIDX (Index/PS/NE), TLBEHI, TLBELO0 and TLBELO1 only.
     * For an empty slot TLBEHI/TLBELO0/TLBELO1 read 0, PS reads 0 and NE
     * reads 1 (sec. 4.2.4.2). */
    Entry->TlbIdx = MiLa64ReadCsrTlbIdx();
    Entry->TlbEhi = MiLa64ReadCsrTlbEhi();
    Entry->TlbElo0 = MiLa64ReadCsrTlbLo0();
    Entry->TlbElo1 = MiLa64ReadCsrTlbLo1();
}

/* Local fences only order this processor's translations; every other active
 * processor is reached through the TLB shootdown IPI. */
VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors && KeNumberProcessors > 1)
        KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
    else
        __asm__ __volatile__("dbar 0\n\tinvtlb 0x0, $r0, $r0" ::: "memory");
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address;
    ULONG_PTR End;
    ULONG_PTR Cursor;
    ULONG64 Asid;
    SIZE_T Pairs;

    if (Size == 0)
        return;

    if (Size > ~(ULONG_PTR)BaseAddress)
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }

    /* Widen to whole 4 KiB pairs: invalidating either half drops both. */
    Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)MI_LA64_TLB_PAIR_MASK;
    End = ((ULONG_PTR)BaseAddress + Size + MI_LA64_TLB_PAIR_MASK) & ~(ULONG_PTR)MI_LA64_TLB_PAIR_MASK;
    Pairs = (SIZE_T)((End - Address) >> MI_LA64_TLB_PAIR_SHIFT);

    if (Pairs == 0 || Pairs > MI_LA64_TLB_RANGE_LIMIT)
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }

    if (Scope == MiTlbAllProcessors && KeNumberProcessors > 1)
    {
        /* The IPI carries a page count over the widened range (the Address
         * and Pages arguments of KiIpiSendTbFlush), not a pair count; the
         * receiver applies its own pair widening. */
        KiIpiSendTbFlush(KeActiveProcessors,
                         (PVOID)Address,
                         (ULONG)((End - Address) >> MI_ARCH_PAGE_SHIFT));
        return;
    }

    /* op 0x6 clears the entries for this VA whose G is 1 (the kernel mappings,
     * which every address space shares) and the G=0 entries for this VA whose
     * ASID equals the register ASID (Table 13).
     * TODO: once address spaces carry their own ASID, this must use that
     * space's ASID instead of the register value; the core does not switch
     * ASIDs yet. */
    Asid = MiLa64ReadCsrAsid() & MI_LA64_ASID_MASK;

    for (Cursor = Address; Cursor != End; Cursor += MI_LA64_TLB_PAIR_SIZE)
        __asm__ __volatile__("invtlb 0x6, %0, %1" :: "r"(Asid), "r"((ULONG64)Cursor) : "memory");
}

VOID
MiArchInvalidateTlbSingle(PVOID VirtualAddress, MI_TLB_SCOPE Scope)
{
    MiArchInvalidateTlbRange(VirtualAddress, PAGE_SIZE, Scope);
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;

    if (PageCount > ((SIZE_T)-1 >> MI_ARCH_PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress,
                                 (SIZE_T)(PageCount << MI_ARCH_PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateProcessors(ULONG64 VirtualAddress, ULONG64 PageCount, ULONG64 Processors)
{
    UNREFERENCED_PARAMETER(Processors);
    /* TODO: send the shootdown only to Processors, as amd64 does; every active
     * processor is invalidated. */
    MiArchTlbInvalidate(VirtualAddress, PageCount, TRUE);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
