/*
 * PROJECT:     LiberNT Monitor Class Function Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Integrated panel brightness through the display miniport or ACPI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "monitor.h"
#include <windef.h>
#include <d3dkmddi.h>
#include <dispmprt.h>
#include <acpiioct.h>
#include <ntddvdeo.h>

#define NDEBUG
#include <debug.h>

#define MONITOR_BRIGHTNESS_LEVELS 101
#define MONITOR_ACPI_OUTPUT_SIZE 4096

static const GUID MonitorGuidBrightness =
    { 0xFDE5BBA4, 0xB3F9, 0x46FB, { 0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4 } };
static const GUID MonitorGuidBrightness2 =
    { 0x148A3C98, 0x0ECD, 0x465A, { 0xB6, 0x34, 0xB0, 0x5F, 0x19, 0x5F, 0x77, 0x39 } };
static const GUID MonitorGuidAcDcPowerSource =
    { 0x5D3E9A59, 0xE9D5, 0x4B00, { 0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48 } };
static const GUID MonitorGuidAcpiInterfaceStandard2 =
    { 0xE8695F63, 0x1831, 0x4870, { 0xA8, 0xCF, 0x9C, 0x2F, 0x03, 0xF9, 0xDC, 0xB5 } };

typedef enum _MONITOR_BRIGHTNESS_SOURCE
{
    MonitorBrightnessKmd,
    MonitorBrightnessAcpi
} MONITOR_BRIGHTNESS_SOURCE;

typedef struct _MONITOR_BRIGHTNESS
{
    KMUTEX Lock;
    MONITOR_BRIGHTNESS_SOURCE Source;
    DXGK_BRIGHTNESS_INTERFACE Interface;
    UCHAR Levels[MONITOR_BRIGHTNESS_LEVELS];
    ULONG LevelCount;
    UCHAR Current;
    UCHAR Ac;
    UCHAR Dc;
    BOOLEAN OnDc;
    PVOID PowerSourceHandle;
    ACPI_INTERFACE_STANDARD2 AcpiInterface;
    BOOLEAN AcpiNotifications;
} MONITOR_BRIGHTNESS;

typedef struct _MONITOR_HOTKEY_WORK
{
    PIO_WORKITEM WorkItem;
    ULONG NotifyCode;
} MONITOR_HOTKEY_WORK, *PMONITOR_HOTKEY_WORK;

static FAST_MUTEX MonitorLcdLock;
static PMONITOR_DEVICE_EXTENSION MonitorLcdOwner;
static PDEVICE_OBJECT MonitorLcdDevice;
static PKEVENT MonitorBrightnessEvent;
static HANDLE MonitorBrightnessEventHandle;

static
NTSTATUS
MonitorAcpiEval(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(InputSize) PVOID Input,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputSize)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, Input, InputSize,
                                        Output, OutputSize, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) && Output && Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
        Status = STATUS_ACPI_INVALID_DATA;
    return Status;
}

static
NTSTATUS
MonitorAcpiEvalIntegers(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ PCSTR Method,
    _Out_writes_(MaxValues) PULONG Values,
    _In_ ULONG MaxValues,
    _Out_ PULONG Count)
{
    ACPI_EVAL_INPUT_BUFFER Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    *Count = 0;
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlCopyMemory(Input.MethodName, Method, sizeof(Input.MethodName));

    Output = ExAllocatePoolZero(PagedPool, MONITOR_ACPI_OUTPUT_SIZE, MONITOR_TAG);
    if (!Output)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = MonitorAcpiEval(DeviceExtension, &Input, sizeof(Input), Output, MONITOR_ACPI_OUTPUT_SIZE);
    if (NT_SUCCESS(Status))
    {
        Argument = Output->Argument;
        End = (PUCHAR)Output + min(Output->Length, MONITOR_ACPI_OUTPUT_SIZE);
        for (Index = 0; Index < Output->Count && *Count < MaxValues; Index++)
        {
            if ((PUCHAR)Argument + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) > End ||
                (PUCHAR)ACPI_METHOD_NEXT_ARGUMENT(Argument) > End)
            {
                break;
            }
            if (Argument->Type == ACPI_METHOD_ARGUMENT_INTEGER && Argument->DataLength >= sizeof(ULONG))
                Values[(*Count)++] = Argument->Argument;
            Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
        }
        if (*Count == 0)
            Status = STATUS_ACPI_INVALID_DATA;
    }

    ExFreePoolWithTag(Output, MONITOR_TAG);
    return Status;
}

static
NTSTATUS
MonitorAcpiSetLevel(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Level)
{
    ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER Input;

    PAGED_CODE();

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER_SIGNATURE;
    RtlCopyMemory(Input.MethodName, "_BCM", sizeof(Input.MethodName));
    Input.IntegerArgument = Level;
    return MonitorAcpiEval(DeviceExtension, &Input, sizeof(Input), NULL, 0);
}

static
VOID
MonitorAddLevel(
    _Inout_ PMONITOR_BRIGHTNESS Brightness,
    _In_ ULONG Value)
{
    ULONG Index, Insert;

    if (Value > 100)
        return;
    for (Insert = 0; Insert < Brightness->LevelCount; Insert++)
    {
        if (Brightness->Levels[Insert] == Value)
            return;
        if (Brightness->Levels[Insert] > Value)
            break;
    }
    for (Index = Brightness->LevelCount; Index > Insert; Index--)
        Brightness->Levels[Index] = Brightness->Levels[Index - 1];
    Brightness->Levels[Insert] = (UCHAR)Value;
    Brightness->LevelCount++;
}

static
UCHAR
MonitorNearestLevel(
    _In_ PMONITOR_BRIGHTNESS Brightness,
    _In_ UCHAR Value)
{
    UCHAR Best = Brightness->Levels[0];
    ULONG Index;

    for (Index = 1; Index < Brightness->LevelCount; Index++)
    {
        if ((Brightness->Levels[Index] > Value ? Brightness->Levels[Index] - Value : Value - Brightness->Levels[Index]) <=
            (Best > Value ? Best - Value : Value - Best))
        {
            Best = Brightness->Levels[Index];
        }
    }
    return Best;
}

static
NTSTATUS
MonitorQueryInterface(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ const GUID *InterfaceType,
    _In_ USHORT Version,
    _Inout_updates_bytes_(Size) PINTERFACE Interface,
    _In_ USHORT Size)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    RtlZeroMemory(Interface, Size);
    Interface->Size = Size;
    Interface->Version = Version;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, DeviceExtension->LowerDevice, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = InterfaceType;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Version = Version;
    Stack->Parameters.QueryInterface.Interface = Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
MonitorQueryBrightnessInterface(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Out_ PDXGK_BRIGHTNESS_INTERFACE Interface)
{
    DXGK_BRIGHTNESS_INTERFACE_2 Interface2;
    NTSTATUS Status;

    PAGED_CODE();

    Status = MonitorQueryInterface(DeviceExtension, &MonitorGuidBrightness2, DXGK_BRIGHTNESS_INTERFACE_VERSION_2,
                                   (PINTERFACE)&Interface2, sizeof(Interface2));
    if (NT_SUCCESS(Status))
        RtlCopyMemory(Interface, &Interface2, sizeof(*Interface));
    else
        Status = MonitorQueryInterface(DeviceExtension, &MonitorGuidBrightness, DXGK_BRIGHTNESS_INTERFACE_VERSION_1,
                                       (PINTERFACE)Interface, sizeof(*Interface));
    if (NT_SUCCESS(Status) &&
        (!Interface->GetPossibleBrightness || !Interface->SetBrightness || !Interface->GetBrightness))
    {
        if (Interface->InterfaceDereference)
            Interface->InterfaceDereference(Interface->Context);
        Status = STATUS_NOT_SUPPORTED;
    }
    return Status;
}

static
BOOLEAN
MonitorBrightnessFromMiniport(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PMONITOR_BRIGHTNESS Brightness)
{
    UCHAR Levels[MONITOR_BRIGHTNESS_LEVELS];
    UCHAR Count = 0, Current = 0;
    ULONG Index;
    NTSTATUS Status;

    if (!NT_SUCCESS(MonitorQueryBrightnessInterface(DeviceExtension, &Brightness->Interface)))
        return FALSE;

    Status = Brightness->Interface.GetPossibleBrightness(Brightness->Interface.Context, sizeof(Levels), &Count, Levels);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < min(Count, sizeof(Levels)); Index++)
            MonitorAddLevel(Brightness, Levels[Index]);
    }
    if (!NT_SUCCESS(Status) || Brightness->LevelCount == 0)
    {
        if (Brightness->Interface.InterfaceDereference)
            Brightness->Interface.InterfaceDereference(Brightness->Interface.Context);
        RtlZeroMemory(&Brightness->Interface, sizeof(Brightness->Interface));
        Brightness->LevelCount = 0;
        return FALSE;
    }

    if (!NT_SUCCESS(Brightness->Interface.GetBrightness(Brightness->Interface.Context, &Current)))
        Current = Brightness->Levels[Brightness->LevelCount - 1];
    Brightness->Source = MonitorBrightnessKmd;
    Brightness->Current = (UCHAR)min(Current, 100);
    Brightness->Ac = Brightness->Current;
    Brightness->Dc = Brightness->Current;
    return TRUE;
}

static
BOOLEAN
MonitorBrightnessFromAcpi(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PMONITOR_BRIGHTNESS Brightness)
{
    ULONG Values[MONITOR_BRIGHTNESS_LEVELS + 2];
    ULONG Count, Index, Current;

    if (!NT_SUCCESS(MonitorAcpiEvalIntegers(DeviceExtension, "_BCL", Values, RTL_NUMBER_OF(Values), &Count)) || Count < 3)
        return FALSE;

    for (Index = 2; Index < Count; Index++)
        MonitorAddLevel(Brightness, Values[Index]);
    if (Brightness->LevelCount == 0)
        return FALSE;

    Brightness->Source = MonitorBrightnessAcpi;
    Brightness->Ac = (UCHAR)min(Values[0], 100);
    Brightness->Dc = (UCHAR)min(Values[1], 100);
    if (NT_SUCCESS(MonitorAcpiEvalIntegers(DeviceExtension, "_BQC", &Current, 1, &Count)))
    {
        Brightness->Current = (UCHAR)min(Current, 100);
        Brightness->Ac = Brightness->Current;
        Brightness->Dc = Brightness->Current;
    }
    else
    {
        Brightness->Current = Brightness->Ac;
        if (!NT_SUCCESS(MonitorAcpiSetLevel(DeviceExtension, MonitorNearestLevel(Brightness, Brightness->Current))))
            return FALSE;
    }
    return TRUE;
}

static
VOID
MonitorBrightnessEnsureEvent(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\" RXGK_MONITOR_BRIGHTNESS_EVENT);

    PAGED_CODE();

    ExAcquireFastMutex(&MonitorLcdLock);
    if (!MonitorBrightnessEvent)
        MonitorBrightnessEvent = IoCreateSynchronizationEvent(&Name, &MonitorBrightnessEventHandle);
    ExReleaseFastMutex(&MonitorLcdLock);
}

static
NTSTATUS
MonitorBrightnessApplyLocked(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PMONITOR_BRIGHTNESS Brightness,
    _In_ UCHAR Level,
    _Out_ PBOOLEAN Changed)
{
    NTSTATUS Status;

    *Changed = FALSE;
    if (Level == Brightness->Current)
        return STATUS_SUCCESS;

    if (Brightness->Source == MonitorBrightnessKmd)
        Status = Brightness->Interface.SetBrightness(Brightness->Interface.Context, MonitorNearestLevel(Brightness, Level));
    else
        Status = MonitorAcpiSetLevel(DeviceExtension, MonitorNearestLevel(Brightness, Level));
    if (NT_SUCCESS(Status))
    {
        Brightness->Current = Level;
        *Changed = TRUE;
    }
    return Status;
}

static
VOID
MonitorBrightnessChanged(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Level)
{
    MonitorBrightnessEnsureEvent();
    if (MonitorBrightnessEvent)
        KeSetEvent(MonitorBrightnessEvent, IO_NO_INCREMENT, FALSE);
    MonitorWmiBrightnessChanged(DeviceExtension, Level);
}

static
NTSTATUS
MonitorBrightnessUpdate(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN SetAc,
    _In_ UCHAR Ac,
    _In_ BOOLEAN SetDc,
    _In_ UCHAR Dc,
    _In_ BOOLEAN Apply)
{
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Changed = FALSE;
    UCHAR Current;

    PAGED_CODE();

    if (!Brightness)
        return STATUS_NOT_SUPPORTED;

    KeWaitForSingleObject(&Brightness->Lock, Executive, KernelMode, FALSE, NULL);
    if (SetAc)
        Brightness->Ac = min(Ac, 100);
    if (SetDc)
        Brightness->Dc = min(Dc, 100);
    if (Apply)
        Status = MonitorBrightnessApplyLocked(DeviceExtension, Brightness, Brightness->OnDc ? Brightness->Dc : Brightness->Ac,
                                              &Changed);
    Current = Brightness->Current;
    KeReleaseMutex(&Brightness->Lock, FALSE);

    if (Changed)
        MonitorBrightnessChanged(DeviceExtension, Current);
    return Status;
}

static
NTSTATUS
NTAPI
MonitorPowerSourceChanged(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = Context;
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;

    UNREFERENCED_PARAMETER(SettingGuid);

    if (!Brightness || ValueLength < sizeof(ULONG))
        return STATUS_SUCCESS;

    KeWaitForSingleObject(&Brightness->Lock, Executive, KernelMode, FALSE, NULL);
    Brightness->OnDc = (*(PULONG)Value != PoAc);
    KeReleaseMutex(&Brightness->Lock, FALSE);
    return MonitorBrightnessUpdate(DeviceExtension, FALSE, 0, FALSE, 0, TRUE);
}

static
VOID
MonitorBrightnessHotkey(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG NotifyCode)
{
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;
    UCHAR Current, Target, Next;
    ULONG Index;

    PAGED_CODE();

    if (!Brightness)
        return;

    KeWaitForSingleObject(&Brightness->Lock, Executive, KernelMode, FALSE, NULL);
    Current = Brightness->Current;
    Next = Brightness->Levels[0];
    if (NotifyCode == ACPI_NOTIFY_INC_BRIGHTNESS_HOTKEY || NotifyCode == ACPI_NOTIFY_CYCLE_BRIGHTNESS_HOTKEY)
    {
        Target = (UCHAR)min(((Current + 9) / 5) * 5, 100);
        Next = Brightness->Levels[Brightness->LevelCount - 1];
        for (Index = 0; Index < Brightness->LevelCount; Index++)
        {
            if (Brightness->Levels[Index] >= Target)
            {
                Next = Brightness->Levels[Index];
                break;
            }
        }
        if (NotifyCode == ACPI_NOTIFY_CYCLE_BRIGHTNESS_HOTKEY &&
            Current == Brightness->Levels[Brightness->LevelCount - 1])
        {
            Next = Brightness->Levels[0];
        }
    }
    else if (NotifyCode == ACPI_NOTIFY_DEC_BRIGHTNESS_HOTKEY)
    {
        Target = Current >= 5 ? (UCHAR)(((Current - 5) / 5) * 5) : 0;
        for (Index = Brightness->LevelCount; Index > 0; Index--)
        {
            if (Brightness->Levels[Index - 1] <= Target)
            {
                Next = Brightness->Levels[Index - 1];
                break;
            }
        }
    }
    KeReleaseMutex(&Brightness->Lock, FALSE);

    if (Next != Current)
        MonitorBrightnessSetCurrent(DeviceExtension, Next);
}

static
VOID
NTAPI
MonitorHotkeyWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PMONITOR_HOTKEY_WORK Work = Context;

    MonitorBrightnessHotkey(DeviceExtension, Work->NotifyCode);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Work);
    IoFreeWorkItem(Work->WorkItem);
    ExFreePoolWithTag(Work, MONITOR_TAG);
}

static
VOID
NTAPI
MonitorAcpiNotify(
    _In_ PVOID NotificationContext,
    _In_ ULONG NotifyCode)
{
    PMONITOR_DEVICE_EXTENSION DeviceExtension = NotificationContext;
    PMONITOR_HOTKEY_WORK Work;

    if (NotifyCode < ACPI_NOTIFY_CYCLE_BRIGHTNESS_HOTKEY || NotifyCode > ACPI_NOTIFY_ZERO_BRIGHTNESS_HOTKEY)
        return;

    Work = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Work), MONITOR_TAG);
    if (!Work)
        return;
    Work->NotifyCode = NotifyCode;
    Work->WorkItem = IoAllocateWorkItem(DeviceExtension->DeviceObject);
    if (!Work->WorkItem || !NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Work)))
    {
        if (Work->WorkItem)
            IoFreeWorkItem(Work->WorkItem);
        ExFreePoolWithTag(Work, MONITOR_TAG);
        return;
    }
    IoQueueWorkItem(Work->WorkItem, MonitorHotkeyWorker, DelayedWorkQueue, Work);
}

static
VOID
MonitorAcpiHotkeysStart(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PMONITOR_BRIGHTNESS Brightness)
{
    PAGED_CODE();

    if (!NT_SUCCESS(MonitorQueryInterface(DeviceExtension, &MonitorGuidAcpiInterfaceStandard2, 1,
                                          (PINTERFACE)&Brightness->AcpiInterface, sizeof(Brightness->AcpiInterface))))
    {
        RtlZeroMemory(&Brightness->AcpiInterface, sizeof(Brightness->AcpiInterface));
        return;
    }

    if (Brightness->AcpiInterface.RegisterForDeviceNotifications &&
        NT_SUCCESS(Brightness->AcpiInterface.RegisterForDeviceNotifications(Brightness->AcpiInterface.Context,
                                                                            MonitorAcpiNotify, DeviceExtension)))
    {
        Brightness->AcpiNotifications = TRUE;
        return;
    }

    if (Brightness->AcpiInterface.InterfaceDereference)
        Brightness->AcpiInterface.InterfaceDereference(Brightness->AcpiInterface.Context);
    RtlZeroMemory(&Brightness->AcpiInterface, sizeof(Brightness->AcpiInterface));
}

BOOLEAN
MonitorBrightnessSupported(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    return DeviceExtension->Brightness != NULL;
}

NTSTATUS
MonitorBrightnessQuery(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Out_ PUCHAR Current,
    _Out_writes_opt_(MaxLevels) PUCHAR Levels,
    _In_ ULONG MaxLevels,
    _Out_ PULONG LevelCount)
{
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;
    ULONG Index;

    PAGED_CODE();

    *Current = 0;
    *LevelCount = 0;
    if (!Brightness)
        return STATUS_NOT_SUPPORTED;

    KeWaitForSingleObject(&Brightness->Lock, Executive, KernelMode, FALSE, NULL);
    *Current = Brightness->Current;
    KeReleaseMutex(&Brightness->Lock, FALSE);
    *LevelCount = MONITOR_BRIGHTNESS_LEVELS;
    for (Index = 0; Levels && Index < min(MaxLevels, MONITOR_BRIGHTNESS_LEVELS); Index++)
        Levels[Index] = (UCHAR)Index;
    return STATUS_SUCCESS;
}

NTSTATUS
MonitorBrightnessSetCurrent(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Level)
{
    if (!DeviceExtension->Brightness)
        return STATUS_NOT_SUPPORTED;

    return MonitorBrightnessUpdate(DeviceExtension, TRUE, Level, TRUE, Level, TRUE);
}

NTSTATUS
MonitorBrightnessRevert(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    return MonitorBrightnessUpdate(DeviceExtension, FALSE, 0, FALSE, 0, TRUE);
}

NTSTATUS
MonitorBrightnessDeviceControl(
    _In_ PMONITOR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;
    PDISPLAY_BRIGHTNESS Display;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG Index;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    if (!Brightness)
        return STATUS_NOT_SUPPORTED;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS:
            if (OutputLength < MONITOR_BRIGHTNESS_LEVELS)
                return STATUS_BUFFER_TOO_SMALL;
            for (Index = 0; Index < MONITOR_BRIGHTNESS_LEVELS; Index++)
                ((PUCHAR)Irp->AssociatedIrp.SystemBuffer)[Index] = (UCHAR)Index;
            Irp->IoStatus.Information = MONITOR_BRIGHTNESS_LEVELS;
            return STATUS_SUCCESS;

        case IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS:
            if (OutputLength < sizeof(DISPLAY_BRIGHTNESS))
                return STATUS_BUFFER_TOO_SMALL;
            Display = Irp->AssociatedIrp.SystemBuffer;
            KeWaitForSingleObject(&Brightness->Lock, Executive, KernelMode, FALSE, NULL);
            Display->ucDisplayPolicy = DISPLAYPOLICY_DC;
            Display->ucACBrightness = Brightness->Current;
            Display->ucDCBrightness = Brightness->Current;
            KeReleaseMutex(&Brightness->Lock, FALSE);
            Irp->IoStatus.Information = sizeof(DISPLAY_BRIGHTNESS);
            return STATUS_SUCCESS;

        case IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS:
            if (InputLength < sizeof(DISPLAY_BRIGHTNESS))
                return STATUS_BUFFER_TOO_SMALL;
            Display = Irp->AssociatedIrp.SystemBuffer;
            if (!(Display->ucDisplayPolicy & DISPLAYPOLICY_BOTH) || (Display->ucDisplayPolicy & ~DISPLAYPOLICY_BOTH) ||
                Display->ucDCBrightness > 100)
            {
                return STATUS_INVALID_PARAMETER;
            }
            return MonitorBrightnessUpdate(DeviceExtension, TRUE, Display->ucDCBrightness, TRUE, Display->ucDCBrightness,
                                           TRUE);
    }
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
MonitorLcdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PMONITOR_DEVICE_EXTENSION Owner;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Stack->MajorFunction == IRP_MJ_CREATE)
        MonitorBrightnessEnsureEvent();

    if (Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
    {
        ExAcquireFastMutex(&MonitorLcdLock);
        Owner = MonitorLcdOwner;
        if (!Owner || !NT_SUCCESS(IoAcquireRemoveLock(&Owner->RemoveLock, Irp)))
            Owner = NULL;
        ExReleaseFastMutex(&MonitorLcdLock);

        if (!Owner)
        {
            Status = STATUS_DEVICE_DOES_NOT_EXIST;
            Irp->IoStatus.Information = 0;
        }
        else
        {
            Status = MonitorBrightnessDeviceControl(Owner, Irp);
            IoReleaseRemoveLock(&Owner->RemoveLock, Irp);
        }
    }
    else if (Stack->MajorFunction != IRP_MJ_CREATE && Stack->MajorFunction != IRP_MJ_CLOSE &&
             Stack->MajorFunction != IRP_MJ_CLEANUP)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

VOID
MonitorBrightnessStart(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\LCD");
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\DosDevices\\LCD");
    PRXGK_MONITOR_DESCRIPTOR Descriptor;
    ULONG Values[MONITOR_BRIGHTNESS_LEVELS + 2];
    PMONITOR_BRIGHTNESS Brightness;
    ULONG Technology;
    NTSTATUS Status;
    ULONG Count;

    PAGED_CODE();

    if (DeviceExtension->Brightness)
        return;
    if (!NT_SUCCESS(MonitorQueryDescriptor(DeviceExtension, &Descriptor)))
        return;
    Technology = Descriptor->VideoOutputTechnology;
    ExFreePoolWithTag(Descriptor, MONITOR_TAG);
    if (Technology != (ULONG)D3DKMDT_VOT_INTERNAL &&
        Technology != (ULONG)D3DKMDT_VOT_DISPLAYPORT_EMBEDDED &&
        Technology != (ULONG)D3DKMDT_VOT_UDI_EMBEDDED)
    {
        return;
    }

    Brightness = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Brightness), MONITOR_TAG);
    if (!Brightness)
        return;
    KeInitializeMutex(&Brightness->Lock, 0);

    if (!MonitorBrightnessFromMiniport(DeviceExtension, Brightness) &&
        !MonitorBrightnessFromAcpi(DeviceExtension, Brightness))
    {
        DPRINT("MONITOR: the integrated panel has no brightness control\n");
        ExFreePoolWithTag(Brightness, MONITOR_TAG);
        return;
    }

    DPRINT1("Integrated panel brightness %u%% through %s, %lu levels\n", Brightness->Current,
            Brightness->Source == MonitorBrightnessKmd ? "the display miniport" : "ACPI", Brightness->LevelCount);
    DeviceExtension->Brightness = Brightness;

    if (Brightness->Source == MonitorBrightnessKmd)
        (VOID)MonitorAcpiEvalIntegers(DeviceExtension, "_BCL", Values, RTL_NUMBER_OF(Values), &Count);
    MonitorAcpiHotkeysStart(DeviceExtension, Brightness);

    Status = PoRegisterPowerSettingCallback(DeviceExtension->DeviceObject, &MonitorGuidAcDcPowerSource,
                                            MonitorPowerSourceChanged, DeviceExtension, &Brightness->PowerSourceHandle);
    if (!NT_SUCCESS(Status))
        Brightness->PowerSourceHandle = NULL;

    ExAcquireFastMutex(&MonitorLcdLock);
    if (!MonitorLcdOwner)
    {
        if (!MonitorLcdDevice &&
            NT_SUCCESS(IoCreateDevice(DeviceExtension->DeviceObject->DriverObject, 0, &DeviceName, FILE_DEVICE_VIDEO,
                                      FILE_DEVICE_SECURE_OPEN, FALSE, &MonitorLcdDevice)))
        {
            if (NT_SUCCESS(IoCreateSymbolicLink(&LinkName, &DeviceName)))
            {
                MonitorLcdDevice->Flags |= DO_BUFFERED_IO;
                MonitorLcdDevice->Flags &= ~DO_DEVICE_INITIALIZING;
            }
            else
            {
                IoDeleteDevice(MonitorLcdDevice);
                MonitorLcdDevice = NULL;
            }
        }
        if (MonitorLcdDevice)
            MonitorLcdOwner = DeviceExtension;
    }
    ExReleaseFastMutex(&MonitorLcdLock);

    MonitorBrightnessChanged(DeviceExtension, Brightness->Current);
}

VOID
MonitorBrightnessStop(
    _Inout_ PMONITOR_DEVICE_EXTENSION DeviceExtension)
{
    PMONITOR_BRIGHTNESS Brightness = DeviceExtension->Brightness;

    PAGED_CODE();

    if (!Brightness)
        return;

    if (Brightness->PowerSourceHandle)
        PoUnregisterPowerSettingCallback(Brightness->PowerSourceHandle);
    if (Brightness->AcpiNotifications)
        Brightness->AcpiInterface.UnregisterForDeviceNotifications(Brightness->AcpiInterface.Context);
    if (Brightness->AcpiInterface.InterfaceDereference)
        Brightness->AcpiInterface.InterfaceDereference(Brightness->AcpiInterface.Context);

    ExAcquireFastMutex(&MonitorLcdLock);
    if (MonitorLcdOwner == DeviceExtension)
        MonitorLcdOwner = NULL;
    ExReleaseFastMutex(&MonitorLcdLock);

    DeviceExtension->Brightness = NULL;
    if (Brightness->Source == MonitorBrightnessKmd && Brightness->Interface.InterfaceDereference)
        Brightness->Interface.InterfaceDereference(Brightness->Interface.Context);
    ExFreePoolWithTag(Brightness, MONITOR_TAG);
}

VOID
MonitorBrightnessInitialize(VOID)
{
    ExInitializeFastMutex(&MonitorLcdLock);
}
