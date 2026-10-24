/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System V shared memory, semaphores and message queues
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define LX_IPC_SHM          0
#define LX_IPC_SEM          1
#define LX_IPC_MSG          2
#define LX_IPC_TYPES        3
#define LX_IPCMNI_SHIFT     15
#define LX_IPCMNI           (1 << LX_IPCMNI_SHIFT)
#define LX_IPC_MIN_CYCLE    64
#define LX_IPC_SEQ_MAX      (MAXLONG >> LX_IPCMNI_SHIFT)

typedef struct _LX_IPC_NAMESPACE LX_IPC_NAMESPACE, *PLX_IPC_NAMESPACE;

typedef struct _LX_IPC_OBJECT
{
    LIST_ENTRY Link;
    PLX_IPC_NAMESPACE Namespace;
    ULONG Type;
    LONG Index;
    LONG Id;
    LONG Key;
    ULONG Uid;
    ULONG Gid;
    ULONG Cuid;
    ULONG Cgid;
    ULONG Mode;
    USHORT Seq;
    BOOLEAN Deleted;
    LONG References;
    LONG64 Ctime;
    LIST_ENTRY Waiters;
} LX_IPC_OBJECT, *PLX_IPC_OBJECT;

typedef struct _LX_IPC_IDS
{
    LIST_ENTRY Objects;
    LONG InUse;
    LONG Seq;
    LONG LastIndex;
    LONG Limit;
} LX_IPC_IDS, *PLX_IPC_IDS;

struct _LX_IPC_NAMESPACE
{
    LIST_ENTRY Link;
    PLX_NS Owner;
    LX_IPC_IDS Ids[LX_IPC_TYPES];
    ULONG64 ShmPages;
    LONG64 UsedSems;
    LONG64 MsgHeaders;
    LONG64 MsgBytes;
};

typedef struct _LX_IPC_WAITER
{
    LIST_ENTRY Link;
    KEVENT Event;
    LONG Num;
    BOOLEAN Zero;
} LX_IPC_WAITER, *PLX_IPC_WAITER;

typedef struct _LX_SHM
{
    LX_IPC_OBJECT Object;
    PVOID Section;
    ULONG64 Size;
    LONG64 Atime;
    LONG64 Dtime;
    ULONG Cpid;
    ULONG Lpid;
    ULONG64 Nattch;
} LX_SHM, *PLX_SHM;

typedef struct _LX_SEM
{
    USHORT Value;
    ULONG Pid;
} LX_SEM, *PLX_SEM;

typedef struct _LX_SEM_ARRAY
{
    LX_IPC_OBJECT Object;
    LONG64 Otime;
    ULONG Count;
    LX_SEM Sems[ANYSIZE_ARRAY];
} LX_SEM_ARRAY, *PLX_SEM_ARRAY;

typedef struct _LX_SEM_ADJUST
{
    LIST_ENTRY Link;
    PLX_PROCESS Process;
    PLX_SEM_ARRAY Array;
    SHORT Adjust[ANYSIZE_ARRAY];
} LX_SEM_ADJUST, *PLX_SEM_ADJUST;

typedef struct _LX_MSG
{
    LIST_ENTRY Link;
    LONG64 Type;
    ULONG Size;
    UCHAR Data[ANYSIZE_ARRAY];
} LX_MSG, *PLX_MSG;

typedef struct _LX_MSG_QUEUE
{
    LX_IPC_OBJECT Object;
    LIST_ENTRY Messages;
    ULONG64 Bytes;
    ULONG64 Count;
    ULONG64 MaxBytes;
    LONG64 Stime;
    LONG64 Rtime;
    ULONG Lspid;
    ULONG Lrpid;
} LX_MSG_QUEUE, *PLX_MSG_QUEUE;

static LIST_ENTRY LxIpcNamespaces;
static LIST_ENTRY LxIpcUndos;
static FAST_MUTEX LxIpcLock;
static const LONG LxIpcLimits[LX_IPC_TYPES] = { LX_SHMMNI, LX_SEMMNI, LX_MSGMNI };

VOID
LxIpcInitialize(VOID)
{
    InitializeListHead(&LxIpcNamespaces);
    InitializeListHead(&LxIpcUndos);
    ExInitializeFastMutex(&LxIpcLock);
}

static
LONG64
LxIpcNow(VOID)
{
    LX_TIMESPEC Now;

    LxQueryClock(LX_CLOCK_REALTIME, &Now);
    return Now.Seconds;
}

static
PLX_IPC_NAMESPACE
LxIpcNamespace(
    _In_ PLX_NS Owner,
    _In_ BOOLEAN Create)
{
    PLX_IPC_NAMESPACE Namespace;
    PLIST_ENTRY Entry;
    ULONG Type;

    for (Entry = LxIpcNamespaces.Flink; Entry != &LxIpcNamespaces; Entry = Entry->Flink)
    {
        Namespace = CONTAINING_RECORD(Entry, LX_IPC_NAMESPACE, Link);
        if (Namespace->Owner == Owner)
            return Namespace;
    }
    if (!Create)
        return NULL;

    Namespace = LxAlloc(sizeof(*Namespace));
    if (Namespace == NULL)
        return NULL;
    Namespace->Owner = Owner;
    for (Type = 0; Type < LX_IPC_TYPES; Type++)
    {
        InitializeListHead(&Namespace->Ids[Type].Objects);
        Namespace->Ids[Type].LastIndex = -1;
        Namespace->Ids[Type].Limit = LxIpcLimits[Type];
    }
    InsertTailList(&LxIpcNamespaces, &Namespace->Link);
    return Namespace;
}

static
PLX_IPC_OBJECT
LxIpcFind(
    _In_opt_ PLX_IPC_NAMESPACE Namespace,
    _In_ ULONG Type,
    _In_ LONG Index,
    _In_ LONG Seq)
{
    PLIST_ENTRY Entry;

    if ((Namespace == NULL) || (Index < 0))
        return NULL;
    for (Entry = Namespace->Ids[Type].Objects.Flink; Entry != &Namespace->Ids[Type].Objects; Entry = Entry->Flink)
    {
        PLX_IPC_OBJECT Object = CONTAINING_RECORD(Entry, LX_IPC_OBJECT, Link);

        if ((Object->Index == Index) && ((Seq < 0) || (Object->Seq == Seq)))
            return Object;
    }
    return NULL;
}

static
PLX_IPC_OBJECT
LxIpcFindId(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Type,
    _In_ LONG Id)
{
    if (Id < 0)
        return NULL;
    return LxIpcFind(LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE), Type, Id & (LX_IPCMNI - 1), Id >> LX_IPCMNI_SHIFT);
}

static
PLX_IPC_OBJECT
LxIpcFindKey(
    _In_ PLX_IPC_NAMESPACE Namespace,
    _In_ ULONG Type,
    _In_ LONG Key)
{
    PLIST_ENTRY Entry;

    for (Entry = Namespace->Ids[Type].Objects.Flink; Entry != &Namespace->Ids[Type].Objects; Entry = Entry->Flink)
    {
        PLX_IPC_OBJECT Object = CONTAINING_RECORD(Entry, LX_IPC_OBJECT, Link);

        if (Object->Key == Key)
            return Object;
    }
    return NULL;
}

static
LONG
LxIpcMaxIndex(
    _In_opt_ PLX_IPC_NAMESPACE Namespace,
    _In_ ULONG Type)
{
    PLIST_ENTRY Entry;
    LONG Max = 0;

    if (Namespace == NULL)
        return 0;
    for (Entry = Namespace->Ids[Type].Objects.Flink; Entry != &Namespace->Ids[Type].Objects; Entry = Entry->Flink)
        Max = max(Max, CONTAINING_RECORD(Entry, LX_IPC_OBJECT, Link)->Index);
    return Max;
}

