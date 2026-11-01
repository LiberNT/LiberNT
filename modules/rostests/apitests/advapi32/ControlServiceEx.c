/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for ControlServiceExA/W and NotifyServiceStatusChangeA
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include "svchlp.h"

#define CTRL_ACCEPT_STOP 200

static SERVICE_STATUS_HANDLE status_handle;
static HANDLE stop_event;
static DWORD accepted_controls;

static void report_status(DWORD state)
{
    SERVICE_STATUS status;

    ZeroMemory(&status, sizeof(status));
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = (state == SERVICE_RUNNING) ? accepted_controls : 0;
    SetServiceStatus(status_handle, &status);
}

static DWORD WINAPI service_handler(DWORD ctrl, DWORD type, void *data, void *context)
{
    switch (ctrl)
    {
        case SERVICE_CONTROL_STOP:
            report_status(SERVICE_STOP_PENDING);
            SetEvent(stop_event);
            break;

        case CTRL_ACCEPT_STOP:
            accepted_controls = SERVICE_ACCEPT_STOP;
            report_status(SERVICE_RUNNING);
            break;

        default:
            break;
    }
    return NO_ERROR;
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    status_handle = RegisterServiceCtrlHandlerExW(argv[0], service_handler, NULL);
    if (!status_handle)
        return;

    accepted_controls = 0;
    report_status(SERVICE_RUNNING);
    WaitForSingleObject(stop_event, 30000);
    report_status(SERVICE_STOPPED);
}

static void run_service(PCSTR name)
{
    SERVICE_TABLE_ENTRYW servtbl[2];
    WCHAR nameW[100];

    MultiByteToWideChar(CP_ACP, 0, name, -1, nameW, ARRAYSIZE(nameW));
    servtbl[0].lpServiceName = nameW;
    servtbl[0].lpServiceProc = service_main;
    servtbl[1].lpServiceName = NULL;
    servtbl[1].lpServiceProc = NULL;
    StartServiceCtrlDispatcherW(servtbl);
}

static BOOL wait_state(SC_HANDLE service, DWORD state)
{
    SERVICE_STATUS_PROCESS status;
    DWORD needed, i;

    for (i = 0; i < 100; i++)
    {
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &needed))
            return FALSE;
        if (status.dwCurrentState == state)
            return TRUE;
        Sleep(100);
    }
    return FALSE;
}

static DWORD control_exW(SC_HANDLE service, DWORD control, DWORD level, DWORD reason, PWSTR comment,
                         SERVICE_STATUS_PROCESS *out)
{
    SERVICE_CONTROL_STATUS_REASON_PARAMSW params;
    BOOL ret;

    ZeroMemory(&params, sizeof(params));
    params.dwReason = reason;
    params.pszComment = comment;
    params.ServiceStatus.dwCurrentState = 0xdeadbeef;
    params.ServiceStatus.dwProcessId = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = ControlServiceExW(service, control, level, &params);
    if (out)
        *out = params.ServiceStatus;
    return ret ? ERROR_SUCCESS : GetLastError();
}

