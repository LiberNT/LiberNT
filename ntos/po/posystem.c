/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The power of the machine as a whole
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

SYSTEM_POWER_CAPABILITIES PopCapabilities;
POP_POWER_ACTION PopAction;
PDEVICE_NODE PopSystemPowerDeviceNode = NULL;
ULONG PopFlushPolicy = 0;
PKWIN32_POWEREVENT_CALLOUT PopEventCallout = NULL;

/* PRIVATE DATA ***************************************************************/

/* One caller that asked to be told when a setting changes */
typedef struct _POP_SETTING_WATCHER
{
    LIST_ENTRY Entry;
    GUID Setting;
    PPOWER_SETTING_CALLBACK Callback;
    PVOID Context;
    PDEVICE_OBJECT DeviceObject;
    PVOID Session;
    ULONG SessionId;
    BOOLEAN Unregistered;
} POP_SETTING_WATCHER, *PPOP_SETTING_WATCHER;

/* What one setting is worth on each power source */
typedef struct _POP_CACHED_SETTING
{
    GUID Setting;
    ULONG Length[PoConditionMaximum];
    PVOID Value[PoConditionMaximum];
} POP_CACHED_SETTING, *PPOP_CACHED_SETTING;

static LIST_ENTRY PopSettingWatchers;
static KGUARDED_MUTEX PopSettingLock;
static POP_CACHED_SETTING PopCachedSettings[POP_MAX_CACHED_POWER_SETTINGS];
static ULONG PopCachedSettingCount;
static SYSTEM_POWER_CONDITION PopPowerSource = PoAc;
static ULONG PopSettingWalks;
static EX_PUSH_LOCK PopIdlenessLock;
static ULONG64 PopIdlenessTime;
static ULONG64 PopIdlenessIdleTicks;
static ULONG PopIdleness;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Asks one device whether the machine may change state, or tells it that it
 * is changing. The same request serves both, the minor function saying which.
 */
