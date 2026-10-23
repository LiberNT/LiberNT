/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Anonymous pipes and FIFOs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_PIPE_DEFAULT_SIZE    65536
#define LX_PIPE_MAX_SIZE        1048576
#define LX_PIPE_BUF             4096

typedef struct _LX_PIPE
{
    volatile LONG RefCount;
    KEVENT Lock;
    PUCHAR Buffer;
    ULONG Size;
    ULONG Head;
    ULONG Count;
    ULONG Readers;
    ULONG Writers;
    BOOLEAN HadWriter;
    LX_POLL_HEAD Poll;
    ULONG64 Ino;
    LIST_ENTRY FifoLink;
    PLX_MOUNT FifoMount;
    LONG64 FifoId;
} LX_PIPE, *PLX_PIPE;

static LIST_ENTRY LxFifoList = { &LxFifoList, &LxFifoList };
static KSPIN_LOCK LxFifoLock;
static volatile LONG64 LxPipeInode = 0x200000;

static
VOID
LxPipeLock(
    _In_ PLX_PIPE Pipe)
{
    KeWaitForSingleObject(&Pipe->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxPipeUnlock(
    _In_ PLX_PIPE Pipe)
{
    KeSetEvent(&Pipe->Lock, IO_NO_INCREMENT, FALSE);
}

static
PLX_PIPE
LxPipeCreate(VOID)
{
    PLX_PIPE Pipe = LxAlloc(sizeof(*Pipe));

    if (Pipe == NULL)
        return NULL;
    Pipe->Buffer = LxAlloc(LX_PIPE_DEFAULT_SIZE);
    if (Pipe->Buffer == NULL)
    {
        LxFree(Pipe);
        return NULL;
    }
    Pipe->RefCount = 1;
    Pipe->Size = LX_PIPE_DEFAULT_SIZE;
    KeInitializeEvent(&Pipe->Lock, SynchronizationEvent, TRUE);
    LxPollHeadInit(&Pipe->Poll);
    InitializeListHead(&Pipe->FifoLink);
    Pipe->Ino = (ULONG64)InterlockedIncrement64(&LxPipeInode);
    return Pipe;
}

static
VOID
LxPipeDereference(
    _In_ PLX_PIPE Pipe)
{
    KIRQL Irql;

    if (Pipe->FifoMount != NULL)
    {
        KeAcquireSpinLock(&LxFifoLock, &Irql);
        if (InterlockedDecrement(&Pipe->RefCount) != 0)
        {
            KeReleaseSpinLock(&LxFifoLock, Irql);
            return;
        }
        RemoveEntryList(&Pipe->FifoLink);
        KeReleaseSpinLock(&LxFifoLock, Irql);
    }
    else if (InterlockedDecrement(&Pipe->RefCount) != 0)
    {
        return;
    }
    LxFree(Pipe->Buffer);
    LxFree(Pipe);
}

static
LONG64
LxPipeRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_PIPE Pipe = File->Object;
    LONG64 Result;

    if (Offset != NULL)
        return -LX_ESPIPE;

    for (;;)
    {
        LxPipeLock(Pipe);
        if (Pipe->Count != 0)
        {
            ULONG Total = (ULONG)min(Length, Pipe->Count);
            ULONG First = min(Total, Pipe->Size - Pipe->Head);

            Result = LxCopyToUser(Buffer, Pipe->Buffer + Pipe->Head, First);
            if ((Result == 0) && (First < Total))
                Result = LxCopyToUser(Buffer + First, Pipe->Buffer, Total - First);
            if (Result == 0)
            {
                Pipe->Head = (Pipe->Head + Total) % Pipe->Size;
                Pipe->Count -= Total;
                if (Pipe->Count == 0)
                    Pipe->Head = 0;
                Result = Total;
            }
            LxPipeUnlock(Pipe);
            LxPollWake(&Pipe->Poll, LX_POLLOUT | LX_POLLWRNORM);
            return Result;
        }
        if (Pipe->Writers == 0)
        {
            LxPipeUnlock(Pipe);
            return 0;
        }
        LxPipeUnlock(Pipe);

        if (File->Flags & LX_O_NONBLOCK)
            return -LX_EAGAIN;
        Result = LxFileWait(File, LX_POLLIN | LX_POLLHUP, NULL);
        if (Result < 0)
            return Result;
    }
}

static
LONG64
LxPipeWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_PIPE Pipe = File->Object;
    ULONG64 Written = 0;
    LONG64 Result;

    if (Offset != NULL)
        return -LX_ESPIPE;

    while (Written < Length)
    {
        ULONG Space, Chunk, Tail, First;

        LxPipeLock(Pipe);
        if (Pipe->Readers == 0)
        {
            LxPipeUnlock(Pipe);
            return (Written != 0) ? (LONG64)Written : LxWriteFailed(LX_EPIPE);
        }

        Space = Pipe->Size - Pipe->Count;
        if ((Space == 0) || ((Length - Written <= LX_PIPE_BUF) && (Space < Length - Written)))
        {
            LxPipeUnlock(Pipe);
            if (Written != 0)
                LxPollWake(&Pipe->Poll, LX_POLLIN | LX_POLLRDNORM);
            if (File->Flags & LX_O_NONBLOCK)
                return (Written != 0) ? (LONG64)Written : -LX_EAGAIN;
            Result = LxFileWait(File, LX_POLLOUT, NULL);
            if (Result < 0)
                return (Written != 0) ? (LONG64)Written : Result;
            continue;
        }

        Chunk = (ULONG)min(Space, Length - Written);
        Tail = (Pipe->Head + Pipe->Count) % Pipe->Size;
        First = min(Chunk, Pipe->Size - Tail);
        Result = LxCopyFromUser(Pipe->Buffer + Tail, Buffer + Written, First);
        if ((Result == 0) && (First < Chunk))
            Result = LxCopyFromUser(Pipe->Buffer, Buffer + Written + First, Chunk - First);
        if (Result != 0)
        {
            LxPipeUnlock(Pipe);
            return (Written != 0) ? (LONG64)Written : Result;
        }
        Pipe->Count += Chunk;
        Written += Chunk;
        LxPipeUnlock(Pipe);
        LxPollWake(&Pipe->Poll, LX_POLLIN | LX_POLLRDNORM);
    }
    return (LONG64)Written;
}

