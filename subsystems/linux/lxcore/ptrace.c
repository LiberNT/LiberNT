/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process tracing: ptrace(2) requests, tracee stops, tracer wait reports and Yama
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define LX_CLONE_UNTRACED       0x00800000
#define LX_PTRACE_EVENTMSG_SYSCALL_ENTRY 1
#define LX_PTRACE_EVENTMSG_SYSCALL_EXIT  2
#define LX_PTW_DEAD             0x40000000
#define LX_SIGNAL_MASK_BIT(Sig) (1ULL << ((Sig) - 1))

static FAST_MUTEX LxPtraceLock;
static LONG LxYamaScope = 1;

VOID
LxPtraceInitialize(VOID)
{
    ExInitializeFastMutex(&LxPtraceLock);
}

static
VOID
LxPtraceReference(
    _In_ PLX_PTRACE Ptrace)
{
    InterlockedIncrement(&Ptrace->RefCount);
}

static
VOID
LxPtraceDereference(
    _In_ PLX_PTRACE Ptrace)
{
    if (InterlockedDecrement(&Ptrace->RefCount) != 0)
        return;
    LxFree(Ptrace->Context);
    LxProcessDereference(Ptrace->Tracer);
    LxProcessDereference(Ptrace->Process);
    LxFree(Ptrace);
}

static
BOOLEAN
LxPtraceFatalPending(
    _In_ PLX_THREAD Thread)
{
    return ((((ULONG64)Thread->Pending | (ULONG64)Thread->Process->Pending) & LX_SIGNAL_MASK_BIT(LX_SIGKILL)) != 0) ||
           PsIsThreadTerminating(PsGetCurrentThread());
}

static
PLX_PTRACE
LxPtraceLinkLocked(
    _In_ PLX_PROCESS Tracer,
    _In_ PLX_THREAD Thread,
    _In_ BOOLEAN Seized,
    _In_ ULONG Options)
{
    PLX_PTRACE Ptrace = LxAlloc(sizeof(*Ptrace));

    if (Ptrace == NULL)
        return NULL;
    Ptrace->RefCount = 2;
    LxProcessReference(Tracer);
    Ptrace->Tracer = Tracer;
    LxProcessReference(Thread->Process);
    Ptrace->Process = Thread->Process;
    Ptrace->Thread = Thread;
    Ptrace->Tid = Thread->Tid;
    Ptrace->Seized = Seized;
    Ptrace->Options = Options;
    KeInitializeEvent(&Ptrace->Resume, NotificationEvent, FALSE);
    InsertTailList(&Tracer->Tracees, &Ptrace->Link);
    Thread->Ptrace = Ptrace;
    InterlockedIncrement(&Thread->Process->Traced);
    return Ptrace;
}

static
VOID
LxPtraceReleaseThreadLocked(
    _In_ PLX_PTRACE Ptrace)
{
    PLX_THREAD Thread = Ptrace->Thread;

    if (Thread == NULL)
        return;
    Thread->Ptrace = NULL;
    InterlockedExchange(&Thread->PtraceWork, 0);
    InterlockedDecrement(&Ptrace->Process->Traced);
    Ptrace->Thread = NULL;
}

static
VOID
LxPtraceWakeLocked(
    _In_ PLX_PTRACE Ptrace)
{
    InterlockedIncrement(&Ptrace->Generation);
    KeSetEvent(&Ptrace->Resume, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxPtraceNotifyTracer(
    _In_ PLX_PROCESS Tracer,
    _In_ PLX_PROCESS Process,
    _In_ ULONG Tid,
    _In_ LONG Code,
    _In_ LONG Status)
{
    ULONG64 Handler, Flags;
    LX_SIGINFO Info;

    if (Tracer->Exited)
        return;
    ExAcquireFastMutex(&Tracer->Lock);
    Handler = Tracer->SigAction[LX_SIGCHLD - 1].Handler;
    Flags = Tracer->SigAction[LX_SIGCHLD - 1].Flags;
    ExReleaseFastMutex(&Tracer->Lock);
    if ((Handler != LX_SIG_IGN) &&
        (((Code != LX_CLD_TRAPPED) && (Code != LX_CLD_STOPPED)) || !(Flags & LX_SA_NOCLDSTOP)))
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = Code;
        Info.Child.Pid = (LONG)LxGlobalToNr(Tracer, Tid);
        Info.Child.Uid = LxProcessCred(Process)->Uid[0];
        Info.Child.Status = Status;
        LxSignalSend(Tracer, NULL, LX_SIGCHLD, &Info);
    }
    LxPollWake(&Tracer->ChildPoll, LX_POLLIN);
}

static
LONG
LxPtraceStopCommon(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Report,
    _In_ LONG Code,
    _Inout_opt_ PLX_SIGINFO Info,
    _In_ BOOLEAN Synthesize,
    _Out_opt_ PLONG SyscallNr)
{
    PCONTEXT Context = LxAlloc(sizeof(CONTEXT));
    PLX_PTRACE Ptrace;
    PLX_PROCESS Tracer;
    BOOLEAN Dirty, TlsDirty;
    ULONG64 Tls;
    LONG Generation, Signal;

    if (Context == NULL)
        return -1;
    LxArchCaptureContext(TrapFrame, ExceptionFrame, Context);
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if ((Ptrace == NULL) || LxPtraceFatalPending(Thread))
    {
        ExReleaseFastMutex(&LxPtraceLock);
        LxFree(Context);
        return -1;
    }
    LxPtraceReference(Ptrace);
    LxFree(Ptrace->Context);
    Ptrace->Context = Context;
    Ptrace->ContextDirty = FALSE;
    Ptrace->Tls = LxArchGetThreadPointer(TrapFrame);
    Ptrace->Report = Report;
    Ptrace->ResumeSignal = 0;
    Ptrace->HasSiginfo = (Info != NULL) || Synthesize;
    if (Info != NULL)
    {
        Ptrace->Siginfo = *Info;
    }
    else if (Synthesize)
    {
        RtlZeroMemory(&Ptrace->Siginfo, sizeof(Ptrace->Siginfo));
        Ptrace->Siginfo.Signo = Report & 0x7F;
        Ptrace->Siginfo.Code = Report;
        Ptrace->Siginfo.Child.Pid = (LONG)LxTidNr(Thread->Process, Thread);
        Ptrace->Siginfo.Child.Uid = LxProcessCred(Thread->Process)->Uid[0];
    }
    KeClearEvent(&Ptrace->Resume);
    Ptrace->Stopped = TRUE;
    Generation = Ptrace->Generation;
    Tracer = Ptrace->Tracer;
    LxProcessReference(Tracer);
    ExReleaseFastMutex(&LxPtraceLock);

    LxPtraceNotifyTracer(Tracer, Thread->Process, Thread->Tid, Code, Report & 0x7F);
    LxProcessDereference(Tracer);
    while ((ReadNoFence(&Ptrace->Generation) == Generation) && !LxPtraceFatalPending(Thread))
    {
        if (KeWaitForSingleObject(&Ptrace->Resume, UserRequest, UserMode, TRUE, NULL) == STATUS_USER_APC)
            break;
    }

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace->Stopped = FALSE;
    Ptrace->Report = 0;
    Context = Ptrace->Context;
    Ptrace->Context = NULL;
    Dirty = Ptrace->ContextDirty;
    TlsDirty = (Ptrace->Tls != LxArchGetThreadPointer(TrapFrame));
    Tls = Ptrace->Tls;
    Signal = Ptrace->ResumeSignal;
    if ((Info != NULL) && Ptrace->HasSiginfo)
        *Info = Ptrace->Siginfo;
    Ptrace->HasSiginfo = FALSE;
    if (SyscallNr != NULL)
        *SyscallNr = Ptrace->SyscallNr;
    ExReleaseFastMutex(&LxPtraceLock);

    if (Dirty)
        LxArchRestoreContext(TrapFrame, ExceptionFrame, Context);
    if (TlsDirty)
        LxArchSetThreadPointer(TrapFrame, Tls);
    LxFree(Context);
    LxPtraceDereference(Ptrace);
    return Signal;
}

static
BOOLEAN
LxPtraceTracedOptions(
    _In_ PLX_THREAD Thread,
    _Out_ PULONG Options,
    _Out_ PBOOLEAN Seized)
{
    PLX_PTRACE Ptrace;

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        *Options = Ptrace->Options;
        *Seized = Ptrace->Seized;
    }
    ExReleaseFastMutex(&LxPtraceLock);
    return Ptrace != NULL;
}

