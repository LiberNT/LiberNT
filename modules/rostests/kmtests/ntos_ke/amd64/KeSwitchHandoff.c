/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 scheduler handoff integration tests
 */

#include <kmt_test.h>
#include <ndk/ketypes.h>

C_ASSERT(FIELD_OFFSET(KTHREAD, InitialStack) == 0x28);

typedef struct _REAPER_HANDOFF_CONTEXT
{
    KEVENT TargetReady;
    KEVENT SpinnerReady;
    KEVENT StartTarget;
    KEVENT StartSpinner;
    KAFFINITY SourceCpu;
    PKTHREAD Target;
    volatile LONG Stop;
    volatile LONG SpinnerStarted;
    volatile LONG StackPresentAtStart;
    volatile LONG Expired;
    volatile LONG Failed;
} REAPER_HANDOFF_CONTEXT;

static REAPER_HANDOFF_CONTEXT ReaperContext;

static
VOID
NTAPI
ReaperTarget(_In_ PVOID Parameter)
{
    REAPER_HANDOFF_CONTEXT *Context = Parameter;
    LARGE_INTEGER Timeout, Delay;
    NTSTATUS Status;

    KeSetSystemAffinityThreadEx(Context->SourceCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 20);
    KeSetEvent(&Context->TargetReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->StartTarget, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else if (!Context->Stop)
    {
        Delay.QuadPart = -50LL * 1000 * 10;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        KeSetEvent(&Context->StartSpinner, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
ReaperSpinner(_In_ PVOID Parameter)
{
    REAPER_HANDOFF_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    NTSTATUS Status;
    KIRQL OldIrql;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->SourceCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 16);
    KeSetEvent(&Context->SpinnerReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->StartSpinner, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else if (!Context->Stop)
    {
        Deadline = KeQueryInterruptTime() + 5LL * 1000 * 1000 * 10;
        while (!((volatile KTHREAD *)Context->Target)->Header.SignalState &&
               !Context->Stop && KeQueryInterruptTime() < Deadline)
            YieldProcessor();
        if (!((volatile KTHREAD *)Context->Target)->Header.SignalState)
        {
            InterlockedExchange(&Context->Failed, 1);
            goto Exit;
        }
        OldIrql = KeRaiseIrqlToDpcLevel();
        if (InterlockedCompareExchangePointer((PVOID volatile *)&Context->Target->InitialStack, NULL, NULL))
            InterlockedExchange(&Context->StackPresentAtStart, 1);
        InterlockedExchange(&Context->SpinnerStarted, 1);
        Deadline = KeQueryInterruptTime() + 1500LL * 1000 * 10;
        while (!Context->Stop && KeQueryInterruptTime() < Deadline)
            YieldProcessor();
        if (!Context->Stop) InterlockedExchange(&Context->Expired, 1);
        KeLowerIrql(OldIrql);
    }
Exit:
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeSwitchReaper)
{
    REAPER_HANDOFF_CONTEXT *Context = &ReaperContext;
    KAFFINITY Active, First, Second, PreviousAffinity;
    HANDLE TargetHandle = NULL, SpinnerHandle = NULL;
    PKTHREAD Target = NULL;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    NTSTATUS Status;
    KPRIORITY PreviousPriority;
    BOOLEAN Reaped = FALSE;

    Active = KeQueryActiveProcessors();
    First = Active & (0 - Active);
    Active &= ~First;
    Second = Active & (0 - Active);
    if (!First || !Second)
    {
        skip(FALSE, "Two processors are required for reaper handoff\n");
        return;
    }
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->TargetReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->SpinnerReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->StartTarget, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->StartSpinner, NotificationEvent, FALSE);
    Context->SourceCpu = First;
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Second);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), 6);
    Status = PsCreateSystemThread(&SpinnerHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  ReaperSpinner, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = PsCreateSystemThread(&TargetHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  ReaperTarget, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = ObReferenceObjectByHandle(TargetHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID *)&Target, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Context->Target = Target;
    Status = KeWaitForSingleObject(&Context->SpinnerReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->TargetReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    ok(Target->InitialStack != NULL, "Target stack was not initialized\n");
    KeSetEvent(&Context->StartTarget, IO_NO_INCREMENT, FALSE);
    Deadline = KeQueryInterruptTime() + 2LL * 1000 * 1000 * 10;
    do
    {
        if (Context->SpinnerStarted &&
            InterlockedCompareExchangePointer((PVOID volatile *)&Target->InitialStack, NULL, NULL) == NULL)
        {
            Reaped = TRUE;
            break;
        }
        YieldProcessor();
    } while (KeQueryInterruptTime() < Deadline);
    ok_eq_long(Context->SpinnerStarted, 1);
    if (Context->SpinnerStarted && !Context->StackPresentAtStart)
        skip(FALSE, "Target stack was reaped before the raised-IRQL interval\n");
    else
        ok(Reaped, "Terminated thread stack was not reaped\n");
    ok_eq_long(Context->Expired, 0);

Cleanup:
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->StartTarget, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Context->StartSpinner, IO_NO_INCREMENT, FALSE);
    if (TargetHandle)
    {
        ZwWaitForSingleObject(TargetHandle, FALSE, NULL);
        ObCloseHandle(TargetHandle, KernelMode);
    }
    if (SpinnerHandle)
    {
        ZwWaitForSingleObject(SpinnerHandle, FALSE, NULL);
        ObCloseHandle(SpinnerHandle, KernelMode);
    }
    ok_eq_long(Context->Failed, 0);
    if (Target) ObDereferenceObject(Target);
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

#define SELF_WAKE_ITERATIONS 8192

typedef struct _SELF_WAKE_CONTEXT
{
    KEVENT Ready;
    KEVENT Go;
    KEVENT Acknowledged;
    KAFFINITY TargetCpu;
    volatile LONG Failed;
    volatile LONG Stop;
} SELF_WAKE_CONTEXT;

static SELF_WAKE_CONTEXT SelfWakeContext;

static
VOID
NTAPI
SelfWakeTarget(_In_ PVOID Parameter)
{
    SELF_WAKE_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Index;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->TargetCpu);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    for (Index = 0; Index < SELF_WAKE_ITERATIONS; Index++)
    {
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, TRUE);
        Status = KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, &Timeout);
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            if (KeGetCurrentIrql() > PASSIVE_LEVEL) KeLowerIrql(PASSIVE_LEVEL);
            InterlockedExchange(&Context->Failed, 1);
        }
        if ((Status != STATUS_SUCCESS) && !Context->Stop)
            InterlockedExchange(&Context->Failed, 1);
        KeSetEvent(&Context->Acknowledged, IO_NO_INCREMENT, FALSE);
        if (Context->Failed || Context->Stop) break;
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeSwitchSelfWake)
{
    SELF_WAKE_CONTEXT *Context = &SelfWakeContext;
    KAFFINITY Active, First, Second, PreviousAffinity;
    HANDLE Handle = NULL;
    PKTHREAD Target = NULL;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Index, Transitions = 0;

    Active = KeQueryActiveProcessors();
    First = Active & (0 - Active);
    Active &= ~First;
    Second = Active & (0 - Active);
    if (!First || !Second)
    {
        skip(FALSE, "Two processors are required for early self-wake testing\n");
        return;
    }
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Go, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->Acknowledged, SynchronizationEvent, FALSE);
    Context->TargetCpu = Second;
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(First);
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  SelfWakeTarget, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID *)&Target, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    for (Index = 0; Index < SELF_WAKE_ITERATIONS; Index++)
    {
        Status = KeWaitForSingleObject(&Context->Ready, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS) break;
        OldIrql = KeRaiseIrqlToDpcLevel();
        KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
        if (((volatile KTHREAD *)Target)->ReadyTransition) Transitions++;
        KeLowerIrql(OldIrql);
        Status = KeWaitForSingleObject(&Context->Acknowledged, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS || Context->Failed) break;
    }
    ok(Index == SELF_WAKE_ITERATIONS, "Self-wake stopped at %lu, status 0x%08lx\n", Index, Status);
    trace("Observed %lu pending ready transitions\n", Transitions);
    if (!Transitions) skip(FALSE, "The pre-switch ready-transition window was not observed\n");

