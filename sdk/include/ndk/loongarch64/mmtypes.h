/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 *              Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

#ifndef _LOONGARCH64_MMTYPES_H
#define _LOONGARCH64_MMTYPES_H

#define PAGE_SIZE                         0x1000
#define PAGE_SHIFT                        12L
#define MM_ALLOCATION_GRANULARITY         0x10000
#define MM_ALLOCATION_GRANULARITY_SHIFT   16L
#define MM_PAGE_FRAME_NUMBER_SIZE         (64 - PAGE_SHIFT)

#define MM_SHARED_USER_DATA_VA            0x7FFE0000UL

#define MI_HIGHEST_USER_ADDRESS           (PVOID)0x00007FFFFFFEFFFFULL
#define MI_LA64_KERNEL_BASE               0xFFFF800000000000ULL

#ifdef __cplusplus
extern "C" {
#endif

extern NTSYSAPI PVOID MmHighestUserAddress;
extern NTSYSAPI PVOID MmSystemRangeStart;
extern NTSYSAPI ULONG_PTR MmUserProbeAddress;

#ifdef __cplusplus
}
#endif

#define MM_HIGHEST_USER_ADDRESS  MmHighestUserAddress
#define MM_SYSTEM_RANGE_START    MmSystemRangeStart
#define MM_USER_PROBE_ADDRESS    MmUserProbeAddress

#define MI_LA64_MAT_SUC                   0
#define MI_LA64_MAT_CC                    1
#define MI_LA64_MAT_WUC                   2

#define MI_LA64_PTE_VALID                 0x0000000000000001ULL
#define MI_LA64_PTE_DIRTY                 0x0000000000000002ULL
#define MI_LA64_PTE_PLV_SHIFT             2
#define MI_LA64_PTE_PLV_MASK              0x000000000000000CULL
#define MI_LA64_PTE_PLV_KERNEL            (0ULL << MI_LA64_PTE_PLV_SHIFT)
#define MI_LA64_PTE_PLV_USER              (3ULL << MI_LA64_PTE_PLV_SHIFT)
#define MI_LA64_PTE_MAT_SHIFT             4
#define MI_LA64_PTE_MAT_MASK              0x0000000000000030ULL
#define MI_LA64_PTE_MAT_SUC               (MI_LA64_MAT_SUC << MI_LA64_PTE_MAT_SHIFT)
#define MI_LA64_PTE_MAT_CC                (MI_LA64_MAT_CC << MI_LA64_PTE_MAT_SHIFT)
#define MI_LA64_PTE_MAT_WUC               (MI_LA64_MAT_WUC << MI_LA64_PTE_MAT_SHIFT)
#define MI_LA64_PTE_GLOBAL                0x0000000000000040ULL
#define MI_LA64_PTE_HUGE                  0x0000000000000040ULL
#define MI_LA64_PTE_PRESENT               0x0000000000000080ULL
#define MI_LA64_PTE_WRITE                 0x0000000000000100ULL
#define MI_LA64_PTE_SOFTWARE_MASK         0x0000000000000E00ULL
#define MI_LA64_PTE_PFN_SHIFT             12
#define MI_LA64_PTE_PFN_BITS              36
#define MI_LA64_PTE_PFN_MASK              0x0000FFFFFFFFF000ULL
#define MI_LA64_PTE_HGLOBAL               0x0000000000001000ULL
#define MI_LA64_PTE_NR                    0x2000000000000000ULL
#define MI_LA64_PTE_NX                    0x4000000000000000ULL
#define MI_LA64_PTE_RPLV                  0x8000000000000000ULL

#define MI_LA64_PWCL_PTBASE_SHIFT         0
#define MI_LA64_PWCL_PTWIDTH_SHIFT        5
#define MI_LA64_PWCL_DIR1BASE_SHIFT       10
#define MI_LA64_PWCL_DIR1WIDTH_SHIFT      15
#define MI_LA64_PWCL_DIR2BASE_SHIFT       20
#define MI_LA64_PWCL_DIR2WIDTH_SHIFT      25
#define MI_LA64_PWCL_PTEWIDTH_SHIFT       30
#define MI_LA64_PWCH_DIR3BASE_SHIFT       0
#define MI_LA64_PWCH_DIR3WIDTH_SHIFT      6
#define MI_LA64_PWCH_DIR4BASE_SHIFT       12
#define MI_LA64_PWCH_DIR4WIDTH_SHIFT      18

#define MI_LA64_PWCL_4LEVEL_4K \
    ((12ULL << MI_LA64_PWCL_PTBASE_SHIFT) | \
     (9ULL  << MI_LA64_PWCL_PTWIDTH_SHIFT) | \
     (21ULL << MI_LA64_PWCL_DIR1BASE_SHIFT) | \
     (9ULL  << MI_LA64_PWCL_DIR1WIDTH_SHIFT) | \
     (30ULL << MI_LA64_PWCL_DIR2BASE_SHIFT) | \
     (9ULL  << MI_LA64_PWCL_DIR2WIDTH_SHIFT) | \
     (0ULL  << MI_LA64_PWCL_PTEWIDTH_SHIFT))

