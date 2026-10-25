/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AF_UNIX sockets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define LX_UNIX_DEFAULT_BUFFER  212992
#define LX_UNIX_MAX_DGRAM       212992
#define LX_UNIX_STREAM_CHUNK    65536
#define LX_UNIX_MAX_FDS         253
#define LX_UNIX_DGRAM_QLEN      512
#define LX_SCM_PIDFD            4
#define LX_SO_PASSPIDFD         76

#define LX_UNIX_UNCONNECTED     0
#define LX_UNIX_LISTENING       1
#define LX_UNIX_CONNECTED       2

typedef struct _LX_UNIX_MSG
{
    LIST_ENTRY Link;
    ULONG Length;
    ULONG Offset;
    PLX_FILE *Fds;
    ULONG FdCount;
    LX_UCRED Cred;
    USHORT AddressLength;
    UCHAR Address[110];
    UCHAR Data[1];
} LX_UNIX_MSG, *PLX_UNIX_MSG;

typedef struct _LX_UNIX_SOCKET LX_UNIX_SOCKET, *PLX_UNIX_SOCKET;

struct _LX_UNIX_SOCKET
{
    volatile LONG RefCount;
    KEVENT Lock;
    ULONG Type;
    ULONG State;
    PLX_UNIX_SOCKET Peer;
    LIST_ENTRY RecvQueue;
    ULONG RecvBytes;
    ULONG RecvCount;
    ULONG RecvLimit;
    ULONG SendLimit;
    LIST_ENTRY Backlog;
    LIST_ENTRY BacklogLink;
    ULONG BacklogCount;
    ULONG BacklogMax;
    LX_POLL_HEAD Poll;
    BOOLEAN ShutRead;
    BOOLEAN ShutWrite;
    BOOLEAN PeerClosed;
    BOOLEAN PassCred;
    BOOLEAN PeerCredValid;
    BOOLEAN Bound;
    BOOLEAN Closed;
    LX_UCRED Cred;
    LX_UCRED PeerCred;
    USHORT AddressLength;
    UCHAR Address[110];
    PLX_INSTANCE Instance;
    PLX_NS NetNs;
    PLX_MOUNT BoundMount;
    LONG64 BoundId;
    LIST_ENTRY BindLink;
    LONG Error;
    LONG64 RecvTimeout;
    LONG64 SendTimeout;
    ULONG64 Ino;
    LIST_ENTRY AllLink;
};

static LIST_ENTRY LxUnixBound = { &LxUnixBound, &LxUnixBound };
static FAST_MUTEX LxUnixBindLock;
static LIST_ENTRY LxUnixAll = { &LxUnixAll, &LxUnixAll };
static FAST_MUTEX LxUnixAllLock;
static volatile LONG64 LxSocketInode = 0x300000;

static
VOID
LxUnixLock(
    _In_ PLX_UNIX_SOCKET Socket)
{
    KeWaitForSingleObject(&Socket->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxUnixUnlock(
    _In_ PLX_UNIX_SOCKET Socket)
{
    KeSetEvent(&Socket->Lock, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxUnixFreeMessage(
    _In_ PLX_UNIX_MSG Message)
{
    ULONG Index;

    for (Index = 0; Index < Message->FdCount; Index++)
    {
        if (Message->Fds[Index] != NULL)
            LxFileDereference(Message->Fds[Index]);
    }
    LxFree(Message->Fds);
    LxFree(Message);
}

static
VOID
LxUnixReference(
    _In_ PLX_UNIX_SOCKET Socket)
{
    InterlockedIncrement(&Socket->RefCount);
}

static
VOID
LxUnixUnlist(
    _In_ PLX_UNIX_SOCKET Socket)
{
    ExAcquireFastMutex(&LxUnixAllLock);
    if (!IsListEmpty(&Socket->AllLink))
    {
        RemoveEntryList(&Socket->AllLink);
        InitializeListHead(&Socket->AllLink);
    }
    ExReleaseFastMutex(&LxUnixAllLock);
}

static
VOID
LxUnixDereference(
    _In_ PLX_UNIX_SOCKET Socket)
{
    if (InterlockedDecrement(&Socket->RefCount) != 0)
        return;

    LxUnixUnlist(Socket);
    while (!IsListEmpty(&Socket->RecvQueue))
        LxUnixFreeMessage(CONTAINING_RECORD(RemoveHeadList(&Socket->RecvQueue), LX_UNIX_MSG, Link));
    if (Socket->Peer != NULL)
        LxUnixDereference(Socket->Peer);
    if (Socket->NetNs != NULL)
        LxNsDereference(Socket->NetNs);
    if (Socket->Instance != NULL)
        LxInstanceDereference(Socket->Instance);
    LxFree(Socket);
}

static
PLX_UNIX_SOCKET
LxUnixCreate(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Type)
{
    PLX_UNIX_SOCKET Socket = LxAlloc(sizeof(*Socket));

    if (Socket == NULL)
        return NULL;
    Socket->RefCount = 1;
    KeInitializeEvent(&Socket->Lock, SynchronizationEvent, TRUE);
    Socket->Type = Type;
    InitializeListHead(&Socket->RecvQueue);
    InitializeListHead(&Socket->Backlog);
    InitializeListHead(&Socket->BacklogLink);
    InitializeListHead(&Socket->BindLink);
    LxPollHeadInit(&Socket->Poll);
    Socket->RecvLimit = LX_UNIX_DEFAULT_BUFFER;
    Socket->SendLimit = LX_UNIX_DEFAULT_BUFFER;
    Socket->Cred.Pid = Process->Pid;
    Socket->Cred.Uid = Process->Uid[1];
    Socket->Cred.Gid = Process->Gid[1];
    Socket->AddressLength = sizeof(USHORT);
    *(PUSHORT)Socket->Address = LX_AF_UNIX;
    LxInstanceReference(Process->Instance);
    Socket->Instance = Process->Instance;
    Socket->NetNs = LxProcessNs(Process, LxNsNet);
    LxNsReference(Socket->NetNs);
    Socket->Ino = (ULONG64)InterlockedIncrement64(&LxSocketInode);
    ExAcquireFastMutex(&LxUnixAllLock);
    InsertTailList(&LxUnixAll, &Socket->AllLink);
    ExReleaseFastMutex(&LxUnixAllLock);
    return Socket;
}

static
ULONG
LxUnixPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_UNIX_SOCKET Socket = File->Object;
    PLX_UNIX_SOCKET Peer;
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Socket->Poll, Entry);

    LxUnixLock(Socket);
    Peer = Socket->Peer;
    if (Socket->State == LX_UNIX_LISTENING)
    {
        if (!IsListEmpty(&Socket->Backlog))
            Mask |= LX_POLLIN | LX_POLLRDNORM;
        LxUnixUnlock(Socket);
        return Mask;
    }
    if (!IsListEmpty(&Socket->RecvQueue))
        Mask |= LX_POLLIN | LX_POLLRDNORM;
    if (Socket->ShutRead)
        Mask |= LX_POLLIN | LX_POLLRDNORM | LX_POLLRDHUP;
    if (Socket->Type != LX_SOCK_DGRAM)
    {
        if (Socket->PeerClosed)
            Mask |= LX_POLLIN | LX_POLLRDNORM | LX_POLLRDHUP | LX_POLLHUP;
        if ((Socket->State == LX_UNIX_CONNECTED) && (Peer != NULL) && !Socket->PeerClosed && !Socket->ShutWrite &&
            (Peer->RecvBytes < Peer->RecvLimit))
        {
            Mask |= LX_POLLOUT | LX_POLLWRNORM;
        }
        if (Socket->ShutWrite && Socket->ShutRead)
            Mask |= LX_POLLHUP;
        if ((Socket->State == LX_UNIX_UNCONNECTED) && !Socket->PeerClosed)
            Mask |= LX_POLLOUT | LX_POLLHUP;
    }
    else
    {
        if ((Peer == NULL) || ((Peer->RecvCount < LX_UNIX_DGRAM_QLEN) && (Peer->RecvBytes < Peer->RecvLimit)))
            Mask |= LX_POLLOUT | LX_POLLWRNORM;
    }
    if (Socket->Error != 0)
        Mask |= LX_POLLERR;
    LxUnixUnlock(Socket);
    return Mask;
}

static
VOID
LxUnixUnbind(
    _In_ PLX_UNIX_SOCKET Socket)
{
    ExAcquireFastMutex(&LxUnixBindLock);
    if (!IsListEmpty(&Socket->BindLink))
    {
        RemoveEntryList(&Socket->BindLink);
        InitializeListHead(&Socket->BindLink);
        LxUnixDereference(Socket);
    }
    ExReleaseFastMutex(&LxUnixBindLock);
}

static
VOID
LxUnixDisconnect(
    _In_ PLX_UNIX_SOCKET Socket)
{
    PLX_UNIX_SOCKET Peer;

    LxUnixLock(Socket);
    Peer = Socket->Peer;
    Socket->Peer = NULL;
    Socket->Closed = TRUE;
    LxUnixUnlock(Socket);

    if (Peer != NULL)
    {
        if (Socket->Type != LX_SOCK_DGRAM)
        {
            LxUnixLock(Peer);
            Peer->PeerClosed = TRUE;
            LxUnixUnlock(Peer);
            LxPollWake(&Peer->Poll, LX_POLLIN | LX_POLLOUT | LX_POLLHUP | LX_POLLRDHUP);
        }
        LxUnixDereference(Peer);
    }
}

static
VOID
LxUnixRelease(
    _In_ PLX_FILE File)
{
    PLX_UNIX_SOCKET Socket = File->Object;

    LxUnixUnlist(Socket);
    LxUnixUnbind(Socket);
    LxUnixLock(Socket);
    Socket->Closed = TRUE;
    while (!IsListEmpty(&Socket->Backlog))
    {
        PLX_UNIX_SOCKET Pending = CONTAINING_RECORD(RemoveHeadList(&Socket->Backlog), LX_UNIX_SOCKET, BacklogLink);

        InitializeListHead(&Pending->BacklogLink);
        LxUnixUnlock(Socket);
        LxUnixDisconnect(Pending);
        LxUnixDereference(Pending);
        LxUnixLock(Socket);
    }
    LxUnixUnlock(Socket);
    LxUnixDisconnect(Socket);
    LxUnixDereference(Socket);
    File->Object = NULL;
}

static
LONG
LxUnixStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_UNIX_SOCKET Socket = File->Object;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFSOCK | 0777;
    Stat->Nlink = 1;
    Stat->Ino = Socket->Ino;
    Stat->Dev = 0x8;
    Stat->BlkSize = 4096;
    return 0;
}

static
LONG64
LxUnixIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_UNIX_SOCKET Socket = File->Object;
    LONG Value = 0;
    PLIST_ENTRY Entry;

    if ((Request == LX_FIONREAD) || (Request == LX_TIOCOUTQ))
    {
        LxUnixLock(Socket);
        if (Request == LX_TIOCOUTQ)
        {
            Value = 0;
        }
        else if (Socket->Type == LX_SOCK_STREAM)
        {
            for (Entry = Socket->RecvQueue.Flink; Entry != &Socket->RecvQueue; Entry = Entry->Flink)
            {
                PLX_UNIX_MSG Message = CONTAINING_RECORD(Entry, LX_UNIX_MSG, Link);

                Value += (LONG)(Message->Length - Message->Offset);
            }
        }
        else if (!IsListEmpty(&Socket->RecvQueue))
        {
            Value = (LONG)CONTAINING_RECORD(Socket->RecvQueue.Flink, LX_UNIX_MSG, Link)->Length;
        }
        LxUnixUnlock(Socket);
        return LxCopyToUser(Argument, &Value, sizeof(Value));
    }
    return -LX_ENOTTY;
}

