/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux system call dispatch and process, signal and time calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_TIMER_ABSTIME 1

static ULONG64 LxReportedSyscalls[1024 / 64];

static
LONG
LxTracePathArgument(
    _In_ PLX_SYSCALL_HANDLER Handler)
{
    if ((Handler == LxSysOpenat) || (Handler == LxSysNewfstatat) || (Handler == LxSysStatx) ||
        (Handler == LxSysFaccessat) || (Handler == LxSysFaccessat2) || (Handler == LxSysReadlinkat) ||
        (Handler == LxSysMkdirat) || (Handler == LxSysUnlinkat) || (Handler == LxSysMknodat) ||
        (Handler == LxSysUtimensat) || (Handler == LxSysFchmodat) || (Handler == LxSysFchownat) ||
        (Handler == LxSysExecveat) || (Handler == LxSysMount) || (Handler == LxSysSymlinkat) ||
        (Handler == LxSysRenameat) || (Handler == LxSysRenameat2) || (Handler == LxSysInotifyAddWatch))
    {
        return 1;
    }
    if ((Handler == LxSysExecve) || (Handler == LxSysChdir) || (Handler == LxSysStatfs) ||
        (Handler == LxSysTruncate) || (Handler == LxSysUmount2) || (Handler == LxSysChroot) ||
        (Handler == LxSysSetxattr) || (Handler == LxSysLsetxattr) || (Handler == LxSysGetxattr) ||
        (Handler == LxSysLgetxattr) || (Handler == LxSysListxattr) || (Handler == LxSysLlistxattr) ||
        (Handler == LxSysRemovexattr) || (Handler == LxSysLremovexattr))
    {
        return 0;
    }
    return -1;
}

static
BOOLEAN
LxSyscallMayBlock(
    _In_ PLX_SYSCALL_HANDLER Handler)
{
    return (Handler == LxSysEpollPwait) || (Handler == LxSysEpollPwait2) || (Handler == LxSysPpoll) ||
           (Handler == LxSysPselect6) || (Handler == LxSysWaitid) || (Handler == LxSysWait4) ||
           (Handler == LxSysFutex) || (Handler == LxSysFutexWaitv) || (Handler == LxSysFutexWait) ||
           (Handler == LxSysNanosleep) || (Handler == LxSysClockNanosleep) ||
           (Handler == LxSysRead) || (Handler == LxSysReadv) || (Handler == LxSysRecvmsg) ||
           (Handler == LxSysRecvfrom) || (Handler == LxSysRecvmmsg) || (Handler == LxSysRtSigtimedwait) ||
           (Handler == LxSysRtSigsuspend) || (Handler == LxSysAccept) || (Handler == LxSysAccept4) ||
           (Handler == LxSysWrite) || (Handler == LxSysWritev) || (Handler == LxSysSendmsg) ||
           (Handler == LxSysSendto) || (Handler == LxSysConnect) || (Handler == LxSysOpenat) ||
           (Handler == LxSysFlock) || (Handler == LxSysSemop) || (Handler == LxSysSemtimedop) ||
           (Handler == LxSysMsgsnd) || (Handler == LxSysMsgrcv) || (Handler == LxSysSplice) ||
           (Handler == LxSysTee) || (Handler == LxSysVmsplice);
}

static
VOID
LxTraceCall(
    _In_ PLX_CALL Call,
    _In_ PLX_SYSCALL_ENTRY Entry,
    _In_ LONG64 Result)
{
    LONG PathArgument = (Entry->Handler != NULL) ? LxTracePathArgument(Entry->Handler) : -1;
    PSTR Path = NULL;

    if ((PathArgument >= 0) && (Call->Args[PathArgument] != 0))
        LxCopyStringFromUser(Call->Args[PathArgument], &Path, 255);
    DbgPrint("lx %lu/%lu %s(0x%llx, 0x%llx, 0x%llx, 0x%llx)%s%s%s = %lld\n",
             Call->Process->Pid, Call->Thread->Tid, (Entry->Name != NULL) ? Entry->Name : "?",
             Call->Args[0], Call->Args[1], Call->Args[2], Call->Args[3],
             (Path != NULL) ? " \"" : "", (Path != NULL) ? Path : "", (Path != NULL) ? "\"" : "",
             (Result == LX_SYSCALL_NO_RESULT) ? 0LL : Result);
    LxFree(Path);
}

LONG64
LxSysCall(
    _Inout_ PLX_CALL Call)
{
    PLX_SYSCALL_ENTRY Entry = LxArchSyscallEntry(Call->Number);
    ULONG Trace = Call->Process->Trace;
    LONG64 Start;

    if ((Entry != NULL) && (Entry->Handler != NULL))
    {
        LONG64 Result;

        Call->Thread->CurrentSyscall = Call->Number;
        RtlCopyMemory(Call->Thread->CurrentArgs, Call->Args, sizeof(Call->Args));
        if (Trace > 1)
        {
            DbgPrint("lx %lu/%lu -> %s(0x%llx, 0x%llx, 0x%llx, 0x%llx)\n", Call->Process->Pid, Call->Thread->Tid,
                     (Entry->Name != NULL) ? Entry->Name : "?", Call->Args[0], Call->Args[1], Call->Args[2], Call->Args[3]);
        }
        Start = (LONG64)KeQueryInterruptTime();
        Result = Entry->Handler(Call);
        Call->Thread->CurrentSyscall = MAXULONG;
        if (((LONG64)KeQueryInterruptTime() - Start > 10000000LL) && !LxSyscallMayBlock(Entry->Handler))
        {
            DbgPrint("lx %lu/%lu slow %s took %lld ms\n", Call->Process->Pid, Call->Thread->Tid,
                     (Entry->Name != NULL) ? Entry->Name : "?", ((LONG64)KeQueryInterruptTime() - Start) / 10000);
        }

        if ((Trace > 1) || ((Trace != 0) && (Result < 0) && (Result > -4096)))
            LxTraceCall(Call, Entry, Result);
        return Result;
    }
    if ((Trace != 0) && (Entry != NULL))
        LxTraceCall(Call, Entry, -LX_ENOSYS);

    if (Call->Number < 1024)
    {
        ULONG64 Bit = 1ULL << (Call->Number % 64);

        if (!(InterlockedOr64((PLONG64)&LxReportedSyscalls[Call->Number / 64], Bit) & Bit))
        {
            DPRINT1("Linux system call %s (%lu) is not implemented\n",
                    ((Entry != NULL) && (Entry->Name != NULL)) ? Entry->Name : "?", Call->Number);
        }
    }
    return -LX_ENOSYS;
}

static
VOID
LxQueryMonotonic(
    _Out_ PLX_TIMESPEC Timespec)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Counter = KeQueryPerformanceCounter(&Frequency);

    Timespec->Seconds = Counter.QuadPart / Frequency.QuadPart;
    Timespec->Nanoseconds = (Counter.QuadPart % Frequency.QuadPart) * 1000000000LL / Frequency.QuadPart;
}

static
VOID
LxQueryRealtime(
    _Out_ PLX_TIMESPEC Timespec)
{
    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, Timespec);
}

LONG
LxQueryClock(
    _In_ ULONG Clock,
    _Out_ PLX_TIMESPEC Timespec)
{
    switch (Clock)
    {
        case LX_CLOCK_REALTIME:
        case LX_CLOCK_REALTIME_COARSE:
        case LX_CLOCK_TAI:
            LxQueryRealtime(Timespec);
            return 0;

        case LX_CLOCK_MONOTONIC:
        case LX_CLOCK_MONOTONIC_RAW:
        case LX_CLOCK_MONOTONIC_COARSE:
        case LX_CLOCK_BOOTTIME:
            LxQueryMonotonic(Timespec);
            return 0;

        case LX_CLOCK_PROCESS_CPUTIME_ID:
        {
            KERNEL_USER_TIMES Times;
            LONG64 Time = 0;

            if (NT_SUCCESS(ZwQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &Times, sizeof(Times), NULL)))
                Time = Times.UserTime.QuadPart + Times.KernelTime.QuadPart;
            Timespec->Seconds = Time / 10000000;
            Timespec->Nanoseconds = (Time % 10000000) * 100;
            return 0;
        }

        case LX_CLOCK_THREAD_CPUTIME_ID:
        {
            ULONG UserTime = 0;
            ULONG KernelTime = KeQueryRuntimeThread(KeGetCurrentThread(), &UserTime);
            LONG64 Time = (LONG64)(UserTime + KernelTime) * KeQueryTimeIncrement();

            Timespec->Seconds = Time / 10000000;
            Timespec->Nanoseconds = (Time % 10000000) * 100;
            return 0;
        }

        default:
            return -LX_EINVAL;
    }
}

static
LONG64
LxSleep(
    _In_ LONG64 Interval100ns)
{
    PLX_THREAD Thread = LxCurrentThread();
    LARGE_INTEGER Interval;
    LONG64 Deadline;
    NTSTATUS Status;

    if (Interval100ns <= 0)
        return 0;

    Deadline = (LONG64)KeQueryInterruptTime() + Interval100ns;
    for (;;)
    {
        LONG64 Now = (LONG64)KeQueryInterruptTime();

        if (LxSignalPending(Thread))
            return -LX_EINTR;
        if (Now >= Deadline)
            return 0;
        Interval.QuadPart = -(Deadline - Now);
        Status = KeDelayExecutionThread(UserMode, TRUE, &Interval);
        if (Status == STATUS_SUCCESS)
            return 0;
        if (Status == STATUS_USER_APC)
            return -LX_EINTR;
    }
}

static
BOOLEAN
LxClockKnown(
    _In_ ULONG64 Clock)
{
    return (Clock <= LX_CLOCK_BOOTTIME_ALARM) || (Clock == LX_CLOCK_TAI);
}

static
LONG64
LxAdjtimex(
    _In_ ULONG64 Pointer)
{
    LX_TIMESPEC Now;
    LX_TIMEX Timex;
    ULONG Modes;
    LONG64 Result = LxCopyFromUser(&Timex, Pointer, sizeof(Timex));

    if (Result != 0)
        return Result;
    if ((Timex.Modes != 0) && (Timex.Modes != LX_ADJ_OFFSET_SS_READ))
        return -LX_EPERM;

    Modes = Timex.Modes;
    RtlZeroMemory(&Timex, sizeof(Timex));
    LxQueryRealtime(&Now);
    Timex.Modes = Modes;
    Timex.MaxError = LX_NTP_PHASE_LIMIT;
    Timex.EstError = LX_NTP_PHASE_LIMIT;
    Timex.Status = LX_STA_UNSYNC;
    Timex.Constant = 2;
    Timex.Precision = 1;
    Timex.Tolerance = LX_NTP_TOLERANCE;
    Timex.TimeSeconds = Now.Seconds;
    Timex.TimeMicroseconds = Now.Nanoseconds / 1000;
    Timex.Tick = LX_USER_TICK_USEC;
    Result = LxCopyToUser(Pointer, &Timex, sizeof(Timex));
    return (Result != 0) ? Result : LX_TIME_ERROR;
}

LX_SYSCALL(LxSysAdjtimex)
{
    return LxAdjtimex(Call->Args[0]);
}

LX_SYSCALL(LxSysClockAdjtime)
{
    if (!LxClockKnown(Call->Args[0]))
        return -LX_EINVAL;
    if (Call->Args[0] != LX_CLOCK_REALTIME)
        return -LX_EOPNOTSUPP;
    return LxAdjtimex(Call->Args[1]);
}

LX_SYSCALL(LxSysClockSettime)
{
    LX_TIMESPEC Timespec;
    LONG64 Result;

    if (!LxClockKnown(Call->Args[0]) ||
        ((Call->Args[0] != LX_CLOCK_REALTIME) && (Call->Args[0] != LX_CLOCK_PROCESS_CPUTIME_ID) &&
         (Call->Args[0] != LX_CLOCK_THREAD_CPUTIME_ID)))
    {
        return -LX_EINVAL;
    }
    Result = LxCopyFromUser(&Timespec, Call->Args[1], sizeof(Timespec));
    if (Result != 0)
        return Result;
    if ((Call->Args[0] == LX_CLOCK_REALTIME) &&
        ((Timespec.Seconds < 0) || (Timespec.Nanoseconds < 0) || (Timespec.Nanoseconds >= 1000000000)))
    {
        return -LX_EINVAL;
    }
    return -LX_EPERM;
}

LX_SYSCALL(LxSysSettimeofday)
{
    LX_TIMEVAL Timeval;
    LONG Zone[2];
    LONG64 Result;

    if (Call->Args[0] != 0)
    {
        Result = LxCopyFromUser(&Timeval, Call->Args[0], sizeof(Timeval));
        if (Result != 0)
            return Result;
        if ((Timeval.Seconds < 0) || (Timeval.Microseconds < 0) || (Timeval.Microseconds >= 1000000))
            return -LX_EINVAL;
    }
    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(Zone, Call->Args[1], sizeof(Zone));
        if (Result != 0)
            return Result;
        if ((Zone[0] > 15 * 60) || (Zone[0] < -15 * 60))
            return -LX_EINVAL;
    }
    return -LX_EPERM;
}

BOOLEAN
LxPtraceMayRead(
    _In_ PLX_PROCESS Caller,
    _In_ PLX_PROCESS Target)
{
    PLX_NS TargetNs;
    BOOLEAN Capable, Allowed = TRUE;
    ULONG Index;

    if (Caller == Target)
        return TRUE;
    TargetNs = LxUserNsOf(Target);
    Capable = LxNsCapable(Caller, TargetNs, LX_CAP_SYS_PTRACE);
    for (Index = 0; Index < 3; Index++)
    {
        if ((Target->Uid[Index] != Caller->Uid[0]) || (Target->Gid[Index] != Caller->Gid[0]))
            Allowed = FALSE;
    }
    if (!Capable && (!Allowed || (Target->Dumpable != 1) || (TargetNs != LxCredUserNs(Caller)) ||
                     (Target->CapPermitted & ~Caller->CapPermitted)))
    {
        Allowed = FALSE;
    }
    else
    {
        Allowed = TRUE;
    }
    LxNsDereference(TargetNs);
    return Allowed;
}

static
LONG64
LxKcmpOrder(
    _In_ PVOID Left,
    _In_ PVOID Right)
{
    return ((ULONG_PTR)Left < (ULONG_PTR)Right) | (((ULONG_PTR)Left > (ULONG_PTR)Right) << 1);
}

LX_SYSCALL(LxSysKcmp)
{
    PLX_PROCESS First = NULL, Second = NULL;
    PLX_FILE Left = NULL, Right = NULL;
    LX_KCMP_EPOLL_SLOT Slot;
    LONG64 Result;
    LONG Error;

    if (((LONG)Call->Args[0] <= 0) || ((LONG)Call->Args[1] <= 0) ||
        (LxThreadLookupNr(Call->Process, (ULONG)Call->Args[0], &First) == NULL) ||
        (LxThreadLookupNr(Call->Process, (ULONG)Call->Args[1], &Second) == NULL))
    {
        Result = -LX_ESRCH;
        goto Exit;
    }
    if (!LxPtraceMayRead(Call->Process, First) || !LxPtraceMayRead(Call->Process, Second))
    {
        Result = -LX_EPERM;
        goto Exit;
    }

    switch ((ULONG)Call->Args[2])
    {
        case LX_KCMP_FILE:
            Left = LxFdGet(First, (LONG)Call->Args[3]);
            Right = LxFdGet(Second, (LONG)Call->Args[4]);
            Result = ((Left != NULL) && (Right != NULL)) ? LxKcmpOrder(Left, Right) : -LX_EBADF;
            break;

        case LX_KCMP_VM:
        case LX_KCMP_FILES:
        case LX_KCMP_FS:
        case LX_KCMP_SIGHAND:
        case LX_KCMP_IO:
            Result = LxKcmpOrder(First, Second);
            break;

        case LX_KCMP_SYSVSEM:
            Result = LxKcmpOrder(LxIpcSemUndoList(First), LxIpcSemUndoList(Second));
            break;

        case LX_KCMP_EPOLL_TFD:
            Left = LxFdGet(First, (LONG)Call->Args[3]);
            if (Left == NULL)
            {
                Result = -LX_EBADF;
                break;
            }
            Result = LxCopyFromUser(&Slot, Call->Args[4], sizeof(Slot));
            if (Result != 0)
                break;
            Right = LxFdGet(Second, (LONG)Slot.Efd);
            if (Right == NULL)
            {
                Result = -LX_EBADF;
                break;
            }
            {
                PLX_FILE Target = LxEpollTarget(Right, (LONG)Slot.Tfd, Slot.Toff, &Error);

                Result = (Target != NULL) ? LxKcmpOrder(Left, Target) : Error;
                if (Target != NULL)
                    LxFileDereference(Target);
            }
            break;

        default:
            Result = -LX_EINVAL;
            break;
    }

Exit:
    if (Left != NULL)
        LxFileDereference(Left);
    if (Right != NULL)
        LxFileDereference(Right);
    if (First != NULL)
        LxProcessDereference(First);
    if (Second != NULL)
        LxProcessDereference(Second);
    return Result;
}

