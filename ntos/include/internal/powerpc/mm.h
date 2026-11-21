/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC memory-manager definitions shared by the
 *              kernel and the NVS ppc backend (ntoskrnl/nvs/arch/ppc)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define _MI_PAGING_LEVELS 2
#define _MI_HAS_NO_EXECUTE 0

typedef MMPTE MMPDE, *PMMPDE;
typedef MMPTE MMPPE, *PMMPPE;

#define MI_USER_PROBE_ADDRESS           ((PVOID)0x7FFF0000UL)
#define MI_DEFAULT_SYSTEM_RANGE_START   ((PVOID)0x80000000UL)
#define MI_HIGHEST_SYSTEM_ADDRESS       ((PVOID)0xFFFFFFFFUL)
#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS          ((PVOID)MM_ALLOCATION_GRANULARITY)
#endif
#define MM_HIGHEST_VAD_ADDRESS          ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))
#ifndef MM_SHARED_USER_DATA_VA
#define MM_SHARED_USER_DATA_VA          0x7FFE0000UL
#endif
#define MI_MAX_ZERO_BITS                21

/* Private MmAccessFault flags derived from DSISR/SRR1 by trap entry. */
#define MI_PPC_FAULT_PRESENT 0x01
#define MI_PPC_FAULT_WRITE   0x02
#define MI_PPC_FAULT_EXECUTE 0x10
#define MI_IS_NOT_PRESENT_FAULT(Code) (!BooleanFlagOn((Code), MI_PPC_FAULT_PRESENT))
#define MI_IS_WRITE_ACCESS(Code) BooleanFlagOn((Code), MI_PPC_FAULT_WRITE)
#define MI_IS_INSTRUCTION_FETCH(Code) BooleanFlagOn((Code), MI_PPC_FAULT_EXECUTE)

/* Software page-table entries: the i386 layout. */
#define MI_PPC_PTE_VALID         0x001UL
#define MI_PPC_PTE_WRITE         0x002UL
#define MI_PPC_PTE_OWNER         0x004UL
#define MI_PPC_PTE_WRITETHROUGH  0x008UL
#define MI_PPC_PTE_CACHEDISABLE  0x010UL
#define MI_PPC_PTE_ACCESSED      0x020UL
#define MI_PPC_PTE_DIRTY         0x040UL
#define MI_PPC_PTE_LARGE         0x080UL
#define MI_PPC_PTE_GLOBAL        0x100UL
#define MI_PPC_PTE_COPYONWRITE   0x200UL
#define MI_PPC_PTE_FRAME_MASK    0xFFFFF000UL
#define MI_PPC_PTE_FRAME_SHIFT   12

/* Level geometry: 1024-entry root (VA[31:22]), 1024-entry leaf (VA[21:12]). */
#define MI_PPC_ROOT_SHIFT        22
#define MI_PPC_ROOT_ENTRIES      1024
#define MI_PPC_ROOT_KERNEL_INDEX 512

/* KSEG0 reaches every RAM page and every page table. */
#define MI_PPC_PFN_TO_VA(Pfn) \
    ((PVOID)(ULONG_PTR)(PPC_LOADER_KSEG0_BASE + ((ULONG_PTR)(Pfn) << PAGE_SHIFT)))
#define MI_PPC_KSEG0_PAGES       (PPC_LOADER_KSEG0_SIZE >> PAGE_SHIFT)

typedef struct _MI_PPC_PAGE_WALK
{
    PMMPTE Entry;
    MMPTE Value;
    PFN_NUMBER TablePageFrame;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Level;
} MI_PPC_PAGE_WALK, *PMI_PPC_PAGE_WALK;

extern PVOID MmSessionBase;
extern SIZE_T MmSessionSize;

NTSTATUS NTAPI MiPpcWalkPageTables(_In_ PFN_NUMBER RootPageFrame, _In_ ULONG_PTR VirtualAddress, _Out_ PMI_PPC_PAGE_WALK Result);
NTSTATUS NTAPI MiPpcWalkCurrentPageTables(_In_ PVOID Address, _Out_ PMI_PPC_PAGE_WALK Result);

#ifdef __cplusplus
}
#endif
