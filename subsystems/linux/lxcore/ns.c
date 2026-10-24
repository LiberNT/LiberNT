/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Namespaces, per-thread file system state and pid numbering
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

#define LX_PROC_DYNAMIC_FIRST   0xF0000000ULL
#define LX_PID_MAX              4194304

typedef struct _LX_UCOUNT
{
    LIST_ENTRY Link;
    PLX_NS Ns;
    ULONG Uid;
    LONG Count;
} LX_UCOUNT, *PLX_UCOUNT;

static LIST_ENTRY LxNsList;
static FAST_MUTEX LxNsLock;
static LIST_ENTRY LxUcountList;
static FAST_MUTEX LxUserNsStateLock;
static volatile LONG64 LxNsNextInum = LX_PROC_DYNAMIC_FIRST - 1;

static const ULONG64 LxNsCloneFlags[LxNsCount] =
{
    LX_CLONE_NEWNS,
    LX_CLONE_NEWUTS,
    LX_CLONE_NEWIPC,
    LX_CLONE_NEWPID,
    LX_CLONE_NEWNET,
    LX_CLONE_NEWCGROUP,
    LX_CLONE_NEWUSER,
    LX_CLONE_NEWTIME
};

static const ULONG64 LxNsInitInums[LxNsCount] =
{
    0,
    0xEFFFFFFEULL,
    0xEFFFFFFFULL,
    0xEFFFFFFCULL,
    0,
    0xEFFFFFFBULL,
    0xEFFFFFFDULL,
    0xEFFFFFFAULL
};

static const PCSTR LxNsNames[LxNsCount] =
{
    "mnt", "uts", "ipc", "pid", "net", "cgroup", "user", "time"
};

VOID
LxNsInitialize(VOID)
{
    InitializeListHead(&LxNsList);
    ExInitializeFastMutex(&LxNsLock);
    InitializeListHead(&LxUcountList);
    ExInitializeFastMutex(&LxUserNsStateLock);
}

PCSTR
LxNsName(
    _In_ LX_NS_KIND Kind)
{
    return (Kind < LxNsCount) ? LxNsNames[Kind] : "unknown";
}

PLX_NS
LxNsCreate(
    _In_ PLX_INSTANCE Instance,
    _In_ LX_NS_KIND Kind,
    _In_opt_ PLX_NS From,
    _In_opt_ PLX_NS Owner)
{
    PLX_NS Ns;

    if ((Kind == LxNsPid) && (From != NULL) && (From->Level + 1 >= LX_PIDNS_MAX_LEVEL))
        return NULL;
    if ((Kind == LxNsUser) && (From != NULL) && (From->Level > LX_USERNS_MAX_PARENT_LEVEL))
        return NULL;

    Ns = LxAlloc(sizeof(*Ns));
    if (Ns == NULL)
        return NULL;

    Ns->RefCount = 1;
    Ns->Kind = Kind;
    Ns->Instance = Instance;
    if ((From == NULL) && (LxNsInitInums[Kind] != 0))
        Ns->Inum = LxNsInitInums[Kind];
    else
        Ns->Inum = (ULONG64)InterlockedIncrement64(&LxNsNextInum);
    if ((Owner != NULL) && (Kind != LxNsUser))
    {
        LxNsReference(Owner);
        Ns->Owner = Owner;
    }

    switch (Kind)
    {
        case LxNsMnt:
            InitializeListHead(&Ns->Mnt.Mounts);
            LxEventSourceInit(&Ns->Mnt.Events);
            if ((From != NULL) && (LxMountNsCopy(Ns, From) != 0))
            {
                if (Ns->Owner != NULL)
                    LxNsDereference(Ns->Owner);
                LxFree(Ns);
                return NULL;
            }
            break;

        case LxNsUts:
            if (From != NULL)
            {
                RtlCopyMemory(Ns->Uts.Hostname, From->Uts.Hostname, sizeof(Ns->Uts.Hostname));
                RtlCopyMemory(Ns->Uts.DomainName, From->Uts.DomainName, sizeof(Ns->Uts.DomainName));
            }
            else
            {
                RtlCopyMemory(Ns->Uts.Hostname, "localhost", sizeof("localhost"));
                RtlCopyMemory(Ns->Uts.DomainName, "(none)", sizeof("(none)"));
            }
            break;

        case LxNsPid:
            Ns->Pid.NextPid = 1;
            if (From != NULL)
            {
                LxNsReference(From);
                Ns->Parent = From;
                Ns->Level = From->Level + 1;
            }
            break;

        case LxNsUser:
            Ns->User.MaxUserNamespaces = (From != NULL) ? MAXLONG : (LX_THREADS_MAX / 2);
            if (From != NULL)
            {
                LxNsReference(From);
                Ns->Parent = From;
                Ns->Level = From->Level + 1;
                Ns->User.SetgroupsDenied = From->User.SetgroupsDenied;
            }
            break;

        case LxNsNet:
            Ns->Net.LoopbackUp = (From == NULL);
            break;

        case LxNsCgroup:
            if (From != NULL)
                Ns->Cgroup.Root = From->Cgroup.Root;
            break;

        case LxNsTime:
            if (From != NULL)
            {
                Ns->Time.Monotonic = From->Time.Monotonic;
                Ns->Time.Boottime = From->Time.Boottime;
            }
            break;

        default:
            break;
    }

    ExAcquireFastMutex(&LxNsLock);
    InsertTailList(&LxNsList, &Ns->Link);
    ExReleaseFastMutex(&LxNsLock);
    return Ns;
}

VOID
LxNsReference(
    _In_ PLX_NS Ns)
{
    InterlockedIncrement(&Ns->RefCount);
}

static
PLX_UCOUNT
LxUcountFind(
    _In_ PLX_NS Ns,
    _In_ ULONG Uid,
    _In_ BOOLEAN Create)
{
    PLIST_ENTRY Entry;
    PLX_UCOUNT Count;

    for (Entry = LxUcountList.Flink; Entry != &LxUcountList; Entry = Entry->Flink)
    {
        Count = CONTAINING_RECORD(Entry, LX_UCOUNT, Link);
        if ((Count->Ns == Ns) && (Count->Uid == Uid))
            return Count;
    }
    if (!Create)
        return NULL;
    Count = LxAlloc(sizeof(*Count));
    if (Count == NULL)
        return NULL;
    Count->Ns = Ns;
    Count->Uid = Uid;
    Count->Count = 0;
    InsertTailList(&LxUcountList, &Count->Link);
    return Count;
}

static
VOID
LxUserNsUncharge(
    _In_ PLX_NS Parent,
    _In_ ULONG Uid,
    _In_opt_ PLX_NS Stop)
{
    PLX_UCOUNT Count;
    PLX_NS Ns;

    for (Ns = Parent; (Ns != NULL) && (Ns != Stop); Uid = Ns->User.OwnerUid, Ns = Ns->Parent)
    {
        Count = LxUcountFind(Ns, Uid, FALSE);
        if (Count == NULL)
            continue;
        if (--Count->Count <= 0)
        {
            RemoveEntryList(&Count->Link);
            LxFree(Count);
        }
    }
}

static
BOOLEAN
LxUserNsCharge(
    _In_ PLX_NS Parent,
    _In_ ULONG Uid)
{
    PLX_UCOUNT Count;
    PLX_NS Ns;
    ULONG Owner;

    ExAcquireFastMutex(&LxNsLock);
    for (Ns = Parent, Owner = Uid; Ns != NULL; Owner = Ns->User.OwnerUid, Ns = Ns->Parent)
    {
        Count = LxUcountFind(Ns, Owner, TRUE);
        if ((Count == NULL) || (Count->Count >= Ns->User.MaxUserNamespaces))
        {
            if ((Count != NULL) && (Count->Count == 0))
            {
                RemoveEntryList(&Count->Link);
                LxFree(Count);
            }
            LxUserNsUncharge(Parent, Uid, Ns);
            ExReleaseFastMutex(&LxNsLock);
            return FALSE;
        }
        Count->Count++;
    }
    ExReleaseFastMutex(&LxNsLock);
    return TRUE;
}

