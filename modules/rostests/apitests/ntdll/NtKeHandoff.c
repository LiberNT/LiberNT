/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Scheduler wait and wake handoff tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"

#define HANDOFF_ITERATIONS 512

typedef struct _HANDOFF_CONTEXT
{
    HANDLE Go;
    HANDLE Ack;
    volatile LONG Stop;
    volatile LONG Completed;
    volatile LONG WorkerError;
} HANDOFF_CONTEXT;

static HANDOFF_CONTEXT Handoff;

static DWORD WINAPI
HandoffWorker(PVOID Parameter)
{
    HANDOFF_CONTEXT *Context = Parameter;
    DWORD Result;

    for (;;)
    {
        Result = WaitForSingleObject(Context->Go, 3000);
        if (Result != WAIT_OBJECT_0)
        {
            InterlockedExchange(&Context->WorkerError, (LONG)Result);
            return 1;
        }
        if (InterlockedCompareExchange(&Context->Stop, 0, 0)) return 0;
        InterlockedIncrement(&Context->Completed);
        if (!SetEvent(Context->Ack))
        {
            InterlockedExchange(&Context->WorkerError, -1);
            return 1;
        }
    }
}

static BOOLEAN
RunHandoffs(DWORD_PTR ProducerCpu, DWORD_PTR WorkerCpu, const char *Label)
{
    HANDOFF_CONTEXT *Context = &Handoff;
    HANDLE Thread = NULL;
    DWORD_PTR PreviousAffinity;
    LARGE_INTEGER Timeout;
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD Result;
    ULONG Index = 0;
    BOOLEAN Joined = TRUE;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Go = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context->Ack = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Context->Go && Context->Ack, "%s: CreateEvent failed %lu\n", Label, GetLastError());
    if (!Context->Go || !Context->Ack) goto Cleanup;

    PreviousAffinity = SetThreadAffinityMask(GetCurrentThread(), ProducerCpu);
    ok(PreviousAffinity != 0, "%s: producer affinity failed %lu\n", Label, GetLastError());
    if (!PreviousAffinity) goto Cleanup;

    Thread = CreateThread(NULL, 0, HandoffWorker, Context, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "%s: CreateThread failed %lu\n", Label, GetLastError());
    if (Thread)
    {
        DWORD_PTR WorkerPrevious = SetThreadAffinityMask(Thread, WorkerCpu);
        ok(WorkerPrevious != 0, "%s: worker affinity failed %lu\n", Label, GetLastError());
        if (!WorkerPrevious) InterlockedExchange(&Context->Stop, 1);
        Result = ResumeThread(Thread);
        ok(Result == 1, "%s: ResumeThread returned %lu\n", Label, Result);
        if (Result == 1 && WorkerPrevious)
        {
            Timeout.QuadPart = -30000000;
            for (Index = 0; Index < HANDOFF_ITERATIONS; Index++)
            {
                Status = NtSignalAndWaitForSingleObject(Context->Go, Context->Ack, FALSE, &Timeout);
                if (Status != STATUS_SUCCESS) break;
            }
            ok(Index == HANDOFF_ITERATIONS,
               "%s: completed %lu handoffs, status 0x%08lx, worker error %ld\n",
               Label, Index, Status, Context->WorkerError);
            ok(Context->Completed == (LONG)Index,
               "%s: worker completed %ld, producer completed %lu\n",
               Label, Context->Completed, Index);
        }
        InterlockedExchange(&Context->Stop, 1);
        SetEvent(Context->Go);
        Result = WaitForSingleObject(Thread, 5000);
        ok(Result == WAIT_OBJECT_0, "%s: worker join returned %lu\n", Label, Result);
        Joined = Result == WAIT_OBJECT_0;
        if (Joined)
            ok(Context->WorkerError == 0, "%s: worker error %ld\n", Label, Context->WorkerError);
    }
    ok(SetThreadAffinityMask(GetCurrentThread(), PreviousAffinity) != 0,
       "%s: affinity restore failed %lu\n", Label, GetLastError());

Cleanup:
    if (Thread) CloseHandle(Thread);
    if (Joined)
    {
        if (Context->Go) CloseHandle(Context->Go);
        if (Context->Ack) CloseHandle(Context->Ack);
    }
    return Joined;
}

START_TEST(NtKeHandoff)
{
    DWORD_PTR ProcessMask, SystemMask, FirstCpu, SecondCpu;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask) || !ProcessMask)
    {
        skip("Cannot obtain process affinity: %lu\n", GetLastError());
        return;
    }
    FirstCpu = ProcessMask & (~ProcessMask + 1);
    ProcessMask &= ~FirstCpu;
    SecondCpu = ProcessMask & (~ProcessMask + 1);
    if (!RunHandoffs(FirstCpu, FirstCpu, "same CPU")) return;
    if (SecondCpu) RunHandoffs(FirstCpu, SecondCpu, "different CPUs");
    else skip("Two available processors are required for cross-CPU handoffs\n");
}
