/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installs and removes the vwifi test adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winreg.h>
#include <winuser.h>
#include <wlanapi.h>
#include <setupapi.h>
#include <newdev.h>

#include "vwifi.h"

static const GUID NetClass = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR VwifiHardwareIds[] = L"ROOT\\VWIFI\0";

BOOL
WaitInterface(HANDLE Client, GUID *Interface)
{
    ULONGLONG Deadline = GetTickCount64() + 10000;
    PWLAN_INTERFACE_INFO_LIST List;
    BOOL Found = FALSE;

    while (!Found && GetTickCount64() < Deadline)
    {
        List = NULL;
        if (WlanEnumInterfaces(Client, NULL, &List) == ERROR_SUCCESS && List != NULL)
        {
            if (List->dwNumberOfItems != 0)
            {
                *Interface = List->InterfaceInfo[0].InterfaceGuid;
                Found = TRUE;
            }
            WlanFreeMemory(List);
        }
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
