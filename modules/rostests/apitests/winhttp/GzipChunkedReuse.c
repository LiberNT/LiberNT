/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test connection reuse after a gzip response sent in chunks
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winsock2.h>
#include <winhttp.h>

static const BYTE GzipBody[] =
{
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x4b, 0xce, 0xcf, 0x2d, 0x28, 0x4a,
    0x2d, 0x2e, 0x4e, 0x4d, 0x51, 0x48, 0xca, 0x4f, 0xa9, 0x04, 0x00, 0xb7, 0xb4, 0xfb, 0x38, 0x0f,
    0x00, 0x00, 0x00
};

static const char GzipHeaders[] =
    "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nTransfer-Encoding: chunked\r\nConnection: Keep-Alive\r\n\r\n23\r\n";
static const char LastChunk[] = "\r\n0\r\n\r\n";
static const char PlainResponse[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: Keep-Alive\r\n\r\nok";

static SOCKET ListenSocket = INVALID_SOCKET;
static LONG Connections;
static LONG Requests;
static BOOL CloseAfterData;

static BOOL ReadRequest(SOCKET Socket)
{
    char Buffer[2048];
    int Size = 0, Received;

    while (Size < (int)sizeof(Buffer) - 1)
    {
        Received = recv(Socket, Buffer + Size, sizeof(Buffer) - 1 - Size, 0);
        if (Received <= 0)
            return FALSE;
        Size += Received;
        Buffer[Size] = 0;
        if (strstr(Buffer, "\r\n\r\n"))
            return TRUE;
    }
    return FALSE;
}

static DWORD WINAPI ServerThread(void *Context)
{
    SOCKET Socket;

    while ((Socket = accept(ListenSocket, NULL, NULL)) != INVALID_SOCKET)
    {
        InterlockedIncrement(&Connections);
        while (ReadRequest(Socket))
        {
            if (InterlockedIncrement(&Requests) == 1)
            {
                send(Socket, GzipHeaders, sizeof(GzipHeaders) - 1, 0);
                send(Socket, (const char *)GzipBody, sizeof(GzipBody), 0);
                Sleep(200);
                if (CloseAfterData)
                    break;
                send(Socket, LastChunk, sizeof(LastChunk) - 1, 0);
            }
            else
            {
                send(Socket, PlainResponse, sizeof(PlainResponse) - 1, 0);
            }
        }
        closesocket(Socket);
    }
    return 0;
}

static BOOL SendRequest(HINTERNET Connect, char *Body, DWORD BodySize, PDWORD BodyRead, PDWORD Error)
{
    HINTERNET Request;
    DWORD Status = 0, Size = sizeof(Status), Read;
    BOOL Ret;

    *BodyRead = 0;
    *Error = 0;
    Request = WinHttpOpenRequest(Connect, L"GET", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    ok(Request != NULL, "WinHttpOpenRequest failed with %lu\n", GetLastError());
    if (!Request)
        return FALSE;
    Ret = WinHttpSendRequest(Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(Request, NULL);
    if (!Ret)
    {
        *Error = GetLastError();
    }
    else
    {
        WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &Status, &Size,
                            NULL);
        ok(Status == HTTP_STATUS_OK, "Status %lu\n", Status);
        for (;;)
        {
            if (!WinHttpReadData(Request, Body + *BodyRead, BodySize - *BodyRead, &Read))
            {
                *Error = GetLastError();
                break;
            }
            if (!Read)
                break;
            *BodyRead += Read;
        }
    }
    WinHttpCloseHandle(Request);
    return Ret;
}

static void RunServer(BOOL Close)
{
    DWORD Decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP, Read, Error;
    struct sockaddr_in Address;
    int AddressSize = sizeof(Address);
    HINTERNET Session, Connect;
    char Body[64];
    HANDLE Thread;
    WSADATA Data;

    winetest_push_context(Close ? "closed" : "kept");
    CloseAfterData = Close;
    Connections = Requests = 0;
    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ListenSocket == INVALID_SOCKET || bind(ListenSocket, (struct sockaddr *)&Address, sizeof(Address)) ||
        listen(ListenSocket, 4) || getsockname(ListenSocket, (struct sockaddr *)&Address, &AddressSize))
    {
        skip("Cannot listen on the loopback address, error %d\n", WSAGetLastError());
        WSACleanup();
        winetest_pop_context();
        return;
    }
    Thread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

    Session = WinHttpOpen(L"LiberNT apitest", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    ok(Session != NULL, "WinHttpOpen failed with %lu\n", GetLastError());
    ok(WinHttpSetOption(Session, WINHTTP_OPTION_DECOMPRESSION, &Decompression, sizeof(Decompression)),
       "WinHttpSetOption failed with %lu\n", GetLastError());
    WinHttpSetTimeouts(Session, 5000, 5000, 5000, 5000);
    Connect = WinHttpConnect(Session, L"127.0.0.1", ntohs(Address.sin_port), 0);
    ok(Connect != NULL, "WinHttpConnect failed with %lu\n", GetLastError());

    ok(SendRequest(Connect, Body, sizeof(Body), &Read, &Error), "The gzip request failed with %lu\n", Error);
    if (Close)
    {
        ok(Read == 0, "Read %lu bytes\n", Read);
        ok(Error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE, "WinHttpReadData failed with %lu\n", Error);
    }
    else
    {
        ok(Read == 15 && !memcmp(Body, "compressed body", 15), "Body %.*s\n", (int)Read, Body);
        ok(Error == 0, "WinHttpReadData failed with %lu\n", Error);
    }

    ok(SendRequest(Connect, Body, sizeof(Body), &Read, &Error), "The second request failed with %lu\n", Error);
    ok(Read == 2 && !memcmp(Body, "ok", 2), "Body %.*s\n", (int)Read, Body);
    ok(Error == 0, "WinHttpReadData failed with %lu\n", Error);
    ok(Requests == 2, "The server got %ld requests\n", Requests);
    ok(Connections == (Close ? 2 : 1), "The server got %ld connections\n", Connections);

    WinHttpCloseHandle(Connect);
    WinHttpCloseHandle(Session);
    closesocket(ListenSocket);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
    WSACleanup();
    winetest_pop_context();
}

START_TEST(GzipChunkedReuse)
{
    RunServer(FALSE);
    RunServer(TRUE);
}
