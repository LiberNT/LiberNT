/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The handle table, which turns a handle into the entry behind it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntos/kdbg/kdb.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG ExpFreeListCount = 1;

/* PRIVATE DATA ***************************************************************/

#define EXP_HANDLE_TABLE_TAG 'btbO'
#define EXP_HANDLE_LEVEL_TAG 'LtbO'
#define EXP_HANDLE_INFO_TAG  'ItbO'

/* The free lists one table carries, one per processor up to this many */
#define EXP_HANDLE_MAX_FREE_LISTS 4

/* How long a contended entry is spun on before the caller gives up its turn */
#define EXP_HANDLE_CONTENTION_SPINS 64

/* The turn a caller gives up when the spin did not see the entry released */
#define EXP_HANDLE_CONTENTION_INTERVAL (-1)

/* The two low bits of TableCode say how many levels the table has */
#define EXP_TABLE_LEVEL_MASK ((ULONG_PTR)3)

#define ExpTableLevel(Code) ((ULONG)((Code) & EXP_TABLE_LEVEL_MASK))
#define ExpTableBase(Code)  ((PVOID)((Code) & ~EXP_TABLE_LEVEL_MASK))

/*
 * What a page of entries needs to say about itself, which no entry has room
 * for: the audit mask a caller may put on a handle, and the references the
 * object manager has taken on the object's behalf and not yet given back.
 */
typedef struct _EXP_HANDLE_PAGE_INFO
{
    HANDLE_TABLE_ENTRY_INFO AuditMasks[LOW_LEVEL_ENTRIES];
    ULONG CachedReferences[LOW_LEVEL_ENTRIES];
} EXP_HANDLE_PAGE_INFO, *PEXP_HANDLE_PAGE_INFO;

C_ASSERT(sizeof(EXP_HANDLE_PAGE_INFO) == HANDLE_TABLE_ENTRY_INFO_SIZE);

/*
 * The first entry of every page of entries is kept back, and holds what the
 * page has to say about itself: where its indices start and where the rest of
 * its own account lives. An index that lands on it is therefore no handle, and
 * handle zero, which lands on the very first one, names nothing.
 */
#define ExpPageOfEntry(Entry) ((PHANDLE_TABLE_ENTRY)PAGE_ALIGN(Entry))

typedef struct _EXP_HANDLE_TABLE_BUCKET
{
    LIST_ENTRY ListHead;
    EX_PUSH_LOCK Lock;
} EXP_HANDLE_TABLE_BUCKET;

static EXP_HANDLE_TABLE_BUCKET ExpHandleTableBuckets[HANDLE_TABLE_LIST_BUCKETS];

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
ExpPageBaseIndex(
    _In_ PHANDLE_TABLE_ENTRY Page)
{
    return (ULONG)Page[0].HighValue;
}

static
PEXP_HANDLE_PAGE_INFO
ExpPageInfo(
    _In_ PHANDLE_TABLE_ENTRY Page)
{
    return (PEXP_HANDLE_PAGE_INFO)Page[0].InfoTable;
}

static
ULONG
ExpEntryIndex(
    _In_ PHANDLE_TABLE_ENTRY Entry)
{
    PHANDLE_TABLE_ENTRY Page = ExpPageOfEntry(Entry);

    return ExpPageBaseIndex(Page) + (ULONG)(Entry - Page);
}

static
PULONG
ExpHandleCachedReference(
    _In_ PHANDLE_TABLE_ENTRY Entry)
{
    PHANDLE_TABLE_ENTRY Page = ExpPageOfEntry(Entry);
    PEXP_HANDLE_PAGE_INFO Info = ExpPageInfo(Page);

    if (Info == NULL)
    {
        return NULL;
    }

    return &Info->CachedReferences[Entry - Page];
}

static
PHANDLE_TABLE_ENTRY_INFO
ExpHandleAuditMask(
    _In_ PHANDLE_TABLE_ENTRY Entry)
{
    PHANDLE_TABLE_ENTRY Page = ExpPageOfEntry(Entry);
    PEXP_HANDLE_PAGE_INFO Info = ExpPageInfo(Page);

    if (Info == NULL)
    {
        return NULL;
    }

    return &Info->AuditMasks[Entry - Page];
}

static
PHANDLE_TABLE_FREE_LIST
ExpFreeList(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ ULONG Index)
{
    return &HandleTable->FreeLists[Index];
}

static
ULONG
ExpCurrentFreeList(VOID)
{
    if (ExpFreeListCount <= 1)
    {
        return 0;
    }

    return KeGetCurrentProcessorNumber() % ExpFreeListCount;
}

static
VOID
ExpFreeTablePagedPool(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Buffer,
    _In_ SIZE_T Size)
{
    if (Process != NULL)
    {
        PsReturnPoolQuota(Process, PagedPool, Size);
    }

    ExFreePoolWithTag(Buffer, EXP_HANDLE_LEVEL_TAG);
}

static
PVOID
ExpAllocateTablePagedPool(
    _In_opt_ PEPROCESS Process,
    _In_ SIZE_T Size)
{
    PVOID Buffer;

    Buffer = ExAllocatePoolZero(PagedPool, Size, EXP_HANDLE_LEVEL_TAG);
    if (Buffer == NULL)
    {
        return NULL;
    }

    if ((Process != NULL) &&
        !NT_SUCCESS(PsChargeProcessPoolQuota(Process, PagedPool, Size)))
    {
        ExFreePoolWithTag(Buffer, EXP_HANDLE_LEVEL_TAG);
        return NULL;
    }

    return Buffer;
}

