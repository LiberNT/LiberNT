/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the IP interface of a Wi-Fi card is the card itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <stdio.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>

#include "vwifi.h"

static VOID
FormatGuid(const GUID *Guid, PSTR Text, SIZE_T Count)
{
    _snprintf(Text, Count, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              Guid->Data1, Guid->Data2, Guid->Data3,
              Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
              Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    Text[Count - 1] = 0;
}

static PIP_ADAPTER_ADDRESSES
QueryAdapters(VOID)
{
    PIP_ADAPTER_ADDRESSES Addresses = NULL;
    ULONG Size = 0x4000;
    ULONG Error;
    ULONG Try;

    for (Try = 0; Try < 4; Try++)
    {
        Addresses = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Addresses == NULL)
            return NULL;
        Error = GetAdaptersAddresses(AF_UNSPEC,
                                     GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_SKIP_ANYCAST |
                                     GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                     NULL, Addresses, &Size);
        if (Error == ERROR_SUCCESS)
            return Addresses;
        HeapFree(GetProcessHeap(), 0, Addresses);
        Addresses = NULL;
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
    }
    return NULL;
}

static PIP_ADAPTER_ADDRESSES
FindAdapter(PIP_ADAPTER_ADDRESSES Addresses, PCSTR Name)
{
    PIP_ADAPTER_ADDRESSES Adapter;

    for (Adapter = Addresses; Adapter != NULL; Adapter = Adapter->Next)
    {
        if (Adapter->AdapterName != NULL && _stricmp(Adapter->AdapterName, Name) == 0)
            return Adapter;
    }
    return NULL;
}

static PIP_ADAPTER_ADDRESSES
WaitAdapter(PCSTR Name, PIP_ADAPTER_ADDRESSES *Addresses)
{
    ULONGLONG Deadline = GetTickCount64() + 5000;
    PIP_ADAPTER_ADDRESSES Adapter;

    for (;;)
    {
        *Addresses = QueryAdapters();
        if (*Addresses != NULL)
        {
            Adapter = FindAdapter(*Addresses, Name);
            if (Adapter != NULL)
                return Adapter;
            HeapFree(GetProcessHeap(), 0, *Addresses);
            *Addresses = NULL;
        }
        if (GetTickCount64() >= Deadline)
            return NULL;
        Sleep(100);
    }
}

static BOOL
GetDeviceText(HDEVINFO Devices, SP_DEVINFO_DATA *Device, PWSTR Text, DWORD Count)
{
    if (SetupDiGetDeviceRegistryPropertyW(Devices, Device, SPDRP_FRIENDLYNAME, NULL,
                                          (PBYTE)Text, Count * sizeof(WCHAR), NULL))
    {
        return TRUE;
    }
    return SetupDiGetDeviceRegistryPropertyW(Devices, Device, SPDRP_DEVICEDESC, NULL,
                                             (PBYTE)Text, Count * sizeof(WCHAR), NULL);
}

static BOOL
GetInstanceId(HDEVINFO Devices, SP_DEVINFO_DATA *Device, PSTR Text, DWORD Count)
{
    HKEY Key;
    DWORD Type;
    DWORD Size = Count;
    LONG Error;

    Key = SetupDiOpenDevRegKey(Devices, Device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (Key == INVALID_HANDLE_VALUE)
        return FALSE;
    Error = RegQueryValueExA(Key, "NetCfgInstanceId", NULL, &Type, (PBYTE)Text, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_SZ;
}

static BOOL
GetInterfaceDescription(HANDLE Client, const GUID *Interface, PWSTR Text, SIZE_T Count)
{
    PWLAN_INTERFACE_INFO_LIST List = NULL;
    BOOL Found = FALSE;
    DWORD Index;

    if (WlanEnumInterfaces(Client, NULL, &List) != ERROR_SUCCESS || List == NULL)
        return FALSE;
    for (Index = 0; Index < List->dwNumberOfItems; Index++)
    {
        if (IsEqualGUID(&List->InterfaceInfo[Index].InterfaceGuid, Interface))
        {
            lstrcpynW(Text, List->InterfaceInfo[Index].strInterfaceDescription, (int)Count);
            Found = TRUE;
            break;
        }
    }
    WlanFreeMemory(List);
    return Found;
}

static VOID
TestAdapterIdentity(HANDLE Client, const GUID *Interface, HDEVINFO Devices, SP_DEVINFO_DATA *Device)
{
    PIP_ADAPTER_ADDRESSES Addresses = NULL;
    PIP_ADAPTER_ADDRESSES Adapter;
    PIP_ADAPTER_ADDRESSES Other;
    CHAR Name[64];
    CHAR InstanceId[64];
    WCHAR DeviceText[256];
    WCHAR WlanText[WLAN_MAX_NAME_LENGTH];
    ULONG SameAddress = 0;

    FormatGuid(Interface, Name, sizeof(Name));

    ok(GetInstanceId(Devices, Device, InstanceId, sizeof(InstanceId)),
       "The vwifi driver key has no NetCfgInstanceId: %lu\n", GetLastError());
    ok(_stricmp(InstanceId, Name) == 0, "Wireless interface %s, adapter %s\n", Name, InstanceId);

    Adapter = WaitAdapter(Name, &Addresses);
    ok(Adapter != NULL, "No IP interface for the wireless interface %s\n", Name);
    if (Adapter == NULL)
        return;

    ok_long(Adapter->IfType, IF_TYPE_IEEE80211);
    ok_long(Adapter->PhysicalAddressLength, 6);
    ok(wcsncmp(Adapter->FriendlyName, L"Wi-Fi", 5) == 0, "FriendlyName %ls\n", Adapter->FriendlyName);

    ok(GetDeviceText(Devices, Device, DeviceText, ARRAYSIZE(DeviceText)),
       "The vwifi devnode has no description: %lu\n", GetLastError());
    ok(wcscmp(Adapter->Description, DeviceText) == 0, "Description %ls, device %ls\n",
       Adapter->Description, DeviceText);

    ok(GetInterfaceDescription(Client, Interface, WlanText, ARRAYSIZE(WlanText)),
       "The wireless interface %s is gone\n", Name);
    ok(wcscmp(Adapter->Description, WlanText) == 0, "Description %ls, wireless interface %ls\n",
       Adapter->Description, WlanText);

    for (Other = Addresses; Other != NULL; Other = Other->Next)
    {
        if (Other->PhysicalAddressLength == Adapter->PhysicalAddressLength &&
            memcmp(Other->PhysicalAddress, Adapter->PhysicalAddress, Adapter->PhysicalAddressLength) == 0)
        {
            SameAddress++;
        }
    }
    ok_long(SameAddress, 1);

    HeapFree(GetProcessHeap(), 0, Addresses);
}

START_TEST(WlanAdapterIdentity)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    DWORD NegotiatedVersion;
    HANDLE Client;
    GUID Interface;
    DWORD Error;

    Error = WlanOpenHandle(WLAN_API_VERSION_2_0, NULL, &NegotiatedVersion, &Client);
    if (Error == ERROR_SERVICE_NOT_ACTIVE)
    {
        skip("WlanSvc is not running\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "WlanOpenHandle failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    if (InstallVwifi(&Devices, &Device))
    {
        if (WaitInterface(Client, &Interface))
            TestAdapterIdentity(Client, &Interface, Devices, &Device);
        else
            ok(FALSE, "No wireless interface appeared for the vwifi adapter\n");
        RemoveVwifi(Devices, &Device);
    }
    WlanCloseHandle(Client, NULL);
}
