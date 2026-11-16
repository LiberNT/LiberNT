/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetServiceRegistryStateKey from inside a service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

#include "svchlp.h"
#include <ndk/cmfuncs.h>

#define PARAMETERS_VALUE 0x1234

typedef DWORD (WINAPI *PFN_GET_SERVICE_REGISTRY_STATE_KEY)(SERVICE_STATUS_HANDLE, SERVICE_REGISTRY_STATE_TYPE, DWORD, HKEY *);

static SERVICE_STATUS_HANDLE status_handle;

static void
report_service_status(DWORD dwCurrentState)
{
    SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, dwCurrentState, 0, NO_ERROR, 0, 0, 0 };
    BOOL res;

    res = SetServiceStatus(status_handle, &status);
    service_ok(res, "SetServiceStatus(%lu) failed: %lu\n", dwCurrentState, GetLastError());
}

static VOID WINAPI
service_handler(DWORD ctrl)
{
    UNREFERENCED_PARAMETER(ctrl);
}

static void
check_key_name(HKEY Key, PCWSTR ServiceName, PCWSTR Subkey)
{
    BYTE Buffer[1024];
    PKEY_NAME_INFORMATION Info = (PVOID)Buffer;
    WCHAR Expected[MAX_PATH];
    ULONG Returned;
    NTSTATUS Status;
    SIZE_T NameChars, ExpectedChars;

    ZeroMemory(Buffer, sizeof(Buffer));
    Status = NtQueryKey(Key, KeyNameInformation, Info, sizeof(Buffer) - sizeof(WCHAR), &Returned);
    service_ok(NT_SUCCESS(Status), "NtQueryKey failed: 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;
    swprintf(Expected, _countof(Expected), L"\\Services\\%ls\\%ls", ServiceName, Subkey);
    NameChars = Info->NameLength / sizeof(WCHAR);
    ExpectedChars = wcslen(Expected);
    service_ok(NameChars > ExpectedChars && _wcsnicmp(Info->Name + NameChars - ExpectedChars, Expected, ExpectedChars) == 0,
               "Key name %.*S does not end with %S\n", (int)NameChars, Info->Name, Expected);
}

static void WINAPI
service_main(DWORD dwArgc, LPWSTR *lpszArgv)
{
    PFN_GET_SERVICE_REGISTRY_STATE_KEY pGetKey;
    HKEY Key = NULL;
    DWORD Error, Value, Size, Type;

    UNREFERENCED_PARAMETER(dwArgc);

    status_handle = RegisterServiceCtrlHandlerW(lpszArgv[0], service_handler);
    service_ok(status_handle != NULL, "RegisterServiceCtrlHandler failed: %lu\n", GetLastError());
    if (!status_handle)
        return;
    report_service_status(SERVICE_RUNNING);

    pGetKey = (PFN_GET_SERVICE_REGISTRY_STATE_KEY)GetProcAddress(LoadLibraryW(L"sechost.dll"), "GetServiceRegistryStateKey");
    service_ok(pGetKey != NULL, "sechost does not export GetServiceRegistryStateKey\n");
    if (pGetKey == NULL)
    {
        report_service_status(SERVICE_STOPPED);
        return;
    }

    /* The parameters key is the one the installer populated */
    Error = pGetKey(status_handle, ServiceRegistryStateParameters, KEY_READ, &Key);
    service_ok(Error == ERROR_SUCCESS, "Parameters: error %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Value = 0;
        Size = sizeof(Value);
        Error = RegQueryValueExW(Key, L"Probe", NULL, &Type, (PBYTE)&Value, &Size);
        service_ok(Error == ERROR_SUCCESS && Type == REG_DWORD && Value == PARAMETERS_VALUE,
                   "Parameters value: error %lu type %lu value 0x%lx\n", Error, Type, Value);
        check_key_name(Key, lpszArgv[0], L"Parameters");
        RegCloseKey(Key);
    }

    /* The persistent key is created on demand and writable by the service */
    Key = NULL;
    Error = pGetKey(status_handle, ServiceRegistryStatePersistent, KEY_READ | KEY_WRITE, &Key);
    service_ok(Error == ERROR_SUCCESS, "Persistent: error %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Value = 7;
        Error = RegSetValueExW(Key, L"Counter", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
        service_ok(Error == ERROR_SUCCESS, "Persistent write: error %lu\n", Error);
        Value = 0;
        Size = sizeof(Value);
        Error = RegQueryValueExW(Key, L"Counter", NULL, &Type, (PBYTE)&Value, &Size);
        service_ok(Error == ERROR_SUCCESS && Value == 7, "Persistent read back: error %lu value %lu\n", Error, Value);
        check_key_name(Key, lpszArgv[0], L"State");
        RegCloseKey(Key);
    }

    Key = (HKEY)(ULONG_PTR)0xdead;
    Error = pGetKey(status_handle, MaxServiceRegistryStateType, KEY_READ, &Key);
    service_ok(Error == ERROR_INVALID_PARAMETER, "Invalid state type: error %lu\n", Error);

    report_service_status(SERVICE_STOPPED);
}

static BOOL
start_service(PCSTR service_nameA, PCWSTR service_nameW)
{
    SERVICE_TABLE_ENTRYW servtbl[] = { { (PWSTR)service_nameW, service_main }, { NULL, NULL } };
    BOOL res;

    UNREFERENCED_PARAMETER(service_nameA);
    res = StartServiceCtrlDispatcherW(servtbl);
    service_ok(res, "StartServiceCtrlDispatcherW failed: %lu\n", GetLastError());
    return res;
}

static void
my_test_server(PCSTR service_nameA, PCWSTR service_nameW, void *param)
{
    SC_HANDLE hSC, hService;
    SERVICE_STATUS ServiceStatus;
    WCHAR KeyPath[MAX_PATH];
    HKEY Key;
    DWORD Value = PARAMETERS_VALUE;
    LONG Error;
    BOOL res;
    int Tries;

    UNREFERENCED_PARAMETER(service_nameA);
    UNREFERENCED_PARAMETER(param);

    hSC = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSC)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }
    hService = register_service_exW(hSC, L"ServiceRegistryState", service_nameW, NULL, SERVICE_ALL_ACCESS,
                                    SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                    NULL, NULL, NULL, NULL, NULL);
    if (!hService)
    {
        skip("CreateServiceW failed with error %lu\n", GetLastError());
        CloseServiceHandle(hSC);
        return;
    }

    swprintf(KeyPath, _countof(KeyPath), L"SYSTEM\\CurrentControlSet\\Services\\%ls\\Parameters", service_nameW);
    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, KeyPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    ok(Error == ERROR_SUCCESS, "Creating the Parameters key failed: %ld\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        RegSetValueExW(Key, L"Probe", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
        RegCloseKey(Key);
    }

    if (StartServiceW(hService, 0, NULL))
    {
        for (Tries = 0; Tries < 100; Tries++)
        {
            ZeroMemory(&ServiceStatus, sizeof(ServiceStatus));
            res = QueryServiceStatus(hService, &ServiceStatus);
            if (!res || ServiceStatus.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(50);
        }
        ok(ServiceStatus.dwCurrentState == SERVICE_STOPPED, "Service state %lu\n", ServiceStatus.dwCurrentState);
    }
    else
    {
        skip("StartServiceW failed with error %lu\n", GetLastError());
    }

    res = DeleteService(hService);
    ok(res, "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(hService);
    CloseServiceHandle(hSC);
}

START_TEST(ServiceRegistryState)
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
