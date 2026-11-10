/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux signal queuing, delivery and return
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ndk/kefuncs.h>

#define NDEBUG
#include <debug.h>

#define LX_MINSIGSTKSZ      6144
#define LX_UNBLOCKABLE      ((1ULL << (LX_SIGKILL - 1)) | (1ULL << (LX_SIGSTOP - 1)))
#define LX_SIGNAL_BIT(Sig)  (1ULL << ((Sig) - 1))
#define LX_STOP_SIGNALS     (LX_SIGNAL_BIT(LX_SIGSTOP) | LX_SIGNAL_BIT(LX_SIGTSTP) | LX_SIGNAL_BIT(LX_SIGTTIN) | \
                             LX_SIGNAL_BIT(LX_SIGTTOU))
#define LX_MAX_WAKE         32

BOOLEAN
LxSignalIgnoredByDefault(
    _In_ LONG Signal)
{
    switch (Signal)
    {
        case LX_SIGCHLD:
        case LX_SIGCONT:
        case LX_SIGURG:
        case LX_SIGWINCH:
            return TRUE;
        default:
            return FALSE;
    }
}

BOOLEAN
LxSignalStopsByDefault(
    _In_ LONG Signal)
{
    return (Signal > 0) && (Signal <= LX_NSIG) && ((LX_SIGNAL_BIT(Signal) & LX_STOP_SIGNALS) != 0);
}

VOID
LxThreadAttach(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;

    Thread->EThread = PsGetCurrentThread();
    ObReferenceObject(Thread->EThread);
    Thread->NtTid = HandleToUlong(PsGetCurrentThreadId());

    ExAcquireFastMutex(&Process->Lock);
    InsertTailList(&Process->Threads, &Thread->Link);
    InterlockedExchange(&Thread->SharedWake, 1);
    if (Process->GroupStopCount != 0)
    {
        Process->GroupStopCount++;
        Thread->JobctlStop = TRUE;
    }
    else if (Process->JobctlFlags & LX_JOBCTL_STOPPED)
    {
        Thread->JobctlStop = TRUE;
    }
    ExReleaseFastMutex(&Process->Lock);
    LxSchedApplyAffinity(Thread);
}

BOOLEAN
LxThreadDetach(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;
    BOOLEAN Last;

    if (Thread->EThread == NULL)
        return FALSE;

    ExAcquireFastMutex(&Process->Lock);
    RemoveEntryList(&Thread->Link);
    InitializeListHead(&Thread->Link);
    Last = IsListEmpty(&Process->Threads);
    ExReleaseFastMutex(&Process->Lock);
    if (!Last && (InterlockedExchange(&Thread->SharedWake, 0) != 0))
        LxSignalRetarget(Process, NULL, ~0ULL);

    ObDereferenceObject(Thread->EThread);
    Thread->EThread = NULL;
    return Last;
}

static
VOID
NTAPI
LxSignalApcKernel(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

static
VOID
NTAPI
LxSignalApcRundown(
    _In_ PKAPC Apc)
{
    LxFree(Apc);
}

static
VOID
NTAPI
LxSignalApcNormal(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

static
VOID
LxSignalQueueApc(
    _In_ PLX_THREAD Base,
    _In_ PKAPC Apc)
{
    KeInitializeApc(Apc, (PKTHREAD)Base->EThread, OriginalApcEnvironment, LxSignalApcKernel, LxSignalApcRundown,
                    LxSignalApcNormal, KernelMode, Apc);
    if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT))
    {
        LxFree(Apc);
        InterlockedExchange(&Base->SignalApc, 0);
    }
}

static
VOID
NTAPI
LxSignalRetryExpired(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    PLX_THREAD Base = Context;
    PKAPC Apc = LxAlloc(sizeof(KAPC));

    UNREFERENCED_PARAMETER(Timer);
    if (Apc == NULL)
        InterlockedExchange(&Base->SignalApc, 0);
    else
        LxSignalQueueApc(Base, Apc);
}

static
VOID
NTAPI
LxSignalApcNormal(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PLX_THREAD Base, Thread;

    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    LxFree(NormalContext);
    if (PsIsThreadTerminating(PsGetCurrentThread()))
        return;
    Base = PsPicoGetThreadContext(PsGetCurrentThread());
    if (Base == NULL)
        return;
    Thread = LxCurrentThread();
    if ((Base->InKernel != 0) || (Thread == NULL) || Thread->VforkExited)
    {
        InterlockedExchange(&Base->SignalApc, 0);
        return;
    }
    /* SignalApc stays set while this runs: it may end the thread, and an APC queued meanwhile could never run */
    if (!LxSignalPending(Thread) || LxArchDeliverAsync(Thread))
    {
        InterlockedExchange(&Base->SignalApc, 0);
        if (LxSignalPending(Thread) && (InterlockedExchange(&Base->SignalApc, 1) == 0))
            LxSignalRetryExpired(NULL, Base);
        return;
    }
    if (Base->SignalRetry == NULL)
        Base->SignalRetry = ExAllocateTimer(LxSignalRetryExpired, Base, EX_TIMER_HIGH_RESOLUTION);
    if (Base->SignalRetry != NULL)
        ExSetTimer(Base->SignalRetry, -1000, 0, NULL);
    else
        InterlockedExchange(&Base->SignalApc, 0);
}

