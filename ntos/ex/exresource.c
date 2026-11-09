/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The executive resource, a lock one writer or many readers hold
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

KSPIN_LOCK ExpResourceSpinLock;
LIST_ENTRY ExpSystemResourcesList;

LARGE_INTEGER ExpTimeout;
LARGE_INTEGER ExShortTime;

ULONG ExpResourceTimeoutCount = 648000;

BOOLEAN ExResourceStrict = FALSE;

/* PRIVATE DATA ***************************************************************/

#define EXP_RESOURCE_TAG 'aChE'

/* How long one pass of a resource wait lasts before the wait is reported */
#define EXP_RESOURCE_WAIT_INTERVAL (-10 * 1000 * 1000 * 4)

/* A brief pause taken when a table has to be grown while the lock is dropped */
#define EXP_RESOURCE_SHORT_INTERVAL (-10 * 1000)

/* The number of owner slots the first table carries */
#define EXP_RESOURCE_INITIAL_ENTRIES 3

/* The owner slots a grown table adds */
#define EXP_RESOURCE_ENTRY_GROWTH 4

/*
 * An owner is named either by its thread or, once a caller has handed the
 * resource over with ExSetResourceOwnerPointer, by a pointer of its own. A
 * pointer owner carries both low bits, which no thread ever does.
 */
#define EXP_RESOURCE_OWNER_POINTER_BITS 3

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
ExpVerifyResource(
    _In_ PERESOURCE Resource)
{
    ASSERT(Resource != NULL);
    ASSERT((((ULONG_PTR)Resource) & (sizeof(ULONG_PTR) - 1)) == 0);
    ASSERT(KeIsExecutingDpc() == FALSE);
}

static
VOID
ExpCheckForApcsDisabled(
    _In_ BOOLEAN Wait,
    _In_ PERESOURCE Resource,
    _In_ PKTHREAD Thread)
{
    if (!Wait || (KeGetCurrentIrql() == APC_LEVEL) ||
        (Thread->CombinedApcDisable != 0))
    {
        return;
    }

    if (ExResourceStrict)
    {
        DPRINT1("Resource %p acquired with APCs enabled by thread %p\n",
                Resource, Thread);
    }
}

static
ULONG
ExpOwnerTableSize(
    _In_ PERESOURCE Resource)
{
    if (Resource->OwnerTable == NULL)
    {
        return 0;
    }

    return Resource->OwnerTable[0].TableSize;
}

/*
 * The resource names its first owner in the slot it carries itself, so a lock
 * one thread at a time holds never needs a table.
 */
static
POWNER_ENTRY
ExpFindEntryForThread(
    _In_ PERESOURCE Resource,
    _In_ ERESOURCE_THREAD Thread)
{
    ULONG Size = ExpOwnerTableSize(Resource);
    ULONG Index;

    if (Resource->OwnerEntry.OwnerThread == Thread)
    {
        return &Resource->OwnerEntry;
    }

    for (Index = 1; Index < Size; Index++)
    {
        if (Resource->OwnerTable[Index].OwnerThread == Thread)
        {
            return &Resource->OwnerTable[Index];
        }
    }

    return NULL;
}

static
POWNER_ENTRY
ExpFindFreeEntry(
    _In_ PERESOURCE Resource)
{
    ULONG Size = ExpOwnerTableSize(Resource);
    ULONG Index;

    if (Resource->OwnerEntry.OwnerThread == 0)
    {
        return &Resource->OwnerEntry;
    }

    for (Index = 1; Index < Size; Index++)
    {
        if (Resource->OwnerTable[Index].OwnerThread == 0)
        {
            return &Resource->OwnerTable[Index];
        }
    }

    return NULL;
}

/*
 * Grows the owner table. The spin lock has to be dropped to touch the pool, so
 * the caller is told to look for its slot again once the lock is back.
 */