static
PHANDLE_TABLE_ENTRY
ExpAllocateLowLevelTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ ULONG BaseIndex)
{
    PHANDLE_TABLE_ENTRY Page;
    PEXP_HANDLE_PAGE_INFO Info;

    Page = ExpAllocateTablePagedPool(HandleTable->QuotaProcess, PAGE_SIZE);
    if (Page == NULL)
    {
        return NULL;
    }

    /* The page has to start a page, or no entry could name the page it is in. */
    if (ExpPageOfEntry(Page) != Page)
    {
        ExpFreeTablePagedPool(HandleTable->QuotaProcess, Page, PAGE_SIZE);
        return NULL;
    }

    Info = ExAllocatePoolZero(PagedPool,
                              sizeof(EXP_HANDLE_PAGE_INFO),
                              EXP_HANDLE_INFO_TAG);
    if (Info == NULL)
    {
        ExpFreeTablePagedPool(HandleTable->QuotaProcess, Page, PAGE_SIZE);
        return NULL;
    }

    Page[0].HighValue = BaseIndex;
    Page[0].InfoTable = (PHANDLE_TABLE_ENTRY_INFO)Info;

    InterlockedIncrement(&HandleTable->ExtraInfoPages);

    return Page;
}

static
VOID
ExpFreeLowLevelTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY Page)
{
    PEXP_HANDLE_PAGE_INFO Info = ExpPageInfo(Page);

    if (Info != NULL)
    {
        Page[0].InfoTable = NULL;
        ExFreePoolWithTag(Info, EXP_HANDLE_INFO_TAG);
        InterlockedDecrement(&HandleTable->ExtraInfoPages);
    }

    ExpFreeTablePagedPool(HandleTable->QuotaProcess, Page, PAGE_SIZE);
}

/*
 * The table object itself, with as many free lists behind it as this system
 * gives a table. It is not paged: a handle is looked up at times when a fault
 * cannot be taken.
 */
PHANDLE_TABLE
NTAPI
ExpAllocateHandleTable(
    _In_opt_ PEPROCESS Process,
    _In_ BOOLEAN InsertInList)
{
    PHANDLE_TABLE HandleTable;
    SIZE_T Size;
    ULONG Index;

    Size = sizeof(HANDLE_TABLE) +
           (SIZE_T)(ExpFreeListCount - 1) * sizeof(HANDLE_TABLE_FREE_LIST);

    HandleTable = ExAllocatePoolZero(NonPagedPool, Size, EXP_HANDLE_TABLE_TAG);
    if (HandleTable == NULL)
    {
        return NULL;
    }

    HandleTable->QuotaProcess = Process;
    HandleTable->UniqueProcessId = (Process != NULL) ?
        PtrToUlong(PsGetProcessId(Process)) : 0;

    ExInitializePushLock(&HandleTable->HandleTableLock);
    ExInitializePushLock(&HandleTable->HandleContentionEvent);

    for (Index = 0; Index < ExpFreeListCount; Index++)
    {
        ExInitializePushLock(&ExpFreeList(HandleTable, Index)->FreeListLock);
    }

    UNREFERENCED_PARAMETER(InsertInList);

    return HandleTable;
}

VOID
NTAPI
ExpFreeHandleTable(
    _In_ PHANDLE_TABLE HandleTable)
{
    ExFreePoolWithTag(HandleTable, EXP_HANDLE_TABLE_TAG);
}

static
PHANDLE_TABLE_ENTRY *
ExpAllocateMidLevelTable(
    _In_ PHANDLE_TABLE HandleTable)
{
    return ExpAllocateTablePagedPool(HandleTable->QuotaProcess,
                                     MID_LEVEL_ENTRIES *
                                     sizeof(PHANDLE_TABLE_ENTRY));
}

static
PHANDLE_TABLE_ENTRY
ExpLookupEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ EXHANDLE Handle)
{
    ULONG_PTR Code = HandleTable->TableCode;
    PHANDLE_TABLE_ENTRY LowTable;
    PHANDLE_TABLE_ENTRY *MidTable;
    PHANDLE_TABLE_ENTRY **HighTable;
    ULONG Index = (ULONG)Handle.Index;

    /* The first entry of each page belongs to the page, not to a caller. */
    if ((Index % LOW_LEVEL_ENTRIES) == 0)
    {
        return NULL;
    }

    if (Index >= HandleTable->NextHandleNeedingPool)
    {
        return NULL;
    }

    if (ExpTableLevel(Code) == 0)
    {
        LowTable = ExpTableBase(Code);
        return (LowTable != NULL) ? &LowTable[Index] : NULL;
    }

    if (ExpTableLevel(Code) == 1)
    {
        MidTable = ExpTableBase(Code);
        LowTable = MidTable[Index / LOW_LEVEL_ENTRIES];
        return (LowTable != NULL) ?
               &LowTable[Index % LOW_LEVEL_ENTRIES] : NULL;
    }

    HighTable = ExpTableBase(Code);
    MidTable = HighTable[Index / MAX_MID_INDEX];
    if (MidTable == NULL)
    {
        return NULL;
    }

    LowTable = MidTable[(Index % MAX_MID_INDEX) / LOW_LEVEL_ENTRIES];
    return (LowTable != NULL) ? &LowTable[Index % LOW_LEVEL_ENTRIES] : NULL;
}

