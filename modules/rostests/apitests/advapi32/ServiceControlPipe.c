/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that only the process the service manager started connects as the service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static WCHAR ServiceName[] = L"LntControlPipeTest";
static SERVICE_STATUS_HANDLE StatusHandle;

typedef struct _CALL_RESULT
{
    SC_HANDLE Service;
    BOOL Result;
    DWORD Error;
} CALL_RESULT, *PCALL_RESULT;

static
DWORD
WINAPI
Handler(
    DWORD Control,
    DWORD EventType,
    LPVOID EventData,
    LPVOID Context)
{
    UNREFERENCED_PARAMETER(Control);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventData);
    UNREFERENCED_PARAMETER(Context);
    return NO_ERROR;
}

static
VOID
WINAPI
ServiceMainW(
    DWORD Argc,
    LPWSTR *Argv)
{
    SERVICE_STATUS Status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_STOPPED, 0, NO_ERROR, 0, 0, 0 };

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    StatusHandle = RegisterServiceCtrlHandlerExW(ServiceName, Handler, NULL);
    if (StatusHandle)
        SetServiceStatus(StatusHandle, &Status);
}

static
DWORD
WINAPI
StartThread(
    PVOID Parameter)
{
    PCALL_RESULT Call = Parameter;

    Call->Result = StartServiceW(Call->Service, 0, NULL);
    Call->Error = GetLastError();
    return 0;
}

static
DWORD
WINAPI
ImpostorThread(
    PVOID Parameter)
{
    SERVICE_TABLE_ENTRYW Table[] = { { ServiceName, ServiceMainW }, { NULL, NULL } };
    PCALL_RESULT Call = Parameter;

    SetLastError(0xdeadbeef);
    Call->Result = StartServiceCtrlDispatcherW(Table);
    Call->Error = GetLastError();
    return 0;
}

START_TEST(ServiceControlPipe)
{
    SERVICE_TABLE_ENTRYW Table[] = { { ServiceName, ServiceMainW }, { NULL, NULL } };
    WCHAR Command[MAX_PATH + 64];
    CALL_RESULT Start = { 0 }, Impostor = { 0 };
    SERVICE_STATUS Status;
    HANDLE Threads[2] = { NULL, NULL };
    SC_HANDLE Manager, Service;
    char **Argv;
    int Argc;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc >= 3 && !strcmp(Argv[2], "service"))
    {
        Sleep(2000);
        StartServiceCtrlDispatcherW(Table);
        return;
    }

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!Manager)
    {
        skip("Cannot open the service manager: %lu\n", GetLastError());
        return;
    }

    Service = OpenServiceW(Manager, ServiceName, SERVICE_ALL_ACCESS);
    if (Service)
    {
        DeleteService(Service);
        CloseServiceHandle(Service);
    }

    Command[0] = L'"';
    GetModuleFileNameW(NULL, Command + 1, MAX_PATH);
    StringCchCatW(Command, _countof(Command), L"\" ServiceControlPipe service");
    Service = CreateServiceW(Manager, ServiceName, ServiceName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, Command, NULL, NULL, NULL, NULL, NULL);
    if (!Service)
    {
        skip("Cannot create the test service: %lu\n", GetLastError());
        CloseServiceHandle(Manager);
        return;
    }

    Start.Service = Service;
    Threads[0] = CreateThread(NULL, 0, StartThread, &Start, 0, NULL);
    ok(Threads[0] != NULL, "CreateThread failed: %lu\n", GetLastError());
    Sleep(500);
    Threads[1] = CreateThread(NULL, 0, ImpostorThread, &Impostor, 0, NULL);
    ok(Threads[1] != NULL, "CreateThread failed: %lu\n", GetLastError());

    if (Threads[1])
    {
        ok(WaitForSingleObject(Threads[1], 10000) == WAIT_OBJECT_0,
           "StartServiceCtrlDispatcherW from a process the service manager did not start did not return\n");
        ok(!Impostor.Result, "StartServiceCtrlDispatcherW succeeded in a process the service manager did not start\n");
        ok(Impostor.Error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT, "Error %lu\n", Impostor.Error);
    }
    if (Threads[0])
    {
        ok(WaitForSingleObject(Threads[0], 40000) == WAIT_OBJECT_0, "StartServiceW did not return\n");
        ok(Start.Result, "StartServiceW failed: %lu\n", Start.Error);
    }

    for (Argc = 0; Argc < 50; Argc++)
    {
        if (!QueryServiceStatus(Service, &Status) || Status.dwCurrentState == SERVICE_STOPPED)
            break;
        Sleep(100);
    }

    DeleteService(Service);
    CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    if (Threads[0])
        CloseHandle(Threads[0]);
    if (Threads[1])
        CloseHandle(Threads[1]);
}
