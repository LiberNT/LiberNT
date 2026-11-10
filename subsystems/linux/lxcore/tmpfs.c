/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory backed tmpfs, ramfs and devtmpfs file systems
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_TMPFS_DIRENT_SIZE    20
#define LX_TMPFS_NAME_MAX       255
#define LX_TMPFS_LINK_MAX       0xFFFFFFFE
#define LX_TMPFS_MAX_SIZE       0x7FFFFFFFFFFFF000LL

typedef struct _LX_TMPFS_STORE
{
    volatile LONG RefCount;
    PLX_TMPFS Fs;
    PVOID Section;
    PUCHAR View;
    ULONG64 Capacity;
} LX_TMPFS_STORE, *PLX_TMPFS_STORE;

typedef struct _LX_TMPFS_DIRENT
{
    LIST_ENTRY Link;
    PLX_TMPFS_INODE Inode;
    ULONG64 Cookie;
    ULONG NameLength;
    CHAR Name[ANYSIZE_ARRAY];
} LX_TMPFS_DIRENT, *PLX_TMPFS_DIRENT;

typedef struct _LX_TMPFS_RESIZE
{
    PLX_TMPFS_STORE Old;
    ULONG Count;
    ULONG Capacity;
    PLX_PROCESS *Visited;
} LX_TMPFS_RESIZE, *PLX_TMPFS_RESIZE;

struct _LX_TMPFS_INODE
{
    volatile LONG RefCount;
    PLX_TMPFS Fs;
    ULONG64 Ino;
    ULONG Mode;
    ULONG Uid;
    ULONG Gid;
    ULONG Nlink;
    ULONG64 Device;
    LX_TIMESPEC Atime;
    LX_TIMESPEC Mtime;
    LX_TIMESPEC Ctime;
    PSTR LinkTarget;
    PUCHAR Xattr;
    ULONG XattrLength;
    LIST_ENTRY Entries;
    ULONG EntryCount;
    ULONG64 NextCookie;
    PLX_TMPFS_INODE Parent;
    KEVENT DataLock;
    FAST_MUTEX StoreLock;
    KEVENT ResizeDone;
    PLX_TMPFS_STORE Store;
    PLX_TMPFS_RESIZE Resize;
    ULONG64 Size;
    BOOLEAN Mapped;
    ULONG64 SquashInode;
    volatile LONG SquashPending;
    PVOID Private;
    PLX_TMPFS_PRIVATE_RELEASE PrivateRelease;
};

struct _LX_TMPFS
{
    volatile LONG RefCount;
    volatile LONG Mounts;
    ERESOURCE Lock;
    PLX_TMPFS_INODE Root;
    volatile LONG64 NextIno;
    volatile LONG64 Used;
    ULONG64 Limit;
    volatile LONG Inodes;
    LONG InodeLimit;
    PLX_SQUASHFS Squash;
    volatile LONG SquashMounted;
};

static PLX_TMPFS LxTmpfsShm;
static LONG64 LxTmpfsResident;

static const LX_FILE_OPS LxTmpfsFileOps;

static
VOID
LxTmpfsNow(
    _Out_ PLX_TIMESPEC Time)
{
    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, Time);
}

static
VOID
LxTmpfsLockShared(
    _In_ PLX_TMPFS Fs)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Fs->Lock, TRUE);
}

static
VOID
LxTmpfsLockExclusive(
    _In_ PLX_TMPFS Fs)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Fs->Lock, TRUE);
}

static
VOID
LxTmpfsUnlock(
    _In_ PLX_TMPFS Fs)
{
    ExReleaseResourceLite(&Fs->Lock);
    KeLeaveCriticalRegion();
}

static
VOID
LxTmpfsDataLock(
    _In_ PLX_TMPFS_INODE Inode)
{
    KeWaitForSingleObject(&Inode->DataLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxTmpfsDataUnlock(
    _In_ PLX_TMPFS_INODE Inode)
{
    KeSetEvent(&Inode->DataLock, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
LxTmpfsIsDirectory(
    _In_ PLX_TMPFS_INODE Inode)
{
    return (Inode->Mode & LX_S_IFMT) == LX_S_IFDIR;
}

static
VOID
LxTmpfsStoreDereference(
    _In_opt_ PLX_TMPFS_STORE Store)
{
    if ((Store == NULL) || (InterlockedDecrement(&Store->RefCount) != 0))
        return;
    if (Store->View != NULL)
        MmUnmapViewInSystemSpace(Store->View);
    ObDereferenceObject(Store->Section);
    InterlockedExchangeAdd64(&Store->Fs->Used, -(LONG64)Store->Capacity);
    InterlockedExchangeAdd64(&LxTmpfsResident, -(LONG64)Store->Capacity);
    LxFree(Store);
}

static
LONG
LxTmpfsStoreCreate(
    _In_ PLX_TMPFS Fs,
    _In_ ULONG64 Capacity,
    _Out_ PLX_TMPFS_STORE *Result)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER Size;
    PLX_TMPFS_STORE Store;
    SIZE_T ViewSize = (SIZE_T)Capacity;
    HANDLE Handle;
    NTSTATUS Status;

    *Result = NULL;
    if ((ULONG64)InterlockedExchangeAdd64(&Fs->Used, (LONG64)Capacity) + Capacity > Fs->Limit)
    {
        InterlockedExchangeAdd64(&Fs->Used, -(LONG64)Capacity);
        return -LX_ENOSPC;
    }
    Store = LxAlloc(sizeof(*Store));
    if (Store == NULL)
    {
        InterlockedExchangeAdd64(&Fs->Used, -(LONG64)Capacity);
        return -LX_ENOMEM;
    }

    Size.QuadPart = (LONG64)Capacity;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&Handle, SECTION_ALL_ACCESS, &ObjectAttributes, &Size, PAGE_EXECUTE_READWRITE, SEC_COMMIT,
                             NULL);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Handle, SECTION_ALL_ACCESS, NULL, KernelMode, &Store->Section, NULL);
        ZwClose(Handle);
    }
    if (NT_SUCCESS(Status))
    {
        Status = MmMapViewInSystemSpace(Store->Section, (PVOID *)&Store->View, &ViewSize);
        if (!NT_SUCCESS(Status))
            ObDereferenceObject(Store->Section);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux tmpfs cannot back %I64u bytes of memory: 0x%08lx\n", Capacity, Status);
        LxFree(Store);
        InterlockedExchangeAdd64(&Fs->Used, -(LONG64)Capacity);
        return -LX_ENOSPC;
    }
    Store->RefCount = 1;
    Store->Fs = Fs;
    Store->Capacity = Capacity;
    InterlockedExchangeAdd64(&LxTmpfsResident, (LONG64)Capacity);
    *Result = Store;
    return 0;
}

