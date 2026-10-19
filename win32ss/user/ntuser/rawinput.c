/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raw input devices, registrations and WM_INPUT delivery
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <win32k.h>
#define _HIDPI_NO_FUNCTION_MACROS_
#include <hidclass.h>
#include <hidpi.h>
#include <hidusage.h>

#define RAWINPUT_MAX_PENDING 10000

typedef struct _RAWINPUT_DEVICE_OBJECT
{
    HEAD head;
    LIST_ENTRY ListEntry;
    UNICODE_STRING Path;
    RID_DEVICE_INFO Info;
    PVOID PreparsedData;
    ULONG PreparsedLength;
    ULONG InputLength;
    BOOLEAN Present;
    BOOLEAN Hidden;
} RAWINPUT_DEVICE_OBJECT, *PRAWINPUT_DEVICE_OBJECT;

typedef struct _RAWINPUT_HID_ARRIVAL
{
    LIST_ENTRY ListEntry;
    PWSTR Link;
    SIZE_T cchLink;
    RID_DEVICE_INFO Info;
    PVOID PreparsedData;
    ULONG PreparsedLength;
    ULONG InputLength;
    BOOLEAN Hidden;
} RAWINPUT_HID_ARRIVAL, *PRAWINPUT_HID_ARRIVAL;

static const GUID RawInputMouseInterface =
    { 0x378de44c, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };
static const GUID RawInputKeyboardInterface =
    { 0x884b96c3, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };
static const GUID RawInputHidInterface =
    { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static LIST_ENTRY gRawInputDevices = { &gRawInputDevices, &gRawInputDevices };
static MOUSE_ATTRIBUTES gRawInputMouseAttributes;
static LONG gRawInputSequence;

VOID NTAPI
RawInputSetMouseAttributes(HANDLE hMouseDevice)
{
    IO_STATUS_BLOCK Iosb;
    MOUSE_ATTRIBUTES Attributes;
    NTSTATUS Status;

    Status = ZwDeviceIoControlFile(hMouseDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_MOUSE_QUERY_ATTRIBUTES,
                                   NULL,
                                   0,
                                   &Attributes,
                                   sizeof(Attributes));
    if (NT_SUCCESS(Status))
        gRawInputMouseAttributes = Attributes;
}

static VOID
RawInputFillDeviceInfo(PRID_DEVICE_INFO Info, DWORD dwType)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->cbSize = sizeof(*Info);
    Info->dwType = dwType;

    if (dwType == RIM_TYPEMOUSE)
    {
        Info->mouse.dwId = gRawInputMouseAttributes.MouseIdentifier;
        Info->mouse.dwNumberOfButtons = gRawInputMouseAttributes.NumberOfButtons;
        Info->mouse.dwSampleRate = gRawInputMouseAttributes.SampleRate;
        Info->mouse.fHasHorizontalWheel = FALSE;
    }
    else
    {
        Info->keyboard.dwType = gKeyboardInfo.KeyboardIdentifier.Type;
        Info->keyboard.dwSubType = gKeyboardInfo.KeyboardIdentifier.Subtype;
        Info->keyboard.dwKeyboardMode = gKeyboardInfo.KeyboardMode;
        Info->keyboard.dwNumberOfFunctionKeys = gKeyboardInfo.NumberOfFunctionKeys;
        Info->keyboard.dwNumberOfIndicators = gKeyboardInfo.NumberOfIndicators;
        Info->keyboard.dwNumberOfKeysTotal = gKeyboardInfo.NumberOfKeysTotal;
    }
}

static VOID
RawInputDeviceUsage(PRAWINPUT_DEVICE_OBJECT Device, PUSHORT pUsagePage, PUSHORT pUsage)
{
    if (Device->Info.dwType == RIM_TYPEHID)
    {
        *pUsagePage = Device->Info.hid.usUsagePage;
        *pUsage = Device->Info.hid.usUsage;
    }
    else
    {
        *pUsagePage = HID_USAGE_PAGE_GENERIC;
        *pUsage = (Device->Info.dwType == RIM_TYPEMOUSE) ? HID_USAGE_GENERIC_MOUSE : HID_USAGE_GENERIC_KEYBOARD;
    }
}

static PRAWINPUTDEVICE
RawInputFindRegistration(PPROCESSINFO ppi, USHORT UsagePage, USHORT Usage);

static VOID
RawInputPostDeviceChange(PRAWINPUT_DEVICE_OBJECT Device, WPARAM Change)
{
    PPROCESSINFO ppi;
    PRAWINPUTDEVICE Registration;
    USHORT UsagePage, Usage;

    if (Device->Hidden)
        return;

    RawInputDeviceUsage(Device, &UsagePage, &Usage);
    for (ppi = gppiList; ppi; ppi = ppi->ppiNext)
    {
        if (!ppi->cRawInputDevices)
            continue;
        Registration = RawInputFindRegistration(ppi, UsagePage, Usage);
        if (Registration && (Registration->dwFlags & RIDEV_DEVNOTIFY) && Registration->hwndTarget)
            UserPostMessage(Registration->hwndTarget, WM_INPUT_DEVICE_CHANGE, Change, (LPARAM)UserHMGetHandle(Device));
    }
}

static VOID
RawInputDeleteDevice(PRAWINPUT_DEVICE_OBJECT Device)
{
    RawInputPostDeviceChange(Device, GIDC_REMOVAL);
    RemoveEntryList(&Device->ListEntry);
    ExFreePoolWithTag(Device->Path.Buffer, USERTAG_DEVICEINFO);
    if (Device->PreparsedData)
        ExFreePoolWithTag(Device->PreparsedData, USERTAG_HIDDESC);
    UserDereferenceObject(Device);
    UserDeleteObject(UserHMGetHandle(Device), TYPE_DEVICEINFO);
}

