/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the Remote Desktop server starts no session for a client that has not signed in
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <winsock2.h>
#include <tlhelp32.h>
#include <stdio.h>
#include "rdpclient.h"

#define COOKIE_USER  "rdpnon1"
#define ROUTED_USER  "rdpnon2"
#define UNKNOWN_USER "rdpnon3"

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

static DWORD
SessionAfter(DWORD Before)
{
    DWORD Wait, Newest = Before;

    for (Wait = 0; Wait < 40 && Newest == Before; Wait++)
    {
        Sleep(500);
        Newest = NewestSession();
    }
    return Newest;
}

/* An X.224 Connection Request with a cookie and a request for CredSSP (MS-RDPBCGR 2.2.1.1), then nothing more */
static BOOL
SendCookieOnly(const char *Cookie)
{
    struct sockaddr_in Address = { 0 };
    BYTE Request[128], Reply[64];
    int Length, Got;
    SOCKET Socket;
    BOOL Answered;

    Length = 11 + _snprintf((char *)Request + 11, sizeof(Request) - 11 - 8, "%s", Cookie);
    Request[Length++] = 1;
    Request[Length++] = 0;
    Request[Length++] = 8;
    Request[Length++] = 0;
    Request[Length++] = 3;
    Request[Length++] = 0;
    Request[Length++] = 0;
    Request[Length++] = 0;
    Request[0] = 3;
    Request[1] = 0;
    Request[2] = (BYTE)(Length >> 8);
    Request[3] = (BYTE)Length;
    Request[4] = (BYTE)(Length - 5);
    Request[5] = 0xE0;
    memset(Request + 6, 0, 5);
    Address.sin_family = AF_INET;
    Address.sin_port = htons(3389);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
        return FALSE;
    Answered = connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0 &&
               send(Socket, (const char *)Request, Length, 0) == Length &&
               (Got = recv(Socket, (char *)Reply, sizeof(Reply), 0)) >= 11 && Reply[0] == 3 && Reply[5] == 0xD0;
    Sleep(5000);
    closesocket(Socket);
    return Answered;
}

START_TEST(PreAuthSession)
{
    RDP_TEST_SETUP Setup;
    RDP_CLIENT Client;
    DWORD Before, After;

    if (!RdpTestStart(&Setup))
        return;
    ok(RdpTestEnableDebugPrivilege(), "Cannot enable SeDebugPrivilege: %lu\n", GetLastError());

    Before = NewestSession();
    ok(SendCookieOnly("Cookie: mstshash=" COOKIE_USER "\r\n"), "The server did not answer the connection request\n");
    After = SessionAfter(Before);
    ok(After == Before, "A connection request naming %s started session %lu\n", COOKIE_USER, After);

    Before = NewestSession();
    ok(SendCookieOnly("Cookie: msts=user=" ROUTED_USER "\r\n"), "The server did not answer the routed request\n");
    After = SessionAfter(Before);
    ok(After == Before, "A routing token naming %s started session %lu\n", ROUTED_USER, After);

    Before = NewestSession();
    RdpClientStart(&Client, UNKNOWN_USER, UNKNOWN_USER);
    ok(!RdpClientWaitConnected(&Client), "%s connected without an account\n", UNKNOWN_USER);
    After = SessionAfter(Before);
    ok(After == Before, "A client that failed NLA as %s started session %lu\n", UNKNOWN_USER, After);
    RdpClientStop(&Client);

    RdpTestEnd(&Setup);
}
