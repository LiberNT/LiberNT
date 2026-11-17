/*
 * PROJECT:     LiberNT Multimedia Class Scheduler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raises the threads avrt registers for a multimedia task
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <ntifs.h>
#include <ndk/psfuncs.h>
#include <drivers/mmcssio.h>

#define NDEBUG
#include <debug.h>

#define MMCSS_TAG 'scmM'
#define MMCSS_MAX_TASKS 32

#define MMCSS_HIGH_PRIORITY 24
#define MMCSS_HIGH_FLOOR 23
#define MMCSS_MEDIUM_BASE 15
#define MMCSS_MEDIUM_FLOOR 16
#define MMCSS_LOW_BASE 8
#define MMCSS_LOW_FLOOR 1
#define MMCSS_AFFINITY_UNUSED 0xFFFFFFFF
#define MMCSS_EXHAUSTED_PRIORITY 1
#define MMCSS_WORKER_PRIORITY 27
#define MMCSS_PERIOD (10 * 10000)
#define MMCSS_IDLE_CHECK (10 * MMCSS_PERIOD)

typedef struct _MMCSS_TASK
{
    WCHAR Name[MMCSS_MAX_TASK_NAME];
    KPRIORITY Priority;
    KPRIORITY Floor;
    KAFFINITY Affinity;
    BOOLEAN Raised;
} MMCSS_TASK, *PMMCSS_TASK;

typedef struct _MMCSS_CLIENT
{
    LIST_ENTRY Link;
    PETHREAD Thread;
    HANDLE Handle;
    PMMCSS_TASK Task;
    ULONG TaskIndex;
    LONG SavedIncrement;
    KAFFINITY SavedAffinity;
    KPRIORITY Priority;
    KPRIORITY Floor;
    KPRIORITY Target;
    ULONG64 PeriodCycles;
    ULONG64 PeriodStamp;
    ULONGLONG PeriodTime;
    ULONG64 WindowCycles;
    ULONG64 WindowStamp;
    ULONGLONG WindowTime;
    ULONGLONG WindowUsed;
    ULONGLONG WindowTotal;
    ULONGLONG ExhaustedUntil;
    BOOLEAN Registered;
    BOOLEAN PrioritySaved;
    BOOLEAN CleanedUp;
    BOOLEAN Exhausted;
} MMCSS_CLIENT, *PMMCSS_CLIENT;

static MMCSS_TASK MmcssTasks[MMCSS_MAX_TASKS];
static ULONG MmcssTaskCount;
static ULONG MmcssResponsiveness = 20;
static BOOLEAN MmcssDisabled;
static LIST_ENTRY MmcssClients;
static KEVENT MmcssLock;
static PKTHREAD MmcssLockOwner;
static volatile LONG MmcssNextIndex;
static volatile LONG MmcssProfileState;
static KEVENT MmcssProfileEvent;
static KEVENT MmcssWakeEvent;
static PEX_TIMER MmcssTimer;
static ULONGLONG MmcssTimerDue;
static ULONGLONG MmcssWindowTime;
static BOOLEAN MmcssWindowRestart = TRUE;

#define MMCSS_PROFILE_NONE 0
#define MMCSS_PROFILE_LOADING 1
#define MMCSS_PROFILE_LOADED 2

static DRIVER_DISPATCH MmcssCreate;
static DRIVER_DISPATCH MmcssCleanup;
static DRIVER_DISPATCH MmcssClose;
static DRIVER_DISPATCH MmcssDeviceControl;

static
VOID
MmcssAcquire(VOID)
{
    PAGED_CODE();

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmcssLock, Executive, KernelMode, FALSE, NULL);
    ASSERT(MmcssLockOwner == NULL);
    MmcssLockOwner = KeGetCurrentThread();
}

static
VOID
MmcssRelease(VOID)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    MmcssLockOwner = NULL;
    KeSetEvent(&MmcssLock, IO_NO_INCREMENT, FALSE);
    KeLeaveCriticalRegion();
}

static
NTSTATUS
MmcssQueryValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_bytes_(Size) PKEY_VALUE_PARTIAL_INFORMATION Info,
    _In_ ULONG Size)
{
    UNICODE_STRING ValueName;
    ULONG Length;

    PAGED_CODE();

    RtlInitUnicodeString(&ValueName, Name);
    return ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Info, Size, &Length);
}

static
NTSTATUS
MmcssQueryDword(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    union
    {
        KEY_VALUE_PARTIAL_INFORMATION Info;
        UCHAR Bytes[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    } Buffer;
    PKEY_VALUE_PARTIAL_INFORMATION Info = &Buffer.Info;
    NTSTATUS Status;

    PAGED_CODE();

    Status = MmcssQueryValue(Key, Name, Info, sizeof(Buffer));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;

    RtlCopyMemory(Value, Info->Data, sizeof(ULONG));
    return STATUS_SUCCESS;
}

static
NTSTATUS
MmcssQueryCategory(
    _In_ HANDLE Key,
    _Out_ PUNICODE_STRING Category,
    _Out_writes_bytes_(Size) PKEY_VALUE_PARTIAL_INFORMATION Info,
    _In_ ULONG Size)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = MmcssQueryValue(Key, L"Scheduling Category", Info, Size - sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Info->Type != REG_SZ)
        return STATUS_OBJECT_TYPE_MISMATCH;

    ((PWCHAR)Info->Data)[Info->DataLength / sizeof(WCHAR)] = UNICODE_NULL;
    RtlInitUnicodeString(Category, (PCWSTR)Info->Data);
    return STATUS_SUCCESS;
}

static
VOID
MmcssLoadTask(
    _In_ HANDLE TasksKey,
    _In_ PKEY_BASIC_INFORMATION KeyInfo)
{
    static const UNICODE_STRING High = RTL_CONSTANT_STRING(L"High");
    static const UNICODE_STRING Medium = RTL_CONSTANT_STRING(L"Medium");
    static const UNICODE_STRING Low = RTL_CONSTANT_STRING(L"Low");
    union
    {
        KEY_VALUE_PARTIAL_INFORMATION Info;
        UCHAR Bytes[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 32 * sizeof(WCHAR)];
    } Buffer;
    PKEY_VALUE_PARTIAL_INFORMATION Info = &Buffer.Info;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name, Category;
    PMMCSS_TASK Task;
    ULONG Priority, Affinity;
    NTSTATUS Status;
    HANDLE Key;

    PAGED_CODE();

    if (MmcssTaskCount >= MMCSS_MAX_TASKS ||
        KeyInfo->NameLength >= MMCSS_MAX_TASK_NAME * sizeof(WCHAR))
    {
        return;
    }

    Name.Buffer = KeyInfo->Name;
    Name.Length = Name.MaximumLength = (USHORT)KeyInfo->NameLength;
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, TasksKey, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
        return;

    Status = MmcssQueryDword(Key, L"Priority", &Priority);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Priority = 1;
        Status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status) && (Priority < 1 || Priority > 8))
        Status = STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(MmcssQueryDword(Key, L"Affinity", &Affinity)) || Affinity == MMCSS_AFFINITY_UNUSED)
        Affinity = 0;

    Task = &MmcssTasks[MmcssTaskCount];
    Task->Affinity = Affinity;
    Task->Raised = TRUE;
    if (NT_SUCCESS(Status))
    {
        Status = MmcssQueryCategory(Key, &Category, Info, sizeof(Buffer));
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Category = Low;
            Status = STATUS_SUCCESS;
        }
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return;

    if (RtlEqualUnicodeString(&Category, &High, TRUE))
    {
        Task->Priority = MMCSS_HIGH_PRIORITY;
        Task->Floor = MMCSS_HIGH_FLOOR;
    }
    else if (RtlEqualUnicodeString(&Category, &Medium, TRUE))
    {
        Task->Priority = MMCSS_MEDIUM_BASE + (KPRIORITY)Priority;
        Task->Floor = MMCSS_MEDIUM_FLOOR;
    }
    else if (RtlEqualUnicodeString(&Category, &Low, TRUE))
    {
        Task->Priority = MMCSS_LOW_BASE;
        Task->Floor = MMCSS_LOW_FLOOR;
        Task->Raised = FALSE;
    }
    else
    {
        return;
    }

    RtlCopyMemory(Task->Name, KeyInfo->Name, KeyInfo->NameLength);
    Task->Name[KeyInfo->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
    MmcssTaskCount++;
    ASSERT(MmcssTaskCount <= MMCSS_MAX_TASKS);
}

static
BOOLEAN
MmcssLoadProfile(VOID)
{
    UNICODE_STRING ProfileName = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile");
    UNICODE_STRING TasksName = RTL_CONSTANT_STRING(L"Tasks");
    union
    {
        KEY_BASIC_INFORMATION Info;
        UCHAR Bytes[FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + MMCSS_MAX_TASK_NAME * sizeof(WCHAR)];
    } Buffer;
    PKEY_BASIC_INFORMATION KeyInfo = &Buffer.Info;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ProfileKey, TasksKey;
    ULONG Index, Length, Value;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes, &ProfileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&ProfileKey, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MMCSS: no SystemProfile key: 0x%lx\n", Status);
        return FALSE;
    }

    if (NT_SUCCESS(MmcssQueryDword(ProfileKey, L"SystemResponsiveness", &Value)))
    {
        if (Value == 100)
            MmcssDisabled = TRUE;
        else if (Value >= 10 && Value < 100)
            MmcssResponsiveness = Value - (Value % 10);
    }

    InitializeObjectAttributes(&ObjectAttributes, &TasksName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, ProfileKey, NULL);
    Status = ZwOpenKey(&TasksKey, KEY_READ, &ObjectAttributes);
    ZwClose(ProfileKey);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MMCSS: no Tasks key: 0x%lx\n", Status);
        return TRUE;
    }

    for (Index = 0; ; Index++)
    {
        Status = ZwEnumerateKey(TasksKey, Index, KeyBasicInformation, KeyInfo, sizeof(Buffer), &Length);
        if (NT_SUCCESS(Status))
            MmcssLoadTask(TasksKey, KeyInfo);
        else if (Status != STATUS_BUFFER_OVERFLOW)
            break;
    }
    ZwClose(TasksKey);
    return TRUE;
}

/* The SOFTWARE hive is mounted after system-start drivers, so the profile is read at first use, once */
static
VOID
MmcssEnsureProfile(VOID)
{
    PAGED_CODE();

    for (;;)
    {
        switch (InterlockedCompareExchange(&MmcssProfileState, MMCSS_PROFILE_LOADING, MMCSS_PROFILE_NONE))
        {
            case MMCSS_PROFILE_LOADED:
                return;

            case MMCSS_PROFILE_LOADING:
                KeWaitForSingleObject(&MmcssProfileEvent, Executive, KernelMode, FALSE, NULL);
                continue;

            default:
                KeClearEvent(&MmcssProfileEvent);
                ASSERT(MmcssTaskCount == 0);
                if (MmcssLoadProfile())
                {
                    DPRINT1("MMCSS: %lu tasks, responsiveness %lu%s\n",
                            MmcssTaskCount, MmcssResponsiveness, MmcssDisabled ? " (disabled)" : "");
                    InterlockedExchange(&MmcssProfileState, MMCSS_PROFILE_LOADED);
                }
                else
                {
                    InterlockedExchange(&MmcssProfileState, MMCSS_PROFILE_NONE);
                }
                KeSetEvent(&MmcssProfileEvent, IO_NO_INCREMENT, FALSE);
                return;
        }
    }
}