/*
 * Makes room for the indices from NextHandleNeedingPool on, deepening the table
 * when the next index no longer fits the shape it has. The table lock is held.
 */
static
PHANDLE_TABLE_ENTRY
ExpGrowHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable,
    _Out_ PULONG BaseIndex)
{
    ULONG_PTR Code = HandleTable->TableCode;
    ULONG Index = HandleTable->NextHandleNeedingPool;
    PHANDLE_TABLE_ENTRY LowTable;
    PHANDLE_TABLE_ENTRY *MidTable;
    PHANDLE_TABLE_ENTRY **HighTable;
    ULONG Level = ExpTableLevel(Code);

    if (Index >= MAX_HIGH_INDEX)
    {
        return NULL;
    }

    LowTable = ExpAllocateLowLevelTable(HandleTable, Index);
    if (LowTable == NULL)
    {
        return NULL;
    }

    if ((Code == 0) && (Index == 0))
    {
        HandleTable->TableCode = (ULONG_PTR)LowTable;
        HandleTable->NextHandleNeedingPool = LOW_LEVEL_ENTRIES;
        *BaseIndex = 0;
        return LowTable;
    }

    if (Level == 0)
    {
        MidTable = ExpAllocateMidLevelTable(HandleTable);
        if (MidTable == NULL)
        {
            ExpFreeLowLevelTable(HandleTable, LowTable);
            return NULL;
        }

        MidTable[0] = ExpTableBase(Code);
        Code = (ULONG_PTR)MidTable | 1;
        HandleTable->TableCode = Code;
        Level = 1;
    }

    if (Level == 1)
    {
        if (Index < MAX_MID_INDEX)
        {
            MidTable = ExpTableBase(Code);
            MidTable[Index / LOW_LEVEL_ENTRIES] = LowTable;
            HandleTable->NextHandleNeedingPool = Index + LOW_LEVEL_ENTRIES;
            *BaseIndex = Index;
            return LowTable;
        }

        HighTable = ExpAllocateTablePagedPool(HandleTable->QuotaProcess,
                                              HIGH_LEVEL_ENTRIES *
                                              sizeof(PVOID));
        if (HighTable == NULL)
        {
            ExpFreeLowLevelTable(HandleTable, LowTable);
            return NULL;
        }

        HighTable[0] = ExpTableBase(Code);
        Code = (ULONG_PTR)HighTable | 2;
        HandleTable->TableCode = Code;
    }

    HighTable = ExpTableBase(Code);
    MidTable = HighTable[Index / MAX_MID_INDEX];
    if (MidTable == NULL)
    {
        MidTable = ExpAllocateMidLevelTable(HandleTable);
        if (MidTable == NULL)
        {
            ExpFreeLowLevelTable(HandleTable, LowTable);
            return NULL;
        }

        HighTable[Index / MAX_MID_INDEX] = MidTable;
    }

    MidTable[(Index % MAX_MID_INDEX) / LOW_LEVEL_ENTRIES] = LowTable;
    HandleTable->NextHandleNeedingPool = Index + LOW_LEVEL_ENTRIES;
    *BaseIndex = Index;

    return LowTable;
}

static
VOID
ExpPushFreeEntry(
    _Inout_ PHANDLE_TABLE_FREE_LIST FreeList,
    _In_ PHANDLE_TABLE_ENTRY Entry,
    _In_ BOOLEAN ToTail)
{
    Entry->Value = 0;
    Entry->NextFreeHandleEntry = NULL;

    if (ToTail)
    {
        if (FreeList->LastFreeHandleEntry != NULL)
        {
            FreeList->LastFreeHandleEntry->NextFreeHandleEntry = Entry;
        }
        else
        {
            FreeList->FirstFreeHandleEntry = Entry;
        }

        FreeList->LastFreeHandleEntry = Entry;
        return;
    }

    Entry->NextFreeHandleEntry = FreeList->FirstFreeHandleEntry;
    FreeList->FirstFreeHandleEntry = Entry;

    if (FreeList->LastFreeHandleEntry == NULL)
    {
        FreeList->LastFreeHandleEntry = Entry;
    }
}

static
PHANDLE_TABLE_ENTRY
ExpPopFreeEntry(
    _Inout_ PHANDLE_TABLE_FREE_LIST FreeList)
{
    PHANDLE_TABLE_ENTRY Entry = FreeList->FirstFreeHandleEntry;

    if (Entry == NULL)
    {
        return NULL;
    }

    FreeList->FirstFreeHandleEntry = Entry->NextFreeHandleEntry;
    if (FreeList->FirstFreeHandleEntry == NULL)
    {
        FreeList->LastFreeHandleEntry = NULL;
    }

    Entry->NextFreeHandleEntry = NULL;
    FreeList->HandleCount++;

    if (FreeList->HandleCount > (LONG)FreeList->HighWaterMark)
    {
        FreeList->HighWaterMark = FreeList->HandleCount;
    }

    return Entry;
}

