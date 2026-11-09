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

/* PRIVATE DATA ***************************************************************/

/* One caller that asked to be told when a setting changes */
typedef struct _POP_SETTING_WATCHER
{
    LIST_ENTRY Entry;
    GUID Setting;
    PPOWER_SETTING_CALLBACK Callback;
    PVOID Context;
    PDEVICE_OBJECT DeviceObject;
} POP_SETTING_WATCHER, *PPOP_SETTING_WATCHER;

static LIST_ENTRY PopSettingWatchers;
static KGUARDED_MUTEX PopSettingLock;
static ULONG PopVideoTimeout = 0;

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

    PAGED_CODE();

    if (SettingGuid == NULL || Callback == NULL)
        return STATUS_INVALID_PARAMETER;

    Watcher = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Watcher), 'cPoP');
    if (Watcher == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Watcher->Setting = *SettingGuid;
    Watcher->Callback = Callback;
    Watcher->Context = Context;
    Watcher->DeviceObject = DeviceObject;

    KeAcquireGuardedMutex(&PopSettingLock);
    InsertTailList(&PopSettingWatchers, &Watcher->Entry);
    KeReleaseGuardedMutex(&PopSettingLock);

    if (Handle != NULL) *Handle = Watcher;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoUnregisterPowerSettingCallback(
    _Inout_ PVOID Handle)
{
    PPOP_SETTING_WATCHER Watcher = Handle;

    PAGED_CODE();

    if (Watcher == NULL) return STATUS_INVALID_PARAMETER;

    KeAcquireGuardedMutex(&PopSettingLock);
    RemoveEntryList(&Watcher->Entry);
    KeReleaseGuardedMutex(&PopSettingLock);

    ExFreePoolWithTag(Watcher, 'cPoP');
    return STATUS_SUCCESS;
}

ULONG
NTAPI
PopGetVideoTimeout(VOID)
{
    return PopVideoTimeout;
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
    PAGED_CODE();

    InitializeListHead(&PopSettingWatchers);
    KeInitializeGuardedMutex(&PopSettingLock);
}

/* What the system says to whoever asked to hear about power events */
NTSTATUS
NTAPI
PopEventCallout(
    _In_ LPCGUID Setting,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PPOP_SETTING_WATCHER Watcher;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopSettingLock);

    for (Entry = PopSettingWatchers.Flink;
         Entry != &PopSettingWatchers;
         Entry = Entry->Flink)
    {
        Watcher = CONTAINING_RECORD(Entry, POP_SETTING_WATCHER, Entry);

        if (!IsEqualGUIDAligned(&Watcher->Setting, Setting)) continue;

        Watcher->Callback(Setting, Value, ValueLength, Watcher->Context);
    }

    KeReleaseGuardedMutex(&PopSettingLock);

    return STATUS_SUCCESS;
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
    KeSetTargetProcessorDpc(&Prcb->PowerState.PerfDpc, (CCHAR)Prcb->Number);
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
        PopInitShutdownList();

        return TRUE;
    }

    if (BootPhase == 1)
    {
        PopReadShutdownPolicy();
        PopInitializeStandby();
        PopHiberInitialize();
        PopAoAc = PopDetectAoAc();

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
    EXECUTION_STATE Was;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (NewFlags & ~(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED |
                     ES_USER_PRESENT | ES_CONTINUOUS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Was = (EXECUTION_STATE)(ULONG_PTR)Thread->Tcb.Win32Thread;

    if (NewFlags & ES_CONTINUOUS)
    {
        Thread->Tcb.Win32Thread = (PVOID)(ULONG_PTR)NewFlags;
        PoSetSystemState(NewFlags);
    }
    else
    {
        /* An asking that is not to stand is counted once and forgotten */
        PoSetSystemState(NewFlags);
    }

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

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (InputBufferLength != 0)
                ProbeForRead(InputBuffer, InputBufferLength, sizeof(ULONG));
            if (OutputBufferLength != 0)
                ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(ULONG));
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

                RtlZeroMemory(&BatteryState, sizeof(BatteryState));
                BatteryState.AcOnLine = TRUE;
                RtlCopyMemory(OutputBuffer, &BatteryState, sizeof(BatteryState));
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
