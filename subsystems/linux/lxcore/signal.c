/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux signal queuing, delivery and return
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_MINSIGSTKSZ      6144
#define LX_UNBLOCKABLE      ((1ULL << (LX_SIGKILL - 1)) | (1ULL << (LX_SIGSTOP - 1)))
#define LX_SIGNAL_BIT(Sig)  (1ULL << ((Sig) - 1))
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
        case LX_SIGSTOP:
        case LX_SIGTSTP:
        case LX_SIGTTIN:
        case LX_SIGTTOU:
            return TRUE;
        default:
            return FALSE;
    }
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
    ExReleaseFastMutex(&Process->Lock);
}

VOID
LxThreadDetach(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;

    if (Thread->EThread == NULL)
        return;

    ExAcquireFastMutex(&Process->Lock);
    RemoveEntryList(&Thread->Link);
    InitializeListHead(&Thread->Link);
    ExReleaseFastMutex(&Process->Lock);

    ObDereferenceObject(Thread->EThread);
    Thread->EThread = NULL;
}

static
VOID
LxSignalWake(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Tid)
{
    PETHREAD Threads[LX_MAX_WAKE];
    PETHREAD Current = PsGetCurrentThread();
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index;

    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; (Entry != &Process->Threads) && (Count < LX_MAX_WAKE); Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if ((Thread->EThread == Current) || ((Tid != 0) && (Thread->Tid != Tid)))
            continue;
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
        ObDereferenceObject(Threads[Index]);
    }
}

static
VOID
LxSignalTerminate(
    _In_ PLX_PROCESS Target,
    _In_ LONG Signal)
{
    PEPROCESS EProcess;
    HANDLE Handle;

    if (Target->Instance->Trace || (Target->Pid == 1))
        DPRINT1("Linux process %lu (%s) is killed by signal %ld\n", Target->Pid, Target->Comm, Signal);
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

VOID
LxSignalNotifyFds(
    _In_ PLX_PROCESS Process)
{
    LxPollWake(&Process->SignalPoll, LX_POLLIN | LX_POLLRDNORM);
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
    BOOLEAN Blocked;

    if ((Signal <= 0) || (Signal > LX_NSIG))
        return;

    if (Target->Exited)
        return;

    ExAcquireFastMutex(&Target->Lock);
    Handler = Target->SigAction[Signal - 1].Handler;
    ExReleaseFastMutex(&Target->Lock);

    Blocked = (Signal != LX_SIGKILL) && (Signal != LX_SIGSTOP) && LxSignalBlocked(Target, TargetThread, Bit);

    if ((Target->Pid == 1) && (Sender != NULL) && (Sender->Process->Instance == Target->Instance) &&
        (Handler == LX_SIG_DFL) && !Blocked)
    {
        return;
    }

    if (Signal == LX_SIGCONT)
        LxSignalWake(Target, 0);

    if (!Blocked && (Signal != LX_SIGKILL))
    {
        if ((Handler == LX_SIG_IGN) || ((Handler == LX_SIG_DFL) && LxSignalIgnoredByDefault(Signal)))
            return;
    }
    if (!Blocked && ((Signal == LX_SIGKILL) || (Handler == LX_SIG_DFL)))
    {
        LxSignalTerminate(Target, Signal);
        return;
    }

    ExAcquireFastMutex(&Target->Lock);
    if (Info != NULL)
        Target->PendingInfo[Signal - 1] = *Info;
    else
        RtlZeroMemory(&Target->PendingInfo[Signal - 1], sizeof(LX_SIGINFO));
    Target->PendingInfo[Signal - 1].Signo = Signal;
    if (TargetThread != NULL)
        InterlockedOr64(&TargetThread->Pending, (LONG64)Bit);
    else
        InterlockedOr64(&Target->Pending, (LONG64)Bit);
    ExReleaseFastMutex(&Target->Lock);

    LxSignalNotifyFds(Target);
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
        RtlZeroMemory(&Process->SigAction[Signal - 1], sizeof(LX_SIGACTION));
    ExReleaseFastMutex(&Process->Lock);
}

VOID
LxSignalCheckPending(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PLX_PROCESS Process = Thread->Process;

    for (;;)
    {
        ULONG64 Deliverable = ((ULONG64)Thread->Pending | (ULONG64)Process->Pending) & ~Thread->SigMask;
        LX_SIGACTION Action;
        LX_SIGINFO Info;
        ULONG64 Bit;
        LONG Signal = 1;

        if (Deliverable == 0)
            break;

        while (!(Deliverable & LX_SIGNAL_BIT(Signal)))
            Signal++;
        Bit = LX_SIGNAL_BIT(Signal);

        if ((ULONG64)Thread->Pending & Bit)
            InterlockedAnd64(&Thread->Pending, ~(LONG64)Bit);
        else if (!((ULONG64)InterlockedAnd64(&Process->Pending, ~(LONG64)Bit) & Bit))
            continue;

        LxSignalTakeAction(Process, Signal, &Action, &Info);
        if ((Action.Handler == LX_SIG_IGN) && (Signal != LX_SIGKILL))
            continue;
        if (Action.Handler == LX_SIG_DFL)
        {
            if (LxSignalIgnoredByDefault(Signal) || (Process->Pid == 1))
                continue;
            LxProcessExitGroup(Process, Signal);
        }

        if (!LxSignalDeliver(Thread, TrapFrame, ExceptionFrame, Signal, &Info, &Action))
            LxProcessExitGroup(Process, LX_SIGSEGV);
        return;
    }

    if (Thread->Suspended)
    {
        Thread->SigMask = Thread->SuspendMask;
        Thread->Suspended = FALSE;
    }
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
    return LX_SYSCALL_NO_RESULT;
}

LX_SYSCALL(LxSysRtSigsuspend)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_PROCESS Process = Call->Process;
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

    Interval.QuadPart = -10000000LL;
    while (!(((ULONG64)Thread->Pending | (ULONG64)Process->Pending) & ~Thread->SigMask))
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

    return (((ULONG64)Wait->Thread->Pending | (ULONG64)Wait->Thread->Process->Pending) &
            (Wait->Mask | ~Wait->Thread->SigMask)) != 0;
}

