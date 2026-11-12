/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for WSASocket
 * PROGRAMMERS:     Peter Hater
 */

#include "ws2_32.h"
#include <winuser.h>
#include <ndk/extypes.h>
#include <ndk/exfuncs.h>
#include "../kernel32/threadwait.h"

void Test_CloseDuplicatedSocket()
{
    char szBuf[10];
    int err;
    SOCKET sck, dup_sck;
    WSAPROTOCOL_INFOW ProtocolInfo;
    struct sockaddr_in to = { AF_INET, 2222, {{{ 0x7f, 0x00, 0x00, 0x01 }}} };

    /* Create the socket */
    sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sck == INVALID_SOCKET)
    {
        skip("socket failed %d. Aborting test.\n", WSAGetLastError());
        return;
    }

    err = sendto(sck, szBuf, _countof(szBuf), 0, (struct sockaddr *)&to, sizeof(to));
    ok(err == _countof(szBuf), "sendto err = %d %d\n", err, WSAGetLastError());

    err = WSADuplicateSocketW(sck, GetCurrentProcessId(), &ProtocolInfo);
    ok(err == 0, "WSADuplicateSocketW err = %d %d\n", err, WSAGetLastError());

    dup_sck = WSASocketW(0, 0, 0, &ProtocolInfo, 0, 0);
    if (dup_sck == INVALID_SOCKET)
    {
        skip("WSASocketW failed %d. Aborting test.\n", WSAGetLastError());
        closesocket(sck);
        return;
    }

    err = sendto(dup_sck, szBuf, _countof(szBuf), 0, (struct sockaddr *)&to, sizeof(to));
    ok(err == _countof(szBuf), "sendto err = %d %d\n", err, WSAGetLastError());

    err = closesocket(sck);
    ok(err == 0, "closesocket sck err = %d %d\n", err, WSAGetLastError());

    err = sendto(dup_sck, szBuf, _countof(szBuf), 0, (struct sockaddr *)&to, sizeof(to));
    ok(err == _countof(szBuf), "sendto err = %d %d\n", err, WSAGetLastError());

    err = closesocket(dup_sck);
    ok(err == 0, "closesocket dup_sck err = %d %d\n", err, WSAGetLastError());
    return;
}

// 100 ms
#define TIMEOUT_SEC 0
#define TIMEOUT_USEC 100000

#define THREAD_PROC_LOOPS 5

#define LISTEN_PORT 22222
#define LISTEN_BACKLOG 5

DWORD WINAPI thread_proc(void* param)
{
    fd_set read, write, except;
    struct timeval tval;
    SOCKET sock = (SOCKET)param;
    int i;

    tval.tv_sec = TIMEOUT_SEC;
    tval.tv_usec = TIMEOUT_USEC;

    for (i = 0; i < THREAD_PROC_LOOPS; ++i)
    {
        FD_ZERO(&read); FD_ZERO(&write); FD_ZERO(&except);
        // write will be empty
        FD_SET(sock, &read); FD_SET(sock, &except);

        select(0, &read, &write, &except, &tval);
    }

    return 0;
}

void Test_CloseWhileSelectSameSocket()
{
    int err;
    SOCKET sock;
    struct sockaddr_in addrin;
    HANDLE hthread;
    DWORD ThreadId;

    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        skip("socket failed %d. Aborting test.\n", WSAGetLastError());
        return;
    }

    memset(&addrin, 0, sizeof(struct sockaddr_in));
    addrin.sin_family = AF_INET;
    addrin.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrin.sin_port = htons(LISTEN_PORT);

    err = bind(sock, (struct sockaddr*)(&addrin), sizeof(struct sockaddr_in));
    ok(err == 0, "bind err = %d %d\n", err, WSAGetLastError());
    err = listen(sock, LISTEN_BACKLOG);
    ok(err == 0, "listen err = %d %d\n", err, WSAGetLastError());

    hthread = CreateThread(NULL, 0, thread_proc, (void*)sock, 0, &ThreadId);
    ok(hthread != NULL, "CreateThread %ld\n", GetLastError());

    ok(WaitForThreadWaitState(ThreadId, NULL, ANY_WAIT_REASON, 5000), "The select thread did not block\n");
    err = closesocket(sock);
    ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());

    WaitForSingleObject(hthread, INFINITE);
    CloseHandle(hthread);
    return;
}