static const struct
{
    DWORD reason;
    DWORD error;
} reason_tests[] =
{
    { 0x00000000, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_UNPLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_FLAG_UNPLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_MIN | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_MIN, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_NONE | SERVICE_STOP_REASON_MINOR_MEMOTYLIMIT, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_MAX | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_MAX, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | 0x00080000 | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | 0x000000ff, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_MAX | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_PLANNED | 0x01000000 | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_PLANNED | 0x0F000000 | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_MAX_CUSTOM | SERVICE_STOP_REASON_MINOR_MAX_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_FLAG_UNPLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MAX_CUSTOM | SERVICE_STOP_REASON_MINOR_MAX_CUSTOM, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | 0x003f0000 | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | 0x000000ff, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | 0x01000000 | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, ERROR_INVALID_SERVICE_CONTROL },
    { SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
    { SERVICE_STOP_REASON_FLAG_UNPLANNED | SERVICE_STOP_REASON_FLAG_MAX | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER, ERROR_INVALID_PARAMETER },
};

static const struct
{
    DWORD length;
    DWORD error;
} comment_tests[] =
{
    { 0, ERROR_INVALID_SERVICE_CONTROL },
    { 126, ERROR_INVALID_SERVICE_CONTROL },
    { 127, ERROR_INVALID_PARAMETER },
    { 128, ERROR_INVALID_PARAMETER },
    { 199, ERROR_INVALID_PARAMETER },
};

static const DWORD valid_reason = SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_OTHER | SERVICE_STOP_REASON_MINOR_OTHER;

static void test_stopped_service(SC_HANDLE service)
{
    SERVICE_CONTROL_STATUS_REASON_PARAMSA paramsA;
    SERVICE_STATUS_PROCESS status;
    DWORD error;
    BOOL ret;

    error = control_exW(NULL, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, valid_reason, NULL, NULL);
    ok(error == ERROR_INVALID_HANDLE, "NULL handle: error %lu\n", error);

    error = control_exW(service, SERVICE_CONTROL_STOP, 0, valid_reason, NULL, NULL);
    ok(error == ERROR_INVALID_LEVEL, "level 0: error %lu\n", error);

    error = control_exW(service, SERVICE_CONTROL_STOP, 2, valid_reason, NULL, NULL);
    ok(error == ERROR_INVALID_LEVEL, "level 2: error %lu\n", error);

    error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, valid_reason, NULL, &status);
    ok(error == ERROR_SERVICE_NOT_ACTIVE, "stopped STOP: error %lu\n", error);
    ok(status.dwCurrentState == SERVICE_STOPPED, "stopped STOP: state %lu\n", status.dwCurrentState);
    ok(status.dwProcessId == 0, "stopped STOP: pid %lu\n", status.dwProcessId);

    error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, &status);
    ok(error == ERROR_INVALID_PARAMETER, "stopped STOP reason 0: error %lu\n", error);

    error = control_exW(service, SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, &status);
    ok(error == ERROR_SERVICE_NOT_ACTIVE, "stopped INTERROGATE: error %lu\n", error);

    error = control_exW(service, 0x7f, SERVICE_CONTROL_STATUS_REASON_INFO, valid_reason, NULL, NULL);
    ok(error == ERROR_INVALID_PARAMETER, "control 0x7f: error %lu\n", error);

    ZeroMemory(&paramsA, sizeof(paramsA));
    paramsA.dwReason = valid_reason;
    paramsA.pszComment = "ansi comment";
    paramsA.ServiceStatus.dwCurrentState = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = ControlServiceExA(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, &paramsA);
    ok(!ret, "ControlServiceExA succeeded\n");
    ok(GetLastError() == ERROR_SERVICE_NOT_ACTIVE, "ControlServiceExA: error %lu\n", GetLastError());
    ok(paramsA.ServiceStatus.dwCurrentState == SERVICE_STOPPED, "ControlServiceExA: state %lu\n", paramsA.ServiceStatus.dwCurrentState);

    SetLastError(0xdeadbeef);
    ret = ControlServiceExA(NULL, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, &paramsA);
    ok(!ret && GetLastError() == ERROR_INVALID_HANDLE, "ControlServiceExA NULL: %d %lu\n", ret, GetLastError());

    SetLastError(0xdeadbeef);
    ret = ControlServiceExA(service, SERVICE_CONTROL_STOP, 0, &paramsA);
    ok(!ret && GetLastError() == ERROR_INVALID_LEVEL, "ControlServiceExA level 0: %d %lu\n", ret, GetLastError());
}

static void test_running_service(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status;
    WCHAR comment[200];
    DWORD error, i;

    error = control_exW(service, SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, &status);
    ok(error == ERROR_SUCCESS, "INTERROGATE reason 0: error %lu\n", error);
    ok(status.dwCurrentState == SERVICE_RUNNING, "INTERROGATE: state %lu\n", status.dwCurrentState);
    ok(status.dwProcessId != 0 && status.dwProcessId != 0xdeadbeef, "INTERROGATE: pid %lu\n", status.dwProcessId);

    for (i = 0; i < ARRAYSIZE(reason_tests); i++)
    {
        error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, reason_tests[i].reason, NULL, &status);
        ok(error == reason_tests[i].error, "reason %08lx: error %lu, expected %lu\n",
           reason_tests[i].reason, error, reason_tests[i].error);
    }

    for (i = 0; i < ARRAYSIZE(comment_tests); i++)
    {
        DWORD j;

        for (j = 0; j < comment_tests[i].length; j++)
            comment[j] = L'a';
        comment[j] = 0;
        error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, valid_reason, comment, NULL);
        ok(error == comment_tests[i].error, "comment length %lu: error %lu, expected %lu\n",
           comment_tests[i].length, error, comment_tests[i].error);
    }

    for (i = 0; i < 199; i++)
        comment[i] = L'a';
    comment[199] = 0;
    error = control_exW(service, SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_STATUS_REASON_INFO, 0, comment, NULL);
    ok(error == ERROR_INVALID_PARAMETER, "INTERROGATE comment 199: error %lu\n", error);

    error = control_exW(service, CTRL_ACCEPT_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, &status);
    ok(error == ERROR_SUCCESS, "user control reason 0: error %lu\n", error);
    ok(status.dwControlsAccepted == SERVICE_ACCEPT_STOP, "user control: accepted %lx\n", status.dwControlsAccepted);
}

