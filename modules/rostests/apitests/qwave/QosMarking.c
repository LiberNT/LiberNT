/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the DSCP marking, queries, enumeration and services of qWAVE flows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <qos2.h>

static LPFN_WSARECVMSG RecvMsg;

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

static SOCKET LoopbackSocket(INT Family, PSOCKADDR_STORAGE Address, PINT Length)
{
    SOCKET Socket = socket(Family, SOCK_DGRAM, IPPROTO_UDP);

    ZeroMemory(Address, sizeof(*Address));
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    if (Family == AF_INET)
    {
        ((PSOCKADDR_IN)Address)->sin_family = AF_INET;
        ((PSOCKADDR_IN)Address)->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        *Length = sizeof(SOCKADDR_IN);
    }
    else
    {
        ((PSOCKADDR_IN6)Address)->sin6_family = AF_INET6;
        ((PSOCKADDR_IN6)Address)->sin6_addr.s6_addr[15] = 1;
        *Length = sizeof(SOCKADDR_IN6);
    }
    if (bind(Socket, (PSOCKADDR)Address, *Length) != 0 || getsockname(Socket, (PSOCKADDR)Address, Length) != 0)
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static INT ReceivedTos(SOCKET Sender, SOCKET Receiver, INT Family, PSOCKADDR Destination, INT Length)
{
    CHAR Data[8] = "qwave", Buffer[8];
    CHAR Control[64];
    WSABUF Buf = { sizeof(Buffer), Buffer };
    WSAMSG Msg = { 0 };
    TIMEVAL Timeout = { 1, 0 };
    DWORD Received = 0;
    PWSACMSGHDR Header;
    fd_set Set;

    if (sendto(Sender, Data, 5, 0, Destination, Length) != 5)
        return -1;
    FD_ZERO(&Set);
    FD_SET(Receiver, &Set);
    if (select(0, &Set, NULL, NULL, &Timeout) != 1)
        return -1;
    Msg.lpBuffers = &Buf;
    Msg.dwBufferCount = 1;
    Msg.Control.buf = Control;
    Msg.Control.len = sizeof(Control);
    if (RecvMsg(Receiver, &Msg, &Received, NULL, NULL) != 0)
        return -1;
    for (Header = WSA_CMSG_FIRSTHDR(&Msg); Header; Header = WSA_CMSG_NXTHDR(&Msg, Header))
    {
        if ((Family == AF_INET && Header->cmsg_level == IPPROTO_IP && Header->cmsg_type == IP_TOS) ||
            (Family == AF_INET6 && Header->cmsg_level == IPPROTO_IPV6 && Header->cmsg_type == IPV6_TCLASS))
        {
            return *(PINT)WSA_CMSG_DATA(Header);
        }
    }
    return -1;
}

static VOID TestMarking(HANDLE Qos, INT Family)
{
    SOCKADDR_STORAGE SenderAddress, ReceiverAddress;
    INT SenderLength, ReceiverLength;
    QOS_TRAFFIC_TYPE Type;
    QOS_FLOWID Flow = 0;
    SOCKET Sender, Receiver;
    DWORD On = 1, Dscp;
    PSOCKADDR To;

    Sender = LoopbackSocket(Family, &SenderAddress, &SenderLength);
    Receiver = LoopbackSocket(Family, &ReceiverAddress, &ReceiverLength);
    if (Sender == INVALID_SOCKET || Receiver == INVALID_SOCKET)
    {
        skip("No loopback sockets of family %d\n", Family);
        if (Sender != INVALID_SOCKET)
            closesocket(Sender);
        if (Receiver != INVALID_SOCKET)
            closesocket(Receiver);
        return;
    }
    To = (PSOCKADDR)&ReceiverAddress;
    ok(setsockopt(Receiver, Family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                  Family == AF_INET ? IP_RECVTOS : IPV6_RECVTCLASS, (PCHAR)&On, sizeof(On)) == 0,
       "receiving the traffic class failed with %d\n", WSAGetLastError());

    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0);
    ok(QOSAddSocketToFlow(Qos, Sender, To, QOSTrafficTypeControl, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0x38 << 2);

    Type = QOSTrafficTypeBackground;
    ok(QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL), "QOSSetFlow failed with %lu\n",
       GetLastError());
    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0x08 << 2);
    Type = QOSTrafficTypeBestEffort;
    ok(QOSSetFlow(Qos, Flow, QOSSetTrafficType, sizeof(Type), &Type, 0, NULL), "QOSSetFlow failed with %lu\n",
       GetLastError());
    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0);

    if (IsAdministrator())
    {
        Dscp = 46;
        ok(QOSSetFlow(Qos, Flow, QOSSetOutgoingDSCPValue, sizeof(Dscp), &Dscp, 0, NULL),
           "QOSSetFlow of the DSCP value failed with %lu\n", GetLastError());
        ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 46 << 2);
    }

    ok(QOSRemoveSocketFromFlow(Qos, Sender, Flow, 0), "QOSRemoveSocketFromFlow failed with %lu\n", GetLastError());
    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0);

    Flow = 0;
    ok(QOSAddSocketToFlow(Qos, Sender, To, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    ok_int(ReceivedTos(Sender, Receiver, Family, To, ReceiverLength), 0x38 << 2);
    closesocket(Sender);
    ok(QOSRemoveSocketFromFlow(Qos, 0, Flow, 0), "QOSRemoveSocketFromFlow of a closed socket's flow failed with %lu\n",
       GetLastError());
    closesocket(Receiver);
}

static VOID TestQuery(HANDLE Qos)
{
    SOCKADDR_STORAGE Address;
    QOS_FLOWRATE_OUTGOING Rate = { 1000000, QOSShapeOnly, QOSFlowRateNotApplicable };
    QOS_FLOW_FUNDAMENTALS Fundamentals;
    QOS_PACKET_PRIORITY Priority;
    QOS_FLOWID Flow = 0;
    UINT64 Value = 0;
    SOCKET Socket;
    ULONG Size;
    INT Length;

    Socket = LoopbackSocket(AF_INET, &Address, &Length);
    ok(Socket != INVALID_SOCKET, "no loopback socket\n");
    if (Socket == INVALID_SOCKET)
        return;
    ok(QOSAddSocketToFlow(Qos, Socket, (PSOCKADDR)&Address, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &Flow),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());

    Size = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSQueryFlow(Qos, Flow, QOSQueryPacketPriority, &Size, &Priority, 0, NULL), "a zero-sized query succeeded\n");
    ok_err(ERROR_INSUFFICIENT_BUFFER);
    ok_int(Size, sizeof(QOS_PACKET_PRIORITY));
    memset(&Priority, 0xcc, sizeof(Priority));
    ok(QOSQueryFlow(Qos, Flow, QOSQueryPacketPriority, &Size, &Priority, 0, NULL), "QOSQueryFlow failed with %lu\n",
       GetLastError());
    ok_int(Size, sizeof(QOS_PACKET_PRIORITY));
    ok(Priority.ConformantDSCPValue == 0x38, "conformant DSCP %lu\n", Priority.ConformantDSCPValue);

    Size = sizeof(Priority);
    SetLastError(0xdeadbeef);
    ok(!QOSQueryFlow(Qos, Flow + 0x1000, QOSQueryPacketPriority, &Size, &Priority, 0, NULL),
       "the query of an unknown flow succeeded\n");
    ok_err(ERROR_NOT_FOUND);

    Size = sizeof(Fundamentals);
    SetLastError(0xdeadbeef);
    ok(!QOSQueryFlow(Qos, Flow, QOSQueryFlowFundamentals, &Size, &Fundamentals, 0, NULL),
       "a non-adaptive flow has fundamentals\n");
    ok_err(ERROR_NOT_SUPPORTED);

    ok(QOSSetFlow(Qos, Flow, QOSSetOutgoingRate, sizeof(Rate), &Rate, 0, NULL), "QOSSetFlow of the rate failed with %lu\n",
       GetLastError());
    Size = sizeof(Value);
    ok(QOSQueryFlow(Qos, Flow, QOSQueryOutgoingRate, &Size, &Value, 0, NULL), "QOSQueryFlow of the rate failed with %lu\n",
       GetLastError());
    ok(Value == 1000000, "rate %I64u\n", Value);

    ok(QOSRemoveSocketFromFlow(Qos, Socket, Flow, 0), "QOSRemoveSocketFromFlow failed with %lu\n", GetLastError());
    closesocket(Socket);
}