VOID
LxNsDereference(
    _In_ PLX_NS Ns)
{
    PLX_NS Parent, Owner;

    if (InterlockedDecrement(&Ns->RefCount) != 0)
        return;

    ExAcquireFastMutex(&LxNsLock);
    RemoveEntryList(&Ns->Link);
    if ((Ns->Kind == LxNsUser) && Ns->User.Charged)
        LxUserNsUncharge(Ns->Parent, Ns->User.OwnerUid, NULL);
    ExReleaseFastMutex(&LxNsLock);

    Parent = Ns->Parent;
    Owner = Ns->Owner;
    switch (Ns->Kind)
    {
        case LxNsMnt:
            LxMountNsRelease(Ns);
            break;
        case LxNsIpc:
            LxIpcNamespaceCleanup(Ns);
            LxMqNamespaceCleanup(Ns);
            break;
        case LxNsUser:
            LxFree(Ns->User.Maps[0]);
            LxFree(Ns->User.Maps[1]);
            break;
        default:
            break;
    }
    LxFree(Ns);
    if (Owner != NULL)
        LxNsDereference(Owner);
    if (Parent != NULL)
        LxNsDereference(Parent);
}

PLX_NS
LxNsOwner(
    _In_ PLX_NS Ns)
{
    if (Ns->Kind == LxNsUser)
        return Ns->Parent;
    return (Ns->Owner != NULL) ? Ns->Owner : Ns->Instance->RootNs[LxNsUser];
}

PLX_NS
LxUserNsOf(
    _In_ PLX_PROCESS Process)
{
    PLX_NS UserNs;

    ExAcquireFastMutex(&LxNsLock);
    UserNs = LxCredUserNs(Process);
    LxNsReference(UserNs);
    ExReleaseFastMutex(&LxNsLock);
    return UserNs;
}

LONG
LxUserNsCreate(
    _In_ PLX_PROCESS Process,
    _Out_ PLX_NS *Result)
{
    PLX_NS Parent = LxCredUserNs(Process);
    PLX_NS Ns;
    PSTR Root;
    BOOLEAN Chrooted;

    *Result = NULL;
    if (Parent->Level > LX_USERNS_MAX_PARENT_LEVEL)
        return -LX_ENOSPC;
    if (!LxUserNsCharge(Parent, Process->Uid[1]))
        return -LX_ENOSPC;
    Root = LxProcessRoot(Process);
    Chrooted = (Root == NULL) || (strcmp(Root, "/") != 0);
    LxFree(Root);
    if (Chrooted || (LxIdFromKernel(Parent, FALSE, Process->Uid[1]) == MAXULONG) ||
        (LxIdFromKernel(Parent, TRUE, Process->Gid[1]) == MAXULONG))
    {
        ExAcquireFastMutex(&LxNsLock);
        LxUserNsUncharge(Parent, Process->Uid[1], NULL);
        ExReleaseFastMutex(&LxNsLock);
        return (Root == NULL) ? -LX_ENOMEM : -LX_EPERM;
    }
    Ns = LxNsCreate(Process->Instance, LxNsUser, Parent, NULL);
    if (Ns == NULL)
    {
        ExAcquireFastMutex(&LxNsLock);
        LxUserNsUncharge(Parent, Process->Uid[1], NULL);
        ExReleaseFastMutex(&LxNsLock);
        return -LX_ENOMEM;
    }
    Ns->User.OwnerUid = Process->Uid[1];
    Ns->User.OwnerGid = Process->Gid[1];
    Ns->User.ParentCouldSetfcap = ((Process->CapEffective >> LX_CAP_SETFCAP) & 1) != 0;
    Ns->User.Charged = TRUE;
    *Result = Ns;
    return 0;
}

VOID
LxUserNsEnter(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_NS UserNs)
{
    PLX_NS Old;

    LxNsReference(UserNs);
    ExAcquireFastMutex(&Process->Lock);
    ExAcquireFastMutex(&LxNsLock);
    Old = Process->Ns[LxNsUser];
    Process->Ns[LxNsUser] = UserNs;
    ExReleaseFastMutex(&LxNsLock);
    Process->SecureBits = 0;
    Process->CapInheritable = 0;
    Process->CapAmbient = 0;
    Process->CapPermitted = LX_CAP_FULL_SET;
    Process->CapEffective = LX_CAP_FULL_SET;
    Process->CapBounding = LX_CAP_FULL_SET;
    ExReleaseFastMutex(&Process->Lock);
    if (Old != NULL)
        LxNsDereference(Old);
}

static
BOOLEAN
LxIdIsSpace(
    _In_ CHAR Character)
{
    return (Character == ' ') || ((Character >= '\t') && (Character <= '\r'));
}

static
PCSTR
LxIdParseNumber(
    _In_ PCSTR Cursor,
    _In_ PCSTR End,
    _Out_ PULONG Value)
{
    ULONG64 Number = 0;

    while ((Cursor < End) && (*Cursor >= '0') && (*Cursor <= '9'))
    {
        Number = Number * 10 + (ULONG64)(*Cursor - '0');
        Cursor++;
    }
    *Value = (ULONG)Number;
    return Cursor;
}

static
PCSTR
LxIdSkipSpaces(
    _In_ PCSTR Cursor,
    _In_ PCSTR End)
{
    while ((Cursor < End) && LxIdIsSpace(*Cursor))
        Cursor++;
    return Cursor;
}

