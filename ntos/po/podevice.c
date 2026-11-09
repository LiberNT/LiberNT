/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The power of one device, and what the system is asked to keep on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* What one caller asks the system to keep on, and for how long */
typedef struct _POP_KEEP_ON
{
    LIST_ENTRY Entry;
    EXECUTION_STATE Flags;
    PETHREAD Thread;
} POP_KEEP_ON, *PPOP_KEEP_ON;

/* What is riding on one power request made for a driver */
typedef struct _POP_ASKED_POWER
{
    PDEVICE_OBJECT DeviceObject;
    PREQUEST_POWER_COMPLETE CompletionFunction;
    PVOID Context;
    UCHAR MinorFunction;
    POWER_STATE PowerState;
} POP_ASKED_POWER, *PPOP_ASKED_POWER;

static LIST_ENTRY PopKeepOnList;
static KSPIN_LOCK PopKeepOnLock;
static EXECUTION_STATE PopKeptOn = 0;

KSPIN_LOCK PopDopeGlobalLock;
BOOLEAN PopAcpiPresent = FALSE;
ULONG PopVSyncState = 0;
ULONG PopLatencySensitivityState = 0;
ULONG PopLatencySensitivityTime = 0;
ULONG PopUserPresentReason = 0;
ULONG PopUserPresentSequence = 0;
LARGE_INTEGER PopUserPresentTime;
static PIRP PopWakeRequest = NULL;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The room a device keeps its power in. A device that was never asked about
 * its power has none, so the first question settles it.
 */
PDEVICE_OBJECT_POWER_EXTENSION
NTAPI
PopGetDope(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;
    PDEVICE_OBJECT_POWER_EXTENSION Made;
    KIRQL OldIrql;

    Dope = DeviceObject->DeviceObjectExtension->Dope;
    if (Dope != NULL) return Dope;

    Made = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(DEVICE_OBJECT_POWER_EXTENSION),
                                 'ePoP');
    if (Made == NULL) return NULL;

    RtlZeroMemory(Made, sizeof(*Made));
    Made->DeviceObject = DeviceObject;
    Made->State = PowerDeviceD0;
    InitializeListHead(&Made->IdleList);
    InitializeListHead(&Made->NotifySourceList);
    InitializeListHead(&Made->NotifyTargetList);
    InitializeListHead(&Made->Volume);

    KeAcquireSpinLock(&PopDopeGlobalLock, &OldIrql);

    /* Two callers asking at once settle it once between them */
    Dope = DeviceObject->DeviceObjectExtension->Dope;
    if (Dope == NULL)
    {
        DeviceObject->DeviceObjectExtension->Dope = Made;
        Dope = Made;
        Made = NULL;
    }

    KeReleaseSpinLock(&PopDopeGlobalLock, OldIrql);

    if (Made != NULL) ExFreePoolWithTag(Made, 'ePoP');

    return Dope;
}

/* What the system has to keep on is the sum of what everybody asked for */
static
VOID
PopRecountKeepOn(VOID)
{
    EXECUTION_STATE Flags = 0;
    PPOP_KEEP_ON Asked;
    PLIST_ENTRY Entry;

    for (Entry = PopKeepOnList.Flink;
         Entry != &PopKeepOnList;
         Entry = Entry->Flink)
    {
        Asked = CONTAINING_RECORD(Entry, POP_KEEP_ON, Entry);
        Flags |= Asked->Flags;
    }

    PopKeptOn = Flags;
}

static
IO_COMPLETION_ROUTINE PopAskedPowerComplete;

static
NTSTATUS
NTAPI
PopAskedPowerComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PPOP_ASKED_POWER Asked = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Asked->CompletionFunction != NULL)
    {
        Asked->CompletionFunction(Asked->DeviceObject,
                                  Asked->MinorFunction,
                                  Asked->PowerState,
                                  Asked->Context,
                                  &Irp->IoStatus);
    }

    ObDereferenceObject(Asked->DeviceObject);
    ExFreePoolWithTag(Asked, 'rPoP');

    IoFreeIrp(Irp);

    /* The request was made by the system, so nobody is waiting on the irp */
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PopInitializeDevicePower(VOID)
{
    PAGED_CODE();

    KeInitializeSpinLock(&PopDopeGlobalLock);
    KeInitializeSpinLock(&PopKeepOnLock);
    InitializeListHead(&PopKeepOnList);
}

VOID
NTAPI
PoInitializeDeviceObject(
    _Inout_ PDEVOBJ_EXTENSION DeviceObjectExtension)
{
    DeviceObjectExtension->PowerFlags = 0;
    DeviceObjectExtension->Dope = NULL;
}

/*
 * Carries a power request down a device's stack. A request about power is
 * never turned back by the system, only passed on.
 */
