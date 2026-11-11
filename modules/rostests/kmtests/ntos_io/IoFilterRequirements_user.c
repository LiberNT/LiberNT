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
#include <cfgmgr32.h>
#include <newdev.h>

#define TEST_REPORTED_MAGIC 0x4B465251
#define TEST_DMA_CHANNEL 3
#define TEST_DMA_REQUEST_LINE 21
#define TEST_DMA_WIDTH 2

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR BusHardwareIds[] = L"ROOT\\KMTESTFILTERREQ\0";
static const WCHAR ChildHardwareIds[] = L"KMTEST\\FILTERREQCHILD\0";
static const WCHAR ChildInstance[] = L"KMTEST\\FILTERREQCHILD\\0";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestFilterReq";
static const PCWSTR Values[] = { L"FilterInformationIsList", L"FilterReportedMagic", L"FilterBusKeptList", L"StartPrivateMask",
                                 L"StartCount", L"StopSeen", L"RestartPrivateMask", L"EjectChild", L"EjectAfterRemove",
                                 L"BusRelationsQueried", L"StartDmaV3" };

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
    ok(ReadValue(L"StartDmaV3", &Value) && Value == (TEST_DMA_CHANNEL | (TEST_DMA_REQUEST_LINE << 8) | (TEST_DMA_WIDTH << 16)),
       "The start resources lost the version 3 DMA channel, request line or width (0x%lx)\n", Value);
}

/* The function driver reports new requirements after its first start; the wait only bounds a lost restart */
static VOID CheckRestart(VOID)
{
    DWORD Start = GetTickCount();
    DWORD Value = 0;

    while ((!ReadValue(L"StartCount", &Value) || Value < 2) && GetTickCount() - Start < 1000)
        Sleep(10);

    ok(Value == 2, "The device was not started again after its requirements changed (%lu starts)\n", Value);
    ok(!ReadValue(L"StopSeen", &Value), "The device was stopped for a change of its requirements\n");
    ok(ReadValue(L"RestartPrivateMask", &Value) && Value == 3, "The restart did not carry the device's resources (mask %lu)\n", Value);
}

/* A user-mode reenumeration asks the bus driver for its children again */
static VOID CheckReenumerate(DEVINST Bus)
{
    DWORD Queried = 0, Value = 0;

    ok(ReadValue(L"BusRelationsQueried", &Queried), "The bus driver was never asked for its children\n");
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);
    ok(ReadValue(L"BusRelationsQueried", &Value) && Value > Queried, "Reenumerating the bus did not ask it for its children\n");
}

/* The bus driver asks for its child's eject at the next enumeration; the wait only bounds a lost eject */
static VOID CheckEject(DEVINST Bus)
{
    DWORD Start, Value = 1;
    ULONG Status = 0, Problem = 0;
    DEVINST Child;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
    {
        ok(FALSE, "The fixture's service key is missing\n");
        return;
    }
    RegSetValueExW(Key, L"EjectChild", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);

    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    Start = GetTickCount();
    while (!ReadValue(L"EjectAfterRemove", &Value) && GetTickCount() - Start < 1000)
        Sleep(10);

    ok(ReadValue(L"EjectAfterRemove", &Value), "The child was not ejected\n");
    ok(Value == 1, "The child was ejected before it was removed\n");

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS)
        return;
    ok_eq_ulong(CM_Get_DevNode_Status(&Status, &Problem, Child, 0), (ULONG)CR_SUCCESS);
    ok(Problem == CM_PROB_HELD_FOR_EJECT, "The ejected child has problem %lu (status 0x%lx)\n", Problem, Status);
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
        CheckRestart();
        CheckReenumerate(Device->DevInst);
        CheckEject(Device->DevInst);
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
