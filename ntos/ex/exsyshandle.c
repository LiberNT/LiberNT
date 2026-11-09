/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling a caller what is held: handles, objects and locks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define EXP_SYSHANDLE_TAG 'dnaH'

/*
 * What the walk of one handle table is gathering, and how much room is left to
 * gather it into. The callback the handle table drives cannot take more than
 * one argument of its own, so everything the walk needs travels in here.
 */
typedef struct _EXP_HANDLE_WALK
{
    PVOID Buffer;
    ULONG Length;
    ULONG Used;
    ULONG Count;
    ULONG UniqueProcessId;
    BOOLEAN Extended;
    BOOLEAN Overflow;
} EXP_HANDLE_WALK, *PEXP_HANDLE_WALK;

/* PRIVATE FUNCTIONS **********************************************************/

static
UCHAR
ExpHandleAttributes(
    _In_ ULONG_PTR ObAttributes)
{
    UCHAR Attributes = 0;

    if (ObAttributes & OBJ_INHERIT)
    {
        Attributes |= OBJ_INHERIT;
    }

    if (ObAttributes & OBJ_AUDIT_OBJECT_CLOSE)
    {
        Attributes |= OBJ_AUDIT_OBJECT_CLOSE;
    }

    return Attributes;
}

/*
 * Writes one handle into the caller's buffer. The entry arrives locked, so the
 * object it names cannot be swapped out from under the read.
 */
static
BOOLEAN
NTAPI
ExpWriteHandleEntry(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_opt_ PVOID Context)
{
    PEXP_HANDLE_WALK Walk = Context;
    POBJECT_HEADER Header;
    ULONG Needed;

    Header = (POBJECT_HEADER)((ULONG_PTR)HandleTableEntry->Object &
                              ~(ULONG_PTR)OBJ_HANDLE_ATTRIBUTES);
    if (Header == NULL)
    {
        return FALSE;
    }

    Needed = Walk->Extended ?
        (ULONG)sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX) :
        (ULONG)sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO);

    Walk->Count++;

    if ((Walk->Buffer == NULL) || (Walk->Used + Needed > Walk->Length))
    {
        Walk->Used += Needed;
        Walk->Overflow = TRUE;
        return FALSE;
    }

    if (Walk->Extended)
    {
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Info =
            (PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX)
            ((PUCHAR)Walk->Buffer + Walk->Used);

        Info->Object = &Header->Body;
        Info->UniqueProcessId = Walk->UniqueProcessId;
        Info->HandleValue = (ULONG_PTR)Handle;
        Info->GrantedAccess = HandleTableEntry->GrantedAccess;
        Info->CreatorBackTraceIndex = 0;
        Info->ObjectTypeIndex = Header->TypeIndex;
        Info->HandleAttributes =
            ExpHandleAttributes(HandleTableEntry->ObAttributes);
        Info->Reserved = 0;
    }
    else
    {
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO Info =
            (PSYSTEM_HANDLE_TABLE_ENTRY_INFO)
            ((PUCHAR)Walk->Buffer + Walk->Used);

        Info->UniqueProcessId = (USHORT)Walk->UniqueProcessId;
        Info->CreatorBackTraceIndex = 0;
        Info->ObjectTypeIndex = Header->TypeIndex;
        Info->HandleAttributes =
            ExpHandleAttributes(HandleTableEntry->ObAttributes);
        Info->HandleValue = (USHORT)(ULONG_PTR)Handle;
        Info->Object = &Header->Body;
        Info->GrantedAccess = HandleTableEntry->GrantedAccess;
    }

    Walk->Used += Needed;

    return FALSE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Walks every handle table in the system. The tables are chained in buckets, so
 * a bucket is held only for as long as it takes to reach the next table, and
 * the table's own lock keeps it alive while its handles are read.
 */
