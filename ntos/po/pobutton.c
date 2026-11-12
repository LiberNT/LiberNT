/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The system buttons and the lid
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define POP_BUTTON_TAG 'tBoP'

typedef struct _POP_BUTTON
{
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    WORK_QUEUE_ITEM WorkItem;
    NTSTATUS Status;
    ULONG Event;
} POP_BUTTON, *PPOP_BUTTON;

static PVOID PopButtonNotification;
static PVOID PopLidNotification;

static
VOID
PopListenToButton(
    _Inout_ PPOP_BUTTON Button);

static
NTSTATUS
PopQueryButtonCaps(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PULONG Caps)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Done;
    PIRP Irp;
    NTSTATUS Status;

    PAGED_CODE();

    *Caps = 0;
    KeInitializeEvent(&Done, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_GET_SYS_BUTTON_CAPS,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        Caps,
                                        sizeof(*Caps),
                                        FALSE,
                                        &Done,
                                        &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->FileObject = FileObject;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Done, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && IoStatus.Information < sizeof(*Caps))
        Status = STATUS_INVALID_DEVICE_REQUEST;

    return Status;
}

static
VOID
PopToggleDisplay(VOID)
{
    PopRequestDisplayState(PopGetDisplayState() == PowerMonitorOff ? PowerMonitorOn : PowerMonitorOff);
}

static
VOID
PopTakeButtonAction(
    _In_ LPCGUID Setting,
    _In_ POWER_ACTION Default)
{
    POWER_ACTION_POLICY Policy;
    POWER_ACTION Action = Default;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (PopReadPowerSetting(Setting, &Policy, sizeof(Policy)))
        Action = Policy.Action;

    DPRINT1("Button action %d\n", Action);

    switch (Action)
    {
        case PowerActionSleep:
            if (PopAoAc)
            {
                PopToggleDisplay();
                break;
            }
            Status = ZwInitiatePowerAction(PowerActionSleep, PowerSystemSleeping1, 0, TRUE);
            break;

        case PowerActionHibernate:
            Status = ZwInitiatePowerAction(PowerActionHibernate, PowerSystemHibernate, 0, TRUE);
            break;

        case PowerActionShutdown:
        case PowerActionShutdownReset:
        case PowerActionShutdownOff:
            Status = ZwInitiatePowerAction(Action,
                                           PowerSystemShutdown,
                                           POWER_ACTION_QUERY_ALLOWED | POWER_ACTION_UI_ALLOWED,
                                           TRUE);
            break;

        case PowerActionDisplayOff:
            PopToggleDisplay();
            break;

        default:
            break;
    }

    if (!NT_SUCCESS(Status))
        DPRINT1("Button action %d failed: 0x%08lx\n", Action, Status);
}

static
VOID
PopPublishLidState(
    _In_ ULONG Open)
{
    ULONG Condition;

    PAGED_CODE();

    for (Condition = PoAc; Condition < PoConditionMaximum; Condition++)
    {
        PopSetPowerSettingValue(&GUID_LIDSWITCH_STATE_CHANGE,
                                (SYSTEM_POWER_CONDITION)Condition,
                                &Open,
                                sizeof(Open));
    }
}

static
VOID
PopHandleButtonEvent(
    _In_ ULONG Event)
{
    PAGED_CODE();

    DPRINT1("System button event 0x%08lx\n", Event);

    if ((Event & SYS_BUTTON_LID) && (Event & SYS_BUTTON_LID_STATE_MASK) != 0)
    {
        PopPublishLidState((Event & SYS_BUTTON_LID_OPEN) ? 1 : 0);

        /* TODO: Windows 11's lid-close action without a GUID_LIDCLOSE_ACTION value is not sourced; nothing is done then */
        if ((Event & SYS_BUTTON_LID_CLOSED) && !(Event & SYS_BUTTON_LID_INITIAL) && !(Event & SYS_BUTTON_WAKE))
            PopTakeButtonAction(&GUID_LIDCLOSE_ACTION, PowerActionNone);
    }

    /* A press that woke the machine has done its work */
    if (Event & SYS_BUTTON_WAKE)
        return;

    if (Event & SYS_BUTTON_POWER)
        PopTakeButtonAction(&GUID_POWERBUTTON_ACTION, PowerActionShutdownOff);

    /* TODO: Windows 11's sleep button action without a GUID_SLEEPBUTTON_ACTION value is not sourced; nothing is done then */
    if (Event & SYS_BUTTON_SLEEP)
        PopTakeButtonAction(&GUID_SLEEPBUTTON_ACTION, PowerActionNone);
}

