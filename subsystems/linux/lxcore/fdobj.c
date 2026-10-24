/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     eventfd, timerfd, signalfd, inotify, pidfd and memfd objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define LX_EVENTFD_MAX      0xFFFFFFFFFFFFFFFEULL
#define LX_INOTIFY_MAX_QUEUE (256 * 1024)
#define LX_MFD_CLOEXEC      0x0001
#define LX_MFD_ALLOW_SEALING 0x0002
#define LX_MFD_HUGETLB      0x0004
#define LX_MFD_NOEXEC_SEAL  0x0008
#define LX_MFD_EXEC         0x0010

typedef struct _LX_EVENTFD
{
    KSPIN_LOCK Lock;
    ULONG64 Count;
    BOOLEAN Semaphore;
    LX_POLL_HEAD Poll;
} LX_EVENTFD, *PLX_EVENTFD;

typedef struct _LX_TIMERFD
{
    KTIMER Timer;
    KDPC Dpc;
    KSPIN_LOCK Lock;
    ULONG Clock;
    ULONG64 Expirations;
    LONG64 IntervalNs;
    LONG64 DueNs;
    LX_POLL_HEAD Poll;
} LX_TIMERFD, *PLX_TIMERFD;

typedef struct _LX_SIGNALFD
{
    ULONG64 Mask;
    PLX_PROCESS Process;
} LX_SIGNALFD, *PLX_SIGNALFD;

typedef struct _LX_INOTIFY_WATCH
{
    LIST_ENTRY Link;
    LONG Wd;
    ULONG Mask;
    PSTR Path;
} LX_INOTIFY_WATCH, *PLX_INOTIFY_WATCH;

typedef struct _LX_INOTIFY_ITEM
{
    LIST_ENTRY Link;
    ULONG Length;
    UCHAR Data[1];
} LX_INOTIFY_ITEM, *PLX_INOTIFY_ITEM;

typedef struct _LX_INOTIFY
{
    LIST_ENTRY GlobalLink;
    PLX_INSTANCE Instance;
    KSPIN_LOCK Lock;
    LIST_ENTRY Watches;
    LIST_ENTRY Events;
    ULONG QueuedBytes;
    LONG NextWd;
    BOOLEAN Overflowed;
    LX_POLL_HEAD Poll;
} LX_INOTIFY, *PLX_INOTIFY;

static LIST_ENTRY LxInotifyList = { &LxInotifyList, &LxInotifyList };
static FAST_MUTEX LxInotifyLock;
static volatile LONG LxInotifyWatchCount;
static volatile LONG LxMemfdCounter;

static
LONG
LxInstallNewFile(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ BOOLEAN CloseOnExec)
{
    LONG Fd = LxFdInstall(Process, File, 0, CloseOnExec);

    LxFileDereference(File);
    return Fd;
}

static
LONG
LxAnonStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = 0600;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->Dev = 0xD;
    Stat->BlkSize = 4096;
    return 0;
}

static
LONG64
LxEventfdRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_EVENTFD Event = File->Object;
    ULONG64 Value;
    LONG64 Result;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Offset);
    if (Length < sizeof(ULONG64))
        return -LX_EINVAL;

    for (;;)
    {
        KeAcquireSpinLock(&Event->Lock, &Irql);
        Value = Event->Count;
        if (Value != 0)
        {
            if (Event->Semaphore)
                Value = 1;
            Event->Count -= Value;
        }
        KeReleaseSpinLock(&Event->Lock, Irql);

        if (Value != 0)
        {
            LxPollWake(&Event->Poll, LX_POLLOUT | LX_POLLWRNORM);
            Result = LxCopyToUser(Buffer, &Value, sizeof(Value));
            return (Result != 0) ? Result : (LONG64)sizeof(Value);
        }
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLIN, NULL);
        if (Result < 0)
            return Result;
    }
}

static
LONG64
LxEventfdWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_EVENTFD Event = File->Object;
    ULONG64 Value;
    LONG64 Result;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Offset);
    if (Length < sizeof(ULONG64))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Value, Buffer, sizeof(Value));
    if (Result != 0)
        return Result;
    if (Value == MAXULONG64)
        return -LX_EINVAL;

    for (;;)
    {
        BOOLEAN Done = FALSE;

        KeAcquireSpinLock(&Event->Lock, &Irql);
        if (LX_EVENTFD_MAX - Event->Count >= Value)
        {
            Event->Count += Value;
            Done = TRUE;
        }
        KeReleaseSpinLock(&Event->Lock, Irql);

        if (Done)
        {
            if (Value != 0)
                LxPollWake(&Event->Poll, LX_POLLIN | LX_POLLRDNORM);
            return sizeof(Value);
        }
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLOUT, NULL);
        if (Result < 0)
            return Result;
    }
}

static
ULONG
LxEventfdPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_EVENTFD Event = File->Object;
    ULONG Mask = 0;
    KIRQL Irql;

    if (Entry != NULL)
        LxPollAdd(&Event->Poll, Entry);
    KeAcquireSpinLock(&Event->Lock, &Irql);
    if (Event->Count != 0)
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Event->Count < LX_EVENTFD_MAX)
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    KeReleaseSpinLock(&Event->Lock, Irql);
    return Mask;
}

static
VOID
LxObjectRelease(
    _In_ PLX_FILE File)
{
    LxFree(File->Object);
    File->Object = NULL;
}

static const LX_FILE_OPS LxEventfdOps =
{
    LxEventfdRead, LxEventfdWrite, NULL, LxEventfdPoll, LxAnonStat, NULL, NULL, NULL, LxObjectRelease
};

