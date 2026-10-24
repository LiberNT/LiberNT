/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Open file descriptions, descriptor tables and file operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_UNIX_EPOCH_100NS 116444736000000000LL
#define LX_DIR_BUFFER_SIZE  (64 * 1024)

static volatile LONG64 LxAnonInode = 0x100000;

PLX_THREAD
LxCurrentThread(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();

    if (!PsIsSystemThread(Thread))
        return PsPicoGetThreadContext(Thread);
    return NULL;
}

VOID
LxNtTimeToTimespec(
    _In_ LONG64 NtTime,
    _Out_ PLX_TIMESPEC Timespec)
{
    LONG64 Unix = NtTime - LX_UNIX_EPOCH_100NS;
    LONG64 Remainder = Unix % 10000000;

    Timespec->Seconds = Unix / 10000000;
    if (Remainder < 0)
    {
        Remainder += 10000000;
        Timespec->Seconds--;
    }
    Timespec->Nanoseconds = Remainder * 100;
}

LONG64
LxTimespecToNtTime(
    _In_ PLX_TIMESPEC Timespec)
{
    return Timespec->Seconds * 10000000 + Timespec->Nanoseconds / 100 + LX_UNIX_EPOCH_100NS;
}

LONG64
LxTimespecToNs(
    _In_ PLX_TIMESPEC Timespec)
{
    if (Timespec->Seconds >= MAXLONG64 / 1000000000LL - 1)
        return MAXLONG64;
    return Timespec->Seconds * 1000000000LL + Timespec->Nanoseconds;
}

