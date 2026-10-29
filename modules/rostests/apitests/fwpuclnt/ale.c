/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for filters at the ALE authorization layers
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <rpc.h>
#include <initguid.h>
#include <fwpmu.h>

static HANDLE Engine;

static
UINT64
AddBlock(
    _In_ const GUID *Layer,
    _In_ const GUID *PortField,
    _In_ USHORT Port,
    _In_opt_ FWP_BYTE_BLOB *AppId)
{
    FWPM_FILTER_CONDITION0 Conditions[2];
    FWPM_FILTER0 Filter;
    UINT64 Id = 0;
    DWORD Error;

    ZeroMemory(&Filter, sizeof(Filter));
    ZeroMemory(Conditions, sizeof(Conditions));
    Filter.layerKey = *Layer;
    Filter.displayData.name = L"LiberNT ALE apitest filter";
    Filter.action.type = FWP_ACTION_BLOCK;
    Filter.weight.type = FWP_EMPTY;
    Filter.filterCondition = Conditions;
    Conditions[0].fieldKey = *PortField;
    Conditions[0].matchType = FWP_MATCH_EQUAL;
    Conditions[0].conditionValue.type = FWP_UINT16;
    Conditions[0].conditionValue.uint16 = Port;
    Filter.numFilterConditions = 1;
    if (AppId != NULL)
    {
        Conditions[1].fieldKey = FWPM_CONDITION_ALE_APP_ID;
        Conditions[1].matchType = FWP_MATCH_EQUAL;
        Conditions[1].conditionValue.type = FWP_BYTE_BLOB_TYPE;
        Conditions[1].conditionValue.byteBlob = AppId;
        Filter.numFilterConditions = 2;
    }

    Error = FwpmFilterAdd0(Engine, &Filter, NULL, &Id);
    ok(Error == ERROR_SUCCESS, "FwpmFilterAdd0 returned 0x%lx\n", Error);
    return Id;
}

static
VOID
DeleteFilter(
    _In_ UINT64 Id)
{
    DWORD Error = FwpmFilterDeleteById0(Engine, Id);

    ok(Error == ERROR_SUCCESS, "FwpmFilterDeleteById0 returned 0x%lx\n", Error);
}

static
struct sockaddr_in
Loopback(
    _In_ USHORT Port)
{
    struct sockaddr_in Address;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = htons(Port);
    return Address;
}

static
int
Bind(
    _In_ SOCKET Socket,
    _In_ ULONG Address,
    _In_ USHORT Port)
{
    struct sockaddr_in Local = Loopback(Port);

    Local.sin_addr.s_addr = htonl(Address);
    return bind(Socket, (struct sockaddr *)&Local, sizeof(Local));
}

static
int
Connect(
    _In_ SOCKET Socket,
    _In_ USHORT Port)
{
    struct sockaddr_in Remote = Loopback(Port);

    return connect(Socket, (struct sockaddr *)&Remote, sizeof(Remote));
}

static
BOOL
Ready(
    _In_ SOCKET Socket,
    _In_ BOOL Write,
    _In_ LONG Milliseconds)
{
    struct timeval Timeout;
    fd_set Set;

    FD_ZERO(&Set);
    FD_SET(Socket, &Set);
    Timeout.tv_sec = Milliseconds / 1000;
    Timeout.tv_usec = (Milliseconds % 1000) * 1000;
    return select(0, Write ? NULL : &Set, Write ? &Set : NULL, NULL, &Timeout) == 1;
}

static
SOCKET
Listen(
    _In_ USHORT Port)
{
    SOCKET Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ok(Bind(Listener, INADDR_LOOPBACK, Port) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == 0, "listen failed: %d\n", WSAGetLastError());
    return Listener;
}

static
VOID
ExpectAccepted(
    _In_ SOCKET Listener,
    _In_ USHORT Port)
{
    SOCKET Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), Server = INVALID_SOCKET;

    ok(Connect(Client, Port) == 0, "connect to %u failed: %d\n", Port, WSAGetLastError());
    if (Ready(Listener, FALSE, 3000))
    {
        Server = accept(Listener, NULL, NULL);
    }
    ok(Server != INVALID_SOCKET, "No connection accepted on %u\n", Port);
    if (Server != INVALID_SOCKET)
    {
        closesocket(Server);
    }
    closesocket(Client);
}

static
VOID
ExpectSynDropped(
    _In_ SOCKET Listener,
    _In_ USHORT Port)
{
    SOCKET Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long NonBlocking = 1;
    int Result;

    ioctlsocket(Client, FIONBIO, &NonBlocking);
    Result = Connect(Client, Port);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK,
       "connect returned %d, error %d\n", Result, WSAGetLastError());
    ok(!Ready(Client, TRUE, 2000), "Connection to %u completed\n", Port);
    ok(!Ready(Listener, FALSE, 0), "Connection to %u was queued for accept\n", Port);
    closesocket(Client);
}

