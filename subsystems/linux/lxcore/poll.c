/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Readiness notification, poll, select and epoll
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_POLL_INTERVAL    (-500000LL)
#define LX_POLL_MAX_FDS     4096

typedef struct _LX_EPOLL
{
    KSPIN_LOCK Lock;
    KEVENT Mutex;
    LIST_ENTRY Items;
    LIST_ENTRY Ready;
    KEVENT Event;
    LX_POLL_HEAD Poll;
    ULONG PollOnlyItems;
} LX_EPOLL, *PLX_EPOLL;

typedef struct _LX_EPOLL_ITEM
{
    LIST_ENTRY Link;
    LIST_ENTRY ReadyLink;
    LIST_ENTRY FileLink;
    PLX_EPOLL Epoll;
    PLX_FILE File;
    LONG Fd;
    ULONG Events;
    ULONG64 Data;
    BOOLEAN OnReady;
    BOOLEAN Rearm;
    BOOLEAN Disabled;
    BOOLEAN Detached;
    LX_POLL_ENTRY PollEntry;
} LX_EPOLL_ITEM, *PLX_EPOLL_ITEM;

typedef struct _LX_POLL_WAITER
{
    KEVENT Event;
    ULONG Events;
} LX_POLL_WAITER, *PLX_POLL_WAITER;