NTSTATUS
LxFillRandom(
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    return BCryptGenRandom(NULL, Buffer, Length, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

VOID
LxUnicodeDecodeLinuxName(
    _Inout_updates_(Count) PWCHAR Name,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        WCHAR Character = Name[Index];

        if ((Character > 0xF000) && (Character < 0xF080))
        {
            WCHAR Plain = Character - 0xF000;

            if ((Plain < 0x20) || (Plain == ':') || (Plain == '*') || (Plain == '?') ||
                (Plain == '"') || (Plain == '<') || (Plain == '>') || (Plain == '|') || (Plain == '\\'))
            {
                Name[Index] = Plain;
            }
        }
    }
}

PLX_FILE
LxFileAlloc(
    _In_ const LX_FILE_OPS *Ops,
    _In_ ULONG Flags,
    _In_ ULONG Type)
{
    PLX_FILE File = LxAlloc(sizeof(*File));

    if (File == NULL)
        return NULL;

    File->RefCount = 1;
    File->Ops = Ops;
    File->Flags = Flags;
    File->Type = Type;
    KeInitializeEvent(&File->Lock, SynchronizationEvent, TRUE);
    KeInitializeSpinLock(&File->EpollLock);
    InitializeListHead(&File->EpollItems);
    File->Ino = (ULONG64)InterlockedIncrement64(&LxAnonInode);
    return File;
}

VOID
LxFileReference(
    _In_ PLX_FILE File)
{
    InterlockedIncrement(&File->RefCount);
}

#define LX_POLL_IN          1
#define LX_POLL_OUT         2
#define LX_POLL_ERR         4
#define LX_POLL_PRI         5
#define LX_POLL_HUP         6

typedef struct _LX_FILE_ASYNC
{
    volatile LONG RefCount;
    FAST_MUTEX Lock;
    PLX_INSTANCE Instance;
    LX_POLL_ENTRY Entry;
    WORK_QUEUE_ITEM Work;
    volatile LONG Queued;
    volatile LONG Events;
    ULONG Access;
    LONG OwnerType;
    ULONG Owner;
    LONG Signal;
    LONG Fd;
    BOOLEAN Registered;
} LX_FILE_ASYNC, *PLX_FILE_ASYNC;

static
VOID
LxFileAsyncDereference(
    _In_ PLX_FILE_ASYNC Async)
{
    if (InterlockedDecrement(&Async->RefCount) != 0)
        return;
    if (Async->Instance != NULL)
        LxInstanceDereference(Async->Instance);
    LxFree(Async);
}

static
VOID
LxFileAsyncDeliver(
    _In_ PLX_FILE_ASYNC Async,
    _In_ ULONG Events)
{
    static const ULONG Bands[6] =
    {
        LX_POLLIN | LX_POLLRDNORM,
        LX_POLLOUT | LX_POLLWRNORM | LX_POLLWRBAND,
        LX_POLLIN | LX_POLLRDNORM | 0x400,
        LX_POLLERR,
        LX_POLLPRI | LX_POLLRDBAND,
        LX_POLLHUP | LX_POLLERR
    };
    PLX_PROCESS Target = NULL;
    PLX_THREAD Thread = NULL;
    LX_SIGINFO Info;
    LONG Type, Signal, Fd, Reason;
    ULONG Owner, Access;
    BOOLEAN Registered;

    ExAcquireFastMutex(&Async->Lock);
    Registered = Async->Registered;
    Type = Async->OwnerType;
    Owner = Async->Owner;
    Signal = Async->Signal;
    Fd = Async->Fd;
    Access = Async->Access;
    ExReleaseFastMutex(&Async->Lock);
    if (!Registered || (Owner == 0) || (Async->Instance == NULL))
        return;

    if ((Access != LX_O_WRONLY) && (Events & (LX_POLLIN | LX_POLLRDNORM)))
        Reason = LX_POLL_IN;
    else if ((Access != LX_O_RDONLY) && (Events & (LX_POLLOUT | LX_POLLWRNORM | LX_POLLWRBAND)))
        Reason = LX_POLL_OUT;
    else if (Events & (LX_POLLPRI | LX_POLLRDBAND))
        Reason = LX_POLL_PRI;
    else if (Events & LX_POLLHUP)
        Reason = LX_POLL_HUP;
    else if (Events & LX_POLLERR)
        Reason = LX_POLL_ERR;
    else
        return;

    RtlZeroMemory(&Info, sizeof(Info));
    if (Signal != 0)
    {
        LONG64 Band = Bands[Reason - LX_POLL_IN];

        Info.Signo = Signal;
        Info.Code = Reason;
        RtlCopyMemory(&Info.Raw[0], &Band, sizeof(Band));
        RtlCopyMemory(&Info.Raw[8], &Fd, sizeof(Fd));
    }
    else
    {
        Signal = LX_SIGIO;
        Info.Signo = LX_SIGIO;
        Info.Code = LX_SI_KERNEL;
    }

    if (Type == LX_F_OWNER_PGRP)
    {
        LxSignalGroupInfo(Async->Instance, NULL, Owner, Signal, &Info, NULL, NULL);
        return;
    }
    if (Type == LX_F_OWNER_TID)
        Thread = LxThreadLookupReferenced(Async->Instance, Owner, &Target);
    else
        Target = LxProcessLookup(Async->Instance, Owner);
    if (Target == NULL)
        return;
    if ((Type != LX_F_OWNER_TID) || (Thread != NULL))
        LxSignalSend(Target, Thread, Signal, &Info);
    LxProcessDereference(Target);
}

static
VOID
NTAPI
LxFileAsyncWork(
    _In_ PVOID Context)
{
    PLX_FILE_ASYNC Async = Context;
    LONG Events;

    InterlockedExchange(&Async->Queued, 0);
    Events = InterlockedExchange(&Async->Events, 0);
    LxFileAsyncDeliver(Async, (ULONG)Events);
    LxFileAsyncDereference(Async);
}

static
VOID
LxFileAsyncNotify(
    _In_ PLX_POLL_ENTRY Entry,
    _In_ ULONG Events)
{
    PLX_FILE_ASYNC Async = Entry->Context;

    InterlockedOr(&Async->Events, (LONG)Events);
    if (InterlockedExchange(&Async->Queued, 1) == 0)
    {
        InterlockedIncrement(&Async->RefCount);
        ExQueueWorkItem(&Async->Work, DelayedWorkQueue);
    }
}

static
PLX_FILE_ASYNC
LxFileAsyncGet(
    _In_ PLX_FILE File,
    _In_ BOOLEAN Create)
{
    PLX_FILE_ASYNC Async = File->Async;
    PLX_THREAD Current = LxCurrentThread();

    if ((Async != NULL) || !Create)
        return Async;
    Async = LxAlloc(sizeof(*Async));
    if (Async == NULL)
        return NULL;
    Async->RefCount = 1;
    ExInitializeFastMutex(&Async->Lock);
    Async->OwnerType = LX_F_OWNER_PID;
    Async->Fd = -1;
    Async->Entry.Notify = LxFileAsyncNotify;
    Async->Entry.Context = Async;
    ExInitializeWorkItem(&Async->Work, LxFileAsyncWork, Async);
    if (Current != NULL)
    {
        Async->Instance = Current->Process->Instance;
        LxInstanceReference(Async->Instance);
    }
    if (InterlockedCompareExchangePointer((PVOID *)&File->Async, Async, NULL) != NULL)
    {
        LxFileAsyncDereference(Async);
        Async = File->Async;
    }
    return Async;
}

LONG
LxFileSetOwner(
    _In_ PLX_FILE File,
    _In_ LONG Type,
    _In_ ULONG Owner)
{
    PLX_FILE_ASYNC Async = LxFileAsyncGet(File, TRUE);

    if (Async == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&Async->Lock);
    Async->OwnerType = Type;
    Async->Owner = Owner;
    ExReleaseFastMutex(&Async->Lock);
    return 0;
}

VOID
LxFileGetOwner(
    _In_ PLX_FILE File,
    _Out_ PLONG Type,
    _Out_ PULONG Owner)
{
    PLX_FILE_ASYNC Async = LxFileAsyncGet(File, FALSE);

    *Type = LX_F_OWNER_PID;
    *Owner = 0;
    if (Async == NULL)
        return;
    ExAcquireFastMutex(&Async->Lock);
    *Type = Async->OwnerType;
    *Owner = Async->Owner;
    ExReleaseFastMutex(&Async->Lock);
}

LONG
LxFileSetSignal(
    _In_ PLX_FILE File,
    _In_ LONG Signal)
{
    PLX_FILE_ASYNC Async = LxFileAsyncGet(File, TRUE);

    if (Async == NULL)
        return -LX_ENOMEM;
    Async->Signal = Signal;
    return 0;
}

LONG
LxFileGetSignal(
    _In_ PLX_FILE File)
{
    PLX_FILE_ASYNC Async = LxFileAsyncGet(File, FALSE);

    return (Async != NULL) ? Async->Signal : 0;
}

VOID
LxFileSetAsync(
    _In_ PLX_FILE File,
    _In_ LONG Fd,
    _In_ BOOLEAN Enable)
{
    PLX_FILE_ASYNC Async = LxFileAsyncGet(File, Enable);

    if (Async == NULL)
        return;
    ExAcquireFastMutex(&Async->Lock);
    Async->Fd = Fd;
    if (Enable && !Async->Registered && (File->Ops->Poll != NULL))
    {
        Async->Access = File->Flags & LX_O_ACCMODE;
        Async->Registered = TRUE;
        File->Ops->Poll(File, &Async->Entry);
    }
    else if (!Enable && Async->Registered)
    {
        LxPollRemove(&Async->Entry);
        Async->Registered = FALSE;
    }
    ExReleaseFastMutex(&Async->Lock);
}

static
VOID
LxFileAsyncRelease(
    _In_ PLX_FILE File)
{
    PLX_FILE_ASYNC Async = File->Async;

    if (Async == NULL)
        return;
    ExAcquireFastMutex(&Async->Lock);
    if (Async->Registered)
    {
        LxPollRemove(&Async->Entry);
        Async->Registered = FALSE;
    }
    ExReleaseFastMutex(&Async->Lock);
    File->Async = NULL;
    LxFileAsyncDereference(Async);
}

VOID
LxFileDereference(
    _In_ PLX_FILE File)
{
    if (InterlockedDecrement(&File->RefCount) != 0)
        return;

    LxEpollFileReleased(File);
    LxFileAsyncRelease(File);
    if (File->Written)
        LxInotifyFileEvent(File, LX_IN_CLOSE_WRITE);
    if (File->Ops->Release != NULL)
        File->Ops->Release(File);
    if (File->PidProcess != NULL)
        LxProcessDereference(File->PidProcess);
    if (File->Handle != NULL)
        ZwClose(File->Handle);
    LxFree(File->DirBuffer);
    LxFree(File->Data);
    LxFree(File->Path);
    LxFree(File);
}

PLX_FILE
LxFdGet(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd)
{
    PLX_FILE File;

    if ((Fd < 0) || (Fd >= LX_MAX_FDS))
        return NULL;

    ExAcquireFastMutex(&Process->Lock);
    File = Process->Fds[Fd];
    if (File != NULL)
        LxFileReference(File);
    ExReleaseFastMutex(&Process->Lock);
    return File;
}

LONG
LxFdInstall(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ LONG MinFd,
    _In_ BOOLEAN CloseOnExec)
{
    ULONG Limit = (ULONG)min(Process->Limits[LX_RLIMIT_NOFILE].Cur, LX_MAX_FDS);
    LONG Fd;

    if ((MinFd < 0) || (MinFd >= LX_MAX_FDS))
        return -LX_EINVAL;

    ExAcquireFastMutex(&Process->Lock);
    for (Fd = MinFd; Fd < (LONG)Limit; Fd++)
    {
        if (Process->Fds[Fd] == NULL)
        {
            LxFileReference(File);
            Process->Fds[Fd] = File;
            Process->FdFlags[Fd] = CloseOnExec ? LX_FD_CLOEXEC : 0;
            ExReleaseFastMutex(&Process->Lock);
            return Fd;
        }
    }
    ExReleaseFastMutex(&Process->Lock);
    return -LX_EMFILE;
}

LONG64
LxFdClose(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd)
{
    PLX_FILE File;

    if ((Fd < 0) || (Fd >= LX_MAX_FDS))
        return -LX_EBADF;

    ExAcquireFastMutex(&Process->Lock);
    File = Process->Fds[Fd];
    Process->Fds[Fd] = NULL;
    Process->FdFlags[Fd] = 0;
    ExReleaseFastMutex(&Process->Lock);

    if (File == NULL)
        return -LX_EBADF;

    LxFileDereference(File);
    return 0;
}

VOID
LxFdCloseOnExec(
    _In_ PLX_PROCESS Process)
{
    LONG Fd;

    for (Fd = 0; Fd < LX_MAX_FDS; Fd++)
    {
        PLX_FILE File = NULL;

        ExAcquireFastMutex(&Process->Lock);
        if ((Process->Fds[Fd] != NULL) && (Process->FdFlags[Fd] & LX_FD_CLOEXEC))
        {
            File = Process->Fds[Fd];
            Process->Fds[Fd] = NULL;
            Process->FdFlags[Fd] = 0;
        }
        ExReleaseFastMutex(&Process->Lock);

        if (File != NULL)
            LxFileDereference(File);
    }
}

LONG64
LxWriteFailed(
    _In_ LONG Error)
{
    PLX_THREAD Thread = LxCurrentThread();

    if ((Error == LX_EPIPE) && (Thread != NULL))
    {
        LX_SIGINFO Info;

        RtlZeroMemory(&Info, sizeof(Info));
        Info.Code = LX_SI_USER;
        LxSignalSend(Thread->Process, Thread, LX_SIGPIPE, &Info);
    }
    return -Error;
}

LONG64
LxFileRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    if ((File->Flags & LX_O_PATH) || ((File->Flags & LX_O_ACCMODE) == LX_O_WRONLY))
        return -LX_EBADF;
    if (File->Ops->Read == NULL)
        return -LX_EINVAL;
    if (Length == 0)
        return 0;
    if (Length > 0x7FFFF000)
        Length = 0x7FFFF000;
    return File->Ops->Read(File, Buffer, Length, Offset);
}

