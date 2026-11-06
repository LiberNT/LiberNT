/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Loop block devices and /dev/loop-control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_LOOP_SET_FD              0x4C00
#define LX_LOOP_CLR_FD              0x4C01
#define LX_LOOP_SET_STATUS          0x4C02
#define LX_LOOP_GET_STATUS          0x4C03
#define LX_LOOP_SET_STATUS64        0x4C04
#define LX_LOOP_GET_STATUS64        0x4C05
#define LX_LOOP_CHANGE_FD           0x4C06
#define LX_LOOP_SET_CAPACITY        0x4C07
#define LX_LOOP_SET_DIRECT_IO       0x4C08
#define LX_LOOP_SET_BLOCK_SIZE      0x4C09
#define LX_LOOP_CONFIGURE           0x4C0A
#define LX_LOOP_CTL_ADD             0x4C80
#define LX_LOOP_CTL_REMOVE          0x4C81
#define LX_LOOP_CTL_GET_FREE        0x4C82

#define LX_BLKROSET                 0x125D
#define LX_BLKROGET                 0x125E
#define LX_BLKRRPART                0x125F
#define LX_BLKGETSIZE               0x1260
#define LX_BLKFLSBUF                0x1261
#define LX_BLKSSZGET                0x1268
#define LX_BLKBSZGET                0x80081270
#define LX_BLKGETSIZE64             0x80081272
#define LX_BLKIOMIN                 0x1278
#define LX_BLKIOOPT                 0x1279
#define LX_BLKALIGNOFF              0x127A
#define LX_BLKPBSZGET               0x127B
#define LX_BLKDISCARDZEROES         0x127C
#define LX_BLKROTATIONAL            0x127E

#define LX_LO_FLAGS_READ_ONLY       0x00000001
#define LX_LO_FLAGS_AUTOCLEAR       0x00000004
#define LX_LO_FLAGS_PARTSCAN        0x00000008
#define LX_LO_FLAGS_DIRECT_IO       0x00000010
#define LX_LO_SETTABLE_FLAGS        (LX_LO_FLAGS_AUTOCLEAR | LX_LO_FLAGS_PARTSCAN)
#define LX_LO_CLEARABLE_FLAGS       LX_LO_FLAGS_AUTOCLEAR
#define LX_LO_CONFIGURE_FLAGS       (LX_LO_FLAGS_READ_ONLY | LX_LO_FLAGS_AUTOCLEAR | LX_LO_FLAGS_PARTSCAN | \
                                     LX_LO_FLAGS_DIRECT_IO)
#define LX_LO_NAME_SIZE             64
#define LX_LO_KEY_SIZE              32
#define LX_LOOP_MAX_MINOR           (1UL << 20)

#pragma pack(push, 1)
typedef struct _LX_LOOP_INFO64
{
    ULONG64 Device;
    ULONG64 Inode;
    ULONG64 Rdevice;
    ULONG64 Offset;
    ULONG64 SizeLimit;
    ULONG Number;
    ULONG EncryptType;
    ULONG EncryptKeySize;
    ULONG Flags;
    UCHAR FileName[LX_LO_NAME_SIZE];
    UCHAR CryptName[LX_LO_NAME_SIZE];
    UCHAR EncryptKey[LX_LO_KEY_SIZE];
    ULONG64 Init[2];
} LX_LOOP_INFO64, *PLX_LOOP_INFO64;

typedef struct _LX_LOOP_CONFIG
{
    ULONG Fd;
    ULONG BlockSize;
    LX_LOOP_INFO64 Info;
    ULONG64 Reserved[8];
} LX_LOOP_CONFIG, *PLX_LOOP_CONFIG;
#pragma pack(pop)

C_ASSERT(sizeof(LX_LOOP_INFO64) == 232);
C_ASSERT(sizeof(LX_LOOP_CONFIG) == 304);

struct _LX_LOOP
{
    LIST_ENTRY Link;
    volatile LONG RefCount;
    PLX_INSTANCE Instance;
    ULONG Number;
    KEVENT Lock;
    PLX_FILE Backing;
    ULONG Opens;
    ULONG Flags;
    ULONG64 Offset;
    ULONG64 SizeLimit;
    ULONG BlockSize;
    UCHAR FileName[LX_LO_NAME_SIZE];
    UCHAR CryptName[LX_LO_NAME_SIZE];
    ULONG64 Init[2];
};

static const LX_FILE_OPS LxLoopOps;
static const LX_FILE_OPS LxLoopControlOps;