BOOLEAN
LxPtraceEventEnabled(
    _In_ PLX_THREAD Thread,
    _In_ LONG Event)
{
    ULONG Options = 0;
    BOOLEAN Seized;

    if (Thread->Ptrace == NULL)
        return FALSE;
    return LxPtraceTracedOptions(Thread, &Options, &Seized) && ((Options & (1UL << Event)) != 0);
}

static
VOID
LxPtraceSetMessage(
    _In_ PLX_THREAD Thread,
    _In_ ULONG64 Message,
    _In_ BOOLEAN MessageIsTid)
{
    PLX_PTRACE Ptrace;

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
        Ptrace->Message = MessageIsTid ? LxGlobalToNr(Ptrace->Tracer, (ULONG)Message) : Message;
    ExReleaseFastMutex(&LxPtraceLock);
}

BOOLEAN
LxPtraceEvent(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Event,
    _In_ ULONG64 Message,
    _In_ BOOLEAN MessageIsTid)
{
    if (!LxPtraceEventEnabled(Thread, Event))
        return FALSE;
    LxPtraceSetMessage(Thread, Message, MessageIsTid);
    LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, LX_SIGTRAP | (Event << 8), LX_CLD_TRAPPED, NULL, TRUE,
                       NULL);
    return TRUE;
}

BOOLEAN
LxPtraceSyscallEntry(
    _Inout_ PLX_CALL Call,
    _Out_ PLONG64 Result)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_PTRACE Ptrace;
    LONG Report = LX_SIGTRAP, Number = (LONG)Call->Number;
    ULONG Ignored;

    *Result = LX_SYSCALL_NO_RESULT;
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        if (Ptrace->Options & LX_PTRACE_O_TRACESYSGOOD)
            Report |= 0x80;
        Ptrace->SyscallOp = LX_PTRACE_SYSCALL_INFO_ENTRY;
        Ptrace->Message = LX_PTRACE_EVENTMSG_SYSCALL_ENTRY;
        Ptrace->SyscallNr = (LONG)Call->Number;
        RtlCopyMemory(Ptrace->SyscallArgs, Call->Args, sizeof(Ptrace->SyscallArgs));
    }
    ExReleaseFastMutex(&LxPtraceLock);
    if (Ptrace == NULL)
        return TRUE;

    if (LxPtraceStopCommon(Thread, Call->TrapFrame, Call->ExceptionFrame, Report, LX_CLD_TRAPPED, NULL, TRUE,
                           &Number) < 0)
    {
        return !LxPtraceFatalPending(Thread);
    }
    if (LxPtraceFatalPending(Thread))
        return FALSE;
    Number = LxArchPtraceSyscallNumber(Call->TrapFrame, Number);
    if (Number == -1)
        return FALSE;
    LxArchGetSyscall(Call->TrapFrame, Call->ExceptionFrame, &Ignored, Call->Args);
    Call->Number = (ULONG)Number;
    return TRUE;
}

VOID
LxPtraceSyscallExit(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG64 Result)
{
    PLX_PTRACE Ptrace;
    LONG Report = LX_SIGTRAP;

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        if (Ptrace->Options & LX_PTRACE_O_TRACESYSGOOD)
            Report |= 0x80;
        Ptrace->SyscallOp = LX_PTRACE_SYSCALL_INFO_EXIT;
        Ptrace->Message = LX_PTRACE_EVENTMSG_SYSCALL_EXIT;
        Ptrace->SyscallRet = Result;
    }
    ExReleaseFastMutex(&LxPtraceLock);
    if (Ptrace != NULL)
        LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Report, LX_CLD_TRAPPED, NULL, TRUE, NULL);
}

BOOLEAN
LxPtraceSeccomp(
    _Inout_ PLX_CALL Call,
    _In_ ULONG Data)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_PTRACE Ptrace;
    LONG Number = (LONG)Call->Number;
    ULONG Ignored;

    if (!LxPtraceEventEnabled(Thread, LX_PTRACE_EVENT_SECCOMP))
        return FALSE;
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        Ptrace->Message = Data;
        Ptrace->SeccompData = Data;
        Ptrace->SyscallOp = LX_PTRACE_SYSCALL_INFO_SECCOMP;
        Ptrace->SyscallNr = (LONG)Call->Number;
        RtlCopyMemory(Ptrace->SyscallArgs, Call->Args, sizeof(Ptrace->SyscallArgs));
    }
    ExReleaseFastMutex(&LxPtraceLock);
    LxPtraceStopCommon(Thread, Call->TrapFrame, Call->ExceptionFrame, LX_SIGTRAP | (LX_PTRACE_EVENT_SECCOMP << 8),
                       LX_CLD_TRAPPED, NULL, TRUE, &Number);
    Number = LxArchPtraceSyscallNumber(Call->TrapFrame, Number);
    LxArchGetSyscall(Call->TrapFrame, Call->ExceptionFrame, &Ignored, Call->Args);
    Call->Number = (ULONG)Number;
    return TRUE;
}

LONG
LxPtraceSignal(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Signal,
    _Inout_ PLX_SIGINFO Info)
{
    PLX_PROCESS Tracer = NULL;
    PLX_PTRACE Ptrace;
    LONG Result;

    Result = LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Signal, LX_CLD_TRAPPED, Info, FALSE, NULL);
    if (Result < 0)
        return Signal;
    if ((Result == 0) || (Result > LX_NSIG))
        return 0;
    if (Result != Info->Signo)
    {
        ExAcquireFastMutex(&LxPtraceLock);
        Ptrace = Thread->Ptrace;
        if (Ptrace != NULL)
        {
            Tracer = Ptrace->Tracer;
            LxProcessReference(Tracer);
        }
        ExReleaseFastMutex(&LxPtraceLock);
        RtlZeroMemory(Info, sizeof(*Info));
        Info->Signo = Result;
        Info->Code = LX_SI_USER;
        if (Tracer != NULL)
        {
            Info->Child.Pid = (LONG)LxPidNr(Thread->Process, Tracer);
            Info->Child.Uid = LxProcessCred(Tracer)->Uid[0];
            LxProcessDereference(Tracer);
        }
    }
    if ((Thread->SigMask & LX_SIGNAL_MASK_BIT(Result)) || LxPtraceFatalPending(Thread))
    {
        LxSignalSend(Thread->Process, Thread, Result, Info);
        return 0;
    }
    return Result;
}

