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
    LxMqFile
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
    LxEventSourceInit(&Root->Events);
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
    LxEventSourceInit(&Child->Events);
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

static
ULONG
LxProcessTtyNumber(
    _In_ PLX_PROCESS Process)
{
    if (Process->Tty == NULL)
        return 0;
    return (ULONG)LxTtyDevice(Process->Tty);
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

    if (Process == NULL)
        return;
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
                 LxProcessTtyNumber(Process),
                 Process->Tty ? (LONG)LxGlobalToNrIn(LxProcPidNs(Self, Synth), Process->Tty->ForegroundPgrp) : -1L,
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
    PLIST_ENTRY Entry;
    CHAR Comm[16], List[256];
    LONG Signal;
    ULONG Index, Fds = 0;

    if (Process == NULL)
        return;
    ExAcquireFastMutex(&Process->Lock);
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
            Blocked = Thread->SigMask;
            Affinity &= Thread->Affinity;
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
                 "Name:\t%s\nUmask:\t%04lo\nState:\t%s\nTgid:\t%lu\nNgid:\t0\nPid:\t%lu\nPPid:\t%lu\nTracerPid:\t0\n"
                 "Uid:\t%lu\t%lu\t%lu\t%lu\nGid:\t%lu\t%lu\t%lu\t%lu\nFDSize:\t%lu\nGroups:\t",
                 Comm, Process->Umask, Process->Exited ? "Z (zombie)" : "S (sleeping)", LxPidNrIn(ProcNs, Process),
                 LxGlobalToNrIn(ProcNs, Synth->Tid ? Synth->Tid : Process->Pid),
                 LxPidNsIsInit(Process) ? 0UL : LxGlobalToNrIn(ProcNs, Process->ParentPid),
                 LxCredViewId(Self, FALSE, Process->Uid[0]), LxCredViewId(Self, FALSE, Process->Uid[1]),
                 LxCredViewId(Self, FALSE, Process->Uid[2]), LxCredViewId(Self, FALSE, Process->Fsuid),
                 LxCredViewId(Self, TRUE, Process->Gid[0]), LxCredViewId(Self, TRUE, Process->Gid[1]),
                 LxCredViewId(Self, TRUE, Process->Gid[2]), LxCredViewId(Self, TRUE, Process->Fsgid), max(Fds, 64UL));
    for (Index = 0; Index < Process->GroupCount; Index++)
        LxTextPrintf(Text, "%lu ", LxCredViewId(Self, TRUE, Process->Groups[Index]));
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
                     "VmPeak:\t%8llu kB\nVmSize:\t%8llu kB\nVmLck:\t       0 kB\nVmPin:\t       0 kB\n"
                     "VmHWM:\t%8llu kB\nVmRSS:\t%8llu kB\nRssAnon:\t       0 kB\nRssFile:\t       0 kB\n"
                     "RssShmem:\t       0 kB\nVmData:\t%8llu kB\nVmStk:\t       0 kB\nVmExe:\t%8llu kB\n"
                     "VmLib:\t       0 kB\nVmPTE:\t       0 kB\nVmSwap:\t       0 kB\nHugetlbPages:\t       0 kB\n",
                     Usage.PeakVirtualSize / 1024, Usage.VirtualSize / 1024, Usage.MaxRss / 1024, Usage.WorkingSet / 1024,
                     Usage.PrivateBytes / 1024, (Process->EndCode - Process->StartCode) / 1024);
    }
    else
    {
        LxTextAppend(Text, "Kthread:\t0\n", 10);
    }
    LxTextPrintf(Text,
                 "CoreDumping:\t0\nTHP_enabled:\t1\nuntag_mask:\t0xffffffffffffff\nThreads:\t%ld\nSigQ:\t0/63429\n",
                 Process->Exited ? 1L : (LONG)Process->ThreadCount);
    LxTextPrintf(Text,
                 "SigPnd:\t%016llx\nShdPnd:\t%016llx\nSigBlk:\t%016llx\nSigIgn:\t%016llx\nSigCgt:\t%016llx\n"
                 "CapInh:\t%016llx\nCapPrm:\t%016llx\nCapEff:\t%016llx\nCapBnd:\t%016llx\nCapAmb:\t%016llx\n"
                 "NoNewPrivs:\t%u\nSeccomp:\t0\nSeccomp_filters:\t0\nSpeculation_Store_Bypass:\tthread vulnerable\n"
                 "SpeculationIndirectBranch:\tunknown\nCpus_allowed:\t%llx\nCpus_allowed_list:\t%s\n"
                 "Mems_allowed:\t1\nMems_allowed_list:\t0\nvoluntary_ctxt_switches:\t0\nnonvoluntary_ctxt_switches:\t0\n",
                 0ULL, (ULONG64)Process->Pending, Blocked, Ignored, Caught, Process->CapInheritable,
                 Process->CapPermitted, Process->CapEffective,
                 Process->CapBounding, Process->CapAmbient, Process->NoNewPrivs ? 1U : 0U, Affinity, List);
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
LxGenPidMaps(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    PLX_PROCESS Process = LxProcessLookup(Self->Instance, Synth->Pid);
    PLIST_ENTRY Entry;

    if (Process == NULL)
        return;

    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
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
    }
    LxMmUnlock(Process);
    LxProcessDereference(Process);
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