static
ULONG
LxPipePoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_PIPE Pipe = File->Object;
    ULONG Access = File->Flags & LX_O_ACCMODE;
    ULONG Mask = 0;

    if (Entry != NULL)
        LxPollAdd(&Pipe->Poll, Entry);

    LxPipeLock(Pipe);
    if ((Access == LX_O_RDONLY) || (Access == LX_O_RDWR))
    {
        if (Pipe->Count != 0)
            Mask |= LX_POLLIN | LX_POLLRDNORM;
        if ((Pipe->Writers == 0) && Pipe->HadWriter)
            Mask |= LX_POLLHUP;
    }
    if ((Access == LX_O_WRONLY) || (Access == LX_O_RDWR))
    {
        if (Pipe->Size - Pipe->Count >= LX_PIPE_BUF)
            Mask |= LX_POLLOUT | LX_POLLWRNORM;
        if (Pipe->Readers == 0)
            Mask |= LX_POLLERR;
    }
    LxPipeUnlock(Pipe);
    return Mask;
}

static
LONG
LxPipeStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_PIPE Pipe = File->Object;
    LX_TIMESPEC Now;
    LARGE_INTEGER System;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFIFO | 0600;
    Stat->Nlink = 1;
    Stat->Ino = Pipe->Ino;
    Stat->Dev = 0xC;
    Stat->BlkSize = 4096;
    KeQuerySystemTime(&System);
    LxNtTimeToTimespec(System.QuadPart, &Now);
    Stat->Atime = Stat->Mtime = Stat->Ctime = Now;
    return 0;
}

static
LONG64
LxPipeIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_PIPE Pipe = File->Object;
    LONG Available;

    if (Request != LX_FIONREAD)
        return -LX_ENOTTY;
    LxPipeLock(Pipe);
    Available = (LONG)Pipe->Count;
    LxPipeUnlock(Pipe);
    return LxCopyToUser(Argument, &Available, sizeof(Available));
}

static
VOID
LxPipeRelease(
    _In_ PLX_FILE File)
{
    PLX_PIPE Pipe = File->Object;
    ULONG Access = File->Flags & LX_O_ACCMODE;

    LxPipeLock(Pipe);
    if ((Access == LX_O_RDONLY) || (Access == LX_O_RDWR))
        Pipe->Readers--;
    if ((Access == LX_O_WRONLY) || (Access == LX_O_RDWR))
        Pipe->Writers--;
    if (Pipe->FifoMount != NULL)
    {
        if ((Pipe->Readers == 0) && (Pipe->Writers == 0))
        {
            Pipe->Count = 0;
            Pipe->Head = 0;
            Pipe->HadWriter = FALSE;
        }
    }
    LxPipeUnlock(Pipe);
    LxPollWake(&Pipe->Poll, LX_POLLIN | LX_POLLOUT | LX_POLLHUP | LX_POLLERR);
    LxPipeDereference(Pipe);
}