LX_SYSCALL(LxSysRtSigtimedwait)
{
    LX_SIGWAIT_CONTEXT Wait;
    LX_TIMESPEC Timeout;
    LARGE_INTEGER Deadline, Interval;
    LX_SIGINFO Info;
    ULONG64 Signal;
    LONG64 Result;

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
        Deadline.QuadPart = (LONG64)KeQueryInterruptTime() + Timeout.Seconds * 10000000LL + Timeout.Nanoseconds / 100;
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

        if (LxSignalPending(Call->Thread))
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
    if (((Info.Code >= 0) || (Info.Code == LX_SI_TKILL)) && (Tgid != Call->Process->Pid))
        return -LX_EPERM;
    Info.Signo = Signal;

    if (Tid != 0)
    {
        Thread = LxThreadLookupReferenced(Call->Process->Instance, Tid, &Target);
        if ((Thread == NULL) || (Target->Pid != Tgid))
        {
            if (Target != NULL)
                LxProcessDereference(Target);
            return -LX_ESRCH;
        }
    }
    else
    {
        Target = LxProcessLookup(Call->Process->Instance, Tgid);
        if (Target == NULL)
            return -LX_ESRCH;
    }

    if (Signal != 0)
        LxSignalSend(Target, Thread, Signal, &Info);
    LxProcessDereference(Target);
    return 0;
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
        if (New.Flags & LX_SS_DISABLE)
        {
            RtlZeroMemory(&Thread->AltStack, sizeof(Thread->AltStack));
            Thread->AltStack.Flags = LX_SS_DISABLE;
        }
        else
        {
            if (New.Flags & ~LX_SS_ONSTACK)
                return -LX_EINVAL;
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
    _In_ ULONG Pgid,
    _In_ LONG Signal)
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
        if (Pgid == 0)
        {
            if ((Process->Pid == 1) || ((Current != NULL) && (Process == Current->Process)))
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

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Code = LX_SI_USER;
    if (Current != NULL)
        Info.Child.Pid = (LONG)Current->Process->Pid;

    Total = Count;
    for (Index = 0; Index < Count; Index++)
    {
        if (Signal != 0)
            LxSignalSend(Targets[Index], NULL, Signal, &Info);
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