#define MI_LA64_PWCH_4LEVEL_4K \
    ((39ULL << MI_LA64_PWCH_DIR3BASE_SHIFT) | \
     (9ULL  << MI_LA64_PWCH_DIR3WIDTH_SHIFT) | \
     (0ULL  << MI_LA64_PWCH_DIR4BASE_SHIFT) | \
     (0ULL  << MI_LA64_PWCH_DIR4WIDTH_SHIFT))

#define MI_LA64_DMW_PLV0                  0x0000000000000001ULL
#define MI_LA64_DMW_PLV3                  0x0000000000000008ULL
#define MI_LA64_DMW_MAT_SHIFT             4
#define MI_LA64_DMW_VSEG_SHIFT            60
#define MI_LA64_DMW0_VSEG                 0x8ULL
#define MI_LA64_DMW1_VSEG                 0x9ULL
#define MI_LA64_DMW2_VSEG                 0xAULL
#define MI_LA64_UNCACHED_BASE             (MI_LA64_DMW0_VSEG << MI_LA64_DMW_VSEG_SHIFT)
#define MI_LA64_CACHED_BASE               (MI_LA64_DMW1_VSEG << MI_LA64_DMW_VSEG_SHIFT)
#define MI_LA64_WRITECOMBINE_BASE         (MI_LA64_DMW2_VSEG << MI_LA64_DMW_VSEG_SHIFT)
#define MI_LA64_DIRECT_MAP_BASE           MI_LA64_CACHED_BASE

typedef struct _HARDWARE_PTE
{
    ULONG64 Valid : 1;
    ULONG64 Dirty : 1;
    ULONG64 PrivilegeLevel : 2;
    ULONG64 CacheType : 2;
    ULONG64 Global : 1;
    ULONG64 Present : 1;
    ULONG64 Write : 1;
    ULONG64 Owner : 1;
    ULONG64 Accessed : 1;
    ULONG64 CopyOnWrite : 1;
    ULONG64 PageFrameNumber : 36;
    ULONG64 SoftwareReserved : 13;
    ULONG64 NotReadable : 1;
    ULONG64 NoExecute : 1;
    ULONG64 RestrictedPrivilege : 1;
} HARDWARE_PTE, *PHARDWARE_PTE;

typedef struct _MMPTE_SOFTWARE
{
    ULONG64 Valid : 1;
    ULONG64 PageFileLow : 4;
    ULONG64 Protection : 5;
    ULONG64 Prototype : 1;
    ULONG64 Transition : 1;
    ULONG64 UsedPageTableEntries : 10;
    ULONG64 Reserved : 10;
    ULONG64 PageFileHigh : 32;
} MMPTE_SOFTWARE;

typedef struct _MMPTE_TRANSITION
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 Reserved0 : 2;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 2;
    ULONG64 PageFrameNumber : 36;
    ULONG64 Reserved2 : 16;
} MMPTE_TRANSITION;

typedef struct _MMPTE_PROTOTYPE
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 ReadOnly : 1;
    ULONG64 Reserved0 : 1;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 6;
    LONG64 ProtoAddress : 48;
} MMPTE_PROTOTYPE;

typedef struct _MMPTE_SUBSECTION
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 Reserved0 : 2;
    ULONG64 Transition : 1;
    ULONG64 Prototype : 1;
    ULONG64 Reserved1 : 6;
    LONG64 SubsectionAddress : 48;
} MMPTE_SUBSECTION;

typedef struct _MMPTE_LIST
{
    ULONG64 Valid : 1;
    ULONG64 Protection : 5;
    ULONG64 OneEntry : 1;
    ULONG64 Reserved0 : 3;
    ULONG64 Prototype : 1;
    ULONG64 Transition : 1;
    ULONG64 Reserved1 : 20;
    ULONG64 NextEntry : 32;
} MMPTE_LIST;

typedef struct _MMPTE
{
    union
    {
        ULONG_PTR Long;
        HARDWARE_PTE Hard;
        HARDWARE_PTE Flush;
        MMPTE_SOFTWARE Soft;
        MMPTE_TRANSITION Trans;
        MMPTE_PROTOTYPE Proto;
        MMPTE_SUBSECTION Subsect;
        MMPTE_LIST List;
    } u;
} MMPTE, *PMMPTE;

C_ASSERT(PAGE_SIZE == (1 << PAGE_SHIFT));
C_ASSERT(MM_ALLOCATION_GRANULARITY == (1 << MM_ALLOCATION_GRANULARITY_SHIFT));
C_ASSERT(sizeof(HARDWARE_PTE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_SOFTWARE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_TRANSITION) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_PROTOTYPE) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_SUBSECTION) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE_LIST) == sizeof(ULONG64));
C_ASSERT(sizeof(MMPTE) == sizeof(ULONG64));
C_ASSERT(__alignof(MMPTE) == sizeof(ULONG64));

#endif