static
NTSTATUS
LxTmpfsCopyMemory(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        if (Source != NULL)
            RtlCopyMemory(Destination, Source, Length);
        else
            RtlZeroMemory(Destination, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

VOID
LxTmpfsReference(
    _In_ PLX_TMPFS Fs)
{
    InterlockedIncrement(&Fs->RefCount);
}

static
VOID
LxTmpfsInodeFree(
    _In_ PLX_TMPFS_INODE Inode)
{
    while (!IsListEmpty(&Inode->Entries))
    {
        PLX_TMPFS_DIRENT Entry = CONTAINING_RECORD(RemoveHeadList(&Inode->Entries), LX_TMPFS_DIRENT, Link);

        Entry->Inode->Parent = NULL;
        LxTmpfsInodeDereference(Entry->Inode);
        LxFree(Entry);
    }
    LxTmpfsStoreDereference(Inode->Store);
    LxFree(Inode->LinkTarget);
    LxFree(Inode->Xattr);
    if (Inode->PrivateRelease != NULL)
        Inode->PrivateRelease(Inode->Private);
    InterlockedDecrement(&Inode->Fs->Inodes);
    LxFree(Inode);
}

VOID
LxTmpfsDereference(
    _In_ PLX_TMPFS Fs)
{
    if (InterlockedDecrement(&Fs->RefCount) != 0)
        return;
    LxTmpfsInodeFree(Fs->Root);
    LxSquashfsClose(Fs->Squash);
    ExDeleteResourceLite(&Fs->Lock);
    LxFree(Fs);
}

VOID
LxTmpfsInodeReference(
    _In_ PLX_TMPFS_INODE Inode)
{
    InterlockedIncrement(&Inode->RefCount);
}

VOID
LxTmpfsInodeDereference(
    _In_ PLX_TMPFS_INODE Inode)
{
    PLX_TMPFS Fs = Inode->Fs;

    if (InterlockedDecrement(&Inode->RefCount) != 0)
        return;
    LxTmpfsInodeFree(Inode);
    LxTmpfsDereference(Fs);
}

static
LONG
LxTmpfsInodeCreate(
    _In_ PLX_TMPFS Fs,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG64 Device,
    _In_opt_ PCSTR LinkTarget,
    _Out_ PLX_TMPFS_INODE *Result)
{
    PLX_TMPFS_INODE Inode;

    *Result = NULL;
    if (InterlockedIncrement(&Fs->Inodes) > Fs->InodeLimit)
    {
        InterlockedDecrement(&Fs->Inodes);
        return -LX_ENOSPC;
    }
    Inode = LxAlloc(sizeof(*Inode));
    if ((Inode != NULL) && (LinkTarget != NULL))
    {
        Inode->LinkTarget = LxStrDup(LinkTarget);
        if (Inode->LinkTarget == NULL)
        {
            LxFree(Inode);
            Inode = NULL;
        }
    }
    if (Inode == NULL)
    {
        InterlockedDecrement(&Fs->Inodes);
        return -LX_ENOMEM;
    }

    Inode->RefCount = 1;
    Inode->Fs = Fs;
    Inode->Ino = (ULONG64)InterlockedIncrement64(&Fs->NextIno);
    Inode->Mode = Mode;
    Inode->Uid = Uid;
    Inode->Gid = Gid;
    Inode->Nlink = ((Mode & LX_S_IFMT) == LX_S_IFDIR) ? 2 : 1;
    Inode->Device = Device;
    LxTmpfsNow(&Inode->Mtime);
    Inode->Atime = Inode->Ctime = Inode->Mtime;
    InitializeListHead(&Inode->Entries);
    Inode->NextCookie = 2;
    KeInitializeEvent(&Inode->DataLock, SynchronizationEvent, TRUE);
    ExInitializeFastMutex(&Inode->StoreLock);
    KeInitializeEvent(&Inode->ResizeDone, NotificationEvent, TRUE);
    LxTmpfsReference(Fs);
    *Result = Inode;
    return 0;
}

static
PLX_TMPFS
LxTmpfsAllocate(
    _In_ ULONG Mode)
{
    LX_MEMORY_STATE State;
    PLX_TMPFS Fs = LxAlloc(sizeof(*Fs));

    if (Fs == NULL)
        return NULL;
    LxQueryMemoryState(&State);
    Fs->RefCount = 1;
    ExInitializeResourceLite(&Fs->Lock);
    Fs->Limit = State.Total / 2;
    Fs->InodeLimit = (LONG)min(State.Total / 2 / LX_PAGE_SIZE, (ULONG64)MAXLONG);
    if (LxTmpfsInodeCreate(Fs, LX_S_IFDIR | (Mode & 07777), 0, 0, 0, NULL, &Fs->Root) != 0)
    {
        ExDeleteResourceLite(&Fs->Lock);
        LxFree(Fs);
        return NULL;
    }
    InterlockedDecrement(&Fs->RefCount);
    return Fs;
}

PLX_TMPFS
LxTmpfsCreate(
    _In_ ULONG Mode)
{
    PLX_TMPFS Fs = LxTmpfsAllocate(Mode);

    if (Fs != NULL)
        Fs->Mounts = 1;
    return Fs;
}

static
BOOLEAN
LxTmpfsMemparse(
    _Inout_ PCSTR *Cursor,
    _Out_ PULONG64 Value)
{
    PCSTR Text = *Cursor;
    ULONG64 Number = 0;
    ULONG Base = 10, Shift = 0, Digit;
    BOOLEAN Any = FALSE;

    if ((Text[0] == '0') && ((Text[1] == 'x') || (Text[1] == 'X')))
    {
        Base = 16;
        Text += 2;
    }
    else if (Text[0] == '0')
    {
        Base = 8;
    }
    for (;; Text++)
    {
        if ((*Text >= '0') && (*Text <= '9'))
            Digit = *Text - '0';
        else if ((*Text >= 'a') && (*Text <= 'f'))
            Digit = *Text - 'a' + 10;
        else if ((*Text >= 'A') && (*Text <= 'F'))
            Digit = *Text - 'A' + 10;
        else
            break;
        if (Digit >= Base)
            break;
        Number = Number * Base + Digit;
        Any = TRUE;
    }
    if (!Any)
        return FALSE;
    switch (*Text | 0x20)
    {
        case 'k': Shift = 10; break;
        case 'm': Shift = 20; break;
        case 'g': Shift = 30; break;
        case 't': Shift = 40; break;
        case 'p': Shift = 50; break;
        case 'e': Shift = 60; break;
        default: break;
    }
    if (Shift != 0)
        Text++;
    *Value = Number << Shift;
    *Cursor = Text;
    return TRUE;
}

static
BOOLEAN
LxTmpfsOptionIs(
    _In_reads_(Length) PCSTR Key,
    _In_ SIZE_T Length,
    _In_z_ PCSTR Name)
{
    return (strlen(Name) == Length) && (RtlCompareMemory(Key, Name, Length) == Length);
}

LONG
LxTmpfsParseOptions(
    _In_ PLX_PROCESS Process,
    _In_opt_ PCSTR Data,
    _Inout_ PLX_TMPFS_OPTIONS Options)
{
    static const PCSTR SuperblockFlags[] =
    {
        "dirsync", "nodirsync", "lazytime", "nolazytime", "mand", "nomand", "ro", "rw", "sync", "async",
    };
    LX_MEMORY_STATE State;
    PCSTR Cursor = Data;

    while ((Cursor != NULL) && (*Cursor != '\0'))
    {
        PCSTR Key = Cursor, Value = NULL, End;
        SIZE_T KeyLength;
        ULONG64 Number;
        ULONG Index, Id;

        End = strchr(Cursor, ',');
        if (End == NULL)
            End = Cursor + strlen(Cursor);
        Cursor = (*End == ',') ? End + 1 : End;
        if (End == Key)
            continue;
        for (KeyLength = 0; (Key + KeyLength < End) && (Key[KeyLength] != '='); KeyLength++)
        {
        }
        if (Key + KeyLength < End)
            Value = Key + KeyLength + 1;

        for (Index = 0; Index < RTL_NUMBER_OF(SuperblockFlags); Index++)
        {
            if ((Value == NULL) && LxTmpfsOptionIs(Key, KeyLength, SuperblockFlags[Index]))
                break;
        }
        if (Index < RTL_NUMBER_OF(SuperblockFlags))
            continue;

        if (LxTmpfsOptionIs(Key, KeyLength, "inode32") || LxTmpfsOptionIs(Key, KeyLength, "inode64"))
        {
            if (Value != NULL)
                return -LX_EINVAL;
            continue;
        }
        if (LxTmpfsOptionIs(Key, KeyLength, "noswap"))
        {
            if ((Value != NULL) || (LxCredUserNs(Process)->Parent != NULL) || !LxCapable(Process, LX_CAP_SYS_ADMIN))
                return -LX_EINVAL;
            continue;
        }
        if (LxTmpfsOptionIs(Key, KeyLength, "huge"))
        {
            if ((Value == NULL) || (End - Value != 5) || (RtlCompareMemory(Value, "never", 5) != 5))
                return -LX_EINVAL;
            continue;
        }
        if (Value == NULL)
            return -LX_EINVAL;
        if (LxTmpfsOptionIs(Key, KeyLength, "size"))
        {
            PCSTR Rest = Value;

            if (!LxTmpfsMemparse(&Rest, &Number))
                return -LX_EINVAL;
            if (*Rest == '%')
            {
                LxQueryMemoryState(&State);
                Number = (State.Total / 100) * Number + ((State.Total % 100) * Number) / 100;
                Rest++;
            }
            if (Rest != End)
                return -LX_EINVAL;
            Options->Blocks = (Number + LX_PAGE_SIZE - 1) / LX_PAGE_SIZE;
            Options->SeenBlocks = TRUE;
        }
        else if (LxTmpfsOptionIs(Key, KeyLength, "nr_blocks") || LxTmpfsOptionIs(Key, KeyLength, "nr_inodes"))
        {
            PCSTR Rest = Value;

            if (!LxTmpfsMemparse(&Rest, &Number) || (Rest != End) || (Number > MAXLONG64))
                return -LX_EINVAL;
            if (Key[3] == 'b')
            {
                Options->Blocks = Number;
                Options->SeenBlocks = TRUE;
            }
            else
            {
                Options->Inodes = Number;
                Options->SeenInodes = TRUE;
            }
        }
        else if (LxTmpfsOptionIs(Key, KeyLength, "mode"))
        {
            ULONG Mode = 0;
            PCSTR Digit;

            for (Digit = Value; (Digit < End) && (*Digit >= '0') && (*Digit <= '7'); Digit++)
                Mode = (Mode << 3) | (ULONG)(*Digit - '0');
            if ((Digit == Value) || (Digit != End) || (End - Value > 11))
                return -LX_EINVAL;
            Options->Mode = Mode & 07777;
        }
        else if (LxTmpfsOptionIs(Key, KeyLength, "uid") || LxTmpfsOptionIs(Key, KeyLength, "gid"))
        {
            PCSTR Rest = Value;

            Number = 0;
            for (; (Rest < End) && (*Rest >= '0') && (*Rest <= '9'); Rest++)
                Number = Number * 10 + (ULONG)(*Rest - '0');
            if ((Rest == Value) || (Rest != End) || (Number >= MAXULONG))
                return -LX_EINVAL;
            Id = LxIdToKernel(LxCredUserNs(Process), Key[0] == 'g', (ULONG)Number);
            if (Id == MAXULONG)
                return -LX_EINVAL;
            if (Key[0] == 'g')
                Options->Gid = Id;
            else
                Options->Uid = Id;
        }
        else
        {
            return -LX_EINVAL;
        }
    }
    return 0;
}

VOID
LxTmpfsApplyOptions(
    _In_ PLX_TMPFS Fs,
    _In_ PLX_TMPFS_OPTIONS Options)
{
    Fs->Root->Mode = LX_S_IFDIR | Options->Mode;
    Fs->Root->Uid = Options->Uid;
    Fs->Root->Gid = Options->Gid;
    if (Options->SeenBlocks)
        Fs->Limit = ((Options->Blocks == 0) || (Options->Blocks > MAXULONG64 / LX_PAGE_SIZE)) ?
                    MAXULONG64 : Options->Blocks * LX_PAGE_SIZE;
    if (Options->SeenInodes)
        Fs->InodeLimit = ((Options->Inodes == 0) || (Options->Inodes > MAXLONG)) ? MAXLONG : (LONG)Options->Inodes;
}

VOID
LxTmpfsInitialize(VOID)
{
    LxTmpfsShm = LxTmpfsAllocate(01777);
}

static
PLX_TMPFS_DIRENT
LxTmpfsDirentAllocate(
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length);

static
BOOLEAN
LxTmpfsSquashPending(
    _In_ PLX_TMPFS_INODE Inode)
{
    return InterlockedCompareExchange(&Inode->SquashPending, 0, 0) != 0;
}

static
VOID
LxTmpfsSquashApply(
    _Inout_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Reference,
    _In_ PLX_SQUASHFS_INODE Info)
{
    ULONG Type = Info->Mode & LX_S_IFMT;

    Inode->Mode = Info->Mode;
    Inode->Uid = Info->Uid;
    Inode->Gid = Info->Gid;
    Inode->Ino = Info->Number;
    Inode->Nlink = Info->Nlink;
    Inode->Device = Info->Device;
    Inode->Size = (Type == LX_S_IFREG) ? Info->Size : 0;
    Inode->Mtime.Seconds = Info->Mtime;
    Inode->Mtime.Nanoseconds = 0;
    Inode->Atime = Inode->Ctime = Inode->Mtime;
    Inode->SquashInode = Reference;
    Inode->SquashPending = ((Type == LX_S_IFDIR) || ((Type == LX_S_IFREG) && (Info->Size != 0))) ? 1 : 0;
}

static
LONG
LxTmpfsSquashAdd(
    _In_ PVOID Context,
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length,
    _In_ ULONG64 Reference)
{
    PLX_TMPFS_INODE Directory = Context, Inode;
    LX_SQUASHFS_INODE Info;
    PLX_TMPFS_DIRENT Entry;
    PSTR Target = NULL;
    PUCHAR Xattr = NULL;
    ULONG XattrLength = 0;
    LONG Error;

    Error = LxSquashfsReadInode(Directory->Fs->Squash, Reference, &Info);
    if ((Error == 0) && ((Info.Mode & LX_S_IFMT) == LX_S_IFLNK))
        Error = LxSquashfsReadLink(Directory->Fs->Squash, &Info, &Target);
    if (Error == 0)
        Error = LxSquashfsReadXattrs(Directory->Fs->Squash, &Info, &Xattr, &XattrLength);
    if (Error == 0)
        Error = LxTmpfsInodeCreate(Directory->Fs, Info.Mode, Info.Uid, Info.Gid, Info.Device, Target, &Inode);
    LxFree(Target);
    if (Error != 0)
    {
        LxFree(Xattr);
        return Error;
    }
    LxTmpfsSquashApply(Inode, Reference, &Info);
    Inode->Xattr = Xattr;
    Inode->XattrLength = XattrLength;
    Entry = LxTmpfsDirentAllocate(Name, Length);
    if (Entry == NULL)
    {
        LxTmpfsInodeDereference(Inode);
        return -LX_ENOMEM;
    }
    Entry->Inode = Inode;
    Entry->Cookie = Directory->NextCookie++;
    InsertTailList(&Directory->Entries, &Entry->Link);
    Directory->EntryCount++;
    if (LxTmpfsIsDirectory(Inode))
        Inode->Parent = Directory;
    return 0;
}

static
LONG
LxTmpfsSquashFill(
    _In_ PLX_TMPFS_INODE Directory)
{
    LONG Error = 0;

    if (!LxTmpfsSquashPending(Directory) || !LxTmpfsIsDirectory(Directory))
        return 0;
    LxTmpfsDataLock(Directory);
    if (LxTmpfsSquashPending(Directory))
    {
        Error = LxSquashfsListDirectory(Directory->Fs->Squash, Directory->SquashInode, LxTmpfsSquashAdd, Directory);
        if (Error == 0)
        {
            InterlockedExchange(&Directory->SquashPending, 0);
        }
        else
        {
            while (!IsListEmpty(&Directory->Entries))
            {
                PLX_TMPFS_DIRENT Entry = CONTAINING_RECORD(RemoveHeadList(&Directory->Entries), LX_TMPFS_DIRENT,
                                                           Link);

                Entry->Inode->Parent = NULL;
                LxTmpfsInodeDereference(Entry->Inode);
                LxFree(Entry);
            }
            Directory->EntryCount = 0;
            Directory->NextCookie = 2;
        }
    }
    LxTmpfsDataUnlock(Directory);
    return Error;
}

static
LONG
LxTmpfsSquashLoad(
    _In_ PLX_TMPFS_INODE Inode)
{
    PLX_TMPFS_STORE Store = NULL;
    LONG Error = 0;

    if (!LxTmpfsSquashPending(Inode) || LxTmpfsIsDirectory(Inode))
        return 0;
    LxTmpfsDataLock(Inode);
    if (LxTmpfsSquashPending(Inode))
    {
        Error = LxTmpfsStoreCreate(Inode->Fs, LX_ALIGN_UP(Inode->Size, LX_PAGE_SIZE), &Store);
        if (Error == 0)
            Error = LxSquashfsReadFile(Inode->Fs->Squash, Inode->SquashInode, Store->View, Store->Capacity);
        if (Error == 0)
        {
            ExAcquireFastMutex(&Inode->StoreLock);
            Inode->Store = Store;
            ExReleaseFastMutex(&Inode->StoreLock);
            InterlockedExchange(&Inode->SquashPending, 0);
            Store = NULL;
        }
        LxTmpfsStoreDereference(Store);
    }
    LxTmpfsDataUnlock(Inode);
    return Error;
}

LONG
LxTmpfsCreateSquashfs(
    _In_ PLX_SQUASHFS Squash,
    _Out_ PLX_TMPFS *Result)
{
    LX_SQUASHFS_INODE Info;
    ULONG64 Root = LxSquashfsRoot(Squash);
    PLX_TMPFS Fs;
    PUCHAR Xattr = NULL;
    ULONG XattrLength = 0;
    LONG Error;

    *Result = NULL;
    Error = LxSquashfsReadInode(Squash, Root, &Info);
    if (Error != 0)
        return Error;
    if ((Info.Mode & LX_S_IFMT) != LX_S_IFDIR)
        return -LX_EINVAL;
    Error = LxSquashfsReadXattrs(Squash, &Info, &Xattr, &XattrLength);
    if (Error != 0)
        return Error;
    Fs = LxTmpfsCreate(Info.Mode & 07777);
    if (Fs == NULL)
    {
        LxFree(Xattr);
        return -LX_ENOMEM;
    }
    Fs->Limit = MAXULONG64;
    Fs->InodeLimit = MAXLONG;
    LxTmpfsSquashApply(Fs->Root, Root, &Info);
    Fs->Root->Xattr = Xattr;
    Fs->Root->XattrLength = XattrLength;
    Fs->Squash = Squash;
    Fs->SquashMounted = 1;
    *Result = Fs;
    return 0;
}

static
PLX_TMPFS_DIRENT
LxTmpfsFind(
    _In_ PLX_TMPFS_INODE Directory,
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length)
{
    PLIST_ENTRY Link;

    for (Link = Directory->Entries.Flink; Link != &Directory->Entries; Link = Link->Flink)
    {
        PLX_TMPFS_DIRENT Entry = CONTAINING_RECORD(Link, LX_TMPFS_DIRENT, Link);

        if ((Entry->NameLength == Length) && (RtlCompareMemory(Entry->Name, Name, Length) == Length))
            return Entry;
    }
    return NULL;
}

static
LONG
LxTmpfsWalk(
    _In_ PLX_TMPFS_INODE Start,
    _In_ PCSTR Path,
    _In_ PCSTR End,
    _Out_ PLX_TMPFS_INODE *Result)
{
    PLX_TMPFS_INODE Inode = Start;

    while (Path < End)
    {
        PCSTR Component;
        ULONG Length;
        PLX_TMPFS_DIRENT Entry;
        LONG Error;

        while ((Path < End) && (*Path == '/'))
            Path++;
        if (Path >= End)
            break;
        Component = Path;
        while ((Path < End) && (*Path != '/'))
            Path++;
        Length = (ULONG)(Path - Component);
        if (!LxTmpfsIsDirectory(Inode))
            return -LX_ENOTDIR;
        if ((Length == 1) && (Component[0] == '.'))
            continue;
        if ((Length == 2) && (Component[0] == '.') && (Component[1] == '.'))
        {
            if (Inode->Parent != NULL)
                Inode = Inode->Parent;
            continue;
        }
        Error = LxTmpfsSquashFill(Inode);
        if (Error != 0)
            return Error;
        Entry = LxTmpfsFind(Inode, Component, Length);
        if (Entry == NULL)
            return -LX_ENOENT;
        Inode = Entry->Inode;
    }
    *Result = Inode;
    return 0;
}

static
LONG
LxTmpfsMountRoot(
    _In_ PLX_MOUNT Mount,
    _Out_ PLX_TMPFS_INODE *Root)
{
    PCSTR Path = (Mount->Root != NULL) ? Mount->Root : "/";

    *Root = NULL;
    if (Mount->Tmpfs == NULL)
        return -LX_ENOENT;
    return LxTmpfsWalk(Mount->Tmpfs->Root, Path, Path + strlen(Path), Root);
}

static
LONG
LxTmpfsResolve(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PLX_TMPFS_INODE *Inode)
{
    PLX_TMPFS_INODE Root;
    LONG Error = LxTmpfsMountRoot(Mount, &Root);

    if (Error != 0)
        return Error;
    return LxTmpfsWalk(Root, Relative, Relative + strlen(Relative), Inode);
}

static
LONG
LxTmpfsSplit(
    _In_ PCSTR Relative,
    _Out_ PCSTR *Name,
    _Out_ PULONG NameLength)
{
    SIZE_T Length = strlen(Relative);
    PCSTR Last;

    while ((Length > 0) && (Relative[Length - 1] == '/'))
        Length--;
    if (Length == 0)
        return -LX_EBUSY;
    Last = Relative + Length;
    while ((Last > Relative) && (Last[-1] != '/'))
        Last--;
    *Name = Last;
    *NameLength = (ULONG)(Relative + Length - Last);
    if (*NameLength > LX_TMPFS_NAME_MAX)
        return -LX_ENAMETOOLONG;
    if (((*NameLength == 1) && (Last[0] == '.')) || ((*NameLength == 2) && (Last[0] == '.') && (Last[1] == '.')))
        return -LX_EINVAL;
    return 0;
}

static
LONG
LxTmpfsResolveParent(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PLX_TMPFS_INODE *Directory,
    _Out_ PCSTR *Name,
    _Out_ PULONG NameLength)
{
    PLX_TMPFS_INODE Root;
    LONG Error;

    *Directory = NULL;
    Error = LxTmpfsSplit(Relative, Name, NameLength);
    if (Error == 0)
        Error = LxTmpfsMountRoot(Mount, &Root);
    if (Error == 0)
        Error = LxTmpfsWalk(Root, Relative, *Name, Directory);
    if ((Error == 0) && !LxTmpfsIsDirectory(*Directory))
        Error = -LX_ENOTDIR;
    return Error;
}

static
PLX_TMPFS_DIRENT
LxTmpfsDirentAllocate(
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length)
{
    PLX_TMPFS_DIRENT Entry = LxAlloc(FIELD_OFFSET(LX_TMPFS_DIRENT, Name) + Length + 1);

    if (Entry != NULL)
    {
        Entry->NameLength = Length;
        RtlCopyMemory(Entry->Name, Name, Length);
    }
    return Entry;
}

static
VOID
LxTmpfsAttach(
    _In_ PLX_TMPFS_INODE Directory,
    _In_ PLX_TMPFS_DIRENT Entry,
    _In_ PLX_TMPFS_INODE Inode,
    _In_ PLX_TIMESPEC Now)
{
    Entry->Inode = Inode;
    Entry->Cookie = Directory->NextCookie++;
    InsertTailList(&Directory->Entries, &Entry->Link);
    Directory->EntryCount++;
    if (LxTmpfsIsDirectory(Inode))
    {
        Directory->Nlink++;
        Inode->Parent = Directory;
    }
    Directory->Mtime = Directory->Ctime = *Now;
}

static
PLX_TMPFS_INODE
LxTmpfsDetach(
    _In_ PLX_TMPFS_INODE Directory,
    _In_ PLX_TMPFS_DIRENT Entry,
    _In_ PLX_TIMESPEC Now)
{
    PLX_TMPFS_INODE Inode = Entry->Inode;

    RemoveEntryList(&Entry->Link);
    Directory->EntryCount--;
    if (LxTmpfsIsDirectory(Inode))
    {
        Directory->Nlink--;
        Inode->Nlink = 0;
        Inode->Parent = NULL;
    }
    else
    {
        Inode->Nlink--;
    }
    Inode->Ctime = *Now;
    Directory->Mtime = Directory->Ctime = *Now;
    LxFree(Entry);
    return Inode;
}

VOID
LxTmpfsPurge(
    _In_ PLX_TMPFS Fs)
{
    PLX_TMPFS_INODE Directory;
    LX_TIMESPEC Now;

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Fs);
    Directory = Fs->Root;
    for (;;)
    {
        PLX_TMPFS_DIRENT Entry;
        PLX_TMPFS_INODE Child;

        if (IsListEmpty(&Directory->Entries))
        {
            if (Directory == Fs->Root)
                break;
            Directory = Directory->Parent;
            continue;
        }
        Entry = CONTAINING_RECORD(Directory->Entries.Flink, LX_TMPFS_DIRENT, Link);
        Child = Entry->Inode;
        if (LxTmpfsIsDirectory(Child) && !IsListEmpty(&Child->Entries))
        {
            Directory = Child;
            continue;
        }
        LxTmpfsInodeDereference(LxTmpfsDetach(Directory, Entry, &Now));
    }
    LxTmpfsUnlock(Fs);
    if ((Fs->Squash != NULL) && (InterlockedExchange(&Fs->SquashMounted, 0) != 0))
        LxSquashfsRemoveUser(Fs->Squash);
}

VOID
LxTmpfsMountReference(
    _In_ PLX_TMPFS Fs)
{
    InterlockedIncrement(&Fs->Mounts);
    LxTmpfsReference(Fs);
}

VOID
LxTmpfsMountDereference(
    _In_ PLX_TMPFS Fs)
{
    if (InterlockedDecrement(&Fs->Mounts) == 0)
        LxTmpfsPurge(Fs);
    LxTmpfsDereference(Fs);
}

LONG
LxTmpfsQuery(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PULONG Type,
    _Out_opt_ PSTR *LinkTarget)
{
    PLX_TMPFS_INODE Inode;
    LONG Error;

    *Type = 0;
    if (LinkTarget != NULL)
        *LinkTarget = NULL;
    if (Mount->Tmpfs == NULL)
        return -LX_ENOENT;
    LxTmpfsLockShared(Mount->Tmpfs);
    Error = LxTmpfsResolve(Mount, Relative, &Inode);
    if (Error == 0)
    {
        *Type = Inode->Mode & LX_S_IFMT;
        if ((*Type == LX_S_IFLNK) && (LinkTarget != NULL))
        {
            *LinkTarget = LxStrDup((Inode->LinkTarget != NULL) ? Inode->LinkTarget : "");
            if (*LinkTarget == NULL)
                Error = -LX_ENOMEM;
        }
    }
    LxTmpfsUnlock(Mount->Tmpfs);
    return Error;
}

LONG
LxTmpfsLookup(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PLX_TMPFS_INODE *Result)
{
    PLX_TMPFS_INODE Inode;
    LONG Error;

    *Result = NULL;
    if (Mount->Tmpfs == NULL)
        return -LX_ENOENT;
    LxTmpfsLockShared(Mount->Tmpfs);
    Error = LxTmpfsResolve(Mount, Relative, &Inode);
    if (Error == 0)
    {
        LxTmpfsInodeReference(Inode);
        *Result = Inode;
    }
    LxTmpfsUnlock(Mount->Tmpfs);
    return Error;
}

LONG
LxTmpfsCreateNode(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG64 Device,
    _In_opt_ PCSTR LinkTarget,
    _Out_opt_ PLX_TMPFS_INODE *Result)
{
    PLX_TMPFS Fs = Mount->Tmpfs;
    PLX_TMPFS_INODE Directory, Inode;
    PLX_TMPFS_DIRENT Entry;
    LX_TIMESPEC Now;
    PCSTR Name;
    ULONG NameLength;
    LONG Error;

    if (Result != NULL)
        *Result = NULL;
    if (Fs == NULL)
        return -LX_ENOENT;
    if (Mount->Flags & LX_MS_RDONLY)
        return -LX_EROFS;
    Error = LxTmpfsSplit(Relative, &Name, &NameLength);
    if (Error != 0)
        return (Error == -LX_EBUSY) ? -LX_EEXIST : Error;
    Error = LxTmpfsInodeCreate(Fs, Mode, Uid, Gid, Device, LinkTarget, &Inode);
    if (Error != 0)
        return Error;
    Entry = LxTmpfsDirentAllocate(Name, NameLength);
    if (Entry == NULL)
    {
        LxTmpfsInodeDereference(Inode);
        return -LX_ENOMEM;
    }

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Fs);
    Error = LxTmpfsResolveParent(Mount, Relative, &Directory, &Name, &NameLength);
    if ((Error == 0) && (LxTmpfsFind(Directory, Name, NameLength) != NULL))
        Error = -LX_EEXIST;
    if ((Error == 0) && (Directory->Nlink == 0))
        Error = -LX_ENOENT;
    if (Error == 0)
    {
        Inode->Mtime = Inode->Atime = Inode->Ctime = Now;
        LxTmpfsAttach(Directory, Entry, Inode, &Now);
        if (Result != NULL)
        {
            LxTmpfsInodeReference(Inode);
            *Result = Inode;
        }
    }
    LxTmpfsUnlock(Fs);
    if (Error != 0)
    {
        LxFree(Entry);
        LxTmpfsInodeDereference(Inode);
    }
    return Error;
}

