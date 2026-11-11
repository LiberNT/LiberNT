/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the system has been doing: the counters it keeps as it runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/*
 * How much paged pool the registry may take and how much it has taken. The
 * configuration manager does not publish these, so the figure a caller sets is
 * the figure it is given back.
 */
ULONG ExpRegistryQuotaAllowed = 0;
ULONG ExpRegistryQuotaUsed = 0;

/* PRIVATE DATA ***************************************************************/

#define EXP_SYSPERF_TAG 'freP'

FIRMWARE_TYPE NTAPI ExGetFirmwareType(VOID);

VOID NTAPI ExQueryPoolUsage(_Out_ PULONG PagedPoolPages,
    _Out_ PULONG NonPagedPoolPages, _Out_ PULONG PagedPoolAllocs,
    _Out_ PULONG PagedPoolFrees, _Out_ PULONG PagedPoolLookasideHits,
    _Out_ PULONG NonPagedPoolAllocs, _Out_ PULONG NonPagedPoolFrees,
    _Out_ PULONG NonPagedPoolLookasideHits);

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Most counters are kept on the processor that moved them, so none of them has
 * to be written under a lock. A figure for the whole machine is the sum over
 * every processor, which is what this walks.
 */
static
VOID
ExpSumProcessorCounters(
    _Out_ PSYSTEM_PERFORMANCE_INFORMATION Info)
{
    ULONG Index;

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        Info->IdleProcessTime.QuadPart +=
            (LONGLONG)Prcb->IdleThread->KernelTime * KeMaximumIncrement;

        Info->IoReadTransferCount.QuadPart +=
            Prcb->IoReadTransferCount.QuadPart;
        Info->IoWriteTransferCount.QuadPart +=
            Prcb->IoWriteTransferCount.QuadPart;
        Info->IoOtherTransferCount.QuadPart +=
            Prcb->IoOtherTransferCount.QuadPart;

        Info->IoReadOperationCount += Prcb->IoReadOperationCount;
        Info->IoWriteOperationCount += Prcb->IoWriteOperationCount;
        Info->IoOtherOperationCount += Prcb->IoOtherOperationCount;

        Info->PageFaultCount += Prcb->MmPageFaultCount;
        Info->CopyOnWriteCount += Prcb->MmCopyOnWriteCount;
        Info->TransitionCount += Prcb->MmTransitionCount;
        Info->CacheTransitionCount += Prcb->MmCacheTransitionCount;
        Info->DemandZeroCount += Prcb->MmDemandZeroCount;
        Info->PageReadCount += Prcb->MmPageReadCount;
        Info->PageReadIoCount += Prcb->MmPageReadIoCount;
        Info->CacheReadCount += Prcb->MmCacheReadCount;
        Info->CacheIoCount += Prcb->MmCacheIoCount;
        Info->DirtyPagesWriteCount += Prcb->MmDirtyPagesWriteCount;
        Info->DirtyWriteIoCount += Prcb->MmDirtyWriteIoCount;
        Info->MappedPagesWriteCount += Prcb->MmMappedPagesWriteCount;
        Info->MappedWriteIoCount += Prcb->MmMappedWriteIoCount;

        Info->CcFastReadNoWait += Prcb->CcFastReadNoWait;
        Info->CcFastReadWait += Prcb->CcFastReadWait;
        Info->CcFastReadNotPossible += Prcb->CcFastReadNotPossible;
        Info->CcCopyReadNoWait += Prcb->CcCopyReadNoWait;
        Info->CcCopyReadWait += Prcb->CcCopyReadWait;
        Info->CcCopyReadNoWaitMiss += Prcb->CcCopyReadNoWaitMiss;

        Info->ContextSwitches += Prcb->KeContextSwitches;
        Info->SystemCalls += Prcb->KeSystemCalls;
    }
}

