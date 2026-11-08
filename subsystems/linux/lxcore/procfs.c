/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     proc, sys, cgroup2 and devpts synthetic file systems
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ntstrsafe.h>
#include <stdarg.h>

#define NDEBUG
#include <debug.h>

#define LX_CGROUP_CPUSET    0x00000001
#define LX_CGROUP_PIDS      0x00000002
#define LX_CGROUP_ALL       (LX_CGROUP_CPUSET | LX_CGROUP_PIDS)
#define LX_PIDS_MAX         4194305

#define LX_PROC_MAX_COMPONENTS  16
#define LX_PROC_TICKS           100000LL
#define LX_RLIMIT_RSS           5

typedef struct _LX_TEXT
{
    PSTR Buffer;
    ULONG Length;
    ULONG Capacity;
    BOOLEAN Failed;
} LX_TEXT, *PLX_TEXT;

typedef enum _LX_SYNTH_ID
{
    LxSynthNone,
    LxProcRoot,
    LxProcLink,
    LxProcStatic,
    LxProcSysDir,
    LxProcSysFile,
    LxProcPidDir,
    LxProcPidFile,
    LxProcPidLink,
    LxProcTaskDir,
    LxProcFdDir,
    LxProcFdLink,
    LxProcFdinfoDir,
    LxProcFdinfoFile,
    LxProcMapFilesDir,
    LxProcMapFileLink,
    LxSysDir,
    LxSysFile,
    LxCgDir,
    LxCgFile,
    LxPtsRoot,
    LxPtsPtmx,
    LxPtsTty,
    LxProcNsDir,
    LxProcNsLink,
    LxNsfsFile,
    LxSysDevNode,
    LxMqRoot,
    LxMqFile,
    LxSysLoop
} LX_SYNTH_ID;

typedef struct _LX_SYNTH
{
    LX_SYNTH_ID Id;
    ULONG Type;
    ULONG Mode;
    ULONG Pid;
    ULONG Tid;
    LONG Fd;
    ULONG Index;
    PLX_CGROUP Cgroup;
    PLX_MOUNT Mount;
    ULONG Uid;
    ULONG Gid;
    LX_NS_KIND NsKind;
    CHAR Prefix[128];
    PSTR LinkTarget;
    BOOLEAN Magic;
    ULONG64 MapStart;
    ULONG64 MapEnd;
} LX_SYNTH, *PLX_SYNTH;

static const PCSTR LxProcNsEntries[] =
{
    "cgroup", "ipc", "mnt", "net", "pid", "pid_for_children", "time", "time_for_children", "user", "uts"
};

static
PLX_NS
LxProcPidNs(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth)
{
    if ((Synth->Mount != NULL) && (Synth->Mount->PidNs != NULL))
        return Synth->Mount->PidNs;
    return (Self->Instance->RootNs[LxNsPid] != NULL) ? Self->Instance->RootNs[LxNsPid] : Self->PidNs;
}

static
PLX_NS
LxProcTargetNs(
    _In_ PLX_PROCESS Target,
    _In_ PCSTR Name,
    _Out_ PLX_NS_KIND Kind)
{
    if (strcmp(Name, "pid") == 0)
    {
        *Kind = LxNsPid;
        return Target->PidNs;
    }
    if (strcmp(Name, "pid_for_children") == 0)
    {
        *Kind = LxNsPid;
        return Target->Ns[LxNsPid];
    }
    if (strcmp(Name, "time") == 0)
    {
        *Kind = LxNsTime;
        return Target->TimeNs;
    }
    if (strcmp(Name, "time_for_children") == 0)
    {
        *Kind = LxNsTime;
        return Target->Ns[LxNsTime];
    }
    for (*Kind = LxNsMnt; *Kind < LxNsCount; (*Kind)++)
    {
        if (strcmp(Name, LxNsName(*Kind)) == 0)
            return Target->Ns[*Kind];
    }
    return NULL;
}

static
PLX_NS
LxProcTaskNs(
    _In_ PLX_PROCESS Target,
    _In_ ULONG Tid,
    _In_ PCSTR Name,
    _Out_ PLX_NS_KIND Kind)
{
    PLX_NS Ns = LxProcTargetNs(Target, Name, Kind);
    PLIST_ENTRY Entry;

    if ((Ns == NULL) || (Tid == 0) || (strstr(Name, "_for_children") != NULL) || (strcmp(Name, "pid") == 0) ||
        (strcmp(Name, "time") == 0))
    {
        return Ns;
    }
    for (Entry = Target->Threads.Flink; Entry != &Target->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if ((Thread->Tid == Tid) && (Thread->NsOverride[*Kind] != NULL))
            return Thread->NsOverride[*Kind];
    }
    return Ns;
}

typedef VOID (*PLX_GENERATOR)(_In_ PLX_PROCESS Process, _In_ PLX_SYNTH Synth, _Inout_ PLX_TEXT Text);
typedef LONG (*PLX_WRITER)(_In_ PLX_FILE File, _In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length);

typedef struct _LX_TABLE_ENTRY
{
    PCSTR Path;
    ULONG Mode;
    PCSTR Text;
    PLX_GENERATOR Generator;
    PLX_WRITER Writer;
} LX_TABLE_ENTRY, *PLX_TABLE_ENTRY;

typedef struct _LX_PID_ENTRY
{
    PCSTR Name;
    ULONG Type;
    ULONG Mode;
    PCSTR Text;
    PLX_GENERATOR Generator;
    PLX_WRITER Writer;
} LX_PID_ENTRY, *PLX_PID_ENTRY;

typedef struct _LX_CGROUP_FILE_ATTR
{
    LIST_ENTRY Link;
    ULONG Index;
    ULONG Uid;
    ULONG Gid;
    ULONG Mode;
} LX_CGROUP_FILE_ATTR, *PLX_CGROUP_FILE_ATTR;

static FAST_MUTEX LxCgroupLock;
static volatile LONG64 LxCgroupNextId = 1;

static
VOID
LxTextAppend(
    _Inout_ PLX_TEXT Text,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    if (Text->Failed)
        return;
    if (Text->Length + Length + 1 > Text->Capacity)
    {
        ULONG Capacity = max(Text->Capacity * 2, Text->Length + Length + 1024);
        PSTR Buffer = LxAlloc(Capacity);

        if (Buffer == NULL)
        {
            Text->Failed = TRUE;
            return;
        }
        if (Text->Buffer != NULL)
        {
            RtlCopyMemory(Buffer, Text->Buffer, Text->Length);
            LxFree(Text->Buffer);
        }
        Text->Buffer = Buffer;
        Text->Capacity = Capacity;
    }
    RtlCopyMemory(Text->Buffer + Text->Length, Data, Length);
    Text->Length += Length;
    Text->Buffer[Text->Length] = '\0';
}

static
VOID
LxTextPrintf(
    _Inout_ PLX_TEXT Text,
    _In_ PCSTR Format,
    ...)
{
    CHAR Local[512];
    va_list Arguments;
    PSTR Large;
    NTSTATUS Status;

    va_start(Arguments, Format);
    Status = RtlStringCbVPrintfA(Local, sizeof(Local), Format, Arguments);
    va_end(Arguments);
    if (NT_SUCCESS(Status))
    {
        LxTextAppend(Text, Local, (ULONG)strlen(Local));
        return;
    }

    Large = LxAlloc(16384);
    if (Large == NULL)
    {
        Text->Failed = TRUE;
        return;
    }
    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Large, 16384, Format, Arguments);
    va_end(Arguments);
    LxTextAppend(Text, Large, (ULONG)strlen(Large));
    LxFree(Large);
}

static
VOID
LxDirAdd(
    _Inout_ PLX_TEXT List,
    _In_ UCHAR Type,
    _In_ PCSTR Name)
{
    LxTextAppend(List, (PCSTR)&Type, 1);
    LxTextAppend(List, Name, (ULONG)strlen(Name) + 1);
}

static
VOID
LxSysDevDirAdd(
    _In_ PVOID Context,
    _In_ UCHAR Type,
    _In_ PCSTR Name)
{
    LxDirAdd((PLX_TEXT)Context, Type, Name);
}

static
ULONG64
LxHashPath(
    _In_ ULONG Seed,
    _In_ PCSTR Path)
{
    ULONG64 Hash = 0xCBF29CE484222325ULL ^ Seed;

    while (*Path != '\0')
    {
        Hash ^= (UCHAR)*Path++;
        Hash *= 0x100000001B3ULL;
    }
    return (Hash & 0x00FFFFFFFFFFFFFFULL) | 0x1000;
}

static
ULONG64
LxSynthIno(
    _In_ PLX_NODE Node)
{
    PLX_MOUNT Mount = Node->Mount;

    PCSTR Relative = Node->Relative;
    ULONG64 Ino;
    ULONG Number = 0;

    if ((Mount->Kind == LxFsProc) && ((Mount->Root == NULL) || (strcmp(Mount->Root, "/") == 0)) &&
        ((Relative[0] == '\0') || (strcmp(Relative, "/") == 0)))
    {
        return LX_PROC_ROOT_INO;
    }
    Ino = LxHashPath(Mount->Id, Relative);
    if (Mount->Kind != LxFsProc)
        return Ino;
    if (*Relative == '/')
        Relative++;
    while ((*Relative >= '0') && (*Relative <= '9') && (Number < 429496729))
        Number = Number * 10 + (*Relative++ - '0');
    if ((Number != 0) && ((*Relative == '\0') || (*Relative == '/')))
    {
        PLX_PROCESS Process = LxProcessLookup(Mount->Instance,
                                              (Mount->PidNs != NULL) ? LxNrToGlobalIn(Mount->PidNs, Number) : Number);

        if (Process != NULL)
        {
            Ino ^= (ULONG64)Process->StartTime * 0x9E3779B97F4A7C15ULL;
            LxProcessDereference(Process);
        }
    }
    return Ino;
}

static
BOOLEAN
LxProcIsPidRoot(
    _In_ PCSTR Relative)
{
    if (*Relative == '/')
        Relative++;
    if (*Relative == '\0')
        return FALSE;
    while ((*Relative >= '0') && (*Relative <= '9'))
        Relative++;
    return (*Relative == '\0') || ((Relative[0] == '/') && (Relative[1] == '\0'));
}

static
BOOLEAN
LxParseNumber(
    _In_ PCSTR String,
    _Out_ PULONG Value)
{
    ULONG Result = 0;

    if ((*String == '\0') || ((*String == '0') && (String[1] != '\0')))
        return FALSE;
    while (*String != '\0')
    {
        if ((*String < '0') || (*String > '9') || (Result > 429496728))
            return FALSE;
        Result = Result * 10 + (*String++ - '0');
    }
    *Value = Result;
    return TRUE;
}

static
LONG
LxParseWriteNumber(
    _In_ PCSTR Data,
    _Out_ PLONG64 Value)
{
    LONG64 Result = 0;
    BOOLEAN Negative = FALSE, Any = FALSE;

    while ((*Data == ' ') || (*Data == '\t'))
        Data++;
    if (*Data == '-')
    {
        Negative = TRUE;
        Data++;
    }
    while ((*Data >= '0') && (*Data <= '9'))
    {
        Result = Result * 10 + (*Data++ - '0');
        Any = TRUE;
    }
    while ((*Data == ' ') || (*Data == '\n') || (*Data == '\t'))
        Data++;
    if (!Any || (*Data != '\0'))
        return -LX_EINVAL;
    *Value = Negative ? -Result : Result;
    return 0;
}

PLX_CGROUP
LxCgroupCreateRoot(VOID)
{
    PLX_CGROUP Root = LxAlloc(sizeof(*Root));

    if (Root == NULL)
        return NULL;
    Root->Name = LxStrDup("");
    if (Root->Name == NULL)
    {
        LxFree(Root);
        return NULL;
    }
    InitializeListHead(&Root->Sibling);
    InitializeListHead(&Root->Children);
    InitializeListHead(&Root->DeadLink);
    InitializeListHead(&Root->DeadList);
    InitializeListHead(&Root->BpfDevice);
    InitializeListHead(&Root->FileAttributes);
    LxEventSourceInit(&Root->Events);
    Root->Mode = 0755;
    Root->PidsLimit = LX_PIDS_MAX;
    Root->Root = Root;
    Root->Id = (ULONG64)InterlockedIncrement64(&LxCgroupNextId);
    Root->Created = (LONG64)KeQueryInterruptTime();
    return Root;
}

static
PLX_CGROUP
LxCgroupCreateChild(
    _In_ PLX_CGROUP Parent,
    _In_ PCSTR Name)
{
    PLX_CGROUP Child = LxAlloc(sizeof(*Child));

    if (Child == NULL)
        return NULL;
    Child->Name = LxStrDup(Name);
    if (Child->Name == NULL)
    {
        LxFree(Child);
        return NULL;
    }
    InitializeListHead(&Child->Children);
    InitializeListHead(&Child->DeadLink);
    InitializeListHead(&Child->DeadList);
    InitializeListHead(&Child->BpfDevice);
    InitializeListHead(&Child->FileAttributes);
    LxEventSourceInit(&Child->Events);
    Child->Mode = 0755;
    Child->PidsLimit = LX_PIDS_MAX;
    Child->Parent = Parent;
    Child->Root = Parent->Root;
    Child->Instance = Parent->Instance;
    Child->Id = (ULONG64)InterlockedIncrement64(&LxCgroupNextId);
    Child->Created = (LONG64)KeQueryInterruptTime();
    InsertTailList(&Parent->Children, &Child->Sibling);
    return Child;
}

static
VOID
LxCgroupFreeOne(
    _In_ PLX_CGROUP Cgroup)
{
    while (!IsListEmpty(&Cgroup->Children))
        LxCgroupFreeOne(CONTAINING_RECORD(RemoveHeadList(&Cgroup->Children), LX_CGROUP, Sibling));
    while (!IsListEmpty(&Cgroup->FileAttributes))
        LxFree(CONTAINING_RECORD(RemoveHeadList(&Cgroup->FileAttributes), LX_CGROUP_FILE_ATTR, Link));
    LxBpfCgroupRelease(Cgroup);
    LxFree(Cgroup->Name);
    LxFree(Cgroup);
}

VOID
LxCgroupFreeTree(
    _In_ PLX_CGROUP Root)
{
    while (!IsListEmpty(&Root->DeadList))
        LxCgroupFreeOne(CONTAINING_RECORD(RemoveHeadList(&Root->DeadList), LX_CGROUP, DeadLink));
    LxCgroupFreeOne(Root);
}

static
PLX_CGROUP
LxCgroupFindChild(
    _In_ PLX_CGROUP Parent,
    _In_ PCSTR Name,
    _In_ SIZE_T Length)
{
    PLIST_ENTRY Entry;

    for (Entry = Parent->Children.Flink; Entry != &Parent->Children; Entry = Entry->Flink)
    {
        PLX_CGROUP Child = CONTAINING_RECORD(Entry, LX_CGROUP, Sibling);

        if ((strlen(Child->Name) == Length) && (strncmp(Child->Name, Name, Length) == 0))
            return Child;
    }
    return NULL;
}

PSTR
LxCgroupPath(
    _In_ PLX_CGROUP Cgroup)
{
    PLX_CGROUP Walk;
    SIZE_T Length = 0;
    PSTR Path, Cursor;

    if (Cgroup == NULL)
        return LxStrDup("/");

    ExAcquireFastMutex(&LxCgroupLock);
    for (Walk = Cgroup; Walk->Parent != NULL; Walk = Walk->Parent)
        Length += strlen(Walk->Name) + 1;
    if (Length == 0)
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return LxStrDup("/");
    }
    Path = LxAlloc(Length + 1);
    if (Path != NULL)
    {
        Cursor = Path + Length;
        *Cursor = '\0';
        for (Walk = Cgroup; Walk->Parent != NULL; Walk = Walk->Parent)
        {
            SIZE_T NameLength = strlen(Walk->Name);

            Cursor -= NameLength;
            RtlCopyMemory(Cursor, Walk->Name, NameLength);
            *--Cursor = '/';
        }
    }
    ExReleaseFastMutex(&LxCgroupLock);
    return Path;
}

static
VOID
LxCgroupNotifyEvents(
    _In_ PLX_CGROUP Cgroup)
{
    PLX_INSTANCE Instance = Cgroup->Instance;
    PLIST_ENTRY Entry;
    PSTR Targets[8];
    PLX_CGROUP Roots[8];
    ULONG Count = 0, Index;
    PSTR Path;

    LxEventSourceSignal(&Cgroup->Events);
    if (Instance == NULL)
        return;

    ExAcquireFastMutex(&Instance->Lock);
    for (Entry = Instance->AllMounts.Flink; (Entry != &Instance->AllMounts) && (Count < RTL_NUMBER_OF(Targets));
         Entry = Entry->Flink)
    {
        PLX_MOUNT Mount = CONTAINING_RECORD(Entry, LX_MOUNT, InstanceLink);

        if ((Mount->Kind == LxFsCgroup) && !Mount->Detached && !IsListEmpty(&Mount->Link))
        {
            Targets[Count] = LxStrDup(Mount->Target);
            Roots[Count] = Mount->CgroupRoot;
            if (Targets[Count] != NULL)
                Count++;
        }
    }
    ExReleaseFastMutex(&Instance->Lock);

    Path = LxCgroupPath(Cgroup);
    for (Index = 0; Index < Count; Index++)
    {
        PSTR RootPath = ((Roots[Index] != NULL) && (Roots[Index]->Parent != NULL)) ? LxCgroupPath(Roots[Index]) : NULL;
        SIZE_T RootLength = (RootPath != NULL) ? strlen(RootPath) : 0;

        if ((Path != NULL) &&
            ((RootLength <= 1) ||
             ((strncmp(Path, RootPath, RootLength) == 0) && ((Path[RootLength] == '\0') || (Path[RootLength] == '/')))))
        {
            PCSTR Rest = (RootLength > 1) ? Path + RootLength : Path;
            PSTR Full = LxStrPrintf("%s%s/cgroup.events", Targets[Index], ((strcmp(Rest, "/") == 0) || (Rest[0] == '\0')) ? "" : Rest);

            if (Full != NULL)
            {
                LxInotifyNotify(Instance, Full, LX_IN_MODIFY, FALSE);
                LxFree(Full);
            }
        }
        LxFree(RootPath);
        LxFree(Targets[Index]);
    }
    LxFree(Path);
}

static
ULONG64
LxCgroupCpusEffectiveLocked(
    _In_opt_ PLX_CGROUP Cgroup)
{
    ULONG64 Parent, Mask;

    if ((Cgroup == NULL) || (Cgroup->Parent == NULL))
        return LxSchedActiveMask();
    Parent = LxCgroupCpusEffectiveLocked(Cgroup->Parent);
    if (!(Cgroup->Parent->SubtreeMask & LX_CGROUP_CPUSET))
        return Parent;
    Mask = Cgroup->CpusetCpus & Parent;
    return (Mask != 0) ? Mask : Parent;
}

ULONG64
LxCgroupCpusAllowed(
    _In_ PLX_PROCESS Process)
{
    ULONG64 Mask;

    ExAcquireFastMutex(&LxCgroupLock);
    Mask = LxCgroupCpusEffectiveLocked(Process->Cgroup);
    ExReleaseFastMutex(&LxCgroupLock);
    return Mask;
}

VOID
LxFormatCpuList(
    _In_ ULONG64 Mask,
    _Out_writes_(Size) PSTR Buffer,
    _In_ SIZE_T Size)
{
    SIZE_T Used = 0;
    ULONG Cpu;

    Buffer[0] = '\0';
    for (Cpu = 0; Cpu < 64; Cpu++)
    {
        ULONG Last = Cpu;

        if (!(Mask & (1ULL << Cpu)))
            continue;
        while ((Last + 1 < 64) && (Mask & (1ULL << (Last + 1))))
            Last++;
        if (Last == Cpu)
            RtlStringCbPrintfA(Buffer + Used, Size - Used, "%s%lu", Used ? "," : "", Cpu);
        else
            RtlStringCbPrintfA(Buffer + Used, Size - Used, "%s%lu-%lu", Used ? "," : "", Cpu, Last);
        Used = strlen(Buffer);
        Cpu = Last;
    }
}

VOID
LxCgroupRefreshAffinity(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_CGROUP Cgroup)
{
    PLX_PROCESS Targets[64];
    ULONG Count, Index, Skip = 0;
    PLIST_ENTRY Entry;
    BOOLEAN More;

    do
    {
        ULONG Seen = 0;

        Count = 0;
        More = FALSE;
        ExAcquireFastMutex(&LxProcessListLock);
        for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
        {
            PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);
            PLX_CGROUP Walk;

            if ((Process->Instance != Instance) || Process->Exited)
                continue;
            for (Walk = Process->Cgroup; (Walk != NULL) && (Walk != Cgroup); Walk = Walk->Parent)
                ;
            if ((Walk == NULL) || (Seen++ < Skip))
                continue;
            if (Count == RTL_NUMBER_OF(Targets))
            {
                More = TRUE;
                break;
            }
            LxProcessReference(Process);
            Targets[Count++] = Process;
        }
        ExReleaseFastMutex(&LxProcessListLock);
        Skip += Count;
        for (Index = 0; Index < Count; Index++)
        {
            LxSchedRefreshProcess(Targets[Index]);
            LxProcessDereference(Targets[Index]);
        }
    } while (More);
}

static
VOID
LxCgroupChargeLocked(
    _In_opt_ PLX_CGROUP Cgroup,
    _In_ LONG64 Count)
{
    for (; (Cgroup != NULL) && (Cgroup->Parent != NULL); Cgroup = Cgroup->Parent)
    {
        Cgroup->PidsCount += Count;
        if (Cgroup->PidsCount > Cgroup->PidsPeak)
            Cgroup->PidsPeak = Cgroup->PidsCount;
    }
}

LONG
LxCgroupTaskAdd(
    _In_ PLX_PROCESS Process,
    _In_ BOOLEAN Limited)
{
    PLX_CGROUP Walk, Over = NULL;

    ExAcquireFastMutex(&LxCgroupLock);
    for (Walk = Process->Cgroup; Limited && (Walk != NULL) && (Walk->Parent != NULL); Walk = Walk->Parent)
    {
        if (Walk->PidsCount + 1 > Walk->PidsLimit)
        {
            Over = Walk;
            break;
        }
    }
    if (Over != NULL)
    {
        Over->PidsEventsLocal++;
        for (Walk = Over; Walk->Parent != NULL; Walk = Walk->Parent)
            Walk->PidsEvents++;
        ExReleaseFastMutex(&LxCgroupLock);
        return -LX_EAGAIN;
    }
    InterlockedIncrement(&Process->ThreadCount);
    LxCgroupChargeLocked(Process->Cgroup, 1);
    ExReleaseFastMutex(&LxCgroupLock);
    return 0;
}

VOID
LxCgroupTaskRemove(
    _In_ PLX_PROCESS Process)
{
    ExAcquireFastMutex(&LxCgroupLock);
    InterlockedDecrement(&Process->ThreadCount);
    LxCgroupChargeLocked(Process->Cgroup, -1);
    ExReleaseFastMutex(&LxCgroupLock);
}

VOID
LxCgroupAttach(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_CGROUP Cgroup)
{
    PLX_CGROUP Changed[64];
    PLX_CGROUP Old, Walk;
    ULONG Count = 0, Index;

    ExAcquireFastMutex(&LxCgroupLock);
    Old = Process->Cgroup;
    if (Old == Cgroup)
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return;
    }
    LxCgroupChargeLocked(Old, -(LONG64)Process->ThreadCount);
    LxCgroupChargeLocked(Cgroup, Process->ThreadCount);
    if (Old != NULL)
    {
        Old->Members--;
        for (Walk = Old; Walk != NULL; Walk = Walk->Parent)
        {
            if ((--Walk->Populated == 0) && (Count < RTL_NUMBER_OF(Changed)))
                Changed[Count++] = Walk;
        }
    }
    if (Cgroup != NULL)
    {
        Cgroup->Members++;
        for (Walk = Cgroup; Walk != NULL; Walk = Walk->Parent)
        {
            if ((Walk->Populated++ == 0) && (Count < RTL_NUMBER_OF(Changed)))
                Changed[Count++] = Walk;
        }
    }
    Process->Cgroup = Cgroup;
    ExReleaseFastMutex(&LxCgroupLock);

    for (Index = 0; Index < Count; Index++)
    {
        if (Changed[Index]->Parent != NULL)
            LxCgroupNotifyEvents(Changed[Index]);
    }
    if (!IsListEmpty(&Process->Threads))
        LxSchedRefreshProcess(Process);
}

PLX_CGROUP
LxCgroupFromFile(
    _In_ PLX_FILE File)
{
    if ((File->Mount == NULL) || (File->Mount->Kind != LxFsCgroup) || (File->Type != LX_S_IFDIR))
        return NULL;
    return File->HandlerContext;
}

static
VOID
LxCgroupKill(
    _In_ PLX_INSTANCE Instance,
    _In_ PLX_CGROUP Cgroup)
{
    PLX_PROCESS Targets[128];
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index;
    LX_SIGINFO Info;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Count < RTL_NUMBER_OF(Targets)); Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);
        PLX_CGROUP Walk;

        if ((Process->Instance != Instance) || Process->Exited)
            continue;
        for (Walk = Process->Cgroup; Walk != NULL; Walk = Walk->Parent)
        {
            if (Walk == Cgroup)
                break;
        }
        if (Walk == NULL)
            continue;
        LxProcessReference(Process);
        Targets[Count++] = Process;
    }
    ExReleaseFastMutex(&LxProcessListLock);

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Code = LX_SI_KERNEL;
    for (Index = 0; Index < Count; Index++)
    {
        LxSignalSend(Targets[Index], NULL, LX_SIGKILL, &Info);
        LxProcessDereference(Targets[Index]);
    }
}

static
PLX_CGROUP
LxCgroupWalk(
    _In_ PLX_CGROUP Root,
    _Inout_updates_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PULONG Consumed)
{
    PLX_CGROUP Current = Root;
    ULONG Index;

    ExAcquireFastMutex(&LxCgroupLock);
    for (Index = 0; Index < Count; Index++)
    {
        PLX_CGROUP Child = LxCgroupFindChild(Current, Components[Index], strlen(Components[Index]));

        if (Child == NULL)
            break;
        Current = Child;
    }
    ExReleaseFastMutex(&LxCgroupLock);
    *Consumed = Index;
    return Current;
}

static
ULONG
LxSplitPath(
    _Inout_ PSTR Buffer,
    _Out_writes_(LX_PROC_MAX_COMPONENTS) PCSTR *Components)
{
    ULONG Count = 0;
    PSTR Cursor = Buffer;

    while (*Cursor != '\0')
    {
        while (*Cursor == '/')
            *Cursor++ = '\0';
        if (*Cursor == '\0')
            break;
        if (Count == LX_PROC_MAX_COMPONENTS)
            return MAXULONG;
        Components[Count++] = Cursor;
        while ((*Cursor != '\0') && (*Cursor != '/'))
            Cursor++;
    }
    return Count;
}

#define LX_TIMENS_MAX_SECONDS (MAXLONG64 / 1000000000LL / 2)

static
LONG64
LxTimeNsSeconds(
    _In_ LONG64 Offset)
{
    return (Offset >= 0) ? (Offset / 1000000000LL) : -((-Offset + 999999999LL) / 1000000000LL);
}

static
LONG64
LxTimeNsNanoseconds(
    _In_ LONG64 Offset)
{
    return Offset - LxTimeNsSeconds(Offset) * 1000000000LL;
}