static
VOID
LxSignalWake(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Tid)
{
    PETHREAD LocalThreads[LX_MAX_WAKE];
    PKAPC LocalApcs[LX_MAX_WAKE];
    PETHREAD *Threads = LocalThreads;
    PKAPC *Apcs = LocalApcs;
    PVOID Buffer = NULL;
    PETHREAD Current = PsGetCurrentThread();
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index, Capacity = LX_MAX_WAKE;

    if ((Tid == 0) && (ReadNoFence(&Process->ThreadCount) + 8 > LX_MAX_WAKE))
    {
        Capacity = (ULONG)ReadNoFence(&Process->ThreadCount) + 8;
        Buffer = LxAlloc(Capacity * (sizeof(PETHREAD) + sizeof(PKAPC)));
        if (Buffer != NULL)
        {
            Threads = Buffer;
            Apcs = (PKAPC *)(Threads + Capacity);
        }
        else
        {
            /* FIXME: only LX_MAX_WAKE threads are woken when this allocation fails */
            Capacity = LX_MAX_WAKE;
        }
    }

    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; (Entry != &Process->Threads) && (Count < Capacity); Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);
        PLX_THREAD Base;

        if (Tid == 0)
            InterlockedExchange(&Thread->SharedWake, 1);
        if ((Thread->EThread == Current) || ((Tid != 0) && (Thread->Tid != Tid)))
            continue;
        Base = PsPicoGetThreadContext(Thread->EThread);
        Apcs[Count] = NULL;
        if ((Base != NULL) && !PsIsThreadTerminating(Thread->EThread) &&
            (InterlockedExchange(&Base->SignalApc, 1) == 0) && ((Apcs[Count] = LxAlloc(sizeof(KAPC))) == NULL))
        {
            InterlockedExchange(&Base->SignalApc, 0);
        }
        ObReferenceObject(Thread->EThread);
        Threads[Count++] = Thread->EThread;
    }
    ExReleaseFastMutex(&Process->Lock);

    for (Index = 0; Index < Count; Index++)
    {
        HANDLE Handle;

        if (NT_SUCCESS(ObOpenObjectByPointer(Threads[Index], OBJ_KERNEL_HANDLE, NULL, THREAD_ALERT,
                                             *PsThreadType, KernelMode, &Handle)))
        {
            ZwAlertThread(Handle);
            ZwClose(Handle);
        }
        if (Apcs[Index] != NULL)
        {
            KeInitializeApc(Apcs[Index], (PKTHREAD)Threads[Index], OriginalApcEnvironment, LxSignalApcKernel,
                            LxSignalApcRundown, LxSignalApcNormal, KernelMode, Apcs[Index]);
            if (!KeInsertQueueApc(Apcs[Index], NULL, NULL, IO_NO_INCREMENT))
                LxFree(Apcs[Index]);
        }
        ObDereferenceObject(Threads[Index]);
    }
    if (Buffer != NULL)
        LxFree(Buffer);
}

static
VOID
LxSignalTerminate(
    _In_ PLX_PROCESS Target,
    _In_ LONG Signal)
{
    PEPROCESS EProcess;
    HANDLE Handle;

    if (Target->Trace || (Target->Pid == 1))
        DPRINT1("Linux process %lu (%s) is killed by signal %ld\n", Target->Pid, Target->Comm, Signal);
    if ((Target->Mm != Target) || (Target->VmUsers != 0))
    {
        PLX_THREAD Current = LxCurrentThread();

        if ((Current != NULL) && (Current->Process == Target))
        {
            LxProcessExitGroup(Target, Signal);
            return;
        }
        LxProcessExitStatus(Target, Signal);
        if (Target->SharedVm || (Target->Mm == Target))
        {
            LxSharedVmTerminate(Target, 128 + Signal, FALSE);
            return;
        }
        InterlockedOr64(&Target->Pending, (LONG64)LX_SIGNAL_BIT(LX_SIGKILL));
        LxSignalWake(Target, 0);
        return;
    }
    if (Target->NtPid == HandleToUlong(PsGetCurrentProcessId()))
        LxProcessExitGroup(Target, Signal);

    LxProcessExitStatus(Target, Signal);
    if (!NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Target->NtPid), &EProcess)))
        return;
    if (NT_SUCCESS(ObOpenObjectByPointer(EProcess, OBJ_KERNEL_HANDLE, NULL, PROCESS_TERMINATE,
                                         *PsProcessType, KernelMode, &Handle)))
    {
        ZwTerminateProcess(Handle, 128 + Signal);
        ZwClose(Handle);
    }
    ObDereferenceObject(EProcess);
    InterlockedOr64(&Target->Pending, (LONG64)LX_SIGNAL_BIT(LX_SIGKILL));
    LxSignalWake(Target, 0);
}

static
BOOLEAN
LxSignalTakenByCurrent(
    _In_ PLX_PROCESS Target,
    _In_opt_ PLX_THREAD TargetThread,
    _In_ ULONG64 Bit)
{
    PLX_THREAD Current = LxCurrentThread();

    return (Current != NULL) && (Current->Process == Target) && (Target->Mm == Target) &&
           ((TargetThread == NULL) || (TargetThread == Current)) && !(Current->SigMask & Bit);
}

static
BOOLEAN
LxSignalBlocked(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_THREAD TargetThread,
    _In_ ULONG64 Bit)
{
    PLIST_ENTRY Entry;
    BOOLEAN Blocked = TRUE;

    if (TargetThread != NULL)
        return (TargetThread->SigMask & Bit) != 0;

    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if (!(Thread->SigMask & Bit))
        {
            Blocked = FALSE;
            break;
        }
    }
    if (IsListEmpty(&Process->Threads))
        Blocked = FALSE;
    ExReleaseFastMutex(&Process->Lock);
    return Blocked;
}

static
BOOLEAN
LxSignalWants(
    _In_ PLX_THREAD Thread,
    _In_ ULONG64 Bits)
{
    return (Thread->EThread != NULL) && !PsIsThreadTerminating(Thread->EThread) && !Thread->Stopped &&
           ((Bits & ~Thread->SigMask) != 0);
}

static
ULONG
LxSignalPickThread(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Bit)
{
    PLIST_ENTRY Entry;
    PLX_THREAD Picked = NULL;
    ULONG Tid = 0;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
    ASSERT((Bit != 0) && ((Bit & (Bit - 1)) == 0));
    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if (!LxSignalWants(Thread, Bit))
            continue;
        if (Thread->Tid == Process->Pid)
        {
            Picked = Thread;
            break;
        }
        if (Picked == NULL)
            Picked = Thread;
    }
    if (Picked != NULL)
    {
        InterlockedExchange(&Picked->SharedWake, 1);
        Tid = Picked->Tid;
    }
    ExReleaseFastMutex(&Process->Lock);
    return Tid;
}

VOID
LxSignalRetarget(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_THREAD Thread,
    _In_ ULONG64 Bits)
{
    PLIST_ENTRY Entry;
    ULONG64 Left;
    ULONG Tids[LX_MAX_WAKE];
    ULONG Count = 0, Index;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
    ASSERT((Thread == NULL) || (Thread->Process == Process));
    ExAcquireFastMutex(&Process->Lock);
    Left = (ULONG64)ReadNoFence64(&Process->Pending) & Bits;
    for (Entry = Process->Threads.Flink; (Entry != &Process->Threads) && (Left != 0) && (Count < LX_MAX_WAKE);
         Entry = Entry->Flink)
    {
        PLX_THREAD Other = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if ((Other == Thread) || !LxSignalWants(Other, Left))
            continue;
        Left &= Other->SigMask;
        InterlockedExchange(&Other->SharedWake, 1);
        Tids[Count++] = Other->Tid;
    }
    ExReleaseFastMutex(&Process->Lock);
    for (Index = 0; Index < Count; Index++)
        LxSignalWake(Process, Tids[Index]);
}

