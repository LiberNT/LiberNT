/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory manager interfaces the rest of the kernel uses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

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

extern SIZE_T MmtotalCommitLimitMaximum;

extern KSPIN_LOCK MmPfnLock;

struct _KTRAP_FRAME;

struct _EPROCESS;

typedef ULONG_PTR SWAPENTRY;

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

typedef struct _MMPFN MMPFN, *PMMPFN;

extern PMMPFN MmPfnDatabase;

#define MM_HIBERNATE_SKIP 0

#define MM_HIBERNATE_ZERO 1

#define MM_HIBERNATE_SAVE 2

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

#ifdef _WIN64
NTSTATUS
NTAPI
MmCreatePeb32(
    IN PEPROCESS Process,
    IN PINITIAL_PEB InitialPeb,
    IN PSECTION_IMAGE_INFORMATION ImageInformation,
    OUT struct _PEB32 **BasePeb
);
#endif

#ifdef _M_ARM64
BOOLEAN
MiIsEcCodeAddress(
    _In_ PEPROCESS Process,
    _In_ PVOID Address);
#endif

#ifdef _M_ARM64
VOID
NTAPI
MmSetPageProtectForWriteFault(
    struct _EPROCESS* Process,
    PVOID Address,
    ULONG flProtect
);
#endif

typedef struct _MM_PREPARED_SYSTEM_IMAGE
{
    HANDLE FileHandle;
    PVOID Section;
} MM_PREPARED_SYSTEM_IMAGE, *PMM_PREPARED_SYSTEM_IMAGE;

typedef struct _MM_PAGE_LIST_COUNTS
{
    PFN_NUMBER Zeroed;
    PFN_NUMBER Free;
    PFN_NUMBER Standby;
    PFN_NUMBER Modified;
    PFN_NUMBER Bad;
} MM_PAGE_LIST_COUNTS, *PMM_PAGE_LIST_COUNTS;

_Requires_exclusive_lock_held_(WorkingSet->WorkingSetMutex)
VOID
NTAPI
MiInitializeWorkingSetList(_Inout_ PMMSUPPORT WorkingSet);

CODE_SEG("INIT")
VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID);

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK* LoaderBlock);

NTSTATUS
MiValidateImageSigningPolicy(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PLONG SigningState);

NTSTATUS
NTAPI
MmGetExecuteOptions(
    _In_ PULONG ExecuteOptions);

NTSTATUS
NTAPI
MmSetExecuteOptions(
    _In_ ULONG ExecuteOptions);

NTSTATUS
MmSetProcessExecutableWriteTracking(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

NTSTATUS
NTAPI
MmAccessFaultEx(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation,
    _In_ BOOLEAN AddressSpaceLocked);

NTSTATUS
NTAPI
MmDbgCopyMemory(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
MmDbgCopyProcessMemory(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
MmCallDllInitialize(
    _In_ PKLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PLIST_ENTRY ModuleListHead);

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmCallBootDllInitialize(
    _In_ PLIST_ENTRY BootDriverListHead);

PVOID
NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR ExportName);

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle);

VOID
NTAPI
MmFreePreparedSystemImage(
    _Inout_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage);

NTSTATUS
NTAPI
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_opt_ PUNICODE_STRING NamePrefix,
    _In_opt_ PUNICODE_STRING LoadedName,
    _In_ ULONG Flags,
    _Out_ PVOID *ModuleObject,
    _Out_ PVOID *ImageBaseAddress);

NTSTATUS
NTAPI
MmLoadSystemImageEx(
    _In_ PUNICODE_STRING FileName,
    _In_opt_ PUNICODE_STRING NamePrefix,
    _In_opt_ PUNICODE_STRING LoadedName,
    _In_ ULONG Flags,
    _Inout_opt_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage,
    _Out_ PVOID *ModuleObject,
    _Out_ PVOID *ImageBaseAddress);

NTSTATUS
NTAPI
MmPrepareSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ ULONG Flags,
    _Out_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage);

NTSTATUS
NTAPI
MmUnloadSystemImage(
    _In_ PVOID ImageHandle);

BOOLEAN
NTAPI
MmVerifyImageIsOkForMpUse(
    _In_ PVOID BaseAddress);