LONG64
LxFileWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    LONG64 Result;

    if ((File->Flags & LX_O_PATH) || ((File->Flags & LX_O_ACCMODE) == LX_O_RDONLY))
        return -LX_EBADF;
    if (File->Ops->Write == NULL)
        return -LX_EINVAL;
    if (Length == 0)
        return 0;
    if (Length > 0x7FFFF000)
        Length = 0x7FFFF000;
    Result = File->Ops->Write(File, Buffer, Length, Offset);
    if ((Result > 0) && (File->Mount != NULL))
    {
        File->Written = TRUE;
        if ((File->Path != NULL) && (File->Type == LX_S_IFREG))
            LxVcacheInvalidatePath(File->Mount->Ns, File->Path);
        LxInotifyFileEvent(File, LX_IN_MODIFY);
    }
    return Result;
}

LONG
LxFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    if (File->Ops->Stat != NULL)
        return File->Ops->Stat(File, Stat);

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = File->Type ? (File->Type | 0600) : 0600;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    Stat->Rdev = File->Rdev;
    return 0;
}

LONG64
LxFileReadKernel(
    _In_ PLX_FILE File,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ ULONG64 Offset)
{
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Position;
    NTSTATUS Status;
    ULONG Total = 0;

    if (File->Handle == NULL)
    {
        if ((File->Data == NULL) || (Offset >= File->DataLength))
            return 0;
        Total = (ULONG)min(Length, File->DataLength - Offset);
        RtlCopyMemory(Buffer, File->Data + Offset, Total);
        return Total;
    }

    while (Total < Length)
    {
        Position.QuadPart = (LONG64)(Offset + Total);
        Status = ZwReadFile(File->Handle, NULL, NULL, NULL, &Iosb, (PUCHAR)Buffer + Total,
                            Length - Total, &Position, NULL);
        if (Status == STATUS_END_OF_FILE)
            break;
        if (!NT_SUCCESS(Status))
            return -LxStatusToErrno(Status);
        if (Iosb.Information == 0)
            break;
        Total += (ULONG)Iosb.Information;
    }
    return Total;
}