static
BOOLEAN
ExpExpandResourceOwnerTable(
    _Inout_ PERESOURCE Resource,
    _Inout_ PKIRQL OldIrql)
{
    POWNER_ENTRY Table, OldTable;
    ULONG OldSize, NewSize;

    OldSize = ExpOwnerTableSize(Resource);
    NewSize = (OldSize == 0) ? (EXP_RESOURCE_INITIAL_ENTRIES + 1) :
                               (OldSize + EXP_RESOURCE_ENTRY_GROWTH);

    KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);

    Table = ExAllocatePoolZero(NonPagedPool,
                               NewSize * sizeof(OWNER_ENTRY),
                               EXP_RESOURCE_TAG);

    KeAcquireSpinLock(&Resource->SpinLock, OldIrql);

    if (Table == NULL)
    {
        return FALSE;
    }

    /* Another thread grew the table while the lock was down. */
    if (ExpOwnerTableSize(Resource) >= NewSize)
    {
        KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);
        ExFreePoolWithTag(Table, EXP_RESOURCE_TAG);
        KeAcquireSpinLock(&Resource->SpinLock, OldIrql);
        return TRUE;
    }

    OldTable = Resource->OwnerTable;
    OldSize = ExpOwnerTableSize(Resource);

    if (OldTable != NULL)
    {
        RtlCopyMemory(Table, OldTable, OldSize * sizeof(OWNER_ENTRY));
    }

    Table[0].OwnerThread = 0;
    Table[0].TableSize = NewSize;
    Resource->OwnerTable = Table;

    if (OldTable != NULL)
    {
        KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);
        ExFreePoolWithTag(OldTable, EXP_RESOURCE_TAG);
        KeAcquireSpinLock(&Resource->SpinLock, OldIrql);
    }

    return TRUE;
}

static
PKEVENT
ExpAllocateExclusiveWaiterEvent(
    _Inout_ PERESOURCE Resource,
    _Inout_ PKIRQL OldIrql)
{
    PKEVENT Event;

    KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);

    Event = ExAllocatePoolZero(NonPagedPool, sizeof(KEVENT), EXP_RESOURCE_TAG);
    if (Event != NULL)
    {
        KeInitializeEvent(Event, SynchronizationEvent, FALSE);
    }

    KeAcquireSpinLock(&Resource->SpinLock, OldIrql);

    if (Event == NULL)
    {
        return NULL;
    }

    if (Resource->ExclusiveWaiters != NULL)
    {
        KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);
        ExFreePoolWithTag(Event, EXP_RESOURCE_TAG);
        KeAcquireSpinLock(&Resource->SpinLock, OldIrql);
        return Resource->ExclusiveWaiters;
    }

    Resource->ExclusiveWaiters = Event;
    return Event;
}

static
PKSEMAPHORE
ExpAllocateSharedWaiterSemaphore(
    _Inout_ PERESOURCE Resource,
    _Inout_ PKIRQL OldIrql)
{
    PKSEMAPHORE Semaphore;

    KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);

    Semaphore = ExAllocatePoolZero(NonPagedPool,
                                   sizeof(KSEMAPHORE),
                                   EXP_RESOURCE_TAG);
    if (Semaphore != NULL)
    {
        KeInitializeSemaphore(Semaphore, 0, MAXLONG);
    }

    KeAcquireSpinLock(&Resource->SpinLock, OldIrql);

    if (Semaphore == NULL)
    {
        return NULL;
    }

    if (Resource->SharedWaiters != NULL)
    {
        KeReleaseSpinLock(&Resource->SpinLock, *OldIrql);
        ExFreePoolWithTag(Semaphore, EXP_RESOURCE_TAG);
        KeAcquireSpinLock(&Resource->SpinLock, OldIrql);
        return Resource->SharedWaiters;
    }

    Resource->SharedWaiters = Semaphore;
    return Semaphore;
}

/*
 * The resource remembers the keenest priority waiting on it, so an owner that
 * is holding a high-priority thread up can be lifted to its level.
 */
static
VOID
ExpRecordWaiterPriority(
    _Inout_ PERESOURCE Resource)
{
    KPRIORITY Priority = KeGetCurrentThread()->Priority;

    if ((Priority > 0) && (Priority < LOW_REALTIME_PRIORITY) &&
        ((UCHAR)Priority > (UCHAR)(Resource->Flag >> 8)))
    {
        Resource->Flag = (USHORT)((Resource->Flag & 0x00FF) |
                                  ((USHORT)(UCHAR)Priority << 8));
    }
}

static
VOID
ExpBoostOwnerThread(
    _In_ PERESOURCE Resource,
    _In_ PKTHREAD Owner)
{
    if ((Owner == NULL) ||
        ((ULONG_PTR)Owner & EXP_RESOURCE_OWNER_POINTER_BITS))
    {
        return;
    }

    if (Owner->Priority >= LOW_REALTIME_PRIORITY)
    {
        return;
    }

    if (Owner->Priority >= (SCHAR)(Resource->Flag >> 8))
    {
        return;
    }

    KeBoostPriorityThread(Owner, (KPRIORITY)(Resource->Flag >> 8));
}

