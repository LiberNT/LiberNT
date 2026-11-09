/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for service manager change notifications and subscriptions
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include "precomp.h"
#include <tlhelp32.h>

#define TEST_SERVICE L"LntChangeNotifyTest"

typedef VOID (CALLBACK *PFN_SC_CALLBACK)(DWORD, PVOID);
typedef DWORD (WINAPI *PFN_SUBSCRIBE)(SC_HANDLE, SC_EVENT_TYPE, PFN_SC_CALLBACK, PVOID, PSC_NOTIFICATION_REGISTRATION *);
typedef VOID (WINAPI *PFN_UNSUBSCRIBE)(PSC_NOTIFICATION_REGISTRATION);

static PFN_SUBSCRIBE pSubscribe;
static PFN_UNSUBSCRIBE pUnsubscribe;
static volatile LONG Callbacks;
static HANDLE NotifyEvent;

static VOID CALLBACK
DatabaseChanged(DWORD Notify, PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    trace("Subscription callback 0x%lx\n", Notify);
    InterlockedIncrement(&Callbacks);
    SetEvent(NotifyEvent);
}

static SC_HANDLE
CreateTestService(SC_HANDLE Manager)
{
    WCHAR Path[MAX_PATH];

    GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\svchost.exe");
    return CreateServiceW(Manager, TEST_SERVICE, TEST_SERVICE, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                          SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, Path, NULL, NULL, NULL, NULL, NULL);
}

static BOOL
WaitCallback(void)
{
    DWORD Start = GetTickCount();

    while (!Callbacks && GetTickCount() - Start < 10000)
        WaitForSingleObject(NotifyEvent, 500);
    return Callbacks != 0;
}

