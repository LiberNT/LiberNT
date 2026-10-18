/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Consumer control collections opened for system use
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>
#define _HIDPI_NO_FUNCTION_MACROS_
#include <hidclass.h>
#include <hidpi.h>
#include <ntddvdeo.h>

DBG_DEFAULT_CHANNEL(UserInput);

#define HIDINPUT_USAGE_BRIGHTNESS_INCREMENT 0x006F
#define HIDINPUT_USAGE_BRIGHTNESS_DECREMENT 0x0070

typedef struct _HIDINPUT_ARRIVAL
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING Path;
} HIDINPUT_ARRIVAL, *PHIDINPUT_ARRIVAL;

typedef struct _HIDINPUT_DEVICE
{
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PHIDP_PREPARSED_DATA PreparsedData;
    PCHAR Report;
    ULONG ReportLength;
    ULONG UsageListLength;
    PUSAGE_AND_PAGE PreviousUsages;
    PUSAGE_AND_PAGE CurrentUsages;
    PUSAGE_AND_PAGE BreakUsages;
    PUSAGE_AND_PAGE MakeUsages;
    HIDP_KEYBOARD_MODIFIER_STATE ModifierState;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    UNICODE_STRING Path;
} HIDINPUT_DEVICE, *PHIDINPUT_DEVICE;

typedef struct _HIDINPUT_STATE
{
    KEVENT ArrivalEvent;
    FAST_MUTEX ArrivalLock;
    LIST_ENTRY Arrivals;
    PVOID NotificationEntry;
    KTIMER RepeatTimer;
    BOOLEAN RepeatArmed;
    BOOLEAN RepeatFired;
    PHIDINPUT_DEVICE RepeatDevice;
    USAGE_AND_PAGE RepeatUsage;
    HIDINPUT_DEVICE Devices[HIDINPUT_MAX_DEVICES];
} HIDINPUT_STATE, *PHIDINPUT_STATE;

