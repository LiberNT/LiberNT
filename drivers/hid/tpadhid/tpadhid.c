/*
 * PROJECT:     LiberNT HID Precision Touchpad Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turns the contacts of a HID precision touchpad into pointer input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidclass.h>
#include <hidpddi.h>
#include <hidpi.h>
#include <ntddmou.h>
#include <ntddkbd.h>
#include <kbdmou.h>

#define NDEBUG
#include <debug.h>

#define TPADHID_TAG 'dpTH'

#define TPADHID_MAX_FINGERS 10
#define TPADHID_MAX_CONTACTS 10

#define TPADHID_MODE_MOUSE 0
#define TPADHID_MODE_TOUCHPAD 3
#define TPADHID_MODE_UNKNOWN 0xFF

#define TPADHID_BUTTON_LEFT 0x01
#define TPADHID_BUTTON_RIGHT 0x02

#define TPADHID_REPORT_PAD 0x01
#define TPADHID_REPORT_LEFT 0x02
#define TPADHID_REPORT_RIGHT 0x04

#define TPADHID_KEY_CTRL 0x01
#define TPADHID_KEY_SHIFT 0x02
#define TPADHID_KEY_ALT 0x04
#define TPADHID_KEY_WIN 0x08

#define TPADHID_SCAN_EXTENDED 0x0100
#define TPADHID_SCAN_BREAK 0x0200
#define TPADHID_SCAN_TAB 0x0F
#define TPADHID_SCAN_CTRL 0x1D
#define TPADHID_SCAN_S 0x1F
#define TPADHID_SCAN_D 0x20
#define TPADHID_SCAN_SHIFT 0x2A
#define TPADHID_SCAN_N 0x31
#define TPADHID_SCAN_ALT 0x38
#define TPADHID_SCAN_LEFT (TPADHID_SCAN_EXTENDED | 0x4B)
#define TPADHID_SCAN_RIGHT (TPADHID_SCAN_EXTENDED | 0x4D)
#define TPADHID_SCAN_WIN (TPADHID_SCAN_EXTENDED | 0x5B)

#define TPADHID_KEYBOARD_TYPE_HID 0x51
#define TPADHID_KEYBOARD_KEYS 10

#define TPADHID_POINTER_COUNTS_PER_MM 12
#define TPADHID_WHEEL_UNITS_PER_MM 30
#define TPADHID_TAP_TIME (180 * 10000)
#define TPADHID_DRAG_TIME (300 * 10000)
#define TPADHID_TAP_TRAVEL_UM 1300
#define TPADHID_SWIPE_START_UM 10000
#define TPADHID_SWIPE_STEP_UM 15000
#define TPADHID_BUTTON_STRIP_UM 10000
#define TPADHID_BUTTON_STRIP_PERCENT 15

#define TPADHID_READ_SUBMITTING 0
#define TPADHID_READ_PENDING 1
#define TPADHID_READ_COMPLETED 2

typedef enum _TPADHID_ROLE
{
    TpadRoleNone,
    TpadRoleTouchpad,
    TpadRoleConfiguration
} TPADHID_ROLE;

typedef enum _TPADHID_TOUCH_MODE
{
    TpadTouchNone,
    TpadTouchScroll,
    TpadTouchPinch,
    TpadTouchSwipe,
    TpadTouchDone
} TPADHID_TOUCH_MODE;

typedef enum _TPADHID_TAP_STATE
{
    TpadTapIdle,
    TpadTapReleasePending,
    TpadTapSecondTouch,
    TpadTapDragging
} TPADHID_TAP_STATE;

typedef struct _TPADHID_CONTACT
{
    ULONG Id;
    LONG X;
    LONG Y;
    BOOLEAN Touching;
} TPADHID_CONTACT, *PTPADHID_CONTACT;

typedef struct _TPADHID_SLOT
{
    BOOLEAN Active;
    ULONG Id;
    LONG X;
    LONG Y;
    LONG StartX;
    LONG StartY;
    LONG ReferenceX;
    LONG ReferenceY;
    LONG DeltaX;
    LONG DeltaY;
} TPADHID_SLOT, *PTPADHID_SLOT;

typedef struct _TPADHID_DEVICE_EXTENSION
{
    PDEVICE_OBJECT NextDeviceObject;
    LIST_ENTRY ListEntry;
    TPADHID_ROLE Role;
    BOOLEAN Started;
    PWSTR ParentId;
    PHIDP_PREPARSED_DATA PreparsedData;
    HIDP_CAPS Caps;

    PDEVICE_OBJECT ClassDeviceObject;
    PSERVICE_CALLBACK_ROUTINE ClassService;

    BOOLEAN InputModeValid;
    UCHAR InputModeReportId;
    UCHAR InputMode;
    UCHAR SinkKeys;
    SYSTEM_POWER_STATE SystemState;
    PIO_WORKITEM ModeWorkItem;
    LONG ModeWorkQueued;
    KEVENT ModeWorkIdle;
    KEYBOARD_INDICATOR_PARAMETERS Indicators;
    KEYBOARD_TYPEMATIC_PARAMETERS Typematic;

    PIRP ReadIrp;
    PMDL ReportMdl;
    PCHAR Report;
    ULONG ReportLength;
    PFILE_OBJECT FileObject;
    KSPIN_LOCK ReadLock;
    BOOLEAN ReadActive;
    BOOLEAN ReadStopping;
    KEVENT ReadIdleEvent;
    LONG ReadState;

    USHORT FingerCount;
    USHORT FingerCollection[TPADHID_MAX_FINGERS];
    BOOLEAN HasContactCount;
    BOOLEAN HasConfidence;
    PUSAGE UsageList;
    ULONG UsageListLength;
    ULONG ButtonListLength;
    LONG MinimumX;
    LONG MaximumY;
    ULONG RangeX;
    ULONG RangeY;
    ULONGLONG ExtentX;
    ULONGLONG ExtentY;

    ULONG FrameExpected;
    ULONG FrameReceived;
    ULONG FrameContactCount;
    TPADHID_CONTACT FrameContacts[TPADHID_MAX_CONTACTS];

    KSPIN_LOCK GestureLock;
    KTIMER TapTimer;
    KDPC TapDpc;
    struct _TPADHID_DEVICE_EXTENSION *KeySink;
    TPADHID_SLOT Slots[TPADHID_MAX_CONTACTS];
    ULONG ActiveCount;
    BOOLEAN TapCandidate;
    ULONG TapFingers;
    ULONGLONG TouchStartTime;
    TPADHID_TAP_STATE TapState;
    BOOLEAN TapHeld;
    TPADHID_TOUCH_MODE TouchMode;
    BOOLEAN PointerArmed;
    BOOLEAN SwitchActive;
    UCHAR KeysHeld;
    LONG SwipeX;
    LONG SwipeY;
    ULONG PinchDistance;
    LONGLONG PinchRemainder;
    LONGLONG PointerRemainderX;
    LONGLONG PointerRemainderY;
    LONGLONG WheelRemainderX;
    LONGLONG WheelRemainderY;
    UCHAR ButtonState;
    UCHAR ReportButtons;
    BOOLEAN PadPressed;
    UCHAR PadTarget;
} TPADHID_DEVICE_EXTENSION, *PTPADHID_DEVICE_EXTENSION;

static KMUTEX TpadListLock;
static LIST_ENTRY TpadDeviceList;
static KSPIN_LOCK TpadKeyLock;

static const WCHAR TpadTouchpadId[] = L"HID_DEVICE_UP:000D_U:0005";
static const WCHAR TpadConfigurationId[] = L"HID_DEVICE_UP:000D_U:000E";

static
NTSTATUS
TpadSendIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputLength, OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
TpadWriteInputMode(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Mode)
{
    ULONG Length = DeviceExtension->Caps.FeatureReportByteLength;
    NTSTATUS Status;
    PCHAR Report;

    Report = ExAllocatePoolWithTag(NonPagedPool, Length, TPADHID_TAG);
    if (!Report)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = HidP_InitializeReportForID(HidP_Feature, DeviceExtension->InputModeReportId, DeviceExtension->PreparsedData, Report, Length);
    if (Status == HIDP_STATUS_SUCCESS)
        Status = HidP_SetUsageValue(HidP_Feature, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_DEVICE_MODE, Mode, DeviceExtension->PreparsedData, Report, Length);
    if (Status == HIDP_STATUS_SUCCESS)
        Status = TpadSendIoctl(DeviceExtension->NextDeviceObject, IOCTL_HID_SET_FEATURE, Report, Length, NULL, 0);

    ExFreePoolWithTag(Report, TPADHID_TAG);
    return Status;
}

static
ULONG
TpadModifierKeys(
    _In_ UCHAR Modifiers,
    _In_ BOOLEAN Release,
    _Out_writes_(4) PUSHORT Keys)
{
    static const USHORT Codes[4] = { TPADHID_SCAN_CTRL, TPADHID_SCAN_SHIFT, TPADHID_SCAN_ALT, TPADHID_SCAN_WIN };
    ULONG Count = 0, Index;

    for (Index = 0; Index < 4; Index++)
    {
        ULONG Bit = Release ? 3 - Index : Index;

        if (Modifiers & (1 << Bit))
            Keys[Count++] = Codes[Bit] | (Release ? TPADHID_SCAN_BREAK : 0);
    }

    return Count;
}

static
VOID
TpadDeliverKeys(
    _In_ PTPADHID_DEVICE_EXTENSION Sink,
    _In_reads_(Count) const USHORT *Keys,
    _In_ ULONG Count)
{
    static const USHORT Codes[4] = { TPADHID_SCAN_CTRL, TPADHID_SCAN_SHIFT, TPADHID_SCAN_ALT, TPADHID_SCAN_WIN };
    KEYBOARD_INPUT_DATA Data[8];
    ULONG Consumed = 0, Index, Bit;

    RtlZeroMemory(Data, sizeof(Data));
    for (Index = 0; Index < Count; Index++)
    {
        Data[Index].MakeCode = Keys[Index] & 0xFF;
        if (Keys[Index] & TPADHID_SCAN_EXTENDED)
            Data[Index].Flags |= KEY_E0;
        if (Keys[Index] & TPADHID_SCAN_BREAK)
            Data[Index].Flags |= KEY_BREAK;

        for (Bit = 0; Bit < 4; Bit++)
        {
            if ((Keys[Index] & ~TPADHID_SCAN_BREAK) != Codes[Bit])
                continue;

            if (Keys[Index] & TPADHID_SCAN_BREAK)
                Sink->SinkKeys &= ~(1 << Bit);
            else
                Sink->SinkKeys |= 1 << Bit;
        }
    }

    Sink->ClassService(Sink->ClassDeviceObject, Data, Data + Count, &Consumed);
}

static
BOOLEAN
TpadSendKeys(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_reads_(Count) const USHORT *Keys,
    _In_ ULONG Count)
{
    PTPADHID_DEVICE_EXTENSION Sink;
    BOOLEAN Sent = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&TpadKeyLock, &OldIrql);
    Sink = DeviceExtension->KeySink;
    if (Sink && Sink->ClassService && Count)
    {
        TpadDeliverKeys(Sink, Keys, Count);
        Sent = TRUE;
    }
    KeReleaseSpinLock(&TpadKeyLock, OldIrql);
    return Sent;
}

static
VOID
TpadReleaseSinkKeys(
    _In_ PTPADHID_DEVICE_EXTENSION Sink)
{
    USHORT Keys[4];
    KIRQL OldIrql;
    ULONG Count;

    KeAcquireSpinLock(&TpadKeyLock, &OldIrql);
    Count = TpadModifierKeys(Sink->SinkKeys, TRUE, Keys);
    if (Count && Sink->ClassService)
        TpadDeliverKeys(Sink, Keys, Count);
    Sink->SinkKeys = 0;
    KeReleaseSpinLock(&TpadKeyLock, OldIrql);
}

static
VOID
TpadKeyChord(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Modifiers,
    _In_ USHORT Key)
{
    USHORT Keys[8];
    ULONG Count;

    Modifiers &= ~DeviceExtension->KeysHeld;
    Count = TpadModifierKeys(Modifiers, FALSE, Keys);
    Keys[Count++] = Key;
    Keys[Count++] = Key | TPADHID_SCAN_BREAK;
    Count += TpadModifierKeys(Modifiers, TRUE, &Keys[Count]);
    TpadSendKeys(DeviceExtension, Keys, Count);
}

static
BOOLEAN
TpadHoldModifier(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Modifier)
{
    USHORT Keys[4];
    ULONG Count = TpadModifierKeys(Modifier, FALSE, Keys);

    if (!TpadSendKeys(DeviceExtension, Keys, Count))
        return FALSE;

    DeviceExtension->KeysHeld |= Modifier;
    return TRUE;
}

static
VOID
TpadReleaseModifiers(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    USHORT Keys[4];
    ULONG Count = TpadModifierKeys(DeviceExtension->KeysHeld, TRUE, Keys);

    TpadSendKeys(DeviceExtension, Keys, Count);
    DeviceExtension->KeysHeld = 0;
    DeviceExtension->SwitchActive = FALSE;
}

static
VOID
TpadUpdateSiblings(
    _In_ PCWSTR ParentId,
    _In_ BOOLEAN DevicePresent)
{
    PTPADHID_DEVICE_EXTENSION Configuration = NULL;
    PTPADHID_DEVICE_EXTENSION Current;
    BOOLEAN Touchpad = FALSE;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    KIRQL OldIrql;
    UCHAR Mode;

    KeWaitForSingleObject(&TpadListLock, Executive, KernelMode, FALSE, NULL);

    for (Entry = TpadDeviceList.Flink; Entry != &TpadDeviceList; Entry = Entry->Flink)
    {
        Current = CONTAINING_RECORD(Entry, TPADHID_DEVICE_EXTENSION, ListEntry);
        if (!Current->Started || _wcsicmp(Current->ParentId, ParentId) != 0)
            continue;

        if (Current->Role == TpadRoleTouchpad)
            Touchpad = TRUE;
        else if (Current->Role == TpadRoleConfiguration)
            Configuration = Current;
    }

    KeAcquireSpinLock(&TpadKeyLock, &OldIrql);
    for (Entry = TpadDeviceList.Flink; Entry != &TpadDeviceList; Entry = Entry->Flink)
    {
        Current = CONTAINING_RECORD(Entry, TPADHID_DEVICE_EXTENSION, ListEntry);
        if (Current->Role == TpadRoleTouchpad && _wcsicmp(Current->ParentId, ParentId) == 0)
            Current->KeySink = Current->Started ? Configuration : NULL;
    }
    KeReleaseSpinLock(&TpadKeyLock, OldIrql);

    if (Configuration && Configuration->InputModeValid)
    {
        Mode = Touchpad ? TPADHID_MODE_TOUCHPAD : TPADHID_MODE_MOUSE;
        if (Configuration->InputMode == TPADHID_MODE_UNKNOWN && !Touchpad)
            Mode = TPADHID_MODE_UNKNOWN;

        if (Mode != Configuration->InputMode)
        {
            Configuration->InputMode = Mode;
            if (DevicePresent)
            {
                Status = TpadWriteInputMode(Configuration, Mode);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("TPADHID: setting input mode %u failed, status=0x%08lx\n", Mode, Status);
                    Configuration->InputMode = TPADHID_MODE_UNKNOWN;
                }
            }
        }
    }

    KeReleaseMutex(&TpadListLock, FALSE);
}

static
VOID
TpadSetStarted(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Started)
{
    KeWaitForSingleObject(&TpadListLock, Executive, KernelMode, FALSE, NULL);
    DeviceExtension->Started = Started;
    if (!Started)
        DeviceExtension->InputMode = TPADHID_MODE_UNKNOWN;
    KeReleaseMutex(&TpadListLock, FALSE);
}

static
VOID
TpadForgetInputMode(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    KeWaitForSingleObject(&TpadListLock, Executive, KernelMode, FALSE, NULL);
    DeviceExtension->InputMode = TPADHID_MODE_UNKNOWN;
    KeReleaseMutex(&TpadListLock, FALSE);
}

static
BOOLEAN
TpadPhysicalExtent(
    _In_ PHIDP_VALUE_CAPS ValueCaps,
    _Out_ PULONG Range,
    _Out_ PULONGLONG Extent)
{
    LONG Exponent = (LONG)(ValueCaps->UnitsExp & 0x0F);
    ULONGLONG Value;

    if (ValueCaps->LogicalMax <= ValueCaps->LogicalMin || ValueCaps->PhysicalMax <= ValueCaps->PhysicalMin)
        return FALSE;

    Value = (ULONGLONG)(ValueCaps->PhysicalMax - ValueCaps->PhysicalMin);
    if (ValueCaps->Units == 0x11)
        Value *= 10000;
    else if (ValueCaps->Units == 0x13)
        Value *= 25400;
    else
        return FALSE;

    if (Exponent > 7)
        Exponent -= 16;

    for (; Exponent > 0; Exponent--)
        Value *= 10;
    for (; Exponent < 0; Exponent++)
        Value /= 10;

    if (!Value || Value > MAXULONG)
        return FALSE;

    *Range = (ULONG)(ValueCaps->LogicalMax - ValueCaps->LogicalMin);
    *Extent = Value;
    return TRUE;
}

static
LONG
TpadScale(
    _Inout_ PLONGLONG Remainder,
    _In_ LONG Units,
    _In_ ULONGLONG Extent,
    _In_ ULONG Factor,
    _In_ ULONGLONG Divisor)
{
    LONGLONG Value = *Remainder + (LONGLONG)Units * (LONGLONG)Extent * Factor;
    LONGLONG Result = Value / (LONGLONG)Divisor;

    *Remainder = Value - Result * (LONGLONG)Divisor;
    return (LONG)Result;
}

static
ULONG
TpadSquareRoot(
    _In_ ULONGLONG Value)
{
    ULONGLONG Result = 0, Bit = 1ULL << 62;

    while (Bit > Value)
        Bit >>= 2;

    while (Bit)
    {
        if (Value >= Result + Bit)
        {
            Value -= Result + Bit;
            Result = (Result >> 1) + Bit;
        }
        else
        {
            Result >>= 1;
        }

        Bit >>= 2;
    }

    return (ULONG)Result;
}

static
LONGLONG
TpadMagnitude(
    _In_ LONGLONG Value)
{
    return Value < 0 ? -Value : Value;
}

static
ULONG
TpadTravel(
    _In_ PTPADHID_SLOT Slot)
{
    return (ULONG)(Slot->DeltaX < 0 ? -Slot->DeltaX : Slot->DeltaX) + (ULONG)(Slot->DeltaY < 0 ? -Slot->DeltaY : Slot->DeltaY);
}

static
VOID
TpadReferenceTravel(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PTPADHID_SLOT Slot,
    _Out_ PLONGLONG TravelX,
    _Out_ PLONGLONG TravelY)
{
    *TravelX = (LONGLONG)(Slot->X - Slot->ReferenceX) * (LONGLONG)DeviceExtension->ExtentX / DeviceExtension->RangeX;
    *TravelY = (LONGLONG)(Slot->Y - Slot->ReferenceY) * (LONGLONG)DeviceExtension->ExtentY / DeviceExtension->RangeY;
}

static
LONGLONG
TpadReferenceReach(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PTPADHID_SLOT Slot)
{
    LONGLONG TravelX, TravelY;

    TpadReferenceTravel(DeviceExtension, Slot, &TravelX, &TravelY);
    return max(TpadMagnitude(TravelX), TpadMagnitude(TravelY));
}

static
ULONG
TpadDistance(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PTPADHID_SLOT First,
    _In_ PTPADHID_SLOT Second)
{
    LONGLONG DistanceX = (LONGLONG)(First->X - Second->X) * (LONGLONG)DeviceExtension->ExtentX / DeviceExtension->RangeX;
    LONGLONG DistanceY = (LONGLONG)(First->Y - Second->Y) * (LONGLONG)DeviceExtension->ExtentY / DeviceExtension->RangeY;

    return TpadSquareRoot((ULONGLONG)(DistanceX * DistanceX + DistanceY * DistanceY));
}

static
USHORT
TpadWheelData(
    _In_ LONG Value)
{
    if (Value > MAXSHORT)
        Value = MAXSHORT;
    else if (Value < -MAXSHORT)
        Value = -MAXSHORT;

    return (USHORT)(SHORT)Value;
}

static
BOOLEAN
TpadSlotTravelled(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PTPADHID_SLOT Slot)
{
    ULONGLONG TravelX = (ULONGLONG)(Slot->X > Slot->StartX ? Slot->X - Slot->StartX : Slot->StartX - Slot->X);
    ULONGLONG TravelY = (ULONGLONG)(Slot->Y > Slot->StartY ? Slot->Y - Slot->StartY : Slot->StartY - Slot->Y);

    return TravelX * DeviceExtension->ExtentX > (ULONGLONG)TPADHID_TAP_TRAVEL_UM * DeviceExtension->RangeX ||
           TravelY * DeviceExtension->ExtentY > (ULONGLONG)TPADHID_TAP_TRAVEL_UM * DeviceExtension->RangeY;
}

static
UCHAR
TpadPadTarget(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    ULONGLONG Strip = DeviceExtension->ExtentY * TPADHID_BUTTON_STRIP_PERCENT / 100;
    BOOLEAN Right = FALSE;
    PTPADHID_SLOT Slot;
    ULONG Index;

    if (Strip > TPADHID_BUTTON_STRIP_UM)
        Strip = TPADHID_BUTTON_STRIP_UM;

    for (Index = 0; Index < TPADHID_MAX_CONTACTS; Index++)
    {
        Slot = &DeviceExtension->Slots[Index];
        if (!Slot->Active || Slot->Y > DeviceExtension->MaximumY || Slot->X < DeviceExtension->MinimumX)
            continue;

        if ((ULONGLONG)(DeviceExtension->MaximumY - Slot->Y) * DeviceExtension->ExtentY >= Strip * DeviceExtension->RangeY)
            continue;

        if ((ULONGLONG)(Slot->X - DeviceExtension->MinimumX) * 2 < DeviceExtension->RangeX)
            return TPADHID_BUTTON_LEFT;

        Right = TRUE;
    }

    return Right ? TPADHID_BUTTON_RIGHT : TPADHID_BUTTON_LEFT;
}

static
USHORT
TpadButtonFlags(
    _In_ UCHAR Previous,
    _In_ UCHAR Current)
{
    UCHAR Changed = Previous ^ Current;
    USHORT Flags = 0;

    if (Changed & TPADHID_BUTTON_LEFT)
        Flags |= (Current & TPADHID_BUTTON_LEFT) ? MOUSE_LEFT_BUTTON_DOWN : MOUSE_LEFT_BUTTON_UP;
    if (Changed & TPADHID_BUTTON_RIGHT)
        Flags |= (Current & TPADHID_BUTTON_RIGHT) ? MOUSE_RIGHT_BUTTON_DOWN : MOUSE_RIGHT_BUTTON_UP;

    return Flags;
}

static
UCHAR
TpadPhysicalButtons(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Buttons = 0;

    if (DeviceExtension->PadPressed)
        Buttons |= DeviceExtension->PadTarget;
    if (DeviceExtension->ReportButtons & TPADHID_REPORT_LEFT)
        Buttons |= TPADHID_BUTTON_LEFT;
    if (DeviceExtension->ReportButtons & TPADHID_REPORT_RIGHT)
        Buttons |= TPADHID_BUTTON_RIGHT;

    return Buttons;
}

static
USHORT
TpadSyncButtons(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Buttons = TpadPhysicalButtons(DeviceExtension);
    USHORT Flags;

    if (DeviceExtension->TapHeld)
        Buttons |= TPADHID_BUTTON_LEFT;

    Flags = TpadButtonFlags(DeviceExtension->ButtonState, Buttons);
    DeviceExtension->ButtonState = Buttons;
    return Flags;
}

static
VOID
TpadUpdateSlots(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    PTPADHID_CONTACT Contact;
    PTPADHID_SLOT Slot, Free;
    ULONG Index, SlotIndex;

    for (SlotIndex = 0; SlotIndex < TPADHID_MAX_CONTACTS; SlotIndex++)
    {
        DeviceExtension->Slots[SlotIndex].DeltaX = 0;
        DeviceExtension->Slots[SlotIndex].DeltaY = 0;
    }

    for (Index = 0; Index < DeviceExtension->FrameContactCount; Index++)
    {
        Contact = &DeviceExtension->FrameContacts[Index];
        Slot = NULL;
        Free = NULL;

        for (SlotIndex = 0; SlotIndex < TPADHID_MAX_CONTACTS; SlotIndex++)
        {
            if (!DeviceExtension->Slots[SlotIndex].Active)
            {
                if (!Free)
                    Free = &DeviceExtension->Slots[SlotIndex];
            }
            else if (DeviceExtension->Slots[SlotIndex].Id == Contact->Id)
            {
                Slot = &DeviceExtension->Slots[SlotIndex];
                break;
            }
        }

        if (!Contact->Touching)
        {
            if (Slot)
                Slot->Active = FALSE;
            continue;
        }

        if (Slot)
        {
            Slot->DeltaX = Contact->X - Slot->X;
            Slot->DeltaY = Contact->Y - Slot->Y;
            Slot->X = Contact->X;
            Slot->Y = Contact->Y;
        }
        else if (Free)
        {
            Free->Active = TRUE;
            Free->Id = Contact->Id;
            Free->X = Free->StartX = Free->ReferenceX = Contact->X;
            Free->Y = Free->StartY = Free->ReferenceY = Contact->Y;
        }
    }
}

static
VOID
TpadTwoFingers(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PTPADHID_SLOT First,
    _In_ PTPADHID_SLOT Second,
    _In_ LONG SumX,
    _In_ LONG SumY,
    _Inout_ PLONG WheelX,
    _Inout_ PLONG WheelY)
{
    LONGLONG FirstX, FirstY, SecondX, SecondY, FirstReach, SecondReach, Value;
    ULONG Distance;

    if (DeviceExtension->TouchMode == TpadTouchNone)
    {
        TpadReferenceTravel(DeviceExtension, First, &FirstX, &FirstY);
        TpadReferenceTravel(DeviceExtension, Second, &SecondX, &SecondY);
        FirstReach = max(TpadMagnitude(FirstX), TpadMagnitude(FirstY));
        SecondReach = max(TpadMagnitude(SecondX), TpadMagnitude(SecondY));
        if (max(FirstReach, SecondReach) < TPADHID_TAP_TRAVEL_UM)
            return;

        if (FirstX * SecondX + FirstY * SecondY < 0 && min(FirstReach, SecondReach) >= TPADHID_TAP_TRAVEL_UM / 2)
        {
            if (TpadHoldModifier(DeviceExtension, TPADHID_KEY_CTRL))
            {
                DeviceExtension->TouchMode = TpadTouchPinch;
                DeviceExtension->PinchDistance = TpadDistance(DeviceExtension, First, Second);
                DeviceExtension->PinchRemainder = 0;
            }
            else
            {
                DeviceExtension->TouchMode = TpadTouchDone;
            }

            return;
        }

        DeviceExtension->TouchMode = TpadTouchScroll;
    }

    if (DeviceExtension->TouchMode == TpadTouchScroll)
    {
        *WheelX = -TpadScale(&DeviceExtension->WheelRemainderX, SumX, DeviceExtension->ExtentX, TPADHID_WHEEL_UNITS_PER_MM, (ULONGLONG)DeviceExtension->RangeX * 2000);
        *WheelY = TpadScale(&DeviceExtension->WheelRemainderY, SumY, DeviceExtension->ExtentY, TPADHID_WHEEL_UNITS_PER_MM, (ULONGLONG)DeviceExtension->RangeY * 2000);
    }
    else if (DeviceExtension->TouchMode == TpadTouchPinch)
    {
        Distance = TpadDistance(DeviceExtension, First, Second);
        Value = DeviceExtension->PinchRemainder + ((LONGLONG)Distance - (LONGLONG)DeviceExtension->PinchDistance) * TPADHID_WHEEL_UNITS_PER_MM;
        *WheelY = (LONG)(Value / 1000);
        DeviceExtension->PinchRemainder = Value - (LONGLONG)*WheelY * 1000;
        DeviceExtension->PinchDistance = Distance;
    }
}

static
VOID
TpadSwipe(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Fingers,
    _In_ LONG SumX,
    _In_ LONG SumY)
{
    LONGLONG TravelX, TravelY, ReachX, ReachY;

    if (DeviceExtension->TouchMode == TpadTouchNone)
    {
        DeviceExtension->TouchMode = TpadTouchSwipe;
        DeviceExtension->SwipeX = 0;
        DeviceExtension->SwipeY = 0;
    }

    if (DeviceExtension->TouchMode != TpadTouchSwipe)
        return;

    DeviceExtension->SwipeX += SumX;
    DeviceExtension->SwipeY += SumY;
    TravelX = (LONGLONG)DeviceExtension->SwipeX * (LONGLONG)DeviceExtension->ExtentX / ((LONGLONG)DeviceExtension->RangeX * Fingers);
    TravelY = (LONGLONG)DeviceExtension->SwipeY * (LONGLONG)DeviceExtension->ExtentY / ((LONGLONG)DeviceExtension->RangeY * Fingers);
    ReachX = TpadMagnitude(TravelX);
    ReachY = TpadMagnitude(TravelY);

    if (DeviceExtension->SwitchActive)
    {
        if (ReachX >= TPADHID_SWIPE_STEP_UM)
        {
            TpadKeyChord(DeviceExtension, TravelX < 0 ? TPADHID_KEY_SHIFT : 0, TPADHID_SCAN_TAB);
            DeviceExtension->SwipeX = 0;
        }
    }
    else if (ReachY >= TPADHID_SWIPE_START_UM && ReachY >= ReachX)
    {
        TpadKeyChord(DeviceExtension, TPADHID_KEY_WIN, TravelY < 0 ? TPADHID_SCAN_TAB : TPADHID_SCAN_D);
        DeviceExtension->TouchMode = TpadTouchDone;
    }
    else if (ReachX >= TPADHID_SWIPE_START_UM)
    {
        if (Fingers == 4)
        {
            TpadKeyChord(DeviceExtension, TPADHID_KEY_CTRL | TPADHID_KEY_WIN, TravelX < 0 ? TPADHID_SCAN_RIGHT : TPADHID_SCAN_LEFT);
            DeviceExtension->TouchMode = TpadTouchDone;
        }
        else if (TpadHoldModifier(DeviceExtension, TPADHID_KEY_ALT))
        {
            DeviceExtension->SwitchActive = TRUE;
            TpadKeyChord(DeviceExtension, TravelX < 0 ? TPADHID_KEY_SHIFT : 0, TPADHID_SCAN_TAB);
            DeviceExtension->SwipeX = 0;
            DeviceExtension->SwipeY = 0;
        }
        else
        {
            DeviceExtension->TouchMode = TpadTouchDone;
        }
    }
}

static
VOID
TpadProcessFrame(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR ReportButtons)
{
    ULONGLONG Now = KeQueryInterruptTime();
    LONG PointerX = 0, PointerY = 0, WheelX = 0, WheelY = 0;
    ULONG ActiveCount = 0, Consumed = 0, Count = 0, Index;
    PTPADHID_SLOT Slot, Moving = NULL, Pair[2] = { NULL, NULL };
    LONG SumX = 0, SumY = 0;
    MOUSE_INPUT_DATA Data[6];
    BOOLEAN DoubleTap = FALSE;
    LARGE_INTEGER DueTime;
    USHORT ButtonFlags;
    UCHAR Physical, Tap = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->GestureLock, &OldIrql);
    TpadUpdateSlots(DeviceExtension);

    for (Index = 0; Index < TPADHID_MAX_CONTACTS; Index++)
    {
        Slot = &DeviceExtension->Slots[Index];
        if (!Slot->Active)
            continue;

        if (ActiveCount < 2)
            Pair[ActiveCount] = Slot;

        ActiveCount++;
        SumX += Slot->DeltaX;
        SumY += Slot->DeltaY;
        if (!Moving || TpadTravel(Slot) > TpadTravel(Moving))
            Moving = Slot;
    }

    if (ActiveCount != DeviceExtension->ActiveCount)
    {
        for (Index = 0; Index < TPADHID_MAX_CONTACTS; Index++)
        {
            DeviceExtension->Slots[Index].ReferenceX = DeviceExtension->Slots[Index].X;
            DeviceExtension->Slots[Index].ReferenceY = DeviceExtension->Slots[Index].Y;
        }

        TpadReleaseModifiers(DeviceExtension);
        DeviceExtension->TouchMode = TpadTouchNone;
        DeviceExtension->PointerArmed = !DeviceExtension->ActiveCount;
    }

    if (!DeviceExtension->ActiveCount && ActiveCount)
    {
        DeviceExtension->TouchStartTime = Now;
        DeviceExtension->TapCandidate = TRUE;
        DeviceExtension->TapFingers = ActiveCount;
        if (DeviceExtension->TapState == TpadTapReleasePending)
        {
            KeCancelTimer(&DeviceExtension->TapTimer);
            if (ActiveCount == 1)
            {
                DeviceExtension->TapState = TpadTapSecondTouch;
            }
            else
            {
                DeviceExtension->TapState = TpadTapIdle;
                DeviceExtension->TapHeld = FALSE;
            }
        }
    }
    else if (ActiveCount > DeviceExtension->TapFingers)
    {
        DeviceExtension->TapFingers = ActiveCount;
        if (DeviceExtension->TapState != TpadTapIdle)
        {
            DeviceExtension->TapState = TpadTapIdle;
            DeviceExtension->TapHeld = FALSE;
        }
    }

    DeviceExtension->ReportButtons = ReportButtons;
    if ((ReportButtons & TPADHID_REPORT_PAD) && !DeviceExtension->PadPressed)
    {
        DeviceExtension->PadPressed = TRUE;
        DeviceExtension->PadTarget = TpadPadTarget(DeviceExtension);
    }
    else if (!(ReportButtons & TPADHID_REPORT_PAD))
    {
        DeviceExtension->PadPressed = FALSE;
    }

    Physical = TpadPhysicalButtons(DeviceExtension);

    if (DeviceExtension->TapCandidate)
    {
        if (ReportButtons || Now - DeviceExtension->TouchStartTime > TPADHID_TAP_TIME)
            DeviceExtension->TapCandidate = FALSE;

        for (Index = 0; Index < TPADHID_MAX_CONTACTS && DeviceExtension->TapCandidate; Index++)
        {
            Slot = &DeviceExtension->Slots[Index];
            if (Slot->Active && TpadSlotTravelled(DeviceExtension, Slot))
                DeviceExtension->TapCandidate = FALSE;
        }

        if (!DeviceExtension->TapCandidate && DeviceExtension->TapState == TpadTapSecondTouch)
            DeviceExtension->TapState = TpadTapDragging;
    }

    if (!DeviceExtension->TapCandidate && Moving)
    {
        if (ActiveCount == 1 || Physical)
        {
            if (!DeviceExtension->PointerArmed && TpadReferenceReach(DeviceExtension, Moving) >= TPADHID_TAP_TRAVEL_UM)
                DeviceExtension->PointerArmed = TRUE;

            if (DeviceExtension->PointerArmed || Physical)
            {
                PointerX = TpadScale(&DeviceExtension->PointerRemainderX, Moving->DeltaX, DeviceExtension->ExtentX, TPADHID_POINTER_COUNTS_PER_MM, (ULONGLONG)DeviceExtension->RangeX * 1000);
                PointerY = TpadScale(&DeviceExtension->PointerRemainderY, Moving->DeltaY, DeviceExtension->ExtentY, TPADHID_POINTER_COUNTS_PER_MM, (ULONGLONG)DeviceExtension->RangeY * 1000);
            }
        }
        else if (ActiveCount == 2)
        {
            TpadTwoFingers(DeviceExtension, Pair[0], Pair[1], SumX, SumY, &WheelX, &WheelY);
        }
        else if (ActiveCount <= 4)
        {
            TpadSwipe(DeviceExtension, ActiveCount, SumX, SumY);
        }
    }

    if (DeviceExtension->ActiveCount && !ActiveCount)
    {
        if (DeviceExtension->TapCandidate && Now - DeviceExtension->TouchStartTime <= TPADHID_TAP_TIME)
        {
            if (DeviceExtension->TapFingers == 1)
            {
                DoubleTap = DeviceExtension->TapState == TpadTapSecondTouch;
                DeviceExtension->TapHeld = TRUE;
                DeviceExtension->TapState = TpadTapReleasePending;
                DueTime.QuadPart = -(LONGLONG)TPADHID_DRAG_TIME;
                KeSetTimer(&DeviceExtension->TapTimer, DueTime, &DeviceExtension->TapDpc);
            }
            else if (DeviceExtension->TapFingers == 2)
            {
                Tap = TPADHID_BUTTON_RIGHT;
            }
            else if (DeviceExtension->TapFingers == 3)
            {
                TpadKeyChord(DeviceExtension, TPADHID_KEY_WIN, TPADHID_SCAN_S);
            }
            else if (DeviceExtension->TapFingers == 4)
            {
                TpadKeyChord(DeviceExtension, TPADHID_KEY_WIN, TPADHID_SCAN_N);
            }
        }
        else if (DeviceExtension->TapState != TpadTapIdle)
        {
            DeviceExtension->TapState = TpadTapIdle;
            DeviceExtension->TapHeld = FALSE;
        }

        DeviceExtension->TapCandidate = FALSE;
        DeviceExtension->PointerRemainderX = 0;
        DeviceExtension->PointerRemainderY = 0;
        DeviceExtension->WheelRemainderX = 0;
        DeviceExtension->WheelRemainderY = 0;
    }

    DeviceExtension->ActiveCount = ActiveCount;
    RtlZeroMemory(Data, sizeof(Data));

    if (DoubleTap)
    {
        Data[Count++].ButtonFlags = MOUSE_LEFT_BUTTON_UP;
        Data[Count++].ButtonFlags = MOUSE_LEFT_BUTTON_DOWN;
    }

    ButtonFlags = TpadSyncButtons(DeviceExtension);
    if (PointerX || PointerY || ButtonFlags)
    {
        Data[Count].LastX = PointerX;
        Data[Count].LastY = PointerY;
        Data[Count].ButtonFlags = ButtonFlags;
        Count++;
    }

    if (WheelY)
    {
        Data[Count].ButtonFlags = MOUSE_WHEEL;
        Data[Count].ButtonData = TpadWheelData(WheelY);
        Count++;
    }

    if (WheelX)
    {
        Data[Count].ButtonFlags = MOUSE_HWHEEL;
        Data[Count].ButtonData = TpadWheelData(WheelX);
        Count++;
    }

    if (Tap && !(DeviceExtension->ButtonState & Tap))
    {
        Data[Count++].ButtonFlags = TpadButtonFlags(0, Tap);
        Data[Count++].ButtonFlags = TpadButtonFlags(Tap, 0);
    }

    if (Count && DeviceExtension->ClassService)
        DeviceExtension->ClassService(DeviceExtension->ClassDeviceObject, Data, Data + Count, &Consumed);

    KeReleaseSpinLock(&DeviceExtension->GestureLock, OldIrql);
}

static
VOID
NTAPI
TpadTapDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = Context;
    MOUSE_INPUT_DATA Data;
    ULONG Consumed = 0;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->GestureLock);
    if (DeviceExtension->TapState == TpadTapReleasePending)
    {
        DeviceExtension->TapState = TpadTapIdle;
        DeviceExtension->TapHeld = FALSE;
        RtlZeroMemory(&Data, sizeof(Data));
        Data.ButtonFlags = TpadSyncButtons(DeviceExtension);
        if (Data.ButtonFlags && DeviceExtension->ClassService)
            DeviceExtension->ClassService(DeviceExtension->ClassDeviceObject, &Data, &Data + 1, &Consumed);
    }
    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->GestureLock);
}

static
VOID
TpadResetGestures(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    MOUSE_INPUT_DATA Data;
    ULONG Consumed = 0;
    KIRQL OldIrql;

    KeCancelTimer(&DeviceExtension->TapTimer);
    KeFlushQueuedDpcs();

    KeAcquireSpinLock(&DeviceExtension->GestureLock, &OldIrql);
    TpadReleaseModifiers(DeviceExtension);
    DeviceExtension->TapState = TpadTapIdle;
    DeviceExtension->TapHeld = FALSE;
    DeviceExtension->PadPressed = FALSE;
    DeviceExtension->ReportButtons = 0;
    RtlZeroMemory(&Data, sizeof(Data));
    Data.ButtonFlags = TpadSyncButtons(DeviceExtension);
    if (Data.ButtonFlags && DeviceExtension->ClassService)
        DeviceExtension->ClassService(DeviceExtension->ClassDeviceObject, &Data, &Data + 1, &Consumed);

    DeviceExtension->FrameExpected = 0;
    DeviceExtension->ActiveCount = 0;
    DeviceExtension->TapCandidate = FALSE;
    DeviceExtension->TouchMode = TpadTouchNone;
    RtlZeroMemory(DeviceExtension->Slots, sizeof(DeviceExtension->Slots));
    KeReleaseSpinLock(&DeviceExtension->GestureLock, OldIrql);
}

static
BOOLEAN
TpadUsagePresent(
    _In_reads_(Length) PUSAGE UsageList,
    _In_ ULONG Length,
    _In_ USAGE Usage)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (UsageList[Index] == Usage)
            return TRUE;
    }

    return FALSE;
}

static
VOID
TpadReadFinger(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ USHORT LinkCollection)
{
    ULONG Length = DeviceExtension->UsageListLength;
    PTPADHID_CONTACT Contact;
    ULONG Id, X, Y;

    if (DeviceExtension->FrameContactCount == TPADHID_MAX_CONTACTS)
        return;

    if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_DIGITIZER, LinkCollection, DeviceExtension->UsageList, &Length, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) != HIDP_STATUS_SUCCESS)
        return;

    if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, LinkCollection, HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER, &Id, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, LinkCollection, HID_USAGE_GENERIC_X, &X, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) != HIDP_STATUS_SUCCESS ||
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, LinkCollection, HID_USAGE_GENERIC_Y, &Y, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) != HIDP_STATUS_SUCCESS)
    {
        return;
    }

    Contact = &DeviceExtension->FrameContacts[DeviceExtension->FrameContactCount++];
    Contact->Id = Id;
    Contact->X = (LONG)X;
    Contact->Y = (LONG)Y;
    Contact->Touching = TpadUsagePresent(DeviceExtension->UsageList, Length, HID_USAGE_DIGITIZER_TIP_SWITCH) &&
                        (!DeviceExtension->HasConfidence || TpadUsagePresent(DeviceExtension->UsageList, Length, HID_USAGE_DIGITIZER_CONFIDENCE));
}

static
VOID
TpadProcessReport(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Count = DeviceExtension->FingerCount;
    ULONG Length, Take, Index;
    UCHAR Buttons = 0;

    if (DeviceExtension->HasContactCount &&
        HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_COUNT, &Count, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) != HIDP_STATUS_SUCCESS)
    {
        return;
    }

    if (Count || !DeviceExtension->FrameExpected)
    {
        DeviceExtension->FrameExpected = Count;
        DeviceExtension->FrameReceived = 0;
        DeviceExtension->FrameContactCount = 0;
    }

    Take = min(DeviceExtension->FingerCount, DeviceExtension->FrameExpected - DeviceExtension->FrameReceived);
    for (Index = 0; Index < Take; Index++)
        TpadReadFinger(DeviceExtension, DeviceExtension->FingerCollection[Index]);

    DeviceExtension->FrameReceived += Take;
    if (DeviceExtension->FrameReceived < DeviceExtension->FrameExpected)
        return;

    if (DeviceExtension->ButtonListLength)
    {
        Length = DeviceExtension->UsageListLength;
        if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, DeviceExtension->UsageList, &Length, DeviceExtension->PreparsedData, DeviceExtension->Report, DeviceExtension->ReportLength) == HIDP_STATUS_SUCCESS)
        {
            if (TpadUsagePresent(DeviceExtension->UsageList, Length, 1))
                Buttons |= TPADHID_REPORT_PAD;
            if (TpadUsagePresent(DeviceExtension->UsageList, Length, 2))
                Buttons |= TPADHID_REPORT_LEFT;
            if (TpadUsagePresent(DeviceExtension->UsageList, Length, 3))
                Buttons |= TPADHID_REPORT_RIGHT;
        }
    }

    DeviceExtension->FrameExpected = 0;
    TpadProcessFrame(DeviceExtension, Buttons);
}

static IO_COMPLETION_ROUTINE TpadReadCompletion;

static
VOID
TpadSubmitRead(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION IoStack;
    KIRQL OldIrql;

    for (;;)
    {
        IoReuseIrp(DeviceExtension->ReadIrp, STATUS_SUCCESS);

        KeAcquireSpinLock(&DeviceExtension->ReadLock, &OldIrql);
        if (DeviceExtension->ReadStopping)
        {
            DeviceExtension->ReadActive = FALSE;
            KeReleaseSpinLock(&DeviceExtension->ReadLock, OldIrql);
            KeSetEvent(&DeviceExtension->ReadIdleEvent, IO_NO_INCREMENT, FALSE);
            return;
        }
        KeReleaseSpinLock(&DeviceExtension->ReadLock, OldIrql);

        RtlZeroMemory(DeviceExtension->Report, DeviceExtension->ReportLength);
        DeviceExtension->ReadIrp->MdlAddress = DeviceExtension->ReportMdl;

        IoStack = IoGetNextIrpStackLocation(DeviceExtension->ReadIrp);
        IoStack->MajorFunction = IRP_MJ_READ;
        IoStack->Parameters.Read.Length = DeviceExtension->ReportLength;
        IoStack->Parameters.Read.Key = 0;
        IoStack->Parameters.Read.ByteOffset.QuadPart = 0;
        IoStack->FileObject = DeviceExtension->FileObject;
        IoSetCompletionRoutine(DeviceExtension->ReadIrp, TpadReadCompletion, DeviceExtension, TRUE, TRUE, TRUE);

        InterlockedExchange(&DeviceExtension->ReadState, TPADHID_READ_SUBMITTING);
        IoCallDriver(DeviceExtension->NextDeviceObject, DeviceExtension->ReadIrp);
        if (InterlockedCompareExchange(&DeviceExtension->ReadState, TPADHID_READ_PENDING, TPADHID_READ_SUBMITTING) == TPADHID_READ_SUBMITTING)
            return;
    }
}

static
NTSTATUS
NTAPI
TpadReadCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = Context;
    NTSTATUS Status = Irp->IoStatus.Status;
    BOOLEAN Inline;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Status))
        TpadProcessReport(DeviceExtension);

    Inline = InterlockedCompareExchange(&DeviceExtension->ReadState, TPADHID_READ_COMPLETED, TPADHID_READ_SUBMITTING) == TPADHID_READ_SUBMITTING;

    if (!NT_SUCCESS(Status) && (Inline || Status == STATUS_CANCELLED || Status == STATUS_DEVICE_NOT_CONNECTED || Status == STATUS_PRIVILEGE_NOT_HELD))
    {
        KeAcquireSpinLock(&DeviceExtension->ReadLock, &OldIrql);
        DeviceExtension->ReadStopping = TRUE;
        KeReleaseSpinLock(&DeviceExtension->ReadLock, OldIrql);
    }

    if (!Inline)
        TpadSubmitRead(DeviceExtension);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
TpadStopRead(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Active;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->ReadLock, &OldIrql);
    Active = DeviceExtension->ReadActive;
    DeviceExtension->ReadStopping = TRUE;
    KeReleaseSpinLock(&DeviceExtension->ReadLock, OldIrql);

    if (Active)
    {
        IoCancelIrp(DeviceExtension->ReadIrp);
        KeWaitForSingleObject(&DeviceExtension->ReadIdleEvent, Executive, KernelMode, FALSE, NULL);
    }

    TpadResetGestures(DeviceExtension);
}

static
VOID
TpadStartRead(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->ReadLock, &OldIrql);
    DeviceExtension->FileObject = FileObject;
    DeviceExtension->ReadActive = TRUE;
    DeviceExtension->ReadStopping = FALSE;
    KeReleaseSpinLock(&DeviceExtension->ReadLock, OldIrql);

    KeClearEvent(&DeviceExtension->ReadIdleEvent);
    TpadSubmitRead(DeviceExtension);
}

static
VOID
TpadFreeResources(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->ReportMdl)
    {
        IoFreeMdl(DeviceExtension->ReportMdl);
        DeviceExtension->ReportMdl = NULL;
    }

    if (DeviceExtension->Report)
    {
        ExFreePoolWithTag(DeviceExtension->Report, TPADHID_TAG);
        DeviceExtension->Report = NULL;
    }

    if (DeviceExtension->UsageList)
    {
        ExFreePoolWithTag(DeviceExtension->UsageList, TPADHID_TAG);
        DeviceExtension->UsageList = NULL;
    }

    if (DeviceExtension->PreparsedData)
    {
        ExFreePoolWithTag(DeviceExtension->PreparsedData, TPADHID_TAG);
        DeviceExtension->PreparsedData = NULL;
    }
}

static
NTSTATUS
TpadStartTouchpad(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    HIDP_VALUE_CAPS ValueCaps[TPADHID_MAX_FINGERS];
    USHORT Count = TPADHID_MAX_FINGERS, One, Index;
    HIDP_BUTTON_CAPS ButtonCaps;
    HIDP_VALUE_CAPS AxisCaps;
    ULONG TouchListLength;
    NTSTATUS Status;

    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER, ValueCaps, &Count, DeviceExtension->PreparsedData);
    if (Status == HIDP_STATUS_BUFFER_TOO_SMALL)
        Count = TPADHID_MAX_FINGERS;
    else if (Status != HIDP_STATUS_SUCCESS || !Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    DeviceExtension->FingerCount = Count;
    for (Index = 0; Index < Count; Index++)
        DeviceExtension->FingerCollection[Index] = ValueCaps[Index].LinkCollection;

    One = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_GENERIC, DeviceExtension->FingerCollection[0], HID_USAGE_GENERIC_X, &AxisCaps, &One, DeviceExtension->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || !TpadPhysicalExtent(&AxisCaps, &DeviceExtension->RangeX, &DeviceExtension->ExtentX))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DeviceExtension->MinimumX = AxisCaps.LogicalMin;

    One = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_GENERIC, DeviceExtension->FingerCollection[0], HID_USAGE_GENERIC_Y, &AxisCaps, &One, DeviceExtension->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || !TpadPhysicalExtent(&AxisCaps, &DeviceExtension->RangeY, &DeviceExtension->ExtentY))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DeviceExtension->MaximumY = AxisCaps.LogicalMax;

    One = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_CONTACT_COUNT, &AxisCaps, &One, DeviceExtension->PreparsedData);
    DeviceExtension->HasContactCount = Status == HIDP_STATUS_SUCCESS || Status == HIDP_STATUS_BUFFER_TOO_SMALL;

    One = 1;
    Status = HidP_GetSpecificButtonCaps(HidP_Input, HID_USAGE_PAGE_DIGITIZER, DeviceExtension->FingerCollection[0], HID_USAGE_DIGITIZER_CONFIDENCE, &ButtonCaps, &One, DeviceExtension->PreparsedData);
    DeviceExtension->HasConfidence = Status == HIDP_STATUS_SUCCESS || Status == HIDP_STATUS_BUFFER_TOO_SMALL;

    TouchListLength = HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_DIGITIZER, DeviceExtension->PreparsedData);
    DeviceExtension->ButtonListLength = HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_BUTTON, DeviceExtension->PreparsedData);
    DeviceExtension->UsageListLength = max(TouchListLength, DeviceExtension->ButtonListLength);
    if (!TouchListLength)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    DeviceExtension->UsageList = ExAllocatePoolWithTag(NonPagedPool, DeviceExtension->UsageListLength * sizeof(USAGE), TPADHID_TAG);
    if (!DeviceExtension->UsageList)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceExtension->ReportLength = DeviceExtension->Caps.InputReportByteLength;
    if (!DeviceExtension->ReportLength)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    DeviceExtension->Report = ExAllocatePoolWithTag(NonPagedPool, DeviceExtension->ReportLength, TPADHID_TAG);
    if (!DeviceExtension->Report)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceExtension->ReportMdl = IoAllocateMdl(DeviceExtension->Report, DeviceExtension->ReportLength, FALSE, FALSE, NULL);
    if (!DeviceExtension->ReportMdl)
        return STATUS_INSUFFICIENT_RESOURCES;

    MmBuildMdlForNonPagedPool(DeviceExtension->ReportMdl);

    TpadResetGestures(DeviceExtension);
    return STATUS_SUCCESS;
}

static
NTSTATUS
TpadStartDevice(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension)
{
    HID_COLLECTION_INFORMATION Information;
    HIDP_VALUE_CAPS ValueCaps;
    NTSTATUS Status;
    USHORT One = 1;

    Status = TpadSendIoctl(DeviceExtension->NextDeviceObject, IOCTL_HID_GET_COLLECTION_INFORMATION, NULL, 0, &Information, sizeof(Information));
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension->PreparsedData = ExAllocatePoolWithTag(NonPagedPool, Information.DescriptorSize, TPADHID_TAG);
    if (!DeviceExtension->PreparsedData)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = TpadSendIoctl(DeviceExtension->NextDeviceObject, IOCTL_HID_GET_COLLECTION_DESCRIPTOR, NULL, 0, DeviceExtension->PreparsedData, Information.DescriptorSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (HidP_GetCaps(DeviceExtension->PreparsedData, &DeviceExtension->Caps) != HIDP_STATUS_SUCCESS || DeviceExtension->Caps.UsagePage != HID_USAGE_PAGE_DIGITIZER)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (DeviceExtension->Role == TpadRoleTouchpad)
    {
        if (DeviceExtension->Caps.Usage != HID_USAGE_DIGITIZER_TOUCH_PAD)
            return STATUS_DEVICE_CONFIGURATION_ERROR;

        return TpadStartTouchpad(DeviceExtension);
    }

    if (DeviceExtension->Caps.Usage != HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Status = HidP_GetSpecificValueCaps(HidP_Feature, HID_USAGE_PAGE_DIGITIZER, 0, HID_USAGE_DIGITIZER_DEVICE_MODE, &ValueCaps, &One, DeviceExtension->PreparsedData);
    if ((Status == HIDP_STATUS_SUCCESS || Status == HIDP_STATUS_BUFFER_TOO_SMALL) && DeviceExtension->Caps.FeatureReportByteLength)
    {
        DeviceExtension->InputModeReportId = ValueCaps.ReportID;
        DeviceExtension->InputModeValid = TRUE;
    }

    return STATUS_SUCCESS;
}

static
VOID
TpadStopDevice(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN DevicePresent)
{
    if (DeviceExtension->Role == TpadRoleTouchpad && DeviceExtension->ReadIrp)
        TpadStopRead(DeviceExtension);

    if (DeviceExtension->Started)
    {
        TpadSetStarted(DeviceExtension, FALSE);
        TpadUpdateSiblings(DeviceExtension->ParentId, DevicePresent);
        if (DeviceExtension->Role == TpadRoleConfiguration)
            TpadReleaseSinkKeys(DeviceExtension);
    }

    DeviceExtension->InputModeValid = FALSE;
    TpadFreeResources(DeviceExtension);
}

static
NTSTATUS
NTAPI
TpadCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    if (!IoForwardIrpSynchronously(DeviceExtension->NextDeviceObject, Irp))
        Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;

    Status = Irp->IoStatus.Status;
    if (NT_SUCCESS(Status) && DeviceExtension->Role == TpadRoleTouchpad && DeviceExtension->Started && !DeviceExtension->FileObject &&
        IoStack->Parameters.Create.SecurityContext && IoStack->Parameters.Create.SecurityContext->DesiredAccess)
    {
        TpadStartRead(DeviceExtension, IoStack->FileObject);
    }

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
TpadClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceExtension->FileObject && IoStack->FileObject == DeviceExtension->FileObject)
    {
        TpadStopRead(DeviceExtension);
        DeviceExtension->FileObject = NULL;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
TpadPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

static
NTSTATUS
TpadKeyboardControl(
    _In_ PTPADHID_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG InputLength = IoStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    PKEYBOARD_ATTRIBUTES Attributes;
    PCONNECT_DATA ConnectData;
    KIRQL OldIrql;

    Irp->IoStatus.Information = 0;

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
            if (OutputLength < sizeof(KEYBOARD_ATTRIBUTES))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Attributes = Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(Attributes, sizeof(*Attributes));
            Attributes->KeyboardIdentifier.Type = TPADHID_KEYBOARD_TYPE_HID;
            Attributes->NumberOfKeysTotal = TPADHID_KEYBOARD_KEYS;
            Attributes->InputDataQueueLength = 8;
            Irp->IoStatus.Information = sizeof(KEYBOARD_ATTRIBUTES);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_KEYBOARD_CONNECT:
            if (InputLength < sizeof(CONNECT_DATA))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            ConnectData = IoStack->Parameters.DeviceIoControl.Type3InputBuffer;
            KeAcquireSpinLock(&TpadKeyLock, &OldIrql);
            if (DeviceExtension->ClassService)
            {
                Status = STATUS_SHARING_VIOLATION;
            }
            else
            {
                DeviceExtension->ClassDeviceObject = ConnectData->ClassDeviceObject;
                DeviceExtension->ClassService = ConnectData->ClassService;
                Status = STATUS_SUCCESS;
            }
            KeReleaseSpinLock(&TpadKeyLock, OldIrql);
            break;

        case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        case IOCTL_KEYBOARD_QUERY_INDICATORS:
            if (OutputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &DeviceExtension->Indicators, sizeof(KEYBOARD_INDICATOR_PARAMETERS));
            Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_KEYBOARD_SET_INDICATORS:
            if (InputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlCopyMemory(&DeviceExtension->Indicators, Irp->AssociatedIrp.SystemBuffer, sizeof(KEYBOARD_INDICATOR_PARAMETERS));
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
            if (OutputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &DeviceExtension->Typematic, sizeof(KEYBOARD_TYPEMATIC_PARAMETERS));
            Irp->IoStatus.Information = sizeof(KEYBOARD_TYPEMATIC_PARAMETERS);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_KEYBOARD_SET_TYPEMATIC:
            if (InputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlCopyMemory(&DeviceExtension->Typematic, Irp->AssociatedIrp.SystemBuffer, sizeof(KEYBOARD_TYPEMATIC_PARAMETERS));
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
            if (OutputLength < sizeof(KEYBOARD_INDICATOR_TRANSLATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer, sizeof(KEYBOARD_INDICATOR_TRANSLATION));
            Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_TRANSLATION);
            Status = STATUS_SUCCESS;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
TpadInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    PMOUSE_ATTRIBUTES Attributes;
    PCONNECT_DATA ConnectData;

    if (DeviceExtension->Role == TpadRoleConfiguration)
        return TpadKeyboardControl(DeviceExtension, Irp);

    Irp->IoStatus.Information = 0;

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_MOUSE_QUERY_ATTRIBUTES:
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUSE_ATTRIBUTES))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Attributes = Irp->AssociatedIrp.SystemBuffer;
            Attributes->MouseIdentifier = WHEELMOUSE_HID_HARDWARE | HORIZONTAL_WHEEL_PRESENT;
            Attributes->NumberOfButtons = 2;
            Attributes->SampleRate = 0;
            Attributes->InputDataQueueLength = 5;
            Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_MOUSE_CONNECT:
            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (DeviceExtension->ClassService)
            {
                Status = STATUS_SHARING_VIOLATION;
                break;
            }

            ConnectData = IoStack->Parameters.DeviceIoControl.Type3InputBuffer;
            DeviceExtension->ClassDeviceObject = ConnectData->ClassDeviceObject;
            DeviceExtension->ClassService = ConnectData->ClassService;
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_MOUSE_DISCONNECT:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
NTAPI
TpadModeWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    TpadForgetInputMode(DeviceExtension);
    TpadUpdateSiblings(DeviceExtension->ParentId, TRUE);
    InterlockedExchange(&DeviceExtension->ModeWorkQueued, 0);
    KeSetEvent(&DeviceExtension->ModeWorkIdle, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
TpadPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    SYSTEM_POWER_STATE State;

    if (DeviceExtension->Role == TpadRoleConfiguration && IoStack->MinorFunction == IRP_MN_SET_POWER && IoStack->Parameters.Power.Type == SystemPowerState)
    {
        State = IoStack->Parameters.Power.State.SystemState;
        if (State == PowerSystemWorking && DeviceExtension->SystemState != PowerSystemWorking && DeviceExtension->Started &&
            !InterlockedExchange(&DeviceExtension->ModeWorkQueued, 1))
        {
            KeClearEvent(&DeviceExtension->ModeWorkIdle);
            IoQueueWorkItem(DeviceExtension->ModeWorkItem, TpadModeWorker, DelayedWorkQueue, DeviceExtension);
        }

        DeviceExtension->SystemState = State;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
TpadPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            if (!IoForwardIrpSynchronously(DeviceExtension->NextDeviceObject, Irp))
                Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;

            Status = Irp->IoStatus.Status;
            if (NT_SUCCESS(Status))
            {
                Status = TpadStartDevice(DeviceExtension);
                if (NT_SUCCESS(Status))
                {
                    TpadSetStarted(DeviceExtension, TRUE);
                    TpadUpdateSiblings(DeviceExtension->ParentId, TRUE);
                }
                else
                {
                    DPRINT1("TPADHID: start failed, status=0x%08lx\n", Status);
                    TpadFreeResources(DeviceExtension);
                }
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            TpadStopDevice(DeviceExtension, TRUE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return TpadPassThrough(DeviceObject, Irp);

        case IRP_MN_SURPRISE_REMOVAL:
            TpadStopDevice(DeviceExtension, FALSE);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return TpadPassThrough(DeviceObject, Irp);

        case IRP_MN_REMOVE_DEVICE:
            TpadStopDevice(DeviceExtension, TRUE);

            KeWaitForSingleObject(&TpadListLock, Executive, KernelMode, FALSE, NULL);
            RemoveEntryList(&DeviceExtension->ListEntry);
            KeReleaseMutex(&TpadListLock, FALSE);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);

            if (DeviceExtension->ModeWorkItem)
            {
                KeWaitForSingleObject(&DeviceExtension->ModeWorkIdle, Executive, KernelMode, FALSE, NULL);
                IoFreeWorkItem(DeviceExtension->ModeWorkItem);
            }

            if (DeviceExtension->ReadIrp)
                IoFreeIrp(DeviceExtension->ReadIrp);
            ExFreePoolWithTag(DeviceExtension->ParentId, TPADHID_TAG);
            IoDetachDevice(DeviceExtension->NextDeviceObject);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return TpadPassThrough(DeviceObject, Irp);

        default:
            return TpadPassThrough(DeviceObject, Irp);
    }
}

static
TPADHID_ROLE
TpadQueryRole(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    TPADHID_ROLE Role = TpadRoleNone;
    ULONG Length = 0;
    NTSTATUS Status;
    PWSTR Buffer, Id;

    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, 0, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL || Length < 2 * sizeof(WCHAR))
        return TpadRoleNone;

    Buffer = ExAllocatePoolWithTag(PagedPool, Length, TPADHID_TAG);
    if (!Buffer)
        return TpadRoleNone;

    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, Length, Buffer, &Length);
    if (NT_SUCCESS(Status))
    {
        Buffer[Length / sizeof(WCHAR) - 1] = UNICODE_NULL;
        Buffer[Length / sizeof(WCHAR) - 2] = UNICODE_NULL;

        for (Id = Buffer; *Id && Role == TpadRoleNone; Id += wcslen(Id) + 1)
        {
            if (_wcsicmp(Id, TpadTouchpadId) == 0)
                Role = TpadRoleTouchpad;
            else if (_wcsicmp(Id, TpadConfigurationId) == 0)
                Role = TpadRoleConfiguration;
        }
    }

    ExFreePoolWithTag(Buffer, TPADHID_TAG);
    return Role;
}

static
NTSTATUS
TpadQueryParentId(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PWSTR *ParentId)
{
    DEVPROPTYPE Type;
    ULONG Length = 0;
    NTSTATUS Status;
    PWSTR Buffer;

    Status = IoGetDevicePropertyData(PhysicalDeviceObject, &DEVPKEY_Device_Parent, LOCALE_NEUTRAL, 0, 0, NULL, &Length, &Type);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;

    Buffer = ExAllocatePoolWithTag(PagedPool, Length + sizeof(WCHAR), TPADHID_TAG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoGetDevicePropertyData(PhysicalDeviceObject, &DEVPKEY_Device_Parent, LOCALE_NEUTRAL, 0, Length, Buffer, &Length, &Type);
    if (!NT_SUCCESS(Status) || Type != DEVPROP_TYPE_STRING)
    {
        ExFreePoolWithTag(Buffer, TPADHID_TAG);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;
    *ParentId = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
TpadAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PTPADHID_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    TPADHID_ROLE Role;
    NTSTATUS Status;
    PWSTR ParentId;

    Role = TpadQueryRole(PhysicalDeviceObject);
    if (Role == TpadRoleNone)
        return STATUS_NOT_SUPPORTED;

    Status = TpadQueryParentId(PhysicalDeviceObject, &ParentId);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateDevice(DriverObject, sizeof(TPADHID_DEVICE_EXTENSION), NULL, Role == TpadRoleTouchpad ? FILE_DEVICE_MOUSE : FILE_DEVICE_KEYBOARD, 0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ParentId, TPADHID_TAG);
        return Status;
    }

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Role = Role;
    DeviceExtension->ParentId = ParentId;
    DeviceExtension->InputMode = TPADHID_MODE_UNKNOWN;
    DeviceExtension->SystemState = PowerSystemWorking;
    KeInitializeSpinLock(&DeviceExtension->ReadLock);
    KeInitializeEvent(&DeviceExtension->ReadIdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&DeviceExtension->ModeWorkIdle, NotificationEvent, TRUE);
    KeInitializeSpinLock(&DeviceExtension->GestureLock);
    KeInitializeTimer(&DeviceExtension->TapTimer);
    KeInitializeDpc(&DeviceExtension->TapDpc, TpadTapDpc, DeviceExtension);

    DeviceExtension->NextDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!DeviceExtension->NextDeviceObject)
    {
        ExFreePoolWithTag(ParentId, TPADHID_TAG);
        IoDeleteDevice(DeviceObject);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    if (Role == TpadRoleTouchpad)
        DeviceExtension->ReadIrp = IoAllocateIrp(DeviceExtension->NextDeviceObject->StackSize, FALSE);
    else
        DeviceExtension->ModeWorkItem = IoAllocateWorkItem(DeviceObject);

    if (!DeviceExtension->ReadIrp && !DeviceExtension->ModeWorkItem)
    {
        IoDetachDevice(DeviceExtension->NextDeviceObject);
        ExFreePoolWithTag(ParentId, TPADHID_TAG);
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeWaitForSingleObject(&TpadListLock, Executive, KernelMode, FALSE, NULL);
    InsertTailList(&TpadDeviceList, &DeviceExtension->ListEntry);
    KeReleaseMutex(&TpadListLock, FALSE);

    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
TpadUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    KeInitializeMutex(&TpadListLock, 0);
    KeInitializeSpinLock(&TpadKeyLock);
    InitializeListHead(&TpadDeviceList);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = TpadCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = TpadClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = TpadPassThrough;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = TpadPassThrough;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = TpadInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = TpadPassThrough;
    DriverObject->MajorFunction[IRP_MJ_POWER] = TpadPower;
    DriverObject->MajorFunction[IRP_MJ_PNP] = TpadPnp;
    DriverObject->DriverUnload = TpadUnload;
    DriverObject->DriverExtension->AddDevice = TpadAddDevice;
    return STATUS_SUCCESS;
}