NTSTATUS
NTAPI
PoCallDriver(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetNextIrpStackLocation(Irp);

    /* TODO: power requests are not held back one behind another */
    if (Stack->MajorFunction != IRP_MJ_POWER) return STATUS_INVALID_PARAMETER;

    return IoCallDriver(DeviceObject, Irp);
}

VOID
NTAPI
PoStartNextPowerIrp(
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Irp);

    /* TODO: nothing is queued behind a power request, so nothing follows it */
}

/*
 * Asks a device about its power on a driver's behalf, and tells the driver
 * what came of it once the stack has answered.
 */
NTSTATUS
NTAPI
PoRequestPowerIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PREQUEST_POWER_COMPLETE CompletionFunction,
    _In_opt_ PVOID Context,
    _Outptr_opt_ PIRP *IrpOut)
{
    PDEVICE_OBJECT Top;
    PPOP_ASKED_POWER Asked;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    if (MinorFunction != IRP_MN_QUERY_POWER &&
        MinorFunction != IRP_MN_SET_POWER &&
        MinorFunction != IRP_MN_WAIT_WAKE)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    Top = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoAllocateIrp(Top->StackSize + 1, FALSE);
    if (Irp == NULL)
    {
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Asked = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Asked), 'rPoP');
    if (Asked == NULL)
    {
        IoFreeIrp(Irp);
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Asked->DeviceObject = Top;
    Asked->CompletionFunction = CompletionFunction;
    Asked->Context = Context;
    Asked->MinorFunction = MinorFunction;
    Asked->PowerState = PowerState;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->Flags = IRP_SYNCHRONOUS_API;

    /* The first location is the system's own, so that the request can be
     * answered for after the stack is done with it */
    IoSetNextIrpStackLocation(Irp);
    IoSetCompletionRoutine(Irp, PopAskedPowerComplete, Asked, TRUE, TRUE, TRUE);

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = MinorFunction;

    if (MinorFunction == IRP_MN_WAIT_WAKE)
    {
        Stack->Parameters.WaitWake.PowerState = PowerState.SystemState;
    }
    else
    {
        Stack->Parameters.Power.Type = DevicePowerState;
        Stack->Parameters.Power.State = PowerState;
        Stack->Parameters.Power.ShutdownType = PowerActionNone;
    }

    if (IrpOut != NULL) *IrpOut = Irp;

    IoCallDriver(Top, Irp);

    return STATUS_PENDING;
}

/* Records what a device says its power now is, and says what it was */
POWER_STATE
NTAPI
PoSetPowerState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ POWER_STATE_TYPE Type,
    _In_ POWER_STATE State)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;
    POWER_STATE Was;

    Was.DeviceState = PowerDeviceD0;

    if (Type != DevicePowerState) return Was;

    Dope = PopGetDope(DeviceObject);
    if (Dope == NULL) return Was;

    Was.DeviceState = Dope->State;
    Dope->State = State.DeviceState;

    return Was;
}

PULONG
NTAPI
PoRegisterDeviceForIdleDetection(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ConservationIdleTime,
    _In_ ULONG PerformanceIdleTime,
    _In_ DEVICE_POWER_STATE State)
{
    PDEVICE_OBJECT_POWER_EXTENSION Dope;

    PAGED_CODE();

    Dope = PopGetDope(DeviceObject);
    if (Dope == NULL) return NULL;

    Dope->ConservationIdleTime = ConservationIdleTime;
    Dope->PerformanceIdleTime = PerformanceIdleTime;
    Dope->IdleCount = 0;

    /* A device that says it needs nothing is no longer watched */
    if (State == PowerDeviceUnspecified) return NULL;

    /* TODO: nothing counts the idle time, so the count never advances */
    return &Dope->IdleCount;
}

NTSTATUS
NTAPI
PoRegisterDeviceNotify(
    _In_ PDEVICE_OBJECT TargetDevice,
    _In_ ULONG TargetDeviceNotifyFlags,
    _In_ ULONG SourceDeviceNotifyFlags,
    _In_ ULONG Flags,
    _In_ PVOID Context,
    _Out_ PVOID *NotifyBlock)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(TargetDevice);
    UNREFERENCED_PARAMETER(TargetDeviceNotifyFlags);
    UNREFERENCED_PARAMETER(SourceDeviceNotifyFlags);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Context);

    *NotifyBlock = NULL;

    /* TODO: devices are not told about one another's power */
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
PoCancelDeviceNotify(
    _In_ PVOID NotifyBlock)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(NotifyBlock);
}

/*
 * What a caller asks the system to keep on while it works. Each asking is
 * kept on its own, so that letting one go does not let the others go.
 */
