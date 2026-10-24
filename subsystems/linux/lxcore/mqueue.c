/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX message queues and the mqueue file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_MQ_PRIO_MAX          32768
#define LX_MQ_HARD_MSGMAX       65536
#define LX_MQ_HARD_MSGSIZEMAX   (16 * 1024 * 1024)
#define LX_MQ_MIN_MSGSIZE       128
#define LX_MQ_NAME_MAX          255
#define LX_MQ_COOKIE_LEN        32
#define LX_MQ_NOTIFY_WOKENUP    1
#define LX_MQ_NOTIFY_REMOVED    2
#define LX_SIGEV_SIGNAL         0
#define LX_SIGEV_NONE           1
#define LX_SIGEV_THREAD         2
#define LX_SI_MESGQ             (-3)
#define LX_MQ_EPOCH_100NS       116444736000000000LL

typedef struct _LX_MQ_ATTR
{
    LONG64 Flags;
    LONG64 MaxMsg;
    LONG64 MsgSize;
    LONG64 CurMsgs;
    LONG64 Reserved[4];
} LX_MQ_ATTR, *PLX_MQ_ATTR;

C_ASSERT(sizeof(LX_MQ_ATTR) == 64);

typedef struct _LX_SIGEVENT
{
    ULONG64 Value;
    LONG Signo;
    LONG Notify;
    UCHAR Pad[48];
} LX_SIGEVENT, *PLX_SIGEVENT;

C_ASSERT(sizeof(LX_SIGEVENT) == 64);

typedef struct _LX_MQ_MESSAGE
{
    LIST_ENTRY Link;
    ULONG Priority;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} LX_MQ_MESSAGE, *PLX_MQ_MESSAGE;

typedef struct _LX_MQ_NAMESPACE
{
    LIST_ENTRY Link;
    PLX_NS Owner;
    LIST_ENTRY Queues;
    ULONG QueueCount;
    ULONG Limits[LxMqLimitCount];
} LX_MQ_NAMESPACE, *PLX_MQ_NAMESPACE;

typedef struct _LX_MQUEUE
{
    volatile LONG RefCount;
    LIST_ENTRY Link;
    PLX_MQ_NAMESPACE Namespace;
    CHAR Name[LX_MQ_NAME_MAX + 1];
    ULONG64 Ino;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    LONG64 Atime;
    LONG64 Mtime;
    LONG64 Ctime;
    KEVENT Lock;
    LIST_ENTRY Messages;
    ULONG MaxMsg;
    ULONG MsgSize;
    ULONG CurMsgs;
    ULONG64 Bytes;
    ULONG Receivers;
    LX_POLL_HEAD Poll;
    PLX_PROCESS NotifyOwner;
    LX_SIGEVENT Notify;
    PLX_FILE NotifySocket;
    UCHAR Cookie[LX_MQ_COOKIE_LEN];
} LX_MQUEUE, *PLX_MQUEUE;

static const ULONG LxMqDefaultLimits[LxMqLimitCount] = { 10, 10, 8192, 8192, 256 };
static LIST_ENTRY LxMqNamespaces;
static FAST_MUTEX LxMqLock;
static volatile LONG64 LxMqInode = 0x500000;
static const LX_FILE_OPS LxMqOps;

VOID
LxMqInitialize(VOID)
{
    InitializeListHead(&LxMqNamespaces);
    ExInitializeFastMutex(&LxMqLock);
}

static
LONG64
LxMqNow(VOID)
{
    LX_TIMESPEC Now;

    LxQueryClock(LX_CLOCK_REALTIME, &Now);
    return Now.Seconds;
}

static
BOOLEAN
LxMqPrivileged(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Capability)
{
    return LxCapable(Process, Capability);
}

static
PLX_MQ_NAMESPACE
LxMqNamespace(
    _In_ PLX_NS Owner,
    _In_ BOOLEAN Create)
{
    PLX_MQ_NAMESPACE Namespace;
    PLIST_ENTRY Entry;

    for (Entry = LxMqNamespaces.Flink; Entry != &LxMqNamespaces; Entry = Entry->Flink)
    {
        Namespace = CONTAINING_RECORD(Entry, LX_MQ_NAMESPACE, Link);
        if (Namespace->Owner == Owner)
            return Namespace;
    }
    if (!Create)
        return NULL;
    Namespace = LxAlloc(sizeof(*Namespace));
    if (Namespace == NULL)
        return NULL;
    Namespace->Owner = Owner;
    InitializeListHead(&Namespace->Queues);
    RtlCopyMemory(Namespace->Limits, LxMqDefaultLimits, sizeof(Namespace->Limits));
    InsertTailList(&LxMqNamespaces, &Namespace->Link);
    return Namespace;
}