static
VOID
LxLoopLock(
    _In_ PLX_LOOP Loop)
{
    KeWaitForSingleObject(&Loop->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxLoopUnlock(
    _In_ PLX_LOOP Loop)
{
    KeSetEvent(&Loop->Lock, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxLoopReference(
    _In_ PLX_LOOP Loop)
{
    InterlockedIncrement(&Loop->RefCount);
}

static
VOID
LxLoopDereference(
    _In_ PLX_LOOP Loop)
{
    if (InterlockedDecrement(&Loop->RefCount) != 0)
        return;
    if (Loop->Backing != NULL)
        LxFileDereference(Loop->Backing);
    LxFree(Loop);
}

static
PLX_LOOP
LxLoopFindLocked(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number)
{
    PLIST_ENTRY Entry;

    for (Entry = Instance->Loops.Flink; Entry != &Instance->Loops; Entry = Entry->Flink)
    {
        PLX_LOOP Loop = CONTAINING_RECORD(Entry, LX_LOOP, Link);

        if (Loop->Number == Number)
            return Loop;
        if (Loop->Number > Number)
            break;
    }
    return NULL;
}

static
VOID
LxLoopInsertLocked(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_LOOP Loop)
{
    PLIST_ENTRY Entry;

    for (Entry = Instance->Loops.Flink; Entry != &Instance->Loops; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_LOOP, Link)->Number > Loop->Number)
            break;
    }
    InsertTailList(Entry, &Loop->Link);
}

static
PLX_LOOP
LxLoopAllocate(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number)
{
    PLX_LOOP Loop = LxAlloc(sizeof(*Loop));

    if (Loop == NULL)
        return NULL;
    Loop->RefCount = 1;
    Loop->Instance = Instance;
    Loop->Number = Number;
    Loop->BlockSize = 512;
    KeInitializeEvent(&Loop->Lock, SynchronizationEvent, TRUE);
    return Loop;
}

static
VOID
LxLoopCreateNode(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number)
{
    PLX_MOUNT Dev;
    PCSTR Relative;
    CHAR Name[24];

    if (Number < LX_LOOP_PRESET_COUNT)
        return;
    Dev = LxMountFind(Instance->RootNs[LxNsMnt], "/dev", &Relative);
    if ((Dev == NULL) || (Dev->Kind != LxFsTmpfs) || (Relative[0] != '\0'))
        return;
    RtlStringCbPrintfA(Name, sizeof(Name), "loop%lu", Number);
    LxTmpfsCreateNode(Dev, Name, LX_S_IFBLK | 0660, 0, 0, ((ULONG64)LX_LOOP_MAJOR << 32) | Number, NULL, NULL);
}

static
VOID
LxLoopRemoveNode(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number)
{
    PLX_MOUNT Dev;
    PCSTR Relative;
    CHAR Name[24];

    Dev = LxMountFind(Instance->RootNs[LxNsMnt], "/dev", &Relative);
    if ((Dev == NULL) || (Dev->Kind != LxFsTmpfs) || (Relative[0] != '\0'))
        return;
    RtlStringCbPrintfA(Name, sizeof(Name), "loop%lu", Number);
    LxTmpfsUnlink(Dev, Name, FALSE);
}

static
PLX_LOOP
LxLoopGet(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number,
    _In_ BOOLEAN Create)
{
    PLX_LOOP Loop, Fresh = NULL;

    if (Number >= LX_LOOP_MAX_MINOR)
        return NULL;
    for (;;)
    {
        ExAcquireFastMutex(&Instance->Lock);
        Loop = LxLoopFindLocked(Instance, Number);
        if ((Loop == NULL) && (Fresh != NULL))
        {
            LxLoopInsertLocked(Instance, Fresh);
            Loop = Fresh;
            Fresh = NULL;
        }
        if (Loop != NULL)
            LxLoopReference(Loop);
        ExReleaseFastMutex(&Instance->Lock);
        if (Fresh != NULL)
            LxLoopDereference(Fresh);
        if ((Loop != NULL) || !Create)
            return Loop;
        Fresh = LxLoopAllocate(Instance, Number);
        if (Fresh == NULL)
            return NULL;
    }
}

static
ULONG64
LxLoopCapacityLocked(
    _In_ PLX_LOOP Loop)
{
    LX_STAT Stat;
    ULONG64 Size;

    if ((Loop->Backing == NULL) || (LxFileStat(Loop->Backing, &Stat) != 0) || (Stat.Size < 0))
        return 0;
    Size = (ULONG64)Stat.Size;
    if (Size <= Loop->Offset)
        return 0;
    Size -= Loop->Offset;
    if ((Loop->SizeLimit != 0) && (Loop->SizeLimit < Size))
        Size = Loop->SizeLimit;
    return Size & ~511ULL;
}

static
PLX_FILE
LxLoopBacking(
    _In_ PLX_LOOP Loop,
    _Out_ PULONG64 Base,
    _Out_ PULONG64 Capacity,
    _Out_opt_ PULONG Flags)
{
    PLX_FILE Backing;

    LxLoopLock(Loop);
    Backing = Loop->Backing;
    if (Backing != NULL)
        LxFileReference(Backing);
    *Base = Loop->Offset;
    *Capacity = LxLoopCapacityLocked(Loop);
    if (Flags != NULL)
        *Flags = Loop->Flags;
    LxLoopUnlock(Loop);
    return Backing;
}

static
VOID
LxLoopDetachLocked(
    _In_ PLX_LOOP Loop,
    _Out_ PLX_FILE *Released)
{
    *Released = Loop->Backing;
    Loop->Backing = NULL;
    Loop->Flags = 0;
    Loop->Offset = 0;
    Loop->SizeLimit = 0;
    Loop->BlockSize = 512;
    RtlZeroMemory(Loop->FileName, sizeof(Loop->FileName));
    RtlZeroMemory(Loop->CryptName, sizeof(Loop->CryptName));
    RtlZeroMemory(Loop->Init, sizeof(Loop->Init));
}

static
LONG
LxLoopValidBlockSize(
    _In_ ULONG BlockSize)
{
    if ((BlockSize < 512) || (BlockSize > LX_PAGE_SIZE) || ((BlockSize & (BlockSize - 1)) != 0))
        return -LX_EINVAL;
    return 0;
}

static
VOID
LxLoopApplyInfoLocked(
    _In_ PLX_LOOP Loop,
    _In_ PLX_LOOP_INFO64 Info)
{
    Loop->Offset = Info->Offset;
    Loop->SizeLimit = Info->SizeLimit;
    RtlCopyMemory(Loop->FileName, Info->FileName, sizeof(Loop->FileName));
    RtlCopyMemory(Loop->CryptName, Info->CryptName, sizeof(Loop->CryptName));
    Loop->FileName[LX_LO_NAME_SIZE - 1] = 0;
    Loop->CryptName[LX_LO_NAME_SIZE - 1] = 0;
    RtlCopyMemory(Loop->Init, Info->Init, sizeof(Loop->Init));
}

static
LONG
LxLoopValidInfo(
    _In_ PLX_LOOP_INFO64 Info)
{
    if ((Info->EncryptType != 0) || (Info->EncryptKeySize > LX_LO_KEY_SIZE))
        return -LX_EINVAL;
    return 0;
}

static
LONG
LxLoopConfigure(
    _In_ PLX_FILE File,
    _In_ LONG Fd,
    _In_ ULONG BlockSize,
    _In_opt_ PLX_LOOP_INFO64 Info)
{
    PLX_LOOP Loop = File->Object;
    PLX_THREAD Thread = LxCurrentThread();
    PLX_FILE Backing;
    LX_STAT Stat;
    LONG Error;

    if ((BlockSize != 0) && (LxLoopValidBlockSize(BlockSize) != 0))
        return -LX_EINVAL;
    if ((Info != NULL) && (LxLoopValidInfo(Info) != 0))
        return -LX_EINVAL;
    if (Thread == NULL)
        return -LX_EBADF;
    Backing = LxFdGet(Thread->Process, Fd);
    if (Backing == NULL)
        return -LX_EBADF;
    Error = LxFileStat(Backing, &Stat);
    /* TODO: loop devices backed by block devices */
    if ((Error == 0) && ((Stat.Mode & LX_S_IFMT) != LX_S_IFREG))
        Error = -LX_EINVAL;
    if ((Error == 0) && (Backing->Ops->Read == NULL))
        Error = -LX_EINVAL;
    if (Error != 0)
    {
        LxFileDereference(Backing);
        return Error;
    }

    LxLoopLock(Loop);
    if (Loop->Backing != NULL)
    {
        LxLoopUnlock(Loop);
        LxFileDereference(Backing);
        return -LX_EBUSY;
    }
    Loop->Flags = (Info != NULL) ? (Info->Flags & LX_LO_CONFIGURE_FLAGS) : 0;
    if (((File->Flags & LX_O_ACCMODE) == LX_O_RDONLY) || ((Backing->Flags & LX_O_ACCMODE) == LX_O_RDONLY) ||
        (Backing->Ops->Write == NULL))
    {
        Loop->Flags |= LX_LO_FLAGS_READ_ONLY;
    }
    if (Info != NULL)
        LxLoopApplyInfoLocked(Loop, Info);
    Loop->BlockSize = (BlockSize != 0) ? BlockSize : 512;
    Loop->Backing = Backing;
    LxLoopUnlock(Loop);
    return 0;
}

static
LONG
LxLoopClear(
    _In_ PLX_LOOP Loop)
{
    PLX_FILE Released = NULL;

    LxLoopLock(Loop);
    if (Loop->Backing == NULL)
    {
        LxLoopUnlock(Loop);
        return -LX_ENXIO;
    }
    if (Loop->Opens > 1)
        Loop->Flags |= LX_LO_FLAGS_AUTOCLEAR;
    else
        LxLoopDetachLocked(Loop, &Released);
    LxLoopUnlock(Loop);
    if (Released != NULL)
        LxFileDereference(Released);
    return 0;
}

static
LONG
LxLoopGetStatus(
    _In_ PLX_LOOP Loop,
    _In_ ULONG64 Argument)
{
    LX_LOOP_INFO64 Info;
    LX_STAT Stat;
    PLX_FILE Backing;
    ULONG64 Base, Capacity;

    RtlZeroMemory(&Info, sizeof(Info));
    Backing = LxLoopBacking(Loop, &Base, &Capacity, NULL);
    if (Backing == NULL)
        return -LX_ENXIO;
    if (LxFileStat(Backing, &Stat) == 0)
    {
        Info.Device = Stat.Dev;
        Info.Inode = Stat.Ino;
        Info.Rdevice = Stat.Rdev;
    }
    LxFileDereference(Backing);

    LxLoopLock(Loop);
    Info.Offset = Loop->Offset;
    Info.SizeLimit = Loop->SizeLimit;
    Info.Number = Loop->Number;
    Info.Flags = Loop->Flags;
    RtlCopyMemory(Info.FileName, Loop->FileName, sizeof(Info.FileName));
    RtlCopyMemory(Info.CryptName, Loop->CryptName, sizeof(Info.CryptName));
    RtlCopyMemory(Info.Init, Loop->Init, sizeof(Info.Init));
    LxLoopUnlock(Loop);
    return (LONG)LxCopyToUser(Argument, &Info, sizeof(Info));
}

static
LONG
LxLoopSetStatus(
    _In_ PLX_LOOP Loop,
    _In_ ULONG64 Argument)
{
    LX_LOOP_INFO64 Info;
    LONG Error = (LONG)LxCopyFromUser(&Info, Argument, sizeof(Info));

    if (Error != 0)
        return Error;
    if (LxLoopValidInfo(&Info) != 0)
        return -LX_EINVAL;
    LxLoopLock(Loop);
    if (Loop->Backing == NULL)
    {
        LxLoopUnlock(Loop);
        return -LX_ENXIO;
    }
    LxLoopApplyInfoLocked(Loop, &Info);
    Loop->Flags = (Info.Flags & LX_LO_SETTABLE_FLAGS) | (Loop->Flags & ~LX_LO_SETTABLE_FLAGS) |
                  (Loop->Flags & ~LX_LO_CLEARABLE_FLAGS);
    LxLoopUnlock(Loop);
    return 0;
}

static
LONG
LxLoopChangeFd(
    _In_ PLX_LOOP Loop,
    _In_ LONG Fd)
{
    PLX_THREAD Thread = LxCurrentThread();
    PLX_FILE Backing, Old;
    LX_STAT New, Current;
    LONG Error = 0;

    if (Thread == NULL)
        return -LX_EBADF;
    Backing = LxFdGet(Thread->Process, Fd);
    if (Backing == NULL)
        return -LX_EBADF;
    if ((LxFileStat(Backing, &New) != 0) || ((New.Mode & LX_S_IFMT) != LX_S_IFREG) || (Backing->Ops->Read == NULL))
    {
        LxFileDereference(Backing);
        return -LX_EINVAL;
    }
    LxLoopLock(Loop);
    Old = Loop->Backing;
    if (Old == NULL)
        Error = -LX_ENXIO;
    else if (!(Loop->Flags & LX_LO_FLAGS_READ_ONLY))
        Error = -LX_EINVAL;
    else if ((LxFileStat(Old, &Current) != 0) || (Current.Size != New.Size))
        Error = -LX_EINVAL;
    if (Error == 0)
        Loop->Backing = Backing;
    LxLoopUnlock(Loop);
    LxFileDereference((Error == 0) ? Old : Backing);
    return Error;
}

static
LONG
LxLoopSetBound(
    _In_ PLX_LOOP Loop,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    LONG Error = 0;

    if ((Request == LX_LOOP_SET_BLOCK_SIZE) && (LxLoopValidBlockSize((ULONG)Argument) != 0))
        return -LX_EINVAL;
    LxLoopLock(Loop);
    if (Loop->Backing == NULL)
    {
        Error = -LX_ENXIO;
    }
    else if (Request == LX_LOOP_SET_BLOCK_SIZE)
    {
        Loop->BlockSize = (ULONG)Argument;
    }
    else if (Request == LX_LOOP_SET_DIRECT_IO)
    {
        if (Argument != 0)
            Loop->Flags |= LX_LO_FLAGS_DIRECT_IO;
        else
            Loop->Flags &= ~LX_LO_FLAGS_DIRECT_IO;
    }
    LxLoopUnlock(Loop);
    return Error;
}

static
LONG
LxLoopPutInt(
    _In_ ULONG64 Argument,
    _In_ ULONG Value)
{
    return (LONG)LxCopyToUser(Argument, &Value, sizeof(Value));
}

static
LONG64
LxLoopIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_LOOP Loop = File->Object;
    LX_LOOP_CONFIG Config;
    PLX_FILE Backing;
    ULONG64 Base, Capacity, Value;
    ULONG Flags;
    LONG Error;

    switch (Request)
    {
        case LX_LOOP_SET_FD:
            return LxLoopConfigure(File, (LONG)Argument, 0, NULL);

        case LX_LOOP_CONFIGURE:
            Error = (LONG)LxCopyFromUser(&Config, Argument, sizeof(Config));
            if (Error != 0)
                return Error;
            return LxLoopConfigure(File, (LONG)Config.Fd, Config.BlockSize, &Config.Info);

        case LX_LOOP_CLR_FD:
            return LxLoopClear(Loop);

        case LX_LOOP_SET_STATUS64:
            return LxLoopSetStatus(Loop, Argument);

        case LX_LOOP_GET_STATUS64:
            return LxLoopGetStatus(Loop, Argument);

        case LX_LOOP_SET_STATUS:
        case LX_LOOP_GET_STATUS:
            /* TODO: the legacy struct loop_info status requests */
            return -LX_EINVAL;

        case LX_LOOP_CHANGE_FD:
            return LxLoopChangeFd(Loop, (LONG)Argument);

        case LX_LOOP_SET_CAPACITY:
        case LX_LOOP_SET_DIRECT_IO:
        case LX_LOOP_SET_BLOCK_SIZE:
            return LxLoopSetBound(Loop, Request, Argument);

        case LX_BLKGETSIZE64:
        case LX_BLKGETSIZE:
        case LX_BLKROGET:
            Backing = LxLoopBacking(Loop, &Base, &Capacity, &Flags);
            if (Backing != NULL)
                LxFileDereference(Backing);
            if (Request == LX_BLKROGET)
                return LxLoopPutInt(Argument, (Flags & LX_LO_FLAGS_READ_ONLY) ? 1 : 0);
            Value = (Request == LX_BLKGETSIZE64) ? Capacity : Capacity >> 9;
            return (LONG)LxCopyToUser(Argument, &Value, sizeof(Value));

        case LX_BLKSSZGET:
        case LX_BLKBSZGET:
        case LX_BLKPBSZGET:
        case LX_BLKIOMIN:
            LxLoopLock(Loop);
            Flags = Loop->BlockSize;
            LxLoopUnlock(Loop);
            return LxLoopPutInt(Argument, Flags);

        case LX_BLKIOOPT:
        case LX_BLKALIGNOFF:
        case LX_BLKDISCARDZEROES:
            return LxLoopPutInt(Argument, 0);

        case LX_BLKROTATIONAL:
        {
            USHORT Rotational = 0;

            return (LONG)LxCopyToUser(Argument, &Rotational, sizeof(Rotational));
        }

        case LX_BLKFLSBUF:
        case LX_BLKRRPART:
            return 0;

        case LX_BLKROSET:
            return -LX_EACCES;

        default:
            return -LX_ENOTTY;
    }
}

static
LONG64
LxLoopRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_LOOP Loop = File->Object;
    ULONG64 Base, Capacity;
    PLX_FILE Backing = LxLoopBacking(Loop, &Base, &Capacity, NULL);
    LONG64 Position, Inner, Result = 0;

    if (Offset == NULL)
        KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Position = (Offset != NULL) ? *Offset : File->Position;
    if ((Backing != NULL) && (Position >= 0) && ((ULONG64)Position < Capacity) && (Length != 0))
    {
        Inner = (LONG64)(Base + (ULONG64)Position);
        Result = Backing->Ops->Read(Backing, Buffer, min(Length, Capacity - (ULONG64)Position), &Inner);
        if (Result > 0)
        {
            if (Offset != NULL)
                *Offset = Position + Result;
            else
                File->Position = Position + Result;
        }
    }
    if (Offset == NULL)
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    if (Backing != NULL)
        LxFileDereference(Backing);
    return Result;
}