static const LX_FILE_OPS LxNullOps =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static
PLX_FILE
LxAnonFileCreate(
    _In_ const LX_FILE_OPS *Ops,
    _In_ ULONG Flags,
    _In_ PCSTR Name,
    _In_ SIZE_T ObjectSize)
{
    PSTR Path = LxStrPrintf("anon_inode:%s", Name);
    PVOID Object = (ObjectSize != 0) ? LxAlloc(ObjectSize) : NULL;
    PLX_FILE File = LxFileAlloc(Ops, Flags, 0);

    if ((Path == NULL) || ((ObjectSize != 0) && (Object == NULL)) || (File == NULL))
    {
        LxFree(Path);
        LxFree(Object);
        if (File != NULL)
        {
            File->Ops = &LxNullOps;
            LxFileDereference(File);
        }
        return NULL;
    }
    File->Path = Path;
    File->Object = Object;
    return File;
}

LX_SYSCALL(LxSysEventfd2)
{
    ULONG Flags = (ULONG)Call->Args[1];
    PLX_EVENTFD Event;
    PLX_FILE File;

    if (Flags & ~(LX_O_CLOEXEC | LX_O_NONBLOCK | LX_EFD_SEMAPHORE))
        return -LX_EINVAL;

    File = LxAnonFileCreate(&LxEventfdOps, LX_O_RDWR | (Flags & LX_O_NONBLOCK), "[eventfd]", sizeof(LX_EVENTFD));
    if (File == NULL)
        return -LX_ENOMEM;
    Event = File->Object;
    KeInitializeSpinLock(&Event->Lock);
    LxPollHeadInit(&Event->Poll);
    Event->Count = (ULONG)Call->Args[0];
    Event->Semaphore = (Flags & LX_EFD_SEMAPHORE) != 0;
    return LxInstallNewFile(Call->Process, File, (Flags & LX_O_CLOEXEC) != 0);
}

static
LONG64
LxClockNs(
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
LxTimerfdDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PLX_TIMERFD Timer = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    KeAcquireSpinLockAtDpcLevel(&Timer->Lock);
    Timer->Expirations++;
    if (Timer->IntervalNs != 0)
        Timer->DueNs += Timer->IntervalNs;
    else
        Timer->DueNs = 0;
    KeReleaseSpinLockFromDpcLevel(&Timer->Lock);
    LxPollWake(&Timer->Poll, LX_POLLIN | LX_POLLRDNORM);
}

static
LONG64
LxTimerfdRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_TIMERFD Timer = File->Object;
    ULONG64 Value;
    LONG64 Result;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Offset);
    if (Length < sizeof(ULONG64))
        return -LX_EINVAL;

    for (;;)
    {
        KeAcquireSpinLock(&Timer->Lock, &Irql);
        Value = Timer->Expirations;
        Timer->Expirations = 0;
        KeReleaseSpinLock(&Timer->Lock, Irql);

        if (Value != 0)
        {
            Result = LxCopyToUser(Buffer, &Value, sizeof(Value));
            return (Result != 0) ? Result : (LONG64)sizeof(Value);
        }
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLIN, NULL);
        if (Result < 0)
            return Result;
    }
}

static
ULONG
LxTimerfdPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_TIMERFD Timer = File->Object;
    ULONG Mask = 0;
    KIRQL Irql;

    if (Entry != NULL)
        LxPollAdd(&Timer->Poll, Entry);
    KeAcquireSpinLock(&Timer->Lock, &Irql);
    if (Timer->Expirations != 0)
        Mask = LX_POLLIN | LX_POLLRDNORM;
    KeReleaseSpinLock(&Timer->Lock, Irql);
    return Mask;
}

static
VOID
LxTimerfdRelease(
    _In_ PLX_FILE File)
{
    PLX_TIMERFD Timer = File->Object;

    KeCancelTimer(&Timer->Timer);
    KeFlushQueuedDpcs();
    LxFree(Timer);
    File->Object = NULL;
}

static const LX_FILE_OPS LxTimerfdOps =
{
    LxTimerfdRead, NULL, NULL, LxTimerfdPoll, LxAnonStat, NULL, NULL, NULL, LxTimerfdRelease
};