void Test_CloseWhileSelectDuplicatedSocket()
{
    int err;
    SOCKET sock, dup_sock;
    WSAPROTOCOL_INFOW ProtocolInfo;
    struct sockaddr_in addrin;
    HANDLE hthread;
    DWORD ThreadId;

    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        skip("socket failed %d. Aborting test.\n", WSAGetLastError());
        return;
    }

    memset(&addrin, 0, sizeof(struct sockaddr_in));
    addrin.sin_family = AF_INET;
    addrin.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrin.sin_port = htons(LISTEN_PORT);

    err = bind(sock, (struct sockaddr*)(&addrin), sizeof(struct sockaddr_in));
    ok(err == 0, "bind err = %d %d\n", err, WSAGetLastError());
    err = listen(sock, LISTEN_BACKLOG);
    ok(err == 0, "listen err = %d %d\n", err, WSAGetLastError());

    err = WSADuplicateSocketW(sock, GetCurrentProcessId(), &ProtocolInfo);
    ok(err == 0, "WSADuplicateSocketW err = %d %d\n", err, WSAGetLastError());

    dup_sock = WSASocketW(0, 0, 0, &ProtocolInfo, 0, 0);
    if (dup_sock == INVALID_SOCKET)
    {
        skip("WSASocketW failed %d. Aborting test.\n", WSAGetLastError());
        closesocket(sock);
        return;
    }

    hthread = CreateThread(NULL, 0, thread_proc, (void*)dup_sock, 0, &ThreadId);
    ok(hthread != NULL, "CreateThread %ld\n", GetLastError());

    err = closesocket(sock);
    ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());

    ok(WaitForThreadWaitState(ThreadId, NULL, ANY_WAIT_REASON, 5000), "The select thread did not block\n");
    err = closesocket(dup_sock);
    ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());

    WaitForSingleObject(hthread, INFINITE);
    CloseHandle(hthread);
    return;
}

void Test_CloseWithPendingAsyncSelect()
{
    HWND hwnd;
    SOCKET listener, client, server, sck;
    struct sockaddr_in addr = { AF_INET, 0, {{{ 0x7f, 0x00, 0x00, 0x01 }}} };
    int len, err, i;
    MSG msg;

    hwnd = CreateWindowExA(0, "STATIC", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowExA failed %lu\n", GetLastError());
    if (!hwnd)
        return;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(listener != INVALID_SOCKET, "socket failed %d\n", WSAGetLastError());
    err = bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    ok(err == 0, "bind err = %d %d\n", err, WSAGetLastError());
    err = listen(listener, SOMAXCONN);
    ok(err == 0, "listen err = %d %d\n", err, WSAGetLastError());
    len = sizeof(addr);
    err = getsockname(listener, (struct sockaddr *)&addr, &len);
    ok(err == 0, "getsockname err = %d %d\n", err, WSAGetLastError());

    for (i = 0; i < 200; i++)
    {
        client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(client != INVALID_SOCKET, "socket failed %d\n", WSAGetLastError());
        err = connect(client, (struct sockaddr *)&addr, sizeof(addr));
        ok(err == 0, "connect err = %d %d\n", err, WSAGetLastError());
        server = accept(listener, NULL, NULL);
        ok(server != INVALID_SOCKET, "accept failed %d\n", WSAGetLastError());
        err = WSAAsyncSelect(server, hwnd, WM_USER, FD_READ | FD_CLOSE);
        ok(err == 0, "WSAAsyncSelect err = %d %d\n", err, WSAGetLastError());
        err = closesocket(server);
        ok(err == 0, "closesocket server err = %d %d\n", err, WSAGetLastError());
        err = closesocket(client);
        ok(err == 0, "closesocket client err = %d %d\n", err, WSAGetLastError());
    }

    for (i = 0; i < 500; i++)
    {
        sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ok(sck != INVALID_SOCKET, "socket failed %d\n", WSAGetLastError());
        err = WSAAsyncSelect(sck, hwnd, WM_USER, FD_READ | FD_WRITE | FD_CLOSE);
        ok(err == 0, "WSAAsyncSelect err = %d %d\n", err, WSAGetLastError());
        err = closesocket(sck);
        ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());
    }

    Sleep(500);
    while (PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE))
        DispatchMessageA(&msg);

    closesocket(listener);
    DestroyWindow(hwnd);
}