static
VOID
LxGenPidTimensOffsets(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    LONG64 Monotonic = 0, Boottime = 0;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    if (Process->Ns[LxNsTime] != NULL)
    {
        Monotonic = Process->Ns[LxNsTime]->Time.Monotonic;
        Boottime = Process->Ns[LxNsTime]->Time.Boottime;
    }
    ExReleaseFastMutex(&Process->Lock);
    LxProcessDereference(Process);
    LxTextPrintf(Text, "%-10s %10lld %9ld\n%-10s %10lld %9ld\n",
                 "monotonic", LxTimeNsSeconds(Monotonic), (LONG)LxTimeNsNanoseconds(Monotonic),
                 "boottime", LxTimeNsSeconds(Boottime), (LONG)LxTimeNsNanoseconds(Boottime));
}

static
PCSTR
LxParseTimensField(
    _In_ PCSTR Data,
    _Out_writes_(Size) PSTR Field,
    _In_ ULONG Size)
{
    ULONG Length = 0;

    while ((*Data == ' ') || (*Data == '\t'))
        Data++;
    while ((*Data != '\0') && (*Data != ' ') && (*Data != '\t') && (*Data != '\n'))
    {
        if (Length + 1 >= Size)
            return NULL;
        Field[Length++] = *Data++;
    }
    Field[Length] = '\0';
    return (Length != 0) ? Data : NULL;
}

static
LONG
LxWritePidTimensOffsets(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    PLX_NS Ns;
    LONG64 Offsets[2], Seconds[2], Nanoseconds[2];
    ULONG Clocks[2], Count = 0, Item;
    BOOLEAN Set[2] = { FALSE, FALSE };
    LONG Error = 0;

    UNREFERENCED_PARAMETER(Length);
    while ((Error == 0) && (*Data != '\0') && (Count < RTL_NUMBER_OF(Clocks)))
    {
        CHAR Clock[16], SecondsText[24], NanosecondsText[16];

        while ((*Data == ' ') || (*Data == '\t') || (*Data == '\n'))
            Data++;
        if (*Data == '\0')
            break;
        Data = LxParseTimensField(Data, Clock, sizeof(Clock));
        if (Data != NULL)
            Data = LxParseTimensField(Data, SecondsText, sizeof(SecondsText));
        if (Data != NULL)
            Data = LxParseTimensField(Data, NanosecondsText, sizeof(NanosecondsText));
        while ((Data != NULL) && ((*Data == ' ') || (*Data == '\t')))
            Data++;
        if ((Data == NULL) || ((*Data != '\n') && (*Data != '\0')) ||
            (LxParseWriteNumber(SecondsText, &Seconds[Count]) != 0) ||
            (LxParseWriteNumber(NanosecondsText, &Nanoseconds[Count]) != 0) || (NanosecondsText[0] == '-') ||
            (Nanoseconds[Count] > 999999999))
        {
            Error = -LX_EINVAL;
            break;
        }
        if ((strcmp(Clock, "monotonic") == 0) || (strcmp(Clock, "1") == 0))
            Clocks[Count] = 0;
        else if ((strcmp(Clock, "boottime") == 0) || (strcmp(Clock, "7") == 0))
            Clocks[Count] = 1;
        else
            Error = -LX_EINVAL;
        Count++;
    }
    if (Count == 0)
        Error = -LX_EINVAL;
    if (Error != 0)
        return Error;
    if (Current == NULL)
        return -LX_ESRCH;
    Process = LxProcessLookup(Current->Process->Instance, (ULONG)(ULONG_PTR)File->HandlerContext);
    if (Process == NULL)
        return -LX_ESRCH;
    ExAcquireFastMutex(&Process->Lock);
    Ns = Process->Ns[LxNsTime];
    if (Ns != NULL)
        LxNsReference(Ns);
    ExReleaseFastMutex(&Process->Lock);
    LxProcessDereference(Process);
    if (Ns == NULL)
        return -LX_ESRCH;
    if (!LxNsCapable(Current->Process, LxNsOwner(Ns), LX_CAP_SYS_TIME))
    {
        LxNsDereference(Ns);
        return -LX_EPERM;
    }

    Offsets[0] = Ns->Time.Monotonic;
    Offsets[1] = Ns->Time.Boottime;
    for (Item = 0; (Item < Count) && (Error == 0); Item++)
    {
        ULONG Index = Clocks[Item];
        LONG64 Offset, Now;
        LX_TIMESPEC Sample;

        if ((Seconds[Item] > LX_TIMENS_MAX_SECONDS) || (Seconds[Item] < -LX_TIMENS_MAX_SECONDS))
        {
            Error = -LX_ERANGE;
            break;
        }
        Offset = Seconds[Item] * 1000000000LL + Nanoseconds[Item];
        LxQueryClock(Index ? LX_CLOCK_BOOTTIME : LX_CLOCK_MONOTONIC, &Sample);
        Now = Sample.Seconds * 1000000000LL + Sample.Nanoseconds;
        if ((Now + Offset < 0) || ((Now + Offset) / 1000000000LL > LX_TIMENS_MAX_SECONDS))
        {
            Error = -LX_ERANGE;
            break;
        }
        Offsets[Index] = Offset;
        Set[Index] = TRUE;
    }

    if ((Error == 0) && Ns->Time.Frozen)
        Error = -LX_EACCES;
    if (Error == 0)
    {
        if (Set[0])
            Ns->Time.Monotonic = Offsets[0];
        if (Set[1])
            Ns->Time.Boottime = Offsets[1];
    }
    LxNsDereference(Ns);
    return Error;
}

static
VOID
LxGenPidStat(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    LONG64 UserTime, KernelTime, ChildUser, ChildKernel;
    PETHREAD Target = NULL;
    PLIST_ENTRY Entry;
    LX_USAGE Usage;
    ULONG64 ChildFaults;
    CHAR Comm[16];
    ULONG Threads;
    CHAR State;
    LONG64 Start;
    BOOLEAN Mapped;
    PLX_TTY Tty;
    ULONG TtyNumber = 0;
    LONG Foreground = -1;

    if (Process == NULL)
        return;
    Tty = LxProcessGetTty(Process);
    if (Tty != NULL)
    {
        TtyNumber = (ULONG)LxTtyDevice(Tty);
        Foreground = (LONG)LxGlobalToNrIn(LxProcPidNs(Self, Synth), Tty->ForegroundPgrp);
        LxTtyDereference(Tty);
    }
    LxProcessUsage(Process, NULL, &Usage);
    Mapped = !Process->Exited;
    State = Process->Exited ? 'Z' : ((Process == Self) ? 'R' : 'S');
    ExAcquireFastMutex(&Process->Lock);
    RtlCopyMemory(Comm, Process->Comm, sizeof(Comm));
    Comm[15] = '\0';
    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if ((Synth->Tid != 0) && (Thread->Tid != Synth->Tid))
            continue;
        if ((State == 'S') && (Thread->CurrentSyscall == MAXULONG))
            State = 'R';
        if ((State != 'Z') && Thread->Stopped && ((Synth->Tid != 0) || (Thread->Tid == Process->Pid)))
            State = 'T';
        if ((State != 'Z') && LxPtraceStopped(Thread) && ((Synth->Tid != 0) || (Thread->Tid == Process->Pid)))
            State = 't';
        if ((Synth->Tid != 0) && (Thread->EThread != NULL))
        {
            Target = Thread->EThread;
            ObReferenceObject(Target);
        }
    }
    ChildUser = Process->ChildUserTime;
    ChildKernel = Process->ChildKernelTime;
    ChildFaults = Process->ChildMinFlt;
    ExReleaseFastMutex(&Process->Lock);
    UserTime = Usage.UserTime;
    KernelTime = Usage.KernelTime;
    if (Target != NULL)
    {
        ULONG UserTicks = 0;
        ULONG KernelTicks = KeQueryRuntimeThread(&Target->Tcb, &UserTicks);

        UserTime = (LONG64)UserTicks * KeQueryTimeIncrement();
        KernelTime = (LONG64)KernelTicks * KeQueryTimeIncrement();
        ObDereferenceObject(Target);
    }
    Threads = Process->Exited ? 1 : (ULONG)max(Process->ThreadCount, 1);
    Start = (Process->StartTime + LxTimeNsOffset(Self, LX_CLOCK_BOOTTIME) / 100) / LX_PROC_TICKS;

    LxTextPrintf(Text, "%lu (%s) %c %lu %lu %lu %lu %ld %u %lu %lu %lu %lu %llu %llu %lld %lld %ld %ld %lu %ld %lld ",
                 LxGlobalToNrIn(LxProcPidNs(Self, Synth), Synth->Tid ? Synth->Tid : Process->Pid), Comm, State,
                 LxPidNsIsInit(Process) ? 0UL : LxGlobalToNrIn(LxProcPidNs(Self, Synth), Process->ParentPid),
                 LxGlobalToNrIn(LxProcPidNs(Self, Synth), Process->Pgid), LxGlobalToNrIn(LxProcPidNs(Self, Synth), Process->Sid),
                 TtyNumber, Foreground,
                 0x400000U, (ULONG)Usage.MinFlt, (ULONG)ChildFaults, 0UL, 0UL, (ULONG64)UserTime / LX_PROC_TICKS,
                 (ULONG64)KernelTime / LX_PROC_TICKS, ChildUser / LX_PROC_TICKS, ChildKernel / LX_PROC_TICKS, 20L, 0L,
                 Threads, 0L, Start);
    LxTextPrintf(Text, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %ld %ld %lu %lu %llu %llu %lld ",
                 Mapped ? Usage.VirtualSize : 0ULL, Mapped ? Usage.WorkingSet / LX_PAGE_SIZE : 0ULL,
                 Process->Limits[LX_RLIMIT_RSS].Cur, Mapped ? Process->StartCode : 0ULL, Mapped ? Process->EndCode : 0ULL,
                 Mapped ? Process->StartStack : 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL,
                 (LONG)Process->ExitSignal, 0L, 0UL, 0UL, 0ULL, 0ULL, 0LL);
    LxTextPrintf(Text, "%llu %llu %llu %llu %llu %llu %llu %ld\n",
                 Mapped ? Process->StartData : 0ULL, Mapped ? Process->EndData : 0ULL, Mapped ? Process->BrkBase : 0ULL,
                 Mapped ? Process->ArgStart : 0ULL, Mapped ? Process->ArgEnd : 0ULL, Mapped ? Process->EnvStart : 0ULL,
                 Mapped ? Process->EnvEnd : 0ULL, Process->Exited ? (LONG)Process->WaitStatus : 0L);
    LxProcessDereference(Process);
}

static
VOID
LxGenPidStatus(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_NS ProcNs;
    ULONG64 Ignored = 0, Caught = 0, Blocked = 0, Affinity = LxSchedActiveMask();
    ULONG SeccompMode = 0, SeccompFilters = 0;
    LONG64 Spec = -LX_EINVAL;
    PCSTR SpecText;
    ULONG TracerPid = 0;
    BOOLEAN TraceStopped = FALSE, NoNewPrivs = FALSE;
    PLIST_ENTRY Entry;
    CHAR Comm[16], List[256];
    LX_CRED Cred;
    LONG Signal;
    ULONG Index, Fds = 0;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    Cred = Process->Cred;
    RtlCopyMemory(Comm, Process->Comm, sizeof(Comm));
    Comm[15] = '\0';
    for (Signal = 0; Signal < LX_NSIG; Signal++)
    {
        if (Process->SigAction[Signal].Handler == LX_SIG_IGN)
            Ignored |= 1ULL << Signal;
        else if (Process->SigAction[Signal].Handler != LX_SIG_DFL)
            Caught |= 1ULL << Signal;
    }
    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if ((Synth->Tid == 0) || (Thread->Tid == Synth->Tid))
        {
            if (Synth->Tid != 0)
                Cred = *Thread->Cred;
            Blocked = Thread->SigMask;
            Affinity &= Thread->Affinity;
            SeccompMode = Thread->SeccompMode;
            SeccompFilters = LxSeccompFilterCount(Thread);
            NoNewPrivs = Thread->NoNewPrivs;
            Spec = LxSpecCtrlGet(Thread, LX_PR_SPEC_STORE_BYPASS);
            TracerPid = LxPtraceTracerPid(Self, Thread);
            TraceStopped = LxPtraceStopped(Thread);
            break;
        }
    }
    for (Index = 0; Index < LX_MAX_FDS; Index++)
    {
        if (Process->Fds[Index] != NULL)
            Fds = Index + 1;
    }
    ExReleaseFastMutex(&Process->Lock);
    LxFormatCpuList(Affinity, List, sizeof(List));

    ProcNs = LxProcPidNs(Self, Synth);
    LxTextPrintf(Text,
                 "Name:\t%s\nUmask:\t%04lo\nState:\t%s\nTgid:\t%lu\nNgid:\t0\nPid:\t%lu\nPPid:\t%lu\nTracerPid:\t%lu\n"
                 "Uid:\t%lu\t%lu\t%lu\t%lu\nGid:\t%lu\t%lu\t%lu\t%lu\nFDSize:\t%lu\nGroups:\t",
                 Comm, Process->Umask,
                 Process->Exited ? "Z (zombie)" :
                 (TraceStopped ? "t (tracing stop)" :
                  ((Process->JobctlFlags & LX_JOBCTL_STOPPED) ? "T (stopped)" : "S (sleeping)")),
                 LxPidNrIn(ProcNs, Process),
                 LxGlobalToNrIn(ProcNs, Synth->Tid ? Synth->Tid : Process->Pid),
                 LxPidNsIsInit(Process) ? 0UL : LxGlobalToNrIn(ProcNs, Process->ParentPid), TracerPid,
                 LxCredViewId(Self, FALSE, Cred.Uid[0]), LxCredViewId(Self, FALSE, Cred.Uid[1]),
                 LxCredViewId(Self, FALSE, Cred.Uid[2]), LxCredViewId(Self, FALSE, Cred.Fsuid),
                 LxCredViewId(Self, TRUE, Cred.Gid[0]), LxCredViewId(Self, TRUE, Cred.Gid[1]),
                 LxCredViewId(Self, TRUE, Cred.Gid[2]), LxCredViewId(Self, TRUE, Cred.Fsgid), max(Fds, 64UL));
    for (Index = 0; Index < Cred.GroupCount; Index++)
        LxTextPrintf(Text, "%lu ", LxCredViewId(Self, TRUE, Cred.Groups[Index]));
    LxTextAppend(Text, "\n", 1);
    {
        static const PCSTR Labels[4] = { "NStgid", "NSpid", "NSpgid", "NSsid" };
        ULONG First = ProcNs->Level, Last = (Process->PidNs != NULL) ? Process->PidNs->Level : 0, Level, Which;
        ULONG Tids[LX_PIDNS_MAX_LEVEL];
        PLX_NS Levels[LX_PIDNS_MAX_LEVEL];
        PLX_NS Walk;

        RtlCopyMemory(Tids, Process->NsPid, sizeof(Tids));
        ExAcquireFastMutex(&Process->Lock);
        for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
        {
            PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

            if ((Synth->Tid != 0) && (Thread->Tid == Synth->Tid))
                RtlCopyMemory(Tids, Thread->NsTid, sizeof(Tids));
        }
        ExReleaseFastMutex(&Process->Lock);
        RtlZeroMemory(Levels, sizeof(Levels));
        for (Walk = Process->PidNs; Walk != NULL; Walk = Walk->Parent)
            Levels[Walk->Level] = Walk;
        for (Which = 0; Which < 4; Which++)
        {
            LxTextPrintf(Text, "%s:", Labels[Which]);
            for (Level = First; (Level <= Last) && (Levels[Level] != NULL); Level++)
            {
                ULONG Value;

                if (Which == 0)
                    Value = Process->NsPid[Level];
                else if (Which == 1)
                    Value = Tids[Level];
                else
                    Value = LxGlobalToNrIn(Levels[Level], (Which == 2) ? Process->Pgid : Process->Sid);
                LxTextPrintf(Text, "\t%lu", Value);
            }
            LxTextAppend(Text, "\n", 1);
        }
    }
    if (!Process->Exited)
    {
        LX_USAGE Usage;

        LxProcessUsage(Process, NULL, &Usage);
        LxTextPrintf(Text,
                     "Kthread:\t0\n"
                     "VmPeak:\t%8llu kB\nVmSize:\t%8llu kB\nVmLck:\t%8llu kB\nVmPin:\t       0 kB\n"
                     "VmHWM:\t%8llu kB\nVmRSS:\t%8llu kB\nRssAnon:\t       0 kB\nRssFile:\t       0 kB\n"
                     "RssShmem:\t       0 kB\nVmData:\t%8llu kB\nVmStk:\t       0 kB\nVmExe:\t%8llu kB\n"
                     "VmLib:\t       0 kB\nVmPTE:\t       0 kB\nVmSwap:\t       0 kB\nHugetlbPages:\t       0 kB\n",
                     Usage.PeakVirtualSize / 1024, Usage.VirtualSize / 1024, LxMmLockedBytes(Process) / 1024,
                     Usage.MaxRss / 1024, Usage.WorkingSet / 1024,
                     Usage.PrivateBytes / 1024, (Process->EndCode - Process->StartCode) / 1024);
    }
    else
    {
        LxTextAppend(Text, "Kthread:\t0\n", 10);
    }
    LxTextPrintf(Text,
                 "CoreDumping:\t0\nTHP_enabled:\t%d\nuntag_mask:\t0xffffffffffffff\nThreads:\t%ld\nSigQ:\t0/63429\n",
                 Process->ThpDisable != 1, Process->Exited ? 1L : (LONG)Process->ThreadCount);
    switch (Spec)
    {
        case -LX_EINVAL:
            SpecText = "unknown";
            break;
        case LX_PR_SPEC_NOT_AFFECTED:
            SpecText = "not vulnerable";
            break;
        case LX_PR_SPEC_PRCTL | LX_PR_SPEC_FORCE_DISABLE:
            SpecText = "thread force mitigated";
            break;
        case LX_PR_SPEC_PRCTL | LX_PR_SPEC_DISABLE:
            SpecText = "thread mitigated";
            break;
        case LX_PR_SPEC_PRCTL | LX_PR_SPEC_ENABLE:
            SpecText = "thread vulnerable";
            break;
        case LX_PR_SPEC_DISABLE:
            SpecText = "globally mitigated";
            break;
        default:
            SpecText = "vulnerable";
            break;
    }
    LxTextPrintf(Text,
                 "SigPnd:\t%016llx\nShdPnd:\t%016llx\nSigBlk:\t%016llx\nSigIgn:\t%016llx\nSigCgt:\t%016llx\n"
                 "CapInh:\t%016llx\nCapPrm:\t%016llx\nCapEff:\t%016llx\nCapBnd:\t%016llx\nCapAmb:\t%016llx\n"
                 "NoNewPrivs:\t%u\nSeccomp:\t%lu\nSeccomp_filters:\t%lu\nSpeculation_Store_Bypass:\t%s\n"
                 "SpeculationIndirectBranch:\tunknown\nCpus_allowed:\t%llx\nCpus_allowed_list:\t%s\n"
                 "Mems_allowed:\t1\nMems_allowed_list:\t0\nvoluntary_ctxt_switches:\t0\nnonvoluntary_ctxt_switches:\t0\n",
                 0ULL, (ULONG64)Process->Pending, Blocked, Ignored, Caught, Cred.CapInheritable,
                 Cred.CapPermitted, Cred.CapEffective,
                 Cred.CapBounding, Cred.CapAmbient, NoNewPrivs ? 1U : 0U, SeccompMode, SeccompFilters,
                 SpecText, Affinity, List);
    LxProcessDereference(Process);
}

static
SIZE_T
LxProcReadMemory(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    PEPROCESS EProcess;
    SIZE_T Copied = 0;

    if (Process->Exited || !NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Process->NtPid), &EProcess)))
        return 0;
    if (LxMmPrepareRemote(Process, EProcess, Address, Length, FALSE) == 0)
        MmCopyVirtualMemory(EProcess, (PVOID)(ULONG_PTR)Address, PsGetCurrentProcess(), Buffer, Length, KernelMode, &Copied);
    ObDereferenceObject(EProcess);
    return Copied;
}

static
VOID
LxProcAppendMemory(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ BOOLEAN StopAtNul,
    _Inout_ PLX_TEXT Text)
{
    CHAR Buffer[512];

    while (Start < End)
    {
        SIZE_T Chunk = (SIZE_T)min(End - Start, sizeof(Buffer));
        SIZE_T Got = LxProcReadMemory(Process, Start, Buffer, Chunk);

        if (Got == 0)
            return;
        if (StopAtNul)
        {
            PCHAR Nul = memchr(Buffer, 0, Got);

            if (Nul != NULL)
            {
                LxTextAppend(Text, Buffer, (ULONG)(Nul - Buffer) + 1);
                return;
            }
        }
        LxTextAppend(Text, Buffer, (ULONG)Got);
        if (Got < Chunk)
            return;
        Start += Got;
    }
}

static
VOID
LxGenPidCmdline(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG64 ArgStart, ArgEnd, EnvStart, EnvEnd;
    CHAR Last = 0;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    ArgStart = Process->ArgStart;
    ArgEnd = Process->ArgEnd;
    EnvStart = Process->EnvStart;
    EnvEnd = Process->EnvEnd;
    if ((ArgEnd <= ArgStart) && (Process->Cmdline != NULL) && !Process->Exited)
        LxTextAppend(Text, Process->Cmdline, Process->CmdlineLength);
    ExReleaseFastMutex(&Process->Lock);
    if ((ArgEnd > ArgStart) && (LxProcReadMemory(Process, ArgEnd - 1, &Last, 1) == 1))
    {
        if (Last == 0)
            LxProcAppendMemory(Process, ArgStart, ArgEnd, FALSE, Text);
        else
            LxProcAppendMemory(Process, ArgStart, min(((EnvStart == ArgEnd) ? EnvEnd : ArgEnd), ArgStart + LX_PAGE_SIZE),
                               TRUE, Text);
    }
    LxProcessDereference(Process);
}

static
VOID
LxGenPidEnviron(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG64 EnvStart, EnvEnd;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    EnvStart = Process->EnvStart;
    EnvEnd = Process->EnvEnd;
    if ((EnvEnd <= EnvStart) && (Process->Environ != NULL) && !Process->Exited)
        LxTextAppend(Text, Process->Environ, Process->EnvironLength);
    ExReleaseFastMutex(&Process->Lock);
    if (EnvEnd > EnvStart)
        LxProcAppendMemory(Process, EnvStart, EnvEnd, FALSE, Text);
    LxProcessDereference(Process);
}

static
VOID
LxGenTaskChildren(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLIST_ENTRY Entry;

    if (Process == NULL)
        return;
    if (Synth->Tid == Process->Pid)
    {
        ExAcquireFastMutex(&Process->Lock);
        for (Entry = Process->Children.Flink; Entry != &Process->Children; Entry = Entry->Flink)
        {
            ULONG Nr = LxGlobalToNrIn(LxProcPidNs(Self, Synth), CONTAINING_RECORD(Entry, LX_CHILD, Link)->Pid);

            if (Nr != 0)
                LxTextPrintf(Text, "%lu ", Nr);
        }
        ExReleaseFastMutex(&Process->Lock);
    }
    LxProcessDereference(Process);
}

static
VOID
LxGenPidStatm(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    LX_USAGE Usage;
    ULONG64 Code = 0;

    if (Process == NULL)
        return;
    LxProcessUsage(Process, NULL, &Usage);
    if (Process->Exited)
        RtlZeroMemory(&Usage, sizeof(Usage));
    else if (Process->EndCode > Process->StartCode)
        Code = (LX_ALIGN_UP(Process->EndCode, LX_PAGE_SIZE) - LX_ALIGN_DOWN(Process->StartCode, LX_PAGE_SIZE)) / LX_PAGE_SIZE;
    LxTextPrintf(Text, "%llu %llu 0 %llu 0 %llu 0\n", Usage.VirtualSize / LX_PAGE_SIZE, Usage.WorkingSet / LX_PAGE_SIZE,
                 Code, Usage.PrivateBytes / LX_PAGE_SIZE);
    LxProcessDereference(Process);
}

static
VOID
LxGenPidAuxv(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG Words = 0;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    if (!Process->Exited)
    {
        while ((Words + 2 <= LX_AUXV_WORDS) && (Process->Auxv[Words] != 0))
            Words += 2;
        if (Words + 2 <= LX_AUXV_WORDS)
            Words += 2;
        LxTextAppend(Text, (PCSTR)Process->Auxv, Words * sizeof(ULONG64));
    }
    ExReleaseFastMutex(&Process->Lock);
    LxProcessDereference(Process);
}

static
VOID
LxGenPidComm(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = NULL;
    PLX_THREAD Thread = NULL;
    CHAR Comm[16];

    if (Synth->Tid != 0)
        Thread = LxThreadLookupReferenced(Self->Instance, Synth->Tid, &Process);
    else
        Process = LxProcessLookup(Self->Instance, Synth->Pid);
    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    RtlCopyMemory(Comm, (Thread != NULL) ? Thread->Comm : Process->Comm, sizeof(Comm));
    ExReleaseFastMutex(&Process->Lock);
    Comm[15] = '\0';
    LxTextPrintf(Text, "%s\n", Comm);
    LxProcessDereference(Process);
}

static
LONG
LxWritePidComm(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Pid = (ULONG)(ULONG_PTR)File->HandlerContext;
    PLX_PROCESS Process;
    CHAR Comm[16];
    ULONG Count = min(Length, 15);

    if (Current == NULL)
        return -LX_ESRCH;
    RtlZeroMemory(Comm, sizeof(Comm));
    RtlCopyMemory(Comm, Data, Count);
    if ((Count != 0) && (Comm[Count - 1] == '\n'))
        Comm[Count - 1] = '\0';

    Process = LxProcessLookup(Current->Process->Instance, Pid);
    if (Process == NULL)
        return -LX_ESRCH;
    ExAcquireFastMutex(&Process->Lock);
    RtlCopyMemory(Process->Comm, Comm, sizeof(Comm));
    ExReleaseFastMutex(&Process->Lock);
    LxProcessPublishName(Process, NULL);
    LxProcessDereference(Process);
    return 0;
}

static
VOID
LxGenPidCgroup(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_CGROUP Root = LxProcessNs(Self, LxNsCgroup)->Cgroup.Root;
    PSTR Path, RootPath;

    if (Process == NULL)
        return;
    Path = LxCgroupPath(Process->Cgroup);
    RootPath = ((Root != NULL) && (Root->Parent != NULL)) ? LxCgroupPath(Root) : NULL;
    if (Path != NULL)
    {
        SIZE_T Length = (RootPath != NULL) ? strlen(RootPath) : 0;

        if ((Length > 1) && (strncmp(Path, RootPath, Length) == 0) && ((Path[Length] == '\0') || (Path[Length] == '/')))
            LxTextPrintf(Text, "0::%s\n", (Path[Length] == '\0') ? "/" : Path + Length);
        else if (Length > 1)
            LxTextPrintf(Text, "0::/..%s\n", Path);
        else
            LxTextPrintf(Text, "0::%s\n", Path);
        LxFree(Path);
    }
    LxFree(RootPath);
    LxProcessDereference(Process);
}

static
VOID
LxGenPidMountinfo(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG Length;
    PSTR Info;

    if (Process == NULL)
        return;
    Info = LxMountInfo((Process == Self) ? Self : Process, TRUE, &Length);
    LxProcessDereference(Process);
    if (Info != NULL)
    {
        LxTextAppend(Text, Info, Length);
        LxFree(Info);
    }
}

static
VOID
LxGenPidMounts(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG Length;
    PSTR Info;

    if (Process == NULL)
        return;
    Info = LxMountInfo((Process == Self) ? Self : Process, FALSE, &Length);
    LxProcessDereference(Process);
    if (Info != NULL)
    {
        LxTextAppend(Text, Info, Length);
        LxFree(Info);
    }
}

static
VOID
LxGenPidOomScoreAdj(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

    if (Process == NULL)
        return;
    LxTextPrintf(Text, "%ld\n", Process->OomScoreAdj);
    LxProcessDereference(Process);
}

