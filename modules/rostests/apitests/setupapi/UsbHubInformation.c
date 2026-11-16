/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the extended hub information and capability IOCTLs of a root hub
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <usb.h>
#include <usbioctl.h>

C_ASSERT(sizeof(USB_30_HUB_DESCRIPTOR) == 12);
C_ASSERT(sizeof(USB_HUB_INFORMATION_EX) == 77);
C_ASSERT(sizeof(USB_HUB_CAPABILITIES_EX) == 4);

static BOOL
IsXhciController(HDEVINFO Set, PSP_DEVICE_INTERFACE_DATA Interface)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR Ids[1024];
    PCWSTR Id;

    if (!SetupDiGetDeviceInterfaceDetailW(Set, Interface, NULL, 0, NULL, &Device) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return FALSE;
    }
    ZeroMemory(Ids, sizeof(Ids));
    if (!SetupDiGetDeviceRegistryPropertyW(Set, &Device, SPDRP_COMPATIBLEIDS, NULL, (PBYTE)Ids, sizeof(Ids) - sizeof(WCHAR), NULL))
        return FALSE;
    for (Id = Ids; *Id; Id += wcslen(Id) + 1)
    {
        if (!_wcsicmp(Id, L"PCI\\CC_0C0330"))
            return TRUE;
    }
    return FALSE;
}

static HANDLE
OpenRootHub(HANDLE Controller)
{
    USB_ROOT_HUB_NAME Size;
    PUSB_ROOT_HUB_NAME Name;
    WCHAR Path[512];
    DWORD Returned;
    HANDLE Hub = INVALID_HANDLE_VALUE;

    ZeroMemory(&Size, sizeof(Size));
    if (!DeviceIoControl(Controller, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, &Size, sizeof(Size), &Returned, NULL) ||
        Size.ActualLength <= sizeof(Size))
    {
        return INVALID_HANDLE_VALUE;
    }
    Name = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size.ActualLength);
    if (!Name)
        return INVALID_HANDLE_VALUE;
    if (DeviceIoControl(Controller, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, Name, Size.ActualLength, &Returned, NULL))
    {
        swprintf(Path, _countof(Path), L"\\\\.\\%ls", Name->RootHubName);
        Hub = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }
    HeapFree(GetProcessHeap(), 0, Name);
    return Hub;
}

static void
TestRootHub(HANDLE Hub, BOOL Xhci)
{
    USB_NODE_INFORMATION Node;
    USB_HUB_INFORMATION_EX Info;
    USB_HUB_CAPABILITIES_EX Caps;
    BYTE Small[8];
    DWORD Returned;
    BOOL Ok;

    ZeroMemory(&Node, sizeof(Node));
    Ok = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_INFORMATION, &Node, sizeof(Node), &Node, sizeof(Node), &Returned, NULL);
    ok(Ok, "IOCTL_USB_GET_NODE_INFORMATION failed: %lu\n", GetLastError());

    FillMemory(&Info, sizeof(Info), 0xCC);
    Returned = 0;
    Ok = DeviceIoControl(Hub, IOCTL_USB_GET_HUB_INFORMATION_EX, &Info, sizeof(Info), &Info, sizeof(Info), &Returned, NULL);
    ok(Ok, "IOCTL_USB_GET_HUB_INFORMATION_EX failed: %lu\n", GetLastError());
    ok_long(Returned, sizeof(USB_HUB_INFORMATION_EX));
    ok_long(Info.HubType, UsbRootHub);
    ok_int(Info.HighestPortNumber, Node.u.HubInformation.HubDescriptor.bNumberOfPorts);

    SetLastError(0xdeadbeef);
    Ok = DeviceIoControl(Hub, IOCTL_USB_GET_HUB_INFORMATION_EX, Small, sizeof(Small), Small, sizeof(Small), &Returned, NULL);
    ok(!Ok, "IOCTL_USB_GET_HUB_INFORMATION_EX succeeded with %u bytes\n", (UINT)sizeof(Small));
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);

    Caps.CapabilityFlags.ul = 0xffffffff;
    Returned = 0;
    Ok = DeviceIoControl(Hub, IOCTL_USB_GET_HUB_CAPABILITIES_EX, &Caps, sizeof(Caps), &Caps, sizeof(Caps), &Returned, NULL);
    ok(Ok, "IOCTL_USB_GET_HUB_CAPABILITIES_EX failed: %lu\n", GetLastError());
    ok_long(Returned, sizeof(USB_HUB_CAPABILITIES_EX));
    ok_int(Caps.CapabilityFlags.HubIsRoot, 1);
    ok_int(Caps.CapabilityFlags.HubIsHighSpeedCapable, 0);
    ok_int(Caps.CapabilityFlags.HubIsMultiTt, 0);
    ok_int(Caps.CapabilityFlags.HubIsBusPowered, 0);
    ok_long(Caps.CapabilityFlags.ReservedMBZ, 0);
    if (Xhci)
        ok_int(Caps.CapabilityFlags.HubIsHighSpeed, 1);

    SetLastError(0xdeadbeef);
    Ok = DeviceIoControl(Hub, IOCTL_USB_GET_HUB_CAPABILITIES_EX, NULL, 0, NULL, 0, &Returned, NULL);
    ok(!Ok, "IOCTL_USB_GET_HUB_CAPABILITIES_EX succeeded without a buffer\n");
    ok_long(GetLastError(), ERROR_INSUFFICIENT_BUFFER);
}

START_TEST(UsbHubInformation)
{
    HDEVINFO Set;
    SP_DEVICE_INTERFACE_DATA Interface = { sizeof(Interface) };
    BYTE DetailBuffer[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = (PVOID)DetailBuffer;
    DWORD Index;
    ULONG Hubs = 0;

    Set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, Index, &Interface); Index++)
    {
        HANDLE Controller, Hub;

        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, sizeof(DetailBuffer), NULL, NULL))
            continue;
        Controller = CreateFileW(Detail->DevicePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Controller == INVALID_HANDLE_VALUE)
            continue;
        Hub = OpenRootHub(Controller);
        if (Hub != INVALID_HANDLE_VALUE)
        {
            TestRootHub(Hub, IsXhciController(Set, &Interface));
            CloseHandle(Hub);
            Hubs++;
        }
        CloseHandle(Controller);
    }
    SetupDiDestroyDeviceInfoList(Set);

    if (Hubs == 0)
        skip("No USB root hub could be opened\n");
}