VOID
LxSignalRecheck(
    _In_ PLX_THREAD Base,
    _In_ PLX_THREAD Thread)
{
    ASSERT(Base->InKernel == 0);
    if (!Thread->VforkExited && LxSignalPending(Thread) && (InterlockedExchange(&Base->SignalApc, 1) == 0))
        LxSignalRetryExpired(NULL, Base);
}

ULONG64
LxSignalPendingSet(
    _In_ PLX_THREAD Thread)
{
    ULONG64 Pending = (ULONG64)ReadNoFence64(&Thread->Pending);

    if (ReadNoFence(&Thread->SharedWake) != 0)
        Pending |= (ULONG64)ReadNoFence64(&Thread->Process->Pending);
    return Pending;
}

VOID
LxSignalNotifyFds(
    _In_ PLX_PROCESS Process)
{
    LxPollWake(&Process->SignalPoll, LX_POLLIN | LX_POLLRDNORM);
}

BOOLEAN
LxSignalInfoHasUid(
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info)
{
    if (Info->Code == LX_SI_USER)
        return TRUE;
    if (Info->Code < 0)
        return (Info->Code != LX_SI_TIMER) && (Info->Code != LX_SI_SIGIO);
    return (Signal == LX_SIGCHLD) && (Info->Code <= LX_CLD_CONTINUED);
}

VOID
LxSignalWakeThread(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Tid)
{
    LxSignalWake(Process, Tid);
}

static
VOID
LxJobctlWakeStopping(
    _In_ PLX_PROCESS Process)
{
    PLIST_ENTRY Entry;
    PULONG Tids;
    ULONG Count = 0, Capacity, Index;

    Capacity = (ULONG)max(ReadNoFence(&Process->ThreadCount), 1) + 8;
    Tids = LxAlloc(sizeof(ULONG) * Capacity);
    if (Tids == NULL)
    {
        LxSignalWake(Process, 0);
        return;
    }
    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; (Entry != &Process->Threads) && (Count < Capacity); Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if (Thread->JobctlStop)
            Tids[Count++] = Thread->Tid;
    }
    ExReleaseFastMutex(&Process->Lock);
    for (Index = 0; Index < Count; Index++)
        LxSignalWake(Process, Tids[Index]);
    LxFree(Tids);
}

static
VOID
LxJobctlNotifyParent(
    _In_ PLX_PROCESS Child,
    _In_ LONG Why,
    _In_ LONG Status)
{
    PLX_PROCESS Parent = (Child->ParentPid != 0) ? LxProcessLookup(Child->Instance, Child->ParentPid) : NULL;
    ULONG64 Handler, Flags;
    LX_SIGINFO Info;

    if (Parent == NULL)
        return;
    if (!Parent->Exited)
    {
        ExAcquireFastMutex(&Parent->Lock);
        Handler = Parent->SigAction[LX_SIGCHLD - 1].Handler;
        Flags = Parent->SigAction[LX_SIGCHLD - 1].Flags;
        ExReleaseFastMutex(&Parent->Lock);
        if ((Handler != LX_SIG_IGN) && !(Flags & LX_SA_NOCLDSTOP))
        {
            RtlZeroMemory(&Info, sizeof(Info));
            Info.Code = Why;
            Info.Child.Pid = (LONG)LxPidNr(Parent, Child);
            Info.Child.Uid = Child->Cred.Uid[0];
            Info.Child.Status = Status;
            LxSignalSend(Parent, NULL, LX_SIGCHLD, &Info);
        }
        LxPollWake(&Parent->ChildPoll, LX_POLLIN);
    }
    LxProcessDereference(Parent);
}

static
VOID
LxJobctlPrepareStop(
    _In_ PLX_PROCESS Target)
{
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&Target->Lock);
    InterlockedAnd64(&Target->Pending, ~(LONG64)LX_SIGNAL_BIT(LX_SIGCONT));
    for (Entry = Target->Threads.Flink; Entry != &Target->Threads; Entry = Entry->Flink)
        InterlockedAnd64(&CONTAINING_RECORD(Entry, LX_THREAD, Link)->Pending, ~(LONG64)LX_SIGNAL_BIT(LX_SIGCONT));
    ExReleaseFastMutex(&Target->Lock);
}

static
VOID
LxJobctlContinue(
    _In_ PLX_PROCESS Target)
{
    PLIST_ENTRY Entry;
    LONG Why = 0;

    ExAcquireFastMutex(&Target->Lock);
    InterlockedAnd64(&Target->Pending, ~(LONG64)LX_STOP_SIGNALS);
    for (Entry = Target->Threads.Flink; Entry != &Target->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        InterlockedAnd64(&Thread->Pending, ~(LONG64)LX_STOP_SIGNALS);
        Thread->JobctlStop = FALSE;
    }
    if (Target->JobctlFlags & LX_JOBCTL_STOPPED)
        Why = LX_CLD_CONTINUED;
    else if (Target->GroupStopCount != 0)
        Why = LX_CLD_STOPPED;
    if (Why != 0)
    {
        Target->JobctlFlags = LX_JOBCTL_CONTINUED;
        Target->GroupStopCount = 0;
        Target->GroupExitCode = 0;
    }
    InterlockedIncrement(&Target->ContinueGeneration);
    KeSetEvent(&Target->ContinueEvent, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&Target->Lock);
    if (Why != 0)
        LxJobctlNotifyParent(Target, Why, (Why == LX_CLD_CONTINUED) ? LX_SIGCONT : 0);
}