NTSTATUS
NTAPI
ExpQueryHandleInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ BOOLEAN Extended)
{
    EXP_HANDLE_WALK Walk;
    PLIST_ENTRY Entry;
    PHANDLE_TABLE HandleTable;
    ULONG Bucket;
    ULONG Header;

    PAGED_CODE();

    Header = Extended ?
        (ULONG)FIELD_OFFSET(SYSTEM_HANDLE_INFORMATION_EX, Handles) :
        (ULONG)FIELD_OFFSET(SYSTEM_HANDLE_INFORMATION, Handles);

    RtlZeroMemory(&Walk, sizeof(Walk));

    Walk.Extended = Extended;
    Walk.Length = (Length > Header) ? (Length - Header) : 0;
    Walk.Buffer = ((Buffer != NULL) && (Length >= Header)) ?
        ((PUCHAR)Buffer + Header) : NULL;

    for (Entry = ExpFirstHandleTableEntry(&Bucket);
         Entry != NULL;
         Entry = ExpNextHandleTableEntry(&Bucket, Entry))
    {
        HandleTable = CONTAINING_RECORD(Entry, HANDLE_TABLE, HandleTableList);

        Walk.UniqueProcessId = HandleTable->UniqueProcessId;

        ExSweepHandleTable(HandleTable, ExpWriteHandleEntry, &Walk);
    }

    *ReturnLength = Header + Walk.Used;

    if ((Buffer == NULL) || (Length < Header) || Walk.Overflow)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (Extended)
    {
        PSYSTEM_HANDLE_INFORMATION_EX Info = Buffer;

        Info->NumberOfHandles = Walk.Count;
        Info->Reserved = 0;
    }
    else
    {
        ((PSYSTEM_HANDLE_INFORMATION)Buffer)->NumberOfHandles = Walk.Count;
    }

    return STATUS_SUCCESS;
}

/*
 * Lists the kinds of object the system knows about, and for each of them how
 * many there are and how many handles name one. The objects themselves are not
 * listed: an object has no list of its own to be walked.
 */
NTSTATUS
NTAPI
ExpQueryObjectInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PSYSTEM_OBJECTTYPE_INFORMATION Info;
    PSYSTEM_OBJECTTYPE_INFORMATION Previous = NULL;
    POBJECT_TYPE Type;
    PUCHAR Current = Buffer;
    ULONG Used = 0;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    for (Index = 0; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        ULONG Needed;

        Type = ObTypeIndexTable[Index];
        if (Type == NULL)
        {
            continue;
        }

        Needed = (ULONG)sizeof(SYSTEM_OBJECTTYPE_INFORMATION) +
                 Type->Name.Length + (ULONG)sizeof(WCHAR);
        Needed = ALIGN_UP_BY(Needed, sizeof(ULONG_PTR));

        if ((Buffer == NULL) || (Used + Needed > Length))
        {
            Used += Needed;
            Status = STATUS_INFO_LENGTH_MISMATCH;
            continue;
        }

        Info = (PSYSTEM_OBJECTTYPE_INFORMATION)Current;

        RtlZeroMemory(Info, sizeof(*Info));

        Info->NumberOfObjects = Type->TotalNumberOfObjects;
        Info->NumberOfHandles = Type->TotalNumberOfHandles;
        Info->TypeIndex = Type->Index;
        Info->InvalidAttributes = Type->TypeInfo.InvalidAttributes;
        Info->GenericMapping = Type->TypeInfo.GenericMapping;
        Info->ValidAccessMask = Type->TypeInfo.ValidAccessMask;
        Info->PoolType = Type->TypeInfo.PoolType;
        Info->SecurityRequired = Type->TypeInfo.SecurityRequired;
        Info->WaitableObject = (Type->DefaultObject != NULL);

        Info->TypeName.Length = Type->Name.Length;
        Info->TypeName.MaximumLength =
            (USHORT)(Type->Name.Length + sizeof(WCHAR));
        Info->TypeName.Buffer =
            (PWCHAR)(Current + sizeof(SYSTEM_OBJECTTYPE_INFORMATION));

        RtlCopyMemory(Info->TypeName.Buffer,
                      Type->Name.Buffer,
                      Type->Name.Length);
        Info->TypeName.Buffer[Type->Name.Length / sizeof(WCHAR)] =
            UNICODE_NULL;

        if (Previous != NULL)
        {
            Previous->NextEntryOffset =
                (ULONG)((PUCHAR)Info - (PUCHAR)Buffer);
        }

        Previous = Info;
        Current += Needed;
        Used += Needed;
    }

    if (Previous != NULL)
    {
        Previous->NextEntryOffset = 0;
    }

    *ReturnLength = Used;

    return Status;
}

/*
 * Lists the executive resources the system holds, which is what a caller
 * looking for a lock that is being contended asks for. Every resource is on one
 * list from the moment it is initialized, which is the list walked here.
 */
