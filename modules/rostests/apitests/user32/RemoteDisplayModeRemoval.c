/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test removing the remote display adapter right after its monitor was reconnected
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
#define REMOTE_INSTANCE    L"ModeRemoval0001"
#define CYCLES             8
#define MODE_CHANGES       12

typedef NTSTATUS (APIENTRY *PFN_OPEN_FROM_DEVICE_NAME)(D3DKMT_OPENADAPTERFROMDEVICENAME *);
typedef NTSTATUS (APIENTRY *PFN_ESCAPE)(const D3DKMT_ESCAPE *);
typedef NTSTATUS (APIENTRY *PFN_CLOSE_ADAPTER)(const D3DKMT_CLOSEADAPTER *);

typedef struct _CREATE_RESULT
{
    HANDLE Event;
    HRESULT Result;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
} CREATE_RESULT, *PCREATE_RESULT;

static const ULONG Modes[][2] = { { 800, 600 }, { 1024, 768 }, { 640, 480 }, { 1280, 720 } };

static PFN_OPEN_FROM_DEVICE_NAME pOpenFromDeviceName;
static PFN_ESCAPE pEscape;
static PFN_CLOSE_ADAPTER pCloseAdapter;

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
GetMonitor(
    _In_ PCWSTR InstanceId,
    _Out_writes_(Length) PWSTR Monitor,
    _In_ ULONG Length)
{
    DEVINST Adapter, Child;
    DWORD Start = GetTickCount();

    do
    {
        if (CM_Locate_DevNodeW(&Adapter, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            CM_Get_Child(&Child, Adapter, 0) == CR_SUCCESS &&
            CM_Get_Device_IDW(Child, Monitor, Length, 0) == CR_SUCCESS)
        {
            return TRUE;
        }
        Sleep(100);
    } while (GetTickCount() - Start < 15000);
    return FALSE;
}

static BOOL
WaitForGone(
    _In_ PCWSTR InstanceId)
{
    DEVINST DevInst;
    DWORD Start = GetTickCount();

    do
    {
        if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            return TRUE;
        Sleep(100);
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
UninstallTestDevice(VOID)
{
    DEVINST DevInst;

    if (CM_Locate_DevNodeW(&DevInst, (DEVINSTID_W)(L"SWD\\" REMOTE_ENUMERATOR L"\\" REMOTE_INSTANCE),
                           CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
    {
        CM_Uninstall_DevNode(DevInst, 0);
    }
}

START_TEST(RemoteDisplayModeRemoval)
{
    static const WCHAR HardwareIds[] = REMOTE_HARDWARE_ID L"\0";
    SW_DEVICE_CREATE_INFO Info;
    CREATE_RESULT Result = { 0 };
    D3DKMT_CLOSEADAPTER Close;
    D3DKMT_HANDLE Adapter;
    HSWDEVICE SwDevice;
    WCHAR Monitor[MAX_DEVICE_ID_LEN];
    ULONG Cycle, Change;
    HRESULT hr;

    pOpenFromDeviceName = (PFN_OPEN_FROM_DEVICE_NAME)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTOpenAdapterFromDeviceName");
    pEscape = (PFN_ESCAPE)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTEscape");
    pCloseAdapter = (PFN_CLOSE_ADAPTER)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTCloseAdapter");
    if (!pOpenFromDeviceName || !pEscape || !pCloseAdapter)
    {
        skip("No D3DKMT functions\n");
        return;
    }

    Result.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    Info.pszInstanceId = REMOTE_INSTANCE;
    Info.pszzHardwareIds = HardwareIds;
    Info.pszzCompatibleIds = HardwareIds;
    Info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    Info.pszDeviceDescription = L"LiberNT remote display removal test";

    for (Cycle = 0; Cycle < CYCLES; Cycle++)
    {
        ResetEvent(Result.Event);
        Result.Result = E_PENDING;
        SwDevice = NULL;
        hr = SwDeviceCreate(REMOTE_ENUMERATOR, L"HTREE\\ROOT\\0", &Info, 0, NULL, CreateCallback, &Result, &SwDevice);
        if (FAILED(hr))
        {
            skip("SwDeviceCreate: 0x%08lx\n", hr);
            break;
        }
        if (WaitForSingleObject(Result.Event, 15000) != WAIT_OBJECT_0 || FAILED(Result.Result) ||
            !HasDriver(Result.InstanceId))
        {
            if (Cycle == 0)
                skip("No started driver for %ls\n", REMOTE_HARDWARE_ID);
            else
                ok(FALSE, "%lu: %ls did not start again\n", Cycle, REMOTE_HARDWARE_ID);
            SwDeviceClose(SwDevice);
            break;
        }
        Adapter = OpenAdapter(Result.InstanceId);
        ok(Adapter != 0, "%lu: cannot open the remote display adapter\n", Cycle);
        if (!Adapter)
        {
            SwDeviceClose(SwDevice);
            break;
        }
        ok(GetMonitor(Result.InstanceId, Monitor, ARRAYSIZE(Monitor)), "%lu: the adapter has no monitor\n", Cycle);

        for (Change = 0; Change < MODE_CHANGES; Change++)
        {
            const ULONG *Mode = Modes[(Cycle + Change) % ARRAYSIZE(Modes)];
            NTSTATUS Status = SetMode(Adapter, Mode[0], Mode[1]);

            ok(Status == STATUS_SUCCESS, "%lu.%lu: SET_MODE %lux%lu returned 0x%08lx\n",
               Cycle, Change, Mode[0], Mode[1], Status);
            Sleep((Change * 37) % 120);
        }

        Close.hAdapter = Adapter;
        ok_hex(pCloseAdapter(&Close), STATUS_SUCCESS);
        SwDeviceClose(SwDevice);
        ok(WaitForGone(Result.InstanceId), "%lu: %ls is still present\n", Cycle, Result.InstanceId);
        ok(WaitForGone(Monitor), "%lu: %ls is still present\n", Cycle, Monitor);
    }

    UninstallTestDevice();
    CloseHandle(Result.Event);
}