static
VOID
LxJobctlStop(
    _In_ PLX_THREAD Thread,
    _In_ LONG Signal,
    _In_ LONG Dequeued,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PLX_PROCESS Process = Thread->Process;
    PLIST_ENTRY Entry;
    BOOLEAN Notify = FALSE, Others = FALSE;
    LONG Generation, ExitCode;

    ExAcquireFastMutex(&Process->Lock);
    if ((Signal != 0) && !Process->WaitStatusValid && (Process->ContinueGeneration == Dequeued))
    {
        if (!(Process->JobctlFlags & LX_JOBCTL_STOPPED))
            Process->GroupExitCode = Signal;
        Process->GroupStopCount = 0;
        for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
        {
            PLX_THREAD Other = CONTAINING_RECORD(Entry, LX_THREAD, Link);

            if ((Other == Thread) || Other->Stopped)
                continue;
            Other->JobctlStop = TRUE;
            Process->GroupStopCount++;
            Others = TRUE;
        }
        Thread->JobctlStop = TRUE;
        Process->GroupStopCount++;
        KeClearEvent(&Process->ContinueEvent);
    }
    if (!Thread->JobctlStop || Process->WaitStatusValid)
    {
        Thread->JobctlStop = FALSE;
        ExReleaseFastMutex(&Process->Lock);
        return;
    }
    Thread->JobctlStop = FALSE;
    if (Process->GroupStopCount != 0)
        Process->GroupStopCount--;
    if ((Process->GroupStopCount == 0) && !(Process->JobctlFlags & LX_JOBCTL_STOPPED))
    {
        Process->JobctlFlags = LX_JOBCTL_STOPPED;
        Notify = TRUE;
    }
    ExitCode = Process->GroupExitCode;
    Thread->Stopped = (Thread->Ptrace == NULL);
    Generation = Process->ContinueGeneration;
    ExReleaseFastMutex(&Process->Lock);

    if (Others)
        LxJobctlWakeStopping(Process);
    if (Notify)
        LxJobctlNotifyParent(Process, LX_CLD_STOPPED, ExitCode & 0x7F);
    if (LxPtraceGroupStop(Thread, TrapFrame, ExceptionFrame, ExitCode, Generation))
    {
        Thread->Stopped = FALSE;
        return;
    }
    Thread->Stopped = TRUE;
    while ((ReadNoFence(&Process->ContinueGeneration) == Generation) &&
           !(((ULONG64)Thread->Pending | (ULONG64)Process->Pending) & LX_SIGNAL_BIT(LX_SIGKILL)) &&
           !(Thread->PtraceWork & LX_PTW_TRAP) && !PsIsThreadTerminating(PsGetCurrentThread()))
    {
        if (KeWaitForSingleObject(&Process->ContinueEvent, UserRequest, UserMode, TRUE, NULL) == STATUS_USER_APC)
            break;
    }
    Thread->Stopped = FALSE;
}

static
BOOLEAN
LxPgrpHasStopped(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Pgid)
{
    PLIST_ENTRY Entry;
    BOOLEAN Stopped = FALSE;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Instance == Instance) && (Process->Pgid == Pgid) && !Process->Exited &&
            (ReadNoFence((PLONG)&Process->JobctlFlags) & LX_JOBCTL_STOPPED))
        {
            Stopped = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&LxProcessListLock);
    return Stopped;
}

BOOLEAN
LxPgrpOrphaned(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Pgid,
    _In_opt_ PLX_PROCESS Ignored)
{
    ULONG ParentPids[128], Sids[128];
    ULONG Count = 0, Index;
    PLIST_ENTRY Entry;
    BOOLEAN Orphaned = TRUE;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Count < RTL_NUMBER_OF(ParentPids));
         Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Instance != Instance) || (Process->Pgid != Pgid) || (Process == Ignored) || Process->Exited)
            continue;
        ParentPids[Count] = Process->ParentPid;
        Sids[Count++] = Process->Sid;
    }
    ExReleaseFastMutex(&LxProcessListLock);

    for (Index = 0; (Index < Count) && Orphaned; Index++)
    {
        PLX_PROCESS Parent;

        if (ParentPids[Index] <= 1)
            continue;
        Parent = LxProcessLookup(Instance, ParentPids[Index]);
        if (Parent == NULL)
            continue;
        if (!Parent->Exited && (Parent->Pgid != Pgid) && (Parent->Sid == Sids[Index]))
            Orphaned = FALSE;
        LxProcessDereference(Parent);
    }
    return Orphaned;
}

VOID
LxJobctlOrphanCheck(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_PROCESS Father)
{
    PLX_PROCESS Parent = Father;
    LX_SIGINFO Info;
    ULONG Pgid = Process->Pgid;

    if (Pgid == 0)
        return;
    if (Father == NULL)
    {
        Parent = (Process->ParentPid != 0) ? LxProcessLookup(Process->Instance, Process->ParentPid) : NULL;
        if (Parent == NULL)
            return;
    }
    if ((Parent->Pgid != Pgid) && (Parent->Sid == Process->Sid) &&
        LxPgrpOrphaned(Process->Instance, Pgid, (Father == NULL) ? Process : NULL) &&
        LxPgrpHasStopped(Process->Instance, Pgid))
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_KERNEL;
        LxSignalGroupInfo(Process->Instance, NULL, Pgid, LX_SIGHUP, &Info, NULL, NULL);
        LxSignalGroupInfo(Process->Instance, NULL, Pgid, LX_SIGCONT, &Info, NULL, NULL);
    }
    if (Father == NULL)
        LxProcessDereference(Parent);
}

