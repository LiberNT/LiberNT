/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service status, events and recovery actions after a service fails
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

#include "svchlp.h"

typedef struct _CRASH_EVENT
{
    DWORD Id;
    WCHAR Count[16];
    WCHAR Type[16];
} CRASH_EVENT;

static SERVICE_STATUS_HANDLE crash_status_handle;
static BOOL service_fails_cleanly;

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
    if (service_fails_cleanly)
    {
        status.dwCurrentState = SERVICE_STOPPED;
        status.dwWin32ExitCode = ERROR_INVALID_FUNCTION;
        SetServiceStatus(crash_status_handle, &status);
        return;
    }
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

/* The Service Control Manager records of the service in the System log, newest first. */
static UINT collect_events(PCWSTR service_name, CRASH_EVENT *events, UINT max)
{
    static BYTE buffer[0x10000];
    HANDLE log = OpenEventLogW(NULL, L"System");
    DWORD read, needed;
    UINT count = 0;

    if (!log)
        return 0;
    while (count < max && ReadEventLogW(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, buffer,
                                        sizeof(buffer), &read, &needed))
    {
        EVENTLOGRECORD *record = (EVENTLOGRECORD *)buffer;

        while (count < max && (BYTE *)record < buffer + read)
        {
            PCWSTR source = (PCWSTR)(record + 1);
            PCWSTR string = (PCWSTR)((BYTE *)record + record->StringOffset);
            DWORD id = record->EventID & 0xffff;

            if (!wcscmp(source, L"Service Control Manager") && (id == 7031 || id == 7034) &&
                record->NumStrings >= 2 && !wcscmp(string, service_name))
            {
                ZeroMemory(&events[count], sizeof(events[count]));
                events[count].Id = id;
                string += wcslen(string) + 1;
                StringCbCopyW(events[count].Count, sizeof(events[count].Count), string);
                if (record->NumStrings >= 4)
                {
                    string += wcslen(string) + 1;
                    string += wcslen(string) + 1;
                    StringCbCopyW(events[count].Type, sizeof(events[count].Type), string);
                }
                ++count;
            }
            record = (EVENTLOGRECORD *)((BYTE *)record + record->Length);
        }
    }
    CloseEventLog(log);
    return count;
}

static UINT wait_events(PCWSTR service_name, CRASH_EVENT *events, UINT wanted)
{
    UINT count = 0, i;

    for (i = 0; i < 75 && count < wanted; ++i)
    {
        count = collect_events(service_name, events, wanted);
        if (count < wanted)
            Sleep(200);
    }
    return count;
}

static SC_HANDLE create_test_service(SC_HANDLE scm, PCWSTR prefix, PCWSTR mode, WCHAR *name, SIZE_T name_size)
{
    StringCbPrintfW(name, name_size, L"%ls%lu", prefix, GetTickCount());
    return register_service_exW(scm, L"ServiceCrash", name, mode, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, NULL, NULL, NULL, NULL, NULL);
}