static
PMMCSS_TASK
MmcssFindTask(
    _In_reads_(MMCSS_MAX_TASK_NAME) PCWSTR Name)
{
    UNICODE_STRING Wanted, Known;
    ULONG Index;

    PAGED_CODE();

    RtlInitUnicodeString(&Wanted, Name);
    for (Index = 0; Index < MmcssTaskCount; Index++)
    {
        RtlInitUnicodeString(&Known, MmcssTasks[Index].Name);
        if (RtlEqualUnicodeString(&Wanted, &Known, TRUE))
            return &MmcssTasks[Index];
    }
    return NULL;
}

static
BOOLEAN
MmcssThreadRegistered(
    _In_ PETHREAD Thread)
{
    PLIST_ENTRY Entry;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());

    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link)->Thread == Thread)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
MmcssTaskIndexLive(
    _In_ ULONG TaskIndex,
    _In_ PMMCSS_TASK Task)
{
    PMMCSS_CLIENT Client;
    PLIST_ENTRY Entry;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());

    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link);
        if (Client->TaskIndex == TaskIndex && Client->Task == Task)
            return TRUE;
    }
    return FALSE;
}

static
NTSTATUS
MmcssSetAffinity(
    _In_ HANDLE Handle,
    _In_ KAFFINITY Affinity)
{
    PAGED_CODE();
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Handle != NULL);
    ASSERT(Affinity != 0);

    return ZwSetInformationThread(Handle, ThreadAffinityMask, &Affinity, sizeof(Affinity));
}