static
LONG64
LxNtRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Position;
    PUCHAR Bounce;
    ULONG64 Total = 0;
    NTSTATUS Status;
    LONG64 Error;

    if (File->Directory)
        return -LX_EISDIR;

    Bounce = LxAllocPaged(LX_IO_CHUNK);
    if (Bounce == NULL)
        return -LX_ENOMEM;

    if (Offset != NULL)
        Position.QuadPart = *Offset;

    while (Total < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Total, LX_IO_CHUNK);

        Status = ZwReadFile(File->Handle, NULL, NULL, NULL, &Iosb, Bounce, Chunk,
                            (Offset != NULL) ? &Position : NULL, NULL);
        if ((Status == STATUS_END_OF_FILE) || (Status == STATUS_PIPE_BROKEN))
            break;
        if (!NT_SUCCESS(Status))
        {
            if (Total == 0)
            {
                LxFreePaged(Bounce);
                return -LxStatusToErrno(Status);
            }
            break;
        }

        Error = LxCopyToUser(Buffer + Total, Bounce, Iosb.Information);
        if (Error != 0)
        {
            LxFreePaged(Bounce);
            return (Total != 0) ? (LONG64)Total : Error;
        }

        Total += Iosb.Information;
        if (Offset != NULL)
            Position.QuadPart += Iosb.Information;
        if ((Iosb.Information < Chunk) || File->NtPipe)
            break;
    }

    if (Offset != NULL)
        *Offset = Position.QuadPart;
    LxFreePaged(Bounce);
    return (LONG64)Total;
}

static
LONG64
LxNtWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Position;
    PUCHAR Bounce;
    ULONG64 Total = 0;
    NTSTATUS Status;
    LONG64 Error;

    if (File->Directory)
        return -LX_EISDIR;

    Bounce = LxAllocPaged(LX_IO_CHUNK);
    if (Bounce == NULL)
        return -LX_ENOMEM;

    if (Offset != NULL)
        Position.QuadPart = *Offset;

    while (Total < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Total, LX_IO_CHUNK);
        LARGE_INTEGER AppendPosition;
        PLARGE_INTEGER WritePosition = NULL;

        Error = LxCopyFromUser(Bounce, Buffer + Total, Chunk);
        if (Error != 0)
        {
            LxFreePaged(Bounce);
            return (Total != 0) ? (LONG64)Total : Error;
        }

        if (Offset != NULL)
        {
            WritePosition = &Position;
        }
        else if ((File->Flags & LX_O_APPEND) && !File->NtPipe)
        {
            AppendPosition.HighPart = -1;
            AppendPosition.LowPart = FILE_WRITE_TO_END_OF_FILE;
            WritePosition = &AppendPosition;
        }

        Status = ZwWriteFile(File->Handle, NULL, NULL, NULL, &Iosb, Bounce, Chunk, WritePosition, NULL);
        if (!NT_SUCCESS(Status))
        {
            LxFreePaged(Bounce);
            if (Total != 0)
                return (LONG64)Total;
            return LxWriteFailed(LxStatusToErrno(Status));
        }

        Total += Iosb.Information;
        if (Offset != NULL)
            Position.QuadPart += Iosb.Information;
        if (Iosb.Information < Chunk)
            break;
    }

    if (Offset != NULL)
        *Offset = Position.QuadPart;
    LxFreePaged(Bounce);
    return (LONG64)Total;
}