static
VOID
LxPtraceListen(
    _In_ PLX_THREAD Thread,
    _In_ LONG Generation)
{
    PLX_PROCESS Process = Thread->Process;
    PLX_PTRACE Ptrace;
    BOOLEAN Listening;

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    Listening = (Ptrace != NULL) && Ptrace->Listening;
    ExReleaseFastMutex(&LxPtraceLock);
    if (!Listening)
        return;
    while ((ReadNoFence(&Process->ContinueGeneration) == Generation) && !LxPtraceFatalPending(Thread) &&
           !(Thread->PtraceWork & LX_PTW_TRAP))
    {
        if (KeWaitForSingleObject(&Process->ContinueEvent, UserRequest, UserMode, TRUE, NULL) == STATUS_USER_APC)
            break;
    }
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        Ptrace->Listening = FALSE;
        InterlockedOr(&Thread->PtraceWork, LX_PTW_TRAP);
    }
    ExReleaseFastMutex(&LxPtraceLock);
}

BOOLEAN
LxPtraceGroupStop(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ LONG Signal,
    _In_ LONG Generation)
{
    ULONG Options;
    BOOLEAN Seized;

    if ((Thread->Ptrace == NULL) || !LxPtraceTracedOptions(Thread, &Options, &Seized))
        return FALSE;
    InterlockedAnd(&Thread->PtraceWork, ~LX_PTW_TRAP);
    if (!Seized)
    {
        LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Signal, LX_CLD_STOPPED, NULL, FALSE, NULL);
        return TRUE;
    }
    LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Signal | (LX_PTRACE_EVENT_STOP << 8), LX_CLD_STOPPED, NULL,
                       TRUE, NULL);
    LxPtraceListen(Thread, Generation);
    return TRUE;
}

VOID
LxPtraceTrap(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PLX_PROCESS Process = Thread->Process;
    ULONG Options;
    BOOLEAN Seized, Group = FALSE;
    LONG Signal = LX_SIGTRAP, Generation;

    InterlockedAnd(&Thread->PtraceWork, ~LX_PTW_TRAP);
    if (!LxPtraceTracedOptions(Thread, &Options, &Seized))
        return;
    ExAcquireFastMutex(&Process->Lock);
    if ((Process->GroupStopCount != 0) || (Process->JobctlFlags & LX_JOBCTL_STOPPED))
    {
        Signal = Process->GroupExitCode ? Process->GroupExitCode : LX_SIGSTOP;
        Group = TRUE;
    }
    Generation = Process->ContinueGeneration;
    ExReleaseFastMutex(&Process->Lock);
    if (Seized)
    {
        LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Signal | (LX_PTRACE_EVENT_STOP << 8), LX_CLD_STOPPED,
                           NULL, TRUE, NULL);
        if (Group)
            LxPtraceListen(Thread, Generation);
    }
    else
    {
        LxPtraceStopCommon(Thread, TrapFrame, ExceptionFrame, Signal, LX_CLD_STOPPED, NULL, FALSE, NULL);
    }
}

LONG
LxPtraceCloneChild(
    _In_ PLX_THREAD Parent,
    _In_ PLX_THREAD Child,
    _In_ ULONG64 Flags,
    _In_ ULONG ExitSignal)
{
    PLX_PTRACE Ptrace, Link;
    LONG Event;
    BOOLEAN Enabled;

    if ((Parent->Ptrace == NULL) || (Flags & LX_CLONE_UNTRACED))
        return 0;
    if (Flags & LX_CLONE_VFORK)
        Event = LX_PTRACE_EVENT_VFORK;
    else if ((Flags & LX_CLONE_THREAD) || (ExitSignal != LX_SIGCHLD))
        Event = LX_PTRACE_EVENT_CLONE;
    else
        Event = LX_PTRACE_EVENT_FORK;

    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Parent->Ptrace;
    Enabled = (Ptrace != NULL) && ((Ptrace->Options & (1UL << Event)) != 0);
    if ((Ptrace != NULL) && (Enabled || (Flags & LX_CLONE_PTRACE)) && !Ptrace->Tracer->Exited)
    {
        Link = LxPtraceLinkLocked(Ptrace->Tracer, Child, Ptrace->Seized, Ptrace->Options);
        if ((Link != NULL) && Link->Seized)
            InterlockedOr(&Child->PtraceWork, LX_PTW_TRAP);
        else if (Link != NULL)
            InterlockedOr64(&Child->Pending, (LONG64)LX_SIGNAL_MASK_BIT(LX_SIGSTOP));
    }
    ExReleaseFastMutex(&LxPtraceLock);
    return Enabled ? Event : 0;
}

VOID
LxPtraceExec(
    _In_ PLX_THREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame)
{
    ULONG Options;
    BOOLEAN Seized;
    LX_SIGINFO Info;

    if ((Thread->Ptrace == NULL) || !LxPtraceTracedOptions(Thread, &Options, &Seized))
        return;
    if (LxPtraceEvent(Thread, TrapFrame, ExceptionFrame, LX_PTRACE_EVENT_EXEC, Thread->Tid, TRUE))
        return;
    if (!Seized)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_USER;
        Info.Child.Pid = (LONG)LxPidNr(Thread->Process, Thread->Process);
        Info.Child.Uid = LxProcessCred(Thread->Process)->Uid[0];
        LxSignalSend(Thread->Process, Thread, LX_SIGTRAP, &Info);
    }
}

VOID
LxPtraceTransfer(
    _In_ PLX_THREAD From,
    _In_ PLX_THREAD To)
{
    if (From->Ptrace == NULL)
        return;
    ExAcquireFastMutex(&LxPtraceLock);
    if ((From->Ptrace != NULL) && (To->Ptrace == NULL))
    {
        To->Ptrace = From->Ptrace;
        To->Ptrace->Thread = To;
        InterlockedExchange(&To->PtraceWork, From->PtraceWork & ~LX_PTW_DEAD);
        From->Ptrace = NULL;
        InterlockedExchange(&From->PtraceWork, 0);
    }
    ExReleaseFastMutex(&LxPtraceLock);
}

BOOLEAN
LxPtraceStepping(
    _In_ PLX_THREAD Thread)
{
    PLX_PTRACE Ptrace;
    BOOLEAN Stepping = FALSE;

    if (Thread->Ptrace == NULL)
        return FALSE;
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
        Stepping = Ptrace->SingleStep;
    ExReleaseFastMutex(&LxPtraceLock);
    return Stepping;
}