static PRAWINPUT_DEVICE_OBJECT
RawInputFindDevicePath(DWORD dwType, PCWSTR pszLink, SIZE_T cchLink)
{
    PLIST_ENTRY Entry;
    PRAWINPUT_DEVICE_OBJECT Device;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType != dwType ||
            Device->Path.Length != cchLink * sizeof(WCHAR) ||
            cchLink < 2)
        {
            continue;
        }
        if (Device->Path.Buffer[0] == pszLink[0] &&
            _wcsnicmp(Device->Path.Buffer + 2, pszLink + 2, cchLink - 2) == 0)
        {
            return Device;
        }
    }
    return NULL;
}

static VOID
RawInputMergeInterfaces(DWORD dwType, PWSTR pszList)
{
    PLIST_ENTRY Entry, Next;
    PRAWINPUT_DEVICE_OBJECT Device;
    PWSTR pszLink;
    SIZE_T cchLink;
    HANDLE hDevice;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType == dwType)
            Device->Present = FALSE;
    }

    for (pszLink = pszList; *pszLink; pszLink += cchLink + 1)
    {
        cchLink = wcslen(pszLink);
        if (cchLink < 4 || cchLink >= UNICODE_STRING_MAX_CHARS)
            continue;

        Device = RawInputFindDevicePath(dwType, pszLink, cchLink);
        if (Device)
        {
            Device->Present = TRUE;
            RawInputFillDeviceInfo(&Device->Info, dwType);
            continue;
        }

        Device = UserCreateObject(gHandleTable, NULL, NULL, &hDevice, TYPE_DEVICEINFO, sizeof(*Device));
        if (!Device)
            break;

        Device->Path.Buffer = ExAllocatePoolWithTag(PagedPool, (cchLink + 1) * sizeof(WCHAR), USERTAG_DEVICEINFO);
        if (!Device->Path.Buffer)
        {
            UserDereferenceObject(Device);
            UserDeleteObject(hDevice, TYPE_DEVICEINFO);
            break;
        }
        RtlCopyMemory(Device->Path.Buffer, pszLink, (cchLink + 1) * sizeof(WCHAR));
        Device->Path.Buffer[1] = L'\\';
        Device->Path.Length = (USHORT)(cchLink * sizeof(WCHAR));
        Device->Path.MaximumLength = Device->Path.Length + sizeof(WCHAR);
        RawInputFillDeviceInfo(&Device->Info, dwType);
        Device->Present = TRUE;
        InsertTailList(&gRawInputDevices, &Device->ListEntry);
        RawInputPostDeviceChange(Device, GIDC_ARRIVAL);
    }

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Next)
    {
        Next = Entry->Flink;
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType == dwType && !Device->Present)
            RawInputDeleteDevice(Device);
    }
}

static VOID
RawInputFreeHidArrival(PRAWINPUT_HID_ARRIVAL Arrival)
{
    if (Arrival->PreparsedData)
        ExFreePoolWithTag(Arrival->PreparsedData, USERTAG_HIDDESC);
    ExFreePoolWithTag(Arrival, USERTAG_DEVICEINFO);
}

static VOID
RawInputQueryHidArrivals(PWSTR pszList, PLIST_ENTRY Arrivals)
{
    HID_COLLECTION_INFORMATION Information;
    PHIDP_PREPARSED_DATA PreparsedData;
    PRAWINPUT_HID_ARRIVAL Arrival;
    UNICODE_STRING Path;
    HIDP_CAPS Caps;
    PWSTR pszLink;
    SIZE_T cchLink;
    BOOLEAN Known;

    for (pszLink = pszList; *pszLink; pszLink += cchLink + 1)
    {
        cchLink = wcslen(pszLink);
        if (cchLink < 4 || cchLink >= UNICODE_STRING_MAX_CHARS)
            continue;

        UserEnterShared();
        Known = RawInputFindDevicePath(RIM_TYPEHID, pszLink, cchLink) != NULL;
        UserLeave();
        if (Known)
            continue;

        Path.Buffer = pszLink;
        Path.Length = Path.MaximumLength = (USHORT)(cchLink * sizeof(WCHAR));
        if (!NT_SUCCESS(HidInputQueryCollection(&Path, &Information, &PreparsedData, &Caps)))
            continue;

        Arrival = ExAllocatePoolZero(PagedPool, sizeof(*Arrival), USERTAG_DEVICEINFO);
        if (!Arrival)
        {
            ExFreePoolWithTag(PreparsedData, USERTAG_HIDDESC);
            continue;
        }
        Arrival->Link = pszLink;
        Arrival->cchLink = cchLink;
        Arrival->Info.cbSize = sizeof(Arrival->Info);
        Arrival->Info.dwType = RIM_TYPEHID;
        Arrival->Info.hid.dwVendorId = Information.VendorID;
        Arrival->Info.hid.dwProductId = Information.ProductID;
        Arrival->Info.hid.dwVersionNumber = Information.VersionNumber;
        Arrival->Info.hid.usUsagePage = Caps.UsagePage;
        Arrival->Info.hid.usUsage = Caps.Usage;
        Arrival->PreparsedData = PreparsedData;
        Arrival->PreparsedLength = Information.DescriptorSize;
        Arrival->InputLength = Caps.InputReportByteLength;
        Arrival->Hidden = Caps.UsagePage == HID_USAGE_PAGE_GENERIC &&
                          (Caps.Usage == HID_USAGE_GENERIC_MOUSE || Caps.Usage == HID_USAGE_GENERIC_KEYBOARD);
        InsertTailList(Arrivals, &Arrival->ListEntry);
    }
}

