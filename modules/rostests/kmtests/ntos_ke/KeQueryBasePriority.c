/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Query a thread's base priority while it attaches to another process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define QUERY_ITERATIONS 200000

typedef struct
{
    KEVENT Started;
    PEPROCESS Target;
    volatile LONG Stop;
    volatile LONG Attachments;
} ATTACH_STATE;

static VOID NTAPI AttachWorker(PVOID Parameter)
{
    ATTACH_STATE *State = Parameter;
    KAPC_STATE ApcState;
    ULONG Iteration;

    KeSetEvent(&State->Started, IO_NO_INCREMENT, FALSE);
    for (Iteration = 0; !State->Stop && Iteration < 4 * QUERY_ITERATIONS; ++Iteration)
    {
        if (Iteration & 1)
        {
            KeStackAttachProcess((PKPROCESS)State->Target, &ApcState);
            KeUnstackDetachProcess(&ApcState);
        }
        else
        {
            KeAttachProcess((PKPROCESS)State->Target);
            KeDetachProcess();
        }
        InterlockedIncrement(&State->Attachments);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(KeQueryBasePriority)
{
    ATTACH_STATE State = {0};
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ThreadHandle;
    PKTHREAD Worker;
    LONG Expected;
    LONG Increment;
    ULONG Iteration;
    ULONG Mismatches = 0;
    ULONG IrqlLeaks = 0;
    NTSTATUS Status;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Requires multiple processors\n");
        return;
    }
    State.Target = PsGetCurrentProcess();
    if (State.Target == PsInitialSystemProcess)
    {
        skip(FALSE, "Run in the requesting test process\n");
        return;
    }

    KeInitializeEvent(&State.Started, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &Attributes,
                                  NULL, NULL, AttachWorker, &State);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType,
                                       KernelMode, (PVOID *)&Worker, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&State.Stop, 1);
        ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
        ZwClose(ThreadHandle);
        return;
    }
    KeWaitForSingleObject(&State.Started, Executive, KernelMode, FALSE, NULL);

    Expected = Worker->BasePriority - Worker->Process->BasePriority;
    for (Iteration = 0; Iteration < QUERY_ITERATIONS; ++Iteration)
    {
        THREAD_BASIC_INFORMATION Info;

        Status = ZwQueryInformationThread(ThreadHandle, ThreadBasicInformation, &Info, sizeof(Info), NULL);
        Increment = NT_SUCCESS(Status) ? Info.BasePriority : MAXLONG;
        if (Increment != Expected)
            Mismatches++;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            IrqlLeaks++;
            KeLowerIrql(PASSIVE_LEVEL);
        }
    }

    InterlockedExchange(&State.Stop, 1);
    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);

    ok_eq_ulong(Mismatches, 0UL);
    ok_eq_ulong(IrqlLeaks, 0UL);
    ok(State.Attachments > 0, "No concurrent process attachments\n");
    trace("%lu queries against %ld attach/detach pairs, increment %ld\n",
          QUERY_ITERATIONS, State.Attachments, Expected);
    ObDereferenceObject(Worker);
}