static
LONG
LxNtStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    IO_STATUS_BLOCK Iosb;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    FILE_INTERNAL_INFORMATION Internal;
    ULONG Mode, Uid, Gid;
    ULONG64 Device;
    NTSTATUS Status;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Nlink = 1;
    Stat->BlkSize = 4096;

    if (File->NtPipe)
    {
        Stat->Mode = LX_S_IFIFO | 0600;
        Stat->Ino = File->Ino;
        return 0;
    }

    Status = ZwQueryInformationFile(File->Handle, &Iosb, &Basic, sizeof(Basic), FileBasicInformation);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    Status = ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    if (NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Internal, sizeof(Internal), FileInternalInformation)))
        Stat->Ino = (ULONG64)Internal.IndexNumber.QuadPart;
    if (Stat->Ino == 0)
        Stat->Ino = File->Ino;

    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 0x801;

    if (Standard.Directory)
    {
        Stat->Mode = LX_S_IFDIR | 0755;
        Stat->Size = 4096;
    }
    else
    {
        Stat->Mode = LX_S_IFREG | 0755;
        Stat->Size = Standard.EndOfFile.QuadPart;
    }

    if (LxNtReadLxAttributes(File->Handle, &Mode, &Uid, &Gid, &Device) == 0)
    {
        if (Mode != MAXULONG)
        {
            if ((Mode & LX_S_IFMT) == 0)
                Mode |= Stat->Mode & LX_S_IFMT;
            Stat->Mode = Mode;
        }
        if (Uid != MAXULONG)
            Stat->Uid = Uid;
        if (Gid != MAXULONG)
            Stat->Gid = Gid;
        if (Device != MAXULONG64)
            Stat->Rdev = ((Device >> 32) << 8) | (Device & 0xFF) | ((Device & 0xFFF00) << 12);
    }

    if ((Stat->Mode & LX_S_IFMT) != LX_S_IFREG && (Stat->Mode & LX_S_IFMT) != LX_S_IFDIR &&
        (Stat->Mode & LX_S_IFMT) != LX_S_IFLNK)
    {
        Stat->Size = 0;
    }

    if (Standard.NumberOfLinks != 0)
        Stat->Nlink = Standard.NumberOfLinks;
    Stat->Blocks = (Standard.AllocationSize.QuadPart + 511) / 512;
    LxNtTimeToTimespec(Basic.LastAccessTime.QuadPart, &Stat->Atime);
    LxNtTimeToTimespec(Basic.LastWriteTime.QuadPart, &Stat->Mtime);
    LxNtTimeToTimespec(Basic.ChangeTime.QuadPart ? Basic.ChangeTime.QuadPart : Basic.LastWriteTime.QuadPart,
                       &Stat->Ctime);
    return 0;
}

static
LONG64
LxNtSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    FILE_POSITION_INFORMATION Position;
    FILE_STANDARD_INFORMATION Standard;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    LONG64 Base;

    if (File->NtPipe)
        return -LX_ESPIPE;

    if (File->Directory)
    {
        if ((Whence != LX_SEEK_SET) || (Offset != 0))
            return (Whence == LX_SEEK_CUR) && (Offset == 0) ? File->DirOffset : -LX_EINVAL;
        KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
        File->DirRestart = TRUE;
        File->DirEnd = FALSE;
        File->DirBufferPos = File->DirBufferUsed = 0;
        File->DirOffset = 0;
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
        return 0;
    }

    switch (Whence)
    {
        case LX_SEEK_SET:
            Base = 0;
            break;

        case LX_SEEK_CUR:
            Status = ZwQueryInformationFile(File->Handle, &Iosb, &Position, sizeof(Position), FilePositionInformation);
            if (!NT_SUCCESS(Status))
                return -LxStatusToErrno(Status);
            Base = Position.CurrentByteOffset.QuadPart;
            break;

        case LX_SEEK_END:
            Status = ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation);
            if (!NT_SUCCESS(Status))
                return -LxStatusToErrno(Status);
            Base = Standard.EndOfFile.QuadPart;
            break;

        case 3:
        case 4:
            Status = ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation);
            if (!NT_SUCCESS(Status))
                return -LxStatusToErrno(Status);
            if (Offset >= Standard.EndOfFile.QuadPart)
                return -LX_ENXIO;
            Base = 0;
            if (Whence == 4)
                Offset = Standard.EndOfFile.QuadPart;
            break;

        default:
            return -LX_EINVAL;
    }

    if (Base + Offset < 0)
        return -LX_EINVAL;

    Position.CurrentByteOffset.QuadPart = Base + Offset;
    Status = ZwSetInformationFile(File->Handle, &Iosb, &Position, sizeof(Position), FilePositionInformation);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    return Base + Offset;
}

static
UCHAR
LxNtEntryType(
    _In_ PLX_FILE Directory,
    _In_ PFILE_ID_FULL_DIR_INFORMATION Entry)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    IO_STATUS_BLOCK Iosb;
    HANDLE Handle;
    ULONG Mode, Uid, Gid;
    ULONG64 Device;
    UCHAR Type = LX_DT_REG;

    if (Entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return LX_DT_DIR;
    if (!(Entry->FileAttributes & FILE_ATTRIBUTE_SYSTEM))
        return LX_DT_REG;

    Name.Buffer = Entry->FileName;
    Name.Length = Name.MaximumLength = (USHORT)Entry->FileNameLength;
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Directory->Handle, NULL);
    if (NT_SUCCESS(ZwCreateFile(&Handle, FILE_READ_EA | SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, NULL, 0)))
    {
        if ((LxNtReadLxAttributes(Handle, &Mode, &Uid, &Gid, &Device) == 0) && (Mode != MAXULONG))
        {
            switch (Mode & LX_S_IFMT)
            {
                case LX_S_IFLNK: Type = 10; break;
                case LX_S_IFCHR: Type = LX_DT_CHR; break;
                case 0060000: Type = 6; break;
                case LX_S_IFIFO: Type = LX_DT_FIFO; break;
                case 0140000: Type = 12; break;
                case LX_S_IFDIR: Type = LX_DT_DIR; break;
                default: Type = LX_DT_REG; break;
            }
        }
        ZwClose(Handle);
    }
    return Type;
}

