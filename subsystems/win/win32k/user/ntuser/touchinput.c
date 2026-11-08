/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Touch screen digitizer collections read as touch pointer input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>
#define _HIDPI_NO_FUNCTION_MACROS_
#include <hidclass.h>
#include <hidpi.h>
#include <loader_framebuffer.h>

DBG_DEFAULT_CHANNEL(UserInput);

#define TOUCHINPUT_USAGE_CERTIFICATION_STATUS 0x00C5
#define TOUCHINPUT_COORDINATE_MAX 0xFFFF
#define TOUCHINPUT_MAX_FEATURE_CAPS 8
#define TOUCHINPUT_ROTATION_UNKNOWN MAXULONG

typedef BOOLEAN (NTAPI *PTOUCHINPUT_GET_GOP_TRANSFORM)(PLOADER_PARAMETER_FRAMEBUFFER_TRANSFORM Transform);

static ULONG TouchInputPanelRotation = TOUCHINPUT_ROTATION_UNKNOWN;

typedef struct _TOUCHINPUT_ARRIVAL
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING Path;
} TOUCHINPUT_ARRIVAL, *PTOUCHINPUT_ARRIVAL;

typedef struct _TOUCHINPUT_DEVICE
{
    HANDLE Handle;
    PFILE_OBJECT FileObject;
    PHIDP_PREPARSED_DATA PreparsedData;
    HIDP_CAPS Caps;
    PCHAR Report;
    ULONG ReportLength;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;
    UNICODE_STRING Path;
    PUSAGE UsageList;
    ULONG UsageListLength;
    PUSHORT FingerCollections;
    ULONG FingerCount;
    ULONG Capacity;
    BOOLEAN HasContactCount;
    BOOLEAN HasConfidence;
    LONG MinimumX;
    LONG RangeX;
    LONG MinimumY;
    LONG RangeY;
    ULONG FrameExpected;
    ULONG FrameReceived;
    ULONG FrameCount;
    PTOUCH_CONTACT Frame;
    struct _POINTER_TOUCH_SOURCE *Source;
} TOUCHINPUT_DEVICE, *PTOUCHINPUT_DEVICE;

typedef struct _TOUCHINPUT_STATE
{
    KEVENT ArrivalEvent;
    FAST_MUTEX ArrivalLock;
    LIST_ENTRY Arrivals;
    PVOID NotificationEntry;
    TOUCHINPUT_DEVICE Devices[TOUCHINPUT_MAX_DEVICES];
} TOUCHINPUT_STATE, *PTOUCHINPUT_STATE;

static const GUID TouchInputInterface =
    { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
static const GUID TouchInputInterfaceArrival =
    { 0xCB3A4004, 0x46F0, 0x11D0, { 0xB0, 0x8F, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3F } };

static PDRIVER_OBJECT gTouchInputDriverObject;
static PTOUCHINPUT_STATE gTouchInput;

static
NTSTATUS
NTAPI
TouchInputInterfaceNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;
    PTOUCHINPUT_STATE State = Context;
    PTOUCHINPUT_ARRIVAL Arrival;
    USHORT Length;

    if (!IsEqualGUID(&Notification->Event, &TouchInputInterfaceArrival))
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
TouchInputIoctl(
    _In_ PTOUCHINPUT_DEVICE Device,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    Status = ZwDeviceIoControlFile(Device->Handle, NULL, NULL, NULL, &Iosb, IoControlCode, NULL, 0, Buffer, Length);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Device->FileObject->Event, Executive, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }
    return Status;
}

static
BOOLEAN
TouchInputGetFeature(
    _In_ PTOUCHINPUT_DEVICE Device,
    _In_ USAGE UsagePage,
    _In_ USAGE Usage,
    _Out_opt_ PULONG Value)
{
    HIDP_VALUE_CAPS ValueCaps[TOUCHINPUT_MAX_FEATURE_CAPS];
    USHORT Count = RTL_NUMBER_OF(ValueCaps);
    ULONG Length = Device->Caps.FeatureReportByteLength;
    BOOLEAN Result = FALSE;
    PCHAR Report;

    if (!Length ||
        HidP_GetSpecificValueCaps(HidP_Feature, UsagePage, 0, Usage, ValueCaps, &Count, Device->PreparsedData) != HIDP_STATUS_SUCCESS ||
        !Count)
    {
        return FALSE;
    }

    Report = ExAllocatePoolZero(NonPagedPool, Length, USERTAG_HIDDATA);
    if (!Report)
        return FALSE;

    if (HidP_InitializeReportForID(HidP_Feature, ValueCaps[0].ReportID, Device->PreparsedData, Report, Length) == HIDP_STATUS_SUCCESS &&
        NT_SUCCESS(TouchInputIoctl(Device, IOCTL_HID_GET_FEATURE, Report, Length)))
    {
        Result = !Value ||
                 HidP_GetUsageValue(HidP_Feature, UsagePage, 0, Usage, Value, Device->PreparsedData, Report, Length) == HIDP_STATUS_SUCCESS;
    }

    ExFreePoolWithTag(Report, USERTAG_HIDDATA);
    return Result;
}

