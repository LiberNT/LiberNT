/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test moving the desktop between display adapters with SetDisplayConfig topologies
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winreg.h>
#include <cfgmgr32.h>
#include <swdevice.h>

#define MAX_TEST_DEVICES 16
#define REMOTE_HARDWARE_ID L"RdpVdd_RemoteDisplay"

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
    UNREFERENCED_PARAMETER(hSwDevice);
    UNREFERENCED_PARAMETER(CreateResult);
    UNREFERENCED_PARAMETER(pszDeviceInstanceId);
    SetEvent(pContext);
}

static BOOL
FindDevice(
    _In_opt_ PCWSTR DeviceName,
    _In_ BOOL Primary,
    _In_ BOOL Other,
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
        if (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
            continue;
        if (DeviceName && (lstrcmpiW(Device.DeviceName, DeviceName) == 0) == Other)
            continue;
        if (Primary && !(Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            continue;
        *Found = Device;
        return TRUE;
    }
    return FALSE;
}

static ULONG
CountDevices(VOID)
{
    DISPLAY_DEVICEW Device;
    ULONG Index, Count = 0;

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!(Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
            Count++;
    }
    return Count;
}

static BOOL
WaitForPrimary(
    _In_ PCWSTR DeviceName,
    _Out_ PDISPLAY_DEVICEW Found)
{
    DWORD Start = GetTickCount();

    for (;;)
    {
        if (FindDevice(DeviceName, TRUE, FALSE, Found))
            return TRUE;
        if (GetTickCount() - Start > 15000)
            return FALSE;
        PumpFor(100);
    }
}

static HSWDEVICE
CreateRemoteDisplay(
    _Out_ PDISPLAY_DEVICEW Remote)
{
    static const WCHAR HardwareIds[] = REMOTE_HARDWARE_ID L"\0";
    SW_DEVICE_CREATE_INFO Info = { 0 };
    HSWDEVICE SwDevice = NULL;
    DISPLAY_DEVICEW Device;
    HANDLE Event;
    DWORD Start;
    ULONG Index;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = L"DisplayTopology";
    Info.pszzHardwareIds = HardwareIds;
    Info.pszzCompatibleIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"LiberNT display topology test";
    if (FAILED(SwDeviceCreate(L"LiberNTDisplayTest", L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, Event,
                              &SwDevice)) ||
        WaitForSingleObject(Event, 15000) != WAIT_OBJECT_0)
    {
        CloseHandle(Event);
        if (SwDevice)
            SwDeviceClose(SwDevice);
        return NULL;
    }
    CloseHandle(Event);

    for (Start = GetTickCount(); GetTickCount() - Start < 15000; PumpFor(100))
    {
        for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
        {
            ZeroMemory(&Device, sizeof(Device));
            Device.cb = sizeof(Device);
            if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
                break;
            if (!lstrcmpiW(Device.DeviceID, REMOTE_HARDWARE_ID))
            {
                *Remote = Device;
                return SwDevice;
            }
        }
    }
    SwDeviceClose(SwDevice);
    return NULL;
}

static VOID
TestOneDisplay(VOID)
{
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_EXTERNAL), ERROR_GEN_FAILURE);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_CLONE), ERROR_GEN_FAILURE);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_EXTEND), ERROR_GEN_FAILURE);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
}