static const LX_FILE_OPS LxPipeOps =
{
    LxPipeRead, LxPipeWrite, LxPipeIoctl, LxPipePoll, LxPipeStat, NULL, NULL, NULL, LxPipeRelease
};

BOOLEAN
LxFileIsPipe(
    _In_ PLX_FILE File)
{
    return File->Ops == &LxPipeOps;
}

LONG
LxPipeSetSize(
    _In_ PLX_FILE File,
    _In_ ULONG Size)
{
    PLX_PIPE Pipe = File->Object;
    PUCHAR Buffer;
    ULONG Index;

    if (File->Ops != &LxPipeOps)
        return -LX_EBADF;
    if (Size > LX_PIPE_MAX_SIZE)
        return -LX_EPERM;
    if (Size < LX_PAGE_SIZE)
        Size = LX_PAGE_SIZE;
    Size = (ULONG)LX_ALIGN_UP(Size, LX_PAGE_SIZE);

    Buffer = LxAlloc(Size);
    if (Buffer == NULL)
        return -LX_ENOMEM;

    LxPipeLock(Pipe);
    if (Pipe->Count > Size)
    {
        LxPipeUnlock(Pipe);
        LxFree(Buffer);
        return -LX_EBUSY;
    }
    for (Index = 0; Index < Pipe->Count; Index++)
        Buffer[Index] = Pipe->Buffer[(Pipe->Head + Index) % Pipe->Size];
    LxFree(Pipe->Buffer);
    Pipe->Buffer = Buffer;
    Pipe->Size = Size;
    Pipe->Head = 0;
    LxPipeUnlock(Pipe);
    LxPollWake(&Pipe->Poll, LX_POLLOUT | LX_POLLWRNORM);
    return (LONG)Size;
}

LONG
LxPipeGetSize(
    _In_ PLX_FILE File)
{
    if (File->Ops != &LxPipeOps)
        return -LX_EBADF;
    return (LONG)((PLX_PIPE)File->Object)->Size;
}

static
PLX_FILE
LxPipeOpenEnd(
    _In_ PLX_PIPE Pipe,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxFileAlloc(&LxPipeOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFIFO);
    ULONG Access = Flags & LX_O_ACCMODE;

    if (File == NULL)
        return NULL;

    InterlockedIncrement(&Pipe->RefCount);
    File->Object = Pipe;
    File->Ino = Pipe->Ino;
    LxPipeLock(Pipe);
    if ((Access == LX_O_RDONLY) || (Access == LX_O_RDWR))
        Pipe->Readers++;
    if ((Access == LX_O_WRONLY) || (Access == LX_O_RDWR))
    {
        Pipe->Writers++;
        Pipe->HadWriter = TRUE;
    }
    LxPipeUnlock(Pipe);
    LxPollWake(&Pipe->Poll, LX_POLLIN | LX_POLLOUT);
    return File;
}

LONG
LxPipeCreatePair(
    _In_ ULONG Flags,
    _Out_ PLX_FILE *ReadEnd,
    _Out_ PLX_FILE *WriteEnd)
{
    PLX_PIPE Pipe = LxPipeCreate();
    CHAR Name[40];

    *ReadEnd = *WriteEnd = NULL;
    if (Pipe == NULL)
        return -LX_ENOMEM;

    *ReadEnd = LxPipeOpenEnd(Pipe, LX_O_RDONLY | (Flags & LX_O_NONBLOCK));
    *WriteEnd = LxPipeOpenEnd(Pipe, LX_O_WRONLY | (Flags & LX_O_NONBLOCK));
    LxPipeDereference(Pipe);
    if ((*ReadEnd == NULL) || (*WriteEnd == NULL))
    {
        if (*ReadEnd != NULL)
            LxFileDereference(*ReadEnd);
        if (*WriteEnd != NULL)
            LxFileDereference(*WriteEnd);
        *ReadEnd = *WriteEnd = NULL;
        return -LX_ENOMEM;
    }

    RtlStringCbPrintfA(Name, sizeof(Name), "pipe:[%llu]", Pipe->Ino);
    (*ReadEnd)->Path = LxStrDup(Name);
    (*WriteEnd)->Path = LxStrDup(Name);
    return 0;
}