static
LONG64
LxProcessVm(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN Write)
{
    ULONG64 LocalCount = Call->Args[2], RemoteCount = Call->Args[4];
    ULONG64 LocalIndex = 0, RemoteIndex = 0, LocalDone = 0, RemoteDone = 0, Total = 0, Index;
    PLX_IOVEC Local = NULL, Remote = NULL;
    PLX_PROCESS Target = NULL;
    PEPROCESS EProcess = NULL;
    LONG64 Result = 0;

    if (Call->Args[5] != 0)
        return -LX_EINVAL;
    if ((LocalCount > LX_UIO_MAXIOV) || (RemoteCount > LX_UIO_MAXIOV))
        return -LX_EINVAL;

    Local = LxAlloc((SIZE_T)(LocalCount + RemoteCount + 1) * sizeof(LX_IOVEC));
    if (Local == NULL)
        return -LX_ENOMEM;
    Remote = Local + LocalCount;
    Result = LxCopyFromUser(Local, Call->Args[1], (SIZE_T)LocalCount * sizeof(LX_IOVEC));
    for (Index = 0; (Result == 0) && (Index < LocalCount); Index++)
        Total += Local[Index].Length;
    if ((Result != 0) || (Total == 0))
        goto Exit;
    Total = 0;
    Result = LxCopyFromUser(Remote, Call->Args[3], (SIZE_T)RemoteCount * sizeof(LX_IOVEC));
    if (Result != 0)
        goto Exit;

    if (((LONG)Call->Args[0] <= 0) ||
        (LxThreadLookupNr(Call->Process, (ULONG)Call->Args[0], &Target) == NULL))
    {
        Result = -LX_ESRCH;
        goto Exit;
    }
    if (!LxPtraceMayRead(Call->Process, Target))
    {
        Result = -LX_EPERM;
        goto Exit;
    }
    if (!NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Target->NtPid), &EProcess)))
    {
        EProcess = NULL;
        Result = -LX_ESRCH;
        goto Exit;
    }

    while ((LocalIndex < LocalCount) && (RemoteIndex < RemoteCount))
    {
        ULONG64 Chunk, LocalAddress, RemoteAddress;
        SIZE_T Copied = 0;
        NTSTATUS Status;

        if (LocalDone == Local[LocalIndex].Length)
        {
            LocalIndex++;
            LocalDone = 0;
            continue;
        }
        if (RemoteDone == Remote[RemoteIndex].Length)
        {
            RemoteIndex++;
            RemoteDone = 0;
            continue;
        }
        Chunk = min(min(Local[LocalIndex].Length - LocalDone, Remote[RemoteIndex].Length - RemoteDone), 1024 * 1024);
        LocalAddress = Local[LocalIndex].Base + LocalDone;
        RemoteAddress = Remote[RemoteIndex].Base + RemoteDone;

        LxMmFaultIn(LocalAddress, (SIZE_T)Chunk, !Write);
        Result = LxMmPrepareRemote(Target, EProcess, RemoteAddress, Chunk, Write);
        if (Result != 0)
            break;
        Status = Write ? MmCopyVirtualMemory(PsGetCurrentProcess(), (PVOID)(ULONG_PTR)LocalAddress, EProcess,
                                             (PVOID)(ULONG_PTR)RemoteAddress, (SIZE_T)Chunk, UserMode, &Copied) :
                         MmCopyVirtualMemory(EProcess, (PVOID)(ULONG_PTR)RemoteAddress, PsGetCurrentProcess(),
                                             (PVOID)(ULONG_PTR)LocalAddress, (SIZE_T)Chunk, UserMode, &Copied);
        Total += Copied;
        LocalDone += Copied;
        RemoteDone += Copied;
        if (!NT_SUCCESS(Status) || (Copied < Chunk))
        {
            Result = -LX_EFAULT;
            break;
        }
    }

Exit:
    if (EProcess != NULL)
        ObDereferenceObject(EProcess);
    if (Target != NULL)
        LxProcessDereference(Target);
    LxFree(Local);
    return (Total != 0) ? (LONG64)Total : Result;
}

LX_SYSCALL(LxSysProcessVmReadv)
{
    return LxProcessVm(Call, FALSE);
}

LX_SYSCALL(LxSysProcessVmWritev)
{
    return LxProcessVm(Call, TRUE);
}

static
ULONG_PTR
NTAPI
LxMembarrierIpi(
    _In_ ULONG_PTR Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeMemoryBarrier();
    return 0;
}

LX_SYSCALL(LxSysMembarrier)
{
    static const LONG Supported = LX_MEMBARRIER_CMD_GLOBAL | LX_MEMBARRIER_CMD_GLOBAL_EXPEDITED |
                                  LX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED | LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED |
                                  LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |
                                  LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE |
                                  LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE |
                                  LX_MEMBARRIER_CMD_GET_REGISTRATIONS;
    PLX_PROCESS Process = Call->Process;
    LONG Command = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[1];

    if ((Command == LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ) ?
        ((Flags != 0) && (Flags != LX_MEMBARRIER_CMD_FLAG_CPU)) : (Flags != 0))
    {
        return -LX_EINVAL;
    }

    switch (Command)
    {
        case LX_MEMBARRIER_CMD_QUERY:
            return Supported;

        case LX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED:
        case LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
        case LX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
            InterlockedOr(&Process->Membarrier, Command);
            return 0;

        case LX_MEMBARRIER_CMD_GET_REGISTRATIONS:
            return Process->Membarrier;

        case LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED:
        case LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
        case LX_MEMBARRIER_CMD_GLOBAL:
        case LX_MEMBARRIER_CMD_GLOBAL_EXPEDITED:
            if (((Command == LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED) ||
                 (Command == LX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE)) &&
                !(Process->Membarrier & (Command << 1)))
            {
                return -LX_EPERM;
            }
            KeMemoryBarrier();
            if (KeQueryActiveProcessorCount(NULL) > 1)
                KeIpiGenericCall(LxMembarrierIpi, 0);
            KeMemoryBarrier();
            return 0;

        default:
            return -LX_EINVAL;
    }
}

LX_SYSCALL(LxSysClockGettime)
{
    LX_TIMESPEC Timespec;
    LONG Error = LxQueryClockIn(Call->Process, (ULONG)Call->Args[0], &Timespec);

    if (Error != 0)
        return Error;
    return LxCopyToUser(Call->Args[1], &Timespec, sizeof(Timespec));
}

LX_SYSCALL(LxSysClockGetres)
{
    LX_TIMESPEC Resolution = { 0, 1 };

    if ((Call->Args[0] > LX_CLOCK_BOOTTIME) && (Call->Args[0] != LX_CLOCK_TAI))
        return -LX_EINVAL;
    if ((Call->Args[0] == LX_CLOCK_REALTIME) || (Call->Args[0] == LX_CLOCK_REALTIME_COARSE) ||
        (Call->Args[0] == LX_CLOCK_TAI))
        Resolution.Nanoseconds = 100;
    if (Call->Args[1] == 0)
        return 0;
    return LxCopyToUser(Call->Args[1], &Resolution, sizeof(Resolution));
}

LX_SYSCALL(LxSysGettimeofday)
{
    LX_TIMESPEC Now;
    LX_TIMEVAL Timeval;
    ULONG Zone[2] = { 0, 0 };
    LONG64 Result = 0;

    if (Call->Args[0] != 0)
    {
        LxQueryRealtime(&Now);
        Timeval.Seconds = Now.Seconds;
        Timeval.Microseconds = Now.Nanoseconds / 1000;
        Result = LxCopyToUser(Call->Args[0], &Timeval, sizeof(Timeval));
    }
    if ((Result == 0) && (Call->Args[1] != 0))
        Result = LxCopyToUser(Call->Args[1], Zone, sizeof(Zone));
    return Result;
}

LX_SYSCALL(LxSysNanosleep)
{
    LX_TIMESPEC Request;
    LONG64 Result = LxCopyFromUser(&Request, Call->Args[0], sizeof(Request));

    if (Result != 0)
        return Result;
    if ((Request.Seconds < 0) || (Request.Nanoseconds < 0) || (Request.Nanoseconds >= 1000000000))
        return -LX_EINVAL;

    Result = LxSleep(Request.Seconds * 10000000 + (Request.Nanoseconds + 99) / 100);
    if ((Result != 0) && (Call->Args[1] != 0))
    {
        LX_TIMESPEC Remaining = { 0, 0 };

        LxCopyToUser(Call->Args[1], &Remaining, sizeof(Remaining));
    }
    return Result;
}

LX_SYSCALL(LxSysClockNanosleep)
{
    LX_TIMESPEC Request, Now;
    LONG64 Result = LxCopyFromUser(&Request, Call->Args[2], sizeof(Request));
    LONG64 Interval;

    if (Result != 0)
        return Result;
    if ((Request.Seconds < 0) || (Request.Nanoseconds < 0) || (Request.Nanoseconds >= 1000000000))
        return -LX_EINVAL;

    Interval = Request.Seconds * 10000000 + (Request.Nanoseconds + 99) / 100;
    if (Call->Args[1] & LX_TIMER_ABSTIME)
    {
        LONG Error = LxQueryClockIn(Call->Process, (ULONG)Call->Args[0], &Now);

        if (Error != 0)
            return Error;
        Interval -= Now.Seconds * 10000000 + Now.Nanoseconds / 100;
    }

    return LxSleep(Interval);
}

LX_SYSCALL(LxSysTimes)
{
    PLX_PROCESS Process = Call->Process;
    LARGE_INTEGER Now;
    LONG64 Times[4] = { 0, 0, 0, 0 };
    LONG64 Result = 0;
    LX_USAGE Usage;

    if (Call->Args[0] != 0)
    {
        LxProcessUsage(Process, NULL, &Usage);
        Times[0] = Usage.UserTime / 100000;
        Times[1] = Usage.KernelTime / 100000;
        ExAcquireFastMutex(&Process->Lock);
        Times[2] = Process->ChildUserTime / 100000;
        Times[3] = Process->ChildKernelTime / 100000;
        ExReleaseFastMutex(&Process->Lock);
        Result = LxCopyToUser(Call->Args[0], Times, sizeof(Times));
    }
    if (Result != 0)
        return Result;

    Now.QuadPart = (LONG64)KeQueryInterruptTime();
    return Now.QuadPart / 100000;
}

LX_SYSCALL(LxSysSchedYield)
{
    UNREFERENCED_PARAMETER(Call);
    ZwYieldExecution();
    return 0;
}

LX_SYSCALL(LxSysSchedGetaffinity)
{
    ULONG Count = KeQueryActiveProcessorCount(NULL);
    ULONG64 Mask = (Count >= 64) ? MAXULONG64 : ((1ULL << Count) - 1);
    LONG64 Result;

    if (Call->Args[1] < sizeof(Mask))
        return -LX_EINVAL;
    Result = LxCopyToUser(Call->Args[2], &Mask, sizeof(Mask));
    return (Result != 0) ? Result : (LONG64)sizeof(Mask);
}

static
LONG
LxSchedTarget(
    _In_ PLX_CALL Call,
    _In_ LONG Pid,
    _Out_ PLX_THREAD *Thread,
    _Out_ PLX_PROCESS *Owner)
{
    *Owner = NULL;
    *Thread = NULL;
    if (Pid < 0)
        return -LX_EINVAL;
    if ((Pid == 0) || ((ULONG)Pid == LxTidNr(Call->Process, Call->Thread)))
    {
        *Thread = Call->Thread;
        return 0;
    }
    *Thread = LxThreadLookupNr(Call->Process, (ULONG)Pid, Owner);
    return (*Thread != NULL) ? 0 : -LX_ESRCH;
}

static
VOID
LxSchedRelease(
    _In_opt_ PLX_PROCESS Owner)
{
    if (Owner != NULL)
        LxProcessDereference(Owner);
}

static
LONG
LxSchedValidate(
    _In_ PLX_SCHED_ATTR Attr)
{
    switch (Attr->Policy)
    {
        case LX_SCHED_FIFO:
        case LX_SCHED_RR:
            return ((Attr->Priority >= 1) && (Attr->Priority <= 99)) ? 0 : -LX_EINVAL;
        case LX_SCHED_NORMAL:
        case LX_SCHED_BATCH:
        case LX_SCHED_IDLE:
        case LX_SCHED_EXT:
            return (Attr->Priority == 0) ? 0 : -LX_EINVAL;
        case LX_SCHED_DEADLINE:
            if ((Attr->Priority != 0) || (Attr->Deadline == 0) || (Attr->Runtime < 1024) ||
                (Attr->Runtime > Attr->Deadline) || ((Attr->Period != 0) && (Attr->Period < Attr->Deadline)))
            {
                return -LX_EINVAL;
            }
            return 0;
    }
    return -LX_EINVAL;
}

VOID
LxSchedInherit(
    _Inout_ PLX_THREAD Child,
    _In_ PLX_THREAD Parent)
{
    Child->Sched = Parent->Sched;
    if (Child->Sched.Flags & LX_SCHED_FLAG_RESET_ON_FORK)
    {
        if ((Child->Sched.Policy == LX_SCHED_FIFO) || (Child->Sched.Policy == LX_SCHED_RR) ||
            (Child->Sched.Policy == LX_SCHED_DEADLINE))
        {
            Child->Sched.Policy = LX_SCHED_NORMAL;
            Child->Sched.Priority = 0;
            Child->Sched.Runtime = Child->Sched.Deadline = Child->Sched.Period = 0;
        }
        if (Child->Sched.Nice < 0)
            Child->Sched.Nice = 0;
        Child->Sched.Flags &= ~(ULONG64)LX_SCHED_FLAG_RESET_ON_FORK;
    }
}

LX_SYSCALL(LxSysSchedSetattr)
{
    LX_SCHED_ATTR Attr, New;
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    ULONG Size;
    LONG64 Result;

    if ((Call->Args[2] != 0) || (Call->Args[1] == 0))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Size, Call->Args[1], sizeof(Size));
    if (Result != 0)
        return Result;
    if (Size == 0)
        Size = LX_SCHED_ATTR_SIZE_VER0;
    if ((Size < LX_SCHED_ATTR_SIZE_VER0) || (Size > LX_PAGE_SIZE))
    {
        Size = sizeof(Attr);
        LxCopyToUser(Call->Args[1], &Size, sizeof(Size));
        return -LX_E2BIG;
    }
    RtlZeroMemory(&Attr, sizeof(Attr));
    Result = LxCopyFromUser(&Attr, Call->Args[1], min(Size, sizeof(Attr)));
    if (Result != 0)
        return Result;
    if (Size > sizeof(Attr))
    {
        UCHAR Byte;
        ULONG Offset;

        for (Offset = sizeof(Attr); Offset < Size; Offset++)
        {
            Result = LxCopyFromUser(&Byte, Call->Args[1] + Offset, 1);
            if (Result != 0)
                return Result;
            if (Byte != 0)
                return -LX_E2BIG;
        }
    }
    if (Attr.Flags & ~(ULONG64)LX_SCHED_FLAG_ALL)
        return -LX_EINVAL;
    if (((Attr.Flags & LX_SCHED_FLAG_UTIL_CLAMP_MIN) && (Attr.UtilMin > 1024)) ||
        ((Attr.Flags & LX_SCHED_FLAG_UTIL_CLAMP_MAX) && (Attr.UtilMax > 1024)))
    {
        return -LX_EINVAL;
    }

    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;

    New = Thread->Sched;
    if (Attr.Flags & LX_SCHED_FLAG_KEEP_POLICY)
        Attr.Policy = New.Policy;
    if (!(Attr.Flags & LX_SCHED_FLAG_KEEP_PARAMS))
    {
        Result = LxSchedValidate(&Attr);
        if (Result == 0)
        {
            New.Policy = Attr.Policy;
            New.Priority = Attr.Priority;
            New.Runtime = Attr.Runtime;
            New.Deadline = Attr.Deadline;
            New.Period = Attr.Period;
            if ((Attr.Policy != LX_SCHED_FIFO) && (Attr.Policy != LX_SCHED_RR) && (Attr.Policy != LX_SCHED_DEADLINE))
                New.Nice = max(-20, min(19, Attr.Nice));
        }
    }
    else
    {
        New.Policy = Attr.Policy;
    }
    if ((Result == 0) && (Attr.Flags & LX_SCHED_FLAG_UTIL_CLAMP_MIN))
        New.UtilMin = Attr.UtilMin;
    if ((Result == 0) && (Attr.Flags & LX_SCHED_FLAG_UTIL_CLAMP_MAX))
        New.UtilMax = Attr.UtilMax;
    if ((Result == 0) && (New.UtilMin > New.UtilMax))
        Result = -LX_EINVAL;
    if (Result == 0)
    {
        New.Flags = (New.Flags & ~(ULONG64)LX_SCHED_FLAG_RESET_ON_FORK) | (Attr.Flags & LX_SCHED_FLAG_RESET_ON_FORK);
        Thread->Sched = New;
    }
    LxSchedRelease(Owner);
    return Result;
}

