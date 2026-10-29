/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for filters at the ALE authorization layers
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
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
    _In_ USHORT Port,
    _In_ BOOL ByApp)
{
    struct sockaddr_in Remote = Loopback(Port);
    FWP_BYTE_BLOB *AppId = NULL;
    WCHAR Own[MAX_PATH];
    SOCKET Receiver, Sender;
    char Buffer[16];
    UINT64 Id;
    int Result;

    if (ByApp)
    {
        GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));
        ok(FwpmGetAppIdFromFileName0(Own, &AppId) == ERROR_SUCCESS, "No app id for %ls\n", Own);
        if (AppId == NULL)
        {
            return;
        }
    }

    Receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Bind(Receiver, INADDR_LOOPBACK, Port) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(Bind(Sender, INADDR_LOOPBACK, 0) == 0, "bind failed: %d\n", WSAGetLastError());

    if (Outbound)
        Id = AddBlock(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &FWPM_CONDITION_IP_REMOTE_PORT, Port, AppId);
    else
        Id = AddBlock(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_CONDITION_IP_LOCAL_PORT, Port, AppId);

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
    if (AppId != NULL)
        FwpmFreeMemory0((void **)&AppId);
}

static
UINT64
AddBlockOn(
    _In_ const GUID *Layer,
    _In_ FWPM_FILTER_CONDITION0 *Conditions,
    _In_ UINT32 Count)
{
    FWPM_FILTER0 Filter;
    UINT64 Id = 0;
    DWORD Error;

    ZeroMemory(&Filter, sizeof(Filter));
    Filter.layerKey = *Layer;
    Filter.displayData.name = L"LiberNT ALE apitest probe filter";
    Filter.action.type = FWP_ACTION_BLOCK;
    Filter.weight.type = FWP_EMPTY;
    Filter.filterCondition = Conditions;
    Filter.numFilterConditions = Count;
    Error = FwpmFilterAdd0(Engine, &Filter, NULL, &Id);
    ok(Error == ERROR_SUCCESS, "FwpmFilterAdd0 returned 0x%lx\n", Error);
    return Id;
}

static
VOID
Cond16(
    _Out_ FWPM_FILTER_CONDITION0 *Condition,
    _In_ const GUID *Field,
    _In_ UINT16 Value)
{
    ZeroMemory(Condition, sizeof(*Condition));
    Condition->fieldKey = *Field;
    Condition->matchType = FWP_MATCH_EQUAL;
    Condition->conditionValue.type = FWP_UINT16;
    Condition->conditionValue.uint16 = Value;
}

static
VOID
Cond32(
    _Out_ FWPM_FILTER_CONDITION0 *Condition,
    _In_ const GUID *Field,
    _In_ FWP_MATCH_TYPE Match,
    _In_ UINT32 Value)
{
    ZeroMemory(Condition, sizeof(*Condition));
    Condition->fieldKey = *Field;
    Condition->matchType = Match;
    Condition->conditionValue.type = FWP_UINT32;
    Condition->conditionValue.uint32 = Value;
}

static
int
SoError(
    _In_ SOCKET Socket)
{
    int Error = -1, Length = sizeof(Error);

    getsockopt(Socket, SOL_SOCKET, SO_ERROR, (char *)&Error, &Length);
    return Error;
}

static
VOID
TestReuse(VOID)
{
    FWPM_FILTER_CONDITION0 Condition;
    SOCKET Listener, Client, Server;
    UINT64 Id;

    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47201);
    Id = AddBlockOn(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, &Condition, 1);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Client, INADDR_LOOPBACK, 47201) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "bind error %d\n", WSAGetLastError());
    DeleteFilter(Id);
    ok(Bind(Client, INADDR_LOOPBACK, 47201) == 0, "bind retry failed: %d\n", WSAGetLastError());
    closesocket(Client);

    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47211);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_LISTEN_V4, &Condition, 1);
    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Listener, INADDR_LOOPBACK, 47211) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "listen error %d\n", WSAGetLastError());
    DeleteFilter(Id);
    ok(listen(Listener, 4) == 0, "listen retry failed: %d\n", WSAGetLastError());
    ExpectAccepted(Listener, 47211);
    closesocket(Listener);

    Listener = Listen(47221);
    Cond16(&Condition, &FWPM_CONDITION_IP_REMOTE_PORT, 47221);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &Condition, 1);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Connect(Client, 47221) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "connect error %d\n", WSAGetLastError());
    DeleteFilter(Id);
    ok(Connect(Client, 47221) == 0, "connect retry failed: %d\n", WSAGetLastError());
    Server = Ready(Listener, FALSE, 3000) ? accept(Listener, NULL, NULL) : INVALID_SOCKET;
    ok(Server != INVALID_SOCKET, "No connection accepted\n");
    if (Server != INVALID_SOCKET)
        closesocket(Server);
    closesocket(Client);
    closesocket(Listener);
}