static VOID
RawInputMergeHid(PWSTR pszList, PLIST_ENTRY Arrivals)
{
    PLIST_ENTRY Entry, Next;
    PRAWINPUT_DEVICE_OBJECT Device;
    PRAWINPUT_HID_ARRIVAL Arrival;
    PWSTR pszLink;
    SIZE_T cchLink;
    HANDLE hDevice;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType == RIM_TYPEHID)
            Device->Present = FALSE;
    }

    for (pszLink = pszList; *pszLink; pszLink += cchLink + 1)
    {
        cchLink = wcslen(pszLink);
        Device = RawInputFindDevicePath(RIM_TYPEHID, pszLink, cchLink);
        if (Device)
            Device->Present = TRUE;
    }

    while (!IsListEmpty(Arrivals))
    {
        Arrival = CONTAINING_RECORD(RemoveHeadList(Arrivals), RAWINPUT_HID_ARRIVAL, ListEntry);
        if (RawInputFindDevicePath(RIM_TYPEHID, Arrival->Link, Arrival->cchLink))
        {
            RawInputFreeHidArrival(Arrival);
            continue;
        }

        Device = UserCreateObject(gHandleTable, NULL, NULL, &hDevice, TYPE_DEVICEINFO, sizeof(*Device));
        if (!Device)
        {
            RawInputFreeHidArrival(Arrival);
            continue;
        }
        Device->Path.Buffer = ExAllocatePoolWithTag(PagedPool, (Arrival->cchLink + 1) * sizeof(WCHAR), USERTAG_DEVICEINFO);
        if (!Device->Path.Buffer)
        {
            UserDereferenceObject(Device);
            UserDeleteObject(hDevice, TYPE_DEVICEINFO);
            RawInputFreeHidArrival(Arrival);
            continue;
        }
        RtlCopyMemory(Device->Path.Buffer, Arrival->Link, (Arrival->cchLink + 1) * sizeof(WCHAR));
        Device->Path.Buffer[1] = L'\\';
        Device->Path.Length = (USHORT)(Arrival->cchLink * sizeof(WCHAR));
        Device->Path.MaximumLength = Device->Path.Length + sizeof(WCHAR);
        Device->Info = Arrival->Info;
        Device->PreparsedData = Arrival->PreparsedData;
        Device->PreparsedLength = Arrival->PreparsedLength;
        Device->InputLength = Arrival->InputLength;
        Device->Hidden = Arrival->Hidden;
        Device->Present = TRUE;
        Arrival->PreparsedData = NULL;
        RawInputFreeHidArrival(Arrival);
        InsertTailList(&gRawInputDevices, &Device->ListEntry);
        RawInputPostDeviceChange(Device, GIDC_ARRIVAL);
    }

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Next)
    {
        Next = Entry->Flink;
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType == RIM_TYPEHID && !Device->Present)
            RawInputDeleteDevice(Device);
    }
}

VOID NTAPI
RawInputUpdateDevices(VOID)
{
    PWSTR pszMice = NULL, pszKeyboards = NULL, pszHid = NULL;
    LIST_ENTRY HidArrivals;

    InitializeListHead(&HidArrivals);
    if (!NT_SUCCESS(IoGetDeviceInterfaces(&RawInputMouseInterface, NULL, 0, &pszMice)))
        pszMice = NULL;
    if (!NT_SUCCESS(IoGetDeviceInterfaces(&RawInputKeyboardInterface, NULL, 0, &pszKeyboards)))
        pszKeyboards = NULL;
    if (!NT_SUCCESS(IoGetDeviceInterfaces(&RawInputHidInterface, NULL, 0, &pszHid)))
        pszHid = NULL;
    if (pszHid)
        RawInputQueryHidArrivals(pszHid, &HidArrivals);

    UserEnterExclusive();
    if (pszMice)
        RawInputMergeInterfaces(RIM_TYPEMOUSE, pszMice);
    if (pszKeyboards)
        RawInputMergeInterfaces(RIM_TYPEKEYBOARD, pszKeyboards);
    if (pszHid)
        RawInputMergeHid(pszHid, &HidArrivals);
    UserLeave();

    if (pszMice)
        ExFreePool(pszMice);
    if (pszKeyboards)
        ExFreePool(pszKeyboards);
    if (pszHid)
        ExFreePool(pszHid);
    HidInputRawInputChanged();
}

static HANDLE
RawInputFirstDevice(DWORD dwType)
{
    PLIST_ENTRY Entry;
    PRAWINPUT_DEVICE_OBJECT Device;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType == dwType)
            return UserHMGetHandle(Device);
    }
    return NULL;
}

static PRAWINPUTDEVICE
RawInputFindRegistration(PPROCESSINFO ppi, USHORT UsagePage, USHORT Usage)
{
    PRAWINPUTDEVICE Device, PageOnly = NULL;
    UINT i;

    for (i = 0; i < ppi->cRawInputDevices; i++)
    {
        Device = &ppi->pRawInputDevices[i];
        if (Device->usUsagePage != UsagePage)
            continue;
        if (Device->usUsage == Usage)
            return ((Device->dwFlags & RIDEV_EXMODEMASK) == RIDEV_EXCLUDE) ? NULL : Device;
        if ((Device->dwFlags & RIDEV_EXMODEMASK) == RIDEV_PAGEONLY)
            PageOnly = Device;
    }
    return PageOnly;
}

BOOL FASTCALL
RawInputIsNoLegacy(PPROCESSINFO ppi, DWORD dwType)
{
    PRAWINPUTDEVICE Device;

    if (!ppi || !ppi->cRawInputDevices)
        return FALSE;

    Device = RawInputFindRegistration(ppi,
                                      HID_USAGE_PAGE_GENERIC,
                                      dwType == RIM_TYPEMOUSE ? HID_USAGE_GENERIC_MOUSE : HID_USAGE_GENERIC_KEYBOARD);
    return Device && (Device->dwFlags & RIDEV_EXMODEMASK) == RIDEV_NOLEGACY;
}