LONG
LxTmpfsUnlink(
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _In_ BOOLEAN Directory)
{
    PLX_TMPFS Fs = Mount->Tmpfs;
    PLX_TMPFS_INODE Parent, Victim = NULL;
    PLX_TMPFS_DIRENT Entry;
    LX_TIMESPEC Now;
    PCSTR Name;
    ULONG NameLength;
    LONG Error;

    if (Fs == NULL)
        return -LX_ENOENT;
    if (Mount->Flags & LX_MS_RDONLY)
        return -LX_EROFS;
    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Fs);
    Error = LxTmpfsResolveParent(Mount, Relative, &Parent, &Name, &NameLength);
    Entry = (Error == 0) ? LxTmpfsFind(Parent, Name, NameLength) : NULL;
    if ((Error == 0) && (Entry == NULL))
        Error = -LX_ENOENT;
    if (Error == 0)
    {
        BOOLEAN IsDirectory = LxTmpfsIsDirectory(Entry->Inode);

        if (Directory && !IsDirectory)
            Error = -LX_ENOTDIR;
        else if (!Directory && IsDirectory)
            Error = -LX_EISDIR;
        else if (IsDirectory && !IsListEmpty(&Entry->Inode->Entries))
            Error = -LX_ENOTEMPTY;
        else
            Victim = LxTmpfsDetach(Parent, Entry, &Now);
    }
    LxTmpfsUnlock(Fs);
    if (Victim != NULL)
        LxTmpfsInodeDereference(Victim);
    return Error;
}

