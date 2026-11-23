/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a user signed in over Remote Desktop gets a RemoteInteractive logon
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <lm.h>
#include <ntsecapi.h>
#include <stdio.h>
#include "rdpclient.h"

static ULONG
LogonsOfType(PCWSTR User, SECURITY_LOGON_TYPE Type)
{
    PSECURITY_LOGON_SESSION_DATA Session;
    PLUID Sessions = NULL;
    ULONG Count = 0, Found = 0, i;

    if (LsaEnumerateLogonSessions(&Count, &Sessions) != 0)
        return 0;
    for (i = 0; i < Count; i++)
    {
        Session = NULL;
        if (LsaGetLogonSessionData(&Sessions[i], &Session) != 0 || !Session)
            continue;
        if (Session->LogonType == (ULONG)Type && Session->UserName.Buffer &&
            Session->UserName.Length == wcslen(User) * sizeof(WCHAR) &&
            !_wcsnicmp(Session->UserName.Buffer, User, wcslen(User)))
        {
            Found++;
        }
        LsaFreeReturnBuffer(Session);
    }
    LsaFreeReturnBuffer(Sessions);
    return Found;
}

START_TEST(RemoteLogonType)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Client;
    WCHAR User[16];
    DWORD Wait;
    char UserA[16];

    _snwprintf(User, ARRAYSIZE(User), L"rdplt%06lx", GetTickCount() & 0xffffff);
    _snprintf(UserA, sizeof(UserA), "%ls", User);
    if (!RdpTestStart(&Setup))
        return;
    if (RdpTestAddUser(User))
    {
        RdpClientStart(&Client, UserA, UserA);
        ok(RdpClientWaitConnected(&Client), "%s could not connect: 0x%08x\n", UserA, Client.Error);
        for (Wait = 0; Client.Alive && !LogonsOfType(User, RemoteInteractive) && !LogonsOfType(User, Interactive) &&
                       Wait < 120; Wait++)
            Sleep(1000);
        ok(LogonsOfType(User, RemoteInteractive) == 1, "%s has %lu RemoteInteractive logons\n", UserA,
           LogonsOfType(User, RemoteInteractive));
        ok(LogonsOfType(User, Interactive) == 0, "%s has %lu Interactive logons\n", UserA,
           LogonsOfType(User, Interactive));
        RdpClientStop(&Client);
    }
    NetUserDel(NULL, User);
    RdpTestEnd(&Setup);
}