static const struct
{
    DWORD reason;
    PWSTR comment;
} logged_stops[] =
{
    { SERVICE_STOP_REASON_FLAG_UNPLANNED | SERVICE_STOP_REASON_MAJOR_HARDWARE | SERVICE_STOP_REASON_MINOR_DISK, NULL },
    { SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_APPLICATION | SERVICE_STOP_REASON_MINOR_RECONFIG, L"second comment" },
    { SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM | SERVICE_STOP_REASON_MINOR_MIN_CUSTOM, L"custom comment" },
};

static void test_logged_stops(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status;
    DWORD error, i;

    for (i = 0; i < ARRAYSIZE(logged_stops); i++)
    {
        ok(StartServiceW(service, 0, NULL), "StartServiceW(%lu) failed: %lu\n", i, GetLastError());
        ok(wait_state(service, SERVICE_RUNNING), "service did not start (%lu)\n", i);
        error = control_exW(service, CTRL_ACCEPT_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, NULL);
        ok(error == ERROR_SUCCESS, "user control (%lu): error %lu\n", i, error);
        error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, logged_stops[i].reason,
                            logged_stops[i].comment, &status);
        ok(error == ERROR_SUCCESS, "logged stop %08lx: error %lu\n", logged_stops[i].reason, error);
        ok(wait_state(service, SERVICE_STOPPED), "service did not stop (%lu)\n", i);
    }
}

static void test_event_log(PCWSTR service_nameW)
{
    BYTE *buffer;
    DWORD read, needed, count7042 = 0, count7035 = 0, i;
    HANDLE log;

    log = OpenEventLogW(NULL, L"System");
    ok(log != NULL, "OpenEventLogW failed: %lu\n", GetLastError());
    if (!log)
        return;

    buffer = HeapAlloc(GetProcessHeap(), 0, 0x10000);
    while (ReadEventLogW(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, buffer, 0x10000, &read, &needed))
    {
        EVENTLOGRECORD *record = (EVENTLOGRECORD *)buffer;

        while ((BYTE *)record < buffer + read)
        {
            PCWSTR source = (PCWSTR)(record + 1);
            PCWSTR str = (PCWSTR)((BYTE *)record + record->StringOffset);

            if (record->NumStrings && !wcscmp(str, service_nameW))
            {
                DWORD id = record->EventID & 0xffff;

                if (id == 7042)
                    count7042++;
                else if (id == 7035)
                    count7035++;
                trace("event %lu source %ls type %u strings %u\n", id, source, record->EventType, record->NumStrings);
                for (i = 0; i < record->NumStrings; i++)
                {
                    trace("  [%lu] '%ls'\n", i, str);
                    str += wcslen(str) + 1;
                }
            }
            record = (EVENTLOGRECORD *)((BYTE *)record + record->Length);
        }
    }
    HeapFree(GetProcessHeap(), 0, buffer);
    CloseEventLog(log);

    ok(count7042 == 0, "event 7042 count %lu\n", count7042);
    ok(count7035 == 0, "event 7035 count %lu\n", count7035);
}

static SERVICE_NOTIFYA *notified;

static void CALLBACK notify_cb(void *param)
{
    notified = param;
}