static VOID TestEnumerate(HANDLE Qos)
{
    SOCKADDR_STORAGE Address;
    QOS_FLOWID First = 0, Second = 0, Ids[64];
    SOCKET A, B;
    ULONG Size, Index;
    BOOL SawFirst = FALSE, SawSecond = FALSE;
    INT Length;

    if (!IsAdministrator())
    {
        Size = sizeof(Ids);
        ok(!QOSEnumerateFlows(Qos, &Size, Ids), "QOSEnumerateFlows succeeded without administrator rights\n");
        return;
    }
    A = LoopbackSocket(AF_INET, &Address, &Length);
    B = LoopbackSocket(AF_INET, &Address, &Length);
    ok(QOSAddSocketToFlow(Qos, A, (PSOCKADDR)&Address, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &First),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    ok(QOSAddSocketToFlow(Qos, B, (PSOCKADDR)&Address, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &Second),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());

    Size = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSEnumerateFlows(Qos, &Size, NULL), "QOSEnumerateFlows into no buffer succeeded\n");
    ok_err(ERROR_INSUFFICIENT_BUFFER);
    ok(Size >= 2 * sizeof(QOS_FLOWID) && Size % sizeof(QOS_FLOWID) == 0, "size %lu\n", Size);

    Size = sizeof(Ids);
    ok(QOSEnumerateFlows(Qos, &Size, Ids), "QOSEnumerateFlows failed with %lu\n", GetLastError());
    for (Index = 0; Index < Size / sizeof(QOS_FLOWID); Index++)
    {
        SawFirst |= Ids[Index] == First;
        SawSecond |= Ids[Index] == Second;
    }
    ok(SawFirst && SawSecond, "flows %lu and %lu are missing from %lu ids\n", First, Second, (ULONG)(Size / sizeof(QOS_FLOWID)));

    closesocket(A);
    closesocket(B);
}