static
PLX_MQUEUE
LxMqFindLocked(
    _In_ PLX_MQ_NAMESPACE Namespace,
    _In_ PCSTR Name)
{
    PLIST_ENTRY Entry;

    for (Entry = Namespace->Queues.Flink; Entry != &Namespace->Queues; Entry = Entry->Flink)
    {
        PLX_MQUEUE Queue = CONTAINING_RECORD(Entry, LX_MQUEUE, Link);

        if (strcmp(Queue->Name, Name) == 0)
            return Queue;
    }
    return NULL;
}

static
VOID
LxMqReference(
    _In_ PLX_MQUEUE Queue)
{
    InterlockedIncrement(&Queue->RefCount);
}

static
VOID
LxMqSendCookie(
    _In_ PLX_FILE Socket,
    _In_reads_bytes_(LX_MQ_COOKIE_LEN) PUCHAR Cookie,
    _In_ UCHAR Code)
{
    UCHAR Data[LX_MQ_COOKIE_LEN];

    RtlCopyMemory(Data, Cookie, sizeof(Data));
    Data[LX_MQ_COOKIE_LEN - 1] = Code;
    LxNetlinkDeliver(Socket, Data, sizeof(Data));
}

static
VOID
LxMqDropNotification(
    _In_ PLX_MQUEUE Queue,
    _In_ BOOLEAN Removed)
{
    PLX_PROCESS Owner = Queue->NotifyOwner;
    PLX_FILE Socket = Queue->NotifySocket;

    Queue->NotifyOwner = NULL;
    Queue->NotifySocket = NULL;
    if ((Socket != NULL) && Removed)
        LxMqSendCookie(Socket, Queue->Cookie, LX_MQ_NOTIFY_REMOVED);
    if (Socket != NULL)
        LxFileDereference(Socket);
    if (Owner != NULL)
        LxProcessDereference(Owner);
}

static
VOID
LxMqDereference(
    _In_ PLX_MQUEUE Queue)
{
    if (InterlockedDecrement(&Queue->RefCount) != 0)
        return;
    while (!IsListEmpty(&Queue->Messages))
        LxFree(CONTAINING_RECORD(RemoveHeadList(&Queue->Messages), LX_MQ_MESSAGE, Link));
    LxMqDropNotification(Queue, TRUE);
    LxFree(Queue);
}

VOID
LxMqNamespaceCleanup(
    _In_ PLX_NS Ns)
{
    PLX_MQ_NAMESPACE Namespace;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(Ns, FALSE);
    if (Namespace != NULL)
        RemoveEntryList(&Namespace->Link);
    ExReleaseFastMutex(&LxMqLock);
    if (Namespace == NULL)
        return;
    while (!IsListEmpty(&Namespace->Queues))
    {
        PLX_MQUEUE Queue = CONTAINING_RECORD(RemoveHeadList(&Namespace->Queues), LX_MQUEUE, Link);

        InitializeListHead(&Queue->Link);
        Queue->Namespace = NULL;
        LxMqDereference(Queue);
    }
    LxFree(Namespace);
}

static
LONG
LxMqCheckName(
    _In_ PCSTR Name)
{
    SIZE_T Length = strlen(Name);

    if (Length == 0)
        return -LX_ENOENT;
    if (Length > LX_MQ_NAME_MAX)
        return -LX_ENAMETOOLONG;
    if ((strchr(Name, '/') != NULL) || (strcmp(Name, ".") == 0) || (strcmp(Name, "..") == 0))
        return -LX_EACCES;
    return 0;
}

static
LONG
LxMqAccess(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MQUEUE Queue,
    _In_ ULONG Mask)
{
    ULONG Bits;

    if (LxMqPrivileged(Process, LX_CAP_DAC_OVERRIDE))
        return 0;
    if (Process->Fsuid == Queue->Uid)
        Bits = (Queue->Mode >> 6) & 7;
    else if (Process->Fsgid == Queue->Gid)
        Bits = (Queue->Mode >> 3) & 7;
    else
        Bits = Queue->Mode & 7;
    return ((Bits & Mask) == Mask) ? 0 : -LX_EACCES;
}

static
PLX_FILE
LxMqFileCreate(
    _In_ PLX_MQUEUE Queue,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    PLX_FILE File = LxFileAlloc(&LxMqOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFREG);

    if (File == NULL)
        return NULL;
    File->Path = LxStrDup(Path);
    if (File->Path == NULL)
    {
        LxFileDereference(File);
        return NULL;
    }
    LxMqReference(Queue);
    File->Object = Queue;
    File->Ino = Queue->Ino;
    return File;
}