static const LX_PID_ENTRY LxPidEntries[] =
{
    { "attr", LX_S_IFDIR, 0555, NULL, NULL, NULL },
    { "auxv", LX_S_IFREG, 0400, NULL, LxGenPidAuxv, NULL },
    { "cgroup", LX_S_IFREG, 0444, NULL, LxGenPidCgroup, NULL },
    { "children", LX_S_IFREG, 0444, NULL, LxGenTaskChildren, NULL },
    { "cmdline", LX_S_IFREG, 0444, NULL, LxGenPidCmdline, NULL },
    { "comm", LX_S_IFREG, 0644, NULL, LxGenPidComm, LxWritePidComm },
    { "coredump_filter", LX_S_IFREG, 0644, "00000033\n", NULL, LxWriteAccept },
    { "cwd", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "environ", LX_S_IFREG, 0400, NULL, LxGenPidEnviron, NULL },
    { "exe", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "fd", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "fdinfo", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "gid_map", LX_S_IFREG, 0644, NULL, LxGenPidGidMap, LxWritePidGidMap },
    { "io", LX_S_IFREG, 0400, "rchar: 0\nwchar: 0\nsyscr: 0\nsyscw: 0\nread_bytes: 0\nwrite_bytes: 0\ncancelled_write_bytes: 0\n", NULL, NULL },
    { "limits", LX_S_IFREG, 0444, NULL, LxGenPidLimits, NULL },
    { "loginuid", LX_S_IFREG, 0644, "4294967295", NULL, LxWriteAccept },
    { "map_files", LX_S_IFDIR, 0500, NULL, NULL, NULL },
    { "maps", LX_S_IFREG, 0444, NULL, LxGenPidMaps, NULL },
    { "mountinfo", LX_S_IFREG, 0444, NULL, LxGenPidMountinfo, NULL },
    { "mounts", LX_S_IFREG, 0444, NULL, LxGenPidMounts, NULL },
    { "mountstats", LX_S_IFREG, 0400, "", NULL, NULL },
    { "ns", LX_S_IFDIR, 0511, NULL, NULL, NULL },
    { "oom_adj", LX_S_IFREG, 0644, "0\n", NULL, LxWriteAccept },
    { "oom_score", LX_S_IFREG, 0444, "0\n", NULL, NULL },
    { "oom_score_adj", LX_S_IFREG, 0644, NULL, LxGenPidOomScoreAdj, LxWritePidOomScoreAdj },
    { "personality", LX_S_IFREG, 0400, "00000000\n", NULL, NULL },
    { "root", LX_S_IFLNK, 0777, NULL, NULL, NULL },
    { "sessionid", LX_S_IFREG, 0444, "4294967295", NULL, NULL },
    { "setgroups", LX_S_IFREG, 0644, NULL, LxGenPidSetgroups, LxWritePidSetgroups },
    { "smaps", LX_S_IFREG, 0444, NULL, LxGenPidMaps, NULL },
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
    { "kernel/core_pattern", 0644, "core\n", NULL, LxWriteAccept },
    { "kernel/core_pipe_limit", 0644, "0\n", NULL, LxWriteAccept },
    { "kernel/dmesg_restrict", 0644, "0\n", NULL, LxWriteAccept },
    { "kernel/domainname", 0644, NULL, LxGenDomainname, LxWriteDomainname },
    { "kernel/hostname", 0644, NULL, LxGenHostname, LxWriteHostname },
    { "kernel/kptr_restrict", 0644, "1\n", NULL, LxWriteAccept },
    { "kernel/ngroups_max", 0444, "65536\n", NULL, NULL },
    { "kernel/ns_last_pid", 0666, NULL, LxGenNsLastPid, LxWriteNsLastPid },
    { "kernel/osrelease", 0444, NULL, LxGenRelease, NULL },
    { "kernel/ostype", 0444, "Linux\n", NULL, NULL },
    { "kernel/overflowgid", 0644, NULL, LxGenOverflowGid, LxWriteOverflowGid },
    { "kernel/overflowuid", 0644, NULL, LxGenOverflowUid, LxWriteOverflowUid },
    { "kernel/panic", 0644, "0\n", NULL, LxWriteAccept },
    { "kernel/pid_max", 0644, "4194304\n", NULL, LxWriteAccept },
    { "kernel/printk", 0644, "4\t4\t1\t7\n", NULL, LxWriteAccept },
    { "kernel/random/boot_id", 0444, NULL, LxGenBootId, NULL },
    { "kernel/random/entropy_avail", 0444, "256\n", NULL, NULL },
    { "kernel/random/poolsize", 0444, "256\n", NULL, NULL },
    { "kernel/random/uuid", 0444, NULL, LxGenUuid, NULL },
    { "kernel/sysrq", 0644, "176\n", NULL, LxWriteAccept },
    { "kernel/threads-max", 0644, "63429\n", NULL, LxWriteAccept },
    { "kernel/unprivileged_bpf_disabled", 0644, "2\n", NULL, LxWriteAccept },
    { "kernel/version", 0444, LX_KERNEL_UTS_VERSION "\n", NULL, NULL },
    { "lxcore/trace", 0200, "", NULL, LxWriteTrace },
    { "net/core/somaxconn", 0644, "4096\n", NULL, LxWriteAccept },
    { "net/unix/max_dgram_qlen", 0644, "512\n", NULL, LxWriteAccept },
    { "user/max_user_namespaces", 0644, NULL, LxGenMaxUserNamespaces, LxWriteMaxUserNamespaces },
    { "vm/max_map_count", 0644, "65530\n", NULL, LxWriteAccept },
    { "vm/overcommit_memory", 0644, "0\n", NULL, LxWriteAccept },
    { "vm/swappiness", 0644, "60\n", NULL, LxWriteAccept },
};

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