static
LONG64
LxLoopWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_LOOP Loop = File->Object;
    ULONG64 Base, Capacity;
    ULONG Flags = 0;
    PLX_FILE Backing = LxLoopBacking(Loop, &Base, &Capacity, &Flags);
    LONG64 Position, Inner, Result;

    if (Offset == NULL)
        KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Position = (Offset != NULL) ? *Offset : File->Position;
    if ((Backing == NULL) || (Position < 0))
        Result = -LX_ENXIO;
    else if (Flags & LX_LO_FLAGS_READ_ONLY)
        Result = -LX_EPERM;
    else if ((ULONG64)Position >= Capacity)
        Result = (Length != 0) ? -LX_ENOSPC : 0;
    else
    {
        Inner = (LONG64)(Base + (ULONG64)Position);
        Result = Backing->Ops->Write(Backing, Buffer, min(Length, Capacity - (ULONG64)Position), &Inner);
    }
    if (Result > 0)
    {
        if (Offset != NULL)
            *Offset = Position + Result;
        else
            File->Position = Position + Result;
    }
    if (Offset == NULL)
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    if (Backing != NULL)
        LxFileDereference(Backing);
    return Result;
}

static
LONG
LxLoopStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_LOOP Loop = File->Object;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFBLK | 0660;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->Rdev = ((ULONG64)LX_LOOP_MAJOR << 8) | (Loop->Number & 0xFF) | ((ULONG64)(Loop->Number & 0xFFF00) << 12);
    Stat->BlkSize = 4096;
    return 0;
}

