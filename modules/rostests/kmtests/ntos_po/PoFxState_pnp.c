/*
 * PROJECT:         LiberNT kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Owned PnP device for public power-framework tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <kmt_test.h>
#include "PoFxState_pnp.h"

typedef struct _TEST_PO_FX_EXTENSION
{
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    IO_REMOVE_LOCK RemoveLock;
    BOOLEAN Started;
} TEST_PO_FX_EXTENSION, *PTEST_PO_FX_EXTENSION;

static FAST_MUTEX DeviceLock;
static KEVENT StartedEvent;
static PDEVICE_OBJECT TestDevice;
static PDEVICE_OBJECT MainDevice;
static KMT_PNP_VETO_STATE VetoState;
static PDRIVER_DISPATCH PreviousDispatch[IRP_MJ_MAXIMUM_FUNCTION + 1];

/* A registration made while the device handles its start request */
static POHANDLE StartHandle;
static KEVENT StartCallbackEvent;
static volatile LONG StartIrpCompleted;
static volatile LONG StartCallbacksBefore;
static volatile LONG StartCallbacksAfter;
static KMT_PO_FX_START_STATE StartState;
static PDRIVER_DISPATCH volatile IrpHook;

static VOID StartCallbackSeen(VOID)
{
    if (StartIrpCompleted)
        InterlockedIncrement(&StartCallbacksAfter);
    else
        InterlockedIncrement(&StartCallbacksBefore);
    KeSetEvent(&StartCallbackEvent, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI StartActiveCallback(PVOID Context, ULONG Component)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Component);
    StartCallbackSeen();
}

static VOID NTAPI StartIdleConditionCallback(PVOID Context, ULONG Component)
{
    UNREFERENCED_PARAMETER(Context);
    StartCallbackSeen();
    PoFxCompleteIdleCondition(StartHandle, Component);
}

static VOID NTAPI StartIdleStateCallback(PVOID Context, ULONG Component, ULONG State)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(State);
    StartCallbackSeen();
    PoFxCompleteIdleState(StartHandle, Component);
}

static VOID NTAPI StartPowerRequiredCallback(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    StartCallbackSeen();
    PoFxReportDevicePoweredOn(StartHandle);
}

static VOID NTAPI StartPowerNotRequiredCallback(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    StartCallbackSeen();
    PoFxCompleteDevicePowerNotRequired(StartHandle);
}

static VOID StartRegister(PDEVICE_OBJECT Pdo)
{
    PO_FX_DEVICE Registration;
    PO_FX_COMPONENT_IDLE_STATE IdleState;

    KeClearEvent(&StartCallbackEvent);
    StartIrpCompleted = 0;
    StartCallbacksBefore = 0;
    StartCallbacksAfter = 0;

    RtlZeroMemory(&Registration, sizeof(Registration));
    RtlZeroMemory(&IdleState, sizeof(IdleState));
    Registration.Version = PO_FX_VERSION;
    Registration.ComponentCount = 1;
    Registration.Components[0].IdleStateCount = 1;
    Registration.Components[0].IdleStates = &IdleState;
    Registration.ComponentActiveConditionCallback = StartActiveCallback;
    Registration.ComponentIdleConditionCallback = StartIdleConditionCallback;
    Registration.ComponentIdleStateCallback = StartIdleStateCallback;
    Registration.DevicePowerRequiredCallback = StartPowerRequiredCallback;
    Registration.DevicePowerNotRequiredCallback = StartPowerNotRequiredCallback;

    StartState.RegisterStatus = PoFxRegisterDevice(Pdo, &Registration, &StartHandle);
    if (!NT_SUCCESS(StartState.RegisterStatus))
    {
        StartHandle = NULL;
        return;
    }

    PoFxStartDevicePowerManagement(StartHandle);
}

static VOID StartUnregister(VOID)
{
    if (StartHandle == NULL)
        return;

    PoFxUnregisterDevice(StartHandle);
    StartHandle = NULL;
}