CODE_SEG("INIT")
BOOLEAN
NTAPI
MmArmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
MmDumpArmPfnDatabase(
    _In_ BOOLEAN StatusOnly);

VOID
NTAPI
MmFreeLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
BOOLEAN
NTAPI
MmInitSystem(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

ULONG64
MmGetPhysicalAddressLimit(VOID);

CODE_SEG("INIT")
BOOLEAN
NTAPI
MiInitializeLoadedModuleList(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
NTAPI
MmIsSystemImageImportingModule(
    _In_ PVOID ImageBase,
    _In_ PCUNICODE_STRING ModuleName);

PFN_NUMBER
NTAPI
MmBuildDumpPageBitmap(
    _Inout_ PRTL_BITMAP Bitmap,
    _In_ BOOLEAN IncludeUserPages,
    _In_ PFN_NUMBER MaximumPages,
    _Out_opt_ PPFN_NUMBER CandidatePages);

VOID
NTAPI
MmCompleteHibernation(VOID);

PFN_NUMBER
NTAPI
MmHibernationFrameLimit(VOID);

BOOLEAN
NTAPI
MmIsFileObjectAPagingFile(
    PFILE_OBJECT FileObject);

PVOID
NTAPI
MmMapHibernationFrame(
    _In_ PFN_NUMBER Frame);

VOID
NTAPI
MmPrepareHibernation(VOID);

UCHAR
NTAPI
MmQueryHibernationFrame(
    _In_ PFN_NUMBER Frame);

BOOLEAN
NTAPI
MmQueryPagingFileUsage(
    _Out_ PULONG64 TotalPages,
    _Out_ PULONG64 PagesInUse,
    _Out_ PULONG64 PeakPagesInUse,
    _Out_ PUNICODE_STRING Name);

VOID
MmShutdownSystem(
    _In_ ULONG Phase);

PVOID
NTAPI
MmAllocateSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG SpecialType);

VOID
NTAPI
MmFreeSpecialPool(
    _In_ PVOID P);

BOOLEAN
NTAPI
MmIsSpecialPoolAddress(
    _In_ PVOID P);

BOOLEAN
NTAPI
MmIsSpecialPoolAddressFree(
    _In_ PVOID P);

BOOLEAN
NTAPI
MmRaisePoolQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T CurrentMaxQuota,
    _Out_ PSIZE_T NewMaxQuota);

VOID
NTAPI
MmReturnPoolQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T QuotaToReturn);

BOOLEAN
NTAPI
MmUseSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag);

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay);

VOID
NTAPI
MmCleanProcessAddressSpace(
    _In_ PEPROCESS Process);

PVOID
NTAPI
MmCreateKernelStack(
    BOOLEAN GuiStack,
    UCHAR Node);

NTSTATUS
NTAPI
MmCreatePeb(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _Out_ PPEB *BasePeb);

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Dest,
    _In_ PULONG_PTR DirectoryTableBase);

NTSTATUS
NTAPI
MmCreateTeb(
    _In_ PEPROCESS Process,
    _In_ PCLIENT_ID ClientId,
    _In_ PINITIAL_TEB InitialTeb,
    _In_opt_ PINITIAL_TEB Wow64InitialTeb,
    _Out_ PTEB* BaseTeb);

VOID
NTAPI
MmDeleteKernelStack(
    PVOID Stack,
    BOOLEAN GuiStack);

VOID
NTAPI
MmDeleteProcessAddressSpace(
    _In_ PEPROCESS Process);

VOID
NTAPI
MmDeleteTeb(
    struct _EPROCESS *Process,
    PTEB Teb);

NTSTATUS
NTAPI
MmGrowKernelStack(
    _In_ PVOID StackPointer);

NTSTATUS
NTAPI
MmGrowKernelStackEx(
    _In_ PVOID StackPointer,
    _In_ ULONG GrowSize);

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase);

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmInitializeHandBuiltProcess2(
    _In_ PEPROCESS Process);

NTSTATUS
MmCreateProcessCfgBitmap(
    _In_ PEPROCESS Process,
    _In_ PVOID Section,
    _In_ ULONGLONG MitigationOptions);

#ifdef _M_AMD64
BOOLEAN MmBindSharedCfgPage(PKTRAP_FRAME TrapFrame);
NTSTATUS
MmGetSystemDllCfgInformation(PEPROCESS Process, PVOID ImageBase, PULONG64 Page,
                             PULONG64 Bitmap, PULONG64 Size, PBOOLEAN ExportSuppression);