LONG
LxMqOpenNamed(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NS IpcNs,
    _In_ PCSTR Name,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _In_opt_ PLX_MQ_ATTR_LIMITS Attr,
    _In_ PCSTR Path,
    _Out_ PLX_FILE *Result)
{
    ULONG Access = Flags & LX_O_ACCMODE;
    PLX_MQ_NAMESPACE Namespace;
    PLX_MQUEUE Queue, Created = NULL;
    LONG Error;

    *Result = NULL;
    if (Access == LX_O_ACCMODE)
        return -LX_EINVAL;
    Error = LxMqCheckName(Name);
    if (Error != 0)
        return Error;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(IpcNs, (Flags & LX_O_CREAT) != 0);
    Queue = (Namespace != NULL) ? LxMqFindLocked(Namespace, Name) : NULL;
    if (Queue != NULL)
    {
        if ((Flags & (LX_O_CREAT | LX_O_EXCL)) == (LX_O_CREAT | LX_O_EXCL))
            Error = -LX_EEXIST;
        else
            Error = LxMqAccess(Process, Queue, (Access == LX_O_RDONLY) ? 4 : ((Access == LX_O_WRONLY) ? 2 : 6));
    }
    else if (!(Flags & LX_O_CREAT))
    {
        Error = -LX_ENOENT;
    }
    else if (Namespace == NULL)
    {
        Error = -LX_ENOMEM;
    }
    else
    {
        ULONG MaxMsg = Namespace->Limits[LxMqMsgDefault], MsgSize = Namespace->Limits[LxMqMsgSizeDefault];
        BOOLEAN Resource = LxMqPrivileged(Process, LX_CAP_SYS_RESOURCE);

        if (Attr != NULL)
        {
            if ((Attr->MaxMsg <= 0) || (Attr->MsgSize <= 0))
                Error = -LX_EINVAL;
            else if (!Resource && ((Attr->MaxMsg > Namespace->Limits[LxMqMsgMax]) ||
                                   (Attr->MsgSize > Namespace->Limits[LxMqMsgSizeMax])))
                Error = -LX_EINVAL;
            else if ((Attr->MaxMsg > LX_MQ_HARD_MSGMAX) || (Attr->MsgSize > LX_MQ_HARD_MSGSIZEMAX))
                Error = -LX_EINVAL;
            MaxMsg = (ULONG)Attr->MaxMsg;
            MsgSize = (ULONG)Attr->MsgSize;
        }
        if ((Error == 0) && !Resource && (Namespace->QueueCount >= Namespace->Limits[LxMqQueuesMax]))
            Error = -LX_ENOSPC;
        if (Error == 0)
        {
            Created = LxAlloc(sizeof(*Created));
            if (Created == NULL)
                Error = -LX_ENOMEM;
        }
        if (Error == 0)
        {
            LONG64 Now = LxMqNow();

            Created->RefCount = 1;
            Created->Namespace = Namespace;
            RtlStringCbCopyA(Created->Name, sizeof(Created->Name), Name);
            Created->Ino = (ULONG64)InterlockedIncrement64(&LxMqInode);
            Created->Mode = Mode & ~Process->Umask & 07777;
            Created->Uid = Process->Fsuid;
            Created->Gid = Process->Fsgid;
            Created->Atime = Created->Mtime = Created->Ctime = Now;
            KeInitializeEvent(&Created->Lock, SynchronizationEvent, TRUE);
            InitializeListHead(&Created->Messages);
            LxPollHeadInit(&Created->Poll);
            Created->MaxMsg = MaxMsg;
            Created->MsgSize = MsgSize;
            InsertTailList(&Namespace->Queues, &Created->Link);
            Namespace->QueueCount++;
            Queue = Created;
        }
    }
    if (Error == 0)
    {
        *Result = LxMqFileCreate(Queue, Flags, Path);
        if (*Result == NULL)
            Error = -LX_ENOMEM;
    }
    ExReleaseFastMutex(&LxMqLock);
    return Error;
}

LONG
LxMqUnlinkNamed(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NS IpcNs,
    _In_ PCSTR Name)
{
    PLX_MQ_NAMESPACE Namespace;
    PLX_MQUEUE Queue;
    LONG Error = LxMqCheckName(Name);

    if (Error != 0)
        return Error;
    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(IpcNs, FALSE);
    Queue = (Namespace != NULL) ? LxMqFindLocked(Namespace, Name) : NULL;
    if (Queue == NULL)
    {
        Error = -LX_ENOENT;
    }
    else if ((Process->Fsuid != Queue->Uid) && !LxMqPrivileged(Process, LX_CAP_FOWNER))
    {
        Error = -LX_EACCES;
    }
    else
    {
        RemoveEntryList(&Queue->Link);
        InitializeListHead(&Queue->Link);
        Queue->Namespace = NULL;
        Namespace->QueueCount--;
    }
    ExReleaseFastMutex(&LxMqLock);
    if ((Error == 0) && (Queue != NULL))
        LxMqDereference(Queue);
    return Error;
}

