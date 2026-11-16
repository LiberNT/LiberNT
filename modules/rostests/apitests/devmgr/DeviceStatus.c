/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The device property sheet reports a device that is not present as not connected (Code 45)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wchar.h>

typedef INT (WINAPI *PFN_DEVICE_PROPERTIES)(HWND, LPCWSTR, LPCWSTR, BOOL);

static PFN_DEVICE_PROPERTIES pDevicePropertiesW;
static WCHAR InstanceId[MAX_DEVICE_ID_LEN];
static INT SheetResult = -1;

static DWORD WINAPI
ShowSheet(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    SheetResult = pDevicePropertiesW(NULL, NULL, InstanceId, FALSE);
    return 0;
}

static BOOL CALLBACK
FindSheet(HWND Window, LPARAM Parameter)
{
    DWORD ProcessId = 0;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId != GetCurrentProcessId() || !IsWindowVisible(Window))
        return TRUE;
    *(HWND *)Parameter = Window;
    return FALSE;
}

static BOOL CALLBACK
FindNotConnectedStatus(HWND Window, LPARAM Parameter)
{
    WCHAR Class[16];
    WCHAR Text[512];

    if (GetClassNameW(Window, Class, ARRAYSIZE(Class)) && !_wcsicmp(Class, L"Edit") &&
        GetWindowTextW(Window, Text, ARRAYSIZE(Text)) && wcsstr(Text, L"(Code 45)") != NULL)
    {
        *(BOOL *)Parameter = TRUE;
        return FALSE;
    }
    return TRUE;
}

static BOOL
CanOpenDevice(LPCWSTR Id)
{
    SP_DEVINFO_DATA Data = { sizeof(Data) };
    HDEVINFO Set;
    BOOL Opened;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;
    Opened = SetupDiOpenDeviceInfoW(Set, Id, NULL, 0, &Data);
    SetupDiDestroyDeviceInfoList(Set);
    return Opened;
}

static BOOL
FindNotPresentDevice(VOID)
{
    SP_DEVINFO_DATA Data = { sizeof(Data) };
    ULONG Status, Problem;
    HDEVINFO Set;
    DWORD Index;
    BOOL Found = FALSE;

    Set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;

    for (Index = 0; !Found && SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0) == CR_NO_SUCH_DEVINST &&
            SetupDiGetDeviceInstanceIdW(Set, &Data, InstanceId, ARRAYSIZE(InstanceId), NULL) &&
            CanOpenDevice(InstanceId))
        {
            Found = TRUE;
        }
    }
    SetupDiDestroyDeviceInfoList(Set);
    return Found;
}

START_TEST(DeviceStatus)
{
    BOOL Found = FALSE;
    HWND Sheet = NULL;
    HMODULE DevMgr;
    HANDLE Thread;
    DWORD ThreadId;
    ULONG Tries;

    DevMgr = LoadLibraryW(L"devmgr.dll");
    pDevicePropertiesW = DevMgr ? (PFN_DEVICE_PROPERTIES)GetProcAddress(DevMgr, "DevicePropertiesW") : NULL;
    if (!pDevicePropertiesW)
    {
        skip("devmgr.dll!DevicePropertiesW is not available\n");
        return;
    }

    if (!FindNotPresentDevice())
    {
        skip("no installed device is absent from this machine\n");
        return;
    }

    Thread = CreateThread(NULL, 0, ShowSheet, NULL, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return;

    for (Tries = 0; Tries < 100 && !Found; Tries++)
    {
        Sleep(100);
        Sheet = NULL;
        EnumWindows(FindSheet, (LPARAM)&Sheet);
        if (Sheet == NULL && WaitForSingleObject(Thread, 0) == WAIT_OBJECT_0)
            break;
        if (Sheet != NULL)
            EnumChildWindows(Sheet, FindNotConnectedStatus, (LPARAM)&Found);
    }
    ok(Sheet != NULL, "no property sheet for %ls (DevicePropertiesW returned %d)\n", InstanceId, SheetResult);
    ok(Found, "the status of %ls does not say (Code 45)\n", InstanceId);

    if (Sheet != NULL)
        PostMessageW(Sheet, WM_CLOSE, 0, 0);
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "the property sheet did not close\n");
    CloseHandle(Thread);
}