static
NTSTATUS
PopAskDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ SYSTEM_POWER_STATE SystemState,
    _In_ POWER_ACTION PowerAction)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT Top;
    KEVENT Done;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();

    Top = IoGetAttachedDeviceReference(DeviceObject);

    KeInitializeEvent(&Done, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_FLUSH_BUFFERS,
                                       Top,
                                       NULL,
                                       0,
                                       NULL,
                                       &Done,
                                       &IoStatusBlock);
    if (Irp == NULL)
    {
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = MinorFunction;
    Stack->Parameters.Power.Type = SystemPowerState;
    Stack->Parameters.Power.State.SystemState = SystemState;
    Stack->Parameters.Power.ShutdownType = PowerAction;

    Status = IoCallDriver(Top, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    ObDereferenceObject(Top);
    return Status;
}

NTSTATUS
PopSendQuerySystemPowerState(
    PDEVICE_OBJECT DeviceObject,
    SYSTEM_POWER_STATE SystemState,
    POWER_ACTION PowerAction)
{
    return PopAskDevice(DeviceObject,
                        IRP_MN_QUERY_POWER,
                        SystemState,
                        PowerAction);
}

NTSTATUS
PopSendSetSystemPowerState(
    PDEVICE_OBJECT DeviceObject,
    SYSTEM_POWER_STATE SystemState,
    POWER_ACTION PowerAction)
{
    return PopAskDevice(DeviceObject,
                        IRP_MN_SET_POWER,
                        SystemState,
                        PowerAction);
}

/*
 * Walks the devices the system knows of, asking or telling each. A device
 * that refuses stops the asking, because the machine stays as it is.
 */
static
NTSTATUS
PopWalkDevices(
    _In_ PDEVICE_NODE Node,
    _In_ UCHAR MinorFunction,
    _In_ SYSTEM_POWER_STATE SystemState,
    _In_ POWER_ACTION PowerAction)
{
    PDEVICE_NODE Child;
    NTSTATUS Status;

    PAGED_CODE();

    if (Node == NULL) return STATUS_SUCCESS;

    for (Child = Node->Child; Child != NULL; Child = Child->Sibling)
    {
        Status = PopWalkDevices(Child, MinorFunction, SystemState, PowerAction);
        if (!NT_SUCCESS(Status) && MinorFunction == IRP_MN_QUERY_POWER)
            return Status;
    }

    if (Node->PhysicalDeviceObject == NULL) return STATUS_SUCCESS;

    return PopAskDevice(Node->PhysicalDeviceObject,
                        MinorFunction,
                        SystemState,
                        PowerAction);
}

NTSTATUS
NTAPI
PopQuerySystemPowerStateTraverse(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PPOP_POWER_ACTION Action = Context;

    return PopWalkDevices(DeviceNode,
                          IRP_MN_QUERY_POWER,
                          Action->SystemState,
                          Action->Action);
}

NTSTATUS
NTAPI
PopSetSystemPowerStateTraverse(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PPOP_POWER_ACTION Action = Context;

    return PopWalkDevices(DeviceNode,
                          IRP_MN_SET_POWER,
                          Action->SystemState,
                          Action->Action);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes the machine to a state. Every device is asked first, and only then
 * is any of them told, so that nothing is left half way.
 */
NTSTATUS
NTAPI
PopSetSystemPowerState(
    SYSTEM_POWER_STATE PowerState,
    POWER_ACTION PowerAction)
{
    NTSTATUS Status;

    PAGED_CODE();

    PopAction.Action = PowerAction;
    PopAction.SystemState = PowerState;

    if (PopSystemPowerDeviceNode == NULL) return STATUS_SUCCESS;

    Status = PopWalkDevices(PopSystemPowerDeviceNode,
                            IRP_MN_QUERY_POWER,
                            PowerState,
                            PowerAction);
    if (!NT_SUCCESS(Status)) return Status;

    return PopWalkDevices(PopSystemPowerDeviceNode,
                          IRP_MN_SET_POWER,
                          PowerState,
                          PowerAction);
}

/* The range of memory that must survive the machine being written out */
NTSTATUS
NTAPI
PoSetHiberRange(
    _In_ PVOID HiberContext,
    _In_ ULONG Flags,
    _In_ PVOID StartPage,
    _In_ ULONG_PTR Length,
    _In_ ULONG PageTag)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(HiberContext);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(StartPage);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(PageTag);

    /* TODO: a driver's own range is not added to what is written out */
    return STATUS_SUCCESS;
}

/* SETTINGS *******************************************************************/

static
PPOP_CACHED_SETTING
PopFindCachedSetting(
    _In_ LPCGUID Setting)
{
    ULONG Index;

    for (Index = 0; Index < PopCachedSettingCount; Index++)
    {
        if (IsEqualGUIDAligned(&PopCachedSettings[Index].Setting, Setting))
        {
            return &PopCachedSettings[Index];
        }
    }

    return NULL;
}

static
VOID
PopFreeSettingWatcher(
    _In_ PPOP_SETTING_WATCHER Watcher)
{
    if (Watcher->Session != NULL)
        MmQuitNextSession(Watcher->Session);

    ExFreePoolWithTag(Watcher, 'cPoP');
}

/* A callback in session space runs attached to the session it was registered from, whoever changed the setting */
static
VOID
PopCallSettingWatcher(
    _In_ PPOP_SETTING_WATCHER Watcher,
    _In_ LPCGUID Setting,
    _In_reads_bytes_(Length) PVOID Value,
    _In_ ULONG Length)
{
    KAPC_STATE ApcState;

    if (Watcher->Session == NULL ||
        PsGetProcessSessionIdEx(PsGetCurrentProcess()) == Watcher->SessionId)
    {
        Watcher->Callback(Setting, Value, Length, Watcher->Context);
        return;
    }

    if (!NT_SUCCESS(MmAttachSession(Watcher->Session, &ApcState)))
        return;

    Watcher->Callback(Setting, Value, Length, Watcher->Context);
    MmDetachSession(Watcher->Session, &ApcState);
}

/*
 * A walk lets go of the lock around every callback, and a callback may take
 * its own registration away. Nothing is unlinked while a walk is under way:
 * a registration that went is only marked, and the last walk to finish frees
 * the marked ones.
 */
static
VOID
PopEndSettingWalk(VOID)
{
    PPOP_SETTING_WATCHER Watcher;
    PLIST_ENTRY Entry, Next;

    if (--PopSettingWalks != 0) return;

    for (Entry = PopSettingWatchers.Flink;
         Entry != &PopSettingWatchers;
         Entry = Next)
    {
        Next = Entry->Flink;
        Watcher = CONTAINING_RECORD(Entry, POP_SETTING_WATCHER, Entry);

        if (!Watcher->Unregistered) continue;

        RemoveEntryList(&Watcher->Entry);
        PopFreeSettingWatcher(Watcher);
    }
}

/* Tells everyone watching one setting what it is worth now */
static
VOID
PopNotifySettingWatchers(
    _In_ LPCGUID Setting,
    _In_reads_bytes_(Length) PVOID Value,
    _In_ ULONG Length)
{
    PPOP_SETTING_WATCHER Watcher;
    PLIST_ENTRY Entry;

    KeAcquireGuardedMutex(&PopSettingLock);
    PopSettingWalks++;

    for (Entry = PopSettingWatchers.Flink;
         Entry != &PopSettingWatchers;
         Entry = Entry->Flink)
    {
        Watcher = CONTAINING_RECORD(Entry, POP_SETTING_WATCHER, Entry);

        if (Watcher->Unregistered) continue;
        if (!IsEqualGUIDAligned(&Watcher->Setting, Setting)) continue;

        KeReleaseGuardedMutex(&PopSettingLock);
        PopCallSettingWatcher(Watcher, Setting, Value, Length);
        KeAcquireGuardedMutex(&PopSettingLock);
    }

    PopEndSettingWalk();
    KeReleaseGuardedMutex(&PopSettingLock);
}

/* Copies what a setting is worth on the source in use; the caller frees it */
static
PVOID
PopCopyCurrentSettingValue(
    _In_ LPCGUID Setting,
    _Out_ PULONG Length)
{
    PPOP_CACHED_SETTING Cached;
    PVOID Copy = NULL;

    *Length = 0;

    Cached = PopFindCachedSetting(Setting);
    if (Cached == NULL || Cached->Value[PopPowerSource] == NULL) return NULL;

    Copy = ExAllocatePoolWithTag(NonPagedPool,
                                 Cached->Length[PopPowerSource],
                                 'vPoP');
    if (Copy == NULL) return NULL;

    RtlCopyMemory(Copy,
                  Cached->Value[PopPowerSource],
                  Cached->Length[PopPowerSource]);
    *Length = Cached->Length[PopPowerSource];

    return Copy;
}

static
NTSTATUS
PopStoreSettingValue(
    _In_ LPCGUID Setting,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(Length) PVOID Value,
    _In_ ULONG Length)
{
    PPOP_CACHED_SETTING Cached;
    PVOID Copy;

    Copy = ExAllocatePoolWithTag(NonPagedPool, Length, 'vPoP');
    if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Copy, Value, Length);

    Cached = PopFindCachedSetting(Setting);
    if (Cached == NULL)
    {
        if (PopCachedSettingCount == POP_MAX_CACHED_POWER_SETTINGS)
        {
            ExFreePoolWithTag(Copy, 'vPoP');
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Cached = &PopCachedSettings[PopCachedSettingCount++];
        RtlZeroMemory(Cached, sizeof(*Cached));
        Cached->Setting = *Setting;
    }

    if (Cached->Value[PowerCondition] != NULL)
    {
        ExFreePoolWithTag(Cached->Value[PowerCondition], 'vPoP');
    }

    Cached->Value[PowerCondition] = Copy;
    Cached->Length[PowerCondition] = Length;

    return STATUS_SUCCESS;
}

/*
 * Changing the power source changes what every setting is worth, so each
 * setting that has a value for the new source is told about again, whether or
 * not the two values differ.
 */
static
VOID
PopSwitchPowerSource(
    _In_ SYSTEM_POWER_CONDITION PowerCondition)
{
    GUID Settings[POP_MAX_CACHED_POWER_SETTINGS];
    ULONG Count, Index, Length;
    PVOID Value;

    KeAcquireGuardedMutex(&PopSettingLock);

    if (PopPowerSource == PowerCondition)
    {
        KeReleaseGuardedMutex(&PopSettingLock);
        return;
    }

    PopPowerSource = PowerCondition;

    Count = PopCachedSettingCount;
    for (Index = 0; Index < Count; Index++)
    {
        Settings[Index] = PopCachedSettings[Index].Setting;
    }

    KeReleaseGuardedMutex(&PopSettingLock);

    for (Index = 0; Index < Count; Index++)
    {
        KeAcquireGuardedMutex(&PopSettingLock);
        Value = PopCopyCurrentSettingValue(&Settings[Index], &Length);
        KeReleaseGuardedMutex(&PopSettingLock);

        if (Value == NULL) continue;

        PopNotifySettingWatchers(&Settings[Index], Value, Length);
        ExFreePoolWithTag(Value, 'vPoP');
    }
}

NTSTATUS
NTAPI
PopSetPowerSettingValue(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    BOOLEAN Current;
    NTSTATUS Status;
    ULONG Source;

    PAGED_CODE();

    if (PowerCondition >= PoConditionMaximum) return STATUS_INVALID_PARAMETER;
    if (ValueLength == 0) return STATUS_INVALID_PARAMETER;
    if (ValueLength > POP_MAX_POWER_SETTING_VALUE_LENGTH)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    if (IsEqualGUIDAligned(SettingGuid,
                           &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE))
    {
        if (ValueLength != sizeof(ULONG) || *(PULONG)Value > 100)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (IsEqualGUIDAligned(SettingGuid, &GUID_ACDC_POWER_SOURCE))
    {
        if (ValueLength != sizeof(ULONG)) return STATUS_INVALID_PARAMETER;

        Source = *(PULONG)Value;
        if (Source != PoAc && Source != PoDc) return STATUS_INVALID_PARAMETER;

        /* The source in use is one value, whichever source it is filed under */
        KeAcquireGuardedMutex(&PopSettingLock);
        Status = PopStoreSettingValue(SettingGuid, PoAc, Value, ValueLength);
        if (NT_SUCCESS(Status))
        {
            Status = PopStoreSettingValue(SettingGuid,
                                          PoDc,
                                          Value,
                                          ValueLength);
        }
        KeReleaseGuardedMutex(&PopSettingLock);

        if (!NT_SUCCESS(Status)) return Status;

        PopSwitchPowerSource((SYSTEM_POWER_CONDITION)Source);
        return STATUS_SUCCESS;
    }

    KeAcquireGuardedMutex(&PopSettingLock);
    Status = PopStoreSettingValue(SettingGuid,
                                  PowerCondition,
                                  Value,
                                  ValueLength);
    Current = (BOOLEAN)(PowerCondition == PopPowerSource);
    KeReleaseGuardedMutex(&PopSettingLock);

    if (!NT_SUCCESS(Status)) return Status;

    /* A value filed for the source not in use changes nothing anyone sees yet */
    if (!Current) return STATUS_SUCCESS;

    PopApplyProcessorPowerSetting(SettingGuid, Value, ValueLength);
    PopNotifySettingWatchers(SettingGuid, Value, ValueLength);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoRegisterPowerSettingCallback(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ LPCGUID SettingGuid,
    _In_ PPOWER_SETTING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Outptr_opt_ PVOID *Handle)
{
    PPOP_SETTING_WATCHER Watcher;
    ULONG Length;
    PVOID Value;

    PAGED_CODE();

    if (SettingGuid == NULL || Callback == NULL)
        return STATUS_INVALID_PARAMETER;

    Watcher = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Watcher), 'cPoP');
    if (Watcher == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Watcher->Setting = *SettingGuid;
    Watcher->Callback = Callback;
    Watcher->Context = Context;
    Watcher->DeviceObject = DeviceObject;
    Watcher->Session = NULL;
    Watcher->SessionId = 0;
    Watcher->Unregistered = FALSE;

    if (MmIsSessionAddress((PVOID)Callback))
    {
        Watcher->SessionId = PsGetProcessSessionIdEx(PsGetCurrentProcess());
        Watcher->Session = MmGetSessionById(Watcher->SessionId);
        if (Watcher->Session == NULL)
        {
            ExFreePoolWithTag(Watcher, 'cPoP');
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (Handle != NULL) *Handle = Watcher;

    KeAcquireGuardedMutex(&PopSettingLock);

    InsertTailList(&PopSettingWatchers, &Watcher->Entry);

    /* Whoever starts watching is told at once what the setting is worth */
    Value = PopCopyCurrentSettingValue(SettingGuid, &Length);
    if (Value == NULL)
    {
        KeReleaseGuardedMutex(&PopSettingLock);
        return STATUS_SUCCESS;
    }

    PopSettingWalks++;
    KeReleaseGuardedMutex(&PopSettingLock);

    PopCallSettingWatcher(Watcher, SettingGuid, Value, Length);

    KeAcquireGuardedMutex(&PopSettingLock);
    PopEndSettingWalk();
    KeReleaseGuardedMutex(&PopSettingLock);

    ExFreePoolWithTag(Value, 'vPoP');

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoUnregisterPowerSettingCallback(
    _Inout_ PVOID Handle)
{
    PPOP_SETTING_WATCHER Watcher = NULL;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (Handle == NULL) return STATUS_INVALID_PARAMETER;

    /* A handle is only what it says it is while its registration is still on the list */
    KeAcquireGuardedMutex(&PopSettingLock);

    for (Entry = PopSettingWatchers.Flink;
         Entry != &PopSettingWatchers;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, POP_SETTING_WATCHER, Entry) == Handle)
        {
            Watcher = Handle;
            break;
        }
    }

    if (Watcher == NULL || Watcher->Unregistered)
    {
        KeReleaseGuardedMutex(&PopSettingLock);
        return STATUS_INVALID_PARAMETER;
    }

    if (PopSettingWalks != 0)
    {
        Watcher->Unregistered = TRUE;
        KeReleaseGuardedMutex(&PopSettingLock);
        return STATUS_SUCCESS;
    }

    RemoveEntryList(&Watcher->Entry);
    KeReleaseGuardedMutex(&PopSettingLock);

    PopFreeSettingWatcher(Watcher);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
PopReadPowerSetting(
    _In_ LPCGUID Setting,
    _Out_writes_bytes_(Length) PVOID Value,
    _In_ ULONG Length)
{
    PPOP_CACHED_SETTING Cached;
    BOOLEAN Found = FALSE;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopSettingLock);

    Cached = PopFindCachedSetting(Setting);
    if (Cached != NULL && Cached->Value[PopPowerSource] != NULL && Cached->Length[PopPowerSource] == Length)
    {
        RtlCopyMemory(Value, Cached->Value[PopPowerSource], Length);
        Found = TRUE;
    }

    KeReleaseGuardedMutex(&PopSettingLock);
    return Found;
}

ULONG
NTAPI
PopGetVideoTimeout(VOID)
{
    PPOP_CACHED_SETTING Cached;
    ULONG Timeout = 0;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopSettingLock);

    Cached = PopFindCachedSetting(&GUID_VIDEO_POWERDOWN_TIMEOUT);
    if (Cached != NULL && Cached->Length[PopPowerSource] == sizeof(ULONG))
        Timeout = *(PULONG)Cached->Value[PopPowerSource];

    KeReleaseGuardedMutex(&PopSettingLock);
    return Timeout;
}

BOOLEAN
NTAPI
PopLockConsoleOnWake(VOID)
{
    /* TODO: nothing is locked when the machine comes back */
    return FALSE;
}

VOID
NTAPI
PopInitializeEventSupport(VOID)
{
    ULONG Preference = 50;
    ULONG Source = PoAc;
    ULONG Condition;

    PAGED_CODE();

    InitializeListHead(&PopSettingWatchers);
    KeInitializeGuardedMutex(&PopSettingLock);

    /* FIXME: the energy preference of the power plan in use is not sourced from Windows 11; the middle of the range stands in */
    for (Condition = PoAc; Condition <= PoDc; Condition++)
    {
        PopStoreSettingValue(&GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE,
                             (SYSTEM_POWER_CONDITION)Condition,
                             &Preference,
                             sizeof(Preference));
        PopStoreSettingValue(&GUID_ACDC_POWER_SOURCE,
                             (SYSTEM_POWER_CONDITION)Condition,
                             &Source,
                             sizeof(Source));
    }
}

/* What a processor does when it has nothing to do */
VOID
FASTCALL
PopIdle0(
    _In_ PPROCESSOR_POWER_STATE PowerState)
{
    UNREFERENCED_PARAMETER(PowerState);

    /* TODO: no deeper idle state is entered than the one the hal gives */
    HalProcessorIdle();
}

VOID
FASTCALL
PopPerfIdle(
    _In_ PPROCESSOR_POWER_STATE PowerState)
{
    PopIdle0(PowerState);
}

VOID
NTAPI
PopPerfIdleDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* TODO: how busy a processor has been does not change how it idles */
}

CODE_SEG("INIT")
VOID
NTAPI
PoInitializePrcb(
    _In_ PKPRCB Prcb)
{
    RtlZeroMemory(&Prcb->PowerState, sizeof(Prcb->PowerState));

    Prcb->PowerState.Idle0KernelTimeLimit = 0xFFFFFFFF;
    Prcb->PowerState.CurrentThrottle = 100;
    Prcb->PowerState.CurrentThrottleIndex = 0;
    Prcb->PowerState.IdleFunction = PopIdle0;

    KeInitializeDpc(&Prcb->PowerState.PerfDpc, PopPerfIdleDpc, Prcb);
    KiSetTargetProcessorDpc(&Prcb->PowerState.PerfDpc, (CCHAR)Prcb->Number);
    KeInitializeTimerEx(&Prcb->PowerState.PerfTimer, SynchronizationTimer);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(
    _In_ ULONG BootPhase)
{
    PAGED_CODE();

    if (BootPhase == 0)
    {
        RtlZeroMemory(&PopCapabilities, sizeof(PopCapabilities));
        RtlZeroMemory(&PopAction, sizeof(PopAction));

        /* Every machine can be stopped, whatever else it can do */
        PopCapabilities.SystemS5 = TRUE;
        PopCapabilities.ThermalControl = FALSE;
        PopCapabilities.SystemBatteriesPresent = FALSE;

        PopInitializeDevicePower();
        PopInitializeVolumes();
        PopInitializeEventSupport();
        PopInitializeThermalRequests();
        if (!PopInitializePowerRequests()) return FALSE;
        PopInitShutdownList();

        return TRUE;
    }

    if (BootPhase == 1)
    {
        PopReadShutdownPolicy();
        PopInitializeStandby();
        PopHiberInitialize();
        PopInitializeSleepSupport();

        return TRUE;
    }

    if (BootPhase == 2)
    {
        /* The hibernation file goes on the boot volume, which the I/O system has only now brought up */
        if (KeLoaderBlock != NULL)
            PopHiberCaptureLoaderBlock(KeLoaderBlock);

        PopInitializeButtons();
        return TRUE;
    }

    return TRUE;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Takes the machine to the state the caller named, which it may do only if
 * it is trusted to stop the machine at all.
 */
NTSTATUS
NTAPI
NtSetSystemPowerState(
    _In_ POWER_ACTION SystemAction,
    _In_ SYSTEM_POWER_STATE MinSystemState,
    _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();

    PAGED_CODE();

    if (SystemAction < PowerActionReserved ||
        SystemAction > PowerActionDisplayOff)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (MinSystemState < PowerSystemSleeping1 ||
        MinSystemState > PowerSystemShutdown)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    if (SystemAction == PowerActionShutdown ||
        SystemAction == PowerActionShutdownReset ||
        SystemAction == PowerActionShutdownOff)
    {
        return PopShutdownSystem(SystemAction);
    }

    return PopSleepSystem(SystemAction, MinSystemState, Flags);
}

NTSTATUS
NTAPI
NtInitiatePowerAction(
    _In_ POWER_ACTION SystemAction,
    _In_ SYSTEM_POWER_STATE MinSystemState,
    _In_ ULONG Flags,
    _In_ BOOLEAN Asynchronous)
{
    PAGED_CODE();

    /* TODO: an action asked for without waiting is still carried out at once */
    UNREFERENCED_PARAMETER(Asynchronous);

    return NtSetSystemPowerState(SystemAction, MinSystemState, Flags);
}

NTSTATUS
NTAPI
NtGetDevicePowerState(
    _In_ HANDLE Device,
    _In_ PDEVICE_POWER_STATE PowerState)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PDEVICE_OBJECT_POWER_EXTENSION Dope;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    DEVICE_POWER_STATE State;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(Device,
                                       0,
                                       IoFileObjectType,
                                       PreviousMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceObject = IoGetRelatedDeviceObject(FileObject);

    Dope = (DeviceObject != NULL)
               ? DeviceObject->DeviceObjectExtension->Dope
               : NULL;

    State = (Dope != NULL) ? Dope->State : PowerDeviceD0;

    ObDereferenceObject(FileObject);

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForWrite(PowerState, sizeof(*PowerState), sizeof(ULONG));

        *PowerState = State;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

BOOLEAN
NTAPI
NtIsSystemResumeAutomatic(VOID)
{
    PAGED_CODE();

    /* TODO: how the machine last came back is not remembered */
    return FALSE;
}

NTSTATUS
NTAPI
NtRequestWakeupLatency(
    _In_ LATENCY_TIME Latency)
{
    PAGED_CODE();

    if (Latency != LT_DONT_CARE && Latency != LT_LOWEST_LATENCY)
        return STATUS_INVALID_PARAMETER;

    /* TODO: what a caller can wait for does not change how the machine idles */
    return STATUS_SUCCESS;
}

/*
 * What a thread asks the machine to keep on while it runs. The asking is
 * remembered on the thread, so that it goes when the thread does.
 */
NTSTATUS
NTAPI
NtSetThreadExecutionState(
    _In_ EXECUTION_STATE NewFlags,
    _Out_ PEXECUTION_STATE PreviousFlags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PETHREAD Thread = PsGetCurrentThread();
    PVOID Asked = Thread->LegacyPowerObject;
    EXECUTION_STATE Was;
    EXECUTION_STATE Keep;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (NewFlags & ~(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED |
                     ES_USER_PRESENT | ES_CONTINUOUS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Was = PopQuerySystemState(Asked) | ES_CONTINUOUS;

    if (NewFlags & ES_CONTINUOUS)
    {
        Keep = NewFlags & ~ES_CONTINUOUS;
        if (Asked != NULL)
        {
            PoRegisterSystemState(Asked, Keep);
        }
        else if (Keep != 0)
        {
            Asked = PoRegisterSystemState(NULL, Keep);
            if (Asked == NULL) return STATUS_INSUFFICIENT_RESOURCES;
            Thread->LegacyPowerObject = Asked;
        }
    }
    else
    {
        /* An asking that is not to stand is counted once and forgotten */
        PoSetSystemState(NewFlags);
    }

    if (NewFlags & ES_DISPLAY_REQUIRED)
        PopNoteDisplayActivity();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForWrite(PreviousFlags, sizeof(*PreviousFlags), sizeof(ULONG));

        *PreviousFlags = Was;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * Answers what the machine can do about power, and takes what it is told to
 * do differently. Only what is asked for is answered.
 */
/*
 * Takes a setting a caller hands in. The value is copied out of the caller's
 * buffer before it is looked at, and the copy is what is kept.
 */
static
NTSTATUS
PopSetPowerSettingValueFromCaller(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    PSET_POWER_SETTING_VALUE Setting = InputBuffer;
    SET_POWER_SETTING_VALUE Header;
    NTSTATUS Status;
    PVOID Value;

    if (InputBuffer == NULL ||
        InputBufferLength < FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlCopyMemory(&Header,
                  Setting,
                  FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data));

    if (Header.Version != POWER_SETTING_VALUE_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Header.PowerCondition >= PoConditionMaximum)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Header.DataLength == 0) return STATUS_INVALID_PARAMETER;

    if (Header.DataLength > POP_MAX_POWER_SETTING_VALUE_LENGTH)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    if (Header.DataLength >
        InputBufferLength - FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Value = ExAllocatePoolWithTag(PagedPool, Header.DataLength, 'vPoP');
    if (Value == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Value, Setting->Data, Header.DataLength);

    Status = PopSetPowerSettingValue(&Header.Guid,
                                     Header.PowerCondition,
                                     Value,
                                     Header.DataLength);

    ExFreePoolWithTag(Value, 'vPoP');

    return Status;
}

static
ULONG
PopQueryIdleness(VOID)
{
    ULONG64 Now;
    ULONG64 Elapsed;
    ULONG64 Idle;
    ULONG64 IdleTicks = 0;
    ULONG Index;
    ULONG Idleness;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&PopIdlenessLock);

    Now = KeQueryInterruptTime();
    /* A sample of a few clock ticks would only measure the tick: a caller that comes back sooner gets the previous figure */
    if (Now - PopIdlenessTime >= 10ULL * KeMaximumIncrement)
    {
        for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
            IdleTicks += KiProcessorBlock[Index]->IdleThread->KernelTime;

        Elapsed = (Now - PopIdlenessTime) * (ULONG)KeNumberProcessors;
        Idle = (IdleTicks - PopIdlenessIdleTicks) * KeMaximumIncrement;
        PopIdleness = (ULONG)(min(Idle, Elapsed) * 100 / Elapsed);
        PopIdlenessTime = Now;
        PopIdlenessIdleTicks = IdleTicks;
    }
    Idleness = PopIdleness;

    ExReleasePushLockExclusive(&PopIdlenessLock);
    KeLeaveCriticalRegion();

    return Idleness;
}

static
NTSTATUS
PopQuerySystemPowerInformation(
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    SYSTEM_POWER_INFORMATION Information;

    if (InputBufferLength != 0) return STATUS_INVALID_PARAMETER;
    if (OutputBufferLength < sizeof(Information)) return STATUS_BUFFER_TOO_SMALL;

    /* No system idle timer is armed, so there is no threshold and no time left on it */
    RtlZeroMemory(&Information, sizeof(Information));
    Information.Idleness = PopQueryIdleness();
    Information.CoolingMode = PopQuerySystemCoolingMode();

    RtlCopyMemory(OutputBuffer, &Information, sizeof(Information));
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopQueryProcessorInformation(
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    PPROCESSOR_POWER_INFORMATION Output = OutputBuffer;
    PROCESSOR_POWER_INFORMATION Processor;
    ULONG Count = (ULONG)KeNumberProcessors;
    ULONG Index;

    if (InputBufferLength != 0) return STATUS_INVALID_PARAMETER;
    if (OutputBufferLength / sizeof(Processor) < Count) return STATUS_BUFFER_TOO_SMALL;

    for (Index = 0; Index < Count; Index++)
    {
        PKPRCB Prcb = KiProcessorBlock[Index];

        Processor.Number = Prcb->Number;
        Processor.MaxMhz = Prcb->MHz;
        /* FIXME: no performance states are tracked; the frequency measured at boot stands for the current one */
        Processor.CurrentMhz = Prcb->MHz;
        Processor.MhzLimit = Prcb->MHz * Prcb->PowerState.CurrentThrottle / 100;
        /* TODO: no processor idle states are registered, so none is reported */
        Processor.MaxIdleState = 0;
        Processor.CurrentIdleState = 0;

        RtlCopyMemory(&Output[Index], &Processor, sizeof(Processor));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtPowerInformation(
    _In_ POWER_INFORMATION_LEVEL PowerInformationLevel,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    SYSTEM_POWER_CAPABILITIES Capabilities;
    SYSTEM_BATTERY_STATE BatteryState;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (((PowerInformationLevel == SystemPowerInformation) &&
         ((OutputBuffer == NULL) || (OutputBufferLength == 0))) ||
        ((PowerInformationLevel == ProcessorInformation) && (OutputBuffer == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (InputBufferLength != 0)
                ProbeForRead(InputBuffer,
                             InputBufferLength,
                             (PowerInformationLevel == SystemReserveHiberFile) ?
                                 TYPE_ALIGNMENT(BOOLEAN) : sizeof(ULONG));
            if (OutputBufferLength != 0)
                ProbeForWrite(OutputBuffer,
                              OutputBufferLength,
                              (PowerInformationLevel == PlatformInformation) ?
                                  TYPE_ALIGNMENT(POWER_PLATFORM_INFORMATION) : sizeof(ULONG));
        }

        switch (PowerInformationLevel)
        {
            case SystemPowerCapabilities:
                if (OutputBufferLength < sizeof(Capabilities))
                {
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                RtlCopyMemory(OutputBuffer,
                              &PopCapabilities,
                              sizeof(PopCapabilities));
                break;

            case SystemBatteryState:
                if (OutputBufferLength < sizeof(BatteryState))
                {
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                /* TODO: no battery is asked; only which source is in use is reported */
                RtlZeroMemory(&BatteryState, sizeof(BatteryState));
                BatteryState.AcOnLine = (BOOLEAN)(PopPowerSource == PoAc);
                RtlCopyMemory(OutputBuffer, &BatteryState, sizeof(BatteryState));
                break;

            case SetPowerSettingValue:
                Status = PopSetPowerSettingValueFromCaller(InputBuffer,
                                                          InputBufferLength);
                break;

            case SystemPowerInformation:
                Status = PopQuerySystemPowerInformation(InputBufferLength,
                                                        OutputBuffer,
                                                        OutputBufferLength);
                break;

            case ProcessorInformation:
                Status = PopQueryProcessorInformation(InputBufferLength,
                                                      OutputBuffer,
                                                      OutputBufferLength);
                break;

            case PowerRequestCreate:
                Status = PopCreatePowerRequest(InputBuffer,
                                               InputBufferLength,
                                               OutputBuffer,
                                               OutputBufferLength);
                break;

            case PowerRequestAction:
                Status = PopActOnPowerRequest(InputBuffer, InputBufferLength);
                break;

            case SystemExecutionState:
                if (OutputBufferLength < sizeof(EXECUTION_STATE))
                {
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                *(PEXECUTION_STATE)OutputBuffer =
                    PopIsDisplayRequired() ? ES_DISPLAY_REQUIRED : 0;
                break;

            case SessionDisplayState:
                Status = PopSetSessionDisplayState(PreviousMode,
                                                   InputBuffer,
                                                   InputBufferLength);
                break;

            case PlatformInformation:
                Status = PopQueryPlatformInformation(InputBuffer,
                                                     InputBufferLength,
                                                     OutputBuffer,
                                                     OutputBufferLength);
                break;

            case SystemPowerStateHandler:
                Status = PopSetPowerStateHandler(PreviousMode,
                                                 InputBuffer,
                                                 InputBufferLength,
                                                 OutputBuffer,
                                                 OutputBufferLength);
                break;

            case SystemPowerStateLogging:
                Status = PopQuerySleepLogging(InputBuffer,
                                              OutputBuffer,
                                              OutputBufferLength);
                break;

            case SystemPowerLoggingEntry:
                /* TODO: what a kernel caller writes through this level is not sourced */
                Status = (PreviousMode != KernelMode) ? STATUS_ACCESS_DENIED : STATUS_NOT_IMPLEMENTED;
                break;

            case SystemReserveHiberFile:
                Status = PopReserveHiberFileFromCaller(PreviousMode,
                                                       InputBuffer,
                                                       InputBufferLength,
                                                       OutputBuffer,
                                                       OutputBufferLength);
                break;

            case LastSleepTime:
            case LastWakeTime:
                Status = PopQuerySleepTime(PowerInformationLevel,
                                           InputBuffer,
                                           OutputBuffer,
                                           OutputBufferLength);
                break;

            default:
                /* TODO: the remaining levels are not answered for */
                Status = STATUS_NOT_IMPLEMENTED;
                break;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}