BOOLEAN
LxMqQuery(
    _In_ PLX_NS IpcNs,
    _In_ PCSTR Name,
    _Out_opt_ PLX_STAT Stat)
{
    PLX_MQ_NAMESPACE Namespace;
    PLX_MQUEUE Queue;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(IpcNs, FALSE);
    Queue = (Namespace != NULL) ? LxMqFindLocked(Namespace, Name) : NULL;
    if ((Queue != NULL) && (Stat != NULL))
    {
        Stat->Mode = LX_S_IFREG | Queue->Mode;
        Stat->Uid = Queue->Uid;
        Stat->Gid = Queue->Gid;
        Stat->Ino = Queue->Ino;
        Stat->Size = (LONG64)Queue->Bytes;
        Stat->Atime.Seconds = Queue->Atime;
        Stat->Mtime.Seconds = Queue->Mtime;
        Stat->Ctime.Seconds = Queue->Ctime;
    }
    ExReleaseFastMutex(&LxMqLock);
    return Queue != NULL;
}

VOID
LxMqList(
    _In_ PLX_NS IpcNs,
    _In_ VOID (*Add)(_In_ PVOID Context, _In_ PCSTR Name),
    _In_ PVOID Context)
{
    PLX_MQ_NAMESPACE Namespace;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(IpcNs, FALSE);
    if (Namespace != NULL)
    {
        for (Entry = Namespace->Queues.Flink; Entry != &Namespace->Queues; Entry = Entry->Flink)
            Add(Context, CONTAINING_RECORD(Entry, LX_MQUEUE, Link)->Name);
    }
    ExReleaseFastMutex(&LxMqLock);
}

ULONG
LxMqGetLimit(
    _In_ PLX_PROCESS Process,
    _In_ LX_MQ_LIMIT Which)
{
    PLX_MQ_NAMESPACE Namespace;
    ULONG Value;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(LxProcessNs(Process, LxNsIpc), FALSE);
    Value = (Namespace != NULL) ? Namespace->Limits[Which] : LxMqDefaultLimits[Which];
    ExReleaseFastMutex(&LxMqLock);
    return Value;
}

LONG
LxMqSetLimit(
    _In_ PLX_PROCESS Process,
    _In_ LX_MQ_LIMIT Which,
    _In_ ULONG Value)
{
    PLX_MQ_NAMESPACE Namespace;
    LONG Error = 0;

    ExAcquireFastMutex(&LxMqLock);
    Namespace = LxMqNamespace(LxProcessNs(Process, LxNsIpc), TRUE);
    if (Namespace == NULL)
    {
        Error = -LX_ENOMEM;
    }
    else
    {
        switch (Which)
        {
            case LxMqMsgMax:
                Error = ((Value < 1) || (Value > LX_MQ_HARD_MSGMAX)) ? -LX_EINVAL : 0;
                break;
            case LxMqMsgDefault:
                Error = ((Value < 1) || (Value > Namespace->Limits[LxMqMsgMax])) ? -LX_EINVAL : 0;
                break;
            case LxMqMsgSizeMax:
                Error = ((Value < LX_MQ_MIN_MSGSIZE) || (Value > LX_MQ_HARD_MSGSIZEMAX)) ? -LX_EINVAL : 0;
                break;
            case LxMqMsgSizeDefault:
                Error = ((Value < LX_MQ_MIN_MSGSIZE) || (Value > Namespace->Limits[LxMqMsgSizeMax])) ? -LX_EINVAL : 0;
                break;
            default:
                Error = (Value > MAXLONG) ? -LX_EINVAL : 0;
                break;
        }
        if (Error == 0)
            Namespace->Limits[Which] = Value;
    }
    ExReleaseFastMutex(&LxMqLock);
    return Error;
}

static
PLX_MQUEUE
LxMqFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _Out_ PLX_FILE *File)
{
    *File = LxFdGet(Process, Fd);
    if (*File == NULL)
        return NULL;
    if ((*File)->Ops != &LxMqOps)
    {
        LxFileDereference(*File);
        *File = NULL;
        return NULL;
    }
    return (*File)->Object;
}

