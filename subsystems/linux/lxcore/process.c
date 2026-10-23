/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux process, thread, descriptor and futex state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

LIST_ENTRY LxProcessList;
FAST_MUTEX LxProcessListLock;

typedef struct _LX_FUTEX_WAITER
{
    LIST_ENTRY Link;
    PEPROCESS Process;
    ULONG64 Address;
    BOOLEAN Woken;
    KEVENT Event;
} LX_FUTEX_WAITER, *PLX_FUTEX_WAITER;

static LIST_ENTRY LxFutexWaiters = { &LxFutexWaiters, &LxFutexWaiters };
static FAST_MUTEX LxFutexLock;

VOID
LxProcessInitialize(VOID)
{
    InitializeListHead(&LxProcessList);
    ExInitializeFastMutex(&LxProcessListLock);
    InitializeListHead(&LxFutexWaiters);
    ExInitializeFastMutex(&LxFutexLock);
}

PVOID
LxAlloc(
    _In_ SIZE_T Size)
{
    return ExAllocatePoolZero(NonPagedPoolNx, Size, LX_TAG);
}

VOID
LxFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, LX_TAG);
}

PSTR
LxStrDup(
    _In_ PCSTR String)
{
    SIZE_T Length = strlen(String) + 1;
    PSTR Copy = LxAlloc(Length);

    if (Copy != NULL)
        RtlCopyMemory(Copy, String, Length);
    return Copy;
}

LONG
LxStatusToErrno(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
            return 0;
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
        case STATUS_NO_SUCH_FILE:
        case STATUS_OBJECT_NAME_INVALID:
        case STATUS_DELETE_PENDING:
            return LX_ENOENT;
        case STATUS_OBJECT_PATH_SYNTAX_BAD:
        case STATUS_NOT_A_DIRECTORY:
            return LX_ENOTDIR;
        case STATUS_FILE_IS_A_DIRECTORY:
            return LX_EISDIR;
        case STATUS_OBJECT_NAME_COLLISION:
            return LX_EEXIST;
        case STATUS_ACCESS_DENIED:
        case STATUS_PRIVILEGE_NOT_HELD:
        case STATUS_CANNOT_DELETE:
            return LX_EACCES;
        case STATUS_SHARING_VIOLATION:
            return LX_EBUSY;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return LX_ENOTEMPTY;
        case STATUS_NO_MEMORY:
        case STATUS_INSUFFICIENT_RESOURCES:
        case STATUS_COMMITMENT_LIMIT:
        case STATUS_CONFLICTING_ADDRESSES:
            return LX_ENOMEM;
        case STATUS_DISK_FULL:
            return LX_ENOSPC;
        case STATUS_MEDIA_WRITE_PROTECTED:
            return LX_EROFS;
        case STATUS_PIPE_BROKEN:
        case STATUS_PIPE_CLOSING:
        case STATUS_PIPE_DISCONNECTED:
            return LX_EPIPE;
        case STATUS_ACCESS_VIOLATION:
        case STATUS_DATATYPE_MISALIGNMENT:
            return LX_EFAULT;
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_PARAMETER_1:
        case STATUS_INVALID_PARAMETER_2:
        case STATUS_INVALID_PARAMETER_3:
        case STATUS_INVALID_PAGE_PROTECTION:
            return LX_EINVAL;
        case STATUS_NOT_SAME_DEVICE:
            return LX_EXDEV;
        case STATUS_INVALID_IMAGE_FORMAT:
        case STATUS_INVALID_IMAGE_NOT_MZ:
            return LX_ENOEXEC;
        case STATUS_NAME_TOO_LONG:
            return LX_ENAMETOOLONG;
        case STATUS_TOO_MANY_OPENED_FILES:
            return LX_EMFILE;
        case STATUS_NOT_IMPLEMENTED:
        case STATUS_NOT_SUPPORTED:
        case STATUS_INVALID_DEVICE_REQUEST:
            return LX_ENOSYS;
        case STATUS_TIMEOUT:
            return LX_ETIMEDOUT;
        case STATUS_ALERTED:
        case STATUS_USER_APC:
            return LX_EINTR;
        default:
            return LX_EIO;
    }
}