BOOLEAN
LxPtraceSeccompSuspended(
    _In_ PLX_THREAD Thread)
{
    ULONG Options;
    BOOLEAN Seized;

    return (Thread->Ptrace != NULL) && LxPtraceTracedOptions(Thread, &Options, &Seized) &&
           ((Options & LX_PTRACE_O_SUSPEND_SECCOMP) != 0);
}

VOID
LxPtraceRelease(
    _In_ PLX_THREAD Thread)
{
    PLX_PTRACE Ptrace;

    if (Thread->Ptrace == NULL)
        return;
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        RemoveEntryList(&Ptrace->Link);
        InitializeListHead(&Ptrace->Link);
        LxPtraceReleaseThreadLocked(Ptrace);
        LxPtraceWakeLocked(Ptrace);
    }
    ExReleaseFastMutex(&LxPtraceLock);
    if (Ptrace != NULL)
    {
        LxPtraceDereference(Ptrace);
        LxPtraceDereference(Ptrace);
    }
}

VOID
LxPtraceThreadExit(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process, Tracer = NULL;
    PLX_PTRACE Ptrace;
    BOOLEAN Drop = FALSE;
    LONG Status;

    Status = Process->WaitStatusValid ? Process->WaitStatus : Thread->ExitCode;
    ExAcquireFastMutex(&LxPtraceLock);
    InterlockedOr(&Thread->PtraceWork, LX_PTW_DEAD);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
    {
        LxPtraceReleaseThreadLocked(Ptrace);
        LxPtraceWakeLocked(Ptrace);
        if ((Thread->Tid == Process->Pid) && (Ptrace->Tracer->Pid == Process->ParentPid))
        {
            RemoveEntryList(&Ptrace->Link);
            InitializeListHead(&Ptrace->Link);
            Drop = TRUE;
        }
        else
        {
            Ptrace->Exited = TRUE;
            Ptrace->ExitStatus = Status;
            Tracer = Ptrace->Tracer;
            LxProcessReference(Tracer);
        }
    }
    ExReleaseFastMutex(&LxPtraceLock);
    if (Ptrace == NULL)
        return;
    if (Tracer != NULL)
    {
        LxPtraceNotifyTracer(Tracer, Process, Ptrace->Tid,
                             (Status & 0x7F) ? ((Status & 0x80) ? LX_CLD_DUMPED : LX_CLD_KILLED) : LX_CLD_EXITED,
                             (Status & 0x7F) ? (Status & 0x7F) : ((Status >> 8) & 0xFF));
        LxProcessDereference(Tracer);
    }
    if (Drop)
        LxPtraceDereference(Ptrace);
    LxPtraceDereference(Ptrace);
}

VOID
LxPtraceTracerExit(
    _In_ PLX_PROCESS Tracer)
{
    LIST_ENTRY Detached;
    PLIST_ENTRY Entry;

    InitializeListHead(&Detached);
    ExAcquireFastMutex(&LxPtraceLock);
    while (!IsListEmpty(&Tracer->Tracees))
    {
        PLX_PTRACE Ptrace = CONTAINING_RECORD(RemoveHeadList(&Tracer->Tracees), LX_PTRACE, Link);

        InsertTailList(&Detached, &Ptrace->Link);
    }
    ExReleaseFastMutex(&LxPtraceLock);

    while (!IsListEmpty(&Detached))
    {
        PLX_PTRACE Ptrace;
        PLX_PROCESS Process;
        ULONG Tid;
        BOOLEAN Kill, Alive;

        Entry = RemoveHeadList(&Detached);
        Ptrace = CONTAINING_RECORD(Entry, LX_PTRACE, Link);
        InitializeListHead(&Ptrace->Link);
        ExAcquireFastMutex(&LxPtraceLock);
        Alive = (Ptrace->Thread != NULL);
        Kill = Alive && (Ptrace->Options & LX_PTRACE_O_EXITKILL);
        LxPtraceReleaseThreadLocked(Ptrace);
        Ptrace->ResumeSignal = 0;
        LxPtraceWakeLocked(Ptrace);
        Process = Ptrace->Process;
        LxProcessReference(Process);
        Tid = Ptrace->Tid;
        ExReleaseFastMutex(&LxPtraceLock);
        if (Kill)
            LxSignalSend(Process, NULL, LX_SIGKILL, NULL);
        else if (Alive)
            LxSignalWakeThread(Process, Tid);
        LxProcessDereference(Process);
        if (Alive)
            LxPtraceDereference(Ptrace);
        LxPtraceDereference(Ptrace);
    }
}