static NTSTATUS NTAPI TestAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT Fdo;
    PTEST_PO_FX_EXTENSION Extension;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    Extension->Pdo = Pdo;
    Extension->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Extension->Lower)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    IoInitializeRemoveLock(&Extension->RemoveLock, 'xfpK', 0, 0);
    Fdo->Flags |= DO_POWER_PAGABLE;

    ExAcquireFastMutex(&DeviceLock);
    if (TestDevice)
    {
        ExReleaseFastMutex(&DeviceLock);
        IoDetachDevice(Extension->Lower);
        IoDeleteDevice(Fdo);
        return STATUS_DEVICE_BUSY;
    }
    KeClearEvent(&StartedEvent);
    RtlZeroMemory(&VetoState, sizeof(VetoState));
    TestDevice = Fdo;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    ExReleaseFastMutex(&DeviceLock);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI TestCompleteIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    PTEST_PO_FX_EXTENSION Extension = Context;
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS NTAPI TestDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PTEST_PO_FX_EXTENSION Extension;
    PDRIVER_DISPATCH Hook;
    NTSTATUS Status;
    POWER_STATE Power;

    if (DeviceObject == MainDevice)
    {
        KMT_PNP_VETO_STATE *State = Irp->AssociatedIrp.SystemBuffer;

        if (Stack->MajorFunction != IRP_MJ_DEVICE_CONTROL ||
            Stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_KMTEST_PNP_VETO)
            return PreviousDispatch[Stack->MajorFunction](DeviceObject, Irp);

        Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        if (Stack->Parameters.DeviceIoControl.InputBufferLength == sizeof(*State) &&
            Stack->Parameters.DeviceIoControl.OutputBufferLength == sizeof(*State) &&
            State->Veto <= 1)
        {
            ExAcquireFastMutex(&DeviceLock);
            VetoState.Veto = State->Veto;
            VetoState.Started = TestDevice && ((PTEST_PO_FX_EXTENSION)TestDevice->DeviceExtension)->Started;
            *State = VetoState;
            ExReleaseFastMutex(&DeviceLock);
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(*State);
        }
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* The devices other kernel tests create on the kmtest driver keep its own dispatch */
    if (DeviceObject != TestDevice)
        return PreviousDispatch[Stack->MajorFunction](DeviceObject, Irp);

    Hook = (PDRIVER_DISPATCH)InterlockedCompareExchangePointer((PVOID volatile *)&IrpHook, NULL, NULL);
    if (Hook != NULL &&
        (Stack->MajorFunction == IRP_MJ_CREATE || Stack->MajorFunction == IRP_MJ_CLEANUP ||
         Stack->MajorFunction == IRP_MJ_CLOSE || Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL))
    {
        return Hook(DeviceObject, Irp);
    }

    Extension = DeviceObject->DeviceExtension;
    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    if (Stack->MajorFunction == IRP_MJ_PNP)
    {
        switch (Stack->MinorFunction)
        {
            case IRP_MN_START_DEVICE:
                if (!IoForwardIrpSynchronously(Extension->Lower, Irp))
                    Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
                Status = Irp->IoStatus.Status;
                if (NT_SUCCESS(Status))
                {
                    StartRegister(Extension->Pdo);
                    Power.DeviceState = PowerDeviceD0;
                    PoSetPowerState(DeviceObject, DevicePowerState, Power);
                    ExAcquireFastMutex(&DeviceLock);
                    Extension->Started = TRUE;
                    KeSetEvent(&StartedEvent, IO_NO_INCREMENT, FALSE);
                    ExReleaseFastMutex(&DeviceLock);
                }
                IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
                InterlockedExchange(&StartIrpCompleted, 1);
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;

            case IRP_MN_QUERY_STOP_DEVICE:
            case IRP_MN_CANCEL_STOP_DEVICE:
                Irp->IoStatus.Status = STATUS_SUCCESS;
                break;

            case IRP_MN_QUERY_REMOVE_DEVICE:
                ExAcquireFastMutex(&DeviceLock);
                VetoState.Queries++;
                Status = VetoState.Veto ? STATUS_DEVICE_BUSY : STATUS_SUCCESS;
                ExReleaseFastMutex(&DeviceLock);
                Irp->IoStatus.Status = Status;
                if (!NT_SUCCESS(Status))
                {
                    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }
                break;

            case IRP_MN_CANCEL_REMOVE_DEVICE:
                ExAcquireFastMutex(&DeviceLock);
                VetoState.Cancels++;
                ExReleaseFastMutex(&DeviceLock);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                break;

            case IRP_MN_STOP_DEVICE:
            case IRP_MN_SURPRISE_REMOVAL:
                ExAcquireFastMutex(&DeviceLock);
                Extension->Started = FALSE;
                KeClearEvent(&StartedEvent);
                ExReleaseFastMutex(&DeviceLock);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                break;

            case IRP_MN_REMOVE_DEVICE:
            {
                PDEVICE_OBJECT Lower = Extension->Lower;

                StartUnregister();
                ExAcquireFastMutex(&DeviceLock);
                VetoState.Removes++;
                TestDevice = NULL;
                Extension->Started = FALSE;
                KeClearEvent(&StartedEvent);
                ExReleaseFastMutex(&DeviceLock);
                IoReleaseRemoveLockAndWait(&Extension->RemoveLock, Irp);
                Irp->IoStatus.Status = STATUS_SUCCESS;
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(Lower, Irp);
                IoDetachDevice(Lower);
                IoDeleteDevice(DeviceObject);
                return Status;
            }
        }
    }
    if (Stack->MajorFunction == IRP_MJ_POWER)
        PoStartNextPowerIrp(Irp);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, TestCompleteIrp, Extension, TRUE, TRUE, TRUE);
    if (Stack->MajorFunction == IRP_MJ_POWER)
        return PoCallDriver(Extension->Lower, Irp);
    return IoCallDriver(Extension->Lower, Irp);
}