void Test_RebindAfterCloseWithPendingReceive(void)
{
    SOCKET first, second;
    struct sockaddr_in addr;
    WSAOVERLAPPED overlapped;
    WSABUF buffer;
    char data[16];
    DWORD flags = 0;
    BOOL reuse = TRUE;
    int err, len = sizeof(addr);

    first = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(first != INVALID_SOCKET, "socket failed %d\n", WSAGetLastError());
    err = setsockopt(first, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    ok(err == 0, "setsockopt err = %d %d\n", err, WSAGetLastError());
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    err = bind(first, (const struct sockaddr *)&addr, sizeof(addr));
    ok(err == 0, "bind err = %d %d\n", err, WSAGetLastError());
    err = getsockname(first, (struct sockaddr *)&addr, &len);
    ok(err == 0, "getsockname err = %d %d\n", err, WSAGetLastError());

    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = WSACreateEvent();
    buffer.buf = data;
    buffer.len = sizeof(data);
    err = WSARecvFrom(first, &buffer, 1, NULL, &flags, NULL, NULL, &overlapped, NULL);
    ok(err == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING, "WSARecvFrom err = %d %d\n", err, WSAGetLastError());
    err = closesocket(first);
    ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());

    second = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(second != INVALID_SOCKET, "socket failed %d\n", WSAGetLastError());
    err = setsockopt(second, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    ok(err == 0, "setsockopt err = %d %d\n", err, WSAGetLastError());
    err = bind(second, (const struct sockaddr *)&addr, sizeof(addr));
    ok(err == 0, "bind err = %d %d\n", err, WSAGetLastError());
    closesocket(second);
    WSACloseEvent(overlapped.hEvent);
}

typedef struct _CLOSE_RACE_CONTEXT
{
    SOCKET Listener;
    int Iterations;
} CLOSE_RACE_CONTEXT;

static DWORD WINAPI CloseRaceServer(PVOID Parameter)
{
    CLOSE_RACE_CONTEXT *Context = Parameter;
    char Data[64] = { 0 };
    int i;

    for (i = 0; i < Context->Iterations; i++)
    {
        SOCKET Client = accept(Context->Listener, NULL, NULL);

        if (Client == INVALID_SOCKET)
            return 1;
        send(Client, Data, sizeof(Data), 0);
        closesocket(Client);
    }
    return 0;
}

/* The end of the stream must wake a read that is being queued while it arrives */
static void Test_ReadRacesClose(void)
{
    enum { ITERATIONS = 500, TIMEOUT_MS = 5000 };
    CLOSE_RACE_CONTEXT Context;
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address), Hung = 0, i;
    HANDLE Thread;
    DWORD Timeout = TIMEOUT_MS;

    Context.Iterations = ITERATIONS;
    Context.Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Context.Listener != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Context.Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Context.Listener, 5) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(Context.Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());

    Thread = CreateThread(NULL, 0, CloseRaceServer, &Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());

    for (i = 0; i < ITERATIONS && !Hung; i++)
    {
        SOCKET Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        char Buffer[64];
        int Received;

        setsockopt(Client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
        ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
        while ((Received = recv(Client, Buffer, sizeof(Buffer), 0)) > 0)
            ;
        if (Received != 0)
            Hung = 1;
        ok(Received == 0, "Iteration %d: recv returned %d, error %d\n", i, Received, WSAGetLastError());
        closesocket(Client);
    }

    closesocket(Context.Listener);
    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0 || Hung, "The server thread did not finish\n");
        CloseHandle(Thread);
    }
}

typedef struct _BLOCKED_RECV_CONTEXT
{
    SOCKET Client;
    HANDLE Started;
    int Received;
    int Error;
} BLOCKED_RECV_CONTEXT;

static DWORD WINAPI BlockedRecvThread(PVOID Parameter)
{
    BLOCKED_RECV_CONTEXT *Context = Parameter;
    char Buffer[64];

    SetEvent(Context->Started);
    Context->Received = recv(Context->Client, Buffer, sizeof(Buffer), 0);
    Context->Error = WSAGetLastError();
    return 0;
}

/* A recv blocked in one thread must survive another thread closing its socket */
static void Test_CloseRacesBlockedRecv(void)
{
    enum { ITERATIONS = 40, SPRAY_COUNT = 256 };
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address), i, j, Failed = 0;
    PVOID Spray[SPRAY_COUNT];
    SOCKET Listener;
    DWORD ThreadId;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());

    for (i = 0; i < ITERATIONS && !Failed; i++)
    {
        BLOCKED_RECV_CONTEXT Context;
        SOCKET Server;
        HANDLE Thread;

        Context.Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Context.Started = CreateEventW(NULL, TRUE, FALSE, NULL);
        Context.Received = 0;
        Context.Error = 0;
        ok(connect(Context.Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
        Server = accept(Listener, NULL, NULL);
        ok(Server != INVALID_SOCKET, "accept failed with %d\n", WSAGetLastError());

        Thread = CreateThread(NULL, 0, BlockedRecvThread, &Context, 0, &ThreadId);
        ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
        WaitForSingleObject(Context.Started, INFINITE);
        ok(WaitForThreadWaitState(ThreadId, NULL, ANY_WAIT_REASON, 5000), "Iteration %d: recv did not block\n", i);

        ok(closesocket(Context.Client) == 0, "closesocket failed with %d\n", WSAGetLastError());
        for (j = 0; j < SPRAY_COUNT; j++)
        {
            SIZE_T Size = 64 + (j % 32) * 64;

            Spray[j] = HeapAlloc(GetProcessHeap(), 0, Size);
            if (Spray[j])
                memset(Spray[j], 0xFF, Size);
        }

        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "Iteration %d: the blocked recv did not return\n", i);
        ok(Context.Received == SOCKET_ERROR, "Iteration %d: recv returned %d\n", i, Context.Received);
        ok(Context.Error == WSAECONNABORTED, "Iteration %d: recv error %d\n", i, Context.Error);
        if (Context.Received != SOCKET_ERROR || Context.Error != WSAECONNABORTED)
            Failed = 1;

        for (j = 0; j < SPRAY_COUNT; j++)
            HeapFree(GetProcessHeap(), 0, Spray[j]);
        CloseHandle(Thread);
        CloseHandle(Context.Started);
        closesocket(Server);
    }

    closesocket(Listener);
}

