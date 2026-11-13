/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/loongarch64/hardware.h
 * PURPOSE:     LoongArch64 memory management hardware definitions
 *
 * The PTE bit positions and the PWCL/PWCH/DMW values come from
 * <ndk/loongarch64/mmtypes.h> (MI_LA64_*); this file adds the control
 * registers, the software-owned PTE bits this backend needs and the
 * register/instruction accessors.
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "archdef.h"

/*
 * Control registers.  Addresses and field layouts: LoongArch Vol1 sec. 7.4.1
 * (CRMD), 7.5.1 (TLBIDX), 7.5.2 (TLBEHI), 7.5.3 (TLBELO0/1), 7.5.4 (ASID),
 * 7.5.5/7.5.6 (PGDL/PGDH), 7.5.8 (PWCL), 7.5.9 (PWCH), 7.5.16 (TLBREHI),
 * 7.5.18 (DMW0-3).
 */
#define MI_LA64_CSR_CRMD        0x0
#define MI_LA64_CSR_TLBIDX      0x10
#define MI_LA64_CSR_TLBEHI      0x11
#define MI_LA64_CSR_TLBELO0     0x12
#define MI_LA64_CSR_TLBELO1     0x13
#define MI_LA64_CSR_ASID        0x18
#define MI_LA64_CSR_PGDL        0x19
#define MI_LA64_CSR_PGDH        0x1A
#define MI_LA64_CSR_PWCL        0x1C
#define MI_LA64_CSR_PWCH        0x1D
#define MI_LA64_CSR_TLBREHI     0x8D
#define MI_LA64_CSR_DMW0        0x180

/* CRMD: PLV 1:0, DA 3, PG 4 (Vol1 sec. 7.4.1).  Table 15 states the only legal
 * DA/PG combinations are {0,1} (mapped) and {1,0} (direct address) and that
 * anything else is "uncertain".  Mapped mode prefers a DMW hit and only then
 * walks the page table (Vol1 sec. 5.2). */
#define MI_LA64_CRMD_PLV_MASK   0x3ULL
#define MI_LA64_CRMD_DA         0x8ULL
#define MI_LA64_CRMD_PG         0x10ULL

/* TLBIDX: Index 15:0 (at most 16 bits), PS 29:24, NE 31 (Vol1 sec. 7.5.1,
 * Table 33; the instruction sections 4.2.4.2-4.2.4.4 call the same bit INV).
 * NE=1 means an empty/invalid TLB entry. */
#define MI_LA64_TLBIDX_INDEX_MASK 0x000000000000FFFFULL
#define MI_LA64_TLBIDX_PS_SHIFT   24
#define MI_LA64_TLBIDX_PS_MASK    0x000000003F000000ULL
#define MI_LA64_TLBIDX_NE         0x0000000080000000ULL

/* ASID: ASID 9:0, ASIDBITS 23:16 read-only (Vol1 sec. 7.5.4, Table 38).
 * ASIDBITS reports how many of the ten bits the implementation provides; this
 * port keys on the architectural 10-bit width, which is what the hardware
 * compares and what TLBFILL stores from CSR.ASID. */
#define MI_LA64_ASID_MASK       0x3FFULL

/* The software-owned leaf bits.  Neither PTE format in Vol1 sec. 5.4.5
 * (Figures 8 and 9) defines bits 9-11, and the same section guarantees that
 * bits "not defined in either of these formats are automatically ignored by
 * the LDDIR and LDPTE instructions or hardware page walking".  Loongson's
 * kernel keeps MODIFIED (9), PROTNONE (10) and SPECIAL (11) there
 * (arch/loongarch/include/asm/pgtable-bits.h); our encoding matches
 * HARDWARE_PTE.Owner (9), .Accessed (10) and .CopyOnWrite (11) in
 * <ndk/loongarch64/mmtypes.h>.
 * TODO: the manual never assigns bits 9-11 to software, so this rests on
 * "undefined bits are ignored"; the current choice is kept. */