#endif

NTSTATUS
NTAPI
MmInitializeProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS Clone,
    _In_opt_ PVOID Section,
    _Inout_ PULONG Flags,
    _In_opt_ POBJECT_NAME_INFORMATION *AuditName);

SIZE_T
NTAPI
MmQueryProcessCommitCharge(
    _In_ PEPROCESS Process);

NTSTATUS
NTAPI
MmSetMemoryPriorityProcess(
    _In_ PEPROCESS Process,
    _In_ UCHAR MemoryPriority);

CODE_SEG("INIT")
NTSTATUS
NTAPI
MmCreatePhysicalMemorySection(VOID);

NTSTATUS
NTAPI
MmCreateSectionForDataScan(
    _Out_ PVOID *SectionObject,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Inout_ PLARGE_INTEGER MaximumSize,
    _In_ ULONG SectionPageProtection,
    _In_ ULONG AllocationAttributes,
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NTAPI
MmGetFileNameForAddress(
    _In_ PVOID Address,
    _Out_ PUNICODE_STRING ModuleName);

NTSTATUS
NTAPI
MmGetFileNameForSection(
    _In_ PVOID Section,
    _Out_ POBJECT_NAME_INFORMATION *ModuleName);

PFILE_OBJECT
NTAPI
MmGetFileObjectForSection(
    _In_ PVOID Section);

PVOID
NTAPI
MmGetImageBaseForSection(
    _In_ PVOID Section);

VOID
NTAPI
MmGetImageInformation(
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation);

NTSTATUS
NTAPI
MmGetSectionImageInformation(
    _In_ PSECTION SectionObject,
    _Out_ PSECTION_IMAGE_INFORMATION ImageInformation);

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

PVOID
NTAPI
MmGetSessionById(
    _In_ ULONG SessionId);

ULONG
NTAPI
MmGetSessionId(
    _In_ PEPROCESS Process);

ULONG
NTAPI
MmGetSessionIdEx(
    _In_ PEPROCESS Process);

ULONG
NTAPI
MmGetSessionLocaleId(VOID);

BOOLEAN
NTAPI
MmIsSessionAddress(
    _In_ PVOID Address);

NTSTATUS
NTAPI
MmMoveProcessToSession(
    _Inout_ PEPROCESS Process,
    _In_ ULONG SessionId);

PVOID
NTAPI
MmGetNextSession(
    _Inout_opt_ PVOID SessionEntry);

BOOLEAN
NTAPI
MmIsSessionImageAddress(
    _In_ PVOID Address);

VOID
NTAPI
MmQuitNextSession(
    _Inout_ PVOID SessionEntry);

NTSTATUS
NTAPI
MmSessionCreate(
    _Out_ PULONG SessionId);

NTSTATUS
NTAPI
MmSessionDelete(
    _In_ ULONG SessionId);

NTSTATUS
NTAPI
MmSetProcessSession(
    _In_ PEPROCESS Process,
    _In_ ULONG SessionId);

_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
MmSetSessionLocaleId(
    _In_ LCID LocaleId);

NTSTATUS
NTAPI
MmCopyVirtualMemory(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize);

VOID
NTAPI
MmQueryPageListCounts(
    _Out_ PMM_PAGE_LIST_COUNTS Counts);

SIZE_T
NTAPI
MmQuerySystemCommitCharge(
    _Out_opt_ PSIZE_T PeakCommitment);

VOID
NTAPI
MmZeroPageThread(VOID);

VOID
NTAPI
ExpCheckPoolAllocation(
    PVOID P,
    POOL_TYPE PoolType,
    ULONG Tag);

VOID
NTAPI
ExReturnPoolQuota(
    _In_ PVOID P);

BOOLEAN
MiArchCreateProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase);

NTSTATUS
NTAPI
MmMapViewInSystemSpaceEx(
    _In_ PVOID Section,
    _Outptr_result_bytebuffer_ (*ViewSize) PVOID *MappedBase,
    _Inout_ PSIZE_T ViewSize,
    _Inout_ PLARGE_INTEGER SectionOffset,
    _In_ ULONG_PTR Flags);

#include <internal/arch/mm.h>
