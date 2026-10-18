/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System sleep states
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _POP_SLEEP_CALL
{
    POWER_STATE_HANDLER Handler;
    LONG NumberProcessors;
    volatile LONG Number;
    NTSTATUS Status;
} POP_SLEEP_CALL, *PPOP_SLEEP_CALL;

typedef struct _POP_SLEEP_TRAVERSE
{
    SYSTEM_POWER_STATE SystemState;
    POWER_ACTION PowerAction;
    PDEVICE_OBJECT PowerDevice;
    ULONG Level;
    UCHAR MinorFunction;
    NTSTATUS Status;
    volatile LONG Pending;
    KEVENT Event;
} POP_SLEEP_TRAVERSE, *PPOP_SLEEP_TRAVERSE;

typedef struct _POP_SLEEP_IRP_CONTEXT
{
    PPOP_SLEEP_TRAVERSE Traverse;
    PDEVICE_NODE DeviceNode;
    PDEVICE_OBJECT TopDeviceObject;
} POP_SLEEP_IRP_CONTEXT, *PPOP_SLEEP_IRP_CONTEXT;

typedef struct _POP_SLEEP_DISABLE
{
    LIST_ENTRY Link;
    SYSTEM_POWER_STATE_DISABLE_REASON Reason;
    ULONG Information[2];
} POP_SLEEP_DISABLE, *PPOP_SLEEP_DISABLE;

#define POP_SLEEP_TAG 'lSoP'

static POWER_STATE_HANDLER PopStateHandlers[PowerStateMaximum];
static KGUARDED_MUTEX PopStateHandlerLock;
static volatile LONG PopSleepInProgress;
static LIST_ENTRY PopSleepDisableList;
static POP_SLEEP_DISABLE PopAoAcDisable;
volatile LONG64 PopLastSleepTime;
volatile LONG64 PopLastWakeTime;

static
VOID
PopSetAffectedStates(
    _Out_ PSYSTEM_POWER_STATE_DISABLE_REASON Reason,
    _In_ ULONG SleepMask)
{
    Reason->AffectedState[PoDisabledStateSleeping1] = (SleepMask & PO_REASON_STATE_S1) != 0;
    Reason->AffectedState[PoDisabledStateSleeping2] = (SleepMask & PO_REASON_STATE_S2) != 0;
    Reason->AffectedState[PoDisabledStateSleeping3] = (SleepMask & PO_REASON_STATE_S3) != 0;
    Reason->AffectedState[PoDisabledStateSleeping4] = (SleepMask & PO_REASON_STATE_S4) != 0;
    Reason->AffectedState[PoDisabledStateSleeping0Idle] = (SleepMask & PO_REASON_STATE_S0IDLE) != 0;
    Reason->AffectedState[PoDisabledStateSleeping4Firmware] = (SleepMask & PO_REASON_STATE_S4FIRM) != 0;
}

static
VOID
PopUpdateSleepCapabilities(VOID)
{
    BOOLEAN Disabled[POWER_STATE_DISABLED_TYPE_MAX] = {FALSE};
    PPOP_SLEEP_DISABLE Disable;
    PLIST_ENTRY Entry;
    ULONG i;

    for (Entry = PopSleepDisableList.Flink; Entry != &PopSleepDisableList; Entry = Entry->Flink)
    {
        Disable = CONTAINING_RECORD(Entry, POP_SLEEP_DISABLE, Link);
        for (i = 0; i < POWER_STATE_DISABLED_TYPE_MAX; i++)
            Disabled[i] |= Disable->Reason.AffectedState[i];
    }

    PopCapabilities.SystemS1 = PopStateHandlers[PowerStateSleeping1].Handler != NULL &&
                               !Disabled[PoDisabledStateSleeping1];
    PopCapabilities.SystemS2 = PopStateHandlers[PowerStateSleeping2].Handler != NULL &&
                               !Disabled[PoDisabledStateSleeping2];
    PopCapabilities.SystemS3 = PopStateHandlers[PowerStateSleeping3].Handler != NULL &&
                               !Disabled[PoDisabledStateSleeping3];
}

