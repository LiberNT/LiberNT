/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux file and path system calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_UTIME_NOW            ((1LL << 30) - 1)
#define LX_UTIME_OMIT           ((1LL << 30) - 2)
#define LX_AT_SYMLINK_FOLLOW    0x400
#define LX_CLOSE_RANGE_UNSHARE  2
#define LX_CLOSE_RANGE_CLOEXEC  4
#define LX_STATX_ATTR_MOUNT_ROOT 0x00002000
#define LX_ANON_INODE_MAGIC     0x09041934ULL
#define LX_PIPEFS_MAGIC         0x50495045ULL
#define LX_SOCKFS_MAGIC         0x534F434BULL
#define LX_DEVPTS_MAGIC         0x00001CD1ULL

PVOID
LxAllocPaged(
    _In_ SIZE_T Size)
{
    return ExAllocatePoolZero(PagedPool, Size, LX_TAG);
}

VOID
LxFreePaged(
    _In_opt_ PVOID Buffer)
{
    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, LX_TAG);
}

static
LONG64
LxCopyPath(
    _In_ ULONG64 User,
    _Out_ PSTR *Path)
{
    *Path = NULL;
    if (User == 0)
        return -LX_EFAULT;
    return LxCopyStringFromUser(User, Path, LX_PATH_MAX - 1);
}

static
PLX_FILE
LxCallFile(
    _In_ PLX_CALL Call,
    _In_ ULONG Index)
{
    return LxFdGet(Call->Process, (LONG)Call->Args[Index]);
}

static
LONG
LxLookupUser(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG Flags,
    _Out_ PLX_NODE Node)
{
    PSTR Path;
    LONG64 Result = LxCopyPath(UserPath, &Path);
    LONG Error;

    RtlZeroMemory(Node, sizeof(*Node));
    if (Result != 0)
        return (LONG)Result;
    Error = LxLookup(Process, DirFd, Path, Flags, Node);
    LxFree(Path);
    return Error;
}

LX_SYSCALL(LxSysRead)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    Result = LxFileRead(File, Call->Args[1], Call->Args[2], NULL);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysWrite)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    Result = LxFileWrite(File, Call->Args[1], Call->Args[2], NULL);
    LxFileDereference(File);
    return Result;
}

static
LONG64
LxVectorIo(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN Write,
    _In_opt_ PLONG64 Offset)
{
    PLX_FILE File;
    LX_IOVEC Vector;
    ULONG64 Count = Call->Args[2], Index;
    LONG64 Total = 0, Result;

    if (Count > 1024)
        return -LX_EINVAL;

    File = LxCallFile(Call, 0);
    if (File == NULL)
        return -LX_EBADF;

    for (Index = 0; Index < Count; Index++)
    {
        Result = LxCopyFromUser(&Vector, Call->Args[1] + Index * sizeof(Vector), sizeof(Vector));
        if (Result == 0)
        {
            if (Vector.Length == 0)
                continue;
            Result = Write ? LxFileWrite(File, Vector.Base, Vector.Length, Offset) :
                             LxFileRead(File, Vector.Base, Vector.Length, Offset);
        }
        if (Result < 0)
        {
            if (Total == 0)
                Total = Result;
            break;
        }
        Total += Result;
        if ((ULONG64)Result < Vector.Length)
            break;
    }

    LxFileDereference(File);
    return Total;
}

LX_SYSCALL(LxSysReadv)
{
    return LxVectorIo(Call, FALSE, NULL);
}

LX_SYSCALL(LxSysWritev)
{
    return LxVectorIo(Call, TRUE, NULL);
}

static
LONG64
LxPositionalIo(
    _In_ PLX_CALL Call,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Vector,
    _In_ LONG64 Position)
{
    PLX_FILE File = LxCallFile(Call, 0);
    IO_STATUS_BLOCK Iosb;
    FILE_POSITION_INFORMATION Saved;
    LONG64 Offset = Position, Result;

    if (File == NULL)
        return -LX_EBADF;
    if ((File->Ops->Seek == NULL) || File->NtPipe)
    {
        LxFileDereference(File);
        return -LX_ESPIPE;
    }
    if (Position < 0)
    {
        LxFileDereference(File);
        return -LX_EINVAL;
    }

    Saved.CurrentByteOffset.QuadPart = -1;
    if ((File->Handle != NULL) &&
        !NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Saved, sizeof(Saved), FilePositionInformation)))
    {
        Saved.CurrentByteOffset.QuadPart = -1;
    }

    if (Vector)
    {
        LxFileDereference(File);
        Result = LxVectorIo(Call, Write, &Offset);
        File = LxCallFile(Call, 0);
    }
    else
    {
        Result = Write ? LxFileWrite(File, Call->Args[1], Call->Args[2], &Offset) :
                         LxFileRead(File, Call->Args[1], Call->Args[2], &Offset);
    }

    if ((File != NULL) && (File->Handle != NULL) && (Saved.CurrentByteOffset.QuadPart >= 0))
        ZwSetInformationFile(File->Handle, &Iosb, &Saved, sizeof(Saved), FilePositionInformation);
    if (File != NULL)
        LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysPread64)
{
    return LxPositionalIo(Call, FALSE, FALSE, (LONG64)Call->Args[3]);
}

LX_SYSCALL(LxSysPwrite64)
{
    return LxPositionalIo(Call, TRUE, FALSE, (LONG64)Call->Args[3]);
}

LX_SYSCALL(LxSysPreadv)
{
    if ((LONG64)Call->Args[3] == -1)
        return LxVectorIo(Call, FALSE, NULL);
    return LxPositionalIo(Call, FALSE, TRUE, (LONG64)Call->Args[3]);
}

LX_SYSCALL(LxSysPwritev)
{
    if ((LONG64)Call->Args[3] == -1)
        return LxVectorIo(Call, TRUE, NULL);
    return LxPositionalIo(Call, TRUE, TRUE, (LONG64)Call->Args[3]);
}

static
LONG64
LxOpenCommon(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _In_ ULONG Resolve)
{
    PSTR Path;
    PLX_FILE File;
    PLX_TTY Tty;
    LONG64 Result;
    LONG Error;

    if ((Flags & LX_O_TMPFILE) == LX_O_TMPFILE)
        return -LX_EOPNOTSUPP;

    Result = LxCopyPath(UserPath, &Path);
    if (Result != 0)
        return Result;
    if (Path[0] == '\0')
    {
        LxFree(Path);
        return -LX_ENOENT;
    }

    Error = LxVfsOpenResolve(Process, DirFd, Path, Flags, Mode, Resolve | LX_LOOKUP_ACCESS, &File);
    LxFree(Path);
    if (Error != 0)
        return Error;

    Tty = LxFileGetTty(File);
    if ((Tty != NULL) && !(Flags & LX_O_NOCTTY))
        LxTtyAttachControlling(Process, Tty);

    Result = LxFdInstall(Process, File, 0, (Flags & LX_O_CLOEXEC) != 0);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysOpenat)
{
    return LxOpenCommon(Call->Process, (LONG)Call->Args[0], Call->Args[1], (ULONG)Call->Args[2], (ULONG)Call->Args[3], 0);
}

LX_SYSCALL(LxSysOpenat2)
{
    LX_OPEN_HOW How;
    ULONG64 Size = Call->Args[3];
    ULONG Resolve = 0;
    LONG64 Result;

    if (Size < sizeof(How))
        return -LX_EINVAL;
    if (Size > LX_PAGE_SIZE)
        return -LX_E2BIG;
    Result = LxCopyFromUser(&How, Call->Args[2], sizeof(How));
    if (Result != 0)
        return Result;
    if (Size > sizeof(How))
    {
        UCHAR Tail[64];
        ULONG64 Done;

        for (Done = sizeof(How); Done < Size; Done += sizeof(Tail))
        {
            ULONG Chunk = (ULONG)min(sizeof(Tail), Size - Done);
            ULONG Index;

            Result = LxCopyFromUser(Tail, Call->Args[2] + Done, Chunk);
            if (Result != 0)
                return Result;
            for (Index = 0; Index < Chunk; Index++)
            {
                if (Tail[Index] != 0)
                    return -LX_E2BIG;
            }
        }
    }

    if ((How.Flags >> 32) != 0)
        return -LX_EINVAL;
    if (How.Resolve & ~(ULONG64)(LX_RESOLVE_NO_XDEV | LX_RESOLVE_NO_MAGICLINKS | LX_RESOLVE_NO_SYMLINKS |
                                 LX_RESOLVE_BENEATH | LX_RESOLVE_IN_ROOT | LX_RESOLVE_CACHED))
        return -LX_EINVAL;
    if ((How.Resolve & LX_RESOLVE_BENEATH) && (How.Resolve & LX_RESOLVE_IN_ROOT))
        return -LX_EINVAL;
    if ((How.Mode & ~07777ULL) ||
        ((How.Mode != 0) && !(How.Flags & LX_O_CREAT) && ((How.Flags & LX_O_TMPFILE) != LX_O_TMPFILE)))
        return -LX_EINVAL;
    if ((How.Resolve & LX_RESOLVE_CACHED) && (How.Flags & (LX_O_CREAT | LX_O_TRUNC | LX_O_TMPFILE)))
        return -LX_EAGAIN;

    if (How.Resolve & LX_RESOLVE_NO_XDEV)
        Resolve |= LX_LOOKUP_NO_XDEV;
    if (How.Resolve & LX_RESOLVE_NO_MAGICLINKS)
        Resolve |= LX_LOOKUP_NO_MAGICLINKS;
    if (How.Resolve & LX_RESOLVE_NO_SYMLINKS)
        Resolve |= LX_LOOKUP_NO_SYMLINKS;
    if (How.Resolve & LX_RESOLVE_BENEATH)
        Resolve |= LX_LOOKUP_BENEATH;
    if (How.Resolve & LX_RESOLVE_IN_ROOT)
        Resolve |= LX_LOOKUP_IN_ROOT;

    return LxOpenCommon(Call->Process, (LONG)Call->Args[0], Call->Args[1], (ULONG)How.Flags, (ULONG)How.Mode, Resolve);
}

LX_SYSCALL(LxSysClose)
{
    return LxFdClose(Call->Process, (LONG)Call->Args[0]);
}

LX_SYSCALL(LxSysCloseRange)
{
    PLX_PROCESS Process = Call->Process;
    ULONG First = (ULONG)Call->Args[0], Last = (ULONG)Call->Args[1], Flags = (ULONG)Call->Args[2];
    ULONG Fd;

    if ((Flags & ~(LX_CLOSE_RANGE_UNSHARE | LX_CLOSE_RANGE_CLOEXEC)) || (First > Last))
        return -LX_EINVAL;
    if (Last >= LX_MAX_FDS)
        Last = LX_MAX_FDS - 1;

    for (Fd = First; (Fd <= Last) && (Fd < LX_MAX_FDS); Fd++)
    {
        if (Flags & LX_CLOSE_RANGE_CLOEXEC)
        {
            ExAcquireFastMutex(&Process->Lock);
            if (Process->Fds[Fd] != NULL)
                Process->FdFlags[Fd] |= LX_FD_CLOEXEC;
            ExReleaseFastMutex(&Process->Lock);
        }
        else if (Process->Fds[Fd] != NULL)
        {
            LxFdClose(Process, (LONG)Fd);
        }
    }
    return 0;
}

LX_SYSCALL(LxSysDup)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    Result = LxFdInstall(Call->Process, File, 0, FALSE);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysDup3)
{
    PLX_PROCESS Process = Call->Process;
    LONG OldFd = (LONG)Call->Args[0], NewFd = (LONG)Call->Args[1];
    ULONG Flags = (ULONG)Call->Args[2];
    PLX_FILE File, Previous;

    if ((Flags & ~LX_O_CLOEXEC) || (OldFd == NewFd))
        return -LX_EINVAL;
    if ((NewFd < 0) || (NewFd >= LX_MAX_FDS) || ((ULONG64)NewFd >= Process->Limits[LX_RLIMIT_NOFILE].Cur))
        return -LX_EBADF;

    File = LxFdGet(Process, OldFd);
    if (File == NULL)
        return -LX_EBADF;

    ExAcquireFastMutex(&Process->Lock);
    Previous = Process->Fds[NewFd];
    Process->Fds[NewFd] = File;
    Process->FdFlags[NewFd] = (Flags & LX_O_CLOEXEC) ? LX_FD_CLOEXEC : 0;
    ExReleaseFastMutex(&Process->Lock);

    if (Previous != NULL)
        LxFileDereference(Previous);
    return NewFd;
}