static
VOID
ExpChainPageEntries(
    _Inout_ PHANDLE_TABLE_FREE_LIST FreeList,
    _In_ PHANDLE_TABLE_ENTRY Page)
{
    ULONG Index;

    for (Index = 1; Index < LOW_LEVEL_ENTRIES; Index++)
    {
        ExpPushFreeEntry(FreeList, &Page[Index], TRUE);
    }
}

static
PHANDLE_TABLE_ENTRY
ExpAllocateHandleTableEntrySlow(
    _Inout_ PHANDLE_TABLE HandleTable,
    _In_ ULONG ListIndex)
{
    PHANDLE_TABLE_FREE_LIST FreeList = ExpFreeList(HandleTable, ListIndex);
    PHANDLE_TABLE_ENTRY Page;
    PHANDLE_TABLE_ENTRY Entry;
    ULONG BaseIndex;
    ULONG Index;

    /* Another processor's list may still be holding entries. */
    for (Index = 0; Index < ExpFreeListCount; Index++)
    {
        PHANDLE_TABLE_FREE_LIST Other = ExpFreeList(HandleTable, Index);

        if (Index == ListIndex)
        {
            continue;
        }

        ExAcquirePushLockExclusive(&Other->FreeListLock);
        Entry = ExpPopFreeEntry(Other);
        ExReleasePushLockExclusive(&Other->FreeListLock);

        if (Entry != NULL)
        {
            return Entry;
        }
    }

    ExAcquirePushLockExclusive(&HandleTable->HandleTableLock);
    Page = ExpGrowHandleTable(HandleTable, &BaseIndex);
    ExReleasePushLockExclusive(&HandleTable->HandleTableLock);

    if (Page == NULL)
    {
        return NULL;
    }

    ExAcquirePushLockExclusive(&FreeList->FreeListLock);
    ExpChainPageEntries(FreeList, Page);
    Entry = ExpPopFreeEntry(FreeList);
    ExReleasePushLockExclusive(&FreeList->FreeListLock);

    return Entry;
}

static
PHANDLE_TABLE_ENTRY
ExpAllocateHandleTableEntry(
    _Inout_ PHANDLE_TABLE HandleTable,
    _Out_ PEXHANDLE Handle)
{
    ULONG ListIndex = ExpCurrentFreeList();
    PHANDLE_TABLE_FREE_LIST FreeList = ExpFreeList(HandleTable, ListIndex);
    PHANDLE_TABLE_ENTRY Entry;

    Handle->Value = 0;

    ExAcquirePushLockExclusive(&FreeList->FreeListLock);
    Entry = ExpPopFreeEntry(FreeList);
    ExReleasePushLockExclusive(&FreeList->FreeListLock);

    if (Entry == NULL)
    {
        Entry = ExpAllocateHandleTableEntrySlow(HandleTable, ListIndex);
        if (Entry == NULL)
        {
            return NULL;
        }
    }

    Handle->Index = ExpEntryIndex(Entry);

    return Entry;
}

/*
 * Gives an entry back. A table that insists on first in, first out hands it out
 * again only after every entry freed before it, so a handle value does not come
 * back while somebody may still be holding the old one.
 */
static
VOID
ExpFreeHandleTableEntry(
    _Inout_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY Entry)
{
    ULONG ListIndex = ExpCurrentFreeList();
    PHANDLE_TABLE_FREE_LIST FreeList = ExpFreeList(HandleTable, ListIndex);
    PULONG Cached = ExpHandleCachedReference(Entry);
    PHANDLE_TABLE_ENTRY_INFO Audit = ExpHandleAuditMask(Entry);

    if (Cached != NULL)
    {
        *Cached = 0;
    }

    if (Audit != NULL)
    {
        Audit->AuditMask = 0;
    }

    Entry->HighValue = 0;

    ExAcquirePushLockExclusive(&FreeList->FreeListLock);
    ExpPushFreeEntry(FreeList, Entry, HandleTable->StrictFIFO ? TRUE : FALSE);
    FreeList->HandleCount--;
    ExReleasePushLockExclusive(&FreeList->FreeListLock);
}

/*
 * Waits for whoever holds the entry to let it go. An entry is held across only
 * a few instructions, so a spin sees nearly every release; a spin that does not
 * is followed by the shortest sleep the clock can measure, which gives the
 * holder its processor back on a machine that has only one.
 *
 * Nothing is signalled on release: HANDLE_TABLE has no field this kernel may
 * use to count who is waiting, and a release that had to look would cost every
 * uncontended release something.
 */
static
VOID
ExpBlockOnLockedHandleEntry(
    _Inout_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY Entry)
{
    LARGE_INTEGER Interval;
    ULONG Spin;

    UNREFERENCED_PARAMETER(HandleTable);

    for (Spin = 0; Spin < EXP_HANDLE_CONTENTION_SPINS; Spin++)
    {
        if ((ULONG_PTR)ReadPointerNoFence(&Entry->Object) &
            EXHANDLE_TABLE_ENTRY_LOCK_BIT)
        {
            return;
        }

        YieldProcessor();
    }

    /* A caller that is raised cannot be taken off its processor, so it spins. */
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        return;
    }

    Interval.QuadPart = EXP_HANDLE_CONTENTION_INTERVAL;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
