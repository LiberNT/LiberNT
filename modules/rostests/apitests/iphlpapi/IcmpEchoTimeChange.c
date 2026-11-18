/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the network keeps receiving while the system time is set
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#define ROUNDS 40
#define REPLY_SIZE (sizeof(ICMP_ECHO_REPLY) + 64)

static BOOL
EnableSystemTimePrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges = { 1 };
    HANDLE Token;
    BOOL Enabled;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return FALSE;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Enabled = LookupPrivilegeValueW(NULL, L"SeSystemtimePrivilege", &Privileges.Privileges[0].Luid) &&
              AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Enabled;
}

static IPAddr
FindGateway(VOID)
{
    PMIB_IPFORWARDTABLE Table;
    ULONG Size = 0, Index;
    IPAddr Gateway = INADDR_ANY;

    if (GetIpForwardTable(NULL, &Size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return INADDR_ANY;
    Table = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Table)
        return INADDR_ANY;
    if (GetIpForwardTable(Table, &Size, FALSE) == NO_ERROR)
    {
        for (Index = 0; Index < Table->dwNumEntries; Index++)
        {
            if (Table->table[Index].dwForwardDest == INADDR_ANY && Table->table[Index].dwForwardNextHop != INADDR_ANY)
            {
                Gateway = Table->table[Index].dwForwardNextHop;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Table);
    return Gateway;
}

static DWORD
Echo(
    _In_ HANDLE Icmp,
    _In_ IPAddr Address)
{
    BYTE Reply[REPLY_SIZE];

    return IcmpSendEcho(Icmp, Address, "time", 4, NULL, Reply, sizeof(Reply), 2000);
}

START_TEST(IcmpEchoTimeChange)
{
    BYTE Reply[REPLY_SIZE];
    SYSTEMTIME Now;
    IPAddr Gateway;
    HANDLE Icmp, Event;
    ULONG Round, Answered = 0;

    if (!EnableSystemTimePrivilege())
    {
        skip("Cannot set the system time\n");
        return;
    }
    Gateway = FindGateway();
    Icmp = IcmpCreateFile();
    ok(Icmp != INVALID_HANDLE_VALUE, "IcmpCreateFile failed: %lu\n", GetLastError());
    if (Icmp == INVALID_HANDLE_VALUE)
        return;
    if (Gateway == INADDR_ANY)
    {
        skip("No default gateway\n");
        IcmpCloseHandle(Icmp);
        return;
    }
    if (!Echo(Icmp, Gateway))
    {
        ok(0, "The gateway does not answer an echo before the time is set: %lu\n", GetLastError());
        IcmpCloseHandle(Icmp);
        return;
    }

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (Round = 0; Round < ROUNDS; Round++)
    {
        ResetEvent(Event);
        IcmpSendEcho2(Icmp, Event, NULL, NULL, Gateway, "time", 4, NULL, Reply, sizeof(Reply), 2000);
        GetSystemTime(&Now);
        ok(SetSystemTime(&Now), "%lu: SetSystemTime failed: %lu\n", Round, GetLastError());
        if (WaitForSingleObject(Event, 3000) == WAIT_OBJECT_0 && IcmpParseReplies(Reply, sizeof(Reply)) != 0)
            Answered++;
    }
    ok(Answered == ROUNDS, "%lu of %u echoes were answered while the time was set\n", Answered, ROUNDS);
    ok(Echo(Icmp, Gateway) != 0, "The gateway no longer answers after the time was set\n");

    CloseHandle(Event);
    IcmpCloseHandle(Icmp);
}