LX_SYSCALL(LxSysFcntl)
{
    PLX_PROCESS Process = Call->Process;
    LONG Fd = (LONG)Call->Args[0];
    ULONG Command = (ULONG)Call->Args[1];
    PLX_FILE File;
    LONG64 Result = 0;

    File = LxFdGet(Process, Fd);
    if (File == NULL)
        return -LX_EBADF;

    switch (Command)
    {
        case LX_F_DUPFD:
        case LX_F_DUPFD_CLOEXEC:
            if ((LONG64)Call->Args[2] < 0 || Call->Args[2] >= LX_MAX_FDS)
                Result = -LX_EINVAL;
            else
                Result = LxFdInstall(Process, File, (LONG)Call->Args[2], Command == LX_F_DUPFD_CLOEXEC);
            break;

        case LX_F_GETFD:
            ExAcquireFastMutex(&Process->Lock);
            Result = Process->FdFlags[Fd];
            ExReleaseFastMutex(&Process->Lock);
            break;

        case LX_F_SETFD:
            ExAcquireFastMutex(&Process->Lock);
            if (Process->Fds[Fd] == File)
                Process->FdFlags[Fd] = (Call->Args[2] & LX_FD_CLOEXEC) ? LX_FD_CLOEXEC : 0;
            ExReleaseFastMutex(&Process->Lock);
            break;

        case LX_F_GETFL:
            Result = File->Flags | LX_O_LARGEFILE;
            break;

        case LX_F_SETFL:
            if ((File->Flags ^ (ULONG)Call->Args[2]) & LX_O_ASYNC)
                LxFileSetAsync(File, Fd, ((ULONG)Call->Args[2] & LX_O_ASYNC) != 0);
            File->Flags = (File->Flags & ~(LX_O_APPEND | LX_O_NONBLOCK | LX_O_ASYNC | LX_O_DIRECT | LX_O_NOATIME)) |
                          ((ULONG)Call->Args[2] & (LX_O_APPEND | LX_O_NONBLOCK | LX_O_ASYNC | LX_O_DIRECT | LX_O_NOATIME));
            break;

        case LX_F_GETLK:
        case LX_F_OFD_GETLK:
        {
            LX_FLOCK Lock;

            Result = LxCopyFromUser(&Lock, Call->Args[2], sizeof(Lock));
            if (Result == 0)
            {
                Lock.Type = LX_F_UNLCK;
                Result = LxCopyToUser(Call->Args[2], &Lock, sizeof(Lock));
            }
            break;
        }

        case LX_F_SETLK:
        case LX_F_SETLKW:
        case LX_F_OFD_SETLK:
        case LX_F_OFD_SETLKW:
        {
            LX_FLOCK Lock;

            Result = LxCopyFromUser(&Lock, Call->Args[2], sizeof(Lock));
            if ((Result == 0) && (Lock.Type != LX_F_RDLCK) && (Lock.Type != LX_F_WRLCK) && (Lock.Type != LX_F_UNLCK))
                Result = -LX_EINVAL;
            break;
        }

        case LX_F_SETOWN:
        {
            LONG Who = (LONG)Call->Args[2];
            ULONG Global = 0;

            if (Who != 0)
            {
                Global = LxNrToGlobal(Process, (Who < 0) ? (ULONG)-Who : (ULONG)Who);
                if ((Global == 0) || !LxInstancePidInUse(Process->Instance, Global))
                {
                    Result = -LX_ESRCH;
                    break;
                }
            }
            Result = LxFileSetOwner(File, (Who < 0) ? LX_F_OWNER_PGRP : LX_F_OWNER_PID, Global);
            break;
        }

        case LX_F_GETOWN:
        {
            LONG Type;
            ULONG Owner;

            LxFileGetOwner(File, &Type, &Owner);
            Result = (Owner != 0) ? (LONG)LxGlobalToNr(Process, Owner) : 0;
            if (Type == LX_F_OWNER_PGRP)
                Result = -Result;
            break;
        }

        case LX_F_SETOWN_EX:
        case LX_F_GETOWN_EX:
        {
            LONG Owner[2];
            ULONG Global = 0;
            LONG Type;

            if (Command == LX_F_GETOWN_EX)
            {
                LxFileGetOwner(File, &Type, &Global);
                Owner[0] = Type;
                Owner[1] = (Global != 0) ? (LONG)LxGlobalToNr(Process, Global) : 0;
                Result = LxCopyToUser(Call->Args[2], Owner, sizeof(Owner));
                break;
            }
            Result = LxCopyFromUser(Owner, Call->Args[2], sizeof(Owner));
            if (Result != 0)
                break;
            if ((Owner[0] < LX_F_OWNER_TID) || (Owner[0] > LX_F_OWNER_PGRP) || (Owner[1] < 0))
            {
                Result = -LX_EINVAL;
                break;
            }
            if (Owner[1] != 0)
            {
                Global = LxNrToGlobal(Process, (ULONG)Owner[1]);
                if ((Global == 0) || !LxInstancePidInUse(Process->Instance, Global))
                {
                    Result = -LX_ESRCH;
                    break;
                }
            }
            Result = LxFileSetOwner(File, Owner[0], Global);
            break;
        }

        case LX_F_SETSIG:
            Result = (((LONG)Call->Args[2] < 0) || ((LONG)Call->Args[2] > LX_NSIG)) ? -LX_EINVAL :
                     LxFileSetSignal(File, (LONG)Call->Args[2]);
            break;

        case LX_F_GETSIG:
            Result = LxFileGetSignal(File);
            break;

        case LX_F_SETLEASE:
        case LX_F_NOTIFY:
            Result = 0;
            break;

        case LX_F_GETLEASE:
            Result = LX_F_UNLCK;
            break;

        case LX_F_SETPIPE_SZ:
            Result = LxPipeSetSize(File, (ULONG)Call->Args[2]);
            break;

        case LX_F_GETPIPE_SZ:
            Result = LxPipeGetSize(File);
            break;

        case LX_F_ADD_SEALS:
            if ((File->Path == NULL) || (strncmp(File->Path, "/memfd:", 7) != 0))
                Result = -LX_EINVAL;
            else if (File->Seals & LX_F_SEAL_SEAL)
                Result = -LX_EPERM;
            else
                File->Seals |= (ULONG)Call->Args[2] & 0x1F;
            break;

        case LX_F_GET_SEALS:
            if ((File->Path == NULL) || (strncmp(File->Path, "/memfd:", 7) != 0))
                Result = -LX_EINVAL;
            else
                Result = File->Seals;
            break;

        default:
            Result = -LX_EINVAL;
            break;
    }

    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysIoctl)
{
    PLX_PROCESS Process = Call->Process;
    LONG Fd = (LONG)Call->Args[0];
    ULONG Request = (ULONG)Call->Args[1];
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;
    LONG Value;

    if (File == NULL)
        return -LX_EBADF;

    switch (Request)
    {
        case LX_FIONBIO:
            Result = LxCopyFromUser(&Value, Call->Args[2], sizeof(Value));
            if (Result == 0)
            {
                if (Value)
                    File->Flags |= LX_O_NONBLOCK;
                else
                    File->Flags &= ~LX_O_NONBLOCK;
            }
            break;

        case LX_FIOCLEX:
        case LX_FIONCLEX:
            ExAcquireFastMutex(&Process->Lock);
            if (Process->Fds[Fd] == File)
                Process->FdFlags[Fd] = (Request == LX_FIOCLEX) ? LX_FD_CLOEXEC : 0;
            ExReleaseFastMutex(&Process->Lock);
            Result = 0;
            break;

        case LX_FIOASYNC:
        {
            LONG Enable;

            Result = LxCopyFromUser(&Enable, Call->Args[2], sizeof(Enable));
            if (Result != 0)
                break;
            if (((File->Flags & LX_O_ASYNC) != 0) != (Enable != 0))
                LxFileSetAsync(File, Fd, Enable != 0);
            if (Enable != 0)
                File->Flags |= LX_O_ASYNC;
            else
                File->Flags &= ~LX_O_ASYNC;
            break;
        }

        default:
            Result = (File->Ops->Ioctl != NULL) ? File->Ops->Ioctl(File, Request, Call->Args[2]) : -LX_ENOTTY;
            break;
    }

    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysLseek)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    if ((File->Ops->Seek == NULL) || File->NtPipe)
        Result = -LX_ESPIPE;
    else if (Call->Args[2] > 4)
        Result = -LX_EINVAL;
    else
        Result = File->Ops->Seek(File, (LONG64)Call->Args[1], (ULONG)Call->Args[2]);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysGetdents64)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    if (File->Ops->Getdents == NULL)
        Result = -LX_ENOTDIR;
    else
        Result = File->Ops->Getdents(File, Call->Args[1], (ULONG)min(Call->Args[2], MAXLONG));
    LxFileDereference(File);
    return Result;
}

static
LONG
LxStatNodeFollow(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _Out_ PLX_STAT Stat)
{
    if ((Node->Type == LX_S_IFLNK) && (Node->LinkTarget == NULL) && (Node->Mount->Kind != LxFsNt))
    {
        PLX_FILE File;
        LONG Error = LxVfsOpenNode(Process, Node, LX_O_RDONLY | LX_O_PATH, 0, &File);

        if (Error == 0)
        {
            Error = LxFileStat(File, Stat);
            LxFileDereference(File);
            return Error;
        }
    }
    return LxVfsStatNode(Process, Node, Stat);
}

static
LONG
LxStatAt(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG Flags,
    _Out_ PLX_STAT Stat,
    _Out_opt_ PLX_MOUNT *MountOut,
    _Out_opt_ PBOOLEAN MountRoot)
{
    PSTR Path;
    LX_NODE Node;
    LONG64 Result;
    LONG Error;

    if (MountOut != NULL)
        *MountOut = NULL;
    if (MountRoot != NULL)
        *MountRoot = FALSE;

    Result = LxCopyPath(UserPath, &Path);
    if (Result != 0)
        return (LONG)Result;

    if ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH))
    {
        PLX_FILE File;

        LxFree(Path);
        if (DirFd == LX_AT_FDCWD)
        {
            Path = LxProcessCwd(Process);
            if (Path == NULL)
                return -LX_ENOMEM;
            Error = LxLookup(Process, LX_AT_FDCWD, Path, LX_LOOKUP_FOLLOW, &Node);
            LxFree(Path);
            if (Error != 0)
                return Error;
            Error = LxVfsStatNode(Process, &Node, Stat);
            if (MountOut != NULL)
                *MountOut = Node.Mount;
            if (MountRoot != NULL)
                *MountRoot = (Node.Relative[0] == '\0');
            LxNodeFree(&Node);
            return Error;
        }

        File = LxFdGet(Process, DirFd);
        if (File == NULL)
            return -LX_EBADF;
        Error = LxFileStat(File, Stat);
        if ((File->Mount != NULL) && (File->Path != NULL) && (MountOut != NULL))
        {
            PCSTR Relative;
            PLX_MOUNT Mount = (File->Mount->Ns != NULL) ? LxMountFind(File->Mount->Ns, File->Path, &Relative) : NULL;

            *MountOut = (Mount != NULL) ? Mount : File->Mount;
            if (MountRoot != NULL)
                *MountRoot = (Mount != NULL) && (Relative[0] == '\0');
        }
        LxFileDereference(File);
        return Error;
    }

    if (Path[0] == '\0')
    {
        LxFree(Path);
        return -LX_ENOENT;
    }

    Error = LxLookup(Process, DirFd, Path, (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    LxFree(Path);
    if (Error != 0)
        return Error;
    if (Flags & LX_AT_SYMLINK_NOFOLLOW)
        Error = LxVfsStatNode(Process, &Node, Stat);
    else
        Error = LxStatNodeFollow(Process, &Node, Stat);
    if (MountOut != NULL)
        *MountOut = Node.Mount;
    if (MountRoot != NULL)
        *MountRoot = (Node.Relative != NULL) && (Node.Relative[0] == '\0');
    LxNodeFree(&Node);
    return Error;
}

static
VOID
LxStatView(
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_STAT Stat)
{
    Stat->Uid = LxCredViewId(Process, FALSE, Stat->Uid);
    Stat->Gid = LxCredViewId(Process, TRUE, Stat->Gid);
}

LX_SYSCALL(LxSysFstat)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LX_STAT Stat;
    LONG Error;

    if (File == NULL)
        return -LX_EBADF;
    Error = LxFileStat(File, &Stat);
    LxFileDereference(File);
    if (Error != 0)
        return Error;
    LxStatView(Call->Process, &Stat);
    return LxCopyToUser(Call->Args[1], &Stat, sizeof(Stat));
}

LX_SYSCALL(LxSysNewfstatat)
{
    LX_STAT Stat;
    ULONG Flags = (ULONG)Call->Args[3];
    LONG Error;

    if (Flags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH | LX_AT_NO_AUTOMOUNT))
        return -LX_EINVAL;
    Error = LxStatAt(Call->Process, (LONG)Call->Args[0], Call->Args[1], Flags, &Stat, NULL, NULL);
    if (Error != 0)
        return Error;
    LxStatView(Call->Process, &Stat);
    return LxCopyToUser(Call->Args[2], &Stat, sizeof(Stat));
}

LX_SYSCALL(LxSysStatx)
{
    ULONG Flags = (ULONG)Call->Args[2];
    LX_STATX Statx;
    LX_STAT Stat;
    PLX_MOUNT Mount;
    BOOLEAN Root;
    LONG Error;

    if (Flags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH | LX_AT_NO_AUTOMOUNT | 0x6000))
        return -LX_EINVAL;
    Error = LxStatAt(Call->Process, (LONG)Call->Args[0], Call->Args[1], Flags, &Stat, &Mount, &Root);
    if (Error != 0)
        return Error;
    LxStatView(Call->Process, &Stat);

    RtlZeroMemory(&Statx, sizeof(Statx));
    Statx.Mask = LX_STATX_BASIC_STATS | LX_STATX_MNT_ID;
    Statx.BlkSize = (ULONG)Stat.BlkSize;
    Statx.Nlink = Stat.Nlink;
    Statx.Uid = Stat.Uid;
    Statx.Gid = Stat.Gid;
    Statx.Mode = (USHORT)Stat.Mode;
    Statx.Ino = Stat.Ino;
    Statx.Size = (ULONG64)Stat.Size;
    Statx.Blocks = (ULONG64)Stat.Blocks;
    Statx.AttributesMask = LX_STATX_ATTR_MOUNT_ROOT;
    Statx.Attributes = Root ? LX_STATX_ATTR_MOUNT_ROOT : 0;
    Statx.Atime.Seconds = Stat.Atime.Seconds;
    Statx.Atime.Nanoseconds = (ULONG)Stat.Atime.Nanoseconds;
    Statx.Mtime.Seconds = Stat.Mtime.Seconds;
    Statx.Mtime.Nanoseconds = (ULONG)Stat.Mtime.Nanoseconds;
    Statx.Ctime.Seconds = Stat.Ctime.Seconds;
    Statx.Ctime.Nanoseconds = (ULONG)Stat.Ctime.Nanoseconds;
    Statx.RdevMajor = (ULONG)((Stat.Rdev >> 8) & 0xFFF);
    Statx.RdevMinor = (ULONG)((Stat.Rdev & 0xFF) | ((Stat.Rdev >> 12) & 0xFFF00));
    Statx.DevMajor = (ULONG)((Stat.Dev >> 8) & 0xFFF);
    Statx.DevMinor = (ULONG)((Stat.Dev & 0xFF) | ((Stat.Dev >> 12) & 0xFFF00));
    Statx.MntId = (Mount != NULL) ? Mount->Id : 0;
    return LxCopyToUser(Call->Args[4], &Statx, sizeof(Statx));
}

static
LONG64
LxStatfsFile(
    _In_ PLX_FILE File,
    _Out_writes_bytes_(120) PVOID Buffer)
{
    PULONG64 Values = Buffer;

    if (File->Mount != NULL)
        return LxMountStatfs(File->Mount, Buffer);

    RtlZeroMemory(Buffer, 120);
    if (LxFileIsPipe(File))
        Values[0] = LX_PIPEFS_MAGIC;
    else if (LxFileIsSocket(File))
        Values[0] = LX_SOCKFS_MAGIC;
    else if (LxFileGetTty(File) != NULL)
        Values[0] = LX_DEVPTS_MAGIC;
    else if (File->Handle != NULL)
        Values[0] = 0x01021994ULL;
    else
        Values[0] = LX_ANON_INODE_MAGIC;
    Values[1] = 4096;
    Values[8] = 255;
    Values[9] = 4096;
    return 0;
}