static
BOOLEAN
LxIdMapOverlaps(
    _In_ PLX_ID_MAP Map,
    _In_ PLX_ID_EXTENT Extent)
{
    ULONG UpperLast = Extent->First + Extent->Count - 1, LowerLast = Extent->LowerFirst + Extent->Count - 1;
    ULONG Index;

    for (Index = 0; Index < Map->Count; Index++)
    {
        PLX_ID_EXTENT Prev = &Map->Extents[Index];

        if ((Prev->First <= UpperLast) && (Prev->First + Prev->Count - 1 >= Extent->First))
            return TRUE;
        if ((Prev->LowerFirst <= LowerLast) && (Prev->LowerFirst + Prev->Count - 1 >= Extent->LowerFirst))
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxIdMapParse(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _Out_ PLX_ID_MAP Map)
{
    PCSTR Line = Data, Limit = Data + strnlen(Data, Length);

    Map->Count = 0;
    while (Line < Limit)
    {
        PCSTR End = Line, Cursor;
        LX_ID_EXTENT Extent;
        BOOLEAN More;

        while ((End < Limit) && (*End != '\n'))
            End++;
        More = (End < Limit) && (End + 1 < Limit);
        Cursor = LxIdSkipSpaces(Line, End);
        Cursor = LxIdParseNumber(Cursor, End, &Extent.First);
        if ((Cursor >= End) || !LxIdIsSpace(*Cursor))
            return -LX_EINVAL;
        Cursor = LxIdSkipSpaces(Cursor, End);
        Cursor = LxIdParseNumber(Cursor, End, &Extent.LowerFirst);
        if ((Cursor >= End) || !LxIdIsSpace(*Cursor))
            return -LX_EINVAL;
        Cursor = LxIdSkipSpaces(Cursor, End);
        Cursor = LxIdParseNumber(Cursor, End, &Extent.Count);
        if ((Cursor < End) && !LxIdIsSpace(*Cursor))
            return -LX_EINVAL;
        if (LxIdSkipSpaces(Cursor, End) != End)
            return -LX_EINVAL;
        if ((Extent.First == MAXULONG) || (Extent.LowerFirst == MAXULONG))
            return -LX_EINVAL;
        if ((ULONG)(Extent.First + Extent.Count) <= Extent.First)
            return -LX_EINVAL;
        if ((ULONG)(Extent.LowerFirst + Extent.Count) <= Extent.LowerFirst)
            return -LX_EINVAL;
        if (LxIdMapOverlaps(Map, &Extent))
            return -LX_EINVAL;
        if ((Map->Count + 1 == LX_ID_MAP_MAX_EXTENTS) && More)
            return -LX_EINVAL;
        if (Map->Count >= LX_ID_MAP_MAX_EXTENTS)
            return -LX_EINVAL;
        Map->Extents[Map->Count++] = Extent;
        if (!More)
            break;
        Line = End + 1;
    }
    return (Map->Count == 0) ? -LX_EINVAL : 0;
}

static
BOOLEAN
LxIdMapPermitted(
    _In_ PLX_PROCESS Writer,
    _In_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_ PLX_ID_MAP Map)
{
    ULONG Index;

    if (!Group)
    {
        for (Index = 0; Index < Map->Count; Index++)
        {
            if (Map->Extents[Index].LowerFirst == 0)
                break;
        }
        if (Index < Map->Count)
        {
            if (UserNs == LxCredUserNs(Writer))
            {
                if (!UserNs->User.ParentCouldSetfcap)
                    return FALSE;
            }
            else if (!LxNsCapable(Writer, UserNs->Parent, LX_CAP_SETFCAP))
            {
                return FALSE;
            }
        }
    }
    if ((Map->Count == 1) && (Map->Extents[0].Count == 1) && (UserNs->User.OwnerUid == Writer->Uid[1]))
    {
        ULONG Id = LxIdToKernel(UserNs->Parent, Group, Map->Extents[0].LowerFirst);

        if (!Group && (Id == Writer->Uid[1]))
            return TRUE;
        if (Group && UserNs->User.SetgroupsDenied && (Id == Writer->Gid[1]))
            return TRUE;
    }
    return LxNsCapable(Writer, UserNs->Parent, Group ? LX_CAP_SETGID : LX_CAP_SETUID);
}

LONG
LxUserNsWriteMap(
    _In_ PLX_PROCESS Writer,
    _In_ PLX_NS UserNs,
    _In_ BOOLEAN Group,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_NS WriterNs = LxCredUserNs(Writer);
    PLX_ID_MAP Map;
    ULONG Index;
    LONG Error;

    if (UserNs->Parent == NULL)
        return -LX_EPERM;
    if ((WriterNs != UserNs) && (WriterNs != UserNs->Parent))
        return -LX_EPERM;
    if (Length >= LX_PAGE_SIZE)
        return -LX_EINVAL;
    Map = LxAlloc(sizeof(*Map));
    if (Map == NULL)
        return -LX_ENOMEM;
    RtlZeroMemory(Map, sizeof(*Map));

    ExAcquireFastMutex(&LxUserNsStateLock);
    Error = -LX_EPERM;
    if ((UserNs->User.Maps[Group ? 1 : 0] != NULL) || !LxNsCapable(Writer, UserNs, LX_CAP_SYS_ADMIN))
        goto Exit;
    Error = LxIdMapParse(Data, Length, Map);
    if (Error != 0)
        goto Exit;
    Error = -LX_EPERM;
    if (!LxIdMapPermitted(Writer, UserNs, Group, Map))
        goto Exit;
    for (Index = 0; Index < Map->Count; Index++)
    {
        ULONG Lower = LxIdRangeToKernel(UserNs->Parent, Group, Map->Extents[Index].LowerFirst, Map->Extents[Index].Count);

        if (Lower == MAXULONG)
            goto Exit;
        Map->Extents[Index].LowerFirst = Lower;
    }
    InterlockedExchangePointer((PVOID volatile *)&UserNs->User.Maps[Group ? 1 : 0], Map);
    Map = NULL;
    Error = 0;
Exit:
    ExReleaseFastMutex(&LxUserNsStateLock);
    LxFree(Map);
    return Error;
}

LONG
LxUserNsWriteSetgroups(
    _In_ PLX_PROCESS Writer,
    _In_ PLX_NS UserNs,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PCSTR Cursor, Limit;
    BOOLEAN Allow;
    LONG Error = -LX_EPERM;

    UNREFERENCED_PARAMETER(Writer);
    if (Length >= 8)
        return -LX_EINVAL;
    Limit = Data + strnlen(Data, Length);
    if (strncmp(Data, "allow", 5) == 0)
    {
        Allow = TRUE;
        Cursor = Data + 5;
    }
    else if (strncmp(Data, "deny", 4) == 0)
    {
        Allow = FALSE;
        Cursor = Data + 4;
    }
    else
    {
        return -LX_EINVAL;
    }
    if (LxIdSkipSpaces(Cursor, Limit) != Limit)
        return -LX_EINVAL;

    ExAcquireFastMutex(&LxUserNsStateLock);
    if (Allow)
    {
        if (!UserNs->User.SetgroupsDenied)
            Error = 0;
    }
    else if ((UserNs->Parent != NULL) && (UserNs->User.Maps[1] == NULL))
    {
        UserNs->User.SetgroupsDenied = TRUE;
        Error = 0;
    }
    ExReleaseFastMutex(&LxUserNsStateLock);
    return Error;
}

BOOLEAN
LxUserNsMaySetgroups(
    _In_ PLX_NS UserNs)
{
    BOOLEAN Allowed;

    if (UserNs->Parent == NULL)
        return TRUE;
    ExAcquireFastMutex(&LxUserNsStateLock);
    Allowed = (UserNs->User.Maps[1] != NULL) && !UserNs->User.SetgroupsDenied;
    ExReleaseFastMutex(&LxUserNsStateLock);
    return Allowed;
}

LONG
LxUserNsGetMax(
    _In_ PLX_PROCESS Process)
{
    return LxCredUserNs(Process)->User.MaxUserNamespaces;
}

LONG
LxUserNsSetMax(
    _In_ PLX_PROCESS Process,
    _In_ LONG64 Value)
{
    PLX_NS UserNs = LxCredUserNs(Process);

    if (!LxNsCapable(Process, UserNs, LX_CAP_SYS_RESOURCE))
        return -LX_EACCES;
    if ((Value < 0) || (Value > MAXLONG))
        return -LX_EINVAL;
    ExAcquireFastMutex(&LxNsLock);
    UserNs->User.MaxUserNamespaces = (LONG)Value;
    ExReleaseFastMutex(&LxNsLock);
    return 0;
}

VOID
LxNsInstanceShutdown(
    _In_ PLX_INSTANCE Instance)
{
    PLIST_ENTRY Entry;
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
    {
        PLX_NS Ns = Instance->RootNs[Index];

        Instance->RootNs[Index] = NULL;
        if (Ns != NULL)
            LxNsDereference(Ns);
    }

    ExAcquireFastMutex(&LxNsLock);
    for (Entry = LxNsList.Flink; Entry != &LxNsList; Entry = Entry->Flink)
    {
        PLX_NS Ns = CONTAINING_RECORD(Entry, LX_NS, Link);

        if (Ns->Instance == Instance)
        {
            if (Ns->Kind == LxNsMnt)
                InitializeListHead(&Ns->Mnt.Mounts);
            Ns->Instance = NULL;
        }
    }
    ExReleaseFastMutex(&LxNsLock);
}

static
VOID
LxNsSet(
    _Inout_ PLX_NS *Slot,
    _In_opt_ PLX_NS Ns)
{
    PLX_NS Old = *Slot;

    if (Ns != NULL)
        LxNsReference(Ns);
    *Slot = Ns;
    if (Old != NULL)
        LxNsDereference(Old);
}

VOID
LxNsAttachRoot(
    _Inout_ PLX_PROCESS Process,
    _In_ PLX_INSTANCE Instance)
{
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
        LxNsSet(&Process->Ns[Index], Instance->RootNs[Index]);
    LxNsSet(&Process->PidNs, Instance->RootNs[LxNsPid]);
    LxNsSet(&Process->TimeNs, Instance->RootNs[LxNsTime]);
    if (Process->TimeNs != NULL)
        Process->TimeNs->Time.Frozen = TRUE;
}

LONG64
LxTimeNsOffset(
    _In_opt_ PLX_PROCESS Process,
    _In_ ULONG Clock)
{
    PLX_NS Ns = (Process != NULL) ? Process->TimeNs : NULL;

    if (Ns == NULL)
        return 0;
    switch (Clock)
    {
        case LX_CLOCK_MONOTONIC:
        case LX_CLOCK_MONOTONIC_RAW:
        case LX_CLOCK_MONOTONIC_COARSE:
            return Ns->Time.Monotonic;
        case LX_CLOCK_BOOTTIME:
        case LX_CLOCK_BOOTTIME_ALARM:
            return Ns->Time.Boottime;
        default:
            return 0;
    }
}

LONG
LxQueryClockIn(
    _In_opt_ PLX_PROCESS Process,
    _In_ ULONG Clock,
    _Out_ PLX_TIMESPEC Timespec)
{
    LONG Error = LxQueryClock(Clock, Timespec);
    LONG64 Offset;

    if (Error != 0)
        return Error;
    Offset = LxTimeNsOffset(Process, Clock);
    if (Offset != 0)
    {
        LONG64 Value = Timespec->Seconds * 1000000000LL + Timespec->Nanoseconds + Offset;

        Timespec->Seconds = Value / 1000000000LL;
        Timespec->Nanoseconds = Value % 1000000000LL;
    }
    return 0;
}

VOID
LxNsReleaseProcess(
    _Inout_ PLX_PROCESS Process)
{
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
        LxNsSet(&Process->Ns[Index], NULL);
    LxNsSet(&Process->PidNs, NULL);
    LxNsSet(&Process->TimeNs, NULL);
}

static
PLX_FS_STATE
LxFsStateCreate(
    _In_ PCSTR Cwd,
    _In_opt_ PCSTR Root)
{
    PLX_FS_STATE Fs = LxAlloc(sizeof(*Fs));

    if (Fs == NULL)
        return NULL;
    Fs->RefCount = 1;
    Fs->Cwd = LxStrDup(Cwd);
    Fs->Root = (Root != NULL) ? LxStrDup(Root) : NULL;
    if ((Fs->Cwd == NULL) || ((Root != NULL) && (Fs->Root == NULL)))
    {
        LxFree(Fs->Cwd);
        LxFree(Fs->Root);
        LxFree(Fs);
        return NULL;
    }
    return Fs;
}

static
VOID
LxFsStateDereference(
    _In_ PLX_FS_STATE Fs)
{
    if (InterlockedDecrement(&Fs->RefCount) != 0)
        return;
    LxFree(Fs->Cwd);
    LxFree(Fs->Root);
    LxFree(Fs);
}

VOID
LxNsThreadInherit(
    _Inout_ PLX_THREAD Thread,
    _In_opt_ PLX_THREAD Parent)
{
    ULONG Index;

    if ((Parent == NULL) || (Parent->Process != Thread->Process))
        return;
    for (Index = 0; Index < LxNsCount; Index++)
        LxNsSet(&Thread->NsOverride[Index], Parent->NsOverride[Index]);
    if (Parent->Fs != NULL)
    {
        InterlockedIncrement(&Parent->Fs->RefCount);
        Thread->Fs = Parent->Fs;
    }
}

VOID
LxNsThreadRelease(
    _Inout_ PLX_THREAD Thread)
{
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
        LxNsSet(&Thread->NsOverride[Index], NULL);
    if (Thread->Fs != NULL)
    {
        LxFsStateDereference(Thread->Fs);
        Thread->Fs = NULL;
    }
}

static
PLX_FS_STATE
LxCurrentFs(
    _In_ PLX_PROCESS Process)
{
    PLX_THREAD Thread = LxCurrentThread();

    if ((Thread != NULL) && (Thread->Process == Process))
        return Thread->Fs;
    return NULL;
}

PSTR
LxProcessRoot(
    _In_ PLX_PROCESS Process)
{
    PLX_FS_STATE Fs = LxCurrentFs(Process);
    PSTR Root;

    ExAcquireFastMutex(&Process->Lock);
    if (Fs != NULL)
        Root = LxStrDup((Fs->Root != NULL) ? Fs->Root : "/");
    else
        Root = LxStrDup((Process->Root != NULL) ? Process->Root : "/");
    ExReleaseFastMutex(&Process->Lock);
    return Root;
}

PSTR
LxProcessCwd(
    _In_ PLX_PROCESS Process)
{
    PLX_FS_STATE Fs = LxCurrentFs(Process);
    PSTR Cwd;

    ExAcquireFastMutex(&Process->Lock);
    Cwd = LxStrDup((Fs != NULL) ? Fs->Cwd : Process->Cwd);
    ExReleaseFastMutex(&Process->Lock);
    return Cwd;
}

LONG
LxProcessSetCwd(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path)
{
    PLX_FS_STATE Fs = LxCurrentFs(Process);
    PSTR Copy = LxStrDup(Path), Old;

    if (Copy == NULL)
        return -LX_ENOMEM;
    ExAcquireFastMutex(&Process->Lock);
    if (Fs != NULL)
    {
        Old = Fs->Cwd;
        Fs->Cwd = Copy;
    }
    else
    {
        Old = Process->Cwd;
        Process->Cwd = Copy;
    }
    ExReleaseFastMutex(&Process->Lock);
    LxFree(Old);
    return 0;
}

LONG
LxProcessChroot(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Path)
{
    PLX_FS_STATE Fs = LxCurrentFs(Process);
    PSTR Copy = (strcmp(Path, "/") == 0) ? NULL : LxStrDup(Path), Old;

    if ((strcmp(Path, "/") != 0) && (Copy == NULL))
        return -LX_ENOMEM;
    ExAcquireFastMutex(&Process->Lock);
    if (Fs != NULL)
    {
        Old = Fs->Root;
        Fs->Root = Copy;
    }
    else
    {
        Old = Process->Root;
        Process->Root = Copy;
    }
    ExReleaseFastMutex(&Process->Lock);
    LxFree(Old);
    return 0;
}

PSTR
LxPathVisible(
    _In_ PLX_PROCESS Viewer,
    _In_ PCSTR Path)
{
    PSTR Root = LxProcessRoot(Viewer), Result;
    SIZE_T Length;

    if (Root == NULL)
        return NULL;
    Length = strlen(Root);
    if ((Length > 1) && (strncmp(Path, Root, Length) == 0) && ((Path[Length] == '\0') || (Path[Length] == '/')))
        Result = LxStrDup((Path[Length] == '\0') ? "/" : Path + Length);
    else
        Result = LxStrDup(Path);
    LxFree(Root);
    return Result;
}

static
PSTR
LxRebaseRootPath(
    _In_ PCSTR Path,
    _In_ PCSTR NewRoot,
    _In_ PCSTR PutOld)
{
    if (strcmp(Path, "/") == 0)
        return LxStrDup("/");
    return LxPathRebase(Path, NewRoot, PutOld);
}

static
VOID
LxRebaseString(
    _Inout_ PSTR *Slot,
    _In_ PCSTR NewRoot,
    _In_ PCSTR PutOld,
    _In_ BOOLEAN RootSlot)
{
    PSTR Rebased;

    if (*Slot == NULL)
        return;
    Rebased = LxRebaseRootPath(*Slot, NewRoot, PutOld);
    if (Rebased == NULL)
        return;
    if (RootSlot && (strcmp(Rebased, "/") == 0))
    {
        LxFree(Rebased);
        Rebased = NULL;
    }
    LxFree(*Slot);
    *Slot = Rebased;
}

VOID
LxNsRebaseProcesses(
    _In_ PLX_NS MntNs,
    _In_ PCSTR NewRoot,
    _In_ PCSTR PutOld)
{
    PLIST_ENTRY Entry, ThreadEntry;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if (Process->Exited)
            continue;
        ExAcquireFastMutex(&Process->Lock);
        if (Process->Ns[LxNsMnt] == MntNs)
        {
            LxRebaseString(&Process->Cwd, NewRoot, PutOld, FALSE);
            if (Process->Cwd == NULL)
                Process->Cwd = LxStrDup("/");
            LxRebaseString(&Process->Root, NewRoot, PutOld, TRUE);
        }
        for (ThreadEntry = Process->Threads.Flink; ThreadEntry != &Process->Threads; ThreadEntry = ThreadEntry->Flink)
        {
            PLX_THREAD Thread = CONTAINING_RECORD(ThreadEntry, LX_THREAD, Link);
            PLX_NS ThreadNs = (Thread->NsOverride[LxNsMnt] != NULL) ? Thread->NsOverride[LxNsMnt] : Process->Ns[LxNsMnt];

            if ((Thread->Fs != NULL) && (ThreadNs == MntNs))
            {
                LxRebaseString(&Thread->Fs->Cwd, NewRoot, PutOld, FALSE);
                if (Thread->Fs->Cwd == NULL)
                    Thread->Fs->Cwd = LxStrDup("/");
                LxRebaseString(&Thread->Fs->Root, NewRoot, PutOld, TRUE);
            }
        }
        ExReleaseFastMutex(&Process->Lock);
    }
    ExReleaseFastMutex(&LxProcessListLock);
}

static
BOOLEAN
LxNsNeedThreadScope(
    _In_ PLX_PROCESS Process)
{
    return Process->ThreadCount > 1;
}

static
LONG
LxNsSetForCaller(
    _In_ PLX_THREAD Thread,
    _In_ LX_NS_KIND Kind,
    _In_ PLX_NS Ns)
{
    PLX_PROCESS Process = Thread->Process;

    if ((Kind == LxNsPid) || (Kind == LxNsTime) || (Kind == LxNsCgroup) || (Kind == LxNsUser) ||
        !LxNsNeedThreadScope(Process))
    {
        ExAcquireFastMutex(&Process->Lock);
        LxNsSet(&Process->Ns[Kind], Ns);
        ExReleaseFastMutex(&Process->Lock);
        if (Thread->NsOverride[Kind] != NULL)
            LxNsSet(&Thread->NsOverride[Kind], NULL);
        return 0;
    }
    LxNsSet(&Thread->NsOverride[Kind], Ns);
    return 0;
}

static
LONG
LxNsUnshareFs(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;
    PLX_FS_STATE Fs;
    PSTR Cwd, Root;

    if (!LxNsNeedThreadScope(Process))
        return 0;
    if ((Thread->Fs != NULL) && (Thread->Fs->RefCount == 1))
        return 0;
    Cwd = LxProcessCwd(Process);
    Root = LxProcessRoot(Process);
    if ((Cwd == NULL) || (Root == NULL))
    {
        LxFree(Cwd);
        LxFree(Root);
        return -LX_ENOMEM;
    }
    Fs = LxFsStateCreate(Cwd, (strcmp(Root, "/") == 0) ? NULL : Root);
    LxFree(Cwd);
    LxFree(Root);
    if (Fs == NULL)
        return -LX_ENOMEM;
    if (Thread->Fs != NULL)
        LxFsStateDereference(Thread->Fs);
    Thread->Fs = Fs;
    return 0;
}

static
LONG
LxNsResetFsToRoot(
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;
    LONG Error;

    Error = LxProcessChroot(Process, "/");
    if (Error == 0)
        Error = LxProcessSetCwd(Process, "/");
    return Error;
}

LONG
LxNsInheritProcess(
    _Inout_ PLX_PROCESS Child,
    _In_ PLX_PROCESS Parent,
    _In_ ULONG64 Flags)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_INSTANCE Instance = Parent->Instance;
    PSTR Cwd, Root;
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
        LxNsSet(&Child->Ns[Index], LxProcessNs(Parent, (LX_NS_KIND)Index));
    LxNsSet(&Child->PidNs, LxProcessNs(Parent, LxNsPid));
    LxNsSet(&Child->TimeNs, LxProcessNs(Parent, LxNsTime));

    if ((Current != NULL) && (Current->Process == Parent) && (Current->Fs != NULL))
    {
        Cwd = LxProcessCwd(Parent);
        Root = LxProcessRoot(Parent);
        if ((Cwd == NULL) || (Root == NULL))
        {
            LxFree(Cwd);
            LxFree(Root);
            return -LX_ENOMEM;
        }
        LxFree(Child->Cwd);
        LxFree(Child->Root);
        Child->Cwd = Cwd;
        Child->Root = (strcmp(Root, "/") == 0) ? NULL : Root;
        if (Child->Root == NULL)
            LxFree(Root);
    }

    if (Flags & LX_CLONE_NEWUSER)
    {
        PLX_NS UserNs;
        LONG Error = LxUserNsCreate(Child, &UserNs);

        if (Error != 0)
            return Error;
        LxUserNsEnter(Child, UserNs);
        LxNsDereference(UserNs);
    }

    for (Index = 0; Index < LxNsCount; Index++)
    {
        PLX_NS Ns;

        if (!(Flags & LxNsCloneFlags[Index]) || (Index == LxNsUser))
            continue;
        Ns = LxNsCreate(Instance, (LX_NS_KIND)Index, Child->Ns[Index], LxCredUserNs(Child));
        if (Ns == NULL)
            return (Index == LxNsPid) ? -LX_EUSERS : -LX_ENOMEM;
        if (Index == LxNsCgroup)
            Ns->Cgroup.Root = (Parent->Cgroup != NULL) ? Parent->Cgroup : Instance->RootCgroup;
        LxNsSet(&Child->Ns[Index], Ns);
        if (Index == LxNsPid)
            LxNsSet(&Child->PidNs, Ns);
        else if (Index == LxNsTime)
            LxNsSet(&Child->TimeNs, Ns);
        LxNsDereference(Ns);
    }

    if (Child->PidNs->Pid.Dead)
        return -LX_ENOMEM;
    if (Child->TimeNs != NULL)
        Child->TimeNs->Time.Frozen = TRUE;
    return 0;
}