static
LONG64
LxUnixRecvInternal(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags);

static
LONG64
LxUnixSendInternal(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags);

static
LONG64
LxUnixRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_THREAD Thread = LxCurrentThread();
    LX_MSGHDR Header;
    LX_IOVEC Vector;

    if (Offset != NULL)
        return -LX_ESPIPE;
    if (Thread == NULL)
        return -LX_EINVAL;
    if (Length == 0)
        return 0;
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Buffer;
    Vector.Length = Length;
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    return LxUnixRecvInternal(Thread->Process, File, &Header, TRUE, MAXULONG);
}

static
LONG64
LxUnixWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_THREAD Thread = LxCurrentThread();
    LX_MSGHDR Header;
    LX_IOVEC Vector;

    if (Offset != NULL)
        return -LX_ESPIPE;
    if (Thread == NULL)
        return -LX_EINVAL;
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Buffer;
    Vector.Length = Length;
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    return LxUnixSendInternal(Thread->Process, File, &Header, TRUE, MAXULONG);
}

static const LX_FILE_OPS LxUnixOps =
{
    LxUnixRead, LxUnixWrite, LxUnixIoctl, LxUnixPoll, LxUnixStat, NULL, NULL, NULL, LxUnixRelease
};

static
PLX_FILE
LxUnixFileCreate(
    _In_ PLX_UNIX_SOCKET Socket,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxFileAlloc(&LxUnixOps, LX_O_RDWR | (Flags & LX_O_NONBLOCK), LX_S_IFSOCK);

    if (File == NULL)
        return NULL;
    File->Object = Socket;
    File->Ino = Socket->Ino;
    File->Path = LxStrPrintf("socket:[%llu]", Socket->Ino);
    return File;
}

static
PLX_UNIX_SOCKET
LxUnixFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _Out_ PLX_FILE *File,
    _Out_ PLONG64 Error)
{
    *File = LxFdGet(Process, Fd);
    if (*File == NULL)
    {
        *Error = -LX_EBADF;
        return NULL;
    }
    if ((*File)->Ops != &LxUnixOps)
    {
        LxFileDereference(*File);
        *File = NULL;
        *Error = -LX_ENOTSOCK;
        return NULL;
    }
    *Error = 0;
    return (*File)->Object;
}

static const LX_SOCKET_OPS *LxSocketFamily(_In_ PLX_FILE File);

BOOLEAN
LxFileIsSocket(
    _In_ PLX_FILE File)
{
    return (File->Ops == &LxUnixOps) || (LxSocketFamily(File) != NULL);
}

static
const LX_SOCKET_OPS *
LxSocketFamily(
    _In_ PLX_FILE File)
{
    if (LxInetIsFile(File))
        return &LxInetSocketOps;
    if (LxNetlinkIsFile(File))
        return &LxNetlinkSocketOps;
    return NULL;
}

static
const LX_SOCKET_OPS *
LxSocketFamilyFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _Out_ PLX_FILE *File)
{
    const LX_SOCKET_OPS *Family;

    *File = LxFdGet(Process, Fd);
    if (*File == NULL)
        return NULL;
    Family = LxSocketFamily(*File);
    if (Family == NULL)
    {
        LxFileDereference(*File);
        *File = NULL;
    }
    return Family;
}

static
PLX_FILE
LxSocketFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _Out_ PLONG64 Error)
{
    PLX_FILE File = LxFdGet(Process, Fd);

    *Error = 0;
    if (File == NULL)
    {
        *Error = -LX_EBADF;
    }
    else if (!LxFileIsSocket(File))
    {
        LxFileDereference(File);
        File = NULL;
        *Error = -LX_ENOTSOCK;
    }
    return File;
}

