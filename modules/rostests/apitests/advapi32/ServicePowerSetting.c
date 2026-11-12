/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a window in a service process gets its power setting notifications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <wingdi.h>
#include <winuser.h>

#include "svchlp.h"

/* GUID_CONSOLE_DISPLAY_STATE: win32k answers it from the display state */
static const GUID ConsoleDisplayState = { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };
/* GUID_ACDC_POWER_SOURCE: win32k answers it through a power manager setting callback */
static const GUID AcDcPowerSource = { 0x5d3e9a59, 0xe9d5, 0x4b00, { 0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48 } };

#define GOT_CONSOLE_DISPLAY_STATE 1
#define GOT_ACDC_POWER_SOURCE     2
#define GOT_BOTH                  (GOT_CONSOLE_DISPLAY_STATE | GOT_ACDC_POWER_SOURCE)

static ULONG Received;
static SERVICE_STATUS_HANDLE StatusHandle;

static LRESULT CALLBACK PowerWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_POWERBROADCAST && wParam == PBT_POWERSETTINGCHANGE)
    {
        const POWERBROADCAST_SETTING *Setting = (const POWERBROADCAST_SETTING *)lParam;

        if (IsEqualGUID(&Setting->PowerSetting, &ConsoleDisplayState))
            Received |= GOT_CONSOLE_DISPLAY_STATE;
        else if (IsEqualGUID(&Setting->PowerSetting, &AcDcPowerSource))
            Received |= GOT_ACDC_POWER_SOURCE;
        return TRUE;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

/* Registers both settings on a hidden window and returns which initial values arrived within a second */
static ULONG ReceiveInitialPowerSettings(void)
{
    WNDCLASSW Class = { 0 };
    HPOWERNOTIFY Display, Source;
    ULONGLONG Start;
    HWND Window;
    MSG Msg;

    Class.lpfnWndProc = PowerWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"ServicePowerSetting";
    RegisterClassW(&Class);
    Window = CreateWindowExW(0, Class.lpszClassName, L"", 0, 0, 0, 0, 0, NULL, NULL, Class.hInstance, NULL);
    if (Window == NULL)
        return 0;

    Received = 0;
    Display = RegisterPowerSettingNotification(Window, &ConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    Source = RegisterPowerSettingNotification(Window, &AcDcPowerSource, DEVICE_NOTIFY_WINDOW_HANDLE);

    Start = GetTickCount64();
    while (Received != GOT_BOTH && GetTickCount64() - Start < 1000)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(1000 - (GetTickCount64() - Start)), QS_ALLINPUT);
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
    }

    if (Source != NULL)
        UnregisterPowerSettingNotification(Source);
    if (Display != NULL)
        UnregisterPowerSettingNotification(Display);
    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    return Received;
}

/*** Service part of the test ***/

static void ReportServiceStatus(DWORD CurrentState)
{
    SERVICE_STATUS Status = { 0 };

    Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    Status.dwCurrentState = CurrentState;
    Status.dwControlsAccepted = (CurrentState == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
    service_ok(SetServiceStatus(StatusHandle, &Status), "SetServiceStatus(%lu) failed: %lu\n", CurrentState, GetLastError());
}

static VOID WINAPI ServiceHandler(DWORD Control)
{
    UNREFERENCED_PARAMETER(Control);
}

static void WINAPI ServiceMain(DWORD Argc, LPWSTR *Argv)
{
    ULONG Got;
    DWORD Session = MAXDWORD;

    UNREFERENCED_PARAMETER(Argc);

    StatusHandle = RegisterServiceCtrlHandlerW(Argv[0], ServiceHandler);
    service_ok(StatusHandle != NULL, "RegisterServiceCtrlHandlerW failed: %lu\n", GetLastError());
    if (StatusHandle == NULL)
        return;
    ReportServiceStatus(SERVICE_RUNNING);

    ProcessIdToSessionId(GetCurrentProcessId(), &Session);
    service_ok(Session == 0, "the service runs in session %lu\n", Session);

    Got = ReceiveInitialPowerSettings();
    service_ok(Got == GOT_BOTH, "a service window got power settings 0x%lx, expected 0x%x\n", Got, GOT_BOTH);

    ReportServiceStatus(SERVICE_STOPPED);
}

static BOOL StartService_(PCSTR ServiceNameA, PCWSTR ServiceNameW)
{
    SERVICE_TABLE_ENTRYW Table[] = { { (PWSTR)ServiceNameW, ServiceMain }, { NULL, NULL } };
    BOOL Result;

    UNREFERENCED_PARAMETER(ServiceNameA);
    Result = StartServiceCtrlDispatcherW(Table);
    service_ok(Result, "StartServiceCtrlDispatcherW failed: %lu\n", GetLastError());
    return Result;
}

/*** Tester part of the test ***/

static void TestServer(PCSTR ServiceNameA, PCWSTR ServiceNameW, void *Param)
{
    SERVICE_STATUS Status = { 0 };
    SC_HANDLE Manager, Service;
    ULONGLONG Start;
    BOOL Result = FALSE;

    UNREFERENCED_PARAMETER(ServiceNameA);
    UNREFERENCED_PARAMETER(Param);

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (Manager == NULL)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }

    Service = register_service_exW(Manager, L"ServicePowerSetting", ServiceNameW, NULL, SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                   NULL, NULL, NULL, NULL, NULL);
    if (Service == NULL)
    {
        skip("CreateServiceW failed with error %lu\n", GetLastError());
        CloseServiceHandle(Manager);
        return;
    }

    if (StartServiceW(Service, 0, NULL))
    {
        Start = GetTickCount64();
        do
        {
            Sleep(1);
            Result = QueryServiceStatus(Service, &Status);
        } while (Result && Status.dwCurrentState != SERVICE_STOPPED && GetTickCount64() - Start < 5000);
        ok(Result, "QueryServiceStatus failed: %lu\n", GetLastError());
        ok(Status.dwCurrentState == SERVICE_STOPPED, "service state %lu\n", Status.dwCurrentState);
    }
    else
    {
        skip("StartServiceW failed with error %lu\n", GetLastError());
    }

    ok(DeleteService(Service), "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
}

START_TEST(ServicePowerSetting)
{
    ULONG Got;
    char **Argv;
    int Argc;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc >= 3)
    {
        service_process(StartService_, Argc, Argv);
        return;
    }

    Got = ReceiveInitialPowerSettings();
    ok(Got == GOT_BOTH, "a desktop window got power settings 0x%lx, expected 0x%x\n", Got, GOT_BOTH);

    test_runner(TestServer, NULL);
}