static
KAFFINITY
MmcssThreadAffinity(
    _In_ PMMCSS_CLIENT Client)
{
    THREAD_BASIC_INFORMATION ThreadInfo;

    PAGED_CODE();
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);
    ASSERT(Client->Handle != NULL);

    if (!NT_SUCCESS(ZwQueryInformationThread(Client->Handle, ThreadBasicInformation, &ThreadInfo, sizeof(ThreadInfo), NULL)))
        return KeQueryActiveProcessors();
    ASSERT(ThreadInfo.AffinityMask != 0);
    return ThreadInfo.AffinityMask;
}

static
VOID
MmcssRevertClient(
    _Inout_ PMMCSS_CLIENT Client)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);
    ASSERT(Client->Thread != NULL);
    ASSERT(Client->Handle != NULL);

    if (!PsIsThreadTerminating(Client->Thread))
    {
        if (Client->PrioritySaved)
            KeSetBasePriorityThread((PKTHREAD)Client->Thread, Client->SavedIncrement);
        MmcssSetAffinity(Client->Handle, Client->SavedAffinity);
    }

    RemoveEntryList(&Client->Link);
    ZwClose(Client->Handle);
    ObDereferenceObject(Client->Thread);
    Client->Handle = NULL;
    Client->Thread = NULL;
    Client->Task = NULL;
    Client->TaskIndex = 0;
    Client->Registered = FALSE;
    Client->PrioritySaved = FALSE;
    Client->Exhausted = FALSE;
}