LONG64
LxCopyFromUser(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_ ULONG64 Source,
    _In_ SIZE_T Length)
{
    LONG64 Result = 0;

    if (Length == 0)
        return 0;

    for (;;)
    {
        _SEH2_TRY
        {
            ProbeForRead((PVOID)(ULONG_PTR)Source, Length, 1);
            RtlCopyMemory(Destination, (PVOID)(ULONG_PTR)Source, Length);
            Result = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = -LX_EFAULT;
        }
        _SEH2_END;

        if ((Result == 0) || !LxMmFaultIn(Source, Length, FALSE))
            return Result;
    }
}

LONG64
LxCopyToUser(
    _In_ ULONG64 Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    LONG64 Result = 0;

    if (Length == 0)
        return 0;

    for (;;)
    {
        _SEH2_TRY
        {
            ProbeForWrite((PVOID)(ULONG_PTR)Destination, Length, 1);
            RtlCopyMemory((PVOID)(ULONG_PTR)Destination, Source, Length);
            Result = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = -LX_EFAULT;
        }
        _SEH2_END;

        if ((Result == 0) || !LxMmFaultIn(Destination, Length, TRUE))
            return Result;
    }
}

LONG64
LxZeroUser(
    _In_ ULONG64 Destination,
    _In_ SIZE_T Length)
{
    LONG64 Result = 0;

    if (Length == 0)
        return 0;

    for (;;)
    {
        _SEH2_TRY
        {
            ProbeForWrite((PVOID)(ULONG_PTR)Destination, Length, 1);
            RtlZeroMemory((PVOID)(ULONG_PTR)Destination, Length);
            Result = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = -LX_EFAULT;
        }
        _SEH2_END;

        if ((Result == 0) || !LxMmFaultIn(Destination, Length, TRUE))
            return Result;
    }
}