LX_SYSCALL(LxSysSchedGetattr)
{
    LX_SCHED_ATTR Attr;
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    ULONG64 Size = Call->Args[2];
    LONG64 Result;

    if ((Call->Args[3] != 0) || (Call->Args[1] == 0) || (Size < LX_SCHED_ATTR_SIZE_VER0) || (Size > LX_PAGE_SIZE))
        return -LX_EINVAL;
    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Attr = Thread->Sched;
    LxSchedRelease(Owner);
    Attr.Size = (ULONG)min(Size, sizeof(Attr));
    Attr.Flags &= LX_SCHED_FLAG_RESET_ON_FORK | LX_SCHED_FLAG_UTIL_CLAMP_MIN | LX_SCHED_FLAG_UTIL_CLAMP_MAX;
    return LxCopyToUser(Call->Args[1], &Attr, Attr.Size);
}

LX_SYSCALL(LxSysSchedSetscheduler)
{
    LONG Policy = (LONG)Call->Args[1];
    LX_SCHED_ATTR Attr;
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG Priority;
    LONG64 Result;

    if ((Policy < 0) || (Call->Args[2] == 0))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Priority, Call->Args[2], sizeof(Priority));
    if (Result != 0)
        return Result;
    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;

    Attr = Thread->Sched;
    Attr.Policy = (ULONG)Policy & ~LX_SCHED_RESET_ON_FORK;
    Attr.Priority = (ULONG)Priority;
    Result = (Priority < 0) ? -LX_EINVAL : LxSchedValidate(&Attr);
    if ((Result == 0) && (Attr.Policy == LX_SCHED_DEADLINE))
        Result = -LX_EINVAL;
    if (Result == 0)
    {
        if (Policy & LX_SCHED_RESET_ON_FORK)
            Attr.Flags |= LX_SCHED_FLAG_RESET_ON_FORK;
        else
            Attr.Flags &= ~(ULONG64)LX_SCHED_FLAG_RESET_ON_FORK;
        Thread->Sched = Attr;
    }
    LxSchedRelease(Owner);
    return Result;
}

LX_SYSCALL(LxSysSchedSetparam)
{
    LX_SCHED_ATTR Attr;
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG Priority;
    LONG64 Result;

    if (Call->Args[1] == 0)
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Priority, Call->Args[1], sizeof(Priority));
    if (Result != 0)
        return Result;
    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Attr = Thread->Sched;
    Attr.Priority = (ULONG)Priority;
    Result = (Priority < 0) ? -LX_EINVAL : LxSchedValidate(&Attr);
    if (Result == 0)
        Thread->Sched.Priority = Attr.Priority;
    LxSchedRelease(Owner);
    return Result;
}

LX_SYSCALL(LxSysSchedGetparam)
{
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG Priority;
    LONG64 Result;

    if (Call->Args[1] == 0)
        return -LX_EINVAL;
    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Priority = (LONG)Thread->Sched.Priority;
    LxSchedRelease(Owner);
    return LxCopyToUser(Call->Args[1], &Priority, sizeof(Priority));
}

LX_SYSCALL(LxSysSchedGetscheduler)
{
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG64 Result;

    Result = LxSchedTarget(Call, (LONG)Call->Args[0], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Result = (LONG64)(Thread->Sched.Policy |
                      ((Thread->Sched.Flags & LX_SCHED_FLAG_RESET_ON_FORK) ? LX_SCHED_RESET_ON_FORK : 0));
    LxSchedRelease(Owner);
    return Result;
}

LX_SYSCALL(LxSysSetpriority)
{
    ULONG Which = (ULONG)Call->Args[0];
    LONG Nice = max(-20, min(19, (LONG)Call->Args[2]));
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG64 Result;

    if (Which > LX_PRIO_USER)
        return -LX_EINVAL;
    if (Which != LX_PRIO_PROCESS)
    {
        Call->Thread->Sched.Nice = Nice;
        return 0;
    }
    Result = LxSchedTarget(Call, (LONG)Call->Args[1], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Thread->Sched.Nice = Nice;
    LxSchedRelease(Owner);
    return 0;
}

LX_SYSCALL(LxSysSchedGetPriorityMax)
{
    ULONG Policy = (ULONG)Call->Args[0];

    if ((Policy == 1) || (Policy == 2))
        return (Call->Number == 125) ? 99 : 1;
    if ((Policy == 0) || (Policy == 3) || (Policy == 5) || (Policy == 6))
        return 0;
    return -LX_EINVAL;
}

LX_SYSCALL(LxSysUname)
{
    PLX_INSTANCE Instance = Call->Process->Instance;
    PLX_NS Uts = LxProcessNs(Call->Process, LxNsUts);
    LX_UTSNAME Name;

    RtlZeroMemory(&Name, sizeof(Name));
    RtlCopyMemory(Name.SysName, "Linux", sizeof("Linux"));
    ExAcquireFastMutex(&Instance->Lock);
    RtlCopyMemory(Name.NodeName, Uts->Uts.Hostname, sizeof(Uts->Uts.Hostname));
    RtlCopyMemory(Name.DomainName, Uts->Uts.DomainName, sizeof(Uts->Uts.DomainName));
    ExReleaseFastMutex(&Instance->Lock);
    RtlCopyMemory(Name.Release, LX_KERNEL_RELEASE, sizeof(LX_KERNEL_RELEASE));
    RtlCopyMemory(Name.Version, LX_KERNEL_UTS_VERSION, sizeof(LX_KERNEL_UTS_VERSION));
    RtlCopyMemory(Name.Machine, LxArchPlatform(), strlen(LxArchPlatform()) + 1);
    return LxCopyToUser(Call->Args[0], &Name, sizeof(Name));
}

static
LONG64
LxSetName(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN Domain)
{
    PLX_INSTANCE Instance = Call->Process->Instance;
    PLX_NS Uts = LxProcessNs(Call->Process, LxNsUts);
    CHAR Name[65];
    LONG64 Result;

    if (Call->Args[1] > 64)
        return -LX_EINVAL;
    if (!LxNsCapable(Call->Process, LxNsOwner(Uts), LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    Result = LxCopyFromUser(Name, Call->Args[0], (SIZE_T)Call->Args[1]);
    if (Result != 0)
        return Result;
    Name[Call->Args[1]] = '\0';
    ExAcquireFastMutex(&Instance->Lock);
    RtlCopyMemory(Domain ? Uts->Uts.DomainName : Uts->Uts.Hostname, Name, (SIZE_T)Call->Args[1] + 1);
    ExReleaseFastMutex(&Instance->Lock);
    return 0;
}

LX_SYSCALL(LxSysSethostname)
{
    return LxSetName(Call, FALSE);
}

LX_SYSCALL(LxSysSetdomainname)
{
    return LxSetName(Call, TRUE);
}

LX_SYSCALL(LxSysSysinfo)
{
    struct
    {
        LONG64 Uptime;
        ULONG64 Loads[3];
        ULONG64 TotalRam;
        ULONG64 FreeRam;
        ULONG64 SharedRam;
        ULONG64 BufferRam;
        ULONG64 TotalSwap;
        ULONG64 FreeSwap;
        USHORT Procs;
        USHORT Pad;
        ULONG Pad2;
        ULONG64 TotalHigh;
        ULONG64 FreeHigh;
        ULONG MemUnit;
        ULONG Pad3;
    } Info;
    SYSTEM_BASIC_INFORMATION Basic;
    LARGE_INTEGER Now;
    PLIST_ENTRY Entry;

    C_ASSERT(sizeof(Info) == 112);

    RtlZeroMemory(&Info, sizeof(Info));
    Now.QuadPart = (LONG64)KeQueryInterruptTime() + LxTimeNsOffset(Call->Process, LX_CLOCK_BOOTTIME) / 100;
    Info.Uptime = Now.QuadPart / 10000000;
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL)))
    {
        Info.TotalRam = (ULONG64)Basic.NumberOfPhysicalPages * Basic.PageSize;
        Info.FreeRam = Info.TotalRam / 2;
    }
    Info.MemUnit = 1;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink)->Instance == Call->Process->Instance)
            Info.Procs++;
    }
    ExReleaseFastMutex(&LxProcessListLock);

    return LxCopyToUser(Call->Args[0], &Info, sizeof(Info));
}

LX_SYSCALL(LxSysGetrandom)
{
    UCHAR Buffer[256];
    ULONG64 Length = min(Call->Args[1], sizeof(Buffer));
    LONG64 Result;

    if (Call->Args[2] & ~7ULL)
        return -LX_EINVAL;
    if (!NT_SUCCESS(LxFillRandom(Buffer, (ULONG)Length)))
        return -LX_EIO;
    Result = LxCopyToUser(Call->Args[0], Buffer, (SIZE_T)Length);
    RtlSecureZeroMemory(Buffer, sizeof(Buffer));
    return (Result != 0) ? Result : (LONG64)Length;
}

static
LONG64
LxLimits(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Resource,
    _In_opt_ PLX_RLIMIT New,
    _Out_opt_ PLX_RLIMIT Old)
{
    if (Resource >= 16)
        return -LX_EINVAL;
    ExAcquireFastMutex(&Process->Lock);
    if (Old != NULL)
        *Old = Process->Limits[Resource];
    if (New != NULL)
    {
        if (New->Cur > New->Max)
        {
            ExReleaseFastMutex(&Process->Lock);
            return -LX_EINVAL;
        }
        if ((Resource == LX_RLIMIT_NOFILE) && (New->Max > 1048576) && (New->Max != LX_RLIM_INFINITY))
        {
            ExReleaseFastMutex(&Process->Lock);
            return -LX_EPERM;
        }
        Process->Limits[Resource] = *New;
    }
    ExReleaseFastMutex(&Process->Lock);
    return 0;
}

LX_SYSCALL(LxSysGetrlimit)
{
    LX_RLIMIT Limit;
    LONG64 Result = LxLimits(Call->Process, (ULONG)Call->Args[0], NULL, &Limit);

    if (Result != 0)
        return Result;
    return LxCopyToUser(Call->Args[1], &Limit, sizeof(Limit));
}

LX_SYSCALL(LxSysSetrlimit)
{
    LX_RLIMIT Limit;
    LONG64 Result = LxCopyFromUser(&Limit, Call->Args[1], sizeof(Limit));

    if (Result != 0)
        return Result;
    return LxLimits(Call->Process, (ULONG)Call->Args[0], &Limit, NULL);
}

LX_SYSCALL(LxSysPrlimit64)
{
    PLX_PROCESS Target = Call->Process;
    LX_RLIMIT New, Old;
    LONG64 Result;

    if ((Call->Args[0] != 0) && ((ULONG)Call->Args[0] != LxPidNr(Call->Process, Call->Process)))
    {
        Target = LxProcessLookupNr(Call->Process, (ULONG)Call->Args[0]);
        if (Target == NULL)
            return -LX_ESRCH;
    }
    else
    {
        LxProcessReference(Target);
    }

    Result = 0;
    if (Call->Args[2] != 0)
        Result = LxCopyFromUser(&New, Call->Args[2], sizeof(New));
    if (Result == 0)
        Result = LxLimits(Target, (ULONG)Call->Args[1], (Call->Args[2] != 0) ? &New : NULL, &Old);
    if ((Result == 0) && (Call->Args[3] != 0))
        Result = LxCopyToUser(Call->Args[3], &Old, sizeof(Old));
    LxProcessDereference(Target);
    return Result;
}

static
LONG64
LxCopyRusage(
    _In_ ULONG64 Address,
    _In_ LONG64 UserTime,
    _In_ LONG64 KernelTime,
    _In_ ULONG64 MaxRss,
    _In_ ULONG64 MinFlt)
{
    LONG64 Usage[18];

    RtlZeroMemory(Usage, sizeof(Usage));
    Usage[0] = UserTime / 10000000;
    Usage[1] = (UserTime % 10000000) / 10;
    Usage[2] = KernelTime / 10000000;
    Usage[3] = (KernelTime % 10000000) / 10;
    Usage[4] = (LONG64)(MaxRss / 1024);
    Usage[8] = (LONG64)MinFlt;
    return LxCopyToUser(Address, Usage, sizeof(Usage));
}

LX_SYSCALL(LxSysGetrusage)
{
    PLX_PROCESS Process = Call->Process;
    LONG Who = (LONG)Call->Args[0];
    LX_USAGE Usage;

    if (Who == 0)
    {
        LxProcessUsage(Process, NULL, &Usage);
        return LxCopyRusage(Call->Args[1], Usage.UserTime, Usage.KernelTime, Usage.MaxRss, Usage.MinFlt);
    }
    if (Who == -1)
    {
        LONG64 UserTime, KernelTime;
        ULONG64 MaxRss, MinFlt;

        ExAcquireFastMutex(&Process->Lock);
        UserTime = Process->ChildUserTime;
        KernelTime = Process->ChildKernelTime;
        MaxRss = Process->ChildMaxRss;
        MinFlt = Process->ChildMinFlt;
        ExReleaseFastMutex(&Process->Lock);
        return LxCopyRusage(Call->Args[1], UserTime, KernelTime, MaxRss, MinFlt);
    }
    if (Who == 1)
    {
        ULONG UserTicks = 0;
        ULONG KernelTicks = KeQueryRuntimeThread(KeGetCurrentThread(), &UserTicks);

        LxProcessUsage(Process, NULL, &Usage);
        return LxCopyRusage(Call->Args[1], (LONG64)UserTicks * KeQueryTimeIncrement(),
                            (LONG64)KernelTicks * KeQueryTimeIncrement(), Usage.MaxRss, 0);
    }
    return -LX_EINVAL;
}

LX_SYSCALL(LxSysGetpid)
{
    return LxPidNr(Call->Process, Call->Process);
}

LX_SYSCALL(LxSysGetppid)
{
    if (LxPidNsIsInit(Call->Process))
        return 0;
    return LxGlobalToNr(Call->Process, Call->Process->ParentPid);
}

LX_SYSCALL(LxSysGettid)
{
    return LxTidNr(Call->Process, Call->Thread);
}

LX_SYSCALL(LxSysGetuid)
{
    PLX_PROCESS Process = Call->Process;

    switch (Call->Number)
    {
        case 174: return LxCredViewId(Process, FALSE, Process->Uid[0]);
        case 175: return LxCredViewId(Process, FALSE, Process->Uid[1]);
        case 176: return LxCredViewId(Process, TRUE, Process->Gid[0]);
        default: return LxCredViewId(Process, TRUE, Process->Gid[1]);
    }
}

LX_SYSCALL(LxSysGetresuid)
{
    BOOLEAN Group = (Call->Number != 148);
    PULONG Ids = Group ? Call->Process->Gid : Call->Process->Uid;
    ULONG View[3], Index;
    LONG64 Result = 0;

    for (Index = 0; Index < 3; Index++)
        View[Index] = LxCredViewId(Call->Process, Group, Ids[Index]);
    for (Index = 0; (Index < 3) && (Result == 0); Index++)
        Result = LxCopyToUser(Call->Args[Index], &View[Index], sizeof(ULONG));
    return Result;
}

static
BOOLEAN
LxSetidKnown(
    _In_reads_(3) const ULONG *Ids,
    _In_ ULONG Id)
{
    return (Id == Ids[0]) || (Id == Ids[1]) || (Id == Ids[2]);
}

