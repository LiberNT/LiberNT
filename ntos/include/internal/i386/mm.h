#pragma once

#ifdef _X86PAE_
#define _MI_PAGING_LEVELS 3
#else
#define _MI_PAGING_LEVELS 2
#endif

#define MI_USER_PROBE_ADDRESS                   (PVOID)0x7FFF0000
#define MI_SYSTEM_CACHE_WS_START                (PVOID)0xC0C00000
#define MI_SYSTEM_CACHE_START                   (PVOID)0xC1000000
#define MI_PAGED_POOL_START                     (PVOID)0xE1000000
#define MI_NONPAGED_POOL_END                    (PVOID)0xFFBE0000
#define MI_DEBUG_MAPPING                        (PVOID)0xFFBFF000

#define MM_HIGHEST_VAD_ADDRESS \
    (PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE))

#define MI_MAX_ZERO_BITS                        21

#define MI_IS_WRITE_ACCESS(FaultCode)        BooleanFlagOn(FaultCode, 0x00000002)

#define MI_IS_INSTRUCTION_FETCH(FaultCode)   BooleanFlagOn(FaultCode, 0x00000010)

#ifndef _X86PAE_
#define PD_COUNT 1
#else
#define PD_COUNT (1 << 2)
#endif

C_ASSERT(PD_COUNT == 1);

#define PTE_PER_PAGE (PAGE_SIZE / sizeof(MMPTE))

#define PDE_PER_PAGE (PAGE_SIZE / sizeof(MMPDE))

#define PPE_PER_PAGE 1

#define PTE_BASE (ULONG_PTR)0xC0000000

#define PDE_BASE (ULONG_PTR)MiPteToPde(PTE_BASE)

#define PDE_MAPPED_VA (PTE_PER_PAGE * PAGE_SIZE)

#define MiAddressToPte(Va) \
    ((PMMPTE)(PTE_BASE + ((((ULONG_PTR)(Va)) / PAGE_SIZE) * sizeof(MMPTE))))

#define MiAddressToPde(Va) \
    ((PMMPDE)(PDE_BASE + ((MiAddressToPdeOffset(Va)) * sizeof(MMPDE))))

#define MiAddressToPteOffset(Va) \
    ((((ULONG_PTR)(Va)) & (PDE_MAPPED_VA - 1)) / PAGE_SIZE)

#define MiAddressToPdeOffset(Va) (((ULONG_PTR)(Va)) / PDE_MAPPED_VA)

#define MiPteToPde(_Pte) ((PMMPDE)MiAddressToPte(_Pte))