VOID
ExpLinkHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable)
{
    ULONG Bucket = HandleTable->UniqueProcessId % HANDLE_TABLE_LIST_BUCKETS;

    ExAcquirePushLockExclusive(&ExpHandleTableBuckets[Bucket].Lock);
    InsertTailList(&ExpHandleTableBuckets[Bucket].ListHead,
                   &HandleTable->HandleTableList);
    ExReleasePushLockExclusive(&ExpHandleTableBuckets[Bucket].Lock);
}

static
VOID
ExpUnlinkHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable)
{
    ULONG Bucket = HandleTable->UniqueProcessId % HANDLE_TABLE_LIST_BUCKETS;

    if (HandleTable->HandleTableList.Flink == NULL)
    {
        return;
    }

    ExAcquirePushLockExclusive(&ExpHandleTableBuckets[Bucket].Lock);
    RemoveEntryList(&HandleTable->HandleTableList);
    ExReleasePushLockExclusive(&ExpHandleTableBuckets[Bucket].Lock);

    HandleTable->HandleTableList.Flink = NULL;
    HandleTable->HandleTableList.Blink = NULL;
}

static
VOID
ExpFreeHandleTableLevels(
    _Inout_ PHANDLE_TABLE HandleTable)
{
    ULONG_PTR Code = HandleTable->TableCode;
    PHANDLE_TABLE_ENTRY *MidTable;
    PHANDLE_TABLE_ENTRY **HighTable;
    ULONG Mid, High;

    HandleTable->TableCode = 0;

    if (Code == 0)
    {
        return;
    }

    if (ExpTableLevel(Code) == 0)
    {
        ExpFreeLowLevelTable(HandleTable, ExpTableBase(Code));
        return;
    }

    if (ExpTableLevel(Code) == 1)
    {
        MidTable = ExpTableBase(Code);

        for (Mid = 0; Mid < MID_LEVEL_ENTRIES; Mid++)
        {
            if (MidTable[Mid] != NULL)
            {
                ExpFreeLowLevelTable(HandleTable, MidTable[Mid]);
            }
        }

        ExpFreeTablePagedPool(HandleTable->QuotaProcess,
                              MidTable,
                              MID_LEVEL_ENTRIES *
                              sizeof(PHANDLE_TABLE_ENTRY));
        return;
    }

    HighTable = ExpTableBase(Code);

    for (High = 0; High < HIGH_LEVEL_ENTRIES; High++)
    {
        MidTable = HighTable[High];
        if (MidTable == NULL)
        {
            continue;
        }

        for (Mid = 0; Mid < MID_LEVEL_ENTRIES; Mid++)
        {
            if (MidTable[Mid] != NULL)
            {
                ExpFreeLowLevelTable(HandleTable, MidTable[Mid]);
            }
        }

        ExpFreeTablePagedPool(HandleTable->QuotaProcess,
                              MidTable,
                              MID_LEVEL_ENTRIES *
                              sizeof(PHANDLE_TABLE_ENTRY));
    }

    ExpFreeTablePagedPool(HandleTable->QuotaProcess,
                          HighTable,
                          HIGH_LEVEL_ENTRIES * sizeof(PVOID));
}

/*
 * Walks every entry that names something, handing each to the caller. The entry
 * is locked while the caller looks at it unless the table is being torn down,
 * when nobody else can reach it any more.
 */
static
BOOLEAN
ExpWalkHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable,
    _In_ PEX_SWEEP_HANDLE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _In_ BOOLEAN Locked,
    _Out_opt_ PHANDLE FoundHandle)
{
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY Entry;
    ULONG Index;

    for (Index = 1; Index < HandleTable->NextHandleNeedingPool; Index++)
    {
        Handle.Value = 0;
        Handle.Index = Index;

        Entry = ExpLookupEntry(HandleTable, Handle);
        if ((Entry == NULL) || (Entry->Value == 0))
        {
            continue;
        }

        if (Locked && !ExpLockHandleTableEntry(HandleTable, Entry))
        {
            continue;
        }

        if (Callback(Entry, Handle.GenericHandleOverlay, Context))
        {
            if (FoundHandle != NULL)
            {
                *FoundHandle = Handle.GenericHandleOverlay;
            }

            if (Locked)
            {
                ExUnlockHandleTableEntry(HandleTable, Entry);
            }

            return TRUE;
        }

        if (Locked)
        {
            ExUnlockHandleTableEntry(HandleTable, Entry);
        }
    }

    return FALSE;
}

/*
 * Puts every entry that names nothing back on the first free list. Used after a
 * table has been filled in by index rather than through the free lists.
 */