static
LONG
LxIpcInsert(
    _In_ PLX_IPC_NAMESPACE Namespace,
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_IPC_OBJECT Object,
    _In_ ULONG Type,
    _In_ LONG Key,
    _In_ ULONG Flags)
{
    PLX_IPC_IDS Ids = &Namespace->Ids[Type];
    LONG Max, Index, Tries;

    if (Ids->InUse >= Ids->Limit)
        return -LX_ENOSPC;
    Max = min(max(Ids->InUse * 3 / 2, LX_IPC_MIN_CYCLE), LX_IPCMNI);
    for (Tries = 0, Index = Ids->LastIndex + 1; Tries < Max; Tries++, Index++)
    {
        if (Index >= Max)
            Index = 0;
        if (LxIpcFind(Namespace, Type, Index, -1) == NULL)
            break;
    }
    if (Tries == Max)
        return -LX_ENOSPC;
    if (Index <= Ids->LastIndex)
    {
        if (++Ids->Seq >= LX_IPC_SEQ_MAX)
            Ids->Seq = 0;
    }
    Ids->LastIndex = Index;

    Object->Namespace = Namespace;
    Object->Type = Type;
    Object->Index = Index;
    Object->Seq = (USHORT)Ids->Seq;
    Object->Id = (Ids->Seq << LX_IPCMNI_SHIFT) + Index;
    Object->Key = Key;
    Object->Uid = Object->Cuid = Process->Uid[1];
    Object->Gid = Object->Cgid = Process->Gid[1];
    Object->Mode = Flags & 0777;
    Object->Ctime = LxIpcNow();
    InitializeListHead(&Object->Waiters);
    InsertTailList(&Ids->Objects, &Object->Link);
    Ids->InUse++;
    return Object->Id;
}

static
VOID
LxIpcUnlink(
    _Inout_ PLX_IPC_OBJECT Object)
{
    PLIST_ENTRY Entry;

    RemoveEntryList(&Object->Link);
    Object->Namespace->Ids[Object->Type].InUse--;
    Object->Deleted = TRUE;
    for (Entry = Object->Waiters.Flink; Entry != &Object->Waiters; Entry = Entry->Flink)
        KeSetEvent(&CONTAINING_RECORD(Entry, LX_IPC_WAITER, Link)->Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
LxIpcWake(
    _In_ PLX_IPC_OBJECT Object)
{
    PLIST_ENTRY Entry;

    for (Entry = Object->Waiters.Flink; Entry != &Object->Waiters; Entry = Entry->Flink)
        KeSetEvent(&CONTAINING_RECORD(Entry, LX_IPC_WAITER, Link)->Event, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
LxIpcWait(
    _Inout_ PLX_IPC_OBJECT Object,
    _Inout_ PLX_IPC_WAITER Waiter,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;

    KeClearEvent(&Waiter->Event);
    InsertTailList(&Object->Waiters, &Waiter->Link);
    Object->References++;
    ExReleaseFastMutex(&LxIpcLock);
    Status = LxWaitForEvent(&Waiter->Event, Timeout);
    ExAcquireFastMutex(&LxIpcLock);
    RemoveEntryList(&Waiter->Link);
    Object->References--;
    return Status;
}

static
BOOLEAN
LxIpcInGroup(
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
LxIpcCheck(
    _In_ PLX_PROCESS Process,
    _In_ PLX_IPC_OBJECT Object,
    _In_ ULONG Flags)
{
    ULONG Requested = (Flags >> 6) | (Flags >> 3) | Flags;
    ULONG Granted = Object->Mode;

    if ((Process->Uid[1] == Object->Cuid) || (Process->Uid[1] == Object->Uid))
        Granted >>= 6;
    else if (LxIpcInGroup(Process, Object->Cgid) || LxIpcInGroup(Process, Object->Gid))
        Granted >>= 3;
    if ((Requested & ~Granted & 0007) && !LxCapable(Process, LX_CAP_IPC_OWNER))
        return -LX_EACCES;
    return 0;
}

static
BOOLEAN
LxIpcOwner(
    _In_ PLX_PROCESS Process,
    _In_ PLX_IPC_OBJECT Object)
{
    return (Process->Uid[1] == Object->Uid) || (Process->Uid[1] == Object->Cuid) || LxCapable(Process, LX_CAP_SYS_ADMIN);
}

static
VOID
LxIpcFillPerm(
    _In_ PLX_IPC_OBJECT Object,
    _Out_ PLX_IPC64_PERM Perm)
{
    RtlZeroMemory(Perm, sizeof(*Perm));
    Perm->Key = Object->Key;
    Perm->Uid = Object->Uid;
    Perm->Gid = Object->Gid;
    Perm->Cuid = Object->Cuid;
    Perm->Cgid = Object->Cgid;
    Perm->Mode = Object->Mode;
    Perm->Seq = Object->Seq;
}

static
VOID
LxIpcSetPerm(
    _Inout_ PLX_IPC_OBJECT Object,
    _In_ PLX_IPC64_PERM Perm)
{
    Object->Uid = Perm->Uid;
    Object->Gid = Perm->Gid;
    Object->Mode = (Object->Mode & ~0777) | (Perm->Mode & 0777);
    Object->Ctime = LxIpcNow();
}

static
PLX_IPC_OBJECT
LxIpcStatObject(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Type,
    _In_ LONG Id,
    _In_ BOOLEAN ByIndex,
    _In_ BOOLEAN AnyAccess,
    _Out_ PLONG64 Result)
{
    PLX_IPC_OBJECT Object;

    Object = ByIndex ? LxIpcFind(LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE), Type, Id, -1) :
                       LxIpcFindId(Process, Type, Id);
    if (Object == NULL)
    {
        *Result = -LX_EINVAL;
        return NULL;
    }
    if (!AnyAccess && (LxIpcCheck(Process, Object, 0444) != 0))
    {
        *Result = -LX_EACCES;
        return NULL;
    }
    *Result = ByIndex ? Object->Id : 0;
    return Object;
}

static
PLX_SHM
LxIpcShmBySection(
    _In_ PVOID Section)
{
    PLIST_ENTRY Entry, Inner;

    for (Entry = LxIpcNamespaces.Flink; Entry != &LxIpcNamespaces; Entry = Entry->Flink)
    {
        PLX_IPC_NAMESPACE Namespace = CONTAINING_RECORD(Entry, LX_IPC_NAMESPACE, Link);

        for (Inner = Namespace->Ids[LX_IPC_SHM].Objects.Flink; Inner != &Namespace->Ids[LX_IPC_SHM].Objects;
             Inner = Inner->Flink)
        {
            PLX_SHM Shm = CONTAINING_RECORD(Inner, LX_SHM, Object.Link);

            if (Shm->Section == Section)
                return Shm;
        }
    }
    return NULL;
}

static
PLX_SHM
LxIpcShmUnlink(
    _Inout_ PLX_SHM Shm)
{
    LxIpcUnlink(&Shm->Object);
    Shm->Object.Namespace->ShmPages -= LX_ALIGN_UP(Shm->Size, LX_PAGE_SIZE) / LX_PAGE_SIZE;
    return Shm;
}

static
VOID
LxIpcShmFree(
    _In_opt_ PLX_SHM Shm)
{
    if (Shm == NULL)
        return;
    ObDereferenceObject(Shm->Section);
    LxFree(Shm);
}

VOID
LxIpcShmVmaOpen(
    _In_ PVOID Section)
{
    PLX_THREAD Thread = LxCurrentThread();
    PLX_SHM Shm;

    ExAcquireFastMutex(&LxIpcLock);
    Shm = LxIpcShmBySection(Section);
    if (Shm != NULL)
    {
        Shm->Nattch++;
        Shm->Atime = LxIpcNow();
        if (Thread != NULL)
            Shm->Lpid = Thread->Process->Pid;
    }
    ExReleaseFastMutex(&LxIpcLock);
}

VOID
LxIpcShmVmaClose(
    _In_ PVOID Section)
{
    PLX_THREAD Thread = LxCurrentThread();
    PLX_SHM Shm, Destroy = NULL;

    ExAcquireFastMutex(&LxIpcLock);
    Shm = LxIpcShmBySection(Section);
    if ((Shm != NULL) && (Shm->Nattch != 0))
    {
        Shm->Nattch--;
        Shm->Dtime = LxIpcNow();
        if (Thread != NULL)
            Shm->Lpid = Thread->Process->Pid;
        if ((Shm->Nattch == 0) && (Shm->Object.Mode & LX_SHM_DEST))
            Destroy = LxIpcShmUnlink(Shm);
    }
    ExReleaseFastMutex(&LxIpcLock);
    LxIpcShmFree(Destroy);
}

static
NTSTATUS
LxIpcCreateSection(
    _In_ ULONG64 Size,
    _Out_ PVOID *Section)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER MaximumSize;
    HANDLE Handle;
    NTSTATUS Status;

    MaximumSize.QuadPart = (LONG64)LX_ALIGN_UP(Size, LX_PAGE_SIZE);
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&Handle, SECTION_ALL_ACCESS, &ObjectAttributes, &MaximumSize, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Handle, SECTION_ALL_ACCESS, NULL, KernelMode, Section, NULL);
    ZwClose(Handle);
    return Status;
}

LX_SYSCALL(LxSysShmget)
{
    PLX_PROCESS Process = Call->Process;
    LONG Key = (LONG)Call->Args[0];
    ULONG64 Size = Call->Args[1];
    ULONG Flags = (ULONG)Call->Args[2];
    ULONG64 Pages = LX_ALIGN_UP(Size, LX_PAGE_SIZE) / LX_PAGE_SIZE;
    PLX_IPC_NAMESPACE Namespace;
    PVOID Section = NULL;
    PLX_SHM Shm;
    LONG64 Result;

    for (;;)
    {
        ExAcquireFastMutex(&LxIpcLock);
        Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), TRUE);
        if (Namespace == NULL)
        {
            Result = -LX_ENOMEM;
            break;
        }
        if (Key != LX_IPC_PRIVATE)
        {
            Shm = (PLX_SHM)LxIpcFindKey(Namespace, LX_IPC_SHM, Key);
            if (Shm != NULL)
            {
                if ((Flags & LX_IPC_CREAT) && (Flags & LX_IPC_EXCL))
                    Result = -LX_EEXIST;
                else if (Shm->Size < Size)
                    Result = -LX_EINVAL;
                else if (LxIpcCheck(Process, &Shm->Object, Flags & 0777) != 0)
                    Result = -LX_EACCES;
                else
                    Result = Shm->Object.Id;
                break;
            }
            if (!(Flags & LX_IPC_CREAT))
            {
                Result = -LX_ENOENT;
                break;
            }
        }
        if ((Size < 1) || (Size > LX_SHMMAX))
        {
            Result = -LX_EINVAL;
            break;
        }
        if ((Namespace->ShmPages + Pages < Namespace->ShmPages) || (Namespace->ShmPages + Pages > LX_SHMMAX))
        {
            Result = -LX_ENOSPC;
            break;
        }
        if (Flags & LX_SHM_HUGETLB)
        {
            Result = -LX_ENOMEM;
            break;
        }
        if (Section == NULL)
        {
            ExReleaseFastMutex(&LxIpcLock);
            if (!NT_SUCCESS(LxIpcCreateSection(Size, &Section)))
                return -LX_ENOMEM;
            continue;
        }

        Shm = LxAlloc(sizeof(*Shm));
        if (Shm == NULL)
        {
            Result = -LX_ENOMEM;
            break;
        }
        Result = LxIpcInsert(Namespace, Process, &Shm->Object, LX_IPC_SHM, Key, Flags);
        if (Result < 0)
        {
            LxFree(Shm);
            break;
        }
        Shm->Section = Section;
        Shm->Size = Size;
        Shm->Cpid = Process->Pid;
        Namespace->ShmPages += Pages;
        Section = NULL;
        break;
    }
    ExReleaseFastMutex(&LxIpcLock);
    if (Section != NULL)
        ObDereferenceObject(Section);
    return Result;
}

