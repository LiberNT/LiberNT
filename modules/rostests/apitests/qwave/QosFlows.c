/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the qWAVE handle and the non-adaptive flow contract of the QoS2 API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <qos2.h>

static SOCKET UdpSocket(VOID)
{
    SOCKADDR_IN Local = { AF_INET };
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    Local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Socket, (PSOCKADDR)&Local, sizeof(Local)) == 0, "bind failed with %d\n", WSAGetLastError());
    return Socket;
}

static BOOL IsAdministrator(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    PSID Administrators;
    BOOL Member = FALSE;

    if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                  0, 0, &Administrators))
    {
        return FALSE;
    }
    if (!CheckTokenMembership(NULL, Administrators, &Member))
        Member = FALSE;
    FreeSid(Administrators);
    return Member;
}

static VOID TestHandle(VOID)
{
    QOS_VERSION Version;
    HANDLE Qos;

    SetLastError(0xdeadbeef);
    ok(!QOSCreateHandle(NULL, NULL), "QOSCreateHandle succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Version.MajorVersion = 1;
    Version.MinorVersion = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSCreateHandle(&Version, NULL), "QOSCreateHandle succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Version.MajorVersion = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSCreateHandle(&Version, &Qos), "QOSCreateHandle 0.0 succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Version.MajorVersion = 1;
    Version.MinorVersion = 5;
    SetLastError(0xdeadbeef);
    ok(!QOSCreateHandle(&Version, &Qos), "QOSCreateHandle 1.5 succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Version.MajorVersion = 2;
    Version.MinorVersion = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSCreateHandle(&Version, &Qos), "QOSCreateHandle 2.0 succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);

    Version.MajorVersion = 1;
    Version.MinorVersion = 0;
    Qos = NULL;
    ok(QOSCreateHandle(&Version, &Qos), "QOSCreateHandle failed with %lu\n", GetLastError());
    ok(Qos != NULL, "no handle\n");
    SetLastError(0xdeadbeef);
    ok(QOSCloseHandle(Qos), "QOSCloseHandle failed with %lu\n", GetLastError());
    ok_err(0xdeadbeef);

    SetLastError(0xdeadbeef);
    ok(!QOSCloseHandle(NULL), "QOSCloseHandle(NULL) succeeded\n");
    ok_err(ERROR_INVALID_HANDLE);
    SetLastError(0xdeadbeef);
    ok(!QOSCloseHandle((HANDLE)(ULONG_PTR)0xdeadbeef), "QOSCloseHandle(0xdeadbeef) succeeded\n");
    ok_err(ERROR_INVALID_HANDLE);
}

static VOID TestFlows(VOID)
{
    QOS_VERSION Version = { 1, 0 };
    SOCKADDR_IN Dest = { AF_INET };
    QOS_TRAFFIC_TYPE Type = QOSTrafficTypeVoice;
    QOS_FLOWID Flow, Joined, Bogus;
    OVERLAPPED Overlapped = { 0 };
    LPOVERLAPPED Completed;
    ULONG_PTR Key;
    HANDLE Qos, Port;
    SOCKET First, Second;
    DWORD Dscp, Bytes;
    BOOL Ret;

    Dest.sin_port = htons(9);
    Dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!QOSCreateHandle(&Version, &Qos))
    {
        skip("QOSCreateHandle failed with %lu\n", GetLastError());
        return;
    }
    First = UdpSocket();
    Second = UdpSocket();

    SetLastError(0xdeadbeef);
    ok(!QOSAddSocketToFlow(Qos, First, (PSOCKADDR)&Dest, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, NULL),
       "QOSAddSocketToFlow without a flow id succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);
    Flow = 0;
    ok(!QOSAddSocketToFlow(Qos, First, NULL, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow of an unconnected socket without a destination succeeded\n");

    Flow = 0;
    ok(QOSAddSocketToFlow(Qos, First, (PSOCKADDR)&Dest, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    ok(Flow != 0, "no flow id\n");
    Bogus = Flow + 0x1000;
    Joined = Flow;
    ok(QOSAddSocketToFlow(Qos, Second, (PSOCKADDR)&Dest, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Joined),
       "QOSAddSocketToFlow to flow %lu failed with %lu\n", Flow, GetLastError());
    ok(Joined == Flow, "flow %lu, expected %lu\n", Joined, Flow);
    Joined = Bogus;
    SetLastError(0xdeadbeef);
    ok(!QOSAddSocketToFlow(Qos, Second, (PSOCKADDR)&Dest, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Joined),
       "QOSAddSocketToFlow to an unknown flow succeeded\n");
    ok_err(ERROR_INVALID_PARAMETER);

    ok(QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL), "QOSSetFlow failed with %lu\n",
       GetLastError());
    SetLastError(0xdeadbeef);
    ok(!QOSSetFlow(Qos, Bogus, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL), "QOSSetFlow of an unknown flow succeeded\n");
    ok_err(ERROR_NOT_FOUND);
    Dscp = 46;
    SetLastError(0xdeadbeef);
    Ret = QOSSetFlow(Qos, Flow, QOSSetOutgoingDSCPValue, sizeof(Dscp), &Dscp, 0, NULL);
    if (IsAdministrator())
    {
        ok(Ret, "QOSSetFlow of the DSCP value failed with %lu\n", GetLastError());
    }
    else
    {
        ok(!Ret, "QOSSetFlow of the DSCP value succeeded without administrator rights\n");
        ok_err(ERROR_ACCESS_DENIED);
    }

    Port = CreateIoCompletionPort(Qos, NULL, 0x51, 0);
    ok(Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    Ret = QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, &Overlapped);
    ok(Ret || GetLastError() == ERROR_IO_PENDING, "overlapped QOSSetFlow failed with %lu\n", GetLastError());
    Completed = NULL;
    Key = 0;
    ok(GetQueuedCompletionStatus(Port, &Bytes, &Key, &Completed, 1000), "no completion: %lu\n", GetLastError());
    ok(Completed == &Overlapped, "completion %p, expected %p\n", Completed, &Overlapped);
    ok(Key == 0x51, "key %lu\n", (ULONG)Key);

    ok(QOSRemoveSocketFromFlow(Qos, Second, Flow, 0), "QOSRemoveSocketFromFlow failed with %lu\n", GetLastError());
    ok(QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL),
       "the flow is gone while a socket is still attached: %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!QOSRemoveSocketFromFlow(Qos, First, Bogus, 0), "QOSRemoveSocketFromFlow of an unknown flow succeeded\n");
    ok_err(ERROR_NOT_FOUND);
    ok(QOSRemoveSocketFromFlow(Qos, First, Flow, 0), "QOSRemoveSocketFromFlow failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL),
       "the flow still exists after its last socket left\n");
    ok_err(ERROR_NOT_FOUND);

    Flow = 0;
    ok(QOSAddSocketToFlow(Qos, First, (PSOCKADDR)&Dest, QOSTrafficTypeBestEffort, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    Joined = Flow;
    ok(QOSAddSocketToFlow(Qos, Second, (PSOCKADDR)&Dest, QOSTrafficTypeBestEffort, QOS_NON_ADAPTIVE_FLOW, &Joined),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    ok(QOSRemoveSocketFromFlow(Qos, 0, Flow, 0), "QOSRemoveSocketFromFlow of the flow failed with %lu\n",
       GetLastError());
    SetLastError(0xdeadbeef);
    ok(!QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL), "the flow survived its removal\n");
    ok_err(ERROR_NOT_FOUND);

    Flow = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSAddSocketToFlow(NULL, First, (PSOCKADDR)&Dest, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow without a handle succeeded\n");
    ok_err(ERROR_INVALID_HANDLE);

    ok(QOSCloseHandle(Qos), "QOSCloseHandle failed with %lu\n", GetLastError());
    if (Port)
        CloseHandle(Port);
    closesocket(Second);
    closesocket(First);
}

START_TEST(QosFlows)
{
    WSADATA Data;

    ok(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "WSAStartup failed\n");
    TestHandle();
    TestFlows();
    WSACleanup();
}