static
VOID
TestNonBlockingConnect(VOID)
{
    FWPM_FILTER_CONDITION0 Condition;
    struct timeval Timeout = { 2, 0 };
    u_long NonBlocking = 1;
    SOCKET Listener, Client;
    fd_set Write, Except;
    UINT64 Id;
    int Result;

    Listener = Listen(47222);
    Cond16(&Condition, &FWPM_CONDITION_IP_REMOTE_PORT, 47222);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &Condition, 1);

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(Client, FIONBIO, &NonBlocking);
    Result = Connect(Client, 47222);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK,
       "connect returned %d, error %d\n", Result, WSAGetLastError());
    FD_ZERO(&Write);
    FD_SET(Client, &Write);
    FD_ZERO(&Except);
    FD_SET(Client, &Except);
    Result = select(0, NULL, &Write, &Except, &Timeout);
    ok(Result == 1 && !FD_ISSET(Client, &Write) && FD_ISSET(Client, &Except),
       "select returned %d, write %d, except %d\n", Result, FD_ISSET(Client, &Write) != 0, FD_ISSET(Client, &Except) != 0);
    ok_int(SoError(Client), WSAEACCES);

    closesocket(Client);
    DeleteFilter(Id);
    closesocket(Listener);
}

static
VOID
TestConditions(VOID)
{
    FWPM_FILTER_CONDITION0 C[2];
    SOCKET Listener, Socket;
    UINT64 Id;

    Listener = Listen(47231);

    Cond32(&C[0], &FWPM_CONDITION_IP_REMOTE_ADDRESS, FWP_MATCH_EQUAL, INADDR_LOOPBACK);
    Cond16(&C[1], &FWPM_CONDITION_IP_REMOTE_PORT, 47231);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, C, 2);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Connect(Socket, 47231) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "connect with a remote address block: error %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);

    Cond32(&C[0], &FWPM_CONDITION_FLAGS, FWP_MATCH_FLAGS_ALL_SET, FWP_CONDITION_FLAG_IS_LOOPBACK);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, C, 2);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Connect(Socket, 47231) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "connect with a loopback flag block: error %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);

    Cond32(&C[0], &FWPM_CONDITION_IP_REMOTE_ADDRESS, FWP_MATCH_EQUAL, INADDR_LOOPBACK);
    Cond16(&C[1], &FWPM_CONDITION_IP_LOCAL_PORT, 47231);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, C, 2);
    ExpectSynDropped(Listener, 47231);
    DeleteFilter(Id);

    Cond32(&C[0], &FWPM_CONDITION_FLAGS, FWP_MATCH_FLAGS_ALL_SET, FWP_CONDITION_FLAG_IS_LOOPBACK);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, C, 2);
    ExpectSynDropped(Listener, 47231);
    DeleteFilter(Id);
    closesocket(Listener);

    Cond32(&C[0], &FWPM_CONDITION_IP_LOCAL_ADDRESS, FWP_MATCH_EQUAL, INADDR_LOOPBACK);
    Cond16(&C[1], &FWPM_CONDITION_IP_LOCAL_PORT, 47241);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_LISTEN_V4, C, 2);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Socket, INADDR_LOOPBACK, 47241) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Socket, 4) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "listen with a local address block: error %d\n", WSAGetLastError());
    closesocket(Socket);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Bind(Socket, INADDR_ANY, 47241) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Socket, 4) == 0, "wildcard listen with a local address block failed: %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);
}