static const GUID HidInputInterface =
    { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
static const GUID HidInputInterfaceArrival =
    { 0xCB3A4004, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };

static PDRIVER_OBJECT gHidInputDriverObject;
static PHIDINPUT_STATE gHidInput;

static
VOID
HidInputStopRepeat(
    _Inout_ PHIDINPUT_STATE State)
{
    KeCancelTimer(&State->RepeatTimer);
    State->RepeatArmed = FALSE;
    State->RepeatFired = FALSE;
    State->RepeatDevice = NULL;
    State->RepeatUsage.Usage = 0;
    State->RepeatUsage.UsagePage = 0;
}

static
VOID
HidInputArmRepeat(
    _Inout_ PHIDINPUT_STATE State,
    _In_ BOOLEAN First)
{
    LARGE_INTEGER DueTime;
    ULONG Delay, Rate;

    UserGetKeyboardTypematic(&Delay, &Rate);
    DueTime.QuadPart = First ? -(LONGLONG)Delay * 10000 : -10000000LL / max(Rate, 1);
    KeSetTimer(&State->RepeatTimer, DueTime, NULL);
    State->RepeatArmed = TRUE;
}

static
BOOLEAN
HidInputIsRetrigger(
    _In_ PUSAGE_AND_PAGE Usage)
{
    return Usage->UsagePage == HID_USAGE_PAGE_CONSUMER &&
           (Usage->Usage == 0x00E9 || Usage->Usage == 0x00EA ||
            Usage->Usage == HIDINPUT_USAGE_BRIGHTNESS_INCREMENT ||
            Usage->Usage == HIDINPUT_USAGE_BRIGHTNESS_DECREMENT);
}

static
NTSTATUS
NTAPI
HidInputInterfaceNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;
    PHIDINPUT_STATE State = Context;
    PHIDINPUT_ARRIVAL Arrival;
    USHORT Length;

    if (!IsEqualGUID(&Notification->Event, &HidInputInterfaceArrival))
        return STATUS_SUCCESS;

    Length = Notification->SymbolicLinkName->Length;
    Arrival = ExAllocatePoolWithTag(PagedPool, sizeof(*Arrival) + Length, USERTAG_HIDDESC);
    if (!Arrival)
        return STATUS_SUCCESS;

    Arrival->Path.Buffer = (PWCH)(Arrival + 1);
    Arrival->Path.Length = Length;
    Arrival->Path.MaximumLength = Length;
    RtlCopyMemory(Arrival->Path.Buffer, Notification->SymbolicLinkName->Buffer, Length);

    ExAcquireFastMutex(&State->ArrivalLock);
    InsertTailList(&State->Arrivals, &Arrival->ListEntry);
    ExReleaseFastMutex(&State->ArrivalLock);
    KeSetEvent(&State->ArrivalEvent, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidInputOpen(
    _In_ PUNICODE_STRING Path,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE Handle,
    _Out_ PFILE_OBJECT *FileObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    *Handle = NULL;
    *FileObject = NULL;
    InitializeObjectAttributes(&ObjectAttributes, Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(Handle, DesiredAccess, &ObjectAttributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    if (!NT_SUCCESS(Status))
    {
        *Handle = NULL;
        return Status;
    }

    Status = ObReferenceObjectByHandle(*Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(*Handle, KernelMode);
        *Handle = NULL;
        *FileObject = NULL;
    }
    return Status;
}

static
NTSTATUS
HidInputIoctl(
    _In_ HANDLE Handle,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Status = ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &Iosb, IoControlCode, NULL, 0, Buffer, Length);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&FileObject->Event, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    return Status;
}

static
VOID
HidInputClose(
    _Inout_ PHIDINPUT_DEVICE Device)
{
    if (gHidInput && gHidInput->RepeatDevice == Device)
        HidInputStopRepeat(gHidInput);
    if (Device->Handle)
    {
        if (Device->Status == STATUS_PENDING)
            (void)ZwCancelIoFile(Device->Handle, &Device->Iosb);
        ObCloseHandle(Device->Handle, KernelMode);
    }
    if (Device->FileObject)
    {
        if (Device->Status == STATUS_PENDING)
            KeWaitForSingleObject(&Device->FileObject->Event, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Device->FileObject);
    }
    if (Device->PreparsedData)
        ExFreePoolWithTag(Device->PreparsedData, USERTAG_HIDDESC);
    if (Device->Report)
        ExFreePoolWithTag(Device->Report, USERTAG_HIDDATA);
    if (Device->PreviousUsages)
        ExFreePoolWithTag(Device->PreviousUsages, USERTAG_HIDDATA);
    if (Device->Path.Buffer)
        ExFreePoolWithTag(Device->Path.Buffer, USERTAG_HIDDESC);
    RtlZeroMemory(Device, sizeof(*Device));
}

static
PHIDP_PREPARSED_DATA
HidInputQueryConsumerCollection(
    _In_ PUNICODE_STRING Path,
    _Out_ PHIDP_CAPS Caps)
{
    HID_COLLECTION_INFORMATION Information;
    PHIDP_PREPARSED_DATA PreparsedData = NULL;
    PFILE_OBJECT FileObject;
    HANDLE Handle;
    NTSTATUS Status;

    Status = HidInputOpen(Path, 0, &Handle, &FileObject);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = HidInputIoctl(Handle, FileObject, IOCTL_HID_GET_COLLECTION_INFORMATION, &Information, sizeof(Information));
    if (NT_SUCCESS(Status) && Information.DescriptorSize != 0)
    {
        PreparsedData = ExAllocatePoolWithTag(NonPagedPool, Information.DescriptorSize, USERTAG_HIDDESC);
        if (PreparsedData)
        {
            Status = HidInputIoctl(Handle, FileObject, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, PreparsedData,
                                   Information.DescriptorSize);
            if (!NT_SUCCESS(Status) ||
                HidP_GetCaps(PreparsedData, Caps) != HIDP_STATUS_SUCCESS ||
                Caps->UsagePage != HID_USAGE_PAGE_CONSUMER ||
                Caps->Usage != HID_USAGE_CONSUMERCTRL ||
                Caps->InputReportByteLength == 0)
            {
                ExFreePoolWithTag(PreparsedData, USERTAG_HIDDESC);
                PreparsedData = NULL;
            }
        }
    }

    ObCloseHandle(Handle, KernelMode);
    ObDereferenceObject(FileObject);
    return PreparsedData;
}

static
VOID
HidInputAttach(
    _Inout_ PHIDINPUT_STATE State,
    _In_ PUNICODE_STRING Path)
{
    PHIDINPUT_DEVICE Device = NULL;
    PHIDP_PREPARSED_DATA PreparsedData;
    HIDP_CAPS Caps;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < HIDINPUT_MAX_DEVICES; Index++)
    {
        if (State->Devices[Index].Handle)
        {
            if (RtlEqualUnicodeString(&State->Devices[Index].Path, Path, TRUE))
                return;
        }
        else if (!Device)
        {
            Device = &State->Devices[Index];
        }
    }
    if (!Device)
        return;

    PreparsedData = HidInputQueryConsumerCollection(Path, &Caps);
    if (!PreparsedData)
        return;

    Device->PreparsedData = PreparsedData;
    Device->ReportLength = Caps.InputReportByteLength;
    Device->UsageListLength = max(HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_UNDEFINED, PreparsedData), 1);
    Device->Report = ExAllocatePoolWithTag(NonPagedPool, Device->ReportLength, USERTAG_HIDDATA);
    Device->PreviousUsages = ExAllocatePoolWithTag(NonPagedPool,
                                                   4 * Device->UsageListLength * sizeof(USAGE_AND_PAGE),
                                                   USERTAG_HIDDATA);
    Device->Path.Buffer = ExAllocatePoolWithTag(PagedPool, Path->Length, USERTAG_HIDDESC);
    if (!Device->Report || !Device->PreviousUsages || !Device->Path.Buffer)
    {
        HidInputClose(Device);
        return;
    }

    RtlZeroMemory(Device->PreviousUsages, 4 * Device->UsageListLength * sizeof(USAGE_AND_PAGE));
    Device->CurrentUsages = Device->PreviousUsages + Device->UsageListLength;
    Device->BreakUsages = Device->CurrentUsages + Device->UsageListLength;
    Device->MakeUsages = Device->BreakUsages + Device->UsageListLength;
    RtlCopyMemory(Device->Path.Buffer, Path->Buffer, Path->Length);
    Device->Path.Length = Path->Length;
    Device->Path.MaximumLength = Path->Length;

    Status = HidInputOpen(Path, FILE_READ_DATA, &Device->Handle, &Device->FileObject);
    if (!NT_SUCCESS(Status))
    {
        HidInputClose(Device);
        return;
    }

    Device->Status = STATUS_SUCCESS;
    Device->Iosb.Information = 0;
    TRACE("Consumer control collection %wZ opened\n", Path);
}

static
BOOLEAN
NTAPI
HidInputInsertScanCodes(
    _In_opt_ PVOID Context,
    _In_reads_bytes_(Length) PCHAR NewScanCodes,
    _In_ ULONG Length)
{
    KEYBOARD_INPUT_DATA InputData;
    ULONG Index;
    CHAR Prefix = 0;

    UNREFERENCED_PARAMETER(Context);

    for (Index = 0; Index < Length; Index++)
    {
        if (NewScanCodes[Index] == (CHAR)0xE0 || NewScanCodes[Index] == (CHAR)0xE1)
        {
            Prefix = NewScanCodes[Index];
            continue;
        }

        RtlZeroMemory(&InputData, sizeof(InputData));
        if (NewScanCodes[Index] & 0x80)
            InputData.Flags |= KEY_BREAK;
        if (Prefix)
        {
            InputData.Flags |= (Prefix == (CHAR)0xE0) ? KEY_E0 : KEY_E1;
            Prefix = 0;
        }
        InputData.MakeCode = NewScanCodes[Index] & 0x7F;
        UserProcessKeyboardInput(&InputData);
    }
    return TRUE;
}

static
VOID
HidInputStepBrightness(
    _In_ BOOLEAN Increase)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\??\\LCD");
    OBJECT_ATTRIBUTES ObjectAttributes;
    DISPLAY_BRIGHTNESS Display;
    IO_STATUS_BLOCK Iosb;
    UCHAR Levels[101];
    UCHAR Current, Target, Next;
    ULONG Count, Index;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Handle, SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA, &ObjectAttributes, &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
        return;

    Status = ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &Iosb, IOCTL_VIDEO_QUERY_SUPPORTED_BRIGHTNESS, NULL, 0,
                                   Levels, sizeof(Levels));
    Count = NT_SUCCESS(Status) ? (ULONG)min(Iosb.Information, sizeof(Levels)) : 0;
    if (Count != 0)
    {
        Status = ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &Iosb, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, NULL, 0,
                                       &Display, sizeof(Display));
    }
    if (Count != 0 && NT_SUCCESS(Status))
    {
        Current = (Display.ucDisplayPolicy & DISPLAYPOLICY_DC) ? Display.ucDCBrightness : Display.ucACBrightness;
        if (Increase)
        {
            Target = (UCHAR)min(((Current + 9) / 5) * 5, 100);
            Next = Levels[Count - 1];
            for (Index = 0; Index < Count; Index++)
            {
                if (Levels[Index] >= Target)
                {
                    Next = Levels[Index];
                    break;
                }
            }
        }
        else
        {
            Target = Current >= 5 ? (UCHAR)(((Current - 5) / 5) * 5) : 0;
            Next = Levels[0];
            for (Index = Count; Index > 0; Index--)
            {
                if (Levels[Index - 1] <= Target)
                {
                    Next = Levels[Index - 1];
                    break;
                }
            }
        }

        if (Next != Current)
        {
            if (Display.ucDisplayPolicy & DISPLAYPOLICY_DC)
            {
                Display.ucDisplayPolicy = DISPLAYPOLICY_DC;
                Display.ucDCBrightness = Next;
            }
            else
            {
                Display.ucDisplayPolicy = DISPLAYPOLICY_AC;
                Display.ucACBrightness = Next;
            }
            (void)ZwDeviceIoControlFile(Handle, NULL, NULL, NULL, &Iosb, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS,
                                        &Display, sizeof(Display), NULL, 0);
        }
    }
    ZwClose(Handle);
}