Cleanup:
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->Go, IO_NO_INCREMENT, FALSE);
    if (Handle)
    {
        ZwWaitForSingleObject(Handle, FALSE, NULL);
        ObCloseHandle(Handle, KernelMode);
    }
    if (Target) ObDereferenceObject(Target);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

typedef struct _STANDBY_STRESS_CONTEXT
{
    KEVENT TargetReady;
    KEVENT CompetitorReady;
    KEVENT PumpReady;
    KEVENT DecoyReady;
    KEVENT Start;
    KEVENT Signal;
    KEVENT AlwaysSignaled;
    KDPC KickDpc;
    KAFFINITY SourceCpu;
    KAFFINITY SpareCpu;
    volatile LONG Stop;
    volatile LONG Failed;
    volatile LONG Progress;
    volatile LONG PumpIterations;
} STANDBY_STRESS_CONTEXT;

static STANDBY_STRESS_CONTEXT StandbyContext;

static
VOID
NTAPI
StandbyKick(_In_ PKDPC Dpc,
            _In_opt_ PVOID DeferredContext,
            _In_opt_ PVOID SystemArgument1,
            _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

static
VOID
NTAPI
StandbyTarget(_In_ PVOID Parameter)
{
    STANDBY_STRESS_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Index;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->SourceCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 15);
    KeSetEvent(&Context->TargetReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->Start, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else
    {
        while (!Context->Stop)
        {
            KeSetSystemAffinityThreadEx(Context->SourceCpu | Context->SpareCpu);
            for (Index = 0; Index < 64 && !Context->Stop; Index++)
            {
                KeSetEvent(&Context->Signal, IO_NO_INCREMENT, TRUE);
                Status = KeWaitForSingleObject(&Context->AlwaysSignaled, Executive, KernelMode, FALSE, NULL);
                if (KeGetCurrentIrql() != PASSIVE_LEVEL)
                {
                    if (KeGetCurrentIrql() > PASSIVE_LEVEL) KeLowerIrql(PASSIVE_LEVEL);
                    InterlockedExchange(&Context->Failed, 1);
                    break;
                }
                if (Status != STATUS_SUCCESS)
                {
                    InterlockedExchange(&Context->Failed, 1);
                    break;
                }
                InterlockedIncrement(&Context->Progress);
            }
            if (Context->Failed) break;
            KeSetSystemAffinityThreadEx(Context->SourceCpu);
        }
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
StandbyCompetitor(_In_ PVOID Parameter)
{
    STANDBY_STRESS_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->SourceCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 5);
    KeSetEvent(&Context->CompetitorReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->Start, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else
        while (!Context->Stop) YieldProcessor();
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
StandbyDecoy(_In_ PVOID Parameter)
{
    STANDBY_STRESS_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->SourceCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 4);
    KeSetEvent(&Context->DecoyReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->Start, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else
        while (!Context->Stop) YieldProcessor();
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
NTAPI
StandbyPump(_In_ PVOID Parameter)
{
    STANDBY_STRESS_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout, Delay;
    NTSTATUS Status;

    PreviousAffinity = KeSetSystemAffinityThreadEx(Context->SpareCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    KeSetEvent(&Context->PumpReady, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&Context->Start, Executive, KernelMode, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
        InterlockedExchange(&Context->Failed, 1);
    else
    {
        Delay.QuadPart = -1LL * 1000 * 10;
        while (!Context->Stop)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            InterlockedIncrement(&Context->PumpIterations);
        }
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeStandbySteal)
{
    STANDBY_STRESS_CONTEXT *Context = &StandbyContext;
    KAFFINITY Active, First, Second, Third, PreviousAffinity;
    HANDLE TargetHandle = NULL, CompetitorHandle = NULL, PumpHandle = NULL, DecoyHandle = NULL;
    PKTHREAD Target = NULL, Competitor = NULL;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline;
    NTSTATUS Status;
    ULONG Iterations = 0, StandbyRunning = 0, MigratedRunning = 0, ThirdNumber = 0;
    KPRIORITY PreviousPriority;

    Active = KeQueryActiveProcessors();
    First = Active & (0 - Active); Active &= ~First;
    Second = Active & (0 - Active); Active &= ~Second;
    Third = Active & (0 - Active);
    if (!First || !Second || !Third)
    {
        skip(FALSE, "Three processors are required for standby-steal testing\n");
        return;
    }
    while (((KAFFINITY)1 << ThirdNumber) != Third) ThirdNumber++;
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeEvent(&Context->TargetReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->CompetitorReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->PumpReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->DecoyReady, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->Start, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->Signal, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->AlwaysSignaled, NotificationEvent, TRUE);
    KeInitializeDpc(&Context->KickDpc, StandbyKick, NULL);
    KeSetTargetProcessorDpc(&Context->KickDpc, (CCHAR)ThirdNumber);
    Context->SourceCpu = First;
    Context->SpareCpu = Third;
    Timeout.QuadPart = -5LL * 1000 * 1000 * 10;
    PreviousAffinity = KeSetSystemAffinityThreadEx(Second);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), 18);
    Status = PsCreateSystemThread(&DecoyHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  StandbyDecoy, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = PsCreateSystemThread(&PumpHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  StandbyPump, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = PsCreateSystemThread(&CompetitorHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  StandbyCompetitor, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = ObReferenceObjectByHandle(CompetitorHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID *)&Competitor, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = PsCreateSystemThread(&TargetHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  StandbyTarget, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = ObReferenceObjectByHandle(TargetHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID *)&Target, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->DecoyReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->PumpReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->CompetitorReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    Status = KeWaitForSingleObject(&Context->TargetReady, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Cleanup;
    trace("Standby target %p\n", Target);
    KeSetEvent(&Context->Start, IO_NO_INCREMENT, FALSE);
    Deadline = KeQueryInterruptTime() + 2LL * 1000 * 1000 * 10;
    while (KeQueryInterruptTime() < Deadline && !Context->Failed)
    {
        ULONG Spin;
        if ((Iterations & 15) == 0) KeInsertQueueDpc(&Context->KickDpc, NULL, NULL);
        KeSetPriorityThread(Competitor, 20);
        for (Spin = 0; Spin < 256; Spin++)
        {
            if (((volatile KTHREAD *)Competitor)->State == Running &&
                ((volatile KTHREAD *)Target)->State == Ready) break;
            YieldProcessor();
        }
        KeSetPriorityThread(Competitor, 5);
        for (Spin = 0; Spin < 256; Spin++)
        {
            if (((volatile KTHREAD *)Target)->Running &&
                ((volatile KTHREAD *)Target)->State == Standby)
            {
                StandbyRunning++;
                if (((volatile KTHREAD *)Target)->NextProcessor == ThirdNumber)
                    MigratedRunning++;
            }
            YieldProcessor();
        }
        Iterations++;
    }
    ok(Iterations != 0 && Context->Progress != 0, "No scheduler stress progress (%lu, %ld)\n",
       Iterations, Context->Progress);
    ok_eq_long(Context->Failed, 0);
    trace("Priority cycles %lu, pump iterations %ld, standby-running samples %lu, migrated-running samples %lu\n",
          Iterations, Context->PumpIterations, StandbyRunning, MigratedRunning);
    trace("Lock-free samples do not establish that a standby steal occurred\n");
    if (!StandbyRunning) skip(FALSE, "No running-standby sample was observed\n");
    if (!MigratedRunning) skip(FALSE, "No migrated-running sample was observed\n");

Cleanup:
    InterlockedExchange(&Context->Stop, 1);
    KeSetEvent(&Context->Start, IO_NO_INCREMENT, FALSE);
    if (Competitor) KeSetPriorityThread(Competitor, 5);
    if (TargetHandle)
    {
        ZwWaitForSingleObject(TargetHandle, FALSE, NULL);
        ObCloseHandle(TargetHandle, KernelMode);
    }
    if (CompetitorHandle)
    {
        ZwWaitForSingleObject(CompetitorHandle, FALSE, NULL);
        ObCloseHandle(CompetitorHandle, KernelMode);
    }
    if (PumpHandle)
    {
        ZwWaitForSingleObject(PumpHandle, FALSE, NULL);
        ObCloseHandle(PumpHandle, KernelMode);
    }
    if (DecoyHandle)
    {
        ZwWaitForSingleObject(DecoyHandle, FALSE, NULL);
        ObCloseHandle(DecoyHandle, KernelMode);
    }
    KeRemoveQueueDpc(&Context->KickDpc);
    KeFlushQueuedDpcs();
    if (Target) ObDereferenceObject(Target);
    if (Competitor) ObDereferenceObject(Competitor);
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