VOID
LxSignalSend(
    _In_ PLX_PROCESS Target,
    _In_opt_ PLX_THREAD TargetThread,
    _In_ LONG Signal,
    _In_opt_ PLX_SIGINFO Info)
{
    ULONG64 Handler, Bit = LX_SIGNAL_BIT(Signal);
    ULONG Tid = (TargetThread != NULL) ? TargetThread->Tid : 0;
    PLX_THREAD Sender = LxCurrentThread();
    LX_SIGINFO LocalInfo;
    BOOLEAN Blocked;

    if ((Signal <= 0) || (Signal > LX_NSIG))
        return;

    if (Target->Exited)
        return;

    ExAcquireFastMutex(&Target->Lock);
    Handler = Target->SigAction[Signal - 1].Handler;
    ExReleaseFastMutex(&Target->Lock);

    Blocked = (Signal != LX_SIGKILL) && (Signal != LX_SIGSTOP) && LxSignalBlocked(Target, TargetThread, Bit);
    if (LxSignalStopsByDefault(Signal))
        LxJobctlPrepareStop(Target);
    else if (Signal == LX_SIGCONT)
        LxJobctlContinue(Target);

    if ((Target->Pid == 1) && (Sender != NULL) && (Sender->Process->Instance == Target->Instance) &&
        (Handler == LX_SIG_DFL) && !Blocked)
    {
        return;
    }
    if (LxPidNsIsInit(Target) && (Sender != NULL) && (Sender->Process->Instance == Target->Instance) &&
        (Handler == LX_SIG_DFL) && !Blocked)
    {
        if (((Sender->Process->PidNs != NULL) && LxPidNsContains(Target->PidNs, Sender->Process->PidNs)) ||
            ((Signal != LX_SIGKILL) && (Signal != LX_SIGSTOP)))
        {
            return;
        }
    }

    if (!Blocked && (Signal != LX_SIGKILL) && (ReadNoFence(&Target->Traced) == 0))
    {
        if ((Handler == LX_SIG_IGN) || ((Handler == LX_SIG_DFL) && LxSignalIgnoredByDefault(Signal)))
            return;
    }
    if (!Blocked &&
        ((Signal == LX_SIGKILL) ||
         ((Handler == LX_SIG_DFL) && !LxSignalStopsByDefault(Signal) && (ReadNoFence(&Target->Traced) == 0))) &&
        !((Signal != LX_SIGKILL) && LxSignalDumpsCore(Signal) && LxSignalTakenByCurrent(Target, TargetThread, Bit)))
    {
        if ((Signal == LX_SIGKILL) && (ReadNoFence(&Target->Traced) != 0) && (Target->Mm == Target) &&
            (Target->VmUsers == 0))
        {
            if (LxProcessExitStatus(Target, LX_SIGKILL))
                LxProcessZapThreads(Target, NULL);
            return;
        }
        LxSignalTerminate(Target, Signal);
        return;
    }

    if ((Info != NULL) && (Target->PidNs != NULL) && (Target->PidNs->Level > 0) &&
        ((Info->Code == LX_SI_USER) || (Info->Code == LX_SI_TKILL)))
    {
        LocalInfo = *Info;
        LocalInfo.Child.Pid = (LONG)LxGlobalToNrIn(Target->PidNs, (ULONG)Info->Child.Pid);
        Info = &LocalInfo;
    }

    ExAcquireFastMutex(&Target->Lock);
    if (Info != NULL)
    {
        Target->PendingInfo[Signal - 1] = *Info;
        if (LxSignalInfoHasUid(Signal, Info))
        {
            Target->PendingInfo[Signal - 1].Child.Uid = LxIdFromKernelMunged(LxCredUserNs(Target), FALSE,
                                                                             Info->Child.Uid);
        }
    }
    else
    {
        RtlZeroMemory(&Target->PendingInfo[Signal - 1], sizeof(LX_SIGINFO));
    }
    Target->PendingInfo[Signal - 1].Signo = Signal;
    if (TargetThread != NULL)
        InterlockedOr64(&TargetThread->Pending, (LONG64)Bit);
    else
        InterlockedOr64(&Target->Pending, (LONG64)Bit);
    ExReleaseFastMutex(&Target->Lock);

    LxSignalNotifyFds(Target);
    if (TargetThread == NULL)
    {
        Tid = LxSignalPickThread(Target, Bit);
        if (Tid == 0)
            return;
    }
    LxSignalWake(Target, Tid);
}

ULONG64
LxSignalDequeue(
    _In_ PLX_THREAD Thread,
    _In_ ULONG64 Mask,
    _Out_ PLX_SIGINFO Info)
{
    PLX_PROCESS Process = Thread->Process;
    LONG Signal;

    RtlZeroMemory(Info, sizeof(*Info));
    for (Signal = 1; Signal <= LX_NSIG; Signal++)
    {
        ULONG64 Bit = LX_SIGNAL_BIT(Signal);

        if (!(Mask & Bit))
            continue;
        if ((ULONG64)Thread->Pending & Bit)
        {
            if (!((ULONG64)InterlockedAnd64(&Thread->Pending, ~(LONG64)Bit) & Bit))
                continue;
        }
        else if ((ULONG64)Process->Pending & Bit)
        {
            if (!((ULONG64)InterlockedAnd64(&Process->Pending, ~(LONG64)Bit) & Bit))
                continue;
        }
        else
        {
            continue;
        }

        ExAcquireFastMutex(&Process->Lock);
        *Info = Process->PendingInfo[Signal - 1];
        ExReleaseFastMutex(&Process->Lock);
        Info->Signo = Signal;
        return (ULONG64)Signal;
    }
    return 0;
}

static
BOOLEAN
LxSignalDeliver(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info,
    _In_ PLX_SIGACTION Action)
{
    PLX_PROCESS Process = Thread->Process;
    ULONG64 OldMask = Thread->Suspended ? Thread->SuspendMask : Thread->SigMask;
    ULONG64 StackTop = LxArchGetUserSp(TrapFrame);
    ULONG64 Restorer = (Action->Flags & LX_SA_RESTORER) ? Action->Restorer : Process->SigTrampoline;
    LX_STACK AltStack = Thread->AltStack;

    if ((Action->Flags & LX_SA_ONSTACK) && (AltStack.Size != 0) && !(AltStack.Flags & LX_SS_DISABLE) &&
        !((StackTop > AltStack.Sp) && (StackTop <= AltStack.Sp + AltStack.Size)))
    {
        StackTop = AltStack.Sp + AltStack.Size;
    }

    if ((AltStack.Size != 0) && (StackTop > AltStack.Sp) && (StackTop <= AltStack.Sp + AltStack.Size))
        AltStack.Flags |= LX_SS_ONSTACK;
    if (AltStack.Size == 0)
        AltStack.Flags = LX_SS_DISABLE;

    if (!LxArchSetupSignalFrame(TrapFrame, ExceptionFrame, StackTop, Signal, Info, Action, Restorer, OldMask, &AltStack))
        return FALSE;

    Thread->Suspended = FALSE;
    Thread->SigMask = OldMask | Action->Mask;
    if (!(Action->Flags & LX_SA_NODEFER))
        Thread->SigMask |= LX_SIGNAL_BIT(Signal);
    Thread->SigMask &= ~LX_UNBLOCKABLE;
    return TRUE;
}