static
VOID
HidInputTranslate(
    _Inout_ PHIDINPUT_DEVICE Device,
    _In_ PUSAGE_AND_PAGE Usages,
    _In_ HIDP_KEYBOARD_DIRECTION Direction,
    _Inout_ PLONG BrightnessSteps)
{
    ULONG Index;

    for (Index = 0; Index < Device->UsageListLength && Usages[Index].Usage; Index++)
    {
        if (Usages[Index].UsagePage == HID_USAGE_PAGE_CONSUMER &&
            (Usages[Index].Usage == HIDINPUT_USAGE_BRIGHTNESS_INCREMENT ||
             Usages[Index].Usage == HIDINPUT_USAGE_BRIGHTNESS_DECREMENT))
        {
            if (Direction == HidP_Keyboard_Make)
                *BrightnessSteps += (Usages[Index].Usage == HIDINPUT_USAGE_BRIGHTNESS_INCREMENT) ? 1 : -1;
            continue;
        }
        if (HidP_TranslateUsageAndPagesToI8042ScanCodes(&Usages[Index], 1, Direction, &Device->ModifierState,
                                                        HidInputInsertScanCodes, Device) != HIDP_STATUS_SUCCESS)
        {
            TRACE("No scan code for usage %x:%x\n", Usages[Index].UsagePage, Usages[Index].Usage);
        }
    }
}

