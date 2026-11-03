/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test raw IPv4 sends with and without IP_HDRINCL
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <apitest.h>
#include <ws2tcpip.h>

START_TEST(raw_send)
{
    WSADATA WsaData;
    SOCKET Sender;
    SOCKADDR_IN Address;
    DWORD IncludeHeader = 0xdeadbeef;
    INT Length = sizeof(IncludeHeader);
    INT Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    Sender = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (Sender == INVALID_SOCKET)
    {
        skip("Raw IPv4 sockets are unavailable, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    Result = getsockopt(Sender, IPPROTO_IP, IP_HDRINCL, (CHAR *)&IncludeHeader, &Length);
    ok(Result == 0, "getsockopt(IP_HDRINCL) failed with %d\n", WSAGetLastError());
    ok(IncludeHeader == 0, "IP_HDRINCL is %lu by default\n", IncludeHeader);

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Result = sendto(Sender, "LiberNT!", 8, 0, (const SOCKADDR *)&Address, sizeof(Address));
    ok(Result == 8, "sendto without IP_HDRINCL returned %d, error %d\n", Result, WSAGetLastError());

    IncludeHeader = TRUE;
    Result = setsockopt(Sender, IPPROTO_IP, IP_HDRINCL, (const CHAR *)&IncludeHeader, sizeof(IncludeHeader));
    ok(Result == 0, "setsockopt(IP_HDRINCL) failed with %d\n", WSAGetLastError());
    IncludeHeader = 0xdeadbeef;
    Length = sizeof(IncludeHeader);
    Result = getsockopt(Sender, IPPROTO_IP, IP_HDRINCL, (CHAR *)&IncludeHeader, &Length);
    ok(Result == 0, "getsockopt(IP_HDRINCL) failed with %d\n", WSAGetLastError());
    ok(IncludeHeader == 1, "IP_HDRINCL is %lu after setting it\n", IncludeHeader);

    closesocket(Sender);
    WSACleanup();
}