static
LONG64
LxLoopSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    PLX_LOOP Loop = File->Object;
    ULONG64 Base, Capacity;
    PLX_FILE Backing = LxLoopBacking(Loop, &Base, &Capacity, NULL);
    LONG64 Origin, Result;

    if (Backing != NULL)
        LxFileDereference(Backing);
    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Origin = (Whence == LX_SEEK_SET) ? 0 : (Whence == LX_SEEK_CUR) ? File->Position :
             (Whence == LX_SEEK_END) ? (LONG64)Capacity : -1;
    if ((Origin < 0) || (Origin + Offset < 0))
        Result = -LX_EINVAL;
    else
        Result = File->Position = Origin + Offset;
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    return Result;
}

static
VOID
LxLoopRelease(
    _In_ PLX_FILE File)
{
    PLX_LOOP Loop = File->Object;
    PLX_FILE Released = NULL;

    if (Loop == NULL)
        return;
    File->Object = NULL;
    LxLoopLock(Loop);
    if ((--Loop->Opens == 0) && (Loop->Backing != NULL) && (Loop->Flags & LX_LO_FLAGS_AUTOCLEAR))
        LxLoopDetachLocked(Loop, &Released);
    LxLoopUnlock(Loop);
    if (Released != NULL)
        LxFileDereference(Released);
    LxLoopDereference(Loop);
}