static void test_server(void)
{
    SERVICE_STATUS_PROCESS status;
    SERVICE_NOTIFYA notify;
    SC_HANDLE scm, service;
    WCHAR service_nameW[64];
    DWORD error, i;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        skip("OpenSCManagerW failed: %lu\n", GetLastError());
        return;
    }

    StringCbPrintfW(service_nameW, sizeof(service_nameW), L"LiberNTCtlEx%lu", GetTickCount());
    service = register_serviceW(scm, L"ControlServiceEx", service_nameW, NULL);
    if (!service)
    {
        CloseServiceHandle(scm);
        return;
    }

    test_stopped_service(service);

    ok(StartServiceW(service, 0, NULL), "StartServiceW failed: %lu\n", GetLastError());
    ok(wait_state(service, SERVICE_RUNNING), "service did not start\n");

    test_running_service(service);

    ZeroMemory(&notify, sizeof(notify));
    notify.dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
    notify.pfnNotifyCallback = notify_cb;
    notify.pContext = &notify;
    notified = NULL;
    error = NotifyServiceStatusChangeA(service, SERVICE_NOTIFY_STOPPED, &notify);
    ok(error == ERROR_SUCCESS, "NotifyServiceStatusChangeA: error %lu\n", error);

    error = control_exW(service, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, valid_reason, L"stop comment", &status);
    ok(error == ERROR_SUCCESS, "STOP: error %lu\n", error);
    ok(status.dwCurrentState == SERVICE_STOP_PENDING || status.dwCurrentState == SERVICE_STOPPED,
       "STOP: state %lu\n", status.dwCurrentState);
    ok(status.dwServiceType == SERVICE_WIN32_OWN_PROCESS, "STOP: type %lx\n", status.dwServiceType);
    ok(wait_state(service, SERVICE_STOPPED), "service did not stop\n");

    for (i = 0; i < 50 && !notified; i++)
        SleepEx(100, TRUE);
    ok(notified == &notify, "notify callback got %p\n", notified);
    ok(notify.dwNotificationStatus == ERROR_SUCCESS, "dwNotificationStatus %lu\n", notify.dwNotificationStatus);
    ok(notify.dwNotificationTriggered == SERVICE_NOTIFY_STOPPED, "dwNotificationTriggered %lx\n", notify.dwNotificationTriggered);
    ok(notify.ServiceStatus.dwCurrentState == SERVICE_STOPPED, "notify state %lu\n", notify.ServiceStatus.dwCurrentState);
    ok(notify.pszServiceNames == NULL, "pszServiceNames %p\n", notify.pszServiceNames);

    test_logged_stops(service);
    test_event_log(service_nameW);

    ok(DeleteService(service), "DeleteService failed: %lu\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
}

static void test_exports(void)
{
    HMODULE advapi32 = GetModuleHandleW(L"advapi32.dll");
    HMODULE sechost = LoadLibraryW(L"sechost.dll");
    FARPROC plain, wide, ansi;

    plain = GetProcAddress(advapi32, "NotifyServiceStatusChange");
    wide = GetProcAddress(advapi32, "NotifyServiceStatusChangeW");
    ansi = GetProcAddress(advapi32, "NotifyServiceStatusChangeA");
    ok(plain != NULL, "advapi32 NotifyServiceStatusChange missing\n");
    ok(wide != NULL && ansi != NULL, "advapi32 NotifyServiceStatusChangeA/W missing\n");
    ok(GetProcAddress(advapi32, "ControlServiceEx") == NULL, "advapi32 exports ControlServiceEx\n");
    ok(GetProcAddress(advapi32, "ControlServiceExA") != NULL, "advapi32 ControlServiceExA missing\n");
    ok(GetProcAddress(advapi32, "ControlServiceExW") != NULL, "advapi32 ControlServiceExW missing\n");

    ok(sechost != NULL, "sechost missing\n");
    if (!sechost)
        return;
    plain = GetProcAddress(sechost, "NotifyServiceStatusChange");
    wide = GetProcAddress(sechost, "NotifyServiceStatusChangeW");
    ansi = GetProcAddress(sechost, "NotifyServiceStatusChangeA");
    ok(plain != NULL && wide != NULL && ansi != NULL, "sechost notify exports %p %p %p\n", plain, wide, ansi);
    ok(plain == wide, "sechost NotifyServiceStatusChange %p, W %p, A %p\n", plain, wide, ansi);
    ok(GetProcAddress(sechost, "ControlServiceExA") != NULL, "sechost ControlServiceExA missing\n");
    ok(GetProcAddress(sechost, "ControlServiceExW") != NULL, "sechost ControlServiceExW missing\n");
}

START_TEST(ControlServiceEx)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3)
    {
        run_service(argv[2]);
        return;
    }

    test_exports();
    test_server();
}
