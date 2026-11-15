

struct _EPROCESS;

extern PFN_COUNT MiFreeSwapPages;
extern PFN_COUNT MiUsedSwapPages;
extern PFN_COUNT MmNumberOfPhysicalPages;
extern UCHAR MmDisablePagingExecutive;
extern PFN_NUMBER MmLowestPhysicalPage;
extern PFN_NUMBER MmHighestPhysicalPage;
extern PFN_NUMBER MmAvailablePages;
extern PFN_NUMBER MmResidentAvailablePages;
extern ULONG MmReadClusterSize;

extern LIST_ENTRY MmLoadedUserImageList;

extern KMUTANT MmSystemLoadLock;

extern ULONG MmNumberOfPagingFiles;

extern PVOID MmUnloadedDrivers;
extern PVOID MmLastUnloadedDrivers;
extern PVOID MmTriageActionTaken;
extern PVOID KernelVerifier;
extern MM_DRIVER_VERIFIER_DATA MmVerifierData;

extern SIZE_T MmTotalCommitLimit;
extern SIZE_T MmTotalCommitLimitMaximum;
extern SIZE_T MmTotalCommittedPages;
extern SIZE_T MmSharedCommit;
extern SIZE_T MmDriverCommit;
extern SIZE_T MmProcessCommit;
extern SIZE_T MmPagedPoolCommit;
extern SIZE_T MmPeakCommitment;


extern KSPIN_LOCK MmPfnLock;

struct _KTRAP_FRAME;
struct _EPROCESS;


#if (_MI_PAGING_LEVELS >= 4)
#define MI_PAGED_POOL_START 0
#define MI_NONPAGED_POOL_END 0
#define MI_SYSTEM_CACHE_START 0
#define MI_SYSTEM_CACHE_WS_START 0
#define MI_DEBUG_MAPPING 0

#define PDE_MAPPED_VA (PTE_PER_PAGE * (ULONG64)PAGE_SIZE)
#define PPE_MAPPED_VA (PDE_PER_PAGE * (ULONG64)PDE_MAPPED_VA)
#define PXE_MAPPED_VA (PPE_PER_PAGE * (ULONG64)PPE_MAPPED_VA)

FORCEINLINE
PVOID
MiPxiToAddress(ULONG Pxi)
{
    return (PVOID)((((LONG64)Pxi) << 55) >> 16);
}
#endif

#define MMDBG_COPY_WRITE            0x00000001
#define MMDBG_COPY_PHYSICAL         0x00000002
#define MMDBG_COPY_UNSAFE           0x00000004
#define MMDBG_COPY_CACHED           0x00000008
#define MMDBG_COPY_UNCACHED         0x00000010
#define MMDBG_COPY_WRITE_COMBINED   0x00000020

#define MMDBG_COPY_MAX_SIZE         0x8

#define MC_MAXIMUM                          (2)

#define MUST_SUCCEED_POOL_MASK              2
#define CACHE_ALIGNED_POOL_MASK             4
#define QUOTA_POOL_MASK                     8
#define SESSION_POOL_MASK                   32

#define MAX_PAGING_FILES                    (16)

#define PAGE_IS_EXECUTABLE                  \
    (PAGE_EXECUTE | \
    PAGE_EXECUTE_READ | \
    PAGE_EXECUTE_READWRITE | \
    PAGE_EXECUTE_WRITECOPY)


extern PMMPFN MmPfnDatabase;

#define MM_HIBERNATE_SKIP 0



typedef struct _MMPFNLIST
{
    PFN_NUMBER Total;
    MMLISTS ListName;
    PFN_NUMBER Flink;
    PFN_NUMBER Blink;
} MMPFNLIST, *PMMPFNLIST;

extern MMPFNLIST MmZeroedPageListHead;
extern MMPFNLIST MmFreePageListHead;
extern MMPFNLIST MmStandbyPageListHead;
extern MMPFNLIST MmModifiedPageListHead;
extern MMPFNLIST MmModifiedNoWritePageListHead;

typedef struct _MM_MEMORY_CONSUMER
{
    ULONG PagesUsed;
    ULONG PagesTarget;
    NTSTATUS (*Trim)(ULONG Target, ULONG Priority, PULONG NrFreed);
} MM_MEMORY_CONSUMER, *PMM_MEMORY_CONSUMER;

typedef struct _MM_PAGED_POOL_INFO
{
    PRTL_BITMAP PagedPoolAllocationMap;
    PRTL_BITMAP EndOfPagedPoolBitmap;
    PMMPTE FirstPteForPagedPool;
    PMMPTE LastPteForPagedPool;
    PMMPDE NextPdeForPagedPoolExpansion;
    ULONG PagedPoolHint;
    SIZE_T PagedPoolCommit;
    SIZE_T AllocatedPagedPool;
} MM_PAGED_POOL_INFO, *PMM_PAGED_POOL_INFO;

extern MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];

typedef VOID
(*PMM_ALTER_REGION_FUNC)(
    PMMSUPPORT AddressSpace,
    PVOID BaseAddress,
    SIZE_T Length,
    ULONG OldType,
    ULONG OldProtect,
    ULONG NewType,
    ULONG NewProtect
);

NTSTATUS
NTAPI
);

NTAPI
);

NTAPI

NTAPI

NTAPI

NTSTATUS

NTSTATUS
NTAPI

NTSTATUS
NTAPI


NTSTATUS
NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTAPI

NTSTATUS
NTAPI

NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

NTSTATUS
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

VOID
NTAPI

VOID
NTAPI

CODE_SEG("INIT")
NTAPI


CODE_SEG("INIT")
NTAPI

NTAPI

NTAPI

VOID
NTAPI

NTAPI

BOOLEAN
NTAPI
NTAPI

VOID
NTAPI

NTAPI

NTAPI



VOID
NTAPI

BOOLEAN
NTAPI

BOOLEAN
NTAPI

NTAPI

NTAPI


NTSTATUS
NTAPI

VOID
NTAPI

NTAPI

NTSTATUS
NTAPI

BOOLEAN
NTAPI

NTSTATUS
NTAPI

VOID
NTAPI

NTAPI

NTAPI

NTSTATUS
NTAPI
MmGrowKernelStack(

NTSTATUS
NTAPI
MmGrowKernelStackEx(
    _In_ PVOID StackPointer,




NTAPI

NTAPI



NTSTATUS
NTAPI

NTAPI

NTAPI


VOID
NTAPI


_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
MmAttachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState);

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmDetachSession(
    _Inout_ PVOID SessionEntry,
    _Out_ PKAPC_STATE ApcState);

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry);

NTAPI
    _In_ ULONG SessionId);

NTSTATUS
NTAPI
MmSetProcessSession(
    _Inout_ PEPROCESS Process,
    _In_ ULONG SessionId);

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId);


VOID

NTAPI

VOID
NTAPI


VOID
NTAPI