static const LX_FILE_OPS LxLoopOps =
{
    LxLoopRead, LxLoopWrite, LxLoopIoctl, NULL, LxLoopStat, LxLoopSeek, NULL, NULL, LxLoopRelease
};

PLX_FILE
LxLoopOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Minor,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    PLX_LOOP Loop = LxLoopGet(Process->Instance, Minor, TRUE);
    PLX_FILE File;

    if (Loop == NULL)
        return NULL;
    File = LxFileAlloc(&LxLoopOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFBLK);
    if (File == NULL)
    {
        LxLoopDereference(Loop);
        return NULL;
    }
    LxLoopLock(Loop);
    Loop->Opens++;
    LxLoopUnlock(Loop);
    File->Object = Loop;
    File->Rdev = ((ULONG64)LX_LOOP_MAJOR << 8) | Minor;
    File->Path = LxStrDup(Path);
    return File;
}

BOOLEAN
LxLoopIsDevice(
    _In_ PLX_FILE File)
{
    return File->Ops == &LxLoopOps;
}

LONG64
LxLoopReadKernel(
    _In_ PLX_FILE Device,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ ULONG64 Offset)
{
    ULONG64 Base, Capacity;
    PLX_FILE Backing = LxLoopBacking(Device->Object, &Base, &Capacity, NULL);
    LONG64 Result = 0;

    if (Backing == NULL)
        return -LX_ENXIO;
    if (Offset < Capacity)
        Result = LxFileReadKernel(Backing, Buffer, (ULONG)min((ULONG64)Length, Capacity - Offset), Base + Offset);
    LxFileDereference(Backing);
    return Result;
}