LX_SYSCALL(LxSysSetid)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Number = Call->Number;
    BOOLEAN Group = (Number == 143) || (Number == 144) || (Number == 149) || (Number == 152);
    BOOLEAN Capable = LxNsCapable(Process, NULL, Group ? LX_CAP_SETGID : LX_CAP_SETUID);
    PULONG Ids = Group ? Process->Gid : Process->Uid;
    PULONG Fs = Group ? &Process->Fsgid : &Process->Fsuid;
    ULONG Arg[3], Key[3], New[3], Index;
    LX_CRED_SAVE Old;
    LONG64 Result = 0;

    for (Index = 0; Index < 3; Index++)
    {
        Arg[Index] = (ULONG)Call->Args[Index];
        Key[Index] = LxCredMakeId(Process, Group, Arg[Index]);
    }

    ExAcquireFastMutex(&Process->Lock);
    LxCredSave(Process, &Old);
    RtlCopyMemory(New, Ids, sizeof(New));
    switch (Number)
    {
        case 146:
        case 144:
            if (Key[0] == MAXULONG)
                Result = -LX_EINVAL;
            else if (Capable)
                New[0] = New[1] = New[2] = Key[0];
            else if ((Key[0] == Ids[0]) || (Key[0] == Ids[2]))
                New[1] = Key[0];
            else
                Result = -LX_EPERM;
            break;

        case 145:
        case 143:
            if (((Arg[0] != MAXULONG) && (Key[0] == MAXULONG)) || ((Arg[1] != MAXULONG) && (Key[1] == MAXULONG)))
                Result = -LX_EINVAL;
            else if ((Arg[0] != MAXULONG) && (Key[0] != Ids[0]) && (Key[0] != Ids[1]) && !Capable)
                Result = -LX_EPERM;
            else if ((Arg[1] != MAXULONG) && !LxSetidKnown(Ids, Key[1]) && !Capable)
                Result = -LX_EPERM;
            if (Result != 0)
                break;
            if (Arg[0] != MAXULONG)
                New[0] = Key[0];
            if (Arg[1] != MAXULONG)
                New[1] = Key[1];
            if ((Arg[0] != MAXULONG) || ((Arg[1] != MAXULONG) && (Key[1] != Ids[0])))
                New[2] = New[1];
            break;

        case 147:
        case 149:
            for (Index = 0; Index < 3; Index++)
            {
                if ((Arg[Index] != MAXULONG) && (Key[Index] == MAXULONG))
                    Result = -LX_EINVAL;
            }
            for (Index = 0; (Index < 3) && (Result == 0); Index++)
            {
                if ((Arg[Index] != MAXULONG) && !LxSetidKnown(Ids, Key[Index]) && !Capable)
                    Result = -LX_EPERM;
            }
            for (Index = 0; (Index < 3) && (Result == 0); Index++)
            {
                if (Arg[Index] != MAXULONG)
                    New[Index] = Key[Index];
            }
            break;

        case 151:
        case 152:
            Result = LxCredViewId(Process, Group, *Fs);
            if ((Key[0] != MAXULONG) && (Key[0] != *Fs) && (LxSetidKnown(Ids, Key[0]) || Capable))
            {
                *Fs = Key[0];
                if (!Group)
                    LxCredFixSetuid(Process, &Old, TRUE);
                LxCredCommit(Process, &Old);
            }
            ExReleaseFastMutex(&Process->Lock);
            return Result;

        default:
            Result = -LX_ENOSYS;
            break;
    }
    if (Result == 0)
    {
        RtlCopyMemory(Ids, New, sizeof(New));
        *Fs = New[1];
        if (!Group)
            LxCredFixSetuid(Process, &Old, FALSE);
        LxCredCommit(Process, &Old);
    }
    ExReleaseFastMutex(&Process->Lock);
    return Result;
}

LX_SYSCALL(LxSysGetgroups)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Size = (ULONG)Call->Args[0];
    ULONG Groups[32];
    ULONG Count, Index;

    ExAcquireFastMutex(&Process->Lock);
    Count = Process->GroupCount;
    RtlCopyMemory(Groups, Process->Groups, Count * sizeof(ULONG));
    ExReleaseFastMutex(&Process->Lock);
    for (Index = 0; Index < Count; Index++)
        Groups[Index] = LxCredViewId(Process, TRUE, Groups[Index]);

    if (Size == 0)
        return Count;
    if (Size < Count)
        return -LX_EINVAL;
    if (Count == 0)
        return 0;
    return (LxCopyToUser(Call->Args[1], Groups, Count * sizeof(ULONG)) == 0) ? (LONG64)Count : -LX_EFAULT;
}

LX_SYSCALL(LxSysSetgroups)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Size = (ULONG)Call->Args[0];
    ULONG Groups[32];
    LONG64 Result = 0;

    ULONG Index;

    if (Size > RTL_NUMBER_OF(Groups))
        return -LX_EINVAL;
    if (!LxNsCapable(Process, NULL, LX_CAP_SETGID) || !LxUserNsMaySetgroups(LxCredUserNs(Process)))
        return -LX_EPERM;
    if (Size != 0)
        Result = LxCopyFromUser(Groups, Call->Args[1], Size * sizeof(ULONG));
    if (Result != 0)
        return Result;
    for (Index = 0; Index < Size; Index++)
    {
        Groups[Index] = LxCredMakeId(Process, TRUE, Groups[Index]);
        if (Groups[Index] == MAXULONG)
            return -LX_EINVAL;
    }
    ExAcquireFastMutex(&Process->Lock);
    Process->GroupCount = Size;
    RtlCopyMemory(Process->Groups, Groups, Size * sizeof(ULONG));
    ExReleaseFastMutex(&Process->Lock);
    return 0;
}

static
LONG64
LxProcessIds(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN Session)
{
    ULONG Pid = (ULONG)Call->Args[0];
    PLX_PROCESS Target;
    LONG64 Result;

    if ((Pid == 0) || (Pid == LxPidNr(Call->Process, Call->Process)))
        return LxGlobalToNr(Call->Process, Session ? Call->Process->Sid : Call->Process->Pgid);

    Target = LxProcessLookupNr(Call->Process, Pid);
    if (Target == NULL)
        return -LX_ESRCH;
    Result = LxGlobalToNr(Call->Process, Session ? Target->Sid : Target->Pgid);
    LxProcessDereference(Target);
    return Result;
}

LX_SYSCALL(LxSysGetpgid)
{
    return LxProcessIds(Call, FALSE);
}

LX_SYSCALL(LxSysGetsid)
{
    return LxProcessIds(Call, TRUE);
}

LX_SYSCALL(LxSysSetpgid)
{
    PLX_PROCESS Process = Call->Process, Target;
    ULONG Pid = Call->Args[0] ? LxNrToGlobal(Process, (ULONG)Call->Args[0]) : Process->Pid;
    LONG Pgid = (LONG)Call->Args[1];
    LONG64 Result = 0;

    if (Pgid < 0)
        return -LX_EINVAL;
    if (Pid == 0)
        return -LX_ESRCH;
    if (Pgid == 0)
        Pgid = (LONG)Pid;
    else
    {
        Pgid = (LONG)LxNrToGlobal(Process, (ULONG)Pgid);
        if (Pgid == 0)
            return -LX_EPERM;
    }

    if (Pid == Process->Pid)
    {
        Target = Process;
        LxProcessReference(Target);
    }
    else
    {
        Target = LxProcessLookup(Process->Instance, Pid);
        if ((Target != NULL) && (Target->ParentPid != Process->Pid))
        {
            LxProcessDereference(Target);
            Target = NULL;
        }
        if (Target == NULL)
            return -LX_ESRCH;
        if (Target->Execed)
        {
            LxProcessDereference(Target);
            return -LX_EACCES;
        }
    }

    if ((Target->Sid != Process->Sid) || (Target->Sid == Target->Pid))
        Result = (Target->Sid == Target->Pid) && (Target->Pgid == (ULONG)Pgid) ? 0 : -LX_EPERM;
    else
        Target->Pgid = (ULONG)Pgid;

    LxProcessDereference(Target);
    return Result;
}

LX_SYSCALL(LxSysSetsid)
{
    PLX_PROCESS Process = Call->Process;
    PLX_TTY Tty;

    if (Process->Pgid == Process->Pid)
        return -LX_EPERM;
    Process->Sid = Process->Pid;
    Process->Pgid = Process->Pid;
    ExAcquireFastMutex(&Process->Lock);
    Tty = Process->Tty;
    Process->Tty = NULL;
    ExReleaseFastMutex(&Process->Lock);
    if (Tty != NULL)
        LxTtyDereference(Tty);
    return LxPidNr(Process, Process);
}

LX_SYSCALL(LxSysSucceed)
{
    UNREFERENCED_PARAMETER(Call);
    return 0;
}

LX_SYSCALL(LxSysSetTidAddress)
{
    Call->Thread->ClearChildTid = Call->Args[0];
    return LxTidNr(Call->Process, Call->Thread);
}

LX_SYSCALL(LxSysExit)
{
    if (Call->Process->ThreadCount <= 1)
        LxProcessExitGroup(Call->Process, ((LONG)Call->Args[0] & 0xFF) << 8);
    PsPicoTerminateCurrentThread((NTSTATUS)(Call->Args[0] & 0xFF));
}

LX_SYSCALL(LxSysExitGroup)
{
    LxProcessExitGroup(Call->Process, ((LONG)Call->Args[0] & 0xFF) << 8);
}

LX_SYSCALL(LxSysKill)
{
    PLX_INSTANCE Instance = Call->Process->Instance;
    LONG Pid = (LONG)Call->Args[0];
    LONG Signal = (LONG)Call->Args[1];
    PLX_PROCESS Target;
    LX_SIGINFO Info;
    ULONG Sent, Denied = 0;

    if ((Signal < 0) || (Signal > LX_NSIG))
        return -LX_EINVAL;

    if (Pid == -1)
    {
        Sent = LxSignalGroupInfo(Instance, Call->Process->PidNs, 0, Signal, NULL, Call->Process, &Denied);
        return ((Sent + Denied) != 0) ? 0 : -LX_ESRCH;
    }
    if (Pid <= 0)
    {
        ULONG Pgid = (Pid == 0) ? Call->Process->Pgid : LxNrToGlobal(Call->Process, (ULONG)-Pid);

        if (Pgid == 0)
            return -LX_ESRCH;
        Sent = LxSignalGroupInfo(Instance, NULL, Pgid, Signal, NULL, Call->Process, &Denied);
        return (Sent != 0) ? 0 : ((Denied != 0) ? -LX_EPERM : -LX_ESRCH);
    }

    Target = LxProcessLookupNr(Call->Process, (ULONG)Pid);
    if (Target == NULL)
        return -LX_ESRCH;
    if (LxSignalPermitted(Call->Process, Target, Signal) != 0)
    {
        LxProcessDereference(Target);
        return -LX_EPERM;
    }

    if ((Signal != 0) && !Target->Exited)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_USER;
        Info.Child.Pid = (LONG)Call->Process->Pid;
        Info.Child.Uid = Call->Process->Uid[0];
        LxSignalSend(Target, NULL, Signal, &Info);
    }
    LxProcessDereference(Target);
    return 0;
}

static
LONG64
LxSignalThread(
    _In_ PLX_CALL Call,
    _In_ LONG Tgid,
    _In_ ULONG Tid,
    _In_ LONG Signal)
{
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LX_SIGINFO Info;

    if ((Signal < 0) || (Signal > LX_NSIG) || ((LONG)Tid <= 0))
        return -LX_EINVAL;

    Thread = LxThreadLookupNr(Call->Process, Tid, &Owner);
    if (Thread == NULL)
    {
        Owner = LxProcessLookupNr(Call->Process, Tid);
        if ((Owner == NULL) || Owner->Exited)
        {
            if (Owner != NULL)
                LxProcessDereference(Owner);
            return -LX_ESRCH;
        }
    }
    if ((Tgid > 0) && (LxPidNr(Call->Process, Owner) != (ULONG)Tgid))
    {
        LxProcessDereference(Owner);
        return -LX_ESRCH;
    }
    if (LxSignalPermitted(Call->Process, Owner, Signal) != 0)
    {
        LxProcessDereference(Owner);
        return -LX_EPERM;
    }

    if (Signal != 0)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_TKILL;
        Info.Child.Pid = (LONG)Call->Process->Pid;
        Info.Child.Uid = Call->Process->Uid[0];
        LxSignalSend(Owner, Thread, Signal, &Info);
    }
    LxProcessDereference(Owner);
    return 0;
}

LX_SYSCALL(LxSysTkill)
{
    return LxSignalThread(Call, -1, (ULONG)Call->Args[0], (LONG)Call->Args[1]);
}

LX_SYSCALL(LxSysTgkill)
{
    if ((LONG)Call->Args[0] <= 0)
        return -LX_EINVAL;
    return LxSignalThread(Call, (LONG)Call->Args[0], (ULONG)Call->Args[1], (LONG)Call->Args[2]);
}

LX_SYSCALL(LxSysRtSigaction)
{
    PLX_PROCESS Process = Call->Process;
    LONG Signal = (LONG)Call->Args[0];
    LX_SIGACTION New, Old;
    LONG64 Result;

    if ((Signal < 1) || (Signal > LX_NSIG) || (Call->Args[3] != sizeof(ULONG64)))
        return -LX_EINVAL;

    if (Call->Args[1] != 0)
    {
        if ((Signal == LX_SIGKILL) || (Signal == LX_SIGSTOP))
            return -LX_EINVAL;
        Result = LxArchReadSigaction(Call->Args[1], &New);
        if (Result != 0)
            return Result;
    }

    ExAcquireFastMutex(&Process->Lock);
    Old = Process->SigAction[Signal - 1];
    if (Call->Args[1] != 0)
        Process->SigAction[Signal - 1] = New;
    ExReleaseFastMutex(&Process->Lock);

    if ((Call->Args[1] != 0) && (New.Handler == LX_SIG_IGN))
    {
        ULONG64 Bit = 1ULL << (Signal - 1);
        PLIST_ENTRY Entry;

        InterlockedAnd64(&Process->Pending, ~(LONG64)Bit);
        ExAcquireFastMutex(&Process->Lock);
        for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
            InterlockedAnd64(&CONTAINING_RECORD(Entry, LX_THREAD, Link)->Pending, ~(LONG64)Bit);
        ExReleaseFastMutex(&Process->Lock);
    }

    if (Call->Args[2] != 0)
        return LxArchWriteSigaction(Call->Args[2], &Old);
    return 0;
}

LX_SYSCALL(LxSysRtSigprocmask)
{
    PLX_THREAD Thread = Call->Thread;
    ULONG64 Old = Thread->SigMask, Set;
    LONG64 Result;

    if (Call->Args[3] != sizeof(ULONG64))
        return -LX_EINVAL;

    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(&Set, Call->Args[1], sizeof(Set));
        if (Result != 0)
            return Result;
        Set &= ~((1ULL << (LX_SIGKILL - 1)) | (1ULL << (LX_SIGSTOP - 1)));

        switch (Call->Args[0])
        {
            case LX_SIG_BLOCK:
                Thread->SigMask |= Set;
                break;
            case LX_SIG_UNBLOCK:
                Thread->SigMask &= ~Set;
                break;
            case LX_SIG_SETMASK:
                Thread->SigMask = Set;
                break;
            default:
                return -LX_EINVAL;
        }
    }

    if (Call->Args[2] != 0)
        return LxCopyToUser(Call->Args[2], &Old, sizeof(Old));
    return 0;
}

typedef struct _LX_CLONE_REQUEST
{
    ULONG64 Flags;
    ULONG ExitSignal;
    ULONG64 Stack;
    ULONG64 Tls;
    ULONG64 ParentTid;
    ULONG64 ChildTid;
    ULONG64 Pidfd;
    LONG CgroupFd;
    ULONG SetTidCount;
    ULONG SetTid[LX_PIDNS_MAX_LEVEL];
} LX_CLONE_REQUEST, *PLX_CLONE_REQUEST;

static
LONG64
LxCreateThread(
    _In_ PLX_CALL Call,
    _In_ PLX_CLONE_REQUEST Request,
    _In_ PCONTEXT Context)
{
    PLX_THREAD Thread;
    CLIENT_ID ClientId;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Tid;
    LONG Error;

    if ((Request->Flags & (LX_CLONE_VM | LX_CLONE_SIGHAND)) != (LX_CLONE_VM | LX_CLONE_SIGHAND))
        return -LX_EINVAL;

    Thread = LxThreadCreate(Call->Process);
    if (Thread == NULL)
        return -LX_ENOMEM;

    Error = LxTidAssign(Call->Process, Thread, Request->SetTid, Request->SetTidCount);
    if (Error != 0)
    {
        LxThreadFree(Thread);
        return Error;
    }
    LxNsThreadInherit(Thread, Call->Thread);
    Tid = LxTidNr(Call->Process, Thread);
    Thread->Tls = (Request->Flags & LX_CLONE_SETTLS) ? Request->Tls : LxArchGetThreadPointer(Call->TrapFrame);
    Thread->SigMask = Call->Thread->SigMask;
    LxSchedInherit(Thread, Call->Thread);
    if (Request->Flags & LX_CLONE_CHILD_CLEARTID)
        Thread->ClearChildTid = Request->ChildTid;
    if (Request->Flags & LX_CLONE_CHILD_SETTID)
        Thread->SetChildTid = Request->ChildTid;
    if (Request->Flags & LX_CLONE_PARENT_SETTID)
        LxCopyToUser(Request->ParentTid, &Tid, sizeof(Tid));

    Status = PsPicoCreateThread(NtCurrentProcess(), Context, Thread, &Handle, &ClientId);
    if (!NT_SUCCESS(Status))
    {
        LxThreadFree(Thread);
        return -LxStatusToErrno(Status);
    }
    ZwClose(Handle);
    return Tid;
}