BOOLEAN
LxPtraceTracedBy(
    _In_ PLX_PROCESS Process,
    _In_ PLX_PROCESS Tracer)
{
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    if (ReadNoFence(&Process->Traced) == 0)
        return FALSE;
    ExAcquireFastMutex(&LxPtraceLock);
    for (Entry = Tracer->Tracees.Flink; Entry != &Tracer->Tracees; Entry = Entry->Flink)
    {
        PLX_PTRACE Ptrace = CONTAINING_RECORD(Entry, LX_PTRACE, Link);

        if ((Ptrace->Process == Process) && (Ptrace->Tid == Process->Pid))
        {
            Found = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&LxPtraceLock);
    return Found;
}

BOOLEAN
LxPtraceWaitFind(
    _In_ PLX_PROCESS Tracer,
    _In_ ULONG Type,
    _In_ ULONG Id,
    _In_opt_ PLX_PROCESS PidfdTarget,
    _In_ ULONG Options,
    _In_ BOOLEAN Consume,
    _Out_ PULONG Tid,
    _Out_ PLONG Status,
    _Out_ PULONG Uid,
    _Out_ PBOOLEAN Trapped,
    _Inout_ PBOOLEAN Any)
{
    PLIST_ENTRY Entry;
    PLX_PTRACE Found = NULL;

    *Tid = 0;
    *Status = 0;
    *Uid = 0;
    *Trapped = FALSE;
    if (IsListEmpty(&Tracer->Tracees))
        return FALSE;
    ExAcquireFastMutex(&LxPtraceLock);
    for (Entry = Tracer->Tracees.Flink; Entry != &Tracer->Tracees; Entry = Entry->Flink)
    {
        PLX_PTRACE Ptrace = CONTAINING_RECORD(Entry, LX_PTRACE, Link);
        BOOLEAN Clone = (Ptrace->Tid != Ptrace->Process->Pid);

        if (!(Options & LX_WALL) && (Clone != ((Options & LX_WCLONE) != 0)))
            continue;
        if (((Type == LX_P_PID) && (Ptrace->Tid != Id)) || ((Type == LX_P_PGID) && (Ptrace->Process->Pgid != Id)) ||
            ((Type == LX_P_PIDFD) && ((Ptrace->Process != PidfdTarget) || Clone)))
        {
            continue;
        }
        *Any = TRUE;
        if (Ptrace->Exited && (Options & LX_WEXITED))
        {
            Found = Ptrace;
            *Status = Ptrace->ExitStatus;
            if (Consume && !(Options & LX_WNOWAIT))
            {
                RemoveEntryList(&Ptrace->Link);
                InitializeListHead(&Ptrace->Link);
            }
            break;
        }
        if (!Ptrace->Exited && Ptrace->Stopped && (Ptrace->Report != 0))
        {
            Found = Ptrace;
            *Status = (Ptrace->Report << 8) | 0x7F;
            *Trapped = TRUE;
            if (Consume && !(Options & LX_WNOWAIT))
                Ptrace->Report = 0;
            break;
        }
    }
    if (Found != NULL)
    {
        *Tid = LxGlobalToNr(Tracer, Found->Tid);
        *Uid = LxProcessCred(Found->Process)->Uid[0];
    }
    ExReleaseFastMutex(&LxPtraceLock);
    if ((Found != NULL) && Found->Exited && Consume && !(Options & LX_WNOWAIT))
        LxPtraceDereference(Found);
    return Found != NULL;
}

BOOLEAN
LxPtraceWaitTrapped(
    _In_ PLX_PROCESS Tracer,
    _In_ ULONG Type,
    _In_ ULONG Id,
    _In_opt_ PLX_PROCESS PidfdTarget,
    _In_ ULONG Options)
{
    ULONG Tid, Uid;
    LONG Status;
    BOOLEAN Trapped, Any = FALSE;

    return LxPtraceWaitFind(Tracer, Type, Id, PidfdTarget, Options, FALSE, &Tid, &Status, &Uid, &Trapped, &Any) ||
           Any;
}

ULONG
LxPtraceTracerPid(
    _In_ PLX_PROCESS Viewer,
    _In_ PLX_THREAD Thread)
{
    PLX_PTRACE Ptrace;
    ULONG Pid = 0;

    if (Thread->Ptrace == NULL)
        return 0;
    ExAcquireFastMutex(&LxPtraceLock);
    Ptrace = Thread->Ptrace;
    if (Ptrace != NULL)
        Pid = LxPidNr(Viewer, Ptrace->Tracer);
    ExReleaseFastMutex(&LxPtraceLock);
    return Pid;
}

BOOLEAN
LxPtraceStopped(
    _In_ PLX_THREAD Thread)
{
    PLX_PTRACE Ptrace = Thread->Ptrace;

    return (Ptrace != NULL) && Ptrace->Stopped;
}

static
BOOLEAN
LxPtraceIsDescendant(
    _In_ PLX_PROCESS Ancestor,
    _In_ PLX_PROCESS Process)
{
    ULONG Pid = Process->Pid;
    ULONG Depth;

    for (Depth = 0; (Pid > 1) && (Depth < 1024); Depth++)
    {
        PLX_PROCESS Current;

        if (Pid == Ancestor->Pid)
            return TRUE;
        Current = LxProcessLookup(Process->Instance, Pid);
        if (Current == NULL)
            return FALSE;
        Pid = Current->ParentPid;
        LxProcessDereference(Current);
    }
    return Pid == Ancestor->Pid;
}

static
BOOLEAN
LxPtraceYamaException(
    _In_ PLX_PROCESS Tracer,
    _In_ PLX_PROCESS Target)
{
    PLX_PROCESS Declared;
    LONG64 Ptracer = ReadNoFence64(&Target->Ptracer);
    BOOLEAN Allowed;

    if (Ptracer == -1)
        return TRUE;
    if (Ptracer <= 0)
        return FALSE;
    Declared = LxProcessLookup(Target->Instance, (ULONG)Ptracer);
    if (Declared == NULL)
        return FALSE;
    Allowed = LxPtraceIsDescendant(Declared, Tracer);
    LxProcessDereference(Declared);
    return Allowed;
}

static
LONG
LxPtraceMayAttach(
    _In_ PLX_PROCESS Tracer,
    _In_ PLX_PROCESS Target)
{
    PLX_NS TargetNs;
    BOOLEAN Capable;
    LONG Scope = ReadNoFence(&LxYamaScope);

    if (Target == Tracer)
        return -LX_EPERM;
    if (!LxPtraceMayRead(Tracer, Target))
        return -LX_EPERM;
    TargetNs = LxUserNsOf(Target);
    Capable = LxNsCapable(Tracer, TargetNs, LX_CAP_SYS_PTRACE);
    LxNsDereference(TargetNs);
    switch (Scope)
    {
        case 0:
            return 0;
        case 1:
            return (Capable || LxPtraceIsDescendant(Tracer, Target) || LxPtraceYamaException(Tracer, Target)) ?
                   0 : -LX_EPERM;
        case 2:
            return Capable ? 0 : -LX_EPERM;
        default:
            return -LX_EPERM;
    }
}

LONG
LxPtraceAccessCheck(
    _In_ PLX_PROCESS Caller,
    _In_ PLX_PROCESS Target)
{
    return (Caller->Mm == Target->Mm) ? 0 : LxPtraceMayAttach(Caller, Target);
}

LONG
LxPtraceSetPtracer(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Value)
{
    PLX_PROCESS Tracer;

    if (Value == 0)
    {
        InterlockedExchange64(&Process->Ptracer, 0);
        return 0;
    }
    if (Value == LX_PR_SET_PTRACER_ANY)
    {
        InterlockedExchange64(&Process->Ptracer, -1);
        return 0;
    }
    if ((LONG)Value <= 0)
        return -LX_EINVAL;
    Tracer = LxProcessLookupNr(Process, (ULONG)Value);
    if (Tracer == NULL)
        return -LX_EINVAL;
    InterlockedExchange64(&Process->Ptracer, Tracer->Pid);
    LxProcessDereference(Tracer);
    return 0;
}

LONG
LxYamaScopeValue(VOID)
{
    return ReadNoFence(&LxYamaScope);
}

LONG
LxYamaSetScope(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Value)
{
    if (!LxCapable(Process, LX_CAP_SYS_PTRACE))
        return -LX_EPERM;
    if (Value > 3)
        return -LX_EINVAL;
    if ((ReadNoFence(&LxYamaScope) == 3) && (Value != 3))
        return -LX_EINVAL;
    InterlockedExchange(&LxYamaScope, (LONG)Value);
    return 0;
}

static
BOOLEAN
LxPtraceThreadListed(
    _In_ PLX_PROCESS Process,
    _In_ PLX_THREAD Thread)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_THREAD, Link) == Thread)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxPtraceAttach(
    _Inout_ PLX_CALL Call,
    _In_ PLX_THREAD Thread,
    _In_ PLX_PROCESS Target,
    _In_ BOOLEAN Seize,
    _In_ ULONG64 Address,
    _In_ ULONG64 Flags)
{
    PLX_PTRACE Ptrace = NULL;
    LONG Error = 0;
    BOOLEAN Stopped;
    ULONG Tid;

    if (Seize && ((Address != 0) || (Flags & ~(ULONG64)LX_PTRACE_O_MASK)))
        return -LX_EIO;
    if (Target->WaitStatusValid || Target->Exited)
        return -LX_EPERM;
    Error = LxPtraceMayAttach(Call->Process, Target);
    if (Error != 0)
        return Error;
    ExAcquireFastMutex(&Target->Lock);
    ExAcquireFastMutex(&LxPtraceLock);
    if (!LxPtraceThreadListed(Target, Thread) || (Thread->PtraceWork & LX_PTW_DEAD))
        Error = -LX_ESRCH;
    else if (Thread->Ptrace != NULL)
        Error = -LX_EPERM;
    else
        Ptrace = LxPtraceLinkLocked(Call->Process, Thread, Seize, Seize ? (ULONG)Flags : 0);
    Stopped = ((Target->JobctlFlags & LX_JOBCTL_STOPPED) != 0) || Thread->Stopped;
    if ((Ptrace != NULL) && Stopped)
        InterlockedOr(&Thread->PtraceWork, LX_PTW_TRAP);
    Tid = Thread->Tid;
    ExReleaseFastMutex(&LxPtraceLock);
    ExReleaseFastMutex(&Target->Lock);
    if (Error != 0)
        return Error;
    if (Ptrace == NULL)
        return -LX_ENOMEM;
    if (!Seize)
    {
        LX_SIGINFO Info;

        RtlZeroMemory(&Info, sizeof(Info));
        Info.Signo = LX_SIGSTOP;
        Info.Code = LX_SI_KERNEL;
        LxSignalSend(Target, Thread, LX_SIGSTOP, &Info);
    }
    if (Stopped)
        LxSignalWakeThread(Target, Tid);
    return 0;
}