NTSTATUS
NTAPI
ExpQueryLocksInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    PRTL_PROCESS_LOCKS Locks = Buffer;
    PLIST_ENTRY Entry;
    PERESOURCE Resource;
    ULONG Count = 0;
    ULONG Needed;
    ULONG Index = 0;
    KIRQL OldIrql;

    PAGED_CODE();

    KeAcquireSpinLock(&ExpResourceSpinLock, &OldIrql);

    for (Entry = ExpSystemResourcesList.Flink;
         Entry != &ExpSystemResourcesList;
         Entry = Entry->Flink)
    {
        Count++;
    }

    Needed = (ULONG)FIELD_OFFSET(RTL_PROCESS_LOCKS, Locks) +
             Count * (ULONG)sizeof(RTL_PROCESS_LOCK_INFORMATION);

    if ((Buffer == NULL) || (Length < Needed))
    {
        KeReleaseSpinLock(&ExpResourceSpinLock, OldIrql);
        *ReturnLength = Needed;
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Locks->NumberOfLocks = Count;

    for (Entry = ExpSystemResourcesList.Flink;
         (Entry != &ExpSystemResourcesList) && (Index < Count);
         Entry = Entry->Flink, Index++)
    {
        PRTL_PROCESS_LOCK_INFORMATION Info = &Locks->Locks[Index];

        Resource = CONTAINING_RECORD(Entry, ERESOURCE, SystemResourcesList);

        RtlZeroMemory(Info, sizeof(*Info));

        Info->Address = Resource;
        Info->Type = RTL_RESOURCE_TYPE;
        Info->CreatorBackTraceIndex = 0;
        Info->OwnerThreadId =
            (ULONG)(ULONG_PTR)Resource->OwnerEntry.OwnerThread;
        Info->ActiveCount = Resource->ActiveCount;
        Info->ContentionCount = Resource->ContentionCount;
        Info->EntryCount = Resource->ActiveEntries;
        Info->RecursionCount = Resource->OwnerEntry.OwnerCount;
        Info->NumberOfSharedWaiters = Resource->NumberOfSharedWaiters;
        Info->NumberOfExclusiveWaiters = Resource->NumberOfExclusiveWaiters;
    }

    KeReleaseSpinLock(&ExpResourceSpinLock, OldIrql);

    *ReturnLength = Needed;

    return STATUS_SUCCESS;
}

/*
 * Lists every lookaside list the system keeps, so a caller can see which of
 * them are earning their keep and which are only holding memory.
 */
NTSTATUS
NTAPI
ExpQueryLookasideInformation(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    static PLIST_ENTRY Heads[] =
    {
        &ExPoolLookasideListHead,
        &ExpNonPagedLookasideListHead,
        &ExpPagedLookasideListHead,
        &ExSystemLookasideListHead
    };

    PSYSTEM_LOOKASIDE_INFORMATION Info = Buffer;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Written = 0;
    ULONG Head;

    PAGED_CODE();

    for (Head = 0; Head < RTL_NUMBER_OF(Heads); Head++)
    {
        for (Entry = Heads[Head]->Flink;
             Entry != Heads[Head];
             Entry = Entry->Flink)
        {
            PGENERAL_LOOKASIDE_POOL Lookaside =
                CONTAINING_RECORD(Entry, GENERAL_LOOKASIDE_POOL, ListEntry);

            Count++;

            if ((Buffer == NULL) ||
                ((Written + 1) * sizeof(SYSTEM_LOOKASIDE_INFORMATION) > Length))
            {
                continue;
            }

            Info[Written].CurrentDepth =
                ExQueryDepthSList(&Lookaside->ListHead);
            Info[Written].MaximumDepth = Lookaside->MaximumDepth;
            Info[Written].TotalAllocates = Lookaside->TotalAllocates;
            Info[Written].AllocateMisses = Lookaside->AllocateMisses;
            Info[Written].TotalFrees = Lookaside->TotalFrees;
            Info[Written].FreeMisses = Lookaside->FreeMisses;
            Info[Written].Type = Lookaside->Type;
            Info[Written].Tag = Lookaside->Tag;
            Info[Written].Size = Lookaside->Size;

            Written++;
        }
    }

    *ReturnLength = Count * (ULONG)sizeof(SYSTEM_LOOKASIDE_INFORMATION);

    if ((Buffer == NULL) || (Written != Count))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    return STATUS_SUCCESS;
}

/* EOF */