static VOID
TestMove(
    _In_ PDISPLAY_DEVICEW Internal,
    _In_ PDISPLAY_DEVICEW External,
    _In_opt_ HSWDEVICE SwDevice)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Before, Mode;
    LONG Result;

    ZeroMemory(&Before, sizeof(Before));
    Before.dmSize = sizeof(Before);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Before), "EnumDisplaySettingsW failed\n");

    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_EXTERNAL), ERROR_SUCCESS);
    PumpFor(500);
    InterlockedExchange(&DisplayChanges, 0);
    Result = SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_EXTERNAL);
    ok_long(Result, ERROR_SUCCESS);
    if (Result != ERROR_SUCCESS)
        return;
    ok(WaitForPrimary(External->DeviceName, &Device), "%ls did not become the primary\n", External->DeviceName);
    PumpFor(2000);
    ok_long(DisplayChanges, 1);
    ok((Device.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)) ==
       (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE),
       "%ls has flags 0x%lx\n", Device.DeviceName, Device.StateFlags);
    ok(FindDevice(Internal->DeviceName, FALSE, FALSE, &Device) &&
       !(Device.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)),
       "%ls has flags 0x%lx\n", Internal->DeviceName, Device.StateFlags);
    ok_int(GetSystemMetrics(SM_CMONITORS), 1);
    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    ok(EnumDisplaySettingsW(External->DeviceName, ENUM_CURRENT_SETTINGS, &Mode) &&
       GetSystemMetrics(SM_CXSCREEN) == (INT)Mode.dmPelsWidth && GetSystemMetrics(SM_CYSCREEN) == (INT)Mode.dmPelsHeight,
       "Screen %dx%d, %ls %lux%lu\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
       External->DeviceName, Mode.dmPelsWidth, Mode.dmPelsHeight);
    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    ok(!EnumDisplaySettingsW(Internal->DeviceName, ENUM_CURRENT_SETTINGS, &Mode),
       "%ls still has a current mode %lux%lu\n", Internal->DeviceName, Mode.dmPelsWidth, Mode.dmPelsHeight);

    InterlockedExchange(&DisplayChanges, 0);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_EXTERNAL), ERROR_SUCCESS);
    PumpFor(1500);
    ok_long(DisplayChanges, 0);

    InterlockedExchange(&DisplayChanges, 0);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
    ok(WaitForPrimary(Internal->DeviceName, &Device), "%ls did not come back as the primary\n", Internal->DeviceName);
    PumpFor(2000);
    ok_long(DisplayChanges, 1);
    ok(FindDevice(External->DeviceName, FALSE, FALSE, &Device) &&
       !(Device.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)),
       "%ls has flags 0x%lx\n", External->DeviceName, Device.StateFlags);
    ok(GetSystemMetrics(SM_CXSCREEN) == (INT)Before.dmPelsWidth && GetSystemMetrics(SM_CYSCREEN) == (INT)Before.dmPelsHeight,
       "Screen %dx%d, was %lux%lu\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
       Before.dmPelsWidth, Before.dmPelsHeight);

    if (SwDevice)
        SwDeviceClose(SwDevice);
}

START_TEST(DisplayTopology)
{
    DISPLAY_DEVICEW Internal, External;
    WNDCLASSW Class = { 0 };
    HSWDEVICE SwDevice = NULL;
    HWND hWatch;

    Class.lpfnWndProc = WatchProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DisplayTopologyWatch";
    RegisterClassW(&Class);
    hWatch = CreateWindowExW(0, Class.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, Class.hInstance, NULL);
    ok(hWatch != NULL, "CreateWindowExW failed: %lu\n", GetLastError());

    if (CountDevices() == 1)
        TestOneDisplay();
    else
        skip("%lu display devices\n", CountDevices());

    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
    PumpFor(1000);
    if (!FindDevice(NULL, TRUE, FALSE, &Internal))
    {
        skip("No primary display device\n");
        goto done;
    }
    if (!FindDevice(Internal.DeviceName, FALSE, TRUE, &External))
    {
        SwDevice = CreateRemoteDisplay(&External);
        if (!SwDevice)
        {
            skip("No second display device\n");
            goto done;
        }
    }
    trace("Internal %ls '%ls', external %ls '%ls'\n", Internal.DeviceName, Internal.DeviceString,
          External.DeviceName, External.DeviceString);
    TestMove(&Internal, &External, SwDevice);

done:
    if (hWatch)
        DestroyWindow(hWatch);
}