static
LONG
LxWritePidOomScoreAdj(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    LONG64 Value;
    LONG Error;

    UNREFERENCED_PARAMETER(Length);
    if (Current == NULL)
        return -LX_ESRCH;
    Error = LxParseWriteNumber(Data, &Value);
    if (Error != 0)
        return Error;
    if ((Value < -1000) || (Value > 1000))
        return -LX_EINVAL;
    Process = LxProcessLookup(Current->Process->Instance, (ULONG)(ULONG_PTR)File->HandlerContext);
    if (Process == NULL)
        return -LX_ESRCH;
    Process->OomScoreAdj = (LONG)Value;
    LxProcessDereference(Process);
    return 0;
}

static
VOID
LxGenPidCoreFilter(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

    if (Process == NULL)
        return;
    LxTextPrintf(Text, "%08lx\n", Process->CoreFilter);
    LxProcessDereference(Process);
}

static
LONG
LxWritePidCoreFilter(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    ULONG64 Value = 0;
    ULONG Index = 0, Base = 10, Digits = 0;

    if (Current == NULL)
        return -LX_ESRCH;
    while ((Length != 0) && ((Data[Length - 1] == '\n') || (Data[Length - 1] == '\0')))
        Length--;
    if ((Length >= 2) && (Data[0] == '0') && ((Data[1] == 'x') || (Data[1] == 'X')))
    {
        Base = 16;
        Index = 2;
    }
    else if ((Length >= 2) && (Data[0] == '0'))
    {
        Base = 8;
        Index = 1;
    }
    for (; Index < Length; Index++, Digits++)
    {
        CHAR Character = Data[Index];
        ULONG Digit;

        if ((Character >= '0') && (Character <= '9'))
            Digit = (ULONG)(Character - '0');
        else if ((Character >= 'a') && (Character <= 'f'))
            Digit = (ULONG)(Character - 'a' + 10);
        else if ((Character >= 'A') && (Character <= 'F'))
            Digit = (ULONG)(Character - 'A' + 10);
        else
            return -LX_EINVAL;
        if (Digit >= Base)
            return -LX_EINVAL;
        Value = Value * Base + Digit;
        if (Value > MAXULONG)
            return -LX_ERANGE;
    }
    if ((Digits == 0) && (Base != 8))
        return -LX_EINVAL;
    Process = LxProcessLookup(Current->Process->Instance, (ULONG)(ULONG_PTR)File->HandlerContext);
    if (Process == NULL)
        return -LX_ESRCH;
    Process->CoreFilter = (ULONG)Value;
    LxProcessDereference(Process);
    return 0;
}

static
VOID
LxGenCorePattern(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    CHAR Pattern[256];

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxCoreGetPattern(Pattern, sizeof(Pattern));
    LxTextPrintf(Text, "%s\n", Pattern);
}

static
LONG
LxWriteCorePattern(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (!LxCapable(Current->Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    return LxCoreSetPattern(Data, Length);
}

static
LONG
LxWriteAccept(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

static
VOID
LxGenPidLimits(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    static const PCSTR Names[16] =
    {
        "Max cpu time", "Max file size", "Max data size", "Max stack size", "Max core file size",
        "Max resident set", "Max processes", "Max open files", "Max locked memory", "Max address space",
        "Max file locks", "Max pending signals", "Max msgqueue size", "Max nice priority",
        "Max realtime priority", "Max realtime timeout"
    };
    static const PCSTR Units[16] =
    {
        "seconds", "bytes", "bytes", "bytes", "bytes", "bytes", "processes", "files", "bytes", "bytes",
        "locks", "signals", "bytes", "", "", "us"
    };
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG Index;

    if (Process == NULL)
        return;
    LxTextPrintf(Text, "%-25s %-20s %-20s %-10s\n", "Limit", "Soft Limit", "Hard Limit", "Units");
    for (Index = 0; Index < 16; Index++)
    {
        CHAR Soft[24], Hard[24];

        if (Process->Limits[Index].Cur == LX_RLIM_INFINITY)
            RtlStringCbCopyA(Soft, sizeof(Soft), "unlimited");
        else
            RtlStringCbPrintfA(Soft, sizeof(Soft), "%llu", Process->Limits[Index].Cur);
        if (Process->Limits[Index].Max == LX_RLIM_INFINITY)
            RtlStringCbCopyA(Hard, sizeof(Hard), "unlimited");
        else
            RtlStringCbPrintfA(Hard, sizeof(Hard), "%llu", Process->Limits[Index].Max);
        LxTextPrintf(Text, "%-25s %-20s %-20s %-10s\n", Names[Index], Soft, Hard, Units[Index]);
    }
    LxProcessDereference(Process);
}

static
VOID
LxGenSmapsEntry(
    _In_ HANDLE Handle,
    _In_ PLX_VMA Vma,
    _Inout_ PLX_TEXT Text)
{
    static const PCSTR Names[] =
    {
        "Size:", "KernelPageSize:", "MMUPageSize:", "Rss:", "Pss:", "Pss_Dirty:", "Shared_Clean:", "Shared_Dirty:",
        "Private_Clean:", "Private_Dirty:", "Referenced:", "Anonymous:", "KSM:", "LazyFree:", "AnonHugePages:",
        "ShmemPmdMapped:", "FilePmdMapped:", "Shared_Hugetlb:", "Private_Hugetlb:", "Swap:", "SwapPss:", "Locked:"
    };
    ULONG64 Values[RTL_NUMBER_OF(Names)] = { 0 };
    ULONG64 Rss = (Handle != NULL) ? LxMmVmaResident(Handle, Vma) / 1024 : 0;
    BOOLEAN Shared = (Vma->Flags & LX_MAP_SHARED) != 0;
    ULONG Index;

    Values[0] = (Vma->End - Vma->Start) / 1024;
    Values[1] = Values[2] = LX_PAGE_SIZE / 1024;
    Values[3] = Values[4] = Values[10] = Rss;
    if (Vma->File == NULL)
    {
        Values[5] = Values[9] = Rss;
        if (!Shared)
            Values[11] = Rss;
    }
    else
    {
        Values[Shared ? 6 : 8] = Rss;
    }
    if (Vma->Flags & LX_VMA_LOCKED)
        Values[21] = Rss;

    for (Index = 0; Index < RTL_NUMBER_OF(Names); Index++)
        LxTextPrintf(Text, "%-16s%8llu kB\n", Names[Index], Values[Index]);
    LxTextPrintf(Text, "THPeligible:    %8u\nVmFlags: %s%s%s%s%s%s%s%s%s%s\n", 0,
                 (Vma->Prot & LX_PROT_READ) ? "rd " : "",
                 (Vma->Prot & LX_PROT_WRITE) ? "wr " : "",
                 (Vma->Prot & LX_PROT_EXEC) ? "ex " : "",
                 Shared ? "sh " : "",
                 "mr ",
                 (Vma->Flags & LX_VMA_NOMAYWRITE) ? "" : "mw ",
                 "me ",
                 (Vma->Flags & (LX_VMA_STACK | LX_MAP_GROWSDOWN)) ? "gd " : "",
                 (Vma->Flags & LX_VMA_LOCKED) ? "lo " : "",
                 (!Shared && (Vma->Prot & LX_PROT_WRITE)) ? "ac " : "");
}

static
VOID
LxGenPidMapsCommon(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text,
    _In_ BOOLEAN Smaps)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PEPROCESS EProcess = NULL;
    HANDLE Handle = NULL;
    PLIST_ENTRY Entry;

    if (Process == NULL)
        return;

    if (Smaps)
    {
        if (Process->Mm->NtPid == HandleToUlong(PsGetCurrentProcessId()))
        {
            Handle = NtCurrentProcess();
        }
        else if (!Process->Exited &&
                 NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Process->Mm->NtPid), &EProcess)))
        {
            if (!NT_SUCCESS(ObOpenObjectByPointer(EProcess, OBJ_KERNEL_HANDLE, NULL, PROCESS_QUERY_INFORMATION,
                                                  *PsProcessType, KernelMode, &Handle)))
            {
                Handle = NULL;
            }
            ObDereferenceObject(EProcess);
        }
    }

    LxMmLock(Process);
    for (Entry = Process->Mm->Vmas.Flink; Entry != &Process->Mm->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PCSTR Name = NULL;
        CHAR Line[128];
        ULONG Length;

        if (Vma->File != NULL)
            Name = Vma->File->Path;
        else if (Vma->Flags & LX_VMA_HEAP)
            Name = "[heap]";
        else if (Vma->Flags & LX_VMA_STACK)
            Name = "[stack]";

        if (!NT_SUCCESS(RtlStringCbPrintfA(Line, sizeof(Line), "%08llx-%08llx %c%c%c%c %08llx 00:00 %llu ",
                                           Vma->Start,
                                           Vma->End,
                                           (Vma->Prot & LX_PROT_READ) ? 'r' : '-',
                                           (Vma->Prot & LX_PROT_WRITE) ? 'w' : '-',
                                           (Vma->Prot & LX_PROT_EXEC) ? 'x' : '-',
                                           (Vma->Flags & LX_MAP_SHARED) ? 's' : 'p',
                                           Vma->Offset,
                                           (Vma->File != NULL) ? Vma->File->Ino : 0ULL)))
        {
            continue;
        }
        Length = (ULONG)strlen(Line);
        LxTextAppend(Text, Line, Length);
        if (Name != NULL)
        {
            for (; Length < 73; Length++)
                LxTextAppend(Text, " ", 1);
            LxTextAppend(Text, Name, (ULONG)strlen(Name));
        }
        LxTextAppend(Text, "\n", 1);
        if (Smaps)
            LxGenSmapsEntry(Handle, Vma, Text);
    }
    LxMmUnlock(Process);
    if ((Handle != NULL) && (Handle != NtCurrentProcess()))
        ZwClose(Handle);
    LxProcessDereference(Process);
}

static
VOID
LxGenPidMaps(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenPidMapsCommon(Self, Synth, Text, FALSE);
}

static
VOID
LxGenPidSmaps(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenPidMapsCommon(Self, Synth, Text, TRUE);
}

static
VOID
LxGenPidSyscall(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLIST_ENTRY Entry;
    ULONG Number = MAXULONG;
    ULONG64 Args[6] = { 0 };
    BOOLEAN Found = FALSE;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
    for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
    {
        PLX_THREAD Thread = CONTAINING_RECORD(Entry, LX_THREAD, Link);

        if (Thread->Tid == (Synth->Tid ? Synth->Tid : Process->Pid))
        {
            Number = Thread->CurrentSyscall;
            RtlCopyMemory(Args, Thread->CurrentArgs, sizeof(Args));
            Found = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&Process->Lock);
    LxProcessDereference(Process);

    if (!Found || (Number == MAXULONG))
        LxTextAppend(Text, "running\n", 8);
    else
        LxTextPrintf(Text, "%lu 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x0 0x0\n",
                     Number, Args[0], Args[1], Args[2], Args[3], Args[4], Args[5]);
}

static
VOID
LxGenPidIdMap(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text,
    _In_ BOOLEAN Group)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_NS UserNs, Lower;
    PLX_ID_MAP Map;
    ULONG Index;

    if (Process == NULL)
        return;
    UserNs = LxUserNsOf(Process);
    LxProcessDereference(Process);
    Lower = LxCredUserNs(Self);
    if ((Lower == UserNs) && (UserNs->Parent != NULL))
        Lower = UserNs->Parent;
    if (UserNs->Parent == NULL)
    {
        LxTextPrintf(Text, "%10lu %10lu %10lu\n", 0UL, LxIdFromKernel(Lower, Group, 0), MAXULONG);
    }
    else
    {
        Map = LxUserNsMap(UserNs, Group);
        for (Index = 0; (Map != NULL) && (Index < Map->Count); Index++)
        {
            LxTextPrintf(Text, "%10lu %10lu %10lu\n", Map->Extents[Index].First,
                         LxIdFromKernel(Lower, Group, Map->Extents[Index].LowerFirst), Map->Extents[Index].Count);
        }
    }
    LxNsDereference(UserNs);
}

static
VOID
LxGenPidUidMap(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenPidIdMap(Self, Synth, Text, FALSE);
}

static
VOID
LxGenPidGidMap(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenPidIdMap(Self, Synth, Text, TRUE);
}

static
VOID
LxGenPidSetgroups(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_NS UserNs;

    if (Process == NULL)
        return;
    UserNs = LxUserNsOf(Process);
    LxProcessDereference(Process);
    LxTextAppend(Text, UserNs->User.SetgroupsDenied ? "deny\n" : "allow\n", UserNs->User.SetgroupsDenied ? 5 : 6);
    LxNsDereference(UserNs);
}

static
PLX_NS
LxPidFileUserNs(
    _In_ PLX_FILE File,
    _Out_ PLX_PROCESS *Writer)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    PLX_NS UserNs;

    *Writer = NULL;
    if (Current == NULL)
        return NULL;
    Process = LxProcessLookup(Current->Process->Instance, (ULONG)(ULONG_PTR)File->HandlerContext);
    if (Process == NULL)
        return NULL;
    UserNs = LxUserNsOf(Process);
    LxProcessDereference(Process);
    *Writer = Current->Process;
    return UserNs;
}

static
LONG
LxWritePidIdMap(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _In_ BOOLEAN Group)
{
    PLX_PROCESS Writer;
    PLX_NS UserNs = LxPidFileUserNs(File, &Writer);
    LONG Error;

    if (UserNs == NULL)
        return -LX_ESRCH;
    Error = LxUserNsWriteMap(Writer, UserNs, Group, Data, Length);
    LxNsDereference(UserNs);
    return Error;
}

static
LONG
LxWritePidUidMap(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    return LxWritePidIdMap(File, Data, Length, FALSE);
}

static
LONG
LxWritePidGidMap(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    return LxWritePidIdMap(File, Data, Length, TRUE);
}

static
LONG
LxWritePidSetgroups(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_PROCESS Writer;
    PLX_NS UserNs = LxPidFileUserNs(File, &Writer);
    LONG Error;

    if (UserNs == NULL)
        return -LX_ESRCH;
    Error = LxUserNsWriteSetgroups(Writer, UserNs, Data, Length);
    LxNsDereference(UserNs);
    return Error;
}

static
HANDLE
LxPagemapHandle(
    _In_ PLX_PROCESS Target)
{
    PEPROCESS EProcess;
    HANDLE Handle = NULL;

    if (Target->Mm->NtPid == HandleToUlong(PsGetCurrentProcessId()))
        return NtCurrentProcess();
    if (NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Target->Mm->NtPid), &EProcess)))
    {
        if (!NT_SUCCESS(ObOpenObjectByPointer(EProcess, OBJ_KERNEL_HANDLE, NULL, PROCESS_QUERY_INFORMATION,
                                              *PsProcessType, KernelMode, &Handle)))
        {
            Handle = NULL;
        }
        ObDereferenceObject(EProcess);
    }
    return Handle;
}

static
LONG64
LxPagemapRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    PLX_PROCESS Target = File->PidProcess;
    PLX_THREAD Current = LxCurrentThread();
    ULONG64 Entries[512], Limit = (ULONG64)(ULONG_PTR)MmHighestUserAddress + 1;
    LONG64 Position = (Offset != NULL) ? *Offset : File->Position, Done = 0, Result = 0;
    BOOLEAN Frames;
    HANDLE Handle;

    if ((Position < 0) || (Position % sizeof(ULONG64)) || (Length % sizeof(ULONG64)))
        return -LX_EINVAL;
    if ((Target == NULL) || Target->Exited)
        return 0;
    Handle = LxPagemapHandle(Target);
    if (Handle == NULL)
        return 0;
    Frames = (Handle == NtCurrentProcess()) && (Current != NULL) && LxCapable(Current->Process, LX_CAP_SYS_ADMIN);

    while ((ULONG64)Done < Length)
    {
        ULONG64 Address = ((ULONG64)(Position + Done) / sizeof(ULONG64)) * LX_PAGE_SIZE;
        ULONG Pages;

        if (Address >= Limit)
            break;
        Pages = (ULONG)min(min((Length - (ULONG64)Done) / sizeof(ULONG64), RTL_NUMBER_OF(Entries)),
                           (Limit - Address) / LX_PAGE_SIZE);
        LxMmPagemap(Target, Handle, Address, Pages, Frames, Entries);
        Result = LxCopyToUser(Buffer + (ULONG64)Done, Entries, (SIZE_T)Pages * sizeof(ULONG64));
        if (Result != 0)
            break;
        Done += (LONG64)Pages * sizeof(ULONG64);
    }
    if (Handle != NtCurrentProcess())
        ZwClose(Handle);
    if (Done == 0)
        return Result;
    if (Offset != NULL)
        *Offset = Position + Done;
    else
        File->Position = Position + Done;
    return Done;
}

static
LONG64
LxPagemapSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    LONG64 Base;

    if (Whence == LX_SEEK_SET)
        Base = 0;
    else if (Whence == LX_SEEK_CUR)
        Base = File->Position;
    else
        return -LX_EINVAL;
    if (Base + Offset < 0)
        return -LX_EINVAL;
    File->Position = Base + Offset;
    return File->Position;
}

static
LONG
LxPagemapStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFREG | 0400;
    Stat->Nlink = 1;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 0;
    return 0;
}

static const LX_FILE_OPS LxPagemapOps =
{
    LxPagemapRead, NULL, NULL, NULL, LxPagemapStat, LxPagemapSeek, NULL, NULL, NULL
};

static
LONG
LxPagemapOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Pid,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    PLX_PROCESS Target = LxProcessLookup(Process->Instance, Pid);
    PLX_FILE File;

    *Result = NULL;
    if (Target == NULL)
        return -LX_ESRCH;
    if ((Flags & LX_O_ACCMODE) != LX_O_RDONLY)
    {
        LxProcessDereference(Target);
        return -LX_EACCES;
    }
    if ((Target != Process) && !LxPtraceMayRead(Process, Target))
    {
        LxProcessDereference(Target);
        return -LX_EACCES;
    }
    File = LxFileAlloc(&LxPagemapOps, LX_O_RDONLY, LX_S_IFREG);
    if (File == NULL)
    {
        LxProcessDereference(Target);
        return -LX_ENOMEM;
    }
    File->Mode = 0400;
    File->PidProcess = Target;
    *Result = File;
    return 0;
}

static
LONG64
LxPidMemTransfer(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset,
    _In_ BOOLEAN Write)
{
    PLX_PROCESS Target = File->PidProcess;
    ULONG64 Address = (ULONG64)((Offset != NULL) ? *Offset : File->Position), Done = 0;
    PEPROCESS EProcess;
    PUCHAR Page;
    LONG64 Result = 0;

    if ((Target == NULL) || Target->Exited)
        return 0;
    if (!NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Target->Mm->NtPid), &EProcess)))
        return 0;
    Page = LxAlloc(LX_PAGE_SIZE);
    if (Page == NULL)
    {
        ObDereferenceObject(EProcess);
        return -LX_ENOMEM;
    }
    while (Done < Length)
    {
        ULONG Size = (ULONG)min(Length - Done, LX_PAGE_SIZE - ((Address + Done) & (LX_PAGE_SIZE - 1)));

        if (Write)
        {
            Result = LxCopyFromUser(Page, Buffer + Done, Size);
            if (Result == 0)
                Result = LxMmForceAccess(Target, EProcess, Address + Done, Page, Size, TRUE);
        }
        else
        {
            Result = LxMmForceAccess(Target, EProcess, Address + Done, Page, Size, FALSE);
            if (Result == 0)
                Result = LxCopyToUser(Buffer + Done, Page, Size);
        }
        if (Result != 0)
            break;
        Done += Size;
    }
    LxFree(Page);
    ObDereferenceObject(EProcess);
    if (Done == 0)
        return Result;
    if (Offset != NULL)
        *Offset = (LONG64)(Address + Done);
    else
        File->Position = (LONG64)(Address + Done);
    return (LONG64)Done;
}

static
LONG64
LxPidMemRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    return LxPidMemTransfer(File, Buffer, Length, Offset, FALSE);
}

static
LONG64
LxPidMemWrite(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    return LxPidMemTransfer(File, Buffer, Length, Offset, TRUE);
}

static
LONG
LxPidMemStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    LxPagemapStat(File, Stat);
    Stat->Mode = LX_S_IFREG | 0600;
    return 0;
}

static const LX_FILE_OPS LxPidMemOps =
{
    LxPidMemRead, LxPidMemWrite, NULL, NULL, LxPidMemStat, LxPagemapSeek, NULL, NULL, NULL
};

static
LONG
LxPidMemOpen(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Pid,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    PLX_PROCESS Target = LxProcessLookup(Process->Instance, Pid);
    PLX_FILE File;

    *Result = NULL;
    if (Target == NULL)
        return -LX_ESRCH;
    if (LxPtraceAccessCheck(Process, Target) != 0)
    {
        LxProcessDereference(Target);
        return -LX_EACCES;
    }
    File = LxFileAlloc(&LxPidMemOps, Flags & LX_O_ACCMODE, LX_S_IFREG);
    if (File == NULL)
    {
        LxProcessDereference(Target);
        return -LX_ENOMEM;
    }
    File->Mode = 0600;
    File->PidProcess = Target;
    *Result = File;
    return 0;
}

static volatile LONG LxAuditSessionId;

static
LONG
LxParseWrittenNumber(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _Out_ PULONG Value);

static
VOID
LxGenPidLoginuid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    ULONG LoginUid;

    if (Process == NULL)
        return;
    LoginUid = Process->LoginUid;
    LxProcessDereference(Process);
    if (LoginUid != MAXULONG)
        LoginUid = LxIdFromKernel(LxCredUserNs(Self), FALSE, LoginUid);
    LxTextPrintf(Text, "%lu", LoginUid);
}

static
VOID
LxGenPidSessionid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

    if (Process == NULL)
        return;
    LxTextPrintf(Text, "%lu", Process->SessionId);
    LxProcessDereference(Process);
}

static
LONG
LxWritePidLoginuid(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    ULONG Pid = (ULONG)(ULONG_PTR)File->HandlerContext, Value, LoginUid, SessionId = MAXULONG;
    LONG Error;

    if ((Current == NULL) || (Current->Process->Pid != Pid))
        return -LX_EPERM;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if (Error != 0)
        return Error;
    Process = Current->Process;
    LoginUid = (Value == MAXULONG) ? MAXULONG : LxIdToKernel(LxCredUserNs(Process), FALSE, Value);
    if ((Value != MAXULONG) && (LoginUid == MAXULONG))
        return -LX_EINVAL;
    if ((Process->LoginUid != MAXULONG) && !LxCapable(Process, LX_CAP_AUDIT_CONTROL))
        return -LX_EPERM;
    if (LoginUid != MAXULONG)
    {
        SessionId = (ULONG)InterlockedIncrement(&LxAuditSessionId);
        if (SessionId == MAXULONG)
            SessionId = (ULONG)InterlockedIncrement(&LxAuditSessionId);
    }
    ExAcquireFastMutex(&Process->Lock);
    Process->LoginUid = LoginUid;
    Process->SessionId = SessionId;
    ExReleaseFastMutex(&Process->Lock);
    return 0;
}

static
VOID
LxGenPidPersonality(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

    if (Process == NULL)
        return;
    LxTextPrintf(Text, "%08lx\n", Process->Personality);
    LxProcessDereference(Process);
}

static const LX_PID_ENTRY LxPidEntries[] =
{
    { "attr", LX_S_IFDIR, 0555, NULL, NULL, NULL },
    { "auxv", LX_S_IFREG, 0400, NULL, LxGenPidAuxv, NULL },
    { "cgroup", LX_S_IFREG, 0444, NULL, LxGenPidCgroup, NULL },
    { "children", LX_S_IFREG, 0444, NULL, LxGenTaskChildren, NULL },
    { "cmdline", LX_S_IFREG, 0444, NULL, LxGenPidCmdline, NULL },
    { "comm", LX_S_IFREG, 0644, NULL, LxGenPidComm, LxWritePidComm },
    { "coredump_filter", LX_S_IFREG, 0644, NULL, LxGenPidCoreFilter, LxWritePidCoreFilter },
    { "cwd", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "environ", LX_S_IFREG, 0400, NULL, LxGenPidEnviron, NULL },
    { "exe", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "fd", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "fdinfo", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "gid_map", LX_S_IFREG, 0644, NULL, LxGenPidGidMap, LxWritePidGidMap },
    { "io", LX_S_IFREG, 0400, "rchar: 0\nwchar: 0\nsyscr: 0\nsyscw: 0\nread_bytes: 0\nwrite_bytes: 0\ncancelled_write_bytes: 0\n", NULL, NULL },
    { "limits", LX_S_IFREG, 0444, NULL, LxGenPidLimits, NULL },
    { "loginuid", LX_S_IFREG, 0644, NULL, LxGenPidLoginuid, LxWritePidLoginuid },
    { "map_files", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "maps", LX_S_IFREG, 0444, NULL, LxGenPidMaps, NULL },
    { "mem", LX_S_IFREG, 0600, NULL, NULL, NULL },
    { "mountinfo", LX_S_IFREG, 0444, NULL, LxGenPidMountinfo, NULL },
    { "mounts", LX_S_IFREG, 0444, NULL, LxGenPidMounts, NULL },
    { "mountstats", LX_S_IFREG, 0400, "", NULL, NULL },
    { "ns", LX_S_IFDIR, 0511, NULL, NULL, NULL },
    { "oom_adj", LX_S_IFREG, 0644, "0\n", NULL, LxWriteAccept },
    { "oom_score", LX_S_IFREG, 0444, "0\n", NULL, NULL },
    { "oom_score_adj", LX_S_IFREG, 0644, NULL, LxGenPidOomScoreAdj, LxWritePidOomScoreAdj },
    { "pagemap", LX_S_IFREG, 0400, NULL, NULL, NULL },
    { "personality", LX_S_IFREG, 0400, NULL, LxGenPidPersonality, NULL },
    { "root", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "sessionid", LX_S_IFREG, 0444, NULL, LxGenPidSessionid, NULL },
    { "setgroups", LX_S_IFREG, 0644, NULL, LxGenPidSetgroups, LxWritePidSetgroups },
    { "smaps", LX_S_IFREG, 0444, NULL, LxGenPidSmaps, NULL },
    { "stat", LX_S_IFREG, 0444, NULL, LxGenPidStat, NULL },
    { "statm", LX_S_IFREG, 0444, NULL, LxGenPidStatm, NULL },
    { "status", LX_S_IFREG, 0444, NULL, LxGenPidStatus, NULL },
    { "syscall", LX_S_IFREG, 0400, NULL, LxGenPidSyscall, NULL },
    { "task", LX_S_IFDIR, 0555, NULL, NULL, NULL },
    { "timens_offsets", LX_S_IFREG, 0644, NULL, LxGenPidTimensOffsets, LxWritePidTimensOffsets },
    { "timerslack_ns", LX_S_IFREG, 0666, "50000\n", NULL, LxWriteAccept },
    { "uid_map", LX_S_IFREG, 0644, NULL, LxGenPidUidMap, LxWritePidUidMap },
    { "wchan", LX_S_IFREG, 0444, "0", NULL, NULL },
};

static
VOID
LxGenKernelConfig(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Length;
    PUCHAR Gzip = LxKernelConfigGzip(&Length);

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    if (Gzip == NULL)
    {
        Text->Failed = TRUE;
        return;
    }
    LxTextAppend(Text, (PCSTR)Gzip, Length);
    LxFree(Gzip);
}

static
VOID
LxGenBootId(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%s\n", Self->Instance->BootId);
}