static
VOID
MmcssMoveClient(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ KPRIORITY Priority)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);
    ASSERT(!Client->Exhausted);
    ASSERT(Priority >= MMCSS_LOW_FLOOR && Priority <= HIGH_PRIORITY);

    Client->Target = Priority;
    if (PsIsThreadTerminating(Client->Thread))
        return;
    if (!Client->PrioritySaved)
    {
        Client->SavedIncrement = KeSetBasePriorityThread((PKTHREAD)Client->Thread, 0);
        Client->PrioritySaved = TRUE;
    }
    KeSetPriorityThread((PKTHREAD)Client->Thread, Priority);
}

static
VOID
MmcssStartPeriod(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ ULONGLONG Now)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);

    Client->PeriodCycles = KeQueryTotalCycleTimeThread((PKTHREAD)Client->Thread, &Client->PeriodStamp);
    Client->PeriodTime = Now;
}

static
VOID
MmcssOpenSegment(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ ULONGLONG Now)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);
    ASSERT(!Client->Exhausted);

    Client->WindowCycles = KeQueryTotalCycleTimeThread((PKTHREAD)Client->Thread, &Client->WindowStamp);
    Client->WindowTime = Now;
}

static
ULONGLONG
MmcssSegmentUsed(
    _In_ PMMCSS_CLIENT Client,
    _In_ ULONGLONG Now)
{
    ULONG64 Cycles, Stamp;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);

    if (Client->Exhausted)
        return 0;
    ASSERT(Now >= Client->WindowTime);
    Cycles = KeQueryTotalCycleTimeThread((PKTHREAD)Client->Thread, &Stamp);
    ASSERT(Cycles >= Client->WindowCycles);
    if (Stamp <= Client->WindowStamp)
        return 0;
    return min((Cycles - Client->WindowCycles) * (Now - Client->WindowTime) / (Stamp - Client->WindowStamp),
               Now - Client->WindowTime);
}

static
VOID
MmcssLowerClient(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ ULONGLONG Now,
    _In_ ULONGLONG Until)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);
    ASSERT(!Client->Exhausted);
    ASSERT(Until >= Now);

    Client->WindowUsed += MmcssSegmentUsed(Client, Now);
    KeSetPriorityThread((PKTHREAD)Client->Thread, MMCSS_EXHAUSTED_PRIORITY);
    Client->Exhausted = TRUE;
    Client->ExhaustedUntil = Until;
}

