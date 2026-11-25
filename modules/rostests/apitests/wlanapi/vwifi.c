/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs and removes the vwifi test adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <stdio.h>

#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>
#include <newdev.h>

#include "vwifi.h"

static const GUID NetClass = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR VwifiHardwareIds[] = L"ROOT\\VWIFI\0";

static BOOL
GetVwifiInstanceId(HDEVINFO Devices, SP_DEVINFO_DATA *Device, PSTR Text, DWORD Count)
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

BOOL
WaitInterface(HANDLE Client, HDEVINFO Devices, SP_DEVINFO_DATA *Device, GUID *Interface)
{
    ULONGLONG Deadline = GetTickCount64() + 10000;
    PWLAN_INTERFACE_INFO_LIST List;
    CHAR InstanceId[64];
    CHAR Name[64];
    BOOL Found = FALSE;
    DWORD Index;

    while (!Found && GetTickCount64() < Deadline)
    {
        List = NULL;
        if (GetVwifiInstanceId(Devices, Device, InstanceId, sizeof(InstanceId)) &&
            WlanEnumInterfaces(Client, NULL, &List) == ERROR_SUCCESS && List != NULL)
        {
            for (Index = 0; Index < List->dwNumberOfItems && !Found; Index++)
            {
                const GUID *Guid = &List->InterfaceInfo[Index].InterfaceGuid;

                _snprintf(Name, sizeof(Name), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                          Guid->Data1, Guid->Data2, Guid->Data3,
                          Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
                          Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
                Name[sizeof(Name) - 1] = 0;
                if (_stricmp(Name, InstanceId) == 0)
                {
                    *Interface = *Guid;
                    Found = TRUE;
                }
            }
        }
        if (List != NULL)
            WlanFreeMemory(List);
        if (!Found)
            Sleep(200);
    }
    return Found;
}

BOOL
InstallVwifi(HDEVINFO *Devices, SP_DEVINFO_DATA *Device)
{
    WCHAR InfPath[MAX_PATH];
    BOOL Reboot = FALSE;
    UINT Length;

    Length = GetWindowsDirectoryW(InfPath, MAX_PATH);
    if (Length == 0 || Length + wcslen(L"\\inf\\netvwifi.inf") >= MAX_PATH)
    {
        skip("Cannot build the vwifi INF path\n");
        return FALSE;
    }
    wcscat(InfPath, L"\\inf\\netvwifi.inf");
    if (GetFileAttributesW(InfPath) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls is not installed\n", InfPath);
        return FALSE;
    }

    *Devices = SetupDiCreateDeviceInfoList(&NetClass, NULL);
    ok(*Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (*Devices == INVALID_HANDLE_VALUE)
        return FALSE;

    Device->cbSize = sizeof(*Device);
    if (!SetupDiCreateDeviceInfoW(*Devices, L"Net", &NetClass, L"LiberNT virtual Wi-Fi test adapter",
                                  NULL, DICD_GENERATE_ID, Device) ||
        !SetupDiSetDeviceRegistryPropertyW(*Devices, Device, SPDRP_HARDWAREID,
                                           (const BYTE *)VwifiHardwareIds, sizeof(VwifiHardwareIds)))
    {
        ok(FALSE, "Creating the vwifi devnode failed: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(*Devices);
        return FALSE;
    }
    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, *Devices, Device))
    {
        ok(FALSE, "Registering the vwifi devnode failed: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(*Devices);
        return FALSE;
    }
    if (!UpdateDriverForPlugAndPlayDevicesW(NULL, VwifiHardwareIds, InfPath,
                                            INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot))
    {
        ok(FALSE, "Installing vwifi failed: %lu\n", GetLastError());
        SetupDiCallClassInstaller(DIF_REMOVE, *Devices, Device);
        SetupDiDestroyDeviceInfoList(*Devices);
        return FALSE;
    }
    return TRUE;
}

VOID
RemoveVwifi(HDEVINFO Devices, SP_DEVINFO_DATA *Device)
{
    ok(SetupDiCallClassInstaller(DIF_REMOVE, Devices, Device),
       "Removing the vwifi devnode failed: %lu\n", GetLastError());
    SetupDiDestroyDeviceInfoList(Devices);
}