static
LONG
LxPtraceTraceme(
    _Inout_ PLX_CALL Call)
{
    PLX_PROCESS Parent;
    PLX_PTRACE Ptrace = NULL;
    LONG Error = 0;

    if (ReadNoFence(&LxYamaScope) == 3)
        return -LX_EPERM;
    Parent = LxProcessLookup(Call->Process->Instance, Call->Process->ParentPid);
    if (Parent == NULL)
        return -LX_EPERM;
    if ((ReadNoFence(&LxYamaScope) == 2) && !LxCapable(Parent, LX_CAP_SYS_PTRACE))
    {
        LxProcessDereference(Parent);
        return -LX_EPERM;
    }
    ExAcquireFastMutex(&LxPtraceLock);
    if (Call->Thread->Ptrace != NULL)
        Error = -LX_EPERM;
    else
        Ptrace = LxPtraceLinkLocked(Parent, Call->Thread, FALSE, 0);
    ExReleaseFastMutex(&LxPtraceLock);
    LxProcessDereference(Parent);
    if ((Error == 0) && (Ptrace == NULL))
        Error = -LX_ENOMEM;
    return Error;
}

static
LONG
LxPtraceAccessMemory(
    _In_ PLX_PROCESS Target,
    _In_ ULONG64 Address,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Write)
{
    PEPROCESS EProcess;
    LONG Result;

    if (!NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Target->Mm->NtPid), &EProcess)))
        return -LX_ESRCH;
    Result = LxMmForceAccess(Target, EProcess, Address, Buffer, Length, Write);
    ObDereferenceObject(EProcess);
    return Result;
}

static
LONG
LxPtraceResume(
    _In_ PLX_PTRACE Ptrace,
    _In_ LONG Request,
    _In_ ULONG64 Data)
{
    PLX_THREAD Thread = Ptrace->Thread;

    if (Data > LX_NSIG)
        return -LX_EIO;
    if ((Ptrace->Context == NULL) || !LxArchPtraceSingleStep(Ptrace->Context, Request == LX_PTRACE_SINGLESTEP))
        return -LX_EIO;
    Ptrace->ContextDirty = TRUE;
    Ptrace->SingleStep = (Request == LX_PTRACE_SINGLESTEP);
    if (Request == LX_PTRACE_SYSCALL)
        InterlockedOr(&Thread->PtraceWork, LX_PTW_SYSCALL);
    else
        InterlockedAnd(&Thread->PtraceWork, ~LX_PTW_SYSCALL);
    Ptrace->ResumeSignal = (LONG)Data;
    Ptrace->Listening = FALSE;
    LxPtraceWakeLocked(Ptrace);
    return 0;
}

static
LONG
LxPtraceRegset(
    _In_ PLX_PTRACE Ptrace,
    _In_ BOOLEAN Set,
    _In_ ULONG Type,
    _In_ ULONG64 IovecAddress)
{
    LX_IOVEC Vector;
    PUCHAR Buffer;
    ULONG Size = 0, Unit = 1, Length;
    LONG Result;

    Result = LxCopyFromUser(&Vector, IovecAddress, sizeof(Vector));
    if (Result != 0)
        return Result;
    Buffer = LxAlloc(LX_PTRACE_REGSET_MAX);
    if (Buffer == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&LxPtraceLock);
    Result = (Ptrace->Context == NULL) ? -LX_ESRCH :
             LxArchPtraceGetRegset(Ptrace->Context, Ptrace->Tls, Ptrace->SyscallNr, Type, Buffer, &Size, &Unit);
    ExReleaseFastMutex(&LxPtraceLock);
    if ((Result == 0) && ((Vector.Length % Unit) != 0))
        Result = -LX_EINVAL;
    Length = (ULONG)min(Vector.Length, (ULONG64)Size);
    if ((Result == 0) && !Set)
    {
        Result = LxCopyToUser(Vector.Base, Buffer, Length);
    }
    else if (Result == 0)
    {
        Result = LxCopyFromUser(Buffer, Vector.Base, Length);
        ExAcquireFastMutex(&LxPtraceLock);
        if ((Result == 0) && (Ptrace->Context == NULL))
            Result = -LX_ESRCH;
        if (Result == 0)
            Result = LxArchPtraceSetRegset(Ptrace->Context, &Ptrace->Tls, &Ptrace->SyscallNr, Type, Buffer, Length);
        if (Result == 0)
            Ptrace->ContextDirty = TRUE;
        ExReleaseFastMutex(&LxPtraceLock);
    }
    LxFree(Buffer);
    if (Result == 0)
    {
        Vector.Length = Length;
        Result = LxCopyToUser(IovecAddress + FIELD_OFFSET(LX_IOVEC, Length), &Vector.Length, sizeof(Vector.Length));
    }
    return Result;
}