static BOOL HasDependency(PCWSTR Dependencies, PCWSTR Name)
{
    for (; Dependencies && *Dependencies; Dependencies += wcslen(Dependencies) + 1)
    {
        if (!_wcsicmp(Dependencies, Name))
            return TRUE;
    }
    return FALSE;
}

static VOID TestService(VOID)
{
    union
    {
        QUERY_SERVICE_CONFIGW Config;
        BYTE Buffer[2048];
    } Data;
    SERVICE_STATUS Status;
    SC_HANDLE Scm, Service;
    DWORD Needed;

    Scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    ok(Scm != NULL, "OpenSCManagerW failed with %lu\n", GetLastError());
    if (!Scm)
        return;

    Service = OpenServiceW(Scm, L"QWAVE", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    ok(Service != NULL, "OpenServiceW(QWAVE) failed with %lu\n", GetLastError());
    if (Service)
    {
        ok(QueryServiceStatus(Service, &Status), "QueryServiceStatus failed with %lu\n", GetLastError());
        ok_int(Status.dwCurrentState, SERVICE_RUNNING);
        ok(QueryServiceConfigW(Service, &Data.Config, sizeof(Data), &Needed), "QueryServiceConfigW failed with %lu\n",
           GetLastError());
        ok_int(Data.Config.dwServiceType, SERVICE_WIN32_SHARE_PROCESS);
        ok_int(Data.Config.dwStartType, SERVICE_DEMAND_START);
        ok(Data.Config.lpServiceStartName && !_wcsicmp(Data.Config.lpServiceStartName, L"NT AUTHORITY\\LocalService"),
           "account %S\n", Data.Config.lpServiceStartName);
        ok(Data.Config.lpBinaryPathName && wcsstr(Data.Config.lpBinaryPathName, L"svchost.exe -k LocalServiceAndNoImpersonation"),
           "image path %S\n", Data.Config.lpBinaryPathName);
        ok(HasDependency(Data.Config.lpDependencies, L"QWAVEdrv"), "QWAVE does not depend on QWAVEdrv\n");
        CloseServiceHandle(Service);
    }

    Service = OpenServiceW(Scm, L"QWAVEdrv", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    ok(Service != NULL, "OpenServiceW(QWAVEdrv) failed with %lu\n", GetLastError());
    if (Service)
    {
        ok(QueryServiceStatus(Service, &Status), "QueryServiceStatus failed with %lu\n", GetLastError());
        ok_int(Status.dwCurrentState, SERVICE_RUNNING);
        ok(QueryServiceConfigW(Service, &Data.Config, sizeof(Data), &Needed), "QueryServiceConfigW failed with %lu\n",
           GetLastError());
        ok_int(Data.Config.dwServiceType, SERVICE_KERNEL_DRIVER);
        ok_int(Data.Config.dwStartType, SERVICE_DEMAND_START);
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Scm);
}

START_TEST(QosMarking)
{
    GUID RecvMsgId = WSAID_WSARECVMSG;
    QOS_VERSION Version = { 1, 0 };
    WSADATA WsaData;
    HANDLE Qos;
    DWORD Bytes;
    SOCKET Probe;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");
    if (!QOSCreateHandle(&Version, &Qos))
    {
        skip("QOSCreateHandle failed with %lu\n", GetLastError());
        WSACleanup();
        return;
    }
    TestService();

    Probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(WSAIoctl(Probe, SIO_GET_EXTENSION_FUNCTION_POINTER, &RecvMsgId, sizeof(RecvMsgId), &RecvMsg, sizeof(RecvMsg),
                &Bytes, NULL, NULL) == 0, "no WSARecvMsg: %d\n", WSAGetLastError());
    closesocket(Probe);
    if (RecvMsg)
    {
        TestMarking(Qos, AF_INET);
        TestMarking(Qos, AF_INET6);
    }
    TestQuery(Qos);
    TestEnumerate(Qos);

    ok(QOSCloseHandle(Qos), "QOSCloseHandle failed with %lu\n", GetLastError());
    WSACleanup();
}
