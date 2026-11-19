/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NetStatisticsGet and the domain name change notification pair
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winsvc.h>
#include <lmcons.h>
#include <lmapibuf.h>
#include <lmerr.h>
#include <lmstats.h>
#include <lmconfig.h>

static BOOL
IsServiceRunning(LPCWSTR Name)
{
    SERVICE_STATUS Status = { 0 };
    SC_HANDLE Manager, Service;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
        return FALSE;
    Service = OpenServiceW(Manager, Name, SERVICE_QUERY_STATUS);
    if (Service != NULL)
    {
        QueryServiceStatus(Service, &Status);
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
    return Status.dwCurrentState == SERVICE_RUNNING;
}

static VOID
TestStatistics(VOID)
{
    LPBYTE Buffer = NULL;
    NET_API_STATUS Status;

    Status = NetStatisticsGet(NULL, L"LanmanWorkstation", 0, 0, &Buffer);
    ok(Status == NERR_Success, "Workstation statistics returned %lu\n", Status);
    ok(Buffer != NULL, "Workstation statistics returned no buffer\n");
    if (Buffer != NULL)
    {
        ok(((PSTAT_WORKSTATION_0)Buffer)->StatisticsStartTime.QuadPart != 0, "No statistics start time\n");
        NetApiBufferFree(Buffer);
        Buffer = NULL;
    }

    Status = NetStatisticsGet(NULL, L"lanmanworkstation", 0, 0, &Buffer);
    ok(Status == NERR_Success, "Lower-case service name returned %lu\n", Status);
    if (Buffer != NULL)
        NetApiBufferFree(Buffer);
    Buffer = NULL;

    if (IsServiceRunning(L"LanmanServer"))
    {
        Status = NetStatisticsGet(NULL, L"LanmanServer", 0, 0, &Buffer);
        ok(Status == NERR_Success, "Server statistics returned %lu\n", Status);
        if (Buffer != NULL)
        {
            ok(((PSTAT_SERVER_0)Buffer)->sts0_start != 0, "No server statistics start time\n");
            NetApiBufferFree(Buffer);
            Buffer = NULL;
        }
    }
    else
    {
        skip("LanmanServer is not running\n");
    }

    Status = NetStatisticsGet(NULL, L"Messenger", 0, 0, &Buffer);
    ok(Status == ERROR_NOT_SUPPORTED, "Other service returned %lu\n", Status);
    ok(Buffer == NULL, "Other service returned a buffer\n");

    Status = NetStatisticsGet(NULL, L"LanmanWorkstation", 1, 0, &Buffer);
    ok(Status == ERROR_INVALID_LEVEL, "Level 1 returned %lu\n", Status);

    Status = NetStatisticsGet(NULL, L"LanmanWorkstation", 0, 1, &Buffer);
    ok(Status == ERROR_INVALID_PARAMETER, "Options 1 returned %lu\n", Status);
}

static DWORD WINAPI
UnregisterThread(PVOID Parameter)
{
    return NetUnregisterDomainNameChangeNotification((HANDLE)Parameter);
}

static VOID
TestDomainNameNotification(VOID)
{
    HANDLE Event = NULL;
    DWORD Flags;
    NET_API_STATUS Status;

    Status = NetRegisterDomainNameChangeNotification(NULL);
    ok(Status == ERROR_INVALID_PARAMETER, "NULL handle pointer returned %lu\n", Status);

    Status = NetRegisterDomainNameChangeNotification(&Event);
    ok(Status == NERR_Success, "Register returned %lu\n", Status);
    ok(Event != NULL, "Register returned no event\n");
    if (Status != NERR_Success || Event == NULL)
        return;

    ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT, "The event starts signaled\n");
    ok(SetEvent(Event), "SetEvent failed (%lu)\n", GetLastError());
    ok(WaitForSingleObject(Event, 0) == WAIT_OBJECT_0, "The event did not signal\n");
    ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT, "The event is not auto-reset\n");

    Status = NetUnregisterDomainNameChangeNotification(Event);
    ok(Status == NERR_Success, "Unregister returned %lu\n", Status);
    SetLastError(0xdeadbeef);
    ok(!GetHandleInformation(Event, &Flags), "The event handle is still open\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "GetHandleInformation error %lu\n", GetLastError());

    Status = NetUnregisterDomainNameChangeNotification(NULL);
    ok(Status == ERROR_INVALID_PARAMETER, "Unregister of NULL returned %lu\n", Status);

    Event = NULL;
    Status = NetRegisterDomainNameChangeNotification(&Event);
    ok(Status == NERR_Success, "Second register returned %lu\n", Status);
    if (Status != NERR_Success || Event == NULL)
        return;

    HANDLE Thread = CreateThread(NULL, 0, UnregisterThread, Event, 0, NULL);
    ok(Thread != NULL, "CreateThread failed (%lu)\n", GetLastError());
    if (Thread == NULL)
        return;
    DWORD ExitCode = 0xdeadbeef;
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "The unregister thread did not finish\n");
    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed (%lu)\n", GetLastError());
    ok(ExitCode == NERR_Success, "Unregister from another thread returned %lu\n", ExitCode);
    CloseHandle(Thread);
}

START_TEST(NetStatisticsGet)
{
    TestStatistics();
    TestDomainNameNotification();
}
