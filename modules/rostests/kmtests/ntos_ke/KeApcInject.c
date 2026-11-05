/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel-Mode Test Suite kernel APC injection API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT ApcFiredEvent;
static KEVENT ThreadReadyEvent;
static KEVENT ThreadGoEvent;
static volatile LONG NormalApcRuns;
static volatile LONG RundownApcRuns;
static PKTHREAD ApcTargetThread;
static KIRQL ApcFireIrql;

static
VOID
NTAPI
NormalApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ExFreePoolWithTag(Apc, 'cApK');
}

static
VOID
NTAPI
NormalRoutine(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ok_eq_pointer(NormalContext, (PVOID)(ULONG_PTR)0xA9C0);
    ok_eq_pointer(SystemArgument1, (PVOID)(ULONG_PTR)0x1111);
    ok_eq_pointer(SystemArgument2, (PVOID)(ULONG_PTR)0x2222);
    ApcFireIrql = KeGetCurrentIrql();
    ok_eq_pointer(KeGetCurrentThread(), ApcTargetThread);
    InterlockedIncrement(&NormalApcRuns);
    KeSetEvent(&ApcFiredEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
ApcTargetThreadRoutine(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    ApcTargetThread = KeGetCurrentThread();
    KeSetEvent(&ThreadReadyEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&ThreadGoEvent, Executive, KernelMode, TRUE, NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestNormalApcInjection(VOID)
{
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    PKAPC Apc;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    BOOLEAN Inserted;

    KeInitializeEvent(&ApcFiredEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadGoEvent, NotificationEvent, FALSE);
    NormalApcRuns = 0;
    ApcTargetThread = NULL;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, ApcTargetThreadRoutine, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&ThreadReadyEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ApcTargetThread != NULL, "target thread not set\n");

    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Apc), 'cApK');
    ok(Apc != NULL, "no pool for APC\n");
    if (Apc != NULL && ApcTargetThread != NULL)
    {
        KeInitializeApc(Apc, ApcTargetThread, OriginalApcEnvironment, NormalApcRoutine, NULL, NormalRoutine, KernelMode, (PVOID)(ULONG_PTR)0xA9C0);
        Inserted = KeInsertQueueApc(Apc, (PVOID)(ULONG_PTR)0x1111, (PVOID)(ULONG_PTR)0x2222, IO_NO_INCREMENT);
        ok_bool_true(Inserted, "KeInsertQueueApc");

        Status = KeWaitForSingleObject(&ApcFiredEvent, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(NormalApcRuns, 1L);
        ok_eq_uint(ApcFireIrql, PASSIVE_LEVEL);
    }

    KeSetEvent(&ThreadGoEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
    ObDereferenceObject(ThreadObject);
}

static
VOID
NTAPI
RundownApcRoutine(
    _In_ PKAPC Apc)
{
    InterlockedIncrement(&RundownApcRuns);
    ExFreePoolWithTag(Apc, 'cApK');
}

static
VOID
NTAPI
NoopNormalRoutine(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

static
VOID
TestApcRundown(VOID)
{
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    PKAPC Apc;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    KeInitializeEvent(&ThreadReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadGoEvent, NotificationEvent, FALSE);
    RundownApcRuns = 0;
    ApcTargetThread = NULL;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, ApcTargetThreadRoutine, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    KeWaitForSingleObject(&ThreadReadyEvent, Executive, KernelMode, FALSE, &Timeout);

    KeSetEvent(&ThreadGoEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);

    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Apc), 'cApK');
    if (Apc != NULL && ApcTargetThread != NULL)
    {
        KeInitializeApc(Apc, ApcTargetThread, OriginalApcEnvironment, NormalApcRoutine, RundownApcRoutine, NoopNormalRoutine, KernelMode, NULL);
        if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT))
            ExFreePoolWithTag(Apc, 'cApK');
    }

    ObDereferenceObject(ThreadObject);
}