LX_SYSCALL(LxSysShmat)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    ULONG64 Address = Call->Args[1];
    ULONG Flags = (ULONG)Call->Args[2];
    ULONG Prot = LX_PROT_READ, Access = 0400;
    PLX_SHM Shm, Destroy = NULL;
    PVOID Section;
    ULONG64 Length;
    LONG64 Result;

    if (Id < 0)
        return -LX_EINVAL;
    if (!(Flags & LX_SHM_RDONLY))
    {
        Prot |= LX_PROT_WRITE;
        Access |= 0200;
    }
    if (Flags & LX_SHM_EXEC)
        Access |= 0100;
    if (Address != 0)
    {
        if (Address & (LX_PAGE_SIZE - 1))
        {
            if (!(Flags & LX_SHM_RND))
                return -LX_EINVAL;
            Address = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
            if ((Address == 0) && (Flags & LX_SHM_REMAP))
                return -LX_EINVAL;
        }
    }
    else if (Flags & LX_SHM_REMAP)
    {
        return -LX_EINVAL;
    }

    ExAcquireFastMutex(&LxIpcLock);
    Shm = (PLX_SHM)LxIpcFindId(Process, LX_IPC_SHM, Id);
    if (Shm == NULL)
    {
        ExReleaseFastMutex(&LxIpcLock);
        return -LX_EINVAL;
    }
    if (LxIpcCheck(Process, &Shm->Object, Access) != 0)
    {
        ExReleaseFastMutex(&LxIpcLock);
        return -LX_EACCES;
    }
    Section = Shm->Section;
    ObReferenceObject(Section);
    Length = LX_ALIGN_UP(Shm->Size, LX_PAGE_SIZE);
    Shm->Nattch++;
    ExReleaseFastMutex(&LxIpcLock);

    Result = LxMmMapSection(Process, Section, Address, Length, Prot, (Flags & LX_SHM_REMAP) != 0);

    ExAcquireFastMutex(&LxIpcLock);
    Shm->Nattch--;
    if ((Shm->Nattch == 0) && (Shm->Object.Mode & LX_SHM_DEST))
        Destroy = LxIpcShmUnlink(Shm);
    ExReleaseFastMutex(&LxIpcLock);
    LxIpcShmFree(Destroy);
    ObDereferenceObject(Section);
    return Result;
}