ULONG64
LxLoopCapacity(
    _In_ PLX_FILE Device)
{
    ULONG64 Base, Capacity;
    PLX_FILE Backing = LxLoopBacking(Device->Object, &Base, &Capacity, NULL);

    if (Backing != NULL)
        LxFileDereference(Backing);
    return Capacity;
}

VOID
LxLoopEnumerate(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_LOOP_ENUM_ROUTINE Routine,
    _In_ PVOID Context)
{
    ULONG Number = 0;

    for (;;)
    {
        PLX_LOOP Loop = NULL;
        PLIST_ENTRY Entry;
        ULONG64 Base, Capacity;
        PLX_FILE Backing;

        ExAcquireFastMutex(&Instance->Lock);
        for (Entry = Instance->Loops.Flink; Entry != &Instance->Loops; Entry = Entry->Flink)
        {
            PLX_LOOP Candidate = CONTAINING_RECORD(Entry, LX_LOOP, Link);

            if (Candidate->Number >= Number)
            {
                Loop = Candidate;
                LxLoopReference(Loop);
                break;
            }
        }
        ExReleaseFastMutex(&Instance->Lock);
        if (Loop == NULL)
            return;
        Backing = LxLoopBacking(Loop, &Base, &Capacity, NULL);
        if (Backing != NULL)
        {
            Routine(Context, Loop->Number, Capacity);
            LxFileDereference(Backing);
        }
        Number = Loop->Number + 1;
        LxLoopDereference(Loop);
    }
}