static
VOID
TouchInputClose(
    _Inout_ PTOUCHINPUT_DEVICE Device)
{
    if (Device->Handle)
    {
        if (Device->Status == STATUS_PENDING)
        {
            (void)ZwCancelIoFile(Device->Handle, &Device->Iosb);
            KeWaitForSingleObject(&Device->FileObject->Event, Executive, KernelMode, FALSE, NULL);
        }
        ObCloseHandle(Device->Handle, KernelMode);
    }
    if (Device->FileObject)
        ObDereferenceObject(Device->FileObject);
    if (Device->Source)
    {
        UserEnterExclusive();
        PointerDestroyTouchSource(Device->Source);
        UserLeave();
    }
    if (Device->PreparsedData)
        ExFreePoolWithTag(Device->PreparsedData, USERTAG_HIDDESC);
    if (Device->Report)
        ExFreePoolWithTag(Device->Report, USERTAG_HIDDATA);
    if (Device->UsageList)
        ExFreePoolWithTag(Device->UsageList, USERTAG_HIDDATA);
    if (Device->FingerCollections)
        ExFreePoolWithTag(Device->FingerCollections, USERTAG_HIDDATA);
    if (Device->Frame)
        ExFreePoolWithTag(Device->Frame, USERTAG_HIDDATA);
    if (Device->Path.Buffer)
        ExFreePoolWithTag(Device->Path.Buffer, USERTAG_HIDDESC);
    RtlZeroMemory(Device, sizeof(*Device));
}

static
BOOLEAN
TouchInputReadAxis(
    _In_ PTOUCHINPUT_DEVICE Device,
    _In_ USAGE Usage,
    _Out_ PLONG Minimum,
    _Out_ PLONG Range)
{
    HIDP_VALUE_CAPS AxisCaps;
    USHORT One = 1;

    if (HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_GENERIC, Device->FingerCollections[0], Usage,
                                  &AxisCaps, &One, Device->PreparsedData) != HIDP_STATUS_SUCCESS ||
        AxisCaps.LogicalMax <= AxisCaps.LogicalMin)
    {
        return FALSE;
    }

    *Minimum = AxisCaps.LogicalMin;
    *Range = AxisCaps.LogicalMax - AxisCaps.LogicalMin;
    return TRUE;
}

static
BOOLEAN
TouchInputDescribe(
    _Inout_ PTOUCHINPUT_DEVICE Device)
{
    PHIDP_VALUE_CAPS ValueCaps;
    HIDP_VALUE_CAPS CountCaps;
    HIDP_BUTTON_CAPS ButtonCaps;
    USHORT Count, One, Index;
    NTSTATUS Status;

    Count = Device->Caps.NumberInputValueCaps;
    if (!Count)
        return FALSE;

    ValueCaps = ExAllocatePoolWithTag(PagedPool, Count * sizeof(HIDP_VALUE_CAPS), USERTAG_HIDDATA);
    if (!ValueCaps)
        return FALSE;

    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER,
                                       ValueCaps, &Count, Device->PreparsedData);
    if (Status == HIDP_STATUS_SUCCESS && Count)
    {
        Device->FingerCollections = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(USHORT), USERTAG_HIDDATA);
        if (Device->FingerCollections)
        {
            for (Index = 0; Index < Count; Index++)
                Device->FingerCollections[Index] = ValueCaps[Index].LinkCollection;
            Device->FingerCount = Count;
        }
    }
    ExFreePoolWithTag(ValueCaps, USERTAG_HIDDATA);
    if (!Device->FingerCount)
        return FALSE;

    if (!TouchInputReadAxis(Device, HID_USAGE_GENERIC_X, &Device->MinimumX, &Device->RangeX) ||
        !TouchInputReadAxis(Device, HID_USAGE_GENERIC_Y, &Device->MinimumY, &Device->RangeY))
    {
        return FALSE;
    }

    One = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_COUNT,
                                       &CountCaps, &One, Device->PreparsedData);
    Device->HasContactCount = Status == HIDP_STATUS_SUCCESS || Status == HIDP_STATUS_BUFFER_TOO_SMALL;

    One = 1;
    Status = HidP_GetSpecificButtonCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, Device->FingerCollections[0],
                                        HID_USAGE_DIGITIZER_CONFIDENCE, &ButtonCaps, &One, Device->PreparsedData);
    Device->HasConfidence = Status == HIDP_STATUS_SUCCESS || Status == HIDP_STATUS_BUFFER_TOO_SMALL;

    Device->UsageListLength = HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_DIGITIZER, Device->PreparsedData);
    if (!Device->UsageListLength)
        return FALSE;
    Device->UsageList = ExAllocatePoolWithTag(NonPagedPool, Device->UsageListLength * sizeof(USAGE), USERTAG_HIDDATA);
    return Device->UsageList != NULL;
}