LX_SYSCALL(LxSysTimerfdCreate)
{
    ULONG Clock = (ULONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[1];
    PLX_TIMERFD Timer;
    PLX_FILE File;

    if (Flags & ~(LX_O_CLOEXEC | LX_O_NONBLOCK))
        return -LX_EINVAL;
    if ((Clock != LX_CLOCK_REALTIME) && (Clock != LX_CLOCK_MONOTONIC) && (Clock != LX_CLOCK_BOOTTIME) &&
        (Clock != 8) && (Clock != 9))
    {
        return -LX_EINVAL;
    }

    File = LxAnonFileCreate(&LxTimerfdOps, LX_O_RDONLY | (Flags & LX_O_NONBLOCK), "[timerfd]", sizeof(LX_TIMERFD));
    if (File == NULL)
        return -LX_ENOMEM;
    Timer = File->Object;
    KeInitializeTimerEx(&Timer->Timer, NotificationTimer);
    KeInitializeDpc(&Timer->Dpc, LxTimerfdDpc, Timer);
    KeInitializeSpinLock(&Timer->Lock);
    LxPollHeadInit(&Timer->Poll);
    Timer->Clock = (Clock == 8) ? LX_CLOCK_REALTIME : ((Clock == 9) ? LX_CLOCK_BOOTTIME : Clock);
    return LxInstallNewFile(Call->Process, File, (Flags & LX_O_CLOEXEC) != 0);
}

static
PLX_TIMERFD
LxTimerfdGet(
    _In_ PLX_CALL Call,
    _Out_ PLX_FILE *File)
{
    *File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (*File == NULL)
        return NULL;
    if ((*File)->Ops != &LxTimerfdOps)
    {
        LxFileDereference(*File);
        *File = NULL;
        return NULL;
    }
    return (*File)->Object;
}

static
VOID
LxTimerfdCurrent(
    _In_ PLX_TIMERFD Timer,
    _Out_ PLX_ITIMERSPEC Value)
{
    LONG64 Remaining = 0, Interval;
    KIRQL Irql;

    KeAcquireSpinLock(&Timer->Lock, &Irql);
    if (Timer->DueNs != 0)
    {
        Remaining = Timer->DueNs - LxClockNs(Timer->Clock);
        if (Remaining <= 0)
            Remaining = 1;
    }
    Interval = Timer->IntervalNs;
    KeReleaseSpinLock(&Timer->Lock, Irql);

    Value->Value.Seconds = Remaining / 1000000000LL;
    Value->Value.Nanoseconds = Remaining % 1000000000LL;
    Value->Interval.Seconds = Interval / 1000000000LL;
    Value->Interval.Nanoseconds = Interval % 1000000000LL;
}

LX_SYSCALL(LxSysTimerfdSettime)
{
    ULONG Flags = (ULONG)Call->Args[1];
    LX_ITIMERSPEC New, Old;
    PLX_TIMERFD Timer;
    PLX_FILE File;
    LONG64 Result, Due, Interval, Now;
    LARGE_INTEGER DueTime;
    KIRQL Irql;

    if (Flags & ~(LX_TFD_TIMER_ABSTIME | LX_TFD_TIMER_CANCEL_ON_SET))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&New, Call->Args[2], sizeof(New));
    if (Result != 0)
        return Result;
    if ((New.Value.Nanoseconds < 0) || (New.Value.Nanoseconds >= 1000000000) || (New.Value.Seconds < 0) ||
        (New.Interval.Nanoseconds < 0) || (New.Interval.Nanoseconds >= 1000000000) || (New.Interval.Seconds < 0))
    {
        return -LX_EINVAL;
    }

    Timer = LxTimerfdGet(Call, &File);
    if (Timer == NULL)
        return (File == NULL) ? -LX_EBADF : -LX_EINVAL;

    LxTimerfdCurrent(Timer, &Old);
    KeCancelTimer(&Timer->Timer);

    Due = LxTimespecToNs(&New.Value);
    Interval = LxTimespecToNs(&New.Interval);
    Now = LxClockNs(Timer->Clock);

    KeAcquireSpinLock(&Timer->Lock, &Irql);
    Timer->Expirations = 0;
    Timer->IntervalNs = (Due != 0) ? Interval : 0;
    if (Due == 0)
        Timer->DueNs = 0;
    else if ((Flags & LX_TFD_TIMER_ABSTIME) || (Due >= MAXLONG64 - Now))
        Timer->DueNs = (Flags & LX_TFD_TIMER_ABSTIME) ? max(Due - LxTimeNsOffset(Call->Process, Timer->Clock), 1) : MAXLONG64;
    else
        Timer->DueNs = Now + Due;
    Due = Timer->DueNs;
    KeReleaseSpinLock(&Timer->Lock, Irql);

    if ((Due != 0) && (Due != MAXLONG64))
    {
        LONG64 Relative = Due - Now;
        LONG Period = 0;

        if (Relative < 100)
            Relative = 100;
        DueTime.QuadPart = -((Relative + 99) / 100);
        if (Interval != 0)
            Period = (LONG)max(1, min(Interval / 1000000LL, MAXLONG));
        KeSetTimerEx(&Timer->Timer, DueTime, Period, &Timer->Dpc);
    }

    LxFileDereference(File);
    if (Call->Args[3] != 0)
        return LxCopyToUser(Call->Args[3], &Old, sizeof(Old));
    return 0;
}

LX_SYSCALL(LxSysTimerfdGettime)
{
    LX_ITIMERSPEC Value;
    PLX_TIMERFD Timer;
    PLX_FILE File;

    Timer = LxTimerfdGet(Call, &File);
    if (Timer == NULL)
        return (File == NULL) ? -LX_EBADF : -LX_EINVAL;
    LxTimerfdCurrent(Timer, &Value);
    LxFileDereference(File);
    return LxCopyToUser(Call->Args[1], &Value, sizeof(Value));
}

typedef struct _LX_SIGNALFD_WAIT
{
    PLX_THREAD Thread;
    ULONG64 Mask;
} LX_SIGNALFD_WAIT, *PLX_SIGNALFD_WAIT;

static
BOOLEAN
LxSignalfdReady(
    _In_ PVOID Context)
{
    PLX_SIGNALFD_WAIT Wait = Context;

    return ((((ULONG64)Wait->Thread->Pending | (ULONG64)Wait->Thread->Process->Pending) & Wait->Mask) != 0) ||
           LxSignalPending(Wait->Thread);
}

static
LONG64
LxSignalfdRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_SIGNALFD Signalfd = File->Object;
    PLX_THREAD Thread = LxCurrentThread();
    LX_SIGNALFD_WAIT Wait;
    ULONG64 Count = Length / sizeof(LX_SIGNALFD_SIGINFO), Done = 0;
    LONG64 Result;

    UNREFERENCED_PARAMETER(Offset);
    if (Count == 0)
        return -LX_EINVAL;
    if ((Thread == NULL) || (Thread->Process != Signalfd->Process))
        return -LX_EAGAIN;

    Wait.Thread = Thread;
    Wait.Mask = Signalfd->Mask;

    for (;;)
    {
        while (Done < Count)
        {
            LX_SIGNALFD_SIGINFO Out;
            LX_SIGINFO Info;
            ULONG64 Signal = LxSignalDequeue(Thread, Signalfd->Mask, &Info);

            if (Signal == 0)
                break;
            RtlZeroMemory(&Out, sizeof(Out));
            Out.Signo = (ULONG)Signal;
            Out.Errno = Info.Errno;
            Out.Code = Info.Code;
            Out.Pid = (ULONG)Info.Child.Pid;
            Out.Uid = Info.Child.Uid;
            if (Signal == LX_SIGCHLD)
                Out.Status = Info.Child.Status;
            if ((Signal == LX_SIGSEGV) || (Signal == LX_SIGBUS) || (Signal == LX_SIGILL) || (Signal == LX_SIGFPE))
                Out.Addr = Info.Address;
            Result = LxCopyToUser(Buffer + Done * sizeof(Out), &Out, sizeof(Out));
            if (Result != 0)
                return (Done != 0) ? (LONG64)(Done * sizeof(Out)) : Result;
            Done++;
        }
        if (Done != 0)
            return (LONG64)(Done * sizeof(LX_SIGNALFD_SIGINFO));
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxWaitHead(&Signalfd->Process->SignalPoll, LxSignalfdReady, &Wait, NULL);
        if (Result < 0)
            return Result;
        if (LxSignalPending(Thread) && !(((ULONG64)Thread->Pending | (ULONG64)Thread->Process->Pending) & Wait.Mask))
            return -LX_EINTR;
    }
}