static
LONG64
LxNtGetdents(
    _In_ PLX_FILE File,
    _In_ ULONG64 UserBuffer,
    _In_ ULONG Length)
{
    ULONG OutSize = min(Length, LX_DIR_BUFFER_SIZE);
    ULONG OutUsed = 0;
    PUCHAR Out;
    PCHAR Name;
    LONG64 Result = 0;

    if (!File->Directory)
        return -LX_ENOTDIR;

    Out = LxAllocPaged(OutSize);
    Name = LxAllocPaged(1024);
    if ((Out == NULL) || (Name == NULL))
    {
        LxFreePaged(Out);
        LxFreePaged(Name);
        return -LX_ENOMEM;
    }

    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    if (File->DirBuffer == NULL)
    {
        File->DirBuffer = LxAlloc(LX_DIR_BUFFER_SIZE);
        if (File->DirBuffer == NULL)
            Result = -LX_ENOMEM;
    }

    while (Result == 0)
    {
        PFILE_ID_FULL_DIR_INFORMATION Entry;
        ULONG NameLength = 0, RecordLength;
        PLX_DIRENT64 Dirent;

        if (File->DirBufferPos >= File->DirBufferUsed)
        {
            IO_STATUS_BLOCK Iosb;
            NTSTATUS Status;

            if (File->DirEnd)
                break;

            Status = ZwQueryDirectoryFile(File->Handle, NULL, NULL, NULL, &Iosb, File->DirBuffer,
                                          LX_DIR_BUFFER_SIZE, FileIdFullDirectoryInformation, FALSE, NULL,
                                          File->DirRestart);
            File->DirRestart = FALSE;
            if ((Status == STATUS_NO_MORE_FILES) || (Status == STATUS_NO_SUCH_FILE))
            {
                File->DirEnd = TRUE;
                break;
            }
            if (!NT_SUCCESS(Status))
            {
                if (OutUsed == 0)
                    Result = -LxStatusToErrno(Status);
                break;
            }
            File->DirBufferUsed = (ULONG)Iosb.Information;
            File->DirBufferPos = 0;
            if (File->DirBufferUsed == 0)
            {
                File->DirEnd = TRUE;
                break;
            }
        }

        Entry = (PFILE_ID_FULL_DIR_INFORMATION)(File->DirBuffer + File->DirBufferPos);
        LxUnicodeDecodeLinuxName(Entry->FileName, Entry->FileNameLength / sizeof(WCHAR));
        if (!NT_SUCCESS(RtlUnicodeToUTF8N(Name, 1023, &NameLength, Entry->FileName, Entry->FileNameLength)))
            NameLength = 0;
        Name[NameLength] = '\0';

        RecordLength = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(LX_DIRENT64, Name) + NameLength + 1, 8);
        if (OutUsed + RecordLength > OutSize)
        {
            if (OutUsed == 0)
                Result = -LX_EINVAL;
            break;
        }

        Dirent = (PLX_DIRENT64)(Out + OutUsed);
        RtlZeroMemory(Dirent, RecordLength);
        Dirent->Ino = Entry->FileId.QuadPart ? (ULONG64)Entry->FileId.QuadPart : (ULONG64)(File->DirOffset + 1);
        Dirent->Off = ++File->DirOffset;
        Dirent->RecLen = (USHORT)RecordLength;
        Dirent->Type = LxNtEntryType(File, Entry);
        RtlCopyMemory(Dirent->Name, Name, NameLength + 1);
        OutUsed += RecordLength;

        if (Entry->NextEntryOffset != 0)
            File->DirBufferPos += Entry->NextEntryOffset;
        else
            File->DirBufferPos = File->DirBufferUsed;
    }
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);

    if ((Result == 0) && (OutUsed != 0))
    {
        Result = LxCopyToUser(UserBuffer, Out, OutUsed);
        if (Result == 0)
            Result = OutUsed;
    }

    LxFreePaged(Out);
    LxFreePaged(Name);
    return Result;
}

static
LONG
LxNtTruncate(
    _In_ PLX_FILE File,
    _In_ LONG64 Length)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    if (File->Directory)
        return -LX_EISDIR;
    if (File->NtPipe)
        return -LX_EINVAL;

    EndOfFile.EndOfFile.QuadPart = Length;
    Status = ZwSetInformationFile(File->Handle, &Iosb, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    return NT_SUCCESS(Status) ? 0 : -LxStatusToErrno(Status);
}

static
ULONG
LxNtPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    FILE_PIPE_PEEK_BUFFER Peek;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Entry);

    if (!File->NtPipe)
        return LX_POLLIN | LX_POLLOUT | LX_POLLRDNORM | LX_POLLWRNORM;
    if ((File->Flags & LX_O_ACCMODE) != LX_O_RDONLY)
        return LX_POLLOUT | LX_POLLWRNORM;

    Status = ZwFsControlFile(File->Handle, NULL, NULL, NULL, &Iosb, FSCTL_PIPE_PEEK, NULL, 0, &Peek, sizeof(Peek));
    if (NT_SUCCESS(Status) || (Status == STATUS_BUFFER_OVERFLOW))
        return (Peek.ReadDataAvailable != 0) ? (LX_POLLIN | LX_POLLRDNORM) : 0;
    return LX_POLLHUP;
}