static
VOID
LxGenUuid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UCHAR Random[16];
    ULONG Index;

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    if (!NT_SUCCESS(LxFillRandom(Random, sizeof(Random))))
        return;
    Random[6] = (Random[6] & 0x0F) | 0x40;
    Random[8] = (Random[8] & 0x3F) | 0x80;
    for (Index = 0; Index < 16; Index++)
    {
        if ((Index == 4) || (Index == 6) || (Index == 8) || (Index == 10))
            LxTextAppend(Text, "-", 1);
        LxTextPrintf(Text, "%02x", Random[Index]);
    }
    LxTextAppend(Text, "\n", 1);
}

static
VOID
LxGenHostname(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    ExAcquireFastMutex(&Self->Instance->Lock);
    LxTextPrintf(Text, "%s\n", LxProcessNs(Self, LxNsUts)->Uts.Hostname);
    ExReleaseFastMutex(&Self->Instance->Lock);
}

static
VOID
LxGenDomainname(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    ExAcquireFastMutex(&Self->Instance->Lock);
    LxTextPrintf(Text, "%s\n", LxProcessNs(Self, LxNsUts)->Uts.DomainName);
    ExReleaseFastMutex(&Self->Instance->Lock);
}

static
LONG
LxWriteName(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _In_ BOOLEAN Domain)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_INSTANCE Instance;
    PLX_NS Uts;
    ULONG Count = Length;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    if (Current == NULL)
        return -LX_ESRCH;
    while ((Count != 0) && (Data[Count - 1] == '\n'))
        Count--;
    if (Count > 64)
        return -LX_EINVAL;
    Instance = Current->Process->Instance;
    Uts = LxProcessNs(Current->Process, LxNsUts);
    ExAcquireFastMutex(&Instance->Lock);
    if (Domain)
    {
        RtlCopyMemory(Uts->Uts.DomainName, Data, Count);
        Uts->Uts.DomainName[Count] = '\0';
    }
    else
    {
        RtlCopyMemory(Uts->Uts.Hostname, Data, Count);
        Uts->Uts.Hostname[Count] = '\0';
    }
    ExReleaseFastMutex(&Instance->Lock);
    return 0;
}

static
VOID
LxGenNsLastPid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%lu\n", LxPidNsLastPid(Self));
}

static
LONG
LxParseWrittenNumber(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _Out_ PULONG Value)
{
    CHAR Number[16];
    ULONG Count = 0;

    *Value = 0;
    while ((Count < Length) && ((Data[Count] == ' ') || (Data[Count] == '\t')))
        Count++;
    Data += Count;
    Length -= Count;
    for (Count = 0; (Count < Length) && (Data[Count] >= '0') && (Data[Count] <= '9'); Count++)
    {
        if (Count >= sizeof(Number) - 1)
            return -LX_EINVAL;
        Number[Count] = Data[Count];
    }
    Number[Count] = '\0';
    if ((Count == 0) || ((Count < Length) && (Data[Count] != '\n')) || !LxParseNumber(Number, Value))
        return -LX_EINVAL;
    return 0;
}

static
LONG
LxWriteNsLastPid(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxPidNsSetLastPid(Current->Process, Value);
}

static
LONG
LxWriteMqLimit(
    _In_ LX_MQ_LIMIT Which,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxMqSetLimit(Current->Process, Which, Value);
}

#define LX_MQ_SYSCTL(Name, Which) \
    static VOID LxGenMq##Name(_In_ PLX_PROCESS Self, _In_ PLX_SYNTH Synth, _Inout_ PLX_TEXT Text) \
    { \
        UNREFERENCED_PARAMETER(Synth); \
        LxTextPrintf(Text, "%lu\n", LxMqGetLimit(Self, Which)); \
    } \
    static LONG LxWriteMq##Name(_In_ PLX_FILE File, _In_reads_bytes_(Length) PCSTR Data, _In_ ULONG Length) \
    { \
        UNREFERENCED_PARAMETER(File); \
        return LxWriteMqLimit(Which, Data, Length); \
    }

LX_MQ_SYSCTL(MsgDefault, LxMqMsgDefault)
LX_MQ_SYSCTL(MsgMax, LxMqMsgMax)
LX_MQ_SYSCTL(MsgSizeDefault, LxMqMsgSizeDefault)
LX_MQ_SYSCTL(MsgSizeMax, LxMqMsgSizeMax)
LX_MQ_SYSCTL(QueuesMax, LxMqQueuesMax)

static
LONG
LxWriteHostname(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    return LxWriteName(File, Data, Length, FALSE);
}

static
LONG
LxWriteDomainname(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    return LxWriteName(File, Data, Length, TRUE);
}

static
VOID
LxGenRelease(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%s\n", LX_KERNEL_RELEASE);
}

static
VOID
LxGenKernelVersion(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%s\n", LX_KERNEL_VERSION);
}

static
LONG
LxWriteTrace(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_PROCESS Process;
    ULONG Pid = 0, Level = 0;
    PCSTR Cursor = Data;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    if (Current == NULL)
        return -LX_ESRCH;
    while ((*Cursor >= '0') && (*Cursor <= '9'))
        Pid = Pid * 10 + (*Cursor++ - '0');
    while (*Cursor == ' ')
        Cursor++;
    while ((*Cursor >= '0') && (*Cursor <= '9'))
        Level = Level * 10 + (*Cursor++ - '0');
    Process = LxProcessLookup(Current->Process->Instance, Pid);
    if (Process == NULL)
        return -LX_ESRCH;
    Process->Trace = Level;
    LxProcessDereference(Process);
    return 0;
}

static
VOID
LxGenOverflowId(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text,
    _In_ BOOLEAN Group)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%lu\n", LxCredOverflowId(Group));
}

static
VOID
LxGenOverflowUid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenOverflowId(Self, Synth, Text, FALSE);
}

static
VOID
LxGenOverflowGid(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenOverflowId(Self, Synth, Text, TRUE);
}

static
LONG
LxWriteOverflowId(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _In_ BOOLEAN Group)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxCredSetOverflowId(Current->Process, Group, Value);
}

static
LONG
LxWriteOverflowUid(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    return LxWriteOverflowId(Data, Length, FALSE);
}

static
LONG
LxWriteOverflowGid(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    return LxWriteOverflowId(Data, Length, TRUE);
}

static
VOID
LxGenMaxUserNamespaces(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxUserNsGetMax(Self));
}

static
LONG
LxWriteMaxUserNamespaces(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxUserNsSetMax(Current->Process, Value);
}

static volatile LONG64 LxTainted;

static
VOID
LxGenDmesgRestrict(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxKlogDmesgRestrictValue());
}

static
LONG
LxWriteDmesgRestrict(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (!LxCapable(Current->Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if ((Error == 0) && (Value > 1))
        Error = -LX_EINVAL;
    if (Error == 0)
        LxKlogSetDmesgRestrict((LONG)Value);
    return Error;
}

static
VOID
LxGenPrintk(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LONG Values[4];

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxKlogGetPrintk(Values);
    LxTextPrintf(Text, "%ld\t%ld\t%ld\t%ld\n", Values[0], Values[1], Values[2], Values[3]);
}

static
LONG
LxWritePrintk(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    return LxKlogWritePrintk(Data, Length);
}

static
LONG
LxWriteRatelimitValue(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _Inout_ PLONG Target)
{
    ULONG Value;
    LONG Error;

    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if ((Error == 0) && (Value > MAXLONG))
        Error = -LX_EINVAL;
    if (Error == 0)
        InterlockedExchange(Target, (LONG)Value);
    return Error;
}

static
VOID
LxGenPrintkRatelimit(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxPrintkRatelimitSeconds);
}

static
LONG
LxWritePrintkRatelimit(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    return LxWriteRatelimitValue(Data, Length, &LxPrintkRatelimitSeconds);
}

static
VOID
LxGenPrintkRatelimitBurst(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxPrintkRatelimitBurst);
}

static
LONG
LxWritePrintkRatelimitBurst(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    return LxWriteRatelimitValue(Data, Length, &LxPrintkRatelimitBurst);
}

static
VOID
LxGenPrintkDevkmsg(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%s\n", LxKlogDevkmsgName());
}

static
LONG
LxWritePrintkDevkmsg(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    return LxKlogSetDevkmsg(Data, Length);
}

static
VOID
LxGenYamaScope(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxYamaScopeValue());
}

static
LONG
LxWriteYamaScope(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxYamaSetScope(Current->Process, Value);
}

static
VOID
LxGenRandomizeVaSpace(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxMmRandomizeVaSpace());
}

static
LONG
LxWriteRandomizeVaSpace(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if ((Error == 0) && (Value > MAXLONG))
        Error = -LX_EINVAL;
    if (Error == 0)
        LxMmSetRandomizeVaSpace((LONG)Value);
    return Error;
}

static
VOID
LxGenMmapRndBits(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%lu\n", LxMmRndBits());
}

static
LONG
LxWriteMmapRndBits(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    return (Error != 0) ? Error : LxMmSetRndBits(Value);
}

static
VOID
LxGenSeccompActions(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text,
    _In_ BOOLEAN Logged)
{
    CHAR Buffer[128];

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxSeccompFormatActions(Logged, Buffer, sizeof(Buffer));
    LxTextPrintf(Text, "%s\n", Buffer);
}

static
VOID
LxGenSeccompActionsAvail(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenSeccompActions(Self, Synth, Text, FALSE);
}

static
VOID
LxGenSeccompActionsLogged(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LxGenSeccompActions(Self, Synth, Text, TRUE);
}

static
LONG
LxWriteSeccompActionsLogged(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (Length == 0)
        return 0;
    return LxSeccompWriteActionsLogged(Current->Process, Data, Length);
}

static
VOID
LxGenTainted(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%llu\n", (ULONG64)LxTainted);
}

static
LONG
LxWriteTainted(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Current == NULL)
        return -LX_ESRCH;
    if (!LxCapable(Current->Process, LX_CAP_SYS_ADMIN))
        return -LX_EPERM;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if (Error == 0)
        InterlockedOr64(&LxTainted, Value);
    return Error;
}

static
VOID
LxGenMaxMapCount(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%ld\n", LxMaxMapCount);
}

static
VOID
LxGenHugePages(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PCSTR Name = strrchr(Synth->Prefix, '/');
    ULONG64 Total, Free, Surplus, Reserved;

    UNREFERENCED_PARAMETER(Self);
    Name = (Name != NULL) ? Name + 1 : Synth->Prefix;
    Total = LxHugeQueryPool(&Free, &Surplus, &Reserved);
    if (strcmp(Name, "free_hugepages") == 0)
        LxTextPrintf(Text, "%llu\n", Free);
    else if (strcmp(Name, "surplus_hugepages") == 0)
        LxTextPrintf(Text, "%llu\n", Surplus);
    else if (strcmp(Name, "resv_hugepages") == 0)
        LxTextPrintf(Text, "%llu\n", Reserved);
    else if (strcmp(Name, "nr_overcommit_hugepages") == 0)
        LxTextAppend(Text, "0\n", 2);
    else
        LxTextPrintf(Text, "%llu\n", Total);
}

static
LONG
LxWriteHugePages(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if ((Error == 0) && (LxHugePageSize() == 0))
        Error = -LX_EINVAL;
    if (Error == 0)
        LxHugeSetPool(Value);
    return Error;
}

static
LONG
LxWriteMaxMapCount(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    ULONG Value;
    LONG Error;

    UNREFERENCED_PARAMETER(File);
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if ((Error == 0) && (Value > MAXLONG))
        Error = -LX_EINVAL;
    if (Error == 0)
        InterlockedExchange(&LxMaxMapCount, (LONG)Value);
    return Error;
}

static const PCSTR LxNetSysctlPaths[LX_NET_SYSCTL_COUNT] =
{
    "net/core/somaxconn",
    "net/ipv4/conf/all/tag",
    "net/ipv4/conf/default/tag",
    "net/ipv4/conf/lo/tag"
};

static
LONG
LxNetSysctlIndex(
    _In_ PCSTR Path)
{
    LONG Index;

    for (Index = 0; Index < LX_NET_SYSCTL_COUNT; Index++)
    {
        if (strcmp(LxNetSysctlPaths[Index], Path) == 0)
            return Index;
    }
    return -1;
}

static
VOID
LxGenNetSysctl(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text);

static
LONG
LxWriteNetSysctl(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length);

static
VOID
LxGenKeySysctl(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text);

static
LONG
LxWriteKeySysctl(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length);

static const LX_TABLE_ENTRY LxProcSysTable[] =
{
    { "fs/file-max", 0644, "9223372036854775807\n", NULL, LxWriteAccept },
    { "fs/file-nr", 0444, "1024\t0\t9223372036854775807\n", NULL, NULL },
    { "fs/inotify/max_queued_events", 0644, "16384\n", NULL, LxWriteAccept },
    { "fs/inotify/max_user_instances", 0644, "128\n", NULL, LxWriteAccept },
    { "fs/inotify/max_user_watches", 0644, "65536\n", NULL, LxWriteAccept },
    { "fs/mqueue/msg_default", 0644, NULL, LxGenMqMsgDefault, LxWriteMqMsgDefault },
    { "fs/mqueue/msg_max", 0644, NULL, LxGenMqMsgMax, LxWriteMqMsgMax },
    { "fs/mqueue/msgsize_default", 0644, NULL, LxGenMqMsgSizeDefault, LxWriteMqMsgSizeDefault },
    { "fs/mqueue/msgsize_max", 0644, NULL, LxGenMqMsgSizeMax, LxWriteMqMsgSizeMax },
    { "fs/mqueue/queues_max", 0644, NULL, LxGenMqQueuesMax, LxWriteMqQueuesMax },
    { "fs/nr_open", 0644, "1048576\n", NULL, LxWriteAccept },
    { "fs/protected_fifos", 0644, "1\n", NULL, LxWriteAccept },
    { "fs/protected_hardlinks", 0644, "1\n", NULL, LxWriteAccept },
    { "fs/protected_regular", 0644, "2\n", NULL, LxWriteAccept },
    { "fs/protected_symlinks", 0644, "1\n", NULL, LxWriteAccept },
    { "fs/suid_dumpable", 0644, "2\n", NULL, LxWriteAccept },
    { "kernel/cap_last_cap", 0444, "40\n", NULL, NULL },
    { "kernel/core_pattern", 0644, NULL, LxGenCorePattern, LxWriteCorePattern },
    { "kernel/core_pipe_limit", 0644, "0\n", NULL, LxWriteAccept },
    { "kernel/dmesg_restrict", 0644, NULL, LxGenDmesgRestrict, LxWriteDmesgRestrict },
    { "kernel/domainname", 0644, NULL, LxGenDomainname, LxWriteDomainname },
    { "kernel/hostname", 0644, NULL, LxGenHostname, LxWriteHostname },
    { "kernel/keys/gc_delay", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/keys/maxbytes", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/keys/maxkeys", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/keys/persistent_keyring_expiry", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/keys/root_maxbytes", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/keys/root_maxkeys", 0644, NULL, LxGenKeySysctl, LxWriteKeySysctl },
    { "kernel/kptr_restrict", 0644, "1\n", NULL, LxWriteAccept },
    { "kernel/ngroups_max", 0444, "65536\n", NULL, NULL },
    { "kernel/ns_last_pid", 0666, NULL, LxGenNsLastPid, LxWriteNsLastPid },
    { "kernel/osrelease", 0444, NULL, LxGenRelease, NULL },
    { "kernel/ostype", 0444, "Linux\n", NULL, NULL },
    { "kernel/overflowgid", 0644, NULL, LxGenOverflowGid, LxWriteOverflowGid },
    { "kernel/overflowuid", 0644, NULL, LxGenOverflowUid, LxWriteOverflowUid },
    { "kernel/panic", 0644, "0\n", NULL, LxWriteAccept },
    { "kernel/pid_max", 0644, "4194304\n", NULL, LxWriteAccept },
    { "kernel/printk", 0644, NULL, LxGenPrintk, LxWritePrintk },
    { "kernel/printk_devkmsg", 0644, NULL, LxGenPrintkDevkmsg, LxWritePrintkDevkmsg },
    { "kernel/printk_ratelimit", 0644, NULL, LxGenPrintkRatelimit, LxWritePrintkRatelimit },
    { "kernel/printk_ratelimit_burst", 0644, NULL, LxGenPrintkRatelimitBurst, LxWritePrintkRatelimitBurst },
    { "kernel/random/boot_id", 0444, NULL, LxGenBootId, NULL },
    { "kernel/random/entropy_avail", 0444, "256\n", NULL, NULL },
    { "kernel/random/poolsize", 0444, "256\n", NULL, NULL },
    { "kernel/random/uuid", 0444, NULL, LxGenUuid, NULL },
    { "kernel/randomize_va_space", 0644, NULL, LxGenRandomizeVaSpace, LxWriteRandomizeVaSpace },
    { "kernel/seccomp/actions_avail", 0444, NULL, LxGenSeccompActionsAvail, NULL },
    { "kernel/seccomp/actions_logged", 0644, NULL, LxGenSeccompActionsLogged, LxWriteSeccompActionsLogged },
    { "kernel/sysrq", 0644, "176\n", NULL, LxWriteAccept },
    { "kernel/tainted", 0644, NULL, LxGenTainted, LxWriteTainted },
    { "kernel/threads-max", 0644, "63429\n", NULL, LxWriteAccept },
    { "kernel/yama/ptrace_scope", 0644, NULL, LxGenYamaScope, LxWriteYamaScope },
    { "kernel/unprivileged_bpf_disabled", 0644, "2\n", NULL, LxWriteAccept },
    { "kernel/version", 0444, LX_KERNEL_UTS_VERSION "\n", NULL, NULL },
    { "lxcore/trace", 0200, "", NULL, LxWriteTrace },
    { "net/core/somaxconn", 0644, NULL, LxGenNetSysctl, LxWriteNetSysctl },
    { "net/ipv4/conf/all/tag", 0644, NULL, LxGenNetSysctl, LxWriteNetSysctl },
    { "net/ipv4/conf/default/tag", 0644, NULL, LxGenNetSysctl, LxWriteNetSysctl },
    { "net/ipv4/conf/lo/tag", 0644, NULL, LxGenNetSysctl, LxWriteNetSysctl },
    { "net/unix/max_dgram_qlen", 0644, "512\n", NULL, LxWriteAccept },
    { "user/max_user_namespaces", 0644, NULL, LxGenMaxUserNamespaces, LxWriteMaxUserNamespaces },
    { "vm/compact_memory", 0200, "", NULL, LxWriteAccept },
    { "vm/drop_caches", 0200, "", NULL, LxWriteAccept },
    { "vm/max_map_count", 0644, NULL, LxGenMaxMapCount, LxWriteMaxMapCount },
    { "vm/mmap_rnd_bits", 0600, NULL, LxGenMmapRndBits, LxWriteMmapRndBits },
    { "vm/nr_hugepages", 0644, NULL, LxGenHugePages, LxWriteHugePages },
    { "vm/nr_overcommit_hugepages", 0644, NULL, LxGenHugePages, LxWriteAccept },
    { "vm/overcommit_memory", 0644, "0\n", NULL, LxWriteAccept },
    { "vm/swappiness", 0644, "60\n", NULL, LxWriteAccept },
};

static
VOID
LxGenNetSysctl(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LONG Index = LxNetSysctlIndex(LxProcSysTable[Synth->Index].Path);

    if (Index >= 0)
        LxTextPrintf(Text, "%ld\n", LxProcessNs(Self, LxNsNet)->Net.Sysctl[Index]);
}

static
LONG
LxWriteNetSysctl(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Table = (ULONG)(ULONG_PTR)File->HandlerContext;
    CHAR Buffer[32];
    LONG64 Value;
    LONG Index, Error;

    if (Current == NULL)
        return -LX_ESRCH;
    if (Table >= RTL_NUMBER_OF(LxProcSysTable))
        return -LX_EINVAL;
    Index = LxNetSysctlIndex(LxProcSysTable[Table].Path);
    if ((Index < 0) || (Length == 0) || (Length >= sizeof(Buffer)))
        return -LX_EINVAL;
    RtlCopyMemory(Buffer, Data, Length);
    Buffer[Length] = '\0';
    Error = LxParseWriteNumber(Buffer, &Value);
    if (Error != 0)
        return Error;
    if ((Value < -(LONG64)MAXLONG - 1) || (Value > MAXLONG))
        return -LX_EINVAL;
    LxProcessNs(Current->Process, LxNsNet)->Net.Sysctl[Index] = (LONG)Value;
    return 0;
}

static
VOID
LxGenKeySysctl(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LONG Value;

    UNREFERENCED_PARAMETER(Self);
    if (LxKeySysctlGet(LxProcSysTable[Synth->Index].Path, &Value))
        LxTextPrintf(Text, "%ld\n", Value);
}

static
LONG
LxWriteKeySysctl(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    ULONG Table = (ULONG)(ULONG_PTR)File->HandlerContext;
    ULONG Value;
    LONG Error;

    if (Table >= RTL_NUMBER_OF(LxProcSysTable))
        return -LX_EINVAL;
    if (Length == 0)
        return 0;
    Error = LxParseWrittenNumber(Data, Length, &Value);
    if (Error == 0)
        Error = LxKeySysctlSet(LxProcSysTable[Table].Path, Value);
    return Error;
}

static
VOID
LxGenCpuOnline(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Count = KeQueryActiveProcessorCount(NULL);

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    if (Count <= 1)
        LxTextAppend(Text, "0\n", 2);
    else
        LxTextPrintf(Text, "0-%lu\n", Count - 1);
}

#define LX_FIRMWARE_RSMB 0x52534D42

typedef struct _LX_SMBIOS_RAW
{
    UCHAR Used20CallingMethod;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    UCHAR DmiRevision;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} LX_SMBIOS_RAW, *PLX_SMBIOS_RAW;

typedef enum _LX_DMI_KIND
{
    LxDmiText,
    LxDmiRelease,
    LxDmiChassisType,
    LxDmiUuid
} LX_DMI_KIND;

typedef struct _LX_DMI_FIELD
{
    PCSTR Name;
    UCHAR Type;
    UCHAR Offset;
    LX_DMI_KIND Kind;
} LX_DMI_FIELD;

static const LX_DMI_FIELD LxDmiFields[] =
{
    { "bios_date", 0, 0x08, LxDmiText },
    { "bios_release", 0, 0x14, LxDmiRelease },
    { "bios_vendor", 0, 0x04, LxDmiText },
    { "bios_version", 0, 0x05, LxDmiText },
    { "board_asset_tag", 2, 0x08, LxDmiText },
    { "board_name", 2, 0x05, LxDmiText },
    { "board_serial", 2, 0x07, LxDmiText },
    { "board_vendor", 2, 0x04, LxDmiText },
    { "board_version", 2, 0x06, LxDmiText },
    { "chassis_asset_tag", 3, 0x08, LxDmiText },
    { "chassis_serial", 3, 0x07, LxDmiText },
    { "chassis_type", 3, 0x05, LxDmiChassisType },
    { "chassis_vendor", 3, 0x04, LxDmiText },
    { "chassis_version", 3, 0x06, LxDmiText },
    { "ec_firmware_release", 0, 0x16, LxDmiRelease },
    { "product_family", 1, 0x1A, LxDmiText },
    { "product_name", 1, 0x05, LxDmiText },
    { "product_serial", 1, 0x07, LxDmiText },
    { "product_sku", 1, 0x19, LxDmiText },
    { "product_uuid", 1, 0x08, LxDmiUuid },
    { "product_version", 1, 0x06, LxDmiText },
    { "sys_vendor", 1, 0x04, LxDmiText },
};

static
PCSTR
LxDmiStructureString(
    _In_reads_bytes_(Limit) PUCHAR Structure,
    _In_ PUCHAR Limit,
    _In_ UCHAR Index)
{
    PUCHAR Cursor = Structure + Structure[1];
    PCSTR Blank;

    if (Index == 0)
        return NULL;
    while ((--Index != 0) && (Cursor < Limit) && (*Cursor != 0))
    {
        while ((Cursor < Limit) && (*Cursor != 0))
            Cursor++;
        Cursor++;
    }
    if ((Cursor >= Limit) || (*Cursor == 0))
        return NULL;
    for (Blank = (PCSTR)Cursor; *Blank == ' '; Blank++)
        ;
    return (*Blank != 0) ? (PCSTR)Cursor : NULL;
}

static
VOID
LxGenDmi(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PCSTR Name = strrchr(Synth->Prefix, '/');
    const LX_DMI_FIELD *Field = NULL;
    PLX_SMBIOS_RAW Raw = NULL;
    ULONG Length = 0, Index;
    PUCHAR Cursor, Limit;

    UNREFERENCED_PARAMETER(Self);
    Name = (Name != NULL) ? Name + 1 : Synth->Prefix;
    for (Index = 0; Index < RTL_NUMBER_OF(LxDmiFields); Index++)
    {
        if (strcmp(LxDmiFields[Index].Name, Name) == 0)
            Field = &LxDmiFields[Index];
    }
    if ((Field == NULL) ||
        (ExGetSystemFirmwareTable(LX_FIRMWARE_RSMB, 0, NULL, 0, &Length) != STATUS_BUFFER_TOO_SMALL) ||
        (Length <= FIELD_OFFSET(LX_SMBIOS_RAW, Data)) || ((Raw = LxAllocPaged(Length)) == NULL) ||
        !NT_SUCCESS(ExGetSystemFirmwareTable(LX_FIRMWARE_RSMB, 0, Raw, Length, &Length)))
    {
        LxFreePaged(Raw);
        LxTextAppend(Text, "\n", 1);
        return;
    }
    Limit = Raw->Data + min(Raw->Length, Length - FIELD_OFFSET(LX_SMBIOS_RAW, Data));
    for (Cursor = Raw->Data; (Cursor + 4 <= Limit) && (Cursor[1] >= 4) && (Cursor[0] != 127);)
    {
        PUCHAR Next = Cursor + Cursor[1];

        if (Cursor[0] == Field->Type)
            break;
        while ((Next + 1 < Limit) && ((Next[0] != 0) || (Next[1] != 0)))
            Next++;
        Cursor = Next + 2;
    }
    if ((Cursor + 4 > Limit) || (Cursor[0] != Field->Type) || (Field->Offset >= Cursor[1]))
    {
        LxTextAppend(Text, "\n", 1);
    }
    else if (Field->Kind == LxDmiText)
    {
        PCSTR Value = LxDmiStructureString(Cursor, Limit, Cursor[Field->Offset]);

        LxTextPrintf(Text, "%s\n", (Value != NULL) ? Value : "");
    }
    else if (Field->Kind == LxDmiRelease)
    {
        if ((Field->Offset + 1 < Cursor[1]) && (Cursor[Field->Offset] != 0xFF))
            LxTextPrintf(Text, "%u.%u\n", Cursor[Field->Offset], Cursor[Field->Offset + 1]);
        else
            LxTextAppend(Text, "\n", 1);
    }
    else if (Field->Kind == LxDmiChassisType)
    {
        LxTextPrintf(Text, "%u\n", Cursor[Field->Offset] & 0x7F);
    }
    else if (Field->Offset + 16 <= Cursor[1])
    {
        PUCHAR Uuid = Cursor + Field->Offset;

        if (((Raw->MajorVersion << 8) | Raw->MinorVersion) >= 0x0206)
        {
            LxTextPrintf(Text, "%02x%02x%02x%02x-%02x%02x-%02x%02x-", Uuid[3], Uuid[2], Uuid[1], Uuid[0],
                         Uuid[5], Uuid[4], Uuid[7], Uuid[6]);
        }
        else
        {
            LxTextPrintf(Text, "%02x%02x%02x%02x-%02x%02x-%02x%02x-", Uuid[0], Uuid[1], Uuid[2], Uuid[3],
                         Uuid[4], Uuid[5], Uuid[6], Uuid[7]);
        }
        LxTextPrintf(Text, "%02x%02x-%02x%02x%02x%02x%02x%02x\n", Uuid[8], Uuid[9], Uuid[10], Uuid[11],
                     Uuid[12], Uuid[13], Uuid[14], Uuid[15]);
    }
    else
    {
        LxTextAppend(Text, "\n", 1);
    }
    LxFreePaged(Raw);
}

#define LX_DMI_ENTRIES(Root) \
    { Root "/bios_date", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/bios_release", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/bios_vendor", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/bios_version", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/board_asset_tag", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/board_name", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/board_serial", 0400, NULL, LxGenDmi, NULL }, \
    { Root "/board_vendor", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/board_version", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/chassis_asset_tag", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/chassis_serial", 0400, NULL, LxGenDmi, NULL }, \
    { Root "/chassis_type", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/chassis_vendor", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/chassis_version", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/ec_firmware_release", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/product_family", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/product_name", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/product_serial", 0400, NULL, LxGenDmi, NULL }, \
    { Root "/product_sku", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/product_uuid", 0400, NULL, LxGenDmi, NULL }, \
    { Root "/product_version", 0444, NULL, LxGenDmi, NULL }, \
    { Root "/sys_vendor", 0444, NULL, LxGenDmi, NULL }

static
VOID
LxGenCpuVulnerability(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text);

static const LX_TABLE_ENTRY LxSysTable[] =
{
    { "block", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "bus", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/block", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    LX_DMI_ENTRIES("class/dmi/id"),
    { "class/input", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/mem", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/misc", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/net", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/tty", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "dev/block", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "dev/char", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "devices/system/cpu/kernel_max", 0444, "8191\n", NULL, NULL },
    { "devices/system/cpu/offline", 0444, "\n", NULL, NULL },
    { "devices/system/cpu/online", 0444, NULL, LxGenCpuOnline, NULL },
    { "devices/system/cpu/possible", 0444, NULL, LxGenCpuOnline, NULL },
    { "devices/system/cpu/present", 0444, NULL, LxGenCpuOnline, NULL },
    { "devices/system/cpu/vulnerabilities/gather_data_sampling", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/ghostwrite", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/indirect_target_selection", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/itlb_multihit", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/l1tf", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/mds", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/meltdown", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/mmio_stale_data", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/old_microcode", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/reg_file_data_sampling", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/retbleed", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/spec_rstack_overflow", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/spec_store_bypass", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/spectre_v1", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/spectre_v2", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/srbds", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/tsa", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/tsx_async_abort", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/system/cpu/vulnerabilities/vmscape", 0444, NULL, LxGenCpuVulnerability, NULL },
    { "devices/virtual", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    LX_DMI_ENTRIES("devices/virtual/dmi/id"),
    { "firmware", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/bpf", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/cgroup", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/fuse/connections", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/pstore", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "hypervisor", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/config", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/debug", 0700 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/mm", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/mm/hugepages/hugepages-2048kB/free_hugepages", 0444, NULL, LxGenHugePages, NULL },
    { "kernel/mm/hugepages/hugepages-2048kB/nr_hugepages", 0644, NULL, LxGenHugePages, LxWriteHugePages },
    { "kernel/mm/hugepages/hugepages-2048kB/nr_overcommit_hugepages", 0644, NULL, LxGenHugePages, LxWriteAccept },
    { "kernel/mm/hugepages/hugepages-2048kB/resv_hugepages", 0444, NULL, LxGenHugePages, NULL },
    { "kernel/mm/hugepages/hugepages-2048kB/surplus_hugepages", 0444, NULL, LxGenHugePages, NULL },
    { "kernel/security", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/tracing", 0700 | LX_S_IFDIR, NULL, NULL, NULL },
    { "module", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "power", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
};

static
VOID
LxGenCpuVulnerability(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PCSTR Name = strrchr(LxSysTable[Synth->Index].Path, '/') + 1;

    UNREFERENCED_PARAMETER(Self);
    LxTextPrintf(Text, "%s\n", LxArchCpuVulnerability(Name));
}

static
VOID
LxGenCpuinfo(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Count = KeQueryActiveProcessorCount(NULL), Index;

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    for (Index = 0; Index < Count; Index++)
        LxTextPrintf(Text, "processor\t: %lu\n%s\n", Index, LxArchCpuinfo());
}

VOID
LxQueryMemoryState(
    _Out_ PLX_MEMORY_STATE State)
{
    SYSTEM_BASIC_INFORMATION Basic;
    SYSTEM_PERFORMANCE_INFORMATION Performance;
    SYSTEM_FILECACHE_INFORMATION Cache;
    PSYSTEM_PAGEFILE_INFORMATION PageFiles, Entry;
    ULONG Size = 2 * PAGE_SIZE, Returned = 0;
    ULONG64 PageSize = PAGE_SIZE;

    RtlZeroMemory(State, sizeof(*State));
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL)))
    {
        PageSize = Basic.PageSize;
        State->Total = (ULONG64)Basic.NumberOfPhysicalPages * PageSize;
    }
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemPerformanceInformation, &Performance, sizeof(Performance), NULL)))
    {
        State->Available = min((ULONG64)Performance.AvailablePages * PageSize, State->Total);
        State->Committed = (ULONG64)Performance.CommittedPages * PageSize;
        State->CommitLimit = (ULONG64)Performance.CommitLimit * PageSize;
    }
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemFileCacheInformation, &Cache, sizeof(Cache), NULL)))
        State->Cached = min((ULONG64)Cache.CurrentSizeIncludingTransitionInPages * PageSize, State->Available);
    State->Free = State->Available - State->Cached;

    PageFiles = LxAllocPaged(Size);
    if ((PageFiles != NULL) &&
        NT_SUCCESS(ZwQuerySystemInformation(SystemPageFileInformation, PageFiles, Size, &Returned)) &&
        (Returned >= sizeof(*PageFiles)))
    {
        for (Entry = PageFiles;; Entry = (PSYSTEM_PAGEFILE_INFORMATION)((PUCHAR)Entry + Entry->NextEntryOffset))
        {
            State->SwapTotal += (ULONG64)Entry->TotalSize * PageSize;
            State->SwapFree += (ULONG64)(Entry->TotalSize - min(Entry->TotalInUse, Entry->TotalSize)) * PageSize;
            if ((Entry->NextEntryOffset == 0) ||
                ((PUCHAR)Entry + Entry->NextEntryOffset + sizeof(*Entry) > (PUCHAR)PageFiles + Returned))
            {
                break;
            }
        }
    }
    LxFreePaged(PageFiles);
}