static
ULONG
LxSignalfdPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_SIGNALFD Signalfd = File->Object;
    PLX_THREAD Thread = LxCurrentThread();
    ULONG64 Pending = (ULONG64)Signalfd->Process->Pending;

    if (Entry != NULL)
        LxPollAdd(&Signalfd->Process->SignalPoll, Entry);
    if ((Thread != NULL) && (Thread->Process == Signalfd->Process))
        Pending |= (ULONG64)Thread->Pending;
    return (Pending & Signalfd->Mask) ? (LX_POLLIN | LX_POLLRDNORM) : 0;
}

static
VOID
LxSignalfdRelease(
    _In_ PLX_FILE File)
{
    PLX_SIGNALFD Signalfd = File->Object;

    LxProcessDereference(Signalfd->Process);
    LxFree(Signalfd);
    File->Object = NULL;
}

static const LX_FILE_OPS LxSignalfdOps =
{
    LxSignalfdRead, NULL, NULL, LxSignalfdPoll, LxAnonStat, NULL, NULL, NULL, LxSignalfdRelease
};

LX_SYSCALL(LxSysSignalfd4)
{
    LONG Fd = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[3];
    PLX_SIGNALFD Signalfd;
    PLX_FILE File;
    ULONG64 Mask;
    LONG64 Result;

    if ((Flags & ~(LX_O_CLOEXEC | LX_O_NONBLOCK)) || (Call->Args[2] != sizeof(ULONG64)))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Mask, Call->Args[1], sizeof(Mask));
    if (Result != 0)
        return Result;
    Mask &= ~((1ULL << (LX_SIGKILL - 1)) | (1ULL << (LX_SIGSTOP - 1)));

    if (Fd != -1)
    {
        File = LxFdGet(Call->Process, Fd);
        if (File == NULL)
            return -LX_EBADF;
        if (File->Ops != &LxSignalfdOps)
        {
            LxFileDereference(File);
            return -LX_EINVAL;
        }
        ((PLX_SIGNALFD)File->Object)->Mask = Mask;
        LxFileDereference(File);
        return Fd;
    }

    File = LxAnonFileCreate(&LxSignalfdOps, LX_O_RDONLY | (Flags & LX_O_NONBLOCK), "[signalfd]", sizeof(LX_SIGNALFD));
    if (File == NULL)
        return -LX_ENOMEM;
    Signalfd = File->Object;
    Signalfd->Mask = Mask;
    LxProcessReference(Call->Process);
    Signalfd->Process = Call->Process;
    return LxInstallNewFile(Call->Process, File, (Flags & LX_O_CLOEXEC) != 0);
}

static
VOID
LxInotifyQueue(
    _In_ PLX_INOTIFY Inotify,
    _In_ LONG Wd,
    _In_ ULONG Mask,
    _In_ ULONG Cookie,
    _In_opt_ PCSTR Name)
{
    ULONG NameLength = (Name != NULL) ? (ULONG)LX_ALIGN_UP(strlen(Name) + 1, sizeof(LX_INOTIFY_EVENT)) : 0;
    ULONG Length = sizeof(LX_INOTIFY_EVENT) + NameLength;
    PLX_INOTIFY_ITEM Item;
    PLX_INOTIFY_EVENT Event;
    KIRQL Irql;

    Item = LxAlloc(FIELD_OFFSET(LX_INOTIFY_ITEM, Data) + Length);
    if (Item == NULL)
        return;
    Item->Length = Length;
    Event = (PLX_INOTIFY_EVENT)Item->Data;
    Event->Wd = Wd;
    Event->Mask = Mask;
    Event->Cookie = Cookie;
    Event->Length = NameLength;
    if (Name != NULL)
        RtlCopyMemory(Event + 1, Name, strlen(Name));

    KeAcquireSpinLock(&Inotify->Lock, &Irql);
    if (Inotify->QueuedBytes + Length > LX_INOTIFY_MAX_QUEUE)
    {
        KeReleaseSpinLock(&Inotify->Lock, Irql);
        LxFree(Item);
        if (!Inotify->Overflowed)
        {
            Inotify->Overflowed = TRUE;
            LxInotifyQueue(Inotify, -1, LX_IN_Q_OVERFLOW, 0, NULL);
        }
        return;
    }
    if (!IsListEmpty(&Inotify->Events) && (Name == NULL))
    {
        PLX_INOTIFY_ITEM Last = CONTAINING_RECORD(Inotify->Events.Blink, LX_INOTIFY_ITEM, Link);
        PLX_INOTIFY_EVENT LastEvent = (PLX_INOTIFY_EVENT)Last->Data;

        if ((LastEvent->Wd == Wd) && (LastEvent->Mask == Mask) && (LastEvent->Cookie == Cookie) &&
            (LastEvent->Length == 0))
        {
            KeReleaseSpinLock(&Inotify->Lock, Irql);
            LxFree(Item);
            return;
        }
    }
    InsertTailList(&Inotify->Events, &Item->Link);
    Inotify->QueuedBytes += Length;
    KeReleaseSpinLock(&Inotify->Lock, Irql);
    LxPollWake(&Inotify->Poll, LX_POLLIN | LX_POLLRDNORM);
}