LONG64
LxCopyStringFromUser(
    _In_ ULONG64 Source,
    _Out_ PSTR *String,
    _In_ SIZE_T MaxLength)
{
    PSTR Buffer;
    SIZE_T Index = 0;
    LONG64 Result = -LX_ENAMETOOLONG;

    *String = NULL;
    Buffer = LxAlloc(MaxLength + 1);
    if (Buffer == NULL)
        return -LX_ENOMEM;

    for (;;)
    {
        _SEH2_TRY
        {
            for (; Index < MaxLength; Index++)
            {
                ProbeForRead((PVOID)(ULONG_PTR)(Source + Index), 1, 1);
                Buffer[Index] = *(volatile CHAR *)(ULONG_PTR)(Source + Index);
                if (Buffer[Index] == '\0')
                {
                    Result = 0;
                    break;
                }
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Result = -LX_EFAULT;
        }
        _SEH2_END;

        if ((Result != -LX_EFAULT) || !LxMmFaultIn(Source + Index, 1, FALSE))
            break;
        Result = -LX_ENAMETOOLONG;
    }

    if (Result != 0)
    {
        LxFree(Buffer);
        return Result;
    }

    *String = Buffer;
    return 0;
}

static
VOID
LxFdCloseAll(
    _In_ PLX_PROCESS Process)
{
    LONG Fd;

    for (Fd = 0; Fd < LX_MAX_FDS; Fd++)
    {
        if (Process->Fds[Fd] != NULL)
            LxFdClose(Process, Fd);
    }
}

VOID
LxProcessCloseFiles(
    _In_ PLX_PROCESS Process)
{
    LxFdCloseAll(Process);
}

static
VOID
LxProcessDefaultLimits(
    _Out_writes_(16) PLX_RLIMIT Limits)
{
    ULONG Index;

    for (Index = 0; Index < 16; Index++)
        Limits[Index].Cur = Limits[Index].Max = LX_RLIM_INFINITY;
    Limits[LX_RLIMIT_STACK].Cur = LX_STACK_SIZE;
    Limits[4].Cur = Limits[4].Max = 0;
    Limits[6].Cur = Limits[6].Max = 63429;
    Limits[LX_RLIMIT_NOFILE].Cur = Limits[LX_RLIMIT_NOFILE].Max = LX_MAX_FDS;
    Limits[8].Cur = Limits[8].Max = 8ULL * 1024 * 1024;
    Limits[11].Cur = Limits[11].Max = 63429;
    Limits[12].Cur = Limits[12].Max = 819200;
    Limits[13].Cur = Limits[13].Max = 0;
    Limits[14].Cur = Limits[14].Max = 0;
}

PLX_PROCESS
LxProcessCreate(
    _In_ PLX_INSTANCE Instance)
{
    PLX_PROCESS Process = LxAlloc(sizeof(*Process));

    if (Process == NULL)
        return NULL;

    Process->RefCount = 1;
    LxInstanceReference(Instance);
    Process->Instance = Instance;
    InitializeListHead(&Process->GlobalLink);
    InitializeListHead(&Process->Children);
    InitializeListHead(&Process->Threads);
    InitializeListHead(&Process->Timers);
    InitializeListHead(&Process->Vmas);
    ExInitializeFastMutex(&Process->Lock);
    KeInitializeEvent(&Process->VforkDone, NotificationEvent, FALSE);
    KeInitializeEvent(&Process->MmLock, SynchronizationEvent, TRUE);
    LxPollHeadInit(&Process->ExitPoll);
    LxPollHeadInit(&Process->SignalPoll);
    LxPollHeadInit(&Process->ChildPoll);
    Process->Umask = 022;
    Process->Dumpable = 1;
    Process->ExitSignal = LX_SIGCHLD;
    Process->CapBounding = (1ULL << 41) - 1;
    Process->StartTime = (LONG64)KeQueryInterruptTime();
    LxProcessDefaultLimits(Process->Limits);
    return Process;
}

PLX_PROCESS
LxProcessFork(
    _In_ PLX_PROCESS Parent)
{
    PLX_PROCESS Process = LxProcessCreate(Parent->Instance);
    LONG Fd;

    if (Process == NULL)
        return NULL;

    ExAcquireFastMutex(&Parent->Lock);
    Process->Cwd = LxStrDup(Parent->Cwd);
    Process->ExePath = (Parent->ExePath != NULL) ? LxStrDup(Parent->ExePath) : NULL;
    if (Parent->CmdlineLength != 0)
    {
        Process->Cmdline = LxAlloc(Parent->CmdlineLength);
        if (Process->Cmdline != NULL)
        {
            RtlCopyMemory(Process->Cmdline, Parent->Cmdline, Parent->CmdlineLength);
            Process->CmdlineLength = Parent->CmdlineLength;
        }
    }
    if (Parent->EnvironLength != 0)
    {
        Process->Environ = LxAlloc(Parent->EnvironLength);
        if (Process->Environ != NULL)
        {
            RtlCopyMemory(Process->Environ, Parent->Environ, Parent->EnvironLength);
            Process->EnvironLength = Parent->EnvironLength;
        }
    }
    RtlCopyMemory(Process->Comm, Parent->Comm, sizeof(Process->Comm));
    RtlCopyMemory(Process->SigAction, Parent->SigAction, sizeof(Process->SigAction));
    RtlCopyMemory(Process->Limits, Parent->Limits, sizeof(Process->Limits));
    RtlCopyMemory(Process->Uid, Parent->Uid, sizeof(Process->Uid));
    RtlCopyMemory(Process->Gid, Parent->Gid, sizeof(Process->Gid));
    Process->Fsuid = Parent->Fsuid;
    Process->Fsgid = Parent->Fsgid;
    Process->GroupCount = Parent->GroupCount;
    RtlCopyMemory(Process->Groups, Parent->Groups, sizeof(Process->Groups));
    Process->NoNewPrivs = Parent->NoNewPrivs;
    Process->KeepCaps = Parent->KeepCaps;
    Process->CapBounding = Parent->CapBounding;
    Process->CapAmbient = Parent->CapAmbient;
    Process->Dumpable = Parent->Dumpable;
    Process->OomScoreAdj = Parent->OomScoreAdj;
    Process->Trace = Parent->Trace;
    for (Fd = 0; Fd < LX_MAX_FDS; Fd++)
    {
        if (Parent->Fds[Fd] != NULL)
        {
            LxFileReference(Parent->Fds[Fd]);
            Process->Fds[Fd] = Parent->Fds[Fd];
            Process->FdFlags[Fd] = Parent->FdFlags[Fd];
        }
    }
    ExReleaseFastMutex(&Parent->Lock);

    if ((Process->Cwd == NULL) || ((Parent->ExePath != NULL) && (Process->ExePath == NULL)))
    {
        LxProcessDereference(Process);
        return NULL;
    }

    Process->ParentPid = Parent->Pid;
    Process->Pgid = Parent->Pgid;
    Process->Sid = Parent->Sid;
    Process->Tty = Parent->Tty;
    if (Process->Tty != NULL)
        LxTtyReference(Process->Tty);
    Process->Umask = Parent->Umask;
    Process->BrkBase = Parent->BrkBase;
    Process->BrkCurrent = Parent->BrkCurrent;
    Process->BrkCommitted = Parent->BrkCommitted;
    Process->BrkLimit = Parent->BrkLimit;
    Process->BrkReserved = Parent->BrkReserved;
    Process->SigTrampoline = Parent->SigTrampoline;
    LxCgroupAttach(Process, Parent->Cgroup);
    return Process;
}

VOID
LxProcessReference(
    _In_ PLX_PROCESS Process)
{
    InterlockedIncrement(&Process->RefCount);
}

VOID
LxProcessDereference(
    _In_ PLX_PROCESS Process)
{
    PLIST_ENTRY Entry;

    if (InterlockedDecrement(&Process->RefCount) != 0)
        return;

    LxFdCloseAll(Process);
    while (!IsListEmpty(&Process->Children))
    {
        PLX_CHILD Child;

        Entry = RemoveHeadList(&Process->Children);
        Child = CONTAINING_RECORD(Entry, LX_CHILD, Link);
        LxProcessDereference(Child->Lx);
        LxFree(Child);
    }
    if (Process->Cgroup != NULL)
        LxCgroupAttach(Process, NULL);
    LxItimerRelease(Process);
    LxPosixTimersRelease(Process, TRUE);
    LxMmRelease(Process);
    if (Process->Tty != NULL)
        LxTtyDereference(Process->Tty);
    LxFree(Process->Cwd);
    LxFree(Process->ExePath);
    LxFree(Process->Cmdline);
    LxFree(Process->Environ);
    LxInstanceDereference(Process->Instance);
    LxFree(Process);
}

VOID
LxProcessRegister(
    _In_ PLX_PROCESS Process)
{
    LxProcessReference(Process);
    ExAcquireFastMutex(&LxProcessListLock);
    InsertTailList(&LxProcessList, &Process->GlobalLink);
    ExReleaseFastMutex(&LxProcessListLock);
}

VOID
LxProcessUnregister(
    _In_ PLX_PROCESS Process)
{
    BOOLEAN Listed;

    ExAcquireFastMutex(&LxProcessListLock);
    Listed = !IsListEmpty(&Process->GlobalLink);
    if (Listed)
    {
        RemoveEntryList(&Process->GlobalLink);
        InitializeListHead(&Process->GlobalLink);
    }
    ExReleaseFastMutex(&LxProcessListLock);
    if (Listed)
        LxProcessDereference(Process);
}

PLX_THREAD
LxThreadCreate(
    _In_ PLX_PROCESS Process)
{
    PLX_THREAD Thread = LxAlloc(sizeof(*Thread));

    if (Thread == NULL)
        return NULL;

    LxProcessReference(Process);
    InterlockedIncrement(&Process->ThreadCount);
    Thread->Process = Process;
    InitializeListHead(&Thread->Link);
    Thread->AltStack.Flags = LX_SS_DISABLE;
    Thread->CurrentSyscall = MAXULONG;
    RtlCopyMemory(Thread->Comm, Process->Comm, sizeof(Thread->Comm));
    return Thread;
}

VOID
LxThreadFree(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;

    InterlockedDecrement(&Process->ThreadCount);
    LxProcessDereference(Process);
    LxFree(Thread);
}

PLX_PROCESS
LxProcessLookup(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Pid)
{
    PLIST_ENTRY Entry;
    PLX_PROCESS Found = NULL;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Instance == Instance) && (Process->Pid == Pid))
        {
            LxProcessReference(Process);
            Found = Process;
            break;
        }
    }
    ExReleaseFastMutex(&LxProcessListLock);
    return Found;
}