static
VOID
LxGenMeminfo(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG64 HugeTotal, HugeFree, HugeSurplus, HugeReserved;
    LX_MEMORY_STATE State;

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    LxQueryMemoryState(&State);
    LxTextPrintf(Text,
                 "MemTotal:       %8llu kB\nMemFree:        %8llu kB\nMemAvailable:   %8llu kB\n"
                 "Buffers:               0 kB\nCached:         %8llu kB\nSwapCached:            0 kB\n"
                 "Active:                0 kB\nInactive:              0 kB\nSwapTotal:      %8llu kB\n"
                 "SwapFree:       %8llu kB\nDirty:                 0 kB\nWriteback:             0 kB\n"
                 "AnonPages:             0 kB\nMapped:                0 kB\nShmem:          %8llu kB\n"
                 "Slab:                  0 kB\nSReclaimable:          0 kB\nSUnreclaim:            0 kB\n"
                 "KernelStack:           0 kB\nPageTables:            0 kB\nCommitLimit:    %8llu kB\n"
                 "Committed_AS:   %8llu kB\nVmallocTotal:   34359738367 kB\nVmallocUsed:           0 kB\n",
                 State.Total / 1024, State.Free / 1024, State.Available / 1024, State.Cached / 1024,
                 State.SwapTotal / 1024, State.SwapFree / 1024, LxTmpfsShmemBytes() / 1024,
                 State.CommitLimit / 1024, State.Committed / 1024);
    HugeTotal = LxHugeQueryPool(&HugeFree, &HugeSurplus, &HugeReserved);
    LxTextPrintf(Text,
                 "HugePages_Total:   %5llu\nHugePages_Free:    %5llu\nHugePages_Rsvd:    %5llu\n"
                 "HugePages_Surp:    %5llu\nHugepagesize:   %8llu kB\nHugetlb:        %8llu kB\n",
                 HugeTotal, HugeFree, HugeReserved, HugeSurplus, LxHugePageSize() / 1024,
                 (HugeTotal + HugeSurplus) * (LxHugePageSize() / 1024));
}

static
PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
LxQueryProcessorTimes(
    _Out_ PULONG Count)
{
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Times;
    ULONG Returned = 0;

    *Count = KeQueryActiveProcessorCount(NULL);
    Times = LxAllocPaged(*Count * sizeof(*Times));
    if ((Times != NULL) &&
        !NT_SUCCESS(ZwQuerySystemInformation(SystemProcessorPerformanceInformation, Times,
                                             *Count * sizeof(*Times), &Returned)))
    {
        LxFreePaged(Times);
        Times = NULL;
    }
    if (Times != NULL)
        *Count = Returned / sizeof(*Times);
    return Times;
}

static
VOID
LxPrintCpuTimes(
    _Inout_ PLX_TEXT Text,
    _In_ PCSTR Name,
    _In_ ULONG64 Idle,
    _In_ ULONG64 Kernel,
    _In_ ULONG64 User,
    _In_ ULONG64 Dpc,
    _In_ ULONG64 Interrupt)
{
    ULONG64 System = (Kernel > Idle + Dpc + Interrupt) ? (Kernel - Idle - Dpc - Interrupt) : 0;

    LxTextPrintf(Text, "%s %llu 0 %llu %llu 0 %llu %llu 0 0 0\n", Name, User / 100000ULL, System / 100000ULL,
                 Idle / 100000ULL, Interrupt / 100000ULL, Dpc / 100000ULL);
}

static
VOID
LxGenStat(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Count, Index, Processes = 0;
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Times = LxQueryProcessorTimes(&Count);
    SYSTEM_PERFORMANCE_INFORMATION Performance;
    ULONG64 Idle = 0, Kernel = 0, User = 0, Dpc = 0, Interrupt = 0, Interrupts = 0, Switches = 0;
    LARGE_INTEGER Now;
    PLIST_ENTRY Entry;
    LONG64 Uptime = (LONG64)KeQueryInterruptTime() / 10000000LL;
    CHAR Name[16];

    UNREFERENCED_PARAMETER(Synth);
    KeQuerySystemTime(&Now);
    for (Index = 0; (Times != NULL) && (Index < Count); Index++)
    {
        Idle += (ULONG64)Times[Index].IdleTime.QuadPart;
        Kernel += (ULONG64)Times[Index].KernelTime.QuadPart;
        User += (ULONG64)Times[Index].UserTime.QuadPart;
        Dpc += (ULONG64)Times[Index].DpcTime.QuadPart;
        Interrupt += (ULONG64)Times[Index].InterruptTime.QuadPart;
        Interrupts += Times[Index].InterruptCount;
    }
    LxPrintCpuTimes(Text, "cpu ", Idle, Kernel, User, Dpc, Interrupt);
    for (Index = 0; Index < Count; Index++)
    {
        RtlStringCbPrintfA(Name, sizeof(Name), "cpu%lu", Index);
        if (Times != NULL)
        {
            LxPrintCpuTimes(Text, Name, (ULONG64)Times[Index].IdleTime.QuadPart,
                            (ULONG64)Times[Index].KernelTime.QuadPart, (ULONG64)Times[Index].UserTime.QuadPart,
                            (ULONG64)Times[Index].DpcTime.QuadPart, (ULONG64)Times[Index].InterruptTime.QuadPart);
        }
        else
        {
            LxPrintCpuTimes(Text, Name, 0, 0, 0, 0, 0);
        }
    }
    LxFreePaged(Times);
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemPerformanceInformation, &Performance, sizeof(Performance), NULL)))
        Switches = Performance.ContextSwitches;
    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink)->Instance == Self->Instance)
            Processes++;
    }
    ExReleaseFastMutex(&LxProcessListLock);
    {
        ULONG64 Load[3];
        ULONG Runnable, Threads;

        LxSchedQueryLoad(Load, &Runnable, &Threads);
        LxTextPrintf(Text, "intr %llu\nctxt %llu\nbtime %lld\nprocesses %lu\nprocs_running %lu\nprocs_blocked 0\nsoftirq 0 0 0 0 0 0 0 0 0 0 0\n",
                     Interrupts, Switches, (Now.QuadPart - 116444736000000000LL) / 10000000LL - Uptime, Processes,
                     max(Runnable, 1));
    }
}

static
VOID
LxGenUptime(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LONG64 Now = (LONG64)KeQueryInterruptTime() + LxTimeNsOffset(Self, LX_CLOCK_BOOTTIME) / 100;
    ULONG Count, Index;
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Times = LxQueryProcessorTimes(&Count);
    ULONG64 Idle = 0;

    UNREFERENCED_PARAMETER(Synth);
    for (Index = 0; (Times != NULL) && (Index < Count); Index++)
        Idle += (ULONG64)Times[Index].IdleTime.QuadPart;
    LxFreePaged(Times);
    LxTextPrintf(Text, "%lld.%02lld %llu.%02llu\n", Now / 10000000LL, (Now / 100000LL) % 100,
                 Idle / 10000000ULL, (Idle / 100000ULL) % 100);
}

static
VOID
LxGenKeys(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_THREAD Reader = LxCurrentThread();
    ULONG Length;
    PSTR Lines;

    UNREFERENCED_PARAMETER(Synth);
    if ((Reader == NULL) || (Reader->Process != Self))
        return;
    Lines = LxKeyFormatProcKeys(Reader, &Length);
    if (Lines == NULL)
    {
        Text->Failed = TRUE;
        return;
    }
    LxTextAppend(Text, Lines, Length);
    LxFree(Lines);
}

static
VOID
LxGenKeyUsers(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Length;
    PSTR Lines;

    UNREFERENCED_PARAMETER(Synth);
    Lines = LxKeyFormatProcKeyUsers(Self, &Length);
    if (Lines == NULL)
    {
        Text->Failed = TRUE;
        return;
    }
    LxTextAppend(Text, Lines, Length);
    LxFree(Lines);
}

static
VOID
LxGenPartitionLoop(
    _In_ PVOID Context,
    _In_ ULONG Number,
    _In_ ULONG64 Capacity)
{
    if (Capacity != 0)
        LxTextPrintf(Context, "%4lu  %7lu %10llu loop%lu\n", (ULONG)LX_LOOP_MAJOR, Number, Capacity >> 10, Number);
}

static
VOID
LxGenPartitions(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "major minor  #blocks  name\n\n");
    LxLoopEnumerate(Self->Instance, LxGenPartitionLoop, Text);
}

static
VOID
LxGenLoadavg(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG64 Load[3];
    ULONG Runnable, Threads;

    UNREFERENCED_PARAMETER(Synth);
    LxSchedQueryLoad(Load, &Runnable, &Threads);
    LxTextPrintf(Text, "%llu.%02llu %llu.%02llu %llu.%02llu %lu/%lu %lu\n",
                 Load[0] / 2048, (Load[0] % 2048) * 100 / 2048, Load[1] / 2048, (Load[1] % 2048) * 100 / 2048,
                 Load[2] / 2048, (Load[2] % 2048) * 100 / 2048, max(Runnable, 1),
                 max(Threads, Self->Instance->ProcessCount), Self->Instance->NextPid - 1);
}

static const LX_TABLE_ENTRY LxProcStaticTable[] =
{
    { "cmdline", 0444, "init=/sbin/init\n", NULL, NULL },
    { "config.gz", 0444, NULL, LxGenKernelConfig, NULL },
    { "consoles", 0444, "console              -W- (EC p a)    5:1\n", NULL, NULL },
    { "cpuinfo", 0444, NULL, LxGenCpuinfo, NULL },
    { "devices", 0444, "Character devices:\n  1 mem\n  4 /dev/vc/0\n  4 tty\n  5 /dev/tty\n  5 /dev/console\n  5 /dev/ptmx\n 10 misc\n136 pts\n\nBlock devices:\n  7 loop\n", NULL, NULL },
    { "filesystems", 0444, "nodev\tsysfs\nnodev\ttmpfs\nnodev\tbdev\nnodev\tproc\nnodev\tcgroup2\nnodev\tdevtmpfs\nnodev\tdevpts\nnodev\tmqueue\nnodev\tpipefs\nnodev\tsockfs\n\tlxfs\n\tsquashfs\n", NULL, NULL },
    { "key-users", 0444, NULL, LxGenKeyUsers, NULL },
    { "keys", 0444, NULL, LxGenKeys, NULL },
    { "loadavg", 0444, NULL, LxGenLoadavg, NULL },
    { "meminfo", 0444, NULL, LxGenMeminfo, NULL },
    { "partitions", 0444, NULL, LxGenPartitions, NULL },
    { "stat", 0444, NULL, LxGenStat, NULL },
    { "swaps", 0444, "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n", NULL, NULL },
    { "uptime", 0444, NULL, LxGenUptime, NULL },
    { "version", 0444, NULL, LxGenKernelVersion, NULL },
};

static
LONG
LxTableResolve(
    _In_reads_(Count) const LX_TABLE_ENTRY *Table,
    _In_ ULONG Count,
    _In_ PCSTR Path,
    _Out_ PULONG Index,
    _Out_ PULONG Type)
{
    SIZE_T Length = strlen(Path);
    ULONG Entry;

    for (Entry = 0; Entry < Count; Entry++)
    {
        PCSTR Candidate = Table[Entry].Path;

        if (strcmp(Candidate, Path) == 0)
        {
            *Index = Entry;
            *Type = (Table[Entry].Mode & LX_S_IFDIR) ? LX_S_IFDIR : LX_S_IFREG;
            return 0;
        }
        if ((Length == 0) || ((strncmp(Candidate, Path, Length) == 0) && (Candidate[Length] == '/')))
        {
            *Index = MAXULONG;
            *Type = LX_S_IFDIR;
            return 0;
        }
    }
    return -LX_ENOENT;
}

static
VOID
LxTableList(
    _In_reads_(Count) const LX_TABLE_ENTRY *Table,
    _In_ ULONG Count,
    _In_ PCSTR Path,
    _Inout_ PLX_TEXT List)
{
    SIZE_T Length = strlen(Path);
    CHAR Last[64] = "";
    ULONG Entry;

    for (Entry = 0; Entry < Count; Entry++)
    {
        PCSTR Candidate = Table[Entry].Path, Rest, Slash;
        CHAR Name[64];
        SIZE_T NameLength;
        UCHAR Type;

        if (Length != 0)
        {
            if ((strncmp(Candidate, Path, Length) != 0) || (Candidate[Length] != '/'))
                continue;
            Rest = Candidate + Length + 1;
        }
        else
        {
            Rest = Candidate;
        }
        Slash = strchr(Rest, '/');
        NameLength = (Slash != NULL) ? (SIZE_T)(Slash - Rest) : strlen(Rest);
        if (NameLength >= sizeof(Name))
            continue;
        RtlCopyMemory(Name, Rest, NameLength);
        Name[NameLength] = '\0';
        if (strcmp(Name, Last) == 0)
            continue;
        RtlCopyMemory(Last, Name, NameLength + 1);
        Type = ((Slash != NULL) || (Table[Entry].Mode & LX_S_IFDIR)) ? LX_DT_DIR : LX_DT_REG;
        LxDirAdd(List, Type, Name);
    }
}

static
BOOLEAN
LxProcessVisible(
    _In_ PLX_PROCESS Self,
    _In_ ULONG Pid)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Pid);

    if (Process == NULL)
        return FALSE;
    LxProcessDereference(Process);
    return TRUE;
}

static
BOOLEAN
LxThreadVisible(
    _In_ PLX_PROCESS Self,
    _In_ ULONG Pid,
    _In_ ULONG Tid)
{
    PLX_PROCESS Process;
    PLX_THREAD Thread = LxThreadLookupReferenced(Self->Instance, Tid, &Process);
    BOOLEAN Result = FALSE;

    if (Thread != NULL)
    {
        Result = (Process->Pid == Pid);
        LxProcessDereference(Process);
    }
    else if (Tid == Pid)
    {
        Result = LxProcessVisible(Self, Pid);
    }
    return Result;
}

static
PSTR
LxFileVisiblePath(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File)
{
    PSTR Current = LxFileCurrentPath(File), Path;

    Path = LxPathVisible(Process, (Current != NULL) ? Current : File->Path);
    LxFree(Current);
    return Path;
}

static
PSTR
LxDescribeFile(
    _In_ PLX_FILE File)
{
    if (File->Path != NULL)
        return LxStrDup(File->Path);
    return LxStrPrintf("anon_inode:[%s]", "unknown");
}

static
BOOLEAN
LxParseHex(
    _Inout_ PCSTR *Cursor,
    _Out_ PULONG64 Value)
{
    PCSTR Text = *Cursor;
    ULONG Digits = 0;

    *Value = 0;
    if ((Text[0] == '0') && (Text[1] != '\0') && (Text[1] != '-'))
        return FALSE;
    for (;; Text++, Digits++)
    {
        CHAR Character = *Text;
        ULONG Digit;

        if ((Character >= '0') && (Character <= '9'))
            Digit = Character - '0';
        else if ((Character >= 'a') && (Character <= 'f'))
            Digit = Character - 'a' + 10;
        else if ((Character >= 'A') && (Character <= 'F'))
            Digit = Character - 'A' + 10;
        else
            break;
        if ((Digits >= 16) || (*Value > (MAXULONG64 >> 4)))
            return FALSE;
        *Value = (*Value << 4) | Digit;
    }
    *Cursor = Text;
    return Digits != 0;
}

static
BOOLEAN
LxParseMapRange(
    _In_ PCSTR Name,
    _Out_ PULONG64 Start,
    _Out_ PULONG64 End)
{
    PCSTR Cursor = Name;

    if (!LxParseHex(&Cursor, Start) || (*Cursor != '-'))
        return FALSE;
    Cursor++;
    return LxParseHex(&Cursor, End) && (*Cursor == '\0');
}

static
LONG
LxResolveFdLink(
    _In_ PLX_PROCESS Self,
    _Inout_ PLX_SYNTH Synth)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_FILE File;

    if (Process == NULL)
        return -LX_ENOENT;
    File = LxFdGet(Process, Synth->Fd);
    LxProcessDereference(Process);
    if (File == NULL)
        return -LX_ENOENT;

    if ((File->Path != NULL) && (File->Path[0] == '/') && (File->Mount != NULL))
        Synth->LinkTarget = LxFileVisiblePath(Self, File);
    else
        Synth->LinkTarget = LxDescribeFile(File);
    Synth->Magic = TRUE;
    LxFileDereference(File);
    return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
}

static
LONG
LxResolvePidEntry(
    _In_ PLX_PROCESS Self,
    _Inout_ PLX_SYNTH Synth,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count)
{
    ULONG Index, Number;

    if (Count == 0)
    {
        Synth->Id = LxProcPidDir;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = 0555;
        return 0;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(LxPidEntries); Index++)
    {
        if (strcmp(LxPidEntries[Index].Name, Components[0]) == 0)
            break;
    }
    if (Index == RTL_NUMBER_OF(LxPidEntries))
        return -LX_ENOENT;
    if ((Synth->Tid != 0) && (strcmp(Components[0], "task") == 0))
        return -LX_ENOENT;
    if ((Synth->Tid == 0) && (strcmp(Components[0], "children") == 0))
        return -LX_ENOENT;

    Synth->Index = Index;
    Synth->Type = LxPidEntries[Index].Type;
    Synth->Mode = LxPidEntries[Index].Mode;

    if (strcmp(Components[0], "task") == 0)
    {
        if (Count == 1)
        {
            Synth->Id = LxProcTaskDir;
            return 0;
        }
        if (!LxParseNumber(Components[1], &Number))
            return -LX_ENOENT;
        Number = LxNrToGlobalIn(LxProcPidNs(Self, Synth), Number);
        if ((Number == 0) || !LxThreadVisible(Self, Synth->Pid, Number))
            return -LX_ENOENT;
        Synth->Tid = Number;
        return LxResolvePidEntry(Self, Synth, Components + 2, Count - 2);
    }

    if (strcmp(Components[0], "ns") == 0)
    {
        PLX_PROCESS Process;
        PLX_NS Ns;

        if (Count == 1)
        {
            Synth->Id = LxProcNsDir;
            return 0;
        }
        if (Count != 2)
            return -LX_ENOENT;
        Process = LxProcessLookup(Self->Instance, Synth->Pid);
        if (Process == NULL)
            return -LX_ENOENT;
        ExAcquireFastMutex(&Process->Lock);
        Ns = LxProcTaskNs(Process, Synth->Tid, Components[1], &Synth->NsKind);
        Synth->LinkTarget = (Ns != NULL) ? LxStrPrintf("%s:[%llu]", LxNsName(Ns->Kind), Ns->Inum) : NULL;
        ExReleaseFastMutex(&Process->Lock);
        LxProcessDereference(Process);
        if (Ns == NULL)
            return -LX_ENOENT;
        if (Synth->LinkTarget == NULL)
            return -LX_ENOMEM;
        RtlStringCbCopyA(Synth->Prefix, sizeof(Synth->Prefix), Components[1]);
        Synth->Id = LxProcNsLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0777;
        Synth->Magic = TRUE;
        return 0;
    }

    if ((strcmp(Components[0], "fd") == 0) || (strcmp(Components[0], "fdinfo") == 0))
    {
        BOOLEAN Info = (Components[0][2] == 'i');

        if (Count == 1)
        {
            Synth->Id = Info ? LxProcFdinfoDir : LxProcFdDir;
            return 0;
        }
        if ((Count != 2) || !LxParseNumber(Components[1], &Number) || (Number >= LX_MAX_FDS))
            return -LX_ENOENT;
        Synth->Fd = (LONG)Number;
        if (Info)
        {
            PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
            PLX_FILE File = (Process != NULL) ? LxFdGet(Process, Synth->Fd) : NULL;

            if (Process != NULL)
                LxProcessDereference(Process);
            if (File == NULL)
                return -LX_ENOENT;
            LxFileDereference(File);
            Synth->Id = LxProcFdinfoFile;
            Synth->Type = LX_S_IFREG;
            Synth->Mode = 0444;
            return 0;
        }
        Synth->Id = LxProcFdLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0700;
        return LxResolveFdLink(Self, Synth);
    }

    if (strcmp(Components[0], "map_files") == 0)
    {
        PLX_PROCESS Process;
        PLX_FILE File;
        ULONG Access;

        if (Count == 1)
        {
            Synth->Id = LxProcMapFilesDir;
            return 0;
        }
        if ((Count != 2) || !LxParseMapRange(Components[1], &Synth->MapStart, &Synth->MapEnd))
            return -LX_ENOENT;
        Process = LxProcessLookup(Self->Instance, Synth->Pid);
        File = (Process != NULL) ? LxMmFileAt(Process, Synth->MapStart, Synth->MapEnd) : NULL;
        if (Process != NULL)
            LxProcessDereference(Process);
        if (File == NULL)
            return -LX_ENOENT;
        Access = File->Flags & LX_O_ACCMODE;
        Synth->Id = LxProcMapFileLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = ((Access != LX_O_WRONLY) ? 0400 : 0) | ((Access != LX_O_RDONLY) ? 0200 : 0);
        if ((File->Path != NULL) && (File->Path[0] == '/') && (File->Mount != NULL))
            Synth->LinkTarget = LxFileVisiblePath(Self, File);
        else
            Synth->LinkTarget = LxDescribeFile(File);
        Synth->Magic = TRUE;
        LxFileDereference(File);
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }

    if (Count != 1)
    {
        if (strcmp(Components[0], "attr") == 0)
        {
            if ((Count == 2) && ((strcmp(Components[1], "current") == 0) || (strcmp(Components[1], "exec") == 0)))
            {
                Synth->Id = LxProcPidFile;
                Synth->Type = LX_S_IFREG;
                Synth->Mode = 0666;
                Synth->Index = MAXULONG;
                return 0;
            }
        }
        return -LX_ENOENT;
    }

    if (LxPidEntries[Index].Type == LX_S_IFLNK)
    {
        PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

        if (Process == NULL)
            return -LX_ENOENT;
        Synth->Id = LxProcPidLink;
        {
            PSTR Raw;

            ExAcquireFastMutex(&Process->Lock);
            if (strcmp(Components[0], "cwd") == 0)
                Raw = LxStrDup(Process->Cwd);
            else if (strcmp(Components[0], "exe") == 0)
                Raw = LxStrDup((Process->ExePath != NULL) ? Process->ExePath : "/");
            else
                Raw = LxStrDup((Process->Root != NULL) ? Process->Root : "/");
            ExReleaseFastMutex(&Process->Lock);
            LxProcessDereference(Process);
            if (Raw == NULL)
                return -LX_ENOMEM;
            Synth->LinkTarget = LxPathVisible(Self, Raw);
            LxFree(Raw);
        }
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }

    Synth->Id = (LxPidEntries[Index].Type == LX_S_IFDIR) ? LxProcPidDir : LxProcPidFile;
    if (strcmp(Components[0], "attr") == 0)
        Synth->Id = LxProcPidDir;
    return 0;
}