VOID
LxInotifyNotify(
    _In_ PLX_INSTANCE Instance,
    _In_ PCSTR Path,
    _In_ ULONG Mask,
    _In_ BOOLEAN Removed)
{
    PLIST_ENTRY Entry, WatchEntry, Next;
    PCSTR Slash, Name;
    SIZE_T ParentLength;

    if (LxInotifyWatchCount == 0)
        return;

    Slash = strrchr(Path, '/');
    if (Slash == NULL)
        return;
    Name = Slash + 1;
    ParentLength = (Slash == Path) ? 1 : (SIZE_T)(Slash - Path);

    ExAcquireFastMutex(&LxInotifyLock);
    for (Entry = LxInotifyList.Flink; Entry != &LxInotifyList; Entry = Entry->Flink)
    {
        PLX_INOTIFY Inotify = CONTAINING_RECORD(Entry, LX_INOTIFY, GlobalLink);

        if (Inotify->Instance != Instance)
            continue;
        for (WatchEntry = Inotify->Watches.Flink; WatchEntry != &Inotify->Watches; WatchEntry = Next)
        {
            PLX_INOTIFY_WATCH Watch = CONTAINING_RECORD(WatchEntry, LX_INOTIFY_WATCH, Link);
            ULONG Events;
            BOOLEAN Drop = FALSE;

            Next = WatchEntry->Flink;
            if (strcmp(Watch->Path, Path) == 0)
            {
                Events = Mask & Watch->Mask & (LX_IN_MODIFY | LX_IN_ATTRIB | LX_IN_ACCESS | LX_IN_OPEN |
                                               LX_IN_CLOSE_WRITE | LX_IN_CLOSE_NOWRITE);
                if (Events != 0)
                    LxInotifyQueue(Inotify, Watch->Wd, Events | (Mask & LX_IN_ISDIR), 0, NULL);
                if (Removed && (Mask & LX_IN_DELETE))
                {
                    if (Watch->Mask & LX_IN_DELETE_SELF)
                        LxInotifyQueue(Inotify, Watch->Wd, LX_IN_DELETE_SELF, 0, NULL);
                    Drop = TRUE;
                }
                else if (Removed && (Mask & LX_IN_MOVED_FROM) && (Watch->Mask & LX_IN_MOVE_SELF))
                {
                    LxInotifyQueue(Inotify, Watch->Wd, LX_IN_MOVE_SELF, 0, NULL);
                }
                if ((Events != 0) && (Watch->Mask & LX_IN_ONESHOT))
                    Drop = TRUE;
            }
            else if ((strlen(Watch->Path) == ParentLength) && (strncmp(Watch->Path, Path, ParentLength) == 0))
            {
                Events = Mask & Watch->Mask & LX_IN_ALL_EVENTS;
                if (Events != 0)
                {
                    LxInotifyQueue(Inotify, Watch->Wd, Events | (Mask & LX_IN_ISDIR), 0, Name);
                    if (Watch->Mask & LX_IN_ONESHOT)
                        Drop = TRUE;
                }
            }

            if (Drop)
            {
                LxInotifyQueue(Inotify, Watch->Wd, LX_IN_IGNORED, 0, NULL);
                RemoveEntryList(&Watch->Link);
                InterlockedDecrement(&LxInotifyWatchCount);
                LxFree(Watch->Path);
                LxFree(Watch);
            }
        }
    }
    ExReleaseFastMutex(&LxInotifyLock);
}

VOID
LxInotifyEvent(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path,
    _In_ ULONG Mask,
    _In_ BOOLEAN Removed)
{
    LxInotifyNotify(Process->Instance, Path, Mask, Removed);
}

VOID
LxInotifyFileEvent(
    _In_ PLX_FILE File,
    _In_ ULONG Mask)
{
    if ((LxInotifyWatchCount == 0) || (File->Mount == NULL) || (File->Path == NULL) || (File->Path[0] != '/'))
        return;
    LxInotifyNotify(File->Mount->Instance, File->Path, Mask | (File->Directory ? LX_IN_ISDIR : 0), FALSE);
}

static
LONG64
LxInotifyRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_INOTIFY Inotify = File->Object;
    ULONG64 Done = 0;
    LONG64 Result;

    UNREFERENCED_PARAMETER(Offset);
    for (;;)
    {
        for (;;)
        {
            PLX_INOTIFY_ITEM Item = NULL;
            KIRQL Irql;

            KeAcquireSpinLock(&Inotify->Lock, &Irql);
            if (!IsListEmpty(&Inotify->Events))
            {
                Item = CONTAINING_RECORD(Inotify->Events.Flink, LX_INOTIFY_ITEM, Link);
                if (Done + Item->Length > Length)
                {
                    Item = NULL;
                    KeReleaseSpinLock(&Inotify->Lock, Irql);
                    if (Done == 0)
                        return -LX_EINVAL;
                    break;
                }
                RemoveEntryList(&Item->Link);
                Inotify->QueuedBytes -= Item->Length;
                if (Inotify->QueuedBytes == 0)
                    Inotify->Overflowed = FALSE;
            }
            KeReleaseSpinLock(&Inotify->Lock, Irql);
            if (Item == NULL)
                break;

            Result = LxCopyToUser(Buffer + Done, Item->Data, Item->Length);
            Done += Item->Length;
            LxFree(Item);
            if (Result != 0)
                return Result;
        }
        if (Done != 0)
            return (LONG64)Done;
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLIN, NULL);
        if (Result < 0)
            return Result;
    }
}

static
ULONG
LxInotifyPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_INOTIFY Inotify = File->Object;
    ULONG Mask;
    KIRQL Irql;

    if (Entry != NULL)
        LxPollAdd(&Inotify->Poll, Entry);
    KeAcquireSpinLock(&Inotify->Lock, &Irql);
    Mask = IsListEmpty(&Inotify->Events) ? 0 : (LX_POLLIN | LX_POLLRDNORM);
    KeReleaseSpinLock(&Inotify->Lock, Irql);
    return Mask;
}

static
LONG64
LxInotifyIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_INOTIFY Inotify = File->Object;
    LONG Queued;

    if (Request != LX_FIONREAD)
        return -LX_ENOTTY;
    Queued = (LONG)Inotify->QueuedBytes;
    return LxCopyToUser(Argument, &Queued, sizeof(Queued));
}