LONG
LxTmpfsRename(
    _In_ PLX_MOUNT FromMount,
    _In_ PCSTR FromRelative,
    _In_ PLX_MOUNT ToMount,
    _In_ PCSTR ToRelative,
    _In_ ULONG Flags)
{
    PLX_TMPFS Fs = FromMount->Tmpfs;
    PLX_TMPFS_INODE FromDirectory, ToDirectory, Source, Walk, Victim = NULL;
    PLX_TMPFS_DIRENT FromEntry, ToEntry, Entry = NULL;
    LX_TIMESPEC Now;
    PCSTR FromName, ToName;
    ULONG FromLength, ToLength;
    LONG Error;

    if ((Fs == NULL) || (Fs != ToMount->Tmpfs))
        return -LX_EXDEV;
    if ((FromMount->Flags | ToMount->Flags) & LX_MS_RDONLY)
        return -LX_EROFS;
    Error = LxTmpfsSplit(ToRelative, &ToName, &ToLength);
    if (Error != 0)
        return Error;
    Entry = LxTmpfsDirentAllocate(ToName, ToLength);
    if (Entry == NULL)
        return -LX_ENOMEM;

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Fs);
    Error = LxTmpfsResolveParent(FromMount, FromRelative, &FromDirectory, &FromName, &FromLength);
    if (Error == 0)
        Error = LxTmpfsResolveParent(ToMount, ToRelative, &ToDirectory, &ToName, &ToLength);
    FromEntry = (Error == 0) ? LxTmpfsFind(FromDirectory, FromName, FromLength) : NULL;
    if ((Error == 0) && (FromEntry == NULL))
        Error = -LX_ENOENT;
    if (Error != 0)
        goto Exit;

    Source = FromEntry->Inode;
    ToEntry = LxTmpfsFind(ToDirectory, ToName, ToLength);
    if ((ToEntry != NULL) && (ToEntry->Inode == Source))
        goto Exit;
    if (LxTmpfsIsDirectory(Source))
    {
        for (Walk = ToDirectory; Walk != NULL; Walk = Walk->Parent)
        {
            if (Walk == Source)
            {
                Error = -LX_EINVAL;
                goto Exit;
            }
        }
    }
    if (ToEntry != NULL)
    {
        PLX_TMPFS_INODE Target = ToEntry->Inode;

        if (Flags & 1)
            Error = -LX_EEXIST;
        else if (LxTmpfsIsDirectory(Source) && !LxTmpfsIsDirectory(Target))
            Error = -LX_ENOTDIR;
        else if (!LxTmpfsIsDirectory(Source) && LxTmpfsIsDirectory(Target))
            Error = -LX_EISDIR;
        else if (LxTmpfsIsDirectory(Target) && !IsListEmpty(&Target->Entries))
            Error = -LX_ENOTEMPTY;
        if (Error != 0)
            goto Exit;
        Victim = LxTmpfsDetach(ToDirectory, ToEntry, &Now);
    }

    RemoveEntryList(&FromEntry->Link);
    FromDirectory->EntryCount--;
    if (LxTmpfsIsDirectory(Source))
        FromDirectory->Nlink--;
    FromDirectory->Mtime = FromDirectory->Ctime = Now;
    LxFree(FromEntry);
    LxTmpfsAttach(ToDirectory, Entry, Source, &Now);
    Source->Ctime = Now;
    Entry = NULL;

