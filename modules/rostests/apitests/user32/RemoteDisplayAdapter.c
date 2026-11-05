/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test a remote display adapter that comes and goes as a software device
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winreg.h>
#include <cfgmgr32.h>
#include <swdevice.h>

#define MAX_TEST_DEVICES 16
#define REMOTE_HARDWARE_ID L"RdpVdd_RemoteDisplay"
#define REMOTE_ENUMERATOR  L"LiberNTDisplayTest"
#define REMOTE_INSTANCE    L"RemoteDisplay0001"

typedef struct _CREATE_RESULT
{
    HANDLE Event;
    HRESULT Result;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
} CREATE_RESULT, *PCREATE_RESULT;

static LONG DisplayChanges;

static LRESULT CALLBACK
WatchProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (uMsg == WM_DISPLAYCHANGE)
        InterlockedIncrement(&DisplayChanges);
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static VOID
PumpFor(
    _In_ DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    do
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static VOID WINAPI
CreateCallback(
    _In_ HSWDEVICE hSwDevice,
    _In_ HRESULT CreateResult,
    _In_opt_ PVOID pContext,
    _In_opt_ PCWSTR pszDeviceInstanceId)
{
    PCREATE_RESULT Result = pContext;

    UNREFERENCED_PARAMETER(hSwDevice);
    Result->Result = CreateResult;
    lstrcpynW(Result->InstanceId, pszDeviceInstanceId ? pszDeviceInstanceId : L"", ARRAYSIZE(Result->InstanceId));
    SetEvent(Result->Event);
}

static BOOL
FindByDeviceId(
    _In_ PCWSTR DeviceId,
    _Out_ PDISPLAY_DEVICEW Found)
{
    DISPLAY_DEVICEW Device;
    ULONG Index;

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!lstrcmpiW(Device.DeviceID, DeviceId))
        {
            *Found = Device;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
FindPrimary(
    _Out_ PDISPLAY_DEVICEW Primary)
{
    DISPLAY_DEVICEW Device;
    ULONG Index;

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            *Primary = Device;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
WaitForDisplay(
    _In_ BOOL Present,
    _Out_ PDISPLAY_DEVICEW Found)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        if (FindByDeviceId(REMOTE_HARDWARE_ID, Found) == Present)
            return TRUE;
        if (GetTickCount() - Start > 15000)
            return FALSE;
        PumpFor(100);
    }
}

static BOOL
HasDriver(
    _In_ PCWSTR InstanceId)
{
    DEVINST DevInst;
    ULONG Status, Problem;
    DWORD Start = GetTickCount();

    for (;;)
    {
        if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0) == CR_SUCCESS)
        {
            if (Status & DN_STARTED)
                return TRUE;
            if ((Status & DN_HAS_PROBLEM) && Problem != CM_PROB_NOT_CONFIGURED)
                return FALSE;
        }
        if (GetTickCount() - Start > 15000)
            return FALSE;
        Sleep(100);
    }
}

static VOID
UninstallTestDevice(VOID)
{
    DEVINST DevInst;

    if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)(L"SWD\\" REMOTE_ENUMERATOR L"\\" REMOTE_INSTANCE),
                           CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
    {
        CM_Uninstall_DevNode(DevInst, 0);
    }
}