static
LONG64
LxPtraceSyscallInfo(
    _In_ PLX_PTRACE Ptrace,
    _In_ ULONG64 Size,
    _In_ ULONG64 Address)
{
    LX_PTRACE_SYSCALL_INFO Info;
    ULONG Actual = FIELD_OFFSET(LX_PTRACE_SYSCALL_INFO, Entry);
    LONG64 Result;

    RtlZeroMemory(&Info, sizeof(Info));
    ExAcquireFastMutex(&LxPtraceLock);
    if (Ptrace->Context == NULL)
    {
        ExReleaseFastMutex(&LxPtraceLock);
        return -LX_ESRCH;
    }
    Info.Arch = LxSeccompAuditArch();
    Info.InstructionPointer = LxArchContextPc(Ptrace->Context);
    Info.StackPointer = LxArchContextSp(Ptrace->Context);
    if (Ptrace->HasSiginfo && (Ptrace->Siginfo.Code == (LX_SIGTRAP | 0x80)))
    {
        if (Ptrace->Message == LX_PTRACE_EVENTMSG_SYSCALL_ENTRY)
            Info.Op = LX_PTRACE_SYSCALL_INFO_ENTRY;
        else if (Ptrace->Message == LX_PTRACE_EVENTMSG_SYSCALL_EXIT)
            Info.Op = LX_PTRACE_SYSCALL_INFO_EXIT;
    }
    else if (Ptrace->HasSiginfo && (Ptrace->Siginfo.Code == (LX_SIGTRAP | (LX_PTRACE_EVENT_SECCOMP << 8))))
    {
        Info.Op = LX_PTRACE_SYSCALL_INFO_SECCOMP;
    }
    switch (Info.Op)
    {
        case LX_PTRACE_SYSCALL_INFO_ENTRY:
        case LX_PTRACE_SYSCALL_INFO_SECCOMP:
            Info.Entry.Nr = (ULONG64)(LONG64)Ptrace->SyscallNr;
            RtlCopyMemory(Info.Entry.Args, Ptrace->SyscallArgs, sizeof(Info.Entry.Args));
            if (Info.Op == LX_PTRACE_SYSCALL_INFO_SECCOMP)
            {
                Info.Entry.RetData = Ptrace->SeccompData;
                Actual = FIELD_OFFSET(LX_PTRACE_SYSCALL_INFO, Entry.RetData) + sizeof(ULONG);
            }
            else
            {
                Actual = FIELD_OFFSET(LX_PTRACE_SYSCALL_INFO, Entry.RetData);
            }
            break;
        case LX_PTRACE_SYSCALL_INFO_EXIT:
            Info.Exit.Rval = LxArchPtraceSyscallReturn(Ptrace->Context);
            Info.Exit.IsError = (Info.Exit.Rval < 0) && (Info.Exit.Rval >= -4095);
            Actual = FIELD_OFFSET(LX_PTRACE_SYSCALL_INFO, Exit.IsError) + 1;
            break;
        default:
            break;
    }
    ExReleaseFastMutex(&LxPtraceLock);
    Result = LxCopyToUser(Address, &Info, (SIZE_T)min(Size, (ULONG64)Actual));
    return (Result != 0) ? Result : Actual;
}

static
LONG64
LxPtracePeekSiginfo(
    _In_ PLX_PTRACE Ptrace,
    _In_ PLX_PROCESS Target,
    _In_ ULONG64 ArgsAddress,
    _In_ ULONG64 Buffer)
{
    LX_PTRACE_PEEKSIGINFO_ARGS Args;
    ULONG64 Pending = 0, Index = 0;
    LONG Signal, Copied = 0;
    LONG64 Result;

    Result = LxCopyFromUser(&Args, ArgsAddress, sizeof(Args));
    if (Result != 0)
        return Result;
    if ((Args.Flags & ~LX_PTRACE_PEEKSIGINFO_SHARED) || (Args.Nr < 0))
        return -LX_EINVAL;
    ExAcquireFastMutex(&LxPtraceLock);
    if (Args.Flags & LX_PTRACE_PEEKSIGINFO_SHARED)
        Pending = (ULONG64)Target->Pending;
    else if (Ptrace->Thread != NULL)
        Pending = (ULONG64)Ptrace->Thread->Pending;
    ExReleaseFastMutex(&LxPtraceLock);
    for (Signal = 1; (Signal <= LX_NSIG) && (Copied < Args.Nr); Signal++)
    {
        LX_SIGINFO Info;

        if (!(Pending & LX_SIGNAL_MASK_BIT(Signal)))
            continue;
        if (Index++ < Args.Off)
            continue;
        ExAcquireFastMutex(&Target->Lock);
        Info = Target->PendingInfo[Signal - 1];
        ExReleaseFastMutex(&Target->Lock);
        Info.Signo = Signal;
        Result = LxCopyToUser(Buffer + (ULONG64)Copied * sizeof(Info), &Info, sizeof(Info));
        if (Result != 0)
            return (Copied != 0) ? Copied : Result;
        Copied++;
    }
    return Copied;
}

