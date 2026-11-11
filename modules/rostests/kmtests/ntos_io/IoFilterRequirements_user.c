/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that IRP_MN_FILTER_RESOURCE_REQUIREMENTS carries the requirements in IoStatus.Information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <newdev.h>

#define TEST_REPORTED_MAGIC 0x4B465251

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR BusHardwareIds[] = L"ROOT\\KMTESTFILTERREQ\0";
static const WCHAR ChildHardwareIds[] = L"KMTEST\\FILTERREQCHILD\0";
static const WCHAR ChildInstance[] = L"KMTEST\\FILTERREQCHILD\\0";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestFilterReq";
static const PCWSTR Values[] = { L"FilterInformationIsList", L"FilterReportedMagic", L"FilterBusKeptList", L"StartPrivateMask" };

static BOOL FixtureInf(PWSTR InfPath, DWORD Count)
{
    DWORD Length = GetModuleFileNameW(NULL, InfPath, Count);
    PWSTR Filename;

    ok(Length != 0 && Length < Count, "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= Count)
        return FALSE;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_filterreq.inf") > Count)
    {
        ok(FALSE, "INF path is too long\n");
        return FALSE;
    }
    wcscpy(Filename + 1, L"kmtest_filterreq.inf");
    return TRUE;
}

static VOID ClearValues(VOID)
{
    HKEY Key;
    ULONG Index;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
        RegDeleteValueW(Key, Values[Index]);
    RegCloseKey(Key);
}

static BOOL ReadValue(PCWSTR Name, PDWORD Value)
{
    DWORD Size = sizeof(*Value), Type = 0;
    HKEY Key;
    LONG Error;

    *Value = MAXDWORD;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Value, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_DWORD;
}

static BOOL InstallChild(PCWSTR InfPath)
{
    BOOL Reboot = FALSE;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 20; Attempt++)
    {
        if (UpdateDriverForPlugAndPlayDevicesW(NULL, ChildHardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot))
            return TRUE;
        if (GetLastError() != ERROR_NO_SUCH_DEVINST)
            break;
        Sleep(250);
    }
    trace("Installing the child returned error %lu\n", GetLastError());
    return FALSE;
}

static VOID CheckRecorded(VOID)
{
    DWORD Value;

    ok(ReadValue(L"FilterInformationIsList", &Value), "The filter request never reached the function driver\n");
    ok(Value == 1, "IoStatus.Information did not point to the requirements list (%lu)\n", Value);
    ok(ReadValue(L"FilterReportedMagic", &Value), "The function driver did not record the supplied list\n");
    ok(Value == TEST_REPORTED_MAGIC, "The supplied list was not the bus driver's requirements (0x%lx)\n", Value);
    ok(ReadValue(L"FilterBusKeptList", &Value), "The filter request did not come back up\n");
    ok(Value == 1, "The bus driver did not leave IoStatus.Information as it was (%lu)\n", Value);
    ok(ReadValue(L"StartPrivateMask", &Value), "The child device did not start\n");
    ok(Value == 3, "The start resources were not the filtered requirements (mask %lu)\n", Value);
}

static VOID RemoveChild(VOID)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    HDEVINFO Devices = SetupDiCreateDeviceInfoList(NULL, NULL);

    if (Devices == INVALID_HANDLE_VALUE)
        return;
    if (SetupDiOpenDeviceInfoW(Devices, ChildInstance, NULL, 0, &Device))
        SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
    SetupDiDestroyDeviceInfoList(Devices);
}

static VOID RunFixture(HDEVINFO Devices, PSP_DEVINFO_DATA Device, PCWSTR InfPath)
{
    BOOL Success, Reboot = FALSE;

    Success = SetupDiSetDeviceRegistryPropertyW(Devices, Device, SPDRP_HARDWAREID, (const BYTE *)BusHardwareIds, sizeof(BusHardwareIds));
    ok(Success, "Setting hardware ID failed: %lu\n", GetLastError());
    if (!Success)
        return;
    Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, Device);
    ok(Success, "Registering test devnode failed: %lu\n", GetLastError());
    if (!Success)
        return;

    Success = UpdateDriverForPlugAndPlayDevicesW(NULL, BusHardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
    ok(Success, "Installing the bus fixture failed: %lu\n", GetLastError());
    if (Success)
    {
        ok(InstallChild(InfPath), "Installing the child fixture failed\n");
        CheckRecorded();
    }

    Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, Device);
    ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    RemoveChild();
}

START_TEST(IoFilterRequirements)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    HDEVINFO Devices;
    BOOL Success;

    if (!FixtureInf(InfPath, RTL_NUMBER_OF(InfPath)))
        return;
    ClearValues();

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestFilterReq", &TestClass, L"Kmtest filter requirements bus", NULL, DICD_GENERATE_ID, &Device);
    ok(Success, "SetupDiCreateDeviceInfo failed: %lu\n", GetLastError());
    if (Success)
        RunFixture(Devices, &Device, InfPath);
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}