PLX_THREAD
LxThreadLookupReferenced(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Tid,
    _Out_ PLX_PROCESS *Owner)
{
    PLIST_ENTRY Entry, ThreadEntry;
    PLX_THREAD Found = NULL;

    *Owner = NULL;
    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Found == NULL); Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Instance != Instance) || Process->Exited)
            continue;
        ExAcquireFastMutex(&Process->Lock);
        for (ThreadEntry = Process->Threads.Flink; ThreadEntry != &Process->Threads; ThreadEntry = ThreadEntry->Flink)
        {
            PLX_THREAD Thread = CONTAINING_RECORD(ThreadEntry, LX_THREAD, Link);

            if (Thread->Tid == Tid)
            {
                Found = Thread;
                LxProcessReference(Process);
                *Owner = Process;
                break;
            }
        }
        ExReleaseFastMutex(&Process->Lock);
    }
    ExReleaseFastMutex(&LxProcessListLock);
    return Found;
}

VOID
LxProcessExitStatus(
    _In_ PLX_PROCESS Process,
    _In_ LONG WaitStatus)
{
    ExAcquireFastMutex(&Process->Lock);
    if (!Process->WaitStatusValid)
    {
        Process->WaitStatus = WaitStatus;
        Process->WaitStatusValid = TRUE;
    }
    ExReleaseFastMutex(&Process->Lock);
}