static
LONG64
LxForkProcess(
    _In_ PLX_CALL Call,
    _In_ PLX_CLONE_REQUEST Request,
    _In_ PCONTEXT Context)
{
    PLX_PROCESS Parent = Call->Process, Child, ListParent;
    PLX_THREAD Thread;
    PLX_CHILD Entry;
    PLX_CGROUP Cgroup = NULL;
    PEPROCESS EProcess = NULL;
    HANDLE ProcessHandle = NULL, ThreadHandle = NULL;
    NTSTATUS Status;
    ULONG Pid, UserPid;
    LONG Error;

    if (Request->Flags & (LX_CLONE_NEWNS | LX_CLONE_NEWUTS | LX_CLONE_NEWIPC | LX_CLONE_NEWUSER |
                          LX_CLONE_NEWPID | LX_CLONE_NEWNET | LX_CLONE_NEWCGROUP | LX_CLONE_NEWTIME))
    {
        if ((Request->Flags & LX_CLONE_NEWUSER) && (Request->Flags & (LX_CLONE_THREAD | LX_CLONE_FS)))
            return -LX_EINVAL;
        if (!(Request->Flags & LX_CLONE_NEWUSER) && !LxNsCapable(Parent, NULL, LX_CAP_SYS_ADMIN))
            return -LX_EPERM;
        if ((Request->Flags & LX_CLONE_NEWNS) && (Request->Flags & LX_CLONE_FS))
            return -LX_EINVAL;
    }
    if ((Request->Flags & LX_CLONE_PARENT) && (LxPidNsIsInit(Parent) || (Parent->Pid == 1)))
        return -LX_EINVAL;

    if (Request->Flags & LX_CLONE_INTO_CGROUP)
    {
        PLX_FILE CgroupFile = LxFdGet(Parent, Request->CgroupFd);

        if (CgroupFile == NULL)
            return -LX_EBADF;
        Cgroup = LxCgroupFromFile(CgroupFile);
        LxFileDereference(CgroupFile);
        if ((Cgroup == NULL) || Cgroup->Removed)
            return -LX_EINVAL;
    }

    if (Request->Flags & LX_CLONE_PARENT)
    {
        ListParent = LxProcessLookup(Parent->Instance, Parent->ParentPid);
        if (ListParent == NULL)
            return -LX_EINVAL;
    }
    else
    {
        ListParent = Parent;
        LxProcessReference(ListParent);
    }

    Child = LxProcessFork(Parent);
    Entry = LxAlloc(sizeof(*Entry));
    if ((Child == NULL) || (Entry == NULL))
    {
        if (Child != NULL)
            LxProcessDereference(Child);
        LxFree(Entry);
        LxProcessDereference(ListParent);
        return -LX_ENOMEM;
    }
    Child->ParentPid = ListParent->Pid;
    Child->ExitSignal = Request->ExitSignal;
    Error = LxNsInheritProcess(Child, Parent, Request->Flags);
    if (Error != 0)
    {
        LxProcessDereference(Child);
        LxFree(Entry);
        LxProcessDereference(ListParent);
        return Error;
    }
    if (Cgroup != NULL)
        LxCgroupAttach(Child, Cgroup);

    Thread = LxThreadCreate(Child);
    if (Thread == NULL)
    {
        LxProcessDereference(Child);
        LxFree(Entry);
        LxProcessDereference(ListParent);
        return -LX_ENOMEM;
    }
    Error = LxPidAssign(Child, Thread, Request->SetTid, Request->SetTidCount);
    if (Error != 0)
    {
        LxThreadFree(Thread);
        LxProcessDereference(Child);
        LxFree(Entry);
        LxProcessDereference(ListParent);
        return Error;
    }
    Pid = Child->Pid;
    UserPid = LxPidNr(Parent, Child);
    Thread->Tls = (Request->Flags & LX_CLONE_SETTLS) ? Request->Tls : LxArchGetThreadPointer(Call->TrapFrame);
    Thread->SigMask = Call->Thread->SigMask;
    LxSchedInherit(Thread, Call->Thread);
    if ((Request->Flags & (LX_CLONE_VM | LX_CLONE_VFORK)) != LX_CLONE_VM)
        Thread->AltStack = Call->Thread->AltStack;
    RtlCopyMemory(Thread->Comm, Call->Thread->Comm, sizeof(Thread->Comm));
    if (Request->Flags & LX_CLONE_CHILD_CLEARTID)
        Thread->ClearChildTid = Request->ChildTid;
    if (Request->Flags & LX_CLONE_CHILD_SETTID)
        Thread->SetChildTid = Request->ChildTid;

    LxMmLock(Parent);
    Status = (LxMmFork(Child, Parent) == 0) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
        Status = PsPicoCreateProcess(TRUE, Child, &ProcessHandle);
    LxMmUnlock(Parent);
    if (NT_SUCCESS(Status))
        Status = ObReferenceObjectByHandle(ProcessHandle, 0, *PsProcessType, KernelMode, (PVOID*)&EProcess, NULL);
    if (!NT_SUCCESS(Status))
    {
        if (ProcessHandle != NULL)
        {
            ZwTerminateProcess(ProcessHandle, Status);
            ZwClose(ProcessHandle);
        }
        LxThreadFree(Thread);
        LxProcessDereference(Child);
        LxFree(Entry);
        LxProcessDereference(ListParent);
        return -LxStatusToErrno(Status);
    }

    Child->NtPid = HandleToUlong(PsGetProcessId(EProcess));
    ObDereferenceObject(EProcess);
    LxProcessRegister(Child);
    InterlockedIncrement((PLONG)&Parent->Instance->ProcessCount);

    LxProcessReference(Child);
    Entry->Pid = Pid;
    Entry->Process = NULL;
    Entry->Lx = Child;
    ExAcquireFastMutex(&ListParent->Lock);
    InsertTailList(&ListParent->Children, &Entry->Link);
    ExReleaseFastMutex(&ListParent->Lock);
    LxProcessDereference(ListParent);

    if (Request->Flags & LX_CLONE_PIDFD)
    {
        PLX_FILE Pidfd = LxPidfdCreate(Child, 0);
        LONG Fd = -LX_ENOMEM;

        if (Pidfd != NULL)
        {
            Fd = LxFdInstall(Parent, Pidfd, 0, TRUE);
            LxFileDereference(Pidfd);
        }
        LxCopyToUser(Request->Pidfd, &Fd, sizeof(Fd));
    }
    if (Request->Flags & LX_CLONE_PARENT_SETTID)
        LxCopyToUser(Request->ParentTid, &UserPid, sizeof(UserPid));

    Status = PsPicoCreateThread(ProcessHandle, Context, Thread, &ThreadHandle, NULL);
    if (!NT_SUCCESS(Status))
    {
        LxProcessExitStatus(Child, LX_SIGKILL);
        ZwTerminateProcess(ProcessHandle, Status);
        ZwClose(ProcessHandle);
        LxThreadFree(Thread);
        LxProcessDereference(Child);
        return -LxStatusToErrno(Status);
    }

    ZwClose(ThreadHandle);
    ZwClose(ProcessHandle);

    if (Request->Flags & LX_CLONE_VFORK)
        KeWaitForSingleObject(&Child->VforkDone, UserRequest, KernelMode, FALSE, NULL);

    return UserPid;
}

static
LONG64
LxCloneCommon(
    _In_ PLX_CALL Call,
    _In_ PLX_CLONE_REQUEST Request)
{
    CONTEXT Context;
    LONG Probe = -1;

    if (((Request->Flags & (LX_CLONE_NEWNS | LX_CLONE_FS)) == (LX_CLONE_NEWNS | LX_CLONE_FS)) ||
        ((Request->Flags & (LX_CLONE_NEWUSER | LX_CLONE_FS)) == (LX_CLONE_NEWUSER | LX_CLONE_FS)) ||
        ((Request->Flags & LX_CLONE_THREAD) && !(Request->Flags & LX_CLONE_SIGHAND)) ||
        ((Request->Flags & LX_CLONE_SIGHAND) && !(Request->Flags & LX_CLONE_VM)) ||
        ((Request->Flags & LX_CLONE_THREAD) && (Request->Flags & (LX_CLONE_NEWUSER | LX_CLONE_NEWPID))) ||
        ((Request->Flags & LX_CLONE_PIDFD) && (Request->Flags & LX_CLONE_DETACHED)))
    {
        return -LX_EINVAL;
    }
    if ((Request->Flags & LX_CLONE_PIDFD) && (LxCopyToUser(Request->Pidfd, &Probe, sizeof(Probe)) != 0))
        return -LX_EFAULT;

    LxArchCaptureContext(Call->TrapFrame, Call->ExceptionFrame, &Context);
    LxArchSetForkContext(&Context, Request->Stack);

    if (Request->Flags & LX_CLONE_THREAD)
        return LxCreateThread(Call, Request, &Context);
    return LxForkProcess(Call, Request, &Context);
}

LX_SYSCALL(LxSysClone)
{
    LX_CLONE_REQUEST Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Flags = Call->Args[0] & ~0xFFULL;
    Request.ExitSignal = (ULONG)(Call->Args[0] & 0xFF);
    Request.Stack = Call->Args[1];
    Request.ParentTid = Call->Args[2];
    Request.Tls = Call->Args[3];
    Request.ChildTid = Call->Args[4];
    Request.Pidfd = Call->Args[2];
    Request.CgroupFd = -1;
    if ((Request.Flags & LX_CLONE_PIDFD) && (Request.Flags & (LX_CLONE_PARENT_SETTID | LX_CLONE_THREAD)))
        return -LX_EINVAL;
    Request.Flags &= ~(LX_CLONE_INTO_CGROUP | LX_CLONE_CLEAR_SIGHAND);
    return LxCloneCommon(Call, &Request);
}

LX_SYSCALL(LxSysClone3)
{
    LX_CLONE_ARGS Args;
    LX_CLONE_REQUEST Request;
    SIZE_T Size = (SIZE_T)Call->Args[1];
    LONG64 Result;

    if (Size > 4096)
        return -LX_E2BIG;
    if (Size < 64)
        return -LX_EINVAL;
    RtlZeroMemory(&Args, sizeof(Args));
    Result = LxCopyFromUser(&Args, Call->Args[0], min(Size, sizeof(Args)));
    if (Result != 0)
        return Result;
    if (Size > sizeof(Args))
    {
        UCHAR Extra[64];
        SIZE_T Offset, Chunk, Index;

        for (Offset = sizeof(Args); Offset < Size; Offset += Chunk)
        {
            Chunk = min(sizeof(Extra), Size - Offset);
            if (LxCopyFromUser(Extra, Call->Args[0] + Offset, Chunk) != 0)
                return -LX_EFAULT;
            for (Index = 0; Index < Chunk; Index++)
            {
                if (Extra[Index] != 0)
                    return -LX_E2BIG;
            }
        }
    }
    if ((Args.SetTidSize > LX_PIDNS_MAX_LEVEL) || ((Args.SetTid == 0) != (Args.SetTidSize == 0)) ||
        (Args.ExitSignal > LX_NSIG))
    {
        return -LX_EINVAL;
    }
    if ((Args.Flags & LX_CLONE_INTO_CGROUP) && ((Size < 88) || (Args.Cgroup > MAXLONG)))
        return -LX_EINVAL;
    if ((Args.Flags & ~(0xFFFFFFFFULL | LX_CLONE_CLEAR_SIGHAND | LX_CLONE_INTO_CGROUP)) ||
        (Args.Flags & (LX_CLONE_DETACHED | 0x7FULL)) ||
        ((Args.Flags & (LX_CLONE_THREAD | LX_CLONE_PARENT)) && (Args.ExitSignal != 0)) ||
        ((Args.Stack == 0) && (Args.StackSize != 0)) || ((Args.Stack != 0) && (Args.StackSize == 0)) ||
        ((Args.Stack != 0) && ((Args.Stack > (ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS) ||
                               (Args.StackSize > (ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS - Args.Stack))))
    {
        return -LX_EINVAL;
    }
    if ((Args.Flags & LX_CLONE_PIDFD) && (Args.Flags & LX_CLONE_THREAD))
        return -LX_EINVAL;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Flags = Args.Flags;
    Request.ExitSignal = (ULONG)Args.ExitSignal;
    Request.Stack = (Args.Stack != 0) ? (Args.Stack + Args.StackSize) : 0;
    Request.ParentTid = Args.ParentTid;
    Request.ChildTid = Args.ChildTid;
    Request.Tls = Args.Tls;
    Request.Pidfd = Args.Pidfd;
    Request.CgroupFd = (LONG)Args.Cgroup;
    if (Args.SetTidSize != 0)
    {
        Result = LxCopyFromUser(Request.SetTid, Args.SetTid, (SIZE_T)Args.SetTidSize * sizeof(ULONG));
        if (Result != 0)
            return Result;
        Request.SetTidCount = (ULONG)Args.SetTidSize;
    }

    if (Request.Flags & LX_CLONE_CLEAR_SIGHAND)
    {
        PLX_PROCESS Process = Call->Process;

        if (Request.Flags & LX_CLONE_SIGHAND)
            return -LX_EINVAL;
        Request.Flags &= ~LX_CLONE_CLEAR_SIGHAND;
        Result = LxCloneCommon(Call, &Request);
        if (Result > 0)
        {
            PLX_PROCESS Child = LxProcessLookupNr(Process, (ULONG)Result);

            if (Child != NULL)
            {
                LONG Signal;

                ExAcquireFastMutex(&Child->Lock);
                for (Signal = 0; Signal < LX_NSIG; Signal++)
                {
                    if (Child->SigAction[Signal].Handler != LX_SIG_IGN)
                        RtlZeroMemory(&Child->SigAction[Signal], sizeof(LX_SIGACTION));
                }
                ExReleaseFastMutex(&Child->Lock);
                LxProcessDereference(Child);
            }
        }
        return Result;
    }
    return LxCloneCommon(Call, &Request);
}

static
LONG
LxCaptureVector(
    _In_ ULONG64 UserVector,
    _Out_ PSTR *List,
    _Out_ PULONG Length,
    _Out_ PULONG Count)
{
    ULONG64 Pointer;
    PSTR String;
    LONG64 Result;
    ULONG StringLength;
    PSTR NewList;

    *List = NULL;
    *Length = 0;
    *Count = 0;
    if (UserVector == 0)
        return 0;

    for (;;)
    {
        Result = LxCopyFromUser(&Pointer, UserVector + (ULONG64)*Count * sizeof(ULONG64), sizeof(Pointer));
        if (Result != 0)
            return (LONG)Result;
        if (Pointer == 0)
            return 0;

        Result = LxCopyStringFromUser(Pointer, &String, LX_MAX_ARG_STRLEN);
        if (Result != 0)
            return (Result == -LX_ENAMETOOLONG) ? -LX_E2BIG : (LONG)Result;

        StringLength = (ULONG)strlen(String) + 1;
        if (*Length + StringLength > LX_MAX_ARG_TOTAL)
        {
            LxFree(String);
            return -LX_E2BIG;
        }

        NewList = LxAlloc(*Length + StringLength);
        if (NewList == NULL)
        {
            LxFree(String);
            return -LX_ENOMEM;
        }
        if (*List != NULL)
            RtlCopyMemory(NewList, *List, *Length);
        RtlCopyMemory(NewList + *Length, String, StringLength);
        LxFree(String);
        LxFree(*List);
        *List = NewList;
        *Length += StringLength;
        (*Count)++;
    }
}

static
LONG64
LxExecCommon(
    _In_ PLX_CALL Call,
    _In_ PSTR FileName,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Argv,
    _In_ ULONG64 Envp)
{
    LX_EXEC_REQUEST Request;
    NTSTATUS Status;
    LONG64 Result;
    LONG Errno = 0;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.FileName = FileName;
    Request.File = File;
    Result = LxCaptureVector(Argv, &Request.Args, &Request.ArgLength, &Request.ArgCount);
    if (Result == 0)
        Result = LxCaptureVector(Envp, &Request.Env, &Request.EnvLength, &Request.EnvCount);

    if (Result == 0)
    {
        Status = LxExecImage(Call->Thread, &Request, Call->TrapFrame, Call->ExceptionFrame, &Errno);
        if (NT_SUCCESS(Status))
            Result = LX_SYSCALL_NO_RESULT;
        else if (Status == STATUS_UNSUCCESSFUL)
            Result = LX_SIGSEGV;
        else
            Result = -Errno;
    }

    LxFree(Request.FileName);
    LxFree(Request.Args);
    LxFree(Request.Env);
    if (File != NULL)
        LxFileDereference(File);

    if (Result == LX_SIGSEGV)
        LxProcessExitGroup(Call->Process, LX_SIGSEGV);
    return Result;
}

LX_SYSCALL(LxSysExecve)
{
    PSTR FileName;
    LONG64 Result = LxCopyStringFromUser(Call->Args[0], &FileName, LX_PATH_MAX - 1);

    if (Result != 0)
        return Result;
    return LxExecCommon(Call, FileName, NULL, Call->Args[1], Call->Args[2]);
}

LX_SYSCALL(LxSysExecveat)
{
    LONG DirFd = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[4];
    PLX_FILE Source = NULL;
    PSTR Path, FileName;
    LONG64 Result;

    if (Flags & ~(LX_AT_EMPTY_PATH | LX_AT_SYMLINK_NOFOLLOW))
        return -LX_EINVAL;
    Result = LxCopyStringFromUser(Call->Args[1], &Path, LX_PATH_MAX - 1);
    if (Result != 0)
        return Result;

    if ((Path[0] == '/') || ((DirFd == LX_AT_FDCWD) && (Path[0] != '\0')))
    {
        FileName = Path;
    }
    else
    {
        PLX_FILE File = LxFdGet(Call->Process, DirFd);

        if (File == NULL)
        {
            LxFree(Path);
            return -LX_EBADF;
        }
        if ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH))
        {
            FileName = LxStrPrintf("/dev/fd/%ld", DirFd);
            Source = File;
            File = NULL;
        }
        else if ((File->Path == NULL) || (File->Path[0] != '/') || (Path[0] == '\0'))
        {
            FileName = NULL;
        }
        else
        {
            FileName = LxStrPrintf("%s/%s", File->Path, Path);
        }
        if (File != NULL)
            LxFileDereference(File);
        LxFree(Path);
        if (FileName == NULL)
        {
            if (Source != NULL)
                LxFileDereference(Source);
            return -LX_ENOENT;
        }
    }
    return LxExecCommon(Call, FileName, Source, Call->Args[2], Call->Args[3]);
}