static
VOID
MmcssRestartWindow(
    _In_ ULONGLONG Now)
{
    PMMCSS_CLIENT Client;
    PLIST_ENTRY Entry;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());

    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link);
        ASSERT(Client->Registered);
        Client->WindowUsed = 0;
        if (!Client->Exhausted)
            MmcssOpenSegment(Client, Now);
    }
    MmcssWindowTime = Now;
    MmcssWindowRestart = FALSE;
}

static
BOOLEAN
MmcssSharesWindow(
    _In_ PMMCSS_CLIENT Client)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Client->Registered);

    return !PsIsThreadTerminating(Client->Thread) && Client->Target >= LOW_REALTIME_PRIORITY;
}

static
VOID
MmcssArmTimer(
    _In_ ULONGLONG Due,
    _In_ ULONGLONG Now)
{
    ASSERT(MmcssLockOwner == KeGetCurrentThread());

    if (MmcssTimerDue != 0 && MmcssTimerDue <= Due)
        return;

    MmcssTimerDue = Due;
    ExSetTimer(MmcssTimer, -(LONGLONG)max(Due - Now, 1), 0, NULL);
}

/* Registered threads that together used the share of the processors they run on give it back together; with no more of them busy than processors, each gives back its own */
static
VOID
MmcssScheduleShared(
    _In_ ULONGLONG Now,
    _In_ ULONGLONG Quota,
    _Inout_ PULONGLONG Next)
{
    ULONGLONG Window, Total = 0, Rate, Limit, Until;
    KAFFINITY Mask = 0;
    ULONG Sharing = 0, Busy = 0, Processors;
    PMMCSS_CLIENT Client;
    PLIST_ENTRY Entry;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());
    ASSERT(Quota != 0 && Quota < MMCSS_PERIOD);

    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        if (MmcssSharesWindow(CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link)))
            Sharing++;
    }
    if (Sharing < 2)
    {
        MmcssWindowRestart = TRUE;
        return;
    }
    if (Now < MmcssWindowTime)
    {
        *Next = min(*Next, MmcssWindowTime);
        return;
    }
    if (MmcssWindowRestart || Now - MmcssWindowTime > 2 * MMCSS_IDLE_CHECK)
    {
        MmcssRestartWindow(Now);
        *Next = min(*Next, Now + Quota);
        return;
    }

    Window = Now - MmcssWindowTime;
    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link);
        Client->WindowTotal = 0;
        if (!MmcssSharesWindow(Client))
            continue;
        ASSERT(Client->Exhausted || Client->WindowTime >= MmcssWindowTime);
        Client->WindowTotal = Client->WindowUsed + MmcssSegmentUsed(Client, Now);
        if (Client->WindowTotal != 0)
        {
            Mask |= MmcssThreadAffinity(Client);
            Total += Client->WindowTotal;
            Busy++;
        }
    }

    Processors = RtlNumberOfSetBitsUlongPtr(Mask & KeQueryActiveProcessors());
    if (Processors == 0)
    {
        if (Window >= MMCSS_PERIOD)
        {
            MmcssRestartWindow(Now);
            *Next = min(*Next, Now + MMCSS_IDLE_CHECK);
        }
        else
        {
            *Next = min(*Next, MmcssWindowTime + MMCSS_PERIOD);
        }
        return;
    }

    Rate = (Window > MMCSS_PERIOD) ? Total * MMCSS_PERIOD / Window : Total;
    Limit = Quota * Processors;
    if (Busy > Processors && Rate >= Limit)
    {
        Until = Now + (Rate / Processors) * MmcssResponsiveness / (100 - MmcssResponsiveness);
        for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
        {
            Client = CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link);
            if (!MmcssSharesWindow(Client))
                continue;
            if (Client->Exhausted)
                Client->ExhaustedUntil = max(Client->ExhaustedUntil, Until);
            else if (KeQueryPriorityThread((PKTHREAD)Client->Thread) == Client->Target)
                MmcssLowerClient(Client, Now, Until);
        }
        MmcssWindowTime = Until;
        MmcssWindowRestart = TRUE;
        *Next = min(*Next, Until);
        return;
    }

    if (Window >= MMCSS_PERIOD)
    {
        MmcssRestartWindow(Now);
        *Next = min(*Next, Now + ((Rate < Limit / 2) ? MMCSS_IDLE_CHECK : Quota));
    }
    else if (Total >= Limit)
    {
        *Next = min(*Next, MmcssWindowTime + MMCSS_PERIOD);
    }
    else
    {
        *Next = min(*Next, min(Now + max((Limit - Total) / Processors, 1), MmcssWindowTime + MMCSS_PERIOD));
    }
}

