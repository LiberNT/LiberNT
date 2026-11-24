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
#include <icmpapi.h>
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

#define BURST_THREADS   8
#define BURST_ECHOES    64
#define BURST_TIMEOUT   1000

typedef struct _BURST_CONTEXT
{
    IPAddr Target;
    volatile LONG Lost;
    volatile LONG Slow;
} BURST_CONTEXT;

static
DWORD
WINAPI
BurstThread(
    _In_ PVOID Parameter)
{
    BURST_CONTEXT *Context = Parameter;
    UCHAR Request[32] = {0};
    UCHAR Reply[sizeof(ICMP_ECHO_REPLY) + sizeof(Request) + 8];
    HANDLE Icmp = IcmpCreateFile();
    ULONG i;

    if (Icmp == INVALID_HANDLE_VALUE)
    {
        InterlockedAdd(&Context->Lost, BURST_ECHOES);
        return 0;
    }
    for (i = 0; i < BURST_ECHOES; i++)
    {
        DWORD Count = IcmpSendEcho(Icmp, Context->Target, Request, sizeof(Request), NULL, Reply, sizeof(Reply),
                                   BURST_TIMEOUT);
        PICMP_ECHO_REPLY Echo = (PICMP_ECHO_REPLY)Reply;

        if (Count == 0 || Echo->Status != IP_SUCCESS)
            InterlockedIncrement(&Context->Lost);
        else if (Echo->RoundTripTime > BURST_TIMEOUT / 2)
            InterlockedIncrement(&Context->Slow);
    }
    IcmpCloseHandle(Icmp);
    return 0;
}

/* Concurrent sends overlap TX completions: a packet queued behind a busy transmitter must still go out. */
static
VOID
TestConcurrentEchoes(
    _In_ IPAddr Target)
{
    BURST_CONTEXT Context = {Target, 0, 0};
    HANDLE Threads[BURST_THREADS];
    UCHAR Request[32] = {0};
    UCHAR Reply[sizeof(ICMP_ECHO_REPLY) + sizeof(Request) + 8];
    HANDLE Icmp = IcmpCreateFile();
    ULONG i;

    ok(Icmp != INVALID_HANDLE_VALUE, "IcmpCreateFile failed: %lu\n", GetLastError());
    if (Icmp == INVALID_HANDLE_VALUE)
        return;
    i = IcmpSendEcho(Icmp, Target, Request, sizeof(Request), NULL, Reply, sizeof(Reply), BURST_TIMEOUT);
    IcmpCloseHandle(Icmp);
    if (i == 0)
    {
        skip("The gateway does not answer ICMP echo\n");
        return;
    }
    for (i = 0; i < BURST_THREADS; i++)
    {
        Threads[i] = CreateThread(NULL, 0, BurstThread, &Context, 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    for (i = 0; i < BURST_THREADS; i++)
    {
        if (Threads[i])
        {
            WaitForSingleObject(Threads[i], INFINITE);
            CloseHandle(Threads[i]);
        }
    }
    ok_int(Context.Lost, 0);
    ok_int(Context.Slow, 0);
}

/* The adapter must have its own IPv4 address and carry a TCP connection and concurrent echoes to its gateway. */
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
        TestConcurrentEchoes(Gateway->sin_addr.S_un.S_addr);
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
    WSACleanup();
}
