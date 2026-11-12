/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System button and lid devices: IOCTL_GET_SYS_BUTTON_CAPS and IOCTL_GET_SYS_BUTTON_EVENT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <winioctl.h>
#include <initguid.h>
#include <poclass.h>
#include <setupapi.h>

static
DWORD
ButtonIoctl(
    _In_ HANDLE Device,
    _In_ ULONG Code,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _Out_ PDWORD Returned)
{
    OVERLAPPED Overlapped = { 0 };
    DWORD Error;
    BOOL Ret;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    *Returned = 0xdeadbeef;
    Ret = DeviceIoControl(Device, Code, NULL, 0, Buffer, Size, Returned, &Overlapped);
    Error = Ret ? ERROR_SUCCESS : GetLastError();
    if (!Ret && Error == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(Overlapped.hEvent, 200) == WAIT_TIMEOUT)
            CancelIoEx(Device, &Overlapped);
        Ret = GetOverlappedResult(Device, &Overlapped, Returned, TRUE);
        Error = Ret ? ERROR_SUCCESS : GetLastError();
    }
    CloseHandle(Overlapped.hEvent);
    return Error;
}

static
VOID
TestButtonDevice(
    _In_ PCWSTR Path,
    _In_ DWORD Access)
{
    ULONG Caps = 0xdeadbeef, Event = 0xdeadbeef;
    HANDLE Device;
    DWORD Error, Returned;

    Device = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                         NULL);
    if (Access)
    {
        trace("%S open for access 0x%lx: error %lu\n", Path, Access,
              (Device == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS);
        if (Device == INVALID_HANDLE_VALUE)
            return;
    }
    else
    {
        ok(Device != INVALID_HANDLE_VALUE, "%S could not be opened: %lu\n", Path, GetLastError());
        if (Device == INVALID_HANDLE_VALUE)
            return;
    }

    Error = ButtonIoctl(Device, IOCTL_GET_SYS_BUTTON_CAPS, &Caps, sizeof(Caps), &Returned);
    if (!(Access & GENERIC_READ))
    {
        ok(Error == ERROR_ACCESS_DENIED, "%S caps without read access: error %lu\n", Path, Error);
        Error = ButtonIoctl(Device, IOCTL_GET_SYS_BUTTON_CAPS, &Caps, sizeof(USHORT), &Returned);
        ok(Error == ERROR_ACCESS_DENIED, "%S short caps without read access: error %lu\n", Path, Error);
        Error = ButtonIoctl(Device, IOCTL_GET_SYS_BUTTON_EVENT, &Event, sizeof(Event), &Returned);
        ok(Error == ERROR_ACCESS_DENIED, "%S event without read access: error %lu\n", Path, Error);
    }
    else
    {
        ok(Error == ERROR_SUCCESS && Returned == 0 && Caps == 0xdeadbeef,
           "%S caps from user mode: error %lu, %lu bytes, caps 0x%08lx\n", Path, Error, Returned, Caps);
        Error = ButtonIoctl(Device, IOCTL_GET_SYS_BUTTON_CAPS, &Caps, sizeof(USHORT), &Returned);
        ok(Error == ERROR_SUCCESS && Returned == 0, "%S short caps from user mode: error %lu, %lu bytes\n", Path, Error,
           Returned);
        Error = ButtonIoctl(Device, IOCTL_GET_SYS_BUTTON_EVENT, &Event, sizeof(Event), &Returned);
        ok(Error == ERROR_SUCCESS && Returned == 0 && Event == 0xdeadbeef,
           "%S event from user mode: error %lu, %lu bytes, event 0x%08lx\n", Path, Error, Returned, Event);
    }
    CloseHandle(Device);
}

static
ULONG
TestButtonInterfaces(
    _In_ const GUID *Interface,
    _In_ BOOL Lid)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    HDEVINFO Set;
    DWORD Index, Size;
    ULONG Count = 0;

    Set = SetupDiGetClassDevsW(Interface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return 0;
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, Interface, Index, &Data); ++Index)
    {
        Size = 0;
        SetupDiGetDeviceInterfaceDetailW(Set, &Data, NULL, 0, &Size, NULL);
        Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (!Detail)
            break;
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Set, &Data, Detail, Size, NULL, NULL))
        {
            trace("%s device %lu: %S\n", Lid ? "lid" : "button", Index, Detail->DevicePath);
            TestButtonDevice(Detail->DevicePath, 0);
            TestButtonDevice(Detail->DevicePath, GENERIC_READ);
            Count++;
        }
        HeapFree(GetProcessHeap(), 0, Detail);
    }
    ok_long(GetLastError(), ERROR_NO_MORE_ITEMS);
    SetupDiDestroyDeviceInfoList(Set);
    return Count;
}

START_TEST(SysButtonDevices)
{
    ULONG Buttons, Lids;

    Buttons = TestButtonInterfaces(&GUID_DEVICE_SYS_BUTTON, FALSE);
    Lids = TestButtonInterfaces(&GUID_DEVICE_LID, TRUE);
    trace("%lu button devices, %lu lid devices\n", Buttons, Lids);
    if (!Buttons)
        skip("No system button device\n");
    if (!Lids)
        skip("No lid device\n");
}