static
VOID
LxInotifyRelease(
    _In_ PLX_FILE File)
{
    PLX_INOTIFY Inotify = File->Object;

    ExAcquireFastMutex(&LxInotifyLock);
    RemoveEntryList(&Inotify->GlobalLink);
    while (!IsListEmpty(&Inotify->Watches))
    {
        PLX_INOTIFY_WATCH Watch = CONTAINING_RECORD(RemoveHeadList(&Inotify->Watches), LX_INOTIFY_WATCH, Link);

        InterlockedDecrement(&LxInotifyWatchCount);
        LxFree(Watch->Path);
        LxFree(Watch);
    }
    ExReleaseFastMutex(&LxInotifyLock);

    while (!IsListEmpty(&Inotify->Events))
        LxFree(CONTAINING_RECORD(RemoveHeadList(&Inotify->Events), LX_INOTIFY_ITEM, Link));
    LxInstanceDereference(Inotify->Instance);
    LxFree(Inotify);
    File->Object = NULL;
}

static const LX_FILE_OPS LxInotifyOps =
{
    LxInotifyRead, NULL, LxInotifyIoctl, LxInotifyPoll, LxAnonStat, NULL, NULL, NULL, LxInotifyRelease
};

LX_SYSCALL(LxSysInotifyInit1)
{
    ULONG Flags = (ULONG)Call->Args[0];
    PLX_INOTIFY Inotify;
    PLX_FILE File;

    if (Flags & ~(LX_O_CLOEXEC | LX_O_NONBLOCK))
        return -LX_EINVAL;
    File = LxAnonFileCreate(&LxInotifyOps, LX_O_RDONLY | (Flags & LX_O_NONBLOCK), "inotify", sizeof(LX_INOTIFY));
    if (File == NULL)
        return -LX_ENOMEM;
    Inotify = File->Object;
    KeInitializeSpinLock(&Inotify->Lock);
    InitializeListHead(&Inotify->Watches);
    InitializeListHead(&Inotify->Events);
    LxPollHeadInit(&Inotify->Poll);
    Inotify->NextWd = 1;
    LxInstanceReference(Call->Process->Instance);
    Inotify->Instance = Call->Process->Instance;
    ExAcquireFastMutex(&LxInotifyLock);
    InsertTailList(&LxInotifyList, &Inotify->GlobalLink);
    ExReleaseFastMutex(&LxInotifyLock);
    return LxInstallNewFile(Call->Process, File, (Flags & LX_O_CLOEXEC) != 0);
}

LX_SYSCALL(LxSysInotifyAddWatch)
{
    ULONG Mask = (ULONG)Call->Args[2];
    PLX_INOTIFY Inotify;
    PLX_FILE File;
    PLIST_ENTRY Entry;
    LX_NODE Node;
    PSTR Path;
    LONG64 Result;
    LONG Error;

    if ((Mask & (LX_IN_ALL_EVENTS | LX_IN_UNMOUNT)) == 0)
        return -LX_EINVAL;
    if ((Mask & LX_IN_MASK_ADD) && (Mask & LX_IN_MASK_CREATE))
        return -LX_EINVAL;

    File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (File == NULL)
        return -LX_EBADF;
    if (File->Ops != &LxInotifyOps)
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }
    Inotify = File->Object;

    Result = LxCopyStringFromUser(Call->Args[1], &Path, LX_PATH_MAX - 1);
    if (Result != 0)
    {
        LxFileDereference(File);
        return Result;
    }
    Error = LxLookup(Call->Process, LX_AT_FDCWD, Path,
                     ((Mask & LX_IN_DONT_FOLLOW) ? 0 : LX_LOOKUP_FOLLOW) | ((Mask & LX_IN_ONLYDIR) ? LX_LOOKUP_DIRECTORY : 0),
                     &Node);
    LxFree(Path);
    if (Error != 0)
    {
        LxFileDereference(File);
        return Error;
    }

    ExAcquireFastMutex(&LxInotifyLock);
    for (Entry = Inotify->Watches.Flink; Entry != &Inotify->Watches; Entry = Entry->Flink)
    {
        PLX_INOTIFY_WATCH Watch = CONTAINING_RECORD(Entry, LX_INOTIFY_WATCH, Link);

        if (strcmp(Watch->Path, Node.Path) == 0)
        {
            if (Mask & LX_IN_MASK_CREATE)
                Result = -LX_EEXIST;
            else
            {
                Watch->Mask = (Mask & LX_IN_MASK_ADD) ? (Watch->Mask | Mask) : Mask;
                Result = Watch->Wd;
            }
            break;
        }
    }
    if (Entry == &Inotify->Watches)
    {
        PLX_INOTIFY_WATCH Watch = LxAlloc(sizeof(*Watch));

        if (Watch != NULL)
            Watch->Path = LxStrDup(Node.Path);
        if ((Watch == NULL) || (Watch->Path == NULL))
        {
            LxFree(Watch);
            Result = -LX_ENOMEM;
        }
        else
        {
            Watch->Wd = Inotify->NextWd++;
            Watch->Mask = Mask;
            InsertTailList(&Inotify->Watches, &Watch->Link);
            InterlockedIncrement(&LxInotifyWatchCount);
            Result = Watch->Wd;
        }
    }
    ExReleaseFastMutex(&LxInotifyLock);

    LxNodeFree(&Node);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysInotifyRmWatch)
{
    PLX_INOTIFY Inotify;
    PLX_FILE File;
    PLIST_ENTRY Entry;
    PLX_INOTIFY_WATCH Found = NULL;

    File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (File == NULL)
        return -LX_EBADF;
    if (File->Ops != &LxInotifyOps)
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }
    Inotify = File->Object;

    ExAcquireFastMutex(&LxInotifyLock);
    for (Entry = Inotify->Watches.Flink; Entry != &Inotify->Watches; Entry = Entry->Flink)
    {
        PLX_INOTIFY_WATCH Watch = CONTAINING_RECORD(Entry, LX_INOTIFY_WATCH, Link);

        if (Watch->Wd == (LONG)Call->Args[1])
        {
            Found = Watch;
            RemoveEntryList(&Watch->Link);
            InterlockedDecrement(&LxInotifyWatchCount);
            break;
        }
    }
    ExReleaseFastMutex(&LxInotifyLock);

    if (Found != NULL)
    {
        LxInotifyQueue(Inotify, Found->Wd, LX_IN_IGNORED, 0, NULL);
        LxFree(Found->Path);
        LxFree(Found);
    }
    LxFileDereference(File);
    return (Found != NULL) ? 0 : -LX_EINVAL;
}