static const LX_TABLE_ENTRY LxSysTable[] =
{
    { "block", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "bus", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "class/block", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
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
    { "devices/virtual", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "firmware", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/bpf", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/cgroup", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/fuse/connections", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "fs/pstore", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "hypervisor", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/config", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/debug", 0700 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/mm", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/security", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "kernel/tracing", 0700 | LX_S_IFDIR, NULL, NULL, NULL },
    { "module", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
    { "power", 0755 | LX_S_IFDIR, NULL, NULL, NULL },
};

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

static
VOID
LxGenMeminfo(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    SYSTEM_BASIC_INFORMATION Basic;
    ULONG64 Total = 0, Free;

    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(Synth);
    if (NT_SUCCESS(ZwQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL)))
        Total = (ULONG64)Basic.NumberOfPhysicalPages * Basic.PageSize / 1024;
    Free = Total / 2;
    LxTextPrintf(Text,
                 "MemTotal:       %8llu kB\nMemFree:        %8llu kB\nMemAvailable:   %8llu kB\n"
                 "Buffers:               0 kB\nCached:                0 kB\nSwapCached:            0 kB\n"
                 "Active:                0 kB\nInactive:              0 kB\nSwapTotal:             0 kB\n"
                 "SwapFree:              0 kB\nDirty:                 0 kB\nWriteback:             0 kB\n"
                 "AnonPages:             0 kB\nMapped:                0 kB\nShmem:                 0 kB\n"
                 "Slab:                  0 kB\nSReclaimable:          0 kB\nSUnreclaim:            0 kB\n"
                 "KernelStack:           0 kB\nPageTables:            0 kB\nCommitLimit:    %8llu kB\n"
                 "Committed_AS:          0 kB\nVmallocTotal:   34359738367 kB\nVmallocUsed:           0 kB\n"
                 "HugePages_Total:       0\nHugePages_Free:        0\nHugepagesize:       2048 kB\n",
                 Total, Free, Free, Total);
}

static
VOID
LxGenStat(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    ULONG Count = KeQueryActiveProcessorCount(NULL), Index, Processes = 0;
    LARGE_INTEGER Now;
    PLIST_ENTRY Entry;
    LONG64 Uptime = (LONG64)KeQueryInterruptTime() / 10000000LL;

    UNREFERENCED_PARAMETER(Synth);
    KeQuerySystemTime(&Now);
    LxTextPrintf(Text, "cpu  0 0 0 0 0 0 0 0 0 0\n");
    for (Index = 0; Index < Count; Index++)
        LxTextPrintf(Text, "cpu%lu 0 0 0 0 0 0 0 0 0 0\n", Index);
    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink)->Instance == Self->Instance)
            Processes++;
    }
    ExReleaseFastMutex(&LxProcessListLock);
    LxTextPrintf(Text, "intr 0\nctxt 0\nbtime %lld\nprocesses %lu\nprocs_running 1\nprocs_blocked 0\nsoftirq 0 0 0 0 0 0 0 0 0 0 0\n",
                 (Now.QuadPart - 116444736000000000LL) / 10000000LL - Uptime, Processes);
}

