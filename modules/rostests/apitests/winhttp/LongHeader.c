/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test response headers longer than one network packet
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winsock2.h>
#include <winhttp.h>

#define LONG_VALUE_LEN 40000

static SOCKET ListenSocket = INVALID_SOCKET;

static DWORD WINAPI ServerThread(void *Context)
{
    static const char Head[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nX-Short: 1\r\nX-Long: ";
    static const char Tail[] = "\r\nX-After: 2\r\n\r\nok";
    char Buffer[2048], *Value;
    SOCKET Socket;
    int Size = 0, Received;

    Socket = accept(ListenSocket, NULL, NULL);
    if (Socket == INVALID_SOCKET)
        return 0;
    while (Size < (int)sizeof(Buffer) - 1)
    {
        Received = recv(Socket, Buffer + Size, sizeof(Buffer) - 1 - Size, 0);
        if (Received <= 0)
            break;
        Size += Received;
        Buffer[Size] = 0;
        if (strstr(Buffer, "\r\n\r\n"))
            break;
    }
    Value = HeapAlloc(GetProcessHeap(), 0, LONG_VALUE_LEN);
    if (Value)
    {
        memset(Value, 'a', LONG_VALUE_LEN);
        send(Socket, Head, sizeof(Head) - 1, 0);
        send(Socket, Value, LONG_VALUE_LEN, 0);
        send(Socket, Tail, sizeof(Tail) - 1, 0);
        HeapFree(GetProcessHeap(), 0, Value);
    }
    shutdown(Socket, SD_SEND);
    while (recv(Socket, Buffer, sizeof(Buffer), 0) > 0)
        ;
    closesocket(Socket);
    return 0;
}

static void CheckHeader(HINTERNET Request, PCWSTR Name, DWORD Length, WCHAR Character)
{
    WCHAR *Value;
    DWORD Size = 0, Index;
    BOOL Same = TRUE;

    ok(!WinHttpQueryHeaders(Request, WINHTTP_QUERY_CUSTOM, Name, NULL, &Size, NULL), "%ls: no error\n", Name);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%ls: error %lu\n", Name, GetLastError());
    ok(Size == Length * sizeof(WCHAR) + sizeof(WCHAR), "%ls: size %lu\n", Name, Size);
    Value = HeapAlloc(GetProcessHeap(), 0, Size + sizeof(WCHAR));
    if (!Value)
        return;
    ok(WinHttpQueryHeaders(Request, WINHTTP_QUERY_CUSTOM, Name, Value, &Size, NULL),
       "%ls: WinHttpQueryHeaders failed with %lu\n", Name, GetLastError());
    ok(Size == Length * sizeof(WCHAR), "%ls: returned size %lu\n", Name, Size);
    for (Index = 0; Index < Length && Index < Size / sizeof(WCHAR); Index++)
    {
        if (Value[Index] != Character)
            Same = FALSE;
    }
    ok(Same, "%ls: wrong value\n", Name);
    HeapFree(GetProcessHeap(), 0, Value);
}

START_TEST(LongHeader)
{
    struct sockaddr_in Address;
    int AddressSize = sizeof(Address);
    HINTERNET Session, Connect, Request;
    DWORD Status = 0, Size = sizeof(Status), Read = 0;
    char Body[16];
    HANDLE Thread;
    WSADATA Data;

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ListenSocket == INVALID_SOCKET || bind(ListenSocket, (struct sockaddr *)&Address, sizeof(Address)) ||
        listen(ListenSocket, 1) || getsockname(ListenSocket, (struct sockaddr *)&Address, &AddressSize))
    {
        skip("Cannot listen on the loopback address, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    Thread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

    Session = WinHttpOpen(L"LiberNT apitest", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    ok(Session != NULL, "WinHttpOpen failed with %lu\n", GetLastError());
    WinHttpSetTimeouts(Session, 5000, 5000, 5000, 5000);
    Connect = WinHttpConnect(Session, L"127.0.0.1", ntohs(Address.sin_port), 0);
    Request = WinHttpOpenRequest(Connect, L"GET", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    ok(Request != NULL, "WinHttpOpenRequest failed with %lu\n", GetLastError());
    if (Request)
    {
        ok(WinHttpSendRequest(Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
           WinHttpReceiveResponse(Request, NULL), "The request failed with %lu\n", GetLastError());
        WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &Status, &Size,
                            NULL);
        ok(Status == HTTP_STATUS_OK, "Status %lu\n", Status);
        CheckHeader(Request, L"X-Short", 1, L'1');
        CheckHeader(Request, L"X-Long", LONG_VALUE_LEN, L'a');
        CheckHeader(Request, L"X-After", 1, L'2');
        ok(WinHttpReadData(Request, Body, sizeof(Body), &Read), "WinHttpReadData failed with %lu\n", GetLastError());
        ok(Read == 2 && !memcmp(Body, "ok", 2), "Body %.*s\n", (int)Read, Body);
        WinHttpCloseHandle(Request);
    }

    WinHttpCloseHandle(Connect);
    WinHttpCloseHandle(Session);
    closesocket(ListenSocket);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
    WSACleanup();
}
