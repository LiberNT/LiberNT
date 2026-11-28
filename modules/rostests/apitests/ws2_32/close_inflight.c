/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test closesocket while a send is still in flight
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winsock2.h>

typedef struct _SEND_CONTEXT
{
    SOCKET Socket;
    INT Result;
    INT Error;
    LONG Sent;
} SEND_CONTEXT, *PSEND_CONTEXT;

static DWORD WINAPI BlockingSender(PVOID Parameter)
{
    PSEND_CONTEXT Context = Parameter;
    static CHAR Buffer[65536];

    for (;;)
    {
        Context->Result = send(Context->Socket, Buffer, sizeof(Buffer), 0);
        if (Context->Result <= 0)
        {
            Context->Error = WSAGetLastError();
            return 0;
        }
        InterlockedExchangeAdd(&Context->Sent, Context->Result);
    }
}

static DWORD WINAPI Closer(PVOID Parameter)
{
    return closesocket((SOCKET)Parameter) == 0 ? 0 : WSAGetLastError();
}

static BOOL Connect(SOCKET *Listener, SOCKET *Client, SOCKET *Server)
{
    SOCKADDR_IN Addr;
    INT Len = sizeof(Addr);

    *Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    *Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*Listener == INVALID_SOCKET || *Client == INVALID_SOCKET)
        return FALSE;
    ZeroMemory(&Addr, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(*Listener, (SOCKADDR *)&Addr, sizeof(Addr)) || listen(*Listener, 1) ||
        getsockname(*Listener, (SOCKADDR *)&Addr, &Len) ||
        connect(*Client, (SOCKADDR *)&Addr, sizeof(Addr)))
        return FALSE;
    *Server = accept(*Listener, NULL, NULL);
    return *Server != INVALID_SOCKET;
}

static void TestCloseWithBlockedSend(void)
{
    SOCKET Listener, Client, Server;
    SEND_CONTEXT Context = { 0 };
    HANDLE SendThread, CloseThread;
    DWORD Wait, Code = 0xdeadbeef;
    ULONGLONG Start, Elapsed;
    LONG Before;

    ok(Connect(&Listener, &Client, &Server), "loopback connection failed, error %d\n", WSAGetLastError());

    Context.Socket = Client;
    SendThread = CreateThread(NULL, 0, BlockingSender, &Context, 0, NULL);
    ok(SendThread != NULL, "CreateThread failed %lu\n", GetLastError());
    do
    {
        Before = Context.Sent;
        Sleep(100);
    } while (Context.Sent != Before && WaitForSingleObject(SendThread, 0) == WAIT_TIMEOUT);
    ok(WaitForSingleObject(SendThread, 0) == WAIT_TIMEOUT, "sender stopped early: result %d error %d\n",
       Context.Result, Context.Error);
    trace("%ld bytes queued before the send blocked\n", Context.Sent);

    Start = GetTickCount64();
    CloseThread = CreateThread(NULL, 0, Closer, (PVOID)Client, 0, NULL);
    Wait = WaitForSingleObject(CloseThread, 2000);
    Elapsed = GetTickCount64() - Start;
    ok(Elapsed < 500, "closesocket took %I64u ms\n", Elapsed);
    ok(Wait == WAIT_OBJECT_0, "closesocket with a blocked send did not return: wait %lu\n", Wait);
    GetExitCodeThread(CloseThread, &Code);
    ok(Code == 0, "closesocket failed with %lu\n", Code);

    Wait = WaitForSingleObject(SendThread, 2000);
    ok(Wait == WAIT_OBJECT_0, "the blocked send did not return after closesocket: wait %lu\n", Wait);
    ok(Context.Result == SOCKET_ERROR, "the blocked send returned %d\n", Context.Result);
    ok(Context.Error == WSAECONNABORTED, "the blocked send failed with %d\n", Context.Error);

    closesocket(Server);
    closesocket(Listener);
    CloseHandle(CloseThread);
    CloseHandle(SendThread);
}

static void TestCloseWithQueuedData(void)
{
    SOCKET Listener, Client, Server;
    static CHAR Buffer[65536];
    u_long NonBlocking = 1;
    HANDLE CloseThread;
    DWORD Wait, Code = 0xdeadbeef;
    ULONGLONG Start, Elapsed;
    LONG Queued = 0;
    INT Result;

    ok(Connect(&Listener, &Client, &Server), "loopback connection failed, error %d\n", WSAGetLastError());
    ok(ioctlsocket(Client, FIONBIO, &NonBlocking) == 0, "FIONBIO failed %d\n", WSAGetLastError());
    while ((Result = send(Client, Buffer, sizeof(Buffer), 0)) > 0)
        Queued += Result;
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK, "send ended with %d error %d\n",
       Result, WSAGetLastError());
    trace("%ld bytes queued\n", Queued);

    Start = GetTickCount64();
    CloseThread = CreateThread(NULL, 0, Closer, (PVOID)Client, 0, NULL);
    Wait = WaitForSingleObject(CloseThread, 2000);
    Elapsed = GetTickCount64() - Start;
    ok(Elapsed < 500, "closesocket took %I64u ms\n", Elapsed);
    ok(Wait == WAIT_OBJECT_0, "closesocket with queued data did not return: wait %lu\n", Wait);
    GetExitCodeThread(CloseThread, &Code);
    ok(Code == 0, "closesocket failed with %lu\n", Code);

    closesocket(Server);
    closesocket(Listener);
    CloseHandle(CloseThread);
}

START_TEST(close_inflight)
{
    WSADATA Data;

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    TestCloseWithQueuedData();
    TestCloseWithBlockedSend();
    WSACleanup();
}
