/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Idle processors take ready threads that are still leaving another processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define STEAL_MAX_THREADS 64
#define STEAL_RUN_MS 1000

typedef struct _STEAL_RING
{
    KEVENT Turn[STEAL_MAX_THREADS];
    ULONG Threads;
    volatile LONG Stop;
} STEAL_RING, *PSTEAL_RING;

typedef struct _STEAL_MEMBER
{
    PSTEAL_RING Ring;
    ULONG Index;
    LONG64 Hops;
    NTSTATUS Status;
} STEAL_MEMBER, *PSTEAL_MEMBER;

static
VOID
NTAPI
StealMember(
    _In_ PVOID Parameter)
{
    PSTEAL_MEMBER Member = Parameter;
    PSTEAL_RING Ring = Member->Ring;
    ULONG Next = (Member->Index + 1) % Ring->Threads;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Ring->Turn[Member->Index], Executive, KernelMode, FALSE, &Timeout);
    while (Status == STATUS_SUCCESS && InterlockedCompareExchange(&Ring->Stop, 0, 0) == 0)
    {
        Member->Hops++;
        KeSetEvent(&Ring->Turn[Next], IO_NO_INCREMENT, TRUE);
        Status = KeWaitForSingleObject(&Ring->Turn[Member->Index], Executive, KernelMode, FALSE, &Timeout);
    }
    Member->Status = Status;

    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeIdleSteal)
{
    PSTEAL_RING Ring;
    PSTEAL_MEMBER Members;
    PVOID Threads[STEAL_MAX_THREADS];
    LARGE_INTEGER Delay, Timeout;
    ULONG Count, Index, Tokens, Joined = 0, Created = 0;
    LONG64 Hops = 0;
    HANDLE Handle;
    NTSTATUS Status;

    if (skip(KeNumberProcessors >= 2, "One processor: no ready thread is taken by another\n"))
        return;

    Count = min(4 * (ULONG)KeNumberProcessors, (ULONG)STEAL_MAX_THREADS);
    Ring = ExAllocatePoolZero(NonPagedPool, sizeof(*Ring), 'lSmK');
    Members = ExAllocatePoolZero(NonPagedPool, Count * sizeof(*Members), 'lSmK');
    ok(Ring != NULL && Members != NULL, "no memory for the ring\n");
    if (Ring == NULL || Members == NULL)
    {
        if (Ring != NULL) ExFreePoolWithTag(Ring, 'lSmK');
        if (Members != NULL) ExFreePoolWithTag(Members, 'lSmK');
        return;
    }

    Ring->Threads = Count;
    for (Index = 0; Index < Count; Index++)
    {
        KeInitializeEvent(&Ring->Turn[Index], SynchronizationEvent, FALSE);
        Members[Index].Ring = Ring;
        Members[Index].Index = Index;
        Members[Index].Status = STATUS_SUCCESS;
        Threads[Index] = NULL;
    }

    for (Index = 0; Index < Count; Index++)
    {
        Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL, StealMember, &Members[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            continue;
        Created++;
        Status = ObReferenceObjectByHandle(Handle, SYNCHRONIZE, *PsThreadType, KernelMode, &Threads[Index], NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Handle);
    }

    Tokens = (ULONG)KeNumberProcessors - 1;
    for (Index = 0; Index < Tokens; Index++)
        KeSetEvent(&Ring->Turn[(Index * Count) / Tokens], IO_NO_INCREMENT, FALSE);

    Delay.QuadPart = -(LONGLONG)STEAL_RUN_MS * 10 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    InterlockedExchange(&Ring->Stop, 1);
    for (Index = 0; Index < Count; Index++)
        KeSetEvent(&Ring->Turn[Index], IO_NO_INCREMENT, FALSE);

    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    for (Index = 0; Index < Count; Index++)
    {
        if (Threads[Index] == NULL)
            continue;
        Status = KeWaitForSingleObject(Threads[Index], Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
            Joined++;
        ObDereferenceObject(Threads[Index]);
    }

    for (Index = 0; Index < Count; Index++)
    {
        ok(Members[Index].Status == STATUS_SUCCESS, "thread %lu stopped waiting with 0x%08lx\n", Index, Members[Index].Status);
        Hops += Members[Index].Hops;
    }

    ok_eq_ulong(Created, Count);
    ok(Hops >= Count, "the tokens went round %I64d times among %lu threads\n", Hops, Count);
    trace("IDLE_STEAL threads=%lu tokens=%lu hops=%I64d\n", Count, Tokens, Hops);

    if (Joined == Created)
    {
        ExFreePoolWithTag(Members, 'lSmK');
        ExFreePoolWithTag(Ring, 'lSmK');
    }
}
