/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Console display state and modern standby
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>

#define POP_FADT_LOW_POWER_S0_IDLE  (1UL << 21)
#define POP_STANDBY_TAG             'bSoP'

typedef struct _POP_STANDBY_PROCESS
{
    LIST_ENTRY Link;
    PEPROCESS Process;
} POP_STANDBY_PROCESS, *PPOP_STANDBY_PROCESS;

extern PKWIN32_POWEREVENT_CALLOUT PopEventCallout;

BOOLEAN PopAoAc;
static KMUTEX PopDisplayLock;
static WORK_QUEUE_ITEM PopDisplayWorkItem;
static volatile LONG PopDisplayQueued;
static volatile LONG PopDisplayTarget = PowerMonitorOn;
static MONITOR_DISPLAY_STATE PopDisplayState = PowerMonitorOn;
static LIST_ENTRY PopStandbyPausedList;
static KTIMER PopDisplayIdleTimer;
static KDPC PopDisplayIdleDpc;
static WORK_QUEUE_ITEM PopDisplayIdleWorkItem;
static volatile LONG PopDisplayIdleQueued;
static volatile LONG PopDisplayActivityTick;

static
ULONG
PopTickMilliseconds(VOID)
{
    LARGE_INTEGER TickCount;

    KeQueryTickCount(&TickCount);
    return (ULONG)KiTickCountToMs(TickCount);
}

BOOLEAN
NTAPI
PopDetectAoAc(VOID)
{
    PFADT Fadt;

    if (HalGetCachedAcpiTable == NULL)
        return FALSE;

    Fadt = HalGetCachedAcpiTable(FADT_SIGNATURE, NULL, NULL);
    if (Fadt == NULL || Fadt->Header.Length < RTL_SIZEOF_THROUGH_FIELD(FADT, flags))
        return FALSE;

    return (Fadt->flags & POP_FADT_LOW_POWER_S0_IDLE) != 0;
}

static
BOOLEAN
PopStandbyIsDesktopApplication(
    _In_ PEPROCESS Process)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PACCESS_TOKEN Token;
    PTOKEN_USER User = NULL;
    PSID Sid;
    BOOLEAN Desktop = FALSE;

    if (Process == PsInitialSystemProcess ||
        Process == PsGetCurrentProcess() ||
        Process->BreakOnTermination ||
        PsGetProcessSessionId(Process) == 0 ||
        PsGetProcessExitProcessCalled(Process))
    {
        return FALSE;
    }

    Token = PsReferencePrimaryToken(Process);
    if (Token == NULL)
        return FALSE;

    if (NT_SUCCESS(SeQueryInformationToken(Token, TokenUser, (PVOID *)&User)))
    {
        Sid = User->User.Sid;
        Desktop = RtlValidSid(Sid) &&
                  RtlEqualMemory(RtlIdentifierAuthoritySid(Sid), &NtAuthority, sizeof(NtAuthority)) &&
                  *RtlSubAuthorityCountSid(Sid) >= 1 &&
                  *RtlSubAuthoritySid(Sid, 0) == SECURITY_NT_NON_UNIQUE;
        ExFreePool(User);
    }

    PsDereferencePrimaryToken(Token);
    return Desktop;
}

static
VOID
PopStandbyPauseDesktopApplications(VOID)
{
    PPOP_STANDBY_PROCESS Entry;
    PEPROCESS Process;
    ULONG Count = 0;

    for (Process = PsGetNextProcess(NULL); Process != NULL; Process = PsGetNextProcess(Process))
    {
        if (!PopStandbyIsDesktopApplication(Process))
            continue;

        Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), POP_STANDBY_TAG);
        if (Entry == NULL)
            continue;

        if (!NT_SUCCESS(PsSuspendProcess(Process)))
        {
            ExFreePoolWithTag(Entry, POP_STANDBY_TAG);
            continue;
        }

        ObReferenceObject(Process);
        Entry->Process = Process;
        InsertTailList(&PopStandbyPausedList, &Entry->Link);
        Count++;
    }

    DPRINT1("Modern standby paused %lu desktop applications\n", Count);
}

static
VOID
PopStandbyResumeDesktopApplications(VOID)
{
    PPOP_STANDBY_PROCESS Entry;
    PLIST_ENTRY Link;
    ULONG Count = 0;

    while (!IsListEmpty(&PopStandbyPausedList))
    {
        Link = RemoveHeadList(&PopStandbyPausedList);
        Entry = CONTAINING_RECORD(Link, POP_STANDBY_PROCESS, Link);
        PsResumeProcess(Entry->Process);
        ObDereferenceObject(Entry->Process);
        ExFreePoolWithTag(Entry, POP_STANDBY_TAG);
        Count++;
    }

    DPRINT1("Modern standby resumed %lu desktop applications\n", Count);
}

