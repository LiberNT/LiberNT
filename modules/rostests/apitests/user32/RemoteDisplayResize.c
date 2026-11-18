/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test resizing the remote display while the desktop is on it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <ntstatus.h>
#include <winternl.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <dbt.h>
#include <initguid.h>
#include <ntddvdeo.h>
#include <d3dkmthk.h>
#include <drivers/directx/rdpvdd.h>

#define REMOTE_HARDWARE_ID L"RdpVdd_RemoteDisplay"
#define REMOTE_ENUMERATOR  L"LiberNTDisplayTest"
#define REMOTE_INSTANCE    L"Resize0001"
#define MAX_TEST_DEVICES   16

typedef NTSTATUS (APIENTRY *PFN_OPEN_FROM_DEVICE_NAME)(D3DKMT_OPENADAPTERFROMDEVICENAME *);
typedef NTSTATUS (APIENTRY *PFN_ESCAPE)(const D3DKMT_ESCAPE *);
typedef NTSTATUS (APIENTRY *PFN_CLOSE_ADAPTER)(const D3DKMT_CLOSEADAPTER *);

typedef struct _CREATE_RESULT
{
    HANDLE Event;
    HRESULT Result;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
} CREATE_RESULT, *PCREATE_RESULT;

static const ULONG Modes[][2] = { { 1100, 700 }, { 800, 600 }, { 1280, 800 }, { 1024, 640 } };

static PFN_OPEN_FROM_DEVICE_NAME pOpenFromDeviceName;
static PFN_ESCAPE pEscape;
static PFN_CLOSE_ADAPTER pCloseAdapter;
static LONG DisplayChanges;
static LPARAM LastDisplayChange;
static LONG MonitorArrivals;
static LONG MonitorRemovals;

static LRESULT CALLBACK
WatchProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (uMsg == WM_DISPLAYCHANGE)
    {
        InterlockedIncrement(&DisplayChanges);
        LastDisplayChange = lParam;
    }
    else if (uMsg == WM_DEVICECHANGE && lParam &&
             ((PDEV_BROADCAST_HDR)lParam)->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE &&
             IsEqualGUID(&((PDEV_BROADCAST_DEVICEINTERFACE_W)lParam)->dbcc_classguid, &GUID_DEVINTERFACE_MONITOR))
    {
        if (wParam == DBT_DEVICEARRIVAL)
            InterlockedIncrement(&MonitorArrivals);
        else if (wParam == DBT_DEVICEREMOVECOMPLETE)
            InterlockedIncrement(&MonitorRemovals);
    }
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
HasDriver(
    _In_ PCWSTR InstanceId)
{
    DEVINST DevInst;
    ULONG Status, Problem;
    DWORD Start = GetTickCount();

    do
    {
        if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0) == CR_SUCCESS)
        {
            if (Status & DN_STARTED)
                return TRUE;
            if ((Status & DN_HAS_PROBLEM) && Problem != CM_PROB_NOT_CONFIGURED)
                return FALSE;
        }
        Sleep(100);
    } while (GetTickCount() - Start < 15000);
    return FALSE;
}

static BOOL
FindRemoteDisplay(
    _Out_ PDISPLAY_DEVICEW Remote)
{
    DWORD Start = GetTickCount();
    ULONG Index;

    do
    {
        for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
        {
            ZeroMemory(Remote, sizeof(*Remote));
            Remote->cb = sizeof(*Remote);
            if (!EnumDisplayDevicesW(NULL, Index, Remote, 0))
                break;
            if (!lstrcmpiW(Remote->DeviceID, REMOTE_HARDWARE_ID))
                return TRUE;
        }
        PumpFor(100);
    } while (GetTickCount() - Start < 15000);
    return FALSE;
}