static
LONG64
LxNtIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    if ((Request == LX_FIONREAD) && File->NtPipe)
    {
        FILE_PIPE_PEEK_BUFFER Peek;
        IO_STATUS_BLOCK Iosb;
        NTSTATUS Status;
        LONG Available = 0;

        Status = ZwFsControlFile(File->Handle, NULL, NULL, NULL, &Iosb, FSCTL_PIPE_PEEK, NULL, 0, &Peek, sizeof(Peek));
        if (NT_SUCCESS(Status) || (Status == STATUS_BUFFER_OVERFLOW))
            Available = (LONG)Peek.ReadDataAvailable;
        return LxCopyToUser(Argument, &Available, sizeof(Available));
    }
    if ((Request == LX_FIONREAD) && !File->Directory)
    {
        FILE_STANDARD_INFORMATION Standard;
        FILE_POSITION_INFORMATION Position;
        IO_STATUS_BLOCK Iosb;
        LONG Available = 0;

        if (NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation)) &&
            NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Position, sizeof(Position), FilePositionInformation)) &&
            (Standard.EndOfFile.QuadPart > Position.CurrentByteOffset.QuadPart))
        {
            Available = (LONG)min(Standard.EndOfFile.QuadPart - Position.CurrentByteOffset.QuadPart, MAXLONG);
        }
        return LxCopyToUser(Argument, &Available, sizeof(Available));
    }
    return -LX_ENOTTY;
}

const LX_FILE_OPS LxNtFileOps =
{
    LxNtRead, LxNtWrite, LxNtIoctl, LxNtPoll, LxNtStat, LxNtSeek, LxNtGetdents, LxNtTruncate, NULL
};

PLX_FILE
LxFileFromNtHandle(
    _In_ HANDLE Handle,
    _In_ ULONG Flags)
{
    IO_STATUS_BLOCK Iosb;
    FILE_FS_DEVICE_INFORMATION Device;
    FILE_STANDARD_INFORMATION Standard;
    PLX_FILE File = LxFileAlloc(&LxNtFileOps, Flags, LX_S_IFREG);

    if (File == NULL)
        return NULL;

    File->Handle = Handle;
    if (NT_SUCCESS(ZwQueryVolumeInformationFile(Handle, &Iosb, &Device, sizeof(Device), FileFsDeviceInformation)) &&
        (Device.DeviceType == FILE_DEVICE_NAMED_PIPE))
    {
        File->NtPipe = TRUE;
        File->PollOnly = TRUE;
        File->Type = LX_S_IFIFO;
    }
    else if (NT_SUCCESS(ZwQueryInformationFile(Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation)) &&
             Standard.Directory)
    {
        File->Directory = TRUE;
        File->Type = LX_S_IFDIR;
    }
    return File;
}

static
LONG64
LxDataRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    LONG64 Position, Result;
    ULONG Count;

    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Position = (Offset != NULL) ? *Offset : File->Position;
    if ((Position < 0) || ((ULONG64)Position >= File->DataLength))
    {
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
        return 0;
    }
    Count = (ULONG)min(Length, File->DataLength - (ULONG64)Position);
    Result = LxCopyToUser(Buffer, File->Data + Position, Count);
    if (Result == 0)
    {
        Result = Count;
        if (Offset != NULL)
            *Offset = Position + Count;
        else
            File->Position = Position + Count;
    }
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    return Result;
}

static
LONG64
LxDataWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PCHAR Data;
    LONG Error;

    UNREFERENCED_PARAMETER(Offset);

    if (File->WriteHandler == NULL)
        return -LX_EINVAL;
    if (Length > 65536)
        return -LX_EINVAL;

    Data = LxAllocPaged((SIZE_T)Length + 1);
    if (Data == NULL)
        return -LX_ENOMEM;
    if (LxCopyFromUser(Data, Buffer, (SIZE_T)Length) != 0)
    {
        LxFreePaged(Data);
        return -LX_EFAULT;
    }
    Data[Length] = '\0';
    Error = File->WriteHandler(File, Data, (ULONG)Length);
    LxFreePaged(Data);
    return (Error != 0) ? Error : (LONG64)Length;
}

static
LONG64
LxDataSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    LONG64 Base;

    switch (Whence)
    {
        case LX_SEEK_SET: Base = 0; break;
        case LX_SEEK_CUR: Base = File->Position; break;
        case LX_SEEK_END: Base = File->DataLength; break;
        default: return -LX_EINVAL;
    }
    if (Base + Offset < 0)
        return -LX_EINVAL;
    File->Position = Base + Offset;
    return File->Position;
}

static
LONG
LxDataStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    LARGE_INTEGER Now;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = File->Type | File->Mode;
    Stat->Nlink = (File->Type == LX_S_IFDIR) ? 2 : 1;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 0;
    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Stat->Mtime);
    Stat->Atime = Stat->Ctime = Stat->Mtime;
    return 0;
}

static
ULONG
LxDataPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    PLX_EVENT_SOURCE Source = File->EventSource;
    ULONG Mask = LX_POLLIN | LX_POLLOUT | LX_POLLRDNORM | LX_POLLWRNORM;

    if (Source != NULL)
    {
        LONG64 Generation;

        if (Entry != NULL)
            LxPollAdd(&Source->Poll, Entry);
        Generation = Source->Generation;
        if (Generation != File->EventSeen)
        {
            File->EventSeen = Generation;
            Mask |= LX_POLLPRI | LX_POLLERR;
        }
    }
    return Mask;
}

static const LX_FILE_OPS LxDataFileOps =
{
    LxDataRead, LxDataWrite, NULL, LxDataPoll, LxDataStat, LxDataSeek, NULL, NULL, NULL
};