static
VOID
TouchInputAttach(
    _Inout_ PTOUCHINPUT_STATE State,
    _In_ PUNICODE_STRING Path)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HID_COLLECTION_INFORMATION Information;
    PTOUCHINPUT_DEVICE Device = NULL;
    IO_STATUS_BLOCK Iosb;
    ULONG Maximum = 0;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < TOUCHINPUT_MAX_DEVICES; Index++)
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

    if (!NT_SUCCESS(HidInputQueryCollection(Path, &Information, &Device->PreparsedData, &Device->Caps)))
        return;
    if (Device->Caps.UsagePage != HID_USAGE_PAGE_DIGITIZER ||
        Device->Caps.Usage != HID_USAGE_DIGITIZER_TOUCH_SCREEN ||
        !Device->Caps.InputReportByteLength ||
        !TouchInputDescribe(Device))
    {
        TouchInputClose(Device);
        return;
    }

    Device->ReportLength = Device->Caps.InputReportByteLength;
    Device->Report = ExAllocatePoolWithTag(NonPagedPool, Device->ReportLength, USERTAG_HIDDATA);
    Device->Path.Buffer = ExAllocatePoolWithTag(PagedPool, Path->Length, USERTAG_HIDDESC);
    if (!Device->Report || !Device->Path.Buffer)
    {
        TouchInputClose(Device);
        return;
    }
    RtlCopyMemory(Device->Path.Buffer, Path->Buffer, Path->Length);
    Device->Path.Length = Path->Length;
    Device->Path.MaximumLength = Path->Length;

    InitializeObjectAttributes(&ObjectAttributes, Path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&Device->Handle, FILE_READ_DATA, &ObjectAttributes, &Iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Device->Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&Device->FileObject, NULL);
        if (!NT_SUCCESS(Status))
        {
            ObCloseHandle(Device->Handle, KernelMode);
            Device->Handle = NULL;
        }
    }
    else
    {
        Device->Handle = NULL;
    }
    if (!NT_SUCCESS(Status))
    {
        ERR("Touch screen %wZ could not be opened: 0x%08lx\n", Path, Status);
        TouchInputClose(Device);
        return;
    }

    TouchInputGetFeature(Device, HID_USAGE_PAGE_DIGITIZER, HID_USAGE_DIGITIZER_CONTACT_COUNT_MAXIMUM, &Maximum);
    TouchInputGetFeature(Device, HID_USAGE_PAGE_VENDOR_DEFINED_BEGIN, TOUCHINPUT_USAGE_CERTIFICATION_STATUS, NULL);

    Device->Capacity = min(max(Maximum, Device->FingerCount), MAX_TOUCH_COUNT);
    Device->Frame = ExAllocatePoolZero(NonPagedPool, Device->Capacity * sizeof(TOUCH_CONTACT), USERTAG_HIDDATA);
    Device->Source = Device->Frame ? PointerCreateTouchSource(Device->Capacity) : NULL;
    if (!Device->Source)
    {
        TouchInputClose(Device);
        return;
    }

    Device->Status = STATUS_SUCCESS;
    Device->Iosb.Information = 0;
    DbgPrint("Touch screen %wZ: %lu contacts, %lu per report\n", Path, Device->Capacity, Device->FingerCount);
}

