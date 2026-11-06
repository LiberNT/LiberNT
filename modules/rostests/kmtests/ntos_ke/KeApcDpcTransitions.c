/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     APC and DPC queue transition tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>

typedef struct _TRANSITION_CONTEXT
{
    KEVENT Done;
    volatile LONG Calls;
    KIRQL Irql;
    PVOID Argument1;
    PVOID Argument2;
} TRANSITION_CONTEXT;

static TRANSITION_CONTEXT ApcContext;
static TRANSITION_CONTEXT DpcContext;

static VOID NTAPI
TransitionApc(PKAPC Apc, PKNORMAL_ROUTINE *NormalRoutine,
              PVOID *NormalContext, PVOID *SystemArgument1, PVOID *SystemArgument2)
{
    TRANSITION_CONTEXT *Context = *SystemArgument1;
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    Context->Irql = KeGetCurrentIrql();
    Context->Argument2 = *SystemArgument2;
    InterlockedIncrement(&Context->Calls);
    KeSetEvent(&Context->Done, IO_NO_INCREMENT, FALSE);
}

START_TEST(KeApcQueueTransitions)
{
    TRANSITION_CONTEXT *Context = &ApcContext;
    BOOLEAN (NTAPI *RemoveApc)(PKAPC);
    KAPC Apc;
    KIRQL OldIrql;
    BOOLEAN First, Duplicate, Removed, RemovedAgain, Reinserted;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    RemoveApc = KmtGetSystemRoutineAddress(L"KeRemoveQueueApc");
    if (!RemoveApc || KeGetCurrentIrql() != PASSIVE_LEVEL || KeAreAllApcsDisabled())
    {
        skip(FALSE, "APC removal unavailable or special APC delivery disabled\n");
        return;
    }
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Done, NotificationEvent, FALSE);
    KeInitializeApc(&Apc, KeGetCurrentThread(), OriginalApcEnvironment,
                    TransitionApc, NULL, NULL, KernelMode, NULL);
    KeRaiseIrql(APC_LEVEL, &OldIrql);
    First = KeInsertQueueApc(&Apc, Context, (PVOID)1, IO_NO_INCREMENT);
    Duplicate = KeInsertQueueApc(&Apc, Context, (PVOID)2, IO_NO_INCREMENT);
    Removed = RemoveApc(&Apc);
    RemovedAgain = RemoveApc(&Apc);
    Reinserted = KeInsertQueueApc(&Apc, Context, (PVOID)3, IO_NO_INCREMENT);
    KeLowerIrql(OldIrql);

    Timeout.QuadPart = -30000000;
    Status = KeWaitForSingleObject(&Context->Done, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS) RemoveApc(&Apc);
    ok(First, "Initial APC insertion failed\n");
    ok(!Duplicate, "Duplicate APC insertion succeeded\n");
    ok(Removed, "Queued APC was not removed\n");
    ok(!RemovedAgain, "Removed APC was removed twice\n");
    ok(Reinserted, "APC reinsertion failed\n");
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context->Calls, 1);
    ok_eq_uint(Context->Irql, APC_LEVEL);
    ok_eq_pointer(Context->Argument2, (PVOID)3);
}

static VOID NTAPI
TransitionDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    TRANSITION_CONTEXT *Context = DeferredContext;
    UNREFERENCED_PARAMETER(Dpc);
    Context->Irql = KeGetCurrentIrql();
    Context->Argument1 = SystemArgument1;
    Context->Argument2 = SystemArgument2;
    InterlockedIncrement(&Context->Calls);
    KeSetEvent(&Context->Done, IO_NO_INCREMENT, FALSE);
}

START_TEST(KeDpcQueueTransitions)
{
    TRANSITION_CONTEXT *Context = &DpcContext;
    KDPC Dpc;
    KIRQL OldIrql;
    BOOLEAN First, Duplicate, Removed, RemovedAgain, Reinserted;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Done, NotificationEvent, FALSE);
    KeInitializeDpc(&Dpc, TransitionDpc, Context);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    First = KeInsertQueueDpc(&Dpc, (PVOID)1, (PVOID)2);
    Duplicate = KeInsertQueueDpc(&Dpc, (PVOID)3, (PVOID)4);
    KeLowerIrql(OldIrql);

    Timeout.QuadPart = -30000000;
    Status = KeWaitForSingleObject(&Context->Done, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS) KeRemoveQueueDpc(&Dpc);
    KeFlushQueuedDpcs();
    ok(First, "Initial DPC insertion failed\n");
    ok(!Duplicate, "Duplicate DPC insertion succeeded\n");
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context->Calls, 1);
    ok_eq_uint(Context->Irql, DISPATCH_LEVEL);
    ok_eq_pointer(Context->Argument1, (PVOID)1);
    ok_eq_pointer(Context->Argument2, (PVOID)2);

    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Done, NotificationEvent, FALSE);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    First = KeInsertQueueDpc(&Dpc, (PVOID)1, (PVOID)2);
    Duplicate = KeInsertQueueDpc(&Dpc, (PVOID)3, (PVOID)4);
    Removed = KeRemoveQueueDpc(&Dpc);
    RemovedAgain = KeRemoveQueueDpc(&Dpc);
    Reinserted = KeInsertQueueDpc(&Dpc, (PVOID)5, (PVOID)6);
    KeLowerIrql(OldIrql);

    Timeout.QuadPart = -30000000;
    Status = KeWaitForSingleObject(&Context->Done, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS) KeRemoveQueueDpc(&Dpc);
    KeFlushQueuedDpcs();
    ok(First, "Second DPC insertion failed\n");
    ok(!Duplicate, "Second duplicate DPC insertion succeeded\n");
    ok(Removed, "Queued DPC was not removed\n");
    ok(!RemovedAgain, "Removed DPC was removed twice\n");
    ok(Reinserted, "DPC reinsertion failed\n");
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context->Calls, 1);
    ok_eq_uint(Context->Irql, DISPATCH_LEVEL);
    ok_eq_pointer(Context->Argument1, (PVOID)5);
    ok_eq_pointer(Context->Argument2, (PVOID)6);
}