static
VOID
ExpWaitForResource(
    _Inout_ PERESOURCE Resource,
    _In_ PVOID Object)
{
    NTSTATUS Status;
    ULONG Pass = 0;
    KIRQL OldIrql;

    InterlockedIncrement((PLONG)&Resource->ContentionCount);

    for (;;)
    {
        Status = KeWaitForSingleObject(Object,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &ExpTimeout);
        if (Status != STATUS_TIMEOUT)
        {
            return;
        }

        Pass++;

        if (Pass >= ExpResourceTimeoutCount)
        {
            DPRINT1("Thread %p still waiting for resource %p\n",
                    KeGetCurrentThread(), Resource);
            Pass = 0;
        }

        KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);
        ExpBoostOwnerThread(Resource,
                            (PKTHREAD)Resource->OwnerEntry.OwnerThread);
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
    }
}

/*
 * Hands the resource to whoever is next in line. A woken exclusive thread finds
 * the resource already marked as its own and only has to write its name in;
 * woken readers each take a slot of their own.
 */
static
VOID
ExpHandOverResource(
    _Inout_ PERESOURCE Resource,
    _In_ KIRQL OldIrql)
{
    ULONG Count;

    if (Resource->NumberOfExclusiveWaiters != 0)
    {
        Resource->Flag |= ResourceOwnedExclusive;
        Resource->OwnerEntry.OwnerThread = 0;
        Resource->OwnerEntry.OwnerCount = 1;
        Resource->ActiveCount = 1;
        Resource->NumberOfExclusiveWaiters--;

        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        KeSetEventBoostPriority(Resource->ExclusiveWaiters, NULL);
        return;
    }

    if (Resource->NumberOfSharedWaiters != 0)
    {
        Count = Resource->NumberOfSharedWaiters;

        Resource->Flag &= ~ResourceOwnedExclusive;
        Resource->ActiveCount = (SHORT)Count;
        Resource->NumberOfSharedWaiters = 0;

        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        KeReleaseSemaphore(Resource->SharedWaiters,
                           IO_NO_INCREMENT,
                           Count,
                           FALSE);
        return;
    }

    Resource->Flag &= ~ResourceOwnedExclusive;
    Resource->ActiveCount = 0;
    Resource->ActiveEntries = 0;

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
}

/*
 * A reader that was woken by a hand-over has already been counted in
 * ActiveCount, so it only claims a slot.
 */
static
VOID
ExpClaimSharedEntry(
    _Inout_ PERESOURCE Resource,
    _In_ ERESOURCE_THREAD Thread)
{
    POWNER_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    for (;;)
    {
        Entry = ExpFindEntryForThread(Resource, Thread);
        if (Entry != NULL)
        {
            Entry->OwnerCount++;
            break;
        }

        Entry = ExpFindFreeEntry(Resource);
        if (Entry != NULL)
        {
            Entry->OwnerThread = Thread;
            Entry->OwnerCount = 1;
            Resource->ActiveEntries++;
            break;
        }

        if (!ExpExpandResourceOwnerTable(Resource, &OldIrql))
        {
            /* FIXME: no slot can be recorded, so the reader is counted in
               ActiveCount without a name; its release cannot find it. */
            DPRINT1("Resource %p has no room for a shared owner\n", Resource);
            break;
        }
    }

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
}