static
VOID
LxEpollLock(
    _In_ PLX_EPOLL Epoll)
{
    KeWaitForSingleObject(&Epoll->Mutex, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxEpollUnlock(
    _In_ PLX_EPOLL Epoll)
{
    KeSetEvent(&Epoll->Mutex, IO_NO_INCREMENT, FALSE);
}

VOID
LxPollHeadInit(
    _Out_ PLX_POLL_HEAD Head)
{
    KeInitializeSpinLock(&Head->Lock);
    InitializeListHead(&Head->Entries);
}

VOID
LxEventSourceInit(
    _Out_ PLX_EVENT_SOURCE Source)
{
    LxPollHeadInit(&Source->Poll);
    Source->Generation = 0;
}

VOID
LxEventSourceSignal(
    _In_ PLX_EVENT_SOURCE Source)
{
    InterlockedIncrement64(&Source->Generation);
    LxPollWake(&Source->Poll, LX_POLLPRI | LX_POLLERR);
}

VOID
LxPollAdd(
    _In_ PLX_POLL_HEAD Head,
    _Inout_ PLX_POLL_ENTRY Entry)
{
    KIRQL Irql;

    KeAcquireSpinLock(&Head->Lock, &Irql);
    Entry->Head = Head;
    InsertTailList(&Head->Entries, &Entry->Link);
    KeReleaseSpinLock(&Head->Lock, Irql);
}

VOID
LxPollRemove(
    _Inout_ PLX_POLL_ENTRY Entry)
{
    PLX_POLL_HEAD Head = Entry->Head;
    KIRQL Irql;

    if (Head == NULL)
        return;

    KeAcquireSpinLock(&Head->Lock, &Irql);
    if (Entry->Head != NULL)
    {
        RemoveEntryList(&Entry->Link);
        Entry->Head = NULL;
    }
    KeReleaseSpinLock(&Head->Lock, Irql);
}

VOID
LxPollWake(
    _In_ PLX_POLL_HEAD Head,
    _In_ ULONG Events)
{
    PLIST_ENTRY Entry, Next;
    KIRQL Irql;

    KeAcquireSpinLock(&Head->Lock, &Irql);
    for (Entry = Head->Entries.Flink; Entry != &Head->Entries; Entry = Next)
    {
        PLX_POLL_ENTRY Poll = CONTAINING_RECORD(Entry, LX_POLL_ENTRY, Link);

        Next = Entry->Flink;
        Poll->Notify(Poll, Events);
    }
    KeReleaseSpinLock(&Head->Lock, Irql);
}

BOOLEAN
LxSignalPending(
    _In_opt_ PLX_THREAD Thread)
{
    if (Thread == NULL)
        return FALSE;
    return (((ULONG64)Thread->Pending | (ULONG64)Thread->Process->Pending) & ~Thread->SigMask) != 0;
}

NTSTATUS
LxWaitForEvent(
    _In_ PKEVENT Event,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLX_THREAD Thread = LxCurrentThread();
    LARGE_INTEGER Remaining;
    PLARGE_INTEGER Wait = Timeout;
    LONG64 Deadline = 0;
    NTSTATUS Status;

    if (LxSignalPending(Thread))
        return STATUS_ALERTED;
    if ((Timeout != NULL) && (Timeout->QuadPart < 0))
        Deadline = (LONG64)KeQueryInterruptTime() - Timeout->QuadPart;

    for (;;)
    {
        Status = KeWaitForSingleObject(Event, UserRequest, UserMode, TRUE, Wait);
        if (Status == STATUS_SUCCESS)
            return STATUS_SUCCESS;
        if (Status == STATUS_TIMEOUT)
            return LxSignalPending(Thread) ? STATUS_ALERTED : STATUS_TIMEOUT;
        if ((Status != STATUS_USER_APC) && (Status != STATUS_ALERTED))
            return Status;
        if ((Thread == NULL) || LxSignalPending(Thread) || (Status == STATUS_USER_APC))
            return STATUS_ALERTED;
        if (Deadline != 0)
        {
            LONG64 Now = (LONG64)KeQueryInterruptTime();

            if (Now >= Deadline)
                return STATUS_TIMEOUT;
            Remaining.QuadPart = -(Deadline - Now);
            Wait = &Remaining;
        }
    }
}

ULONG
LxFilePoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    if (File->Ops->Poll != NULL)
        return File->Ops->Poll(File, Entry);
    return LX_POLLIN | LX_POLLOUT | LX_POLLRDNORM | LX_POLLWRNORM;
}

static
VOID
LxPollWaiterNotify(
    _In_ PLX_POLL_ENTRY Entry,
    _In_ ULONG Events)
{
    PLX_POLL_WAITER Waiter = Entry->Context;

    if ((Waiter->Events != 0) && (Events != 0) && !(Events & (Waiter->Events | LX_POLLERR | LX_POLLHUP)))
        return;
    KeSetEvent(&Waiter->Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxSetWaitMask(
    _In_ PLX_THREAD Thread,
    _In_ ULONG64 SigMaskPointer,
    _In_ ULONG64 SigSetSize,
    _Out_ PLONG64 Error)
{
    ULONG64 Mask;

    *Error = 0;
    if (SigMaskPointer == 0)
        return;
    if (SigSetSize != sizeof(ULONG64))
    {
        *Error = -LX_EINVAL;
        return;
    }
    *Error = LxCopyFromUser(&Mask, SigMaskPointer, sizeof(Mask));
    if (*Error != 0)
        return;

    Thread->SuspendMask = Thread->SigMask;
    Thread->Suspended = TRUE;
    Thread->SigMask = Mask & ~((1ULL << (LX_SIGKILL - 1)) | (1ULL << (LX_SIGSTOP - 1)));
}

typedef struct _LX_FILE_WAITER
{
    LX_POLL_WAITER Waiter;
    LX_POLL_ENTRY Entry;
} LX_FILE_WAITER, *PLX_FILE_WAITER;

LONG64
LxFileWait(
    _In_ PLX_FILE File,
    _In_ ULONG Events,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLX_FILE_WAITER Wait = LxAlloc(sizeof(*Wait));
    NTSTATUS Status;
    ULONG Mask;

    if (Wait == NULL)
        return -LX_ENOMEM;

    KeInitializeEvent(&Wait->Waiter.Event, NotificationEvent, FALSE);
    Wait->Waiter.Events = Events;
    Wait->Entry.Notify = LxPollWaiterNotify;
    Wait->Entry.Context = &Wait->Waiter;
    Mask = LxFilePoll(File, &Wait->Entry);
    if (Mask & (Events | LX_POLLERR | LX_POLLHUP))
    {
        LxPollRemove(&Wait->Entry);
        LxFree(Wait);
        return 0;
    }

    if ((Wait->Entry.Head == NULL) && File->PollOnly && (Timeout == NULL))
    {
        LARGE_INTEGER Interval;

        Interval.QuadPart = LX_POLL_INTERVAL;
        Status = LxWaitForEvent(&Wait->Waiter.Event, &Interval);
        if (Status == STATUS_TIMEOUT)
            Status = STATUS_SUCCESS;
    }
    else
    {
        Status = LxWaitForEvent(&Wait->Waiter.Event, Timeout);
    }
    LxPollRemove(&Wait->Entry);
    LxFree(Wait);

    if (Status == STATUS_ALERTED)
        return -LX_EINTR;
    if (Status == STATUS_TIMEOUT)
        return -LX_EAGAIN;
    return 0;
}

LONG64
LxWaitHead(
    _In_ PLX_POLL_HEAD Head,
    _In_ BOOLEAN (*Ready)(_In_ PVOID Context),
    _In_ PVOID Context,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLX_FILE_WAITER Wait;
    NTSTATUS Status;

    if (Ready(Context))
        return 0;

    Wait = LxAlloc(sizeof(*Wait));
    if (Wait == NULL)
        return -LX_ENOMEM;

    KeInitializeEvent(&Wait->Waiter.Event, NotificationEvent, FALSE);
    Wait->Entry.Notify = LxPollWaiterNotify;
    Wait->Entry.Context = &Wait->Waiter;
    LxPollAdd(Head, &Wait->Entry);
    if (Ready(Context))
        Status = STATUS_SUCCESS;
    else
        Status = LxWaitForEvent(&Wait->Waiter.Event, Timeout);
    LxPollRemove(&Wait->Entry);
    LxFree(Wait);

    if (Status == STATUS_ALERTED)
        return -LX_EINTR;
    if (Status == STATUS_TIMEOUT)
        return -LX_EAGAIN;
    return 0;
}

typedef struct _LX_POLL_ITEM
{
    LONG Fd;
    SHORT Events;
    SHORT Revents;
} LX_POLL_ITEM, *PLX_POLL_ITEM;

static
LONG64
LxPollCore(
    _In_ PLX_PROCESS Process,
    _Inout_updates_(Count) PLX_POLL_ITEM Items,
    _In_ ULONG Count,
    _In_opt_ PLX_TIMESPEC Timeout)
{
    PLX_POLL_WAITER Waiter;
    PLX_POLL_ENTRY Entries;
    PLX_FILE *Files;
    LARGE_INTEGER Deadline, Now, Interval;
    BOOLEAN PollOnly = FALSE;
    LONG64 Result = 0;
    ULONG Index;

    Waiter = LxAlloc(sizeof(*Waiter));
    Entries = LxAlloc(sizeof(LX_POLL_ENTRY) * max(Count, 1));
    Files = LxAlloc(sizeof(PLX_FILE) * max(Count, 1));
    if ((Waiter == NULL) || (Entries == NULL) || (Files == NULL))
    {
        LxFree(Waiter);
        LxFree(Entries);
        LxFree(Files);
        return -LX_ENOMEM;
    }
    KeInitializeEvent(&Waiter->Event, NotificationEvent, FALSE);

    for (Index = 0; Index < Count; Index++)
    {
        Entries[Index].Notify = LxPollWaiterNotify;
        Entries[Index].Context = Waiter;
        if (Items[Index].Fd >= 0)
            Files[Index] = LxFdGet(Process, Items[Index].Fd);
    }

    if (Timeout != NULL)
    {
        Now.QuadPart = (LONG64)KeQueryInterruptTime();
        Deadline.QuadPart = Now.QuadPart + Timeout->Seconds * 10000000 + Timeout->Nanoseconds / 100;
    }
    else
    {
        Deadline.QuadPart = MAXLONGLONG;
    }

    for (;;)
    {
        ULONG Ready = 0;
        NTSTATUS Status;

        KeClearEvent(&Waiter->Event);
        for (Index = 0; Index < Count; Index++)
        {
            ULONG Mask;

            Items[Index].Revents = 0;
            if (Items[Index].Fd < 0)
                continue;
            if (Files[Index] == NULL)
            {
                Items[Index].Revents = LX_POLLNVAL;
                Ready++;
                continue;
            }

            if (Files[Index]->PollOnly)
                PollOnly = TRUE;
            Mask = LxFilePoll(Files[Index], (Entries[Index].Head == NULL) ? &Entries[Index] : NULL);
            Mask &= (ULONG)(USHORT)Items[Index].Events | LX_POLLERR | LX_POLLHUP | LX_POLLNVAL;
            Items[Index].Revents = (SHORT)Mask;
            if (Mask != 0)
                Ready++;
        }

        if (Ready != 0)
        {
            Result = Ready;
            break;
        }

        Now.QuadPart = (LONG64)KeQueryInterruptTime();
        if (Now.QuadPart >= Deadline.QuadPart)
        {
            Result = 0;
            break;
        }

        Interval.QuadPart = (Deadline.QuadPart == MAXLONGLONG) ? MINLONGLONG : -(Deadline.QuadPart - Now.QuadPart);
        if (PollOnly && ((Interval.QuadPart == MINLONGLONG) || (Interval.QuadPart < LX_POLL_INTERVAL)))
            Interval.QuadPart = LX_POLL_INTERVAL;
        Status = LxWaitForEvent(&Waiter->Event, (Interval.QuadPart == MINLONGLONG) ? NULL : &Interval);
        if (Status == STATUS_ALERTED)
        {
            Result = -LX_EINTR;
            break;
        }
    }

    for (Index = 0; Index < Count; Index++)
    {
        LxPollRemove(&Entries[Index]);
        if (Files[Index] != NULL)
            LxFileDereference(Files[Index]);
    }
    LxFree(Files);
    LxFree(Entries);
    LxFree(Waiter);
    return Result;
}

LX_SYSCALL(LxSysPpoll)
{
    PLX_POLL_ITEM Items;
    ULONG64 Count = Call->Args[1];
    LX_TIMESPEC Timeout;
    LONG64 Result;

    if (Count > LX_POLL_MAX_FDS)
        return -LX_EINVAL;
    if (Call->Args[2] != 0)
    {
        Result = LxCopyFromUser(&Timeout, Call->Args[2], sizeof(Timeout));
        if (Result != 0)
            return Result;
        if ((Timeout.Seconds < 0) || (Timeout.Nanoseconds < 0) || (Timeout.Nanoseconds >= 1000000000))
            return -LX_EINVAL;
    }

    Items = LxAllocPaged(sizeof(LX_POLL_ITEM) * max(Count, 1));
    if (Items == NULL)
        return -LX_ENOMEM;
    Result = LxCopyFromUser(Items, Call->Args[0], (SIZE_T)Count * sizeof(LX_POLL_ITEM));
    if (Result == 0)
        LxSetWaitMask(Call->Thread, Call->Args[3], Call->Args[4], &Result);
    if (Result == 0)
    {
        Result = LxPollCore(Call->Process, Items, (ULONG)Count, (Call->Args[2] != 0) ? &Timeout : NULL);
        if (Result >= 0)
        {
            LONG64 Copy = LxCopyToUser(Call->Args[0], Items, (SIZE_T)Count * sizeof(LX_POLL_ITEM));

            if (Copy != 0)
                Result = Copy;
        }
    }
    LxFreePaged(Items);
    return Result;
}

LX_SYSCALL(LxSysPselect6)
{
    LONG Nfds = (LONG)Call->Args[0];
    ULONG64 Sets[3] = { Call->Args[1], Call->Args[2], Call->Args[3] };
    ULONG64 Bits[3][LX_MAX_FDS / 64];
    static const SHORT Masks[3] = { LX_POLLIN | LX_POLLHUP | LX_POLLERR, LX_POLLOUT | LX_POLLERR, LX_POLLPRI };
    PLX_POLL_ITEM Items;
    LX_TIMESPEC Timeout;
    ULONG64 SigArgs[2] = { 0, 0 };
    ULONG Count = 0, Index;
    LONG64 Result = 0;
    LONG Fd, Set;

    if ((Nfds < 0) || (Nfds > LX_MAX_FDS))
        return -LX_EINVAL;

    RtlZeroMemory(Bits, sizeof(Bits));
    for (Set = 0; Set < 3; Set++)
    {
        if (Sets[Set] != 0)
        {
            Result = LxCopyFromUser(Bits[Set], Sets[Set], (SIZE_T)LX_ALIGN_UP(Nfds, 64) / 8);
            if (Result != 0)
                return Result;
        }
    }
    if (Call->Args[4] != 0)
    {
        Result = LxCopyFromUser(&Timeout, Call->Args[4], sizeof(Timeout));
        if (Result != 0)
            return Result;
    }
    if (Call->Args[5] != 0)
    {
        Result = LxCopyFromUser(SigArgs, Call->Args[5], sizeof(SigArgs));
        if (Result != 0)
            return Result;
    }

    Items = LxAllocPaged(sizeof(LX_POLL_ITEM) * max(Nfds, 1));
    if (Items == NULL)
        return -LX_ENOMEM;

    for (Fd = 0; Fd < Nfds; Fd++)
    {
        SHORT Events = 0;

        for (Set = 0; Set < 3; Set++)
        {
            if (Bits[Set][Fd / 64] & (1ULL << (Fd % 64)))
                Events |= Masks[Set];
        }
        if (Events != 0)
        {
            Items[Count].Fd = Fd;
            Items[Count].Events = Events;
            Count++;
        }
    }

    LxSetWaitMask(Call->Thread, SigArgs[0], SigArgs[1], &Result);
    if (Result == 0)
        Result = LxPollCore(Call->Process, Items, Count, (Call->Args[4] != 0) ? &Timeout : NULL);
    if (Result >= 0)
    {
        LONG64 Total = 0;

        RtlZeroMemory(Bits, sizeof(Bits));
        for (Index = 0; Index < Count; Index++)
        {
            Fd = Items[Index].Fd;
            if (Items[Index].Revents & LX_POLLNVAL)
            {
                Result = -LX_EBADF;
                break;
            }
            for (Set = 0; Set < 3; Set++)
            {
                if ((Items[Index].Events & Masks[Set] & Items[Index].Revents) &&
                    (Sets[Set] != 0))
                {
                    Bits[Set][Fd / 64] |= 1ULL << (Fd % 64);
                    Total++;
                }
            }
        }
        if (Result >= 0)
        {
            Result = Total;
            for (Set = 0; Set < 3; Set++)
            {
                if (Sets[Set] != 0)
                {
                    LONG64 Copy = LxCopyToUser(Sets[Set], Bits[Set], (SIZE_T)LX_ALIGN_UP(Nfds, 64) / 8);

                    if (Copy != 0)
                        Result = Copy;
                }
            }
        }
    }

    LxFreePaged(Items);
    return Result;
}

static
VOID
LxEpollQueue(
    _In_ PLX_EPOLL_ITEM Item)
{
    PLX_EPOLL Epoll = Item->Epoll;
    BOOLEAN Wake = FALSE;
    KIRQL Irql;

    KeAcquireSpinLock(&Epoll->Lock, &Irql);
    if (Item->OnReady)
    {
        Item->Rearm = TRUE;
    }
    else if (!Item->Disabled && !Item->Detached)
    {
        InsertTailList(&Epoll->Ready, &Item->ReadyLink);
        Item->OnReady = TRUE;
        KeSetEvent(&Epoll->Event, IO_NO_INCREMENT, FALSE);
        Wake = TRUE;
    }
    KeReleaseSpinLock(&Epoll->Lock, Irql);

    if (Wake)
        LxPollWake(&Epoll->Poll, LX_POLLIN);
}

static
VOID
LxEpollNotify(
    _In_ PLX_POLL_ENTRY Entry,
    _In_ ULONG Events)
{
    PLX_EPOLL_ITEM Item = Entry->Context;

    if ((Events != 0) && !(Events & (Item->Events | LX_POLLERR | LX_POLLHUP)))
        return;
    LxEpollQueue(Item);
}

static
VOID
LxEpollDetach(
    _In_ PLX_EPOLL_ITEM Item)
{
    PLX_EPOLL Epoll = Item->Epoll;
    KIRQL Irql;

    LxPollRemove(&Item->PollEntry);
    KeAcquireSpinLock(&Epoll->Lock, &Irql);
    if (Item->OnReady)
    {
        RemoveEntryList(&Item->ReadyLink);
        Item->OnReady = FALSE;
    }
    Item->Detached = TRUE;
    KeReleaseSpinLock(&Epoll->Lock, Irql);
}

VOID
LxEpollFileReleased(
    _In_ PLX_FILE File)
{
    for (;;)
    {
        PLX_EPOLL_ITEM Item = NULL;
        KIRQL Irql;

        KeAcquireSpinLock(&File->EpollLock, &Irql);
        if (!IsListEmpty(&File->EpollItems))
        {
            Item = CONTAINING_RECORD(RemoveHeadList(&File->EpollItems), LX_EPOLL_ITEM, FileLink);
            InitializeListHead(&Item->FileLink);
        }
        KeReleaseSpinLock(&File->EpollLock, Irql);
        if (Item == NULL)
            break;

        LxEpollLock(Item->Epoll);
        LxEpollDetach(Item);
        if (Item->File->PollOnly)
            Item->Epoll->PollOnlyItems--;
        RemoveEntryList(&Item->Link);
        LxEpollUnlock(Item->Epoll);
        LxFree(Item);
    }
}

static
ULONG
LxEpollPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_EPOLL Epoll = File->Object;
    ULONG Mask;
    KIRQL Irql;

    if (Entry != NULL)
        LxPollAdd(&Epoll->Poll, Entry);
    KeAcquireSpinLock(&Epoll->Lock, &Irql);
    Mask = IsListEmpty(&Epoll->Ready) ? 0 : (LX_POLLIN | LX_POLLRDNORM);
    KeReleaseSpinLock(&Epoll->Lock, Irql);
    return Mask;
}

static
VOID
LxEpollRelease(
    _In_ PLX_FILE File)
{
    PLX_EPOLL Epoll = File->Object;

    LxEpollLock(Epoll);
    while (!IsListEmpty(&Epoll->Items))
    {
        PLX_EPOLL_ITEM Item = CONTAINING_RECORD(RemoveHeadList(&Epoll->Items), LX_EPOLL_ITEM, Link);
        KIRQL Irql;

        LxEpollDetach(Item);
        KeAcquireSpinLock(&Item->File->EpollLock, &Irql);
        RemoveEntryList(&Item->FileLink);
        KeReleaseSpinLock(&Item->File->EpollLock, Irql);
        LxFree(Item);
    }
    LxEpollUnlock(Epoll);
    LxFree(Epoll);
}

static
LONG
LxEpollStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = 0600;
    Stat->Nlink = 1;
    Stat->Ino = (ULONG64)(ULONG_PTR)File->Object;
    Stat->BlkSize = 4096;
    return 0;
}

static const LX_FILE_OPS LxEpollOps =
{
    NULL, NULL, NULL, LxEpollPoll, LxEpollStat, NULL, NULL, NULL, LxEpollRelease
};

LX_SYSCALL(LxSysEpollCreate1)
{
    PLX_EPOLL Epoll;
    PLX_FILE File;
    LONG64 Result;

    if (Call->Args[0] & ~(ULONG64)LX_O_CLOEXEC)
        return -LX_EINVAL;

    Epoll = LxAlloc(sizeof(*Epoll));
    if (Epoll == NULL)
        return -LX_ENOMEM;
    KeInitializeSpinLock(&Epoll->Lock);
    KeInitializeEvent(&Epoll->Mutex, SynchronizationEvent, TRUE);
    InitializeListHead(&Epoll->Items);
    InitializeListHead(&Epoll->Ready);
    KeInitializeEvent(&Epoll->Event, NotificationEvent, FALSE);
    LxPollHeadInit(&Epoll->Poll);

    File = LxFileAlloc(&LxEpollOps, LX_O_RDWR, 0);
    if (File == NULL)
    {
        LxFree(Epoll);
        return -LX_ENOMEM;
    }
    File->Object = Epoll;
    File->Path = LxStrDup("anon_inode:[eventpoll]");

    Result = LxFdInstall(Call->Process, File, 0, (Call->Args[0] & LX_O_CLOEXEC) != 0);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysEpollCtl)
{
    PLX_FILE EpollFile, Target;
    PLX_EPOLL Epoll;
    PLX_EPOLL_ITEM Item = NULL;
    LX_EPOLL_EVENT Event = { 0 };
    ULONG Operation = (ULONG)Call->Args[1];
    LONG Fd = (LONG)Call->Args[2];
    PLIST_ENTRY Entry;
    LONG64 Result = 0;
    KIRQL Irql;

    if (Operation != LX_EPOLL_CTL_DEL)
    {
        Result = LxCopyFromUser(&Event, Call->Args[3], sizeof(Event));
        if (Result != 0)
            return Result;
    }

    EpollFile = LxFdGet(Call->Process, (LONG)Call->Args[0]);
    if (EpollFile == NULL)
        return -LX_EBADF;
    if (EpollFile->Ops != &LxEpollOps)
    {
        LxFileDereference(EpollFile);
        return -LX_EINVAL;
    }
    Target = LxFdGet(Call->Process, Fd);
    if (Target == NULL)
    {
        LxFileDereference(EpollFile);
        return -LX_EBADF;
    }
    if (Target == EpollFile)
    {
        LxFileDereference(Target);
        LxFileDereference(EpollFile);
        return -LX_EINVAL;
    }

    Epoll = EpollFile->Object;
    LxEpollLock(Epoll);
    for (Entry = Epoll->Items.Flink; Entry != &Epoll->Items; Entry = Entry->Flink)
    {
        PLX_EPOLL_ITEM Candidate = CONTAINING_RECORD(Entry, LX_EPOLL_ITEM, Link);

        if ((Candidate->File == Target) && (Candidate->Fd == Fd))
        {
            Item = Candidate;
            break;
        }
    }

    switch (Operation)
    {
        case LX_EPOLL_CTL_ADD:
            if (Item != NULL)
            {
                Result = -LX_EEXIST;
                break;
            }
            Item = LxAlloc(sizeof(*Item));
            if (Item == NULL)
            {
                Result = -LX_ENOMEM;
                break;
            }
            Item->Epoll = Epoll;
            Item->File = Target;
            Item->Fd = Fd;
            Item->Events = Event.Events | LX_POLLERR | LX_POLLHUP;
            Item->Data = Event.Data;
            Item->PollEntry.Notify = LxEpollNotify;
            Item->PollEntry.Context = Item;
            InsertTailList(&Epoll->Items, &Item->Link);
            KeAcquireSpinLock(&Target->EpollLock, &Irql);
            InsertTailList(&Target->EpollItems, &Item->FileLink);
            KeReleaseSpinLock(&Target->EpollLock, Irql);
            if (Target->PollOnly)
                Epoll->PollOnlyItems++;
            if (LxFilePoll(Target, &Item->PollEntry) & Item->Events)
                LxEpollQueue(Item);
            break;

        case LX_EPOLL_CTL_MOD:
            if (Item == NULL)
            {
                Result = -LX_ENOENT;
                break;
            }
            Item->Events = Event.Events | LX_POLLERR | LX_POLLHUP;
            Item->Data = Event.Data;
            Item->Disabled = FALSE;
            if (LxFilePoll(Target, NULL) & Item->Events)
                LxEpollQueue(Item);
            break;

        case LX_EPOLL_CTL_DEL:
            if (Item == NULL)
            {
                Result = -LX_ENOENT;
                break;
            }
            LxEpollDetach(Item);
            KeAcquireSpinLock(&Target->EpollLock, &Irql);
            RemoveEntryList(&Item->FileLink);
            KeReleaseSpinLock(&Target->EpollLock, Irql);
            if (Target->PollOnly)
                Epoll->PollOnlyItems--;
            RemoveEntryList(&Item->Link);
            LxFree(Item);
            break;

        default:
            Result = -LX_EINVAL;
            break;
    }
    LxEpollUnlock(Epoll);

    LxFileDereference(Target);
    LxFileDereference(EpollFile);
    return Result;
}

static
LONG64
LxEpollWait(
    _In_ PLX_CALL Call,
    _In_ LONG EpollFd,
    _In_ ULONG64 Events,
    _In_ LONG MaxEvents,
    _In_ LONG64 Timeout100ns,
    _In_ ULONG64 SigMask,
    _In_ ULONG64 SigSetSize)
{
    PLX_FILE EpollFile;
    PLX_EPOLL Epoll;
    PLX_EPOLL_EVENT Output;
    LARGE_INTEGER Deadline, Now, Interval;
    LONG64 Result = 0;
    LONG Count = 0;

    if ((MaxEvents <= 0) || (MaxEvents > 65536))
        return -LX_EINVAL;

    EpollFile = LxFdGet(Call->Process, EpollFd);
    if (EpollFile == NULL)
        return -LX_EBADF;
    if (EpollFile->Ops != &LxEpollOps)
    {
        LxFileDereference(EpollFile);
        return -LX_EINVAL;
    }
    Epoll = EpollFile->Object;

    Output = LxAllocPaged(sizeof(LX_EPOLL_EVENT) * MaxEvents);
    if (Output == NULL)
    {
        LxFileDereference(EpollFile);
        return -LX_ENOMEM;
    }

    LxSetWaitMask(Call->Thread, SigMask, SigSetSize, &Result);
    if (Result != 0)
    {
        LxFreePaged(Output);
        LxFileDereference(EpollFile);
        return Result;
    }

    Now.QuadPart = (LONG64)KeQueryInterruptTime();
    Deadline.QuadPart = (Timeout100ns < 0) ? MAXLONGLONG : Now.QuadPart + Timeout100ns;

    for (;;)
    {
        LIST_ENTRY Local;
        NTSTATUS Status;
        KIRQL Irql;

        InitializeListHead(&Local);
        LxEpollLock(Epoll);

        if (Epoll->PollOnlyItems != 0)
        {
            PLIST_ENTRY Entry;

            for (Entry = Epoll->Items.Flink; Entry != &Epoll->Items; Entry = Entry->Flink)
            {
                PLX_EPOLL_ITEM Item = CONTAINING_RECORD(Entry, LX_EPOLL_ITEM, Link);

                if (Item->File->PollOnly && (LxFilePoll(Item->File, NULL) & Item->Events))
                    LxEpollQueue(Item);
            }
        }

        KeAcquireSpinLock(&Epoll->Lock, &Irql);
        while (!IsListEmpty(&Epoll->Ready))
        {
            PLX_EPOLL_ITEM Item = CONTAINING_RECORD(RemoveHeadList(&Epoll->Ready), LX_EPOLL_ITEM, ReadyLink);

            Item->Rearm = FALSE;
            InsertTailList(&Local, &Item->ReadyLink);
        }
        KeClearEvent(&Epoll->Event);
        KeReleaseSpinLock(&Epoll->Lock, Irql);

        while (!IsListEmpty(&Local))
        {
            PLX_EPOLL_ITEM Item = CONTAINING_RECORD(RemoveHeadList(&Local), LX_EPOLL_ITEM, ReadyLink);
            BOOLEAN Requeue = FALSE;
            ULONG Mask = 0;

            if (Count >= MaxEvents)
            {
                Requeue = TRUE;
            }
            else
            {
                Mask = LxFilePoll(Item->File, NULL) & Item->Events;
                if (Mask != 0)
                {
                    Output[Count].Events = Mask;
                    Output[Count].Pad = 0;
                    Output[Count].Data = Item->Data;
                    Count++;

                    if (Item->Events & LX_EPOLLONESHOT)
                        Item->Disabled = TRUE;
                    else if (!(Item->Events & LX_EPOLLET))
                        Requeue = TRUE;
                }
            }

            KeAcquireSpinLock(&Epoll->Lock, &Irql);
            if ((Requeue || Item->Rearm) && !Item->Disabled && !Item->Detached)
            {
                InsertTailList(&Epoll->Ready, &Item->ReadyLink);
                KeSetEvent(&Epoll->Event, IO_NO_INCREMENT, FALSE);
            }
            else
            {
                Item->OnReady = FALSE;
            }
            Item->Rearm = FALSE;
            KeReleaseSpinLock(&Epoll->Lock, Irql);
        }
        LxEpollUnlock(Epoll);

        if (Count != 0)
            break;

        Now.QuadPart = (LONG64)KeQueryInterruptTime();
        if (Now.QuadPart >= Deadline.QuadPart)
            break;

        Interval.QuadPart = (Deadline.QuadPart == MAXLONGLONG) ? MINLONGLONG : -(Deadline.QuadPart - Now.QuadPart);
        if ((Epoll->PollOnlyItems != 0) && ((Interval.QuadPart == MINLONGLONG) || (Interval.QuadPart < LX_POLL_INTERVAL)))
            Interval.QuadPart = LX_POLL_INTERVAL;
        Status = LxWaitForEvent(&Epoll->Event, (Interval.QuadPart == MINLONGLONG) ? NULL : &Interval);
        if (Status == STATUS_ALERTED)
        {
            Result = -LX_EINTR;
            break;
        }
    }

    if ((Result == 0) && (Count != 0))
    {
        Result = LxCopyToUser(Events, Output, sizeof(LX_EPOLL_EVENT) * Count);
        if (Result == 0)
            Result = Count;
    }

    LxFreePaged(Output);
    LxFileDereference(EpollFile);
    return Result;
}

LX_SYSCALL(LxSysEpollPwait)
{
    LONG Timeout = (LONG)Call->Args[3];

    return LxEpollWait(Call, (LONG)Call->Args[0], Call->Args[1], (LONG)Call->Args[2],
                       (Timeout < 0) ? -1 : (LONG64)Timeout * 10000, Call->Args[4], Call->Args[5]);
}

LX_SYSCALL(LxSysEpollPwait2)
{
    LX_TIMESPEC Timeout;
    LONG64 Value = -1;

    if (Call->Args[3] != 0)
    {
        LONG64 Result = LxCopyFromUser(&Timeout, Call->Args[3], sizeof(Timeout));

        if (Result != 0)
            return Result;
        Value = Timeout.Seconds * 10000000 + Timeout.Nanoseconds / 100;
    }
    return LxEpollWait(Call, (LONG)Call->Args[0], Call->Args[1], (LONG)Call->Args[2], Value, Call->Args[4], Call->Args[5]);
}