static
ULONG
LxPidfdPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_PROCESS Process = File->Object;

    if (Entry != NULL)
        LxPollAdd(&Process->ExitPoll, Entry);
    return Process->Exited ? (LX_POLLIN | LX_POLLRDNORM | LX_POLLHUP) : 0;
}

static
VOID
LxPidfdRelease(
    _In_ PLX_FILE File)
{
    LxProcessDereference(File->Object);
    File->Object = NULL;
}

static
LONG64
LxPidfdIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Argument);
    return -LX_ENOTTY;
}

static const LX_FILE_OPS LxPidfdOps =
{
    NULL, NULL, LxPidfdIoctl, LxPidfdPoll, LxAnonStat, NULL, NULL, NULL, LxPidfdRelease
};

PLX_PROCESS
LxPidfdProcess(
    _In_ PLX_FILE File)
{
    return (File->Ops == &LxPidfdOps) ? File->Object : NULL;
}

PLX_FILE
LxPidfdCreate(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxAnonFileCreate(&LxPidfdOps, LX_O_RDWR | (Flags & LX_O_NONBLOCK), "[pidfd]", 0);

    if (File == NULL)
        return NULL;
    LxProcessReference(Process);
    File->Object = Process;
    return File;
}

LX_SYSCALL(LxSysPidfdOpen)
{
    ULONG Flags = (ULONG)Call->Args[1];
    PLX_PROCESS Target;
    PLX_FILE File;

    if (Flags & ~(LX_O_NONBLOCK | LX_O_EXCL))
        return -LX_EINVAL;
    if ((LONG)Call->Args[0] <= 0)
        return -LX_EINVAL;
    Target = LxProcessLookupNr(Call->Process, (ULONG)Call->Args[0]);
    if (Target == NULL)
        return -LX_ESRCH;
    File = LxPidfdCreate(Target, Flags);
    LxProcessDereference(Target);
    if (File == NULL)
        return -LX_ENOMEM;
    return LxInstallNewFile(Call->Process, File, TRUE);
}

#define LX_PIDFD_SIGNAL_THREAD          0x00000001
#define LX_PIDFD_SIGNAL_THREAD_GROUP    0x00000002
#define LX_PIDFD_SIGNAL_PROCESS_GROUP   0x00000004

LX_SYSCALL(LxSysPidfdSendSignal)
{
    LONG Signal = (LONG)Call->Args[1];
    ULONG Flags = (ULONG)Call->Args[3];
    PLX_PROCESS Target;
    PLX_FILE File;
    LX_SIGINFO Info;
    LONG64 Result = 0;

    if (Flags & ~(LX_PIDFD_SIGNAL_THREAD | LX_PIDFD_SIGNAL_THREAD_GROUP | LX_PIDFD_SIGNAL_PROCESS_GROUP))
        return -LX_EINVAL;
    if (Flags & (Flags - 1))
        return -LX_EINVAL;
    File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (File == NULL)
        return -LX_EBADF;
    Target = LxPidfdProcess(File);
    if (Target == NULL)
        Target = File->PidProcess;
    if (Target == NULL)
    {
        LxFileDereference(File);
        return -LX_EBADF;
    }
    if ((Call->Process->PidNs != NULL) && ((Target->PidNs == NULL) || !LxPidNsContains(Call->Process->PidNs, Target->PidNs)))
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }

    RtlZeroMemory(&Info, sizeof(Info));
    if (Call->Args[2] != 0)
    {
        Result = LxCopyFromUser(&Info, Call->Args[2], sizeof(Info));
        if ((Result == 0) && (Info.Signo != Signal))
            Result = -LX_EINVAL;
        if ((Result == 0) && LxSignalInfoHasUid(Signal, &Info))
            Info.Child.Uid = LxCredMakeId(Call->Process, FALSE, Info.Child.Uid);
        if ((Result == 0) && ((Target != Call->Process) || (Flags & LX_PIDFD_SIGNAL_PROCESS_GROUP)) &&
            ((Info.Code >= 0) || (Info.Code == LX_SI_TKILL)))
        {
            Result = -LX_EPERM;
        }
    }
    else
    {
        Info.Signo = Signal;
        Info.Code = LX_SI_USER;
        Info.Child.Pid = (LONG)Call->Process->Pid;
        Info.Child.Uid = Call->Process->Uid[0];
    }
    if ((Result == 0) && ((Signal < 0) || (Signal > LX_NSIG)))
        Result = -LX_EINVAL;
    if ((Result == 0) && Target->Exited)
        Result = -LX_ESRCH;
    if ((Result == 0) && !(Flags & LX_PIDFD_SIGNAL_PROCESS_GROUP))
        Result = LxSignalPermitted(Call->Process, Target, Signal);
    if ((Result == 0) && (Signal != 0))
    {
        if (Flags & LX_PIDFD_SIGNAL_PROCESS_GROUP)
        {
            ULONG Denied = 0;

            if (LxSignalGroupInfo(Target->Instance, NULL, Target->Pgid, Signal, &Info, Call->Process, &Denied) == 0)
                Result = (Denied != 0) ? -LX_EPERM : -LX_ESRCH;
        }
        else if (Flags & LX_PIDFD_SIGNAL_THREAD)
        {
            PLX_PROCESS Owner = NULL;
            PLX_THREAD Leader = LxThreadLookupReferenced(Target->Instance, Target->Pid, &Owner);

            if ((Leader == NULL) || (Owner != Target))
                Result = -LX_ESRCH;
            else
                LxSignalSend(Target, Leader, Signal, &Info);
            if (Owner != NULL)
                LxProcessDereference(Owner);
        }
        else
        {
            LxSignalSend(Target, NULL, Signal, &Info);
        }
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysPidfdGetfd)
{
    PLX_PROCESS Target;
    PLX_FILE File, Remote;
    LONG64 Result;

    if (Call->Args[2] != 0)
        return -LX_EINVAL;
    File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (File == NULL)
        return -LX_EBADF;
    Target = LxPidfdProcess(File);
    if (Target == NULL)
    {
        LxFileDereference(File);
        return -LX_EBADF;
    }
    Remote = LxFdGet(Target, (LONG)Call->Args[1]);
    LxFileDereference(File);
    if (Remote == NULL)
        return -LX_EBADF;
    Result = LxFdInstall(Call->Process, Remote, 0, TRUE);
    LxFileDereference(Remote);
    return Result;
}