VOID
LxProcessExitGroup(
    _In_ PLX_PROCESS Process,
    _In_ LONG WaitStatus)
{
    NTSTATUS ExitCode;

    LxProcessExitStatus(Process, WaitStatus);
    if ((WaitStatus & 0x7F) == 0)
        ExitCode = (WaitStatus >> 8) & 0xFF;
    else
        ExitCode = 128 + (WaitStatus & 0x7F);

    ZwTerminateProcess(NtCurrentProcess(), ExitCode);
    PsPicoTerminateCurrentThread(ExitCode);
}

VOID
LxProcessSetCommand(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Args,
    _In_ ULONG ArgLength,
    _In_ PCSTR Env,
    _In_ ULONG EnvLength,
    _In_ PCSTR ExePath,
    _In_ PCSTR CommPath)
{
    PSTR NewArgs = (ArgLength != 0) ? LxAlloc(ArgLength) : NULL;
    PSTR NewEnv = (EnvLength != 0) ? LxAlloc(EnvLength) : NULL;
    PSTR NewExe = LxStrDup(ExePath);
    PCSTR Base = strrchr(CommPath, '/');
    PSTR OldArgs, OldEnv, OldExe;

    if (NewArgs != NULL)
        RtlCopyMemory(NewArgs, Args, ArgLength);
    if (NewEnv != NULL)
        RtlCopyMemory(NewEnv, Env, EnvLength);
    Base = (Base != NULL) ? Base + 1 : CommPath;

    ExAcquireFastMutex(&Process->Lock);
    OldArgs = Process->Cmdline;
    OldEnv = Process->Environ;
    OldExe = Process->ExePath;
    Process->Cmdline = NewArgs;
    Process->CmdlineLength = (NewArgs != NULL) ? ArgLength : 0;
    Process->Environ = NewEnv;
    Process->EnvironLength = (NewEnv != NULL) ? EnvLength : 0;
    Process->ExePath = (NewExe != NULL) ? NewExe : OldExe;
    RtlZeroMemory(Process->Comm, sizeof(Process->Comm));
    RtlCopyMemory(Process->Comm, Base, min(strlen(Base), sizeof(Process->Comm) - 1));
    ExReleaseFastMutex(&Process->Lock);

    LxFree(OldArgs);
    LxFree(OldEnv);
    if (NewExe != NULL)
        LxFree(OldExe);
}

