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
        (Handler == LxSysXattrGet) || (Handler == LxSysXattrSet) || (Handler == LxSysXattrList))
    {
        return 0;
    }
    return -1;
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
    ULONG Trace = Call->Process->Instance->Trace;

    if ((Entry != NULL) && (Entry->Handler != NULL))
    {
        LONG64 Result;

        Call->Thread->CurrentSyscall = Call->Number;
        RtlCopyMemory(Call->Thread->CurrentArgs, Call->Args, sizeof(Call->Args));
        Result = Entry->Handler(Call);
        Call->Thread->CurrentSyscall = MAXULONG;

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
            LxQueryRealtime(Timespec);
            return 0;

        case LX_CLOCK_MONOTONIC:
        case LX_CLOCK_MONOTONIC_RAW:
        case LX_CLOCK_MONOTONIC_COARSE:
        case LX_CLOCK_BOOTTIME:
            LxQueryMonotonic(Timespec);
            return 0;

        case LX_CLOCK_PROCESS_CPUTIME_ID:
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

LX_SYSCALL(LxSysClockGettime)
{
    LX_TIMESPEC Timespec;
    LONG Error = LxQueryClock((ULONG)Call->Args[0], &Timespec);

    if (Error != 0)
        return Error;
    return LxCopyToUser(Call->Args[1], &Timespec, sizeof(Timespec));
}

LX_SYSCALL(LxSysClockGetres)
{
    LX_TIMESPEC Resolution = { 0, 1 };

    if (Call->Args[0] > LX_CLOCK_BOOTTIME)
        return -LX_EINVAL;
    if ((Call->Args[0] == LX_CLOCK_REALTIME) || (Call->Args[0] == LX_CLOCK_REALTIME_COARSE))
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
        LONG Error = LxQueryClock((ULONG)Call->Args[0], &Now);

        if (Error != 0)
            return Error;
        Interval -= Now.Seconds * 10000000 + Now.Nanoseconds / 100;
    }

    return LxSleep(Interval);
}