START_TEST(RemoteDisplayAdapter)
{
    static const WCHAR HardwareIds[] = REMOTE_HARDWARE_ID L"\0";
    SW_DEVICE_CREATE_INFO Info;
    CREATE_RESULT Result = { 0 };
    DISPLAY_DEVICEW Primary, PrimaryAfter, Remote, Device;
    DEVMODEW Before, After;
    WNDCLASSW Class = { 0 };
    HSWDEVICE SwDevice;
    HWND hWatch;
    ULONG Round;
    HRESULT hr;

    if (FindByDeviceId(REMOTE_HARDWARE_ID, &Device))
    {
        skip("A remote display adapter already exists as %ls\n", Device.DeviceName);
        return;
    }
    ok(FindPrimary(&Primary), "No primary display device\n");
    ZeroMemory(&Before, sizeof(Before));
    Before.dmSize = sizeof(Before);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Before), "EnumDisplaySettingsW failed\n");

    Class.lpfnWndProc = WatchProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"RemoteDisplayAdapterWatch";
    RegisterClassW(&Class);
    hWatch = CreateWindowExW(0, Class.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, Class.hInstance, NULL);
    ok(hWatch != NULL, "CreateWindowExW failed: %lu\n", GetLastError());

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = REMOTE_INSTANCE;
    Info.pszzHardwareIds = HardwareIds;
    Info.pszzCompatibleIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"LiberNT remote display test";

    for (Round = 0; Round < 3; Round++)
    {
        PumpFor(500);
        InterlockedExchange(&DisplayChanges, 0);
        ResetEvent(Result.Event);
        Result.Result = E_PENDING;
        SwDevice = NULL;
        hr = SwDeviceCreate(REMOTE_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, &Result, &SwDevice);
        if (hr == E_ACCESSDENIED)
        {
            skip("SwDeviceCreate needs an administrator\n");
            break;
        }
        ok(hr == S_OK, "%lu: SwDeviceCreate: 0x%08lx\n", Round, hr);
        if (FAILED(hr))
            break;
        ok(WaitForSingleObject(Result.Event, 15000) == WAIT_OBJECT_0, "%lu: no create callback\n", Round);
        ok(Result.Result == S_OK, "%lu: create result 0x%08lx\n", Round, Result.Result);

        if (!HasDriver(Result.InstanceId))
        {
            skip("No started driver for %ls\n", REMOTE_HARDWARE_ID);
            SwDeviceClose(SwDevice);
            break;
        }

        ok(WaitForDisplay(TRUE, &Remote), "%lu: the remote display adapter is not listed\n", Round);
        ok(!(Remote.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)),
           "%lu: %ls has flags 0x%lx\n", Round, Remote.DeviceName, Remote.StateFlags);
        ok(FindPrimary(&PrimaryAfter) && !lstrcmpiW(PrimaryAfter.DeviceName, Primary.DeviceName),
           "%lu: primary %ls became %ls\n", Round, Primary.DeviceName, PrimaryAfter.DeviceName);
        ok(lstrcmpiW(Remote.DeviceName, Primary.DeviceName) != 0, "%lu: the remote display took %ls\n",
           Round, Primary.DeviceName);
        ok(GetSystemMetrics(SM_CMONITORS) == 1, "%lu: SM_CMONITORS is %d\n", Round, GetSystemMetrics(SM_CMONITORS));
        PumpFor(1500);
        ok(DisplayChanges == 0, "%lu: adding the adapter sent %ld WM_DISPLAYCHANGE\n", Round, DisplayChanges);

        SwDeviceClose(SwDevice);
        ok(WaitForDisplay(FALSE, &Device), "%lu: %ls is still listed\n", Round, Device.DeviceName);
        ok(FindPrimary(&PrimaryAfter) && !lstrcmpiW(PrimaryAfter.DeviceName, Primary.DeviceName),
           "%lu: primary %ls became %ls after the removal\n", Round, Primary.DeviceName, PrimaryAfter.DeviceName);

        ZeroMemory(&After, sizeof(After));
        After.dmSize = sizeof(After);
        ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &After) &&
           After.dmPelsWidth == Before.dmPelsWidth && After.dmPelsHeight == Before.dmPelsHeight,
           "%lu: desktop is %lux%lu, was %lux%lu\n", Round, After.dmPelsWidth, After.dmPelsHeight,
           Before.dmPelsWidth, Before.dmPelsHeight);
    }

    UninstallTestDevice();
    CloseHandle(Result.Event);
    if (hWatch)
        DestroyWindow(hWatch);
}