/* Threads that used their share of a period run below the Low category for the rest of it (MSDN, Multimedia Class Scheduler Service) */
static
VOID
MmcssSchedule(VOID)
{
    ULONGLONG Now, Next, Quota, Used, Elapsed;
    ULONG64 Cycles, Stamp;
    PMMCSS_CLIENT Client;
    PLIST_ENTRY Entry;

    ASSERT(MmcssLockOwner == KeGetCurrentThread());

    MmcssTimerDue = 0;
    if (IsListEmpty(&MmcssClients))
    {
        ExCancelTimer(MmcssTimer, NULL);
        return;
    }

    ASSERT(MmcssResponsiveness >= 10 && MmcssResponsiveness < 100);
    Quota = MMCSS_PERIOD * (100 - MmcssResponsiveness) / 100;
    Now = KeQueryInterruptTimePrecise(&Stamp);
    Next = MAXULONGLONG;

    for (Entry = MmcssClients.Flink; Entry != &MmcssClients; Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, MMCSS_CLIENT, Link);
        ASSERT(Client->Registered);
        if (PsIsThreadTerminating(Client->Thread))
            continue;

        /* TODO: whether a Low category thread is lowered once it used its quota is not known; it is left alone */
        if (Client->Target < LOW_REALTIME_PRIORITY)
        {
            ASSERT(!Client->Exhausted);
            continue;
        }
        ASSERT(Client->PrioritySaved);

        if (Client->Exhausted)
        {
            if (Now < Client->ExhaustedUntil)
            {
                Next = min(Next, Client->ExhaustedUntil);
                continue;
            }
            KeSetPriorityThread((PKTHREAD)Client->Thread, Client->Target);
            Client->Exhausted = FALSE;
            MmcssStartPeriod(Client, Now);
            MmcssOpenSegment(Client, Now);
        }

        ASSERT(Now >= Client->PeriodTime);
        Elapsed = Now - Client->PeriodTime;
        if (Elapsed > 2 * MMCSS_IDLE_CHECK)
        {
            MmcssStartPeriod(Client, Now);
            Next = min(Next, Now + Quota);
            continue;
        }

        Cycles = KeQueryTotalCycleTimeThread((PKTHREAD)Client->Thread, &Stamp);
        ASSERT(Cycles >= Client->PeriodCycles);
        Used = 0;
        if (Stamp > Client->PeriodStamp)
            Used = min((Cycles - Client->PeriodCycles) * Elapsed / (Stamp - Client->PeriodStamp), Elapsed);
        if (Elapsed > MMCSS_PERIOD)
            Used = Used * MMCSS_PERIOD / Elapsed;
        ASSERT(Used <= MMCSS_PERIOD);

        /* A late check still gives back the share of what was used; a thread whose priority was changed since is left alone */
        if (Used >= Quota && KeQueryPriorityThread((PKTHREAD)Client->Thread) == Client->Target)
        {
            MmcssLowerClient(Client, Now, Now + Used * MmcssResponsiveness / (100 - MmcssResponsiveness));
            Next = min(Next, Client->ExhaustedUntil);
            continue;
        }

        if (Elapsed >= MMCSS_PERIOD)
        {
            MmcssStartPeriod(Client, Now);
            Next = min(Next, Now + ((Used < Quota / 2) ? MMCSS_IDLE_CHECK : Quota));
        }
        else if (Used >= Quota)
        {
            Next = min(Next, Client->PeriodTime + MMCSS_PERIOD);
        }
        else
        {
            Next = min(Next, min(Now + (Quota - Used), Client->PeriodTime + MMCSS_PERIOD));
        }
    }

    MmcssScheduleShared(Now, Quota, &Next);
    if (Next == MAXULONGLONG)
    {
        ExCancelTimer(MmcssTimer, NULL);
        return;
    }
    ASSERT(Next >= Now);
    MmcssArmTimer(Next, Now);
}