LX_SYSCALL(LxSysShmdt)
{
    if (Call->Args[0] & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    return LxMmDetachSection(Call->Process, Call->Args[0]);
}

LX_SYSCALL(LxSysShmctl)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    LONG Command = (LONG)Call->Args[1];
    ULONG64 Buffer = Call->Args[2];
    PLX_SHM Shm, Destroy = NULL;
    LX_SHMID64_DS Stat;
    LONG64 Result;

    if ((Command < 0) || (Id < 0))
        return -LX_EINVAL;

    switch (Command)
    {
        case LX_IPC_INFO:
        {
            LX_SHMINFO64 Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.Shmmax = LX_SHMMAX;
            Info.Shmmin = 1;
            Info.Shmmni = LX_SHMMNI;
            Info.Shmseg = LX_SHMMNI;
            Info.Shmall = LX_SHMMAX;
            ExAcquireFastMutex(&LxIpcLock);
            Result = LxIpcMaxIndex(LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE), LX_IPC_SHM);
            ExReleaseFastMutex(&LxIpcLock);
            return (LxCopyToUser(Buffer, &Info, sizeof(Info)) != 0) ? -LX_EFAULT : Result;
        }

        case LX_SHM_INFO:
        {
            PLX_IPC_NAMESPACE Namespace;
            LX_SHM_USAGE Info;

            RtlZeroMemory(&Info, sizeof(Info));
            ExAcquireFastMutex(&LxIpcLock);
            Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE);
            if (Namespace != NULL)
            {
                Info.UsedIds = Namespace->Ids[LX_IPC_SHM].InUse;
                Info.ShmTot = Namespace->ShmPages;
                Info.ShmRss = Namespace->ShmPages;
            }
            Result = LxIpcMaxIndex(Namespace, LX_IPC_SHM);
            ExReleaseFastMutex(&LxIpcLock);
            return (LxCopyToUser(Buffer, &Info, sizeof(Info)) != 0) ? -LX_EFAULT : Result;
        }

        case LX_IPC_STAT:
        case LX_SHM_STAT:
        case LX_SHM_STAT_ANY:
            ExAcquireFastMutex(&LxIpcLock);
            Shm = (PLX_SHM)LxIpcStatObject(Process, LX_IPC_SHM, Id, Command != LX_IPC_STAT,
                                           Command == LX_SHM_STAT_ANY, &Result);
            if (Shm != NULL)
            {
                RtlZeroMemory(&Stat, sizeof(Stat));
                LxIpcFillPerm(&Shm->Object, &Stat.Perm);
                Stat.Segsz = Shm->Size;
                Stat.Atime = Shm->Atime;
                Stat.Dtime = Shm->Dtime;
                Stat.Ctime = Shm->Object.Ctime;
                Stat.Cpid = LxGlobalToNr(Process, Shm->Cpid);
                Stat.Lpid = LxGlobalToNr(Process, Shm->Lpid);
                Stat.Nattch = Shm->Nattch;
            }
            ExReleaseFastMutex(&LxIpcLock);
            if ((Shm != NULL) && (LxCopyToUser(Buffer, &Stat, sizeof(Stat)) != 0))
                Result = -LX_EFAULT;
            return Result;

        case LX_IPC_SET:
        case LX_IPC_RMID:
        case LX_SHM_LOCK:
        case LX_SHM_UNLOCK:
            if ((Command == LX_IPC_SET) && (LxCopyFromUser(&Stat, Buffer, sizeof(Stat)) != 0))
                return -LX_EFAULT;
            ExAcquireFastMutex(&LxIpcLock);
            Shm = (PLX_SHM)LxIpcFindId(Process, LX_IPC_SHM, Id);
            Result = 0;
            if (Shm == NULL)
                Result = -LX_EINVAL;
            else if (!LxIpcOwner(Process, &Shm->Object))
                Result = -LX_EPERM;
            else if ((Command == LX_SHM_LOCK) && !LxCapable(Process, LX_CAP_IPC_LOCK) &&
                     (Process->Limits[LX_RLIMIT_MEMLOCK].Cur == 0))
                Result = -LX_EPERM;
            else if (Command == LX_IPC_SET)
                LxIpcSetPerm(&Shm->Object, &Stat.Perm);
            else if (Command == LX_SHM_LOCK)
                Shm->Object.Mode |= LX_SHM_LOCKED;
            else if (Command == LX_SHM_UNLOCK)
                Shm->Object.Mode &= ~LX_SHM_LOCKED;
            else if (Shm->Nattch != 0)
            {
                Shm->Object.Mode |= LX_SHM_DEST;
                Shm->Object.Key = LX_IPC_PRIVATE;
            }
            else
                Destroy = LxIpcShmUnlink(Shm);
            ExReleaseFastMutex(&LxIpcLock);
            LxIpcShmFree(Destroy);
            return Result;

        default:
            return -LX_EINVAL;
    }
}

static
VOID
LxIpcFreeObject(
    _In_ PLX_IPC_OBJECT Object)
{
    if (Object->Type == LX_IPC_MSG)
    {
        PLX_MSG_QUEUE Queue = CONTAINING_RECORD(Object, LX_MSG_QUEUE, Object);

        while (!IsListEmpty(&Queue->Messages))
            LxFree(CONTAINING_RECORD(RemoveHeadList(&Queue->Messages), LX_MSG, Link));
    }
    LxFree(Object);
}

static
VOID
LxIpcRelease(
    _In_ PLX_IPC_OBJECT Object,
    _Inout_ PLX_IPC_OBJECT *Free)
{
    if (Object->Deleted && (Object->References == 0))
        *Free = Object;
}

static
PLX_SEM_ADJUST
LxIpcUndoFind(
    _In_ PLX_PROCESS Process,
    _In_ PLX_SEM_ARRAY Array,
    _In_ BOOLEAN Create)
{
    PLX_SEM_ADJUST Undo;
    PLIST_ENTRY Entry;

    for (Entry = LxIpcUndos.Flink; Entry != &LxIpcUndos; Entry = Entry->Flink)
    {
        Undo = CONTAINING_RECORD(Entry, LX_SEM_ADJUST, Link);
        if ((Undo->Process == Process) && (Undo->Array == Array))
            return Undo;
    }
    if (!Create)
        return NULL;
    Undo = LxAlloc(FIELD_OFFSET(LX_SEM_ADJUST, Adjust) + Array->Count * sizeof(SHORT));
    if (Undo != NULL)
    {
        Undo->Process = Process;
        Undo->Array = Array;
        InsertTailList(&LxIpcUndos, &Undo->Link);
    }
    return Undo;
}

static
VOID
LxIpcUndoClear(
    _In_ PLX_SEM_ARRAY Array,
    _In_ LONG Num,
    _In_ BOOLEAN Free)
{
    PLIST_ENTRY Entry, Next;

    for (Entry = LxIpcUndos.Flink; Entry != &LxIpcUndos; Entry = Next)
    {
        PLX_SEM_ADJUST Undo = CONTAINING_RECORD(Entry, LX_SEM_ADJUST, Link);

        Next = Entry->Flink;
        if (Undo->Array != Array)
            continue;
        if (Free)
        {
            RemoveEntryList(&Undo->Link);
            LxFree(Undo);
        }
        else if (Num >= 0)
            Undo->Adjust[Num] = 0;
        else
            RtlZeroMemory(Undo->Adjust, Array->Count * sizeof(SHORT));
    }
}

PVOID
LxIpcSemUndoList(
    _In_ PLX_PROCESS Process)
{
    PLIST_ENTRY Entry;
    PVOID Found = NULL;

    ExAcquireFastMutex(&LxIpcLock);
    for (Entry = LxIpcUndos.Flink; Entry != &LxIpcUndos; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_SEM_ADJUST, Link)->Process == Process)
        {
            Found = Process;
            break;
        }
    }
    ExReleaseFastMutex(&LxIpcLock);
    return Found;
}

LX_SYSCALL(LxSysSemget)
{
    PLX_PROCESS Process = Call->Process;
    LONG Key = (LONG)Call->Args[0];
    LONG Count = (LONG)Call->Args[1];
    ULONG Flags = (ULONG)Call->Args[2];
    PLX_IPC_NAMESPACE Namespace;
    PLX_SEM_ARRAY Array;
    LONG64 Result;

    if ((Count < 0) || (Count > LX_SEMMSL))
        return -LX_EINVAL;

    ExAcquireFastMutex(&LxIpcLock);
    Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), TRUE);
    if (Namespace == NULL)
    {
        Result = -LX_ENOMEM;
        goto Exit;
    }
    if (Key != LX_IPC_PRIVATE)
    {
        Array = (PLX_SEM_ARRAY)LxIpcFindKey(Namespace, LX_IPC_SEM, Key);
        if (Array != NULL)
        {
            if ((Flags & LX_IPC_CREAT) && (Flags & LX_IPC_EXCL))
                Result = -LX_EEXIST;
            else if ((ULONG)Count > Array->Count)
                Result = -LX_EINVAL;
            else if (LxIpcCheck(Process, &Array->Object, Flags & 0777) != 0)
                Result = -LX_EACCES;
            else
                Result = Array->Object.Id;
            goto Exit;
        }
        if (!(Flags & LX_IPC_CREAT))
        {
            Result = -LX_ENOENT;
            goto Exit;
        }
    }
    if (Count == 0)
    {
        Result = -LX_EINVAL;
        goto Exit;
    }
    if (Namespace->UsedSems + Count > LX_SEMMNS)
    {
        Result = -LX_ENOSPC;
        goto Exit;
    }
    Array = LxAlloc(FIELD_OFFSET(LX_SEM_ARRAY, Sems) + Count * sizeof(LX_SEM));
    if (Array == NULL)
    {
        Result = -LX_ENOMEM;
        goto Exit;
    }
    Result = LxIpcInsert(Namespace, Process, &Array->Object, LX_IPC_SEM, Key, Flags);
    if (Result < 0)
    {
        LxFree(Array);
        goto Exit;
    }
    Array->Count = (ULONG)Count;
    Namespace->UsedSems += Count;

Exit:
    ExReleaseFastMutex(&LxIpcLock);
    return Result;
}