static
LONG64
LxLoopControlAdd(
    _In_ PLX_INSTANCE Instance,
    _In_ LONG Requested)
{
    PLX_LOOP Loop, Fresh;
    ULONG Number = 0;
    PLIST_ENTRY Entry;

    if (Requested >= (LONG)LX_LOOP_MAX_MINOR)
        return -LX_EINVAL;
    Fresh = LxLoopAllocate(Instance, 0);
    if (Fresh == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&Instance->Lock);
    if (Requested >= 0)
    {
        Number = (ULONG)Requested;
        Loop = LxLoopFindLocked(Instance, Number);
    }
    else
    {
        Loop = NULL;
        for (Entry = Instance->Loops.Flink; Entry != &Instance->Loops; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, LX_LOOP, Link)->Number != Number)
                break;
            Number++;
        }
    }
    if (Loop == NULL)
    {
        Fresh->Number = Number;
        LxLoopInsertLocked(Instance, Fresh);
        Fresh = NULL;
    }
    ExReleaseFastMutex(&Instance->Lock);
    if (Fresh != NULL)
    {
        LxLoopDereference(Fresh);
        return -LX_EEXIST;
    }
    LxLoopCreateNode(Instance, Number);
    return Number;
}

static
LONG64
LxLoopControlRemove(
    _In_ PLX_INSTANCE Instance,
    _In_ LONG Requested)
{
    PLX_LOOP Loop;
    LONG64 Result = 0;

    if (Requested < 0)
        return -LX_EINVAL;
    ExAcquireFastMutex(&Instance->Lock);
    Loop = LxLoopFindLocked(Instance, (ULONG)Requested);
    if (Loop == NULL)
        Result = -LX_ENODEV;
    else if ((Loop->Backing != NULL) || (Loop->Opens != 0))
        Result = -LX_EBUSY;
    else
        RemoveEntryList(&Loop->Link);
    ExReleaseFastMutex(&Instance->Lock);
    if (Result != 0)
        return Result;
    LxLoopRemoveNode(Instance, (ULONG)Requested);
    LxLoopDereference(Loop);
    return 0;
}

