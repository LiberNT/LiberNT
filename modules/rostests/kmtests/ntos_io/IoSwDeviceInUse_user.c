/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a software device created again while its removed predecessor is still open gets one
 *              device node, which starts once the predecessor's last handle is closed
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <initguid.h>

DEFINE_GUID(GUID_KMTEST_SWDEVICE_INUSE, 0x765f3e7f, 0x1406, 0x49c6, 0xb7, 0x44, 0x3e, 0x98, 0x67, 0xf2, 0x99, 0xa8);

#define TEST_ENUMERATOR L"KmtestSwDevInUse"
#define TEST_INSTANCE   L"InUse0001"
#define TEST_DEVICE_ID  L"SWD\\" TEST_ENUMERATOR L"\\" TEST_INSTANCE

static const WCHAR HardwareIds[] = L"KMTESTSWDEVINUSE\0";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestSwDevInUse";
static const PCWSTR Values[] = { L"Adds", L"Starts", L"Surprises", L"Removes", L"Opens", L"InterfaceFailures" };

typedef struct _CREATE_RESULT
{
    HANDLE Event;
    HRESULT Result;
} CREATE_RESULT, *PCREATE_RESULT;

static DWORD ReadValue(PCWSTR Name)
{
    DWORD Value = 0, Size = sizeof(Value), Type = 0;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)&Value, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Value = 0;
    RegCloseKey(Key);
    return Value;
}

static BOOL WaitValueFor(PCWSTR Name, DWORD Expected, DWORD Timeout)
{
    DWORD Start = GetTickCount();

    while (ReadValue(Name) < Expected)
    {
        if (GetTickCount() - Start > Timeout)
            return FALSE;
        Sleep(10);
    }
    return TRUE;
}

static BOOL WaitValue(PCWSTR Name, DWORD Expected)
{
    return WaitValueFor(Name, Expected, 15000);
}

static VOID WINAPI CreateCallback(HSWDEVICE Device, HRESULT CreateResult, PVOID Context, PCWSTR InstanceId)
{
    PCREATE_RESULT Result = Context;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(InstanceId);
    Result->Result = CreateResult;
    SetEvent(Result->Event);
}

static HRESULT CreateDevice(PCREATE_RESULT Result, HSWDEVICE *Device)
{
    SW_DEVICE_CREATE_INFO Info;

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = TEST_INSTANCE;
    Info.pszzHardwareIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"Kmtest software device in use";
    ResetEvent(Result->Event);
    Result->Result = E_PENDING;
    *Device = NULL;
    return SwDeviceCreate(TEST_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Result, Device);
}

static ULONG CountRootChildren(VOID)
{
    WCHAR Id[MAX_DEVICE_ID_LEN];
    DEVINST Root, Child, Next;
    ULONG Found = 0, Steps;

    if (CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
        CM_Get_Child(&Child, Root, 0) != CR_SUCCESS)
        return 0;
    for (Steps = 0; Steps < 4096; Steps++)
    {
        if (CM_Get_Device_IDW(Child, Id, RTL_NUMBER_OF(Id), 0) == CR_SUCCESS && !_wcsicmp(Id, TEST_DEVICE_ID))
            Found++;
        if (CM_Get_Sibling(&Next, Child, 0) != CR_SUCCESS || Next == Child)
            break;
        Child = Next;
    }
    return Found;
}

static HANDLE OpenInterface(VOID)
{
    BYTE Buffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + MAX_PATH * sizeof(WCHAR)];
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)Buffer;
    SP_DEVICE_INTERFACE_DATA Interface = { sizeof(Interface) };
    HANDLE File = INVALID_HANDLE_VALUE;
    HDEVINFO Devices;

    Devices = SetupDiGetClassDevsW(&GUID_KMTEST_SWDEVICE_INUSE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Devices == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (SetupDiEnumDeviceInterfaces(Devices, NULL, &GUID_KMTEST_SWDEVICE_INUSE, 0, &Interface))
    {
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, sizeof(Buffer), NULL, NULL))
            File = CreateFileW(Detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                               NULL);
    }
    SetupDiDestroyDeviceInfoList(Devices);
    return File;
}

static BOOL WaitForInterface(HANDLE *File)
{
    DWORD Start = GetTickCount();

    while ((*File = OpenInterface()) == INVALID_HANDLE_VALUE)
    {
        if (GetTickCount() - Start > 15000)
            return FALSE;
        Sleep(50);
    }
    return TRUE;
}