static
LONG
LxIpcSemTry(
    _Inout_ PLX_SEM_ARRAY Array,
    _In_reads_(Count) PLX_SEMBUF Sops,
    _In_ ULONG Count,
    _Inout_opt_ PLX_SEM_ADJUST Undo,
    _In_ ULONG Pid,
    _Out_ PULONG Blocked)
{
    LONG Result = 0;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PLX_SEM Sem = &Array->Sems[Sops[Index].Num];
        LONG Value = Sem->Value;

        if ((Sops[Index].Op == 0) && (Value != 0))
        {
            Result = 1;
            break;
        }
        Value += Sops[Index].Op;
        if (Value < 0)
        {
            Result = 1;
            break;
        }
        if (Value > LX_SEMVMX)
        {
            Result = -LX_ERANGE;
            break;
        }
        if ((Sops[Index].Flags & LX_SEM_UNDO) && (Undo != NULL))
        {
            LONG Adjust = Undo->Adjust[Sops[Index].Num] - Sops[Index].Op;

            if ((Adjust < -LX_SEMAEM - 1) || (Adjust > LX_SEMAEM))
            {
                Result = -LX_ERANGE;
                break;
            }
            Undo->Adjust[Sops[Index].Num] = (SHORT)Adjust;
        }
        Sem->Value = (USHORT)Value;
    }

    if (Result == 0)
    {
        for (Index = 0; Index < Count; Index++)
            Array->Sems[Sops[Index].Num].Pid = Pid;
        return 0;
    }

    *Blocked = Index;
    while (Index-- != 0)
    {
        Array->Sems[Sops[Index].Num].Value = (USHORT)(Array->Sems[Sops[Index].Num].Value - Sops[Index].Op);
        if ((Sops[Index].Flags & LX_SEM_UNDO) && (Undo != NULL))
            Undo->Adjust[Sops[Index].Num] = (SHORT)(Undo->Adjust[Sops[Index].Num] + Sops[Index].Op);
    }
    return Result;
}

static
LONG64
LxIpcSemop(
    _In_ PLX_PROCESS Process,
    _In_ LONG Id,
    _In_ ULONG64 Pointer,
    _In_ ULONG64 Count,
    _In_ ULONG64 TimeoutPointer)
{
    PLX_IPC_OBJECT Free = NULL;
    PLX_SEM_ADJUST Undo = NULL;
    PLX_IPC_WAITER Waiter = NULL;
    PLX_SEM_ARRAY Array;
    PLX_SEMBUF Sops;
    LARGE_INTEGER Remaining;
    PLARGE_INTEGER Timeout = NULL;
    LONG64 Deadline = 0, Result;
    BOOLEAN HasUndo = FALSE, Alter = FALSE;
    ULONG Index, Max = 0, Blocked = 0;

    if ((Count < 1) || (Id < 0))
        return -LX_EINVAL;
    if (Count > LX_SEMOPM)
        return -LX_E2BIG;
    Sops = LxAlloc((SIZE_T)Count * sizeof(LX_SEMBUF));
    if (Sops == NULL)
        return -LX_ENOMEM;
    Result = LxCopyFromUser(Sops, Pointer, (SIZE_T)Count * sizeof(LX_SEMBUF));
    if (Result != 0)
        goto Exit;
    if (TimeoutPointer != 0)
    {
        LX_TIMESPEC Timespec;

        Result = LxCopyFromUser(&Timespec, TimeoutPointer, sizeof(Timespec));
        if (Result != 0)
            goto Exit;
        if ((Timespec.Seconds < 0) || (Timespec.Nanoseconds < 0) || (Timespec.Nanoseconds >= 1000000000))
        {
            Result = -LX_EINVAL;
            goto Exit;
        }
        Deadline = (LONG64)KeQueryInterruptTime() + (LxTimespecToNs(&Timespec) + 99) / 100;
        Timeout = &Remaining;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Max = max(Max, (ULONG)Sops[Index].Num);
        HasUndo |= (Sops[Index].Flags & LX_SEM_UNDO) != 0;
        Alter |= (Sops[Index].Op != 0);
    }
    Waiter = LxAlloc(sizeof(*Waiter));
    if (Waiter == NULL)
    {
        Result = -LX_ENOMEM;
        goto Exit;
    }
    KeInitializeEvent(&Waiter->Event, NotificationEvent, FALSE);

    ExAcquireFastMutex(&LxIpcLock);
    Array = (PLX_SEM_ARRAY)LxIpcFindId(Process, LX_IPC_SEM, Id);
    Result = 0;
    if (Array == NULL)
        Result = -LX_EINVAL;
    else if (Max >= Array->Count)
        Result = -LX_EFBIG;
    else if (LxIpcCheck(Process, &Array->Object, Alter ? 0222 : 0444) != 0)
        Result = -LX_EACCES;
    else if (HasUndo && ((Undo = LxIpcUndoFind(Process, Array, TRUE)) == NULL))
        Result = -LX_ENOMEM;

    while ((Result == 0) && (Array != NULL))
    {
        NTSTATUS Status;

        Result = LxIpcSemTry(Array, Sops, (ULONG)Count, Undo, Process->Pid, &Blocked);
        if (Result <= 0)
        {
            if (Result == 0)
            {
                Array->Otime = LxIpcNow();
                if (Alter)
                    LxIpcWake(&Array->Object);
            }
            break;
        }
        if (Sops[Blocked].Flags & LX_IPC_NOWAIT)
        {
            Result = -LX_EAGAIN;
            break;
        }

        Waiter->Num = Sops[Blocked].Num;
        Waiter->Zero = (Sops[Blocked].Op == 0);
        if (Timeout != NULL)
        {
            LONG64 Now = (LONG64)KeQueryInterruptTime();

            Remaining.QuadPart = (Now >= Deadline) ? 0 : -(Deadline - Now);
        }
        Status = LxIpcWait(&Array->Object, Waiter, Timeout);
        if (Array->Object.Deleted)
        {
            Result = -LX_EIDRM;
            LxIpcRelease(&Array->Object, &Free);
            break;
        }
        if (Status == STATUS_TIMEOUT)
        {
            Result = -LX_EAGAIN;
            break;
        }
        if (Status != STATUS_SUCCESS)
        {
            Result = -LX_EINTR;
            break;
        }
        Result = 0;
    }
    ExReleaseFastMutex(&LxIpcLock);
    if (Free != NULL)
        LxIpcFreeObject(Free);

Exit:
    if (Waiter != NULL)
        LxFree(Waiter);
    LxFree(Sops);
    return Result;
}

LX_SYSCALL(LxSysSemop)
{
    return LxIpcSemop(Call->Process, (LONG)Call->Args[0], Call->Args[1], Call->Args[2], 0);
}

LX_SYSCALL(LxSysSemtimedop)
{
    return LxIpcSemop(Call->Process, (LONG)Call->Args[0], Call->Args[1], Call->Args[2], Call->Args[3]);
}

static
ULONG
LxIpcSemCount(
    _In_ PLX_SEM_ARRAY Array,
    _In_ LONG Num,
    _In_ BOOLEAN Zero)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    for (Entry = Array->Object.Waiters.Flink; Entry != &Array->Object.Waiters; Entry = Entry->Flink)
    {
        PLX_IPC_WAITER Waiter = CONTAINING_RECORD(Entry, LX_IPC_WAITER, Link);

        if ((Waiter->Num == Num) && (Waiter->Zero == Zero))
            Count++;
    }
    return Count;
}

