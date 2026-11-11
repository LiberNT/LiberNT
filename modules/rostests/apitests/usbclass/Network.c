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
#include <netioapi.h>

typedef enum _USB_ADAPTER_STATE
{
    NoUsbAdapter,
    UsbAdapterWaiting,
    UsbAdapterReady
} USB_ADAPTER_STATE;

static
BOOL
IsUsbAdapter(
    _In_ const IP_ADAPTER_ADDRESSES *Adapter)
{
    WCHAR Description[256];

    lstrcpynW(Description, Adapter->Description, ARRAYSIZE(Description));
    _wcsupr(Description);
    return wcsstr(Description, L"USB") || wcsstr(Description, L"RNDIS") || wcsstr(Description, L"NCM") ||
           wcsstr(Description, L"ECM") || wcsstr(Description, L"ASIX") || wcsstr(Description, L"SMSC");
}

static
USB_ADAPTER_STATE
FindUsbAdapter(
    _Out_writes_bytes_(Size) PIP_ADAPTER_ADDRESSES Adapters,
    _In_ ULONG Size,
    _Out_ PIP_ADAPTER_ADDRESSES *Ready)
{
    PIP_ADAPTER_ADDRESSES Adapter;
    USB_ADAPTER_STATE State = NoUsbAdapter;
    ULONG Error;

    *Ready = NULL;
    Error = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, Adapters, &Size);
    if (Error != ERROR_SUCCESS)
    {
        ok(0, "GetAdaptersAddresses failed: %lu\n", Error);
        return NoUsbAdapter;
    }
    for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
    {
        if (!IsUsbAdapter(Adapter))
            continue;
        if (Adapter->OperStatus == IfOperStatusUp && Adapter->FirstUnicastAddress && Adapter->FirstGatewayAddress)
        {
            *Ready = Adapter;
            return UsbAdapterReady;
        }
        State = UsbAdapterWaiting;
    }
    return State;
}

static
VOID
NETIOAPI_API_
AddressChanged(
    _In_ PVOID Context,
    _In_opt_ PMIB_UNICASTIPADDRESS_ROW Row,
    _In_ MIB_NOTIFICATION_TYPE Type)
{
    SetEvent(Context);
}

static
VOID
NETIOAPI_API_
RouteChanged(
    _In_ PVOID Context,
    _In_opt_ PMIB_IPFORWARD_ROW2 Row,
    _In_ MIB_NOTIFICATION_TYPE Type)
{
    SetEvent(Context);
}

/* The adapter must have its own IPv4 address and carry a TCP connection to its gateway. */
START_TEST(Network)
{
    PIP_ADAPTER_ADDRESSES Adapters, Adapter;
    HANDLE Changed, AddressNotify = NULL, RouteNotify = NULL;
    ULONG Size = 64 * 1024;
    ULONGLONG Start, Elapsed;
    USB_ADAPTER_STATE State;
    WSADATA Wsa;

    ok_int(WSAStartup(MAKEWORD(2, 2), &Wsa), 0);
    Changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Changed != NULL, "CreateEvent failed: %lu\n", GetLastError());
    ok_int(NotifyUnicastIpAddressChange(AF_INET, AddressChanged, Changed, FALSE, &AddressNotify), NO_ERROR);
    ok_int(NotifyRouteChange2(AF_INET, RouteChanged, Changed, FALSE, &RouteNotify), NO_ERROR);
    Adapters = HeapAlloc(GetProcessHeap(), 0, Size);
    Start = GetTickCount64();
    while ((State = FindUsbAdapter(Adapters, Size, &Adapter)) == UsbAdapterWaiting &&
           (Elapsed = GetTickCount64() - Start) < 15000)
    {
        WaitForSingleObject(Changed, (DWORD)(15000 - Elapsed));
    }
    if (RouteNotify)
        CancelMibChangeNotify2(RouteNotify);
    if (AddressNotify)
        CancelMibChangeNotify2(AddressNotify);
    CloseHandle(Changed);
    if (State == NoUsbAdapter)
    {
        skip("No USB network adapter is present\n");
    }
    else if (State == UsbAdapterWaiting)
    {
        for (Adapter = Adapters; Adapter; Adapter = Adapter->Next)
            trace("Adapter %S status %u\n", Adapter->Description, Adapter->OperStatus);
        skip("No USB network adapter got an address and a gateway\n");
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
