/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Query the cycle time of running and switching threads at every IRQL
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <kmt_test.h>

#define SWITCH_QUERIES 200000

typedef struct
{
    KEVENT Started;
    volatile LONG Stop;
    BOOLEAN Yield;
} CYCLE_STATE;

static VOID NTAPI CycleWorker(PVOID Parameter)
{
    CYCLE_STATE *State = Parameter;

    KeSetEvent(&State->Started, IO_NO_INCREMENT, FALSE);
    while (!State->Stop)
    {
        if (State->Yield)
            ZwYieldExecution();
        else
            YieldProcessor();
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static BOOLEAN StartWorker(CYCLE_STATE *State, BOOLEAN Yield, PHANDLE Handle, PKTHREAD *Thread)
{
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    RtlZeroMemory(State, sizeof(*State));
    State->Yield = Yield;
    KeInitializeEvent(&State->Started, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(Handle, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, CycleWorker, State);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Status = ObReferenceObjectByHandle(*Handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KeWaitForSingleObject(&State->Started, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&State->Stop, 1);
        ZwWaitForSingleObject(*Handle, FALSE, NULL);
        ZwClose(*Handle);
        return FALSE;
    }
    return TRUE;
}

static VOID StopWorker(CYCLE_STATE *State, HANDLE Handle, PKTHREAD Thread)
{
    InterlockedExchange(&State->Stop, 1);
    ok_eq_hex(ZwWaitForSingleObject(Handle, FALSE, NULL), STATUS_SUCCESS);
    ZwClose(Handle);
    ObDereferenceObject(Thread);
}

static VOID TestRaisedIrql(VOID)
{
    ULONG64 Before, After, BeforeStamp, AfterStamp, Passive;
    CYCLE_STATE State;
    HANDLE Handle;
    PKTHREAD Worker;
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Before = KeQueryTotalCycleTimeThread(KeGetCurrentThread(), &BeforeStamp);
    KeStallExecutionProcessor(1000);
    After = KeQueryTotalCycleTimeThread(KeGetCurrentThread(), &AfterStamp);
    KeLowerIrql(OldIrql);
    ok(After > Before, "1 ms at DISPATCH_LEVEL left the calling thread at %I64u cycles from %I64u\n", After, Before);
    ok(AfterStamp > BeforeStamp, "cycle stamp went from %I64u to %I64u\n", BeforeStamp, AfterStamp);

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "a running thread needs a processor of its own\n");
        return;
    }
    if (!StartWorker(&State, FALSE, &Handle, &Worker))
        return;

    Passive = KeQueryTotalCycleTimeThread(Worker, &BeforeStamp);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Before = KeQueryTotalCycleTimeThread(Worker, &BeforeStamp);
    KeStallExecutionProcessor(1000);
    After = KeQueryTotalCycleTimeThread(Worker, &AfterStamp);
    KeLowerIrql(OldIrql);
    ok(Before >= Passive, "the running thread went from %I64u cycles to %I64u at DISPATCH_LEVEL\n", Passive, Before);
    ok(After >= Before, "the running thread went from %I64u cycles to %I64u at DISPATCH_LEVEL\n", Before, After);
    ok(AfterStamp > BeforeStamp, "cycle stamp went from %I64u to %I64u\n", BeforeStamp, AfterStamp);

    StopWorker(&State, Handle, Worker);
}

static VOID TestSwitchingThread(VOID)
{
    ULONG64 Previous = 0, Cycles, Stamp;
    ULONG Iteration, Decreases = 0;
    CYCLE_STATE State;
    HANDLE Handle;
    PKTHREAD Worker;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "a switching thread needs a processor of its own\n");
        return;
    }
    if (!StartWorker(&State, TRUE, &Handle, &Worker))
        return;

    for (Iteration = 0; Iteration < SWITCH_QUERIES; Iteration++)
    {
        Cycles = KeQueryTotalCycleTimeThread(Worker, &Stamp);
        if (Cycles < Previous)
            Decreases++;
        Previous = Cycles;
    }
    ok_eq_ulong(Decreases, 0UL);
    ok(Previous > 0, "the switching thread was charged no cycles\n");

    StopWorker(&State, Handle, Worker);
}

START_TEST(KeCycleTime)
{
    TestRaisedIrql();
    TestSwitchingThread();
}