LX_SYSCALL(LxSysSemctl)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    LONG Num = (LONG)Call->Args[1];
    LONG Command = (LONG)Call->Args[2];
    ULONG64 Argument = Call->Args[3];
    PLX_IPC_OBJECT Free = NULL;
    PLX_SEM_ARRAY Array;
    LX_SEMID64_DS Stat;
    PUSHORT Values = NULL;
    ULONG Count = 0, Index;
    LONG64 Result;

    if (Id < 0)
        return -LX_EINVAL;

    switch (Command)
    {
        case LX_IPC_INFO:
        case LX_SEM_INFO:
        {
            PLX_IPC_NAMESPACE Namespace;
            LX_SEMINFO Info;

            Info.Semmni = LX_SEMMNI;
            Info.Semmns = LX_SEMMNS;
            Info.Semmsl = LX_SEMMSL;
            Info.Semopm = LX_SEMOPM;
            Info.Semvmx = LX_SEMVMX;
            Info.Semmnu = LX_SEMMNS;
            Info.Semmap = LX_SEMMNS;
            Info.Semume = LX_SEMOPM;
            Info.Semusz = LX_SEMUSZ;
            Info.Semaem = LX_SEMAEM;
            ExAcquireFastMutex(&LxIpcLock);
            Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE);
            if (Command == LX_SEM_INFO)
            {
                Info.Semusz = (Namespace != NULL) ? Namespace->Ids[LX_IPC_SEM].InUse : 0;
                Info.Semaem = (Namespace != NULL) ? (LONG)Namespace->UsedSems : 0;
            }
            Result = LxIpcMaxIndex(Namespace, LX_IPC_SEM);
            ExReleaseFastMutex(&LxIpcLock);
            return (LxCopyToUser(Argument, &Info, sizeof(Info)) != 0) ? -LX_EFAULT : Result;
        }

        case LX_IPC_STAT:
        case LX_SEM_STAT:
        case LX_SEM_STAT_ANY:
            ExAcquireFastMutex(&LxIpcLock);
            Array = (PLX_SEM_ARRAY)LxIpcStatObject(Process, LX_IPC_SEM, Id, Command != LX_IPC_STAT,
                                                   Command == LX_SEM_STAT_ANY, &Result);
            if (Array != NULL)
            {
                RtlZeroMemory(&Stat, sizeof(Stat));
                LxIpcFillPerm(&Array->Object, &Stat.Perm);
                Stat.Otime = Array->Otime;
                Stat.Ctime = Array->Object.Ctime;
                Stat.Nsems = Array->Count;
            }
            ExReleaseFastMutex(&LxIpcLock);
            if ((Array != NULL) && (LxCopyToUser(Argument, &Stat, sizeof(Stat)) != 0))
                Result = -LX_EFAULT;
            return Result;

        case LX_GETVAL:
        case LX_GETPID:
        case LX_GETNCNT:
        case LX_GETZCNT:
        case LX_GETALL:
            ExAcquireFastMutex(&LxIpcLock);
            Array = (PLX_SEM_ARRAY)LxIpcFindId(Process, LX_IPC_SEM, Id);
            if (Array == NULL)
                Result = -LX_EINVAL;
            else if (LxIpcCheck(Process, &Array->Object, 0444) != 0)
                Result = -LX_EACCES;
            else if (Command == LX_GETALL)
            {
                Count = Array->Count;
                Values = LxAlloc(Count * sizeof(USHORT));
                Result = (Values != NULL) ? 0 : -LX_ENOMEM;
                for (Index = 0; (Values != NULL) && (Index < Count); Index++)
                    Values[Index] = Array->Sems[Index].Value;
            }
            else if ((Num < 0) || ((ULONG)Num >= Array->Count))
                Result = -LX_EINVAL;
            else if (Command == LX_GETVAL)
                Result = Array->Sems[Num].Value;
            else if (Command == LX_GETPID)
                Result = LxGlobalToNr(Process, Array->Sems[Num].Pid);
            else
                Result = LxIpcSemCount(Array, Num, Command == LX_GETZCNT);
            ExReleaseFastMutex(&LxIpcLock);
            if (Values != NULL)
            {
                if (LxCopyToUser(Argument, Values, Count * sizeof(USHORT)) != 0)
                    Result = -LX_EFAULT;
                LxFree(Values);
            }
            return Result;

        case LX_SETALL:
            ExAcquireFastMutex(&LxIpcLock);
            Array = (PLX_SEM_ARRAY)LxIpcFindId(Process, LX_IPC_SEM, Id);
            Result = (Array == NULL) ? -LX_EINVAL : LxIpcCheck(Process, &Array->Object, 0222);
            Count = (Array != NULL) ? Array->Count : 0;
            ExReleaseFastMutex(&LxIpcLock);
            if (Result != 0)
                return Result;
            Values = LxAlloc(Count * sizeof(USHORT));
            if (Values == NULL)
                return -LX_ENOMEM;
            Result = LxCopyFromUser(Values, Argument, Count * sizeof(USHORT));
            for (Index = 0; (Result == 0) && (Index < Count); Index++)
            {
                if (Values[Index] > LX_SEMVMX)
                    Result = -LX_ERANGE;
            }
            if (Result == 0)
            {
                ExAcquireFastMutex(&LxIpcLock);
                if (LxIpcFindId(Process, LX_IPC_SEM, Id) != &Array->Object)
                {
                    Result = -LX_EIDRM;
                }
                else
                {
                    for (Index = 0; Index < Count; Index++)
                    {
                        Array->Sems[Index].Value = Values[Index];
                        Array->Sems[Index].Pid = Process->Pid;
                    }
                    LxIpcUndoClear(Array, -1, FALSE);
                    Array->Object.Ctime = LxIpcNow();
                    LxIpcWake(&Array->Object);
                }
                ExReleaseFastMutex(&LxIpcLock);
            }
            LxFree(Values);
            return Result;

        case LX_SETVAL:
            if (((LONG)Argument > LX_SEMVMX) || ((LONG)Argument < 0))
                return -LX_ERANGE;
            ExAcquireFastMutex(&LxIpcLock);
            Array = (PLX_SEM_ARRAY)LxIpcFindId(Process, LX_IPC_SEM, Id);
            Result = 0;
            if (Array == NULL)
                Result = -LX_EINVAL;
            else if ((Num < 0) || ((ULONG)Num >= Array->Count))
                Result = -LX_EINVAL;
            else if (LxIpcCheck(Process, &Array->Object, 0222) != 0)
                Result = -LX_EACCES;
            else
            {
                Array->Sems[Num].Value = (USHORT)Argument;
                Array->Sems[Num].Pid = Process->Pid;
                LxIpcUndoClear(Array, Num, FALSE);
                Array->Object.Ctime = LxIpcNow();
                LxIpcWake(&Array->Object);
            }
            ExReleaseFastMutex(&LxIpcLock);
            return Result;

        case LX_IPC_SET:
        case LX_IPC_RMID:
            if ((Command == LX_IPC_SET) && (LxCopyFromUser(&Stat, Argument, sizeof(Stat)) != 0))
                return -LX_EFAULT;
            ExAcquireFastMutex(&LxIpcLock);
            Array = (PLX_SEM_ARRAY)LxIpcFindId(Process, LX_IPC_SEM, Id);
            Result = 0;
            if (Array == NULL)
                Result = -LX_EINVAL;
            else if (!LxIpcOwner(Process, &Array->Object))
                Result = -LX_EPERM;
            else if (Command == LX_IPC_SET)
                LxIpcSetPerm(&Array->Object, &Stat.Perm);
            else
            {
                LxIpcUndoClear(Array, -1, TRUE);
                LxIpcUnlink(&Array->Object);
                Array->Object.Namespace->UsedSems -= Array->Count;
                LxIpcRelease(&Array->Object, &Free);
            }
            ExReleaseFastMutex(&LxIpcLock);
            if (Free != NULL)
                LxIpcFreeObject(Free);
            return Result;

        default:
            return -LX_EINVAL;
    }
}

LX_SYSCALL(LxSysMsgget)
{
    PLX_PROCESS Process = Call->Process;
    LONG Key = (LONG)Call->Args[0];
    ULONG Flags = (ULONG)Call->Args[1];
    PLX_IPC_NAMESPACE Namespace;
    PLX_MSG_QUEUE Queue;
    LONG64 Result;

    ExAcquireFastMutex(&LxIpcLock);
    Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), TRUE);
    if (Namespace == NULL)
    {
        Result = -LX_ENOMEM;
        goto Exit;
    }
    if (Key != LX_IPC_PRIVATE)
    {
        Queue = (PLX_MSG_QUEUE)LxIpcFindKey(Namespace, LX_IPC_MSG, Key);
        if (Queue != NULL)
        {
            if ((Flags & LX_IPC_CREAT) && (Flags & LX_IPC_EXCL))
                Result = -LX_EEXIST;
            else if (LxIpcCheck(Process, &Queue->Object, Flags & 0777) != 0)
                Result = -LX_EACCES;
            else
                Result = Queue->Object.Id;
            goto Exit;
        }
        if (!(Flags & LX_IPC_CREAT))
        {
            Result = -LX_ENOENT;
            goto Exit;
        }
    }
    Queue = LxAlloc(sizeof(*Queue));
    if (Queue == NULL)
    {
        Result = -LX_ENOMEM;
        goto Exit;
    }
    Result = LxIpcInsert(Namespace, Process, &Queue->Object, LX_IPC_MSG, Key, Flags);
    if (Result < 0)
    {
        LxFree(Queue);
        goto Exit;
    }
    InitializeListHead(&Queue->Messages);
    Queue->MaxBytes = LX_MSGMNB;

