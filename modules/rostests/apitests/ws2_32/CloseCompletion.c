/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test when closesocket lets overlapped completion routines run
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

static LONG CompletionCalls;
static DWORD CompletionError;
static DWORD CompletionBytes;

static void CALLBACK
Completion(DWORD Error, DWORD Bytes, LPWSAOVERLAPPED Overlapped, DWORD Flags)
{
    UNREFERENCED_PARAMETER(Overlapped);
    UNREFERENCED_PARAMETER(Flags);

    CompletionError = Error;
    CompletionBytes = Bytes;
    InterlockedIncrement(&CompletionCalls);
}

static BOOL
CreateTcpPair(SOCKET *Client, SOCKET *Server)
{
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address);
    SOCKET Listener;

    *Client = INVALID_SOCKET;
    *Server = INVALID_SOCKET;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Listener == INVALID_SOCKET)
        return FALSE;

    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) ||
        getsockname(Listener, (struct sockaddr *)&Address, &Length) ||
        listen(Listener, 1))
    {
        closesocket(Listener);
        return FALSE;
    }

    *Client = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (*Client != INVALID_SOCKET &&
        !connect(*Client, (struct sockaddr *)&Address, sizeof(Address)))
    {
        *Server = accept(Listener, NULL, NULL);
    }
    closesocket(Listener);

    if (*Server == INVALID_SOCKET)
    {
        if (*Client != INVALID_SOCKET)
            closesocket(*Client);
        *Client = INVALID_SOCKET;
        return FALSE;
    }

    return TRUE;
}

static void
Test_CompletedBeforeClose(void)
{
    WSAOVERLAPPED Overlapped = { 0 };
    SOCKET Client, Server;
    char Buffer[16];
    WSABUF Buf;
    DWORD Received, Flags, Start;
    int Result;

    if (!CreateTcpPair(&Client, &Server))
    {
        skip("Could not create a loopback TCP connection, error %d\n", WSAGetLastError());
        return;
    }

    CompletionCalls = 0;
    CompletionError = 0xdeadbeef;
    CompletionBytes = 0xdeadbeef;
    Buf.buf = Buffer;
    Buf.len = sizeof(Buffer);
    Flags = 0;
    Result = WSARecv(Client, &Buf, 1, &Received, &Flags, &Overlapped, Completion);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING,
       "WSARecv returned %d, error %d\n", Result, WSAGetLastError());

    ok(send(Server, "hello", 5, 0) == 5, "send failed, error %d\n", WSAGetLastError());
    Start = GetTickCount();
    while (!HasOverlappedIoCompleted(&Overlapped) && GetTickCount() - Start < 5000)
        Sleep(10);
    ok(HasOverlappedIoCompleted(&Overlapped), "The receive did not complete\n");
    ok(CompletionCalls == 0, "The completion routine ran %ld times before an alertable wait\n", CompletionCalls);

    ok(!closesocket(Client), "closesocket failed, error %d\n", WSAGetLastError());
    ok(CompletionCalls == 0, "The completion routine ran %ld times inside closesocket\n", CompletionCalls);

    SleepEx(0, TRUE);
    ok(CompletionCalls == 1, "The completion routine ran %ld times after an alertable wait\n", CompletionCalls);
    ok(CompletionError == 0, "The completion routine got error %lu\n", CompletionError);
    ok(CompletionBytes == 5, "The completion routine got %lu bytes\n", CompletionBytes);

    closesocket(Server);
}

static void
Test_PendingAtClose(void)
{
    WSAOVERLAPPED Overlapped = { 0 };
    SOCKET Client, Server;
    char Buffer[16];
    WSABUF Buf;
    DWORD Received, Flags;
    int Result;

    if (!CreateTcpPair(&Client, &Server))
    {
        skip("Could not create a loopback TCP connection, error %d\n", WSAGetLastError());
        return;
    }

    CompletionCalls = 0;
    CompletionError = 0xdeadbeef;
    CompletionBytes = 0xdeadbeef;
    Buf.buf = Buffer;
    Buf.len = sizeof(Buffer);
    Flags = 0;
    Result = WSARecv(Client, &Buf, 1, &Received, &Flags, &Overlapped, Completion);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING,
       "WSARecv returned %d, error %d\n", Result, WSAGetLastError());

    ok(!closesocket(Client), "closesocket failed, error %d\n", WSAGetLastError());
    ok(CompletionCalls == 0, "The completion routine ran %ld times inside closesocket\n", CompletionCalls);

    SleepEx(100, TRUE);
    ok(CompletionCalls == 1, "The completion routine ran %ld times after an alertable wait\n", CompletionCalls);
    ok(CompletionError == WSAECONNABORTED, "The completion routine got error %lu\n", CompletionError);
    ok(CompletionBytes == 0, "The completion routine got %lu bytes\n", CompletionBytes);

    closesocket(Server);
}

START_TEST(CloseCompletion)
{
    WSADATA Data;

    ok(!WSAStartup(MAKEWORD(2, 2), &Data), "WSAStartup failed\n");
    Test_CompletedBeforeClose();
    Test_PendingAtClose();
    WSACleanup();
}
