/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Remote Desktop listener switched by fDenyTSConnections
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winsvc.h>

#define TERMINAL_SERVER_KEY L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server"

#define PROTOCOL_RDP       0x00
#define PROTOCOL_SSL       0x01
#define PROTOCOL_HYBRID    0x02
#define PROTOCOL_HYBRID_EX 0x08

#define TYPE_RDP_NEG_REQ     0x01
#define TYPE_RDP_NEG_RSP     0x02
#define TYPE_RDP_NEG_FAILURE 0x03

#define HYBRID_REQUIRED_BY_SERVER 0x05

static SOCKET
ConnectLoopback(int Family)
{
    SOCKADDR_STORAGE Address = { 0 };
    int Length;
    SOCKET Socket;

    if (Family == AF_INET6)
    {
        SOCKADDR_IN6 *Address6 = (SOCKADDR_IN6 *)&Address;
        Address6->sin6_family = AF_INET6;
        Address6->sin6_port = htons(3389);
        Address6->sin6_addr.s6_addr[15] = 1;
        Length = sizeof(*Address6);
    }
    else
    {
        SOCKADDR_IN *Address4 = (SOCKADDR_IN *)&Address;
        Address4->sin_family = AF_INET;
        Address4->sin_port = htons(3389);
        Address4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Length = sizeof(*Address4);
    }

    Socket = socket(Family, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    if (connect(Socket, (SOCKADDR *)&Address, Length) == SOCKET_ERROR)
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static BOOL
WaitForListener(BOOL Expected, int Family)
{
    DWORD Start = GetTickCount();
    SOCKET Socket;
    BOOL Listening;

    do
    {
        Socket = ConnectLoopback(Family);
        Listening = Socket != INVALID_SOCKET;
        if (Listening)
            closesocket(Socket);
        if (Listening == Expected)
            return TRUE;
        Sleep(200);
    } while (GetTickCount() - Start < 15000);

    return FALSE;
}

static BOOL
Negotiate(BYTE Requested, BYTE *Type, DWORD *Value)
{
    BYTE Request[19] = { 3, 0, 0, 19, 14, 0xE0, 0, 0, 0, 0, 0, TYPE_RDP_NEG_REQ, 0, 8, 0, 0, 0, 0, 0 };
    BYTE Response[64];
    int Received = 0, Result;
    SOCKET Socket;

    Request[15] = Requested;
    Socket = ConnectLoopback(AF_INET);
    if (Socket == INVALID_SOCKET)
        return FALSE;
    if (send(Socket, (char *)Request, sizeof(Request), 0) != sizeof(Request))
    {
        closesocket(Socket);
        return FALSE;
    }
    while (Received < 4 || Received < ((Response[2] << 8) | Response[3]))
    {
        Result = recv(Socket, (char *)Response + Received, sizeof(Response) - Received, 0);
        if (Result <= 0)
            break;
        Received += Result;
    }
    closesocket(Socket);

    if (Received != 19 || Response[0] != 3 || Response[5] != 0xD0)
        return FALSE;
    *Type = Response[11];
    *Value = Response[15] | (Response[16] << 8) | (Response[17] << 16) | ((DWORD)Response[18] << 24);
    return TRUE;
}

static BOOL
WaitForNegotiation(void)
{
    DWORD Start = GetTickCount(), Value;
    BYTE Type;

    do
    {
        if (Negotiate(PROTOCOL_SSL | PROTOCOL_HYBRID, &Type, &Value))
            return TRUE;
        Sleep(200);
    } while (GetTickCount() - Start < 15000);

    return FALSE;
}

static void
TestNegotiation(BYTE Requested, BYTE ExpectedType, DWORD ExpectedValue)
{
    BYTE Type = 0;
    DWORD Value = 0;

    ok(Negotiate(Requested, &Type, &Value), "No negotiation response for protocols 0x%x\n", Requested);
    ok(Type == ExpectedType, "Protocols 0x%x: type %u, expected %u\n", Requested, Type, ExpectedType);
    ok(Value == ExpectedValue, "Protocols 0x%x: value 0x%lx, expected 0x%lx\n", Requested, Value, ExpectedValue);
}

static BOOL
ServiceInstalled(PCWSTR Name)
{
    SC_HANDLE Manager, Service;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!Manager)
        return FALSE;
    Service = OpenServiceW(Manager, Name, SERVICE_QUERY_STATUS);
    if (Service)
        CloseServiceHandle(Service);
    CloseServiceHandle(Manager);
    return Service != NULL;
}

START_TEST(RemoteDesktop)
{
    DWORD Original = 1, Size = sizeof(Original), Type, Value;
    WSADATA Data;
    LONG Error;
    HKEY Key;

    if (!ServiceInstalled(L"TermService") && !ServiceInstalled(L"LiberNTRDPServer"))
    {
        skip("No Remote Desktop server is installed\n");
        return;
    }

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, TERMINAL_SERVER_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("Changing fDenyTSConnections needs an administrator\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW failed with %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(Key, L"fDenyTSConnections", NULL, &Type, (BYTE *)&Original, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        Original = 1;
    }
    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");

    Value = 1;
    ok(RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Value, sizeof(Value)) == ERROR_SUCCESS,
       "Cannot turn Remote Desktop off\n");
    ok(WaitForListener(FALSE, AF_INET), "Port 3389 still accepts connections with Remote Desktop off\n");

    Value = 0;
    ok(RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Value, sizeof(Value)) == ERROR_SUCCESS,
       "Cannot turn Remote Desktop on\n");
    ok(WaitForListener(TRUE, AF_INET), "Port 3389 of 127.0.0.1 does not listen with Remote Desktop on\n");
    ok(WaitForListener(TRUE, AF_INET6), "Port 3389 of ::1 does not listen with Remote Desktop on\n");
    /* Windows 11 resets connections for a moment after the listener appears. */
    ok(WaitForNegotiation(), "Port 3389 does not answer the negotiation with Remote Desktop on\n");

    TestNegotiation(PROTOCOL_SSL | PROTOCOL_HYBRID, TYPE_RDP_NEG_RSP, PROTOCOL_HYBRID);
    TestNegotiation(PROTOCOL_HYBRID, TYPE_RDP_NEG_RSP, PROTOCOL_HYBRID);
    TestNegotiation(PROTOCOL_SSL | PROTOCOL_HYBRID | PROTOCOL_HYBRID_EX, TYPE_RDP_NEG_RSP, PROTOCOL_HYBRID_EX);
    TestNegotiation(PROTOCOL_SSL, TYPE_RDP_NEG_FAILURE, HYBRID_REQUIRED_BY_SERVER);
    TestNegotiation(PROTOCOL_RDP, TYPE_RDP_NEG_FAILURE, HYBRID_REQUIRED_BY_SERVER);

    Value = 1;
    ok(RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Value, sizeof(Value)) == ERROR_SUCCESS,
       "Cannot turn Remote Desktop off\n");
    ok(WaitForListener(FALSE, AF_INET), "Port 3389 still accepts connections after Remote Desktop was turned off\n");

    RegSetValueExW(Key, L"fDenyTSConnections", 0, REG_DWORD, (BYTE *)&Original, sizeof(Original));
    RegCloseKey(Key);
    WSACleanup();
}