LX_SYSCALL(LxSysMemfdCreate)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    UNICODE_STRING NtPath;
    WCHAR Suffix[32];
    ULONG Flags = (ULONG)Call->Args[1];
    PLX_INSTANCE Instance = Call->Process->Instance;
    HANDLE Handle;
    PLX_FILE File;
    PSTR Name;
    NTSTATUS Status;
    LONG64 Result;

    if (Flags & ~(LX_MFD_CLOEXEC | LX_MFD_ALLOW_SEALING | LX_MFD_HUGETLB | LX_MFD_NOEXEC_SEAL | LX_MFD_EXEC))
        return -LX_EINVAL;
    Result = LxCopyStringFromUser(Call->Args[0], &Name, 249);
    if (Result != 0)
        return (Result == -LX_ENAMETOOLONG) ? -LX_EINVAL : Result;

    RtlStringCbPrintfW(Suffix, sizeof(Suffix), L"\\memfd.%lu", (ULONG)InterlockedIncrement(&LxMemfdCounter));
    NtPath.MaximumLength = Instance->TmpNt.Length + (USHORT)(wcslen(Suffix) * sizeof(WCHAR)) + sizeof(WCHAR);
    NtPath.Length = 0;
    NtPath.Buffer = LxAllocPaged(NtPath.MaximumLength);
    if (NtPath.Buffer == NULL)
    {
        LxFree(Name);
        return -LX_ENOMEM;
    }
    RtlAppendUnicodeStringToString(&NtPath, &Instance->TmpNt);
    RtlAppendUnicodeToString(&NtPath, Suffix);

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE, &ObjectAttributes, &Iosb, NULL,
                          FILE_ATTRIBUTE_TEMPORARY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_CREATE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE, NULL, 0);
    LxFreePaged(NtPath.Buffer);
    if (!NT_SUCCESS(Status))
    {
        LxFree(Name);
        return -LxStatusToErrno(Status);
    }

    File = LxFileFromNtHandle(Handle, LX_O_RDWR);
    if (File == NULL)
    {
        ZwClose(Handle);
        LxFree(Name);
        return -LX_ENOMEM;
    }
    File->Path = LxStrPrintf("/memfd:%s (deleted)", Name);
    File->Mode = 0777;
    File->Seals = (Flags & LX_MFD_ALLOW_SEALING) ? 0 : LX_F_SEAL_SEAL;
    LxFree(Name);
    return LxInstallNewFile(Call->Process, File, (Flags & LX_MFD_CLOEXEC) != 0);
}

VOID
LxFdinfoExtra(
    _In_ PLX_FILE File,
    _In_opt_ PVOID Context)
{
    CHAR Line[96];

    if (Context == NULL)
        return;
    if (File->Ops == &LxPidfdOps)
    {
        PLX_PROCESS Process = File->Object;
        PLX_THREAD Viewer = LxCurrentThread();
        ULONG First = ((Viewer != NULL) && (Viewer->Process->PidNs != NULL)) ? Viewer->Process->PidNs->Level : 0;
        ULONG Last = (Process->PidNs != NULL) ? Process->PidNs->Level : 0;
        LONG Pid = (Viewer != NULL) ? (LONG)LxPidNr(Viewer->Process, Process) : (LONG)Process->Pid;
        ULONG Level;

        if (Process->Exited)
            Pid = -1;
        RtlStringCbPrintfA(Line, sizeof(Line), "Pid:\t%ld\nNSpid:", Pid);
        LxFdinfoAppend(Context, Line);
        if ((Pid <= 0) || (First > Last))
        {
            RtlStringCbPrintfA(Line, sizeof(Line), "\t%ld", Pid);
            LxFdinfoAppend(Context, Line);
        }
        else
        {
            for (Level = First; Level <= Last; Level++)
            {
                RtlStringCbPrintfA(Line, sizeof(Line), "\t%lu", Process->NsPid[Level]);
                LxFdinfoAppend(Context, Line);
            }
        }
        LxFdinfoAppend(Context, "\n");
    }
    else if (File->Ops == &LxEventfdOps)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "eventfd-count: %16llx\neventfd-id: %llu\n",
                           ((PLX_EVENTFD)File->Object)->Count, File->Ino);
        LxFdinfoAppend(Context, Line);
    }
    else if (File->Ops == &LxSignalfdOps)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "sigmask:\t%016llx\n", ((PLX_SIGNALFD)File->Object)->Mask);
        LxFdinfoAppend(Context, Line);
    }
    else if (File->Ops == &LxTimerfdOps)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "clockid: %lu\n", ((PLX_TIMERFD)File->Object)->Clock);
        LxFdinfoAppend(Context, Line);
    }
}

VOID
LxFdObjectsInitialize(VOID)
{
    ExInitializeFastMutex(&LxInotifyLock);
}