Exit:
    LxTmpfsUnlock(Fs);
    LxFree(Entry);
    if (Victim != NULL)
        LxTmpfsInodeDereference(Victim);
    return Error;
}

LONG
LxTmpfsLink(
    _In_ PLX_MOUNT FromMount,
    _In_ PCSTR FromRelative,
    _In_ PLX_MOUNT ToMount,
    _In_ PCSTR ToRelative)
{
    PLX_TMPFS Fs = FromMount->Tmpfs;
    PLX_TMPFS_INODE Source, Directory;
    PLX_TMPFS_DIRENT Entry;
    LX_TIMESPEC Now;
    PCSTR Name;
    ULONG NameLength;
    LONG Error;

    if ((Fs == NULL) || (Fs != ToMount->Tmpfs))
        return -LX_EXDEV;
    if (ToMount->Flags & LX_MS_RDONLY)
        return -LX_EROFS;
    Error = LxTmpfsSplit(ToRelative, &Name, &NameLength);
    if (Error != 0)
        return (Error == -LX_EBUSY) ? -LX_EEXIST : Error;
    Entry = LxTmpfsDirentAllocate(Name, NameLength);
    if (Entry == NULL)
        return -LX_ENOMEM;

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Fs);
    Error = LxTmpfsResolve(FromMount, FromRelative, &Source);
    if ((Error == 0) && LxTmpfsIsDirectory(Source))
        Error = -LX_EPERM;
    if (Error == 0)
        Error = LxTmpfsResolveParent(ToMount, ToRelative, &Directory, &Name, &NameLength);
    if ((Error == 0) && (LxTmpfsFind(Directory, Name, NameLength) != NULL))
        Error = -LX_EEXIST;
    if ((Error == 0) && (Source->Nlink == 0))
        Error = -LX_ENOENT;
    if ((Error == 0) && (Source->Nlink >= LX_TMPFS_LINK_MAX))
        Error = -LX_EMLINK;
    if (Error == 0)
    {
        LxTmpfsInodeReference(Source);
        Source->Nlink++;
        Source->Ctime = Now;
        LxTmpfsAttach(Directory, Entry, Source, &Now);
        Entry = NULL;
    }
    LxTmpfsUnlock(Fs);
    LxFree(Entry);
    return Error;
}

PSTR
LxTmpfsInodePath(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ PLX_MOUNT Mount)
{
    PLX_TMPFS_INODE Root, Walk;
    PSTR Buffer, Result = NULL;
    SIZE_T Used = LX_PATH_MAX - 1;

    if ((Mount->Tmpfs != Inode->Fs) || !LxTmpfsIsDirectory(Inode))
        return NULL;
    Buffer = LxAlloc(LX_PATH_MAX);
    if (Buffer == NULL)
        return NULL;
    Buffer[Used] = '\0';
    LxTmpfsLockShared(Inode->Fs);
    if (LxTmpfsMountRoot(Mount, &Root) == 0)
    {
        for (Walk = Inode; (Walk != NULL) && (Walk != Root); Walk = Walk->Parent)
        {
            PLX_TMPFS_DIRENT Found = NULL;
            PLIST_ENTRY Link;

            if (Walk->Parent == NULL)
                break;
            for (Link = Walk->Parent->Entries.Flink; Link != &Walk->Parent->Entries; Link = Link->Flink)
            {
                PLX_TMPFS_DIRENT Entry = CONTAINING_RECORD(Link, LX_TMPFS_DIRENT, Link);

                if (Entry->Inode == Walk)
                {
                    Found = Entry;
                    break;
                }
            }
            if ((Found == NULL) || (Used < Found->NameLength + 1))
                break;
            Used -= Found->NameLength;
            RtlCopyMemory(Buffer + Used, Found->Name, Found->NameLength);
            Buffer[--Used] = '/';
        }
        if (Walk == Root)
        {
            BOOLEAN Top = (strcmp(Mount->Target, "/") == 0);

            if (Buffer[Used] == '\0')
                Result = LxStrDup(Mount->Target);
            else
                Result = LxStrPrintf("%s%s", Top ? "" : Mount->Target, Buffer + Used);
        }
    }
    LxTmpfsUnlock(Inode->Fs);
    LxFree(Buffer);
    return Result;
}

static
ULONG64
LxTmpfsEncodeDevice(
    _In_ ULONG64 Device)
{
    return ((Device >> 32) << 8) | (Device & 0xFF) | ((Device & 0xFFF00) << 12);
}

VOID
LxTmpfsInodeStat(
    _In_ PLX_TMPFS_INODE Inode,
    _In_opt_ PLX_MOUNT Mount,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    LxTmpfsLockShared(Inode->Fs);
    Stat->Dev = (Mount != NULL) ? Mount->Dev : 0;
    Stat->Ino = Inode->Ino;
    Stat->Mode = Inode->Mode;
    Stat->Nlink = Inode->Nlink;
    Stat->Uid = Inode->Uid;
    Stat->Gid = Inode->Gid;
    Stat->BlkSize = 4096;
    switch (Inode->Mode & LX_S_IFMT)
    {
        case LX_S_IFREG:
            Stat->Size = (LONG64)Inode->Size;
            Stat->Blocks = (LONG64)(LX_ALIGN_UP(Inode->Size, LX_PAGE_SIZE) / 512);
            break;
        case LX_S_IFDIR:
            Stat->Size = ((LONG64)Inode->EntryCount + 2) * LX_TMPFS_DIRENT_SIZE;
            break;
        case LX_S_IFLNK:
            Stat->Size = (Inode->LinkTarget != NULL) ? (LONG64)strlen(Inode->LinkTarget) : 0;
            break;
        case LX_S_IFCHR:
        case LX_S_IFBLK:
            Stat->Rdev = LxTmpfsEncodeDevice(Inode->Device);
            break;
        default:
            break;
    }
    Stat->Atime = Inode->Atime;
    Stat->Mtime = Inode->Mtime;
    Stat->Ctime = Inode->Ctime;
    LxTmpfsUnlock(Inode->Fs);
}