static HRAWINPUT
RawInputNextHandle(VOID)
{
    LONG Sequence;

    do
    {
        Sequence = InterlockedIncrement(&gRawInputSequence);
    } while (!Sequence);

    return (HRAWINPUT)(ULONG_PTR)(ULONG)Sequence;
}

static VOID
RawInputDispatch(const RAWINPUT *Template, USHORT UsagePage, USHORT Usage, ULONG_PTR ExtraInfo, BOOL bInjected, DWORD dwTime)
{
    PUSER_MESSAGE_QUEUE pqFocus;
    PWND pwndForeground, pwnd;
    PPROCESSINFO ppi, ppiForeground;
    PRAWINPUTDEVICE Device;
    PRAWINPUT RawInput;
    INPUT_MESSAGE_SOURCE Source;
    BOOL bForegroundRegistered;
    WPARAM wParam;
    MSG Msg;

    pqFocus = IntGetFocusMessageQueue();
    if (!pqFocus)
        return;
    pwndForeground = pqFocus->spwndFocus ? pqFocus->spwndFocus : pqFocus->spwndActive;
    if (!pwndForeground)
        return;
    ppiForeground = pwndForeground->head.pti->ppi;
    bForegroundRegistered = RawInputFindRegistration(ppiForeground, UsagePage, Usage) != NULL;

    if (Template->header.dwType == RIM_TYPEMOUSE)
        Source.deviceType = IMDT_MOUSE;
    else if (Template->header.dwType == RIM_TYPEKEYBOARD)
        Source.deviceType = IMDT_KEYBOARD;
    else
        Source.deviceType = IMDT_UNAVAILABLE;
    Source.originId = bInjected ? IMO_INJECTED : IMO_HARDWARE;

    for (ppi = gppiList; ppi; ppi = ppi->ppiNext)
    {
        if (!ppi->cRawInputDevices)
            continue;
        Device = RawInputFindRegistration(ppi, UsagePage, Usage);
        if (!Device)
            continue;

        if (ppi == ppiForeground)
        {
            wParam = RIM_INPUT;
            pwnd = Device->hwndTarget ? ValidateHwndNoErr(Device->hwndTarget) : pqFocus->spwndFocus;
        }
        else
        {
            if (!(Device->dwFlags & RIDEV_INPUTSINK) &&
                !((Device->dwFlags & RIDEV_EXINPUTSINK) && !bForegroundRegistered))
            {
                continue;
            }
            wParam = RIM_INPUTSINK;
            pwnd = ValidateHwndNoErr(Device->hwndTarget);
            if (pwnd && pwnd->head.rpdesk != gpdeskInputDesktop)
                pwnd = NULL;
        }

        if (!pwnd || (pwnd->state2 & WNDS2_INDESTROY))
            continue;
        if (pwnd->head.pti->cRawInputPending >= RAWINPUT_MAX_PENDING)
            continue;

        RawInput = ExAllocatePoolWithTag(PagedPool, Template->header.dwSize, USERTAG_HIDDATA);
        if (!RawInput)
            continue;
        RtlCopyMemory(RawInput, Template, Template->header.dwSize);
        RawInput->header.wParam = wParam;

        Msg.hwnd = UserHMGetHandle(pwnd);
        Msg.message = WM_INPUT;
        Msg.wParam = wParam;
        Msg.lParam = (LPARAM)RawInputNextHandle();
        Msg.time = dwTime;
        Msg.pt = gpsi->ptCursor;

        if (!MsqPostRawInputMessage(pwnd->head.pti, &Msg, RawInput, ExtraInfo, &Source))
            ExFreePoolWithTag(RawInput, USERTAG_HIDDATA);
    }
}

VOID NTAPI
RawInputProcessKeyboard(WORD wScanCode, WORD wVk, DWORD dwFlags, UINT uMsg, ULONG_PTR dwExtraInfo, BOOL bInjected, DWORD dwTime)
{
    RAWINPUT Raw;

    RtlZeroMemory(&Raw, sizeof(Raw));
    Raw.header.dwType = RIM_TYPEKEYBOARD;
    Raw.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    Raw.header.hDevice = bInjected ? NULL : RawInputFirstDevice(RIM_TYPEKEYBOARD);
    Raw.data.keyboard.MakeCode = wScanCode;
    Raw.data.keyboard.Flags = (dwFlags & KEYEVENTF_KEYUP) ? RI_KEY_BREAK : RI_KEY_MAKE;
    if ((dwFlags & KEYEVENTF_EXTENDEDKEY) && wVk != VK_SHIFT)
        Raw.data.keyboard.Flags |= RI_KEY_E0;
    Raw.data.keyboard.VKey = wVk;
    Raw.data.keyboard.Message = uMsg;
    Raw.data.keyboard.ExtraInformation = (ULONG)dwExtraInfo;

    RawInputDispatch(&Raw, HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_KEYBOARD, dwExtraInfo, bInjected, dwTime);
}

VOID NTAPI
RawInputProcessMouseData(PMOUSE_INPUT_DATA pMouseInputData)
{
    RAWINPUT Raw;

    RtlZeroMemory(&Raw, sizeof(Raw));
    Raw.header.dwType = RIM_TYPEMOUSE;
    Raw.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    Raw.header.hDevice = RawInputFirstDevice(RIM_TYPEMOUSE);
    Raw.data.mouse.usFlags = pMouseInputData->Flags;
    Raw.data.mouse.usButtonFlags = pMouseInputData->ButtonFlags;
    Raw.data.mouse.usButtonData = pMouseInputData->ButtonData;
    Raw.data.mouse.ulRawButtons = pMouseInputData->RawButtons;
    Raw.data.mouse.lLastX = pMouseInputData->LastX;
    Raw.data.mouse.lLastY = pMouseInputData->LastY;
    Raw.data.mouse.ulExtraInformation = pMouseInputData->ExtraInformation;

    RawInputDispatch(&Raw, HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MOUSE, pMouseInputData->ExtraInformation, FALSE, EngGetTickCount32());
}

