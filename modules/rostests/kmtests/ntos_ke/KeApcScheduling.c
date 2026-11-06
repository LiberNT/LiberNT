/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     APC delivery during scheduler handoff
 */

#include <kmt_test.h>

#define HANDOFF_ITERATIONS 2048

typedef struct _APC_HANDOFF_CONTEXT
{
    KEVENT Ready;
    KEVENT Go;
    KEVENT Acknowledged;
    KAPC Apc;
    PKTHREAD Target;
    KAFFINITY TargetAffinity;
    volatile LONG Delivered;
    volatile LONG Failed;
    volatile LONG Stop;
} APC_HANDOFF_CONTEXT;

static APC_HANDOFF_CONTEXT Handoff;

static
VOID
NTAPI
HandoffApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    APC_HANDOFF_CONTEXT *Context = *SystemArgument1;
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument2);
    InterlockedIncrement(&Context->Delivered);
}

static
VOID
NTAPI
HandoffThread(_In_ PVOID Parameter)
{
    APC_HANDOFF_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    ULONG Index;
    NTSTATUS Status;

    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->TargetAffinity);
    Context->Target = KeGetCurrentThread();
    for (Index = 0; Index < HANDOFF_ITERATIONS; Index++)
    {
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, TRUE);
        Status = KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, &Timeout);
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            if (KeGetCurrentIrql() > PASSIVE_LEVEL) KeLowerIrql(PASSIVE_LEVEL);
            InterlockedExchange(&Context->Failed, 1);
            KeSetEvent(&Context->Acknowledged, IO_NO_INCREMENT, FALSE);
            break;
        }
        if (Status != STATUS_SUCCESS || Context->Stop) break;
        Deadline = KeQueryInterruptTime() + 2LL * 1000 * 1000 * 10;
        while (Context->Delivered != (LONG)Index + 1 &&
               !Context->Stop && KeQueryInterruptTime() < Deadline)
            YieldProcessor();
        if (Context->Delivered != (LONG)Index + 1)
            InterlockedExchange(&Context->Failed, 1);
        KeSetEvent(&Context->Acknowledged, IO_NO_INCREMENT, FALSE);
        if (Context->Failed || Context->Stop) break;
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeApcScheduling)
{
    APC_HANDOFF_CONTEXT *Context = &Handoff;
    KAFFINITY Active, Producer, PreviousAffinity;
    LARGE_INTEGER Timeout;
    HANDLE Handle;
    PVOID ThreadObject;
    NTSTATUS Status;
    BOOLEAN Inserted;
    ULONG Index;

    Active = KeQueryActiveProcessors();
    Producer = Active & (0 - Active);
    Active &= ~Producer;
    if (!Producer || !Active)
    {
        skip(FALSE, "Two processors are required for APC handoff\n");
        return;
    }
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Go, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Acknowledged, SynchronizationEvent, FALSE);
    Context->TargetAffinity = Active;
    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Producer);
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  HandoffThread, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }
    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Context->Stop, 1);
        KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
        ZwWaitForSingleObject(Handle, FALSE, NULL);
        ObCloseHandle(Handle, KernelMode);
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }
    ObCloseHandle(Handle, KernelMode);

    for (Index = 0; Index < HANDOFF_ITERATIONS; Index++)
    {
        Status = KeWaitForSingleObject(&Context->Ready, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS) break;
        KeInitializeApc(&Context->Apc, Context->Target, OriginalApcEnvironment,
                        HandoffApcRoutine, NULL, NULL, KernelMode, NULL);
        KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
        Inserted = KeInsertQueueApc(&Context->Apc, Context, NULL, IO_NO_INCREMENT);
        if (!Inserted) break;
        Status = KeWaitForSingleObject(&Context->Acknowledged, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS || Context->Failed) break;
    }
    ok(Index == HANDOFF_ITERATIONS, "APC handoff stopped at %lu, status 0x%08lx, delivered %ld\n",
       Index, Status, Context->Delivered);
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(ThreadObject);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
