/*
 * PROJECT:     LiberNT Windows Start-Up Application
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Session 0 start-up: window station, LSA and service control manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <wincon.h>
#include <sddl.h>
#include <ndk/rtlfuncs.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(wininit);

static HANDLE ShutdownRequest;
static HANDLE ShutdownComplete;
static HANDLE ServicesProcess;

static
DWORD
GetWaitToKillServiceTimeout(VOID)
{
    WCHAR Value[16];
    DWORD Size = sizeof(Value);

    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SYSTEM\\CurrentControlSet\\Control",
                     L"WaitToKillServiceTimeout",
                     RRF_RT_REG_SZ,
                     NULL,
                     Value,
                     &Size) != ERROR_SUCCESS)
    {
        return 20000;
    }
    return wcstoul(Value, NULL, 10);
}

static
DWORD
WINAPI
ShutdownThread(
    _In_ PVOID Parameter)
{
    LPTHREAD_START_ROUTINE CtrlRoutine;
    HANDLE Thread;

    UNREFERENCED_PARAMETER(Parameter);

    for (;;)
    {
        WaitForSingleObject(ShutdownRequest, INFINITE);

        CtrlRoutine = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CtrlRoutine");
        Thread = CtrlRoutine ? CreateRemoteThread(ServicesProcess,
                                                  NULL,
                                                  0,
                                                  CtrlRoutine,
                                                  (PVOID)(ULONG_PTR)CTRL_SHUTDOWN_EVENT,
                                                  0,
                                                  NULL)
                             : NULL;
        if (Thread)
        {
            WaitForSingleObject(Thread, GetWaitToKillServiceTimeout());
            CloseHandle(Thread);
        }
        else
        {
            ERR("WININIT: Could not notify the service control manager (error %lu)\n", GetLastError());
        }

        SetEvent(ShutdownComplete);
    }
    return 0;
}

static
BOOL
CreateShutdownEvents(VOID)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, FALSE };
    HANDLE Thread;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)",
                                                              SDDL_REVISION_1,
                                                              &Attributes.lpSecurityDescriptor,
                                                              NULL))
    {
        return FALSE;
    }

    ShutdownRequest = CreateEventW(&Attributes, FALSE, FALSE, L"WininitShutdownRequest");
    ShutdownComplete = CreateEventW(&Attributes, FALSE, FALSE, L"WininitShutdownComplete");
    LocalFree(Attributes.lpSecurityDescriptor);
    if (!ShutdownRequest || !ShutdownComplete)
        return FALSE;

    Thread = CreateThread(NULL, 0, ShutdownThread, NULL, 0, NULL);
    if (!Thread)
        return FALSE;
    CloseHandle(Thread);
    return TRUE;
}

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

    ServicesProcess = Processes[0];
    if (!CreateShutdownEvents())
        ERR("WININIT: Could not set up the shutdown notification (error %lu)\n", GetLastError());

    WaitForMultipleObjects(ARRAYSIZE(Processes), Processes, TRUE, INFINITE);
    return 0;
}
