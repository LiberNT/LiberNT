/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Priority boost of executive resource owners by their waiters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define OWNER_PRIORITY 6
#define WAITER_PRIORITY 12
#define BOOSTED_PRIORITY (LOW_REALTIME_PRIORITY - 1)

#define MAX_OWNERS 2

typedef enum _OWNER_KIND
{
    OwnerExclusive,
    OwnerShared,
    OwnerPointerThread,
    OwnerPointerStorage
} OWNER_KIND;

typedef struct _BOOST_OWNER
{
    PERESOURCE Resource;
    OWNER_KIND Kind;
    KPRIORITY Priority;
    ULONG_PTR Storage;
    KEVENT Acquired;
    KEVENT Release;
    HANDLE Handle;
    PKTHREAD Thread;
} BOOST_OWNER, *PBOOST_OWNER;

typedef struct _BOOST_WAITER
{
    PERESOURCE Resource;
    HANDLE Handle;
} BOOST_WAITER, *PBOOST_WAITER;

static
VOID
NTAPI
OwnerThread(
    _In_ PVOID Context)
{
    PBOOST_OWNER Owner = Context;
    ERESOURCE_THREAD Name = (ERESOURCE_THREAD)KeGetCurrentThread();
    LARGE_INTEGER Timeout;

    KeSetPriorityThread(KeGetCurrentThread(), Owner->Priority);

    KeEnterCriticalRegion();

    if (Owner->Kind == OwnerShared)
        ExAcquireResourceSharedLite(Owner->Resource, TRUE);
    else
        ExAcquireResourceExclusiveLite(Owner->Resource, TRUE);

    if (Owner->Kind == OwnerPointerThread)
    {
        Name |= 3;
        ExSetResourceOwnerPointerEx(Owner->Resource, (PVOID)Name, FLAG_OWNER_POINTER_IS_THREAD);
    }
    else if (Owner->Kind == OwnerPointerStorage)
    {
        Name = (ERESOURCE_THREAD)&Owner->Storage | 3;
        ExSetResourceOwnerPointerEx(Owner->Resource, (PVOID)Name, 0);
    }

    KeSetEvent(&Owner->Acquired, IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10LL;
    KeWaitForSingleObject(&Owner->Release, Executive, KernelMode, FALSE, &Timeout);

    ExReleaseResourceForThreadLite(Owner->Resource, Name);

    KeLeaveCriticalRegion();

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
WaiterThread(
    _In_ PVOID Context)
{
    PBOOST_WAITER Waiter = Context;

    KeSetPriorityThread(KeGetCurrentThread(), WAITER_PRIORITY);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(Waiter->Resource, TRUE);
    ExReleaseResourceLite(Waiter->Resource);
    KeLeaveCriticalRegion();

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
StartSystemThread(
    _In_ PKSTART_ROUTINE Routine,
    _In_ PVOID Context,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    return PsCreateSystemThread(Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, Routine, Context);
}

static
VOID
JoinSystemThread(
    _In_ HANDLE Handle)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -10 * 1000 * 1000 * 15LL;
    Status = ZwWaitForSingleObject(Handle, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Handle);
}

typedef struct _BOOST_CASE
{
    PCSTR Name;
    OWNER_KIND Kind;
    ULONG OwnerCount;
    BOOLEAN DisableBoost;
    KPRIORITY Expected;
    PERESOURCE Resource;
    BOOST_OWNER Owners[MAX_OWNERS];
    BOOST_WAITER Waiter;
    KPRIORITY Seen[MAX_OWNERS];
    ULONG Started;
    BOOLEAN WaiterStarted;
} BOOST_CASE, *PBOOST_CASE;

static
VOID
StartCase(
    _Inout_ PBOOST_CASE Case)
{
    LARGE_INTEGER Interval;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Index;
    ULONG Spins;

    Case->Resource = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Case->Resource), 'tsoB');
    if (skip(Case->Resource != NULL, "%s: no pool\n", Case->Name))
        return;

    ExInitializeResourceLite(Case->Resource);
    if (Case->DisableBoost)
        ExDisableResourceBoostLite(Case->Resource);

    for (Index = 0; Index < Case->OwnerCount; Index++)
    {
        PBOOST_OWNER Owner = &Case->Owners[Index];

        RtlZeroMemory(Owner, sizeof(*Owner));
        Owner->Resource = Case->Resource;
        Owner->Kind = Case->Kind;
        Owner->Priority = OWNER_PRIORITY + (KPRIORITY)Index;
        KeInitializeEvent(&Owner->Acquired, NotificationEvent, FALSE);
        KeInitializeEvent(&Owner->Release, NotificationEvent, FALSE);

        Status = StartSystemThread(OwnerThread, Owner, &Owner->Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            break;

        Status = ObReferenceObjectByHandle(Owner->Handle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Owner->Thread, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Timeout.QuadPart = -10 * 1000 * 1000 * 5LL;
        Status = KeWaitForSingleObject(&Owner->Acquired, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Case->Started++;
    }

    RtlZeroMemory(&Case->Waiter, sizeof(Case->Waiter));
    Case->Waiter.Resource = Case->Resource;
    Status = StartSystemThread(WaiterThread, &Case->Waiter, &Case->Waiter.Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Case->WaiterStarted = TRUE;

    Interval.QuadPart = -10 * 1000;
    for (Spins = 0; (Spins < 1000) && (ExGetExclusiveWaiterCount(Case->Resource) == 0); Spins++)
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    ok_eq_ulong(ExGetExclusiveWaiterCount(Case->Resource), 1UL);
}

static
BOOLEAN
CaseBoosted(
    _In_ PBOOST_CASE Case)
{
    ULONG Index;

    if (Case->Expected < 0)
        return TRUE;

    for (Index = 0; Index < Case->Started; Index++)
    {
        if ((Case->Owners[Index].Thread == NULL) || (KeQueryPriorityThread(Case->Owners[Index].Thread) != Case->Expected))
            return FALSE;
    }

    return TRUE;
}

static
VOID
FinishCase(
    _Inout_ PBOOST_CASE Case)
{
    ULONG Index;

    if (Case->Resource == NULL)
        return;

    for (Index = 0; Index < Case->Started; Index++)
        Case->Seen[Index] = (Case->Owners[Index].Thread != NULL) ? KeQueryPriorityThread(Case->Owners[Index].Thread) : -1;

    for (Index = 0; Index < Case->Started; Index++)
        KeSetEvent(&Case->Owners[Index].Release, IO_NO_INCREMENT, FALSE);

    for (Index = 0; Index < Case->Started; Index++)
    {
        JoinSystemThread(Case->Owners[Index].Handle);
        if (Case->Owners[Index].Thread != NULL)
            ObDereferenceObject(Case->Owners[Index].Thread);
    }

    if (Case->WaiterStarted)
        JoinSystemThread(Case->Waiter.Handle);

    for (Index = 0; Index < Case->Started; Index++)
    {
        ok(Case->Seen[Index] == ((Case->Expected < 0) ? Case->Owners[Index].Priority : Case->Expected),
           "%s: owner %lu at priority %ld, expected %ld\n", Case->Name, Index, Case->Seen[Index],
           (Case->Expected < 0) ? Case->Owners[Index].Priority : Case->Expected);
    }

    ExDeleteResourceLite(Case->Resource);
    ExFreePoolWithTag(Case->Resource, 'tsoB');
}

/* The last case boosts: once it has, every earlier waiter has also ended its first 500 ms pass */
static
VOID
RunCases(
    _Inout_updates_(Count) PBOOST_CASE Cases,
    _In_ ULONG Count)
{
    LARGE_INTEGER Interval;
    ULONGLONG Deadline;
    BOOLEAN Boosted;
    ULONG Index;

    ASSERT(Cases[Count - 1].Expected >= 0);

    for (Index = 0; Index < Count; Index++)
        StartCase(&Cases[Index]);

    Interval.QuadPart = -10 * 1000;
    Deadline = KeQueryInterruptTime() + 2ULL * 10 * 1000 * 1000;
    do
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        Boosted = TRUE;
        for (Index = 0; Index < Count; Index++)
            Boosted = Boosted && CaseBoosted(&Cases[Index]);
    } while (!Boosted && (KeQueryInterruptTime() < Deadline));

    for (Index = 0; Index < Count; Index++)
        FinishCase(&Cases[Index]);
}

START_TEST(ExResourceBoost)
{
    BOOST_CASE Cases[] =
    {
        { "boost disabled", OwnerExclusive, 1, TRUE, -1 },
        { "shared owners", OwnerShared, 2, FALSE, BOOSTED_PRIORITY },
        { "exclusive owner", OwnerExclusive, 1, FALSE, BOOSTED_PRIORITY },
    };

    RunCases(Cases, RTL_NUMBER_OF(Cases));
}

START_TEST(ExResourceBoostOwnerPointer)
{
    BOOST_CASE Cases[] =
    {
        { "owner pointer naming storage", OwnerPointerStorage, 1, FALSE, -1 },
        { "owner pointer naming a thread", OwnerPointerThread, 1, FALSE, BOOSTED_PRIORITY },
    };

    RunCases(Cases, RTL_NUMBER_OF(Cases));
}