static
VOID
HidInputUpdateRepeat(
    _Inout_ PHIDINPUT_STATE State,
    _In_ PHIDINPUT_DEVICE Device)
{
    PUSAGE_AND_PAGE Last = NULL;
    ULONG Index;

    for (Index = 0; Index < Device->UsageListLength && Device->BreakUsages[Index].Usage; Index++)
    {
        if (State->RepeatDevice == Device &&
            Device->BreakUsages[Index].Usage == State->RepeatUsage.Usage &&
            Device->BreakUsages[Index].UsagePage == State->RepeatUsage.UsagePage)
        {
            HidInputStopRepeat(State);
        }
    }

    for (Index = 0; Index < Device->UsageListLength && Device->MakeUsages[Index].Usage; Index++)
        Last = &Device->MakeUsages[Index];
    if (!Last)
        return;

    if (HidInputIsRetrigger(Last))
    {
        State->RepeatDevice = Device;
        State->RepeatUsage = *Last;
        State->RepeatFired = FALSE;
        HidInputArmRepeat(State, TRUE);
    }
    else
    {
        HidInputStopRepeat(State);
    }
}

static
VOID
HidInputRepeat(
    _Inout_ PHIDINPUT_STATE State)
{
    PHIDINPUT_DEVICE Device = State->RepeatDevice;
    USAGE_AND_PAGE Usage = State->RepeatUsage;

    if (!Device || !Device->Handle || !Usage.Usage)
        return;

    if (Usage.Usage == HIDINPUT_USAGE_BRIGHTNESS_INCREMENT || Usage.Usage == HIDINPUT_USAGE_BRIGHTNESS_DECREMENT)
    {
        HidInputStepBrightness(Usage.Usage == HIDINPUT_USAGE_BRIGHTNESS_INCREMENT);
    }
    else
    {
        UserEnterExclusive();
        if (HidP_TranslateUsageAndPagesToI8042ScanCodes(&Usage, 1, HidP_Keyboard_Make, &Device->ModifierState,
                                                        HidInputInsertScanCodes, Device) != HIDP_STATUS_SUCCESS)
        {
            TRACE("No scan code for usage %x:%x\n", Usage.UsagePage, Usage.Usage);
        }
        UserLeave();
    }
    HidInputArmRepeat(State, FALSE);
}

