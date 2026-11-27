/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the display a service in session 0 draws to
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

#include <wingdi.h>
#include <winuser.h>

#include "svchlp.h"

/*** Service part of the test ***/

static SERVICE_STATUS_HANDLE status_handle;

static void
report_service_status(DWORD dwCurrentState)
{
    SERVICE_STATUS status;
    BOOL res;

    ZeroMemory(&status, sizeof(status));
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = dwCurrentState;
    status.dwWin32ExitCode = NO_ERROR;
    status.dwControlsAccepted = (dwCurrentState == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;

    res = SetServiceStatus(status_handle, &status);
    service_ok(res, "SetServiceStatus(%lu) failed: %lu\n", dwCurrentState, GetLastError());
}

static VOID WINAPI
service_handler(DWORD ctrl)
{
    if (ctrl == SERVICE_CONTROL_STOP)
        report_service_status(SERVICE_STOPPED);
}

static void
check_screen_dc(void)
{
    DISPLAY_DEVICEW Device;
    DWORD SessionId = MAXDWORD;
    HDC hdc;
    int Width, Height, Bits;

    ProcessIdToSessionId(GetCurrentProcessId(), &SessionId);
    service_ok(SessionId == 0, "The service runs in session %lu\n", SessionId);

    hdc = GetDC(NULL);
    service_ok(hdc != NULL, "GetDC(NULL) failed: %lu\n", GetLastError());
    if (hdc == NULL)
        return;

    Width = GetDeviceCaps(hdc, HORZRES);
    Height = GetDeviceCaps(hdc, VERTRES);
    Bits = GetDeviceCaps(hdc, BITSPIXEL);
    service_trace("Session 0 screen: %d x %d, %d bpp\n", Width, Height, Bits);
    service_ok(Width == 1024 && Height == 768, "Session 0 screen is %d x %d\n", Width, Height);
    service_ok(Bits == 32, "Session 0 screen has %d bits per pixel\n", Bits);
    service_ok(GetSystemMetrics(SM_CXSCREEN) == Width && GetSystemMetrics(SM_CYSCREEN) == Height,
               "SM_CXSCREEN x SM_CYSCREEN is %d x %d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    service_ok(PatBlt(hdc, 0, 0, 16, 16, BLACKNESS), "PatBlt failed: %lu\n", GetLastError());
    service_ok(ReleaseDC(NULL, hdc) == 1, "ReleaseDC failed\n");

    ZeroMemory(&Device, sizeof(Device));
    Device.cb = sizeof(Device);
    if (EnumDisplayDevicesW(NULL, 0, &Device, 0))
        service_trace("Session 0 display 0: %S, %S, flags 0x%lx\n", Device.DeviceName, Device.DeviceString, Device.StateFlags);
}

static void WINAPI
service_main(DWORD dwArgc, LPWSTR *lpszArgv)
{
    UNREFERENCED_PARAMETER(dwArgc);

    status_handle = RegisterServiceCtrlHandlerW(lpszArgv[0], service_handler);
    service_ok(status_handle != NULL, "RegisterServiceCtrlHandler failed: %lu\n", GetLastError());
    if (!status_handle)
        return;

    report_service_status(SERVICE_RUNNING);
    check_screen_dc();
    report_service_status(SERVICE_STOPPED);
}

static BOOL
start_service(PCSTR service_nameA, PCWSTR service_nameW)
{
    SERVICE_TABLE_ENTRYW servtbl[] =
    {
        { (PWSTR)service_nameW, service_main },
        { NULL, NULL }
    };
    BOOL res;

    UNREFERENCED_PARAMETER(service_nameA);

    res = StartServiceCtrlDispatcherW(servtbl);
    service_ok(res, "StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
    return res;
}

/*** Tester part of the test ***/

static void
my_test_server(PCSTR service_nameA, PCWSTR service_nameW, void *param)
{
    DEVMODEW Before, After;
    SC_HANDLE hSC, hService;
    SERVICE_STATUS Status;
    ULONG Waited;
    BOOL res;

    UNREFERENCED_PARAMETER(service_nameA);
    UNREFERENCED_PARAMETER(param);

    ZeroMemory(&Before, sizeof(Before));
    Before.dmSize = sizeof(Before);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Before), "EnumDisplaySettingsW failed: %lu\n", GetLastError());

    hSC = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSC)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }

    hService = register_serviceW(hSC, L"ServiceDisplay", service_nameW, NULL);
    if (!hService)
    {
        skip("CreateServiceW failed with error %lu\n", GetLastError());
        CloseServiceHandle(hSC);
        return;
    }

    if (StartServiceW(hService, 0, NULL))
    {
        for (Waited = 0; Waited < 5000; Waited += 20)
        {
            ZeroMemory(&Status, sizeof(Status));
            res = QueryServiceStatus(hService, &Status);
            if (!res || Status.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(20);
        }
        ok(Status.dwCurrentState == SERVICE_STOPPED, "The service did not stop: state %lu\n", Status.dwCurrentState);
    }
    else
    {
        skip("StartServiceW failed with error %lu\n", GetLastError());
    }

    ZeroMemory(&After, sizeof(After));
    After.dmSize = sizeof(After);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &After), "EnumDisplaySettingsW failed: %lu\n", GetLastError());
    ok(After.dmPelsWidth == Before.dmPelsWidth && After.dmPelsHeight == Before.dmPelsHeight &&
       After.dmBitsPerPel == Before.dmBitsPerPel,
       "The console mode changed from %lux%lux%lu to %lux%lux%lu\n",
       Before.dmPelsWidth, Before.dmPelsHeight, Before.dmBitsPerPel,
       After.dmPelsWidth, After.dmPelsHeight, After.dmBitsPerPel);

    ok(DeleteService(hService), "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(hService);
    CloseServiceHandle(hSC);
}

START_TEST(ServiceDisplay)
{
    int argc;
    char **argv;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3)
    {
        service_process(start_service, argc, argv);
        return;
    }

    test_runner(my_test_server, NULL);
}