static
BOOLEAN
ExpAcquireResourceShared(
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN Wait,
    _In_ BOOLEAN StarveExclusive)
{
    ERESOURCE_THREAD Thread = (ERESOURCE_THREAD)KeGetCurrentThread();
    POWNER_ENTRY Entry;
    PKSEMAPHORE Semaphore;
    KIRQL OldIrql;

    ExpVerifyResource(Resource);
    ExpCheckForApcsDisabled(Wait, Resource, KeGetCurrentThread());

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    for (;;)
    {
        /* A thread that already holds it, either way, just goes deeper. */
        Entry = ExpFindEntryForThread(Resource, Thread);
        if (Entry != NULL)
        {
            Entry->OwnerCount++;
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return TRUE;
        }

        if (Resource->ActiveCount == 0)
        {
            Resource->Flag &= ~ResourceOwnedExclusive;
            Resource->OwnerEntry.OwnerThread = Thread;
            Resource->OwnerEntry.OwnerCount = 1;
            Resource->ActiveCount = 1;
            Resource->ActiveEntries = 1;
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return TRUE;
        }

        if (!(Resource->Flag & ResourceOwnedExclusive) &&
            (StarveExclusive || (Resource->NumberOfExclusiveWaiters == 0)))
        {
            Entry = ExpFindFreeEntry(Resource);
            if (Entry == NULL)
            {
                if (!ExpExpandResourceOwnerTable(Resource, &OldIrql))
                {
                    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
                    ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
                }

                continue;
            }

            Entry->OwnerThread = Thread;
            Entry->OwnerCount = 1;
            Resource->ActiveCount++;
            Resource->ActiveEntries++;
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return TRUE;
        }

        if (!Wait)
        {
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return FALSE;
        }

        Semaphore = Resource->SharedWaiters;
        if (Semaphore == NULL)
        {
            Semaphore = ExpAllocateSharedWaiterSemaphore(Resource, &OldIrql);
            if (Semaphore == NULL)
            {
                KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
                ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
            }

            continue;
        }

        Resource->NumberOfSharedWaiters++;
        ExpRecordWaiterPriority(Resource);
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);

        ExpWaitForResource(Resource, Semaphore);
        ExpClaimSharedEntry(Resource, Thread);
        return TRUE;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExpResourceInitialization(VOID)
{
    ExpTimeout.QuadPart = EXP_RESOURCE_WAIT_INTERVAL;
    ExShortTime.QuadPart = EXP_RESOURCE_SHORT_INTERVAL;

    KeInitializeSpinLock(&ExpResourceSpinLock);
    InitializeListHead(&ExpSystemResourcesList);
}

NTSTATUS
NTAPI
ExInitializeResourceLite(
    _Out_ PERESOURCE Resource)
{
    KIRQL OldIrql;

    RtlZeroMemory(Resource, sizeof(ERESOURCE));

    KeInitializeSpinLock(&Resource->SpinLock);

    KeAcquireSpinLock(&ExpResourceSpinLock, &OldIrql);
    InsertTailList(&ExpSystemResourcesList, &Resource->SystemResourcesList);
    KeReleaseSpinLock(&ExpResourceSpinLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExReinitializeResourceLite(
    _Inout_ PERESOURCE Resource)
{
    PKEVENT Event = Resource->ExclusiveWaiters;
    PKSEMAPHORE Semaphore = Resource->SharedWaiters;
    POWNER_ENTRY Table = Resource->OwnerTable;

    Resource->ExclusiveWaiters = NULL;
    Resource->SharedWaiters = NULL;
    Resource->OwnerTable = NULL;
    Resource->ActiveCount = 0;
    Resource->ActiveEntries = 0;
    Resource->Flag = 0;
    Resource->ContentionCount = 0;
    Resource->NumberOfSharedWaiters = 0;
    Resource->NumberOfExclusiveWaiters = 0;
    Resource->OwnerEntry.OwnerThread = 0;
    Resource->OwnerEntry.TableSize = 0;

    if (Table != NULL)
    {
        ExFreePoolWithTag(Table, EXP_RESOURCE_TAG);
    }

    if (Event != NULL)
    {
        KeInitializeEvent(Event, SynchronizationEvent, FALSE);
        Resource->ExclusiveWaiters = Event;
    }

    if (Semaphore != NULL)
    {
        KeInitializeSemaphore(Semaphore, 0, MAXLONG);
        Resource->SharedWaiters = Semaphore;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExDeleteResourceLite(
    _Inout_ PERESOURCE Resource)
{
    KIRQL OldIrql;

    ASSERT(Resource->ActiveCount == 0);
    ASSERT(Resource->NumberOfSharedWaiters == 0);
    ASSERT(Resource->NumberOfExclusiveWaiters == 0);

    KeAcquireSpinLock(&ExpResourceSpinLock, &OldIrql);
    RemoveEntryList(&Resource->SystemResourcesList);
    KeReleaseSpinLock(&ExpResourceSpinLock, OldIrql);

    if (Resource->OwnerTable != NULL)
    {
        ExFreePoolWithTag(Resource->OwnerTable, EXP_RESOURCE_TAG);
        Resource->OwnerTable = NULL;
    }

    if (Resource->ExclusiveWaiters != NULL)
    {
        ExFreePoolWithTag(Resource->ExclusiveWaiters, EXP_RESOURCE_TAG);
        Resource->ExclusiveWaiters = NULL;
    }

    if (Resource->SharedWaiters != NULL)
    {
        ExFreePoolWithTag(Resource->SharedWaiters, EXP_RESOURCE_TAG);
        Resource->SharedWaiters = NULL;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
ExAcquireResourceExclusiveLite(
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN Wait)
{
    ERESOURCE_THREAD Thread = (ERESOURCE_THREAD)KeGetCurrentThread();
    PKEVENT Event;
    KIRQL OldIrql;

    ExpVerifyResource(Resource);
    ASSERT(!(Resource->Flag & ResourceNeverExclusive));
    ExpCheckForApcsDisabled(Wait, Resource, KeGetCurrentThread());

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    for (;;)
    {
        if (Resource->ActiveCount == 0)
        {
            Resource->Flag |= ResourceOwnedExclusive;
            Resource->OwnerEntry.OwnerThread = Thread;
            Resource->OwnerEntry.OwnerCount = 1;
            Resource->ActiveCount = 1;
            Resource->ActiveEntries = 1;
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return TRUE;
        }

        if ((Resource->Flag & ResourceOwnedExclusive) &&
            (Resource->OwnerEntry.OwnerThread == Thread))
        {
            Resource->OwnerEntry.OwnerCount++;
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return TRUE;
        }

        if (!Wait)
        {
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return FALSE;
        }

        Event = Resource->ExclusiveWaiters;
        if (Event == NULL)
        {
            Event = ExpAllocateExclusiveWaiterEvent(Resource, &OldIrql);
            if (Event == NULL)
            {
                KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
                ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
            }

            continue;
        }

        Resource->NumberOfExclusiveWaiters++;
        ExpRecordWaiterPriority(Resource);
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);

        ExpWaitForResource(Resource, Event);

        /* The hand-over left the resource owned; only the name is missing. */
        Resource->OwnerEntry.OwnerThread = Thread;
        return TRUE;
    }
}

BOOLEAN
NTAPI
ExTryToAcquireResourceExclusiveLite(
    _Inout_ PERESOURCE Resource)
{
    return ExAcquireResourceExclusiveLite(Resource, FALSE);
}

BOOLEAN
NTAPI
ExAcquireResourceSharedLite(
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN Wait)
{
    return ExpAcquireResourceShared(Resource, Wait, FALSE);
}

BOOLEAN
NTAPI
ExAcquireSharedStarveExclusive(
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN Wait)
{
    return ExpAcquireResourceShared(Resource, Wait, TRUE);
}

BOOLEAN
NTAPI
ExAcquireSharedWaitForExclusive(
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN Wait)
{
    return ExpAcquireResourceShared(Resource, Wait, FALSE);
}

VOID
NTAPI
ExReleaseResourceForThreadLite(
    _Inout_ PERESOURCE Resource,
    _In_ ERESOURCE_THREAD Thread)
{
    POWNER_ENTRY Entry;
    KIRQL OldIrql;

    ASSERT(Thread != 0);

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    if (Resource->Flag & ResourceOwnedExclusive)
    {
        ASSERT(Resource->OwnerEntry.OwnerThread == Thread);
        ASSERT(Resource->OwnerEntry.OwnerCount > 0);

        if (--Resource->OwnerEntry.OwnerCount != 0)
        {
            KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
            return;
        }

        Resource->OwnerEntry.OwnerThread = 0;
        Resource->ActiveCount = 0;
        Resource->ActiveEntries = 0;

        ExpHandOverResource(Resource, OldIrql);
        return;
    }

    Entry = ExpFindEntryForThread(Resource, Thread);
    if (Entry == NULL)
    {
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        KeBugCheckEx(RESOURCE_NOT_OWNED,
                     (ULONG_PTR)Resource,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Resource->OwnerTable,
                     0);
    }

    ASSERT(Entry->OwnerCount > 0);

    if (--Entry->OwnerCount != 0)
    {
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        return;
    }

    Entry->OwnerThread = 0;
    Resource->ActiveEntries--;

    ASSERT(Resource->ActiveCount > 0);

    if (--Resource->ActiveCount != 0)
    {
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        return;
    }

    ExpHandOverResource(Resource, OldIrql);
}

VOID
FASTCALL
ExReleaseResourceLite(
    _Inout_ PERESOURCE Resource)
{
    ExReleaseResourceForThreadLite(Resource,
                                   (ERESOURCE_THREAD)KeGetCurrentThread());
}

VOID
NTAPI
ExConvertExclusiveToSharedLite(
    _Inout_ PERESOURCE Resource)
{
    ULONG Count;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    ASSERT(Resource->Flag & ResourceOwnedExclusive);
    ASSERT(Resource->OwnerEntry.OwnerThread ==
           (ERESOURCE_THREAD)KeGetCurrentThread());

    Resource->Flag &= ~ResourceOwnedExclusive;

    Count = Resource->NumberOfSharedWaiters;
    if (Count == 0)
    {
        KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
        return;
    }

    Resource->ActiveCount = (SHORT)(Resource->ActiveCount + Count);
    Resource->NumberOfSharedWaiters = 0;

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);

    KeReleaseSemaphore(Resource->SharedWaiters, IO_NO_INCREMENT, Count, FALSE);
}

/*
 * Renames the owner, so a thread other than the one that took the resource can
 * give it back. The name a caller hands in carries both low bits, which tells it
 * apart from any thread.
 */
VOID
NTAPI
ExSetResourceOwnerPointer(
    _Inout_ PERESOURCE Resource,
    _In_ PVOID OwnerPointer)
{
    ERESOURCE_THREAD Thread = (ERESOURCE_THREAD)KeGetCurrentThread();
    POWNER_ENTRY Entry;
    KIRQL OldIrql;

    ASSERT(OwnerPointer != NULL);
    ASSERT(((ULONG_PTR)OwnerPointer & EXP_RESOURCE_OWNER_POINTER_BITS) ==
           EXP_RESOURCE_OWNER_POINTER_BITS);

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    Entry = ExpFindEntryForThread(Resource, Thread);
    if (Entry != NULL)
    {
        Entry->OwnerThread = (ERESOURCE_THREAD)OwnerPointer;
        Resource->Flag |= ResourceReleaseByOtherThread;
    }

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);
}

VOID
NTAPI
ExSetResourceOwnerPointerEx(
    _Inout_ PERESOURCE Resource,
    _In_ PVOID OwnerPointer,
    _In_ ULONG Flags)
{
    /* TODO: the flags of this routine are not published; none is honoured. */
    UNREFERENCED_PARAMETER(Flags);

    ExSetResourceOwnerPointer(Resource, OwnerPointer);
}

BOOLEAN
NTAPI
ExIsResourceAcquiredExclusiveLite(
    _In_ PERESOURCE Resource)
{
    BOOLEAN Owned;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    Owned = (Resource->Flag & ResourceOwnedExclusive) &&
            (Resource->OwnerEntry.OwnerThread ==
             (ERESOURCE_THREAD)KeGetCurrentThread());

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);

    return Owned;
}

ULONG
NTAPI
ExIsResourceAcquiredSharedLite(
    _In_ PERESOURCE Resource)
{
    POWNER_ENTRY Entry;
    ULONG Count = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Resource->SpinLock, &OldIrql);

    Entry = ExpFindEntryForThread(Resource,
                                  (ERESOURCE_THREAD)KeGetCurrentThread());
    if (Entry != NULL)
    {
        Count = Entry->OwnerCount;
    }

    KeReleaseSpinLock(&Resource->SpinLock, OldIrql);

    return Count;
}

ULONG
NTAPI
ExGetExclusiveWaiterCount(
    _In_ PERESOURCE Resource)
{
    return Resource->NumberOfExclusiveWaiters;
}

ULONG
NTAPI
ExGetSharedWaiterCount(
    _In_ PERESOURCE Resource)
{
    return Resource->NumberOfSharedWaiters;
}

VOID
NTAPI
ExDisableResourceBoostLite(
    _In_ PERESOURCE Resource)
{
    /* TODO: no published field of ERESOURCE records this, so the request is
       dropped and waiters keep boosting the owner. */
    UNREFERENCED_PARAMETER(Resource);
}

PVOID
NTAPI
ExEnterCriticalRegionAndAcquireResourceExclusive(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(Resource, TRUE);
    return Resource;
}

PVOID
NTAPI
ExEnterCriticalRegionAndAcquireResourceShared(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);
    return Resource;
}

PVOID
NTAPI
ExEnterCriticalRegionAndAcquireSharedWaitForExclusive(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireSharedWaitForExclusive(Resource, TRUE);
    return Resource;
}

VOID
FASTCALL
ExReleaseResourceAndLeaveCriticalRegion(
    _Inout_ PERESOURCE Resource)
{
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();
}

/* TODO: the priority region is not published, so a critical region is entered
   in its place and the caller's priority is left alone. */
VOID
FASTCALL
ExpEnterPriorityRegionAndAcquireResourceShared(
    _Inout_ PERESOURCE Resource)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);
}

/* EOF */