VOID NTAPI
RawInputProcessMouseInput(const MOUSEINPUT *pMouseInput)
{
    RAWINPUT Raw;
    DWORD dwFlags = pMouseInput->dwFlags;
    USHORT usButtonFlags = 0;

    if (dwFlags & MOUSEEVENTF_LEFTDOWN)   usButtonFlags |= RI_MOUSE_LEFT_BUTTON_DOWN;
    if (dwFlags & MOUSEEVENTF_LEFTUP)     usButtonFlags |= RI_MOUSE_LEFT_BUTTON_UP;
    if (dwFlags & MOUSEEVENTF_RIGHTDOWN)  usButtonFlags |= RI_MOUSE_RIGHT_BUTTON_DOWN;
    if (dwFlags & MOUSEEVENTF_RIGHTUP)    usButtonFlags |= RI_MOUSE_RIGHT_BUTTON_UP;
    if (dwFlags & MOUSEEVENTF_MIDDLEDOWN) usButtonFlags |= RI_MOUSE_MIDDLE_BUTTON_DOWN;
    if (dwFlags & MOUSEEVENTF_MIDDLEUP)   usButtonFlags |= RI_MOUSE_MIDDLE_BUTTON_UP;
    if (dwFlags & MOUSEEVENTF_XDOWN)
    {
        if (pMouseInput->mouseData & XBUTTON1) usButtonFlags |= RI_MOUSE_BUTTON_4_DOWN;
        if (pMouseInput->mouseData & XBUTTON2) usButtonFlags |= RI_MOUSE_BUTTON_5_DOWN;
    }
    if (dwFlags & MOUSEEVENTF_XUP)
    {
        if (pMouseInput->mouseData & XBUTTON1) usButtonFlags |= RI_MOUSE_BUTTON_4_UP;
        if (pMouseInput->mouseData & XBUTTON2) usButtonFlags |= RI_MOUSE_BUTTON_5_UP;
    }
    if (dwFlags & MOUSEEVENTF_WHEEL)  usButtonFlags |= RI_MOUSE_WHEEL;
    if (dwFlags & MOUSEEVENTF_HWHEEL) usButtonFlags |= RI_MOUSE_HWHEEL;

    RtlZeroMemory(&Raw, sizeof(Raw));
    Raw.header.dwType = RIM_TYPEMOUSE;
    Raw.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    Raw.header.hDevice = NULL;
    if (dwFlags & MOUSEEVENTF_ABSOLUTE)
    {
        Raw.data.mouse.usFlags = MOUSE_MOVE_ABSOLUTE;
        if (dwFlags & MOUSEEVENTF_VIRTUALDESK)
            Raw.data.mouse.usFlags |= MOUSE_VIRTUAL_DESKTOP;
    }
    else
    {
        Raw.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    }
    Raw.data.mouse.usButtonFlags = usButtonFlags;
    if (dwFlags & (MOUSEEVENTF_WHEEL | MOUSEEVENTF_HWHEEL))
        Raw.data.mouse.usButtonData = (USHORT)pMouseInput->mouseData;
    if (dwFlags & MOUSEEVENTF_MOVE)
    {
        Raw.data.mouse.lLastX = pMouseInput->dx;
        Raw.data.mouse.lLastY = pMouseInput->dy;
    }
    Raw.data.mouse.ulExtraInformation = (ULONG)pMouseInput->dwExtraInfo;

    if (!Raw.data.mouse.lLastX && !Raw.data.mouse.lLastY && !usButtonFlags)
        return;

    RawInputDispatch(&Raw,
                     HID_USAGE_PAGE_GENERIC,
                     HID_USAGE_GENERIC_MOUSE,
                     pMouseInput->dwExtraInfo,
                     TRUE,
                     pMouseInput->time ? pMouseInput->time : EngGetTickCount32());
}

VOID NTAPI
RawInputProcessHidReport(HANDLE hDevice, const UCHAR *Report, ULONG Length)
{
    PRAWINPUT_DEVICE_OBJECT Device;
    PRAWINPUT Raw;
    ULONG Size;

    Device = UserGetObject(gHandleTable, hDevice, TYPE_DEVICEINFO);
    if (!Device || Device->Info.dwType != RIM_TYPEHID || Device->Hidden || !Length)
        return;

    Size = FIELD_OFFSET(RAWINPUT, data.hid.bRawData) + Length;
    Raw = ExAllocatePoolZero(PagedPool, Size, USERTAG_HIDDATA);
    if (!Raw)
        return;
    Raw->header.dwType = RIM_TYPEHID;
    Raw->header.dwSize = Size;
    Raw->header.hDevice = hDevice;
    Raw->data.hid.dwSizeHid = Length;
    Raw->data.hid.dwCount = 1;
    RtlCopyMemory(Raw->data.hid.bRawData, Report, Length);

    RawInputDispatch(Raw, Device->Info.hid.usUsagePage, Device->Info.hid.usUsage, 0, FALSE, EngGetTickCount32());
    ExFreePoolWithTag(Raw, USERTAG_HIDDATA);
}

