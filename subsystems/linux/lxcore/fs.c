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

    Error = LxVfsOpenResolve(Process, DirFd, Path, Flags, Mode, Resolve, &File);
    LxFree(Path);
    if (Error != 0)
        return Error;

    Tty = LxFileGetTty(File);
    if ((Tty != NULL) && !(Flags & LX_O_NOCTTY) && (Process->Sid == Process->Pid) && (Process->Tty == NULL))
    {
        LxTtyReference(Tty);
        Process->Tty = Tty;
        Tty->Session = Process->Sid;
        Tty->ForegroundPgrp = Process->Pgid;
        if (Tty->Instance == NULL)
            Tty->Instance = Process->Instance;
    }

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
        case LX_F_SETSIG:
        case LX_F_SETLEASE:
        case LX_F_NOTIFY:
            Result = 0;
            break;

        case LX_F_GETOWN:
        case LX_F_GETSIG:
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
            Result = 0;
            break;

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
            ExAcquireFastMutex(&Process->Lock);
            Path = LxStrDup(Process->Cwd);
            ExReleaseFastMutex(&Process->Lock);
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
            PLX_MOUNT Mount = LxMountFind(File->Mount->Instance, File->Path, &Relative);

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
LxAccessNode(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Mode)
{
    LX_STAT Stat;
    LONG Error;

    if (Mode & ~7UL)
        return -LX_EINVAL;
    if ((Mode & 2) && (Node->Mount->Flags & LX_MS_RDONLY))
        return -LX_EROFS;
    if (Mode & 1)
    {
        Error = LxStatNodeFollow(Process, Node, &Stat);
        if (Error != 0)
            return Error;
        if (((Stat.Mode & LX_S_IFMT) != LX_S_IFDIR) && !(Stat.Mode & 0111))
            return -LX_EACCES;
    }
    return 0;
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
            else if (Mode & 1)
            {
                Error = LxFileStat(File, &Stat);
                if ((Error == 0) && ((Stat.Mode & LX_S_IFMT) != LX_S_IFDIR) && !(Stat.Mode & 0111))
                    Error = -LX_EACCES;
            }
            LxFileDereference(File);
            return Error;
        }
    }
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1],
                         (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    if (Error != 0)
        return Error;
    Error = LxAccessNode(Call->Process, &Node, Mode);
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
    SIZE_T Length;
    LONG64 Result;
    PSTR Cwd;

    ExAcquireFastMutex(&Process->Lock);
    Cwd = LxStrDup(Process->Cwd);
    ExReleaseFastMutex(&Process->Lock);
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
    PSTR Copy = LxStrDup(Path), Old;

    if (Copy == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&Process->Lock);
    Old = Process->Cwd;
    Process->Cwd = Copy;
    ExReleaseFastMutex(&Process->Lock);
    LxFree(Old);
    return 0;
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
        Result = -LX_ENOTDIR;
    else
        Result = LxChangeDirectory(Call->Process, File->Path);
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
    Error = (strcmp(Node.Path, "/") == 0) ? 0 : -LX_EPERM;
    LxNodeFree(&Node);
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
        LxVcacheInvalidatePath(File->Mount->Instance, File->Path);
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
            LxVcacheInvalidatePath(File->Mount->Instance, File->Path);
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

LX_SYSCALL(LxSysFchown)
{
    PLX_FILE File = LxCallFile(Call, 0);
    LONG Error;

    if (File == NULL)
        return -LX_EBADF;
    Error = LxSetAttributesFile(Call->Process, File, MAXULONG, (ULONG)Call->Args[1], (ULONG)Call->Args[2]);
    LxFileDereference(File);
    return Error;
}

LX_SYSCALL(LxSysFchownat)
{
    return LxSetAttributesAt(Call, (LONG)Call->Args[0], Call->Args[1], (ULONG)Call->Args[4],
                             MAXULONG, (ULONG)Call->Args[2], (ULONG)Call->Args[3]);
}

LX_SYSCALL(LxSysReadlinkat)
{
    LX_NODE Node;
    PSTR Target;
    SIZE_T Length;
    LONG64 Result;
    LONG Error;

    if ((LONG64)Call->Args[3] <= 0)
        return -LX_EINVAL;
    Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], 0, &Node);
    if (Error != 0)
        return Error;
    Error = LxVfsReadlink(Call->Process, &Node, &Target);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;
    Length = min(strlen(Target), Call->Args[3]);
    Result = LxCopyToUser(Call->Args[2], Target, Length);
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
        File = LxCallFile(Call, 0);
        if (File == NULL)
            return -LX_EBADF;
        Error = LxVfsSetTimes(NULL, File, Times);
        LxFileDereference(File);
        return (Error == -LX_EACCES) ? 0 : Error;
    }

    Error = LxLookup(Call->Process, (LONG)Call->Args[0], Path, (Flags & LX_AT_SYMLINK_NOFOLLOW) ? 0 : LX_LOOKUP_FOLLOW, &Node);
    LxFree(Path);
    if (Error != 0)
        return Error;
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