static
PLX_NS
LxPidNsAt(
    _In_ PLX_NS Ns,
    _In_ ULONG Level)
{
    while ((Ns != NULL) && (Ns->Level > Level))
        Ns = Ns->Parent;
    return ((Ns != NULL) && (Ns->Level == Level)) ? Ns : NULL;
}

BOOLEAN
LxPidNsContains(
    _In_ PLX_NS Outer,
    _In_ PLX_NS Inner)
{
    return (Inner->Level >= Outer->Level) && (LxPidNsAt(Inner, Outer->Level) == Outer);
}

ULONG
LxPidNrIn(
    _In_ PLX_NS PidNs,
    _In_ PLX_PROCESS Target)
{
    if ((Target->PidNs == NULL) || (PidNs->Level == 0))
        return Target->Pid;
    if (!LxPidNsContains(PidNs, Target->PidNs))
        return 0;
    return Target->NsPid[PidNs->Level];
}

ULONG
LxPidNr(
    _In_ PLX_PROCESS Viewer,
    _In_ PLX_PROCESS Target)
{
    return LxPidNrIn(Viewer->PidNs, Target);
}

ULONG
LxTidNr(
    _In_ PLX_PROCESS Viewer,
    _In_ PLX_THREAD Thread)
{
    PLX_NS PidNs = Viewer->PidNs;
    PLX_PROCESS Target = Thread->Process;

    if ((Target->PidNs == NULL) || (PidNs->Level == 0))
        return Thread->Tid;
    if (!LxPidNsContains(PidNs, Target->PidNs))
        return 0;
    return Thread->NsTid[PidNs->Level];
}