static
LONG
LxMqDeadline(
    _In_ ULONG64 User,
    _Out_ PLARGE_INTEGER Deadline)
{
    LX_TIMESPEC Timeout;
    LONG64 Result = LxCopyFromUser(&Timeout, User, sizeof(Timeout));

    if (Result != 0)
        return (LONG)Result;
    if ((Timeout.Seconds < 0) || (Timeout.Nanoseconds < 0) || (Timeout.Nanoseconds >= 1000000000))
        return -LX_EINVAL;
    Deadline->QuadPart = Timeout.Seconds * 10000000LL + Timeout.Nanoseconds / 100 + LX_MQ_EPOCH_100NS;
    return 0;
}

static
BOOLEAN
LxMqSpaceReady(
    _In_ PVOID Context)
{
    PLX_MQUEUE Queue = Context;

    return Queue->CurMsgs < Queue->MaxMsg;
}

static
BOOLEAN
LxMqDataReady(
    _In_ PVOID Context)
{
    PLX_MQUEUE Queue = Context;

    return Queue->CurMsgs != 0;
}

static
VOID
LxMqDeliver(
    _In_ PLX_PROCESS Sender,
    _In_ PLX_PROCESS Owner,
    _In_ PLX_SIGEVENT Notify,
    _In_opt_ PLX_FILE Socket,
    _In_reads_bytes_(LX_MQ_COOKIE_LEN) PUCHAR Cookie)
{
    LX_SIGINFO Info;

    if (Notify->Notify == LX_SIGEV_THREAD)
    {
        if (Socket != NULL)
            LxMqSendCookie(Socket, Cookie, LX_MQ_NOTIFY_WOKENUP);
        return;
    }
    if (Notify->Notify != LX_SIGEV_SIGNAL)
        return;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Signo = Notify->Signo;
    Info.Code = LX_SI_MESGQ;
    Info.Child.Pid = (Owner->PidNs != NULL) ? (LONG)LxGlobalToNrIn(Owner->PidNs, Sender->Pid) : (LONG)Sender->Pid;
    Info.Child.Uid = Sender->Uid[0];
    RtlCopyMemory(&Info.Raw[8], &Notify->Value, sizeof(Notify->Value));
    LxSignalSend(Owner, NULL, Notify->Signo, &Info);
}

LX_SYSCALL(LxSysMqOpen)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Flags = (ULONG)Call->Args[1];
    LX_MQ_ATTR_LIMITS Limits, *Attr = NULL;
    LX_MQ_ATTR UserAttr;
    PSTR Name, Path;
    PLX_FILE File;
    LONG64 Result;
    LONG Error;

    Result = LxCopyStringFromUser(Call->Args[0], &Name, LX_PATH_MAX - 1);
    if (Result != 0)
        return Result;
    if ((Flags & LX_O_CREAT) && (Call->Args[3] != 0))
    {
        Result = LxCopyFromUser(&UserAttr, Call->Args[3], sizeof(UserAttr));
        if (Result != 0)
        {
            LxFree(Name);
            return Result;
        }
        Limits.MaxMsg = UserAttr.MaxMsg;
        Limits.MsgSize = UserAttr.MsgSize;
        Attr = &Limits;
    }
    Path = LxStrPrintf("/%s", Name);
    if (Path == NULL)
    {
        LxFree(Name);
        return -LX_ENOMEM;
    }
    Error = LxMqOpenNamed(Process, LxProcessNs(Process, LxNsIpc), Name, Flags, (ULONG)Call->Args[2], Attr, Path, &File);
    LxFree(Path);
    LxFree(Name);
    if (Error != 0)
        return Error;
    Result = LxFdInstall(Process, File, 0, TRUE);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMqUnlink)
{
    PSTR Name;
    LONG64 Result = LxCopyStringFromUser(Call->Args[0], &Name, LX_PATH_MAX - 1);

    if (Result != 0)
        return Result;
    Result = LxMqUnlinkNamed(Call->Process, LxProcessNs(Call->Process, LxNsIpc), Name);
    LxFree(Name);
    return Result;
}