typedef struct _LX_WAIT_SELECTOR
{
    PLX_PROCESS Process;
    ULONG Type;
    ULONG Id;
    PLX_PROCESS PidfdTarget;
    ULONG Options;
} LX_WAIT_SELECTOR, *PLX_WAIT_SELECTOR;

static
BOOLEAN
LxWaitMatches(
    _In_ PLX_WAIT_SELECTOR Selector,
    _In_ PLX_CHILD Child)
{
    switch (Selector->Type)
    {
        case LX_P_PID:
            return Child->Lx->Pid == Selector->Id;
        case LX_P_PGID:
            return Child->Lx->Pgid == Selector->Id;
        case LX_P_PIDFD:
            return Child->Lx == Selector->PidfdTarget;
        default:
            return TRUE;
    }
}

static
BOOLEAN
LxWaitReady(
    _In_ PVOID Context)
{
    PLX_WAIT_SELECTOR Selector = Context;
    PLX_PROCESS Process = Selector->Process;
    PLIST_ENTRY Entry;
    BOOLEAN Ready = FALSE, Any = FALSE;

    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Children.Flink; Entry != &Process->Children; Entry = Entry->Flink)
    {
        PLX_CHILD Child = CONTAINING_RECORD(Entry, LX_CHILD, Link);

        if (!LxWaitMatches(Selector, Child))
            continue;
        Any = TRUE;
        if (Child->Lx->Exited)
        {
            Ready = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&Process->Lock);
    return Ready || !Any;
}

static
LONG64
LxWaitChild(
    _In_ PLX_WAIT_SELECTOR Selector,
    _Out_ PULONG FoundPid,
    _Out_ PLONG WaitStatus,
    _Out_ PULONG Uid,
    _Out_ PLX_USAGE ChildUsage)
{
    PLX_PROCESS Process = Selector->Process;

    *FoundPid = 0;
    *WaitStatus = 0;
    *Uid = 0;
    RtlZeroMemory(ChildUsage, sizeof(*ChildUsage));

    for (;;)
    {
        PLIST_ENTRY Entry;
        PLX_CHILD Found = NULL;
        BOOLEAN Any = FALSE;
        LONG64 Result;

        ExAcquireFastMutex(&Process->Lock);
        for (Entry = Process->Children.Flink; Entry != &Process->Children; Entry = Entry->Flink)
        {
            PLX_CHILD Child = CONTAINING_RECORD(Entry, LX_CHILD, Link);

            if (!LxWaitMatches(Selector, Child))
                continue;
            Any = TRUE;
            if (Child->Lx->Exited)
            {
                Found = Child;
                if (!(Selector->Options & LX_WNOWAIT))
                    RemoveEntryList(&Child->Link);
                break;
            }
        }
        ExReleaseFastMutex(&Process->Lock);

        if (Found != NULL)
        {
            *FoundPid = LxPidNr(Process, Found->Lx);
            *WaitStatus = Found->Lx->WaitStatus;
            *Uid = Found->Lx->Uid[0];
            LxProcessUsage(Found->Lx, Found->Process, ChildUsage);
            ChildUsage->UserTime += Found->Lx->ChildUserTime;
            ChildUsage->KernelTime += Found->Lx->ChildKernelTime;
            ChildUsage->MinFlt += Found->Lx->ChildMinFlt;
            ChildUsage->MaxRss = max(ChildUsage->MaxRss, Found->Lx->ChildMaxRss);
            if (!(Selector->Options & LX_WNOWAIT))
            {
                ExAcquireFastMutex(&Process->Lock);
                Process->ChildUserTime += ChildUsage->UserTime;
                Process->ChildKernelTime += ChildUsage->KernelTime;
                Process->ChildMinFlt += ChildUsage->MinFlt;
                Process->ChildMaxRss = max(Process->ChildMaxRss, ChildUsage->MaxRss);
                ExReleaseFastMutex(&Process->Lock);
                LxProcessUnregister(Found->Lx);
                LxProcessDereference(Found->Lx);
                LxFree(Found);
            }
            return 0;
        }
        if (!Any)
            return -LX_ECHILD;
        if (Selector->Options & LX_WNOHANG)
            return 0;

        Result = LxWaitHead(&Process->ChildPoll, LxWaitReady, Selector, NULL);
        if ((Result == -LX_EINTR) && !LxWaitReady(Selector))
            return -LX_ERESTARTSYS;
    }
}

LX_SYSCALL(LxSysWait4)
{
    LX_WAIT_SELECTOR Selector;
    LX_USAGE Usage;
    LONG Pid = (LONG)Call->Args[0];
    ULONG FoundPid, Uid;
    LONG Status;
    LONG64 Result;

    if (Call->Args[2] & ~(LX_WNOHANG | LX_WUNTRACED | LX_WCONTINUED | LX_WNOTHREAD | LX_WALL | LX_WCLONE))
        return -LX_EINVAL;

    RtlZeroMemory(&Selector, sizeof(Selector));
    Selector.Process = Call->Process;
    Selector.Options = (ULONG)Call->Args[2];
    if (Pid > 0)
    {
        Selector.Type = LX_P_PID;
        Selector.Id = LxNrToGlobal(Call->Process, (ULONG)Pid);
        if (Selector.Id == 0)
            return -LX_ECHILD;
    }
    else if (Pid == 0)
    {
        Selector.Type = LX_P_PGID;
        Selector.Id = Call->Process->Pgid;
    }
    else if (Pid < -1)
    {
        Selector.Type = LX_P_PGID;
        Selector.Id = LxNrToGlobal(Call->Process, (ULONG)-Pid);
        if (Selector.Id == 0)
            return -LX_ECHILD;
    }
    else
    {
        Selector.Type = LX_P_ALL;
    }

    Result = LxWaitChild(&Selector, &FoundPid, &Status, &Uid, &Usage);
    if (Result != 0)
        return Result;
    if (FoundPid == 0)
        return 0;
    if (Call->Args[1] != 0)
    {
        Result = LxCopyToUser(Call->Args[1], &Status, sizeof(Status));
        if (Result != 0)
            return Result;
    }
    if (Call->Args[3] != 0)
    {
        Result = LxCopyRusage(Call->Args[3], Usage.UserTime, Usage.KernelTime, Usage.MaxRss, Usage.MinFlt);
        if (Result != 0)
            return Result;
    }
    return FoundPid;
}

LX_SYSCALL(LxSysWaitid)
{
    LX_WAIT_SELECTOR Selector;
    LX_USAGE Usage;
    ULONG Type = (ULONG)Call->Args[0];
    ULONG Options = (ULONG)Call->Args[3];
    PLX_FILE Pidfd = NULL;
    ULONG FoundPid, Uid;
    LONG Status;
    LX_SIGINFO Info;
    LONG64 Result;

    if (Options & ~(LX_WNOHANG | LX_WSTOPPED | LX_WEXITED | LX_WCONTINUED | LX_WNOWAIT | LX_WNOTHREAD | LX_WALL | LX_WCLONE))
        return -LX_EINVAL;
    if (!(Options & (LX_WEXITED | LX_WSTOPPED | LX_WCONTINUED)))
        return -LX_EINVAL;

    RtlZeroMemory(&Selector, sizeof(Selector));
    Selector.Process = Call->Process;
    Selector.Options = Options;
    Selector.Type = Type;
    Selector.Id = (ULONG)Call->Args[1];

    switch (Type)
    {
        case LX_P_ALL:
            break;
        case LX_P_PID:
            if ((LONG)Selector.Id <= 0)
                return -LX_EINVAL;
            Selector.Id = LxNrToGlobal(Call->Process, Selector.Id);
            if (Selector.Id == 0)
                return -LX_ECHILD;
            break;
        case LX_P_PGID:
            if (Selector.Id == 0)
                Selector.Id = Call->Process->Pgid;
            else
                Selector.Id = LxNrToGlobal(Call->Process, Selector.Id);
            if (Selector.Id == 0)
                return -LX_ECHILD;
            break;
        case LX_P_PIDFD:
            Pidfd = LxFdGet(Call->Process, (LONG)Selector.Id);
            if (Pidfd == NULL)
                return -LX_EBADF;
            Selector.PidfdTarget = LxPidfdProcess(Pidfd);
            if (Selector.PidfdTarget == NULL)
            {
                LxFileDereference(Pidfd);
                return -LX_EINVAL;
            }
            if (Pidfd->Flags & LX_O_NONBLOCK)
                Selector.Options |= LX_WNOHANG;
            break;
        default:
            return -LX_EINVAL;
    }

    if (!(Options & LX_WEXITED))
        Selector.Options |= LX_WNOHANG;

    Result = LxWaitChild(&Selector, &FoundPid, &Status, &Uid, &Usage);
    if ((Result == 0) && (FoundPid == 0) && (Type == LX_P_PIDFD) && !(Options & LX_WNOHANG) && (Pidfd->Flags & LX_O_NONBLOCK))
        Result = -LX_EAGAIN;
    if (Pidfd != NULL)
        LxFileDereference(Pidfd);
    if (Result != 0)
        return Result;

    RtlZeroMemory(&Info, sizeof(Info));
    if (FoundPid != 0)
    {
        Info.Signo = LX_SIGCHLD;
        Info.Child.Pid = (LONG)FoundPid;
        Info.Child.Uid = LxCredViewId(Call->Process, FALSE, Uid);
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
    }
    if (Call->Args[2] != 0)
    {
        Result = LxCopyToUser(Call->Args[2], &Info, sizeof(Info));
        if (Result != 0)
            return Result;
    }
    if (Call->Args[4] != 0)
        return LxCopyRusage(Call->Args[4], Usage.UserTime, Usage.KernelTime, Usage.MaxRss, Usage.MinFlt);
    return 0;
}

#define LX_PR_SET_MM_START_CODE     1
#define LX_PR_SET_MM_END_CODE       2
#define LX_PR_SET_MM_START_DATA     3
#define LX_PR_SET_MM_END_DATA       4
#define LX_PR_SET_MM_START_STACK    5
#define LX_PR_SET_MM_START_BRK      6
#define LX_PR_SET_MM_BRK            7
#define LX_PR_SET_MM_ARG_START      8
#define LX_PR_SET_MM_ARG_END        9
#define LX_PR_SET_MM_ENV_START      10
#define LX_PR_SET_MM_ENV_END        11
#define LX_PR_SET_MM_AUXV           12
#define LX_PR_SET_MM_EXE_FILE       13
#define LX_PR_SET_MM_MAP            14
#define LX_PR_SET_MM_MAP_SIZE       15

typedef struct _LX_PRCTL_MM_MAP
{
    ULONG64 StartCode;
    ULONG64 EndCode;
    ULONG64 StartData;
    ULONG64 EndData;
    ULONG64 StartBrk;
    ULONG64 Brk;
    ULONG64 StartStack;
    ULONG64 ArgStart;
    ULONG64 ArgEnd;
    ULONG64 EnvStart;
    ULONG64 EnvEnd;
    ULONG64 Auxv;
    ULONG AuxvSize;
    ULONG ExeFd;
} LX_PRCTL_MM_MAP, *PLX_PRCTL_MM_MAP;

static
VOID
LxPrctlMmCurrent(
    _In_ PLX_PROCESS Process,
    _Out_ PLX_PRCTL_MM_MAP Map)
{
    RtlZeroMemory(Map, sizeof(*Map));
    ExAcquireFastMutex(&Process->Lock);
    Map->StartCode = Process->StartCode;
    Map->EndCode = Process->EndCode;
    Map->StartData = Process->StartData;
    Map->EndData = Process->EndData;
    Map->StartStack = Process->StartStack;
    Map->ArgStart = Process->ArgStart;
    Map->ArgEnd = Process->ArgEnd;
    Map->EnvStart = Process->EnvStart;
    Map->EnvEnd = Process->EnvEnd;
    ExReleaseFastMutex(&Process->Lock);
    LxMmLock(Process);
    Map->StartBrk = Process->BrkBase;
    Map->Brk = Process->BrkCurrent;
    LxMmUnlock(Process);
    Map->ExeFd = MAXULONG;
}