ULONG NTAPI
RawInputGetHidTargets(PRAWINPUT_HID_TARGET Targets, ULONG MaxTargets)
{
    PLIST_ENTRY Entry;
    PRAWINPUT_DEVICE_OBJECT Device;
    PPROCESSINFO ppi;
    ULONG Count = 0;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices && Count < MaxTargets; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Info.dwType != RIM_TYPEHID || Device->Hidden || !Device->InputLength)
            continue;

        for (ppi = gppiList; ppi; ppi = ppi->ppiNext)
        {
            if (ppi->cRawInputDevices &&
                RawInputFindRegistration(ppi, Device->Info.hid.usUsagePage, Device->Info.hid.usUsage))
            {
                break;
            }
        }
        if (!ppi)
            continue;

        Targets[Count].Path.Buffer = ExAllocatePoolWithTag(PagedPool, Device->Path.MaximumLength, USERTAG_HIDDESC);
        if (!Targets[Count].Path.Buffer)
            continue;
        RtlCopyMemory(Targets[Count].Path.Buffer, Device->Path.Buffer, Device->Path.MaximumLength);
        Targets[Count].Path.Buffer[1] = L'?';
        Targets[Count].Path.Length = Device->Path.Length;
        Targets[Count].Path.MaximumLength = Device->Path.MaximumLength;
        Targets[Count].hDevice = UserHMGetHandle(Device);
        Targets[Count].ReportLength = Device->InputLength;
        Count++;
    }
    return Count;
}

VOID FASTCALL
RawInputSetThreadData(PTHREADINFO pti, PUSER_MESSAGE Message, BOOL bRemove)
{
    PRAWINPUT Data;

    if (bRemove)
    {
        Data = Message->RawInput;
        Message->RawInput = NULL;
        InterlockedDecrement((PLONG)&pti->cRawInputPending);
    }
    else
    {
        Data = ExAllocatePoolWithTag(PagedPool, Message->RawInput->header.dwSize, USERTAG_HIDDATA);
        if (!Data)
            return;
        RtlCopyMemory(Data, Message->RawInput, Message->RawInput->header.dwSize);
    }

    if (pti->pRawInputData)
        ExFreePoolWithTag(pti->pRawInputData, USERTAG_HIDDATA);
    pti->pRawInputData = Data;
    pti->hRawInputData = (HRAWINPUT)Message->Msg.lParam;
}

VOID FASTCALL
RawInputCleanupThread(PTHREADINFO pti)
{
    if (pti->pRawInputData)
    {
        ExFreePoolWithTag(pti->pRawInputData, USERTAG_HIDDATA);
        pti->pRawInputData = NULL;
    }
    pti->hRawInputData = NULL;
}

VOID FASTCALL
RawInputCleanupProcess(PPROCESSINFO ppi)
{
    if (ppi->pRawInputDevices)
    {
        ExFreePoolWithTag(ppi->pRawInputDevices, USERTAG_HIDDESC);
        ppi->pRawInputDevices = NULL;
        HidInputRawInputChanged();
    }
    ppi->cRawInputDevices = 0;
}

static VOID
RawInputPostDeviceArrivals(const RAWINPUTDEVICE *Filter)
{
    PLIST_ENTRY Entry;
    PRAWINPUT_DEVICE_OBJECT Device;
    USHORT UsagePage, Usage;

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
        if (Device->Hidden)
            continue;
        RawInputDeviceUsage(Device, &UsagePage, &Usage);
        if (Filter->usUsagePage != UsagePage ||
            (Filter->usUsage != Usage && (Filter->dwFlags & RIDEV_EXMODEMASK) != RIDEV_PAGEONLY))
        {
            continue;
        }
        UserPostMessage(Filter->hwndTarget, WM_INPUT_DEVICE_CHANGE, GIDC_ARRIVAL, (LPARAM)UserHMGetHandle(Device));
    }
}

static VOID
RawInputRegisterDevice(PRAWINPUTDEVICE Devices, UINT *pcDevices, const RAWINPUTDEVICE *Device)
{
    PRAWINPUTDEVICE Pos, End;

    for (Pos = Devices, End = Devices + *pcDevices; Pos != End; Pos++)
    {
        if (Pos->usUsagePage < Device->usUsagePage)
            continue;
        if (Pos->usUsagePage > Device->usUsagePage)
            break;
        if (Pos->usUsage >= Device->usUsage)
            break;
    }

    if (Device->dwFlags & RIDEV_REMOVE)
    {
        if (Pos != End && Pos->usUsagePage == Device->usUsagePage && Pos->usUsage == Device->usUsage)
        {
            RtlMoveMemory(Pos, Pos + 1, (End - (Pos + 1)) * sizeof(*Pos));
            (*pcDevices)--;
        }
        return;
    }

    if ((Device->dwFlags & RIDEV_DEVNOTIFY) && Device->hwndTarget)
        RawInputPostDeviceArrivals(Device);

    if (Pos == End || Pos->usUsagePage != Device->usUsagePage || Pos->usUsage != Device->usUsage)
    {
        RtlMoveMemory(Pos + 1, Pos, (End - Pos) * sizeof(*Pos));
        (*pcDevices)++;
    }
    *Pos = *Device;
}

