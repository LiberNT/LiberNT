/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test a USB network adapter getting an address and reaching its gateway
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

static
BOOL
IsUsbAdapter(
    _In_ const IP_ADAPTER_ADDRESSES *Adapter)
{
    return wcsstr(Adapter->Description, L"USB") || wcsstr(Adapter->Description, L"RNDIS") ||
           wcsstr(Adapter->Description, L"NCM") || wcsstr(Adapter->Description, L"ECM") ||
           wcsstr(Adapter->Description, L"ASIX") || wcsstr(Adapter->Description, L"SMSC");
}

/* The adapter must have its own IPv4 address and carry a TCP connection to its gateway. */
START_TEST(Network)
{
    PIP_ADAPTER_ADDRESSES Adapters, Adapter;
    ULONG Size = 64 * 1024, Waited;
    WSADATA Wsa;
    BOOL Found = FALSE;

    ok_int(WSAStartup(MAKEWORD(2, 2), &Wsa), 0);
    Adapters = HeapAlloc(GetProcessHeap(), 0, Size);
    for (Waited = 0; Waited < 15000 && !Found; Waited += 500)
    {
        ok_int(GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, Adapters, &Size), ERROR_SUCCESS);
        for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
        {
            if (IsUsbAdapter(Adapter) && Adapter->OperStatus == IfOperStatusUp && Adapter->FirstUnicastAddress &&
                Adapter->FirstGatewayAddress)
            {
                Found = TRUE;
                break;
            }
        }
        if (!Found)
            Sleep(500);
    }
    if (!Found)
    {
        for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
            trace("Adapter %S status %u\n", Adapter->Description, Adapter->OperStatus);
        skip("No USB network adapter has an address and a gateway\n");
    }
    else
    {
        SOCKADDR_IN *Gateway = (SOCKADDR_IN *)Adapter->FirstGatewayAddress->Address.lpSockaddr;
        SOCKADDR_IN *Address = (SOCKADDR_IN *)Adapter->FirstUnicastAddress->Address.lpSockaddr;
        SOCKADDR_IN Target = *Gateway;
        SOCKET Socket;
        CHAR Text[32];
        int Result;

        trace("%S has %s\n", Adapter->Description, inet_ntop(AF_INET, &Address->sin_addr, Text, sizeof(Text)));
        Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(Socket != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
        Target.sin_port = htons(22);
        Result = connect(Socket, (SOCKADDR *)&Target, sizeof(Target));
        ok(Result == 0 || WSAGetLastError() == WSAECONNREFUSED,
           "The gateway %s answered nothing over the USB adapter: %d\n",
           inet_ntop(AF_INET, &Gateway->sin_addr, Text, sizeof(Text)), WSAGetLastError());
        closesocket(Socket);
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
    WSACleanup();
}