static
LONG
LxResolveProc(
    _In_ PLX_PROCESS Self,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    PLX_THREAD Current = LxCurrentThread();
    ULONG Number, Index, Type;
    CHAR Path[128];
    SIZE_T Used = 0;

    if (Count == 0)
    {
        Synth->Id = LxProcRoot;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = 0555;
        return 0;
    }

    if ((Count == 1) && (strcmp(Components[0], "self") == 0))
    {
        ULONG Nr = LxPidNrIn(LxProcPidNs(Self, Synth), Self);

        if (Nr == 0)
            return -LX_ENOENT;
        Synth->Id = LxProcLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0777;
        Synth->LinkTarget = LxStrPrintf("%lu", Nr);
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }
    if ((Count == 1) && (strcmp(Components[0], "thread-self") == 0))
    {
        PLX_NS ProcNs = LxProcPidNs(Self, Synth);
        ULONG Nr = LxPidNrIn(ProcNs, Self);

        if (Nr == 0)
            return -LX_ENOENT;
        Synth->Id = LxProcLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0777;
        Synth->LinkTarget = LxStrPrintf("%lu/task/%lu", Nr,
                                        (Current != NULL) ? LxGlobalToNrIn(ProcNs, Current->Tid) : Nr);
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }
    if ((Count == 1) && (strcmp(Components[0], "mounts") == 0))
    {
        Synth->Id = LxProcLink;
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0777;
        Synth->LinkTarget = LxStrDup("self/mounts");
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }

    if (LxParseNumber(Components[0], &Number))
    {
        Number = LxNrToGlobalIn(LxProcPidNs(Self, Synth), Number);
        if ((Number == 0) || !LxProcessVisible(Self, Number))
            return -LX_ENOENT;
        Synth->Pid = Number;
        return LxResolvePidEntry(Self, Synth, Components + 1, Count - 1);
    }

    if (strcmp(Components[0], "sys") == 0)
    {
        for (Index = 1; Index < Count; Index++)
        {
            SIZE_T Length = strlen(Components[Index]);

            if (Used + Length + 2 >= sizeof(Path))
                return -LX_ENOENT;
            if (Used != 0)
                Path[Used++] = '/';
            RtlCopyMemory(Path + Used, Components[Index], Length);
            Used += Length;
        }
        Path[Used] = '\0';
        if (LxTableResolve(LxProcSysTable, RTL_NUMBER_OF(LxProcSysTable), Path, &Index, &Type) != 0)
            return -LX_ENOENT;
        RtlStringCbCopyA(Synth->Prefix, sizeof(Synth->Prefix), Path);
        Synth->Index = Index;
        Synth->Type = Type;
        Synth->Id = (Type == LX_S_IFDIR) ? LxProcSysDir : LxProcSysFile;
        Synth->Mode = (Type == LX_S_IFDIR) ? 0555 : (LxProcSysTable[Index].Mode & 07777);
        return 0;
    }

    if (Count == 1)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(LxProcStaticTable); Index++)
        {
            if (strcmp(LxProcStaticTable[Index].Path, Components[0]) == 0)
            {
                Synth->Id = LxProcStatic;
                Synth->Type = LX_S_IFREG;
                Synth->Mode = LxProcStaticTable[Index].Mode;
                Synth->Index = Index;
                return 0;
            }
        }
    }
    return -LX_ENOENT;
}

static const PCSTR LxSysLoopAttributes[] =
{
    "alignment_offset", "dev", "discard_alignment", "hidden", "removable", "ro", "size", "uevent"
};

static const PCSTR LxSysLoopBoundAttributes[] =
{
    "autoclear", "backing_file", "dio", "offset", "partscan", "sizelimit"
};

static const PCSTR LxSysLoopQueueAttributes[] =
{
    "hw_sector_size", "logical_block_size", "minimum_io_size", "optimal_io_size", "physical_block_size", "rotational"
};

static
BOOLEAN
LxSysLoopNumber(
    _In_ PCSTR Text,
    _In_ PCSTR Prefix,
    _Out_ PULONG Number)
{
    SIZE_T Length = strlen(Prefix);

    return (strncmp(Text, Prefix, Length) == 0) && (Text[Length] != '\0') && LxParseNumber(Text + Length, Number);
}

static
BOOLEAN
LxSysLoopFind(
    _In_reads_(Count) const PCSTR *Names,
    _In_ ULONG Count,
    _In_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (strcmp(Names[Index], Name) == 0)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxResolveSysLoop(
    _In_ PLX_PROCESS Process,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    LX_LOOP_STATE State;
    ULONG Number;
    PCSTR Link = NULL;
    BOOLEAN Exists;

    if ((Count == 2) && (strcmp(Components[0], "block") == 0) && LxSysLoopNumber(Components[1], "loop", &Number))
        Link = "../devices/virtual/block/loop%lu";
    else if ((Count == 3) && (strcmp(Components[0], "class") == 0) && (strcmp(Components[1], "block") == 0) &&
             LxSysLoopNumber(Components[2], "loop", &Number))
        Link = "../../devices/virtual/block/loop%lu";
    else if ((Count == 3) && (strcmp(Components[0], "dev") == 0) && (strcmp(Components[1], "block") == 0) &&
             LxSysLoopNumber(Components[2], "7:", &Number))
        Link = "../../devices/virtual/block/loop%lu";
    else if ((Count >= 3) && (strcmp(Components[0], "devices") == 0) && (strcmp(Components[1], "virtual") == 0) &&
             (strcmp(Components[2], "block") == 0))
        Number = (Count >= 4) && LxSysLoopNumber(Components[3], "loop", &Number) ? Number : MAXULONG;
    else
        return -LX_ENOENT;

    Synth->Id = LxSysLoop;
    Synth->Mode = 0755;
    Synth->Type = LX_S_IFDIR;
    Synth->Index = Number;
    if (Count == 3 && (Link == NULL))
        return 0;
    if (Number == MAXULONG)
        return -LX_ENOENT;
    Exists = LxLoopQuery(Process->Instance, Number, &State);
    LxFree(State.BackingPath);
    if (!Exists)
        return -LX_ENOENT;
    if (Link != NULL)
    {
        Synth->Type = LX_S_IFLNK;
        Synth->Mode = 0777;
        Synth->LinkTarget = LxStrPrintf(Link, Number);
        return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
    }
    if (Count == 4)
        return 0;
    if (Count == 5)
    {
        if (strcmp(Components[4], "subsystem") == 0)
        {
            Synth->Type = LX_S_IFLNK;
            Synth->Mode = 0777;
            Synth->LinkTarget = LxStrDup("../../../../class/block");
            return (Synth->LinkTarget != NULL) ? 0 : -LX_ENOMEM;
        }
        if ((strcmp(Components[4], "queue") == 0) || ((strcmp(Components[4], "loop") == 0) && State.Bound))
            return 0;
        if (!LxSysLoopFind(LxSysLoopAttributes, RTL_NUMBER_OF(LxSysLoopAttributes), Components[4]))
            return -LX_ENOENT;
    }
    else if ((Count == 6) && (strcmp(Components[4], "queue") == 0))
    {
        if (!LxSysLoopFind(LxSysLoopQueueAttributes, RTL_NUMBER_OF(LxSysLoopQueueAttributes), Components[5]))
            return -LX_ENOENT;
    }
    else if ((Count == 6) && (strcmp(Components[4], "loop") == 0) && State.Bound)
    {
        if (!LxSysLoopFind(LxSysLoopBoundAttributes, RTL_NUMBER_OF(LxSysLoopBoundAttributes), Components[5]))
            return -LX_ENOENT;
    }
    else
    {
        return -LX_ENOENT;
    }
    Synth->Type = LX_S_IFREG;
    Synth->Mode = (strcmp(Components[Count - 1], "uevent") == 0) ? 0644 : 0444;
    return 0;
}

static
VOID
LxSysLoopListLink(
    _In_ PVOID Context,
    _In_ ULONG Number,
    _In_ ULONG64 Capacity)
{
    CHAR Name[24];

    UNREFERENCED_PARAMETER(Capacity);
    RtlStringCbPrintfA(Name, sizeof(Name), "loop%lu", Number);
    LxDirAdd(Context, LX_DT_LNK, Name);
}

static
VOID
LxSysLoopListDevice(
    _In_ PVOID Context,
    _In_ ULONG Number,
    _In_ ULONG64 Capacity)
{
    CHAR Name[24];

    UNREFERENCED_PARAMETER(Capacity);
    RtlStringCbPrintfA(Name, sizeof(Name), "7:%lu", Number);
    LxDirAdd(Context, LX_DT_LNK, Name);
}

static
VOID
LxSysLoopListDirectory(
    _In_ PVOID Context,
    _In_ ULONG Number,
    _In_ ULONG64 Capacity)
{
    CHAR Name[24];

    UNREFERENCED_PARAMETER(Capacity);
    RtlStringCbPrintfA(Name, sizeof(Name), "loop%lu", Number);
    LxDirAdd(Context, LX_DT_DIR, Name);
}

static
VOID
LxSysLoopListTable(
    _In_ PLX_PROCESS Process,
    _In_ PCSTR Prefix,
    _Inout_ PLX_TEXT Text)
{
    if ((strcmp(Prefix, "block") == 0) || (strcmp(Prefix, "class/block") == 0))
        LxLoopList(Process->Instance, LxSysLoopListLink, Text);
    else if (strcmp(Prefix, "dev/block") == 0)
        LxLoopList(Process->Instance, LxSysLoopListDevice, Text);
    else if (strcmp(Prefix, "devices/virtual") == 0)
        LxDirAdd(Text, LX_DT_DIR, "block");
}

static
VOID
LxSysLoopListEntries(
    _In_ PLX_PROCESS Process,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LX_LOOP_STATE State;
    PCSTR Last = strrchr(Synth->Prefix, '/');
    ULONG Index;

    LxDirAdd(Text, LX_DT_DIR, ".");
    LxDirAdd(Text, LX_DT_DIR, "..");
    if (Synth->Index == MAXULONG)
    {
        LxLoopList(Process->Instance, LxSysLoopListDirectory, Text);
        return;
    }
    if (!LxLoopQuery(Process->Instance, Synth->Index, &State))
        return;
    LxFree(State.BackingPath);
    Last = (Last != NULL) ? Last + 1 : Synth->Prefix;
    if (strcmp(Last, "queue") == 0)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(LxSysLoopQueueAttributes); Index++)
            LxDirAdd(Text, LX_DT_REG, LxSysLoopQueueAttributes[Index]);
    }
    else if (strcmp(Last, "loop") == 0)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(LxSysLoopBoundAttributes); Index++)
            LxDirAdd(Text, LX_DT_REG, LxSysLoopBoundAttributes[Index]);
    }
    else
    {
        for (Index = 0; Index < RTL_NUMBER_OF(LxSysLoopAttributes); Index++)
            LxDirAdd(Text, LX_DT_REG, LxSysLoopAttributes[Index]);
        LxDirAdd(Text, LX_DT_LNK, "subsystem");
        LxDirAdd(Text, LX_DT_DIR, "queue");
        if (State.Bound)
            LxDirAdd(Text, LX_DT_DIR, "loop");
    }
}

static
VOID
LxSysLoopRead(
    _In_ PLX_PROCESS Process,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LX_LOOP_STATE State;
    PCSTR Name = strrchr(Synth->Prefix, '/');
    ULONG Number = Synth->Index;

    if ((Name == NULL) || !LxLoopQuery(Process->Instance, Number, &State))
        return;
    Name++;
    if (strcmp(Name, "dev") == 0)
        LxTextPrintf(Text, "%lu:%lu\n", (ULONG)LX_LOOP_MAJOR, Number);
    else if (strcmp(Name, "size") == 0)
        LxTextPrintf(Text, "%llu\n", State.Capacity >> 9);
    else if (strcmp(Name, "ro") == 0)
        LxTextPrintf(Text, "%u\n", (State.Flags & 1) ? 1 : 0);
    else if (strcmp(Name, "uevent") == 0)
        LxTextPrintf(Text, "MAJOR=%lu\nMINOR=%lu\nDEVNAME=loop%lu\nDEVTYPE=disk\n", (ULONG)LX_LOOP_MAJOR, Number,
                     Number);
    else if (strcmp(Name, "backing_file") == 0)
        LxTextPrintf(Text, "%s\n", (State.BackingPath != NULL) ? State.BackingPath : "");
    else if (strcmp(Name, "offset") == 0)
        LxTextPrintf(Text, "%llu\n", State.Offset);
    else if (strcmp(Name, "sizelimit") == 0)
        LxTextPrintf(Text, "%llu\n", State.SizeLimit);
    else if (strcmp(Name, "autoclear") == 0)
        LxTextPrintf(Text, "%u\n", (State.Flags & 4) ? 1 : 0);
    else if (strcmp(Name, "partscan") == 0)
        LxTextPrintf(Text, "%u\n", (State.Flags & 8) ? 1 : 0);
    else if (strcmp(Name, "dio") == 0)
        LxTextPrintf(Text, "%u\n", (State.Flags & 16) ? 1 : 0);
    else if ((strcmp(Name, "logical_block_size") == 0) || (strcmp(Name, "physical_block_size") == 0) ||
             (strcmp(Name, "minimum_io_size") == 0) || (strcmp(Name, "hw_sector_size") == 0))
        LxTextPrintf(Text, "%lu\n", State.BlockSize);
    else
        LxTextPrintf(Text, "0\n");
    LxFree(State.BackingPath);
}

static
LONG
LxResolveSys(
    _In_ PLX_PROCESS Process,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    CHAR Path[128];
    SIZE_T Used = 0;
    ULONG Index, Type, CpuCount = KeQueryActiveProcessorCount(NULL), Cpu;

    for (Index = 0; Index < Count; Index++)
    {
        SIZE_T Length = strlen(Components[Index]);

        if (Used + Length + 2 >= sizeof(Path))
            return -LX_ENOENT;
        if (Used != 0)
            Path[Used++] = '/';
        RtlCopyMemory(Path + Used, Components[Index], Length);
        Used += Length;
    }
    Path[Used] = '\0';
    RtlStringCbCopyA(Synth->Prefix, sizeof(Synth->Prefix), Path);

    if (LxResolveSysLoop(Process, Components, Count, Synth) == 0)
        return 0;

    if ((Count >= 4) && (strcmp(Components[0], "devices") == 0) && (strcmp(Components[1], "system") == 0) &&
        (strcmp(Components[2], "cpu") == 0) && (strncmp(Components[3], "cpu", 3) == 0) &&
        LxParseNumber(Components[3] + 3, &Cpu) && (Cpu < CpuCount))
    {
        if (Count == 4)
        {
            Synth->Id = LxSysDir;
            Synth->Type = LX_S_IFDIR;
            Synth->Mode = 0755;
            Synth->Index = MAXULONG - 1;
            return 0;
        }
        if ((Count == 5) && (strcmp(Components[4], "online") == 0))
        {
            Synth->Id = LxSysFile;
            Synth->Type = LX_S_IFREG;
            Synth->Mode = 0644;
            Synth->Index = MAXULONG - 1;
            return 0;
        }
        return -LX_ENOENT;
    }

    if (LxTableResolve(LxSysTable, RTL_NUMBER_OF(LxSysTable), Path, &Index, &Type) != 0)
    {
        if (LxSysDevQuery(Path, &Type, &Synth->Mode, &Synth->LinkTarget) != 0)
            return -LX_ENOENT;
        Synth->Id = LxSysDevNode;
        Synth->Type = Type;
        return 0;
    }
    Synth->Index = Index;
    Synth->Type = Type;
    Synth->Id = (Type == LX_S_IFDIR) ? LxSysDir : LxSysFile;
    Synth->Mode = (Type == LX_S_IFDIR) ? 0755 : (LxSysTable[Index].Mode & 07777);
    return 0;
}

static const PCSTR LxCgroupControllers[] =
{
    "cpuset",
    "pids",
};

static const PCSTR LxCgroupFiles[] =
{
    "cgroup.controllers",
    "cgroup.events",
    "cgroup.freeze",
    "cgroup.kill",
    "cgroup.max.depth",
    "cgroup.max.descendants",
    "cgroup.pressure",
    "cgroup.procs",
    "cgroup.stat",
    "cgroup.subtree_control",
    "cgroup.threads",
    "cgroup.type",
    "cpu.stat",
    "cpu.stat.local",
    "cpuset.cpus",
    "cpuset.cpus.effective",
    "cpuset.cpus.exclusive",
    "cpuset.cpus.exclusive.effective",
    "cpuset.cpus.isolated",
    "cpuset.cpus.partition",
    "cpuset.mems",
    "cpuset.mems.effective",
    "pids.current",
    "pids.events",
    "pids.events.local",
    "pids.max",
    "pids.peak",
};

static
BOOLEAN
LxCgroupFileAllowed(
    _In_ PLX_CGROUP Cgroup,
    _In_ ULONG Index)
{
    PCSTR Name = LxCgroupFiles[Index];

    if (strncmp(Name, "pids.", 5) == 0)
        return (Cgroup->Parent != NULL) && (Cgroup->Parent->SubtreeMask & LX_CGROUP_PIDS);
    if (strncmp(Name, "cpuset.", 7) == 0)
    {
        if (Cgroup->Parent == NULL)
            return (strcmp(Name, "cpuset.cpus.effective") == 0) || (strcmp(Name, "cpuset.mems.effective") == 0) ||
                   (strcmp(Name, "cpuset.cpus.isolated") == 0);
        return (strcmp(Name, "cpuset.cpus.isolated") != 0) && (Cgroup->Parent->SubtreeMask & LX_CGROUP_CPUSET);
    }
    if (Cgroup->Parent != NULL)
        return TRUE;
    return (strcmp(Name, "cgroup.events") != 0) && (strcmp(Name, "cgroup.freeze") != 0) &&
           (strcmp(Name, "cgroup.kill") != 0) && (strcmp(Name, "cgroup.type") != 0) &&
           (strcmp(Name, "cpu.stat.local") != 0);
}

static
VOID
LxCgroupFileAttributes(
    _In_ PLX_CGROUP Cgroup,
    _Inout_ PLX_SYNTH Synth)
{
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&LxCgroupLock);
    for (Entry = Cgroup->FileAttributes.Flink; Entry != &Cgroup->FileAttributes; Entry = Entry->Flink)
    {
        PLX_CGROUP_FILE_ATTR Attr = CONTAINING_RECORD(Entry, LX_CGROUP_FILE_ATTR, Link);

        if (Attr->Index == Synth->Index)
        {
            Synth->Uid = Attr->Uid;
            Synth->Gid = Attr->Gid;
            Synth->Mode = Attr->Mode;
            break;
        }
    }
    ExReleaseFastMutex(&LxCgroupLock);
}

static
LONG
LxResolveCgroup(
    _In_ PLX_PROCESS Self,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    PLX_CGROUP Cgroup;
    ULONG Consumed, Index;

    Cgroup = LxCgroupWalk(((Synth->Mount != NULL) && (Synth->Mount->CgroupRoot != NULL)) ? Synth->Mount->CgroupRoot :
                          Self->Instance->RootCgroup, (PCSTR *)Components, Count, &Consumed);
    Synth->Cgroup = Cgroup;
    Synth->Uid = Cgroup->Uid;
    Synth->Gid = Cgroup->Gid;
    if (Consumed == Count)
    {
        Synth->Id = LxCgDir;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = Cgroup->Mode;
        return 0;
    }
    if (Consumed + 1 != Count)
        return -LX_ENOENT;

    for (Index = 0; Index < RTL_NUMBER_OF(LxCgroupFiles); Index++)
    {
        if ((strcmp(LxCgroupFiles[Index], Components[Consumed]) == 0) && LxCgroupFileAllowed(Cgroup, Index))
        {
            Synth->Id = LxCgFile;
            Synth->Type = LX_S_IFREG;
            Synth->Mode = ((strcmp(LxCgroupFiles[Index], "cgroup.events") == 0) ||
                           (strcmp(LxCgroupFiles[Index], "cgroup.controllers") == 0) ||
                           (strcmp(LxCgroupFiles[Index], "cgroup.stat") == 0) ||
                           (strncmp(LxCgroupFiles[Index], "cpu.stat", 8) == 0) ||
                           ((strncmp(LxCgroupFiles[Index], "pids.", 5) == 0) &&
                            (strcmp(LxCgroupFiles[Index], "pids.max") != 0)) ||
                           (strstr(LxCgroupFiles[Index], ".effective") != NULL) ||
                           (strcmp(LxCgroupFiles[Index], "cpuset.cpus.isolated") == 0)) ? 0444 : 0644;
            Synth->Index = Index;
            Synth->Uid = Cgroup->FileUid;
            Synth->Gid = Cgroup->FileGid;
            LxCgroupFileAttributes(Cgroup, Synth);
            return 0;
        }
    }
    return -LX_ENOENT;
}

static
LONG
LxResolveMqueue(
    _In_ PLX_MOUNT Mount,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    LX_STAT Stat;

    if (Count == 0)
    {
        Synth->Id = LxMqRoot;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = 01777;
        return 0;
    }
    RtlZeroMemory(&Stat, sizeof(Stat));
    if ((Count != 1) || (Mount->NsFile == NULL) || !LxMqQuery(Mount->NsFile, Components[0], &Stat))
        return -LX_ENOENT;
    Synth->Id = LxMqFile;
    Synth->Type = LX_S_IFREG;
    Synth->Mode = Stat.Mode & 07777;
    RtlStringCbCopyA(Synth->Prefix, sizeof(Synth->Prefix), Components[0]);
    return 0;
}

static
VOID
LxMqDirAdd(
    _In_ PVOID Context,
    _In_ PCSTR Name)
{
    LxDirAdd((PLX_TEXT)Context, LX_DT_REG, Name);
}

static
PLX_DEVPTS
LxPtsDevpts(
    _In_ PLX_PROCESS Process,
    _In_opt_ PLX_MOUNT Mount)
{
    return ((Mount != NULL) && (Mount->Devpts != NULL)) ? Mount->Devpts : Process->Instance->Devpts;
}

static
LONG
LxResolvePts(
    _In_ PLX_PROCESS Self,
    _In_reads_(Count) PCSTR *Components,
    _In_ ULONG Count,
    _Out_ PLX_SYNTH Synth)
{
    ULONG Number;

    if (Count == 0)
    {
        Synth->Id = LxPtsRoot;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = 0755;
        return 0;
    }
    if (Count != 1)
        return -LX_ENOENT;
    if (strcmp(Components[0], "ptmx") == 0)
    {
        Synth->Id = LxPtsPtmx;
        Synth->Type = LX_S_IFCHR;
        Synth->Mode = LxPtsDevpts(Self, Synth->Mount)->PtmxMode;
        return 0;
    }
    if (LxParseNumber(Components[0], &Number))
    {
        PLX_TTY Tty = LxTtyLookup(Self->Instance, LxPtsDevpts(Self, Synth->Mount), Number);

        if (Tty == NULL)
            return -LX_ENOENT;
        LxTtyDereference(Tty);
        Synth->Id = LxPtsTty;
        Synth->Type = LX_S_IFCHR;
        Synth->Mode = 0620;
        Synth->Index = Number;
        return 0;
    }
    return -LX_ENOENT;
}

static
LONG
LxSynthResolve(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PLX_SYNTH Synth)
{
    PCSTR Components[LX_PROC_MAX_COMPONENTS];
    PSTR Buffer;
    ULONG Count;
    LONG Error;

    RtlZeroMemory(Synth, sizeof(*Synth));
    Synth->Fd = -1;
    Synth->Mount = Mount;
    if (Mount->Kind == LxFsNsfs)
    {
        if (Relative[0] != '\0')
            return -LX_ENOTDIR;
        Synth->Id = LxNsfsFile;
        Synth->Type = LX_S_IFREG;
        Synth->Mode = 0444;
        return 0;
    }
    if ((Mount->Root != NULL) && (strcmp(Mount->Root, "/") != 0))
        Buffer = LxStrPrintf("%s%s%s", Mount->Root + 1, (Relative[0] != '\0') ? "/" : "", Relative);
    else
        Buffer = LxStrDup(Relative);
    if (Buffer == NULL)
        return -LX_ENOMEM;
    Count = LxSplitPath(Buffer, Components);
    if (Count == MAXULONG)
    {
        LxFree(Buffer);
        return -LX_ENOENT;
    }

    switch (Mount->Kind)
    {
        case LxFsProc:
            Error = LxResolveProc(Process, Components, Count, Synth);
            break;
        case LxFsSys:
            Error = LxResolveSys(Process, Components, Count, Synth);
            break;
        case LxFsCgroup:
            Error = LxResolveCgroup(Process, Components, Count, Synth);
            break;
        case LxFsDevpts:
            Error = LxResolvePts(Process, Components, Count, Synth);
            break;
        case LxFsMqueue:
            Error = LxResolveMqueue(Mount, Components, Count, Synth);
            break;
        default:
            Error = -LX_ENOENT;
            break;
    }
    LxFree(Buffer);
    if (Error != 0)
    {
        LxFree(Synth->LinkTarget);
        Synth->LinkTarget = NULL;
    }
    return Error;
}

PSTR
LxSynthFollowPath(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative)
{
    LX_SYNTH Synth;
    PLX_PROCESS Target;
    PLX_FILE File;
    PSTR Path = NULL;

    if (LxSynthResolve(Process, Mount, Relative, &Synth) != 0)
        return NULL;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxProcFdLink)
        return NULL;
    Target = LxProcessLookup(Process->Instance, Synth.Pid);
    File = (Target != NULL) ? LxFdGet(Target, Synth.Fd) : NULL;
    if (Target != NULL)
        LxProcessDereference(Target);
    if (File == NULL)
        return NULL;
    if ((File->Type == LX_S_IFDIR) && (File->Path != NULL) && (File->Path[0] == '/') && (File->Mount != NULL))
        Path = LxFileVisiblePath(Process, File);
    LxFileDereference(File);
    return Path;
}

PSTR
LxSynthFdPath(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node)
{
    LX_SYNTH Synth;
    PLX_PROCESS Target;
    PLX_FILE File;
    PSTR Path = NULL;

    if ((Node->Mount == NULL) || !LxMountIsSynthetic(Node->Mount) || (Node->Type != LX_S_IFLNK) || (Node->Relative == NULL))
        return NULL;
    if (LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth) != 0)
        return NULL;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxProcFdLink)
        return NULL;
    Target = LxProcessLookup(Process->Instance, Synth.Pid);
    File = (Target != NULL) ? LxFdGet(Target, Synth.Fd) : NULL;
    if (Target != NULL)
        LxProcessDereference(Target);
    if (File == NULL)
        return NULL;
    if ((File->Path != NULL) && (File->Path[0] == '/') && (File->Mount != NULL))
        Path = LxFileVisiblePath(Process, File);
    LxFileDereference(File);
    return Path;
}