LX_SYSCALL(LxSysMqTimedsend)
{
    ULONG64 Length = Call->Args[2];
    ULONG Priority = (ULONG)Call->Args[3];
    LARGE_INTEGER Deadline;
    PLX_MQ_MESSAGE Message;
    PLX_PROCESS Owner = NULL;
    PLX_FILE File, Socket = NULL;
    PLX_MQUEUE Queue;
    LX_SIGEVENT Notify;
    UCHAR Cookie[LX_MQ_COOKIE_LEN];
    PLIST_ENTRY Entry;
    LONG64 Result = 0;

    if (Call->Args[3] >= LX_MQ_PRIO_MAX)
        return -LX_EINVAL;
    if (Call->Args[4] != 0)
    {
        Result = LxMqDeadline(Call->Args[4], &Deadline);
        if (Result != 0)
            return Result;
    }
    Queue = LxMqFromFd(Call->Process, (LONG)Call->Args[0], &File);
    if (Queue == NULL)
        return -LX_EBADF;
    if ((File->Flags & LX_O_ACCMODE) == LX_O_RDONLY)
    {
        LxFileDereference(File);
        return -LX_EBADF;
    }
    if (Length > Queue->MsgSize)
    {
        LxFileDereference(File);
        return -LX_EMSGSIZE;
    }
    Message = LxAlloc(FIELD_OFFSET(LX_MQ_MESSAGE, Data) + (SIZE_T)Length + 1);
    if (Message == NULL)
    {
        LxFileDereference(File);
        return -LX_ENOMEM;
    }
    Message->Priority = Priority;
    Message->Length = (ULONG)Length;
    Result = LxCopyFromUser(Message->Data, Call->Args[1], (SIZE_T)Length);
    if (Result != 0)
    {
        LxFree(Message);
        LxFileDereference(File);
        return Result;
    }

    for (;;)
    {
        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        if (Queue->CurMsgs < Queue->MaxMsg)
        {
            for (Entry = Queue->Messages.Flink; Entry != &Queue->Messages; Entry = Entry->Flink)
            {
                if (CONTAINING_RECORD(Entry, LX_MQ_MESSAGE, Link)->Priority < Priority)
                    break;
            }
            InsertTailList(Entry, &Message->Link);
            Queue->CurMsgs++;
            Queue->Bytes += Message->Length;
            Queue->Mtime = Queue->Ctime = LxMqNow();
            if ((Queue->CurMsgs == 1) && (Queue->Receivers == 0) && (Queue->NotifyOwner != NULL))
            {
                Owner = Queue->NotifyOwner;
                Socket = Queue->NotifySocket;
                Notify = Queue->Notify;
                RtlCopyMemory(Cookie, Queue->Cookie, sizeof(Cookie));
                Queue->NotifyOwner = NULL;
                Queue->NotifySocket = NULL;
            }
            KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
            LxPollWake(&Queue->Poll, LX_POLLIN | LX_POLLRDNORM);
            break;
        }
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
        if (File->Flags & LX_O_NONBLOCK)
        {
            Result = -LX_EAGAIN;
            break;
        }
        Result = LxWaitHead(&Queue->Poll, LxMqSpaceReady, Queue, (Call->Args[4] != 0) ? &Deadline : NULL);
        if (Result == -LX_EINTR)
        {
            Result = -LX_ERESTARTSYS;
            break;
        }
        if (Result == -LX_EAGAIN)
        {
            Result = -LX_ETIMEDOUT;
            break;
        }
        Result = 0;
    }
    if (Result != 0)
        LxFree(Message);
    if (Owner != NULL)
    {
        LxMqDeliver(Call->Process, Owner, &Notify, Socket, Cookie);
        if (Socket != NULL)
            LxFileDereference(Socket);
        LxProcessDereference(Owner);
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMqTimedreceive)
{
    ULONG64 Length = Call->Args[2];
    LARGE_INTEGER Deadline;
    PLX_MQ_MESSAGE Message = NULL;
    PLX_MQUEUE Queue;
    PLX_FILE File;
    LONG64 Result = 0;

    if (Call->Args[4] != 0)
    {
        Result = LxMqDeadline(Call->Args[4], &Deadline);
        if (Result != 0)
            return Result;
    }
    Queue = LxMqFromFd(Call->Process, (LONG)Call->Args[0], &File);
    if (Queue == NULL)
        return -LX_EBADF;
    if ((File->Flags & LX_O_ACCMODE) == LX_O_WRONLY)
    {
        LxFileDereference(File);
        return -LX_EBADF;
    }
    if (Length < Queue->MsgSize)
    {
        LxFileDereference(File);
        return -LX_EMSGSIZE;
    }

    for (;;)
    {
        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        if (!IsListEmpty(&Queue->Messages))
        {
            Message = CONTAINING_RECORD(RemoveHeadList(&Queue->Messages), LX_MQ_MESSAGE, Link);
            Queue->CurMsgs--;
            Queue->Bytes -= Message->Length;
            Queue->Atime = Queue->Ctime = LxMqNow();
            KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
            LxPollWake(&Queue->Poll, LX_POLLOUT | LX_POLLWRNORM);
            break;
        }
        if (File->Flags & LX_O_NONBLOCK)
        {
            KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
            Result = -LX_EAGAIN;
            break;
        }
        Queue->Receivers++;
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
        Result = LxWaitHead(&Queue->Poll, LxMqDataReady, Queue, (Call->Args[4] != 0) ? &Deadline : NULL);
        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        Queue->Receivers--;
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
        if (Result == -LX_EINTR)
        {
            Result = -LX_ERESTARTSYS;
            break;
        }
        if (Result == -LX_EAGAIN)
        {
            Result = -LX_ETIMEDOUT;
            break;
        }
        Result = 0;
    }
    if (Message != NULL)
    {
        Result = LxCopyToUser(Call->Args[1], Message->Data, Message->Length);
        if ((Result == 0) && (Call->Args[3] != 0))
            Result = LxCopyToUser(Call->Args[3], &Message->Priority, sizeof(ULONG));
        if (Result == 0)
            Result = Message->Length;
        LxFree(Message);
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMqNotify)
{
    PLX_PROCESS Process = Call->Process;
    PLX_FILE File, Socket = NULL;
    UCHAR Cookie[LX_MQ_COOKIE_LEN];
    LX_SIGEVENT Notify;
    PLX_MQUEUE Queue;
    LONG64 Result = 0;

    Queue = LxMqFromFd(Process, (LONG)Call->Args[0], &File);
    if (Queue == NULL)
        return -LX_EBADF;
    if (Call->Args[1] == 0)
    {
        PLX_PROCESS Owner = NULL;

        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        if (Queue->NotifyOwner == Process)
        {
            Owner = Queue->NotifyOwner;
            Socket = Queue->NotifySocket;
            RtlCopyMemory(Cookie, Queue->Cookie, sizeof(Cookie));
            Queue->NotifyOwner = NULL;
            Queue->NotifySocket = NULL;
        }
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
        if (Socket != NULL)
        {
            LxMqSendCookie(Socket, Cookie, LX_MQ_NOTIFY_REMOVED);
            LxFileDereference(Socket);
        }
        if (Owner != NULL)
            LxProcessDereference(Owner);
        LxFileDereference(File);
        return 0;
    }

    Result = LxCopyFromUser(&Notify, Call->Args[1], sizeof(Notify));
    if ((Result == 0) && (Notify.Notify != LX_SIGEV_NONE) && (Notify.Notify != LX_SIGEV_SIGNAL) &&
        (Notify.Notify != LX_SIGEV_THREAD))
    {
        Result = -LX_EINVAL;
    }
    if ((Result == 0) && (Notify.Notify == LX_SIGEV_SIGNAL) && ((Notify.Signo < 1) || (Notify.Signo > LX_NSIG)))
        Result = -LX_EINVAL;
    if ((Result == 0) && (Notify.Notify == LX_SIGEV_THREAD))
    {
        Result = LxCopyFromUser(Cookie, Notify.Value, sizeof(Cookie));
        if (Result == 0)
        {
            Socket = LxFdGet(Process, Notify.Signo);
            if (Socket == NULL)
                Result = -LX_EBADF;
            else if (!LxNetlinkIsFile(Socket))
                Result = (Socket->Type == LX_S_IFSOCK) ? -LX_ECONNREFUSED : -LX_ENOTSOCK;
        }
    }
    if (Result == 0)
    {
        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        if (Queue->NotifyOwner != NULL)
        {
            Result = -LX_EBUSY;
        }
        else
        {
            LxProcessReference(Process);
            Queue->NotifyOwner = Process;
            Queue->Notify = Notify;
            Queue->NotifySocket = Socket;
            RtlCopyMemory(Queue->Cookie, Cookie, sizeof(Cookie));
            Socket = NULL;
        }
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
    }
    if (Socket != NULL)
        LxFileDereference(Socket);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMqGetsetattr)
{
    LX_MQ_ATTR Attr;
    PLX_MQUEUE Queue;
    PLX_FILE File;
    LONG64 Result = 0;

    Queue = LxMqFromFd(Call->Process, (LONG)Call->Args[0], &File);
    if (Queue == NULL)
        return -LX_EBADF;
    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(&Attr, Call->Args[1], sizeof(Attr));
        if ((Result == 0) && (Attr.Flags & ~(LONG64)LX_O_NONBLOCK))
            Result = -LX_EINVAL;
    }
    if ((Result == 0) && (Call->Args[2] != 0))
    {
        LX_MQ_ATTR Old;

        RtlZeroMemory(&Old, sizeof(Old));
        KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
        Old.Flags = File->Flags & LX_O_NONBLOCK;
        Old.MaxMsg = Queue->MaxMsg;
        Old.MsgSize = Queue->MsgSize;
        Old.CurMsgs = Queue->CurMsgs;
        KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
        Result = LxCopyToUser(Call->Args[2], &Old, sizeof(Old));
    }
    if ((Result == 0) && (Call->Args[1] != 0))
    {
        if (Attr.Flags & LX_O_NONBLOCK)
            File->Flags |= LX_O_NONBLOCK;
        else
            File->Flags &= ~LX_O_NONBLOCK;
    }
    LxFileDereference(File);
    return Result;
}

static
LONG64
LxMqRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_MQUEUE Queue = File->Object;
    LONG64 Position = (Offset != NULL) ? *Offset : File->Position;
    CHAR Text[128];
    SIZE_T Size;
    LONG Pid = 0, Notify = 0, Signo = 0;
    PLX_THREAD Current = LxCurrentThread();
    LONG64 Result;

    KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
    if (Queue->NotifyOwner != NULL)
    {
        Notify = Queue->Notify.Notify;
        Signo = (Notify == LX_SIGEV_SIGNAL) ? Queue->Notify.Signo : 0;
        Pid = (Current != NULL) ? (LONG)LxPidNr(Current->Process, Queue->NotifyOwner) : (LONG)Queue->NotifyOwner->Pid;
    }
    RtlStringCbPrintfA(Text, sizeof(Text), "QSIZE:%-10llu NOTIFY:%-5ld SIGNO:%-5ld NOTIFY_PID:%-6ld\n", Queue->Bytes, Notify,
                       Signo, Pid);
    Queue->Atime = LxMqNow();
    KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
    Size = strlen(Text);
    if ((Position < 0) || ((SIZE_T)Position >= Size))
        return 0;
    Length = min(Length, (ULONG64)(Size - (SIZE_T)Position));
    Result = LxCopyToUser(Buffer, Text + Position, (SIZE_T)Length);
    if (Result != 0)
        return Result;
    if (Offset != NULL)
        *Offset += (LONG64)Length;
    else
        File->Position += (LONG64)Length;
    return (LONG64)Length;
}