static
VOID
TestBindFields(VOID)
{
    static const struct
    {
        UCHAR Protocol;
        ULONG Address;
        USHORT Port;
        BOOL WildcardFlag;
        BOOL ZeroAddress;
    } Binds[] =
    {
        { IPPROTO_UDP, INADDR_ANY, 47253, FALSE, FALSE },
        { IPPROTO_UDP, INADDR_ANY, 0, TRUE, TRUE },
        { IPPROTO_UDP, INADDR_LOOPBACK, 0, TRUE, FALSE },
        { IPPROTO_TCP, INADDR_ANY, 47254, FALSE, FALSE },
        { IPPROTO_TCP, INADDR_ANY, 0, TRUE, TRUE },
        { IPPROTO_TCP, INADDR_LOOPBACK, 0, TRUE, FALSE },
    };
    FWPM_FILTER_CONDITION0 C[2];
    FWP_BYTE_BLOB *OwnId = NULL;
    WCHAR Own[MAX_PATH];
    SOCKET Socket;
    UINT64 Id;
    UINT i, Field;
    int Result;

    GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));
    ok(FwpmGetAppIdFromFileName0(Own, &OwnId) == ERROR_SUCCESS, "No app id for %ls\n", Own);
    if (OwnId == NULL)
    {
        return;
    }

    ZeroMemory(&C[1], sizeof(C[1]));
    C[1].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    C[1].matchType = FWP_MATCH_EQUAL;
    C[1].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    C[1].conditionValue.byteBlob = OwnId;

    for (i = 0; i < ARRAYSIZE(Binds); i++)
    {
        for (Field = 0; Field < 3; Field++)
        {
            BOOL Blocked = Field == 0 ? Binds[i].WildcardFlag : Field == 1 ? Binds[i].ZeroAddress : FALSE;

            if (Field == 0)
                Cond32(&C[0], &FWPM_CONDITION_FLAGS, FWP_MATCH_FLAGS_ALL_SET, FWP_CONDITION_FLAG_IS_WILDCARD_BIND);
            else if (Field == 1)
                Cond32(&C[0], &FWPM_CONDITION_IP_LOCAL_ADDRESS, FWP_MATCH_EQUAL, 0);
            else
                Cond16(&C[0], &FWPM_CONDITION_IP_LOCAL_PORT, 0);
            Id = AddBlockOn(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, C, 2);
            Socket = socket(AF_INET, Binds[i].Protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, Binds[i].Protocol);
            Result = Bind(Socket, Binds[i].Address, Binds[i].Port);
            ok(Blocked ? (Result == SOCKET_ERROR && WSAGetLastError() == WSAEACCES) : Result == 0,
               "Field %u, protocol %u, address %lx, port %u: bind returned %d, error %d\n",
               Field, Binds[i].Protocol, Binds[i].Address, Binds[i].Port, Result, WSAGetLastError());
            closesocket(Socket);
            DeleteFilter(Id);
        }
    }

    FwpmFreeMemory0((void **)&OwnId);
}