PVOID
NTAPI
PoRegisterSystemState(
    _Inout_opt_ PVOID StateHandle,
    _In_ EXECUTION_STATE Flags)
{
    PPOP_KEEP_ON Asked = StateHandle;
    KIRQL OldIrql;

    if (Asked != NULL)
    {
        KeAcquireSpinLock(&PopKeepOnLock, &OldIrql);
        Asked->Flags = Flags;
        PopRecountKeepOn();
        KeReleaseSpinLock(&PopKeepOnLock, OldIrql);
        return Asked;
    }

    Asked = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Asked), 'sPoP');
    if (Asked == NULL) return NULL;

    Asked->Flags = Flags;
    Asked->Thread = NULL;

    KeAcquireSpinLock(&PopKeepOnLock, &OldIrql);
    InsertTailList(&PopKeepOnList, &Asked->Entry);
    PopRecountKeepOn();
    KeReleaseSpinLock(&PopKeepOnLock, OldIrql);

    return Asked;
}

VOID
NTAPI
PoUnregisterSystemState(
    _Inout_ PVOID StateHandle)
{
    PPOP_KEEP_ON Asked = StateHandle;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Asked == NULL) return;

    KeAcquireSpinLock(&PopKeepOnLock, &OldIrql);
    RemoveEntryList(&Asked->Entry);
    PopRecountKeepOn();
    KeReleaseSpinLock(&PopKeepOnLock, OldIrql);

    ExFreePoolWithTag(Asked, 'sPoP');
}

VOID
NTAPI
PoSetSystemState(
    _In_ EXECUTION_STATE Flags)
{
    KIRQL OldIrql;

    /* An asking that nobody holds on to counts once and is forgotten */
    KeAcquireSpinLock(&PopKeepOnLock, &OldIrql);
    PopKeptOn |= (Flags & ~ES_CONTINUOUS);
    KeReleaseSpinLock(&PopKeepOnLock, OldIrql);
}

/* A thread's own asking goes when the thread does */
VOID
NTAPI
PopCleanupPowerState(
    _In_ PPOWER_STATE PowerState)
{
    UNREFERENCED_PARAMETER(PowerState);

    /* TODO: a thread's asking is not kept on the thread, so none is let go */
}

VOID
NTAPI
PopReleaseThreadPowerObject(
    _In_ PVOID PowerObject)
{
    PAGED_CODE();

    if (PowerObject != NULL) PoUnregisterSystemState(PowerObject);
}

BOOLEAN
NTAPI
PopIsDisplayRequired(VOID)
{
    return (PopKeptOn & ES_DISPLAY_REQUIRED) != 0;
}

VOID
NTAPI
PoSetUserPresent(
    _In_ ULONG Reason)
{
    PopUserPresentReason = Reason;
    PopUserPresentSequence++;
    KeQuerySystemTime(&PopUserPresentTime);
}

VOID
NTAPI
PoLatencySensitivityHint(
    _In_ ULONG Hint)
{
    PopLatencySensitivityState = Hint;
    PopLatencySensitivityTime = (ULONG)KeQueryInterruptTime();
}

VOID
NTAPI
PoNotifyVSyncChange(
    _In_ ULONG State)
{
    PopVSyncState = State;
}

VOID
NTAPI
PoNotifySystemTimeSet(VOID)
{
    /* TODO: nothing is woken or rescheduled when the clock is put right */
}

NTSTATUS
NTAPI
PopAddRemoveSysCapsCallback(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(NotificationStructure);
    UNREFERENCED_PARAMETER(Context);

    /* TODO: what the machine can do is not read from its devices */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PopSetPowerSettingValue(
    _In_ LPCGUID SettingGuid,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(SettingGuid);
    UNREFERENCED_PARAMETER(PowerCondition);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);

    /* TODO: no setting is kept, so nobody is told one changed */
    return STATUS_NOT_FOUND;
}

/*
 * Remembers which request brought the machine back, so that whoever asks can
 * tell a wake from an ordinary power request. No published flag in a request
 * says this, so the request is remembered by identity instead.
 */
VOID
NTAPI
PoSetSystemWake(
    _Inout_ PIRP Irp)
{
    /* TODO: only the last request to say so is remembered, so two wake
     * sources at once collapse into one */
    InterlockedExchangePointer((PVOID volatile *)&PopWakeRequest, Irp);
}

BOOLEAN
NTAPI
PoGetSystemWake(
    _In_ PIRP Irp)
{
    return (BOOLEAN)(PopWakeRequest == Irp);
}

/* How long a device still has to answer before it is given up on */
BOOLEAN
NTAPI
PoQueryWatchdogTime(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PULONG SecondsRemaining)
{
    UNREFERENCED_PARAMETER(Pdo);

    *SecondsRemaining = 0;

    /* TODO: no device is timed while it answers, so none is ever given up on */
    return FALSE;
}

NTSTATUS
NTAPI
PopApplyProcessorPowerSetting(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(SettingGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);

    /* TODO: a processor is not told to run differently by a setting */
    return STATUS_NOT_SUPPORTED;
}
