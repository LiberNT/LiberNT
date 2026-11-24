/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a session whose display disappears with the Remote Desktop service keeps running
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <lm.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "rdpclient.h"

#define SERVICE L"LiberNTRDPServer"

static ULONG
ProcessesInSession(DWORD Session, PCWSTR Name)
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
            if (ProcessIdToSessionId(Entry.th32ProcessID, &ProcessSession) && ProcessSession == Session &&
                (!Name || !_wcsicmp(Entry.szExeFile, Name)))
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

static BOOL
KillService(SC_HANDLE Service)
{
    SERVICE_STATUS_PROCESS Status;
    HANDLE Process;
    DWORD Size;
    BOOL Killed;

    if (!QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (LPBYTE)&Status, sizeof(Status), &Size) ||
        !Status.dwProcessId)
        return FALSE;
    Process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, Status.dwProcessId);
    if (!Process)
        return FALSE;
    Killed = TerminateProcess(Process, 1) && WaitForSingleObject(Process, 10000) == WAIT_OBJECT_0;
    CloseHandle(Process);
    return Killed;
}

START_TEST(ServiceCrash)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Client;
    SC_HANDLE Manager, Service;
    WCHAR User[16], Next[16];
    char UserA[16], NextA[16];
    DWORD Before, Session = 0, Restarted = 0, Wait, Own = 0;

    _snwprintf(User, ARRAYSIZE(User), L"rdpsc%06lx", GetTickCount() & 0xffffff);
    _snprintf(UserA, sizeof(UserA), "%ls", User);
    _snwprintf(Next, ARRAYSIZE(Next), L"rdpsn%06lx", GetTickCount() & 0xffffff);
    _snprintf(NextA, sizeof(NextA), "%ls", Next);
    ProcessIdToSessionId(GetCurrentProcessId(), &Own);
    if (!RdpTestEnableDebugPrivilege())
    {
        skip("Ending the service and finding the processes of other sessions needs SeDebugPrivilege\n");
        return;
    }
    if (!RdpTestStart(&Setup))
        return;
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    Service = Manager ? OpenServiceW(Manager, SERVICE, SERVICE_START | SERVICE_QUERY_STATUS) : NULL;
    ok(Service != NULL, "Cannot open %ls: %lu\n", SERVICE, GetLastError());
    if (Service && RdpTestAddUser(User))
    {
        Before = NewestSession();
        RdpClientStart(&Client, UserA, UserA);
        ok(RdpClientWaitConnected(&Client), "%s could not connect: 0x%08x\n", UserA, Client.Error);
        for (Wait = 0; Wait < 100 && (Session = NewestSession()) <= Before; Wait++)
            Sleep(200);
        ok(Session > Before && Session != Own, "No new session started (newest %lu, before %lu)\n", Session, Before);
        if (Session > Before && Session != Own)
        {
            Sleep(3000);
            ok(ProcessesInSession(Session, L"csrss.exe") == 1, "Session %lu has no csrss\n", Session);
            ok(KillService(Service), "Cannot end %ls: %lu\n", SERVICE, GetLastError());
            RdpClientStop(&Client);
            Sleep(10000);
            ok(ProcessesInSession(Session, L"csrss.exe") == 1, "csrss of session %lu did not survive its display\n",
               Session);
            ok(ProcessesInSession(Session, L"winlogon.exe") == 1,
               "Winlogon of session %lu did not survive its display\n", Session);
            ok(StartServiceW(Service, 0, NULL), "Cannot start %ls again: %lu\n", SERVICE, GetLastError());
            ok(WaitForState(Service, SERVICE_RUNNING), "%ls did not start again\n", SERVICE);
            if (RdpTestAddUser(Next))
            {
                RdpClientStart(&Client, NextA, NextA);
                ok(RdpClientWaitConnected(&Client), "%s could not connect after the restart: 0x%08x\n", NextA,
                   Client.Error);
                for (Wait = 0; Wait < 200 && (Restarted = NewestSession()) <= Session; Wait++)
                    Sleep(200);
                ok(Restarted > Session, "No session started for %s after the restart (newest %lu)\n", NextA, Restarted);
                RdpClientStop(&Client);
                NetUserDel(NULL, Next);
            }
        }
        else
        {
            RdpClientStop(&Client);
        }
    }
    if (Service)
        CloseServiceHandle(Service);
    if (Manager)
        CloseServiceHandle(Manager);
    NetUserDel(NULL, User);
    RdpTestEnd(&Setup);
}