static
PLX_PROCESS
LxProcessFindReaper(
    _In_ PLX_PROCESS Process)
{
    PLX_INSTANCE Instance = Process->Instance;
    ULONG Pid = Process->ParentPid;
    ULONG Depth;

    for (Depth = 0; (Pid > 1) && (Depth < 256); Depth++)
    {
        PLX_PROCESS Ancestor = LxProcessLookup(Instance, Pid);

        if (Ancestor == NULL)
            break;
        if (Ancestor->ChildSubreaper && !Ancestor->Exited)
            return Ancestor;
        Pid = Ancestor->ParentPid;
        LxProcessDereference(Ancestor);
    }
    return LxProcessLookup(Instance, 1);
}

static
VOID
LxProcessNotifyParent(
    _In_ PLX_PROCESS Parent,
    _In_ PLX_PROCESS Child)
{
    LX_SIGINFO Info;
    LONG Status = Child->WaitStatus;
    ULONG64 Handler, Flags;

    ExAcquireFastMutex(&Parent->Lock);
    Handler = Parent->SigAction[LX_SIGCHLD - 1].Handler;
    Flags = Parent->SigAction[LX_SIGCHLD - 1].Flags;
    ExReleaseFastMutex(&Parent->Lock);

    if ((Handler == LX_SIG_IGN) || (Flags & 2))
    {
        PLIST_ENTRY Entry;
        PLX_CHILD Found = NULL;

        ExAcquireFastMutex(&Parent->Lock);
        for (Entry = Parent->Children.Flink; Entry != &Parent->Children; Entry = Entry->Flink)
        {
            PLX_CHILD Candidate = CONTAINING_RECORD(Entry, LX_CHILD, Link);

            if (Candidate->Lx == Child)
            {
                Found = Candidate;
                RemoveEntryList(&Candidate->Link);
                break;
            }
        }
        ExReleaseFastMutex(&Parent->Lock);
        if (Found != NULL)
        {
            LxProcessUnregister(Child);
            LxProcessDereference(Found->Lx);
            LxFree(Found);
        }
    }

    if (Child->ExitSignal != 0)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        if (Status & 0x7F)
        {
            Info.Code = (Status & 0x80) ? LX_CLD_DUMPED : LX_CLD_KILLED;
            Info.Child.Status = Status & 0x7F;
        }
        else
        {
            Info.Code = LX_CLD_EXITED;
            Info.Child.Status = (Status >> 8) & 0xFF;
        }
        Info.Child.Pid = (LONG)Child->Pid;
        Info.Child.Uid = Child->Uid[0];
        LxSignalSend(Parent, NULL, (LONG)Child->ExitSignal, &Info);
    }
    LxPollWake(&Parent->ChildPoll, LX_POLLIN);
}