LX_SYSCALL(LxSysStatfs)
{
    LX_NODE Node;
    ULONG64 Buffer[15];
    LONG Error;

    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Node);
    if (Error != 0)
        return Error;
    Error = LxMountStatfs(Node.Mount, Buffer);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;
    return LxCopyToUser(Call->Args[1], Buffer, sizeof(Buffer));
}

LX_SYSCALL(LxSysFstatfs)
{
    PLX_FILE File = LxCallFile(Call, 0);
    ULONG64 Buffer[15];
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    Result = LxStatfsFile(File, Buffer);
    LxFileDereference(File);
    if (Result != 0)
        return Result;
    return LxCopyToUser(Call->Args[1], Buffer, sizeof(Buffer));
}

static
LONG
LxAccessCheck(
    _In_ PLX_PROCESS Process,
    _In_ PLX_STAT Stat,
    _In_ ULONG Mode,
    _In_ ULONG Flags)
{
    ULONG Fsuid = Process->Fsuid, Fsgid = Process->Fsgid;
    ULONG64 Effective = Process->CapEffective;

    if (Mode == 0)
        return 0;
    if (!(Flags & LX_AT_EACCESS))
    {
        Fsuid = Process->Uid[0];
        Fsgid = Process->Gid[0];
        if (!(Process->SecureBits & LX_SECBIT_NO_SETUID_FIXUP))
            Effective = (Process->Uid[0] == LxCredMakeId(Process, FALSE, 0)) ? Process->CapPermitted : 0;
    }
    return LxPermissionAs(Process, Fsuid, Fsgid, Effective, Stat->Mode, Stat->Uid, Stat->Gid, Mode);
}

static
LONG
LxAccessNode(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Mode,
    _In_ ULONG Flags)
{
    LX_STAT Stat;
    LONG Error;

    if (Mode & ~7UL)
        return -LX_EINVAL;
    if ((Mode & 2) && (Node->Mount->Flags & LX_MS_RDONLY))
        return -LX_EROFS;
    if (Mode == 0)
        return 0;
    Error = LxStatNodeFollow(Process, Node, &Stat);
    if (Error != 0)
        return Error;
    return LxAccessCheck(Process, &Stat, Mode, Flags);
}

static
LONG64
LxAccess(
    _In_ PLX_CALL Call,
    _In_ ULONG Mode,
    _In_ ULONG Flags)
{
    LX_NODE Node;
    LONG Error;

    if (Flags & ~(LX_AT_EACCESS | LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    if (Flags & LX_AT_EMPTY_PATH)
    {
        CHAR First = 0;
        LONG64 Result = LxCopyFromUser(&First, Call->Args[1], 1);

        if ((Result == 0) && (First == '\0'))
        {
            PLX_FILE File = LxCallFile(Call, 0);
            LX_STAT Stat;

            if (File == NULL)
                return -LX_EBADF;
            Error = 0;
            if (Mode & ~7UL)
                Error = -LX_EINVAL;
            else if ((Mode & 2) && (File->Mount != NULL) && (File->Mount->Flags & LX_MS_RDONLY))
                Error = -LX_EROFS;
            else if (Mode != 0)
            {
                Error = LxFileStat(File, &Stat);
                if (Error == 0)
                    Error = LxAccessCheck(Call->Process, &Stat, Mode, Flags);
            }
            LxFileDereference(File);
            return Error;
        }
    }
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1],
                         (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    if (Error != 0)
        return Error;
    Error = LxAccessNode(Call->Process, &Node, Mode, Flags);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysFaccessat)
{
    return LxAccess(Call, (ULONG)Call->Args[2], 0);
}

LX_SYSCALL(LxSysFaccessat2)
{
    return LxAccess(Call, (ULONG)Call->Args[2], (ULONG)Call->Args[3]);
}

LX_SYSCALL(LxSysGetcwd)
{
    PLX_PROCESS Process = Call->Process;
    SIZE_T Length, RootLength;
    LONG64 Result;
    PSTR Cwd, Root, Visible;

    Cwd = LxProcessCwd(Process);
    Root = LxProcessRoot(Process);
    if ((Cwd == NULL) || (Root == NULL))
    {
        LxFree(Cwd);
        LxFree(Root);
        return -LX_ENOMEM;
    }
    RootLength = strlen(Root);
    if (RootLength <= 1)
        Visible = LxStrDup(Cwd);
    else if ((strncmp(Cwd, Root, RootLength) == 0) && ((Cwd[RootLength] == '\0') || (Cwd[RootLength] == '/')))
        Visible = LxStrDup((Cwd[RootLength] == '\0') ? "/" : Cwd + RootLength);
    else
        Visible = LxStrPrintf("(unreachable)%s", Cwd);
    LxFree(Cwd);
    LxFree(Root);
    Cwd = Visible;
    if (Cwd == NULL)
        return -LX_ENOMEM;

    Length = strlen(Cwd) + 1;
    if (Call->Args[1] < Length)
        Result = -LX_ERANGE;
    else
        Result = LxCopyToUser(Call->Args[0], Cwd, Length);
    LxFree(Cwd);
    return (Result != 0) ? Result : (LONG64)Length;
}

static
LONG64
LxChangeDirectory(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path)
{
    return LxProcessSetCwd(Process, Path);
}

LX_SYSCALL(LxSysChdir)
{
    LX_NODE Node;
    LONG64 Result;
    LONG Error;

    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW | LX_LOOKUP_DIRECTORY, &Node);
    if (Error != 0)
        return Error;
    if (Node.Type != LX_S_IFDIR)
    {
        LxNodeFree(&Node);
        return -LX_ENOTDIR;
    }
    Result = LxVfsNodePermission(Call->Process, &Node, LX_MAY_EXEC);
    if (Result == 0)
        Result = LxChangeDirectory(Call->Process, Node.Path);
    LxNodeFree(&Node);
    return Result;
}

LX_SYSCALL(LxSysFchdir)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    if ((File->Type != LX_S_IFDIR) || (File->Path == NULL))
    {
        Result = -LX_ENOTDIR;
    }
    else
    {
        LX_STAT Stat;

        Result = LxFileStat(File, &Stat);
        if (Result == 0)
            Result = LxPermission(Call->Process, Stat.Mode, Stat.Uid, Stat.Gid, LX_MAY_EXEC);
        if (Result == 0)
            Result = LxChangeDirectory(Call->Process, File->Path);
    }
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysChroot)
{
    LX_NODE Node;
    LONG Error;

    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW | LX_LOOKUP_DIRECTORY, &Node);
    if (Error != 0)
        return Error;
    if (Node.Type != LX_S_IFDIR)
        Error = -LX_ENOTDIR;
    else
        Error = LxVfsNodePermission(Call->Process, &Node, LX_MAY_EXEC);
    if ((Error == 0) && !LxNsCapable(Call->Process, NULL, LX_CAP_SYS_CHROOT))
        Error = -LX_EPERM;
    if (Error == 0)
        Error = LxProcessChroot(Call->Process, Node.Path);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysPivotRoot)
{
    LX_NODE NewRoot, PutOld;
    LONG Error;

    if (!LxMayMount(Call->Process))
        return -LX_EPERM;
    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW | LX_LOOKUP_DIRECTORY, &NewRoot);
    if (Error != 0)
        return Error;
    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[1], LX_LOOKUP_FOLLOW | LX_LOOKUP_DIRECTORY, &PutOld);
    if (Error != 0)
    {
        LxNodeFree(&NewRoot);
        return Error;
    }
    if ((NewRoot.Type != LX_S_IFDIR) || (PutOld.Type != LX_S_IFDIR))
        Error = -LX_ENOTDIR;
    else
        Error = LxMountPivotRoot(Call->Process, &NewRoot, &PutOld);
    LxNodeFree(&PutOld);
    LxNodeFree(&NewRoot);
    return Error;
}

LX_SYSCALL(LxSysMkdirat)
{
    LX_NODE Node;
    LONG Error;

    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], LX_LOOKUP_CREATE, &Node);
    if (Error != 0)
        return Error;
    Error = LxVfsMkdir(Call->Process, &Node, (ULONG)Call->Args[2]);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysMknodat)
{
    ULONG Mode = (ULONG)Call->Args[2];
    ULONG64 Dev = Call->Args[3];
    ULONG64 Major = ((Dev >> 8) & 0xFFF) | ((Dev >> 32) & ~0xFFFULL);
    ULONG64 Minor = (Dev & 0xFF) | ((Dev >> 12) & ~0xFFULL);
    LX_NODE Node;
    LONG Error;

    switch (Mode & LX_S_IFMT)
    {
        case 0:
        case LX_S_IFREG:
        case LX_S_IFIFO:
        case LX_S_IFSOCK:
        case LX_S_IFCHR:
        case LX_S_IFBLK:
            break;
        default:
            return -LX_EINVAL;
    }

    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], LX_LOOKUP_CREATE, &Node);
    if (Error != 0)
        return Error;
    if (((Mode & LX_S_IFMT) == 0) || ((Mode & LX_S_IFMT) == LX_S_IFREG))
    {
        PLX_FILE File;

        Error = Node.Exists ? -LX_EEXIST : LxVfsOpenNode(Call->Process, &Node, LX_O_CREAT | LX_O_EXCL | LX_O_WRONLY,
                                                          Mode & 07777, &File);
        if (Error == 0)
            LxFileDereference(File);
    }
    else
    {
        Error = LxVfsMknod(Call->Process, &Node, Mode, ((ULONG64)(ULONG)Major << 32) | (ULONG)Minor);
    }
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysUnlinkat)
{
    ULONG Flags = (ULONG)Call->Args[2];
    LX_NODE Node;
    LONG Error;

    if (Flags & ~LX_AT_REMOVEDIR)
        return -LX_EINVAL;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], 0, &Node);
    if (Error != 0)
        return Error;
    Error = LxVfsUnlink(Call->Process, &Node, (Flags & LX_AT_REMOVEDIR) != 0);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysSymlinkat)
{
    PSTR Target;
    LX_NODE Node;
    LONG64 Result;
    LONG Error;

    Result = LxCopyPath(Call->Args[0], &Target);
    if (Result != 0)
        return Result;
    if (Target[0] == '\0')
    {
        LxFree(Target);
        return -LX_ENOENT;
    }
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[1], Call->Args[2], LX_LOOKUP_CREATE, &Node);
    if (Error == 0)
    {
        Error = LxVfsSymlink(Call->Process, &Node, Target);
        LxNodeFree(&Node);
    }
    LxFree(Target);
    return Error;
}

LX_SYSCALL(LxSysLinkat)
{
    ULONG Flags = (ULONG)Call->Args[4];
    LX_NODE From, To;
    LONG Error;

    if (Flags & ~(LX_AT_SYMLINK_FOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1],
                         (Flags & LX_AT_SYMLINK_FOLLOW) ? LX_LOOKUP_FOLLOW : 0, &From);
    if (Error != 0)
        return Error;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[2], Call->Args[3], LX_LOOKUP_CREATE, &To);
    if (Error == 0)
    {
        Error = LxVfsLink(Call->Process, &From, &To);
        LxNodeFree(&To);
    }
    LxNodeFree(&From);
    return Error;
}

static
LONG64
LxRename(
    _In_ PLX_CALL Call,
    _In_ ULONG Flags)
{
    LX_NODE From, To;
    LONG Error;

    if (Flags & ~LX_RENAME_NOREPLACE)
        return -LX_EINVAL;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], 0, &From);
    if (Error != 0)
        return Error;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[2], Call->Args[3], LX_LOOKUP_CREATE, &To);
    if (Error == 0)
    {
        Error = LxVfsRename(Call->Process, &From, &To, Flags);
        LxNodeFree(&To);
    }
    LxNodeFree(&From);
    return Error;
}

LX_SYSCALL(LxSysRenameat)
{
    return LxRename(Call, 0);
}

LX_SYSCALL(LxSysRenameat2)
{
    return LxRename(Call, (ULONG)Call->Args[4]);
}

LX_SYSCALL(LxSysTruncate)
{
    LX_NODE Node;
    LONG Error;

    if ((LONG64)Call->Args[1] < 0)
        return -LX_EINVAL;
    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Node);
    if (Error != 0)
        return Error;
    if (Node.Type != LX_S_IFDIR)
        Error = LxVfsNodePermission(Call->Process, &Node, LX_MAY_WRITE);
    if (Error == 0)
        Error = LxVfsTruncate(&Node, (LONG64)Call->Args[1]);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysFtruncate)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG64 Result;

    if (File == NULL)
        return -LX_EBADF;
    if ((LONG64)Call->Args[1] < 0)
        Result = -LX_EINVAL;
    else if ((File->Flags & LX_O_ACCMODE) == LX_O_RDONLY)
        Result = -LX_EINVAL;
    else if (File->Ops->Truncate == NULL)
        Result = -LX_EINVAL;
    else
        Result = File->Ops->Truncate(File, (LONG64)Call->Args[1]);
    if ((File->Mount != NULL) && (File->Path != NULL))
        LxVcacheInvalidatePath(File->Mount->Ns, File->Path);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysFallocate)
{
    PLX_FILE File = LxCallFile(Call, 0);
    ULONG Mode = (ULONG)Call->Args[1];
    LONG64 Offset = (LONG64)Call->Args[2], Length = (LONG64)Call->Args[3];
    LX_STAT Stat;
    LONG64 Result = 0;

    if (File == NULL)
        return -LX_EBADF;
    if ((Offset < 0) || (Length <= 0))
        Result = -LX_EINVAL;
    else if (Mode & ~1UL)
        Result = -LX_EOPNOTSUPP;
    else if ((File->Handle == NULL) || File->Directory || (File->Ops->Truncate == NULL))
        Result = -LX_ENODEV;
    else if (!(Mode & 1))
    {
        Result = LxFileStat(File, &Stat);
        if ((Result == 0) && (Stat.Size < Offset + Length))
            Result = File->Ops->Truncate(File, Offset + Length);
        if ((File->Mount != NULL) && (File->Path != NULL))
            LxVcacheInvalidatePath(File->Mount->Ns, File->Path);
    }
    LxFileDereference(File);
    return Result;
}