static
ULONG
LxMqPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_MQUEUE Queue = File->Object;
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Queue->Poll, Entry);
    KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
    if (Queue->CurMsgs != 0)
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Queue->CurMsgs < Queue->MaxMsg)
        Mask |= LX_POLLOUT | LX_POLLWRNORM;
    KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
    return Mask;
}

static
LONG
LxMqStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_MQUEUE Queue = File->Object;

    RtlZeroMemory(Stat, sizeof(*Stat));
    KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
    Stat->Mode = LX_S_IFREG | Queue->Mode;
    Stat->Nlink = (Queue->Namespace != NULL) ? 1 : 0;
    Stat->Uid = Queue->Uid;
    Stat->Gid = Queue->Gid;
    Stat->Ino = Queue->Ino;
    Stat->Size = (LONG64)Queue->Bytes;
    Stat->BlkSize = 4096;
    Stat->Atime.Seconds = Queue->Atime;
    Stat->Mtime.Seconds = Queue->Mtime;
    Stat->Ctime.Seconds = Queue->Ctime;
    KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
    return 0;
}

static
VOID
LxMqRelease(
    _In_ PLX_FILE File)
{
    PLX_MQUEUE Queue = File->Object;
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Owner = NULL;
    PLX_FILE Socket = NULL;
    UCHAR Cookie[LX_MQ_COOKIE_LEN];

    if (Queue == NULL)
        return;
    KeWaitForSingleObject(&Queue->Lock, Executive, KernelMode, FALSE, NULL);
    if ((Current != NULL) && (Queue->NotifyOwner == Current->Process))
    {
        Owner = Queue->NotifyOwner;
        Socket = Queue->NotifySocket;
        RtlCopyMemory(Cookie, Queue->Cookie, sizeof(Cookie));
        Queue->NotifyOwner = NULL;
        Queue->NotifySocket = NULL;
    }
    KeSetEvent(&Queue->Lock, IO_NO_INCREMENT, FALSE);
    if (Socket != NULL)
    {
        LxMqSendCookie(Socket, Cookie, LX_MQ_NOTIFY_REMOVED);
        LxFileDereference(Socket);
    }
    if (Owner != NULL)
        LxProcessDereference(Owner);
    File->Object = NULL;
    LxMqDereference(Queue);
}

static const LX_FILE_OPS LxMqOps =
{
    LxMqRead,
    NULL,
    NULL,
    LxMqPoll,
    LxMqStat,
    NULL,
    NULL,
    NULL,
    LxMqRelease
};