static
LONG64
LxPtraceRequest(
    _Inout_ PLX_CALL Call,
    _In_ PLX_PTRACE Ptrace,
    _In_ PLX_PROCESS Target,
    _In_ LONG Request,
    _In_ ULONG64 Address,
    _In_ ULONG64 Data)
{
    LX_SIGINFO Info;
    ULONG64 Word;
    LONG64 Result;
    ULONG Tid = Ptrace->Tid;

    switch (Request)
    {
        case LX_PTRACE_PEEKTEXT:
        case LX_PTRACE_PEEKDATA:
            Result = LxPtraceAccessMemory(Target, Address, &Word, sizeof(Word), FALSE);
            if (Result != 0)
                return Result;
            return LxCopyToUser(Data, &Word, sizeof(Word));

        case LX_PTRACE_POKETEXT:
        case LX_PTRACE_POKEDATA:
            Word = Data;
            return LxPtraceAccessMemory(Target, Address, &Word, sizeof(Word), TRUE);

        case LX_PTRACE_GETEVENTMSG:
            ExAcquireFastMutex(&LxPtraceLock);
            Word = Ptrace->Message;
            ExReleaseFastMutex(&LxPtraceLock);
            return LxCopyToUser(Data, &Word, sizeof(Word));

        case LX_PTRACE_SETOPTIONS:
            if (Data & ~(ULONG64)LX_PTRACE_O_MASK)
                return -LX_EINVAL;
            if ((Data & LX_PTRACE_O_SUSPEND_SECCOMP) &&
                (!LxCapable(Call->Process, LX_CAP_SYS_ADMIN) || (Call->Thread->SeccompMode != 0)))
            {
                return -LX_EPERM;
            }
            ExAcquireFastMutex(&LxPtraceLock);
            Ptrace->Options = (ULONG)Data;
            ExReleaseFastMutex(&LxPtraceLock);
            return 0;

        case LX_PTRACE_GETSIGINFO:
            ExAcquireFastMutex(&LxPtraceLock);
            Result = Ptrace->HasSiginfo ? 0 : -LX_EINVAL;
            Info = Ptrace->Siginfo;
            ExReleaseFastMutex(&LxPtraceLock);
            return (Result != 0) ? Result : LxCopyToUser(Data, &Info, sizeof(Info));

        case LX_PTRACE_SETSIGINFO:
            Result = LxCopyFromUser(&Info, Data, sizeof(Info));
            if (Result != 0)
                return Result;
            ExAcquireFastMutex(&LxPtraceLock);
            Result = Ptrace->HasSiginfo ? 0 : -LX_EINVAL;
            if (Result == 0)
                Ptrace->Siginfo = Info;
            ExReleaseFastMutex(&LxPtraceLock);
            return Result;

        case LX_PTRACE_GETREGSET:
        case LX_PTRACE_SETREGSET:
            return LxPtraceRegset(Ptrace, Request == LX_PTRACE_SETREGSET, (ULONG)Address, Data);

        case LX_PTRACE_GETSIGMASK:
            if (Address != sizeof(ULONG64))
                return -LX_EINVAL;
            ExAcquireFastMutex(&LxPtraceLock);
            Result = (Ptrace->Thread != NULL) ? 0 : -LX_ESRCH;
            Word = (Ptrace->Thread != NULL) ? Ptrace->Thread->SigMask : 0;
            ExReleaseFastMutex(&LxPtraceLock);
            return (Result != 0) ? Result : LxCopyToUser(Data, &Word, sizeof(Word));

        case LX_PTRACE_SETSIGMASK:
            if (Address != sizeof(ULONG64))
                return -LX_EINVAL;
            Result = LxCopyFromUser(&Word, Data, sizeof(Word));
            if (Result != 0)
                return Result;
            ExAcquireFastMutex(&LxPtraceLock);
            Result = (Ptrace->Thread != NULL) ? 0 : -LX_ESRCH;
            if (Result == 0)
                Ptrace->Thread->SigMask = Word & ~(LX_SIGNAL_MASK_BIT(LX_SIGKILL) | LX_SIGNAL_MASK_BIT(LX_SIGSTOP));
            ExReleaseFastMutex(&LxPtraceLock);
            return Result;

        case LX_PTRACE_PEEKSIGINFO:
            return LxPtracePeekSiginfo(Ptrace, Target, Address, Data);

        case LX_PTRACE_GET_SYSCALL_INFO:
            return LxPtraceSyscallInfo(Ptrace, Address, Data);

        case LX_PTRACE_GET_RSEQ_CONFIGURATION:
        {
            UCHAR Configuration[24] = { 0 };

            Result = LxCopyToUser(Data, Configuration, (SIZE_T)min(Address, (ULONG64)sizeof(Configuration)));
            return (Result != 0) ? Result : (LONG64)sizeof(Configuration);
        }

        case LX_PTRACE_CONT:
        case LX_PTRACE_SYSCALL:
        case LX_PTRACE_SINGLESTEP:
            ExAcquireFastMutex(&LxPtraceLock);
            Result = (Ptrace->Thread != NULL) ? LxPtraceResume(Ptrace, Request, Data) : -LX_ESRCH;
            ExReleaseFastMutex(&LxPtraceLock);
            return Result;

        case LX_PTRACE_LISTEN:
            ExAcquireFastMutex(&LxPtraceLock);
            Result = (Ptrace->Seized && Ptrace->Stopped && Ptrace->HasSiginfo &&
                      ((Ptrace->Siginfo.Code >> 8) == LX_PTRACE_EVENT_STOP)) ? 0 : -LX_EIO;
            if (Result == 0)
            {
                Ptrace->Listening = TRUE;
                Ptrace->ResumeSignal = 0;
                LxPtraceWakeLocked(Ptrace);
            }
            ExReleaseFastMutex(&LxPtraceLock);
            return Result;

        case LX_PTRACE_DETACH:
            if (Data > LX_NSIG)
                return -LX_EIO;
            ExAcquireFastMutex(&LxPtraceLock);
            Ptrace->ResumeSignal = (LONG)Data;
            RemoveEntryList(&Ptrace->Link);
            InitializeListHead(&Ptrace->Link);
            if ((Ptrace->Thread != NULL) && (Target->JobctlFlags & LX_JOBCTL_STOPPED))
                Ptrace->Thread->JobctlStop = TRUE;
            LxPtraceReleaseThreadLocked(Ptrace);
            LxPtraceWakeLocked(Ptrace);
            ExReleaseFastMutex(&LxPtraceLock);
            LxPtraceDereference(Ptrace);
            LxPtraceDereference(Ptrace);
            return 0;

        case LX_PTRACE_KILL:
            LxSignalSend(Target, NULL, LX_SIGKILL, NULL);
            return 0;

        case LX_PTRACE_INTERRUPT:
            if (!Ptrace->Seized)
                return -LX_EIO;
            ExAcquireFastMutex(&LxPtraceLock);
            if (Ptrace->Thread != NULL)
                InterlockedOr(&Ptrace->Thread->PtraceWork, LX_PTW_TRAP);
            if (Ptrace->Listening)
            {
                Ptrace->Listening = FALSE;
                LxPtraceWakeLocked(Ptrace);
            }
            ExReleaseFastMutex(&LxPtraceLock);
            LxSignalWakeThread(Target, Tid);
            return 0;

        default:
            return -LX_EIO;
    }
}

static
PLX_PTRACE
LxPtraceFindTracee(
    _In_ PLX_PROCESS Tracer,
    _In_ ULONG Tid,
    _Out_ PBOOLEAN Stopped)
{
    PLIST_ENTRY Entry;
    PLX_PTRACE Found = NULL;

    *Stopped = FALSE;
    ExAcquireFastMutex(&LxPtraceLock);
    for (Entry = Tracer->Tracees.Flink; Entry != &Tracer->Tracees; Entry = Entry->Flink)
    {
        PLX_PTRACE Ptrace = CONTAINING_RECORD(Entry, LX_PTRACE, Link);

        if ((Ptrace->Tid == Tid) && !Ptrace->Exited && (Ptrace->Thread != NULL))
        {
            Found = Ptrace;
            LxPtraceReference(Found);
            *Stopped = Ptrace->Stopped;
            break;
        }
    }
    ExReleaseFastMutex(&LxPtraceLock);
    return Found;
}

LX_SYSCALL(LxSysPtrace)
{
    LONG Request = (LONG)Call->Args[0];
    LONG Pid = (LONG)Call->Args[1];
    ULONG64 Address = Call->Args[2], Data = Call->Args[3];
    PLX_PROCESS Target = NULL;
    PLX_THREAD Thread;
    PLX_PTRACE Ptrace;
    BOOLEAN Stopped;
    ULONG Global;
    LONG64 Result;

    if (Request == LX_PTRACE_TRACEME)
        return LxPtraceTraceme(Call);
    if (Pid <= 0)
        return -LX_ESRCH;
    if ((Request == LX_PTRACE_ATTACH) || (Request == LX_PTRACE_SEIZE))
    {
        Thread = LxThreadLookupNr(Call->Process, (ULONG)Pid, &Target);
        if (Thread == NULL)
            return -LX_ESRCH;
        Result = (Target == Call->Process) ? -LX_EPERM :
                 LxPtraceAttach(Call, Thread, Target, Request == LX_PTRACE_SEIZE, Address, Data);
        LxProcessDereference(Target);
        return Result;
    }

    Global = LxNrToGlobal(Call->Process, (ULONG)Pid);
    Ptrace = (Global != 0) ? LxPtraceFindTracee(Call->Process, Global, &Stopped) : NULL;
    if (Ptrace == NULL)
        return -LX_ESRCH;
    if (!Stopped && (Request != LX_PTRACE_KILL) && (Request != LX_PTRACE_INTERRUPT) && (Request != LX_PTRACE_LISTEN))
    {
        LxPtraceDereference(Ptrace);
        return -LX_ESRCH;
    }
    Target = Ptrace->Process;
    LxProcessReference(Target);
    Result = LxPtraceRequest(Call, Ptrace, Target, Request, Address, Data);
    LxProcessDereference(Target);
    LxPtraceDereference(Ptrace);
    return Result;
}