static
BOOLEAN
HidInputDispatchReport(
    _Inout_ PHIDINPUT_STATE State,
    _Inout_ PHIDINPUT_DEVICE Device)
{
    ULONG Length = Device->UsageListLength;
    LONG BrightnessSteps = 0;

    if (Device->Iosb.Information != Device->ReportLength)
        return FALSE;

    if (HidP_GetUsagesEx(HidP_Input, HIDP_LINK_COLLECTION_UNSPECIFIED, Device->CurrentUsages, &Length,
                         Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_UsageAndPageListDifference(Device->PreviousUsages, Device->CurrentUsages, Device->BreakUsages,
                                        Device->MakeUsages, Device->UsageListLength) != HIDP_STATUS_SUCCESS)
    {
        return FALSE;
    }

    RtlCopyMemory(Device->PreviousUsages, Device->CurrentUsages, Device->UsageListLength * sizeof(USAGE_AND_PAGE));
    if (!Device->BreakUsages[0].Usage && !Device->MakeUsages[0].Usage)
        return FALSE;

    HidInputUpdateRepeat(State, Device);

    UserEnterExclusive();
    HidInputTranslate(Device, Device->BreakUsages, HidP_Keyboard_Break, &BrightnessSteps);
    HidInputTranslate(Device, Device->MakeUsages, HidP_Keyboard_Make, &BrightnessSteps);
    UserLeave();

    for (; BrightnessSteps > 0; BrightnessSteps--)
        HidInputStepBrightness(TRUE);
    for (; BrightnessSteps < 0; BrightnessSteps++)
        HidInputStepBrightness(FALSE);
    return TRUE;
}

NTSTATUS
NTAPI
InitHidInputImpl(
    _In_ PDRIVER_OBJECT DriverObject)
{
    gHidInputDriverObject = DriverObject;
    return STATUS_SUCCESS;
}

VOID
NTAPI
HidInputStart(VOID)
{
    PHIDINPUT_STATE State;
    NTSTATUS Status;

    State = ExAllocatePoolZero(NonPagedPool, sizeof(*State), USERTAG_HIDDATA);
    if (!State)
        return;

    KeInitializeEvent(&State->ArrivalEvent, SynchronizationEvent, FALSE);
    KeInitializeTimer(&State->RepeatTimer);
    ExInitializeFastMutex(&State->ArrivalLock);
    InitializeListHead(&State->Arrivals);
    gHidInput = State;

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&HidInputInterface,
                                            gHidInputDriverObject,
                                            HidInputInterfaceNotification,
                                            State,
                                            &State->NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        ERR("HID interface notification not registered: 0x%08lx\n", Status);
        State->NotificationEntry = NULL;
    }
}

VOID
NTAPI
HidInputStop(VOID)
{
    PHIDINPUT_STATE State = gHidInput;
    PLIST_ENTRY Entry;
    ULONG Index;

    if (!State)
        return;

    if (State->NotificationEntry)
        IoUnregisterPlugPlayNotification(State->NotificationEntry);
    HidInputStopRepeat(State);

    for (Index = 0; Index < HIDINPUT_MAX_DEVICES; Index++)
    {
        if (State->Devices[Index].Handle)
            HidInputClose(&State->Devices[Index]);
    }

    while (!IsListEmpty(&State->Arrivals))
    {
        Entry = RemoveHeadList(&State->Arrivals);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, HIDINPUT_ARRIVAL, ListEntry), USERTAG_HIDDESC);
    }

    gHidInput = NULL;
    ExFreePoolWithTag(State, USERTAG_HIDDATA);
}