static
BOOLEAN
LxPidNsMatch(
    _In_ PLX_NS PidNs,
    _In_ PLX_PROCESS Process)
{
    return (Process->Instance == PidNs->Instance) && (Process->PidNs != NULL) &&
           LxPidNsContains(PidNs, Process->PidNs);
}

ULONG
LxNrToGlobalIn(
    _In_ PLX_NS PidNs,
    _In_ ULONG Nr)
{
    PLIST_ENTRY Entry, ThreadEntry;
    ULONG Global = 0, Level = PidNs->Level;

    if (Level == 0)
        return Nr;
    if (Nr == 0)
        return 0;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Global == 0); Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if (!LxPidNsMatch(PidNs, Process))
            continue;
        if (Process->NsPid[Level] == Nr)
        {
            Global = Process->Pid;
            break;
        }
        ExAcquireFastMutex(&Process->Lock);
        for (ThreadEntry = Process->Threads.Flink; ThreadEntry != &Process->Threads; ThreadEntry = ThreadEntry->Flink)
        {
            PLX_THREAD Thread = CONTAINING_RECORD(ThreadEntry, LX_THREAD, Link);

            if (Thread->NsTid[Level] == Nr)
            {
                Global = Thread->Tid;
                break;
            }
        }
        ExReleaseFastMutex(&Process->Lock);
    }
    ExReleaseFastMutex(&LxProcessListLock);
    return Global;
}