static BOOL WaitChildren(ULONG Expected)
{
    DWORD Start = GetTickCount();

    while (CountRootChildren() != Expected)
    {
        if (GetTickCount() - Start > 15000)
            return FALSE;
        Sleep(50);
    }
    return TRUE;
}

static VOID CheckStarted(VOID)
{
    ULONG Status, Problem;
    DEVINST DevInst;
    CONFIGRET cr;

    cr = CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)TEST_DEVICE_ID, CM_LOCATE_DEVNODE_NORMAL);
    ok(cr == CR_SUCCESS, "The device is not present: %lu\n", cr);
    if (cr != CR_SUCCESS)
        return;
    cr = CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0);
    ok(cr == CR_SUCCESS && (Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM),
       "Device status %lu: 0x%08lx, problem %lu\n", cr, Status, Problem);
}

static VOID Uninstall(VOID)
{
    DEVINST DevInst;

    if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)TEST_DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
        CM_Uninstall_DevNode(DevInst, 0);
}

START_TEST(IoSwDeviceInUse)
{
    CREATE_RESULT First = { 0 }, Second = { 0 };
    HSWDEVICE Device = NULL, Again = NULL;
    HANDLE File = INVALID_HANDLE_VALUE;
    WCHAR InfPath[MAX_PATH];
    PWSTR Filename;
    DWORD Length, Wait;
    BOOL Reboot = FALSE, Success;
    HRESULT hr;
    HKEY Key;
    ULONG Index;

    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(InfPath), "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= RTL_NUMBER_OF(InfPath))
        return;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_swdinuse.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "INF path is too long\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_swdinuse.inf");

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) == ERROR_SUCCESS)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
            RegDeleteValueW(Key, Values[Index]);
        RegCloseKey(Key);
    }
    Uninstall();

    First.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Second.Event = CreateEventW(NULL, TRUE, FALSE, NULL);

    hr = CreateDevice(&First, &Device);
    ok(hr == S_OK, "SwDeviceCreate: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;
    ok(WaitForSingleObject(First.Event, 15000) == WAIT_OBJECT_0, "No creation callback\n");

    if (!WaitValueFor(L"Starts", 1, 5000))
    {
        Success = UpdateDriverForPlugAndPlayDevicesW(NULL, HardwareIds, InfPath,
                                                     INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
        ok(Success, "Installing the fixture failed: %lu\n", GetLastError());
        if (!Success)
            goto Cleanup;
        ok(WaitValue(L"Starts", 1), "The device did not start\n");
    }
    ok_eq_ulong(ReadValue(L"Adds"), 1UL);
    ok(WaitForInterface(&File), "The device interface could not be opened: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        goto Cleanup;
    CheckStarted();

    SwDeviceClose(Device);
    Device = NULL;
    ok(WaitValue(L"Surprises", 1), "The open device was not surprise-removed\n");
    Sleep(500);
    ok_eq_ulong(ReadValue(L"Removes"), 0UL);

    hr = CreateDevice(&Second, &Again);
    ok(hr == S_OK, "SwDeviceCreate while the removed device is open: 0x%08lx\n", hr);
    Wait = WaitForSingleObject(Second.Event, 15000);
    ok(Wait == WAIT_OBJECT_0, "No creation callback while the removed device is open\n");
    ok(Second.Result == S_OK, "Creation result while the removed device is open: 0x%08lx\n", Second.Result);
    ok(WaitValue(L"Starts", 2), "The device created again did not start while the removed one is open\n");
    ok_eq_ulong(ReadValue(L"Adds"), 2UL);
    ok_eq_ulong(ReadValue(L"Removes"), 0UL);
    ok_eq_ulong(CountRootChildren(), 1UL);
    CheckStarted();

    CloseHandle(File);
    File = INVALID_HANDLE_VALUE;
    ok(WaitValue(L"Removes", 1), "The removed device was not removed after its last close\n");
    Sleep(500);
    ok_eq_ulong(ReadValue(L"Removes"), 1UL);
    ok_eq_ulong(ReadValue(L"Adds"), 2UL);
    ok_eq_ulong(ReadValue(L"InterfaceFailures"), 0UL);
    ok_eq_ulong(CountRootChildren(), 1UL);
    CheckStarted();

Cleanup:
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (Again != NULL)
    {
        SwDeviceClose(Again);
        ok(WaitValue(L"Removes", 2), "The device created again was not removed\n");
    }
    if (Device != NULL)
        SwDeviceClose(Device);
    ok(WaitChildren(0), "%lu device nodes after the last close\n", CountRootChildren());
    Uninstall();
    CloseHandle(First.Event);
    CloseHandle(Second.Event);
}