static
LONG
LxSetAttributesFile(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    LX_NODE Node;
    LONG Error;

    if ((File->Handle == NULL) || File->NtPipe)
        return 0;
    Error = LxVfsSetAttributes(Process, NULL, File, Mode, Uid, Gid);
    if ((Error != -LX_EACCES) || (File->Path == NULL) || (File->Path[0] != '/'))
        return Error;
    Error = LxLookup(Process, LX_AT_FDCWD, File->Path, 0, &Node);
    if (Error != 0)
        return Error;
    Error = LxVfsSetAttributes(Process, &Node, NULL, Mode, Uid, Gid);
    LxNodeFree(&Node);
    return Error;
}

static
LONG64
LxSetAttributesAt(
    _In_ PLX_CALL Call,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG Flags,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    LX_NODE Node;
    PSTR Path;
    LONG64 Result;
    LONG Error;

    Result = LxCopyPath(UserPath, &Path);
    if (Result != 0)
        return Result;
    if ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH))
    {
        PLX_FILE File = LxFdGet(Call->Process, DirFd);

        LxFree(Path);
        if (File == NULL)
            return -LX_EBADF;
        Error = LxSetAttributesFile(Call->Process, File, Mode, Uid, Gid);
        LxFileDereference(File);
        return Error;
    }
    Error = LxLookup(Call->Process, DirFd, Path, (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    LxFree(Path);
    if (Error != 0)
        return Error;
    Error = LxVfsSetAttributes(Call->Process, &Node, NULL, Mode, Uid, Gid);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysFchmod)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG Error;

    if (File == NULL)
        return -LX_EBADF;
    Error = LxSetAttributesFile(Call->Process, File, (ULONG)Call->Args[1] & 07777, MAXULONG, MAXULONG);
    LxFileDereference(File);
    return Error;
}

LX_SYSCALL(LxSysFchmodat)
{
    return LxSetAttributesAt(Call, (LONG)Call->Args[0], Call->Args[1], (ULONG)Call->Args[3],
                             (ULONG)Call->Args[2] & 07777, MAXULONG, MAXULONG);
}

static
BOOLEAN
LxChownIds(
    _In_ PLX_PROCESS Process,
    _Inout_ PULONG Uid,
    _Inout_ PULONG Gid)
{
    if (*Uid != MAXULONG)
    {
        *Uid = LxCredMakeId(Process, FALSE, *Uid);
        if (*Uid == MAXULONG)
            return FALSE;
    }
    if (*Gid != MAXULONG)
    {
        *Gid = LxCredMakeId(Process, TRUE, *Gid);
        if (*Gid == MAXULONG)
            return FALSE;
    }
    return TRUE;
}

LX_SYSCALL(LxSysFchown)
{
    ULONG Uid = (ULONG)Call->Args[1], Gid = (ULONG)Call->Args[2];
    PLX_FILE File;
    LONG Error;

    if (!LxChownIds(Call->Process, &Uid, &Gid))
        return -LX_EINVAL;
    File = LxCallFile(Call, 0);
    if (File == NULL)
        return -LX_EBADF;
    Error = LxSetAttributesFile(Call->Process, File, MAXULONG, Uid, Gid);
    LxFileDereference(File);
    return Error;
}

LX_SYSCALL(LxSysFchownat)
{
    ULONG Uid = (ULONG)Call->Args[2], Gid = (ULONG)Call->Args[3];

    if (!LxChownIds(Call->Process, &Uid, &Gid))
        return -LX_EINVAL;
    return LxSetAttributesAt(Call, (LONG)Call->Args[0], Call->Args[1], (ULONG)Call->Args[4], MAXULONG, Uid, Gid);
}

LX_SYSCALL(LxSysReadlinkat)
{
    LX_NODE Node;
    PSTR Target, Path;
    PCSTR Visible;
    SIZE_T Length;
    LONG64 Result;
    LONG Error;

    if ((LONG64)Call->Args[3] <= 0)
        return -LX_EINVAL;
    Result = LxCopyPath(Call->Args[1], &Path);
    if (Result != 0)
        return Result;
    if (Path[0] == '\0')
    {
        PLX_FILE File = LxFdGet(Call->Process, (LONG)Call->Args[0]);

        LxFree(Path);
        if (File == NULL)
            return -LX_EBADF;
        Error = ((File->Type == LX_S_IFLNK) && (File->Path != NULL)) ?
                LxLookup(Call->Process, LX_AT_FDCWD, File->Path, 0, &Node) : -LX_EINVAL;
        LxFileDereference(File);
    }
    else
    {
        Error = LxLookup(Call->Process, (LONG)Call->Args[0], Path, 0, &Node);
        LxFree(Path);
    }
    if (Error != 0)
        return Error;
    Error = LxVfsReadlink(Call->Process, &Node, &Target);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;
    Visible = Target;
    if (strncmp(Target, "/.lxcore-detached/", 18) == 0)
    {
        Visible = Target + 18;
        while ((*Visible >= '0') && (*Visible <= '9'))
            Visible++;
        if (*Visible == '\0')
            Visible = "/";
    }
    Length = min(strlen(Visible), Call->Args[3]);
    Result = LxCopyToUser(Call->Args[2], Visible, Length);
    LxFree(Target);
    return (Result != 0) ? Result : (LONG64)Length;
}

LX_SYSCALL(LxSysFsync)
{
    PLX_FILE File = LxCallFile(Call, 0);
    IO_STATUS_BLOCK Iosb;

    if (File == NULL)
        return -LX_EBADF;
    if ((File->Handle != NULL) && !File->Directory && !File->NtPipe && ((File->Flags & LX_O_ACCMODE) != LX_O_RDONLY))
        ZwFlushBuffersFile(File->Handle, &Iosb);
    LxFileDereference(File);
    return 0;
}

static
VOID
LxFlushVolume(
    _In_ PCUNICODE_STRING NtPath)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Volume;
    IO_STATUS_BLOCK Iosb;
    USHORT Count = NtPath->Length / sizeof(WCHAR), Index, Separators = 0;
    HANDLE Handle;

    if ((Count >= 4) && (NtPath->Buffer[0] == L'\\') && (NtPath->Buffer[1] == L'?') &&
        (NtPath->Buffer[2] == L'?') && (NtPath->Buffer[3] == L'\\'))
    {
        if ((Count < 6) || (NtPath->Buffer[5] != L':'))
            return;
        Index = 6;
    }
    else
    {
        for (Index = 0; Index < Count; Index++)
        {
            if ((NtPath->Buffer[Index] == L'\\') && (++Separators == 3))
                break;
        }
    }
    if (Index == 0)
        return;

    Volume.Buffer = NtPath->Buffer;
    Volume.Length = Volume.MaximumLength = Index * sizeof(WCHAR);
    InitializeObjectAttributes(&ObjectAttributes, &Volume, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenFile(&Handle, FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &Iosb,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        ZwFlushBuffersFile(Handle, &Iosb);
        ZwClose(Handle);
    }
}

LX_SYSCALL(LxSysSync)
{
    LxFlushVolume(&Call->Process->Instance->RootNt);
    return 0;
}

LX_SYSCALL(LxSysSyncfs)
{
    PLX_FILE File = LxCallFile(Call, 0);

    if (File == NULL)
        return -LX_EBADF;
    LxFileDereference(File);
    LxFlushVolume(&Call->Process->Instance->RootNt);
    return 0;
}

static
LONG
LxUtimePermission(
    _In_ PLX_PROCESS Process,
    _In_ PLX_STAT Stat,
    _In_reads_(2) PLX_TIMESPEC Times)
{
    BOOLEAN Touch = (Times[0].Nanoseconds == LX_UTIME_NOW) && (Times[1].Nanoseconds == LX_UTIME_NOW);
    BOOLEAN Set = ((Times[0].Nanoseconds != LX_UTIME_NOW) && (Times[0].Nanoseconds != LX_UTIME_OMIT)) ||
                  ((Times[1].Nanoseconds != LX_UTIME_NOW) && (Times[1].Nanoseconds != LX_UTIME_OMIT));

    if (LxOwnerOrCapable(Process, Stat->Uid, Stat->Gid))
        return 0;
    if (Set)
        return -LX_EPERM;
    if (Touch && (LxPermission(Process, Stat->Mode, Stat->Uid, Stat->Gid, LX_MAY_WRITE) != 0))
        return -LX_EACCES;
    return 0;
}

LX_SYSCALL(LxSysUtimensat)
{
    LX_TIMESPEC Times[2];
    ULONG Flags = (ULONG)Call->Args[3];
    LX_NODE Node;
    PLX_FILE File;
    PSTR Path = NULL;
    LONG64 Result;
    LONG Error;

    if (Call->Args[2] != 0)
    {
        Result = LxCopyFromUser(Times, Call->Args[2], sizeof(Times));
        if (Result != 0)
            return Result;
    }
    else
    {
        Times[0].Nanoseconds = Times[1].Nanoseconds = LX_UTIME_NOW;
    }
    if ((Times[0].Nanoseconds == LX_UTIME_OMIT) && (Times[1].Nanoseconds == LX_UTIME_OMIT))
        return 0;

    if (Call->Args[1] != 0)
    {
        Result = LxCopyPath(Call->Args[1], &Path);
        if (Result != 0)
            return Result;
    }

    if ((Path == NULL) || ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH)))
    {
        LxFree(Path);
        LX_STAT Stat;

        File = LxCallFile(Call, 0);
        if (File == NULL)
            return -LX_EBADF;
        Error = LxFileStat(File, &Stat);
        if (Error == 0)
            Error = LxUtimePermission(Call->Process, &Stat, Times);
        if (Error == 0)
        {
            Error = LxVfsSetTimes(NULL, File, Times);
            if (Error == -LX_EACCES)
                Error = 0;
        }
        LxFileDereference(File);
        return Error;
    }

    Error = LxLookup(Call->Process, (LONG)Call->Args[0], Path, (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    LxFree(Path);
    if (Error != 0)
        return Error;
    {
        LX_STAT Stat;

        Error = LxVfsStatNode(Call->Process, &Node, &Stat);
        if (Error == 0)
            Error = LxUtimePermission(Call->Process, &Stat, Times);
    }
    if (Error == 0)
        Error = LxVfsSetTimes(&Node, NULL, Times);
    LxNodeFree(&Node);
    return Error;
}

LX_SYSCALL(LxSysUmask)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Previous;

    ExAcquireFastMutex(&Process->Lock);
    Previous = Process->Umask;
    Process->Umask = (ULONG)Call->Args[0] & 0777;
    ExReleaseFastMutex(&Process->Lock);
    return Previous;
}

LX_SYSCALL(LxSysFlock)
{
    PLX_FILE File = LxCallFile(Call, 0);
    ULONG Operation = (ULONG)Call->Args[1] & ~LX_LOCK_NB;

    if (File == NULL)
        return -LX_EBADF;
    LxFileDereference(File);
    if ((Operation != LX_LOCK_SH) && (Operation != LX_LOCK_EX) && (Operation != LX_LOCK_UN))
        return -LX_EINVAL;
    return 0;
}

static
LONG64
LxTransfer(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE In,
    _In_opt_ PLONG64 InOffset,
    _In_ PLX_FILE Out,
    _In_opt_ PLONG64 OutOffset,
    _In_ ULONG64 Count)
{
    PVOID Base = NULL;
    SIZE_T Size = LX_IO_CHUNK;
    ULONG64 Total = 0;
    LONG64 Result = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Process);

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return -LX_ENOMEM;

    while (Total < Count)
    {
        ULONG Chunk = (ULONG)min(Count - Total, LX_IO_CHUNK);
        LONG64 Read, Written;

        Read = LxFileRead(In, (ULONG64)(ULONG_PTR)Base, Chunk, InOffset);
        if (Read <= 0)
        {
            if ((Read < 0) && (Total == 0))
                Result = Read;
            break;
        }
        Written = LxFileWrite(Out, (ULONG64)(ULONG_PTR)Base, (ULONG64)Read, OutOffset);
        if (Written <= 0)
        {
            if ((Written < 0) && (Total == 0))
                Result = Written;
            break;
        }
        Total += (ULONG64)Written;
        if ((Written < Read) || (Read < Chunk))
            break;
    }

    Size = 0;
    ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
    return (Total != 0) ? (LONG64)Total : Result;
}

LX_SYSCALL(LxSysSendfile)
{
    PLX_FILE Out = LxCallFile(Call, 0), In = LxCallFile(Call, 1);
    LONG64 Offset = 0, Result;

    if ((Out == NULL) || (In == NULL))
    {
        Result = -LX_EBADF;
        goto Exit;
    }
    if (In->Directory || (In->Ops->Read == NULL))
    {
        Result = -LX_EINVAL;
        goto Exit;
    }
    if (Call->Args[2] != 0)
    {
        Result = LxCopyFromUser(&Offset, Call->Args[2], sizeof(Offset));
        if ((Result == 0) && (Offset < 0))
            Result = -LX_EINVAL;
        if (Result != 0)
            goto Exit;
    }

    Result = LxTransfer(Call->Process, In, (Call->Args[2] != 0) ? &Offset : NULL, Out, NULL, Call->Args[3]);
    if ((Result > 0) && (Call->Args[2] != 0))
        LxCopyToUser(Call->Args[2], &Offset, sizeof(Offset));

Exit:
    if (Out != NULL)
        LxFileDereference(Out);
    if (In != NULL)
        LxFileDereference(In);
    return Result;
}

static
BOOLEAN
LxSpliceWouldBlock(
    _In_ PLX_FILE File,
    _In_ ULONG Events)
{
    return LxFileIsPipe(File) && !(LxFilePoll(File, NULL) & (Events | LX_POLLHUP | LX_POLLERR));
}

static
LONG64
LxSpliceOffset(
    _In_ PLX_FILE File,
    _In_ ULONG64 Pointer,
    _Out_ PLONG64 Offset)
{
    LONG64 Result;

    *Offset = 0;
    if (Pointer == 0)
        return 0;
    if (LxFileIsPipe(File))
        return -LX_ESPIPE;
    if ((File->Ops->Seek == NULL) || File->NtPipe)
        return -LX_EINVAL;
    Result = LxCopyFromUser(Offset, Pointer, sizeof(*Offset));
    if ((Result == 0) && (*Offset < 0))
        Result = -LX_EINVAL;
    return Result;
}