#define MI_LA64_PTE_OWNER       0x0000000000000200ULL  /* bit 9  - unused by this backend */
#define MI_LA64_PTE_ACCESSED    0x0000000000000400ULL  /* bit 10 - software access hint */
#define MI_LA64_PTE_COPYONWRITE 0x0000000000000800ULL  /* bit 11 - private, not yet copied */

C_ASSERT((MI_LA64_PTE_OWNER | MI_LA64_PTE_ACCESSED | MI_LA64_PTE_COPYONWRITE) ==
         MI_LA64_PTE_SOFTWARE_MASK);

/*
 * A leaf carries V (0) and P (7): V is the valid bit of the page, P is
 * "whether the physical page exists", and both are needed for a translation
 * (Vol1 sec. 5.4.5, Figure 8).  A directory entry is the child table's frame
 * in PA[PALEN-1:12] plus P -- the encoding the loader installs
 * (LOONGARCH64_DIRECTORY_ENTRY) and the one this backend recognises.
 *
 * TODO: the manual defines P for the entry but never says whether a
 * directory-level P is consulted, and Loongson's kernel stores bare frames in
 * directory entries (arch/loongarch/include/asm/pgtable.h: pmd_bad() rejects
 * any low bit, empty slots point at a dummy table).  Hardware must therefore
 * take the child address from PA[PALEN-1:12] alone, which keeps frame|P
 * working; the converse -- that a P-clear directory entry stops a hardware
 * walk -- is not established and is the TODO G2 check in cpu.c.
 *
 * "Empty" is therefore two things: 0, which the core writes when it unlinks a
 * table, and the loader's shared empty-table pointer, a page-aligned value
 * with P clear (winldr.c La64BuildPageTables).  MiArchPteIsValid accepts the
 * first bit of either form, and MiArchBootRootFrame collapses the second form
 * to 0 before the core adopts the tree.
 *
 * Telling hardware entries from software ones (MiSoftMake in mipte.h: kind at
 * bits 1-3, protection 4-8, file 9-11, value from 12) forces the two-form
 * test.  No bit the loader sets on a directory entry is free in a software
 * entry -- bit 7 is protection bit 3 (MI_PROT_NOCACHE/NOACCESS) -- so "P set"
 * alone would misread a software PTE as a translation.  The split above is
 * exact instead:
 *   - a leaf needs V (bit 0), and MiSoftMake never sets bit 0, because Kind is
 *     shifted left by one;
 *   - a table pointer needs P with an otherwise zero offset, and a software
 *     entry with Kind 0 (the only kind that leaves bits 1-3 clear) is never
 *     stored -- every call site passes a real MiSoftKind.
 */
FORCEINLINE
BOOLEAN
MiLa64PteIsLeaf(_In_ MI_PTE Pte)
{
    return (Pte & (MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT)) ==
           (MI_LA64_PTE_VALID | MI_LA64_PTE_PRESENT);
}