static
LONG64
LxLoopControlGetFree(
    _In_ PLX_INSTANCE Instance)
{
    ULONG Number;

    for (Number = 0; Number < LX_LOOP_MAX_MINOR; Number++)
    {
        PLX_LOOP Loop = LxLoopGet(Instance, Number, TRUE);
        BOOLEAN Free;

        if (Loop == NULL)
            return -LX_ENOMEM;
        LxLoopLock(Loop);
        Free = (Loop->Backing == NULL);
        LxLoopUnlock(Loop);
        LxLoopDereference(Loop);
        if (Free)
        {
            LxLoopCreateNode(Instance, Number);
            return Number;
        }
    }
    return -LX_ENOSPC;
}

static
LONG64
LxLoopControlIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_INSTANCE Instance = File->Object;

    switch (Request)
    {
        case LX_LOOP_CTL_ADD:
            return LxLoopControlAdd(Instance, (LONG)Argument);
        case LX_LOOP_CTL_REMOVE:
            return LxLoopControlRemove(Instance, (LONG)Argument);
        case LX_LOOP_CTL_GET_FREE:
            return LxLoopControlGetFree(Instance);
        default:
            return -LX_ENOTTY;
    }
}

static
LONG
LxLoopControlStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFCHR | 0600;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->Rdev = (10 << 8) | LX_LOOP_CONTROL_MINOR;
    Stat->BlkSize = 4096;
    return 0;
}

static const LX_FILE_OPS LxLoopControlOps =
{
    NULL, NULL, LxLoopControlIoctl, NULL, LxLoopControlStat, NULL, NULL, NULL, NULL
};

PLX_FILE
LxLoopControlOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Flags,
    _In_ PCSTR Path)
{
    PLX_FILE File = LxFileAlloc(&LxLoopControlOps, Flags & (LX_O_ACCMODE | LX_O_NONBLOCK), LX_S_IFCHR);

    if (File == NULL)
        return NULL;
    File->Object = Process->Instance;
    File->Rdev = (10 << 8) | LX_LOOP_CONTROL_MINOR;
    File->Path = LxStrDup(Path);
    return File;
}

BOOLEAN
LxLoopQuery(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Number,
    _Out_ PLX_LOOP_STATE State)
{
    PLX_LOOP Loop = LxLoopGet(Instance, Number, FALSE);

    RtlZeroMemory(State, sizeof(*State));
    State->BlockSize = 512;
    if (Loop == NULL)
        return Number < LX_LOOP_PRESET_COUNT;
    LxLoopLock(Loop);
    State->Bound = (Loop->Backing != NULL);
    State->Flags = Loop->Flags;
    State->BlockSize = Loop->BlockSize;
    State->Offset = Loop->Offset;
    State->SizeLimit = Loop->SizeLimit;
    State->Capacity = LxLoopCapacityLocked(Loop);
    if ((Loop->Backing != NULL) && (Loop->Backing->Path != NULL))
        State->BackingPath = LxStrDup(Loop->Backing->Path);
    LxLoopUnlock(Loop);
    LxLoopDereference(Loop);
    return TRUE;
}

VOID
LxLoopList(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_LOOP_ENUM_ROUTINE Routine,
    _In_ PVOID Context)
{
    ULONG Number = 0;

    for (;;)
    {
        PLIST_ENTRY Entry;
        ULONG Next = MAXULONG;

        ExAcquireFastMutex(&Instance->Lock);
        for (Entry = Instance->Loops.Flink; Entry != &Instance->Loops; Entry = Entry->Flink)
        {
            PLX_LOOP Loop = CONTAINING_RECORD(Entry, LX_LOOP, Link);

            if (Loop->Number >= Number)
            {
                Next = Loop->Number;
                break;
            }
        }
        ExReleaseFastMutex(&Instance->Lock);
        if ((Number < LX_LOOP_PRESET_COUNT) && (Next > Number))
            Next = Number;
        if (Next == MAXULONG)
            return;
        Routine(Context, Next, 0);
        Number = Next + 1;
    }
}

VOID
LxLoopFreeAll(
    _In_ PLX_INSTANCE Instance)
{
    while (!IsListEmpty(&Instance->Loops))
    {
        PLX_LOOP Loop = CONTAINING_RECORD(RemoveHeadList(&Instance->Loops), LX_LOOP, Link);

        LxLoopDereference(Loop);
    }
}