static
ULONG
TouchInputScale(
    _In_ ULONG Value,
    _In_ LONG Minimum,
    _In_ LONG Range)
{
    LONG Offset = (LONG)Value - Minimum;

    if (Offset <= 0)
        return 0;
    if (Offset >= Range)
        return TOUCHINPUT_COORDINATE_MAX;
    return (ULONG)(((ULONGLONG)Offset * TOUCHINPUT_COORDINATE_MAX) / (ULONG)Range);
}

static
ULONG
TouchInputGetPanelRotation(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"InbvGetGopFrameBufferTransform");
    PTOUCHINPUT_GET_GOP_TRANSFORM GetTransform;
    LOADER_PARAMETER_FRAMEBUFFER_TRANSFORM Transform;
    ULONG Rotation = LoaderFramebufferRotationIdentity;

    if (TouchInputPanelRotation != TOUCHINPUT_ROTATION_UNKNOWN)
        return TouchInputPanelRotation;

    GetTransform = (PTOUCHINPUT_GET_GOP_TRANSFORM)MmGetSystemRoutineAddress(&Name);
    if (GetTransform && GetTransform(&Transform) && Transform.Rotation <= LoaderFramebufferRotation270)
        Rotation = Transform.Rotation;
    if (Rotation != LoaderFramebufferRotationIdentity)
        DbgPrint("Touch screen input follows the display rotation of %lu degrees\n", Rotation * 90);
    TouchInputPanelRotation = Rotation;
    return Rotation;
}

static
VOID
TouchInputRotateContact(
    _Inout_ PTOUCH_CONTACT Contact)
{
    ULONG X = Contact->X, Y = Contact->Y;

    switch (TouchInputGetPanelRotation())
    {
        case LoaderFramebufferRotation90:
            Contact->X = Y;
            Contact->Y = TOUCHINPUT_COORDINATE_MAX - X;
            break;
        case LoaderFramebufferRotation180:
            Contact->X = TOUCHINPUT_COORDINATE_MAX - X;
            Contact->Y = TOUCHINPUT_COORDINATE_MAX - Y;
            break;
        case LoaderFramebufferRotation270:
            Contact->X = TOUCHINPUT_COORDINATE_MAX - Y;
            Contact->Y = X;
            break;
    }
}

static
VOID
TouchInputReadContact(
    _Inout_ PTOUCHINPUT_DEVICE Device,
    _In_ USHORT LinkCollection)
{
    ULONG Length = Device->UsageListLength;
    PTOUCH_CONTACT Contact;
    ULONG Id, X, Y, Index;
    BOOLEAN Tip = FALSE;
    BOOLEAN Confidence = !Device->HasConfidence;

    if (Device->FrameCount >= Device->Capacity)
        return;

    if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_DIGITIZER, LinkCollection, Device->UsageList, &Length,
                       Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, LinkCollection, HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER,
                           &Id, Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, LinkCollection, HID_USAGE_GENERIC_X,
                           &X, Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, LinkCollection, HID_USAGE_GENERIC_Y,
                           &Y, Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS)
    {
        return;
    }

    for (Index = 0; Index < Length; Index++)
    {
        if (Device->UsageList[Index] == HID_USAGE_DIGITIZER_TIP_SWITCH)
            Tip = TRUE;
        else if (Device->UsageList[Index] == HID_USAGE_DIGITIZER_CONFIDENCE)
            Confidence = TRUE;
    }

    Contact = &Device->Frame[Device->FrameCount++];
    Contact->ContactId = Id;
    Contact->X = TouchInputScale(X, Device->MinimumX, Device->RangeX);
    Contact->Y = TouchInputScale(Y, Device->MinimumY, Device->RangeY);
    TouchInputRotateContact(Contact);
    Contact->InContact = Tip;
    Contact->Confidence = Confidence;
}

static
BOOLEAN
TouchInputDispatchReport(
    _Inout_ PTOUCHINPUT_DEVICE Device)
{
    ULONG Count = Device->FingerCount;
    ULONG Take, Index;

    if (!Device->Iosb.Information)
        return FALSE;

    if (Device->HasContactCount &&
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_COUNT, &Count,
                           Device->PreparsedData, Device->Report, Device->ReportLength) != HIDP_STATUS_SUCCESS)
    {
        return FALSE;
    }

    if (Count || !Device->FrameExpected)
    {
        Device->FrameExpected = min(Count, Device->Capacity);
        Device->FrameReceived = 0;
        Device->FrameCount = 0;
    }

    Take = min(Device->FingerCount, Device->FrameExpected - Device->FrameReceived);
    for (Index = 0; Index < Take; Index++)
        TouchInputReadContact(Device, Device->FingerCollections[Index]);

    Device->FrameReceived += Take;
    if (Device->FrameReceived < Device->FrameExpected)
        return FALSE;

    Device->FrameExpected = 0;
    UserEnterExclusive();
    PointerProcessTouchFrame(Device->Source, Device->Frame, Device->FrameCount);
    UserLeave();
    return TRUE;
}

