/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service status and events after a service process terminates unexpectedly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

#include "svchlp.h"

static SERVICE_STATUS_HANDLE crash_status_handle;

static VOID WINAPI crash_handler(DWORD ctrl)
{
    UNREFERENCED_PARAMETER(ctrl);
}

static void WINAPI crash_main(DWORD argc, LPWSTR *argv)
{
    SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, 0, NO_ERROR, 0, 0, 0 };

    UNREFERENCED_PARAMETER(argc);
    crash_status_handle = RegisterServiceCtrlHandlerW(argv[0], crash_handler);
    if (!crash_status_handle)
        return;
    SetServiceStatus(crash_status_handle, &status);
    TerminateProcess(GetCurrentProcess(), 0x2a);
}

static BOOL wait_stopped(SC_HANDLE service, SERVICE_STATUS_PROCESS *status)
{
    DWORD needed, i;

    for (i = 0; i < 100; ++i)
    {
        ZeroMemory(status, sizeof(*status));
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)status, sizeof(*status), &needed))
            return FALSE;
        if (status->dwCurrentState == SERVICE_STOPPED)
            return TRUE;
        Sleep(100);
    }
    return FALSE;
}

static void check_crash(SC_HANDLE service, UINT round)
{
    SERVICE_STATUS_PROCESS status;
    BOOL res;

    res = StartServiceW(service, 0, NULL);
    ok(res, "round %u: StartServiceW failed: %lu\n", round, GetLastError());
    if (!res)
        return;
    res = wait_stopped(service, &status);
    ok(res, "round %u: service still in state %lu\n", round, status.dwCurrentState);
    ok(status.dwWin32ExitCode == ERROR_PROCESS_ABORTED, "round %u: exit code %lu\n", round, status.dwWin32ExitCode);
    ok(status.dwServiceSpecificExitCode == 0, "round %u: specific exit code %lu\n", round, status.dwServiceSpecificExitCode);
    ok(status.dwProcessId == 0, "round %u: process id %lu\n", round, status.dwProcessId);
    ok(status.dwControlsAccepted == 0, "round %u: controls %#lx\n", round, status.dwControlsAccepted);
    ok(status.dwCheckPoint == 0 && status.dwWaitHint == 0, "round %u: checkpoint %lu wait hint %lu\n",
       round, status.dwCheckPoint, status.dwWaitHint);
}

/* The newest System log record of the Service Control Manager naming the service. */
static BOOL find_crash_event(PCWSTR service_name, DWORD *event_id, WCHAR *count, DWORD count_size)
{
    BYTE buffer[0x10000];
    HANDLE log = OpenEventLogW(NULL, L"System");
    DWORD read, needed;
    BOOL found = FALSE;

    if (!log)
        return FALSE;
    while (!found && ReadEventLogW(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, buffer, sizeof(buffer),
                                   &read, &needed))
    {
        EVENTLOGRECORD *record = (EVENTLOGRECORD *)buffer;

        while (!found && (BYTE *)record < buffer + read)
        {
            PCWSTR source = (PCWSTR)(record + 1);
            PCWSTR strings = (PCWSTR)((BYTE *)record + record->StringOffset);

            if (!wcscmp(source, L"Service Control Manager") && record->NumStrings >= 2 && !wcscmp(strings, service_name))
            {
                *event_id = record->EventID;
                StringCbCopyW(count, count_size, strings + wcslen(strings) + 1);
                found = TRUE;
            }
            record = (EVENTLOGRECORD *)((BYTE *)record + record->Length);
        }
    }
    CloseEventLog(log);
    return found;
}

static void test_crash(void)
{
    WCHAR service_name[64], count[16];
    SC_HANDLE scm, service;
    DWORD event_id = 0;
    UINT round;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }
    StringCbPrintfW(service_name, sizeof(service_name), L"LntCrashTest%lu", GetTickCount());
    service = register_service_exW(scm, L"ServiceCrash", service_name, NULL, SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                   NULL, NULL, NULL, NULL, NULL);
    if (!service)
    {
        CloseServiceHandle(scm);
        return;
    }
    for (round = 1; round <= 2; ++round)
    {
        check_crash(service, round);
        count[0] = 0;
        ok(find_crash_event(service_name, &event_id, count, sizeof(count)), "round %u: no crash event\n", round);
        ok((event_id & 0xffff) == 7034, "round %u: event %lu\n", round, event_id & 0xffff);
        ok(_wtoi(count) == (int)round, "round %u: failure count %ls\n", round, count);
    }
    ok(DeleteService(service), "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
}

START_TEST(ServiceCrash)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    if (argc >= 3)
    {
        WCHAR name[100];
        SERVICE_TABLE_ENTRYW table[] = { { name, crash_main }, { NULL, NULL } };

        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, name, ARRAYSIZE(name));
        StartServiceCtrlDispatcherW(table);
        return;
    }
    test_crash();
}