static
VOID
ExpRebuildFreeLists(
    _Inout_ PHANDLE_TABLE HandleTable)
{
    PHANDLE_TABLE_FREE_LIST FreeList = ExpFreeList(HandleTable, 0);
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY Entry;
    ULONG Index;
    ULONG Used = 0;
    ULONG List;

    for (List = 0; List < ExpFreeListCount; List++)
    {
        PHANDLE_TABLE_FREE_LIST Other = ExpFreeList(HandleTable, List);

        Other->FirstFreeHandleEntry = NULL;
        Other->LastFreeHandleEntry = NULL;
        Other->HandleCount = 0;
    }

    for (Index = 1; Index < HandleTable->NextHandleNeedingPool; Index++)
    {
        Handle.Value = 0;
        Handle.Index = Index;

        Entry = ExpLookupEntry(HandleTable, Handle);
        if (Entry == NULL)
        {
            continue;
        }

        if (Entry->Value != 0)
        {
            Used++;
            continue;
        }

        ExpPushFreeEntry(FreeList, Entry, TRUE);
    }

    FreeList->HandleCount = (LONG)Used;
    if (Used > FreeList->HighWaterMark)
    {
        FreeList->HighWaterMark = Used;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeHandleTables(VOID)
{
    ULONG Index;

    ExpFreeListCount = KeNumberProcessors;
    if (ExpFreeListCount > EXP_HANDLE_MAX_FREE_LISTS)
    {
        ExpFreeListCount = EXP_HANDLE_MAX_FREE_LISTS;
    }

    for (Index = 0; Index < HANDLE_TABLE_LIST_BUCKETS; Index++)
    {
        InitializeListHead(&ExpHandleTableBuckets[Index].ListHead);
        ExInitializePushLock(&ExpHandleTableBuckets[Index].Lock);
    }
}

PLIST_ENTRY
NTAPI
ExpFirstHandleTableEntry(
    _Out_ PULONG Bucket)
{
    ULONG Index;

    for (Index = 0; Index < HANDLE_TABLE_LIST_BUCKETS; Index++)
    {
        if (!IsListEmpty(&ExpHandleTableBuckets[Index].ListHead))
        {
            *Bucket = Index;
            return ExpHandleTableBuckets[Index].ListHead.Flink;
        }
    }

    *Bucket = HANDLE_TABLE_LIST_BUCKETS;
    return NULL;
}

PLIST_ENTRY
NTAPI
ExpNextHandleTableEntry(
    _Inout_ PULONG Bucket,
    _In_ PLIST_ENTRY Entry)
{
    ULONG Index = *Bucket;

    if (Index >= HANDLE_TABLE_LIST_BUCKETS)
    {
        return NULL;
    }

    if (Entry->Flink != &ExpHandleTableBuckets[Index].ListHead)
    {
        return Entry->Flink;
    }

    for (Index++; Index < HANDLE_TABLE_LIST_BUCKETS; Index++)
    {
        if (!IsListEmpty(&ExpHandleTableBuckets[Index].ListHead))
        {
            *Bucket = Index;
            return ExpHandleTableBuckets[Index].ListHead.Flink;
        }
    }

    *Bucket = HANDLE_TABLE_LIST_BUCKETS;
    return NULL;
}

PHANDLE_TABLE
NTAPI
ExCreateHandleTable(
    _In_opt_ PEPROCESS Process,
    _In_ BOOLEAN InsertInList)
{
    PHANDLE_TABLE HandleTable;
    PHANDLE_TABLE_ENTRY Page;
    ULONG BaseIndex;

    HandleTable = ExpAllocateHandleTable(Process, InsertInList);
    if (HandleTable == NULL)
    {
        return NULL;
    }

    Page = ExpGrowHandleTable(HandleTable, &BaseIndex);
    if (Page == NULL)
    {
        ExpFreeHandleTable(HandleTable);
        return NULL;
    }

    ExpChainPageEntries(ExpFreeList(HandleTable, 0), Page);

    if (InsertInList)
    {
        ExpLinkHandleTable(HandleTable);
    }

    return HandleTable;
}

VOID
NTAPI
ExRemoveHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable)
{
    ExpUnlinkHandleTable(HandleTable);
}

VOID
NTAPI
ExDestroyHandleTable(
    _Inout_ PHANDLE_TABLE HandleTable,
    _In_opt_ PVOID DestroyHandleProcedure)
{
    ExpUnlinkHandleTable(HandleTable);

    HandleTable->Rundown = TRUE;

    if (DestroyHandleProcedure != NULL)
    {
        ExpWalkHandleTable(HandleTable,
                           (PEX_SWEEP_HANDLE_CALLBACK)DestroyHandleProcedure,
                           HandleTable,
                           FALSE,
                           NULL);
    }

    ExpFreeHandleTableLevels(HandleTable);

    ExpFreeHandleTable(HandleTable);
}

LONG
NTAPI
ExpGetHandleCount(
    _In_ PHANDLE_TABLE HandleTable)
{
    LONG Count = 0;
    ULONG Index;

    for (Index = 0; Index < ExpFreeListCount; Index++)
    {
        Count += ExpFreeList(HandleTable, Index)->HandleCount;
    }

    return Count;
}

PHANDLE_TABLE_ENTRY
NTAPI
ExpLookupHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ EXHANDLE Handle)
{
    return ExpLookupEntry(HandleTable, Handle);
}

BOOLEAN
NTAPI
ExpLockHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    ULONG_PTR Value;

    for (;;)
    {
        Value = (ULONG_PTR)ReadPointerNoFence(&HandleTableEntry->Object);

        /* Nothing is named here, so there is nothing to hold. */
        if (Value == 0)
        {
            return FALSE;
        }

        if (!(Value & EXHANDLE_TABLE_ENTRY_LOCK_BIT))
        {
            ExpBlockOnLockedHandleEntry(HandleTable, HandleTableEntry);
            continue;
        }

        if ((ULONG_PTR)InterlockedCompareExchangePointer(
                &HandleTableEntry->Object,
                (PVOID)(Value & ~(ULONG_PTR)EXHANDLE_TABLE_ENTRY_LOCK_BIT),
                (PVOID)Value) == Value)
        {
            return TRUE;
        }
    }
}

