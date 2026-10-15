/*
 * PROJECT:     LiberNT Windows Start-Up Application
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Session 0 start-up: window station, LSA and service control manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <ndk/rtlfuncs.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(wininit);

static
HANDLE
StartSystemProcess(
    _In_ PCWSTR ImageName)
{
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInformation;

    if (!CreateProcessW(ImageName,
                        NULL,
                        NULL,
                        NULL,
                        FALSE,
                        DETACHED_PROCESS,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInformation))
    {
        ERR("WININIT: Failed to start %ls (error %lu)\n", ImageName, GetLastError());
        return NULL;
    }

    CloseHandle(ProcessInformation.hThread);
    return ProcessInformation.hProcess;
}

int
WINAPI
wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    HANDLE Processes[2];
    HWINSTA WindowStation;
    HDESK Desktop;

    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    if (!RegisterLogonProcess(GetCurrentProcessId(), TRUE))
        ERR("WININIT: Could not register as the session logon process\n");

    RtlSetProcessIsCritical(TRUE, NULL, FALSE);

    WindowStation = CreateWindowStationW(L"WinSta0", 0, MAXIMUM_ALLOWED, NULL);
    if (!WindowStation || !SetProcessWindowStation(WindowStation))
    {
        ERR("WININIT: Could not create WinSta0 (error %lu)\n", GetLastError());
    }
    else
    {
        if (!CreateDesktopW(L"Winlogon", NULL, NULL, 0, MAXIMUM_ALLOWED, NULL))
            ERR("WININIT: Could not create the Winlogon desktop (error %lu)\n", GetLastError());
        Desktop = CreateDesktopW(L"Default", NULL, NULL, 0, MAXIMUM_ALLOWED, NULL);
        if (!Desktop || !SetThreadDesktop(Desktop))
            ERR("WININIT: Could not use the Default desktop (error %lu)\n", GetLastError());
    }

    Processes[0] = StartSystemProcess(L"services.exe");
    Processes[1] = StartSystemProcess(L"lsass.exe");
    if (!Processes[0] || !Processes[1])
        return 1;

    WaitForMultipleObjects(ARRAYSIZE(Processes), Processes, TRUE, INFINITE);
    return 0;
}