LX_SYSCALL(LxSysSocket)
{
    ULONG Domain = (ULONG)Call->Args[0];
    ULONG Type = (ULONG)Call->Args[1] & LX_SOCK_TYPE_MASK;
    ULONG Flags = (ULONG)Call->Args[1] & ~LX_SOCK_TYPE_MASK;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG Fd;

    if (Flags & ~(LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC))
        return -LX_EINVAL;
    if ((Domain == LX_AF_INET) || (Domain == LX_AF_INET6))
        return LxInetSocket(Call->Process, Domain, Type, (ULONG)Call->Args[2], Flags);
    if (Domain == LX_AF_NETLINK)
        return LxNetlinkSocket(Call->Process, Type, (ULONG)Call->Args[2], Flags);
    if (Domain != LX_AF_UNIX)
        return -LX_EAFNOSUPPORT;
    if ((Type != LX_SOCK_STREAM) && (Type != LX_SOCK_DGRAM) && (Type != LX_SOCK_SEQPACKET))
        return -LX_ESOCKTNOSUPPORT;
    if (Call->Args[2] != 0)
        return -LX_EPROTONOSUPPORT;

    Socket = LxUnixCreate(Call->Process, Type);
    if (Socket == NULL)
        return -LX_ENOMEM;
    File = LxUnixFileCreate(Socket, Flags);
    if (File == NULL)
    {
        LxUnixDereference(Socket);
        return -LX_ENOMEM;
    }
    Fd = LxFdInstall(Call->Process, File, 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    LxFileDereference(File);
    return Fd;
}

LX_SYSCALL(LxSysSocketpair)
{
    ULONG Type = (ULONG)Call->Args[1] & LX_SOCK_TYPE_MASK;
    ULONG Flags = (ULONG)Call->Args[1] & ~LX_SOCK_TYPE_MASK;
    PLX_UNIX_SOCKET First, Second;
    PLX_FILE Files[2];
    LONG Fds[2];
    LONG64 Result;

    if (Flags & ~(LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC))
        return -LX_EINVAL;
    if (Call->Args[0] != LX_AF_UNIX)
        return -LX_EAFNOSUPPORT;
    if ((Type != LX_SOCK_STREAM) && (Type != LX_SOCK_DGRAM) && (Type != LX_SOCK_SEQPACKET))
        return -LX_ESOCKTNOSUPPORT;
    if (Call->Args[2] != 0)
        return -LX_EPROTONOSUPPORT;

    First = LxUnixCreate(Call->Process, Type);
    Second = LxUnixCreate(Call->Process, Type);
    if ((First == NULL) || (Second == NULL))
    {
        if (First != NULL)
            LxUnixDereference(First);
        if (Second != NULL)
            LxUnixDereference(Second);
        return -LX_ENOMEM;
    }
    LxUnixReference(Second);
    First->Peer = Second;
    LxUnixReference(First);
    Second->Peer = First;
    First->State = Second->State = LX_UNIX_CONNECTED;
    First->PeerCred = Second->Cred;
    Second->PeerCred = First->Cred;
    First->PeerCredValid = Second->PeerCredValid = TRUE;

    Files[0] = LxUnixFileCreate(First, Flags);
    if (Files[0] == NULL)
    {
        LxUnixDisconnect(First);
        LxUnixDereference(First);
        LxUnixDisconnect(Second);
        LxUnixDereference(Second);
        return -LX_ENOMEM;
    }
    Files[1] = LxUnixFileCreate(Second, Flags);
    if (Files[1] == NULL)
    {
        LxFileDereference(Files[0]);
        LxUnixDisconnect(Second);
        LxUnixDereference(Second);
        return -LX_ENOMEM;
    }

    Fds[0] = LxFdInstall(Call->Process, Files[0], 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    Fds[1] = (Fds[0] >= 0) ? LxFdInstall(Call->Process, Files[1], 0, (Flags & LX_SOCK_CLOEXEC) != 0) : -LX_EMFILE;
    LxFileDereference(Files[0]);
    LxFileDereference(Files[1]);
    if (Fds[1] < 0)
    {
        if (Fds[0] >= 0)
            LxFdClose(Call->Process, Fds[0]);
        return Fds[1];
    }
    Result = LxCopyToUser(Call->Args[3], Fds, sizeof(Fds));
    if (Result != 0)
    {
        LxFdClose(Call->Process, Fds[0]);
        LxFdClose(Call->Process, Fds[1]);
    }
    return Result;
}

typedef struct _LX_UNIX_ADDRESS
{
    BOOLEAN Abstract;
    BOOLEAN Unnamed;
    USHORT RawLength;
    UCHAR Raw[110];
    CHAR Path[LX_UNIX_PATH_MAX + 1];
    ULONG NameLength;
} LX_UNIX_ADDRESS, *PLX_UNIX_ADDRESS;

static
LONG64
LxUnixParseAddress(
    _In_ ULONG64 UserAddress,
    _In_ ULONG Length,
    _Out_ PLX_UNIX_ADDRESS Address)
{
    LONG64 Result;
    ULONG PathLength;

    RtlZeroMemory(Address, sizeof(*Address));
    if ((Length < sizeof(USHORT)) || (Length > sizeof(Address->Raw)))
        return -LX_EINVAL;
    Result = LxCopyFromUser(Address->Raw, UserAddress, Length);
    if (Result != 0)
        return Result;
    if (*(PUSHORT)Address->Raw != LX_AF_UNIX)
        return -LX_EAFNOSUPPORT;
    Address->RawLength = (USHORT)Length;

    if (Length == sizeof(USHORT))
    {
        Address->Unnamed = TRUE;
        return 0;
    }
    if (Address->Raw[2] == 0)
    {
        Address->Abstract = TRUE;
        Address->NameLength = Length - sizeof(USHORT);
        return 0;
    }

    PathLength = 0;
    while ((sizeof(USHORT) + PathLength < Length) && (Address->Raw[2 + PathLength] != 0))
        PathLength++;
    if (PathLength > LX_UNIX_PATH_MAX - 1)
        return -LX_EINVAL;
    RtlCopyMemory(Address->Path, Address->Raw + 2, PathLength);
    Address->Path[PathLength] = '\0';
    Address->RawLength = (USHORT)(sizeof(USHORT) + PathLength + 1);
    Address->Raw[2 + PathLength] = 0;
    return 0;
}

static
LONG
LxUnixNodeId(
    _In_ PLX_NODE Node,
    _Out_ PLONG64 Id)
{
    FILE_INTERNAL_INFORMATION Internal;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    NTSTATUS Status;

    if ((Node->Mount == NULL) || (Node->Mount->Kind != LxFsNt))
        return -LX_ECONNREFUSED;
    InitializeObjectAttributes(&ObjectAttributes, &Node->NtPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    Status = ZwQueryInformationFile(Handle, &Iosb, &Internal, sizeof(Internal), FileInternalInformation);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    *Id = Internal.IndexNumber.QuadPart;
    return 0;
}

static
PLX_UNIX_SOCKET
LxUnixLookupBound(
    _In_ PLX_PROCESS Process,
    _In_ PLX_UNIX_ADDRESS Address,
    _Out_ PLONG64 Error)
{
    PLX_UNIX_SOCKET Found = NULL;
    PLIST_ENTRY Entry;
    PLX_MOUNT Mount = NULL;
    LONG64 Id = 0;

    *Error = 0;
    if (Address->Unnamed)
    {
        *Error = -LX_EINVAL;
        return NULL;
    }
    if (!Address->Abstract)
    {
        LX_NODE Node;
        LONG Status = LxLookup(Process, LX_AT_FDCWD, Address->Path, LX_LOOKUP_FOLLOW, &Node);

        if (Status != 0)
        {
            *Error = (Status == -LX_ENOENT) ? -LX_ENOENT : Status;
            return NULL;
        }
        if (Node.Type != LX_S_IFSOCK)
        {
            LxNodeFree(&Node);
            *Error = -LX_ECONNREFUSED;
            return NULL;
        }
        Status = LxUnixNodeId(&Node, &Id);
        Mount = Node.Mount;
        LxNodeFree(&Node);
        if (Status != 0)
        {
            *Error = Status;
            return NULL;
        }
    }

    ExAcquireFastMutex(&LxUnixBindLock);
    for (Entry = LxUnixBound.Flink; Entry != &LxUnixBound; Entry = Entry->Flink)
    {
        PLX_UNIX_SOCKET Socket = CONTAINING_RECORD(Entry, LX_UNIX_SOCKET, BindLink);

        if (Socket->Instance != Process->Instance)
            continue;
        if (Address->Abstract)
        {
            if (Socket->NetNs != LxProcessNs(Process, LxNsNet))
                continue;
            if ((Socket->BoundMount == NULL) && (Socket->AddressLength == Address->RawLength) &&
                (RtlCompareMemory(Socket->Address, Address->Raw, Address->RawLength) == Address->RawLength))
            {
                Found = Socket;
                break;
            }
        }
        else if ((Socket->BoundMount == Mount) && (Socket->BoundId == Id))
        {
            Found = Socket;
            break;
        }
    }
    if (Found != NULL)
        LxUnixReference(Found);
    ExReleaseFastMutex(&LxUnixBindLock);

    if (Found == NULL)
        *Error = -LX_ECONNREFUSED;
    return Found;
}

static
LONG64
LxUnixBindAddress(
    _In_ PLX_PROCESS Process,
    _In_ PLX_UNIX_SOCKET Socket,
    _In_ PLX_UNIX_ADDRESS Address)
{
    PLIST_ENTRY Entry;
    LONG64 Id = 0;
    PLX_MOUNT Mount = NULL;

    if (Socket->Bound)
        return -LX_EINVAL;

    if (Address->Unnamed)
    {
        UCHAR Random[3];
        CHAR Name[8];

        LxFillRandom(Random, sizeof(Random));
        RtlStringCbPrintfA(Name, sizeof(Name), "%05lx", ((ULONG)Random[0] << 16 | (ULONG)Random[1] << 8 | Random[2]) & 0xFFFFF);
        Address->Abstract = TRUE;
        Address->Raw[2] = 0;
        RtlCopyMemory(Address->Raw + 3, Name, 5);
        Address->RawLength = 8;
    }
    else if (!Address->Abstract)
    {
        LX_NODE Node;
        LONG Status = LxLookup(Process, LX_AT_FDCWD, Address->Path, LX_LOOKUP_CREATE, &Node);

        if (Status != 0)
            return Status;
        if (Node.Exists)
        {
            LxNodeFree(&Node);
            return -LX_EADDRINUSE;
        }
        if ((Node.Mount == NULL) || (Node.Mount->Kind != LxFsNt))
        {
            LxNodeFree(&Node);
            return -LX_EACCES;
        }
        Status = LxVfsMknod(Process, &Node, LX_S_IFSOCK | 0777, 0);
        if (Status == 0)
            Status = LxUnixNodeId(&Node, &Id);
        Mount = Node.Mount;
        LxNodeFree(&Node);
        if (Status != 0)
            return (Status == -LX_EEXIST) ? -LX_EADDRINUSE : Status;
    }

    ExAcquireFastMutex(&LxUnixBindLock);
    if (Address->Abstract)
    {
        for (Entry = LxUnixBound.Flink; Entry != &LxUnixBound; Entry = Entry->Flink)
        {
            PLX_UNIX_SOCKET Other = CONTAINING_RECORD(Entry, LX_UNIX_SOCKET, BindLink);

            if ((Other->Instance == Socket->Instance) && (Other->NetNs == Socket->NetNs) && (Other->BoundMount == NULL) &&
                (Other->AddressLength == Address->RawLength) &&
                (RtlCompareMemory(Other->Address, Address->Raw, Address->RawLength) == Address->RawLength))
            {
                ExReleaseFastMutex(&LxUnixBindLock);
                return -LX_EADDRINUSE;
            }
        }
    }
    else
    {
        for (Entry = LxUnixBound.Flink; Entry != &LxUnixBound; Entry = Entry->Flink)
        {
            PLX_UNIX_SOCKET Other = CONTAINING_RECORD(Entry, LX_UNIX_SOCKET, BindLink);

            if ((Other->BoundMount == Mount) && (Other->BoundId == Id))
            {
                RemoveEntryList(&Other->BindLink);
                InitializeListHead(&Other->BindLink);
                LxUnixDereference(Other);
                break;
            }
        }
    }
    RtlCopyMemory(Socket->Address, Address->Raw, Address->RawLength);
    Socket->AddressLength = Address->RawLength;
    Socket->BoundMount = Mount;
    Socket->BoundId = Id;
    Socket->Bound = TRUE;
    LxUnixReference(Socket);
    InsertTailList(&LxUnixBound, &Socket->BindLink);
    ExReleaseFastMutex(&LxUnixBindLock);
    return 0;
}

LX_SYSCALL(LxSysBind)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    LX_UNIX_ADDRESS Address;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG64 Result;

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Bind(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    Result = LxUnixParseAddress(Call->Args[1], (ULONG)Call->Args[2], &Address);
    if (Result == 0)
    {
        LxUnixLock(Socket);
        Result = (Socket->Bound) ? -LX_EINVAL : 0;
        LxUnixUnlock(Socket);
        if (Result == 0)
            Result = LxUnixBindAddress(Call->Process, Socket, &Address);
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysListen)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG64 Result;
    LONG Backlog = (LONG)Call->Args[1];

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Listen(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    if (Socket->Type == LX_SOCK_DGRAM)
    {
        LxFileDereference(File);
        return -LX_EOPNOTSUPP;
    }
    if (!Socket->Bound)
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }

    LxUnixLock(Socket);
    if (Socket->State == LX_UNIX_CONNECTED)
        Result = -LX_EINVAL;
    else
    {
        Socket->State = LX_UNIX_LISTENING;
        Socket->BacklogMax = (Backlog <= 0) ? 1 : (ULONG)min(Backlog, 4096);
    }
    LxUnixUnlock(Socket);
    LxFileDereference(File);
    return Result;
}

typedef struct _LX_UNIX_WAIT
{
    PLX_UNIX_SOCKET Socket;
    BOOLEAN Backlog;
} LX_UNIX_WAIT, *PLX_UNIX_WAIT;

static
BOOLEAN
LxUnixBacklogReady(
    _In_ PVOID Context)
{
    PLX_UNIX_SOCKET Listener = Context;

    return (Listener->BacklogCount < Listener->BacklogMax) || Listener->Closed || (Listener->State != LX_UNIX_LISTENING);
}

static
LONG64
LxUnixConnectStream(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ PLX_UNIX_SOCKET Socket,
    _In_ PLX_UNIX_SOCKET Listener)
{
    PLX_UNIX_SOCKET Server;
    LONG64 Result;

    for (;;)
    {
        LxUnixLock(Listener);
        if ((Listener->State != LX_UNIX_LISTENING) || Listener->Closed || (Listener->Type != Socket->Type))
        {
            LxUnixUnlock(Listener);
            return (Listener->Type != Socket->Type) ? -LX_EPROTOTYPE : -LX_ECONNREFUSED;
        }
        if (Listener->BacklogCount < Listener->BacklogMax)
            break;
        LxUnixUnlock(Listener);
        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxWaitHead(&Listener->Poll, LxUnixBacklogReady, Listener, NULL);
        if (Result == -LX_EINTR)
            return Result;
    }

    Server = LxUnixCreate(Process, Socket->Type);
    if (Server == NULL)
    {
        LxUnixUnlock(Listener);
        return -LX_ENOMEM;
    }
    Server->Cred = Listener->Cred;
    RtlCopyMemory(Server->Address, Listener->Address, Listener->AddressLength);
    Server->AddressLength = Listener->AddressLength;
    Server->State = LX_UNIX_CONNECTED;
    Server->PeerCred = Socket->Cred;
    Server->PeerCredValid = TRUE;
    Server->PassCred = Listener->PassCred;
    LxUnixReference(Socket);
    Server->Peer = Socket;

    LxUnixLock(Socket);
    if (Socket->State == LX_UNIX_CONNECTED)
    {
        LxUnixUnlock(Socket);
        LxUnixUnlock(Listener);
        LxUnixDereference(Socket);
        Server->Peer = NULL;
        LxUnixDereference(Server);
        return -LX_EISCONN;
    }
    LxUnixReference(Server);
    Socket->Peer = Server;
    Socket->State = LX_UNIX_CONNECTED;
    Socket->PeerCred = Listener->Cred;
    Socket->PeerCredValid = TRUE;
    LxUnixUnlock(Socket);

    InsertTailList(&Listener->Backlog, &Server->BacklogLink);
    Listener->BacklogCount++;
    LxUnixUnlock(Listener);

    LxPollWake(&Listener->Poll, LX_POLLIN | LX_POLLRDNORM);
    LxPollWake(&Socket->Poll, LX_POLLOUT | LX_POLLWRNORM);
    return 0;
}

LX_SYSCALL(LxSysConnect)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    LX_UNIX_ADDRESS Address;
    PLX_UNIX_SOCKET Socket, Target;
    PLX_FILE File;
    LONG64 Result;

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Connect(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;

    Result = LxUnixParseAddress(Call->Args[1], (ULONG)Call->Args[2], &Address);
    if ((Result == 0) && Address.Unnamed && (Socket->Type == LX_SOCK_DGRAM))
    {
        LxUnixDisconnect(Socket);
        LxUnixLock(Socket);
        Socket->Closed = FALSE;
        Socket->State = LX_UNIX_UNCONNECTED;
        LxUnixUnlock(Socket);
        LxFileDereference(File);
        return 0;
    }
    if (Result != 0)
    {
        LxFileDereference(File);
        return Result;
    }

    Target = LxUnixLookupBound(Call->Process, &Address, &Result);
    if (Target == NULL)
    {
        LxFileDereference(File);
        return Result;
    }

    if (Socket->Type == LX_SOCK_DGRAM)
    {
        PLX_UNIX_SOCKET Previous;

        if (Target->Type != LX_SOCK_DGRAM)
        {
            LxUnixDereference(Target);
            LxFileDereference(File);
            return -LX_EPROTOTYPE;
        }
        LxUnixLock(Socket);
        Previous = Socket->Peer;
        Socket->Peer = Target;
        Socket->State = LX_UNIX_CONNECTED;
        Socket->PeerCred = Target->Cred;
        Socket->PeerCredValid = TRUE;
        LxUnixUnlock(Socket);
        if (Previous != NULL)
            LxUnixDereference(Previous);
        Result = 0;
    }
    else
    {
        Result = LxUnixConnectStream(Call->Process, File, Socket, Target);
        LxUnixDereference(Target);
    }
    LxFileDereference(File);
    return Result;
}

static
LONG64
LxUnixCopyAddress(
    _In_ ULONG64 UserAddress,
    _In_ ULONG64 UserLength,
    _In_reads_bytes_(Length) PUCHAR Address,
    _In_ ULONG Length)
{
    LONG Available;
    LONG64 Result;

    if ((UserAddress == 0) || (UserLength == 0))
        return 0;
    Result = LxCopyFromUser(&Available, UserLength, sizeof(Available));
    if (Result != 0)
        return Result;
    if (Available < 0)
        return -LX_EINVAL;
    Result = LxCopyToUser(UserAddress, Address, min((ULONG)Available, Length));
    if (Result == 0)
        Result = LxCopyToUser(UserLength, &Length, sizeof(Length));
    return Result;
}

static
BOOLEAN
LxUnixAcceptReady(
    _In_ PVOID Context)
{
    PLX_UNIX_SOCKET Listener = Context;

    return !IsListEmpty(&Listener->Backlog) || Listener->Closed;
}

static
LONG64
LxUnixAccept(
    _In_ PLX_CALL Call,
    _In_ ULONG Flags)
{
    PLX_UNIX_SOCKET Listener, Server = NULL;
    PLX_FILE File, NewFile;
    LONG64 Result;
    LONG Fd;

    if (Flags & ~(LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC))
        return -LX_EINVAL;
    Listener = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Listener == NULL)
        return Result;
    if (Listener->State != LX_UNIX_LISTENING)
    {
        LxFileDereference(File);
        return (Listener->Type == LX_SOCK_DGRAM) ? -LX_EOPNOTSUPP : -LX_EINVAL;
    }

    for (;;)
    {
        LxUnixLock(Listener);
        if (!IsListEmpty(&Listener->Backlog))
        {
            Server = CONTAINING_RECORD(RemoveHeadList(&Listener->Backlog), LX_UNIX_SOCKET, BacklogLink);
            InitializeListHead(&Server->BacklogLink);
            Listener->BacklogCount--;
        }
        LxUnixUnlock(Listener);
        if (Server != NULL)
            break;
        if (File->Flags & LX_O_NONBLOCK)
        {
            LxFileDereference(File);
            return -LX_EAGAIN;
        }
        Result = LxWaitHead(&Listener->Poll, LxUnixAcceptReady, Listener, NULL);
        if (Result == -LX_EINTR)
        {
            LxFileDereference(File);
            return Result;
        }
    }
    LxPollWake(&Listener->Poll, LX_POLLOUT);

    NewFile = LxUnixFileCreate(Server, Flags);
    if (NewFile == NULL)
    {
        LxUnixDisconnect(Server);
        LxUnixDereference(Server);
        LxFileDereference(File);
        return -LX_ENOMEM;
    }
    Fd = LxFdInstall(Call->Process, NewFile, 0, (Flags & LX_SOCK_CLOEXEC) != 0);
    if (Fd >= 0)
    {
        PLX_UNIX_SOCKET Peer = Server->Peer;
        UCHAR Address[110];
        ULONG Length = sizeof(USHORT);

        *(PUSHORT)Address = LX_AF_UNIX;
        if (Peer != NULL)
        {
            Length = Peer->AddressLength;
            RtlCopyMemory(Address, Peer->Address, Length);
        }
        Result = LxUnixCopyAddress(Call->Args[1], Call->Args[2], Address, Length);
        if (Result != 0)
        {
            LxFdClose(Call->Process, Fd);
            Fd = (LONG)Result;
        }
    }
    LxFileDereference(NewFile);
    LxFileDereference(File);
    return Fd;
}

LX_SYSCALL(LxSysAccept)
{
    PLX_FILE InetFile;
    const LX_SOCKET_OPS *Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    LONG64 Result;

    if (Family == NULL)
        return LxUnixAccept(Call, 0);
    Result = Family->Accept(Call, InetFile, 0);
    LxFileDereference(InetFile);
    return Result;
}

LX_SYSCALL(LxSysAccept4)
{
    PLX_FILE InetFile;
    const LX_SOCKET_OPS *Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    LONG64 Result;

    if (Family == NULL)
        return LxUnixAccept(Call, (ULONG)Call->Args[3]);
    Result = Family->Accept(Call, InetFile, (ULONG)Call->Args[3]);
    LxFileDereference(InetFile);
    return Result;
}

static
LONG64
LxUnixName(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN PeerName)
{
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    UCHAR Address[110];
    ULONG Length;
    LONG64 Result;

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    LxUnixLock(Socket);
    if (PeerName)
    {
        if ((Socket->State != LX_UNIX_CONNECTED) || (Socket->Peer == NULL))
        {
            LxUnixUnlock(Socket);
            LxFileDereference(File);
            return -LX_ENOTCONN;
        }
        Length = Socket->Peer->AddressLength;
        RtlCopyMemory(Address, Socket->Peer->Address, Length);
    }
    else
    {
        Length = Socket->AddressLength;
        RtlCopyMemory(Address, Socket->Address, Length);
    }
    LxUnixUnlock(Socket);
    LxFileDereference(File);
    return LxUnixCopyAddress(Call->Args[1], Call->Args[2], Address, Length);
}

LX_SYSCALL(LxSysGetsockname)
{
    PLX_FILE InetFile;
    const LX_SOCKET_OPS *Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    LONG64 Result;

    if (Family == NULL)
        return LxUnixName(Call, FALSE);
    Result = Family->Name(Call, InetFile, FALSE);
    LxFileDereference(InetFile);
    return Result;
}

LX_SYSCALL(LxSysGetpeername)
{
    PLX_FILE InetFile;
    const LX_SOCKET_OPS *Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    LONG64 Result;

    if (Family == NULL)
        return LxUnixName(Call, TRUE);
    Result = Family->Name(Call, InetFile, TRUE);
    LxFileDereference(InetFile);
    return Result;
}

LONG64
LxSocketGatherIov(
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _Out_ PLX_IOVEC *Vectors,
    _Out_ PULONG64 Total)
{
    PLX_IOVEC Copy;
    ULONG64 Index, Sum = 0;
    LONG64 Result;

    *Vectors = NULL;
    *Total = 0;
    if (Header->IovLength > 1024)
        return -LX_EMSGSIZE;
    if (Header->IovLength == 0)
        return 0;
    Copy = LxAlloc(sizeof(LX_IOVEC) * (SIZE_T)Header->IovLength);
    if (Copy == NULL)
        return -LX_ENOMEM;
    if (KernelIov)
    {
        RtlCopyMemory(Copy, (PVOID)(ULONG_PTR)Header->Iov, sizeof(LX_IOVEC) * (SIZE_T)Header->IovLength);
        Result = 0;
    }
    else
    {
        Result = LxCopyFromUser(Copy, Header->Iov, sizeof(LX_IOVEC) * (SIZE_T)Header->IovLength);
    }
    if (Result != 0)
    {
        LxFree(Copy);
        return Result;
    }
    for (Index = 0; Index < Header->IovLength; Index++)
    {
        if (Copy[Index].Length > 0x7FFFF000)
        {
            LxFree(Copy);
            return -LX_EINVAL;
        }
        Sum += Copy[Index].Length;
    }
    *Vectors = Copy;
    *Total = Sum;
    return 0;
}

LONG64
LxSocketCopyIn(
    _In_reads_(Count) PLX_IOVEC Vectors,
    _In_ ULONG64 Count,
    _In_ ULONG64 Skip,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG64 Index, Done = 0;

    for (Index = 0; (Index < Count) && (Done < Length); Index++)
    {
        ULONG64 Chunk;
        LONG64 Result;

        if (Skip >= Vectors[Index].Length)
        {
            Skip -= Vectors[Index].Length;
            continue;
        }
        Chunk = min(Vectors[Index].Length - Skip, Length - Done);
        Result = LxCopyFromUser(Buffer + Done, Vectors[Index].Base + Skip, (SIZE_T)Chunk);
        if (Result != 0)
            return Result;
        Done += Chunk;
        Skip = 0;
    }
    return 0;
}

LONG64
LxSocketCopyOut(
    _In_reads_(Count) PLX_IOVEC Vectors,
    _In_ ULONG64 Count,
    _In_ ULONG64 Skip,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG64 Index, Done = 0;

    for (Index = 0; (Index < Count) && (Done < Length); Index++)
    {
        ULONG64 Chunk;
        LONG64 Result;

        if (Skip >= Vectors[Index].Length)
        {
            Skip -= Vectors[Index].Length;
            continue;
        }
        Chunk = min(Vectors[Index].Length - Skip, Length - Done);
        Result = LxCopyToUser(Vectors[Index].Base + Skip, Buffer + Done, (SIZE_T)Chunk);
        if (Result != 0)
            return Result;
        Done += Chunk;
        Skip = 0;
    }
    return 0;
}

static
LONG64
LxUnixParseControl(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MSGHDR Header,
    _Out_ PLX_FILE **Fds,
    _Out_ PULONG FdCount,
    _Inout_ PLX_UCRED Cred)
{
    PUCHAR Control;
    ULONG64 Offset = 0;
    LONG64 Result = 0;

    *Fds = NULL;
    *FdCount = 0;
    if ((Header->Control == 0) || (Header->ControlLength == 0))
        return 0;
    if (Header->ControlLength > 65536)
        return -LX_ENOBUFS;
    Control = LxAlloc((SIZE_T)Header->ControlLength);
    if (Control == NULL)
        return -LX_ENOMEM;
    Result = LxCopyFromUser(Control, Header->Control, (SIZE_T)Header->ControlLength);
    if (Result != 0)
    {
        LxFree(Control);
        return Result;
    }

    while (Offset + sizeof(LX_CMSGHDR) <= Header->ControlLength)
    {
        PLX_CMSGHDR Message = (PLX_CMSGHDR)(Control + Offset);
        ULONG64 DataLength;

        if ((Message->Length < sizeof(LX_CMSGHDR)) || (Offset + Message->Length > Header->ControlLength))
        {
            Result = -LX_EINVAL;
            break;
        }
        DataLength = Message->Length - sizeof(LX_CMSGHDR);
        if (Message->Level == LX_SOL_SOCKET)
        {
            if (Message->Type == LX_SCM_RIGHTS)
            {
                ULONG Count = (ULONG)(DataLength / sizeof(LONG)), Index;
                PLONG Values = (PLONG)(Message + 1);
                PLX_FILE *Array;

                if ((Count == 0) || (*FdCount + Count > LX_UNIX_MAX_FDS))
                {
                    Result = -LX_EINVAL;
                    break;
                }
                Array = LxAlloc(sizeof(PLX_FILE) * (*FdCount + Count));
                if (Array == NULL)
                {
                    Result = -LX_ENOMEM;
                    break;
                }
                if (*Fds != NULL)
                {
                    RtlCopyMemory(Array, *Fds, sizeof(PLX_FILE) * *FdCount);
                    LxFree(*Fds);
                }
                *Fds = Array;
                for (Index = 0; Index < Count; Index++)
                {
                    PLX_FILE File = LxFdGet(Process, Values[Index]);

                    if (File == NULL)
                    {
                        Result = -LX_EBADF;
                        break;
                    }
                    Array[(*FdCount)++] = File;
                }
                if (Result != 0)
                    break;
            }
            else if (Message->Type == LX_SCM_CREDENTIALS)
            {
                LX_UCRED Supplied;

                if (DataLength < sizeof(Supplied))
                {
                    Result = -LX_EINVAL;
                    break;
                }
                RtlCopyMemory(&Supplied, Message + 1, sizeof(Supplied));
                Supplied.Uid = LxCredMakeId(Process, FALSE, Supplied.Uid);
                Supplied.Gid = LxCredMakeId(Process, TRUE, Supplied.Gid);
                if ((Supplied.Uid == MAXULONG) || (Supplied.Gid == MAXULONG))
                {
                    Result = -LX_EINVAL;
                    break;
                }
                if (((Supplied.Pid != LxPidNr(Process, Process)) &&
                     !LxNsCapable(Process, LxNsOwner(Process->PidNs), LX_CAP_SYS_ADMIN)) ||
                    ((Supplied.Uid != Process->Uid[0]) && (Supplied.Uid != Process->Uid[1]) &&
                     (Supplied.Uid != Process->Uid[2]) && !LxNsCapable(Process, NULL, LX_CAP_SETUID)) ||
                    ((Supplied.Gid != Process->Gid[0]) && (Supplied.Gid != Process->Gid[1]) &&
                     (Supplied.Gid != Process->Gid[2]) && !LxNsCapable(Process, NULL, LX_CAP_SETGID)))
                {
                    Result = -LX_EPERM;
                    break;
                }
                Supplied.Pid = LxNrToGlobal(Process, Supplied.Pid);
                *Cred = Supplied;
            }
            else
            {
                Result = -LX_EINVAL;
                break;
            }
        }
        Offset += LX_ALIGN_UP(Message->Length, 8);
    }

    LxFree(Control);
    if (Result != 0)
    {
        ULONG Index;

        for (Index = 0; Index < *FdCount; Index++)
            LxFileDereference((*Fds)[Index]);
        LxFree(*Fds);
        *Fds = NULL;
        *FdCount = 0;
    }
    return Result;
}

typedef struct _LX_UNIX_SPACE
{
    PLX_UNIX_SOCKET Target;
    ULONG Needed;
} LX_UNIX_SPACE, *PLX_UNIX_SPACE;

static
BOOLEAN
LxUnixSpaceReady(
    _In_ PVOID Context)
{
    PLX_UNIX_SPACE Space = Context;
    PLX_UNIX_SOCKET Target = Space->Target;

    if (Target->Closed || Target->ShutRead)
        return TRUE;
    if (Target->Type == LX_SOCK_DGRAM)
        return (Target->RecvCount < LX_UNIX_DGRAM_QLEN) && (Target->RecvBytes + Space->Needed <= Target->RecvLimit);
    return Target->RecvBytes < Target->RecvLimit;
}

static
LONG64
LxUnixSendInternal(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_UNIX_SOCKET Socket = File->Object, Target = NULL;
    BOOLEAN NoSignal = (Flags != MAXULONG) && (Flags & LX_MSG_NOSIGNAL);

    if (File->Ops != &LxUnixOps)
        return LxSocketFamily(File)->Sendmsg(File, Header, KernelIov, (Flags == MAXULONG) ? 0 : Flags);
    BOOLEAN NonBlock = (File->Flags & LX_O_NONBLOCK) || ((Flags != MAXULONG) && (Flags & LX_MSG_DONTWAIT));
    PLX_IOVEC Vectors;
    ULONG64 Total, Sent = 0;
    PLX_FILE *Fds = NULL;
    ULONG FdCount = 0;
    LX_UCRED Cred;
    LONG64 Result;

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;

    Cred.Pid = Process->Pid;
    Cred.Uid = Process->Uid[1];
    Cred.Gid = Process->Gid[1];
    Result = LxUnixParseControl(Process, Header, &Fds, &FdCount, &Cred);
    if (Result != 0)
    {
        LxFree(Vectors);
        return Result;
    }

    if ((Header->Name != 0) && (Header->NameLength != 0) && (Socket->Type == LX_SOCK_DGRAM))
    {
        LX_UNIX_ADDRESS Address;

        Result = LxUnixParseAddress(Header->Name, (ULONG)Header->NameLength, &Address);
        if (Result == 0)
            Target = LxUnixLookupBound(Process, &Address, &Result);
        if ((Target != NULL) && (Target->Type != LX_SOCK_DGRAM))
        {
            LxUnixDereference(Target);
            Target = NULL;
            Result = -LX_EPROTOTYPE;
        }
    }
    else
    {
        LxUnixLock(Socket);
        if (Socket->ShutWrite)
            Result = -LX_EPIPE;
        else if ((Socket->Peer == NULL) || (Socket->State != LX_UNIX_CONNECTED))
            Result = (Socket->Type == LX_SOCK_DGRAM) ? -LX_EDESTADDRREQ : (Socket->PeerClosed ? -LX_EPIPE : -LX_ENOTCONN);
        else if (Socket->PeerClosed)
            Result = (Socket->Type == LX_SOCK_DGRAM) ? -LX_ECONNREFUSED : -LX_EPIPE;
        else
        {
            Target = Socket->Peer;
            LxUnixReference(Target);
        }
        LxUnixUnlock(Socket);
    }

    if (Target == NULL)
    {
        ULONG Index;

        for (Index = 0; Index < FdCount; Index++)
            LxFileDereference(Fds[Index]);
        LxFree(Fds);
        LxFree(Vectors);
        if ((Result == -LX_EPIPE) && !NoSignal)
            return LxWriteFailed(LX_EPIPE);
        return Result;
    }

    if ((Socket->Type != LX_SOCK_STREAM) && (Total > LX_UNIX_MAX_DGRAM))
    {
        Result = -LX_EMSGSIZE;
        goto Exit;
    }
    if ((Socket->Type == LX_SOCK_STREAM) && (Total == 0))
    {
        Result = 0;
        goto Exit;
    }

    do
    {
        ULONG Chunk = (Socket->Type == LX_SOCK_STREAM) ? (ULONG)min(Total - Sent, LX_UNIX_STREAM_CHUNK) : (ULONG)Total;
        PLX_UNIX_MSG Message;
        LX_UNIX_SPACE Space;

        for (;;)
        {
            BOOLEAN Ready, Dead;

            LxUnixLock(Target);
            Dead = Target->Closed || Target->ShutRead;
            if (Socket->Type == LX_SOCK_DGRAM)
                Ready = (Target->RecvCount < LX_UNIX_DGRAM_QLEN) && (Target->RecvBytes + Chunk <= max(Target->RecvLimit, Chunk));
            else
                Ready = Target->RecvBytes < Target->RecvLimit;
            LxUnixUnlock(Target);

            if (Dead)
            {
                Result = (Socket->Type == LX_SOCK_DGRAM) ? -LX_ECONNREFUSED : -LX_EPIPE;
                goto Exit;
            }
            if (Ready)
                break;
            if (NonBlock)
            {
                Result = -LX_EAGAIN;
                goto Exit;
            }
            Space.Target = Target;
            Space.Needed = Chunk;
            Result = LxWaitHead(&Target->Poll, LxUnixSpaceReady, &Space, NULL);
            if (Result == -LX_EINTR)
                goto Exit;
        }

        Message = LxAlloc(FIELD_OFFSET(LX_UNIX_MSG, Data) + max(Chunk, 1));
        if (Message == NULL)
        {
            Result = -LX_ENOMEM;
            goto Exit;
        }
        Result = LxSocketCopyIn(Vectors, Header->IovLength, Sent, Message->Data, Chunk);
        if (Result != 0)
        {
            LxFree(Message);
            goto Exit;
        }
        Message->Length = Chunk;
        Message->Cred = Cred;
        Message->AddressLength = Socket->AddressLength;
        RtlCopyMemory(Message->Address, Socket->Address, Socket->AddressLength);
        if (Sent == 0)
        {
            Message->Fds = Fds;
            Message->FdCount = FdCount;
            Fds = NULL;
            FdCount = 0;
        }

        LxUnixLock(Target);
        InsertTailList(&Target->RecvQueue, &Message->Link);
        Target->RecvBytes += Chunk;
        Target->RecvCount++;
        LxUnixUnlock(Target);
        LxPollWake(&Target->Poll, LX_POLLIN | LX_POLLRDNORM);

        Sent += Chunk;
    } while (Sent < Total);
    Result = 0;

Exit:
    if (Fds != NULL)
    {
        ULONG Index;

        for (Index = 0; Index < FdCount; Index++)
            LxFileDereference(Fds[Index]);
        LxFree(Fds);
    }
    LxFree(Vectors);
    LxUnixDereference(Target);
    if (Sent != 0)
        return (LONG64)Sent;
    if (Total == 0 && Result == 0)
        return 0;
    if ((Result == -LX_EPIPE) && !NoSignal)
        return LxWriteFailed(LX_EPIPE);
    return Result;
}

static
BOOLEAN
LxUnixRecvReady(
    _In_ PVOID Context)
{
    PLX_UNIX_SOCKET Socket = Context;

    return !IsListEmpty(&Socket->RecvQueue) || Socket->PeerClosed || Socket->ShutRead || Socket->Closed ||
           ((Socket->Type != LX_SOCK_DGRAM) && (Socket->State != LX_UNIX_CONNECTED));
}

static
LONG64
LxUnixDeliverControl(
    _In_ PLX_PROCESS Process,
    _In_ PLX_UNIX_SOCKET Socket,
    _In_ PLX_UNIX_MSG Message,
    _Inout_ PLX_MSGHDR Header,
    _In_ ULONG Flags,
    _Inout_ PULONG OutFlags)
{
    UCHAR Buffer[16 + sizeof(LX_UCRED) + 8 + 16 + LX_UNIX_MAX_FDS * sizeof(LONG) + 8];
    ULONG Used = 0;
    LONG64 Result;

    if ((Header->Control == 0) || (Header->ControlLength == 0))
    {
        if (Message->FdCount != 0)
            *OutFlags |= LX_MSG_CTRUNC;
        Header->ControlLength = 0;
        return 0;
    }

    if (Socket->PassCred)
    {
        PLX_CMSGHDR Cmsg = (PLX_CMSGHDR)Buffer;
        ULONG Space = (ULONG)LX_ALIGN_UP(sizeof(LX_CMSGHDR) + sizeof(LX_UCRED), 8);

        if (Space <= Header->ControlLength)
        {
            RtlZeroMemory(Cmsg, Space);
            Cmsg->Length = sizeof(LX_CMSGHDR) + sizeof(LX_UCRED);
            Cmsg->Level = LX_SOL_SOCKET;
            Cmsg->Type = LX_SCM_CREDENTIALS;
            {
                PLX_THREAD Receiver = LxCurrentThread();
                LX_UCRED Cred = Message->Cred;

                if (Receiver != NULL)
                {
                    Cred.Pid = LxGlobalToNr(Receiver->Process, Cred.Pid);
                    Cred.Uid = LxCredViewId(Receiver->Process, FALSE, Cred.Uid);
                    Cred.Gid = LxCredViewId(Receiver->Process, TRUE, Cred.Gid);
                }
                RtlCopyMemory(Cmsg + 1, &Cred, sizeof(LX_UCRED));
            }
            Used += Space;
        }
        else
        {
            *OutFlags |= LX_MSG_CTRUNC;
        }
    }

    if (Message->FdCount != 0)
    {
        PLX_CMSGHDR Cmsg = (PLX_CMSGHDR)(Buffer + Used);
        ULONG Fit, Index;
        PLONG Values = (PLONG)(Cmsg + 1);

        if (Used + sizeof(LX_CMSGHDR) + sizeof(LONG) > Header->ControlLength)
            Fit = 0;
        else
            Fit = (ULONG)min(Message->FdCount, (Header->ControlLength - Used - sizeof(LX_CMSGHDR)) / sizeof(LONG));
        if (Fit < Message->FdCount)
            *OutFlags |= LX_MSG_CTRUNC;
        if (Fit != 0)
        {
            ULONG Installed = 0;

            for (Index = 0; Index < Fit; Index++)
            {
                LONG Fd = LxFdInstall(Process, Message->Fds[Index], 0,
                                      (Flags != MAXULONG) && (Flags & LX_MSG_CMSG_CLOEXEC));

                if (Fd < 0)
                {
                    *OutFlags |= LX_MSG_CTRUNC;
                    break;
                }
                Values[Installed++] = Fd;
            }
            if (Installed != 0)
            {
                Cmsg->Length = sizeof(LX_CMSGHDR) + Installed * sizeof(LONG);
                Cmsg->Level = LX_SOL_SOCKET;
                Cmsg->Type = LX_SCM_RIGHTS;
                Used += (ULONG)min(LX_ALIGN_UP(Cmsg->Length, 8), Header->ControlLength - Used);
            }
        }
    }

    Result = LxCopyToUser(Header->Control, Buffer, Used);
    Header->ControlLength = Used;
    return Result;
}

static
VOID
LxUnixWakeWriters(
    _In_ PLX_UNIX_SOCKET Socket)
{
    PLX_UNIX_SOCKET Peer;

    LxPollWake(&Socket->Poll, LX_POLLOUT | LX_POLLWRNORM);
    if (Socket->Type == LX_SOCK_DGRAM)
        return;

    LxUnixLock(Socket);
    Peer = Socket->Peer;
    if (Peer != NULL)
        LxUnixReference(Peer);
    LxUnixUnlock(Socket);
    if (Peer != NULL)
    {
        LxPollWake(&Peer->Poll, LX_POLLOUT | LX_POLLWRNORM);
        LxUnixDereference(Peer);
    }
}

static
LONG64
LxUnixRecvInternal(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ PLX_MSGHDR Header,
    _In_ BOOLEAN KernelIov,
    _In_ ULONG Flags)
{
    PLX_UNIX_SOCKET Socket = File->Object;
    ULONG Options = (Flags == MAXULONG) ? 0 : Flags;
    BOOLEAN NonBlock = (File->Flags & LX_O_NONBLOCK) || (Options & LX_MSG_DONTWAIT);

    if (File->Ops != &LxUnixOps)
        return LxSocketFamily(File)->Recvmsg(File, Header, KernelIov, Options);
    BOOLEAN Peek = (Options & LX_MSG_PEEK) != 0;
    BOOLEAN ControlDelivered = FALSE;
    PLX_IOVEC Vectors;
    ULONG64 Total, Done = 0;
    ULONG OutFlags = 0;
    LONG64 Result;
    LARGE_INTEGER Timeout;

    Result = LxSocketGatherIov(Header, KernelIov, &Vectors, &Total);
    if (Result != 0)
        return Result;

    if ((Socket->Type != LX_SOCK_DGRAM) && (Socket->State == LX_UNIX_LISTENING))
    {
        LxFree(Vectors);
        return -LX_ENOTCONN;
    }

    for (;;)
    {
        PLX_UNIX_MSG Message = NULL, Consumed = NULL;
        BOOLEAN Eof = FALSE;

        LxUnixLock(Socket);
        if (!IsListEmpty(&Socket->RecvQueue))
        {
            Message = CONTAINING_RECORD(Socket->RecvQueue.Flink, LX_UNIX_MSG, Link);

            if (Socket->Type == LX_SOCK_STREAM)
            {
                ULONG Available = Message->Length - Message->Offset;
                ULONG Chunk = (ULONG)min(Available, Total - Done);

                if ((Done != 0) && (Message->FdCount != 0))
                {
                    LxUnixUnlock(Socket);
                    break;
                }
                Result = LxSocketCopyOut(Vectors, Header->IovLength, Done, Message->Data + Message->Offset, Chunk);
                if (Result != 0)
                {
                    LxUnixUnlock(Socket);
                    break;
                }
                if (Done == 0)
                {
                    LxUnixUnlock(Socket);
                    ControlDelivered = TRUE;
                    Result = LxUnixDeliverControl(Process, Socket, Message, Header, Flags, &OutFlags);
                    LxUnixLock(Socket);
                    if (!Peek)
                    {
                        ULONG Index;

                        for (Index = 0; Index < Message->FdCount; Index++)
                            LxFileDereference(Message->Fds[Index]);
                        Message->FdCount = 0;
                    }
                }
                Done += Chunk;
                if (!Peek)
                {
                    Message->Offset += Chunk;
                    Socket->RecvBytes -= Chunk;
                    if (Message->Offset == Message->Length)
                    {
                        RemoveEntryList(&Message->Link);
                        Socket->RecvCount--;
                        Consumed = Message;
                    }
                }
                LxUnixUnlock(Socket);
                if (Consumed != NULL)
                    LxUnixFreeMessage(Consumed);
                if (!Peek)
                    LxUnixWakeWriters(Socket);
                if (Peek || (Done >= Total) || !(Options & LX_MSG_WAITALL))
                {
                    if ((Done < Total) && !Peek && !(Options & LX_MSG_WAITALL))
                    {
                        LxUnixLock(Socket);
                        if (!IsListEmpty(&Socket->RecvQueue) &&
                            (CONTAINING_RECORD(Socket->RecvQueue.Flink, LX_UNIX_MSG, Link)->FdCount == 0))
                        {
                            LxUnixUnlock(Socket);
                            continue;
                        }
                        LxUnixUnlock(Socket);
                    }
                    break;
                }
                continue;
            }

            if (!Peek)
            {
                RemoveEntryList(&Message->Link);
                Socket->RecvBytes -= Message->Length;
                Socket->RecvCount--;
            }
            LxUnixUnlock(Socket);

            Result = LxSocketCopyOut(Vectors, Header->IovLength, 0, Message->Data, (ULONG)min(Message->Length, Total));
            if ((Result == 0) && (Message->Length > Total))
                OutFlags |= LX_MSG_TRUNC;
            if ((Result == 0) && (Header->Name != 0))
            {
                LONG Length = (LONG)min(Message->AddressLength, Header->NameLength);

                if (Length > 0)
                    Result = LxCopyToUser(Header->Name, Message->Address, Length);
                Header->NameLength = Message->AddressLength;
            }
            if (Result == 0)
            {
                ControlDelivered = TRUE;
                Result = LxUnixDeliverControl(Process, Socket, Message, Header, Peek ? MAXULONG : Flags, &OutFlags);
            }
            Done = (Options & LX_MSG_TRUNC) ? Message->Length : min(Message->Length, Total);
            if (!Peek)
            {
                LxUnixFreeMessage(Message);
                LxUnixWakeWriters(Socket);
            }
            break;
        }

        Eof = Socket->ShutRead || ((Socket->Type != LX_SOCK_DGRAM) &&
                                   (Socket->PeerClosed || (Socket->Peer != NULL && Socket->Peer->ShutWrite)));
        if ((Socket->Type != LX_SOCK_DGRAM) && (Socket->State != LX_UNIX_CONNECTED) && !Socket->PeerClosed)
        {
            LxUnixUnlock(Socket);
            Result = -LX_ENOTCONN;
            break;
        }
        LxUnixUnlock(Socket);

        if (Eof || (Done != 0))
            break;
        if (NonBlock)
        {
            Result = -LX_EAGAIN;
            break;
        }
        Timeout.QuadPart = -Socket->RecvTimeout;
        Result = LxWaitHead(&Socket->Poll, LxUnixRecvReady, Socket, (Socket->RecvTimeout != 0) ? &Timeout : NULL);
        if (Result != 0)
            break;
    }

    LxFree(Vectors);
    Header->Flags = OutFlags;
    if (!ControlDelivered)
        Header->ControlLength = 0;
    if ((Socket->Type == LX_SOCK_STREAM) && (Header->Name != 0))
        Header->NameLength = 0;
    if (Done != 0)
        return (LONG64)Done;
    return Result;
}

static
LONG64
LxUnixCopyHeaderIn(
    _In_ ULONG64 UserHeader,
    _Out_ PLX_MSGHDR Header)
{
    return LxCopyFromUser(Header, UserHeader, sizeof(*Header));
}

static
LONG64
LxUnixSendmsgOne(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ ULONG64 UserHeader,
    _In_ ULONG Flags)
{
    LX_MSGHDR Header;
    LONG64 Result = LxUnixCopyHeaderIn(UserHeader, &Header);

    if (Result != 0)
        return Result;
    return LxUnixSendInternal(Call->Process, File, &Header, FALSE, Flags);
}

static
LONG64
LxUnixRecvmsgOne(
    _In_ PLX_CALL Call,
    _In_ PLX_FILE File,
    _In_ ULONG64 UserHeader,
    _In_ ULONG Flags)
{
    LX_MSGHDR Header;
    LONG64 Result = LxUnixCopyHeaderIn(UserHeader, &Header), Copy;

    if (Result != 0)
        return Result;
    Result = LxUnixRecvInternal(Call->Process, File, &Header, FALSE, Flags);
    if (Result < 0)
        return Result;
    Copy = LxCopyToUser(UserHeader + FIELD_OFFSET(LX_MSGHDR, NameLength), &Header.NameLength, sizeof(Header.NameLength));
    if (Copy == 0)
        Copy = LxCopyToUser(UserHeader + FIELD_OFFSET(LX_MSGHDR, ControlLength), &Header.ControlLength, sizeof(Header.ControlLength));
    if (Copy == 0)
        Copy = LxCopyToUser(UserHeader + FIELD_OFFSET(LX_MSGHDR, Flags), &Header.Flags, sizeof(Header.Flags));
    return (Copy != 0) ? Copy : Result;
}

LX_SYSCALL(LxSysSendmsg)
{
    PLX_FILE File;
    LONG64 Result;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    Result = LxUnixSendmsgOne(Call, File, Call->Args[1], (ULONG)Call->Args[2]);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysRecvmsg)
{
    PLX_FILE File;
    LONG64 Result;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    Result = LxUnixRecvmsgOne(Call, File, Call->Args[1], (ULONG)Call->Args[2]);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysSendto)
{
    PLX_FILE File;
    LX_MSGHDR Header;
    LX_IOVEC Vector;
    LONG64 Result;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Call->Args[1];
    Vector.Length = Call->Args[2];
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    Header.Name = Call->Args[4];
    Header.NameLength = (LONG)Call->Args[5];
    Result = LxUnixSendInternal(Call->Process, File, &Header, TRUE, (ULONG)Call->Args[3]);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysRecvfrom)
{
    PLX_FILE File;
    LX_MSGHDR Header;
    LX_IOVEC Vector;
    LONG64 Result, Copy;
    LONG Available = 0;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    RtlZeroMemory(&Header, sizeof(Header));
    Vector.Base = Call->Args[1];
    Vector.Length = Call->Args[2];
    Header.Iov = (ULONG64)(ULONG_PTR)&Vector;
    Header.IovLength = 1;
    if ((Call->Args[4] != 0) && (Call->Args[5] != 0))
    {
        Copy = LxCopyFromUser(&Available, Call->Args[5], sizeof(Available));
        if (Copy != 0)
        {
            LxFileDereference(File);
            return Copy;
        }
        Header.Name = Call->Args[4];
        Header.NameLength = Available;
    }
    Result = LxUnixRecvInternal(Call->Process, File, &Header, TRUE, (ULONG)Call->Args[3]);
    if ((Result >= 0) && (Call->Args[4] != 0) && (Call->Args[5] != 0))
    {
        Copy = LxCopyToUser(Call->Args[5], &Header.NameLength, sizeof(Header.NameLength));
        if (Copy != 0)
            Result = Copy;
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysSendmmsg)
{
    PLX_FILE File;
    ULONG Count = (ULONG)min(Call->Args[2], 1024), Index;
    LONG64 Result;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG64 Entry = Call->Args[1] + (ULONG64)Index * sizeof(LX_MMSGHDR);
        ULONG Length;

        Result = LxUnixSendmsgOne(Call, File, Entry, (ULONG)Call->Args[3]);
        if (Result < 0)
            break;
        Length = (ULONG)Result;
        Result = LxCopyToUser(Entry + FIELD_OFFSET(LX_MMSGHDR, Length), &Length, sizeof(Length));
        if (Result != 0)
            break;
    }
    LxFileDereference(File);
    return (Index != 0) ? (LONG64)Index : Result;
}

LX_SYSCALL(LxSysRecvmmsg)
{
    PLX_FILE File;
    ULONG Count = (ULONG)min(Call->Args[2], 1024), Index;
    ULONG Flags = (ULONG)Call->Args[3];
    LONG64 Result;

    File = LxSocketFromFd(Call->Process, (LONG)Call->Args[0], &Result);
    if (File == NULL)
        return Result;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG64 Entry = Call->Args[1] + (ULONG64)Index * sizeof(LX_MMSGHDR);
        ULONG Length;

        Result = LxUnixRecvmsgOne(Call, File, Entry, (Index == 0) ? Flags : (Flags | LX_MSG_DONTWAIT));
        if (Result < 0)
            break;
        Length = (ULONG)Result;
        Result = LxCopyToUser(Entry + FIELD_OFFSET(LX_MMSGHDR, Length), &Length, sizeof(Length));
        if (Result != 0)
            break;
        if (Flags & 0x10000)
            Flags |= LX_MSG_DONTWAIT;
    }
    LxFileDereference(File);
    return (Index != 0) ? (LONG64)Index : Result;
}

LX_SYSCALL(LxSysShutdown)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG64 Result;
    ULONG How = (ULONG)Call->Args[1];
    PLX_UNIX_SOCKET Peer;

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Shutdown(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    if (How > LX_SHUT_RDWR)
        return -LX_EINVAL;
    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    LxUnixLock(Socket);
    if ((Socket->State != LX_UNIX_CONNECTED) && (Socket->Type != LX_SOCK_DGRAM))
    {
        LxUnixUnlock(Socket);
        LxFileDereference(File);
        return -LX_ENOTCONN;
    }
    if ((How == LX_SHUT_RD) || (How == LX_SHUT_RDWR))
        Socket->ShutRead = TRUE;
    if ((How == LX_SHUT_WR) || (How == LX_SHUT_RDWR))
        Socket->ShutWrite = TRUE;
    Peer = Socket->Peer;
    if (Peer != NULL)
        LxUnixReference(Peer);
    LxUnixUnlock(Socket);
    LxPollWake(&Socket->Poll, LX_POLLIN | LX_POLLOUT | LX_POLLHUP | LX_POLLRDHUP);
    if (Peer != NULL)
    {
        if (Socket->Type != LX_SOCK_DGRAM)
        {
            LxUnixLock(Peer);
            if ((How == LX_SHUT_WR) || (How == LX_SHUT_RDWR))
                Peer->ShutRead = TRUE;
            if ((How == LX_SHUT_RD) || (How == LX_SHUT_RDWR))
                Peer->ShutWrite = TRUE;
            LxUnixUnlock(Peer);
        }
        LxPollWake(&Peer->Poll, LX_POLLIN | LX_POLLOUT | LX_POLLRDHUP | LX_POLLHUP);
        LxUnixDereference(Peer);
    }
    LxFileDereference(File);
    return 0;
}

LONG64
LxSockoptInt(
    _In_ ULONG64 Value,
    _In_ ULONG64 Length,
    _Out_ PLONG Result)
{
    if (Length < sizeof(LONG))
        return -LX_EINVAL;
    return LxCopyFromUser(Result, Value, sizeof(LONG));
}

LX_SYSCALL(LxSysSetsockopt)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG64 Result;
    LONG Value = 0;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Setsockopt(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    if (Level != LX_SOL_SOCKET)
    {
        LxFileDereference(File);
        return -LX_ENOPROTOOPT;
    }

    switch (Name)
    {
        case LX_SO_PASSCRED:
            Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
            if (Result == 0)
                Socket->PassCred = (Value != 0);
            break;

        case LX_SO_RCVBUF:
        case LX_SO_RCVBUFFORCE:
            Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
            if (Result == 0)
                Socket->RecvLimit = (ULONG)max(min((LONG64)Value * 2, 64 * 1024 * 1024), 2304);
            break;

        case LX_SO_SNDBUF:
        case LX_SO_SNDBUFFORCE:
            Result = LxSockoptInt(Call->Args[3], Call->Args[4], &Value);
            if (Result == 0)
                Socket->SendLimit = (ULONG)max(min((LONG64)Value * 2, 64 * 1024 * 1024), 4608);
            break;

        case LX_SO_RCVTIMEO_OLD:
        case LX_SO_RCVTIMEO_NEW:
        case LX_SO_SNDTIMEO_OLD:
        case LX_SO_SNDTIMEO_NEW:
        {
            LX_TIMEVAL Timeval;

            if (Call->Args[4] < sizeof(Timeval))
            {
                Result = -LX_EINVAL;
                break;
            }
            Result = LxCopyFromUser(&Timeval, Call->Args[3], sizeof(Timeval));
            if (Result != 0)
                break;
            if ((Name == LX_SO_RCVTIMEO_OLD) || (Name == LX_SO_RCVTIMEO_NEW))
                Socket->RecvTimeout = Timeval.Seconds * 10000000LL + Timeval.Microseconds * 10;
            else
                Socket->SendTimeout = Timeval.Seconds * 10000000LL + Timeval.Microseconds * 10;
            break;
        }

        case LX_SO_DEBUG:
        case LX_SO_REUSEADDR:
        case LX_SO_BROADCAST:
        case LX_SO_KEEPALIVE:
        case LX_SO_PRIORITY:
        case LX_SO_LINGER:
        case LX_SO_REUSEPORT:
        case LX_SO_RCVLOWAT:
        case LX_SO_SNDLOWAT:
        case LX_SO_TIMESTAMP:
        case LX_SO_PASSSEC:
        case LX_SO_PASSPIDFD:
        case 26:
        case 27:
        case 44:
        case 50:
            Result = 0;
            break;

        default:
            Result = -LX_ENOPROTOOPT;
            break;
    }
    LxFileDereference(File);
    return Result;
}

LONG64
LxSockoptReturn(
    _In_ PLX_CALL Call,
    _In_reads_bytes_(Length) const VOID *Value,
    _In_ ULONG Length)
{
    LONG Available;
    LONG64 Result = LxCopyFromUser(&Available, Call->Args[4], sizeof(Available));

    if (Result != 0)
        return Result;
    if (Available < 0)
        return -LX_EINVAL;
    Result = LxCopyToUser(Call->Args[3], Value, min((ULONG)Available, Length));
    if (Result == 0)
    {
        Available = (LONG)min((ULONG)Available, Length);
        Result = LxCopyToUser(Call->Args[4], &Available, sizeof(Available));
    }
    return Result;
}

LX_SYSCALL(LxSysGetsockopt)
{
    const LX_SOCKET_OPS *Family;
    PLX_FILE InetFile;
    LONG64 InetResult;
    PLX_UNIX_SOCKET Socket;
    PLX_FILE File;
    LONG64 Result;
    LONG Value;
    ULONG Level = (ULONG)Call->Args[1], Name = (ULONG)Call->Args[2];

    Family = LxSocketFamilyFromFd(Call->Process, (LONG)Call->Args[0], &InetFile);
    if (Family != NULL)
    {
        InetResult = Family->Getsockopt(Call, InetFile);
        LxFileDereference(InetFile);
        return InetResult;
    }

    Socket = LxUnixFromFd(Call->Process, (LONG)Call->Args[0], &File, &Result);
    if (Socket == NULL)
        return Result;
    if (Level != LX_SOL_SOCKET)
    {
        LxFileDereference(File);
        return -LX_ENOPROTOOPT;
    }

    switch (Name)
    {
        case LX_SO_TYPE:
            Value = (LONG)Socket->Type;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_DOMAIN:
            Value = LX_AF_UNIX;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_PROTOCOL:
            Value = 0;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_ERROR:
            Value = Socket->Error;
            Socket->Error = 0;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_ACCEPTCONN:
            Value = (Socket->State == LX_UNIX_LISTENING);
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_PASSCRED:
            Value = Socket->PassCred;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_RCVBUF:
            Value = (LONG)Socket->RecvLimit;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_SNDBUF:
            Value = (LONG)Socket->SendLimit;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        case LX_SO_PEERCRED:
        {
            LX_UCRED Cred;

            if (!Socket->PeerCredValid)
            {
                Cred.Pid = 0;
                Cred.Uid = Cred.Gid = MAXULONG;
            }
            else
            {
                Cred = Socket->PeerCred;
                Cred.Pid = LxGlobalToNr(Call->Process, Cred.Pid);
                Cred.Uid = LxCredViewId(Call->Process, FALSE, Cred.Uid);
                Cred.Gid = LxCredViewId(Call->Process, TRUE, Cred.Gid);
            }
            Result = LxSockoptReturn(Call, &Cred, sizeof(Cred));
            break;
        }
        case LX_SO_PEERGROUPS:
            Value = 0;
            Result = LxSockoptReturn(Call, &Value, 0);
            break;
        case LX_SO_RCVTIMEO_OLD:
        case LX_SO_RCVTIMEO_NEW:
        case LX_SO_SNDTIMEO_OLD:
        case LX_SO_SNDTIMEO_NEW:
        {
            LX_TIMEVAL Timeval;
            LONG64 Timeout = ((Name == LX_SO_RCVTIMEO_OLD) || (Name == LX_SO_RCVTIMEO_NEW)) ?
                             Socket->RecvTimeout : Socket->SendTimeout;

            Timeval.Seconds = Timeout / 10000000LL;
            Timeval.Microseconds = (Timeout % 10000000LL) / 10;
            Result = LxSockoptReturn(Call, &Timeval, sizeof(Timeval));
            break;
        }
        case LX_SO_REUSEADDR:
        case LX_SO_KEEPALIVE:
        case LX_SO_BROADCAST:
        case LX_SO_PASSSEC:
            Value = 0;
            Result = LxSockoptReturn(Call, &Value, sizeof(Value));
            break;
        default:
            Result = -LX_ENOPROTOOPT;
            break;
    }
    LxFileDereference(File);
    return Result;
}

VOID
LxSocketInitialize(VOID)
{
    ExInitializeFastMutex(&LxUnixBindLock);
    ExInitializeFastMutex(&LxUnixAllLock);
}

#define LX_TCP_ESTABLISHED  1
#define LX_TCP_CLOSE        7
#define LX_TCP_LISTEN       10

static
VOID
LxUnixDiagFill(
    _In_ PLX_UNIX_SOCKET Socket,
    _Out_ PLX_UNIX_DIAG Item)
{
    PLX_UNIX_SOCKET Peer;
    PLIST_ENTRY Entry;
    ULONG Major, Minor;

    RtlZeroMemory(Item, sizeof(*Item));
    LxUnixLock(Socket);
    Peer = Socket->Peer;
    Item->Type = Socket->Type;
    if (Socket->State == LX_UNIX_LISTENING)
        Item->State = LX_TCP_LISTEN;
    else if ((Socket->State == LX_UNIX_CONNECTED) || ((Socket->Type == LX_SOCK_DGRAM) && (Peer != NULL)))
        Item->State = LX_TCP_ESTABLISHED;
    else
        Item->State = LX_TCP_CLOSE;
    Item->Ino = Socket->Ino;
    Item->PeerIno = (Peer != NULL) ? (ULONG)Peer->Ino : 0;
    Item->Uid = Socket->Cred.Uid;
    Item->RecvBytes = Socket->RecvBytes;
    Item->RecvBuffer = Socket->RecvLimit;
    Item->SendBuffer = Socket->SendLimit;
    if (Socket->State == LX_UNIX_LISTENING)
    {
        Item->RecvQueue = Socket->BacklogCount;
        Item->SendQueue = Socket->BacklogMax;
    }
    else
    {
        if (Socket->Type != LX_SOCK_DGRAM)
            Item->RecvQueue = Socket->RecvBytes;
        else if (!IsListEmpty(&Socket->RecvQueue))
            Item->RecvQueue = CONTAINING_RECORD(Socket->RecvQueue.Flink, LX_UNIX_MSG, Link)->Length;
        if ((Socket->Type != LX_SOCK_DGRAM) && (Peer != NULL))
            Item->SendQueue = Peer->RecvBytes;
    }
    Item->Shutdown = ((Socket->ShutRead || Socket->PeerClosed) ? 1 : 0) | ((Socket->ShutWrite || Socket->PeerClosed) ? 2 : 0);
    if (Socket->Bound && (Socket->AddressLength > sizeof(USHORT)))
    {
        Item->NameLength = (USHORT)min(Socket->AddressLength - sizeof(USHORT), sizeof(Item->Name));
        RtlCopyMemory(Item->Name, Socket->Address + sizeof(USHORT), Item->NameLength);
        if ((Item->Name[0] != '\0') && (Socket->BoundMount != NULL))
        {
            Major = (Socket->BoundMount->Dev >> 8) & 0xFFF;
            Minor = (Socket->BoundMount->Dev & 0xFF) | ((Socket->BoundMount->Dev >> 12) & 0xFFF00);
            Item->HasVfs = TRUE;
            Item->VfsIno = (ULONG)Socket->BoundId;
            Item->VfsDev = (Major << 20) | Minor;
        }
    }
    if (Socket->State == LX_UNIX_LISTENING)
    {
        for (Entry = Socket->Backlog.Flink; Entry != &Socket->Backlog; Entry = Entry->Flink)
            Item->IconCount++;
        if (Item->IconCount != 0)
            Item->Icons = LxAlloc(Item->IconCount * sizeof(ULONG));
        if (Item->Icons == NULL)
            Item->IconCount = 0;
        Minor = 0;
        for (Entry = Socket->Backlog.Flink; (Entry != &Socket->Backlog) && (Minor < Item->IconCount); Entry = Entry->Flink)
            Item->Icons[Minor++] = (ULONG)CONTAINING_RECORD(Entry, LX_UNIX_SOCKET, BacklogLink)->Ino;
    }
    LxUnixUnlock(Socket);
}

LONG
LxUnixDiagCollect(
    _In_ PLX_NS NetNs,
    _In_ ULONG64 Ino,
    _Out_ PLX_UNIX_DIAG *Entries,
    _Out_ PULONG Count)
{
    PLX_UNIX_SOCKET *Sockets;
    PLX_UNIX_DIAG Diag = NULL;
    PLIST_ENTRY Entry;
    ULONG Capacity = 16, Found = 0, Index;

    *Entries = NULL;
    *Count = 0;
    ExAcquireFastMutex(&LxUnixAllLock);
    for (Entry = LxUnixAll.Flink; Entry != &LxUnixAll; Entry = Entry->Flink)
        Capacity++;
    ExReleaseFastMutex(&LxUnixAllLock);

    Sockets = LxAlloc(Capacity * sizeof(*Sockets));
    if (Sockets == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&LxUnixAllLock);
    for (Entry = LxUnixAll.Flink; (Entry != &LxUnixAll) && (Found < Capacity); Entry = Entry->Flink)
    {
        PLX_UNIX_SOCKET Socket = CONTAINING_RECORD(Entry, LX_UNIX_SOCKET, AllLink);
        LONG Old;

        if ((Socket->NetNs != NetNs) || ((Ino != 0) && (Socket->Ino != Ino)))
            continue;
        Old = Socket->RefCount;
        while ((Old != 0) && (InterlockedCompareExchange(&Socket->RefCount, Old + 1, Old) != Old))
            Old = Socket->RefCount;
        if (Old != 0)
            Sockets[Found++] = Socket;
    }
    ExReleaseFastMutex(&LxUnixAllLock);

    if (Found != 0)
        Diag = LxAlloc(Found * sizeof(*Diag));
    for (Index = 0; Index < Found; Index++)
    {
        if (Diag != NULL)
            LxUnixDiagFill(Sockets[Index], &Diag[Index]);
        LxUnixDereference(Sockets[Index]);
    }
    LxFree(Sockets);
    if ((Found != 0) && (Diag == NULL))
        return -LX_ENOMEM;
    *Entries = Diag;
    *Count = Found;
    return 0;
}

VOID
LxUnixDiagFree(
    _In_opt_ PLX_UNIX_DIAG Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL)
        return;
    for (Index = 0; Index < Count; Index++)
    {
        if (Entries[Index].Icons != NULL)
            LxFree(Entries[Index].Icons);
    }
    LxFree(Entries);
}