LX_SYSCALL(LxSysSplice)
{
    PLX_FILE In = LxCallFile(Call, 0), Out = LxCallFile(Call, 2);
    ULONG Flags = (ULONG)Call->Args[5];
    LONG64 InOffset, OutOffset, Result;

    if ((In == NULL) || (Out == NULL) ||
        ((In->Flags & LX_O_ACCMODE) == LX_O_WRONLY) || ((Out->Flags & LX_O_ACCMODE) == LX_O_RDONLY))
    {
        Result = -LX_EBADF;
        goto Exit;
    }
    if ((Flags & ~LX_SPLICE_F_ALL) || (!LxFileIsPipe(In) && !LxFileIsPipe(Out)) ||
        (LxFileIsPipe(In) && LxFileIsPipe(Out) && (In->Object == Out->Object)) ||
        (!LxFileIsPipe(Out) && (Out->Flags & LX_O_APPEND)))
    {
        Result = -LX_EINVAL;
        goto Exit;
    }
    Result = LxSpliceOffset(In, Call->Args[1], &InOffset);
    if (Result == 0)
        Result = LxSpliceOffset(Out, Call->Args[3], &OutOffset);
    if ((Result != 0) || (Call->Args[4] == 0))
        goto Exit;
    if ((Flags & LX_SPLICE_F_NONBLOCK) &&
        (LxSpliceWouldBlock(In, LX_POLLIN) || LxSpliceWouldBlock(Out, LX_POLLOUT)))
    {
        Result = -LX_EAGAIN;
        goto Exit;
    }

    Result = LxTransfer(Call->Process, In, (Call->Args[1] != 0) ? &InOffset : NULL,
                        Out, (Call->Args[3] != 0) ? &OutOffset : NULL, Call->Args[4]);
    if (Result > 0)
    {
        if (Call->Args[1] != 0)
            LxCopyToUser(Call->Args[1], &InOffset, sizeof(InOffset));
        if (Call->Args[3] != 0)
            LxCopyToUser(Call->Args[3], &OutOffset, sizeof(OutOffset));
    }

Exit:
    if (In != NULL)
        LxFileDereference(In);
    if (Out != NULL)
        LxFileDereference(Out);
    return Result;
}

LX_SYSCALL(LxSysTee)
{
    PLX_FILE In = LxCallFile(Call, 0), Out = LxCallFile(Call, 1);
    ULONG Flags = (ULONG)Call->Args[3];
    LONG64 Result;

    if (Flags & ~LX_SPLICE_F_ALL)
        Result = -LX_EINVAL;
    else if ((In == NULL) || (Out == NULL) ||
             ((In->Flags & LX_O_ACCMODE) == LX_O_WRONLY) || ((Out->Flags & LX_O_ACCMODE) == LX_O_RDONLY))
        Result = -LX_EBADF;
    else if (!LxFileIsPipe(In) || !LxFileIsPipe(Out))
        Result = -LX_EINVAL;
    else
        Result = LxPipeTee(In, Out, Call->Args[2], (Flags & LX_SPLICE_F_NONBLOCK) != 0);

    if (In != NULL)
        LxFileDereference(In);
    if (Out != NULL)
        LxFileDereference(Out);
    return Result;
}

LX_SYSCALL(LxSysVmsplice)
{
    PLX_FILE File;
    ULONG Flags = (ULONG)Call->Args[3];
    ULONG Access;
    LONG64 Result = 0;

    if (Flags & ~LX_SPLICE_F_ALL)
        return -LX_EINVAL;
    File = LxCallFile(Call, 0);
    if (File == NULL)
        return -LX_EBADF;
    Access = File->Flags & LX_O_ACCMODE;
    if (!LxFileIsPipe(File))
        Result = -LX_EBADF;
    else if ((Flags & LX_SPLICE_F_NONBLOCK) &&
             LxSpliceWouldBlock(File, (Access == LX_O_RDONLY) ? LX_POLLIN : LX_POLLOUT))
        Result = -LX_EAGAIN;
    LxFileDereference(File);
    if (Result != 0)
        return Result;
    return LxVectorIo(Call, Access != LX_O_RDONLY, NULL);
}

LX_SYSCALL(LxSysCopyFileRange)
{
    PLX_FILE In = LxCallFile(Call, 0), Out = LxCallFile(Call, 2);
    LONG64 InOffset = 0, OutOffset = 0, Result;

    if ((In == NULL) || (Out == NULL))
    {
        Result = -LX_EBADF;
        goto Exit;
    }
    if (Call->Args[5] != 0)
    {
        Result = -LX_EINVAL;
        goto Exit;
    }
    if (In->Directory || Out->Directory)
    {
        Result = -LX_EISDIR;
        goto Exit;
    }
    if (Call->Args[1] != 0)
    {
        Result = LxCopyFromUser(&InOffset, Call->Args[1], sizeof(InOffset));
        if (Result != 0)
            goto Exit;
    }
    if (Call->Args[3] != 0)
    {
        Result = LxCopyFromUser(&OutOffset, Call->Args[3], sizeof(OutOffset));
        if (Result != 0)
            goto Exit;
    }

    Result = LxTransfer(Call->Process, In, (Call->Args[1] != 0) ? &InOffset : NULL,
                        Out, (Call->Args[3] != 0) ? &OutOffset : NULL, Call->Args[4]);
    if (Result > 0)
    {
        if (Call->Args[1] != 0)
            LxCopyToUser(Call->Args[1], &InOffset, sizeof(InOffset));
        if (Call->Args[3] != 0)
            LxCopyToUser(Call->Args[3], &OutOffset, sizeof(OutOffset));
    }

Exit:
    if (In != NULL)
        LxFileDereference(In);
    if (Out != NULL)
        LxFileDereference(Out);
    return Result;
}

static
LONG
LxMountLookup(
    _In_ PLX_PROCESS Process,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG Flags,
    _Out_ PLX_NODE Node)
{
    PSTR Path;
    LONG Error = LxLookupUser(Process, DirFd, UserPath, Flags, Node);

    if (Error != 0)
        return Error;
    Path = LxSynthFdPath(Process, Node);
    if (Path == NULL)
        return 0;
    LxNodeFree(Node);
    Error = LxLookup(Process, LX_AT_FDCWD, Path, Flags & ~LX_LOOKUP_FOLLOW, Node);
    LxFree(Path);
    return Error;
}

LX_SYSCALL(LxSysMount)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Flags = (ULONG)Call->Args[3];
    PSTR Source = NULL, Type = NULL;
    LX_NODE Target;
    LONG64 Result;
    LONG Error;

    Error = LxMountLookup(Process, LX_AT_FDCWD, Call->Args[1], LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;
    if (!LxMayMount(Process))
    {
        LxNodeFree(&Target);
        return -LX_EPERM;
    }

    if (Flags & LX_MS_REMOUNT)
    {
        PCSTR Relative;
        PLX_MOUNT Mount = LxMountFind(LxProcessNs(Process, LxNsMnt), Target.Path, &Relative);

        if ((Mount == NULL) || (Relative[0] != '\0'))
            Error = (Flags & LX_MS_BIND) ? 0 : -LX_EINVAL;
        else
            LxMountSetFlags(Mount, Flags & (LX_MS_RDONLY | LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC),
                            LX_MS_RDONLY | LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC);
        LxNodeFree(&Target);
        return Error;
    }
    if (!(Flags & LX_MS_BIND) && (Flags & (LX_MS_PRIVATE | LX_MS_SLAVE | LX_MS_SHARED | LX_MS_UNBINDABLE)))
    {
        PLX_NS Ns = LxProcessNs(Process, LxNsMnt);
        PCSTR Relative;
        PLX_MOUNT Mount = LxMountFind(Ns, Target.Path, &Relative);

        Error = ((Mount == NULL) || (Relative[0] != '\0')) ? -LX_EINVAL : LxMountSetPropagation(Ns, Mount, Flags);
        LxNodeFree(&Target);
        return Error;
    }
    if (Flags & LX_MS_BIND)
    {
        LX_NODE Source;

        Error = LxMountLookup(Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Source);
        if (Error == 0)
        {
            Error = LxMountBind(Process, &Source, Target.Path, Flags);
            LxNodeFree(&Source);
        }
        LxNodeFree(&Target);
        return Error;
    }
    if (Flags & LX_MS_MOVE)
    {
        LX_NODE Source;

        Error = LxMountLookup(Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Source);
        if (Error == 0)
        {
            Error = ((Source.Mount == NULL) || (Source.Relative[0] != '\0')) ? -LX_EINVAL :
                    LxMountMove(LxProcessNs(Process, LxNsMnt), Source.Mount, Target.Path);
            LxNodeFree(&Source);
        }
        LxNodeFree(&Target);
        return Error;
    }
    if (Target.Type != LX_S_IFDIR)
    {
        LxNodeFree(&Target);
        return -LX_ENOTDIR;
    }

    Result = (Call->Args[0] != 0) ? LxCopyStringFromUser(Call->Args[0], &Source, 255) : 0;
    if ((Result == 0) && (Call->Args[2] != 0))
        Result = LxCopyStringFromUser(Call->Args[2], &Type, 63);
    if ((Result == 0) && (Type == NULL))
        Result = -LX_EINVAL;
    if (Result == 0)
        Result = LxMountAdd(Process, Target.Path, (Source != NULL) ? Source : "none", Type, Flags, NULL);

    LxFree(Source);
    LxFree(Type);
    LxNodeFree(&Target);
    return Result;
}

