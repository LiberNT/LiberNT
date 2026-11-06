/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a HEAD request leaves the cached GET response of its URL intact
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winsock2.h>
#include <wininet.h>
#include <stdio.h>

static const char Body[] = "<?xml version=\"1.0\"?><headcache>cached body</headcache>";

static SOCKET ListenSocket = INVALID_SOCKET;
static LONG GetCount, HeadCount;

static DWORD WINAPI
ServerThread(PVOID Parameter)
{
    char Request[2048], Response[512];
    SOCKET Client;
    int Length, Total, HeaderLength;
    BOOL Head;

    UNREFERENCED_PARAMETER(Parameter);

    for (;;)
    {
        Client = accept(ListenSocket, NULL, NULL);
        if (Client == INVALID_SOCKET)
            break;

        Total = 0;
        do
        {
            Length = recv(Client, Request + Total, sizeof(Request) - 1 - Total, 0);
            if (Length <= 0)
                break;
            Total += Length;
            Request[Total] = 0;
        } while (!strstr(Request, "\r\n\r\n") && Total < (int)sizeof(Request) - 1);

        if (Total > 0)
        {
            Head = !strncmp(Request, "HEAD ", 5);
            InterlockedIncrement(Head ? &HeadCount : &GetCount);
            HeaderLength = sprintf(Response,
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/xml\r\n"
                                   "Content-Length: %u\r\n"
                                   "Cache-Control: max-age=3600\r\n"
                                   "Last-Modified: Mon, 02 Nov 2026 00:00:00 GMT\r\n"
                                   "Connection: close\r\n"
                                   "\r\n",
                                   (unsigned)(sizeof(Body) - 1));
            send(Client, Response, HeaderLength, 0);
            if (!Head)
                send(Client, Body, sizeof(Body) - 1, 0);
        }
        shutdown(Client, SD_SEND);
        closesocket(Client);
    }
    return 0;
}

static VOID
SendRequest(HINTERNET Connect, PCSTR Verb, DWORD Flags, PDWORD Status, PCHAR Data, DWORD DataSize, PDWORD Read)
{
    HINTERNET Request;
    DWORD Size, Chunk;

    *Status = 0;
    *Read = 0;
    Request = HttpOpenRequestA(Connect, Verb, "/headcache.xml", NULL, NULL, NULL, Flags, 0);
    ok(Request != NULL, "%s: HttpOpenRequestA failed, error %lu\n", Verb, GetLastError());
    if (!Request)
        return;

    if (!HttpSendRequestA(Request, NULL, 0, NULL, 0))
    {
        ok(FALSE, "%s 0x%lx: HttpSendRequestA failed, error %lu\n", Verb, Flags, GetLastError());
        InternetCloseHandle(Request);
        return;
    }

    Size = sizeof(*Status);
    ok(HttpQueryInfoA(Request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, Status, &Size, NULL),
       "%s: HttpQueryInfoA failed, error %lu\n", Verb, GetLastError());

    while (*Read < DataSize && InternetReadFile(Request, Data + *Read, DataSize - *Read, &Chunk) && Chunk)
        *Read += Chunk;

    InternetCloseHandle(Request);
}

static DWORD
GetCacheSize(PCSTR Url, PDWORD Error)
{
    BYTE Buffer[4096];
    INTERNET_CACHE_ENTRY_INFOA *Info = (PVOID)Buffer;
    DWORD Size = sizeof(Buffer);

    *Error = ERROR_SUCCESS;
    if (!GetUrlCacheEntryInfoA(Url, Info, &Size))
    {
        *Error = GetLastError();
        return 0;
    }
    return Info->dwSizeLow;
}

START_TEST(HeadCache)
{
    struct sockaddr_in Address = { 0 };
    char Url[64], Data[512];
    DWORD Status, Read, Error, Size;
    HINTERNET Session, Connect;
    HANDLE Thread;
    WSADATA WsaData;
    int AddressLength = sizeof(Address);

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");
    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(ListenSocket != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (ListenSocket == INVALID_SOCKET)
        return;
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(ListenSocket, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed, error %d\n", WSAGetLastError());
    ok(getsockname(ListenSocket, (struct sockaddr *)&Address, &AddressLength) == 0,
       "getsockname failed, error %d\n", WSAGetLastError());
    ok(listen(ListenSocket, 4) == 0, "listen failed, error %d\n", WSAGetLastError());
    sprintf(Url, "http://127.0.0.1:%u/headcache.xml", ntohs(Address.sin_port));

    Thread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());

    Session = InternetOpenA("LiberNT HeadCache", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    ok(Session != NULL, "InternetOpenA failed, error %lu\n", GetLastError());
    Connect = InternetConnectA(Session, "127.0.0.1", ntohs(Address.sin_port), NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    ok(Connect != NULL, "InternetConnectA failed, error %lu\n", GetLastError());
    if (!Session || !Connect || !Thread)
        goto cleanup;

    DeleteUrlCacheEntryA(Url);

    SendRequest(Connect, "GET", 0, &Status, Data, sizeof(Data), &Read);
    ok(Status == 200, "GET: status %lu\n", Status);
    ok(Read == sizeof(Body) - 1 && !memcmp(Data, Body, Read), "GET: read %lu bytes\n", Read);
    Size = GetCacheSize(Url, &Error);
    ok(Error == ERROR_SUCCESS, "GET: no cache entry, error %lu\n", Error);
    ok(Size == sizeof(Body) - 1, "GET: cache entry size %lu\n", Size);

    SendRequest(Connect, "HEAD", 0, &Status, Data, sizeof(Data), &Read);
    ok(Status == 200, "HEAD: status %lu\n", Status);
    ok(Read == 0, "HEAD: read %lu bytes\n", Read);
    Size = GetCacheSize(Url, &Error);
    ok(Error == ERROR_SUCCESS, "HEAD: cache entry gone, error %lu\n", Error);
    ok(Size == sizeof(Body) - 1, "HEAD: cache entry size %lu\n", Size);

    SendRequest(Connect, "GET", INTERNET_FLAG_FROM_CACHE, &Status, Data, sizeof(Data), &Read);
    ok(Read == sizeof(Body) - 1 && !memcmp(Data, Body, Read), "GET from cache after HEAD: read %lu bytes\n", Read);

    SendRequest(Connect, "GET", 0, &Status, Data, sizeof(Data), &Read);
    ok(Status == 200, "GET after HEAD: status %lu\n", Status);
    ok(Read == sizeof(Body) - 1 && !memcmp(Data, Body, Read), "GET after HEAD: read %lu bytes\n", Read);

    ok(DeleteUrlCacheEntryA(Url), "DeleteUrlCacheEntryA failed, error %lu\n", GetLastError());
    SendRequest(Connect, "HEAD", 0, &Status, Data, sizeof(Data), &Read);
    ok(Status == 200, "HEAD without a cached GET: status %lu\n", Status);
    Size = GetCacheSize(Url, &Error);
    ok(Error == ERROR_FILE_NOT_FOUND, "HEAD without a cached GET: cache entry size %lu, error %lu\n", Size, Error);

    ok(HeadCount == 2, "server saw %ld HEAD requests\n", HeadCount);
    ok(GetCount == 1, "server saw %ld GET requests\n", GetCount);
    DeleteUrlCacheEntryA(Url);

cleanup:
    if (Connect)
        InternetCloseHandle(Connect);
    if (Session)
        InternetCloseHandle(Session);
    closesocket(ListenSocket);
    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "server thread did not exit\n");
        CloseHandle(Thread);
    }
    WSACleanup();
}