static void delete_test_service(SC_HANDLE service)
{
    ok(DeleteService(service), "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(service);
}

static BOOL set_actions(SC_HANDLE service, SC_ACTION *actions, DWORD count, PWSTR command)
{
    SERVICE_FAILURE_ACTIONSW failure = { INFINITE, NULL, command, count, actions };
    BOOL res = ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    ok(res, "ChangeServiceConfig2W(failure actions) failed: %lu\n", GetLastError());
    return res;
}

/* Waits for the recovery program to write "fail=<count>" into the file. */
static BOOL read_marker(PCWSTR path, DWORD timeout, char *text, DWORD size)
{
    DWORD waited, read = 0;

    text[0] = 0;
    for (waited = 0; waited <= timeout; waited += 100)
    {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                  OPEN_EXISTING, 0, NULL);

        if (file != INVALID_HANDLE_VALUE)
        {
            BOOL res = ReadFile(file, text, size - 1, &read, NULL);

            CloseHandle(file);
            if (res && read)
            {
                text[read] = 0;
                return TRUE;
            }
        }
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

static void test_crash(SC_HANDLE scm)
{
    WCHAR name[64];
    CRASH_EVENT events[2];
    SC_HANDLE service = create_test_service(scm, L"LntCrashTest", NULL, name, sizeof(name));
    UINT round;

    if (!service)
        return;
    for (round = 1; round <= 2; ++round)
    {
        check_crash(service, round);
        ZeroMemory(events, sizeof(events));
        ok(wait_events(name, events, round) == round, "round %u: no crash event\n", round);
        ok(events[0].Id == 7034, "round %u: event %lu\n", round, events[0].Id);
        ok(_wtoi(events[0].Count) == (int)round, "round %u: failure count %ls\n", round, events[0].Count);
    }
    delete_test_service(service);
}

static void test_restart(SC_HANDLE scm)
{
    SC_ACTION actions[2] = { { SC_ACTION_RESTART, 0 }, { SC_ACTION_NONE, 0 } };
    SERVICE_STATUS_PROCESS status;
    CRASH_EVENT events[2];
    WCHAR name[64];
    SC_HANDLE service = create_test_service(scm, L"LntRestartTest", NULL, name, sizeof(name));

    if (!service)
        return;
    if (set_actions(service, actions, ARRAYSIZE(actions), NULL))
    {
        ok(StartServiceW(service, 0, NULL), "StartServiceW failed: %lu\n", GetLastError());
        ok(wait_events(name, events, 2) == 2, "the restarted service did not fail again\n");
        ok(events[1].Id == 7031 && _wtoi(events[1].Count) == 1 && _wtoi(events[1].Type) == SC_ACTION_RESTART,
           "first failure: event %lu count %ls action %ls\n", events[1].Id, events[1].Count, events[1].Type);
        ok(events[0].Id == 7034 && _wtoi(events[0].Count) == 2 && !events[0].Type[0],
           "second failure: event %lu count %ls action %ls\n", events[0].Id, events[0].Count, events[0].Type);
        ok(wait_stopped(service, &status) && status.dwWin32ExitCode == ERROR_PROCESS_ABORTED,
           "final state %lu exit code %lu\n", status.dwCurrentState, status.dwWin32ExitCode);
    }
    delete_test_service(service);
}

static void make_command(WCHAR *path, SIZE_T path_size, WCHAR *command, SIZE_T command_size, PCWSTR name)
{
    WCHAR windows[MAX_PATH];

    GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    StringCbPrintfW(path, path_size, L"%ls\\Temp\\%ls.txt", windows, name);
    StringCbPrintfW(command, command_size, L"%ls\\system32\\cmd.exe /c (echo fail=%%1%%)>\"%ls\"", windows, path);
    DeleteFileW(path);
}

static void test_run_command(SC_HANDLE scm)
{
    SC_ACTION action = { SC_ACTION_RUN_COMMAND, 0 };
    WCHAR name[64], path[MAX_PATH], command[2 * MAX_PATH];
    SERVICE_STATUS_PROCESS status;
    char text[64];
    SC_HANDLE service = create_test_service(scm, L"LntRunTest", NULL, name, sizeof(name));

    if (!service)
        return;
    make_command(path, sizeof(path), command, sizeof(command), name);
    if (set_actions(service, &action, 1, command))
    {
        ok(StartServiceW(service, 0, NULL), "StartServiceW failed: %lu\n", GetLastError());
        ok(wait_stopped(service, &status), "service still in state %lu\n", status.dwCurrentState);
        ok(read_marker(path, 10000, text, sizeof(text)) && !strncmp(text, "fail=1", 6),
           "recovery program output '%s'\n", text);
    }
    DeleteFileW(path);
    delete_test_service(service);
}

static void test_non_crash_flag(SC_HANDLE scm)
{
    SC_ACTION action = { SC_ACTION_RUN_COMMAND, 0 };
    WCHAR name[64], path[MAX_PATH], command[2 * MAX_PATH];
    SERVICE_FAILURE_ACTIONS_FLAG flag;
    SERVICE_STATUS_PROCESS status;
    DWORD needed = 0;
    char text[64];
    BOOL res;
    SC_HANDLE service = create_test_service(scm, L"LntFlagTest", L"fail", name, sizeof(name));

    if (!service)
        return;
    make_command(path, sizeof(path), command, sizeof(command), name);

    flag.fFailureActionsOnNonCrashFailures = 0x55;
    res = QueryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, (BYTE *)&flag, sizeof(flag), &needed);
    ok(res && needed == sizeof(flag) && flag.fFailureActionsOnNonCrashFailures == FALSE,
       "default flag: %d needed %lu value %d\n", res, needed, flag.fFailureActionsOnNonCrashFailures);
    needed = 0;
    SetLastError(0xdeadbeef);
    res = QueryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, (BYTE *)&flag, 0, &needed);
    ok(!res && GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed == sizeof(flag),
       "short buffer: %d error %lu needed %lu\n", res, GetLastError(), needed);

    if (set_actions(service, &action, 1, command))
    {
        ok(StartServiceW(service, 0, NULL), "StartServiceW failed: %lu\n", GetLastError());
        ok(wait_stopped(service, &status) && status.dwWin32ExitCode == ERROR_INVALID_FUNCTION,
           "without the flag: state %lu exit code %lu\n", status.dwCurrentState, status.dwWin32ExitCode);
        ok(!read_marker(path, 2000, text, sizeof(text)), "recovery ran without the flag: '%s'\n", text);

        flag.fFailureActionsOnNonCrashFailures = TRUE;
        ok(ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag),
           "ChangeServiceConfig2W(flag) failed: %lu\n", GetLastError());
        flag.fFailureActionsOnNonCrashFailures = 0x55;
        res = QueryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, (BYTE *)&flag, sizeof(flag), &needed);
        ok(res && flag.fFailureActionsOnNonCrashFailures == TRUE, "flag after set: %d value %d\n",
           res, flag.fFailureActionsOnNonCrashFailures);

        ok(StartServiceW(service, 0, NULL), "StartServiceW failed: %lu\n", GetLastError());
        ok(wait_stopped(service, &status) && status.dwWin32ExitCode == ERROR_INVALID_FUNCTION,
           "with the flag: state %lu exit code %lu\n", status.dwCurrentState, status.dwWin32ExitCode);
        ok(read_marker(path, 10000, text, sizeof(text)) && !strncmp(text, "fail=1", 6),
           "recovery program output with the flag '%s'\n", text);
    }
    DeleteFileW(path);
    delete_test_service(service);
}

START_TEST(ServiceCrash)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    SC_HANDLE scm;

    if (argc >= 3)
    {
        WCHAR name[100];
        SERVICE_TABLE_ENTRYW table[] = { { name, crash_main }, { NULL, NULL } };

        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, name, ARRAYSIZE(name));
        service_fails_cleanly = argc >= 4 && !strcmp(argv[3], "fail");
        StartServiceCtrlDispatcherW(table);
        return;
    }

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }
    test_crash(scm);
    test_restart(scm);
    test_run_command(scm);
    test_non_crash_flag(scm);
    CloseServiceHandle(scm);
}