VOID
LxProcessExited(
    _In_ PLX_PROCESS Process,
    _In_ NTSTATUS ExitStatus)
{
    PLX_INSTANCE Instance = Process->Instance;
    PLX_PROCESS Parent, Reaper;
    LIST_ENTRY Orphans;

    LxProcessExitStatus(Process, (ExitStatus & 0xFF) << 8);
    Process->ExitTime = (LONG64)KeQueryInterruptTime();
    if (Process->RealTimer != NULL)
        KeCancelTimer(&Process->RealTimer->Timer);
    LxPosixTimersRelease(Process, FALSE);
    LxFdCloseAll(Process);
    KeSetEvent(&Process->VforkDone, IO_NO_INCREMENT, FALSE);

    ExAcquireFastMutex(&Process->Lock);
    Process->Exited = TRUE;
    InitializeListHead(&Orphans);
    while (!IsListEmpty(&Process->Children))
        InsertTailList(&Orphans, RemoveHeadList(&Process->Children));
    ExReleaseFastMutex(&Process->Lock);

    if (Process->Pid == 1)
    {
        DPRINT1("Linux instance init '%s' exited with wait status 0x%lx\n", Process->Comm, Process->WaitStatus);
        Instance->InitStatus = Process->WaitStatus;
        Instance->ShuttingDown = TRUE;
        LxSignalGroup(Instance, 0, LX_SIGKILL);
        while (!IsListEmpty(&Orphans))
        {
            PLX_CHILD Child = CONTAINING_RECORD(RemoveHeadList(&Orphans), LX_CHILD, Link);

            LxProcessUnregister(Child->Lx);
            LxProcessDereference(Child->Lx);
            LxFree(Child);
        }
    }
    else if (!IsListEmpty(&Orphans))
    {
        Reaper = LxProcessFindReaper(Process);
        while (!IsListEmpty(&Orphans))
        {
            PLX_CHILD Child = CONTAINING_RECORD(RemoveHeadList(&Orphans), LX_CHILD, Link);

            if ((Reaper == NULL) || Reaper->Exited)
            {
                LxProcessUnregister(Child->Lx);
                LxProcessDereference(Child->Lx);
                LxFree(Child);
                continue;
            }
            Child->Lx->ParentPid = Reaper->Pid;
            ExAcquireFastMutex(&Reaper->Lock);
            InsertTailList(&Reaper->Children, &Child->Link);
            ExReleaseFastMutex(&Reaper->Lock);
            if (Child->Lx->Exited)
                LxProcessNotifyParent(Reaper, Child->Lx);
        }
        if (Reaper != NULL)
            LxProcessDereference(Reaper);
    }

    LxCgroupAttach(Process, NULL);

    Parent = (Process->ParentPid != 0) ? LxProcessLookup(Instance, Process->ParentPid) : NULL;
    if ((Parent != NULL) && !Parent->Exited)
        LxProcessNotifyParent(Parent, Process);
    else
        LxProcessUnregister(Process);
    if (Parent != NULL)
        LxProcessDereference(Parent);

    LxPollWake(&Process->ExitPoll, LX_POLLIN | LX_POLLRDNORM | LX_POLLHUP);
    if (Process->Pid == 1)
        KeSetEvent(&Instance->InitExited, IO_NO_INCREMENT, FALSE);
}