static
VOID
NTAPI
PopButtonWorker(
    _In_ PVOID Context)
{
    PPOP_BUTTON Button = Context;
    ULONG Event = Button->Event;

    PAGED_CODE();

    if (!NT_SUCCESS(Button->Status))
    {
        DPRINT1("A system button stopped answering: 0x%08lx\n", Button->Status);
        ObDereferenceObject(Button->FileObject);
        ExFreePoolWithTag(Button, POP_BUTTON_TAG);
        return;
    }

    PopListenToButton(Button);
    PopHandleButtonEvent(Event);
}

static IO_COMPLETION_ROUTINE PopButtonEventComplete;

static
NTSTATUS
NTAPI
PopButtonEventComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PPOP_BUTTON Button = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    Button->Status = Irp->IoStatus.Status;
    if (NT_SUCCESS(Button->Status) && Irp->IoStatus.Information < sizeof(ULONG))
        Button->Status = STATUS_INVALID_DEVICE_REQUEST;

    IoFreeIrp(Irp);
    ExQueueWorkItem(&Button->WorkItem, DelayedWorkQueue);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
PopListenToButton(
    _Inout_ PPOP_BUTTON Button)
{
    PIO_STACK_LOCATION Stack;
    PIRP Irp;

    Irp = IoAllocateIrp(Button->DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        Button->Status = STATUS_INSUFFICIENT_RESOURCES;
        ExQueueWorkItem(&Button->WorkItem, DelayedWorkQueue);
        return;
    }

    Button->Event = 0;
    Irp->RequestorMode = KernelMode;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->AssociatedIrp.SystemBuffer = &Button->Event;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->FileObject = Button->FileObject;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_GET_SYS_BUTTON_EVENT;
    Stack->Parameters.DeviceIoControl.InputBufferLength = 0;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(Button->Event);

    IoSetCompletionRoutine(Irp, PopButtonEventComplete, Button, TRUE, TRUE, TRUE);
    IoCallDriver(Button->DeviceObject, Irp);
}

static
VOID
PopWatchButton(
    _In_ PUNICODE_STRING SymbolicLinkName)
{
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PPOP_BUTTON Button;
    ULONG Caps;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoGetDeviceObjectPointer(SymbolicLinkName, FILE_READ_DATA, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%wZ not opened: 0x%08lx\n", SymbolicLinkName, Status);
        return;
    }

    Status = PopQueryButtonCaps(DeviceObject, FileObject, &Caps);
    if (!NT_SUCCESS(Status) || (Caps & (SYS_BUTTON_POWER | SYS_BUTTON_SLEEP | SYS_BUTTON_LID)) == 0)
    {
        DPRINT1("%wZ reported no button: 0x%08lx\n", SymbolicLinkName, Status);
        ObDereferenceObject(FileObject);
        return;
    }

    if (Caps & SYS_BUTTON_POWER)
        PopCapabilities.PowerButtonPresent = TRUE;
    if (Caps & SYS_BUTTON_SLEEP)
        PopCapabilities.SleepButtonPresent = TRUE;
    if (Caps & SYS_BUTTON_LID)
        PopCapabilities.LidPresent = TRUE;

    Button = ExAllocatePoolZero(NonPagedPool, sizeof(*Button), POP_BUTTON_TAG);
    if (Button == NULL)
    {
        ObDereferenceObject(FileObject);
        return;
    }

    Button->DeviceObject = DeviceObject;
    Button->FileObject = FileObject;
    ExInitializeWorkItem(&Button->WorkItem, PopButtonWorker, Button);

    DPRINT1("System button 0x%lx watched on %wZ\n", Caps, SymbolicLinkName);
    PopListenToButton(Button);
}

NTSTATUS
NTAPI
PopAddRemoveSysCapsCallback(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;

    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    /* A button that goes away fails its pending request, which ends its watch */
    /* TODO: a query to remove a watched button is not answered by letting its file object go */
    if (IsEqualGUIDAligned(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
        PopWatchButton(Notification->SymbolicLinkName);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PopInitializeButtons(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&GUID_DEVICE_SYS_BUTTON,
                                            IopRootDriverObject,
                                            PopAddRemoveSysCapsCallback,
                                            NULL,
                                            &PopButtonNotification);
    if (!NT_SUCCESS(Status))
        DPRINT1("System buttons not watched: 0x%08lx\n", Status);

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&GUID_DEVICE_LID,
                                            IopRootDriverObject,
                                            PopAddRemoveSysCapsCallback,
                                            NULL,
                                            &PopLidNotification);
    if (!NT_SUCCESS(Status))
        DPRINT1("The lid is not watched: 0x%08lx\n", Status);
}
