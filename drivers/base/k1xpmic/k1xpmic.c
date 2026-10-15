/*
 * PROJECT:     LiberNT SpacemiT K1 PMIC Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT P1 real-time clock and power key over the K1 I2C controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <initguid.h>
#include <poclass.h>
#include <ntstrsafe.h>
#include <spb.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>

#define NDEBUG
#include <debug.h>

#define K1XPMIC_TAG             'mPiK'

#define P1_RTC_SECOND           0x0D
#define P1_RTC_FIELDS           6
#define P1_RTC_CONTROL          0x1D
#define P1_RTC_CONTROL_ENABLE   (1U << 2)
#define P1_EVENT_STATUS         0x91
#define P1_EVENT_ENABLE         0x98
#define P1_EVENT_REGISTERS      7
#define P1_POWER_KEY_STATUS     0x97
#define P1_POWER_KEY_ENABLE     0x9E
#define P1_POWER_KEY_SHORT      (1U << 2)
#define P1_WRITE_MAXIMUM        16
#define K1XPMIC_OPEN_ATTEMPTS   50

typedef struct _K1XPMIC_COMMON
{
    BOOLEAN IsPowerButton;
    PDEVICE_OBJECT Self;
} K1XPMIC_COMMON, *PK1XPMIC_COMMON;

typedef struct _K1XPMIC_EXTENSION
{
    K1XPMIC_COMMON Common;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PFILE_OBJECT SpbFile;
    PDEVICE_OBJECT SpbDevice;
    BOOLEAN HasPmic;
    BOOLEAN ShutdownRegistered;
    BOOLEAN ButtonReported;
    PDEVICE_OBJECT Button;
    PKINTERRUPT Interrupt;
    KMUTEX Lock;
} K1XPMIC_EXTENSION, *PK1XPMIC_EXTENSION;

typedef struct _K1XPMIC_BUTTON_EXTENSION
{
    K1XPMIC_COMMON Common;
    PK1XPMIC_EXTENSION Parent;
    KSPIN_LOCK Lock;
    PIRP PendingIrp;
    ULONG PendingEvents;
    BOOLEAN Started;
    UNICODE_STRING InterfaceName;
} K1XPMIC_BUTTON_EXTENSION, *PK1XPMIC_BUTTON_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static NTSTATUS NTAPI
K1PmicSpbComplete(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
K1PmicSpbSequence(_In_ PK1XPMIC_EXTENSION Extension, _In_ PSPB_TRANSFER_LIST List, _In_ ULONG Length)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    if (!Extension->SpbDevice)
        return STATUS_DEVICE_NOT_READY;
    Irp = IoAllocateIrp(Extension->SpbDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->RequestorMode = KernelMode;
    Irp->AssociatedIrp.SystemBuffer = List;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->FileObject = Extension->SpbFile;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_SPB_EXECUTE_SEQUENCE;
    Stack->Parameters.DeviceIoControl.InputBufferLength = Length;
    IoSetCompletionRoutine(Irp, K1PmicSpbComplete, &Event, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(Extension->SpbDevice, Irp);
    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS
K1PmicAccess(_In_ PK1XPMIC_EXTENSION Extension, _In_ UCHAR Register,
             _Inout_updates_(Count) PUCHAR Data, _In_ ULONG Count, _In_ BOOLEAN Write)
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) Sequence;
    PSPB_TRANSFER_LIST_ENTRY Entries = Sequence.List.Transfers;
    UCHAR Buffer[1 + P1_WRITE_MAXIMUM];
    NTSTATUS Status;

    RtlZeroMemory(&Sequence, sizeof(Sequence));
    Sequence.List.Size = sizeof(SPB_TRANSFER_LIST);
    if (Write)
    {
        if (Count > P1_WRITE_MAXIMUM)
            return STATUS_INVALID_PARAMETER;
        Buffer[0] = Register;
        RtlCopyMemory(Buffer + 1, Data, Count);
        Sequence.List.TransferCount = 1;
        Entries[0] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(SpbTransferDirectionToDevice, 0, Buffer, 1 + Count);
    }
    else
    {
        Buffer[0] = Register;
        Sequence.List.TransferCount = 2;
        Entries[0] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(SpbTransferDirectionToDevice, 0, Buffer, 1);
        Entries[1] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(SpbTransferDirectionFromDevice, 0, Data, Count);
    }
    Status = K1PmicSpbSequence(Extension, &Sequence.List,
                               FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers) +
                               Sequence.List.TransferCount * sizeof(SPB_TRANSFER_LIST_ENTRY));
    if (!NT_SUCCESS(Status))
        DPRINT1("K1XPMIC: I2C %s of register 0x%02x failed 0x%08lx\n", Write ? "write" : "read", Register, Status);
    return Status;
}

static VOID
K1PmicLock(_In_ PK1XPMIC_EXTENSION Extension)
{
    KeWaitForSingleObject(&Extension->Lock, Executive, KernelMode, FALSE, NULL);
}

static VOID
K1PmicUnlock(_In_ PK1XPMIC_EXTENSION Extension)
{
    KeReleaseMutex(&Extension->Lock, FALSE);
}

static NTSTATUS
K1PmicReadTime(_In_ PK1XPMIC_EXTENSION Extension, _Out_ PLARGE_INTEGER Time)
{
    UCHAR First[P1_RTC_FIELDS], Second[P1_RTC_FIELDS];
    TIME_FIELDS Fields;
    ULONG Attempt;
    NTSTATUS Status;

    Status = K1PmicAccess(Extension, P1_RTC_SECOND, First, sizeof(First), FALSE);
    for (Attempt = 0; NT_SUCCESS(Status) && Attempt < 4; ++Attempt)
    {
        Status = K1PmicAccess(Extension, P1_RTC_SECOND, Second, sizeof(Second), FALSE);
        if (!NT_SUCCESS(Status) || RtlEqualMemory(First, Second, sizeof(First)))
            break;
        RtlCopyMemory(First, Second, sizeof(First));
    }
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Fields, sizeof(Fields));
    Fields.Second = First[0] & 0x3F;
    Fields.Minute = First[1] & 0x3F;
    Fields.Hour = First[2] & 0x1F;
    Fields.Day = (First[3] & 0x1F) + 1;
    Fields.Month = (First[4] & 0x0F) + 1;
    Fields.Year = 2000 + (First[5] & 0x3F);
    if (!RtlTimeFieldsToTime(&Fields, Time))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1PmicWriteTime(_In_ PK1XPMIC_EXTENSION Extension, _In_ PLARGE_INTEGER Time)
{
    TIME_FIELDS Fields;
    UCHAR Data[P1_RTC_FIELDS], Control;
    NTSTATUS Status;

    RtlTimeToTimeFields(Time, &Fields);
    if (Fields.Year < 2000 || Fields.Year > 2063)
        return STATUS_INVALID_PARAMETER;
    Data[0] = (UCHAR)Fields.Second;
    Data[1] = (UCHAR)Fields.Minute;
    Data[2] = (UCHAR)Fields.Hour;
    Data[3] = (UCHAR)(Fields.Day - 1);
    Data[4] = (UCHAR)(Fields.Month - 1);
    Data[5] = (UCHAR)(Fields.Year - 2000);

    Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Control &= ~P1_RTC_CONTROL_ENABLE;
    Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Extension, P1_RTC_SECOND, Data, sizeof(Data), TRUE);
    Control |= P1_RTC_CONTROL_ENABLE;
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    else
        (VOID)K1PmicAccess(Extension, P1_RTC_CONTROL, &Control, 1, TRUE);
    return Status;
}

static NTSTATUS
K1PmicCopyString(_In_reads_bytes_(Size) PCWSTR Source, _In_ SIZE_T Size, _Out_ PULONG_PTR Information)
{
    PWSTR Copy = ExAllocatePoolWithTag(PagedPool, Size, K1XPMIC_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static VOID
K1PmicCreateButton(_In_ PK1XPMIC_EXTENSION Extension)
{
    PK1XPMIC_BUTTON_EXTENSION Button;
    PDEVICE_OBJECT Pdo;

    if (!Extension->Interrupt || Extension->Button)
        return;
    if (!NT_SUCCESS(IoCreateDevice(Extension->Common.Self->DriverObject, sizeof(K1XPMIC_BUTTON_EXTENSION), NULL,
                                   FILE_DEVICE_UNKNOWN, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                                   FALSE, &Pdo)))
        return;
    Button = Pdo->DeviceExtension;
    RtlZeroMemory(Button, sizeof(*Button));
    Button->Common.IsPowerButton = TRUE;
    Button->Common.Self = Pdo;
    Button->Parent = Extension;
    KeInitializeSpinLock(&Button->Lock);
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    Extension->Button = Pdo;
    Extension->ButtonReported = TRUE;
    IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
}

static NTSTATUS
K1PmicOpenSpb(_In_ PK1XPMIC_EXTENSION Extension, _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Connection)
{
    WCHAR PathBuffer[RESOURCE_HUB_PATH_CHARS];
    UNICODE_STRING Path;
    LARGE_INTEGER Delay;
    ULONG Attempt;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Path, PathBuffer, sizeof(PathBuffer));
    Status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&Path, Connection->u.Connection.IdLowPart,
                                              Connection->u.Connection.IdHighPart);
    if (!NT_SUCCESS(Status))
        return Status;
    Delay.QuadPart = -100LL * 10000LL;
    for (Attempt = 0; Attempt < K1XPMIC_OPEN_ATTEMPTS; ++Attempt)
    {
        Status = IoGetDeviceObjectPointer(&Path, FILE_READ_DATA | FILE_WRITE_DATA,
                                          &Extension->SpbFile, &Extension->SpbDevice);
        if (NT_SUCCESS(Status) || (Status != STATUS_DEVICE_NOT_READY &&
                                   Status != STATUS_OBJECT_NAME_NOT_FOUND &&
                                   Status != STATUS_NO_SUCH_DEVICE))
            break;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    if (!NT_SUCCESS(Status))
    {
        Extension->SpbFile = NULL;
        Extension->SpbDevice = NULL;
    }
    return Status;
}

static NTSTATUS
K1PmicQuiesceEvents(_In_ PK1XPMIC_EXTENSION Extension)
{
    UCHAR Disable[P1_EVENT_REGISTERS], Clear[P1_EVENT_REGISTERS];
    NTSTATUS Status;

    RtlZeroMemory(Disable, sizeof(Disable));
    RtlFillMemory(Clear, sizeof(Clear), 0xFF);
    Status = K1PmicAccess(Extension, P1_EVENT_ENABLE, Disable, sizeof(Disable), TRUE);
    if (NT_SUCCESS(Status))
        Status = K1PmicAccess(Extension, P1_EVENT_STATUS, Clear, sizeof(Clear), TRUE);
    return Status;
}

static VOID
K1ButtonReport(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _In_ ULONG Events);

static BOOLEAN NTAPI
K1PmicIsr(_In_ PKINTERRUPT Interrupt, _In_ PVOID Context)
{
    PK1XPMIC_EXTENSION Extension = Context;
    PK1XPMIC_BUTTON_EXTENSION Button = NULL;
    UCHAR Status = 0;

    UNREFERENCED_PARAMETER(Interrupt);

    K1PmicLock(Extension);
    if (NT_SUCCESS(K1PmicAccess(Extension, P1_POWER_KEY_STATUS, &Status, 1, FALSE)) && Status)
        (VOID)K1PmicAccess(Extension, P1_POWER_KEY_STATUS, &Status, 1, TRUE);
    if (Extension->Button)
        Button = Extension->Button->DeviceExtension;
    K1PmicUnlock(Extension);

    DPRINT("K1XPMIC: power key events 0x%02x\n", Status);
    if ((Status & P1_POWER_KEY_SHORT) && Button && Button->Started)
        K1ButtonReport(Button, SYS_BUTTON_POWER);
    return TRUE;
}

static NTSTATUS
K1PmicStart(_In_ PK1XPMIC_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Connection = NULL, Interrupt = NULL;
    LARGE_INTEGER Time;
    ULONG Index;
    NTSTATUS Status;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeConnection && !Connection &&
            Descriptor->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            Descriptor->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
            Connection = Descriptor;
        else if (Descriptor->Type == CmResourceTypeInterrupt && !Interrupt)
            Interrupt = Descriptor;
    }
    if (!Connection)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = K1PmicOpenSpb(Extension, Connection);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("K1XPMIC: I2C connection unavailable 0x%08lx\n", Status);
        return Status;
    }
    Extension->HasPmic = TRUE;

    K1PmicLock(Extension);
    Status = K1PmicReadTime(Extension, &Time);
    K1PmicUnlock(Extension);
    if (NT_SUCCESS(Status))
    {
        TIME_FIELDS Fields;

        RtlTimeToTimeFields(&Time, &Fields);
        DPRINT("K1XPMIC: RTC %04u-%02u-%02u %02u:%02u:%02u UTC\n",
               Fields.Year, Fields.Month, Fields.Day, Fields.Hour, Fields.Minute, Fields.Second);
        ZwSetSystemTime(&Time, NULL);
    }
    else
    {
        DPRINT1("K1XPMIC: RTC read failed 0x%08lx\n", Status);
    }

    if (NT_SUCCESS(IoRegisterShutdownNotification(Extension->Common.Self)))
        Extension->ShutdownRegistered = TRUE;

    if (Interrupt)
    {
        K1PmicLock(Extension);
        Status = K1PmicQuiesceEvents(Extension);
        K1PmicUnlock(Extension);
        if (NT_SUCCESS(Status))
            Status = IoConnectInterrupt(&Extension->Interrupt, K1PmicIsr, Extension, NULL,
                                        Interrupt->u.Interrupt.Vector, PASSIVE_LEVEL, PASSIVE_LEVEL,
                                        LevelSensitive, Interrupt->ShareDisposition == CmResourceShareShared,
                                        Interrupt->u.Interrupt.Affinity, FALSE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("K1XPMIC: PMIC interrupt not connected 0x%08lx\n", Status);
            Extension->Interrupt = NULL;
        }
    }
    K1PmicCreateButton(Extension);
    return STATUS_SUCCESS;
}

static VOID
K1PmicStop(_In_ PK1XPMIC_EXTENSION Extension)
{
    if (Extension->ShutdownRegistered)
    {
        IoUnregisterShutdownNotification(Extension->Common.Self);
        Extension->ShutdownRegistered = FALSE;
    }
    if (Extension->Interrupt)
    {
        IoDisconnectInterrupt(Extension->Interrupt);
        Extension->Interrupt = NULL;
    }
    if (Extension->SpbFile)
    {
        ObDereferenceObject(Extension->SpbFile);
        Extension->SpbFile = NULL;
        Extension->SpbDevice = NULL;
    }
    Extension->HasPmic = FALSE;
}

static NTSTATUS
K1ButtonSetEnable(_In_ PK1XPMIC_EXTENSION Parent, _In_ UCHAR Enable)
{
    return K1PmicAccess(Parent, P1_POWER_KEY_ENABLE, &Enable, 1, TRUE);
}

static VOID
K1ButtonReport(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _In_ ULONG Events)
{
    PIRP Irp;
    KIRQL Irql;

    KeAcquireSpinLock(&Button->Lock, &Irql);
    Irp = Button->PendingIrp;
    if (Irp && IoSetCancelRoutine(Irp, NULL))
    {
        Button->PendingIrp = NULL;
        Events |= Button->PendingEvents;
        Button->PendingEvents = 0;
    }
    else
    {
        Irp = NULL;
        Button->PendingEvents |= Events;
    }
    KeReleaseSpinLock(&Button->Lock, Irql);

    if (Irp)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
        Irp->IoStatus.Information = sizeof(ULONG);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static NTSTATUS
K1ButtonStart(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _In_ PIO_STACK_LOCATION Stack)
{
    PK1XPMIC_EXTENSION Parent = Button->Parent;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Stack);
    if (!Parent || !Parent->HasPmic || !Parent->Interrupt)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    K1PmicLock(Parent);
    Button->Started = TRUE;
    Status = K1ButtonSetEnable(Parent, P1_POWER_KEY_SHORT);
    K1PmicUnlock(Parent);
    if (!NT_SUCCESS(Status))
    {
        Button->Started = FALSE;
        DPRINT1("K1XPMIC: power key start failed 0x%08lx\n", Status);
        return Status;
    }

    Status = IoRegisterDeviceInterface(Button->Common.Self, &GUID_DEVICE_SYS_BUTTON, NULL, &Button->InterfaceName);
    if (NT_SUCCESS(Status))
        Status = IoSetDeviceInterfaceState(&Button->InterfaceName, TRUE);
    return Status;
}

static VOID
K1ButtonStop(_In_ PK1XPMIC_BUTTON_EXTENSION Button)
{
    PK1XPMIC_EXTENSION Parent = Button->Parent;
    PIRP Irp = NULL;
    KIRQL Irql;

    if (Button->InterfaceName.Buffer)
    {
        (VOID)IoSetDeviceInterfaceState(&Button->InterfaceName, FALSE);
        RtlFreeUnicodeString(&Button->InterfaceName);
    }
    if (Button->Started && Parent)
    {
        K1PmicLock(Parent);
        Button->Started = FALSE;
        (VOID)K1ButtonSetEnable(Parent, 0);
        K1PmicUnlock(Parent);
    }
    KeAcquireSpinLock(&Button->Lock, &Irql);
    if (Button->PendingIrp && IoSetCancelRoutine(Button->PendingIrp, NULL))
    {
        Irp = Button->PendingIrp;
        Button->PendingIrp = NULL;
    }
    KeReleaseSpinLock(&Button->Lock, Irql);
    if (Irp)
    {
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static VOID NTAPI
K1ButtonCancel(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_BUTTON_EXTENSION Button = DeviceObject->DeviceExtension;
    KIRQL Irql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Button->Lock, &Irql);
    if (Button->PendingIrp == Irp)
        Button->PendingIrp = NULL;
    KeReleaseSpinLock(&Button->Lock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static NTSTATUS
K1ButtonDeviceControl(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Events;
    KIRQL Irql;
    NTSTATUS Status;

    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_GET_SYS_BUTTON_CAPS)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = SYS_BUTTON_POWER;
        Irp->IoStatus.Information = sizeof(ULONG);
        Status = STATUS_SUCCESS;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_GET_SYS_BUTTON_EVENT)
    {
        KeAcquireSpinLock(&Button->Lock, &Irql);
        Events = Button->PendingEvents;
        if (Events)
        {
            Button->PendingEvents = 0;
            KeReleaseSpinLock(&Button->Lock, Irql);
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
            Irp->IoStatus.Information = sizeof(ULONG);
            Status = STATUS_SUCCESS;
        }
        else if (Button->PendingIrp || !Button->Started)
        {
            KeReleaseSpinLock(&Button->Lock, Irql);
            Status = Button->Started ? STATUS_DEVICE_BUSY : STATUS_DEVICE_NOT_READY;
        }
        else
        {
            IoMarkIrpPending(Irp);
            IoSetCancelRoutine(Irp, K1ButtonCancel);
            if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
            {
                KeReleaseSpinLock(&Button->Lock, Irql);
                Irp->IoStatus.Status = STATUS_CANCELLED;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_PENDING;
            }
            Button->PendingIrp = Irp;
            KeReleaseSpinLock(&Button->Lock, Irql);
            return STATUS_PENDING;
        }
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS
K1ButtonPnp(_In_ PK1XPMIC_BUTTON_EXTENSION Button, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"K1XPMIC\\PowerButton";
                    Status = K1PmicCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"K1XPMIC\\PowerButton\0";
                    Status = K1PmicCopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR Id[] = L"0";
                    Status = K1PmicCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"SpacemiT P1 power button";
                Status = K1PmicCopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->RawDeviceOK = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), K1XPMIC_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = Button->Common.Self;
                ObReferenceObject(Button->Common.Self);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
            Status = K1ButtonStart(Button, Stack);
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            K1ButtonStop(Button);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            K1ButtonStop(Button);
            Status = STATUS_SUCCESS;
            if (!Button->Parent || !Button->Parent->ButtonReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Button->Parent)
                    Button->Parent->Button = NULL;
                IoDeleteDevice(Button->Common.Self);
                return Status;
            }
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
K1PmicDispatchShutdown(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    LARGE_INTEGER Time;

    if (!Extension->Common.IsPowerButton && Extension->HasPmic)
    {
        KeQuerySystemTime(&Time);
        K1PmicLock(Extension);
        (VOID)K1PmicWriteTime(Extension, &Time);
        K1PmicUnlock(Extension);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1PmicDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    if (Extension->Common.IsPowerButton)
        return K1ButtonPnp(DeviceObject->DeviceExtension, Irp);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = K1PmicStart(Extension, Stack);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Extension->Button && Extension->ButtonReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool,
                                                  FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  K1XPMIC_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Extension->Button;
                    ObReferenceObject(Extension->Button);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            Extension->ButtonReported = FALSE;
            if (Extension->Button)
            {
                PK1XPMIC_BUTTON_EXTENSION Button = Extension->Button->DeviceExtension;

                K1ButtonStop(Button);
                Button->Parent = NULL;
                IoDeleteDevice(Extension->Button);
                Extension->Button = NULL;
            }
            K1PmicStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Common.IsPowerButton)
    {
        Status = Irp->IoStatus.Status;
        if (IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_SET_POWER ||
            IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_POWER)
            Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (!Extension->Common.IsPowerButton)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1PmicDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Common.IsPowerButton)
        return K1ButtonDeviceControl(DeviceObject->DeviceExtension, Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XPMIC_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Extension->Common.IsPowerButton)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1PmicAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PK1XPMIC_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(K1XPMIC_EXTENSION), NULL, FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Common.Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    KeInitializeMutex(&Extension->Lock, 0);
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = K1PmicDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = K1PmicDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = K1PmicDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = K1PmicDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = K1PmicDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = K1PmicDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = K1PmicDispatchShutdown;
    DriverObject->DriverExtension->AddDevice = K1PmicAddDevice;
    return STATUS_SUCCESS;
}