static BOOL
WaitForPrimary(
    _In_ PCWSTR DeviceName)
{
    DISPLAY_DEVICEW Device;
    DWORD Start = GetTickCount();
    ULONG Index;

    do
    {
        for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
        {
            ZeroMemory(&Device, sizeof(Device));
            Device.cb = sizeof(Device);
            if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
                break;
            if ((Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) && !lstrcmpiW(Device.DeviceName, DeviceName))
                return TRUE;
        }
        PumpFor(100);
    } while (GetTickCount() - Start < 15000);
    return FALSE;
}

static D3DKMT_HANDLE
OpenAdapter(
    _In_ PCWSTR InstanceId)
{
    D3DKMT_OPENADAPTERFROMDEVICENAME Open = { 0 };
    WCHAR Interfaces[512];
    DWORD Start = GetTickCount();

    do
    {
        if (CM_Get_Device_Interface_ListW((LPGUID)&GUID_DISPLAY_DEVICE_ARRIVAL, (DEVINSTID_W)InstanceId, Interfaces,
                                          ARRAYSIZE(Interfaces), CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS &&
            Interfaces[0])
        {
            Open.pDeviceName = Interfaces;
            if (NT_SUCCESS(pOpenFromDeviceName(&Open)))
                return Open.hAdapter;
        }
        Sleep(100);
    } while (GetTickCount() - Start < 15000);
    return 0;
}

static NTSTATUS
SetMode(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    RDPVDD_ESCAPE Request = { RDPVDD_ESCAPE_SET_MODE, Width, Height };
    D3DKMT_ESCAPE Escape = { 0 };

    Escape.hAdapter = Adapter;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Request;
    Escape.PrivateDriverDataSize = sizeof(Request);
    return pEscape(&Escape);
}

static VOID
TestResize(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ PCWSTR DeviceName,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ LONG ExpectedChanges)
{
    DEVMODEW Mode;
    DWORD Start;

    InterlockedExchange(&DisplayChanges, 0);
    InterlockedExchange(&MonitorArrivals, 0);
    InterlockedExchange(&MonitorRemovals, 0);
    LastDisplayChange = 0;
    ok_hex(SetMode(Adapter, Width, Height), STATUS_SUCCESS);
    for (Start = GetTickCount(); GetTickCount() - Start < 10000; PumpFor(50))
    {
        if (GetSystemMetrics(SM_CXSCREEN) == (INT)Width && GetSystemMetrics(SM_CYSCREEN) == (INT)Height)
            break;
    }
    PumpFor(2000);
    ok(DisplayChanges == ExpectedChanges, "%lux%lu: %ld WM_DISPLAYCHANGE, expected %ld\n",
       Width, Height, DisplayChanges, ExpectedChanges);
    if (ExpectedChanges)
    {
        ok(LOWORD(LastDisplayChange) == Width && HIWORD(LastDisplayChange) == Height,
           "%lux%lu: the last WM_DISPLAYCHANGE was %ux%u\n", Width, Height,
           LOWORD(LastDisplayChange), HIWORD(LastDisplayChange));
    }
    ok(GetSystemMetrics(SM_CXSCREEN) == (INT)Width && GetSystemMetrics(SM_CYSCREEN) == (INT)Height,
       "%lux%lu: the screen is %dx%d\n", Width, Height, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    ok(MonitorRemovals == ExpectedChanges && MonitorArrivals == ExpectedChanges,
       "%lux%lu: %ld monitor removals and %ld arrivals, expected %ld\n", Width, Height, MonitorRemovals,
       MonitorArrivals, ExpectedChanges);
    ok(GetSystemMetrics(SM_CMONITORS) == 1, "%lux%lu: SM_CMONITORS is %d\n", Width, Height,
       GetSystemMetrics(SM_CMONITORS));
    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    ok(EnumDisplaySettingsW(DeviceName, ENUM_CURRENT_SETTINGS, &Mode) &&
       Mode.dmPelsWidth == Width && Mode.dmPelsHeight == Height,
       "%lux%lu: %ls is at %lux%lu\n", Width, Height, DeviceName, Mode.dmPelsWidth, Mode.dmPelsHeight);
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

START_TEST(RemoteDisplayResize)
{
    static const WCHAR HardwareIds[] = REMOTE_HARDWARE_ID L"\0";
    SW_DEVICE_CREATE_INFO Info = { 0 };
    CREATE_RESULT Result = { 0 };
    D3DKMT_CLOSEADAPTER Close;
    D3DKMT_HANDLE Adapter = 0;
    DISPLAY_DEVICEW Remote;
    WNDCLASSW Class = { 0 };
    DEV_BROADCAST_DEVICEINTERFACE_W Filter = { 0 };
    HDEVNOTIFY Notify = NULL;
    HSWDEVICE SwDevice = NULL;
    HWND hWatch;
    DWORD Start;
    ULONG Index;
    HRESULT hr;

    pOpenFromDeviceName = (PFN_OPEN_FROM_DEVICE_NAME)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTOpenAdapterFromDeviceName");
    pEscape = (PFN_ESCAPE)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTEscape");
    pCloseAdapter = (PFN_CLOSE_ADAPTER)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCloseAdapter");
    if (!pOpenFromDeviceName || !pEscape || !pCloseAdapter)
    {
        skip("No D3DKMT functions\n");
        return;
    }

    Class.lpfnWndProc = WatchProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"RemoteDisplayResizeWatch";
    RegisterClassW(&Class);
    hWatch = CreateWindowExW(0, Class.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, Class.hInstance, NULL);
    ok(hWatch != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    Filter.dbcc_size = sizeof(Filter);
    Filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    Filter.dbcc_classguid = GUID_DEVINTERFACE_MONITOR;
    Notify = RegisterDeviceNotificationW(hWatch, &Filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    ok(Notify != NULL, "RegisterDeviceNotificationW failed: %lu\n", GetLastError());

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = REMOTE_INSTANCE;
    Info.pszzHardwareIds = HardwareIds;
    Info.pszzCompatibleIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"LiberNT remote display resize test";
    hr = SwDeviceCreate(REMOTE_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, &Result, &SwDevice);
    if (FAILED(hr) || WaitForSingleObject(Result.Event, 15000) != WAIT_OBJECT_0 || FAILED(Result.Result) ||
        !HasDriver(Result.InstanceId) || !FindRemoteDisplay(&Remote))
    {
        skip("No started driver for %ls (0x%08lx)\n", REMOTE_HARDWARE_ID, hr);
        goto done;
    }
    Adapter = OpenAdapter(Result.InstanceId);
    ok(Adapter != 0, "Cannot open the remote display adapter\n");
    if (!Adapter)
        goto done;

    for (Start = GetTickCount(); MonitorArrivals == 0 && GetTickCount() - Start < 10000; PumpFor(50));
    ok(MonitorArrivals >= 1, "The remote display brought %ld monitor arrivals\n", MonitorArrivals);
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_EXTERNAL), ERROR_SUCCESS);
    ok(WaitForPrimary(Remote.DeviceName), "%ls did not become the primary\n", Remote.DeviceName);
    PumpFor(2000);

    for (Index = 0; Index < ARRAYSIZE(Modes); Index++)
        TestResize(Adapter, Remote.DeviceName, Modes[Index][0], Modes[Index][1], 1);
    TestResize(Adapter, Remote.DeviceName, Modes[ARRAYSIZE(Modes) - 1][0], Modes[ARRAYSIZE(Modes) - 1][1], 1);

    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
    PumpFor(2000);

done:
    if (Adapter)
    {
        Close.hAdapter = Adapter;
        ok_hex(pCloseAdapter(&Close), STATUS_SUCCESS);
    }
    if (SwDevice)
        SwDeviceClose(SwDevice);
    UninstallTestDevice();
    CloseHandle(Result.Event);
    if (Notify)
        UnregisterDeviceNotification(Notify);
    if (hWatch)
        DestroyWindow(hWatch);
}
