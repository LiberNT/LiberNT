/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The TCP owner table lists the connections a listener accepted
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <iphlpapi.h>
#include <winsock2.h>

typedef struct _CHURN
{
    struct sockaddr_in Address;
    volatile LONG Stop;
    LONG Accepted;
    SOCKET Listener;
} CHURN, *PCHURN;

static DWORD WINAPI
ChurnThread(PVOID Parameter)
{
    PCHURN Churn = Parameter;
    SOCKET Client, Server;

    while (!Churn->Stop)
    {
        Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connect(Client, (struct sockaddr *)&Churn->Address, sizeof(Churn->Address)) == 0)
        {
            Server = accept(Churn->Listener, NULL, NULL);
            if (Server != INVALID_SOCKET)
            {
                InterlockedIncrement(&Churn->Accepted);
                closesocket(Server);
            }
        }
        closesocket(Client);
    }
    return 0;
}

static void
CountRows(USHORT Port, int *Listen, int *Server, int *Client)
{
    PMIB_TCPTABLE_OWNER_PID Table;
    DWORD Size = 0, Pid = GetCurrentProcessId(), Index;

    *Listen = *Server = *Client = 0;
    GetExtendedTcpTable(NULL, &Size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    Size += 64 * sizeof(MIB_TCPROW_OWNER_PID);
    Table = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Table)
        return;
    ok(GetExtendedTcpTable(Table, &Size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR,
       "GetExtendedTcpTable failed\n");
    for (Index = 0; Index < Table->dwNumEntries; Index++)
    {
        PMIB_TCPROW_OWNER_PID Row = &Table->table[Index];

        if (Row->dwOwningPid != Pid)
            continue;
        if ((USHORT)Row->dwLocalPort == Port && Row->dwState == MIB_TCP_STATE_LISTEN)
            (*Listen)++;
        else if ((USHORT)Row->dwLocalPort == Port && Row->dwState == MIB_TCP_STATE_ESTAB)
            (*Server)++;
        else if ((USHORT)Row->dwRemotePort == Port && Row->dwState == MIB_TCP_STATE_ESTAB)
            (*Client)++;
    }
    HeapFree(GetProcessHeap(), 0, Table);
}

START_TEST(TcpTableAccepted)
{
    struct sockaddr_in Address = { 0 };
    SOCKET Listener, Clients[3], Servers[3];
    int Length = sizeof(Address), Listen, Server, Client, Index, Round;
    CHURN Churn = { { 0 } };
    HANDLE Thread;
    WSADATA Data;

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed %d\n", WSAGetLastError());
    getsockname(Listener, (struct sockaddr *)&Address, &Length);

    for (Index = 0; Index < 3; Index++)
    {
        Clients[Index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(connect(Clients[Index], (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed %d\n", WSAGetLastError());
        Servers[Index] = accept(Listener, NULL, NULL);
        ok(Servers[Index] != INVALID_SOCKET, "accept failed %d\n", WSAGetLastError());
    }

    for (Round = 0; Round < 10; Round++)
    {
        CountRows(Address.sin_port, &Listen, &Server, &Client);
        ok(Listen == 1, "Round %d: %d listening rows\n", Round, Listen);
        ok(Server == 3, "Round %d: %d accepted rows\n", Round, Server);
        ok(Client == 3, "Round %d: %d client rows\n", Round, Client);
    }

    /* Query the table while connections on the listener come and go */
    Churn.Address = Address;
    Churn.Listener = Listener;
    Thread = CreateThread(NULL, 0, ChurnThread, &Churn, 0, NULL);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    for (Round = 0; Round < 200; Round++)
    {
        CountRows(Address.sin_port, &Listen, &Server, &Client);
        if (Listen != 1 || Server < 3 || Client < 3)
        {
            ok(0, "Churn round %d: %d listening, %d accepted, %d client rows\n", Round, Listen, Server, Client);
            break;
        }
    }
    InterlockedExchange(&Churn.Stop, 1);
    ok(WaitForSingleObject(Thread, 20000) == WAIT_OBJECT_0, "The churn thread did not end\n");
    ok(Churn.Accepted > 0, "No connection was accepted during the churn\n");
    CloseHandle(Thread);

    for (Index = 0; Index < 3; Index++)
    {
        closesocket(Clients[Index]);
        closesocket(Servers[Index]);
    }
    closesocket(Listener);
}