LONG
LxSynthQuery(
    _In_ PLX_PROCESS Process,
    _In_ PLX_MOUNT Mount,
    _In_ PCSTR Relative,
    _Out_ PULONG Type,
    _Out_opt_ PSTR *LinkTarget)
{
    LX_SYNTH Synth;
    LONG Error = LxSynthResolve(Process, Mount, Relative, &Synth);

    *Type = 0;
    if (Error != 0)
        return Error;
    *Type = Synth.Type;
    if ((LinkTarget != NULL) && !Synth.Magic)
    {
        *LinkTarget = Synth.LinkTarget;
        Synth.LinkTarget = NULL;
    }
    LxFree(Synth.LinkTarget);
    return 0;
}

LONG
LxSynthReadlink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _Out_ PSTR *Target)
{
    LX_SYNTH Synth;
    LONG Error = LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth);

    *Target = NULL;
    if (Error != 0)
        return Error;
    if (Synth.Type != LX_S_IFLNK)
    {
        LxFree(Synth.LinkTarget);
        return -LX_EINVAL;
    }
    *Target = Synth.LinkTarget;
    return 0;
}

static
LONG64
LxSynthDirGetdents(
    _In_ PLX_FILE File,
    _In_ ULONG64 UserBuffer,
    _In_ ULONG Length)
{
    PUCHAR Out;
    ULONG OutSize = min(Length, 65536), OutUsed = 0;
    LONG64 Result = 0;
    ULONG Position, Index;

    Out = LxAllocPaged(OutSize);
    if (Out == NULL)
        return -LX_ENOMEM;

    KeWaitForSingleObject(&File->Lock, Executive, KernelMode, FALSE, NULL);
    Position = 0;
    Index = 0;
    while (Position < File->DataLength)
    {
        UCHAR Type = File->Data[Position];
        PCSTR Name = (PCSTR)File->Data + Position + 1;
        ULONG NameLength = (ULONG)strlen(Name);
        ULONG Record = (ULONG)LX_ALIGN_UP(FIELD_OFFSET(LX_DIRENT64, Name) + NameLength + 1, 8);

        if ((LONG64)Index >= File->DirOffset)
        {
            PLX_DIRENT64 Dirent;

            if (OutUsed + Record > OutSize)
            {
                if (OutUsed == 0)
                    Result = -LX_EINVAL;
                break;
            }
            Dirent = (PLX_DIRENT64)(Out + OutUsed);
            RtlZeroMemory(Dirent, Record);
            Dirent->Ino = LxHashPath(Index, Name) ^ File->Ino;
            Dirent->Off = Index + 1;
            Dirent->RecLen = (USHORT)Record;
            Dirent->Type = Type;
            RtlCopyMemory(Dirent->Name, Name, NameLength + 1);
            OutUsed += Record;
            File->DirOffset = Index + 1;
        }
        Position += 1 + NameLength + 1;
        Index++;
    }
    KeSetEvent(&File->Lock, IO_NO_INCREMENT, FALSE);

    if ((Result == 0) && (OutUsed != 0))
    {
        Result = LxCopyToUser(UserBuffer, Out, OutUsed);
        if (Result == 0)
            Result = OutUsed;
    }
    LxFreePaged(Out);
    return Result;
}

static
LONG64
LxSynthDirSeek(
    _In_ PLX_FILE File,
    _In_ LONG64 Offset,
    _In_ ULONG Whence)
{
    if (Whence == LX_SEEK_CUR)
        Offset += File->DirOffset;
    else if (Whence != LX_SEEK_SET)
        return -LX_EINVAL;
    if (Offset < 0)
        return -LX_EINVAL;
    File->DirOffset = Offset;
    return Offset;
}

static
LONG64
LxSynthDirRead(
    _In_ PLX_FILE File,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _Inout_opt_ PLONG64 Offset)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Offset);
    return -LX_EISDIR;
}

static
LONG
LxSynthDirStat(
    _In_ PLX_FILE File,
    _Out_ PLX_STAT Stat)
{
    PLX_THREAD Thread = LxCurrentThread();

    if ((Thread != NULL) && (File->Mount != NULL) && (File->SynthRelative != NULL))
    {
        LX_NODE Node;

        RtlZeroMemory(&Node, sizeof(Node));
        Node.Path = File->Path;
        Node.Mount = File->Mount;
        Node.Relative = File->SynthRelative;
        if (LxSynthStat(Thread->Process, &Node, Stat) == 0)
            return 0;
    }

    RtlZeroMemory(Stat, sizeof(*Stat));
    Stat->Mode = LX_S_IFDIR | File->Mode;
    Stat->Nlink = 2;
    Stat->Ino = File->Ino;
    Stat->BlkSize = 4096;
    Stat->Dev = (File->Mount != NULL) ? File->Mount->Dev : 0;
    return 0;
}

static const LX_FILE_OPS LxSynthDirOps =
{
    LxSynthDirRead, NULL, NULL, NULL, LxSynthDirStat, LxSynthDirSeek, LxSynthDirGetdents, NULL, NULL
};

static
VOID
LxListProc(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT List)
{
    PLIST_ENTRY Entry;
    ULONG Index;

    switch (Synth->Id)
    {
        case LxProcRoot:
        {
            ULONG Pids[512], Count = 0;
            PLX_NS ProcNs = LxProcPidNs(Self, Synth);

            ExAcquireFastMutex(&LxProcessListLock);
            for (Entry = LxProcessList.Flink; (Entry != &LxProcessList) && (Count < RTL_NUMBER_OF(Pids)); Entry = Entry->Flink)
            {
                PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);
                ULONG Nr;

                if (Process->Instance != Self->Instance)
                    continue;
                Nr = LxPidNrIn(ProcNs, Process);
                if (Nr != 0)
                    Pids[Count++] = Nr;
            }
            ExReleaseFastMutex(&LxProcessListLock);

            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            for (Index = 0; Index < Count; Index++)
            {
                CHAR Name[16];

                RtlStringCbPrintfA(Name, sizeof(Name), "%lu", Pids[Index]);
                LxDirAdd(List, LX_DT_DIR, Name);
            }
            LxDirAdd(List, LX_DT_LNK, "self");
            LxDirAdd(List, LX_DT_LNK, "thread-self");
            LxDirAdd(List, LX_DT_LNK, "mounts");
            LxDirAdd(List, LX_DT_DIR, "sys");
            for (Index = 0; Index < RTL_NUMBER_OF(LxProcStaticTable); Index++)
                LxDirAdd(List, LX_DT_REG, LxProcStaticTable[Index].Path);
            break;
        }

        case LxProcSysDir:
            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            LxTableList(LxProcSysTable, RTL_NUMBER_OF(LxProcSysTable), Synth->Prefix, List);
            break;

        case LxProcPidDir:
            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            if (Synth->Index < RTL_NUMBER_OF(LxPidEntries) && (strcmp(LxPidEntries[Synth->Index].Name, "attr") == 0) &&
                (Synth->Type == LX_S_IFDIR) && (Synth->Mode == 0555) && (Synth->Fd == -1) &&
                (Synth->LinkTarget == NULL) && (Synth->Prefix[0] == 'a'))
            {
                LxDirAdd(List, LX_DT_REG, "current");
                LxDirAdd(List, LX_DT_REG, "exec");
                break;
            }
            for (Index = 0; Index < RTL_NUMBER_OF(LxPidEntries); Index++)
            {
                UCHAR Type = (LxPidEntries[Index].Type == LX_S_IFDIR) ? LX_DT_DIR :
                             (LxPidEntries[Index].Type == LX_S_IFLNK) ? LX_DT_LNK : LX_DT_REG;

                if ((Synth->Tid != 0) && (strcmp(LxPidEntries[Index].Name, "task") == 0))
                    continue;
                if ((Synth->Tid == 0) && (strcmp(LxPidEntries[Index].Name, "children") == 0))
                    continue;
                LxDirAdd(List, Type, LxPidEntries[Index].Name);
            }
            break;

        case LxProcTaskDir:
        {
            PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
            ULONG Level = LxProcPidNs(Self, Synth)->Level;

            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            if (Process == NULL)
                break;
            ExAcquireFastMutex(&Process->Lock);
            for (Entry = Process->Threads.Flink; Entry != &Process->Threads; Entry = Entry->Flink)
            {
                CHAR Name[16];

                RtlStringCbPrintfA(Name, sizeof(Name), "%lu", CONTAINING_RECORD(Entry, LX_THREAD, Link)->NsTid[Level]);
                LxDirAdd(List, LX_DT_DIR, Name);
            }
            if (IsListEmpty(&Process->Threads))
            {
                CHAR Name[16];

                RtlStringCbPrintfA(Name, sizeof(Name), "%lu", Process->NsPid[Level]);
                LxDirAdd(List, LX_DT_DIR, Name);
            }
            ExReleaseFastMutex(&Process->Lock);
            LxProcessDereference(Process);
            break;
        }

        case LxProcNsDir:
            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            for (Index = 0; Index < RTL_NUMBER_OF(LxProcNsEntries); Index++)
                LxDirAdd(List, LX_DT_LNK, LxProcNsEntries[Index]);
            break;

        case LxProcMapFilesDir:
        {
            PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
            PULONG64 Ranges = LxAllocPaged(2 * 4096 * sizeof(ULONG64));
            ULONG Count = 0;

            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            if ((Process != NULL) && (Ranges != NULL))
                Count = LxMmFileRanges(Process, Ranges, 4096);
            for (Index = 0; Index < Count; Index++)
            {
                CHAR Name[40];

                RtlStringCbPrintfA(Name, sizeof(Name), "%llx-%llx", Ranges[Index * 2], Ranges[Index * 2 + 1]);
                LxDirAdd(List, LX_DT_LNK, Name);
            }
            if (Ranges != NULL)
                LxFreePaged(Ranges);
            if (Process != NULL)
                LxProcessDereference(Process);
            break;
        }

        case LxProcFdDir:
        case LxProcFdinfoDir:
        {
            PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);

            LxDirAdd(List, LX_DT_DIR, ".");
            LxDirAdd(List, LX_DT_DIR, "..");
            if (Process == NULL)
                break;
            ExAcquireFastMutex(&Process->Lock);
            for (Index = 0; Index < LX_MAX_FDS; Index++)
            {
                CHAR Name[16];

                if (Process->Fds[Index] == NULL)
                    continue;
                RtlStringCbPrintfA(Name, sizeof(Name), "%lu", Index);
                LxDirAdd(List, (Synth->Id == LxProcFdDir) ? LX_DT_LNK : LX_DT_REG, Name);
            }
            ExReleaseFastMutex(&Process->Lock);
            LxProcessDereference(Process);
            break;
        }

        default:
            break;
    }
}

static
VOID
LxListCgroup(
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT List)
{
    PLX_CGROUP Cgroup = Synth->Cgroup;
    PLIST_ENTRY Entry;
    ULONG Index;

    LxDirAdd(List, LX_DT_DIR, ".");
    LxDirAdd(List, LX_DT_DIR, "..");
    for (Index = 0; Index < RTL_NUMBER_OF(LxCgroupFiles); Index++)
    {
        if (LxCgroupFileAllowed(Cgroup, Index))
            LxDirAdd(List, LX_DT_REG, LxCgroupFiles[Index]);
    }
    ExAcquireFastMutex(&LxCgroupLock);
    for (Entry = Cgroup->Children.Flink; Entry != &Cgroup->Children; Entry = Entry->Flink)
        LxDirAdd(List, LX_DT_DIR, CONTAINING_RECORD(Entry, LX_CGROUP, Sibling)->Name);
    ExReleaseFastMutex(&LxCgroupLock);
}

static
VOID
LxGenFdinfo(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLX_FILE File;
    ULONG Flags = 0;

    if (Process == NULL)
        return;
    File = LxFdGet(Process, Synth->Fd);
    if (File != NULL)
    {
        ExAcquireFastMutex(&Process->Lock);
        if (Process->FdFlags[Synth->Fd] & LX_FD_CLOEXEC)
            Flags = LX_O_CLOEXEC;
        ExReleaseFastMutex(&Process->Lock);
    }
    LxProcessDereference(Process);
    if (File == NULL)
        return;

    LxTextPrintf(Text, "pos:\t%lld\nflags:\t0%lo\nmnt_id:\t%lu\nino:\t%llu\n",
                 File->Position, File->Flags | Flags, (File->Mount != NULL) ? File->Mount->Id : 15UL, File->Ino);
    LxFdinfoExtra(File, Text->Failed ? NULL : (PVOID)Text);
    LxFileDereference(File);
}

VOID
LxFdinfoAppend(
    _In_ PVOID TextContext,
    _In_ PCSTR Line)
{
    if (TextContext != NULL)
        LxTextAppend(TextContext, Line, (ULONG)strlen(Line));
}