static
ULONG
ExpCountLookasideHits(
    _In_ PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Entry;
    ULONG Hits = 0;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PGENERAL_LOOKASIDE_POOL Lookaside =
            CONTAINING_RECORD(Entry, GENERAL_LOOKASIDE_POOL, ListEntry);

        if (Lookaside->TotalAllocates > Lookaside->AllocateMisses)
        {
            Hits += Lookaside->TotalAllocates - Lookaside->AllocateMisses;
        }
    }

    return Hits;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ExpQueryPerformanceInformation(
    _Out_ PSYSTEM_PERFORMANCE_INFORMATION Info,
    _In_ ULONG Length)
{
    ULONG DirtyPages;
    ULONG DirtyPageThreshold;
    ULONG PagedHeapHits;
    ULONG NonPagedHeapHits;

    if (Length < sizeof(SYSTEM_PERFORMANCE_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Info, sizeof(*Info));

    ExpSumProcessorCounters(Info);

    Info->AvailablePages = (ULONG)MmAvailablePages;
    Info->CommittedPages = (ULONG)MmTotalCommittedPages;
    Info->CommitLimit = (ULONG)MmTotalCommitLimit;
    Info->PeakCommitment = (ULONG)MmPeakCommitment;
    Info->ResidentAvailablePages = (LONGLONG)MmResidentAvailablePages;
    Info->SharedCommittedPages = MmSharedCommit;

    ExQueryPoolUsage(&Info->PagedPoolPages,
                     &Info->NonPagedPoolPages,
                     &Info->PagedPoolAllocs,
                     &Info->PagedPoolFrees,
                     &PagedHeapHits,
                     &Info->NonPagedPoolAllocs,
                     &Info->NonPagedPoolFrees,
                     &NonPagedHeapHits);

    Info->NonPagedPoolLookasideHits =
        ExpCountLookasideHits(&ExpNonPagedLookasideListHead);
    Info->PagedPoolLookasideHits =
        ExpCountLookasideHits(&ExpPagedLookasideListHead);

    CcNtQueryDirtyPages(&DirtyPages, &DirtyPageThreshold);

    Info->CcTotalDirtyPages = DirtyPages;
    Info->CcDirtyPageThreshold = DirtyPageThreshold;

    /* TODO: the free system page-table entries and the resident code and
       driver page counts are not kept. */

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryInterruptInformation(
    _Out_ PSYSTEM_INTERRUPT_INFORMATION Info,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    ULONG Needed;
    ULONG Index;

    Needed = KeNumberProcessors *
             (ULONG)sizeof(SYSTEM_INTERRUPT_INFORMATION);

    *ReturnLength = Needed;

    if (Length < Needed)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        RtlZeroMemory(&Info[Index], sizeof(Info[Index]));

        Info[Index].ContextSwitches = Prcb->KeContextSwitches;
        Info[Index].DpcCount = Prcb->DpcLastCount;
        Info[Index].DpcRate = Prcb->DpcRequestRate;
        Info[Index].TimeIncrement = KeMaximumIncrement;
        Info[Index].DpcBypassCount = 0;
        Info[Index].ApcBypassCount = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryExceptionInformation(
    _Out_ PSYSTEM_EXCEPTION_INFORMATION Info,
    _In_ ULONG Length)
{
    ULONG Index;

    if (Length < sizeof(SYSTEM_EXCEPTION_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Info, sizeof(*Info));

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        Info->AlignmentFixupCount += Prcb->KeAlignmentFixupCount;
        Info->ExceptionDispatchCount += Prcb->KeExceptionDispatchCount;
    }

    /* The emulation counts belong to processors that need an emulator. */
    Info->FloatingEmulationCount = 0;
    Info->ByteWordEmulationCount = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryContextSwitchInformation(
    _Out_ PSYSTEM_CONTEXT_SWITCH_INFORMATION Info,
    _In_ ULONG Length)
{
    ULONG Index;

    if (Length < sizeof(SYSTEM_CONTEXT_SWITCH_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Info, sizeof(*Info));

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        Info->ContextSwitches += KiProcessorBlock[Index]->KeContextSwitches;
    }

    /* TODO: the scheduler does not count how it chose a processor, so the
       breakdown of those choices is reported as none. */

    return STATUS_SUCCESS;
}

/*
 * The time each processor has spent doing nothing, which is what a caller
 * watching how busy the machine is subtracts from the whole.
 */
NTSTATUS
NTAPI
ExpQueryProcessorIdleInformation(
    _Out_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PULONG64 Times = Buffer;
    ULONG Needed;
    ULONG Index;

    Needed = KeNumberProcessors * (ULONG)sizeof(ULONG64);

    *ReturnLength = Needed;

    if (Length < Needed)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        Times[Index] = (ULONG64)Prcb->IdleThread->KernelTime *
                       KeMaximumIncrement;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryProcessorCycleTimeInformation(
    _Out_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ BOOLEAN IdleOnly)
{
    PULONGLONG Times = Buffer;
    ULONG Needed;
    ULONG Index;

    Needed = KeNumberProcessors * (ULONG)sizeof(ULONGLONG);

    *ReturnLength = Needed;

    if (Length < Needed)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        Times[Index] = IdleOnly ? Prcb->IdleThread->CycleTime :
                                  Prcb->CycleTime;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryTimeAdjustment(
    _Out_ PSYSTEM_QUERY_TIME_ADJUST_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_QUERY_TIME_ADJUST_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Info->TimeAdjustment = KeTimeAdjustment;
    Info->TimeIncrement = KeMaximumIncrement;
    Info->Enable = (BOOLEAN)(KeTimeAdjustment == KeMaximumIncrement);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryDpcBehaviour(
    _Out_ PSYSTEM_DPC_BEHAVIOR_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_DPC_BEHAVIOR_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Info->Spare = 0;
    Info->DpcQueueDepth = KiMaximumDpcQueueDepth;
    Info->MinimumDpcRate = KiMinimumDpcRate;
    Info->AdjustDpcThreshold = KiAdjustDpcThreshold;
    Info->IdealDpcRate = KiIdealDpcRate;

    return STATUS_SUCCESS;
}

/*
 * How much of the paging files is in use. A machine with no paging file has
 * nothing to report, which is not an error.
 */
NTSTATUS
NTAPI
ExpQueryPageFileInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PSYSTEM_PAGEFILE_INFORMATION Info;
    UNICODE_STRING Name;
    ULONG64 TotalPages;
    ULONG64 PagesInUse;
    ULONG64 PeakPagesInUse;
    ULONG Used = 0;
    ULONG Needed;

    PAGED_CODE();

    *ReturnLength = 0;

    if (!MmQueryPagingFileUsage(&TotalPages,
                                &PagesInUse,
                                &PeakPagesInUse,
                                &Name))
    {
        return STATUS_SUCCESS;
    }

    Needed = (ULONG)sizeof(SYSTEM_PAGEFILE_INFORMATION) +
             Name.Length + (ULONG)sizeof(WCHAR);
    Needed = ALIGN_UP_BY(Needed, sizeof(ULONG_PTR));

    if ((Buffer == NULL) || (Needed > Length))
    {
        *ReturnLength = Needed;
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Info = Buffer;

    Info->NextEntryOffset = 0;
    Info->TotalSize = (ULONG)TotalPages;
    Info->TotalInUse = (ULONG)PagesInUse;
    Info->PeakUsage = (ULONG)PeakPagesInUse;

    Info->PageFileName.Length = Name.Length;
    Info->PageFileName.MaximumLength = (USHORT)(Name.Length + sizeof(WCHAR));
    Info->PageFileName.Buffer =
        (PWCHAR)((PUCHAR)Buffer + sizeof(SYSTEM_PAGEFILE_INFORMATION));

    RtlCopyMemory(Info->PageFileName.Buffer, Name.Buffer, Name.Length);
    Info->PageFileName.Buffer[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Used = Needed;

    *ReturnLength = Used;

    return STATUS_SUCCESS;
}

/*
 * How the physical pages of the machine are divided between the lists the
 * memory manager keeps them on.
 */
NTSTATUS
NTAPI
ExpQueryMemoryListInformation(
    _Out_ PSYSTEM_MEMORY_LIST_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_MEMORY_LIST_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Info, sizeof(*Info));

    Info->ZeroPageCount = MmZeroedPageListHead.Total;
    Info->FreePageCount = MmFreePageListHead.Total;
    Info->ModifiedPageCount = MmModifiedPageListHead.Total;
    Info->ModifiedNoWritePageCount = MmModifiedNoWritePageListHead.Total;
    Info->BadPageCount = 0;
    Info->ModifiedPageCountPageFile = MmModifiedPageListHead.Total;

    /* The standby pages are reported against the one priority that is kept. */
    Info->PageCountByPriority[0] = MmStandbyPageListHead.Total;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryFileCacheInformation(
    _Out_ PSYSTEM_FILECACHE_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_FILECACHE_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    ULONG DirtyPages;
    ULONG Threshold;

    RtlZeroMemory(Info, sizeof(*Info));

    CcNtQueryDirtyPages(&DirtyPages, &Threshold);

    Info->CurrentSize = (SIZE_T)DirtyPages << PAGE_SHIFT;
    Info->PeakSize = Info->CurrentSize;
    Info->MinimumWorkingSet = 0;
    Info->MaximumWorkingSet = (SIZE_T)Threshold << PAGE_SHIFT;
    Info->CurrentSizeIncludingTransitionInPages = Info->CurrentSize;
    Info->PeakSizeIncludingTransitionInPages = Info->CurrentSize;

    /* TODO: the cache manager does not keep a fault count of its own. */

    return STATUS_SUCCESS;
}

static
BOOLEAN
ExpIsCoreMaster(
    _In_ ULONG Index)
{
    PKPRCB Prcb = KiProcessorBlock[Index];

    return (Prcb != NULL) &&
           ((KeActiveProcessors & Prcb->SetMember) != 0) &&
           (Prcb->MultiThreadSetMaster == Prcb);
}

static
PSYSTEM_LOGICAL_PROCESSOR_INFORMATION
ExpNextLogicalProcessorRecord(
    _In_ PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Records,
    _Inout_ PULONG Written,
    _In_ ULONG Count,
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _In_ KAFFINITY Mask)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Record;

    if (*Written >= Count)
    {
        return NULL;
    }

    Record = &Records[(*Written)++];
    RtlZeroMemory(Record, sizeof(*Record));
    Record->ProcessorMask = (ULONG_PTR)Mask;
    Record->Relationship = Relationship;

    return Record;
}

/*
 * How the processors of the machine relate to each other: which of them share
 * a core, a cache or a package, and which node each belongs to. The caches and
 * the packages are the ones the architecture's own code found on the processors.
 */
NTSTATUS
NTAPI
ExpQueryLogicalProcessorInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Record;
    PKI_CACHE_RECORD Caches;
    PKAFFINITY Packages;
    GROUP_AFFINITY Affinity;
    USHORT HighestNode = KeQueryHighestNodeNumber();
    ULONG CacheCount;
    ULONG PackageCount;
    ULONG Count = 0;
    ULONG Written = 0;
    ULONG Needed;
    ULONG Index;
    USHORT Node;

    PAGED_CODE();

    Caches = ExAllocatePoolWithTag(PagedPool,
                                   KI_MAX_CACHE_RECORDS * sizeof(KI_CACHE_RECORD) +
                                   MAXIMUM_PROCESSORS * sizeof(KAFFINITY),
                                   EXP_SYSPERF_TAG);
    if (Caches == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Packages = (PKAFFINITY)(Caches + KI_MAX_CACHE_RECORDS);

    KiQueryProcessorTopology(Caches,
                             KI_MAX_CACHE_RECORDS,
                             &CacheCount,
                             Packages,
                             MAXIMUM_PROCESSORS,
                             &PackageCount);

    if (PackageCount == 0)
    {
        Packages[0] = KeActiveProcessors;
        PackageCount = 1;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        if (ExpIsCoreMaster(Index)) Count++;
    }

    for (Node = 0; Node <= HighestNode; Node++)
    {
        KeQueryNodeActiveAffinity(Node, &Affinity, NULL);
        if (Affinity.Mask != 0) Count++;
    }

    Count += CacheCount + PackageCount;
    Needed = Count * (ULONG)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    *ReturnLength = Needed;

    if ((Buffer == NULL) || (Length < Needed))
    {
        ExFreePoolWithTag(Caches, EXP_SYSPERF_TAG);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    _SEH2_TRY
    {
        for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
        {
            PKPRCB Prcb = KiProcessorBlock[Index];

            if (!ExpIsCoreMaster(Index)) continue;

            Record = ExpNextLogicalProcessorRecord(Buffer,
                                                   &Written,
                                                   Count,
                                                   RelationProcessorCore,
                                                   (KAFFINITY)Prcb->MultiThreadProcessorSet);
            if (Record == NULL) break;

            Record->ProcessorCore.Flags =
                (Prcb->MultiThreadProcessorSet != Prcb->SetMember) ? LTP_PC_SMT : 0;
        }

        for (Index = 0; Index < CacheCount; Index++)
        {
            Record = ExpNextLogicalProcessorRecord(Buffer,
                                                   &Written,
                                                   Count,
                                                   RelationCache,
                                                   Caches[Index].ProcessorSet);
            if (Record == NULL) break;

            Record->Cache = Caches[Index].Descriptor;
        }

        for (Index = 0; Index < PackageCount; Index++)
        {
            if (ExpNextLogicalProcessorRecord(Buffer,
                                              &Written,
                                              Count,
                                              RelationProcessorPackage,
                                              Packages[Index]) == NULL) break;
        }

        for (Node = 0; Node <= HighestNode; Node++)
        {
            KeQueryNodeActiveAffinity(Node, &Affinity, NULL);
            if (Affinity.Mask == 0) continue;

            Record = ExpNextLogicalProcessorRecord(Buffer,
                                                   &Written,
                                                   Count,
                                                   RelationNumaNode,
                                                   Affinity.Mask);
            if (Record == NULL) break;

            Record->NumaNode.NodeNumber = Node;
        }
    }
    _SEH2_FINALLY
    {
        ExFreePoolWithTag(Caches, EXP_SYSPERF_TAG);
    }
    _SEH2_END;

    *ReturnLength = Written * (ULONG)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryBootEnvironmentInformation(
    _Out_ PSYSTEM_BOOT_ENVIRONMENT_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_BOOT_ENVIRONMENT_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlZeroMemory(Info, sizeof(*Info));

    if ((KeLoaderBlock != NULL) && (KeLoaderBlock->Extension != NULL))
    {
        Info->BootIdentifier =
            KeLoaderBlock->Extension->BootIdentifier;
    }

    Info->FirmwareType = ExGetFirmwareType();
    Info->BootFlags = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpQueryRegistryQuota(
    _Out_ PSYSTEM_REGISTRY_QUOTA_INFORMATION Info,
    _In_ ULONG Length)
{
    if (Length < sizeof(SYSTEM_REGISTRY_QUOTA_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Info->RegistryQuotaAllowed = ExpRegistryQuotaAllowed;
    Info->RegistryQuotaUsed = ExpRegistryQuotaUsed;
    Info->PagedPoolSize = (SIZE_T)MmPagedPoolCommit << PAGE_SHIFT;

    return STATUS_SUCCESS;
}

/* EOF */