LX_SYSCALL(LxSysPipe2)
{
    PLX_FILE ReadEnd, WriteEnd;
    ULONG Flags = (ULONG)Call->Args[1];
    LONG Fds[2];
    LONG64 Result;
    LONG Error;

    if (Flags & ~(LX_O_CLOEXEC | LX_O_NONBLOCK | LX_O_DIRECT))
        return -LX_EINVAL;

    Error = LxPipeCreatePair(Flags, &ReadEnd, &WriteEnd);
    if (Error != 0)
        return Error;

    Fds[0] = LxFdInstall(Call->Process, ReadEnd, 0, (Flags & LX_O_CLOEXEC) != 0);
    Fds[1] = (Fds[0] >= 0) ? LxFdInstall(Call->Process, WriteEnd, 0, (Flags & LX_O_CLOEXEC) != 0) : -LX_EMFILE;
    LxFileDereference(ReadEnd);
    LxFileDereference(WriteEnd);
    if (Fds[1] < 0)
    {
        if (Fds[0] >= 0)
            LxFdClose(Call->Process, Fds[0]);
        return Fds[1];
    }

    Result = LxCopyToUser(Call->Args[0], Fds, sizeof(Fds));
    if (Result != 0)
    {
        LxFdClose(Call->Process, Fds[0]);
        LxFdClose(Call->Process, Fds[1]);
    }
    return Result;
}

static
BOOLEAN
LxFifoHasPeer(
    _In_ PLX_PIPE Pipe,
    _In_ ULONG Access)
{
    BOOLEAN Result;

    LxPipeLock(Pipe);
    Result = (Access == LX_O_RDONLY) ? (Pipe->Writers != 0) : (Pipe->Readers != 0);
    LxPipeUnlock(Pipe);
    return Result;
}

LONG
LxFifoOpen(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    FILE_INTERNAL_INFORMATION Internal;
    IO_STATUS_BLOCK Iosb;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PLX_PIPE Pipe = NULL, Created = NULL;
    PLIST_ENTRY Entry;
    ULONG Access = Flags & LX_O_ACCMODE;
    HANDLE Handle;
    NTSTATUS Status;
    PLX_FILE File;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Process);

    *Result = NULL;
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

    for (;;)
    {
        KeAcquireSpinLock(&LxFifoLock, &Irql);
        for (Entry = LxFifoList.Flink; Entry != &LxFifoList; Entry = Entry->Flink)
        {
            PLX_PIPE Candidate = CONTAINING_RECORD(Entry, LX_PIPE, FifoLink);

            if ((Candidate->FifoMount == Node->Mount) && (Candidate->FifoId == Internal.IndexNumber.QuadPart))
            {
                InterlockedIncrement(&Candidate->RefCount);
                Pipe = Candidate;
                break;
            }
        }
        if ((Pipe == NULL) && (Created != NULL))
        {
            Created->FifoMount = Node->Mount;
            Created->FifoId = Internal.IndexNumber.QuadPart;
            InsertTailList(&LxFifoList, &Created->FifoLink);
            Pipe = Created;
            Created = NULL;
        }
        KeReleaseSpinLock(&LxFifoLock, Irql);

        if (Pipe != NULL)
            break;
        Created = LxPipeCreate();
        if (Created == NULL)
            return -LX_ENOMEM;
    }
    if (Created != NULL)
        LxPipeDereference(Created);

    if ((Access == LX_O_WRONLY) && (Flags & LX_O_NONBLOCK) && !LxFifoHasPeer(Pipe, Access))
    {
        LxPipeDereference(Pipe);
        return -LX_ENXIO;
    }

    File = LxPipeOpenEnd(Pipe, Flags);
    LxPipeDereference(Pipe);
    if (File == NULL)
        return -LX_ENOMEM;
    File->Path = LxStrDup(Node->Path);
    File->Mount = Node->Mount;

    if ((Access != LX_O_RDWR) && !(Flags & LX_O_NONBLOCK))
    {
        while (!LxFifoHasPeer(Pipe, Access))
        {
            LONG64 Wait = LxFileWait(File, (Access == LX_O_RDONLY) ? LX_POLLIN : LX_POLLOUT, NULL);

            if (LxFifoHasPeer(Pipe, Access))
                break;
            if (Wait < 0)
            {
                LxFileDereference(File);
                return (LONG)Wait;
            }
            if (Access == LX_O_WRONLY)
            {
                LARGE_INTEGER Delay;

                Delay.QuadPart = -100000;
                KeDelayExecutionThread(UserMode, TRUE, &Delay);
            }
        }
    }

    *Result = File;
    return 0;
}

VOID
LxPipeInitialize(VOID)
{
    KeInitializeSpinLock(&LxFifoLock);
}