static
LONG
LxPrctlMmValidate(
    _In_ PLX_PRCTL_MM_MAP Map)
{
    const ULONG64 Values[] = { Map->StartCode, Map->EndCode, Map->StartData, Map->EndData, Map->StartBrk, Map->Brk,
                               Map->StartStack, Map->ArgStart, Map->ArgEnd, Map->EnvStart, Map->EnvEnd };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
    {
        if ((Values[Index] > (ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS) || (Values[Index] < LX_MMAP_MIN_ADDR))
            return -LX_EINVAL;
    }
    if ((Map->StartCode >= Map->EndCode) || (Map->StartData > Map->EndData) || (Map->StartBrk > Map->Brk) ||
        (Map->ArgStart > Map->ArgEnd) || (Map->EnvStart > Map->EnvEnd))
    {
        return -LX_EINVAL;
    }
    return 0;
}

static
VOID
LxPrctlMmApply(
    _In_ PLX_PROCESS Process,
    _In_ PLX_PRCTL_MM_MAP Map)
{
    ExAcquireFastMutex(&Process->Lock);
    Process->StartCode = Map->StartCode;
    Process->EndCode = Map->EndCode;
    Process->StartData = Map->StartData;
    Process->EndData = Map->EndData;
    Process->StartStack = Map->StartStack;
    Process->ArgStart = Map->ArgStart;
    Process->ArgEnd = Map->ArgEnd;
    Process->EnvStart = Map->EnvStart;
    Process->EnvEnd = Map->EnvEnd;
    ExReleaseFastMutex(&Process->Lock);
    LxMmSetBrk(Process, Map->StartBrk, Map->Brk);
}

static
LONG
LxPrctlSetExeFile(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Fd)
{
    PLX_FILE File = LxFdGet(Process, (LONG)Fd);
    PSTR Path = NULL, Old;
    LONG Error = 0;

    if (File == NULL)
        return -LX_EBADF;
    if ((File->Type != LX_S_IFREG) || File->Directory || (File->Handle == NULL) || (File->Path == NULL) ||
        ((File->Mount != NULL) && (File->Mount->Flags & LX_MS_NOEXEC)))
    {
        Error = -LX_EACCES;
    }
    else if (LxMmMapsPath(Process, Process->ExePath))
    {
        Error = -LX_EBUSY;
    }
    else
    {
        Path = LxStrDup(File->Path);
        if (Path == NULL)
            Error = -LX_ENOMEM;
    }
    LxFileDereference(File);
    if (Error != 0)
        return Error;
    ExAcquireFastMutex(&Process->Lock);
    Old = Process->ExePath;
    Process->ExePath = Path;
    ExReleaseFastMutex(&Process->Lock);
    LxFree(Old);
    return 0;
}

static
LONG
LxPrctlSetMm(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Option,
    _In_ ULONG64 Address,
    _In_ ULONG64 Arg4,
    _In_ ULONG64 Arg5)
{
    ULONG64 Auxv[LX_AUXV_WORDS];
    LX_PRCTL_MM_MAP Map;
    LONG64 Result;
    LONG Error;

    if (Arg5 || (Arg4 && (Option != LX_PR_SET_MM_AUXV) && (Option != LX_PR_SET_MM_MAP) && (Option != LX_PR_SET_MM_MAP_SIZE)))
        return -LX_EINVAL;

    if (Option == LX_PR_SET_MM_MAP_SIZE)
    {
        ULONG Size = sizeof(LX_PRCTL_MM_MAP);

        return (LONG)LxCopyToUser(Address, &Size, sizeof(Size));
    }
    if (Option == LX_PR_SET_MM_MAP)
    {
        if (Arg4 != sizeof(Map))
            return -LX_EINVAL;
        Result = LxCopyFromUser(&Map, Address, sizeof(Map));
        if (Result != 0)
            return (LONG)Result;
        Error = LxPrctlMmValidate(&Map);
        if (Error != 0)
            return Error;
        if (Map.AuxvSize != 0)
        {
            if ((Map.Auxv == 0) || (Map.AuxvSize > sizeof(Auxv)))
                return -LX_EINVAL;
            RtlZeroMemory(Auxv, sizeof(Auxv));
            Result = LxCopyFromUser(Auxv, Map.Auxv, Map.AuxvSize);
            if (Result != 0)
                return (LONG)Result;
            Auxv[LX_AUXV_WORDS - 2] = 0;
            Auxv[LX_AUXV_WORDS - 1] = 0;
        }
        if (Map.ExeFd != MAXULONG)
        {
            if (!LxNsCapable(Process, NULL, LX_CAP_CHECKPOINT_RESTORE) && !LxNsCapable(Process, NULL, LX_CAP_SYS_ADMIN))
                return -LX_EPERM;
            Error = LxPrctlSetExeFile(Process, Map.ExeFd);
            if (Error != 0)
                return Error;
        }
        LxPrctlMmApply(Process, &Map);
        if (Map.AuxvSize != 0)
        {
            ExAcquireFastMutex(&Process->Lock);
            RtlCopyMemory(Process->Auxv, Auxv, sizeof(Auxv));
            ExReleaseFastMutex(&Process->Lock);
        }
        return 0;
    }

    if (!LxCapable(Process, LX_CAP_SYS_RESOURCE))
        return -LX_EPERM;
    if (Option == LX_PR_SET_MM_EXE_FILE)
        return LxPrctlSetExeFile(Process, (ULONG)Address);
    if (Option == LX_PR_SET_MM_AUXV)
    {
        if (Arg4 > sizeof(Auxv))
            return -LX_EINVAL;
        Result = LxCopyFromUser(Auxv, Address, (SIZE_T)Arg4);
        if (Result != 0)
            return (LONG)Result;
        ExAcquireFastMutex(&Process->Lock);
        RtlCopyMemory(Process->Auxv, Auxv, (SIZE_T)Arg4);
        ExReleaseFastMutex(&Process->Lock);
        return 0;
    }
    if ((Address > (ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS) || (Address < LX_MMAP_MIN_ADDR))
        return -LX_EINVAL;

    LxPrctlMmCurrent(Process, &Map);
    switch (Option)
    {
        case LX_PR_SET_MM_START_CODE: Map.StartCode = Address; break;
        case LX_PR_SET_MM_END_CODE: Map.EndCode = Address; break;
        case LX_PR_SET_MM_START_DATA: Map.StartData = Address; break;
        case LX_PR_SET_MM_END_DATA: Map.EndData = Address; break;
        case LX_PR_SET_MM_START_STACK: Map.StartStack = Address; break;
        case LX_PR_SET_MM_START_BRK: Map.StartBrk = Address; break;
        case LX_PR_SET_MM_BRK: Map.Brk = Address; break;
        case LX_PR_SET_MM_ARG_START: Map.ArgStart = Address; break;
        case LX_PR_SET_MM_ARG_END: Map.ArgEnd = Address; break;
        case LX_PR_SET_MM_ENV_START: Map.EnvStart = Address; break;
        case LX_PR_SET_MM_ENV_END: Map.EnvEnd = Address; break;
        default: return -LX_EINVAL;
    }
    Error = LxPrctlMmValidate(&Map);
    if (Error != 0)
        return Error;
    if (((Option == LX_PR_SET_MM_START_STACK) || (Option == LX_PR_SET_MM_ARG_START) || (Option == LX_PR_SET_MM_ARG_END) ||
         (Option == LX_PR_SET_MM_ENV_START) || (Option == LX_PR_SET_MM_ENV_END)) &&
        !LxMmHasVmaAbove(Process, Address))
    {
        return -LX_EFAULT;
    }
    LxPrctlMmApply(Process, &Map);
    return 0;
}

LX_SYSCALL(LxSysPrctl)
{
    PLX_PROCESS Process = Call->Process;
    PLX_THREAD Thread = Call->Thread;
    ULONG Option = (ULONG)Call->Args[0];
    ULONG64 Arg = Call->Args[1];
    LONG Value;

    switch (Option)
    {
        case LX_PR_SET_PDEATHSIG:
            if (Arg > LX_NSIG)
                return -LX_EINVAL;
            Process->PdeathSignal = (ULONG)Arg;
            return 0;

        case LX_PR_GET_PDEATHSIG:
            Value = (LONG)Process->PdeathSignal;
            return LxCopyToUser(Arg, &Value, sizeof(Value));

        case LX_PR_GET_DUMPABLE:
            return Process->Dumpable;

        case LX_PR_SET_DUMPABLE:
            if (Arg > 1)
                return -LX_EINVAL;
            Process->Dumpable = (ULONG)Arg;
            return 0;

        case LX_PR_GET_KEEPCAPS:
            return (Process->SecureBits & LX_SECBIT_KEEP_CAPS) ? 1 : 0;

        case LX_PR_SET_KEEPCAPS:
            if (Arg > 1)
                return -LX_EINVAL;
            if (Process->SecureBits & LX_SECBIT_KEEP_CAPS_LOCKED)
                return -LX_EPERM;
            ExAcquireFastMutex(&Process->Lock);
            if (Arg != 0)
                Process->SecureBits |= LX_SECBIT_KEEP_CAPS;
            else
                Process->SecureBits &= ~LX_SECBIT_KEEP_CAPS;
            ExReleaseFastMutex(&Process->Lock);
            return 0;

        case LX_PR_SET_NAME:
        {
            CHAR Name[16];
            LONG64 Result;
            ULONG Index;

            RtlZeroMemory(Name, sizeof(Name));
            for (Index = 0; Index < 15; Index++)
            {
                Result = LxCopyFromUser(&Name[Index], Arg + Index, 1);
                if (Result != 0)
                    return Result;
                if (Name[Index] == '\0')
                    break;
            }
            Name[15] = '\0';
            ExAcquireFastMutex(&Process->Lock);
            RtlCopyMemory(Thread->Comm, Name, sizeof(Name));
            if (Thread->Tid == Process->Pid)
                RtlCopyMemory(Process->Comm, Name, sizeof(Name));
            ExReleaseFastMutex(&Process->Lock);
            return 0;
        }

        case LX_PR_GET_NAME:
            return LxCopyToUser(Arg, Thread->Comm, sizeof(Thread->Comm));

        case LX_PR_GET_SECCOMP:
        case LX_PR_SET_SECCOMP:
            return -LX_EINVAL;

        case LX_PR_SET_MM:
            return LxPrctlSetMm(Process, (ULONG)Arg, Call->Args[2], Call->Args[3], Call->Args[4]);

        case LX_PR_CAPBSET_READ:
            if (Arg > LX_CAP_LAST_CAP)
                return -LX_EINVAL;
            return (Process->CapBounding >> Arg) & 1;

        case LX_PR_CAPBSET_DROP:
            if (!LxNsCapable(Process, NULL, LX_CAP_SETPCAP))
                return -LX_EPERM;
            if (Arg > LX_CAP_LAST_CAP)
                return -LX_EINVAL;
            ExAcquireFastMutex(&Process->Lock);
            Process->CapBounding &= ~(1ULL << Arg);
            ExReleaseFastMutex(&Process->Lock);
            return 0;

        case LX_PR_CAP_AMBIENT:
        {
            ULONG64 Bit = 1ULL << (Call->Args[2] & 63);

            if (Arg == 4)
            {
                if (Call->Args[2] | Call->Args[3] | Call->Args[4])
                    return -LX_EINVAL;
                ExAcquireFastMutex(&Process->Lock);
                Process->CapAmbient = 0;
                ExReleaseFastMutex(&Process->Lock);
                return 0;
            }
            if ((Call->Args[2] > LX_CAP_LAST_CAP) || Call->Args[3] || Call->Args[4])
                return -LX_EINVAL;
            if (Arg == 1)
                return (Process->CapAmbient & Bit) ? 1 : 0;
            if ((Arg != 2) && (Arg != 3))
                return -LX_EINVAL;
            if ((Arg == 2) && (!(Process->CapPermitted & Bit) || !(Process->CapInheritable & Bit) ||
                               (Process->SecureBits & LX_SECBIT_NO_CAP_AMBIENT_RAISE)))
            {
                return -LX_EPERM;
            }
            ExAcquireFastMutex(&Process->Lock);
            if (Arg == 2)
                Process->CapAmbient |= Bit;
            else
                Process->CapAmbient &= ~Bit;
            ExReleaseFastMutex(&Process->Lock);
            return 0;
        }

        case LX_PR_GET_SECUREBITS:
            return Process->SecureBits;

        case LX_PR_SET_SECUREBITS:
            return LxCredSetSecureBits(Process, Arg);

        case LX_PR_SET_TIMERSLACK:
            return 0;

        case LX_PR_GET_TIMERSLACK:
            return 50000;

        case LX_PR_SET_CHILD_SUBREAPER:
            Process->ChildSubreaper = (Arg != 0);
            return 0;

        case LX_PR_GET_CHILD_SUBREAPER:
            Value = Process->ChildSubreaper;
            return LxCopyToUser(Arg, &Value, sizeof(Value));

        case LX_PR_SET_NO_NEW_PRIVS:
            if (Arg != 1)
                return -LX_EINVAL;
            Process->NoNewPrivs = TRUE;
            return 0;

        case LX_PR_GET_NO_NEW_PRIVS:
            return Process->NoNewPrivs;

        case LX_PR_GET_TID_ADDRESS:
            return LxCopyToUser(Arg, &Thread->ClearChildTid, sizeof(ULONG64));

        case LX_PR_SET_THP_DISABLE:
        case LX_PR_GET_THP_DISABLE:
        case 0x59616D61:
        case 0x53564D41:
            return 0;

        default:
            return -LX_EINVAL;
    }
}

LX_SYSCALL(LxSysCapget)
{
    struct
    {
        ULONG Version;
        LONG Pid;
    } Header;
    ULONG Data[6];
    PLX_PROCESS Target = Call->Process;
    LONG64 Result;

    Result = LxCopyFromUser(&Header, Call->Args[0], sizeof(Header));
    if (Result != 0)
        return Result;
    if ((Header.Version != 0x19980330) && (Header.Version != 0x20071026) && (Header.Version != 0x20080522))
    {
        Header.Version = 0x20080522;
        Result = LxCopyToUser(Call->Args[0], &Header, sizeof(Header));
        if (Result != 0)
            return Result;
        return (Call->Args[1] != 0) ? -LX_EINVAL : 0;
    }
    if (Call->Args[1] == 0)
        return 0;
    if (Header.Pid < 0)
        return -LX_EINVAL;

    if ((Header.Pid != 0) && ((ULONG)Header.Pid != LxPidNr(Call->Process, Call->Process)))
    {
        Target = LxProcessLookupNr(Call->Process, (ULONG)Header.Pid);
        if (Target == NULL)
            return -LX_ESRCH;
    }
    else
    {
        LxProcessReference(Target);
    }

    Data[0] = (ULONG)Target->CapEffective;
    Data[1] = (ULONG)Target->CapPermitted;
    Data[2] = (ULONG)Target->CapInheritable;
    Data[3] = (ULONG)(Target->CapEffective >> 32);
    Data[4] = (ULONG)(Target->CapPermitted >> 32);
    Data[5] = (ULONG)(Target->CapInheritable >> 32);
    LxProcessDereference(Target);
    return LxCopyToUser(Call->Args[1], Data, (Header.Version == 0x19980330) ? 12 : sizeof(Data));
}

LX_SYSCALL(LxSysCapset)
{
    struct
    {
        ULONG Version;
        LONG Pid;
    } Header;
    ULONG Data[6];
    LONG64 Result = LxCopyFromUser(&Header, Call->Args[0], sizeof(Header));

    if (Result != 0)
        return Result;
    if ((Header.Version != 0x19980330) && (Header.Version != 0x20071026) && (Header.Version != 0x20080522))
    {
        Header.Version = 0x20080522;
        Result = LxCopyToUser(Call->Args[0], &Header, sizeof(Header));
        return (Result != 0) ? Result : -LX_EINVAL;
    }
    if ((Header.Pid != 0) && ((ULONG)Header.Pid != LxTidNr(Call->Process, Call->Thread)))
        return -LX_EPERM;
    RtlZeroMemory(Data, sizeof(Data));
    Result = LxCopyFromUser(Data, Call->Args[1], ((Header.Version == 0x19980330) ? 3 : 6) * sizeof(ULONG));
    if (Result != 0)
        return Result;
    return LxCredSetCapabilities(Call->Process, (Data[0] | ((ULONG64)Data[3] << 32)) & LX_CAP_FULL_SET,
                                 (Data[1] | ((ULONG64)Data[4] << 32)) & LX_CAP_FULL_SET,
                                 (Data[2] | ((ULONG64)Data[5] << 32)) & LX_CAP_FULL_SET);
}

LX_SYSCALL(LxSysReboot)
{
    PLX_PROCESS Process = Call->Process;
    PLX_INSTANCE Instance = Process->Instance;
    ULONG Command = (ULONG)Call->Args[2];
    LONG Signal;

    if (((ULONG)Call->Args[0] != LX_REBOOT_MAGIC1) ||
        (((ULONG)Call->Args[1] != LX_REBOOT_MAGIC2) && ((ULONG)Call->Args[1] != 85072278) &&
         ((ULONG)Call->Args[1] != 369367448) && ((ULONG)Call->Args[1] != 537993216)))
    {
        return -LX_EINVAL;
    }
    if (!LxNsCapable(Process, LxNsOwner(Process->PidNs), LX_CAP_SYS_BOOT))
        return -LX_EPERM;

    switch (Command)
    {
        case LX_REBOOT_CMD_CAD_ON:
        case LX_REBOOT_CMD_CAD_OFF:
            return 0;
        case LX_REBOOT_CMD_RESTART:
        case 0xA1B2C3D4:
            Signal = LX_SIGHUP;
            break;
        case LX_REBOOT_CMD_HALT:
        case LX_REBOOT_CMD_POWER_OFF:
            Signal = LX_SIGINT;
            break;
        default:
            return -LX_EINVAL;
    }

    if ((Process->PidNs != NULL) && (Process->PidNs->Level > 0))
    {
        PLX_NS PidNs = Process->PidNs;

        LxSignalGroup(Instance, PidNs, 0, LX_SIGKILL);
        if (LxPidNsIsInit(Process))
            LxProcessExitGroup(Process, Signal);
        else
        {
            PLX_PROCESS Init = LxProcessLookup(Instance, PidNs->Pid.ReaperPid);

            if (Init != NULL)
            {
                LxSignalSend(Init, NULL, LX_SIGKILL, NULL);
                LxProcessDereference(Init);
            }
        }
        return 0;
    }

    DPRINT1("Linux instance init requested %s\n", (Signal == LX_SIGHUP) ? "a restart" : "a power off");
    Instance->RebootCommand = (LONG)Command;
    Instance->ShuttingDown = TRUE;
    LxSignalGroup(Instance, NULL, 0, LX_SIGKILL);
    if (Process->Pid == 1)
        LxProcessExitGroup(Process, Signal);
    else
    {
        PLX_PROCESS Init = LxProcessLookup(Instance, 1);

        if (Init != NULL)
        {
            LX_SIGINFO Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.Code = LX_SI_KERNEL;
            LxSignalSend(Init, NULL, LX_SIGKILL, &Info);
            LxProcessDereference(Init);
        }
    }
    return 0;
}

LX_SYSCALL(LxSysGetpriority)
{
    ULONG Which = (ULONG)Call->Args[0];
    PLX_PROCESS Owner;
    PLX_THREAD Thread;
    LONG64 Result;

    if (Which > LX_PRIO_USER)
        return -LX_EINVAL;
    if (Which != LX_PRIO_PROCESS)
        return 20 - Call->Thread->Sched.Nice;
    Result = LxSchedTarget(Call, (LONG)Call->Args[1], &Thread, &Owner);
    if (Result != 0)
        return Result;
    Result = 20 - Thread->Sched.Nice;
    LxSchedRelease(Owner);
    return Result;
}

LX_SYSCALL(LxSysGetcpu)
{
    ULONG Cpu = KeGetCurrentProcessorNumber(), Node = 0;
    LONG64 Result = 0;

    if (Call->Args[0] != 0)
        Result = LxCopyToUser(Call->Args[0], &Cpu, sizeof(Cpu));
    if ((Result == 0) && (Call->Args[1] != 0))
        Result = LxCopyToUser(Call->Args[1], &Node, sizeof(Node));
    return Result;
}

LX_SYSCALL(LxSysSyslog)
{
    switch ((ULONG)Call->Args[0])
    {
        case 10:
            return 65536;
        case 9:
        case 2:
        case 3:
        case 4:
            return 0;
        default:
            return 0;
    }
}

LX_SYSCALL(LxSysIoprioGet)
{
    UNREFERENCED_PARAMETER(Call);
    return (2 << 13) | 4;
}

LX_SYSCALL(LxSysKeyctl)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_ENOSYS;
}

LX_SYSCALL(LxSysPersonality)
{
    return (Call->Args[0] == 0xFFFFFFFF) ? 0 : 0;
}

LX_SYSCALL(LxSysMincore)
{
    ULONG64 Address = Call->Args[0];
    ULONG64 End = Address + LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG64 Output = Call->Args[2];
    UCHAR Vector[64];

    if (Address & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    if (End < Address)
        return -LX_ENOMEM;
    if (!LxMmIsMapped(Call->Process, Address, End))
        return -LX_ENOMEM;

    while (Address < End)
    {
        MEMORY_BASIC_INFORMATION Information;
        ULONG64 RegionEnd;
        NTSTATUS Status;
        UCHAR Value;

        Status = ZwQueryVirtualMemory(NtCurrentProcess(), (PVOID)(ULONG_PTR)Address, MemoryBasicInformation,
                                      &Information, sizeof(Information), NULL);
        if (!NT_SUCCESS(Status) || (Information.State == MEM_FREE))
            return -LX_ENOMEM;
        Value = (Information.State == MEM_COMMIT) ? 1 : 0;
        RegionEnd = min(End, (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize);

        RtlFillMemory(Vector, sizeof(Vector), Value);
        while (Address < RegionEnd)
        {
            ULONG64 Pages = min((RegionEnd - Address) / LX_PAGE_SIZE, sizeof(Vector));
            LONG64 Result = LxCopyToUser(Output, Vector, (SIZE_T)Pages);

            if (Result != 0)
                return Result;
            Output += Pages;
            Address += Pages * LX_PAGE_SIZE;
        }
    }
    return 0;
}

static
VOID
NTAPI
LxItimerWork(
    _In_ PVOID Context)
{
    PLX_ITIMER Timer = Context;
    PLX_PROCESS Process = Timer->Process;
    LX_SIGINFO Info;

    InterlockedExchange(&Timer->WorkQueued, 0);
    if (!Process->Exited)
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_KERNEL;
        LxSignalSend(Process, NULL, LX_SIGALRM, &Info);
    }
    LxProcessDereference(Process);
}

static
VOID
NTAPI
LxItimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PLX_ITIMER Timer = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    if (Timer->IntervalNs != 0)
        Timer->Due += Timer->IntervalNs / 100;
    else
        Timer->Due = 0;
    if (InterlockedExchange(&Timer->WorkQueued, 1) == 0)
    {
        InterlockedIncrement(&Timer->Process->RefCount);
        ExQueueWorkItem(&Timer->Work, DelayedWorkQueue);
    }
}

VOID
LxItimerRelease(
    _In_ PLX_PROCESS Process)
{
    if (Process->RealTimer == NULL)
        return;
    KeCancelTimer(&Process->RealTimer->Timer);
    KeFlushQueuedDpcs();
    LxFree(Process->RealTimer);
    Process->RealTimer = NULL;
}

static
VOID
LxItimerCurrent(
    _In_opt_ PLX_ITIMER Timer,
    _Out_writes_(4) PLX_TIMEVAL Value)
{
    LONG64 Remaining = 0, Interval = 0;

    if ((Timer != NULL) && (Timer->Due != 0))
    {
        Remaining = Timer->Due - (LONG64)KeQueryInterruptTime();
        if (Remaining <= 0)
            Remaining = 10;
        Interval = Timer->IntervalNs / 100;
    }
    Value[0].Seconds = Interval / 10000000LL;
    Value[0].Microseconds = (Interval % 10000000LL) / 10;
    Value[1].Seconds = Remaining / 10000000LL;
    Value[1].Microseconds = (Remaining % 10000000LL) / 10;
}

LX_SYSCALL(LxSysGetitimer)
{
    LX_TIMEVAL Value[2];

    if (Call->Args[0] > 2)
        return -LX_EINVAL;
    LxItimerCurrent((Call->Args[0] == 0) ? Call->Process->RealTimer : NULL, Value);
    return LxCopyToUser(Call->Args[1], Value, sizeof(Value));
}

LX_SYSCALL(LxSysSetitimer)
{
    PLX_PROCESS Process = Call->Process;
    PLX_ITIMER Timer;
    LX_TIMEVAL New[2], Old[2];
    LARGE_INTEGER DueTime;
    LONG64 Value, Interval, Result;

    if (Call->Args[0] > 2)
        return -LX_EINVAL;
    RtlZeroMemory(New, sizeof(New));
    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(New, Call->Args[1], sizeof(New));
        if (Result != 0)
            return Result;
        if ((New[0].Microseconds < 0) || (New[0].Microseconds >= 1000000) ||
            (New[1].Microseconds < 0) || (New[1].Microseconds >= 1000000))
        {
            return -LX_EINVAL;
        }
    }
    if (Call->Args[0] != 0)
    {
        RtlZeroMemory(Old, sizeof(Old));
        return (Call->Args[2] != 0) ? LxCopyToUser(Call->Args[2], Old, sizeof(Old)) : 0;
    }

    Timer = Process->RealTimer;
    if (Timer == NULL)
    {
        Timer = LxAlloc(sizeof(*Timer));
        if (Timer == NULL)
            return -LX_ENOMEM;
        KeInitializeTimerEx(&Timer->Timer, NotificationTimer);
        KeInitializeDpc(&Timer->Dpc, LxItimerDpc, Timer);
        ExInitializeWorkItem(&Timer->Work, LxItimerWork, Timer);
        Timer->Process = Process;
        if (InterlockedCompareExchangePointer((PVOID *)&Process->RealTimer, Timer, NULL) != NULL)
        {
            LxFree(Timer);
            Timer = Process->RealTimer;
        }
    }

    LxItimerCurrent(Timer, Old);
    KeCancelTimer(&Timer->Timer);

    Value = New[1].Seconds * 10000000LL + New[1].Microseconds * 10;
    Interval = New[0].Seconds * 10000000LL + New[0].Microseconds * 10;
    Timer->IntervalNs = (Value != 0) ? Interval * 100 : 0;
    Timer->Due = (Value != 0) ? (LONG64)KeQueryInterruptTime() + Value : 0;
    if (Value != 0)
    {
        DueTime.QuadPart = -Value;
        KeSetTimerEx(&Timer->Timer, DueTime, (Interval != 0) ? (LONG)max(1, Interval / 10000) : 0, &Timer->Dpc);
    }

    if (Call->Args[2] != 0)
        return LxCopyToUser(Call->Args[2], Old, sizeof(Old));
    return 0;
}