VOID
NTAPI
ExUnlockHandleTableEntry(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    ULONG_PTR Value;

    UNREFERENCED_PARAMETER(HandleTable);

    Value = (ULONG_PTR)ReadPointerNoFence(&HandleTableEntry->Object);

    ASSERT((Value & EXHANDLE_TABLE_ENTRY_LOCK_BIT) == 0);

    InterlockedExchangePointer(
        &HandleTableEntry->Object,
        (PVOID)(Value | EXHANDLE_TABLE_ENTRY_LOCK_BIT));
}

HANDLE
NTAPI
ExCreateHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    PHANDLE_TABLE_ENTRY Entry;
    PULONG Cached;
    EXHANDLE Handle;

    Entry = ExpAllocateHandleTableEntry(HandleTable, &Handle);
    if (Entry == NULL)
    {
        return NULL;
    }

    Cached = ExpHandleCachedReference(Entry);
    if (Cached != NULL)
    {
        *Cached = 0;
    }

    Entry->HighValue = HandleTableEntry->HighValue;

    InterlockedExchangePointer(
        &Entry->Object,
        (PVOID)((ULONG_PTR)HandleTableEntry->Object |
                EXHANDLE_TABLE_ENTRY_LOCK_BIT));

    return Handle.GenericHandleOverlay;
}

BOOLEAN
NTAPI
ExDestroyHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_opt_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY Entry = HandleTableEntry;

    ExHandle.GenericHandleOverlay = Handle;
    ExHandle.TagBits = 0;

    if (Entry == NULL)
    {
        Entry = ExpLookupEntry(HandleTable, ExHandle);
        if (Entry == NULL)
        {
            return FALSE;
        }

        if (!ExpLockHandleTableEntry(HandleTable, Entry))
        {
            return FALSE;
        }
    }

    ExpFreeHandleTableEntry(HandleTable, Entry);

    return TRUE;
}

PHANDLE_TABLE_ENTRY
NTAPI
ExMapHandleToPointer(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY Entry;

    ExHandle.GenericHandleOverlay = Handle;
    ExHandle.TagBits = 0;

    Entry = ExpLookupEntry(HandleTable, ExHandle);
    if (Entry == NULL)
    {
        return NULL;
    }

    if (!ExpLockHandleTableEntry(HandleTable, Entry))
    {
        return NULL;
    }

    return Entry;
}

PULONG
NTAPI
ExGetHandleCachedReferenceCount(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_opt_ PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY Entry = HandleTableEntry;

    if (Entry == NULL)
    {
        ExHandle.GenericHandleOverlay = Handle;
        ExHandle.TagBits = 0;

        Entry = ExpLookupEntry(HandleTable, ExHandle);
        if (Entry == NULL)
        {
            return NULL;
        }
    }

    return ExpHandleCachedReference(Entry);
}

BOOLEAN
NTAPI
ExChangeHandle(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ HANDLE Handle,
    _In_ PEX_CHANGE_HANDLE_CALLBACK ChangeRoutine,
    _In_ ULONG_PTR Context)
{
    PHANDLE_TABLE_ENTRY Entry;
    BOOLEAN Result;

    Entry = ExMapHandleToPointer(HandleTable, Handle);
    if (Entry == NULL)
    {
        return FALSE;
    }

    Result = ChangeRoutine(Entry, Context);

    ExUnlockHandleTableEntry(HandleTable, Entry);

    return Result;
}

VOID
NTAPI
ExSweepHandleTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PEX_SWEEP_HANDLE_CALLBACK EnumHandleProcedure,
    _In_opt_ PVOID Context)
{
    ExpWalkHandleTable(HandleTable, EnumHandleProcedure, Context, FALSE, NULL);
}

BOOLEAN
NTAPI
ExEnumHandleTable(
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PEX_ENUM_HANDLE_CALLBACK EnumHandleProcedure,
    _Inout_ PVOID Context,
    _Out_opt_ PHANDLE Handle)
{
    return ExpWalkHandleTable(HandleTable,
                              (PEX_SWEEP_HANDLE_CALLBACK)EnumHandleProcedure,
                              Context,
                              TRUE,
                              Handle);
}

/*
 * Builds a second table naming the same things, for a process that inherits
 * from another. A handle keeps the value it had, so the child finds what it was
 * told it would find; the caller decides, entry by entry, what the copy says.
 */