ULONG
LxNrToGlobal(
    _In_ PLX_PROCESS Viewer,
    _In_ ULONG Nr)
{
    return LxNrToGlobalIn(Viewer->PidNs, Nr);
}

ULONG
LxGlobalToNrIn(
    _In_ PLX_NS PidNs,
    _In_ ULONG Global)
{
    PLIST_ENTRY Entry, ThreadEntry;
    ULONG Nr = 0, Level = PidNs->Level;

    if ((Level == 0) || (Global == 0))
        return Global;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);
        BOOLEAN Found = FALSE;

        if (Process->Instance != PidNs->Instance)
            continue;
        if (Process->Pid == Global)
        {
            Nr = LxPidNrIn(PidNs, Process);
            break;
        }
        ExAcquireFastMutex(&Process->Lock);
        for (ThreadEntry = Process->Threads.Flink; ThreadEntry != &Process->Threads; ThreadEntry = ThreadEntry->Flink)
        {
            PLX_THREAD Thread = CONTAINING_RECORD(ThreadEntry, LX_THREAD, Link);

            if (Thread->Tid == Global)
            {
                if (LxPidNsMatch(PidNs, Process))
                    Nr = Thread->NsTid[Level];
                Found = TRUE;
                break;
            }
        }
        ExReleaseFastMutex(&Process->Lock);
        if (Found)
            break;
    }
    ExReleaseFastMutex(&LxProcessListLock);
    return Nr;
}

ULONG
LxGlobalToNr(
    _In_ PLX_PROCESS Viewer,
    _In_ ULONG Global)
{
    return LxGlobalToNrIn(Viewer->PidNs, Global);
}

PLX_PROCESS
LxProcessLookupNr(
    _In_ PLX_PROCESS Viewer,
    _In_ ULONG Nr)
{
    ULONG Global = LxNrToGlobal(Viewer, Nr);

    return (Global != 0) ? LxProcessLookup(Viewer->Instance, Global) : NULL;
}

PLX_THREAD
LxThreadLookupNr(
    _In_ PLX_PROCESS Viewer,
    _In_ ULONG Nr,
    _Out_ PLX_PROCESS *Owner)
{
    ULONG Global = LxNrToGlobal(Viewer, Nr);

    *Owner = NULL;
    return (Global != 0) ? LxThreadLookupReferenced(Viewer->Instance, Global, Owner) : NULL;
}

BOOLEAN
LxPidNsIsInit(
    _In_ PLX_PROCESS Process)
{
    return (Process->PidNs != NULL) && (Process->PidNs->Level > 0) && (Process->NsPid[Process->PidNs->Level] == 1);
}

static
ULONG
LxPidNsAllocate(
    _In_ PLX_NS PidNs)
{
    ULONG Tries;

    for (Tries = 0; Tries < LX_PID_MAX; Tries++)
    {
        ULONG Nr;

        ExAcquireFastMutex(&LxNsLock);
        Nr = PidNs->Pid.NextPid++;
        if (PidNs->Pid.NextPid >= LX_PID_MAX)
            PidNs->Pid.NextPid = 2;
        ExReleaseFastMutex(&LxNsLock);
        if (LxNrToGlobalIn(PidNs, Nr) == 0)
            return Nr;
    }
    return 0;
}

static
LONG
LxPidFill(
    _In_ PLX_NS PidNs,
    _In_ ULONG Global,
    _In_reads_opt_(SetTidCount) const ULONG *SetTid,
    _In_ ULONG SetTidCount,
    _Out_writes_(LX_PIDNS_MAX_LEVEL) PULONG Numbers)
{
    PLX_NS Ns;

    RtlZeroMemory(Numbers, LX_PIDNS_MAX_LEVEL * sizeof(ULONG));
    Numbers[0] = Global;
    for (Ns = PidNs; (Ns != NULL) && (Ns->Level > 0); Ns = Ns->Parent)
    {
        ULONG Depth = PidNs->Level - Ns->Level;

        if (Ns->Pid.Dead)
            return -LX_ENOMEM;
        if (Depth < SetTidCount)
        {
            if (LxNrToGlobalIn(Ns, SetTid[Depth]) != 0)
                return -LX_EEXIST;
            Numbers[Ns->Level] = SetTid[Depth];
            continue;
        }
        Numbers[Ns->Level] = LxPidNsAllocate(Ns);
        if (Numbers[Ns->Level] == 0)
            return -LX_EAGAIN;
    }
    return 0;
}

static
LONG
LxPidAllocate(
    _In_ PLX_PROCESS Process,
    _In_reads_opt_(SetTidCount) const ULONG *SetTid,
    _In_ ULONG SetTidCount,
    _Out_ PULONG Global,
    _Out_writes_(LX_PIDNS_MAX_LEVEL) PULONG Numbers)
{
    PLX_NS PidNs = Process->PidNs, Ns = Process->PidNs;
    ULONG Depth;

    if (SetTidCount > PidNs->Level + 1)
        return -LX_EINVAL;
    for (Depth = 0; Depth < SetTidCount; Depth++, Ns = Ns->Parent)
    {
        if ((SetTid[Depth] < 1) || (SetTid[Depth] >= LX_PID_MAX))
            return -LX_EINVAL;
        if ((Ns->Level > 0) && (SetTid[Depth] != 1) && !Ns->Pid.HasInit)
            return -LX_EINVAL;
        if (!LxNsCapable(Process, LxNsOwner(Ns), LX_CAP_CHECKPOINT_RESTORE) &&
            !LxNsCapable(Process, LxNsOwner(Ns), LX_CAP_SYS_ADMIN))
        {
            return -LX_EPERM;
        }
    }
    if (SetTidCount > PidNs->Level)
    {
        *Global = SetTid[PidNs->Level];
        if (LxInstancePidInUse(Process->Instance, *Global))
            return -LX_EEXIST;
    }
    else
    {
        *Global = LxInstanceAllocatePid(Process->Instance);
    }
    return LxPidFill(PidNs, *Global, SetTid, SetTidCount, Numbers);
}