LX_SYSCALL(LxSysTimes)
{
    LARGE_INTEGER Now;
    LONG64 Times[4] = { 0, 0, 0, 0 };
    LONG64 Result = 0;

    if (Call->Args[0] != 0)
        Result = LxCopyToUser(Call->Args[0], Times, sizeof(Times));
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

LX_SYSCALL(LxSysSchedGetparam)
{
    LONG Priority = 0;

    return LxCopyToUser(Call->Args[1], &Priority, sizeof(Priority));
}

LX_SYSCALL(LxSysSchedGetscheduler)
{
    UNREFERENCED_PARAMETER(Call);
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
    LX_UTSNAME Name;

    RtlZeroMemory(&Name, sizeof(Name));
    RtlCopyMemory(Name.SysName, "Linux", sizeof("Linux"));
    ExAcquireFastMutex(&Instance->Lock);
    RtlCopyMemory(Name.NodeName, Instance->Hostname, sizeof(Instance->Hostname));
    RtlCopyMemory(Name.DomainName, Instance->DomainName, sizeof(Instance->DomainName));
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
    CHAR Name[65];
    LONG64 Result;

    if (Call->Args[1] > 64)
        return -LX_EINVAL;
    if (Call->Process->Uid[1] != 0)
        return -LX_EPERM;
    Result = LxCopyFromUser(Name, Call->Args[0], (SIZE_T)Call->Args[1]);
    if (Result != 0)
        return Result;
    Name[Call->Args[1]] = '\0';
    ExAcquireFastMutex(&Instance->Lock);
    RtlCopyMemory(Domain ? Instance->DomainName : Instance->Hostname, Name, (SIZE_T)Call->Args[1] + 1);
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
    Now.QuadPart = (LONG64)KeQueryInterruptTime();
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

    if ((Call->Args[0] != 0) && ((ULONG)Call->Args[0] != Call->Process->Pid))
    {
        Target = LxProcessLookup(Call->Process->Instance, (ULONG)Call->Args[0]);
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

LX_SYSCALL(LxSysGetrusage)
{
    UCHAR Usage[144];

    RtlZeroMemory(Usage, sizeof(Usage));
    return LxCopyToUser(Call->Args[1], Usage, sizeof(Usage));
}

LX_SYSCALL(LxSysGetpid)
{
    return Call->Process->Pid;
}

LX_SYSCALL(LxSysGetppid)
{
    return Call->Process->ParentPid;
}

LX_SYSCALL(LxSysGettid)
{
    return Call->Thread->Tid;
}

LX_SYSCALL(LxSysGetuid)
{
    PLX_PROCESS Process = Call->Process;

    switch (Call->Number)
    {
        case 174: return Process->Uid[0];
        case 175: return Process->Uid[1];
        case 176: return Process->Gid[0];
        default: return Process->Gid[1];
    }
}

LX_SYSCALL(LxSysGetresuid)
{
    PULONG Ids = (Call->Number == 148) ? Call->Process->Uid : Call->Process->Gid;
    LONG64 Result = LxCopyToUser(Call->Args[0], &Ids[0], sizeof(ULONG));

    if (Result == 0)
        Result = LxCopyToUser(Call->Args[1], &Ids[1], sizeof(ULONG));
    if (Result == 0)
        Result = LxCopyToUser(Call->Args[2], &Ids[2], sizeof(ULONG));
    return Result;
}

LX_SYSCALL(LxSysSetid)
{
    PLX_PROCESS Process = Call->Process;
    ULONG A = (ULONG)Call->Args[0], B = (ULONG)Call->Args[1], C = (ULONG)Call->Args[2];
    BOOLEAN Privileged = (Process->Uid[1] == 0);
    LONG64 Result = 0;

    ExAcquireFastMutex(&Process->Lock);
    switch (Call->Number)
    {
        case 146:
            if (Privileged)
                Process->Uid[0] = Process->Uid[1] = Process->Uid[2] = Process->Fsuid = A;
            else if ((A == Process->Uid[0]) || (A == Process->Uid[2]))
                Process->Uid[1] = Process->Fsuid = A;
            else
                Result = -LX_EPERM;
            break;
        case 144:
            if (Privileged)
                Process->Gid[0] = Process->Gid[1] = Process->Gid[2] = Process->Fsgid = A;
            else if ((A == Process->Gid[0]) || (A == Process->Gid[2]))
                Process->Gid[1] = Process->Fsgid = A;
            else
                Result = -LX_EPERM;
            break;
        case 145:
            if (A != MAXULONG)
                Process->Uid[0] = A;
            if (B != MAXULONG)
                Process->Uid[1] = Process->Fsuid = B;
            break;
        case 143:
            if (A != MAXULONG)
                Process->Gid[0] = A;
            if (B != MAXULONG)
                Process->Gid[1] = Process->Fsgid = B;
            break;
        case 147:
            if (A != MAXULONG)
                Process->Uid[0] = A;
            if (B != MAXULONG)
                Process->Uid[1] = Process->Fsuid = B;
            if (C != MAXULONG)
                Process->Uid[2] = C;
            break;
        case 149:
            if (A != MAXULONG)
                Process->Gid[0] = A;
            if (B != MAXULONG)
                Process->Gid[1] = Process->Fsgid = B;
            if (C != MAXULONG)
                Process->Gid[2] = C;
            break;
        case 151:
            Result = Process->Fsuid;
            if (A != MAXULONG)
                Process->Fsuid = A;
            break;
        case 152:
            Result = Process->Fsgid;
            if (A != MAXULONG)
                Process->Fsgid = A;
            break;
        default:
            Result = -LX_ENOSYS;
            break;
    }
    if ((Result == 0) && (Process->Uid[1] != 0) && Privileged && !Process->KeepCaps && (Call->Number != 151))
        Process->CapAmbient = 0;
    ExReleaseFastMutex(&Process->Lock);
    return Result;
}

LX_SYSCALL(LxSysGetgroups)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Size = (ULONG)Call->Args[0];
    ULONG Groups[32];
    ULONG Count;

    ExAcquireFastMutex(&Process->Lock);
    Count = Process->GroupCount;
    RtlCopyMemory(Groups, Process->Groups, Count * sizeof(ULONG));
    ExReleaseFastMutex(&Process->Lock);

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

    if (Size > RTL_NUMBER_OF(Groups))
        return -LX_EINVAL;
    if (Process->Uid[1] != 0)
        return -LX_EPERM;
    if (Size != 0)
        Result = LxCopyFromUser(Groups, Call->Args[1], Size * sizeof(ULONG));
    if (Result != 0)
        return Result;
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

    if ((Pid == 0) || (Pid == Call->Process->Pid))
        return Session ? Call->Process->Sid : Call->Process->Pgid;

    Target = LxProcessLookup(Call->Process->Instance, Pid);
    if (Target == NULL)
        return -LX_ESRCH;
    Result = Session ? Target->Sid : Target->Pgid;
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
    ULONG Pid = Call->Args[0] ? (ULONG)Call->Args[0] : Process->Pid;
    LONG Pgid = (LONG)Call->Args[1];
    LONG64 Result = 0;

    if (Pgid < 0)
        return -LX_EINVAL;
    if (Pgid == 0)
        Pgid = (LONG)Pid;

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
    return Process->Pid;
}

LX_SYSCALL(LxSysSucceed)
{
    UNREFERENCED_PARAMETER(Call);
    return 0;
}

LX_SYSCALL(LxSysSetTidAddress)
{
    Call->Thread->ClearChildTid = Call->Args[0];
    return Call->Thread->Tid;
}

LX_SYSCALL(LxSysFutex)
{
    ULONG Operation = (ULONG)Call->Args[1] & ~(LX_FUTEX_PRIVATE | LX_FUTEX_CLOCK_REALTIME);
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER TimeoutPointer = NULL;
    LX_TIMESPEC Timespec;
    LONG64 Result;

    switch (Operation)
    {
        case LX_FUTEX_WAIT:
        case LX_FUTEX_WAIT_BITSET:
            if (Call->Args[3] != 0)
            {
                Result = LxCopyFromUser(&Timespec, Call->Args[3], sizeof(Timespec));
                if (Result != 0)
                    return Result;
                if (Operation == LX_FUTEX_WAIT_BITSET)
                {
                    LX_TIMESPEC Now;

                    LxQueryClock((Call->Args[1] & LX_FUTEX_CLOCK_REALTIME) ? LX_CLOCK_REALTIME : LX_CLOCK_MONOTONIC, &Now);
                    Timeout.QuadPart = (Timespec.Seconds - Now.Seconds) * 10000000 +
                                       (Timespec.Nanoseconds - Now.Nanoseconds) / 100;
                    if (Timeout.QuadPart <= 0)
                        return -LX_ETIMEDOUT;
                    Timeout.QuadPart = -Timeout.QuadPart;
                }
                else
                {
                    Timeout.QuadPart = -(Timespec.Seconds * 10000000 + Timespec.Nanoseconds / 100);
                    if (Timeout.QuadPart == 0)
                        Timeout.QuadPart = -1;
                }
                TimeoutPointer = &Timeout;
            }
            return LxFutexWait(Call->Args[0], (ULONG)Call->Args[2], TimeoutPointer);

        case LX_FUTEX_WAKE:
        case LX_FUTEX_WAKE_BITSET:
            return LxFutexWake(Call->Args[0], (LONG)Call->Args[2]);

        case 3:
        case 4:
            Result = LxFutexWake(Call->Args[0], (LONG)Call->Args[2]);
            return Result + LxFutexWake(Call->Args[4], MAXLONG);

        default:
            return -LX_ENOSYS;
    }
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

    if ((Signal < 0) || (Signal > LX_NSIG))
        return -LX_EINVAL;

    if (Pid == 0)
        return LxSignalGroup(Instance, Call->Process->Pgid, Signal) ? 0 : -LX_ESRCH;
    if (Pid == -1)
        return LxSignalGroup(Instance, 0, Signal) ? 0 : -LX_ESRCH;
    if (Pid < -1)
        return LxSignalGroup(Instance, (ULONG)-Pid, Signal) ? 0 : -LX_ESRCH;

    Target = LxProcessLookup(Instance, (ULONG)Pid);
    if (Target == NULL)
        return -LX_ESRCH;

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

    Thread = LxThreadLookupReferenced(Call->Process->Instance, Tid, &Owner);
    if (Thread == NULL)
    {
        Owner = LxProcessLookup(Call->Process->Instance, Tid);
        if ((Owner == NULL) || Owner->Exited)
        {
            if (Owner != NULL)
                LxProcessDereference(Owner);
            return -LX_ESRCH;
        }
    }
    if ((Tgid > 0) && (Owner->Pid != (ULONG)Tgid))
    {
        LxProcessDereference(Owner);
        return -LX_ESRCH;
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
        Result = LxCopyFromUser(&New, Call->Args[1], sizeof(New));
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
        return LxCopyToUser(Call->Args[2], &Old, sizeof(Old));
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

    if ((Request->Flags & (LX_CLONE_VM | LX_CLONE_SIGHAND)) != (LX_CLONE_VM | LX_CLONE_SIGHAND))
        return -LX_EINVAL;

    Thread = LxThreadCreate(Call->Process);
    if (Thread == NULL)
        return -LX_ENOMEM;

    Tid = LxInstanceAllocatePid(Call->Process->Instance);
    Thread->Tid = Tid;
    Thread->Tls = (Request->Flags & LX_CLONE_SETTLS) ? Request->Tls : LxArchGetThreadPointer();
    Thread->SigMask = Call->Thread->SigMask;
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
    ULONG Pid;

    if (Request->Flags & (LX_CLONE_NEWNS | LX_CLONE_NEWUTS | LX_CLONE_NEWIPC | LX_CLONE_NEWUSER |
                          LX_CLONE_NEWPID | LX_CLONE_NEWNET | LX_CLONE_NEWCGROUP | LX_CLONE_NEWTIME))
    {
        return -LX_EPERM;
    }

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
    Child->Pid = LxInstanceAllocatePid(Parent->Instance);
    Pid = Child->Pid;
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
    Thread->Tid = Pid;
    Thread->Tls = (Request->Flags & LX_CLONE_SETTLS) ? Request->Tls : LxArchGetThreadPointer();
    Thread->SigMask = Call->Thread->SigMask;
    Thread->AltStack = Call->Thread->AltStack;
    RtlCopyMemory(Thread->Comm, Call->Thread->Comm, sizeof(Thread->Comm));
    if (Request->Flags & LX_CLONE_CHILD_CLEARTID)
        Thread->ClearChildTid = Request->ChildTid;
    if (Request->Flags & LX_CLONE_CHILD_SETTID)
        Thread->SetChildTid = Request->ChildTid;

    Status = PsPicoCreateProcess(TRUE, Child, &ProcessHandle);
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
        LxCopyToUser(Request->ParentTid, &Pid, sizeof(Pid));

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

    return Pid;
}

static
LONG64
LxCloneCommon(
    _In_ PLX_CALL Call,
    _In_ PLX_CLONE_REQUEST Request)
{
    CONTEXT Context;

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

    if ((Size < 64) || (Size > 4096))
        return -LX_EINVAL;
    RtlZeroMemory(&Args, sizeof(Args));
    Result = LxCopyFromUser(&Args, Call->Args[0], min(Size, sizeof(Args)));
    if (Result != 0)
        return Result;
    if ((Args.SetTid != 0) || (Args.SetTidSize != 0) || (Args.ExitSignal > LX_NSIG))
        return -LX_EINVAL;
    if ((Args.Flags & LX_CLONE_INTO_CGROUP) && (Size < 88))
        return -LX_EINVAL;
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

    if (Request.Flags & LX_CLONE_CLEAR_SIGHAND)
    {
        PLX_PROCESS Process = Call->Process;

        if (Request.Flags & LX_CLONE_SIGHAND)
            return -LX_EINVAL;
        Request.Flags &= ~LX_CLONE_CLEAR_SIGHAND;
        Result = LxCloneCommon(Call, &Request);
        if (Result > 0)
        {
            PLX_PROCESS Child = LxProcessLookup(Process->Instance, (ULONG)Result);

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
    _In_ ULONG64 Argv,
    _In_ ULONG64 Envp)
{
    LX_EXEC_REQUEST Request;
    NTSTATUS Status;
    LONG64 Result;
    LONG Errno = 0;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.FileName = FileName;
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
    return LxExecCommon(Call, FileName, Call->Args[1], Call->Args[2]);
}

LX_SYSCALL(LxSysExecveat)
{
    LONG DirFd = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[4];
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
        if ((File->Path == NULL) || (File->Path[0] != '/'))
            FileName = NULL;
        else if (Path[0] == '\0')
            FileName = (Flags & LX_AT_EMPTY_PATH) ? LxStrDup(File->Path) : NULL;
        else
            FileName = LxStrPrintf("%s/%s", File->Path, Path);
        LxFileDereference(File);
        LxFree(Path);
        if (FileName == NULL)
            return -LX_ENOENT;
    }
    return LxExecCommon(Call, FileName, Call->Args[2], Call->Args[3]);
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
    _Out_ PULONG Uid)
{
    PLX_PROCESS Process = Selector->Process;

    *FoundPid = 0;
    *WaitStatus = 0;
    *Uid = 0;

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
            *FoundPid = Found->Lx->Pid;
            *WaitStatus = Found->Lx->WaitStatus;
            *Uid = Found->Lx->Uid[0];
            if (!(Selector->Options & LX_WNOWAIT))
            {
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
        if (Result == -LX_EINTR)
            return Result;
    }
}

LX_SYSCALL(LxSysWait4)
{
    LX_WAIT_SELECTOR Selector;
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
        Selector.Id = (ULONG)Pid;
    }
    else if (Pid == 0)
    {
        Selector.Type = LX_P_PGID;
        Selector.Id = Call->Process->Pgid;
    }
    else if (Pid < -1)
    {
        Selector.Type = LX_P_PGID;
        Selector.Id = (ULONG)-Pid;
    }
    else
    {
        Selector.Type = LX_P_ALL;
    }

    Result = LxWaitChild(&Selector, &FoundPid, &Status, &Uid);
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
        LxZeroUser(Call->Args[3], 144);
    return FoundPid;
}

LX_SYSCALL(LxSysWaitid)
{
    LX_WAIT_SELECTOR Selector;
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
            break;
        case LX_P_PGID:
            if (Selector.Id == 0)
                Selector.Id = Call->Process->Pgid;
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

    Result = LxWaitChild(&Selector, &FoundPid, &Status, &Uid);
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
        Info.Child.Uid = Uid;
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
        LxZeroUser(Call->Args[4], 144);
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
            return Process->KeepCaps;

        case LX_PR_SET_KEEPCAPS:
            if (Arg > 1)
                return -LX_EINVAL;
            Process->KeepCaps = (BOOLEAN)Arg;
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
        case LX_PR_SET_MM:
            return -LX_EINVAL;

        case LX_PR_CAPBSET_READ:
            if (Arg > 40)
                return -LX_EINVAL;
            return (Process->CapBounding >> Arg) & 1;

        case LX_PR_CAPBSET_DROP:
            if (Arg > 40)
                return -LX_EINVAL;
            Process->CapBounding &= ~(1ULL << Arg);
            return 0;

        case LX_PR_CAP_AMBIENT:
            if (Arg == 4)
            {
                Process->CapAmbient = 0;
                return 0;
            }
            if (Call->Args[2] > 40)
                return -LX_EINVAL;
            if (Arg == 1)
                return (Process->CapAmbient >> Call->Args[2]) & 1;
            if (Arg == 2)
            {
                Process->CapAmbient |= 1ULL << Call->Args[2];
                return 0;
            }
            if (Arg == 3)
            {
                Process->CapAmbient &= ~(1ULL << Call->Args[2]);
                return 0;
            }
            return -LX_EINVAL;

        case LX_PR_GET_SECUREBITS:
            return 0;

        case LX_PR_SET_SECUREBITS:
            return 0;

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
    ULONG64 Effective;
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

    if ((Header.Pid != 0) && ((ULONG)Header.Pid != Call->Process->Pid))
    {
        Target = LxProcessLookup(Call->Process->Instance, (ULONG)Header.Pid);
        if (Target == NULL)
            return -LX_ESRCH;
    }
    else
    {
        LxProcessReference(Target);
    }

    Effective = (Target->Uid[1] == 0) ? Target->CapBounding : Target->CapAmbient;
    RtlZeroMemory(Data, sizeof(Data));
    Data[0] = (ULONG)Effective;
    Data[1] = (ULONG)Effective;
    Data[3] = (ULONG)(Effective >> 32);
    Data[4] = (ULONG)(Effective >> 32);
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
    LONG64 Result = LxCopyFromUser(&Header, Call->Args[0], sizeof(Header));

    if (Result != 0)
        return Result;
    if ((Header.Version != 0x19980330) && (Header.Version != 0x20071026) && (Header.Version != 0x20080522))
        return -LX_EINVAL;
    if ((Header.Pid != 0) && ((ULONG)Header.Pid != Call->Process->Pid))
        return -LX_EPERM;
    return 0;
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
    if (Process->Uid[1] != 0)
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

    DPRINT1("Linux instance init requested %s\n", (Signal == LX_SIGHUP) ? "a restart" : "a power off");
    Instance->RebootCommand = (LONG)Command;
    Instance->ShuttingDown = TRUE;
    LxSignalGroup(Instance, 0, LX_SIGKILL);
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
    UNREFERENCED_PARAMETER(Call);
    return 20;
}

LX_SYSCALL(LxSysUnshare)
{
    ULONG64 Flags = Call->Args[0];

    if (Flags & (LX_CLONE_NEWNS | LX_CLONE_NEWUTS | LX_CLONE_NEWIPC | LX_CLONE_NEWUSER | LX_CLONE_NEWPID |
                 LX_CLONE_NEWNET | LX_CLONE_NEWCGROUP | LX_CLONE_NEWTIME))
    {
        return -LX_EPERM;
    }
    return 0;
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
    ULONG64 Pages = (Call->Args[1] + LX_PAGE_SIZE - 1) / LX_PAGE_SIZE, Index;
    UCHAR Ones[64];
    LONG64 Result = 0;

    if (Call->Args[0] & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    RtlFillMemory(Ones, sizeof(Ones), 1);
    for (Index = 0; (Index < Pages) && (Result == 0); Index += sizeof(Ones))
        Result = LxCopyToUser(Call->Args[2] + Index, Ones, (SIZE_T)min(Pages - Index, sizeof(Ones)));
    return Result;
}