BOOL
APIENTRY
NtUserRegisterRawInputDevices(
    IN PCRAWINPUTDEVICE pRawInputDevices,
    IN UINT uiNumDevices,
    IN UINT cbSize)
{
    PPROCESSINFO ppi;
    PRAWINPUTDEVICE Local = NULL, NewDevices;
    UINT i, cDevices;
    BOOL Ret = FALSE;

    if (cbSize != sizeof(RAWINPUTDEVICE) || uiNumDevices > MAXULONG / sizeof(RAWINPUTDEVICE) / 2)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (uiNumDevices)
    {
        Local = ExAllocatePoolWithTag(PagedPool, uiNumDevices * sizeof(RAWINPUTDEVICE), USERTAG_HIDDESC);
        if (!Local)
        {
            EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        _SEH2_TRY
        {
            ProbeForRead(pRawInputDevices, uiNumDevices * sizeof(RAWINPUTDEVICE), 1);
            RtlCopyMemory(Local, pRawInputDevices, uiNumDevices * sizeof(RAWINPUTDEVICE));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
            _SEH2_YIELD(goto Free);
        }
        _SEH2_END;
    }

    for (i = 0; i < uiNumDevices; i++)
    {
        if (((Local[i].dwFlags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) && !Local[i].hwndTarget) ||
            ((Local[i].dwFlags & RIDEV_REMOVE) && Local[i].hwndTarget))
        {
            EngSetLastError(ERROR_INVALID_PARAMETER);
            goto Free;
        }
    }

    RawInputUpdateDevices();

    UserEnterExclusive();
    ppi = GetW32ProcessInfo();

    cDevices = ppi->cRawInputDevices;
    if (!cDevices && !uiNumDevices)
    {
        Ret = TRUE;
        goto Leave;
    }

    NewDevices = ExAllocatePoolWithTag(PagedPool, (cDevices + uiNumDevices) * sizeof(RAWINPUTDEVICE), USERTAG_HIDDESC);
    if (!NewDevices)
    {
        EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto Leave;
    }
    if (cDevices)
        RtlCopyMemory(NewDevices, ppi->pRawInputDevices, cDevices * sizeof(RAWINPUTDEVICE));

    for (i = 0; i < uiNumDevices; i++)
        RawInputRegisterDevice(NewDevices, &cDevices, &Local[i]);

    if (ppi->pRawInputDevices)
        ExFreePoolWithTag(ppi->pRawInputDevices, USERTAG_HIDDESC);
    if (cDevices)
    {
        ppi->pRawInputDevices = NewDevices;
    }
    else
    {
        ExFreePoolWithTag(NewDevices, USERTAG_HIDDESC);
        ppi->pRawInputDevices = NULL;
    }
    ppi->cRawInputDevices = cDevices;
    Ret = TRUE;
    HidInputRawInputChanged();

Leave:
    UserLeave();
Free:
    if (Local)
        ExFreePoolWithTag(Local, USERTAG_HIDDESC);
    return Ret;
}

DWORD
APIENTRY
NtUserGetRegisteredRawInputDevices(
    PRAWINPUTDEVICE pRawInputDevices,
    PUINT puiNumDevices,
    UINT cbSize)
{
    PPROCESSINFO ppi;
    UINT Capacity, Count;
    DWORD Ret = (DWORD)-1;

    if (cbSize != sizeof(RAWINPUTDEVICE) || !puiNumDevices)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }

    UserEnterShared();
    ppi = GetW32ProcessInfo();

    _SEH2_TRY
    {
        ProbeForWrite(puiNumDevices, sizeof(UINT), 1);
        Capacity = *puiNumDevices;
        if (pRawInputDevices && !Capacity)
        {
            EngSetLastError(ERROR_INVALID_PARAMETER);
            _SEH2_LEAVE;
        }

        Count = ppi->cRawInputDevices;
        *puiNumDevices = Count;
        if (!pRawInputDevices)
        {
            Ret = 0;
        }
        else if (Capacity < Count)
        {
            EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            ProbeForWrite(pRawInputDevices, Count * sizeof(RAWINPUTDEVICE), 1);
            RtlCopyMemory(pRawInputDevices, ppi->pRawInputDevices, Count * sizeof(RAWINPUTDEVICE));
            Ret = Count;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        Ret = (DWORD)-1;
    }
    _SEH2_END;

    UserLeave();
    return Ret;
}

DWORD
APIENTRY
NtUserGetRawInputDeviceList(
    PRAWINPUTDEVICELIST pRawInputDeviceList,
    PUINT puiNumDevices,
    UINT cbSize)
{
    PLIST_ENTRY Entry;
    PRAWINPUT_DEVICE_OBJECT Device;
    UINT Count = 0, i;
    DWORD Ret = (DWORD)-1;

    if (cbSize != sizeof(RAWINPUTDEVICELIST))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }

    RawInputUpdateDevices();

    UserEnterShared();

    for (Entry = gRawInputDevices.Flink; Entry != &gRawInputDevices; Entry = Entry->Flink)
    {
        if (!CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry)->Hidden)
            Count++;
    }

    _SEH2_TRY
    {
        ProbeForWrite(puiNumDevices, sizeof(UINT), 1);
        if (!pRawInputDeviceList)
        {
            *puiNumDevices = Count;
            Ret = 0;
        }
        else if (*puiNumDevices < Count)
        {
            *puiNumDevices = Count;
            EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            ProbeForWrite(pRawInputDeviceList, Count * sizeof(RAWINPUTDEVICELIST), 1);
            for (Entry = gRawInputDevices.Flink, i = 0; Entry != &gRawInputDevices; Entry = Entry->Flink)
            {
                Device = CONTAINING_RECORD(Entry, RAWINPUT_DEVICE_OBJECT, ListEntry);
                if (Device->Hidden)
                    continue;
                pRawInputDeviceList[i].hDevice = UserHMGetHandle(Device);
                pRawInputDeviceList[i].dwType = Device->Info.dwType;
                i++;
            }
            Ret = Count;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        Ret = (DWORD)-1;
    }
    _SEH2_END;

    UserLeave();
    return Ret;
}

