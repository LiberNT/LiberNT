/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a removed and deleted child gets no PnP IRPs when its parent is removed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <newdev.h>

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR QueryIds[] = L"ROOT\\KMTESTREMOVECHILD\0";
static const WCHAR FailIds[] = L"ROOT\\KMTESTREMOVECHILDFAIL\0";
static const WCHAR QueryKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestRemoveChild\\Query";
static const WCHAR FailKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestRemoveChild\\Fail";

static DWORD ReadValue(PCWSTR KeyName, PCWSTR Name)
{
    DWORD Value = MAXDWORD, Size = sizeof(Value), Type = 0;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return MAXDWORD;
    if (RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Value = MAXDWORD;
    RegCloseKey(Key);
    return Value;
}

static BOOL WaitValue(PCWSTR KeyName, PCWSTR Name, DWORD Expected)
{
    DWORD Start = GetTickCount();

    while (ReadValue(KeyName, Name) != Expected)
    {
        if (GetTickCount() - Start > 5000)
            return FALSE;
        Sleep(1);
    }
    return TRUE;
}

static BOOL AddDevnode(HDEVINFO Devices, PSP_DEVINFO_DATA Device, PCWSTR Ids, DWORD IdsSize, PCWSTR InfPath)
{
    BOOL Success, Reboot = FALSE;

    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestRemoveChild", &TestClass, L"Kmtest removed child fixture",
                                       NULL, DICD_GENERATE_ID, Device);
    ok(Success, "SetupDiCreateDeviceInfo failed: %lu\n", GetLastError());
    if (!Success)
        return FALSE;
    Success = SetupDiSetDeviceRegistryPropertyW(Devices, Device, SPDRP_HARDWAREID, (const BYTE *)Ids, IdsSize);
    ok(Success, "Setting hardware ID failed: %lu\n", GetLastError());
    if (!Success)
        return FALSE;
    Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, Device);
    ok(Success, "Registering test devnode failed: %lu\n", GetLastError());
    if (!Success)
        return FALSE;
    Success = UpdateDriverForPlugAndPlayDevicesW(NULL, Ids, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE,
                                                 &Reboot);
    ok(Success, "Installing the fixture failed: %lu\n", GetLastError());
    return TRUE;
}

static VOID CheckChild(PCWSTR KeyName)
{
    DWORD Surprises = ReadValue(KeyName, L"ChildSurprises");

    ok_eq_ulong(ReadValue(KeyName, L"ChildStarts"), 1UL);
    ok_eq_ulong(ReadValue(KeyName, L"ChildRemoves"), 1UL);
    ok(Surprises == 1 || Surprises == MAXDWORD, "The child got %lu surprise removals\n", Surprises);
    ok(ReadValue(KeyName, L"ChildIrpsAfterDelete") == MAXDWORD,
       "The deleted child got %lu PnP IRPs, the last one minor %lu\n",
       ReadValue(KeyName, L"ChildIrpsAfterDelete"), ReadValue(KeyName, L"ChildLateMinor"));
}

START_TEST(IoRemoveChild)
{
    HDEVINFO Devices;
    SP_DEVINFO_DATA QueryDevice = { sizeof(QueryDevice) }, FailDevice = { sizeof(FailDevice) };
    BOOL QueryRegistered = FALSE, FailRegistered = FALSE, Success;
    WCHAR InfPath[MAX_PATH];
    PWSTR Filename;
    DWORD Length;

    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(InfPath), "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= RTL_NUMBER_OF(InfPath))
        return;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_remchild.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "INF path is too long\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_remchild.inf");

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, QueryKey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, FailKey);

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;

    QueryRegistered = AddDevnode(Devices, &QueryDevice, QueryIds, sizeof(QueryIds), InfPath);
    if (!QueryRegistered)
        goto Cleanup;
    ok(WaitValue(QueryKey, L"ChildRemoves", 1), "The missing child was not removed\n");

    trace("Parent failed after start\n");
    FailRegistered = AddDevnode(Devices, &FailDevice, FailIds, sizeof(FailIds), InfPath);
    if (FailRegistered)
    {
        ok(WaitValue(FailKey, L"ParentRemoved", 1), "The failed parent was not removed\n");
        CheckChild(FailKey);
    }

    trace("Parent removed by query-remove\n");
    Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &QueryDevice);
    ok(Success, "Removing the parent failed: %lu\n", GetLastError());
    QueryRegistered = !Success;
    ok(WaitValue(QueryKey, L"ParentRemoved", 1), "The parent was not removed\n");
    CheckChild(QueryKey);

Cleanup:
    if (FailRegistered)
    {
        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &FailDevice);
        ok(Success, "Removing the failed devnode failed: %lu\n", GetLastError());
    }
    if (QueryRegistered)
    {
        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &QueryDevice);
        ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    }
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}
