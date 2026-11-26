/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that WSASendMsg auto-binds an unbound UDP socket before checking lpNumberOfBytesSent
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include "ws2_32.h"
#include <mswsock.h>

START_TEST(WSASendMsgAutoBind)
{
    GUID SendMsgGuid = WSAID_WSASENDMSG;
    struct sockaddr_in TargetAddress;
    struct sockaddr_in LocalAddress;
    LPFN_WSASENDMSG pWSASendMsg = NULL;
    WSADATA WsaData;
    WSABUF Buffer;
    WSAMSG Message;
    SOCKET Socket;
    DWORD Bytes = 0;
    CHAR Payload = 'A';
    int AddressLength;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    Result = WSAIoctl(Socket,
                      SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &SendMsgGuid,
                      sizeof(SendMsgGuid),
                      &pWSASendMsg,
                      sizeof(pWSASendMsg),
                      &Bytes,
                      NULL,
                      NULL);
    ok(Result == 0, "WSAIoctl failed with %d\n", WSAGetLastError());
    ok(pWSASendMsg != NULL, "WSASendMsg pointer is NULL\n");
    ok(Bytes == sizeof(pWSASendMsg), "WSAIoctl returned %lu bytes\n", Bytes);
    if (Result != 0 || !pWSASendMsg)
        goto Cleanup;

    AddressLength = sizeof(LocalAddress);
    ZeroMemory(&LocalAddress, sizeof(LocalAddress));
    WSASetLastError(0xdeadbeef);
    Result = getsockname(Socket, (struct sockaddr *)&LocalAddress, &AddressLength);
    ok(Result == SOCKET_ERROR, "initial getsockname returned %d\n", Result);
    ok(WSAGetLastError() == WSAEINVAL, "initial getsockname error %d, expected WSAEINVAL\n", WSAGetLastError());

    ZeroMemory(&TargetAddress, sizeof(TargetAddress));
    TargetAddress.sin_family = AF_INET;
    TargetAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TargetAddress.sin_port = htons(9);

    Buffer.buf = &Payload;
    Buffer.len = sizeof(Payload);

    ZeroMemory(&Message, sizeof(Message));
    Message.name = (LPSOCKADDR)&TargetAddress;
    Message.namelen = sizeof(TargetAddress);
    Message.lpBuffers = &Buffer;
    Message.dwBufferCount = 1;

    WSASetLastError(0xdeadbeef);
    Result = pWSASendMsg(Socket, &Message, 0, NULL, NULL, NULL);
    ok(Result == SOCKET_ERROR, "WSASendMsg returned %d, expected SOCKET_ERROR\n", Result);
    ok(WSAGetLastError() == WSAEFAULT, "WSASendMsg error %d, expected WSAEFAULT\n", WSAGetLastError());

    AddressLength = sizeof(LocalAddress);
    ZeroMemory(&LocalAddress, sizeof(LocalAddress));
    WSASetLastError(0xdeadbeef);
    Result = getsockname(Socket, (struct sockaddr *)&LocalAddress, &AddressLength);
    ok(Result == 0, "post-WSASendMsg getsockname returned %d, error %d\n", Result, WSAGetLastError());
    ok(AddressLength == sizeof(LocalAddress), "getsockname length %d\n", AddressLength);
    ok(LocalAddress.sin_family == AF_INET, "sin_family %u\n", LocalAddress.sin_family);
    ok(LocalAddress.sin_port != 0, "sin_port is 0 after WSASendMsg\n");

Cleanup:
    closesocket(Socket);
    WSACleanup();
}