static
VOID
LxSignalTakeAction(
    _In_ PLX_PROCESS Process,
    _In_ LONG Signal,
    _Out_ PLX_SIGACTION Action,
    _Out_opt_ PLX_SIGINFO Info)
{
    ExAcquireFastMutex(&Process->Lock);
    *Action = Process->SigAction[Signal - 1];
    if (Info != NULL)
        *Info = Process->PendingInfo[Signal - 1];
    if ((Action->Flags & LX_SA_RESETHAND) && (Action->Handler > LX_SIG_IGN))
        Process->SigAction[Signal - 1].Handler = LX_SIG_DFL;
    ExReleaseFastMutex(&Process->Lock);
}

VOID
LxSignalCheckPending(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PLX_PROCESS Process = Thread->Process;
    LONG64 Restart = Thread->Restart;
    ULONG64 Blocked;
    BOOLEAN Shared = FALSE;

    Thread->Restart = 0;
    for (;;)
    {
        ULONG64 Deliverable;
        LX_SIGACTION Action;
        LX_SIGINFO Info;
        ULONG64 Bit;
        LONG Signal = 1, Generation;

        if (InterlockedExchange(&Thread->SharedWake, 0) != 0)
            Shared = TRUE;
        if (Thread->PtraceWork & LX_PTW_TRAP)
        {
            LxPtraceTrap(Thread, TrapFrame, ExceptionFrame);
            Shared = TRUE;
            continue;
        }
        if (Thread->JobctlStop)
        {
            LxJobctlStop(Thread, 0, 0, TrapFrame, ExceptionFrame);
            Shared = TRUE;
            continue;
        }
        Deliverable = ((ULONG64)Thread->Pending | (Shared ? (ULONG64)Process->Pending : 0)) & ~Thread->SigMask;
        if (Deliverable == 0)
            break;

        while (!(Deliverable & LX_SIGNAL_BIT(Signal)))
            Signal++;
        Bit = LX_SIGNAL_BIT(Signal);

        Generation = ReadNoFence(&Process->ContinueGeneration);
        if ((ULONG64)Thread->Pending & Bit)
            InterlockedAnd64(&Thread->Pending, ~(LONG64)Bit);
        else if (!((ULONG64)InterlockedAnd64(&Process->Pending, ~(LONG64)Bit) & Bit))
            continue;

        if ((Thread->Ptrace != NULL) && (Signal != LX_SIGKILL))
        {
            ExAcquireFastMutex(&Process->Lock);
            Info = Process->PendingInfo[Signal - 1];
            ExReleaseFastMutex(&Process->Lock);
            Info.Signo = Signal;
            Signal = LxPtraceSignal(Thread, TrapFrame, ExceptionFrame, Signal, &Info);
            if (Signal == 0)
                continue;
            Bit = LX_SIGNAL_BIT(Signal);
            LxSignalTakeAction(Process, Signal, &Action, NULL);
        }
        else
        {
            LxSignalTakeAction(Process, Signal, &Action, &Info);
        }
        if ((Action.Handler == LX_SIG_IGN) && (Signal != LX_SIGKILL))
            continue;
        if (Action.Handler == LX_SIG_DFL)
        {
            if (LxSignalIgnoredByDefault(Signal) ||
                (((Process->Pid == 1) || LxPidNsIsInit(Process)) && (Signal != LX_SIGSTOP)))
            {
                continue;
            }
            if (LxSignalStopsByDefault(Signal))
            {
                if ((Signal == LX_SIGSTOP) || !LxPgrpOrphaned(Process->Instance, Process->Pgid, NULL))
                    LxJobctlStop(Thread, Signal, Generation, TrapFrame, ExceptionFrame);
                continue;
            }
            if (LxSignalDumpsCore(Signal) && LxCoreDump(Thread, TrapFrame, ExceptionFrame, Signal))
                Signal |= 0x80;
            LxPtraceEvent(Thread, TrapFrame, ExceptionFrame, LX_PTRACE_EVENT_EXIT,
                          Process->WaitStatusValid ? (ULONG64)Process->WaitStatus : (ULONG64)Signal, FALSE);
            LxProcessExitGroup(Process, Signal);
            return;
        }

        if ((Restart == -LX_ERESTARTNOINTR) || ((Restart == -LX_ERESTARTSYS) && (Action.Flags & LX_SA_RESTART)))
            LxArchRestartSyscall(TrapFrame, Thread->RestartArgument);
        Blocked = Thread->SigMask;
        if (!LxSignalDeliver(Thread, TrapFrame, ExceptionFrame, Signal, &Info, &Action))
            LxProcessExitGroup(Process, LX_SIGSEGV);
        if (Shared && ((ULONG64)Process->Pending & ~Thread->SigMask))
            InterlockedExchange(&Thread->SharedWake, 1);
        if (Thread->SigMask & ~Blocked)
            LxSignalRetarget(Process, Thread, Thread->SigMask & ~Blocked);
        return;
    }

    if (Restart != 0)
        LxArchRestartSyscall(TrapFrame, Thread->RestartArgument);

    if (Thread->Suspended)
    {
        Blocked = Thread->SigMask;
        Thread->SigMask = Thread->SuspendMask;
        Thread->Suspended = FALSE;
        InterlockedExchange(&Thread->SharedWake, 1);
        if (Thread->SigMask & ~Blocked)
            LxSignalRetarget(Process, Thread, Thread->SigMask & ~Blocked);
    }
}

VOID
LxSignalForce(
    _In_ PLX_THREAD Thread,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info)
{
    PLX_PROCESS Process = Thread->Process;
    ULONG64 Bit = LX_SIGNAL_BIT(Signal);

    ExAcquireFastMutex(&Process->Lock);
    if ((Thread->SigMask & Bit) || (Process->SigAction[Signal - 1].Handler == LX_SIG_IGN))
    {
        Process->SigAction[Signal - 1].Handler = LX_SIG_DFL;
        Thread->SigMask &= ~Bit;
    }
    Process->PendingInfo[Signal - 1] = *Info;
    InterlockedOr64(&Thread->Pending, (LONG64)Bit);
    ExReleaseFastMutex(&Process->Lock);
}

BOOLEAN
LxSignalDeliverFault(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Signal,
    _In_ PLX_SIGINFO Info)
{
    LX_SIGACTION Action;

    if (Thread->SigMask & LX_SIGNAL_BIT(Signal))
        return FALSE;

    LxSignalTakeAction(Thread->Process, Signal, &Action, NULL);
    if ((Action.Handler == LX_SIG_DFL) || (Action.Handler == LX_SIG_IGN))
        return FALSE;

    return LxSignalDeliver(Thread, TrapFrame, ExceptionFrame, Signal, Info, &Action);
}