VOID
LxTmpfsInodeAttributes(
    _In_ PLX_TMPFS_INODE Inode,
    _Out_ PULONG Mode,
    _Out_ PULONG Uid,
    _Out_ PULONG Gid,
    _Out_ PULONG64 Device)
{
    LxTmpfsLockShared(Inode->Fs);
    *Mode = Inode->Mode;
    *Uid = Inode->Uid;
    *Gid = Inode->Gid;
    *Device = Inode->Device;
    LxTmpfsUnlock(Inode->Fs);
}

VOID
LxTmpfsInodeSetAttributes(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    LX_TIMESPEC Now;

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Inode->Fs);
    Inode->Mode = (Inode->Mode & LX_S_IFMT) | (Mode & 07777);
    Inode->Uid = Uid;
    Inode->Gid = Gid;
    Inode->Ctime = Now;
    LxTmpfsUnlock(Inode->Fs);
}

VOID
LxTmpfsInodeSetTimes(
    _In_ PLX_TMPFS_INODE Inode,
    _In_reads_(2) PLX_TIMESPEC Times)
{
    LX_TIMESPEC Now;
    ULONG Index;

    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Inode->Fs);
    for (Index = 0; Index < 2; Index++)
    {
        PLX_TIMESPEC Target = (Index == 0) ? &Inode->Atime : &Inode->Mtime;

        if (Times[Index].Nanoseconds == ((1LL << 30) - 2))
            continue;
        *Target = (Times[Index].Nanoseconds == ((1LL << 30) - 1)) ? Now : Times[Index];
    }
    Inode->Ctime = Now;
    LxTmpfsUnlock(Inode->Fs);
}

PSTR
LxTmpfsInodeLinkTarget(
    _In_ PLX_TMPFS_INODE Inode)
{
    PSTR Target;

    LxTmpfsLockShared(Inode->Fs);
    Target = LxStrDup((Inode->LinkTarget != NULL) ? Inode->LinkTarget : "");
    LxTmpfsUnlock(Inode->Fs);
    return Target;
}

ULONG64
LxTmpfsInodeId(
    _In_ PLX_TMPFS_INODE Inode)
{
    return Inode->Ino;
}

ULONG64
LxTmpfsInodeSize(
    _In_ PLX_TMPFS_INODE Inode)
{
    ULONG64 Size;

    ExAcquireFastMutex(&Inode->StoreLock);
    Size = Inode->Size;
    ExReleaseFastMutex(&Inode->StoreLock);
    return Size;
}

LONG
LxTmpfsXattrRead(
    _In_ PLX_TMPFS_INODE Inode,
    _Out_writes_bytes_to_(Capacity, *Length) PUCHAR Buffer,
    _In_ ULONG Capacity,
    _Out_ PULONG Length)
{
    LxTmpfsLockShared(Inode->Fs);
    *Length = min(Inode->XattrLength, Capacity);
    if (*Length != 0)
        RtlCopyMemory(Buffer, Inode->Xattr, *Length);
    LxTmpfsUnlock(Inode->Fs);
    return 0;
}

LONG
LxTmpfsSetPrivate(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ PVOID Object,
    _In_ PLX_TMPFS_PRIVATE_RELEASE Release)
{
    LONG Error = 0;

    LxTmpfsLockExclusive(Inode->Fs);
    if (Inode->PrivateRelease != NULL)
    {
        Error = -LX_EEXIST;
    }
    else
    {
        Inode->Private = Object;
        Inode->PrivateRelease = Release;
    }
    LxTmpfsUnlock(Inode->Fs);
    return Error;
}

PVOID
LxTmpfsGetPrivate(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ PLX_TMPFS_PRIVATE_RELEASE Release)
{
    PVOID Object = NULL;

    LxTmpfsLockShared(Inode->Fs);
    if (Inode->PrivateRelease == Release)
        Object = Inode->Private;
    LxTmpfsUnlock(Inode->Fs);
    return Object;
}

LONG
LxTmpfsXattrWrite(
    _In_ PLX_TMPFS_INODE Inode,
    _In_reads_bytes_(Length) PUCHAR Blob,
    _In_ ULONG Length)
{
    PUCHAR Copy = NULL, Old;
    LX_TIMESPEC Now;

    if (Length != 0)
    {
        Copy = LxAlloc(Length);
        if (Copy == NULL)
            return -LX_ENOMEM;
        RtlCopyMemory(Copy, Blob, Length);
    }
    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Inode->Fs);
    Old = Inode->Xattr;
    Inode->Xattr = Copy;
    Inode->XattrLength = Length;
    Inode->Ctime = Now;
    LxTmpfsUnlock(Inode->Fs);
    LxFree(Old);
    return 0;
}

static
PLX_TMPFS_STORE
LxTmpfsReadStore(
    _In_ PLX_TMPFS_INODE Inode,
    _Out_ PULONG64 Size)
{
    PLX_TMPFS_STORE Store;

    ExAcquireFastMutex(&Inode->StoreLock);
    Store = (Inode->Resize != NULL) ? Inode->Resize->Old : Inode->Store;
    if (Store != NULL)
        InterlockedIncrement(&Store->RefCount);
    *Size = Inode->Size;
    ExReleaseFastMutex(&Inode->StoreLock);
    return Store;
}

static
VOID
LxTmpfsResizeVisit(
    _In_ PVOID Context,
    _In_ PLX_PROCESS Process)
{
    PLX_TMPFS_INODE Inode = Context;
    PLX_TMPFS_RESIZE Resize = Inode->Resize;
    PLX_PROCESS *Grown = NULL;

    if (Resize->Count == Resize->Capacity)
    {
        ULONG Capacity = Resize->Capacity ? Resize->Capacity * 2 : 16;

        Grown = LxAlloc(Capacity * sizeof(*Grown));
        if (Grown == NULL)
            return;
        ExAcquireFastMutex(&Inode->StoreLock);
        if (Resize->Count != 0)
            RtlCopyMemory(Grown, Resize->Visited, Resize->Count * sizeof(*Grown));
        LxFree(Resize->Visited);
        Resize->Visited = Grown;
        Resize->Capacity = Capacity;
        ExReleaseFastMutex(&Inode->StoreLock);
    }
    ExAcquireFastMutex(&Inode->StoreLock);
    Resize->Visited[Resize->Count++] = Process;
    ExReleaseFastMutex(&Inode->StoreLock);
}

static
LONG
LxTmpfsResize(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Capacity)
{
    PLX_TMPFS_STORE Old = Inode->Store, New = NULL;
    LX_TMPFS_RESIZE Resize;
    ULONG64 Copy;
    BOOLEAN Mapped;
    LONG Error;

    if (Capacity != 0)
    {
        Error = LxTmpfsStoreCreate(Inode->Fs, Capacity, &New);
        if (Error != 0)
            return Error;
    }
    Copy = (Old != NULL) ? min(min(Inode->Size, Old->Capacity), Capacity) : 0;
    RtlZeroMemory(&Resize, sizeof(Resize));
    Resize.Old = Old;

    ExAcquireFastMutex(&Inode->StoreLock);
    Mapped = Inode->Mapped;
    ExReleaseFastMutex(&Inode->StoreLock);
    if (!Mapped)
    {
        if (Copy != 0)
            LxTmpfsCopyMemory(New->View, Old->View, (SIZE_T)Copy);
        ExAcquireFastMutex(&Inode->StoreLock);
        Mapped = Inode->Mapped;
        if (!Mapped)
            Inode->Store = New;
        ExReleaseFastMutex(&Inode->StoreLock);
        if (!Mapped)
        {
            LxTmpfsStoreDereference(Old);
            return 0;
        }
    }

    ExAcquireFastMutex(&Inode->StoreLock);
    Inode->Resize = &Resize;
    Inode->Store = New;
    KeClearEvent(&Inode->ResizeDone);
    ExReleaseFastMutex(&Inode->StoreLock);

    LxMmReleaseKeyedViews(Inode, LxTmpfsResizeVisit, Inode);
    if (Copy != 0)
        LxTmpfsCopyMemory(New->View, Old->View, (SIZE_T)Copy);

    ExAcquireFastMutex(&Inode->StoreLock);
    Inode->Resize = NULL;
    ExReleaseFastMutex(&Inode->StoreLock);
    KeSetEvent(&Inode->ResizeDone, IO_NO_INCREMENT, FALSE);
    LxFree(Resize.Visited);
    LxTmpfsStoreDereference(Old);
    return 0;
}

static
VOID
LxTmpfsZeroStore(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLX_TMPFS_STORE Store = Inode->Store;

    if (Store == NULL)
        return;
    End = min(End, Store->Capacity);
    if (Start < End)
        LxTmpfsCopyMemory(Store->View + Start, NULL, (SIZE_T)(End - Start));
}

static
VOID
LxTmpfsSetSize(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Size)
{
    ExAcquireFastMutex(&Inode->StoreLock);
    Inode->Size = Size;
    ExReleaseFastMutex(&Inode->StoreLock);
}

static
LONG
LxTmpfsTruncateLocked(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Size)
{
    ULONG64 Old = Inode->Size, Capacity = (Inode->Store != NULL) ? Inode->Store->Capacity : 0;
    ULONG64 Needed = LX_ALIGN_UP(Size, LX_PAGE_SIZE);
    LONG Error = 0;

    if (Size > Capacity)
    {
        LxTmpfsZeroStore(Inode, Old, LX_ALIGN_UP(Old, LX_PAGE_SIZE));
        Error = LxTmpfsResize(Inode, Needed);
    }
    else if (Size < Old)
    {
        if ((Needed == 0) || (Needed <= Capacity / 2))
        {
            LxTmpfsSetSize(Inode, Size);
            Error = LxTmpfsResize(Inode, Needed);
            if (Error != 0)
                LxTmpfsZeroStore(Inode, Size, Old);
        }
        else
        {
            LxTmpfsZeroStore(Inode, Size, Old);
        }
    }
    else if (Size > Old)
    {
        LxTmpfsZeroStore(Inode, Old, LX_ALIGN_UP(Old, LX_PAGE_SIZE));
    }
    if (Error == 0)
        LxTmpfsSetSize(Inode, Size);
    return Error;
}