FORCEINLINE
BOOLEAN
MiLa64PteIsTable(_In_ MI_PTE Pte)
{
    /* This backend's child pointer: a page frame with P and nothing else in
     * the page offset.  A page-aligned value with P clear is the loader's
     * shared empty table, not a child. */
    return (Pte & (PAGE_SIZE - 1)) == MI_LA64_PTE_PRESENT;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrCrmd(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x0" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrPgdl(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x19" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrPgdh(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x1a" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrPwcl(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x1c" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrPwch(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x1d" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrAsid(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x18" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrTlbEhi(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x11" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrTlbLo0(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x12" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrTlbLo1(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x13" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG64
MiLa64ReadCsrTlbIdx(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x10" : "=r"(Value));
    return Value;
}

/* DMW0-DMW3 are CSRs 0x180-0x183 with VSEG 63:60, MAT 5:4 and PLV0-3 3:0
 * (Vol1 sec. 7.5.18).  A window hits when VA[63:60] equals its VSEG and the
 * current CRMD.PLV has its PLV bit set; the hit translates VA[PALEN-1:0] with
 * no TLB entry (Vol1 sec. 5.2, 5.2.1, 7.5.18).  Windows 2 and 3 serve
 * load/store only, windows 0 and 1 also serve fetch. */
FORCEINLINE
ULONG64
MiLa64ReadDmw(_In_ ULONG Index)
{
    ULONG64 Value = 0;

    switch (Index)
    {
        case 0:
            __asm__ __volatile__("csrrd %0, 0x180" : "=r"(Value));
            break;
        case 1:
            __asm__ __volatile__("csrrd %0, 0x181" : "=r"(Value));
            break;
        case 2:
            __asm__ __volatile__("csrrd %0, 0x182" : "=r"(Value));
            break;
        case 3:
            __asm__ __volatile__("csrrd %0, 0x183" : "=r"(Value));
            break;
        default:
            break;
    }

    return Value;
}

/* The TLB instructions consume only CSRs: TLBEHI carries the VPPN, TLBELO0/1
 * the even and odd page, TLBIDX carries Index, PS and NE, and the ASID of a
 * written entry comes from CSR.ASID (Vol1 sec. 7.5.1-7.5.4, sec. 4.2.4.2-4.2.4.4).
 * No general register is an operand of TLBRD, TLBWR or TLBFILL.
 *
 * TLBRD updates TLBIDX (Index, PS and NE), TLBEHI, TLBELO0 and TLBELO1 and
 * nothing else; TLBREHI is the TLB-refill-context register and is not written
 * by TLBRD (Vol1 sec. 7.5.16). */
typedef struct _MI_LA64_TLB_ENTRY
{
    ULONG64 TlbIdx;
    ULONG64 TlbEhi;
    ULONG64 TlbElo0;
    ULONG64 TlbElo1;
} MI_LA64_TLB_ENTRY, *PMI_LA64_TLB_ENTRY;

FORCEINLINE
VOID
MiLa64WriteCsrTlbIdx(_In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0x10" : "+r"(Value) : : "memory");
}

FORCEINLINE
VOID
MiLa64WriteCsrTlbEhi(_In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0x11" : "+r"(Value) : : "memory");
}

FORCEINLINE
VOID
MiLa64WriteCsrTlbLo0(_In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0x12" : "+r"(Value) : : "memory");
}

FORCEINLINE
VOID
MiLa64WriteCsrTlbLo1(_In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0x13" : "+r"(Value) : : "memory");
}

/* Insert the entry described by TLBEHI/TLBELO0/TLBELO1 into a slot the
 * implementation picks at random.  PS comes from TLBIDX.PS while
 * CSR.TLBRERA.IsTLBR is 0 (Table 33, sec. 4.2.4.4), which is what makes the
 * entry land in the STLB (PS == STLBPS.PS) or the MTLB (anything else, and
 * only the MTLB stores PS per entry -- Vol1 sec. 5.4.1/5.4.2).  TLBREHI.PS
 * (Table 52) is the page size of the refill context and is used only while
 * IsTLBR is 1, so ordinary fills must not write it. */
VOID
MiLa64TlbFill(
    _In_ ULONG64 Vppn,
    _In_ ULONG64 EntryLo0,
    _In_ ULONG64 EntryLo1,
    _In_ ULONG PageShift);

/* Insert into the slot named by Index (TLBIDX.Index); TLBIDX.PS keeps the page
 * size already programmed there and TLBIDX.NE must stay 0 so the entry is
 * valid (Table 33, sec. 4.2.4.3). */
VOID
MiLa64TlbWriteIndex(
    _In_ ULONG Index);

/* Read the slot named by Index into Entry.  TLBRD fills TLBEHI/TLBELO0/1,
 * writes the entry's PS into TLBIDX.PS and its E bit inverted into TLBIDX.NE,
 * and zeroes TLBEHI/TLBELO0/1 for an empty slot (sec. 4.2.4.2). */
VOID
MiLa64TlbReadIndex(
    _In_ ULONG Index,
    _Out_ PMI_LA64_TLB_ENTRY Entry);