static
VOID
NTAPI
MmcssTimerCallback(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(Context);

    KeSetEvent(&MmcssWakeEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
MmcssWorker(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    PAGED_CODE();

    KeSetPriorityThread(KeGetCurrentThread(), MMCSS_WORKER_PRIORITY);
    for (;;)
    {
        KeWaitForSingleObject(&MmcssWakeEvent, Executive, KernelMode, FALSE, NULL);
        MmcssAcquire();
        MmcssSchedule();
        MmcssRelease();
    }
}

static
NTSTATUS
MmcssRegister(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ PETHREAD Thread,
    _Inout_ PMMCSS_REGISTER_INPUT Input,
    _Out_ PMMCSS_REGISTER_OUTPUT Output)
{
    PMMCSS_TASK First, Second = NULL;
    THREAD_BASIC_INFORMATION ThreadInfo;
    PROCESS_BASIC_INFORMATION ProcessInfo;
    KAFFINITY Affinity;
    HANDLE Handle;
    ULONGLONG Now;
    ULONG64 Qpc;
    NTSTATUS Status;

    PAGED_CODE();

    MmcssEnsureProfile();
    Input->FirstTask[MMCSS_MAX_TASK_NAME - 1] = UNICODE_NULL;
    Input->SecondTask[MMCSS_MAX_TASK_NAME - 1] = UNICODE_NULL;

    First = MmcssFindTask(Input->FirstTask);
    if (Input->SecondTask[0] != UNICODE_NULL)
        Second = MmcssFindTask(Input->SecondTask);
    if (First == NULL || (Input->SecondTask[0] != UNICODE_NULL && Second == NULL))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if (MmcssDisabled)
        return STATUS_SERVER_DISABLED;
    if (Second != NULL && Second->Priority > First->Priority)
        First = Second;

    ASSERT(Thread == PsGetCurrentThread());
    Status = ZwQueryInformationThread(NtCurrentThread(), ThreadBasicInformation, &ThreadInfo, sizeof(ThreadInfo), NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    MmcssAcquire();
    if (Client->CleanedUp)
    {
        Status = STATUS_FILE_CLOSED;
    }
    else if (Client->Registered)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else if (Input->TaskIndex != 0 && !MmcssTaskIndexLive(Input->TaskIndex, First))
    {
        Status = STATUS_INVALID_PARAMETER_1;
    }
    else if (MmcssThreadRegistered(Thread))
    {
        Status = STATUS_ALREADY_REGISTERED;
    }
    else
    {
        Affinity = ProcessInfo.AffinityMask;
        if (First->Affinity != 0 && (First->Affinity & ~ProcessInfo.AffinityMask) == 0)
            Affinity = First->Affinity;
        Status = ObOpenObjectByPointer(Thread, OBJ_KERNEL_HANDLE, NULL, THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION,
                                       *PsThreadType, KernelMode, &Handle);
        if (NT_SUCCESS(Status))
        {
            Status = MmcssSetAffinity(Handle, Affinity);
            if (!NT_SUCCESS(Status))
                ZwClose(Handle);
        }
        if (NT_SUCCESS(Status))
        {
            ObReferenceObject(Thread);
            Client->SavedAffinity = ThreadInfo.AffinityMask;
            Client->Handle = Handle;
            Client->Thread = Thread;
            Client->Task = First;
            Client->TaskIndex = (Input->TaskIndex != 0) ? Input->TaskIndex
                                                        : (ULONG)InterlockedIncrement(&MmcssNextIndex);
            ASSERT(Client->TaskIndex != 0);
            Client->Priority = First->Priority;
            Client->Floor = First->Floor;
            Client->Target = 0;
            Client->Exhausted = FALSE;
            ASSERT(!Client->PrioritySaved);
            InsertTailList(&MmcssClients, &Client->Link);
            Client->Registered = TRUE;
            if (First->Raised)
                MmcssMoveClient(Client, Client->Priority);
            Now = KeQueryInterruptTimePrecise(&Qpc);
            MmcssStartPeriod(Client, Now);
            MmcssOpenSegment(Client, Now);
            Client->WindowUsed = 0;
            MmcssWindowRestart = TRUE;
            MmcssArmTimer(Now + MMCSS_PERIOD * (100 - MmcssResponsiveness) / 100, Now);
            Output->TaskIndex = Client->TaskIndex;
        }
    }
    MmcssRelease();

    return Status;
}

static
NTSTATUS
MmcssSetPriority(
    _Inout_ PMMCSS_CLIENT Client,
    _In_ LONG Level)
{
    KPRIORITY Priority;
    NTSTATUS Status;

    PAGED_CODE();

    MmcssAcquire();
    if (!Client->Registered)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else if (Level < -2 || Level > 2)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Priority = max(Client->Priority + Level, Client->Floor);
        if (Client->Exhausted)
            Client->Target = Priority;
        else
            MmcssMoveClient(Client, Priority);
        Status = STATUS_SUCCESS;
    }
    MmcssRelease();

    return Status;
}

static
NTSTATUS
NTAPI
MmcssCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMMCSS_CLIENT Client;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();

    Client = ExAllocatePoolQuotaZero(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, sizeof(*Client), MMCSS_TAG);
    if (Client == NULL)
        Status = STATUS_INSUFFICIENT_RESOURCES;
    else
        Stack->FileObject->FsContext = Client;

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
MmcssCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMMCSS_CLIENT Client = Stack->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();
    ASSERT(Client != NULL);

    MmcssAcquire();
    ASSERT(!Client->CleanedUp);
    if (Client->Registered)
        MmcssRevertClient(Client);
    Client->CleanedUp = TRUE;
    MmcssRelease();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MmcssClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMMCSS_CLIENT Client = Stack->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();
    ASSERT(Client != NULL);
    ASSERT(!Client->Registered);

    Stack->FileObject->FsContext = NULL;
    ExFreePoolWithTag(Client, MMCSS_TAG);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MmcssDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMMCSS_CLIENT Client = Stack->FileObject->FsContext;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    PETHREAD Thread = Irp->Tail.Overlay.Thread;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();
    ASSERT(Client != NULL);
    ASSERT(Thread == PsGetCurrentThread());

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_MMCSS_REGISTER:
            if (InputLength < sizeof(MMCSS_REGISTER_INPUT) || OutputLength < sizeof(MMCSS_REGISTER_OUTPUT))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            Status = MmcssRegister(Client, Thread, Buffer, Buffer);
            if (NT_SUCCESS(Status))
                Information = sizeof(MMCSS_REGISTER_OUTPUT);
            break;

        case IOCTL_MMCSS_SET_PRIORITY:
            if (InputLength < sizeof(MMCSS_SET_PRIORITY_INPUT))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            Status = MmcssSetPriority(Client, ((PMMCSS_SET_PRIORITY_INPUT)Buffer)->Priority);
            break;

        case IOCTL_MMCSS_QUERY_RESPONSIVENESS:
            if (OutputLength < sizeof(MMCSS_RESPONSIVENESS_OUTPUT))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (!Client->Registered)
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }
            ((PMMCSS_RESPONSIVENESS_OUTPUT)Buffer)->SystemResponsiveness = MmcssResponsiveness;
            Information = sizeof(MMCSS_RESPONSIVENESS_OUTPUT);
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(MMCSS_DEVICE_NAME);
    PDEVICE_OBJECT DeviceObject;
    HANDLE Worker;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    PAGED_CODE();

    InitializeListHead(&MmcssClients);
    KeInitializeEvent(&MmcssLock, SynchronizationEvent, TRUE);
    KeInitializeEvent(&MmcssProfileEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&MmcssWakeEvent, SynchronizationEvent, FALSE);

    MmcssTimer = ExAllocateTimer(MmcssTimerCallback, NULL, EX_TIMER_HIGH_RESOLUTION);
    if (MmcssTimer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExDeleteTimer(MmcssTimer, TRUE, FALSE, NULL);
        return Status;
    }

    Status = PsCreateSystemThread(&Worker, THREAD_ALL_ACCESS, NULL, NULL, NULL, MmcssWorker, NULL);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        ExDeleteTimer(MmcssTimer, TRUE, FALSE, NULL);
        return Status;
    }
    ZwClose(Worker);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = MmcssCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = MmcssCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = MmcssClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MmcssDeviceControl;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