static
LONG
LxCgroupWriteProcs(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    PLX_CGROUP Cgroup = File->HandlerContext;
    PLX_PROCESS Process;
    LONG64 Pid;
    LONG Error;

    UNREFERENCED_PARAMETER(Length);
    if ((Current == NULL) || (Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    Error = LxParseWriteNumber(Data, &Pid);
    if (Error != 0)
        return Error;
    if (Pid == 0)
    {
        Process = Current->Process;
        LxProcessReference(Process);
    }
    else
    {
        Process = LxProcessLookupNr(Current->Process, (ULONG)Pid);
        if (Process == NULL)
        {
            PLX_THREAD Thread = LxThreadLookupNr(Current->Process, (ULONG)Pid, &Process);

            UNREFERENCED_PARAMETER(Thread);
            if (Process == NULL)
                return -LX_ESRCH;
        }
    }
    if (Process->Exited)
    {
        LxProcessDereference(Process);
        return -LX_ESRCH;
    }
    if ((Cgroup->Parent != NULL) && (Cgroup->SubtreeMask != 0))
    {
        LxProcessDereference(Process);
        return -LX_EBUSY;
    }
    LxCgroupAttach(Process, Cgroup);
    LxProcessDereference(Process);
    return 0;
}

static
LONG
LxCgroupWriteKill(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_THREAD Current = LxCurrentThread();
    LONG64 Value;
    LONG Error;

    UNREFERENCED_PARAMETER(Length);
    if (Current == NULL)
        return -LX_ESRCH;
    Error = LxParseWriteNumber(Data, &Value);
    if (Error != 0)
        return Error;
    if (Value != 1)
        return -LX_EINVAL;
    LxCgroupKill(Current->Process->Instance, File->HandlerContext);
    return 0;
}

static
LONG
LxCgroupWriteFreeze(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    LONG64 Value;
    LONG Error;

    UNREFERENCED_PARAMETER(Length);
    Error = LxParseWriteNumber(Data, &Value);
    if (Error != 0)
        return Error;
    if ((Value != 0) && (Value != 1))
        return -LX_EINVAL;
    if (Cgroup->Frozen != (BOOLEAN)Value)
    {
        Cgroup->Frozen = (BOOLEAN)Value;
        LxCgroupNotifyEvents(Cgroup);
    }
    return 0;
}

static
LONG
LxCgroupWriteSubtree(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext, Child;
    PCSTR Cursor = Data, Limit = Data + strnlen(Data, Length);
    ULONG Enable = 0, Disable = 0, Available;
    PLIST_ENTRY Entry;
    LONG Error = 0;

    if ((Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    while (Cursor < Limit)
    {
        PCSTR Start;
        SIZE_T Size;
        ULONG Bit = 0, Index;

        while ((Cursor < Limit) && ((*Cursor == ' ') || (*Cursor == '\n') || (*Cursor == '\t')))
            Cursor++;
        if (Cursor >= Limit)
            break;
        Start = Cursor;
        while ((Cursor < Limit) && (*Cursor != ' ') && (*Cursor != '\n') && (*Cursor != '\t'))
            Cursor++;
        Size = (SIZE_T)(Cursor - Start);
        if ((Size < 2) || ((Start[0] != '+') && (Start[0] != '-')))
            return -LX_EINVAL;
        for (Index = 0; Index < RTL_NUMBER_OF(LxCgroupControllers); Index++)
        {
            if ((strlen(LxCgroupControllers[Index]) == Size - 1) &&
                (strncmp(LxCgroupControllers[Index], Start + 1, Size - 1) == 0))
            {
                Bit = 1UL << Index;
            }
        }
        if (Bit == 0)
            return -LX_EINVAL;
        if (Start[0] == '+')
        {
            Enable |= Bit;
            Disable &= ~Bit;
        }
        else
        {
            Disable |= Bit;
            Enable &= ~Bit;
        }
    }

    ExAcquireFastMutex(&LxCgroupLock);
    Available = (Cgroup->Parent != NULL) ? Cgroup->Parent->SubtreeMask : LX_CGROUP_ALL;
    Enable &= ~Cgroup->SubtreeMask;
    Disable &= Cgroup->SubtreeMask;
    if (Enable & ~Available)
    {
        Error = -LX_ENOENT;
    }
    else if (Enable && (Cgroup->Parent != NULL) && (Cgroup->Members != 0))
    {
        Error = -LX_EBUSY;
    }
    else if (Disable)
    {
        for (Entry = Cgroup->Children.Flink; Entry != &Cgroup->Children; Entry = Entry->Flink)
        {
            Child = CONTAINING_RECORD(Entry, LX_CGROUP, Sibling);
            if (!Child->Removed && (Child->SubtreeMask & Disable))
                Error = -LX_EBUSY;
        }
    }
    if (Error == 0)
        Cgroup->SubtreeMask = (Cgroup->SubtreeMask | Enable) & ~Disable;
    ExReleaseFastMutex(&LxCgroupLock);
    return Error;
}

static
LONG
LxParseCpuList(
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _In_ ULONG Bits,
    _Out_ PULONG64 Mask)
{
    PCSTR Cursor = Data, End = Data + strnlen(Data, Length);

    *Mask = 0;
    while ((End > Cursor) && ((End[-1] == ' ') || (End[-1] == '\t') || (End[-1] == '\n')))
        End--;
    while ((Cursor < End) && ((*Cursor == ' ') || (*Cursor == '\t')))
        Cursor++;
    while (Cursor < End)
    {
        ULONG64 Range[2] = { 0, 0 };
        ULONG Part, Digits;

        for (Part = 0; Part < 2; Part++)
        {
            for (Digits = 0; (Cursor < End) && (*Cursor >= '0') && (*Cursor <= '9'); Digits++)
            {
                Range[Part] = Range[Part] * 10 + (ULONG64)(*Cursor++ - '0');
                if (Range[Part] > MAXULONG)
                    return -LX_EOVERFLOW;
            }
            if (Digits == 0)
                return -LX_EINVAL;
            if ((Part != 0) || (Cursor >= End) || (*Cursor != '-'))
                break;
            Cursor++;
        }
        if (Part == 0)
            Range[1] = Range[0];
        if (Range[0] > Range[1])
            return -LX_EINVAL;
        if (Range[1] >= Bits)
            return -LX_ERANGE;
        for (; Range[0] <= Range[1]; Range[0]++)
            *Mask |= 1ULL << Range[0];
        if (Cursor >= End)
            break;
        if ((*Cursor != ',') || (++Cursor >= End))
            return -LX_EINVAL;
    }
    return 0;
}

static
LONG
LxCgroupWriteCpusetCpus(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    PLX_THREAD Current = LxCurrentThread();
    ULONG64 Mask;
    LONG Error;

    if ((Cgroup == NULL) || Cgroup->Removed || (Current == NULL))
        return -LX_ENODEV;
    Error = LxParseCpuList(Data, Length, KeQueryActiveProcessorCount(NULL), &Mask);
    if (Error != 0)
        return Error;
    ExAcquireFastMutex(&LxCgroupLock);
    if ((Cgroup->Populated != 0) && (Cgroup->CpusetCpus != 0) && (Mask == 0))
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return -LX_ENOSPC;
    }
    Cgroup->CpusetCpus = Mask;
    ExReleaseFastMutex(&LxCgroupLock);
    LxCgroupRefreshAffinity(Current->Process->Instance, Cgroup);
    return 0;
}

static
LONG
LxCgroupWriteCpusetMems(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    ULONG64 Mask;
    LONG Error;

    if ((Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    Error = LxParseCpuList(Data, Length, 64, &Mask);
    if (Error != 0)
        return Error;
    if (Mask & ~1ULL)
        return -LX_EINVAL;
    ExAcquireFastMutex(&LxCgroupLock);
    if ((Cgroup->Populated != 0) && (Cgroup->CpusetMems != 0) && (Mask == 0))
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return -LX_ENOSPC;
    }
    Cgroup->CpusetMems = Mask;
    ExReleaseFastMutex(&LxCgroupLock);
    return 0;
}

static
LONG
LxCgroupWriteCpusetExclusive(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    ULONG64 Mask;
    LONG Error;

    if ((Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    Error = LxParseCpuList(Data, Length, KeQueryActiveProcessorCount(NULL), &Mask);
    if (Error != 0)
        return Error;
    return (Mask != 0) ? -LX_EINVAL : 0;
}

static
LONG
LxCgroupWriteCpusetPartition(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    PCSTR End = Data + strnlen(Data, Length);

    if ((Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    while ((End > Data) && ((End[-1] == ' ') || (End[-1] == '\n')))
        End--;
    return ((End - Data == 6) && (strncmp(Data, "member", 6) == 0)) ? 0 : -LX_EINVAL;
}

static
LONG
LxCgroupWritePidsMax(
    _In_ PLX_FILE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    PLX_CGROUP Cgroup = File->HandlerContext;
    PCSTR End = Data + strnlen(Data, Length);
    LONG64 Value;

    if ((Cgroup == NULL) || Cgroup->Removed)
        return -LX_ENODEV;
    while ((Data < End) && ((*Data == ' ') || (*Data == '\t') || (*Data == '\n')))
        Data++;
    while ((End > Data) && ((End[-1] == ' ') || (End[-1] == '\t') || (End[-1] == '\n')))
        End--;
    if ((End - Data == 3) && (strncmp(Data, "max", 3) == 0))
    {
        Value = LX_PIDS_MAX;
    }
    else
    {
        CHAR Number[24];

        if ((End == Data) || (End - Data >= (LONG_PTR)sizeof(Number)))
            return -LX_EINVAL;
        RtlCopyMemory(Number, Data, End - Data);
        Number[End - Data] = '\0';
        if ((LxParseWriteNumber(Number, &Value) != 0) || (Value < 0) || (Value >= LX_PIDS_MAX))
            return -LX_EINVAL;
    }
    ExAcquireFastMutex(&LxCgroupLock);
    Cgroup->PidsLimit = Value;
    ExReleaseFastMutex(&LxCgroupLock);
    return 0;
}

static
VOID
LxGenCgroupFile(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_CGROUP Cgroup = Synth->Cgroup;
    PCSTR Name = LxCgroupFiles[Synth->Index];
    PLIST_ENTRY Entry;

    if ((strcmp(Name, "cgroup.procs") == 0) || (strcmp(Name, "cgroup.threads") == 0))
    {
        ExAcquireFastMutex(&LxProcessListLock);
        for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
        {
            PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

            if ((Process->Instance == Self->Instance) && (Process->Cgroup == Cgroup) && !Process->Exited &&
                (LxPidNr(Self, Process) != 0))
            {
                LxTextPrintf(Text, "%lu\n", LxPidNr(Self, Process));
            }
        }
        ExReleaseFastMutex(&LxProcessListLock);
    }
    else if (strcmp(Name, "cgroup.events") == 0)
    {
        LxTextPrintf(Text, "populated %u\nfrozen %u\n", (Cgroup->Populated != 0) ? 1U : 0U, Cgroup->Frozen ? 1U : 0U);
    }
    else if (strcmp(Name, "cgroup.freeze") == 0)
    {
        LxTextPrintf(Text, "%u\n", Cgroup->Frozen ? 1U : 0U);
    }
    else if (strcmp(Name, "cgroup.type") == 0)
    {
        LxTextAppend(Text, "domain\n", 7);
    }
    else if ((strcmp(Name, "cgroup.max.depth") == 0) || (strcmp(Name, "cgroup.max.descendants") == 0))
    {
        LxTextAppend(Text, "max\n", 4);
    }
    else if (strcmp(Name, "cgroup.stat") == 0)
    {
        ULONG Descendants = 0;

        ExAcquireFastMutex(&LxCgroupLock);
        for (Entry = Cgroup->Children.Flink; Entry != &Cgroup->Children; Entry = Entry->Flink)
            Descendants++;
        ExReleaseFastMutex(&LxCgroupLock);
        LxTextPrintf(Text, "nr_descendants %lu\nnr_subsys_cpu 0\nnr_dying_descendants 0\n", Descendants);
    }
    else if ((strcmp(Name, "cgroup.controllers") == 0) || (strcmp(Name, "cgroup.subtree_control") == 0))
    {
        ULONG Mask, Index;
        BOOLEAN First = TRUE;

        ExAcquireFastMutex(&LxCgroupLock);
        if (strcmp(Name, "cgroup.subtree_control") == 0)
            Mask = Cgroup->SubtreeMask;
        else
            Mask = (Cgroup->Parent != NULL) ? Cgroup->Parent->SubtreeMask : LX_CGROUP_ALL;
        ExReleaseFastMutex(&LxCgroupLock);
        for (Index = 0; Index < RTL_NUMBER_OF(LxCgroupControllers); Index++)
        {
            if (Mask & (1UL << Index))
            {
                LxTextPrintf(Text, "%s%s", First ? "" : " ", LxCgroupControllers[Index]);
                First = FALSE;
            }
        }
        LxTextAppend(Text, "\n", 1);
    }
    else if (strncmp(Name, "cpuset.", 7) == 0)
    {
        CHAR List[256];
        ULONG64 Mask = 0;

        ExAcquireFastMutex(&LxCgroupLock);
        if (strcmp(Name, "cpuset.cpus") == 0)
            Mask = Cgroup->CpusetCpus;
        else if (strcmp(Name, "cpuset.cpus.effective") == 0)
            Mask = LxCgroupCpusEffectiveLocked(Cgroup);
        else if (strcmp(Name, "cpuset.mems") == 0)
            Mask = Cgroup->CpusetMems;
        else if (strcmp(Name, "cpuset.mems.effective") == 0)
            Mask = 1;
        ExReleaseFastMutex(&LxCgroupLock);
        if (strcmp(Name, "cpuset.cpus.partition") == 0)
        {
            LxTextAppend(Text, "member\n", 7);
        }
        else
        {
            LxFormatCpuList(Mask, List, sizeof(List));
            LxTextPrintf(Text, "%s\n", List);
        }
    }
    else if (strncmp(Name, "pids.", 5) == 0)
    {
        LONG64 Value;

        ExAcquireFastMutex(&LxCgroupLock);
        if (strcmp(Name, "pids.max") == 0)
            Value = Cgroup->PidsLimit;
        else if (strcmp(Name, "pids.current") == 0)
            Value = Cgroup->PidsCount;
        else if (strcmp(Name, "pids.peak") == 0)
            Value = Cgroup->PidsPeak;
        else if (strcmp(Name, "pids.events") == 0)
            Value = Cgroup->PidsEvents;
        else
            Value = Cgroup->PidsEventsLocal;
        ExReleaseFastMutex(&LxCgroupLock);
        if ((strcmp(Name, "pids.max") == 0) && (Value >= LX_PIDS_MAX))
            LxTextAppend(Text, "max\n", 4);
        else if (strncmp(Name, "pids.events", 11) == 0)
            LxTextPrintf(Text, "max %lld\n", Value);
        else
            LxTextPrintf(Text, "%lld\n", Value);
    }
    else if (strcmp(Name, "cgroup.pressure") == 0)
    {
        LxTextAppend(Text, "1\n", 2);
    }
    else if (strncmp(Name, "cpu.stat", 8) == 0)
    {
        LxTextAppend(Text, "usage_usec 0\nuser_usec 0\nsystem_usec 0\nnr_periods 0\nnr_throttled 0\nthrottled_usec 0\n", 82);
    }
}

LONG
LxSynthOpen(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Flags,
    _Out_ PLX_FILE *Result)
{
    LX_SYNTH Synth;
    LX_TEXT Text;
    PLX_FILE File = NULL;
    PLX_WRITER Writer = NULL;
    LONG Error;

    *Result = NULL;
    Error = LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth);
    if (Error != 0)
        return Error;
    RtlZeroMemory(&Text, sizeof(Text));

    if (Synth.Type == LX_S_IFDIR)
    {
        if ((Flags & LX_O_ACCMODE) != LX_O_RDONLY)
        {
            Error = -LX_EISDIR;
            goto Exit;
        }
        if (Node->Mount->Kind == LxFsCgroup)
            LxListCgroup(&Synth, &Text);
        else if (Synth.Id == LxSysDevNode)
        {
            LxDirAdd(&Text, LX_DT_DIR, ".");
            LxDirAdd(&Text, LX_DT_DIR, "..");
            LxSysDevList(Synth.Prefix, LxSysDevDirAdd, &Text);
        }
        else if (Synth.Id == LxMqRoot)
        {
            LxDirAdd(&Text, LX_DT_DIR, ".");
            LxDirAdd(&Text, LX_DT_DIR, "..");
            if (Node->Mount->NsFile != NULL)
                LxMqList(Node->Mount->NsFile, LxMqDirAdd, &Text);
        }
        else if (Synth.Id == LxSysLoop)
        {
            LxSysLoopListEntries(Process, &Synth, &Text);
        }
        else if ((Synth.Id == LxSysDir) || (Synth.Id == LxProcRoot) || (Synth.Id == LxPtsRoot))
        {
            if (Synth.Id == LxSysDir)
            {
                LxDirAdd(&Text, LX_DT_DIR, ".");
                LxDirAdd(&Text, LX_DT_DIR, "..");
                if (Synth.Index == MAXULONG - 1)
                {
                    LxDirAdd(&Text, LX_DT_REG, "online");
                }
                else
                {
                    LxTableList(LxSysTable, RTL_NUMBER_OF(LxSysTable), Synth.Prefix, &Text);
                    LxSysDevList(Synth.Prefix, LxSysDevDirAdd, &Text);
                    LxSysLoopListTable(Process, Synth.Prefix, &Text);
                    if (strcmp(Synth.Prefix, "devices/system/cpu") == 0)
                    {
                        ULONG Cpu, Count = KeQueryActiveProcessorCount(NULL);

                        for (Cpu = 0; Cpu < Count; Cpu++)
                        {
                            CHAR Name[16];

                            RtlStringCbPrintfA(Name, sizeof(Name), "cpu%lu", Cpu);
                            LxDirAdd(&Text, LX_DT_DIR, Name);
                        }
                    }
                }
            }
            else if (Synth.Id == LxPtsRoot)
            {
                LxDirAdd(&Text, LX_DT_DIR, ".");
                LxDirAdd(&Text, LX_DT_DIR, "..");
                ULONG Indexes[64], Count, Slot;

                LxDirAdd(&Text, LX_DT_CHR, "ptmx");
                Count = LxTtyList(Process->Instance, LxPtsDevpts(Process, Synth.Mount), Indexes, RTL_NUMBER_OF(Indexes));
                for (Slot = 0; Slot < Count; Slot++)
                {
                    CHAR Name[16];

                    RtlStringCbPrintfA(Name, sizeof(Name), "%lu", Indexes[Slot]);
                    LxDirAdd(&Text, LX_DT_CHR, Name);
                }
            }
            else
            {
                LxListProc(Process, &Synth, &Text);
            }
        }
        else
        {
            if ((Synth.Id == LxProcPidDir) && (Synth.Index < RTL_NUMBER_OF(LxPidEntries)) &&
                (strcmp(LxPidEntries[Synth.Index].Name, "attr") == 0))
            {
                LxDirAdd(&Text, LX_DT_DIR, ".");
                LxDirAdd(&Text, LX_DT_DIR, "..");
                LxDirAdd(&Text, LX_DT_REG, "current");
                LxDirAdd(&Text, LX_DT_REG, "exec");
            }
            else
            {
                LxListProc(Process, &Synth, &Text);
            }
        }
        if (Text.Failed)
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }

        File = LxFileAlloc(&LxSynthDirOps, LX_O_RDONLY | (Flags & (LX_O_NONBLOCK | LX_O_PATH)), LX_S_IFDIR);
        if (File != NULL)
            File->SynthRelative = LxStrDup(Node->Relative);
        if ((File == NULL) || (File->SynthRelative == NULL))
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }
        File->Mode = Synth.Mode;
        File->Directory = TRUE;
        File->Data = (PUCHAR)Text.Buffer;
        File->DataLength = Text.Length;
        Text.Buffer = NULL;
        File->Ino = LxSynthIno(Node);
        if (Node->Mount->Kind == LxFsCgroup)
            File->HandlerContext = Synth.Cgroup;
        if ((Node->Mount->Kind == LxFsProc) && (Synth.Id == LxProcPidDir) && (Synth.Tid == 0) && (Synth.Pid != 0) &&
            LxProcIsPidRoot(Node->Relative))
        {
            File->PidProcess = LxProcessLookup(Process->Instance, Synth.Pid);
        }
    }
    else if ((Synth.Id == LxNsfsFile) || ((Synth.Id == LxProcNsLink) && !(Flags & LX_O_NOFOLLOW)))
    {
        PLX_NS Ns = LxNsFromNode(Process, Node);

        if (Ns == NULL)
        {
            Error = -LX_ENOENT;
            goto Exit;
        }
        File = LxNsFileOpen(Ns, Flags);
        LxNsDereference(Ns);
        if (File == NULL)
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }
        *Result = File;
        File = NULL;
        goto Exit;
    }
    else if (Synth.Type == LX_S_IFLNK)
    {
        if (((Synth.Id == LxProcFdLink) || (Synth.Id == LxProcMapFileLink)) && !(Flags & LX_O_NOFOLLOW))
        {
            PLX_PROCESS Target = LxProcessLookup(Process->Instance, Synth.Pid);
            if ((Synth.Id == LxProcMapFileLink) && !LxCapable(Process, LX_CAP_CHECKPOINT_RESTORE) &&
                !LxCapable(Process, LX_CAP_SYS_ADMIN))
            {
                if (Target != NULL)
                    LxProcessDereference(Target);
                Error = -LX_EPERM;
                goto Exit;
            }
            if (Synth.Id == LxProcMapFileLink)
                File = (Target != NULL) ? LxMmFileAt(Target, Synth.MapStart, Synth.MapEnd) : NULL;
            else
                File = (Target != NULL) ? LxFdGet(Target, Synth.Fd) : NULL;
            if (Target != NULL)
                LxProcessDereference(Target);
            if (File == NULL)
            {
                Error = -LX_ENOENT;
                goto Exit;
            }
            if (!(Flags & LX_O_PATH))
            {
                Error = LxVfsReopen(Process, File, Flags, Result);
                if (Error <= 0)
                    goto Exit;
                Error = 0;
            }
            *Result = File;
            File = NULL;
            goto Exit;
        }
        if (!(Flags & LX_O_PATH))
        {
            Error = -LX_ELOOP;
            goto Exit;
        }
        File = LxDataFileCreate(Node->Path, NULL, 0, LX_S_IFLNK | 0777);
        if (File == NULL)
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }
        File->Flags = LX_O_PATH;
        File->Ino = LxSynthIno(Node);
        File->Mount = Node->Mount;
        *Result = File;
        File = NULL;
        goto Exit;
    }
    else if (Synth.Id == LxMqFile)
    {
        Error = LxMqOpenNamed(Process, Node->Mount->NsFile, Synth.Prefix, Flags & ~(LX_O_CREAT | LX_O_EXCL), 0, NULL,
                              Node->Path, &File);
        if (Error != 0)
            goto Exit;
    }
    else if (Synth.Id == LxPtsTty)
    {
        PLX_TTY Tty = LxTtyLookup(Process->Instance, LxPtsDevpts(Process, Synth.Mount), Synth.Index);

        if (Tty == NULL)
        {
            Error = -LX_EIO;
            goto Exit;
        }
        Error = LxTtyOpenSlave(Process, Tty, Flags, &File);
        LxTtyDereference(Tty);
        if (Error != 0)
            goto Exit;
        File->Ino = LxSynthIno(Node);
    }
    else if (Synth.Id == LxPtsPtmx)
    {
        Error = LxPtmxOpen(Process, LxPtsDevpts(Process, Synth.Mount), Flags, &File);
        if (Error != 0)
            goto Exit;
        File->Ino = LxSynthIno(Node);
    }
    else
    {
        PLX_EVENT_SOURCE Events = NULL;
        PVOID Context = NULL;

        switch (Synth.Id)
        {
            case LxProcStatic:
                if (LxProcStaticTable[Synth.Index].Generator != NULL)
                    LxProcStaticTable[Synth.Index].Generator(Process, &Synth, &Text);
                else
                    LxTextAppend(&Text, LxProcStaticTable[Synth.Index].Text, (ULONG)strlen(LxProcStaticTable[Synth.Index].Text));
                break;

            case LxProcSysFile:
                if (LxProcSysTable[Synth.Index].Generator != NULL)
                    LxProcSysTable[Synth.Index].Generator(Process, &Synth, &Text);
                else
                    LxTextAppend(&Text, LxProcSysTable[Synth.Index].Text, (ULONG)strlen(LxProcSysTable[Synth.Index].Text));
                Writer = LxProcSysTable[Synth.Index].Writer;
                Context = (PVOID)(ULONG_PTR)Synth.Index;
                break;

            case LxSysLoop:
                LxSysLoopRead(Process, &Synth, &Text);
                break;

            case LxSysDevNode:
            {
                PVOID Data;
                ULONG Length;

                if (LxSysDevRead(Process, Synth.Prefix, &Data, &Length) == 0)
                {
                    LxTextAppend(&Text, Data, Length);
                    LxFree(Data);
                }
                break;
            }

            case LxSysFile:
                if (Synth.Index == MAXULONG - 1)
                {
                    LxTextAppend(&Text, "1\n", 2);
                    Writer = LxWriteAccept;
                }
                else if (LxSysTable[Synth.Index].Generator != NULL)
                {
                    LxSysTable[Synth.Index].Generator(Process, &Synth, &Text);
                    Writer = LxSysTable[Synth.Index].Writer;
                }
                else
                {
                    LxTextAppend(&Text, LxSysTable[Synth.Index].Text, (ULONG)strlen(LxSysTable[Synth.Index].Text));
                    Writer = LxSysTable[Synth.Index].Writer;
                }
                break;

            case LxProcPidFile:
                if ((Synth.Index < RTL_NUMBER_OF(LxPidEntries)) &&
                    ((strcmp(LxPidEntries[Synth.Index].Name, "pagemap") == 0) ||
                     (strcmp(LxPidEntries[Synth.Index].Name, "mem") == 0)))
                {
                    Error = (LxPidEntries[Synth.Index].Name[0] == 'm') ?
                            LxPidMemOpen(Process, Synth.Pid, Flags, &File) :
                            LxPagemapOpen(Process, Synth.Pid, Flags, &File);
                    if (Error != 0)
                        goto Exit;
                    File->Ino = LxSynthIno(Node);
                    File->Mount = Node->Mount;
                    File->Path = LxStrDup(Node->Path);
                    *Result = File;
                    File = NULL;
                    goto Exit;
                }
                if (Synth.Index == MAXULONG)
                {
                    LxTextAppend(&Text, "unconfined\n", 11);
                    Writer = LxWriteAccept;
                    break;
                }
                if (LxPidEntries[Synth.Index].Generator != NULL)
                    LxPidEntries[Synth.Index].Generator(Process, &Synth, &Text);
                else
                    LxTextAppend(&Text, LxPidEntries[Synth.Index].Text, (ULONG)strlen(LxPidEntries[Synth.Index].Text));
                Writer = LxPidEntries[Synth.Index].Writer;
                Context = (PVOID)(ULONG_PTR)Synth.Pid;
                if ((Writer == LxWritePidSetgroups) && ((Flags & LX_O_ACCMODE) != LX_O_RDONLY))
                {
                    PLX_PROCESS Target = LxProcessLookup(Process->Instance, Synth.Pid);
                    PLX_NS UserNs;
                    BOOLEAN Capable;

                    if (Target == NULL)
                    {
                        Error = -LX_ESRCH;
                        goto Exit;
                    }
                    UserNs = LxUserNsOf(Target);
                    LxProcessDereference(Target);
                    Capable = LxNsCapable(Process, UserNs, LX_CAP_SYS_ADMIN);
                    LxNsDereference(UserNs);
                    if (!Capable)
                    {
                        Error = -LX_EACCES;
                        goto Exit;
                    }
                }
                break;

            case LxProcFdinfoFile:
                LxGenFdinfo(Process, &Synth, &Text);
                break;

            case LxCgFile:
            {
                PCSTR Name = LxCgroupFiles[Synth.Index];

                LxGenCgroupFile(Process, &Synth, &Text);
                Context = Synth.Cgroup;
                if ((strcmp(Name, "cgroup.procs") == 0) || (strcmp(Name, "cgroup.threads") == 0))
                    Writer = LxCgroupWriteProcs;
                else if (strcmp(Name, "cgroup.kill") == 0)
                    Writer = LxCgroupWriteKill;
                else if (strcmp(Name, "cgroup.freeze") == 0)
                    Writer = LxCgroupWriteFreeze;
                else if (strcmp(Name, "cgroup.subtree_control") == 0)
                    Writer = LxCgroupWriteSubtree;
                else if (strcmp(Name, "pids.max") == 0)
                    Writer = LxCgroupWritePidsMax;
                else if (strcmp(Name, "cpuset.cpus") == 0)
                    Writer = LxCgroupWriteCpusetCpus;
                else if (strcmp(Name, "cpuset.mems") == 0)
                    Writer = LxCgroupWriteCpusetMems;
                else if (strcmp(Name, "cpuset.cpus.exclusive") == 0)
                    Writer = LxCgroupWriteCpusetExclusive;
                else if (strcmp(Name, "cpuset.cpus.partition") == 0)
                    Writer = LxCgroupWriteCpusetPartition;
                else if (strcmp(Name, "cgroup.events") == 0)
                    Events = &Synth.Cgroup->Events;
                else if (Synth.Mode & 0200)
                    Writer = LxWriteAccept;
                break;
            }

            default:
                break;
        }
        if (Text.Failed)
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }
        if (((Flags & LX_O_ACCMODE) != LX_O_RDONLY) && (Writer == NULL))
        {
            Error = -LX_EACCES;
            goto Exit;
        }

        File = LxDataFileCreate(Node->Path, Text.Buffer, Text.Length, LX_S_IFREG | Synth.Mode);
        if (File == NULL)
        {
            Error = -LX_ENOMEM;
            goto Exit;
        }
        File->Flags = Flags & (LX_O_ACCMODE | LX_O_NONBLOCK | LX_O_APPEND);
        File->WriteHandler = Writer;
        File->HandlerContext = Context;
        if ((Synth.Id == LxProcPidFile) && (Synth.Index < RTL_NUMBER_OF(LxPidEntries)) &&
            (strncmp(LxPidEntries[Synth.Index].Name, "mount", 5) == 0))
        {
            Events = &LxProcessNs(Process, LxNsMnt)->Mnt.Events;
            if (LxPidEntries[Synth.Index].Generator != NULL)
            {
                PLX_PROCESS Target = LxProcessLookup(Process->Instance, Synth.Pid);

                if (Target != NULL)
                {
                    File->MountsRoot = LxProcessRoot(Target);
                    if (File->MountsRoot != NULL)
                    {
                        File->MountsNs = LxProcessNs(Target, LxNsMnt);
                        LxNsReference(File->MountsNs);
                        File->MountsFull = (LxPidEntries[Synth.Index].Generator == LxGenPidMountinfo);
                    }
                    LxProcessDereference(Target);
                }
            }
        }
        if (Events != NULL)
        {
            File->EventSource = Events;
            File->EventSeen = Events->Generation;
        }
        File->Ino = LxSynthIno(Node);
        File->Mount = Node->Mount;
        *Result = File;
        File = NULL;
        goto Exit;
    }

    if (File != NULL)
    {
        File->Path = LxStrDup(Node->Path);
        File->Mount = Node->Mount;
        *Result = File;
        File = NULL;
    }

Exit:
    LxFree(Text.Buffer);
    LxFree(Synth.LinkTarget);
    if (File != NULL)
        LxFileDereference(File);
    return Error;
}

LONG
LxSynthStat(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _Out_ PLX_STAT Stat)
{
    LX_SYNTH Synth;
    LARGE_INTEGER Now;
    LONG Error = LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth);

    if (Error != 0)
        return Error;
    RtlZeroMemory(Stat, sizeof(*Stat));
    if (Synth.Id == LxNsfsFile)
    {
        LxFree(Synth.LinkTarget);
        Stat->Mode = LX_S_IFREG | 0444;
        Stat->Nlink = 1;
        Stat->Dev = 4;
        Stat->Ino = (Node->Mount->NsFile != NULL) ? Node->Mount->NsFile->Inum : 0;
        Stat->BlkSize = 4096;
        return 0;
    }
    Stat->Mode = Synth.Type | Synth.Mode;
    Stat->Nlink = (Synth.Type == LX_S_IFDIR) ? 2 : 1;
    Stat->Ino = LxSynthIno(Node);
    Stat->BlkSize = 4096;
    Stat->Dev = Node->Mount->Dev;
    if (Synth.Id == LxMqFile)
    {
        if (!LxMqQuery(Node->Mount->NsFile, Synth.Prefix, Stat))
            return -LX_ENOENT;
        Stat->Nlink = 1;
        Stat->BlkSize = 4096;
        Stat->Dev = Node->Mount->Dev;
        return 0;
    }
    if (Synth.Id == LxPtsTty)
    {
        PLX_TTY Tty = LxTtyLookup(Process->Instance, LxPtsDevpts(Process, Synth.Mount), Synth.Index);

        if (Tty == NULL)
            return -LX_ENOENT;
        Stat->Mode = LX_S_IFCHR | Tty->Mode;
        Stat->Rdev = LxTtyDevice(Tty);
        Stat->Uid = Tty->Uid;
        Stat->Gid = Tty->Gid;
        LxTtyDereference(Tty);
    }
    else if (Synth.Id == LxPtsPtmx)
    {
        Stat->Rdev = (5 << 8) | 2;
    }
    else if ((Synth.Id == LxCgDir) || (Synth.Id == LxCgFile))
    {
        Stat->Uid = Synth.Uid;
        Stat->Gid = Synth.Gid;
    }
    if (Synth.Type == LX_S_IFLNK && Synth.LinkTarget != NULL)
        Stat->Size = (LONG64)strlen(Synth.LinkTarget);

    if ((Synth.Pid != 0) && (Synth.Id != LxProcRoot))
    {
        PLX_PROCESS Target = LxProcessLookup(Process->Instance, Synth.Pid);

        if (Target != NULL)
        {
            Stat->Uid = Target->Cred.Uid[1];
            Stat->Gid = Target->Cred.Gid[1];
            if (Synth.Id == LxProcTaskDir)
                Stat->Nlink = 2 + (Target->Exited ? 1 : (ULONG)max(Target->ThreadCount, 1));
            LxProcessDereference(Target);
        }
    }

    KeQuerySystemTime(&Now);
    LxNtTimeToTimespec(Now.QuadPart, &Stat->Mtime);
    Stat->Atime = Stat->Ctime = Stat->Mtime;
    LxFree(Synth.LinkTarget);
    return 0;
}

LONG
LxSynthMkdir(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node)
{
    PLX_CRED Cred = LxProcessCred(Process);
    PCSTR Relative = Node->Relative, Name;
    PSTR Parent;
    LX_SYNTH Synth;
    LONG Error;
    PLX_CGROUP Child;

    if (Node->Mount->Kind != LxFsCgroup)
        return -LX_EPERM;

    Name = strrchr(Relative, '/');
    if (Name == NULL)
    {
        Parent = LxStrDup("");
        Name = Relative;
    }
    else
    {
        Parent = LxAlloc((SIZE_T)(Name - Relative) + 1);
        if (Parent != NULL)
        {
            RtlCopyMemory(Parent, Relative, (SIZE_T)(Name - Relative));
            Parent[Name - Relative] = '\0';
        }
        Name++;
    }
    if (Parent == NULL)
        return -LX_ENOMEM;
    if ((*Name == '\0') || (strchr(Name, '.') == Name) || (strlen(Name) > 255))
    {
        LxFree(Parent);
        return -LX_EINVAL;
    }

    Error = LxSynthResolve(Process, Node->Mount, Parent, &Synth);
    LxFree(Parent);
    if (Error != 0)
        return Error;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxCgDir)
        return -LX_ENOTDIR;

    ExAcquireFastMutex(&LxCgroupLock);
    if (LxCgroupFindChild(Synth.Cgroup, Name, strlen(Name)) != NULL)
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return -LX_EEXIST;
    }
    Child = LxCgroupCreateChild(Synth.Cgroup, Name);
    ExReleaseFastMutex(&LxCgroupLock);
    if (Child == NULL)
        return -LX_ENOMEM;
    if (Child->Instance == NULL)
        Child->Instance = Process->Instance;
    Child->Uid = Child->FileUid = Cred->Fsuid;
    Child->Gid = Child->FileGid = Cred->Fsgid;
    return 0;
}

PLX_TTY
LxPtsNodeTty(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node)
{
    LX_SYNTH Synth;

    if (LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth) != 0)
        return NULL;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxPtsTty)
        return NULL;
    return LxTtyLookup(Process->Instance, LxPtsDevpts(Process, Synth.Mount), Synth.Index);
}

LONG
LxCgroupSetAttributes(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node,
    _In_ ULONG Mode,
    _In_ ULONG Uid,
    _In_ ULONG Gid)
{
    PLX_CRED Cred = LxProcessCred(Process);
    PLX_CGROUP_FILE_ATTR Attr = NULL, New;
    PLIST_ENTRY Entry;
    LX_SYNTH Synth;
    ULONG Index;
    LONG Error;

    Error = LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth);
    if (Error != 0)
        return Error;
    LxFree(Synth.LinkTarget);
    if ((Synth.Id != LxCgDir) && (Synth.Id != LxCgFile))
        return 0;
    if ((Uid != MAXULONG) && !((Cred->Fsuid == Synth.Uid) && (Uid == Synth.Uid)) &&
        !LxCapable(Process, LX_CAP_CHOWN))
    {
        return -LX_EPERM;
    }
    if ((Gid != MAXULONG) && !LxCapable(Process, LX_CAP_CHOWN))
    {
        BOOLEAN Allowed = (Cred->Fsuid == Synth.Uid) && ((Gid == Synth.Gid) || (Gid == Cred->Fsgid));

        for (Index = 0; !Allowed && (Cred->Fsuid == Synth.Uid) && (Index < Cred->GroupCount); Index++)
            Allowed = (Cred->Groups[Index] == Gid);
        if (!Allowed)
            return -LX_EPERM;
    }
    if ((Mode != MAXULONG) && (Cred->Fsuid != Synth.Uid) && !LxCapable(Process, LX_CAP_FOWNER))
        return -LX_EPERM;

    New = (Synth.Id == LxCgFile) ? LxAlloc(sizeof(*New)) : NULL;
    if ((Synth.Id == LxCgFile) && (New == NULL))
        return -LX_ENOMEM;
    ExAcquireFastMutex(&LxCgroupLock);
    if (Synth.Id == LxCgDir)
    {
        if (Uid != MAXULONG)
            Synth.Cgroup->Uid = Uid;
        if (Gid != MAXULONG)
            Synth.Cgroup->Gid = Gid;
        if (Mode != MAXULONG)
            Synth.Cgroup->Mode = Mode & 07777;
    }
    else
    {
        for (Entry = Synth.Cgroup->FileAttributes.Flink; Entry != &Synth.Cgroup->FileAttributes; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, LX_CGROUP_FILE_ATTR, Link)->Index == Synth.Index)
            {
                Attr = CONTAINING_RECORD(Entry, LX_CGROUP_FILE_ATTR, Link);
                break;
            }
        }
        if (Attr == NULL)
        {
            Attr = New;
            New = NULL;
            Attr->Index = Synth.Index;
            Attr->Uid = Synth.Uid;
            Attr->Gid = Synth.Gid;
            Attr->Mode = Synth.Mode;
            InsertTailList(&Synth.Cgroup->FileAttributes, &Attr->Link);
        }
        if (Uid != MAXULONG)
            Attr->Uid = Uid;
        if (Gid != MAXULONG)
            Attr->Gid = Gid;
        if (Mode != MAXULONG)
            Attr->Mode = Mode & 07777;
    }
    ExReleaseFastMutex(&LxCgroupLock);
    LxFree(New);
    return 0;
}

LONG
LxSynthRmdir(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node)
{
    LX_SYNTH Synth;
    PLX_CGROUP Cgroup;
    LONG Error;

    if (Node->Mount->Kind != LxFsCgroup)
        return -LX_EPERM;
    Error = LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth);
    if (Error != 0)
        return Error;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxCgDir)
        return -LX_ENOTDIR;
    Cgroup = Synth.Cgroup;
    if (Cgroup->Parent == NULL)
        return -LX_EBUSY;

    ExAcquireFastMutex(&LxCgroupLock);
    if ((Cgroup->Populated != 0) || !IsListEmpty(&Cgroup->Children))
    {
        ExReleaseFastMutex(&LxCgroupLock);
        return -LX_EBUSY;
    }
    RemoveEntryList(&Cgroup->Sibling);
    InitializeListHead(&Cgroup->Sibling);
    Cgroup->Removed = TRUE;
    InsertTailList(&Cgroup->Root->DeadList, &Cgroup->DeadLink);
    ExReleaseFastMutex(&LxCgroupLock);
    return 0;
}

VOID
LxSynthInitialize(VOID)
{
    ExInitializeFastMutex(&LxCgroupLock);
}

PLX_NS
LxNsFromNode(
    _In_ PLX_PROCESS Process,
    _In_ PLX_NODE Node)
{
    LX_SYNTH Synth;
    PLX_PROCESS Target;
    PLX_NS Ns;
    LX_NS_KIND Kind;

    if (Node->Mount == NULL)
        return NULL;
    if ((Node->Mount->Kind == LxFsNsfs) && (Node->Relative[0] == '\0') && (Node->Mount->NsFile != NULL))
    {
        LxNsReference(Node->Mount->NsFile);
        return Node->Mount->NsFile;
    }
    if (Node->Mount->Kind != LxFsProc)
        return NULL;
    if (LxSynthResolve(Process, Node->Mount, Node->Relative, &Synth) != 0)
        return NULL;
    LxFree(Synth.LinkTarget);
    if (Synth.Id != LxProcNsLink)
        return NULL;
    Target = LxProcessLookup(Process->Instance, Synth.Pid);
    if (Target == NULL)
        return NULL;
    ExAcquireFastMutex(&Target->Lock);
    Ns = LxProcTaskNs(Target, Synth.Tid, Synth.Prefix, &Kind);
    if (Ns != NULL)
        LxNsReference(Ns);
    ExReleaseFastMutex(&Target->Lock);
    LxProcessDereference(Target);
    return Ns;
}