Exit:
    ExReleaseFastMutex(&LxIpcLock);
    return Result;
}

LX_SYSCALL(LxSysMsgsnd)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    ULONG64 Pointer = Call->Args[1];
    ULONG64 Size = Call->Args[2];
    ULONG Flags = (ULONG)Call->Args[3];
    PLX_IPC_OBJECT Free = NULL;
    PLX_IPC_WAITER Waiter;
    PLX_MSG_QUEUE Queue;
    PLX_MSG Message;
    LONG64 Result;

    if ((Size > LX_MSGMAX) || (Id < 0))
        return -LX_EINVAL;
    Message = LxAlloc(FIELD_OFFSET(LX_MSG, Data) + (SIZE_T)Size);
    if (Message == NULL)
        return -LX_ENOMEM;
    Result = LxCopyFromUser(&Message->Type, Pointer, sizeof(Message->Type));
    if ((Result == 0) && (Message->Type < 1))
        Result = -LX_EINVAL;
    if (Result == 0)
        Result = LxCopyFromUser(Message->Data, Pointer + sizeof(LONG64), (SIZE_T)Size);
    Waiter = (Result == 0) ? LxAlloc(sizeof(*Waiter)) : NULL;
    if ((Result == 0) && (Waiter == NULL))
        Result = -LX_ENOMEM;
    if (Result != 0)
    {
        LxFree(Message);
        return Result;
    }
    Message->Size = (ULONG)Size;
    KeInitializeEvent(&Waiter->Event, NotificationEvent, FALSE);
    Waiter->Num = -1;

    ExAcquireFastMutex(&LxIpcLock);
    for (;;)
    {
        NTSTATUS Status;

        Queue = (PLX_MSG_QUEUE)LxIpcFindId(Process, LX_IPC_MSG, Id);
        if (Queue == NULL)
        {
            Result = -LX_EINVAL;
            break;
        }
        if (LxIpcCheck(Process, &Queue->Object, 0222) != 0)
        {
            Result = -LX_EACCES;
            break;
        }
        if ((Size + Queue->Bytes <= Queue->MaxBytes) && (1 + Queue->Count <= Queue->MaxBytes))
        {
            InsertTailList(&Queue->Messages, &Message->Link);
            Queue->Bytes += Size;
            Queue->Count++;
            Queue->Lspid = Process->Pid;
            Queue->Stime = LxIpcNow();
            Queue->Object.Namespace->MsgHeaders++;
            Queue->Object.Namespace->MsgBytes += Size;
            LxIpcWake(&Queue->Object);
            Message = NULL;
            Result = 0;
            break;
        }
        if (Flags & LX_IPC_NOWAIT)
        {
            Result = -LX_EAGAIN;
            break;
        }
        Status = LxIpcWait(&Queue->Object, Waiter, NULL);
        if (Queue->Object.Deleted)
        {
            Result = -LX_EIDRM;
            LxIpcRelease(&Queue->Object, &Free);
            break;
        }
        if (Status != STATUS_SUCCESS)
        {
            Result = -LX_EINTR;
            break;
        }
    }
    ExReleaseFastMutex(&LxIpcLock);
    if (Free != NULL)
        LxIpcFreeObject(Free);
    if (Message != NULL)
        LxFree(Message);
    LxFree(Waiter);
    return Result;
}

static
PLX_MSG
LxIpcMsgFind(
    _In_ PLX_MSG_QUEUE Queue,
    _In_ LONG64 Type,
    _In_ ULONG Flags)
{
    PLX_MSG Found = NULL;
    PLIST_ENTRY Entry;
    LONG64 Index = 0;
    LONG64 Limit = (Type == MINLONG64) ? MAXLONG64 : -Type;

    for (Entry = Queue->Messages.Flink; Entry != &Queue->Messages; Entry = Entry->Flink, Index++)
    {
        PLX_MSG Message = CONTAINING_RECORD(Entry, LX_MSG, Link);

        if (Flags & LX_MSG_COPY)
        {
            if (Index == Type)
                return Message;
        }
        else if (Type == 0)
        {
            return Message;
        }
        else if (Type > 0)
        {
            if ((Flags & LX_MSG_EXCEPT) ? (Message->Type != Type) : (Message->Type == Type))
                return Message;
        }
        else if (Message->Type <= Limit)
        {
            Found = Message;
            Limit = Message->Type - 1;
            if (Message->Type == 1)
                break;
        }
    }
    return Found;
}

LX_SYSCALL(LxSysMsgrcv)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    ULONG64 Pointer = Call->Args[1];
    LONG64 Size = (LONG64)Call->Args[2];
    LONG64 Type = (LONG64)Call->Args[3];
    ULONG Flags = (ULONG)Call->Args[4];
    PLX_IPC_OBJECT Free = NULL;
    PLX_IPC_WAITER Waiter;
    PLX_MSG_QUEUE Queue;
    PLX_MSG Message = NULL;
    ULONG Length;
    LONG64 Result;

    if ((Id < 0) || (Size < 0))
        return -LX_EINVAL;
    if ((Flags & LX_MSG_COPY) && ((Flags & LX_MSG_EXCEPT) || !(Flags & LX_IPC_NOWAIT)))
        return -LX_EINVAL;
    Waiter = LxAlloc(sizeof(*Waiter));
    if (Waiter == NULL)
        return -LX_ENOMEM;
    KeInitializeEvent(&Waiter->Event, NotificationEvent, FALSE);
    Waiter->Num = -1;

    ExAcquireFastMutex(&LxIpcLock);
    for (;;)
    {
        PLX_MSG Found;
        NTSTATUS Status;

        Queue = (PLX_MSG_QUEUE)LxIpcFindId(Process, LX_IPC_MSG, Id);
        if (Queue == NULL)
        {
            Result = -LX_EINVAL;
            break;
        }
        if (LxIpcCheck(Process, &Queue->Object, 0444) != 0)
        {
            Result = -LX_EACCES;
            break;
        }
        Found = LxIpcMsgFind(Queue, Type, Flags);
        if (Found != NULL)
        {
            if (((ULONG64)Size < Found->Size) && !(Flags & LX_MSG_NOERROR))
            {
                Result = -LX_E2BIG;
                break;
            }
            if (Flags & LX_MSG_COPY)
            {
                Message = LxAlloc(FIELD_OFFSET(LX_MSG, Data) + Found->Size);
                if (Message == NULL)
                {
                    Result = -LX_ENOMEM;
                    break;
                }
                RtlCopyMemory(Message, Found, FIELD_OFFSET(LX_MSG, Data) + Found->Size);
            }
            else
            {
                Message = Found;
                RemoveEntryList(&Message->Link);
                Queue->Bytes -= Message->Size;
                Queue->Count--;
                Queue->Lrpid = Process->Pid;
                Queue->Rtime = LxIpcNow();
                Queue->Object.Namespace->MsgHeaders--;
                Queue->Object.Namespace->MsgBytes -= Message->Size;
                LxIpcWake(&Queue->Object);
            }
            Result = 0;
            break;
        }
        if (Flags & LX_IPC_NOWAIT)
        {
            Result = -LX_ENOMSG;
            break;
        }
        Status = LxIpcWait(&Queue->Object, Waiter, NULL);
        if (Queue->Object.Deleted)
        {
            Result = -LX_EIDRM;
            LxIpcRelease(&Queue->Object, &Free);
            break;
        }
        if (Status != STATUS_SUCCESS)
        {
            Result = -LX_EINTR;
            break;
        }
    }
    ExReleaseFastMutex(&LxIpcLock);
    if (Free != NULL)
        LxIpcFreeObject(Free);
    LxFree(Waiter);

    if (Message != NULL)
    {
        Length = (ULONG)min((ULONG64)Size, (ULONG64)Message->Size);
        Result = LxCopyToUser(Pointer, &Message->Type, sizeof(Message->Type));
        if (Result == 0)
            Result = LxCopyToUser(Pointer + sizeof(LONG64), Message->Data, Length);
        if (Result == 0)
            Result = Length;
        LxFree(Message);
    }
    return Result;
}