LX_SYSCALL(LxSysMount)
{
    PLX_PROCESS Process = Call->Process;
    ULONG Flags = (ULONG)Call->Args[3];
    PSTR Source = NULL, Type = NULL;
    LX_NODE Target;
    LONG64 Result;
    LONG Error;

    Error = LxLookupUser(Process, LX_AT_FDCWD, Call->Args[1], LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;

    if (Flags & LX_MS_REMOUNT)
    {
        PCSTR Relative;
        PLX_MOUNT Mount = LxMountFind(Process->Instance, Target.Path, &Relative);

        if ((Mount == NULL) || (Relative[0] != '\0'))
            Error = (Flags & LX_MS_BIND) ? 0 : -LX_EINVAL;
        else
            LxMountSetFlags(Process->Instance, Mount, Flags & (LX_MS_RDONLY | LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC),
                            LX_MS_RDONLY | LX_MS_NOSUID | LX_MS_NODEV | LX_MS_NOEXEC);
        LxNodeFree(&Target);
        return Error;
    }
    if (Flags & (LX_MS_PRIVATE | LX_MS_SLAVE | LX_MS_SHARED | LX_MS_UNBINDABLE))
    {
        LxNodeFree(&Target);
        return 0;
    }
    if (Flags & LX_MS_BIND)
    {
        LX_NODE Source;

        Error = LxLookupUser(Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Source);
        if (Error == 0)
        {
            Error = LxMountBind(Process->Instance, &Source, Target.Path, Flags);
            LxNodeFree(&Source);
        }
        LxNodeFree(&Target);
        return Error;
    }
    if (Flags & LX_MS_MOVE)
    {
        LX_NODE Source;

        Error = LxLookupUser(Process, LX_AT_FDCWD, Call->Args[0], LX_LOOKUP_FOLLOW, &Source);
        if (Error == 0)
        {
            Error = ((Source.Mount == NULL) || (Source.Relative[0] != '\0')) ? -LX_EINVAL :
                    LxMountMove(Process->Instance, Source.Mount, Target.Path);
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
        Result = LxMountAdd(Process->Instance, Target.Path, (Source != NULL) ? Source : "none", Type, Flags, NULL);

    LxFree(Source);
    LxFree(Type);
    LxNodeFree(&Target);
    return Result;
}

LX_SYSCALL(LxSysUmount2)
{
    LX_NODE Target;
    LONG Error;

    Error = LxLookupUser(Call->Process, LX_AT_FDCWD, Call->Args[0], (Call->Args[1] & 8) ? 0 : LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;
    Error = LxMountRemove(Call->Process->Instance, Target.Path);
    LxNodeFree(&Target);
    return Error;
}

LX_SYSCALL(LxSysXattrGet)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_ENODATA;
}

LX_SYSCALL(LxSysXattrList)
{
    UNREFERENCED_PARAMETER(Call);
    return 0;
}

LX_SYSCALL(LxSysXattrSet)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_EOPNOTSUPP;
}

LX_SYSCALL(LxSysNameToHandleAt)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_EOPNOTSUPP;
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
    Target = LxStrDup(Mount->Target);
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
            Mount = LxMountFind(Process->Instance, File->Path, &Relative);
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

        *Error = LxLookupUser(Process, Fd, UserPath, LX_LOOKUP_FOLLOW, &Node);
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

    Error = LxMountCreateDetached(Call->Process->Instance, Context->FsType, Context->Source,
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
        Error = LxMountMove(Call->Process->Instance, Mount, Path);
        LxFree(Path);
        return Error;
    }

    Error = LxLookupUser(Call->Process, (LONG)Call->Args[2], Call->Args[3], LX_LOOKUP_FOLLOW, &Target);
    if (Error != 0)
        return Error;
    Error = LxMountMove(Call->Process->Instance, Mount, Target.Path);
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
    LxMountSetFlags(Call->Process->Instance, Mount, LxMountAttrToFlags(Attributes.Set), LxMountAttrToFlags(Attributes.Clear));
    return 0;
}

LX_SYSCALL(LxSysOpenTree)
{
    ULONG Flags = (ULONG)Call->Args[2];
    LX_NODE Node;
    PLX_MOUNT Mount;
    LONG Error;
    CHAR First = 0;

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
        Error = LxLookupUser(Call->Process, (LONG)Call->Args[0], Call->Args[1], LX_LOOKUP_FOLLOW, &Node);
    }
    if (Error != 0)
        return Error;

    Error = LxMountBindDetached(Call->Process->Instance, &Node, &Mount);
    LxNodeFree(&Node);
    if (Error != 0)
        return Error;
    return LxInstallMountRoot(Call->Process, Mount, (Flags & LX_O_CLOEXEC) != 0);
}