#define LX_SIGEV_SIGNAL     0
#define LX_SIGEV_NONE       1
#define LX_SIGEV_THREAD     2
#define LX_SIGEV_THREAD_ID  4

typedef struct _LX_POSIX_TIMER
{
    LIST_ENTRY Link;
    KTIMER Timer;
    KDPC Dpc;
    WORK_QUEUE_ITEM Work;
    PLX_PROCESS Process;
    volatile LONG WorkQueued;
    LONG Id;
    ULONG Clock;
    LONG Signal;
    ULONG Notify;
    ULONG Tid;
    ULONG64 Value;
    LONG64 IntervalNs;
    LONG64 DueNs;
    volatile LONG Overrun;
    LONG LastOverrun;
} LX_POSIX_TIMER, *PLX_POSIX_TIMER;

typedef struct _LX_SIGEVENT
{
    ULONG64 Value;
    LONG Signal;
    LONG Notify;
    LONG ThreadId;
    UCHAR Pad[44];
} LX_SIGEVENT, *PLX_SIGEVENT;

C_ASSERT(sizeof(LX_SIGEVENT) == 64);

static
LONG64
LxClockNow(
    _In_ ULONG Clock)
{
    LX_TIMESPEC Now;

    if (LxQueryClock(Clock, &Now) != 0)
        return 0;
    return Now.Seconds * 1000000000LL + Now.Nanoseconds;
}

static
VOID
NTAPI
LxPosixTimerWork(
    _In_ PVOID Context)
{
    PLX_POSIX_TIMER Timer = Context;
    PLX_PROCESS Process = Timer->Process;
    PLX_THREAD Thread = NULL;
    PLX_PROCESS Owner = NULL;
    LX_SIGINFO Info;

    InterlockedExchange(&Timer->WorkQueued, 0);
    if (!Process->Exited && (Timer->Notify != LX_SIGEV_NONE))
    {
        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_TIMER;
        Timer->LastOverrun = InterlockedExchange(&Timer->Overrun, 0);
        RtlCopyMemory(&Info.Raw[0], &Timer->Id, sizeof(LONG));
        RtlCopyMemory(&Info.Raw[4], &Timer->LastOverrun, sizeof(LONG));
        RtlCopyMemory(&Info.Raw[8], &Timer->Value, sizeof(ULONG64));
        if (Timer->Notify & LX_SIGEV_THREAD_ID)
            Thread = LxThreadLookupReferenced(Process->Instance, Timer->Tid, &Owner);
        if ((Thread != NULL) && (Owner == Process))
            LxSignalSend(Process, Thread, Timer->Signal, &Info);
        else
            LxSignalSend(Process, NULL, Timer->Signal, &Info);
        if (Owner != NULL)
            LxProcessDereference(Owner);
    }
    LxProcessDereference(Process);
}

static
VOID
NTAPI
LxPosixTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PLX_POSIX_TIMER Timer = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    if (Timer->IntervalNs != 0)
        Timer->DueNs += Timer->IntervalNs;
    else
        Timer->DueNs = 0;
    if (InterlockedExchange(&Timer->WorkQueued, 1) == 0)
    {
        InterlockedIncrement(&Timer->Process->RefCount);
        ExQueueWorkItem(&Timer->Work, DelayedWorkQueue);
    }
    else
    {
        InterlockedIncrement(&Timer->Overrun);
    }
}

VOID
LxPosixTimersRelease(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Free)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Timers.Flink; Entry != &Process->Timers; Entry = Entry->Flink)
        KeCancelTimer(&CONTAINING_RECORD(Entry, LX_POSIX_TIMER, Link)->Timer);
    if (!Free)
        return;
    KeFlushQueuedDpcs();
    while (!IsListEmpty(&Process->Timers))
    {
        PLX_POSIX_TIMER Timer = CONTAINING_RECORD(RemoveHeadList(&Process->Timers), LX_POSIX_TIMER, Link);

        while (Timer->WorkQueued)
        {
            LARGE_INTEGER Delay;

            Delay.QuadPart = -10000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }
        LxFree(Timer);
    }
}

static
PLX_POSIX_TIMER
LxPosixTimerFind(
    _In_ PLX_PROCESS Process,
    _In_ LONG Id)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Timers.Flink; Entry != &Process->Timers; Entry = Entry->Flink)
    {
        PLX_POSIX_TIMER Timer = CONTAINING_RECORD(Entry, LX_POSIX_TIMER, Link);

        if (Timer->Id == Id)
            return Timer;
    }
    return NULL;
}

LX_SYSCALL(LxSysTimerCreate)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Clock = (ULONG)Call->Args[0];
    PLX_POSIX_TIMER Timer;
    LX_SIGEVENT Event;
    LX_TIMESPEC Probe;
    LONG64 Result;

    if (LxQueryClock((Clock == 8) ? LX_CLOCK_REALTIME : ((Clock == 9) ? LX_CLOCK_BOOTTIME : Clock), &Probe) != 0)
        return -LX_EINVAL;
    RtlZeroMemory(&Event, sizeof(Event));
    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(&Event, Call->Args[1], sizeof(Event));
        if (Result != 0)
            return Result;
        if ((Event.Notify != LX_SIGEV_SIGNAL) && (Event.Notify != LX_SIGEV_NONE) &&
            (Event.Notify != (LX_SIGEV_SIGNAL | LX_SIGEV_THREAD_ID)))
        {
            return -LX_EINVAL;
        }
        if ((Event.Notify != LX_SIGEV_NONE) && ((Event.Signal <= 0) || (Event.Signal > LX_NSIG)))
            return -LX_EINVAL;
    }
    else
    {
        Event.Notify = LX_SIGEV_SIGNAL;
        Event.Signal = LX_SIGALRM;
    }

    Timer = LxAlloc(sizeof(*Timer));
    if (Timer == NULL)
        return -LX_ENOMEM;
    KeInitializeTimerEx(&Timer->Timer, NotificationTimer);
    KeInitializeDpc(&Timer->Dpc, LxPosixTimerDpc, Timer);
    ExInitializeWorkItem(&Timer->Work, LxPosixTimerWork, Timer);
    Timer->Process = Process;
    Timer->Clock = (Clock == 8) ? LX_CLOCK_REALTIME : ((Clock == 9) ? LX_CLOCK_BOOTTIME : Clock);
    Timer->Signal = Event.Signal;
    Timer->Notify = (ULONG)Event.Notify;
    Timer->Tid = LxNrToGlobal(Process, (ULONG)Event.ThreadId);
    Timer->Value = (Call->Args[1] != 0) ? Event.Value : 0;

    ExAcquireFastMutex(&Process->Lock);
    Timer->Id = Process->NextTimerId++;
    if (Call->Args[1] == 0)
        Timer->Value = (ULONG64)Timer->Id;
    InsertTailList(&Process->Timers, &Timer->Link);
    ExReleaseFastMutex(&Process->Lock);

    Result = LxCopyToUser(Call->Args[2], &Timer->Id, sizeof(LONG));
    return Result;
}

static
VOID
LxPosixTimerCurrent(
    _In_ PLX_POSIX_TIMER Timer,
    _Out_ PLX_ITIMERSPEC Value)
{
    LONG64 Remaining = 0;

    if (Timer->DueNs != 0)
    {
        Remaining = Timer->DueNs - LxClockNow(Timer->Clock);
        if (Remaining <= 0)
            Remaining = 1;
    }
    Value->Value.Seconds = Remaining / 1000000000LL;
    Value->Value.Nanoseconds = Remaining % 1000000000LL;
    Value->Interval.Seconds = Timer->IntervalNs / 1000000000LL;
    Value->Interval.Nanoseconds = Timer->IntervalNs % 1000000000LL;
}

LX_SYSCALL(LxSysTimerSettime)
{
    PLX_PROCESS Process = Call->Process;
    PLX_POSIX_TIMER Timer;
    LX_ITIMERSPEC New, Old;
    LARGE_INTEGER DueTime;
    LONG64 Result, Due, Interval, Now;

    Result = LxCopyFromUser(&New, Call->Args[2], sizeof(New));
    if (Result != 0)
        return Result;
    if ((New.Value.Nanoseconds < 0) || (New.Value.Nanoseconds >= 1000000000) || (New.Value.Seconds < 0) ||
        (New.Interval.Nanoseconds < 0) || (New.Interval.Nanoseconds >= 1000000000) || (New.Interval.Seconds < 0))
    {
        return -LX_EINVAL;
    }

    ExAcquireFastMutex(&Process->Lock);
    Timer = LxPosixTimerFind(Process, (LONG)Call->Args[0]);
    ExReleaseFastMutex(&Process->Lock);
    if (Timer == NULL)
        return -LX_EINVAL;

    LxPosixTimerCurrent(Timer, &Old);
    KeCancelTimer(&Timer->Timer);
    Due = LxTimespecToNs(&New.Value);
    Interval = LxTimespecToNs(&New.Interval);
    Now = LxClockNow(Timer->Clock);
    Timer->IntervalNs = (Due != 0) ? Interval : 0;
    if (Due == 0)
        Timer->DueNs = 0;
    else if (Call->Args[1] & LX_TFD_TIMER_ABSTIME)
        Timer->DueNs = max(Due - LxTimeNsOffset(Process, Timer->Clock), 1);
    else
        Timer->DueNs = (Due >= MAXLONG64 - Now) ? MAXLONG64 : Now + Due;
    if ((Timer->DueNs != 0) && (Timer->DueNs != MAXLONG64))
    {
        LONG64 Relative = Timer->DueNs - Now;

        if (Relative < 100)
            Relative = 100;
        DueTime.QuadPart = -((Relative + 99) / 100);
        KeSetTimerEx(&Timer->Timer, DueTime, (Interval != 0) ? (LONG)max(1, min(Interval / 1000000LL, MAXLONG)) : 0,
                     &Timer->Dpc);
    }

    if (Call->Args[3] != 0)
        return LxCopyToUser(Call->Args[3], &Old, sizeof(Old));
    return 0;
}

LX_SYSCALL(LxSysTimerGettime)
{
    PLX_PROCESS Process = Call->Process;
    PLX_POSIX_TIMER Timer;
    LX_ITIMERSPEC Value;

    ExAcquireFastMutex(&Process->Lock);
    Timer = LxPosixTimerFind(Process, (LONG)Call->Args[0]);
    ExReleaseFastMutex(&Process->Lock);
    if (Timer == NULL)
        return -LX_EINVAL;
    LxPosixTimerCurrent(Timer, &Value);
    return LxCopyToUser(Call->Args[1], &Value, sizeof(Value));
}

LX_SYSCALL(LxSysTimerGetoverrun)
{
    PLX_PROCESS Process = Call->Process;
    PLX_POSIX_TIMER Timer;

    ExAcquireFastMutex(&Process->Lock);
    Timer = LxPosixTimerFind(Process, (LONG)Call->Args[0]);
    ExReleaseFastMutex(&Process->Lock);
    return (Timer != NULL) ? Timer->LastOverrun : -LX_EINVAL;
}

LX_SYSCALL(LxSysTimerDelete)
{
    PLX_PROCESS Process = Call->Process;
    PLX_POSIX_TIMER Timer;

    ExAcquireFastMutex(&Process->Lock);
    Timer = LxPosixTimerFind(Process, (LONG)Call->Args[0]);
    if (Timer != NULL)
        RemoveEntryList(&Timer->Link);
    ExReleaseFastMutex(&Process->Lock);
    if (Timer == NULL)
        return -LX_EINVAL;
    KeCancelTimer(&Timer->Timer);
    KeFlushQueuedDpcs();
    while (Timer->WorkQueued)
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    LxFree(Timer);
    return 0;
}