LONG
LxTmpfsInodeTruncate(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ LONG64 Size)
{
    LX_TIMESPEC Now;
    LONG Error;

    if (LxTmpfsIsDirectory(Inode))
        return -LX_EISDIR;
    if ((Inode->Mode & LX_S_IFMT) != LX_S_IFREG)
        return -LX_EINVAL;
    if ((Size < 0) || (Size > LX_TMPFS_MAX_SIZE))
        return -LX_EFBIG;
    LxTmpfsDataLock(Inode);
    Error = LxTmpfsTruncateLocked(Inode, (ULONG64)Size);
    LxTmpfsDataUnlock(Inode);
    if (Error == 0)
    {
        LxTmpfsNow(&Now);
        LxTmpfsLockExclusive(Inode->Fs);
        Inode->Mtime = Inode->Ctime = Now;
        LxTmpfsUnlock(Inode->Fs);
    }
    return Error;
}

LONG
LxTmpfsInodeZeroRange(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    LxTmpfsDataLock(Inode);
    LxTmpfsZeroStore(Inode, Start, min(End, Inode->Size));
    LxTmpfsDataUnlock(Inode);
    return 0;
}

static
PLX_TMPFS_STORE
LxTmpfsMappedStore(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ BOOLEAN Shared,
    _Out_opt_ PULONG64 Size)
{
    PLX_THREAD Thread = LxCurrentThread();
    PLX_PROCESS Self = (Thread != NULL) ? Thread->Process->Mm : NULL;
    PLX_TMPFS_STORE Store;

    for (;;)
    {
        BOOLEAN Wait = FALSE;

        ExAcquireFastMutex(&Inode->StoreLock);
        Inode->Mapped = TRUE;
        Store = Inode->Store;
        if (Inode->Resize != NULL)
        {
            ULONG Index;

            for (Index = 0; Shared && (Self != NULL) && (Index < Inode->Resize->Count); Index++)
            {
                if (Inode->Resize->Visited[Index] == Self)
                    Wait = TRUE;
            }
            if (!Wait)
                Store = Inode->Resize->Old;
        }
        if (!Wait)
        {
            if (Store != NULL)
                InterlockedIncrement(&Store->RefCount);
            if (Size != NULL)
                *Size = Inode->Size;
        }
        ExReleaseFastMutex(&Inode->StoreLock);
        if (!Wait)
            return Store;
        KeWaitForSingleObject(&Inode->ResizeDone, Executive, KernelMode, FALSE, NULL);
    }
}

NTSTATUS
LxTmpfsMapSection(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ BOOLEAN Shared,
    _Out_ PHANDLE Handle,
    _Out_opt_ PULONG64 Size)
{
    PLX_TMPFS_STORE Store;
    NTSTATUS Status;

    *Handle = NULL;
    if (LxTmpfsSquashLoad(Inode) != 0)
        return STATUS_DATA_ERROR;
    Store = LxTmpfsMappedStore(Inode, Shared, Size);
    if (Store == NULL)
        return STATUS_MAPPED_FILE_SIZE_ZERO;
    Status = ObOpenObjectByPointer(Store->Section, OBJ_KERNEL_HANDLE, NULL, SECTION_ALL_ACCESS, NULL, KernelMode,
                                   Handle);
    LxTmpfsStoreDereference(Store);
    return Status;
}

LONG
LxTmpfsInodeRemoveRange(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Size;
    PLX_TMPFS_STORE Store;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Inode->Fs->Squash != NULL)
        return -LX_EROFS;
    Store = LxTmpfsMappedStore(Inode, TRUE, &Size);
    if (Store == NULL)
        return 0;
    End = min(min(End, Size), Store->Capacity);
    if (Start < End)
        Status = LxTmpfsCopyMemory(Store->View + Start, NULL, (SIZE_T)(End - Start));
    LxTmpfsStoreDereference(Store);
    return NT_SUCCESS(Status) ? 0 : -LX_EIO;
}

LONG64
LxTmpfsReadKernel(
    _In_ PLX_TMPFS_INODE Inode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ ULONG64 Offset)
{
    ULONG64 Size, Count, Stored;
    PLX_TMPFS_STORE Store;
    NTSTATUS Status = STATUS_SUCCESS;
    LONG Error = LxTmpfsSquashLoad(Inode);

    if (Error != 0)
        return Error;
    Store = LxTmpfsReadStore(Inode, &Size);
    if (Offset >= Size)
    {
        LxTmpfsStoreDereference(Store);
        return 0;
    }
    Count = min((ULONG64)Length, Size - Offset);
    Stored = (Store != NULL) ? ((Store->Capacity > Offset) ? min(Count, Store->Capacity - Offset) : 0) : 0;
    if (Stored != 0)
        Status = LxTmpfsCopyMemory(Buffer, Store->View + Offset, (SIZE_T)Stored);
    if (NT_SUCCESS(Status) && (Stored < Count))
        RtlZeroMemory((PUCHAR)Buffer + Stored, (SIZE_T)(Count - Stored));
    LxTmpfsStoreDereference(Store);
    return NT_SUCCESS(Status) ? (LONG64)Count : -LX_EIO;
}

static
LONG64
LxTmpfsRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_TMPFS_INODE Inode = File->Object;
    PLX_TMPFS_STORE Store;
    ULONG64 Size, Count, Stored;
    LONG64 Position, Result = 0;

    if (LxTmpfsIsDirectory(Inode))
        return -LX_EISDIR;
    Result = LxTmpfsSquashLoad(Inode);
    if (Result != 0)
        return Result;
    if (Offset == NULL)
        KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Position = (Offset != NULL) ? *Offset : File->Position;
    Store = LxTmpfsReadStore(Inode, &Size);
    if ((Position >= 0) && ((ULONG64)Position < Size))
    {
        Count = min(Length, Size - (ULONG64)Position);
        Stored = (Store != NULL) ? ((Store->Capacity > (ULONG64)Position) ?
                                    min(Count, Store->Capacity - (ULONG64)Position) : 0) : 0;
        if (Stored != 0)
            Result = LxCopyToUser(Buffer, Store->View + Position, (SIZE_T)Stored);
        if ((Result == 0) && (Stored < Count))
            Result = LxZeroUser(Buffer + Stored, (SIZE_T)(Count - Stored));
        if (Result == 0)
        {
            Result = (LONG64)Count;
            if (Offset != NULL)
                *Offset = Position + (LONG64)Count;
            else
                File->Position = Position + (LONG64)Count;
        }
    }
    LxTmpfsStoreDereference(Store);
    if (Offset == NULL)
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    if ((Result > 0) && (Inode->Fs->Squash == NULL))
    {
        LX_TIMESPEC Now;

        LxTmpfsNow(&Now);
        if ((Inode->Atime.Seconds <= Inode->Mtime.Seconds) || (Inode->Atime.Seconds <= Inode->Ctime.Seconds) ||
            (Now.Seconds - Inode->Atime.Seconds >= 86400))
        {
            LxTmpfsLockExclusive(Inode->Fs);
            Inode->Atime = Now;
            LxTmpfsUnlock(Inode->Fs);
        }
    }
    return Result;
}

static
LONG64
LxTmpfsWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_TMPFS_INODE Inode = File->Object;
    ULONG64 Position, End, Done = 0, Capacity;
    LONG64 Result = 0;
    LX_TIMESPEC Now;

    if (LxTmpfsIsDirectory(Inode))
        return -LX_EISDIR;
    if (Offset == NULL)
        KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    LxTmpfsDataLock(Inode);
    if (File->Flags & LX_O_APPEND)
        Position = Inode->Size;
    else
        Position = (ULONG64)((Offset != NULL) ? *Offset : File->Position);
    End = Position + Length;
    if (((LONG64)Position < 0) || (End < Position) || (End > (ULONG64)LX_TMPFS_MAX_SIZE))
        Result = -LX_EFBIG;
    Capacity = (Inode->Store != NULL) ? Inode->Store->Capacity : 0;
    if ((Result == 0) && (Length != 0) && (End > Capacity))
    {
        ULONG64 Grown = max(LX_ALIGN_UP(End, LX_PAGE_SIZE), Capacity + Capacity / 2);

        LxTmpfsZeroStore(Inode, Inode->Size, LX_ALIGN_UP(Inode->Size, LX_PAGE_SIZE));
        Result = LxTmpfsResize(Inode, min(Grown, (ULONG64)LX_TMPFS_MAX_SIZE));
        if ((Result != 0) && (Grown > LX_ALIGN_UP(End, LX_PAGE_SIZE)))
            Result = LxTmpfsResize(Inode, LX_ALIGN_UP(End, LX_PAGE_SIZE));
    }
    else if ((Result == 0) && (Position > Inode->Size))
    {
        LxTmpfsZeroStore(Inode, Inode->Size, LX_ALIGN_UP(Inode->Size, LX_PAGE_SIZE));
    }
    while ((Result == 0) && (Done < Length))
    {
        ULONG64 Chunk = min(Length - Done, LX_IO_CHUNK);

        Result = LxCopyFromUser(Inode->Store->View + Position + Done, Buffer + Done, (SIZE_T)Chunk);
        if (Result == 0)
            Done += Chunk;
    }
    if ((Done != 0) && (Position + Done > Inode->Size))
        LxTmpfsSetSize(Inode, Position + Done);
    LxTmpfsDataUnlock(Inode);
    if (Done != 0)
    {
        if (Offset != NULL)
            *Offset = (LONG64)(Position + Done);
        else
            File->Position = (LONG64)(Position + Done);
    }
    if (Offset == NULL)
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    if (Done == 0)
        return Result;
    LxTmpfsNow(&Now);
    LxTmpfsLockExclusive(Inode->Fs);
    Inode->Mtime = Inode->Ctime = Now;
    LxTmpfsUnlock(Inode->Fs);
    return (LONG64)Done;
}