PHANDLE_TABLE
NTAPI
ExDupHandleTable(
    _In_ PEPROCESS Process,
    _In_ PHANDLE_TABLE HandleTable,
    _In_ PEX_DUPLICATE_HANDLE_CALLBACK DupHandleProcedure,
    _In_ ULONG_PTR Mask)
{
    PHANDLE_TABLE NewTable;
    PHANDLE_TABLE_ENTRY Entry, NewEntry;
    HANDLE_TABLE_ENTRY Copy;
    EXHANDLE Handle;
    ULONG BaseIndex;
    ULONG Index;

    NewTable = ExCreateHandleTable(Process, FALSE);
    if (NewTable == NULL)
    {
        return NULL;
    }

    NewTable->Duplicated = TRUE;

    ExAcquirePushLockExclusive(&HandleTable->HandleTableLock);

    /* The copy has to be at least as deep, so every index can be named. */
    while (NewTable->NextHandleNeedingPool < HandleTable->NextHandleNeedingPool)
    {
        if (ExpGrowHandleTable(NewTable, &BaseIndex) == NULL)
        {
            ExReleasePushLockExclusive(&HandleTable->HandleTableLock);
            ExDestroyHandleTable(NewTable, NULL);
            return NULL;
        }
    }

    for (Index = 1; Index < HandleTable->NextHandleNeedingPool; Index++)
    {
        Handle.Value = 0;
        Handle.Index = Index;

        Entry = ExpLookupEntry(HandleTable, Handle);
        if ((Entry == NULL) || (Entry->Value == 0))
        {
            continue;
        }

        if (!ExpLockHandleTableEntry(HandleTable, Entry))
        {
            continue;
        }

        if ((Entry->ObAttributes & Mask) != Mask)
        {
            ExUnlockHandleTableEntry(HandleTable, Entry);
            continue;
        }

        Copy.Object = (PVOID)((ULONG_PTR)Entry->Object |
                              EXHANDLE_TABLE_ENTRY_LOCK_BIT);
        Copy.HighValue = Entry->HighValue;

        if (!DupHandleProcedure(Process, HandleTable, Entry, &Copy))
        {
            ExUnlockHandleTableEntry(HandleTable, Entry);
            continue;
        }

        ExUnlockHandleTableEntry(HandleTable, Entry);

        NewEntry = ExpLookupEntry(NewTable, Handle);
        if (NewEntry == NULL)
        {
            continue;
        }

        NewEntry->HighValue = Copy.HighValue;
        NewEntry->Object = (PVOID)((ULONG_PTR)Copy.Object |
                                   EXHANDLE_TABLE_ENTRY_LOCK_BIT);
    }

    ExReleasePushLockExclusive(&HandleTable->HandleTableLock);

    ExpRebuildFreeLists(NewTable);

    ExpLinkHandleTable(NewTable);

    return NewTable;
}

/*
 * What the walk the debugger command drives is gathering: either one handle the
 * user named, or every handle of every table.
 */
typedef struct _EXP_KDBG_HANDLE_WALK
{
    ULONG UniqueProcessId;
    HANDLE Wanted;
    ULONG Found;
} EXP_KDBG_HANDLE_WALK, *PEXP_KDBG_HANDLE_WALK;

static
BOOLEAN
NTAPI
ExpKdbgPrintHandle(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_opt_ PVOID Context)
{
    PEXP_KDBG_HANDLE_WALK Walk = Context;
    POBJECT_HEADER Header;
    POBJECT_TYPE Type;

    if ((Walk->Wanted != NULL) && (Handle != Walk->Wanted))
    {
        return FALSE;
    }

    Header = (POBJECT_HEADER)((ULONG_PTR)HandleTableEntry->Object &
                              ~(ULONG_PTR)OBJ_HANDLE_ATTRIBUTES);
    if (Header == NULL)
    {
        return FALSE;
    }

    Type = ObTypeIndexTable[Header->TypeIndex];

    KdbpPrint("  %p  %-16wZ  access %08lx  ptr %5ld  hnd %5ld  obj %p\n",
              Handle,
              (Type != NULL) ? &Type->Name : NULL,
              HandleTableEntry->GrantedAccess,
              (LONG)Header->PointerCount,
              (LONG)Header->HandleCount,
              &Header->Body);

    Walk->Found++;

    /* One handle was asked for, so the walk stops once it has been shown. */
    return (BOOLEAN)(Walk->Wanted != NULL);
}

/*
 * The debugger's handle command. Without an argument it shows every handle of
 * every process; with one it shows that handle wherever it is held.
 */
BOOLEAN
ExpKdbgExtHandle(
    ULONG Argc,
    PCHAR Argv[])
{
    EXP_KDBG_HANDLE_WALK Walk;
    PLIST_ENTRY Entry;
    PHANDLE_TABLE HandleTable;
    ULONG_PTR Value = 0;
    ULONG Bucket;

    RtlZeroMemory(&Walk, sizeof(Walk));

    if (Argc >= 2)
    {
        if (!KdbpGetHexNumber(Argv[1], &Value))
        {
            KdbpPrint("Invalid handle: %s\n", Argv[1]);
            return TRUE;
        }

        Walk.Wanted = (HANDLE)Value;
    }

    for (Entry = ExpFirstHandleTableEntry(&Bucket);
         Entry != NULL;
         Entry = ExpNextHandleTableEntry(&Bucket, Entry))
    {
        HandleTable = CONTAINING_RECORD(Entry, HANDLE_TABLE, HandleTableList);

        Walk.UniqueProcessId = HandleTable->UniqueProcessId;

        KdbpPrint("Process %lu, handle table %p, %ld handle(s)\n",
                  HandleTable->UniqueProcessId,
                  HandleTable,
                  ExpGetHandleCount(HandleTable));

        ExSweepHandleTable(HandleTable, ExpKdbgPrintHandle, &Walk);

        if ((Walk.Wanted != NULL) && (Walk.Found != 0))
        {
            break;
        }
    }

    if ((Walk.Wanted != NULL) && (Walk.Found == 0))
    {
        KdbpPrint("Handle %p is not held by any process\n", Walk.Wanted);
    }

    return TRUE;
}

/* EOF */