static
VOID
TestBind(VOID)
{
    SOCKET Tcp, Udp;
    UINT64 Id;

    Id = AddBlock(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, 47101, NULL);

    Tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Tcp, INADDR_LOOPBACK, 47101) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "TCP bind error %d\n", WSAGetLastError());
    ok(Bind(Tcp, INADDR_LOOPBACK, 47102) == 0, "TCP bind to another port failed: %d\n", WSAGetLastError());
    closesocket(Tcp);

    Udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Bind(Udp, INADDR_ANY, 47101) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "UDP wildcard bind error %d\n", WSAGetLastError());
    closesocket(Udp);

    DeleteFilter(Id);

    Tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Tcp, INADDR_LOOPBACK, 47101) == 0, "TCP bind after delete failed: %d\n", WSAGetLastError());
    closesocket(Tcp);
}

static
VOID
TestListen(VOID)
{
    SOCKET Listener;
    UINT64 Id;

    Id = AddBlock(&FWPM_LAYER_ALE_AUTH_LISTEN_V4, &FWPM_CONDITION_IP_LOCAL_PORT, 47111, NULL);

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Listener, INADDR_LOOPBACK, 47111) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "listen error %d\n", WSAGetLastError());
    closesocket(Listener);

    DeleteFilter(Id);

    Listener = Listen(47111);
    closesocket(Listener);
}

static
VOID
TestConnect(VOID)
{
    SOCKET Listener, Client;
    UINT64 Id;

    Listener = Listen(47121);
    Id = AddBlock(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &FWPM_CONDITION_IP_REMOTE_PORT, 47121, NULL);

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Connect(Client, 47121) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "connect error %d\n", WSAGetLastError());
    closesocket(Client);
    ok(!Ready(Listener, FALSE, 500), "Blocked connection was queued for accept\n");

    DeleteFilter(Id);

    ExpectAccepted(Listener, 47121);
    closesocket(Listener);
}

static
VOID
TestRecvAccept(VOID)
{
    WCHAR Own[MAX_PATH], Other[MAX_PATH];
    FWP_BYTE_BLOB *OwnId = NULL, *OtherId = NULL;
    SOCKET Listener;
    UINT64 Id;

    Listener = Listen(47131);
    Id = AddBlock(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, 47131, NULL);
    ExpectSynDropped(Listener, 47131);
    DeleteFilter(Id);
    ExpectAccepted(Listener, 47131);
    closesocket(Listener);

    GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));
    GetSystemDirectoryW(Other, ARRAYSIZE(Other));
    lstrcatW(Other, L"\\cmd.exe");
    ok(FwpmGetAppIdFromFileName0(Own, &OwnId) == ERROR_SUCCESS, "No app id for %ls\n", Own);
    ok(FwpmGetAppIdFromFileName0(Other, &OtherId) == ERROR_SUCCESS, "No app id for %ls\n", Other);
    if (OwnId == NULL || OtherId == NULL)
    {
        return;
    }

    Listener = Listen(47141);
    Id = AddBlock(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, 47141, OtherId);
    ExpectAccepted(Listener, 47141);
    DeleteFilter(Id);
    Id = AddBlock(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, 47141, OwnId);
    ExpectSynDropped(Listener, 47141);
    DeleteFilter(Id);
    closesocket(Listener);

    FwpmFreeMemory0((void **)&OwnId);
    FwpmFreeMemory0((void **)&OtherId);
}

static
VOID
TestDatagram(
    _In_ BOOL Outbound,
    _In_ USHORT Port)
{
    struct sockaddr_in Remote = Loopback(Port);
    SOCKET Receiver, Sender;
    char Buffer[16];
    UINT64 Id;
    int Result;

    Receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Bind(Receiver, INADDR_LOOPBACK, Port) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(Bind(Sender, INADDR_LOOPBACK, 0) == 0, "bind failed: %d\n", WSAGetLastError());

    if (Outbound)
        Id = AddBlock(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &FWPM_CONDITION_IP_REMOTE_PORT, Port, NULL);
    else
        Id = AddBlock(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, Port, NULL);

    Result = sendto(Sender, "first", 5, 0, (struct sockaddr *)&Remote, sizeof(Remote));
    trace("%s: blocked sendto returned %d, error %d\n", Outbound ? "connect" : "recv-accept", Result, WSAGetLastError());
    ok(!Ready(Receiver, FALSE, 1000), "Blocked datagram to %u arrived\n", Port);

    DeleteFilter(Id);

    Result = sendto(Sender, "second", 6, 0, (struct sockaddr *)&Remote, sizeof(Remote));
    ok(Result == 6, "sendto returned %d, error %d\n", Result, WSAGetLastError());
    Result = Ready(Receiver, FALSE, 2000) ? recv(Receiver, Buffer, sizeof(Buffer), 0) : -1;
    ok(Result == 6 && !memcmp(Buffer, "second", 6), "Received %d bytes after the filter was deleted\n", Result);

    closesocket(Sender);
    closesocket(Receiver);
}

START_TEST(ale)
{
    FWPM_SESSION0 Session;
    WSADATA WsaData;
    DWORD Error;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    ZeroMemory(&Session, sizeof(Session));
    Session.displayData.name = L"LiberNT ALE apitest session";
    Session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    Error = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &Engine);
    if (Error != ERROR_SUCCESS)
    {
        skip("FwpmEngineOpen0 returned 0x%lx; the test needs administrator rights\n", Error);
        WSACleanup();
        return;
    }

    TestBind();
    TestListen();
    TestConnect();
    TestRecvAccept();
    TestDatagram(FALSE, 47151);
    TestDatagram(TRUE, 47152);

    FwpmEngineClose0(Engine);
    WSACleanup();
}