static
VOID
NTAPI
SelfSpecialApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    *(volatile LONG *)*SystemArgument1 = 1;
    KeSetEvent((PKEVENT)*SystemArgument2, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestSelfSpecialApc(VOID)
{
    KAPC Apc;
    KEVENT Done;
    volatile LONG Delivered;
    ULONG Mode, Index, Late, Rejected;

    for (Mode = 0; Mode < 2; Mode++)
    {
        Late = Rejected = 0;
        if (Mode) KeEnterCriticalRegion();
        for (Index = 0; Index < 16384; Index++)
        {
            Delivered = 0;
            KeInitializeEvent(&Done, NotificationEvent, FALSE);
            KeInitializeApc(&Apc, KeGetCurrentThread(), CurrentApcEnvironment,
                            SelfSpecialApcRoutine, NULL, NULL, KernelMode, NULL);
            if (!KeInsertQueueApc(&Apc, (PVOID)&Delivered, &Done, IO_NO_INCREMENT))
            {
                Rejected++;
                break;
            }
            if (!Delivered) Late++;
            /* Keep both stack objects alive even on the broken return path. */
            KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
        }
        if (Mode) KeLeaveCriticalRegion();
        ok_eq_ulong(Rejected, 0);
        ok_eq_ulong(Late, 0);
    }
}

#define GUARDED_APC_ITERATIONS 128

typedef struct _GUARDED_APC_CONTEXT
{
    KEVENT Ready;
    KEVENT Proceed;
    KEVENT Fired;
    KEVENT Acknowledged;
    KAPC Apc;
    PKTHREAD Target;
    KAFFINITY TargetAffinity;
    volatile LONG Delivered;
    volatile LONG Failed;
    volatile LONG Stop;
} GUARDED_APC_CONTEXT;

static GUARDED_APC_CONTEXT GuardedApcContext;

static VOID NTAPI
GuardedSpecialApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    GUARDED_APC_CONTEXT *Context = *SystemArgument1;
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument2);
    InterlockedIncrement(&Context->Delivered);
    KeSetEvent(&Context->Fired, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
GuardedApcRundown(_In_ PKAPC Apc)
{
    GUARDED_APC_CONTEXT *Context = CONTAINING_RECORD(Apc, GUARDED_APC_CONTEXT, Apc);
    InterlockedExchange(&Context->Failed, 1);
    KeSetEvent(&Context->Fired, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
GuardedApcThread(_In_ PVOID Parameter)
{
    GUARDED_APC_CONTEXT *Context = Parameter;
    LARGE_INTEGER Timeout;
    KAFFINITY PreviousAffinity;
    NTSTATUS Status;
    ULONG Index;

    Timeout.QuadPart = -3LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->TargetAffinity);
    Context->Target = KeGetCurrentThread();
    for (Index = 0; Index < GUARDED_APC_ITERATIONS; ++Index)
    {
        KeEnterGuardedRegion();
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
        Status = KeWaitForSingleObject(&Context->Proceed, Executive, KernelMode, FALSE, &Timeout);
        KeLeaveGuardedRegion();
        if (Status != STATUS_SUCCESS || Context->Stop) break;

        Status = KeWaitForSingleObject(&Context->Fired, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS || Context->Delivered != (LONG)Index + 1)
        {
            InterlockedExchange(&Context->Failed, 1);
            KeSetEvent(&Context->Acknowledged, IO_NO_INCREMENT, FALSE);
            break;
        }
        KeSetEvent(&Context->Acknowledged, IO_NO_INCREMENT, FALSE);
    }
    if (Index != GUARDED_APC_ITERATIONS && !Context->Stop)
        InterlockedExchange(&Context->Failed, 1);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
TestGuardedApcRace(VOID)
{
    GUARDED_APC_CONTEXT *Context = &GuardedApcContext;
    KAFFINITY Active, First, Second, PreviousAffinity;
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    BOOLEAN Inserted = FALSE;
    ULONG Index;

    Active = KeQueryActiveProcessors();
    First = Active & (0 - Active);
    Active &= ~First;
    Second = Active & (0 - Active);
    if (!First || !Second)
    {
        skip(FALSE, "Two processors are required for the guarded APC race\n");
        return;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Proceed, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Fired, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Acknowledged, SynchronizationEvent, FALSE);
    Context->TargetAffinity = Second;
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(First);

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  GuardedApcThread, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Context->Stop, 1);
        KeSetEvent(&Context->Proceed, IO_NO_INCREMENT, FALSE);
        ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
        ObCloseHandle(ThreadHandle, KernelMode);
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }
    ObCloseHandle(ThreadHandle, KernelMode);

    for (Index = 0; Index < GUARDED_APC_ITERATIONS; ++Index)
    {
        Status = KeWaitForSingleObject(&Context->Ready, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS) break;
        KeInitializeApc(&Context->Apc, Context->Target, OriginalApcEnvironment,
                        GuardedSpecialApcRoutine, GuardedApcRundown, NULL, KernelMode, NULL);
        if (Index & 1) KeSetEvent(&Context->Proceed, IO_NO_INCREMENT, FALSE);
        Inserted = KeInsertQueueApc(&Context->Apc, Context, NULL, IO_NO_INCREMENT);
        if (!(Index & 1)) KeSetEvent(&Context->Proceed, IO_NO_INCREMENT, FALSE);
        if (!Inserted) break;
        Status = KeWaitForSingleObject(&Context->Acknowledged, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS || Context->Failed) break;
    }
    ok(Index == GUARDED_APC_ITERATIONS, "Guarded APC race stopped at %lu, status 0x%08lx\n", Index, Status);
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->Proceed, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, NULL);
    ok_eq_long(Context->Delivered, GUARDED_APC_ITERATIONS);
    ObDereferenceObject(ThreadObject);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

START_TEST(KeApcInject)
{
    TestSelfSpecialApc();
    TestNormalApcInjection();
    TestApcRundown();
    TestGuardedApcRace();
}
