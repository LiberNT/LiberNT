/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests SIO_ADDRESS_LIST_SORT destination address ordering
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

typedef struct _SORT_BUFFER
{
    SOCKET_ADDRESS_LIST List;
    SOCKET_ADDRESS Extra[3];
    SOCKADDR_IN6 Addresses[4];
} SORT_BUFFER;

static void
SetAddress(
    _Out_ SOCKADDR_IN6 *Address,
    _In_reads_(16) const UCHAR *Bytes)
{
    ZeroMemory(Address, sizeof(*Address));
    Address->sin6_family = AF_INET6;
    memcpy(&Address->sin6_addr, Bytes, 16);
}

static void
BuildList(
    _Out_ SORT_BUFFER *Buffer,
    _In_ INT Count,
    _In_reads_(Count) const SOCKADDR_IN6 *Addresses)
{
    INT Index;

    ZeroMemory(Buffer, sizeof(*Buffer));
    Buffer->List.iAddressCount = Count;
    for (Index = 0; Index < Count; Index++)
    {
        Buffer->Addresses[Index] = Addresses[Index];
        Buffer->List.Address[Index].lpSockaddr = (LPSOCKADDR)&Buffer->Addresses[Index];
        Buffer->List.Address[Index].iSockaddrLength = sizeof(SOCKADDR_IN6);
    }
}

START_TEST(addresslistsort)
{
    static const UCHAR Loopback6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    static const UCHAR Loopback4Mapped[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1 };
    SOCKADDR_IN6 Addresses[2];
    SORT_BUFFER Input, Output;
    SOCKADDR_IN6 *First, *Second;
    DWORD Returned = 0;
    WSADATA WsaData;
    SOCKET Socket;
    int Result;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    Socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket == INVALID_SOCKET)
    {
        skip("No IPv6 socket, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    SetAddress(&Addresses[0], Loopback4Mapped);
    SetAddress(&Addresses[1], Loopback6);
    BuildList(&Input, 2, Addresses);
    ZeroMemory(&Output, sizeof(Output));

    Result = WSAIoctl(Socket, SIO_ADDRESS_LIST_SORT, &Input, sizeof(Input),
                      &Output, sizeof(Output), &Returned, NULL, NULL);
    ok(Result == 0, "WSAIoctl failed, error %d\n", WSAGetLastError());
    if (Result == 0)
    {
        ok(Output.List.iAddressCount == 2, "Got %d addresses\n", Output.List.iAddressCount);
        if (Output.List.iAddressCount == 2)
        {
            First = (SOCKADDR_IN6 *)Output.List.Address[0].lpSockaddr;
            Second = (SOCKADDR_IN6 *)Output.List.Address[1].lpSockaddr;
            ok(First->sin6_family == AF_INET6 && Second->sin6_family == AF_INET6, "Unexpected families\n");
            ok(!memcmp(&First->sin6_addr, Loopback6, 16), "::1 is not sorted first\n");
            ok(!memcmp(&Second->sin6_addr, Loopback4Mapped, 16), "::ffff:127.0.0.1 is not sorted second\n");
        }
    }

    closesocket(Socket);
    WSACleanup();
}