typedef struct _SEND_RACE_CONTEXT
{
    SOCKET Listener;
    ULONG Received;
} SEND_RACE_CONTEXT;

static DWORD WINAPI SendRaceServer(PVOID Parameter)
{
    SEND_RACE_CONTEXT *Context = Parameter;
    SOCKET Client = accept(Context->Listener, NULL, NULL);
    char Buffer[1024];
    int Received;

    if (Client == INVALID_SOCKET)
        return 1;
    while ((Received = recv(Client, Buffer, sizeof(Buffer), 0)) > 0)
        Context->Received += Received;
    closesocket(Client);
    return 0;
}

/* A send queued while the last outstanding data is acknowledged must still go out */
static void Test_SendRacesAck(void)
{
    enum { CHUNK = 65536, CHUNKS = 64, TIMEOUT_MS = 5000 };
    SEND_RACE_CONTEXT Context = { INVALID_SOCKET, 0 };
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address), i, Sent = 0;
    DWORD Timeout = TIMEOUT_MS;
    char *Data;
    HANDLE Thread;
    SOCKET Client;

    Data = calloc(1, CHUNK);
    Context.Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Context.Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Context.Listener, 1) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(Context.Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());
    Thread = CreateThread(NULL, 0, SendRaceServer, &Context, 0, NULL);

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(Client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&Timeout, sizeof(Timeout));
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
    for (i = 0; i < CHUNKS; i++)
    {
        int Result = send(Client, Data, CHUNK, 0);

        if (Result != CHUNK)
        {
            ok(0, "Chunk %d: send returned %d, error %d\n", i, Result, WSAGetLastError());
            break;
        }
        Sent += Result;
    }
    shutdown(Client, SD_SEND);
    ok(WaitForSingleObject(Thread, TIMEOUT_MS) == WAIT_OBJECT_0, "The receiver did not see the end of the data\n");
    ok(Context.Received == (ULONG)Sent, "Received %lu of %d bytes\n", Context.Received, Sent);
    closesocket(Client);
    closesocket(Context.Listener);
    CloseHandle(Thread);
    free(Data);
}

/* A peer that connects and closes before accept is posted must read as a closed stream */
static void Test_AcceptAfterPeerClose(void)
{
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address), Received;
    DWORD Timeout = 3000;
    SOCKET Listener, Client, Accepted;
    char Buffer[16];

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
    closesocket(Client);
    Sleep(300);

    Accepted = accept(Listener, NULL, NULL);
    ok(Accepted != INVALID_SOCKET, "accept failed with %d\n", WSAGetLastError());
    setsockopt(Accepted, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
    Received = recv(Accepted, Buffer, sizeof(Buffer), 0);
    ok(Received == 0, "recv returned %d, error %d\n", Received, WSAGetLastError());
    closesocket(Accepted);
    closesocket(Listener);
}

START_TEST(close)
{
    int err;
    WSADATA wdata;

    /* Start up Winsock */
    err = WSAStartup(MAKEWORD(2, 2), &wdata);
    ok(err == 0, "WSAStartup failed, iResult == %d %d\n", err, WSAGetLastError());

    Test_CloseDuplicatedSocket();
    Test_CloseWhileSelectSameSocket();
    Test_CloseWhileSelectDuplicatedSocket();
    Test_CloseWithPendingAsyncSelect();
    Test_RebindAfterCloseWithPendingReceive();
    Test_ReadRacesClose();
    Test_CloseRacesBlockedRecv();
    Test_SendRacesAck();
    Test_AcceptAfterPeerClose();

    WSACleanup();
}