ULONG
NTAPI
HidInputPrepareWait(
    _Inout_ PVOID *WaitObjects,
    _Inout_ PULONG WaitCount)
{
    PHIDINPUT_STATE State = gHidInput;
    PHIDINPUT_DEVICE Device;
    LARGE_INTEGER ByteOffset;
    ULONG Expected = 1;
    ULONG Index;

    if (!State)
        return 0;

    WaitObjects[(*WaitCount)++] = &State->ArrivalEvent;
    if (State->RepeatArmed)
    {
        WaitObjects[(*WaitCount)++] = &State->RepeatTimer;
        Expected++;
    }
    ByteOffset.QuadPart = 0;

    for (Index = 0; Index < HIDINPUT_MAX_DEVICES; Index++)
    {
        Device = &State->Devices[Index];
        if (!Device->Handle)
            continue;

        Expected++;
        if (Device->Status != STATUS_PENDING)
        {
            Device->Status = ZwReadFile(Device->Handle, NULL, NULL, NULL, &Device->Iosb, Device->Report,
                                        Device->ReportLength, &ByteOffset, NULL);
            if (Device->Status != STATUS_PENDING && NT_SUCCESS(Device->Status))
                Device->Status = Device->Iosb.Status;
        }
        if (Device->Status == STATUS_PENDING)
            WaitObjects[(*WaitCount)++] = &Device->FileObject->Event;
    }
    return Expected;
}

BOOLEAN
NTAPI
HidInputSignaled(
    _In_ PVOID Object)
{
    PHIDINPUT_STATE State = gHidInput;
    PHIDINPUT_DEVICE Device;
    ULONG Index;

    if (!State)
        return FALSE;
    if (Object == &State->ArrivalEvent)
        return TRUE;
    if (Object == &State->RepeatTimer)
    {
        State->RepeatArmed = FALSE;
        State->RepeatFired = TRUE;
        return TRUE;
    }

    for (Index = 0; Index < HIDINPUT_MAX_DEVICES; Index++)
    {
        Device = &State->Devices[Index];
        if (Device->Handle && Device->Status == STATUS_PENDING && Object == &Device->FileObject->Event)
        {
            Device->Status = Device->Iosb.Status;
            return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN
NTAPI
HidInputProcess(VOID)
{
    PHIDINPUT_STATE State = gHidInput;
    PHIDINPUT_DEVICE Device;
    PHIDINPUT_ARRIVAL Arrival;
    PLIST_ENTRY Entry;
    BOOLEAN Dispatched = FALSE;
    ULONG Index;

    if (!State)
        return FALSE;

    for (;;)
    {
        ExAcquireFastMutex(&State->ArrivalLock);
        Entry = IsListEmpty(&State->Arrivals) ? NULL : RemoveHeadList(&State->Arrivals);
        ExReleaseFastMutex(&State->ArrivalLock);
        if (!Entry)
            break;

        Arrival = CONTAINING_RECORD(Entry, HIDINPUT_ARRIVAL, ListEntry);
        HidInputAttach(State, &Arrival->Path);
        ExFreePoolWithTag(Arrival, USERTAG_HIDDESC);
    }

    for (Index = 0; Index < HIDINPUT_MAX_DEVICES; Index++)
    {
        Device = &State->Devices[Index];
        if (!Device->Handle || Device->Status == STATUS_PENDING)
            continue;

        if (NT_SUCCESS(Device->Status))
        {
            if (HidInputDispatchReport(State, Device))
                Dispatched = TRUE;
        }
        else
        {
            TRACE("Consumer control collection %wZ closed: 0x%08lx\n", &Device->Path, Device->Status);
            HidInputClose(Device);
        }
    }

    if (State->RepeatFired)
    {
        State->RepeatFired = FALSE;
        HidInputRepeat(State);
        Dispatched = TRUE;
    }
    return Dispatched;
}