static void
test_Subscribe(SC_HANDLE Manager)
{
    PSC_NOTIFICATION_REGISTRATION Subscription = NULL;
    SC_HANDLE Service, Minimal;
    DWORD Error, i;

    Error = pSubscribe(Manager, SC_EVENT_DATABASE_CHANGE, DatabaseChanged, NULL, &Subscription);
    ok(Error == ERROR_SUCCESS, "SubscribeServiceChangeNotifications returned %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    Callbacks = 0;
    Service = CreateTestService(Manager);
    ok(Service != NULL, "CreateServiceW failed (%lu)\n", GetLastError());
    if (Service)
    {
        ok(WaitCallback(), "No callback for the created service\n");

        Callbacks = 0;
        ok(DeleteService(Service), "DeleteService failed (%lu)\n", GetLastError());
        CloseServiceHandle(Service);
        ok(WaitCallback(), "No callback for the deleted service\n");
    }
    pUnsubscribe(Subscription);

    for (i = 0; i < 5; i++)
    {
        Error = pSubscribe(Manager, SC_EVENT_DATABASE_CHANGE, DatabaseChanged, NULL, &Subscription);
        ok(Error == ERROR_SUCCESS, "Subscription %lu returned %lu\n", i, Error);
        if (Error == ERROR_SUCCESS)
            pUnsubscribe(Subscription);
    }

    Minimal = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    ok(Minimal != NULL, "OpenSCManagerW failed (%lu)\n", GetLastError());
    if (Minimal)
    {
        Error = pSubscribe(Minimal, SC_EVENT_DATABASE_CHANGE, DatabaseChanged, NULL, &Subscription);
        ok(Error == ERROR_SUCCESS, "Subscription with SC_MANAGER_ENUMERATE_SERVICE returned %lu\n", Error);
        if (Error == ERROR_SUCCESS)
        {
            Callbacks = 0;
            Service = CreateTestService(Manager);
            if (Service)
            {
                ok(WaitCallback(), "No callback through the minimal handle\n");
                DeleteService(Service);
                CloseServiceHandle(Service);
            }
            pUnsubscribe(Subscription);
        }
        CloseServiceHandle(Minimal);
    }
}

static ULONG
CountThreads(void)
{
    THREADENTRY32 Entry = { sizeof(Entry) };
    HANDLE Snapshot;
    ULONG Count = 0;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Thread32First(Snapshot, &Entry))
    {
        do
        {
            if (Entry.th32OwnerProcessID == GetCurrentProcessId())
                Count++;
        } while (Thread32Next(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Count;
}

static void
test_SubscribeCleanup(SC_HANDLE Manager)
{
    PSC_NOTIFICATION_REGISTRATION Subscription = NULL;
    ULONG Before, After, i;
    DWORD Error;

    Before = CountThreads();
    for (i = 0; i < 8; i++)
    {
        Error = pSubscribe(Manager, SC_EVENT_DATABASE_CHANGE, DatabaseChanged, NULL, &Subscription);
        ok(Error == ERROR_SUCCESS, "Subscription %lu returned %lu\n", i, Error);
        if (Error == ERROR_SUCCESS)
            pUnsubscribe(Subscription);
    }
    Sleep(8000);
    After = CountThreads();
    trace("Threads before %lu, after 8 subscriptions %lu\n", Before, After);
    ok(After < Before + 4, "8 ended subscriptions left %lu threads\n", After - Before);
}

static volatile BOOL ApcFired;

static VOID CALLBACK
IgnoreNotify(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
}

static void
test_Masks(SC_HANDLE Manager)
{
    SERVICE_NOTIFYW Notify = { SERVICE_NOTIFY_STATUS_CHANGE };
    SC_HANDLE Own, Service;
    DWORD Error;

    Notify.pfnNotifyCallback = IgnoreNotify;
    Own = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (Own)
    {
        Error = NotifyServiceStatusChangeW(Own, SERVICE_NOTIFY_CREATED | SERVICE_NOTIFY_RUNNING, &Notify);
        ok(Error == ERROR_INVALID_PARAMETER, "Mixed mask on a manager handle returned %lu\n", Error);
        Error = NotifyServiceStatusChangeW(Own, SERVICE_NOTIFY_RUNNING, &Notify);
        ok(Error == ERROR_INVALID_HANDLE, "State mask on a manager handle returned %lu\n", Error);
        CloseServiceHandle(Own);
    }

    Service = CreateTestService(Manager);
    ok(Service != NULL, "CreateServiceW failed (%lu)\n", GetLastError());
    if (Service)
    {
        Error = NotifyServiceStatusChangeW(Service, SERVICE_NOTIFY_CREATED, &Notify);
        ok(Error == ERROR_INVALID_HANDLE, "Database mask on a service handle returned %lu\n", Error);
        Error = NotifyServiceStatusChangeW(Service, SERVICE_NOTIFY_DELETED | SERVICE_NOTIFY_RUNNING, &Notify);
        ok(Error == ERROR_INVALID_PARAMETER, "Mixed mask on a service handle returned %lu\n", Error);
        DeleteService(Service);
        CloseServiceHandle(Service);
    }
    SleepEx(0, TRUE);
}

static VOID CALLBACK
ManagerNotified(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    ApcFired = TRUE;
}

static void
test_ServiceNames(void)
{
    SERVICE_NOTIFYW Notify = { SERVICE_NOTIFY_STATUS_CHANGE };
    SC_HANDLE Manager, Service;
    DWORD Error, Start;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!Manager)
        return;
    Notify.pfnNotifyCallback = ManagerNotified;
    ApcFired = FALSE;
    Error = NotifyServiceStatusChangeW(Manager, SERVICE_NOTIFY_CREATED, &Notify);
    ok(Error == ERROR_SUCCESS, "NotifyServiceStatusChangeW returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Service = CreateTestService(Manager);
        ok(Service != NULL, "CreateServiceW failed (%lu)\n", GetLastError());
        for (Start = GetTickCount(); !ApcFired && GetTickCount() - Start < 10000; )
            SleepEx(500, TRUE);
        ok(ApcFired, "The notification did not come\n");
        if (ApcFired)
        {
            ok(Notify.dwNotificationTriggered & SERVICE_NOTIFY_CREATED, "Triggered 0x%lx\n",
               Notify.dwNotificationTriggered);
            ok(Notify.pszServiceNames && !_wcsicmp(Notify.pszServiceNames, L"/" TEST_SERVICE),
               "Service names %S\n", Notify.pszServiceNames ? Notify.pszServiceNames : L"(null)");
            if (Notify.pszServiceNames)
                LocalFree(Notify.pszServiceNames);
        }
        if (Service)
        {
            DeleteService(Service);
            CloseServiceHandle(Service);
        }
    }
    CloseServiceHandle(Manager);
}

static volatile LONG StatusApcs;

static VOID CALLBACK
ServiceNotified(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedIncrement(&StatusApcs);
}

static void
test_ServiceStatus(SC_HANDLE Manager)
{
    const DWORD Mask = SERVICE_NOTIFY_RUNNING | SERVICE_NOTIFY_START_PENDING | SERVICE_NOTIFY_STOP_PENDING |
                       SERVICE_NOTIFY_STOPPED | SERVICE_NOTIFY_DELETE_PENDING;
    SERVICE_NOTIFYW Notify;
    SC_HANDLE Service;
    DWORD Error, Start, i;

    Service = CreateTestService(Manager);
    ok(Service != NULL, "CreateServiceW failed (%lu)\n", GetLastError());
    if (!Service)
        return;

    if (pSubscribe && pUnsubscribe)
    {
        PSC_NOTIFICATION_REGISTRATION Subscription = NULL;

        SC_HANDLE StatusOnly;

        Error = pSubscribe(Service, SC_EVENT_STATUS_CHANGE, DatabaseChanged, NULL, &Subscription);
        ok(Error == ERROR_SUCCESS, "Status subscription returned %lu\n", Error);
        if (Error == ERROR_SUCCESS)
            pUnsubscribe(Subscription);

        StatusOnly = OpenServiceW(Manager, TEST_SERVICE, SERVICE_QUERY_STATUS);
        ok(StatusOnly != NULL, "OpenServiceW failed (%lu)\n", GetLastError());
        if (StatusOnly)
        {
            Error = pSubscribe(StatusOnly, SC_EVENT_STATUS_CHANGE, DatabaseChanged, NULL, &Subscription);
            ok(Error == ERROR_SUCCESS, "Status subscription with SERVICE_QUERY_STATUS returned %lu\n", Error);
            if (Error == ERROR_SUCCESS)
                pUnsubscribe(Subscription);
            CloseServiceHandle(StatusOnly);
        }
    }

    for (i = 0; i < 1; i++)
    {
        ZeroMemory(&Notify, sizeof(Notify));
        Notify.dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
        Notify.pfnNotifyCallback = ServiceNotified;
        StatusApcs = 0;
        Error = NotifyServiceStatusChangeW(Service, Mask, &Notify);
        ok(Error == ERROR_SUCCESS, "Request %lu: NotifyServiceStatusChangeW returned %lu\n", i, Error);
        if (Error != ERROR_SUCCESS)
            break;
        for (Start = GetTickCount(); !StatusApcs && GetTickCount() - Start < 5000; )
            SleepEx(200, TRUE);
        ok(StatusApcs == 1, "Request %lu: %ld results\n", i, StatusApcs);
        ok(Notify.dwNotificationStatus == ERROR_SUCCESS, "Request %lu: status %lu\n", i, Notify.dwNotificationStatus);
        ok(Notify.dwNotificationTriggered == SERVICE_NOTIFY_STOPPED, "Request %lu: triggered 0x%lx\n", i,
           Notify.dwNotificationTriggered);
        ok(Notify.ServiceStatus.dwCurrentState == SERVICE_STOPPED, "Request %lu: state %lu\n", i,
           Notify.ServiceStatus.dwCurrentState);
        ok(Notify.pszServiceNames == NULL, "Request %lu: service names %p\n", i, Notify.pszServiceNames);
    }

    {
        SC_HANDLE Other;

        ZeroMemory(&Notify, sizeof(Notify));
        Notify.dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
        Notify.pfnNotifyCallback = ServiceNotified;
        StatusApcs = 0;
        Error = NotifyServiceStatusChangeW(Service, SERVICE_NOTIFY_RUNNING, &Notify);
        ok(Error == ERROR_SUCCESS, "Running request: NotifyServiceStatusChangeW returned %lu\n", Error);
        Other = CreateServiceW(Manager, TEST_SERVICE L"2", TEST_SERVICE L"2", SERVICE_ALL_ACCESS,
                               SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                               L"C:\\Windows\\system32\\svchost.exe", NULL, NULL, NULL, NULL, NULL);
        ok(Other != NULL, "CreateServiceW failed (%lu)\n", GetLastError());
        SleepEx(1000, TRUE);
        ok(StatusApcs == 0, "Creating another service ended the request: status %lu, triggered 0x%lx\n",
           Notify.dwNotificationStatus, Notify.dwNotificationTriggered);
        if (Other)
        {
            DeleteService(Other);
            CloseServiceHandle(Other);
        }
        SleepEx(500, TRUE);
        ok(StatusApcs == 0, "Deleting another service ended the request: status %lu, triggered 0x%lx\n",
           Notify.dwNotificationStatus, Notify.dwNotificationTriggered);
    }

    ZeroMemory(&Notify, sizeof(Notify));
    Notify.dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
    Notify.pfnNotifyCallback = ServiceNotified;
    StatusApcs = 0;
    Error = NotifyServiceStatusChangeW(Service, SERVICE_NOTIFY_DELETE_PENDING, &Notify);
    ok(Error == ERROR_SUCCESS, "Delete request: NotifyServiceStatusChangeW returned %lu\n", Error);
    ok(DeleteService(Service), "DeleteService failed (%lu)\n", GetLastError());
    if (Error == ERROR_SUCCESS)
    {
        for (Start = GetTickCount(); !StatusApcs && GetTickCount() - Start < 5000; )
            SleepEx(200, TRUE);
        ok(StatusApcs == 1, "Delete request: %ld results\n", StatusApcs);
        ok(Notify.dwNotificationTriggered == SERVICE_NOTIFY_DELETE_PENDING, "Delete request: triggered 0x%lx\n",
           Notify.dwNotificationTriggered);
    }
    Error = NotifyServiceStatusChangeW(Service, Mask, &Notify);
    ok(Error == ERROR_SERVICE_MARKED_FOR_DELETE, "Request after delete returned %lu\n", Error);
    CloseServiceHandle(Service);
}

START_TEST(ServiceChangeNotify)
{
    HMODULE hSechost = LoadLibraryW(L"sechost.dll");
    SC_HANDLE Manager, Service;

    pSubscribe = hSechost ? (PFN_SUBSCRIBE)GetProcAddress(hSechost, "SubscribeServiceChangeNotifications") : NULL;
    pUnsubscribe = hSechost ? (PFN_UNSUBSCRIBE)GetProcAddress(hSechost, "UnsubscribeServiceChangeNotifications") : NULL;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!Manager)
    {
        skip("Cannot open the service manager (%lu)\n", GetLastError());
        return;
    }

    Service = OpenServiceW(Manager, TEST_SERVICE, DELETE);
    if (Service)
    {
        DeleteService(Service);
        CloseServiceHandle(Service);
    }

    NotifyEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (pSubscribe && pUnsubscribe)
    {
        test_Subscribe(Manager);
        test_SubscribeCleanup(Manager);
    }
    else
        skip("SubscribeServiceChangeNotifications is missing\n");
    test_ServiceNames();
    test_ServiceStatus(Manager);
    test_Masks(Manager);

    CloseHandle(NotifyEvent);
    CloseServiceHandle(Manager);
}