LX_SYSCALL(LxSysRtSigreturn)
{
    ULONG64 Mask;

    if (LxArchSignalReturn(Call->TrapFrame, Call->ExceptionFrame, &Mask) != 0)
        LxProcessExitGroup(Call->Process, LX_SIGSEGV);

    Call->Thread->SigMask = Mask & ~LX_UNBLOCKABLE;
    InterlockedExchange(&Call->Thread->SharedWake, 1);
    return LX_SYSCALL_NO_RESULT;
}

LX_SYSCALL(LxSysRtSigsuspend)
{
    PLX_THREAD Thread = Call->Thread;
    LARGE_INTEGER Interval;
    ULONG64 Mask;
    LONG64 Result;

    if (Call->Args[1] != sizeof(ULONG64))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Mask, Call->Args[0], sizeof(Mask));
    if (Result != 0)
        return Result;

    Thread->SuspendMask = Thread->SigMask;
    Thread->Suspended = TRUE;
    Thread->SigMask = Mask & ~LX_UNBLOCKABLE;
    InterlockedExchange(&Thread->SharedWake, 1);

    Interval.QuadPart = -10000000LL;
    while (!(LxSignalPendingSet(Thread) & ~Thread->SigMask))
        KeDelayExecutionThread(UserMode, TRUE, &Interval);

    return -LX_EINTR;
}

LX_SYSCALL(LxSysRtSigpending)
{
    ULONG64 Pending = ((ULONG64)Call->Thread->Pending | (ULONG64)Call->Process->Pending) & Call->Thread->SigMask;

    if (Call->Args[1] > sizeof(ULONG64))
        return -LX_EINVAL;
    return LxCopyToUser(Call->Args[0], &Pending, (SIZE_T)Call->Args[1]);
}

typedef struct _LX_SIGWAIT_CONTEXT
{
    PLX_THREAD Thread;
    ULONG64 Mask;
} LX_SIGWAIT_CONTEXT, *PLX_SIGWAIT_CONTEXT;

static
BOOLEAN
LxSigwaitReady(
    _In_ PVOID Context)
{
    PLX_SIGWAIT_CONTEXT Wait = Context;

    return ((((ULONG64)Wait->Thread->Pending | (ULONG64)Wait->Thread->Process->Pending) & Wait->Mask) != 0) ||
           ((LxSignalPendingSet(Wait->Thread) & ~Wait->Thread->SigMask) != 0);
}

LX_SYSCALL(LxSysRtSigtimedwait)
{
    LX_SIGWAIT_CONTEXT Wait;
    LX_TIMESPEC Timeout;
    LARGE_INTEGER Deadline, Interval;
    LX_SIGINFO Info;
    ULONG64 Signal;
    LONG64 Result;
    BOOLEAN Interrupted = FALSE;

    if (Call->Args[3] != sizeof(ULONG64))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Wait.Mask, Call->Args[0], sizeof(Wait.Mask));
    if (Result != 0)
        return Result;
    Wait.Mask &= ~LX_UNBLOCKABLE;
    Wait.Thread = Call->Thread;

    Deadline.QuadPart = MAXLONGLONG;
    if (Call->Args[2] != 0)
    {
        Result = LxCopyFromUser(&Timeout, Call->Args[2], sizeof(Timeout));
        if (Result != 0)
            return Result;
        if ((Timeout.Seconds < 0) || (Timeout.Nanoseconds < 0) || (Timeout.Nanoseconds >= 1000000000))
            return -LX_EINVAL;
        Deadline.QuadPart = (LONG64)KeQueryInterruptTime() + LxTimespecTo100ns(&Timeout);
    }

    for (;;)
    {
        LONG64 Now;

        Signal = LxSignalDequeue(Call->Thread, Wait.Mask, &Info);
        if (Signal != 0)
        {
            if (Call->Args[1] != 0)
            {
                Result = LxCopyToUser(Call->Args[1], &Info, sizeof(Info));
                if (Result != 0)
                    return Result;
            }
            return (LONG64)Signal;
        }

        if (Interrupted || LxSignalPending(Call->Thread))
            return -LX_EINTR;

        Now = (LONG64)KeQueryInterruptTime();
        if (Now >= Deadline.QuadPart)
            return -LX_EAGAIN;
        Interval.QuadPart = (Deadline.QuadPart == MAXLONGLONG) ? -10000000LL : -(Deadline.QuadPart - Now);
        if (Interval.QuadPart < -10000000LL)
            Interval.QuadPart = -10000000LL;
        Result = LxWaitHead(&Call->Process->SignalPoll, LxSigwaitReady, &Wait, &Interval);
        if ((Result < 0) && (Result != -LX_EAGAIN) && (Result != -LX_EINTR))
            return Result;
        Interrupted = (Result == -LX_EINTR);
    }
}

static
LONG64
LxSignalQueue(
    _In_ PLX_CALL Call,
    _In_ ULONG Tgid,
    _In_ ULONG Tid,
    _In_ LONG Signal,
    _In_ ULONG64 InfoPointer)
{
    PLX_PROCESS Target;
    PLX_THREAD Thread = NULL;
    LX_SIGINFO Info;
    LONG64 Result;

    if ((Signal < 0) || (Signal > LX_NSIG))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Info, InfoPointer, sizeof(Info));
    if (Result != 0)
        return Result;
    if (((Info.Code >= 0) || (Info.Code == LX_SI_TKILL)) &&
        (Tgid != ((Tid != 0) ? LxPidNr(Call->Process, Call->Process) : LxTidNr(Call->Process, Call->Thread))))
        return -LX_EPERM;
    Info.Signo = Signal;
    if (LxSignalInfoHasUid(Signal, &Info))
        Info.Child.Uid = LxCredMakeId(Call->Process, FALSE, Info.Child.Uid);

    if (Tid != 0)
    {
        Thread = LxThreadLookupNr(Call->Process, Tid, &Target);
        if ((Thread == NULL) || (LxPidNr(Call->Process, Target) != Tgid))
        {
            if (Target != NULL)
                LxProcessDereference(Target);
            return -LX_ESRCH;
        }
    }
    else
    {
        Target = LxProcessLookupNr(Call->Process, Tgid);
        if ((Target == NULL) && (LxThreadLookupNr(Call->Process, Tgid, &Target) == NULL))
            return -LX_ESRCH;
        ASSERT(Target != NULL);
    }

    Result = LxSignalPermitted(Call->Process, Target, Signal);
    if ((Result == 0) && (Signal != 0))
        LxSignalSend(Target, Thread, Signal, &Info);
    LxProcessDereference(Target);
    return Result;
}