VOID KmtPoFxInitializePnp(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT ControlDevice)
{
    ULONG Index;

    ExInitializeFastMutex(&DeviceLock);
    KeInitializeEvent(&StartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&StartCallbackEvent, NotificationEvent, FALSE);
    MainDevice = ControlDevice;
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        PreviousDispatch[Index] = DriverObject->MajorFunction[Index];
        DriverObject->MajorFunction[Index] = TestDispatch;
    }
    DriverObject->DriverExtension->AddDevice = TestAddDevice;
}

PDEVICE_OBJECT KmtPoFxAcquireDevice(PDEVICE_OBJECT *Pdo)
{
    PDEVICE_OBJECT Fdo = NULL;
    PTEST_PO_FX_EXTENSION Extension;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    *Pdo = NULL;
    Timeout.QuadPart = -100000000;
    Status = KeWaitForSingleObject(&StartedEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        return NULL;

    if (StartHandle != NULL)
    {
        Timeout.QuadPart = -10000LL * 2000;
        KeWaitForSingleObject(&StartCallbackEvent, Executive, KernelMode, FALSE, &Timeout);
        StartState.CallbacksBeforeStart = StartCallbacksBefore;
        StartState.CallbacksAfterStart = StartCallbacksAfter;
        StartUnregister();
    }

    ExAcquireFastMutex(&DeviceLock);
    if (TestDevice)
    {
        Extension = TestDevice->DeviceExtension;
        if (Extension->Started && NT_SUCCESS(IoAcquireRemoveLock(&Extension->RemoveLock, TestDevice)))
        {
            Fdo = TestDevice;
            *Pdo = Extension->Pdo;
        }
    }
    ExReleaseFastMutex(&DeviceLock);
    return Fdo;
}

VOID KmtPoFxGetStartState(PKMT_PO_FX_START_STATE State)
{
    *State = StartState;
}

VOID KmtPoFxReleaseDevice(PDEVICE_OBJECT Fdo)
{
    PTEST_PO_FX_EXTENSION Extension = Fdo->DeviceExtension;

    IoReleaseRemoveLock(&Extension->RemoveLock, Fdo);
}

VOID KmtPoFxSetIrpHook(PDRIVER_DISPATCH Hook)
{
    InterlockedExchangePointer((PVOID volatile *)&IrpHook, (PVOID)Hook);
}
