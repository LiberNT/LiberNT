/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 memory-manager definitions: the four-level 4 KiB
 *              address space, the hardware page-table entry format and the
 *              architecture helpers shared by the kernel and the NVS
 *              LoongArch64 backend (ntos/mm/arch/loongarch64).
 *
 * The constants themselves live in <ndk/loongarch64/mmtypes.h> (MMPTE,
 * MI_LA64_PTE_*, MI_LA64_PWCL_*, MI_LA64_PWCH_*, MI_LA64_*_BASE); this file
 * adds the kernel-private policy on top of them and never redefines them.
 *
 * Sources:
 *   [V1-5.4.5]  page-walk levels; PGDL/PGDH select the top directory on
 *               VA[PALEN-1]; PWCL/PWCH give each level's index base/width
 *   [V1-7.5.8]  PWCL: PTbase 4:0, PTwidth 9:5, Dir1/2 base+width, PTEWidth
 *   [V1-7.5.9]  PWCH: Dir3/Dir4 base+width
 *   [V1-7.5.3]  TLBELO: V 0, D 1, PLV 3:2, MAT 5:4, G 6, PPN, NR 61, NX 62,
 *               RPLV 63
 *   [V1-6.1.1]  ESTAT.Ecode values
 *   [L-PTE]     linux arch/loongarch/include/asm/pgtable.h, Loongson
 *               copyright: PTE bit positions, PPN mask, mat encodings
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* PGDL points at the Dir3 table, so a 4 KiB translation walks PGD, Dir3,
 * Dir2, Dir1 and the PT: four levels below the root register, 9 index bits
 * each, [V1-5.4.5] with the 4-level 4 KiB PWCL/PWCH encoding in
 * <ndk/loongarch64/mmtypes.h>. */
#define _MI_PAGING_LEVELS 4
#define _MI_HAS_NO_EXECUTE 1

/* All page-walk levels share the same 64-bit entry format. */
typedef MMPTE MMPDE, *PMMPDE;
typedef MMPTE MMPPE, *PMMPPE;

/* 48-bit VALEN split into the two canonical halves ([V1-5.4.2]; the loader's
 * LOONGARCH64_VALEN_48). A kernel VA sign-extends bit 47, so the highest
 * user address stays one byte below the boundary the type layer publishes. */
#define MI_DEFAULT_SYSTEM_RANGE_START   ((PVOID)MI_LA64_KERNEL_BASE)
#define MI_HIGHEST_SYSTEM_ADDRESS       ((PVOID)0xFFFFFFFFFFFFFFFFULL)

/* First address above the user half, rounded up to the allocation
 * granularity; a probe there must fault. This is the same boundary the
 * ARM64 port uses for the same 64 KiB-guarded 47-bit user split. */
#define MI_USER_PROBE_ADDRESS           ((PVOID)0x00007FFFFFFF0000ULL)

#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS          ((PVOID)MM_ALLOCATION_GRANULARITY)
#endif

#define MM_HIGHEST_VAD_ADDRESS          ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))

/* The 64-bit NT ZeroBits count limit is independent of the VALEN limit. */
#define MI_MAX_ZERO_BITS                53

/* WOW64 compatibility limits; no 32-bit guest ABI is implemented. */
#define MM_HIGHEST_USER_ADDRESS_WOW64   0x7FFEFFFF
#define MM_HIGHEST_USER_ADDRESS_WOW64_LARGE 0xFFFEFFFF
#define MM_SYSTEM_RANGE_START_WOW64     0x80000000

/* Private MmAccessFault flags, never raw ESTAT.Ecode. Trap entry derives
 * PRESENT from the stabilized translation and WRITE/EXECUTE from the cause
 * (PIL/PIS/PIF and the page-modification trap), exactly as the other ports
 * do; the numerical values are this kernel's own, not hardware bits. */
#define MI_LA64_FAULT_PRESENT 0x01
#define MI_LA64_FAULT_WRITE   0x02
#define MI_LA64_FAULT_EXECUTE 0x10
#define MI_IS_NOT_PRESENT_FAULT(Code) (!BooleanFlagOn((Code), MI_LA64_FAULT_PRESENT))
#define MI_IS_WRITE_ACCESS(Code) BooleanFlagOn((Code), MI_LA64_FAULT_WRITE)
#define MI_IS_INSTRUCTION_FETCH(Code) BooleanFlagOn((Code), MI_LA64_FAULT_EXECUTE)

/* Hardware-meaningful PTE bits (V, D, PLV, MAT, G, P, W, PPN, NR, NX, RPLV)
 * and the PWCL/PWCH encodings come from <ndk/loongarch64/mmtypes.h> as
 * MI_LA64_PTE_* / MI_LA64_MAT_* / MI_LA64_PWCL_* / MI_LA64_PWCH_*.
 * [V1-5.4.5] Figure 8 places them: V 0, D 1, PLV 3:2, MAT 5:4, H 6, P 7,
 * W 8, PA[PALEN-1:12], NR 61, NX 62, RPLV 63, and Figure 9 moves the G of a
 * huge entry to bit 12. A leaf PTE uses bit 6 as G, a directory entry uses
 * it as the huge-page flag, which is why MI_LA64_PTE_GLOBAL and
 * MI_LA64_PTE_HUGE name the same bit. Bits 11:9 are undefined in both
 * formats and are ignored by LDDIR/LDPTE and the hardware walk, so they carry
 * the kernel's software flags (Owner, Accessed, CopyOnWrite) through
 * MI_LA64_PTE_SOFTWARE_MASK.
 *
 * Unlike Svpbmt on the RISC-V port there is no switch to enable memory
 * attributes here: every TLB entry carries its own MAT field, [V1-5.4.2], so
 * this port has no MiLa64PbmtEnabled equivalent. */

/* The DMW1 window maps physical memory at a fixed offset, so the direct map
 * reaches every RAM page and page table ([V1-7.5.18]; the loader's
 * LOONGARCH64_DMW1_VALUE = 0x9000000000000011 sets VSEG 0x9, MAT=CC,
 * PLV0). */
#define MI_LA64_PFN_TO_VA(Pfn) \
    ((PVOID)(ULONG_PTR)(MI_LA64_DIRECT_MAP_BASE + ((ULONG64)(Pfn) << PAGE_SHIFT)))

/* Canonical for the 48-bit VALEN split: bits 63:48 repeat bit 47, which is
 * what sign-extending the low 48 bits tests. */
FORCEINLINE
BOOLEAN
MiLa64IsCanonicalAddress(_In_ ULONG_PTR Address)
{
    return ((ULONG_PTR)((LONG64)(Address << 16) >> 16) == Address);
}

/* Read-only walk of the live page tables through the direct map. Level 0 is
 * the PT entry; Level 1 the Dir1 entry, and so on up to the Dir3 entry that
 * PGDL/PGDH points at. */
typedef struct _MI_LA64_PAGE_WALK
{
    PMMPTE Entry;
    MMPTE Value;
    PFN_NUMBER TablePageFrame;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Level;
    BOOLEAN Global;
} MI_LA64_PAGE_WALK, *PMI_LA64_PAGE_WALK;

VOID NTAPI MiLa64InitializePageTableAccess(_In_ PFN_NUMBER HighestTableFrame);

NTSTATUS
NTAPI
MiLa64WalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _Out_ PMI_LA64_PAGE_WALK Result);

NTSTATUS NTAPI MiLa64WalkCurrentPageTables(_In_ PVOID Address, _Out_ PMI_LA64_PAGE_WALK Result);

#ifdef __cplusplus
}
#endif