static
VOID
PopPublishDisplayState(
    _In_ MONITOR_DISPLAY_STATE State)
{
    WIN32_POWEREVENT_PARAMETERS Parameters;
    ULONG MonitorOn = (State != PowerMonitorOff);
    ULONG Value = State;
    ULONG Condition;

    for (Condition = PoAc; Condition < PoConditionMaximum; Condition++)
    {
        PopSetPowerSettingValue(&GUID_CONSOLE_DISPLAY_STATE, (SYSTEM_POWER_CONDITION)Condition, &Value, sizeof(Value));
        PopSetPowerSettingValue(&GUID_MONITOR_POWER_ON, (SYSTEM_POWER_CONDITION)Condition, &MonitorOn, sizeof(MonitorOn));
    }

    if (PopAoAc && State == PowerMonitorOff)
    {
        PopSleepCallWin32(PowerState_MonitorOff, PowerActionSleep, PowerSystemWorking, 0);
    }
    else if (PopEventCallout != NULL)
    {
        Parameters.EventNumber = PsW32DisplayState;
        Parameters.Code = State;
        (VOID)PopEventCallout(&Parameters);
    }
}

static
VOID
PopApplyDisplayState(
    _In_ MONITOR_DISPLAY_STATE State)
{
    MONITOR_DISPLAY_STATE Previous = PopDisplayState;

    if (State == Previous)
        return;

    PopDisplayState = State;
    DPRINT1("The console display is now %s\n",
            State == PowerMonitorOff ? "off" : State == PowerMonitorDim ? "dimmed" : "on");

    if (PopAoAc && Previous == PowerMonitorOff)
    {
        PopStandbyResumeDesktopApplications();
        DPRINT1("Modern standby exited\n");
    }

    if (PopAoAc && State == PowerMonitorOff && PopLockConsoleOnWake())
        PopSleepCallWin32(PowerState_NotifyWL, PowerActionSleep, PowerSystemWorking, POWER_ACTION_LOCK_CONSOLE);

    PopPublishDisplayState(State);

    if (PopAoAc && State == PowerMonitorOff)
    {
        DPRINT1("Modern standby entered\n");
        PopStandbyPauseDesktopApplications();
    }
}

static
VOID
NTAPI
PopDisplayWorker(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    KeWaitForSingleObject(&PopDisplayLock, Executive, KernelMode, FALSE, NULL);
    InterlockedExchange(&PopDisplayQueued, 0);
    PopApplyDisplayState((MONITOR_DISPLAY_STATE)InterlockedCompareExchange(&PopDisplayTarget, 0, 0));
    KeReleaseMutex(&PopDisplayLock, FALSE);
}

VOID
NTAPI
PopRequestDisplayState(
    _In_ MONITOR_DISPLAY_STATE State)
{
    InterlockedExchange(&PopDisplayTarget, State);
    if (InterlockedExchange(&PopDisplayQueued, 1) == 0)
        ExQueueWorkItem(&PopDisplayWorkItem, DelayedWorkQueue);
}

MONITOR_DISPLAY_STATE
NTAPI
PopGetDisplayState(VOID)
{
    return (MONITOR_DISPLAY_STATE)InterlockedCompareExchange(&PopDisplayTarget, 0, 0);
}

VOID
NTAPI
PopNoteDisplayActivity(VOID)
{
    InterlockedExchange(&PopDisplayActivityTick, (LONG)PopTickMilliseconds());
    if (PopGetDisplayState() != PowerMonitorOn)
        PopRequestDisplayState(PowerMonitorOn);
}

static
VOID
NTAPI
PopDisplayIdleWorker(
    _In_ PVOID Context)
{
    ULONG Timeout;
    ULONG Now;
    ULONG Idle;

    UNREFERENCED_PARAMETER(Context);

    InterlockedExchange(&PopDisplayIdleQueued, 0);

    Timeout = PopGetVideoTimeout();
    if (Timeout == 0 || PopGetDisplayState() != PowerMonitorOn || PopIsDisplayRequired())
        return;

    Now = PopTickMilliseconds();
    Idle = min(Now - SharedUserData->LastSystemRITEventTickCount,
               Now - (ULONG)InterlockedCompareExchange(&PopDisplayActivityTick, 0, 0));
    if (Idle / 1000 < Timeout)
        return;

    DPRINT1("The display idled out after %lu seconds\n", Timeout);
    PopRequestDisplayState(PowerMonitorOff);
}

static
VOID
NTAPI
PopDisplayIdleDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedExchange(&PopDisplayIdleQueued, 1) == 0)
        ExQueueWorkItem(&PopDisplayIdleWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
PopInitializeStandby(VOID)
{
    LARGE_INTEGER DueTime;

    KeInitializeMutex(&PopDisplayLock, 0);
    ExInitializeWorkItem(&PopDisplayWorkItem, PopDisplayWorker, NULL);
    InitializeListHead(&PopStandbyPausedList);

    PopDisplayActivityTick = (LONG)PopTickMilliseconds();
    ExInitializeWorkItem(&PopDisplayIdleWorkItem, PopDisplayIdleWorker, NULL);
    KeInitializeDpc(&PopDisplayIdleDpc, PopDisplayIdleDpcRoutine, NULL);
    KeInitializeTimerEx(&PopDisplayIdleTimer, SynchronizationTimer);
    DueTime.QuadPart = -10000000LL;
    KeSetTimerEx(&PopDisplayIdleTimer, DueTime, 1000, &PopDisplayIdleDpc);
}
