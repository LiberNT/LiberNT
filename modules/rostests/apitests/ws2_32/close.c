/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for WSASocket
 * PROGRAMMERS:     Peter Hater
 */

#include "ws2_32.h"
#include <winuser.h>

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

// 250 ms
#define TIME_SLEEP1 250

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

    hthread = CreateThread(NULL, 0, thread_proc, (void*)sock, 0, NULL);
    ok(hthread != NULL, "CreateThread %ld\n", GetLastError());

    Sleep(TIME_SLEEP1);
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

    hthread = CreateThread(NULL, 0, thread_proc, (void*)dup_sock, 0, NULL);
    ok(hthread != NULL, "CreateThread %ld\n", GetLastError());

    err = closesocket(sock);
    ok(err == 0, "closesocket err = %d %d\n", err, WSAGetLastError());

    Sleep(TIME_SLEEP1);
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

    WSACleanup();
}

