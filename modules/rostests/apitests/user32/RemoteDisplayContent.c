/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the remote display scans out the desktop composed on it
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <ntstatus.h>
#include <winternl.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <initguid.h>
#include <ntddvdeo.h>
#include <d3dkmthk.h>
#include <drivers/directx/rdpvdd.h>

#define REMOTE_HARDWARE_ID L"RdpVdd_RemoteDisplay"
#define REMOTE_ENUMERATOR  L"LiberNTDisplayTest"
#define REMOTE_INSTANCE    L"Content0001"
#define TEST_COLOR         RGB(0x20, 0x80, 0xC0)
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

static const ULONG Modes[][2] = { { 800, 600 }, { 1280, 800 } };

static PFN_OPEN_FROM_DEVICE_NAME pOpenFromDeviceName;
static PFN_ESCAPE pEscape;
static PFN_CLOSE_ADAPTER pCloseAdapter;

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

static NTSTATUS
FrameEscape(
    _In_ D3DKMT_HANDLE Adapter,
    _Inout_ PRDPVDD_FRAME_ESCAPE Frame)
{
    D3DKMT_ESCAPE Escape = { 0 };

    Escape.hAdapter = Adapter;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = Frame;
    Escape.PrivateDriverDataSize = sizeof(*Frame);
    return pEscape(&Escape);
}

static VOID
TestScanout(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ HWND hFill,
    _In_ PCSTR What)
{
    static const POINT Probes[] = { { 1, 1 }, { 2, 2 }, { 3, 2 }, { 2, 3 }, { 4, 4 } };
    RDPVDD_FRAME_ESCAPE Frame;
    HANDLE Event;
    PULONG Pixels;
    ULONG Width = GetSystemMetrics(SM_CXSCREEN), Height = GetSystemMetrics(SM_CYSCREEN);
    ULONG Expected = (GetRValue(TEST_COLOR) << 16) | (GetGValue(TEST_COLOR) << 8) | GetBValue(TEST_COLOR);
    ULONG Index, Pixel;

    SetWindowPos(hFill, HWND_TOPMOST, 0, 0, Width, Height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    /* The remote client draws the pointer, so the frame must not contain it */
    SetCursorPos(Width * Probes[2].x / 5 - 4, Height * Probes[2].y / 5 - 4);
    RedrawWindow(hFill, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    PumpFor(2000);

    Pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)Width * Height * sizeof(ULONG));
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Pixels != NULL && Event != NULL, "%s: no frame buffer\n", What);
    if (!Pixels || !Event)
        goto cleanup;
    ZeroMemory(&Frame, sizeof(Frame));
    Frame.Code = RDPVDD_ESCAPE_OPEN_FRAME;
    Frame.Event = (ULONG64)(ULONG_PTR)Event;
    ok_hex(FrameEscape(Adapter, &Frame), STATUS_SUCCESS);
    ZeroMemory(&Frame, sizeof(Frame));
    Frame.Code = RDPVDD_ESCAPE_READ_FRAME;
    Frame.Buffer = (ULONG64)(ULONG_PTR)Pixels;
    Frame.BufferWidth = Width;
    Frame.BufferHeight = Height;
    ok_hex(FrameEscape(Adapter, &Frame), STATUS_SUCCESS);
    ok(Frame.Width == Width && Frame.Height == Height, "%s: the remote display scans out %lux%lu, the screen is %lux%lu\n",
       What, Frame.Width, Frame.Height, Width, Height);
    ok(Frame.RectCount != 0, "%s: no frame was read\n", What);
    if (Frame.Width != Width || Frame.Height != Height || Frame.RectCount == 0)
        goto cleanup;
    for (Index = 0; Index < ARRAYSIZE(Probes); Index++)
    {
        ULONG X = Width * Probes[Index].x / 5, Y = Height * Probes[Index].y / 5;

        Pixel = Pixels[(SIZE_T)Y * Width + X] & 0xFFFFFF;
        ok(Pixel == Expected, "%s: pixel (%lu,%lu) of the remote display is 0x%06lx, expected 0x%06lx\n",
           What, X, Y, Pixel, Expected);
    }

cleanup:
    if (Event)
        CloseHandle(Event);
    if (Pixels)
        HeapFree(GetProcessHeap(), 0, Pixels);
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

START_TEST(RemoteDisplayContent)
{
    static const WCHAR HardwareIds[] = REMOTE_HARDWARE_ID L"\0";
    SW_DEVICE_CREATE_INFO Info = { 0 };
    CREATE_RESULT Result = { 0 };
    D3DKMT_CLOSEADAPTER Close;
    D3DKMT_HANDLE Adapter = 0;
    DISPLAY_DEVICEW Remote;
    WNDCLASSW Class = { 0 };
    HSWDEVICE SwDevice = NULL;
    HBRUSH Brush;
    HWND hFill = NULL;
    CHAR What[32];
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

    Brush = CreateSolidBrush(TEST_COLOR);
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = Brush;
    Class.lpszClassName = L"RemoteDisplayContentFill";
    RegisterClassW(&Class);

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = REMOTE_INSTANCE;
    Info.pszzHardwareIds = HardwareIds;
    Info.pszzCompatibleIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"LiberNT remote display content test";
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

    hFill = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"RemoteDisplayContentFill", L"",
                            WS_POPUP, 0, 0, 1, 1, NULL, NULL, Class.hInstance, NULL);
    ok(hFill != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_EXTERNAL), ERROR_SUCCESS);
    ok(WaitForPrimary(Remote.DeviceName), "%ls did not become the primary\n", Remote.DeviceName);
    PumpFor(2000);
    if (hFill)
        TestScanout(Adapter, hFill, "Moved to the remote display");

    for (Index = 0; hFill && Index < ARRAYSIZE(Modes); Index++)
    {
        ok_hex(SetMode(Adapter, Modes[Index][0], Modes[Index][1]), STATUS_SUCCESS);
        for (Start = GetTickCount(); GetTickCount() - Start < 10000; PumpFor(50))
        {
            if (GetSystemMetrics(SM_CXSCREEN) == (INT)Modes[Index][0] && GetSystemMetrics(SM_CYSCREEN) == (INT)Modes[Index][1])
                break;
        }
        sprintf(What, "%lux%lu", Modes[Index][0], Modes[Index][1]);
        TestScanout(Adapter, hFill, What);
    }

    ok_long(SetDisplayConfig(0, NULL, 0, NULL, SDC_APPLY | SDC_TOPOLOGY_INTERNAL), ERROR_SUCCESS);
    PumpFor(2000);

done:
    if (hFill)
        DestroyWindow(hFill);
    if (Adapter)
    {
        Close.hAdapter = Adapter;
        ok_hex(pCloseAdapter(&Close), STATUS_SUCCESS);
    }
    if (SwDevice)
        SwDeviceClose(SwDevice);
    UninstallTestDevice();
    CloseHandle(Result.Event);
    DeleteObject(Brush);
}