ULONG
LxPidNsLastPid(
    _In_ PLX_PROCESS Process)
{
    PLX_NS PidNs = Process->PidNs;
    ULONG Last;

    if (PidNs->Level == 0)
    {
        ExAcquireFastMutex(&Process->Instance->Lock);
        Last = Process->Instance->NextPid - 1;
        ExReleaseFastMutex(&Process->Instance->Lock);
        return Last;
    }
    ExAcquireFastMutex(&LxNsLock);
    Last = PidNs->Pid.NextPid - 1;
    ExReleaseFastMutex(&LxNsLock);
    return Last;
}

LONG
LxPidNsSetLastPid(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Value)
{
    PLX_NS PidNs = Process->PidNs;

    if (!LxNsCapable(Process, LxNsOwner(PidNs), LX_CAP_CHECKPOINT_RESTORE) &&
        !LxNsCapable(Process, LxNsOwner(PidNs), LX_CAP_SYS_ADMIN))
    {
        return -LX_EPERM;
    }
    if (Value >= LX_PID_MAX)
        return -LX_EINVAL;
    if (PidNs->Level == 0)
    {
        ExAcquireFastMutex(&Process->Instance->Lock);
        Process->Instance->NextPid = Value + 1;
        ExReleaseFastMutex(&Process->Instance->Lock);
        return 0;
    }
    ExAcquireFastMutex(&LxNsLock);
    PidNs->Pid.NextPid = Value + 1;
    ExReleaseFastMutex(&LxNsLock);
    return 0;
}

LONG
LxPidAssign(
    _Inout_ PLX_PROCESS Process,
    _Inout_ PLX_THREAD Thread,
    _In_reads_opt_(SetTidCount) const ULONG *SetTid,
    _In_ ULONG SetTidCount)
{
    PLX_NS PidNs = Process->PidNs;
    LONG Error;

    Error = LxPidAllocate(Process, SetTid, SetTidCount, &Process->Pid, Process->NsPid);
    if (Error != 0)
        return Error;
    Thread->Tid = Process->Pid;
    RtlCopyMemory(Thread->NsTid, Process->NsPid, sizeof(Thread->NsTid));
    if ((PidNs->Level > 0) && (Process->NsPid[PidNs->Level] == 1))
    {
        PidNs->Pid.HasInit = TRUE;
        PidNs->Pid.ReaperPid = Process->Pid;
    }
    return 0;
}

LONG
LxTidAssign(
    _In_ PLX_PROCESS Process,
    _Inout_ PLX_THREAD Thread,
    _In_reads_opt_(SetTidCount) const ULONG *SetTid,
    _In_ ULONG SetTidCount)
{
    return LxPidAllocate(Process, SetTid, SetTidCount, &Thread->Tid, Thread->NsTid);
}

static
LX_NS_KIND
LxNsKindFromFlag(
    _In_ ULONG64 Flag)
{
    ULONG Index;

    for (Index = 0; Index < LxNsCount; Index++)
    {
        if (LxNsCloneFlags[Index] == Flag)
            return (LX_NS_KIND)Index;
    }
    return LxNsCount;
}

static
BOOLEAN
LxUserNsWithin(
    _In_opt_ PLX_NS Ns,
    _In_ PLX_NS Ancestor)
{
    for (; Ns != NULL; Ns = Ns->Parent)
    {
        if (Ns == Ancestor)
            return TRUE;
    }
    return FALSE;
}

static
LONG64
LxNsFileIoctl(
    _In_ PLX_FILE File,
    _In_ ULONG Request,
    _In_ ULONG64 Argument)
{
    PLX_NS Ns = File->Object;
    PLX_THREAD Thread = LxCurrentThread();
    PLX_NS Target = NULL;
    PLX_FILE NewFile;
    LONG Fd;
    ULONG Uid = 0;

    switch (Request)
    {
        case LX_NS_GET_NSTYPE:
            return (LONG64)LxNsCloneFlags[Ns->Kind];

        case LX_NS_GET_OWNER_UID:
            if (Ns->Kind != LxNsUser)
                return -LX_EINVAL;
            Uid = (Thread != NULL) ? LxCredViewId(Thread->Process, FALSE, Ns->User.OwnerUid) : Ns->User.OwnerUid;
            return LxCopyToUser(Argument, &Uid, sizeof(Uid));

        case LX_NS_GET_PARENT:
            if ((Ns->Kind != LxNsPid) && (Ns->Kind != LxNsUser))
                return -LX_EINVAL;
            Target = Ns->Parent;
            if (Target == NULL)
                return -LX_EPERM;
            if ((Ns->Kind == LxNsPid) && (Thread != NULL) && !LxPidNsContains(Thread->Process->PidNs, Target))
                return -LX_EPERM;
            if ((Ns->Kind == LxNsUser) && (Thread != NULL) && !LxUserNsWithin(Target, LxCredUserNs(Thread->Process)))
                return -LX_EPERM;
            break;

        case LX_NS_GET_USERNS:
            if ((Ns->Instance == NULL) || (Ns->Instance->RootNs[LxNsUser] == NULL))
                return -LX_EPERM;
            Target = LxNsOwner(Ns);
            if ((Target == NULL) || ((Thread != NULL) && !LxUserNsWithin(Target, LxCredUserNs(Thread->Process))))
                return -LX_EPERM;
            break;

        default:
            return -LX_ENOTTY;
    }

    if (Thread == NULL)
        return -LX_ESRCH;
    NewFile = LxNsFileOpen(Target, LX_O_RDONLY);
    if (NewFile == NULL)
        return -LX_ENOMEM;
    Fd = LxFdInstall(Thread->Process, NewFile, 0, TRUE);
    LxFileDereference(NewFile);
    return Fd;
}

static
LONG
LxNsFileStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_NS Ns = File->Object;

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Dev = 4;
    Stat->Ino = Ns->Inum;
    Stat->Mode = LX_S_IFREG | 0444;
    Stat->Nlink = 1;
    Stat->BlkSize = 4096;
    return 0;
}

static
VOID
LxNsFileRelease(
    _In_ PLX_FILE File)
{
    if (File->Object != NULL)
        LxNsDereference(File->Object);
    File->Object = NULL;
}

static const LX_FILE_OPS LxNsFileOps =
{
    NULL, NULL, LxNsFileIoctl, NULL, LxNsFileStat, NULL, NULL, NULL, LxNsFileRelease
};

PLX_FILE
LxNsFileOpen(
    _In_ PLX_NS Ns,
    _In_ ULONG Flags)
{
    PLX_FILE File = LxFileAlloc(&LxNsFileOps, Flags & (LX_O_ACCMODE | LX_O_PATH), LX_S_IFREG);

    if (File == NULL)
        return NULL;
    File->Path = LxStrPrintf("%s:[%llu]", LxNsName(Ns->Kind), Ns->Inum);
    if (File->Path == NULL)
    {
        LxFileDereference(File);
        return NULL;
    }
    LxNsReference(Ns);
    File->Object = Ns;
    File->Ino = Ns->Inum;
    return File;
}

PLX_NS
LxNsFromFile(
    _In_ PLX_FILE File)
{
    PLX_NS Ns;

    if (File->Ops != &LxNsFileOps)
        return NULL;
    Ns = File->Object;
    LxNsReference(Ns);
    return Ns;
}