LX_SYSCALL(LxSysUmount2)
{
    LX_NODE Target;
    LONG Error;

    if (Call->Args[1] & ~(ULONG64)(LX_MNT_FORCE | LX_MNT_DETACH | LX_MNT_EXPIRE | LX_UMOUNT_NOFOLLOW))
        return -LX_EINVAL;
    if (!LxMayMount(Call->Process))
        return -LX_EPERM;
    Error = LxMountLookup(Call->Process, LX_AT_FDCWD, Call->Args[0],
                          (Call->Args[1] & LX_UMOUNT_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;
    if ((Target.Mount == NULL) || (Target.Relative[0] != '\0'))
        Error = -LX_EINVAL;
    else
        Error = LxMountUnmount(LxProcessNs(Call->Process, LxNsMnt), Target.Mount, (ULONG)Call->Args[1]);
    LxNodeFree(&Target);
    return Error;
}

#define LX_XATTR_CREATE     0x1
#define LX_XATTR_REPLACE    0x2
#define LX_XATTR_NAME_MAX   255
#define LX_XATTR_SIZE_MAX   65536
#define LX_XATTR_BLOB_MAX   65535

typedef struct _LX_XATTR_TARGET
{
    PLX_FILE File;
    HANDLE Handle;
    BOOLEAN Close;
    ULONG Type;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
} LX_XATTR_TARGET, *PLX_XATTR_TARGET;

typedef struct _LX_XATTR_ARGS
{
    ULONG64 Value;
    ULONG Size;
    ULONG Flags;
} LX_XATTR_ARGS, *PLX_XATTR_ARGS;

static
VOID
LxXattrRelease(
    _Inout_ PLX_XATTR_TARGET Target)
{
    if (Target->Close && (Target->Handle != NULL))
        ZwClose(Target->Handle);
    if (Target->File != NULL)
        LxFileDereference(Target->File);
    RtlZeroMemory(Target, sizeof(*Target));
}

static
LONG
LxXattrResolve(
    _In_ PLX_CALL Call,
    _In_ LONG DirFd,
    _In_ ULONG64 UserPath,
    _In_ ULONG AtFlags,
    _In_ BOOLEAN UseFd,
    _In_ BOOLEAN Write,
    _Out_ PLX_XATTR_TARGET Target)
{
    ULONG64 Device;
    LX_NODE Node;
    PSTR Path = NULL;
    LONG Error;

    RtlZeroMemory(Target, sizeof(*Target));
    if (!UseFd)
    {
        LONG64 Result = (UserPath != 0) ? LxCopyPath(UserPath, &Path) : 0;

        if (Result != 0)
            return (LONG)Result;
        if (((Path == NULL) || (Path[0] == '\0')) && (AtFlags & LX_AT_EMPTY_PATH))
        {
            UseFd = TRUE;
        }
        else if (Path == NULL)
        {
            return -LX_EFAULT;
        }
    }

    if (UseFd)
    {
        LxFree(Path);
        Target->File = LxFdGet(Call->Process, DirFd);
        if (Target->File == NULL)
            return -LX_EBADF;
        Target->Type = Target->File->Type;
        Error = LxXattrOpenFile(Target->File, Write, &Target->Handle, &Target->Close);
    }
    else
    {
        Error = LxLookup(Call->Process, DirFd, Path, (AtFlags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
        LxFree(Path);
        if (Error != 0)
            return Error;
        Target->Type = Node.Type;
        Error = LxXattrOpenNode(&Node, Write, &Target->Handle);
        Target->Close = (Error == 0);
        LxNodeFree(&Node);
    }
    if (Error != 0)
    {
        LxXattrRelease(Target);
        return Error;
    }
    if (LxNtReadLxAttributes(Target->Handle, &Target->Mode, &Target->Uid, &Target->Gid, &Device) != 0)
        Target->Mode = Target->Uid = Target->Gid = MAXULONG;
    if ((Target->Mode != MAXULONG) && (Target->Mode & LX_S_IFMT))
        Target->Type = Target->Mode & LX_S_IFMT;
    return 0;
}

static
BOOLEAN
LxXattrInGroup(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Gid)
{
    ULONG Index;

    if (Process->Gid[1] == Gid)
        return TRUE;
    for (Index = 0; Index < Process->GroupCount; Index++)
    {
        if (Process->Groups[Index] == Gid)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxXattrPermission(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Name,
    _In_ PLX_XATTR_TARGET Target,
    _In_ BOOLEAN Write)
{
    BOOLEAN Admin = LxCapable(Process, LX_CAP_SYS_ADMIN);
    ULONG Type = Target->Type & LX_S_IFMT;
    ULONG Bits;

    if (strncmp(Name, "security.", 9) == 0)
        return (Write && !Admin) ? -LX_EPERM : 0;
    if (strncmp(Name, "trusted.", 8) == 0)
        return Admin ? 0 : (Write ? -LX_EPERM : -LX_ENODATA);
    if (strncmp(Name, "user.", 5) != 0)
        return -LX_EOPNOTSUPP;

    if ((Type != LX_S_IFREG) && (Type != LX_S_IFDIR))
        return Write ? -LX_EPERM : -LX_ENODATA;
    if (Target->Mode == MAXULONG)
        return 0;
    if ((Type == LX_S_IFDIR) && (Target->Mode & LX_S_ISVTX) && Write && (Process->Fsuid != Target->Uid) &&
        !LxCapableWrtIds(Process, Target->Uid, Target->Gid, LX_CAP_FOWNER))
    {
        return -LX_EPERM;
    }
    if (LxCapableWrtIds(Process, Target->Uid, Target->Gid, LX_CAP_DAC_OVERRIDE) ||
        (!Write && LxCapableWrtIds(Process, Target->Uid, Target->Gid, LX_CAP_DAC_READ_SEARCH)))
    {
        return 0;
    }
    if (Process->Fsuid == Target->Uid)
        Bits = Target->Mode >> 6;
    else if (LxXattrInGroup(Process, Target->Gid))
        Bits = Target->Mode >> 3;
    else
        Bits = Target->Mode;
    return (Bits & (Write ? 2 : 4)) ? 0 : -LX_EACCES;
}

static
LONG
LxXattrName(
    _In_ ULONG64 UserName,
    _Out_ PSTR *Name)
{
    LONG64 Result = LxCopyStringFromUser(UserName, Name, LX_XATTR_NAME_MAX + 1);

    if (Result == -LX_ENAMETOOLONG)
        return -LX_ERANGE;
    if (Result != 0)
        return (LONG)Result;
    if (((*Name)[0] == '\0') || (strlen(*Name) > LX_XATTR_NAME_MAX))
    {
        LxFree(*Name);
        *Name = NULL;
        return -LX_ERANGE;
    }
    return 0;
}

static
PUCHAR
LxXattrFind(
    _In_reads_bytes_(Length) PUCHAR Blob,
    _In_ ULONG Length,
    _In_ PCSTR Name,
    _Out_ PULONG RecordLength,
    _Out_ PUSHORT ValueLength)
{
    ULONG NameLength = (ULONG)strlen(Name);
    ULONG Offset = 0;

    while (Offset + 4 <= Length)
    {
        USHORT EntryName, EntryValue;

        RtlCopyMemory(&EntryName, Blob + Offset, 2);
        RtlCopyMemory(&EntryValue, Blob + Offset + 2, 2);
        if (Offset + 4 + EntryName + EntryValue > Length)
            break;
        if ((EntryName == NameLength) && (RtlCompareMemory(Blob + Offset + 4, Name, NameLength) == NameLength))
        {
            *RecordLength = 4 + EntryName + EntryValue;
            *ValueLength = EntryValue;
            return Blob + Offset;
        }
        Offset += 4 + EntryName + EntryValue;
    }
    *RecordLength = 0;
    *ValueLength = 0;
    return NULL;
}

static
LONG64
LxXattrGetCommon(
    _In_ PLX_CALL Call,
    _In_ PLX_XATTR_TARGET Target,
    _In_ ULONG64 UserName,
    _In_ ULONG64 Value,
    _In_ ULONG64 Size)
{
    PUCHAR Blob = LxAlloc(LX_XATTR_BLOB_MAX), Record;
    ULONG Length, RecordLength;
    USHORT ValueLength;
    PSTR Name = NULL;
    LONG64 Result;

    if (Blob == NULL)
        return -LX_ENOMEM;
    Result = LxXattrName(UserName, &Name);
    if (Result == 0)
        Result = LxXattrPermission(Call->Process, Name, Target, FALSE);
    if (Result == 0)
        Result = LxXattrRead(Target->Handle, Blob, LX_XATTR_BLOB_MAX, &Length);
    if (Result == 0)
    {
        Record = LxXattrFind(Blob, Length, Name, &RecordLength, &ValueLength);
        if (Record == NULL)
            Result = -LX_ENODATA;
        else if (Size == 0)
            Result = ValueLength;
        else if (Size < ValueLength)
            Result = -LX_ERANGE;
        else
        {
            Result = LxCopyToUser(Value, Record + 4 + strlen(Name), ValueLength);
            if (Result == 0)
                Result = ValueLength;
        }
    }
    LxFree(Name);
    LxFree(Blob);
    return Result;
}

static
LONG64
LxXattrSetCommon(
    _In_ PLX_CALL Call,
    _In_ PLX_XATTR_TARGET Target,
    _In_ ULONG64 UserName,
    _In_ ULONG64 Value,
    _In_ ULONG64 Size,
    _In_ ULONG Flags,
    _In_ BOOLEAN Remove)
{
    PUCHAR Blob = LxAlloc(2 * LX_XATTR_BLOB_MAX + LX_XATTR_SIZE_MAX), Record, Updated;
    ULONG Length, RecordLength, NameLength, NewLength;
    USHORT ValueLength;
    PSTR Name = NULL;
    LONG64 Result;

    if (Blob == NULL)
        return -LX_ENOMEM;
    Updated = Blob + LX_XATTR_BLOB_MAX;
    Result = LxXattrName(UserName, &Name);
    if ((Result == 0) && !Remove && (Flags & ~(LX_XATTR_CREATE | LX_XATTR_REPLACE)))
        Result = -LX_EINVAL;
    if ((Result == 0) && !Remove && (Size > LX_XATTR_SIZE_MAX))
        Result = -LX_E2BIG;
    if (Result == 0)
        Result = LxXattrPermission(Call->Process, Name, Target, TRUE);
    if (Result == 0)
        Result = LxXattrRead(Target->Handle, Blob, LX_XATTR_BLOB_MAX, &Length);
    if (Result != 0)
        goto Exit;

    NameLength = (ULONG)strlen(Name);
    Record = LxXattrFind(Blob, Length, Name, &RecordLength, &ValueLength);
    if ((Record == NULL) && (Remove || (Flags & LX_XATTR_REPLACE)))
    {
        Result = -LX_ENODATA;
        goto Exit;
    }
    if ((Record != NULL) && !Remove && (Flags & LX_XATTR_CREATE))
    {
        Result = -LX_EEXIST;
        goto Exit;
    }

    NewLength = 0;
    if (Record != NULL)
    {
        RtlCopyMemory(Updated, Blob, Record - Blob);
        NewLength = (ULONG)(Record - Blob);
        RtlCopyMemory(Updated + NewLength, Record + RecordLength, Length - NewLength - RecordLength);
        NewLength += Length - (ULONG)(Record - Blob) - RecordLength;
    }
    else
    {
        RtlCopyMemory(Updated, Blob, Length);
        NewLength = Length;
    }
    if (!Remove)
    {
        USHORT Lengths[2] = { (USHORT)NameLength, (USHORT)Size };

        if (NewLength + 4 + NameLength + Size > LX_XATTR_BLOB_MAX)
        {
            Result = -LX_ENOSPC;
            goto Exit;
        }
        RtlCopyMemory(Updated + NewLength, Lengths, 4);
        RtlCopyMemory(Updated + NewLength + 4, Name, NameLength);
        Result = LxCopyFromUser(Updated + NewLength + 4 + NameLength, Value, (SIZE_T)Size);
        if (Result != 0)
            goto Exit;
        NewLength += 4 + NameLength + (ULONG)Size;
    }
    Result = LxXattrWrite(Target->Handle, Updated, NewLength);

Exit:
    LxFree(Name);
    LxFree(Blob);
    return Result;
}

static
LONG64
LxXattrListCommon(
    _In_ PLX_CALL Call,
    _In_ PLX_XATTR_TARGET Target,
    _In_ ULONG64 List,
    _In_ ULONG64 Size)
{
    PUCHAR Blob = LxAlloc(2 * LX_XATTR_BLOB_MAX), Names;
    ULONG Length, Offset = 0, Total = 0;
    LONG64 Result;

    if (Blob == NULL)
        return -LX_ENOMEM;
    Names = Blob + LX_XATTR_BLOB_MAX;
    Result = LxXattrRead(Target->Handle, Blob, LX_XATTR_BLOB_MAX, &Length);
    while ((Result == 0) && (Offset + 4 <= Length))
    {
        USHORT EntryName, EntryValue;

        RtlCopyMemory(&EntryName, Blob + Offset, 2);
        RtlCopyMemory(&EntryValue, Blob + Offset + 2, 2);
        if (Offset + 4 + EntryName + EntryValue > Length)
            break;
        if (!((EntryName >= 8) && (RtlCompareMemory(Blob + Offset + 4, "trusted.", 8) == 8) &&
              !LxCapable(Call->Process, LX_CAP_SYS_ADMIN)))
        {
            RtlCopyMemory(Names + Total, Blob + Offset + 4, EntryName);
            Total += EntryName;
            Names[Total++] = '\0';
        }
        Offset += 4 + EntryName + EntryValue;
    }
    if (Result == 0)
    {
        if (Size == 0)
            Result = Total;
        else if (Size < Total)
            Result = -LX_ERANGE;
        else
        {
            Result = LxCopyToUser(List, Names, Total);
            if (Result == 0)
                Result = Total;
        }
    }
    LxFree(Blob);
    return Result;
}

static
LONG64
LxXattrCall(
    _In_ PLX_CALL Call,
    _In_ ULONG Operation,
    _In_ LONG DirFd,
    _In_ ULONG64 Path,
    _In_ ULONG AtFlags,
    _In_ BOOLEAN UseFd,
    _In_ ULONG64 Name,
    _In_ ULONG64 Value,
    _In_ ULONG64 Size,
    _In_ ULONG Flags)
{
    LX_XATTR_TARGET Target;
    LONG64 Result;

    Result = LxXattrResolve(Call, DirFd, Path, AtFlags, UseFd, (Operation == 1) || (Operation == 3), &Target);
    if (Result != 0)
        return Result;
    switch (Operation)
    {
        case 0:
            Result = LxXattrGetCommon(Call, &Target, Name, Value, Size);
            break;
        case 1:
            Result = LxXattrSetCommon(Call, &Target, Name, Value, Size, Flags, FALSE);
            break;
        case 2:
            Result = LxXattrListCommon(Call, &Target, Value, Size);
            break;
        default:
            Result = LxXattrSetCommon(Call, &Target, Name, 0, 0, 0, TRUE);
            break;
    }
    LxXattrRelease(&Target);
    return Result;
}

LX_SYSCALL(LxSysSetxattr)
{
    return LxXattrCall(Call, 1, LX_AT_FDCWD, Call->Args[0], 0, FALSE, Call->Args[1], Call->Args[2], Call->Args[3], (ULONG)Call->Args[4]);
}

LX_SYSCALL(LxSysLsetxattr)
{
    return LxXattrCall(Call, 1, LX_AT_FDCWD, Call->Args[0], LX_AT_SYMLINK_NOFOLLOW, FALSE, Call->Args[1], Call->Args[2],
                       Call->Args[3], (ULONG)Call->Args[4]);
}

LX_SYSCALL(LxSysFsetxattr)
{
    return LxXattrCall(Call, 1, (LONG)Call->Args[0], 0, 0, TRUE, Call->Args[1], Call->Args[2], Call->Args[3], (ULONG)Call->Args[4]);
}

LX_SYSCALL(LxSysGetxattr)
{
    return LxXattrCall(Call, 0, LX_AT_FDCWD, Call->Args[0], 0, FALSE, Call->Args[1], Call->Args[2], Call->Args[3], 0);
}

LX_SYSCALL(LxSysLgetxattr)
{
    return LxXattrCall(Call, 0, LX_AT_FDCWD, Call->Args[0], LX_AT_SYMLINK_NOFOLLOW, FALSE, Call->Args[1], Call->Args[2],
                       Call->Args[3], 0);
}

LX_SYSCALL(LxSysFgetxattr)
{
    return LxXattrCall(Call, 0, (LONG)Call->Args[0], 0, 0, TRUE, Call->Args[1], Call->Args[2], Call->Args[3], 0);
}

LX_SYSCALL(LxSysListxattr)
{
    return LxXattrCall(Call, 2, LX_AT_FDCWD, Call->Args[0], 0, FALSE, 0, Call->Args[1], Call->Args[2], 0);
}

LX_SYSCALL(LxSysLlistxattr)
{
    return LxXattrCall(Call, 2, LX_AT_FDCWD, Call->Args[0], LX_AT_SYMLINK_NOFOLLOW, FALSE, 0, Call->Args[1], Call->Args[2], 0);
}

LX_SYSCALL(LxSysFlistxattr)
{
    return LxXattrCall(Call, 2, (LONG)Call->Args[0], 0, 0, TRUE, 0, Call->Args[1], Call->Args[2], 0);
}

LX_SYSCALL(LxSysRemovexattr)
{
    return LxXattrCall(Call, 3, LX_AT_FDCWD, Call->Args[0], 0, FALSE, Call->Args[1], 0, 0, 0);
}

LX_SYSCALL(LxSysLremovexattr)
{
    return LxXattrCall(Call, 3, LX_AT_FDCWD, Call->Args[0], LX_AT_SYMLINK_NOFOLLOW, FALSE, Call->Args[1], 0, 0, 0);
}

LX_SYSCALL(LxSysFremovexattr)
{
    return LxXattrCall(Call, 3, (LONG)Call->Args[0], 0, 0, TRUE, Call->Args[1], 0, 0, 0);
}

static
LONG64
LxXattrArgs(
    _In_ ULONG64 Pointer,
    _In_ ULONG64 Size,
    _Out_ PLX_XATTR_ARGS Args)
{
    UCHAR Extra[64];
    ULONG64 Offset;
    LONG64 Result;

    RtlZeroMemory(Args, sizeof(*Args));
    if (Size < sizeof(*Args))
        return -LX_EINVAL;
    if (Size > LX_PAGE_SIZE)
        return -LX_E2BIG;
    Result = LxCopyFromUser(Args, Pointer, sizeof(*Args));
    for (Offset = sizeof(*Args); (Result == 0) && (Offset < Size); Offset += sizeof(Extra))
    {
        ULONG Chunk = (ULONG)min(sizeof(Extra), Size - Offset), Index;

        Result = LxCopyFromUser(Extra, Pointer + Offset, Chunk);
        for (Index = 0; (Result == 0) && (Index < Chunk); Index++)
        {
            if (Extra[Index] != 0)
                Result = -LX_E2BIG;
        }
    }
    return Result;
}

LX_SYSCALL(LxSysSetxattrat)
{
    ULONG AtFlags = (ULONG)Call->Args[2];
    LX_XATTR_ARGS Args;
    LONG64 Result;

    if (AtFlags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    Result = LxXattrArgs(Call->Args[4], Call->Args[5], &Args);
    if (Result != 0)
        return Result;
    return LxXattrCall(Call, 1, (LONG)Call->Args[0], Call->Args[1], AtFlags, FALSE, Call->Args[3], Args.Value, Args.Size,
                       Args.Flags);
}

LX_SYSCALL(LxSysGetxattrat)
{
    ULONG AtFlags = (ULONG)Call->Args[2];
    LX_XATTR_ARGS Args;
    LONG64 Result;

    if (AtFlags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    Result = LxXattrArgs(Call->Args[4], Call->Args[5], &Args);
    if ((Result == 0) && (Args.Flags != 0))
        Result = -LX_EINVAL;
    if (Result != 0)
        return Result;
    return LxXattrCall(Call, 0, (LONG)Call->Args[0], Call->Args[1], AtFlags, FALSE, Call->Args[3], Args.Value, Args.Size, 0);
}

LX_SYSCALL(LxSysListxattrat)
{
    ULONG AtFlags = (ULONG)Call->Args[2];

    if (AtFlags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    return LxXattrCall(Call, 2, (LONG)Call->Args[0], Call->Args[1], AtFlags, FALSE, 0, Call->Args[3], Call->Args[4], 0);
}

LX_SYSCALL(LxSysRemovexattrat)
{
    ULONG AtFlags = (ULONG)Call->Args[2];

    if (AtFlags & ~(LX_AT_SYMLINK_NOFOLLOW | LX_AT_EMPTY_PATH))
        return -LX_EINVAL;
    return LxXattrCall(Call, 3, (LONG)Call->Args[0], Call->Args[1], AtFlags, FALSE, Call->Args[3], 0, 0, 0);
}

#define LX_AT_HANDLE_MNT_ID_UNIQUE  0x001
#define LX_AT_HANDLE_FID            0x200
#define LX_AT_SYMLINK_FOLLOW        0x400
#define LX_MAX_HANDLE_SZ            128
#define LX_FILEID_INO32_GEN         1
#define LX_FILEID_INVALID           0xFF

static
LONG
LxNtFileId(
    _In_ HANDLE Handle,
    _Out_ PULONG64 FileId)
{
    FILE_INTERNAL_INFORMATION Internal;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Status = ZwQueryInformationFile(Handle, &Iosb, &Internal, sizeof(Internal), FileInternalInformation);
    if (!NT_SUCCESS(Status))
        return -LxStatusToErrno(Status);
    *FileId = (ULONG64)Internal.IndexNumber.QuadPart;
    return 0;
}

static
LONG
LxNodeFileId(
    _In_ PLX_NODE Node,
    _Out_ PULONG64 FileId)
{
    HANDLE Handle;
    LONG Error;

    Error = LxNtOpenHandle(&Node->NtPath, FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Handle);
    if (Error != 0)
        return Error;
    Error = LxNtFileId(Handle, FileId);
    ZwClose(Handle);
    return Error;
}

static
PLX_MOUNT
LxFileMount(
    _In_ PLX_FILE File)
{
    PLX_MOUNT Mount;
    PCSTR Relative;

    if ((File->Mount == NULL) || (File->Path == NULL) || (File->Mount->Ns == NULL))
        return File->Mount;
    Mount = LxMountFind(File->Mount->Ns, File->Path, &Relative);
    return (Mount != NULL) ? Mount : File->Mount;
}

static
POBJECT_NAME_INFORMATION
LxNtObjectName(
    _In_ HANDLE Handle)
{
    ULONG Size = sizeof(OBJECT_NAME_INFORMATION) + LX_PATH_MAX * sizeof(WCHAR), Returned = 0;
    POBJECT_NAME_INFORMATION Name = LxAllocPaged(Size);
    NTSTATUS Status;

    if (Name == NULL)
        return NULL;
    Status = ZwQueryObject(Handle, ObjectNameInformation, Name, Size, &Returned);
    if (!NT_SUCCESS(Status) || (Name->Name.Length == 0))
    {
        LxFreePaged(Name);
        return NULL;
    }
    return Name;
}

LX_SYSCALL(LxSysNameToHandleAt)
{
    LONG DirFd = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[4];
    ULONG Header[2], Data[2];
    PLX_MOUNT Mount = NULL;
    ULONG64 FileId = 0, MountId;
    LX_NODE Node;
    PSTR Path;
    LONG64 Result;
    LONG Error;

    if (Flags & ~(LX_AT_EMPTY_PATH | LX_AT_SYMLINK_FOLLOW | LX_AT_HANDLE_FID | LX_AT_HANDLE_MNT_ID_UNIQUE))
        return -LX_EINVAL;
    Result = LxCopyFromUser(Header, Call->Args[2], sizeof(Header));
    if (Result != 0)
        return Result;
    if (Header[0] > LX_MAX_HANDLE_SZ)
        return -LX_EINVAL;
    Result = LxCopyPath(Call->Args[1], &Path);
    if (Result != 0)
        return Result;

    if ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH) && (DirFd != LX_AT_FDCWD))
    {
        PLX_FILE File = LxFdGet(Call->Process, DirFd);

        LxFree(Path);
        if (File == NULL)
            return -LX_EBADF;
        Mount = LxFileMount(File);
        if ((Mount == NULL) || (Mount->Kind != LxFsNt) || (File->Handle == NULL) || File->NtPipe)
            Error = -LX_EOPNOTSUPP;
        else
            Error = LxNtFileId(File->Handle, &FileId);
        LxFileDereference(File);
    }
    else
    {
        if (Path[0] == '\0')
        {
            LxFree(Path);
            if (!(Flags & LX_AT_EMPTY_PATH))
                return -LX_ENOENT;
            Path = LxProcessCwd(Call->Process);
            if (Path == NULL)
                return -LX_ENOMEM;
        }
        Error = LxLookup(Call->Process, DirFd, Path, (Flags & LX_AT_SYMLINK_FOLLOW) ? LX_LOOKUP_FOLLOW : 0, &Node);
        LxFree(Path);
        if (Error != 0)
            return Error;
        Mount = Node.Mount;
        if (!Node.Exists)
            Error = -LX_ENOENT;
        else if ((Mount == NULL) || (Mount->Kind != LxFsNt))
            Error = -LX_EOPNOTSUPP;
        else
            Error = LxNodeFileId(&Node, &FileId);
        LxNodeFree(&Node);
    }
    if (Error != 0)
        return Error;

    MountId = Mount->Id;
    Result = (Flags & LX_AT_HANDLE_MNT_ID_UNIQUE) ? LxCopyToUser(Call->Args[3], &MountId, sizeof(MountId)) :
                                                    LxCopyToUser(Call->Args[3], &MountId, sizeof(ULONG));
    if (Result != 0)
        return Result;
    if (Header[0] < sizeof(Data))
    {
        Header[0] = sizeof(Data);
        Header[1] = LX_FILEID_INVALID;
        Result = LxCopyToUser(Call->Args[2], Header, sizeof(Header));
        return (Result != 0) ? Result : -LX_EOVERFLOW;
    }
    Header[0] = sizeof(Data);
    Header[1] = LX_FILEID_INO32_GEN;
    Data[0] = (ULONG)FileId;
    Data[1] = (ULONG)(FileId >> 48);
    Result = LxCopyToUser(Call->Args[2], Header, sizeof(Header));
    if (Result == 0)
        Result = LxCopyToUser(Call->Args[2] + sizeof(Header), Data, sizeof(Data));
    return Result;
}

static
LONG
LxHandleToPath(
    _In_ PLX_MOUNT Mount,
    _In_ ULONG64 FileId,
    _Out_ PSTR *Path)
{
    POBJECT_NAME_INFORMATION RootName = NULL, TargetName = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING RootPath, ById;
    IO_STATUS_BLOCK Iosb;
    HANDLE Root = NULL, Target = NULL;
    ULONG64 Check = 0;
    ULONG RootChars, Chars, Index, Needed = 0, Written = 0;
    SIZE_T TargetLength;
    PWCHAR Relative;
    NTSTATUS Status;
    LONG Error;

    *Path = NULL;
    Error = LxNtPath(Mount, "", &RootPath);
    if (Error != 0)
        return Error;
    Error = LxNtOpenHandle(&RootPath, FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0, &Root);
    LxFreePaged(RootPath.Buffer);
    if (Error != 0)
        return -LX_ESTALE;

    ById.Buffer = (PWCH)&FileId;
    ById.Length = ById.MaximumLength = sizeof(FileId);
    InitializeObjectAttributes(&ObjectAttributes, &ById, OBJ_KERNEL_HANDLE, Root, NULL);
    Status = ZwCreateFile(&Target, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                          FILE_OPEN_BY_FILE_ID | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    Error = -LX_ESTALE;
    if (!NT_SUCCESS(Status))
    {
        Target = NULL;
        goto Exit;
    }
    if ((LxNtFileId(Target, &Check) != 0) || (Check != FileId))
        goto Exit;
    RootName = LxNtObjectName(Root);
    TargetName = LxNtObjectName(Target);
    if ((RootName == NULL) || (TargetName == NULL))
        goto Exit;

    RootChars = RootName->Name.Length / sizeof(WCHAR);
    while ((RootChars != 0) && (RootName->Name.Buffer[RootChars - 1] == L'\\'))
        RootChars--;
    RootName->Name.Length = (USHORT)(RootChars * sizeof(WCHAR));
    Chars = TargetName->Name.Length / sizeof(WCHAR);
    if (!RtlPrefixUnicodeString(&RootName->Name, &TargetName->Name, TRUE) ||
        ((Chars != RootChars) && (TargetName->Name.Buffer[RootChars] != L'\\')))
    {
        goto Exit;
    }

    Relative = TargetName->Name.Buffer + RootChars;
    Chars -= RootChars;
    for (Index = 0; Index < Chars; Index++)
    {
        if (Relative[Index] == L'\\')
            Relative[Index] = L'/';
    }
    LxUnicodeDecodeLinuxName(Relative, Chars);
    if ((Chars != 0) && !NT_SUCCESS(RtlUnicodeToUTF8N(NULL, 0, &Needed, Relative, Chars * sizeof(WCHAR))))
        goto Exit;

    TargetLength = strlen(Mount->Target);
    *Path = LxAlloc(TargetLength + Needed + 2);
    Error = -LX_ENOMEM;
    if (*Path == NULL)
        goto Exit;
    RtlCopyMemory(*Path, Mount->Target, TargetLength);
    if ((TargetLength != 0) && ((*Path)[TargetLength - 1] == '/') && (Chars != 0))
        TargetLength--;
    if ((Chars != 0) &&
        !NT_SUCCESS(RtlUnicodeToUTF8N(*Path + TargetLength, Needed, &Written, Relative, Chars * sizeof(WCHAR))))
    {
        LxFree(*Path);
        *Path = NULL;
        Error = -LX_ESTALE;
        goto Exit;
    }
    (*Path)[TargetLength + Written] = '\0';
    if ((*Path)[0] == '\0')
    {
        (*Path)[0] = '/';
        (*Path)[1] = '\0';
    }
    Error = 0;

Exit:
    if (RootName != NULL)
        LxFreePaged(RootName);
    if (TargetName != NULL)
        LxFreePaged(TargetName);
    if (Target != NULL)
        ZwClose(Target);
    ZwClose(Root);
    return Error;
}

LX_SYSCALL(LxSysOpenByHandleAt)
{
    LONG MountFd = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[2];
    PLX_PROCESS Process = Call->Process;
    ULONG Header[2], Data[2];
    ULONG64 FileId, Check = 0;
    PLX_MOUNT Mount;
    PLX_FILE File;
    PLX_TTY Tty;
    LX_NODE Node;
    PSTR Path;
    LONG64 Result;
    LONG Error;

    if (!LxCapable(Process, LX_CAP_DAC_READ_SEARCH))
        return -LX_EPERM;
    Result = LxCopyFromUser(Header, Call->Args[1], sizeof(Header));
    if (Result != 0)
        return Result;
    if ((Header[0] == 0) || (Header[0] > LX_MAX_HANDLE_SZ) || ((LONG)Header[1] < 0))
        return -LX_EINVAL;

    if (MountFd == LX_AT_FDCWD)
    {
        Path = LxProcessCwd(Process);
        if (Path == NULL)
            return -LX_ENOMEM;
        Error = LxLookup(Process, LX_AT_FDCWD, Path, LX_LOOKUP_FOLLOW, &Node);
        LxFree(Path);
        if (Error != 0)
            return Error;
        Mount = Node.Mount;
        LxNodeFree(&Node);
    }
    else
    {
        File = LxFdGet(Process, MountFd);
        if (File == NULL)
            return -LX_EBADF;
        Mount = LxFileMount(File);
        LxFileDereference(File);
    }
    if ((Mount == NULL) || (Mount->Kind != LxFsNt))
        return -LX_EOPNOTSUPP;
    if ((Header[1] != LX_FILEID_INO32_GEN) || (Header[0] != sizeof(Data)))
        return -LX_ESTALE;
    Result = LxCopyFromUser(Data, Call->Args[1] + sizeof(Header), sizeof(Data));
    if (Result != 0)
        return Result;
    FileId = ((ULONG64)Data[1] << 48) | Data[0];

    Error = LxHandleToPath(Mount, FileId, &Path);
    if (Error != 0)
        return Error;
    Error = LxLookup(Process, LX_AT_FDCWD, Path, 0, &Node);
    LxFree(Path);
    if (Error != 0)
        return -LX_ESTALE;
    if (!Node.Exists || (Node.Mount != Mount) || (LxNodeFileId(&Node, &Check) != 0) || (Check != FileId))
    {
        LxNodeFree(&Node);
        return -LX_ESTALE;
    }
    Error = LxVfsOpenNode(Process, &Node, Flags & ~(LX_O_CREAT | LX_O_EXCL), 0, &File);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;

    Tty = LxFileGetTty(File);
    if ((Tty != NULL) && !(Flags & LX_O_NOCTTY))
        LxTtyAttachControlling(Process, Tty);
    Result = LxFdInstall(Process, File, 0, (Flags & LX_O_CLOEXEC) != 0);
    LxFileDereference(File);
    return Result;
}

#define LX_FSOPEN_CLOEXEC           0x00000001
#define LX_FSMOUNT_CLOEXEC          0x00000001
#define LX_FSCONFIG_SET_FLAG        0
#define LX_FSCONFIG_SET_STRING      1
#define LX_FSCONFIG_CMD_CREATE      6
#define LX_FSCONFIG_CMD_RECONFIGURE 7
#define LX_FSCONFIG_CMD_CREATE_EXCL 8
#define LX_MOVE_MOUNT_F_EMPTY_PATH  0x00000004
#define LX_MOVE_MOUNT_T_EMPTY_PATH  0x00000040
#define LX_OPEN_TREE_CLONE          0x00000001
#define LX_MOUNT_ATTR_RDONLY        0x00000001
#define LX_MOUNT_ATTR_NOSUID        0x00000002
#define LX_MOUNT_ATTR_NODEV         0x00000004
#define LX_MOUNT_ATTR_NOEXEC        0x00000008

typedef struct _LX_FSCONTEXT
{
    CHAR FsType[32];
    CHAR Source[128];
    ULONG Mode;
    ULONG Flags;
    BOOLEAN Created;
    BOOLEAN Mounted;
} LX_FSCONTEXT, *PLX_FSCONTEXT;

static
VOID
LxFsContextRelease(
    _In_ PLX_FILE File)
{
    LxFree(File->Object);
    File->Object = NULL;
}

static const LX_FILE_OPS LxFsContextOps =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, LxFsContextRelease
};

static
ULONG
LxMountAttrToFlags(
    _In_ ULONG64 Attributes)
{
    ULONG Flags = 0;

    if (Attributes & LX_MOUNT_ATTR_RDONLY)
        Flags |= LX_MS_RDONLY;
    if (Attributes & LX_MOUNT_ATTR_NOSUID)
        Flags |= LX_MS_NOSUID;
    if (Attributes & LX_MOUNT_ATTR_NODEV)
        Flags |= LX_MS_NODEV;
    if (Attributes & LX_MOUNT_ATTR_NOEXEC)
        Flags |= LX_MS_NOEXEC;
    return Flags;
}

static
LONG64
LxInstallMountRoot(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MOUNT Mount,
    _In_ BOOLEAN CloseOnExec)
{
    PLX_FILE File;
    PSTR Target;
    LONG64 Result;
    LONG Error;

    ExAcquireFastMutex(&Process->Instance->Lock);
    Target = (Mount->Alias != NULL) ? LxStrDup(Mount->Alias) : LxStrDup(Mount->Target);
    ExReleaseFastMutex(&Process->Instance->Lock);
    if (Target == NULL)
        return -LX_ENOMEM;
    Error = LxVfsOpen(Process, LX_AT_FDCWD, Target, LX_O_PATH | LX_O_DIRECTORY, 0, &File);
    LxFree(Target);
    if (Error != 0)
        return Error;
    Result = LxFdInstall(Process, File, 0, CloseOnExec);
    LxFileDereference(File);
    return Result;
}

static
PLX_MOUNT
LxMountFromFd(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _In_ ULONG64 UserPath,
    _In_ BOOLEAN EmptyPath,
    _Out_ PLONG Error)
{
    PLX_MOUNT Mount = NULL;
    PCSTR Relative;
    CHAR First = 0;

    *Error = 0;
    if ((UserPath != 0) && (LxCopyFromUser(&First, UserPath, 1) != 0))
    {
        *Error = -LX_EFAULT;
        return NULL;
    }
    if (EmptyPath && (First == '\0'))
    {
        PLX_FILE File = LxFdGet(Process, Fd);

        if (File == NULL)
        {
            *Error = -LX_EBADF;
            return NULL;
        }
        if ((File->Path != NULL) && (File->Path[0] == '/'))
            Mount = LxMountFind(LxProcessNs(Process, LxNsMnt), File->Path, &Relative);
        LxFileDereference(File);
        if ((Mount == NULL) || (Relative[0] != '\0'))
        {
            *Error = -LX_EINVAL;
            return NULL;
        }
        return Mount;
    }
    else
    {
        LX_NODE Node;

        *Error = LxMountLookup(Process, Fd, UserPath, LX_LOOKUP_FOLLOW, &Node);
        if (*Error != 0)
            return NULL;
        if ((Node.Mount == NULL) || (Node.Relative[0] != '\0'))
            *Error = -LX_EINVAL;
        else
            Mount = Node.Mount;
        LxNodeFree(&Node);
        return Mount;
    }
}

LX_SYSCALL(LxSysFsopen)
{
    PLX_FSCONTEXT Context;
    PLX_FILE File;
    PSTR Name;
    LONG64 Result;
    LONG Fd;

    if (!LxMayMount(Call->Process))
        return -LX_EPERM;
    if (Call->Args[1] & ~LX_FSOPEN_CLOEXEC)
        return -LX_EINVAL;
    Result = LxCopyStringFromUser(Call->Args[0], &Name, 31);
    if (Result != 0)
        return Result;
    if ((strcmp(Name, "tmpfs") != 0) && (strcmp(Name, "ramfs") != 0) && (strcmp(Name, "proc") != 0) &&
        (strcmp(Name, "sysfs") != 0) && (strcmp(Name, "cgroup2") != 0) && (strcmp(Name, "devpts") != 0) &&
        (strcmp(Name, "mqueue") != 0) && (strcmp(Name, "devtmpfs") != 0))
    {
        LxFree(Name);
        return -LX_ENODEV;
    }

    Context = LxAlloc(sizeof(*Context));
    File = LxFileAlloc(&LxFsContextOps, LX_O_RDWR, 0);
    if ((Context == NULL) || (File == NULL))
    {
        LxFree(Context);
        LxFree(Name);
        if (File != NULL)
            LxFileDereference(File);
        return -LX_ENOMEM;
    }
    RtlStringCbCopyA(Context->FsType, sizeof(Context->FsType), Name);
    RtlStringCbCopyA(Context->Source, sizeof(Context->Source), Name);
    Context->Mode = 01777;
    LxFree(Name);
    File->Object = Context;
    File->Path = LxStrDup("anon_inode:[fscontext]");
    Fd = LxFdInstall(Call->Process, File, 0, (Call->Args[1] & LX_FSOPEN_CLOEXEC) != 0);
    LxFileDereference(File);
    return Fd;
}

static
PLX_FSCONTEXT
LxFsContextGet(
    _In_ PLX_PROCESS Process,
    _In_ LONG Fd,
    _Out_ PLX_FILE *File)
{
    *File = LxFdGet(Process, Fd);
    if (*File == NULL)
        return NULL;
    if ((*File)->Ops != &LxFsContextOps)
    {
        LxFileDereference(*File);
        *File = NULL;
        return NULL;
    }
    return (*File)->Object;
}

LX_SYSCALL(LxSysFsconfig)
{
    PLX_FSCONTEXT Context;
    PLX_FILE File;
    PSTR Key = NULL, Value = NULL;
    ULONG Command = (ULONG)Call->Args[1];
    LONG64 Result = 0;

    Context = LxFsContextGet(Call->Process, (LONG)Call->Args[0], &File);
    if (Context == NULL)
        return (File == NULL) ? -LX_EBADF : -LX_EINVAL;

    if ((Command == LX_FSCONFIG_SET_FLAG) || (Command == LX_FSCONFIG_SET_STRING))
    {
        Result = LxCopyStringFromUser(Call->Args[2], &Key, 255);
        if ((Result == 0) && (Command == LX_FSCONFIG_SET_STRING))
            Result = LxCopyStringFromUser(Call->Args[3], &Value, 1023);
        if (Result == 0)
        {
            if ((strcmp(Key, "ro") == 0) && (Command == LX_FSCONFIG_SET_FLAG))
                Context->Flags |= LX_MS_RDONLY;
            else if ((strcmp(Key, "rw") == 0) && (Command == LX_FSCONFIG_SET_FLAG))
                Context->Flags &= ~LX_MS_RDONLY;
            else if ((strcmp(Key, "source") == 0) && (Value != NULL))
                RtlStringCbCopyA(Context->Source, sizeof(Context->Source), Value);
            else if ((strcmp(Key, "mode") == 0) && (Value != NULL))
            {
                ULONG Mode = 0;
                PCSTR Cursor = Value;

                while ((*Cursor >= '0') && (*Cursor <= '7'))
                    Mode = Mode * 8 + (*Cursor++ - '0');
                if ((*Cursor != '\0') || (Mode > 07777))
                    Result = -LX_EINVAL;
                else
                    Context->Mode = Mode;
            }
        }
    }
    else if ((Command == LX_FSCONFIG_CMD_CREATE) || (Command == LX_FSCONFIG_CMD_CREATE_EXCL))
    {
        if (Context->Created)
            Result = -LX_EBUSY;
        else
            Context->Created = TRUE;
    }
    else if (Command == LX_FSCONFIG_CMD_RECONFIGURE)
    {
        Result = 0;
    }
    else if (Command > LX_FSCONFIG_CMD_CREATE_EXCL)
    {
        Result = -LX_EOPNOTSUPP;
    }

    LxFree(Key);
    LxFree(Value);
    LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysFsmount)
{
    PLX_FSCONTEXT Context;
    PLX_MOUNT Mount;
    PLX_FILE File;
    LONG64 Result;
    LONG Error;

    if (!LxMayMount(Call->Process))
        return -LX_EPERM;
    if (Call->Args[1] & ~LX_FSMOUNT_CLOEXEC)
        return -LX_EINVAL;
    Context = LxFsContextGet(Call->Process, (LONG)Call->Args[0], &File);
    if (Context == NULL)
        return (File == NULL) ? -LX_EBADF : -LX_EINVAL;
    if (!Context->Created || Context->Mounted)
    {
        LxFileDereference(File);
        return -LX_EBUSY;
    }

    Error = LxMountCreateDetached(Call->Process, Context->FsType, Context->Source,
                                  Context->Flags | LxMountAttrToFlags(Call->Args[2]), Context->Mode, &Mount);
    if (Error != 0)
    {
        LxFileDereference(File);
        return Error;
    }
    Context->Mounted = TRUE;
    LxFileDereference(File);
    Result = LxInstallMountRoot(Call->Process, Mount, (Call->Args[1] & LX_FSMOUNT_CLOEXEC) != 0);
    return Result;
}

LX_SYSCALL(LxSysMoveMount)
{
    ULONG Flags = (ULONG)Call->Args[4];
    PLX_MOUNT Mount;
    LX_NODE Target;
    CHAR First = 0;
    LONG Error;

    if (!LxMayMount(Call->Process))
        return -LX_EPERM;
    Mount = LxMountFromFd(Call->Process, (LONG)Call->Args[0], Call->Args[1], (Flags & LX_MOVE_MOUNT_F_EMPTY_PATH) != 0, &Error);
    if (Mount == NULL)
        return Error;

    if ((Flags & LX_MOVE_MOUNT_T_EMPTY_PATH) && (Call->Args[3] != 0) &&
        (LxCopyFromUser(&First, Call->Args[3], 1) == 0) && (First == '\0'))
    {
        PLX_FILE File = LxFdGet(Call->Process, (LONG)Call->Args[2]);
        PSTR Path;

        if (File == NULL)
            return -LX_EBADF;
        Path = ((File->Path != NULL) && (File->Path[0] == '/')) ? LxStrDup(File->Path) : NULL;
        LxFileDereference(File);
        if (Path == NULL)
            return -LX_EINVAL;
        Error = LxMountMove(LxProcessNs(Call->Process, LxNsMnt), Mount, Path);
        LxFree(Path);
        return Error;
    }

    Error = LxMountLookup(Call->Process, (LONG)Call->Args[2], Call->Args[3], LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;
    Error = LxMountMove(LxProcessNs(Call->Process, LxNsMnt), Mount, Target.Path);
    LxNodeFree(&Target);
    return Error;
}

LX_SYSCALL(LxSysMountSetattr)
{
    struct
    {
        ULONG64 Set;
        ULONG64 Clear;
        ULONG64 Propagation;
        ULONG64 UserNamespace;
    } Attributes;
    ULONG Flags = (ULONG)Call->Args[2];
    PLX_MOUNT Mount;
    LONG64 Result;
    LONG Error;

    if ((LONG)Call->Args[0] < 0 && (LONG)Call->Args[0] != LX_AT_FDCWD)
        return -LX_EBADF;
    if (Call->Args[4] < sizeof(Attributes))
        return -LX_EINVAL;
    Result = LxCopyFromUser(&Attributes, Call->Args[3], sizeof(Attributes));
    if (Result != 0)
        return Result;
    Mount = LxMountFromFd(Call->Process, (LONG)Call->Args[0], Call->Args[1], (Flags & LX_AT_EMPTY_PATH) != 0, &Error);
    if (Mount == NULL)
        return Error;
    if (Attributes.Propagation != 0)
    {
        Error = LxMountSetPropagation(Mount->Ns, Mount,
                                      (ULONG)Attributes.Propagation | ((Flags & LX_AT_RECURSIVE) ? LX_MS_REC : 0));
        if (Error != 0)
            return Error;
    }
    LxMountSetFlags(Mount, LxMountAttrToFlags(Attributes.Set), LxMountAttrToFlags(Attributes.Clear));
    return 0;
}

LX_SYSCALL(LxSysOpenTree)
{
    ULONG Flags = (ULONG)Call->Args[2];
    LX_NODE Node;
    PLX_MOUNT Mount;
    LONG Error;
    CHAR First = 0;

    if ((Flags & LX_OPEN_TREE_CLONE) && !LxMayMount(Call->Process))
        return -LX_EPERM;
    if (!(Flags & LX_OPEN_TREE_CLONE))
    {
        PLX_FILE File;
        PSTR Path;
        LONG64 Result = LxCopyPath(Call->Args[1], &Path);

        if (Result != 0)
            return Result;
        if ((Path[0] == '\0') && (Flags & LX_AT_EMPTY_PATH))
        {
            LxFree(Path);
            File = LxFdGet(Call->Process, (LONG)Call->Args[0]);
            if (File == NULL)
                return -LX_EBADF;
        }
        else
        {
            Error = LxVfsOpen(Call->Process, (LONG)Call->Args[0], Path,
                              LX_O_PATH | ((Flags & LX_AT_SYMLINK_NOFOLLOW) ? LX_O_NOFOLLOW : 0), 0, &File);
            LxFree(Path);
            if (Error != 0)
                return Error;
        }
        Result = LxFdInstall(Call->Process, File, 0, (Flags & LX_O_CLOEXEC) != 0);
        LxFileDereference(File);
        return Result;
    }

    if ((Flags & LX_AT_EMPTY_PATH) && (Call->Args[1] != 0) && (LxCopyFromUser(&First, Call->Args[1], 1) == 0) &&
        (First == '\0'))
    {
        PLX_FILE File = LxFdGet(Call->Process, (LONG)Call->Args[0]);

        if (File == NULL)
            return -LX_EBADF;
        if ((File->Path == NULL) || (File->Path[0] != '/'))
        {
            LxFileDereference(File);
            return -LX_EINVAL;
        }
        Error = LxLookup(Call->Process, LX_AT_FDCWD, File->Path, LX_LOOKUP_FOLLOW, &Node);
        LxFileDereference(File);
    }
    else
    {
        Error = LxMountLookup(Call->Process, (LONG)Call->Args[0], Call->Args[1], LX_LOOKUP_FOLLOW, &Node);
    }
    if (Error != 0)
        return Error;

    Error = LxMountBindDetached(Call->Process, &Node, &Mount);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;
    return LxInstallMountRoot(Call->Process, Mount, (Flags & LX_O_CLOEXEC) != 0);
}
