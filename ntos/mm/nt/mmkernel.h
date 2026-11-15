/*
 */

#pragma once

typedef struct _PHYSICAL_MEMORY_RUN
{
    PFN_NUMBER BasePage;
    PFN_NUMBER PageCount;
} PHYSICAL_MEMORY_RUN, *PPHYSICAL_MEMORY_RUN;

typedef struct _PHYSICAL_MEMORY_DESCRIPTOR
{
    ULONG NumberOfRuns;
    PFN_NUMBER NumberOfPages;
    PHYSICAL_MEMORY_RUN Run[1];
} PHYSICAL_MEMORY_DESCRIPTOR, *PPHYSICAL_MEMORY_DESCRIPTOR;

extern PPHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock;

extern PVOID MmNonPagedPoolStart;
extern PVOID MmNonPagedPoolExpansionStart;
extern PVOID MmNonPagedPoolEnd;
extern PVOID MmPagedPoolStart;
extern PVOID MmPagedPoolEnd;
extern PVOID MmSessionBase;
extern SIZE_T MmSessionSize;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern SIZE_T MmMaximumNonPagedPoolInBytes;
extern SIZE_T MmSizeOfPagedPoolInBytes;
extern SIZE_T MmAllocationFragment;
extern ULONG MmNumberOfSystemPtes;
extern ULONG MmSecondaryColors;
extern PFN_NUMBER MmLowMemoryThreshold;
extern PFN_NUMBER MmHighMemoryThreshold;