LX_SYSCALL(LxSysUnshare)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_PROCESS Process = Call->Process;
    ULONG64 Flags = Call->Args[0];
    ULONG64 NsFlags = LX_CLONE_NEWNS | LX_CLONE_NEWUTS | LX_CLONE_NEWIPC | LX_CLONE_NEWUSER | LX_CLONE_NEWPID |
                      LX_CLONE_NEWNET | LX_CLONE_NEWCGROUP | LX_CLONE_NEWTIME;
    PLX_NS UserNs = NULL;
    ULONG Index;
    LONG Error;

    if (Flags & LX_CLONE_NEWUSER)
        Flags |= LX_CLONE_THREAD | LX_CLONE_FS;
    if (Flags & ~(NsFlags | LX_CLONE_FS | LX_CLONE_FILES | LX_CLONE_SYSVSEM | LX_CLONE_THREAD | LX_CLONE_SIGHAND |
                  LX_CLONE_VM))
    {
        return -LX_EINVAL;
    }
    if ((Flags & (LX_CLONE_THREAD | LX_CLONE_SIGHAND | LX_CLONE_VM)) && (Process->ThreadCount > 1))
        return -LX_EINVAL;

    if (Flags & (LX_CLONE_FS | LX_CLONE_NEWNS))
    {
        Error = LxNsUnshareFs(Thread);
        if (Error != 0)
            return Error;
    }
    if (Flags & LX_CLONE_NEWUSER)
    {
        Error = LxUserNsCreate(Process, &UserNs);
        if (Error != 0)
            return Error;
    }
    if ((Flags & NsFlags & ~LX_CLONE_NEWUSER) && (UserNs == NULL) && !LxNsCapable(Process, NULL, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    if (UserNs != NULL)
    {
        LxUserNsEnter(Process, UserNs);
        LxNsDereference(UserNs);
    }

    for (Index = 0; Index < LxNsCount; Index++)
    {
        PLX_NS Ns;

        if (!(Flags & LxNsCloneFlags[Index]) || (Index == LxNsUser))
            continue;
        Ns = LxNsCreate(Process->Instance, (LX_NS_KIND)Index, LxProcessNs(Process, (LX_NS_KIND)Index),
                        LxCredUserNs(Process));
        if (Ns == NULL)
            return (Index == LxNsPid) ? -LX_EUSERS : -LX_ENOMEM;
        if (Index == LxNsCgroup)
            Ns->Cgroup.Root = (Process->Cgroup != NULL) ? Process->Cgroup : Process->Instance->RootCgroup;
        Error = LxNsSetForCaller(Thread, (LX_NS_KIND)Index, Ns);
        LxNsDereference(Ns);
        if (Error != 0)
            return Error;
    }
    return 0;
}

static
LONG
LxNsJoin(
    _In_ PLX_THREAD Thread,
    _In_ PLX_NS Ns)
{
    PLX_PROCESS Process = Thread->Process;
    LONG Error;

    if (Ns->Instance != Process->Instance)
        return -LX_EINVAL;

    if (Ns->Kind == LxNsUser)
    {
        if (Ns == LxCredUserNs(Process))
            return -LX_EINVAL;
        if ((Process->ThreadCount > 1) || ((Thread->Fs != NULL) && (Thread->Fs->RefCount != 1)))
            return -LX_EINVAL;
        if (!LxNsCapable(Process, Ns, LX_CAP_SYS_ADMIN))
            return -LX_EPERM;
        LxUserNsEnter(Process, Ns);
        return 0;
    }
    if (!LxNsCapable(Process, LxNsOwner(Ns), LX_CAP_SYS_ADMIN) || !LxNsCapable(Process, NULL, LX_CAP_SYS_ADMIN) ||
        ((Ns->Kind == LxNsMnt) && !LxNsCapable(Process, NULL, LX_CAP_SYS_CHROOT)))
    {
        return -LX_EPERM;
    }

    switch (Ns->Kind)
    {
        case LxNsPid:
            if (!LxPidNsContains(Process->PidNs, Ns))
                return -LX_EINVAL;
            return LxNsSetForCaller(Thread, LxNsPid, Ns);

        case LxNsMnt:
            if (LxNsNeedThreadScope(Process) && ((Thread->Fs == NULL) || (Thread->Fs->RefCount != 1)))
                return -LX_EINVAL;
            Error = LxNsSetForCaller(Thread, LxNsMnt, Ns);
            if (Error == 0)
                Error = LxNsResetFsToRoot(Thread);
            return Error;

        case LxNsTime:
            Error = LxNsSetForCaller(Thread, LxNsTime, Ns);
            if (Error == 0)
            {
                ExAcquireFastMutex(&Process->Lock);
                LxNsSet(&Process->TimeNs, Ns);
                ExReleaseFastMutex(&Process->Lock);
                Ns->Time.Frozen = TRUE;
            }
            return Error;

        default:
            return LxNsSetForCaller(Thread, Ns->Kind, Ns);
    }
}

LX_SYSCALL(LxSysSetns)
{
    PLX_THREAD Thread = Call->Thread;
    PLX_PROCESS Process = Call->Process;
    ULONG64 Type = Call->Args[1];
    PLX_FILE File;
    PLX_PROCESS Target;
    PLX_NS Ns;
    LONG Error = 0;

    File = LxFdGet(Process, (LONG)Call->Args[0]);
    if (File == NULL)
        return -LX_EBADF;

    Target = LxPidfdProcess(File);
    if (Target != NULL)
    {
        PLX_NS Joined[LxNsCount];
        ULONG Index;

        if ((Type == 0) || (Type & ~(LX_CLONE_NEWNS | LX_CLONE_NEWUTS | LX_CLONE_NEWIPC | LX_CLONE_NEWPID |
                                     LX_CLONE_NEWNET | LX_CLONE_NEWCGROUP | LX_CLONE_NEWTIME | LX_CLONE_NEWUSER)))
        {
            LxFileDereference(File);
            return -LX_EINVAL;
        }
        if (Target->Exited)
        {
            LxFileDereference(File);
            return -LX_ESRCH;
        }
        if (!LxPtraceMayRead(Process, Target))
        {
            LxFileDereference(File);
            return -LX_EPERM;
        }
        ExAcquireFastMutex(&Target->Lock);
        for (Index = 0; Index < LxNsCount; Index++)
        {
            Joined[Index] = (Index == LxNsPid) ? Target->PidNs : (Index == LxNsTime) ? Target->TimeNs : Target->Ns[Index];
            if (Joined[Index] != NULL)
                LxNsReference(Joined[Index]);
        }
        ExReleaseFastMutex(&Target->Lock);
        if ((Type & LX_CLONE_NEWUSER) && (Joined[LxNsUser] != NULL))
            Error = LxNsJoin(Thread, Joined[LxNsUser]);
        for (Index = 0; (Index < LxNsCount) && (Error == 0); Index++)
        {
            if ((Type & LxNsCloneFlags[Index]) && (Joined[Index] != NULL) && (Index != LxNsUser))
                Error = LxNsJoin(Thread, Joined[Index]);
        }
        for (Index = 0; Index < LxNsCount; Index++)
        {
            if (Joined[Index] != NULL)
                LxNsDereference(Joined[Index]);
        }
        LxFileDereference(File);
        return Error;
    }

    Ns = LxNsFromFile(File);
    LxFileDereference(File);
    if (Ns == NULL)
        return -LX_EINVAL;
    if ((Type != 0) && (LxNsKindFromFlag(Type) != Ns->Kind))
    {
        LxNsDereference(Ns);
        return -LX_EINVAL;
    }
    Error = LxNsJoin(Thread, Ns);
    LxNsDereference(Ns);
    return Error;
}
