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
    OwnerShared
} OWNER_KIND;

typedef struct _BOOST_OWNER
{
    PERESOURCE Resource;
    OWNER_KIND Kind;
    KPRIORITY Priority;
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

static
VOID
TestBoost(
    _In_ PCSTR Name,
    _In_ OWNER_KIND Kind,
    _In_ ULONG OwnerCount,
    _In_ BOOLEAN DisableBoost,
    _In_ KPRIORITY Expected)
{
    BOOST_OWNER Owners[MAX_OWNERS];
    BOOST_WAITER Waiter;
    LARGE_INTEGER Interval;
    LARGE_INTEGER Timeout;
    KPRIORITY Seen[MAX_OWNERS];
    PERESOURCE Resource;
    NTSTATUS Status;
    ULONG Started = 0;
    ULONG Index;

    Resource = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Resource), 'tsoB');
    if (skip(Resource != NULL, "%s: no pool\n", Name))
        return;

    ExInitializeResourceLite(Resource);
    if (DisableBoost)
        ExDisableResourceBoostLite(Resource);

    for (Index = 0; Index < OwnerCount; Index++)
    {
        PBOOST_OWNER Owner = &Owners[Index];

        RtlZeroMemory(Owner, sizeof(*Owner));
        Owner->Resource = Resource;
        Owner->Kind = Kind;
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

        Started++;
    }

    RtlZeroMemory(&Waiter, sizeof(Waiter));
    Waiter.Resource = Resource;
    Status = StartSystemThread(WaiterThread, &Waiter, &Waiter.Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Interval.QuadPart = -10 * 1000 * 1500;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);

    for (Index = 0; Index < Started; Index++)
        Seen[Index] = (Owners[Index].Thread != NULL) ? KeQueryPriorityThread(Owners[Index].Thread) : -1;

    for (Index = 0; Index < Started; Index++)
        KeSetEvent(&Owners[Index].Release, IO_NO_INCREMENT, FALSE);

    for (Index = 0; Index < Started; Index++)
    {
        JoinSystemThread(Owners[Index].Handle);
        if (Owners[Index].Thread != NULL)
            ObDereferenceObject(Owners[Index].Thread);
    }

    if (NT_SUCCESS(Status))
        JoinSystemThread(Waiter.Handle);

    for (Index = 0; Index < Started; Index++)
    {
        ok(Seen[Index] == ((Expected < 0) ? Owners[Index].Priority : Expected),
           "%s: owner %lu at priority %ld, expected %ld\n", Name, Index, Seen[Index],
           (Expected < 0) ? Owners[Index].Priority : Expected);
    }

    ExDeleteResourceLite(Resource);
    ExFreePoolWithTag(Resource, 'tsoB');
}

START_TEST(ExResourceBoost)
{
    TestBoost("exclusive owner", OwnerExclusive, 1, FALSE, BOOSTED_PRIORITY);
    TestBoost("boost disabled", OwnerExclusive, 1, TRUE, -1);
    TestBoost("shared owners", OwnerShared, 2, FALSE, BOOSTED_PRIORITY);
}
