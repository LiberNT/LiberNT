/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a PnP driver is unloaded once the removal of its only device leaves it without a device
 *              object, after its remove request returns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <newdev.h>

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR HardwareIds[] = L"ROOT\\KMTESTREMOVEUNLOAD\0";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestRemoveUnload";
static const PCWSTR Values[] = { L"Loaded", L"Started", L"Unloaded", L"UnloadDuringRemove", L"DeviceLeftAtUnload" };

static DWORD ReadValue(PCWSTR Name)
{
    DWORD Value = MAXDWORD, Size = sizeof(Value), Type = 0;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return MAXDWORD;
    if (RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Value = MAXDWORD;
    RegCloseKey(Key);
    return Value;
}

START_TEST(IoRemoveUnload)
{
    HDEVINFO Devices;
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    PWSTR Filename;
    DWORD Length;
    BOOL Success, Reboot = FALSE, Registered = FALSE;
    HKEY Key;
    ULONG Index;

    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(InfPath), "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= RTL_NUMBER_OF(InfPath))
        return;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_rmunload.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "INF path is too long\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_rmunload.inf");

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) == ERROR_SUCCESS)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
            RegDeleteValueW(Key, Values[Index]);
        RegCloseKey(Key);
    }

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestRemoveUnload", &TestClass, L"Kmtest remove-and-unload fixture",
                                       NULL, DICD_GENERATE_ID, &Device);
    ok(Success, "SetupDiCreateDeviceInfo failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Success = SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)HardwareIds,
                                                sizeof(HardwareIds));
    ok(Success, "Setting hardware ID failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, &Device);
    ok(Success, "Registering test devnode failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Registered = TRUE;

    Success = UpdateDriverForPlugAndPlayDevicesW(NULL, HardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE,
                                                 &Reboot);
    ok(Success, "Installing the fixture failed: %lu\n", GetLastError());
    ok_eq_ulong(ReadValue(L"Loaded"), 1UL);
    ok_eq_ulong(ReadValue(L"Started"), 1UL);
    ok(ReadValue(L"Unloaded") == MAXDWORD, "The driver was unloaded while its device was started\n");

    Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
    ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    Registered = !Success;

    ok_eq_ulong(ReadValue(L"UnloadDuringRemove"), 0UL);
    ok_eq_ulong(ReadValue(L"Unloaded"), 1UL);
    ok_eq_ulong(ReadValue(L"DeviceLeftAtUnload"), 0UL);

Cleanup:
    if (Registered)
    {
        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
        ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    }
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}