NTSTATUS
NTAPI
InitTouchInputImpl(
    _In_ PDRIVER_OBJECT DriverObject)
{
    gTouchInputDriverObject = DriverObject;
    return STATUS_SUCCESS;
}

VOID
NTAPI
TouchInputStart(VOID)
{
    PTOUCHINPUT_STATE State;
    NTSTATUS Status;

    State = ExAllocatePoolZero(NonPagedPool, sizeof(*State), USERTAG_HIDDATA);
    if (!State)
        return;

    KeInitializeEvent(&State->ArrivalEvent, SynchronizationEvent, FALSE);
    ExInitializeFastMutex(&State->ArrivalLock);
    InitializeListHead(&State->Arrivals);
    gTouchInput = State;

    Status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
                                            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                                            (PVOID)&TouchInputInterface,
                                            gTouchInputDriverObject,
                                            TouchInputInterfaceNotification,
                                            State,
                                            &State->NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        ERR("Touch screen notification not registered: 0x%08lx\n", Status);
        State->NotificationEntry = NULL;
    }
}

VOID
NTAPI
TouchInputStop(VOID)
{
    PTOUCHINPUT_STATE State = gTouchInput;
    PLIST_ENTRY Entry;
    ULONG Index;

    if (!State)
        return;

    if (State->NotificationEntry)
        IoUnregisterPlugPlayNotification(State->NotificationEntry);

    for (Index = 0; Index < TOUCHINPUT_MAX_DEVICES; Index++)
    {
        if (State->Devices[Index].Handle)
            TouchInputClose(&State->Devices[Index]);
    }

    while (!IsListEmpty(&State->Arrivals))
    {
        Entry = RemoveHeadList(&State->Arrivals);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry, TOUCHINPUT_ARRIVAL, ListEntry), USERTAG_HIDDESC);
    }

    gTouchInput = NULL;
    ExFreePoolWithTag(State, USERTAG_HIDDATA);
}

ULONG
NTAPI
TouchInputPrepareWait(
    _Inout_ PVOID *WaitObjects,
    _Inout_ PULONG WaitCount)
{
    PTOUCHINPUT_STATE State = gTouchInput;
    PTOUCHINPUT_DEVICE Device;
    LARGE_INTEGER ByteOffset;
    ULONG Expected = 1;
    ULONG Index;

    if (!State)
        return 0;

    WaitObjects[(*WaitCount)++] = &State->ArrivalEvent;
    ByteOffset.QuadPart = 0;

    for (Index = 0; Index < TOUCHINPUT_MAX_DEVICES; Index++)
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
TouchInputSignaled(
    _In_ PVOID Object)
{
    PTOUCHINPUT_STATE State = gTouchInput;
    PTOUCHINPUT_DEVICE Device;
    ULONG Index;

    if (!State)
        return FALSE;
    if (Object == &State->ArrivalEvent)
        return TRUE;

    for (Index = 0; Index < TOUCHINPUT_MAX_DEVICES; Index++)
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
TouchInputProcess(VOID)
{
    PTOUCHINPUT_STATE State = gTouchInput;
    PTOUCHINPUT_DEVICE Device;
    PTOUCHINPUT_ARRIVAL Arrival;
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

        Arrival = CONTAINING_RECORD(Entry, TOUCHINPUT_ARRIVAL, ListEntry);
        TouchInputAttach(State, &Arrival->Path);
        ExFreePoolWithTag(Arrival, USERTAG_HIDDESC);
    }

    for (Index = 0; Index < TOUCHINPUT_MAX_DEVICES; Index++)
    {
        Device = &State->Devices[Index];
        if (!Device->Handle || Device->Status == STATUS_PENDING)
            continue;

        if (NT_SUCCESS(Device->Status))
        {
            if (TouchInputDispatchReport(Device))
                Dispatched = TRUE;
            Device->Iosb.Information = 0;
        }
        else
        {
            TRACE("Touch screen %wZ closed: 0x%08lx\n", &Device->Path, Device->Status);
            TouchInputClose(Device);
        }
    }
    return Dispatched;
}