DWORD
APIENTRY
NtUserGetRawInputDeviceInfo(
    HANDLE hDevice,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize)
{
    PRAWINPUT_DEVICE_OBJECT Device;
    UINT cbData = 0, Len = 0, cbCopy = 0;
    PVOID pSource = NULL;
    DWORD Ret = (DWORD)-1;

    _SEH2_TRY
    {
        ProbeForWrite(pcbSize, sizeof(UINT), 1);
        cbData = *pcbSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        _SEH2_YIELD(return (DWORD)-1);
    }
    _SEH2_END;

    if (uiCommand != RIDI_DEVICENAME && uiCommand != RIDI_DEVICEINFO && uiCommand != RIDI_PREPARSEDDATA)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }

    UserEnterShared();

    Device = UserGetObject(gHandleTable, hDevice, TYPE_DEVICEINFO);
    if (!Device || Device->Hidden)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        goto Leave;
    }

    switch (uiCommand)
    {
        case RIDI_DEVICENAME:
            Len = Device->Path.Length / sizeof(WCHAR) + 1;
            cbCopy = Len * sizeof(WCHAR);
            pSource = Device->Path.Buffer;
            break;

        case RIDI_DEVICEINFO:
            Len = sizeof(RID_DEVICE_INFO);
            cbCopy = Len;
            pSource = &Device->Info;
            break;

        case RIDI_PREPARSEDDATA:
            Len = Device->PreparsedLength;
            cbCopy = Len;
            pSource = Device->PreparsedData;
            break;
    }

    _SEH2_TRY
    {
        if (pData && Len <= cbData && cbCopy)
        {
            ProbeForWrite(pData, cbCopy, 1);
            RtlCopyMemory(pData, pSource, cbCopy);
        }
        *pcbSize = Len;

        if (!pData)
            Ret = 0;
        else if (cbData < Len)
            EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        else
            Ret = Len;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        Ret = (DWORD)-1;
    }
    _SEH2_END;

Leave:
    UserLeave();
    return Ret;
}

DWORD
APIENTRY
NtUserGetRawInputData(
    HRAWINPUT hRawInput,
    UINT uiCommand,
    LPVOID pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    PTHREADINFO pti;
    PRAWINPUT RawInput;
    UINT Size;
    DWORD Ret = (DWORD)-1;

    UserEnterShared();
    pti = PsGetCurrentThreadWin32Thread();
    RawInput = pti->pRawInputData;

    if (!hRawInput || !RawInput || pti->hRawInputData != hRawInput)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        goto Leave;
    }

    if (cbSizeHeader != sizeof(RAWINPUTHEADER))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Leave;
    }

    if (uiCommand == RID_INPUT)
    {
        Size = RawInput->header.dwSize;
    }
    else if (uiCommand == RID_HEADER)
    {
        Size = sizeof(RAWINPUTHEADER);
    }
    else
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Leave;
    }

    _SEH2_TRY
    {
        ProbeForWrite(pcbSize, sizeof(UINT), 1);
        if (!pData)
        {
            *pcbSize = Size;
            Ret = 0;
        }
        else if (*pcbSize < Size)
        {
            EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            ProbeForWrite(pData, Size, 1);
            RtlCopyMemory(pData, RawInput, Size);
            Ret = Size;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        Ret = (DWORD)-1;
    }
    _SEH2_END;

Leave:
    UserLeave();
    return Ret;
}

DWORD
APIENTRY
NtUserGetRawInputBuffer(
    PRAWINPUT pData,
    PUINT pcbSize,
    UINT cbSizeHeader)
{
    const UINT Align = sizeof(ULONG_PTR) - 1;
    PTHREADINFO pti;
    PLIST_ENTRY Entry, Next;
    PUSER_MESSAGE Message;
    UINT cbBuffer, cbUsable, cbTotal = 0, cbNext = 0, cbReturn, cbItem, Count = 0;
    DWORD Ret = (DWORD)-1;

    if (cbSizeHeader != sizeof(RAWINPUTHEADER) || !pcbSize)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        return (DWORD)-1;
    }

    UserEnterExclusive();
    pti = PsGetCurrentThreadWin32Thread();

    _SEH2_TRY
    {
        ProbeForWrite(pcbSize, sizeof(UINT), 1);
        cbBuffer = *pcbSize;

        if (!pData)
        {
            cbReturn = 0;
            for (Entry = pti->PostedMessagesListHead.Flink; Entry != &pti->PostedMessagesListHead; Entry = Entry->Flink)
            {
                Message = CONTAINING_RECORD(Entry, USER_MESSAGE, ListEntry);
                if (Message->Msg.message == WM_INPUT && Message->RawInput)
                {
                    cbReturn = Message->RawInput->header.dwSize;
                    break;
                }
            }
            *pcbSize = cbReturn;
            Ret = 0;
            _SEH2_LEAVE;
        }

        ProbeForWrite(pData, cbBuffer, 1);
        cbUsable = cbBuffer & ~Align;
        cbReturn = cbBuffer;

        for (Entry = pti->PostedMessagesListHead.Flink; Entry != &pti->PostedMessagesListHead; Entry = Next)
        {
            Next = Entry->Flink;
            Message = CONTAINING_RECORD(Entry, USER_MESSAGE, ListEntry);
            if (Message->Msg.message != WM_INPUT || !Message->RawInput)
                continue;

            cbItem = Message->RawInput->header.dwSize;
            if (cbTotal + cbItem > cbUsable)
            {
                cbNext = cbItem;
                break;
            }

            RtlCopyMemory((PBYTE)pData + cbTotal, Message->RawInput, cbItem);
            cbTotal += (cbItem + Align) & ~Align;
            MsqDestroyMessage(Message);
            ClearMsgBitsMask(pti, QS_RAWINPUT);
            Count++;
        }

        if (!cbNext)
        {
            if (Count)
                cbNext = sizeof(RAWINPUT);
            else
                cbReturn = 0;
        }

        if (cbNext && cbBuffer <= cbNext)
        {
            cbReturn = cbNext;
            EngSetLastError(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            Ret = Count;
        }
        *pcbSize = cbReturn;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        Ret = (DWORD)-1;
    }
    _SEH2_END;

    UserLeave();
    return Ret;
}
