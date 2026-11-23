/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the sessions the Remote Desktop service started end when the service stops
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <lm.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "rdpclient.h"

#define SERVICE L"LiberNTRDPServer"

static ULONG
ProcessesInSession(DWORD Session)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    DWORD ProcessSession;
    ULONG Count = 0;
    HANDLE Snapshot;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (ProcessIdToSessionId(Entry.th32ProcessID, &ProcessSession) && ProcessSession == Session)
                Count++;
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Count;
}

static DWORD
NewestSession(VOID)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    DWORD Session, Newest = 0;
    HANDLE Snapshot;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (ProcessIdToSessionId(Entry.th32ProcessID, &Session) && Session > Newest)
                Newest = Session;
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Newest;
}

static BOOL
WaitForState(SC_HANDLE Service, DWORD State)
{
    SERVICE_STATUS Status;
    DWORD Wait;

    for (Wait = 0; Wait < 300; Wait++)
    {
        if (!QueryServiceStatus(Service, &Status))
            return FALSE;
        if (Status.dwCurrentState == State)
            return TRUE;
        Sleep(100);
    }
    return FALSE;
}

START_TEST(ServiceStop)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Client;
    SERVICE_STATUS Status;
    SC_HANDLE Manager, Service;
    WCHAR User[16];
    char UserA[16];
    DWORD Before, Session = 0, Wait, Own = 0;

    _snwprintf(User, ARRAYSIZE(User), L"rdpst%06lx", GetTickCount() & 0xffffff);
    _snprintf(UserA, sizeof(UserA), "%ls", User);
    ProcessIdToSessionId(GetCurrentProcessId(), &Own);
    if (!RdpTestEnableDebugPrivilege())
    {
        skip("Finding the sessions of other users' processes needs SeDebugPrivilege\n");
        return;
    }
    if (!RdpTestStart(&Setup))
        return;
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    Service = Manager ? OpenServiceW(Manager, SERVICE, SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS) : NULL;
    ok(Service != NULL, "Cannot open %ls: %lu\n", SERVICE, GetLastError());
    if (Service && RdpTestAddUser(User))
    {
        Before = NewestSession();
        RdpClientStart(&Client, UserA, UserA);
        ok(RdpClientWaitConnected(&Client), "%s could not connect: 0x%08x\n", UserA, Client.Error);
        Session = NewestSession();
        ok(Session > Before && Session != Own, "No new session started (newest %lu, before %lu)\n", Session, Before);
        RdpClientStop(&Client);
        if (Session > Before && Session != Own)
        {
            ok(ProcessesInSession(Session) != 0, "Session %lu has no processes\n", Session);
            ok(ControlService(Service, SERVICE_CONTROL_STOP, &Status), "Cannot stop %ls: %lu\n", SERVICE,
               GetLastError());
            ok(WaitForState(Service, SERVICE_STOPPED), "%ls did not stop\n", SERVICE);
            for (Wait = 0; Wait < 100 && ProcessesInSession(Session); Wait++)
                Sleep(100);
            ok(ProcessesInSession(Session) == 0, "%lu processes of session %lu outlived the service\n",
               ProcessesInSession(Session), Session);
            ok(StartServiceW(Service, 0, NULL), "Cannot start %ls again: %lu\n", SERVICE, GetLastError());
            ok(WaitForState(Service, SERVICE_RUNNING), "%ls did not start again\n", SERVICE);
        }
    }
    if (Service)
        CloseServiceHandle(Service);
    if (Manager)
        CloseServiceHandle(Manager);
    NetUserDel(NULL, User);
    RdpTestEnd(&Setup);
}