LONG64
LxFutexWait(
    _In_ ULONG64 Address,
    _In_ ULONG Value,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLX_FUTEX_WAITER Waiter;
    ULONG Current;
    NTSTATUS Status;
    LONG64 Result;
    BOOLEAN Woken;

    if (Address & 3)
        return -LX_EINVAL;

    Waiter = LxAlloc(sizeof(*Waiter));
    if (Waiter == NULL)
        return -LX_ENOMEM;
    Waiter->Process = PsGetCurrentProcess();
    Waiter->Address = Address;
    KeInitializeEvent(&Waiter->Event, NotificationEvent, FALSE);

    Result = LxCopyFromUser(&Current, Address, sizeof(Current));
    if ((Result == 0) && (Current != Value))
        Result = -LX_EAGAIN;
    if (Result != 0)
    {
        LxFree(Waiter);
        return Result;
    }

    ExAcquireFastMutex(&LxFutexLock);
    InsertTailList(&LxFutexWaiters, &Waiter->Link);
    ExReleaseFastMutex(&LxFutexLock);

    Result = LxCopyFromUser(&Current, Address, sizeof(Current));
    if ((Result == 0) && (Current != Value))
        Status = STATUS_SUCCESS;
    else
        Status = LxWaitForEvent(&Waiter->Event, Timeout);

    ExAcquireFastMutex(&LxFutexLock);
    Woken = Waiter->Woken;
    if (!Woken)
        RemoveEntryList(&Waiter->Link);
    ExReleaseFastMutex(&LxFutexLock);
    LxFree(Waiter);

    if (Woken || (Status == STATUS_SUCCESS))
        return Woken ? 0 : -LX_EAGAIN;
    return (Status == STATUS_TIMEOUT) ? -LX_ETIMEDOUT : -LX_EINTR;
}

LONG64
LxFutexWake(
    _In_ ULONG64 Address,
    _In_ LONG Count)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PLIST_ENTRY Entry, Next;
    LONG Woken = 0;

    ExAcquireFastMutex(&LxFutexLock);
    for (Entry = LxFutexWaiters.Flink; (Entry != &LxFutexWaiters) && (Woken < Count); Entry = Next)
    {
        PLX_FUTEX_WAITER Waiter = CONTAINING_RECORD(Entry, LX_FUTEX_WAITER, Link);

        Next = Entry->Flink;
        if ((Waiter->Process == Process) && (Waiter->Address == Address))
        {
            RemoveEntryList(&Waiter->Link);
            Waiter->Woken = TRUE;
            KeSetEvent(&Waiter->Event, IO_NO_INCREMENT, FALSE);
            Woken++;
        }
    }
    ExReleaseFastMutex(&LxFutexLock);
    return Woken;
}

LONG
LxSignalFromException(
    _In_ NTSTATUS ExceptionCode,
    _Out_ PLONG Code)
{
    switch (ExceptionCode)
    {
        case STATUS_ILLEGAL_INSTRUCTION:
        case STATUS_PRIVILEGED_INSTRUCTION:
        case STATUS_INVALID_LOCK_SEQUENCE:
            *Code = LX_ILL_ILLOPC;
            return LX_SIGILL;
        case STATUS_BREAKPOINT:
        case STATUS_SINGLE_STEP:
            *Code = LX_TRAP_BRKPT;
            return LX_SIGTRAP;
        case STATUS_DATATYPE_MISALIGNMENT:
        case STATUS_IN_PAGE_ERROR:
            *Code = LX_BUS_ADRALN;
            return LX_SIGBUS;
        case STATUS_FLOAT_DIVIDE_BY_ZERO:
        case STATUS_FLOAT_INVALID_OPERATION:
        case STATUS_FLOAT_OVERFLOW:
        case STATUS_FLOAT_UNDERFLOW:
        case STATUS_FLOAT_INEXACT_RESULT:
        case STATUS_FLOAT_DENORMAL_OPERAND:
        case STATUS_FLOAT_STACK_CHECK:
        case STATUS_INTEGER_DIVIDE_BY_ZERO:
        case STATUS_INTEGER_OVERFLOW:
            *Code = LX_FPE_INTDIV;
            return LX_SIGFPE;
        case STATUS_ACCESS_VIOLATION:
            *Code = LX_SEGV_ACCERR;
            return LX_SIGSEGV;
        default:
            *Code = LX_SEGV_MAPERR;
            return LX_SIGSEGV;
    }
}