static
BOOLEAN
PopIsSleepStateEnabled(
    _In_ SYSTEM_POWER_STATE SystemState)
{
    switch (SystemState)
    {
        case PowerSystemSleeping1:
            return PopCapabilities.SystemS1;
        case PowerSystemSleeping2:
            return PopCapabilities.SystemS2;
        case PowerSystemSleeping3:
            return PopCapabilities.SystemS3;
        default:
            return FALSE;
    }
}

VOID
NTAPI
PopInitializeSleepSupport(VOID)
{
    KeInitializeGuardedMutex(&PopStateHandlerLock);
    InitializeListHead(&PopSleepDisableList);

    PopAoAcDisable.Reason.PowerReasonCode = SPSD_REASON_AOAC_NOBIOSSUPPORT;
    PopSetAffectedStates(&PopAoAcDisable.Reason, PO_REASON_STATE_S0IDLE);
    InsertTailList(&PopSleepDisableList, &PopAoAcDisable.Link);
}

NTSTATUS
NTAPI
PoDisableSleepStates(
    _In_ PO_SLEEP_DISABLE_REASON Reason,
    _In_ ULONG SleepMask,
    _Outptr_ PVOID *Token)
{
    PPOP_SLEEP_DISABLE Disable;

    PAGED_CODE();

    if ((ULONG)Reason >= PoSleepDisableReasonMax ||
        SleepMask == 0 ||
        (SleepMask & ~(PO_REASON_STATE_ALL | PO_REASON_STATE_S0IDLE)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Disable = ExAllocatePoolZero(PagedPool, sizeof(*Disable), POP_SLEEP_TAG);
    if (Disable == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Disable->Reason.PowerReasonCode = SPSD_REASON_INTERNALLYDISABLED;
    Disable->Reason.PowerReasonLength = sizeof(Disable->Information);
    Disable->Information[0] = Reason;
    Disable->Information[1] = SleepMask;
    PopSetAffectedStates(&Disable->Reason, SleepMask);

    KeAcquireGuardedMutex(&PopStateHandlerLock);
    InsertTailList(&PopSleepDisableList, &Disable->Link);
    PopUpdateSleepCapabilities();
    KeReleaseGuardedMutex(&PopStateHandlerLock);

    DPRINT1("Sleep states 0x%lx disabled, reason %u\n", SleepMask, Reason);
    *Token = Disable;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PoReenableSleepStates(
    _In_ PVOID Token)
{
    PPOP_SLEEP_DISABLE Disable = Token;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopStateHandlerLock);
    RemoveEntryList(&Disable->Link);
    PopUpdateSleepCapabilities();
    KeReleaseGuardedMutex(&PopStateHandlerLock);

    DPRINT1("Sleep states 0x%lx reenabled, reason %lu\n",
            Disable->Information[1], Disable->Information[0]);
    ExFreePoolWithTag(Disable, POP_SLEEP_TAG);
}

NTSTATUS
NTAPI
PopQuerySleepDisableReasons(
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    PPOP_SLEEP_DISABLE Disable;
    PLIST_ENTRY Entry;
    PUCHAR Snapshot;
    ULONG RecordLength;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PopStateHandlerLock);

    Length = sizeof(ULONG);
    for (Entry = PopSleepDisableList.Flink; Entry != &PopSleepDisableList; Entry = Entry->Flink)
    {
        Disable = CONTAINING_RECORD(Entry, POP_SLEEP_DISABLE, Link);
        Length += sizeof(Disable->Reason) + Disable->Reason.PowerReasonLength;
    }

    if (OutputBufferLength < Length)
    {
        KeReleaseGuardedMutex(&PopStateHandlerLock);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Snapshot = ExAllocatePoolWithTag(PagedPool, Length, POP_SLEEP_TAG);
    if (Snapshot == NULL)
    {
        KeReleaseGuardedMutex(&PopStateHandlerLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *(PULONG)Snapshot = Length;
    RecordLength = sizeof(ULONG);
    for (Entry = PopSleepDisableList.Flink; Entry != &PopSleepDisableList; Entry = Entry->Flink)
    {
        Disable = CONTAINING_RECORD(Entry, POP_SLEEP_DISABLE, Link);
        RtlCopyMemory(Snapshot + RecordLength, &Disable->Reason, sizeof(Disable->Reason));
        RecordLength += sizeof(Disable->Reason);
        RtlCopyMemory(Snapshot + RecordLength, Disable->Information, Disable->Reason.PowerReasonLength);
        RecordLength += Disable->Reason.PowerReasonLength;
    }

    KeReleaseGuardedMutex(&PopStateHandlerLock);

    _SEH2_TRY
    {
        RtlCopyMemory(OutputBuffer, Snapshot, Length);
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ExFreePoolWithTag(Snapshot, POP_SLEEP_TAG);
    return Status;
}

NTSTATUS
NTAPI
PopRegisterPowerStateHandler(
    _In_ PPOWER_STATE_HANDLER PowerStateHandler)
{
    SYSTEM_POWER_STATE SystemState = PowerSystemUnspecified;

    if ((ULONG)PowerStateHandler->Type >= PowerStateMaximum ||
        PowerStateHandler->Handler == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireGuardedMutex(&PopStateHandlerLock);

    PopStateHandlers[PowerStateHandler->Type] = *PowerStateHandler;

    switch (PowerStateHandler->Type)
    {
        case PowerStateSleeping1:
            SystemState = PowerSystemSleeping1;
            break;
        case PowerStateSleeping2:
            SystemState = PowerSystemSleeping2;
            break;
        case PowerStateSleeping3:
            SystemState = PowerSystemSleeping3;
            break;
        case PowerStateShutdownOff:
            PopCapabilities.SystemS5 = TRUE;
            break;
        default:
            break;
    }

    if (PowerStateHandler->RtcWake &&
        SystemState != PowerSystemUnspecified &&
        SystemState > PopCapabilities.RtcWake)
    {
        PopCapabilities.RtcWake = SystemState;
    }

    PopUpdateSleepCapabilities();
    KeReleaseGuardedMutex(&PopStateHandlerLock);

    DPRINT1("Power state handler %u registered\n", PowerStateHandler->Type);
    return STATUS_SUCCESS;
}

static
SYSTEM_POWER_STATE
PopChooseSleepState(
    _In_ SYSTEM_POWER_STATE LightestSystemState,
    _In_ ULONG Flags,
    _Out_ PPOWER_STATE_HANDLER Handler)
{
    SYSTEM_POWER_STATE State;
    SYSTEM_POWER_STATE Chosen = PowerSystemUnspecified;
    POWER_STATE_HANDLER_TYPE Type;

    if (LightestSystemState < PowerSystemSleeping1)
        LightestSystemState = PowerSystemSleeping1;
    if (LightestSystemState > PowerSystemSleeping3)
        return PowerSystemUnspecified;

    KeAcquireGuardedMutex(&PopStateHandlerLock);

    for (State = LightestSystemState; State <= PowerSystemSleeping3; State++)
    {
        Type = PowerStateSleeping1 + (State - PowerSystemSleeping1);
        if (!PopIsSleepStateEnabled(State))
            continue;

        Chosen = State;
        *Handler = PopStateHandlers[Type];
        if (Flags & POWER_ACTION_LIGHTEST_FIRST)
            break;
    }

    KeReleaseGuardedMutex(&PopStateHandlerLock);
    return Chosen;
}

static
BOOLEAN
PopSleepIsActiveNode(
    _In_ PDEVICE_NODE DeviceNode)
{
    if (DeviceNode == IopRootDeviceNode || (DeviceNode->Flags & DNF_LEGACY_DRIVER))
        return FALSE;

    switch (DeviceNode->State)
    {
        case DeviceNodeStartPostWork:
        case DeviceNodeStarted:
        case DeviceNodeQueryStopped:
        case DeviceNodeEnumeratePending:
        case DeviceNodeEnumerateCompletion:
            return TRUE;
        default:
            return FALSE;
    }
}

static
NTSTATUS
PopSleepDeepestLevelTraverse(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PULONG DeepestLevel = Context;

    if (DeviceNode->Level > *DeepestLevel)
        *DeepestLevel = DeviceNode->Level;

    return STATUS_SUCCESS;
}

static
VOID
PopSleepRecordStatus(
    _Inout_ PPOP_SLEEP_TRAVERSE Traverse,
    _In_ PDEVICE_NODE DeviceNode,
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_NOT_SUPPORTED || NT_SUCCESS(Status))
        return;

    DPRINT1("%wZ failed the S%u %s request: 0x%08lx\n",
            &DeviceNode->InstancePath,
            Traverse->SystemState - PowerSystemWorking,
            Traverse->MinorFunction == IRP_MN_QUERY_POWER ? "query" : "set",
            Status);
    InterlockedCompareExchange(&Traverse->Status, Status, STATUS_SUCCESS);
}

static IO_COMPLETION_ROUTINE PopSleepIrpCompletion;

static
NTSTATUS
NTAPI
PopSleepIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PPOP_SLEEP_IRP_CONTEXT IrpContext = Context;
    PPOP_SLEEP_TRAVERSE Traverse = IrpContext->Traverse;

    UNREFERENCED_PARAMETER(DeviceObject);

    PopSleepRecordStatus(Traverse, IrpContext->DeviceNode, Irp->IoStatus.Status);
    IoFreeIrp(Irp);
    ObDereferenceObject(IrpContext->TopDeviceObject);
    ExFreePoolWithTag(IrpContext, POP_SLEEP_TAG);

    if (InterlockedDecrement(&Traverse->Pending) == 0)
        KeSetEvent(&Traverse->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
PopSleepDeviceTraverse(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ PVOID Context)
{
    PPOP_SLEEP_TRAVERSE Traverse = Context;
    PPOP_SLEEP_IRP_CONTEXT IrpContext;
    PDEVICE_OBJECT TopDeviceObject;
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    if (DeviceNode->Level != Traverse->Level || !PopSleepIsActiveNode(DeviceNode))
        return STATUS_SUCCESS;

    TopDeviceObject = IoGetAttachedDeviceReference(DeviceNode->PhysicalDeviceObject);
    if (TopDeviceObject == Traverse->PowerDevice)
    {
        ObDereferenceObject(TopDeviceObject);
        return STATUS_SUCCESS;
    }

    IrpContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*IrpContext), POP_SLEEP_TAG);
    Irp = IrpContext ? IoAllocateIrp(TopDeviceObject->StackSize, FALSE) : NULL;
    if (Irp == NULL)
    {
        if (IrpContext)
            ExFreePoolWithTag(IrpContext, POP_SLEEP_TAG);
        ObDereferenceObject(TopDeviceObject);
        PopSleepRecordStatus(Traverse, DeviceNode, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_SUCCESS;
    }

    IrpContext->Traverse = Traverse;
    IrpContext->DeviceNode = DeviceNode;
    IrpContext->TopDeviceObject = TopDeviceObject;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_POWER;
    Stack->MinorFunction = Traverse->MinorFunction;
    Stack->Parameters.Power.Type = SystemPowerState;
    Stack->Parameters.Power.State.SystemState = Traverse->SystemState;
    Stack->Parameters.Power.ShutdownType = Traverse->PowerAction;
    IoSetCompletionRoutine(Irp, PopSleepIrpCompletion, IrpContext, TRUE, TRUE, TRUE);

    InterlockedIncrement(&Traverse->Pending);
    PoCallDriver(TopDeviceObject, Irp);
    return STATUS_SUCCESS;
}

static
VOID
PopSleepSendToLevel(
    _Inout_ PPOP_SLEEP_TRAVERSE Traverse,
    _In_ ULONG Level)
{
    DEVICETREE_TRAVERSE_CONTEXT Context;

    Traverse->Level = Level;
    Traverse->Pending = 1;
    KeClearEvent(&Traverse->Event);
    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     PopSleepDeviceTraverse,
                                     Traverse);
    IopTraverseDeviceTree(&Context);

    if (InterlockedDecrement(&Traverse->Pending) != 0)
        KeWaitForSingleObject(&Traverse->Event, Executive, KernelMode, FALSE, NULL);
}

static
NTSTATUS
PopSleepSendToPowerDevice(
    _In_ PPOP_SLEEP_TRAVERSE Traverse)
{
    NTSTATUS Status;

    if (Traverse->PowerDevice == NULL)
        return STATUS_SUCCESS;

    if (Traverse->MinorFunction == IRP_MN_QUERY_POWER)
        Status = PopSendQuerySystemPowerState(Traverse->PowerDevice, Traverse->SystemState, Traverse->PowerAction);
    else
        Status = PopSendSetSystemPowerState(Traverse->PowerDevice, Traverse->SystemState, Traverse->PowerAction);

    if (Status == STATUS_NOT_SUPPORTED)
        Status = STATUS_SUCCESS;

    if (!NT_SUCCESS(Status))
        DPRINT1("The system power device failed the S%u request: 0x%08lx\n",
                Traverse->SystemState - PowerSystemWorking, Status);

    return Status;
}

static
NTSTATUS
PopSleepSendDevices(
    _In_ PDEVICE_OBJECT PowerDevice,
    _In_ UCHAR MinorFunction,
    _In_ SYSTEM_POWER_STATE SystemState,
    _In_ POWER_ACTION PowerAction)
{
    POP_SLEEP_TRAVERSE Traverse;
    DEVICETREE_TRAVERSE_CONTEXT Context;
    ULONG DeepestLevel = 0;
    ULONG Level;
    NTSTATUS Status;

    Traverse.SystemState = SystemState;
    Traverse.PowerAction = PowerAction;
    Traverse.PowerDevice = PowerDevice;
    Traverse.MinorFunction = MinorFunction;
    Traverse.Status = STATUS_SUCCESS;
    KeInitializeEvent(&Traverse.Event, NotificationEvent, FALSE);

    IopInitDeviceTreeTraverseContext(&Context,
                                     IopRootDeviceNode,
                                     PopSleepDeepestLevelTraverse,
                                     &DeepestLevel);
    IopTraverseDeviceTree(&Context);

    if (SystemState == PowerSystemWorking)
    {
        Status = PopSleepSendToPowerDevice(&Traverse);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(Traverse.Status))
            Traverse.Status = Status;

        for (Level = 1; Level <= DeepestLevel; Level++)
            PopSleepSendToLevel(&Traverse, Level);

        return Traverse.Status;
    }

    for (Level = DeepestLevel; Level > 0; Level--)
        PopSleepSendToLevel(&Traverse, Level);

    if (!NT_SUCCESS(Traverse.Status))
        return Traverse.Status;

    return PopSleepSendToPowerDevice(&Traverse);
}

static
ULONG_PTR
NTAPI
PopSleepIpiWorker(
    _In_ ULONG_PTR Argument)
{
    PPOP_SLEEP_CALL Call = (PPOP_SLEEP_CALL)Argument;
    NTSTATUS Status;

    Status = Call->Handler.Handler(Call->Handler.Context,
                                   NULL,
                                   NULL,
                                   Call->NumberProcessors,
                                   &Call->Number);
    if (KeGetCurrentProcessorNumber() == 0)
        Call->Status = Status;

    return (ULONG_PTR)Status;
}

NTSTATUS
NTAPI
PopSleepSystem(
    _In_ POWER_ACTION SystemAction,
    _In_ SYSTEM_POWER_STATE LightestSystemState,
    _In_ ULONG Flags)
{
    POP_SLEEP_CALL Call;
    SYSTEM_POWER_STATE SystemState;
    PDEVICE_OBJECT PowerPdo;
    PDEVICE_OBJECT PowerDevice = NULL;
    NTSTATUS Status;

    if (SystemAction != PowerActionSleep)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Call, sizeof(Call));
    SystemState = PopChooseSleepState(LightestSystemState, Flags, &Call.Handler);
    if (SystemState == PowerSystemUnspecified)
    {
        DPRINT1("No sleep state at or below S%u is available\n",
                LightestSystemState - PowerSystemWorking);
        return STATUS_NOT_SUPPORTED;
    }

    if (InterlockedCompareExchange(&PopSleepInProgress, TRUE, FALSE) != FALSE)
        return STATUS_DEVICE_BUSY;

    DPRINT1("Entering sleep state S%u\n", SystemState - PowerSystemWorking);

    if (NT_SUCCESS(IopGetSystemPowerDeviceObject(&PowerPdo)))
    {
        PowerDevice = IoGetAttachedDeviceReference(PowerPdo);
        if (PowerDevice == PowerPdo)
        {
            ObDereferenceObject(PowerDevice);
            PowerDevice = NULL;
        }
    }

    ExNotifyCallback(PowerStateCallback, (PVOID)PO_CB_SYSTEM_STATE_LOCK, (PVOID)0);
    PopFlushVolumes(TRUE);

    if (Flags & POWER_ACTION_QUERY_ALLOWED)
    {
        Status = PopSleepSendDevices(PowerDevice, IRP_MN_QUERY_POWER, SystemState, SystemAction);
        if (!NT_SUCCESS(Status) && !(Flags & POWER_ACTION_CRITICAL))
        {
            DPRINT1("Sleep vetoed: 0x%08lx\n", Status);
            PopSleepSendDevices(PowerDevice, IRP_MN_SET_POWER, PowerSystemWorking, PowerActionNone);
            goto Done;
        }
    }

    Status = PopSleepSendDevices(PowerDevice, IRP_MN_SET_POWER, SystemState, SystemAction);
    if (NT_SUCCESS(Status))
    {
        Call.NumberProcessors = KeNumberProcessors;
        Call.Number = 0;
        Call.Status = STATUS_UNSUCCESSFUL;
        InterlockedExchange64(&PopLastSleepTime, (LONG64)KeQueryInterruptTime());
        KdSave(TRUE);
        KeIpiGenericCall(PopSleepIpiWorker, (ULONG_PTR)&Call);
        KdRestore(TRUE);
        Status = Call.Status;
        if (NT_SUCCESS(Status))
        {
            InterlockedExchange64(&PopLastWakeTime, (LONG64)KeQueryInterruptTime());
            DPRINT1("Resumed from sleep state S%u\n", SystemState - PowerSystemWorking);
        }
        else
            DPRINT1("The platform did not enter S%u: 0x%08lx\n",
                    SystemState - PowerSystemWorking, Status);
    }

    PopSleepSendDevices(PowerDevice, IRP_MN_SET_POWER, PowerSystemWorking, SystemAction);

    if (ExAcquireTimeRefreshLock(TRUE))
    {
        ExUpdateSystemTimeFromCmos(FALSE, 1);
        ExReleaseTimeRefreshLock();
    }

Done:
    ExNotifyCallback(PowerStateCallback, (PVOID)PO_CB_SYSTEM_STATE_LOCK, (PVOID)1);

    if (PowerDevice)
        ObDereferenceObject(PowerDevice);

    InterlockedExchange(&PopSleepInProgress, FALSE);
    return Status;
}