static
VOID
LxGenUptime(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    LONG64 Now = (LONG64)KeQueryInterruptTime() + LxTimeNsOffset(Self, LX_CLOCK_BOOTTIME) / 100;

    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "%lld.%02lld %lld.%02lld\n", Now / 10000000LL, (Now / 100000LL) % 100,
                 Now / 10000000LL, (Now / 100000LL) % 100);
}

static
VOID
LxGenLoadavg(
    _In_ PLX_PROCESS Self,
    _In_ PLX_SYNTH Synth,
    _Inout_ PLX_TEXT Text)
{
    UNREFERENCED_PARAMETER(Synth);
    LxTextPrintf(Text, "0.00 0.00 0.00 1/%lu %lu\n", Self->Instance->ProcessCount, Self->Instance->NextPid - 1);
}

static const LX_TABLE_ENTRY LxProcStaticTable[] =
{
    { "cmdline", 0444, "init=/sbin/init\n", NULL, NULL },
    { "config.gz", 0444, NULL, LxGenKernelConfig, NULL },
    { "consoles", 0444, "console              -W- (EC p a)    5:1\n", NULL, NULL },
    { "cpuinfo", 0444, NULL, LxGenCpuinfo, NULL },
    { "devices", 0444, "Character devices:\n  1 mem\n  4 /dev/vc/0\n  4 tty\n  5 /dev/tty\n  5 /dev/console\n  5 /dev/ptmx\n136 pts\n\nBlock devices:\n", NULL, NULL },
    { "filesystems", 0444, "nodev\tsysfs\nnodev\ttmpfs\nnodev\tbdev\nnodev\tproc\nnodev\tcgroup2\nnodev\tdevtmpfs\nnodev\tdevpts\nnodev\tmqueue\nnodev\tpipefs\nnodev\tsockfs\n\tlxfs\n", NULL, NULL },
    { "loadavg", 0444, NULL, LxGenLoadavg, NULL },
    { "meminfo", 0444, NULL, LxGenMeminfo, NULL },
    { "partitions", 0444, "major minor  #blocks  name\n\n", NULL, NULL },
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
        Synth->LinkTarget = LxPathVisible(Self, File->Path);
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
            Synth->LinkTarget = LxPathVisible(Self, File->Path);
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

static
LONG
LxResolveSys(
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
    if (Consumed == Count)
    {
        Synth->Id = LxCgDir;
        Synth->Type = LX_S_IFDIR;
        Synth->Mode = 0755;
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
        Synth->Mode = 0666;
        return 0;
    }
    if (LxParseNumber(Components[0], &Number))
    {
        PLX_TTY Tty = LxTtyLookup(Self->Instance, Number);

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
            Error = LxResolveSys(Components, Count, Synth);
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
        Path = LxPathVisible(Process, File->Path);
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

    if ((Node->Mount == NULL) || (Node->Mount->Kind == LxFsNt) || (Node->Type != LX_S_IFLNK) || (Node->Relative == NULL))
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
        Path = LxPathVisible(Process, File->Path);
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
                Count = LxTtyList(Process->Instance, Indexes, RTL_NUMBER_OF(Indexes));
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
        if (File == NULL)
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
    else if ((Synth.Id == LxNsfsFile) || (Synth.Id == LxProcNsLink))
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
        PLX_TTY Tty = LxTtyLookup(Process->Instance, Synth.Index);

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
        Error = LxPtmxOpen(Process, Flags, &File);
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
                }
                else
                {
                    LxTextAppend(&Text, LxSysTable[Synth.Index].Text, (ULONG)strlen(LxSysTable[Synth.Index].Text));
                }
                break;

            case LxProcPidFile:
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
        PLX_TTY Tty = LxTtyLookup(Process->Instance, Synth.Index);

        if (Tty == NULL)
            return -LX_ENOENT;
        Stat->Rdev = LxTtyDevice(Tty);
        Stat->Uid = Tty->Uid;
        Stat->Gid = Tty->Gid;
        LxTtyDereference(Tty);
    }
    else if (Synth.Id == LxPtsPtmx)
    {
        Stat->Rdev = (5 << 8) | 2;
        Stat->Gid = 5;
    }
    if (Synth.Type == LX_S_IFLNK && Synth.LinkTarget != NULL)
        Stat->Size = (LONG64)strlen(Synth.LinkTarget);

    if ((Synth.Pid != 0) && (Synth.Id != LxProcRoot))
    {
        PLX_PROCESS Target = LxProcessLookup(Process->Instance, Synth.Pid);

        if (Target != NULL)
        {
            Stat->Uid = Target->Uid[1];
            Stat->Gid = Target->Gid[1];
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