static
VOID
TestImplicitBind(VOID)
{
    struct sockaddr_in Remote = Loopback(47262);
    FWPM_FILTER_CONDITION0 C[2];
    FWP_BYTE_BLOB *OwnId = NULL;
    WCHAR Own[MAX_PATH];
    SOCKET Listener, Socket;
    UINT64 Id;
    int Result;

    GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));
    ok(FwpmGetAppIdFromFileName0(Own, &OwnId) == ERROR_SUCCESS, "No app id for %ls\n", Own);
    if (OwnId == NULL)
    {
        return;
    }

    Listener = Listen(47261);
    ZeroMemory(C, sizeof(C));
    C[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    C[0].matchType = FWP_MATCH_EQUAL;
    C[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    C[0].conditionValue.byteBlob = OwnId;
    C[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
    C[1].matchType = FWP_MATCH_EQUAL;
    C[1].conditionValue.type = FWP_UINT8;
    C[1].conditionValue.uint8 = IPPROTO_TCP;
    Id = AddBlockOn(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, C, 2);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Connect(Socket, 47261) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "connect with a blocked implicit bind: error %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);
    closesocket(Listener);

    C[1].conditionValue.uint8 = IPPROTO_UDP;
    Id = AddBlockOn(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, C, 2);
    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Result = sendto(Socket, "x", 1, 0, (struct sockaddr *)&Remote, sizeof(Remote));
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL,
       "sendto with a blocked implicit bind returned %d, error %d\n", Result, WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);

    FwpmFreeMemory0((void **)&OwnId);
}

static
SOCKET
DualStack(
    _In_ int Type,
    _In_ USHORT Port,
    _Out_ int *BindResult)
{
    SOCKET Socket = socket(AF_INET6, Type, Type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
    struct sockaddr_in6 Local;
    DWORD Off = 0;

    ok(setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&Off, sizeof(Off)) == 0,
       "IPV6_V6ONLY failed: %d\n", WSAGetLastError());
    ZeroMemory(&Local, sizeof(Local));
    Local.sin6_family = AF_INET6;
    Local.sin6_port = htons(Port);
    *BindResult = bind(Socket, (struct sockaddr *)&Local, sizeof(Local));
    return Socket;
}

static
struct sockaddr_in6
MappedLoopback(
    _In_ USHORT Port)
{
    struct sockaddr_in6 Address;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin6_family = AF_INET6;
    Address.sin6_port = htons(Port);
    Address.sin6_addr.s6_addr[10] = 0xff;
    Address.sin6_addr.s6_addr[11] = 0xff;
    Address.sin6_addr.s6_addr[12] = 127;
    Address.sin6_addr.s6_addr[15] = 1;
    return Address;
}

static
VOID
TestDualStack(VOID)
{
    FWPM_FILTER_CONDITION0 Condition;
    struct sockaddr_in6 Mapped;
    struct sockaddr_in Remote;
    SOCKET Listener, Socket, Receiver;
    u_long NonBlocking = 1;
    char Buffer[8];
    UINT64 Id;
    int Result;

    Listener = Listen(47271);
    Cond16(&Condition, &FWPM_CONDITION_IP_REMOTE_PORT, 47271);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &Condition, 1);
    Socket = DualStack(SOCK_STREAM, 0, &Result);
    ok(Result == 0, "bind failed: %d\n", WSAGetLastError());
    Mapped = MappedLoopback(47271);
    ok(connect(Socket, (struct sockaddr *)&Mapped, sizeof(Mapped)) == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
       "Dual-stack connect error %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);
    closesocket(Listener);

    Receiver = DualStack(SOCK_DGRAM, 47273, &Result);
    ok(Result == 0, "bind failed: %d\n", WSAGetLastError());
    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47273);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &Condition, 1);
    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Remote = Loopback(47273);
    ok(sendto(Socket, "dual", 4, 0, (struct sockaddr *)&Remote, sizeof(Remote)) == 4, "sendto failed: %d\n", WSAGetLastError());
    ok(!Ready(Receiver, FALSE, 1000), "Blocked datagram reached the dual-stack socket\n");
    DeleteFilter(Id);
    ok(sendto(Socket, "dual", 4, 0, (struct sockaddr *)&Remote, sizeof(Remote)) == 4, "sendto failed: %d\n", WSAGetLastError());
    ok(Ready(Receiver, FALSE, 2000) && recv(Receiver, Buffer, sizeof(Buffer), 0) == 4,
       "No datagram after the filter was deleted\n");
    closesocket(Socket);
    closesocket(Receiver);

    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47274);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_LISTEN_V4, &Condition, 1);
    Socket = DualStack(SOCK_STREAM, 47274, &Result);
    ok(Result == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Socket, 4) == 0, "Dual-stack listen failed: %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);

    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47275);
    Id = AddBlockOn(&FWPM_LAYER_ALE_RESOURCE_ASSIGNMENT_V4, &Condition, 1);
    Socket = DualStack(SOCK_STREAM, 47275, &Result);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEACCES, "Dual-stack TCP bind error %d\n", WSAGetLastError());
    closesocket(Socket);
    Socket = DualStack(SOCK_DGRAM, 47275, &Result);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEACCES, "Dual-stack UDP bind error %d\n", WSAGetLastError());
    closesocket(Socket);
    DeleteFilter(Id);

    Listener = DualStack(SOCK_STREAM, 47276, &Result);
    ok(Result == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == 0, "listen failed: %d\n", WSAGetLastError());
    Cond16(&Condition, &FWPM_CONDITION_IP_LOCAL_PORT, 47276);
    Id = AddBlockOn(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &Condition, 1);
    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(Socket, FIONBIO, &NonBlocking);
    Connect(Socket, 47276);
    ok(!Ready(Socket, TRUE, 1500), "IPv4 connection to a dual-stack listener completed\n");
    closesocket(Socket);
    DeleteFilter(Id);
    closesocket(Listener);
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
    TestDatagram(FALSE, 47151, FALSE);
    TestDatagram(TRUE, 47152, FALSE);
    TestDatagram(FALSE, 47153, TRUE);
    TestDatagram(TRUE, 47154, TRUE);
    TestReuse();
    TestNonBlockingConnect();
    TestConditions();
    TestBindFields();
    TestImplicitBind();
    TestDualStack();

    FwpmEngineClose0(Engine);
    WSACleanup();
}