LX_SYSCALL(LxSysMsgctl)
{
    PLX_PROCESS Process = Call->Process;
    LONG Id = (LONG)Call->Args[0];
    LONG Command = (LONG)Call->Args[1];
    ULONG64 Buffer = Call->Args[2];
    PLX_IPC_OBJECT Free = NULL;
    PLX_MSG_QUEUE Queue;
    LX_MSQID64_DS Stat;
    LONG64 Result;

    if ((Id < 0) || (Command < 0))
        return -LX_EINVAL;

    switch (Command)
    {
        case LX_IPC_INFO:
        case LX_MSG_INFO:
        {
            PLX_IPC_NAMESPACE Namespace;
            LX_MSGINFO Info;

            RtlZeroMemory(&Info, sizeof(Info));
            Info.Msgmni = LX_MSGMNI;
            Info.Msgmax = LX_MSGMAX;
            Info.Msgmnb = LX_MSGMNB;
            Info.Msgssz = LX_MSGSSZ;
            Info.Msgseg = LX_MSGSEG;
            Info.Msgmap = LX_MSGMNB;
            Info.Msgpool = LX_MSGPOOL;
            Info.Msgtql = LX_MSGMNB;
            ExAcquireFastMutex(&LxIpcLock);
            Namespace = LxIpcNamespace(LxProcessNs(Process, LxNsIpc), FALSE);
            if (Command == LX_MSG_INFO)
            {
                Info.Msgpool = (Namespace != NULL) ? Namespace->Ids[LX_IPC_MSG].InUse : 0;
                Info.Msgmap = (Namespace != NULL) ? (LONG)Namespace->MsgHeaders : 0;
                Info.Msgtql = (Namespace != NULL) ? (LONG)Namespace->MsgBytes : 0;
            }
            Result = LxIpcMaxIndex(Namespace, LX_IPC_MSG);
            ExReleaseFastMutex(&LxIpcLock);
            return (LxCopyToUser(Buffer, &Info, sizeof(Info)) != 0) ? -LX_EFAULT : Result;
        }

        case LX_IPC_STAT:
        case LX_MSG_STAT:
        case LX_MSG_STAT_ANY:
            ExAcquireFastMutex(&LxIpcLock);
            Queue = (PLX_MSG_QUEUE)LxIpcStatObject(Process, LX_IPC_MSG, Id, Command != LX_IPC_STAT,
                                                   Command == LX_MSG_STAT_ANY, &Result);
            if (Queue != NULL)
            {
                RtlZeroMemory(&Stat, sizeof(Stat));
                LxIpcFillPerm(&Queue->Object, &Stat.Perm);
                Stat.Stime = Queue->Stime;
                Stat.Rtime = Queue->Rtime;
                Stat.Ctime = Queue->Object.Ctime;
                Stat.Cbytes = Queue->Bytes;
                Stat.Qnum = Queue->Count;
                Stat.Qbytes = Queue->MaxBytes;
                Stat.Lspid = LxGlobalToNr(Process, Queue->Lspid);
                Stat.Lrpid = LxGlobalToNr(Process, Queue->Lrpid);
            }
            ExReleaseFastMutex(&LxIpcLock);
            if ((Queue != NULL) && (LxCopyToUser(Buffer, &Stat, sizeof(Stat)) != 0))
                Result = -LX_EFAULT;
            return Result;

        case LX_IPC_SET:
        case LX_IPC_RMID:
            if ((Command == LX_IPC_SET) && (LxCopyFromUser(&Stat, Buffer, sizeof(Stat)) != 0))
                return -LX_EFAULT;
            ExAcquireFastMutex(&LxIpcLock);
            Queue = (PLX_MSG_QUEUE)LxIpcFindId(Process, LX_IPC_MSG, Id);
            Result = 0;
            if (Queue == NULL)
                Result = -LX_EINVAL;
            else if (!LxIpcOwner(Process, &Queue->Object))
                Result = -LX_EPERM;
            else if (Command == LX_IPC_SET)
            {
                if ((Stat.Qbytes > LX_MSGMNB) && !LxCapable(Process, LX_CAP_SYS_RESOURCE))
                {
                    Result = -LX_EPERM;
                }
                else
                {
                    Queue->MaxBytes = Stat.Qbytes;
                    LxIpcSetPerm(&Queue->Object, &Stat.Perm);
                    LxIpcWake(&Queue->Object);
                }
            }
            else
            {
                Queue->Object.Namespace->MsgHeaders -= Queue->Count;
                Queue->Object.Namespace->MsgBytes -= Queue->Bytes;
                LxIpcUnlink(&Queue->Object);
                LxIpcRelease(&Queue->Object, &Free);
            }
            ExReleaseFastMutex(&LxIpcLock);
            if (Free != NULL)
                LxIpcFreeObject(Free);
            return Result;

        default:
            return -LX_EINVAL;
    }
}

VOID
LxIpcProcessExit(
    _In_ PLX_PROCESS Process)
{
    PLIST_ENTRY Entry, Next;

    ExAcquireFastMutex(&LxIpcLock);
    for (Entry = LxIpcUndos.Flink; Entry != &LxIpcUndos; Entry = Next)
    {
        PLX_SEM_ADJUST Undo = CONTAINING_RECORD(Entry, LX_SEM_ADJUST, Link);
        PLX_SEM_ARRAY Array = Undo->Array;
        ULONG Index;

        Next = Entry->Flink;
        if (Undo->Process != Process)
            continue;
        for (Index = 0; Index < Array->Count; Index++)
        {
            LONG Value;

            if (Undo->Adjust[Index] == 0)
                continue;
            Value = (LONG)Array->Sems[Index].Value + Undo->Adjust[Index];
            Array->Sems[Index].Value = (USHORT)min(max(Value, 0), LX_SEMVMX);
            Array->Sems[Index].Pid = Process->Pid;
        }
        LxIpcWake(&Array->Object);
        RemoveEntryList(&Undo->Link);
        LxFree(Undo);
    }
    ExReleaseFastMutex(&LxIpcLock);
}

VOID
LxIpcNamespaceCleanup(
    _In_ PLX_NS Ns)
{
    PLX_IPC_NAMESPACE Namespace;
    ULONG Type;

    ExAcquireFastMutex(&LxIpcLock);
    Namespace = LxIpcNamespace(Ns, FALSE);
    if (Namespace != NULL)
        RemoveEntryList(&Namespace->Link);
    ExReleaseFastMutex(&LxIpcLock);
    if (Namespace == NULL)
        return;

    for (Type = 0; Type < LX_IPC_TYPES; Type++)
    {
        while (!IsListEmpty(&Namespace->Ids[Type].Objects))
        {
            PLX_IPC_OBJECT Object = CONTAINING_RECORD(RemoveHeadList(&Namespace->Ids[Type].Objects), LX_IPC_OBJECT, Link);

            if (Type == LX_IPC_SHM)
            {
                LxIpcShmFree(CONTAINING_RECORD(Object, LX_SHM, Object));
                continue;
            }
            if (Type == LX_IPC_SEM)
            {
                ExAcquireFastMutex(&LxIpcLock);
                LxIpcUndoClear(CONTAINING_RECORD(Object, LX_SEM_ARRAY, Object), -1, TRUE);
                ExReleaseFastMutex(&LxIpcLock);
            }
            LxIpcFreeObject(Object);
        }
    }
    LxFree(Namespace);
}