LX_SYSCALL(LxSysRtSigqueueinfo)
{
    return LxSignalQueue(Call, (ULONG)Call->Args[0], 0, (LONG)Call->Args[1], Call->Args[2]);
}

LX_SYSCALL(LxSysRtTgsigqueueinfo)
{
    return LxSignalQueue(Call, (ULONG)Call->Args[0], (ULONG)Call->Args[1], (LONG)Call->Args[2], Call->Args[3]);
}

LX_SYSCALL(LxSysSigaltstack)
{
    PLX_THREAD Thread = Call->Thread;
    LX_STACK New, Old = Thread->AltStack;
    ULONG64 Sp = Call->TrapFrame ? LxArchGetUserSp(Call->TrapFrame) : 0;
    BOOLEAN OnStack = (Old.Size != 0) && (Sp > Old.Sp) && (Sp <= Old.Sp + Old.Size);
    LONG64 Result;

    if (Call->Args[0] != 0)
    {
        Result = LxCopyFromUser(&New, Call->Args[0], sizeof(New));
        if (Result != 0)
            return Result;
        if (OnStack)
            return -LX_EPERM;
        /* TODO: SS_AUTODISARM is rejected as on WSL1; Linux 4.7+ accepts it and disarms the stack on delivery. */
        if ((New.Flags != 0) && (New.Flags != LX_SS_ONSTACK) && (New.Flags != LX_SS_DISABLE))
            return -LX_EINVAL;
        if (New.Flags == LX_SS_DISABLE)
        {
            RtlZeroMemory(&Thread->AltStack, sizeof(Thread->AltStack));
            Thread->AltStack.Flags = LX_SS_DISABLE;
        }
        else
        {
            if (New.Size < LX_MINSIGSTKSZ)
                return -LX_ENOMEM;
            Thread->AltStack.Sp = New.Sp;
            Thread->AltStack.Size = New.Size;
            Thread->AltStack.Flags = 0;
        }
    }

    if (Call->Args[1] != 0)
    {
        if (Old.Size == 0)
            Old.Flags = LX_SS_DISABLE;
        if (OnStack)
            Old.Flags |= LX_SS_ONSTACK;
        Old.Pad = 0;
        return LxCopyToUser(Call->Args[1], &Old, sizeof(Old));
    }
    return 0;
}

ULONG
LxSignalGroup(
    _In_ PLX_INSTANCE Instance,
    _In_opt_ PLX_NS PidNs,
    _In_ ULONG Pgid,
    _In_ LONG Signal)
{
    return LxSignalGroupInfo(Instance, PidNs, Pgid, Signal, NULL, NULL, NULL);
}

LONG
LxSignalPermitted(
    _In_ PLX_PROCESS Sender,
    _In_ PLX_PROCESS Target,
    _In_ LONG Signal)
{
    PLX_CRED Cred;

    if (Sender == Target)
        return 0;
    Cred = LxProcessCred(Sender);
    if ((Cred->Uid[1] == Target->Cred.Uid[2]) || (Cred->Uid[1] == Target->Cred.Uid[0]) ||
        (Cred->Uid[0] == Target->Cred.Uid[2]) || (Cred->Uid[0] == Target->Cred.Uid[0]))
    {
        return 0;
    }
    {
        PLX_NS TargetNs = LxUserNsOf(Target);
        BOOLEAN Capable = LxNsCapable(Sender, TargetNs, LX_CAP_KILL);

        LxNsDereference(TargetNs);
        if (Capable)
            return 0;
    }
    if ((Signal == LX_SIGCONT) && (Sender->Sid == Target->Sid))
        return 0;
    return -LX_EPERM;
}

ULONG
LxSignalGroupInfo(
    _In_ PLX_INSTANCE Instance,
    _In_opt_ PLX_NS PidNs,
    _In_ ULONG Pgid,
    _In_ LONG Signal,
    _In_opt_ PLX_SIGINFO SignalInfo,
    _In_opt_ PLX_PROCESS Sender,
    _Out_opt_ PULONG Denied)
{
    PLX_PROCESS Targets[128];
    PLX_PROCESS Self = NULL;
    PLX_THREAD Current = LxCurrentThread();
    PLIST_ENTRY Entry;
    LX_SIGINFO Info;
    ULONG Count = 0, Index, Total;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Count < RTL_NUMBER_OF(Targets)); Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Instance != Instance) || Process->Exited)
            continue;
        if ((PidNs != NULL) && ((Process->PidNs == NULL) || !LxPidNsContains(PidNs, Process->PidNs)))
            continue;
        if (Pgid == 0)
        {
            if ((Process->Pid == 1) || ((Current != NULL) && (Process == Current->Process)))
                continue;
            if ((PidNs != NULL) && (PidNs->Level > 0) && (Process->Pid == PidNs->Pid.ReaperPid))
                continue;
        }
        else if (Process->Pgid != Pgid)
        {
            continue;
        }

        LxProcessReference(Process);
        if ((Current != NULL) && (Process == Current->Process))
            Self = Process;
        else
            Targets[Count++] = Process;
    }
    ExReleaseFastMutex(&LxProcessListLock);

    if (SignalInfo != NULL)
    {
        Info = *SignalInfo;
    }
    else
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_USER;
        if (Current != NULL)
            Info.Child.Pid = (LONG)Current->Process->Pid;
    }

    Total = 0;
    if (Denied != NULL)
        *Denied = 0;
    for (Index = 0; Index < Count; Index++)
    {
        if ((Sender != NULL) && (LxSignalPermitted(Sender, Targets[Index], Signal) != 0))
        {
            if (Denied != NULL)
                (*Denied)++;
        }
        else
        {
            if (Signal != 0)
                LxSignalSend(Targets[Index], NULL, Signal, &Info);
            Total++;
        }
        LxProcessDereference(Targets[Index]);
    }
    if (Self != NULL)
    {
        if (Signal != 0)
            LxSignalSend(Self, NULL, Signal, &Info);
        LxProcessDereference(Self);
        Total++;
    }
    return Total;
}
