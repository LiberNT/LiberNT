/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the bytes a hub reports for IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <usbioctl.h>

#define INFO_HEADER_SIZE FIELD_OFFSET(USB_NODE_CONNECTION_INFORMATION_EX, PipeList)
#define LARGE_PIPES 32

static
PUCHAR
AllocateAtPageEnd(
    _In_ SIZE_T Size,
    _Out_ PVOID *Region)
{
    SYSTEM_INFO SystemInfo;
    SIZE_T Pages;
    DWORD OldProtect;
    PUCHAR Base;

    GetSystemInfo(&SystemInfo);
    Pages = (Size + SystemInfo.dwPageSize - 1) / SystemInfo.dwPageSize;
    Base = VirtualAlloc(NULL, (Pages + 1) * SystemInfo.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *Region = Base;
    if (!Base)
        return NULL;
    VirtualProtect(Base + Pages * SystemInfo.dwPageSize, SystemInfo.dwPageSize, PAGE_NOACCESS, &OldProtect);
    return Base + Pages * SystemInfo.dwPageSize - Size;
}

static
VOID
TestPort(
    _In_ HANDLE Hub,
    _In_ ULONG Port,
    _Inout_ PULONG ZeroPipeDevices)
{
    PUSB_NODE_CONNECTION_INFORMATION_EX Info;
    SIZE_T LargeSize = INFO_HEADER_SIZE + LARGE_PIPES * sizeof(USB_PIPE_INFO);
    PVOID Region;
    DWORD Returned;
    BOOL Result;

    Info = (PUSB_NODE_CONNECTION_INFORMATION_EX)AllocateAtPageEnd(INFO_HEADER_SIZE, &Region);
    ok(Info != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Info)
        return;

    Info->ConnectionIndex = Port;
    Returned = 0xdeadbeef;
    Result = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, Info, INFO_HEADER_SIZE,
                             Info, INFO_HEADER_SIZE, &Returned, NULL);
    ok(Result, "port %lu: the header-only query failed with %lu\n", Port, GetLastError());
    if (Result)
    {
        ok(Returned == INFO_HEADER_SIZE, "port %lu: %lu bytes reported for a %u byte buffer\n",
           Port, Returned, (UINT)INFO_HEADER_SIZE);
        if (Info->ConnectionStatus == DeviceConnected && Info->NumberOfOpenPipes == 0)
            (*ZeroPipeDevices)++;
    }
    VirtualFree(Region, 0, MEM_RELEASE);

    Info = (PUSB_NODE_CONNECTION_INFORMATION_EX)AllocateAtPageEnd(LargeSize, &Region);
    ok(Info != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Info)
        return;

    Info->ConnectionIndex = Port;
    Returned = 0xdeadbeef;
    Result = DeviceIoControl(Hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, Info, (DWORD)LargeSize,
                             Info, (DWORD)LargeSize, &Returned, NULL);
    ok(Result, "port %lu: the full query failed with %lu\n", Port, GetLastError());
    if (Result && Info->ConnectionStatus == DeviceConnected && Info->NumberOfOpenPipes <= LARGE_PIPES)
    {
        ok(Returned == INFO_HEADER_SIZE + Info->NumberOfOpenPipes * sizeof(USB_PIPE_INFO),
           "port %lu: %lu bytes reported for %lu open pipes\n", Port, Returned, Info->NumberOfOpenPipes);
    }
    VirtualFree(Region, 0, MEM_RELEASE);
}

START_TEST(NodeConnectionInfo)
{
    SP_DEVICE_INTERFACE_DATA Interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    USB_NODE_INFORMATION Node;
    ULONG Index, Port, Hubs = 0, ZeroPipeDevices = 0;
    DWORD Size, Returned;
    HDEVINFO Set;
    HANDLE Hub;

    Set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Interface.cbSize = sizeof(Interface);
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, &GUID_DEVINTERFACE_USB_HUB, Index, &Interface); Index++)
    {
        Size = 0;
        SetupDiGetDeviceInterfaceDetailW(Set, &Interface, NULL, 0, &Size, NULL);
        Detail = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Detail)
            continue;
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Set, &Interface, Detail, Size, NULL, NULL))
        {
            Hub = CreateFileW(Detail->DevicePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (Hub != INVALID_HANDLE_VALUE)
            {
                if (DeviceIoControl(Hub, IOCTL_USB_GET_NODE_INFORMATION, &Node, sizeof(Node), &Node, sizeof(Node),
                                    &Returned, NULL))
                {
                    Hubs++;
                    for (Port = 1; Port <= Node.u.HubInformation.HubDescriptor.bNumberOfPorts; Port++)
                        TestPort(Hub, Port, &ZeroPipeDevices);
                }
                CloseHandle(Hub);
            }
        }
        HeapFree(GetProcessHeap(), 0, Detail);
    }
    SetupDiDestroyDeviceInfoList(Set);

    trace("%lu hubs, %lu connected devices without open pipes\n", Hubs, ZeroPipeDevices);
    if (!Hubs)
        skip("No USB hub found\n");
}