PLX_FILE
LxDataFileCreate(
    _In_ PCSTR Path,
    _In_ PVOID Data,
    _In_ ULONG Length,
    _In_ ULONG Mode)
{
    PLX_FILE File = LxFileAlloc(&LxDataFileOps, LX_O_RDWR, Mode & LX_S_IFMT);

    if (File == NULL)
        return NULL;

    File->Mode = Mode & 07777;

    File->Path = LxStrDup(Path);
    if (Length != 0)
    {
        File->Data = LxAlloc(Length);
        if (File->Data == NULL)
        {
            LxFileDereference(File);
            return NULL;
        }
        RtlCopyMemory(File->Data, Data, Length);
    }
    File->DataLength = Length;
    return File;
}

typedef enum _LX_DEVICE_KIND
{
    LxDeviceNull,
    LxDeviceZero,
    LxDeviceFull,
    LxDeviceRandom,
    LxDeviceKmsg
} LX_DEVICE_KIND;

static
LONG64
LxDeviceRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UCHAR Data[256];
    ULONG Count;
    LONG64 Error;

    UNREFERENCED_PARAMETER(Offset);

    switch ((LX_DEVICE_KIND)(ULONG_PTR)File->Object)
    {
        case LxDeviceNull:
            return 0;

        case LxDeviceZero:
        case LxDeviceFull:
            Count = (ULONG)min(Length, LX_IO_CHUNK);
            Error = LxZeroUser(Buffer, Count);
            return (Error != 0) ? Error : Count;

        case LxDeviceRandom:
            Count = (ULONG)min(Length, sizeof(Data));
            if (!NT_SUCCESS(LxFillRandom(Data, Count)))
                return -LX_EIO;
            Error = LxCopyToUser(Buffer, Data, Count);
            RtlSecureZeroMemory(Data, sizeof(Data));
            return (Error != 0) ? Error : Count;

        default:
            return (File->Flags & LX_O_NONBLOCK) ? -LX_EAGAIN : 0;
    }
}

static
LONG64
LxDeviceWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UNREFERENCED_PARAMETER(Offset);

    switch ((LX_DEVICE_KIND)(ULONG_PTR)File->Object)
    {
        case LxDeviceFull:
            return -LX_ENOSPC;

        case LxDeviceKmsg:
        {
            CHAR Data[512];
            ULONG Count = (ULONG)min(Length, sizeof(Data) - 1);

            if (LxCopyFromUser(Data, Buffer, Count) != 0)
                return -LX_EFAULT;
            Data[Count] = '\0';
            DbgPrint("%s%s", Data, ((Count != 0) && (Data[Count - 1] == '\n')) ? "" : "\n");
            return (LONG64)Length;
        }

        default:
            return (LONG64)Length;
    }
}

static
LONG64
LxDeviceSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Whence);
    return 0;
}

static
LONG
LxDeviceStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0666;
    Stat->Nlink = 1;
    Stat->Rdev = File->Rdev;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 5;
    return 0;
}

static const LX_FILE_OPS LxDeviceOps =
{
    LxDeviceRead, LxDeviceWrite, NULL, NULL, LxDeviceStat, LxDeviceSeek, NULL, NULL, NULL
};

PLX_FILE
LxDeviceOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Device,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    ULONG Major = (ULONG)(Device >> 32);
    ULONG Minor = (ULONG)Device;
    LX_DEVICE_KIND Kind;
    PLX_FILE File;
    PLX_TTY Tty = NULL;

    if (Major == 1)
    {
        switch (Minor)
        {
            case 3: Kind = LxDeviceNull; break;
            case 5: Kind = LxDeviceZero; break;
            case 7: Kind = LxDeviceFull; break;
            case 8:
            case 9: Kind = LxDeviceRandom; break;
            case 11: Kind = LxDeviceKmsg; break;
            default: return NULL;
        }
    }
    else if ((Major == 5) && (Minor == 2))
    {
        if (LxPtmxOpen(Process, Flags, &File) != 0)
            return NULL;
        File->Path = LxStrDup(Path);
        return File;
    }
    else if (Major == 136)
    {
        Tty = LxTtyLookup(Process->Instance, Minor);
        if (Tty == NULL)
            return NULL;
        if (LxTtyOpenSlave(Process, Tty, Flags | LX_O_NOCTTY, &File) != 0)
            File = NULL;
        LxTtyDereference(Tty);
        if (File != NULL)
            File->Path = LxStrDup(Path);
        return File;
    }
    else if ((Major == LX_WIN_DEVICE_MAJOR) && (Minor == LX_WIN_DEVICE_MINOR))
    {
        return LxWinOpen(Process, Flags, Path);
    }
    else if (Major == LX_USB_DEVICE_MAJOR)
    {
        return LxUsbfsOpen(Minor, Flags, Path);
    }
    else if ((Major == 5) && (Minor == 0))
    {
        Tty = Process->Tty;
    }
    else if (((Major == 5) && (Minor == 1)) || ((Major == 4) && (Minor <= 1)))
    {
        Tty = Process->Instance->Console ? Process->Instance->Console : Process->Tty;
    }
    else
    {
        return NULL;
    }

    if ((Major == 5) || (Major == 4))
    {
        if (Tty == NULL)
            return NULL;
        File = LxTtyOpen(Tty, Flags);
        if (File != NULL)
        {
            File->Path = LxStrDup(Path);
            File->Rdev = (Major << 8) | Minor;
        }
        return File;
    }

    File = LxFileAlloc(&LxDeviceOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFCHR);
    if (File == NULL)
        return NULL;
    File->Object = (PVOID)(ULONG_PTR)Kind;
    File->Rdev = (Major << 8) | Minor;
    File->Path = LxStrDup(Path);
    return File;
}