static
ULONG
LxTmpfsPoll(
    _In_ PLX_FILE File,
    _In_opt_ PLX_POLL_ENTRY Entry)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Entry);
    return LX_POLLIN | LX_POLLOUT | LX_POLLRDNORM | LX_POLLWRNORM;
}

static
LONG
LxTmpfsFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    LxTmpfsInodeStat(File->Object, File->Mount, Stat);
    return 0;
}

static
LONG64
LxTmpfsSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    PLX_TMPFS_INODE Inode = File->Object;
    LONG64 Base = 0, Size, Result = 0;

    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    if (LxTmpfsIsDirectory(Inode))
    {
        if (Whence == LX_SEEK_CUR)
            Offset += File->DirOffset;
        if (((Whence != LX_SEEK_SET) && (Whence != LX_SEEK_CUR)) || (Offset < 0))
            Result = -LX_EINVAL;
        else
            Result = File->DirOffset = Offset;
        KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
        return Result;
    }
    Size = (LONG64)LxTmpfsInodeSize(Inode);
    switch (Whence)
    {
        case LX_SEEK_SET:
            break;
        case LX_SEEK_CUR:
            Base = File->Position;
            break;
        case LX_SEEK_END:
            Base = Size;
            break;
        case 3:
        case 4:
            if ((Offset < 0) || (Offset >= Size))
                Result = -LX_ENXIO;
            else if (Whence == 4)
                Offset = Size;
            break;
        default:
            Result = -LX_EINVAL;
            break;
    }
    if ((Result == 0) && (Base + Offset < 0))
        Result = -LX_EINVAL;
    if (Result == 0)
        Result = File->Position = Base + Offset;
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);
    return Result;
}

static
BOOLEAN
LxTmpfsEmit(
    _Inout_ PUCHAR Out,
    _In_ ULONG OutSize,
    _Inout_ PULONG OutUsed,
    _In_ ULONG64 Ino,
    _In_ LONG64 Next,
    _In_ UCHAR Type,
    _In_reads_(Length) PCSTR Name,
    _In_ ULONG Length)
{
    ULONG Record = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(LX_DIRENT64, Name) + Length + 1, 8);
    PLX_DIRENT64 Dirent;

    if (*OutUsed + Record > OutSize)
        return FALSE;
    Dirent = (PLX_DIRENT64)(Out + *OutUsed);
    RtlZeroMemory(Dirent, Record);
    Dirent->Ino = Ino;
    Dirent->Off = Next;
    Dirent->RecLen = (USHORT)Record;
    Dirent->Type = Type;
    RtlCopyMemory(Dirent->Name, Name, Length);
    *OutUsed += Record;
    return TRUE;
}

static
LONG64
LxTmpfsGetdents(
    _In_ PLX_FILE File,
    _In_ ULONG64 UserBuffer,
    _In_ ULONG Length)
{
    PLX_TMPFS_INODE Inode = File->Object;
    ULONG OutSize = min(Length, 65536), OutUsed = 0;
    PLIST_ENTRY Link;
    LONG64 Result = 0;
    BOOLEAN Full = FALSE;
    PUCHAR Out;

    if (!LxTmpfsIsDirectory(Inode))
        return -LX_ENOTDIR;
    Result = LxTmpfsSquashFill(Inode);
    if (Result != 0)
        return Result;
    Out = LxAllocPaged(OutSize);
    if (Out == NULL)
        return -LX_ENOMEM;

    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    LxTmpfsLockShared(Inode->Fs);
    if (File->DirOffset == 0)
    {
        Full = !LxTmpfsEmit(Out, OutSize, &OutUsed, Inode->Ino, 1, LX_DT_DIR, ".", 1);
        if (!Full)
            File->DirOffset = 1;
    }
    if (!Full && (File->DirOffset == 1))
    {
        Full = !LxTmpfsEmit(Out, OutSize, &OutUsed, (Inode->Parent != NULL) ? Inode->Parent->Ino : Inode->Ino, 2,
                            LX_DT_DIR, "..", 2);
        if (!Full)
            File->DirOffset = 2;
    }
    for (Link = Inode->Entries.Flink; !Full && (Link != &Inode->Entries); Link = Link->Flink)
    {
        PLX_TMPFS_DIRENT Entry = CONTAINING_RECORD(Link, LX_TMPFS_DIRENT, Link);

        if ((LONG64)Entry->Cookie < File->DirOffset)
            continue;
        Full = !LxTmpfsEmit(Out, OutSize, &OutUsed, Entry->Inode->Ino, (LONG64)Entry->Cookie + 1,
                            (UCHAR)((Entry->Inode->Mode & LX_S_IFMT) >> 12), Entry->Name, Entry->NameLength);
        if (!Full)
            File->DirOffset = (LONG64)Entry->Cookie + 1;
    }
    LxTmpfsUnlock(Inode->Fs);
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);

    if (Full && (OutUsed == 0))
        Result = -LX_EINVAL;
    else if (OutUsed != 0)
    {
        Result = LxCopyToUser(UserBuffer, Out, OutUsed);
        if (Result == 0)
            Result = OutUsed;
    }
    LxFreePaged(Out);
    return Result;
}

static
LONG
LxTmpfsFileTruncate(
    _In_ PLX_FILE File,
    _In_ LONG64 Length)
{
    return LxTmpfsInodeTruncate(File->Object, Length);
}

static
VOID
LxTmpfsRelease(
    _In_ PLX_FILE File)
{
    PLX_TMPFS_INODE Inode = File->Object;
    PLX_TMPFS Fs;

    if (Inode == NULL)
        return;
    Fs = Inode->Fs;
    File->Object = NULL;
    if (Fs->Squash != NULL)
        LxSquashfsRemoveUser(Fs->Squash);
    LxTmpfsInodeDereference(Inode);
    LxTmpfsDereference(Fs);
}

static const LX_FILE_OPS LxTmpfsFileOps =
{
    LxTmpfsRead, LxTmpfsWrite, NULL, LxTmpfsPoll, LxTmpfsFileStat, LxTmpfsSeek, LxTmpfsGetdents, LxTmpfsFileTruncate,
    LxTmpfsRelease
};

PLX_TMPFS_INODE
LxTmpfsFileInode(
    _In_opt_ PLX_FILE File)
{
    return ((File != NULL) && (File->Ops == &LxTmpfsFileOps)) ? File->Object : NULL;
}

LONG
LxTmpfsOpenFile(
    _In_ PLX_TMPFS_INODE Inode,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    PLX_FILE File = LxFileAlloc(&LxTmpfsFileOps, Flags, Inode->Mode & LX_S_IFMT);

    *Result = NULL;
    if (File == NULL)
        return -LX_ENOMEM;
    LxTmpfsReference(Inode->Fs);
    LxTmpfsInodeReference(Inode);
    if (Inode->Fs->Squash != NULL)
        LxSquashfsAddUser(Inode->Fs->Squash);
    File->Object = Inode;
    File->Ino = Inode->Ino;
    File->Directory = LxTmpfsIsDirectory(Inode);
    File->Mode = Inode->Mode & 07777;
    *Result = File;
    return 0;
}

LONG
LxTmpfsCreateAnonymous(
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _Out_ PLX_FILE *Result)
{
    PLX_TMPFS_INODE Inode;
    LONG Error;

    *Result = NULL;
    if (LxTmpfsShm == NULL)
        return -LX_ENOMEM;
    Error = LxTmpfsInodeCreate(LxTmpfsShm, Mode, Uid, Gid, 0, NULL, &Inode);
    if (Error != 0)
        return Error;
    Error = LxTmpfsOpenFile(Inode, LX_O_RDWR, Result);
    LxTmpfsInodeDereference(Inode);
    return Error;
}

ULONG64
LxTmpfsShmemBytes(VOID)
{
    return (ULONG64)ReadNoFence64(&LxTmpfsResident);
}

VOID
LxTmpfsStatfs(
    _In_ PLX_TMPFS Fs,
    _Out_ PULONG64 Blocks,
    _Out_ PULONG64 Free,
    _Out_ PULONG64 Files,
    _Out_ PULONG64 FreeFiles)
{
    ULONG64 Used = (ULONG64)Fs->Used;
    LONG Inodes = Fs->Inodes;

    if (Fs->Squash != NULL)
    {
        LxSquashfsStatfs(Fs->Squash, Blocks, Files);
        *Blocks = LX_ALIGN_UP(*Blocks, LX_PAGE_SIZE) / LX_PAGE_SIZE;
        *Free = 0;
        *FreeFiles = 0;
        return;
    }

    *Blocks = Fs->Limit / LX_PAGE_SIZE;
    *Free = (Used < Fs->Limit) ? (Fs->Limit - Used) / LX_PAGE_SIZE : 0;
    *Files = (ULONG64)Fs->InodeLimit;
    *FreeFiles = (Inodes < Fs->InodeLimit) ? (ULONG64)(Fs->InodeLimit - Inodes) : 0;
}
