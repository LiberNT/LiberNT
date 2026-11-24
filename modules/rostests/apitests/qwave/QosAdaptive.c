/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests adaptive qWAVE flows against the LLTD QoS sink whose IPv4 address QWAVE_SINK names
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
    ok(bind(Socket, (PSOCKADDR)&Local, sizeof(Local)) == 0, "bind failed with %d\n", WSAGetLastError());
    return Socket;
}

static ULONG PacketPriority(HANDLE Qos, QOS_FLOWID Flow)
{
    QOS_PACKET_PRIORITY Priority = { 0xFFFFFFFF };
    ULONG Size = sizeof(Priority);

    ok(QOSQueryFlow(Qos, Flow, QOSQueryPacketPriority, &Size, &Priority, 0, NULL), "QOSQueryFlow failed with %lu\n",
       GetLastError());
    return Priority.ConformantDSCPValue;
}

static VOID TestNotifications(HANDLE Qos, QOS_FLOWID Flow, QOS_FLOWID Plain)
{
    OVERLAPPED Available = { 0 }, Congested = { 0 }, Uncongested = { 0 };
    UINT64 Threshold = 1, Bandwidth = 0;
    ULONG Size;
    DWORD Bytes;
    BOOL Ret;

    Available.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Congested.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Uncongested.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    Size = sizeof(Threshold);
    Ret = QOSNotifyFlow(Qos, Flow, QOSNotifyAvailable, &Size, &Threshold, 0, &Available);
    ok(Ret || GetLastError() == ERROR_IO_PENDING, "QOSNotifyFlow(available) failed with %lu\n", GetLastError());
    ok(WaitForSingleObject(Available.hEvent, 2000) == WAIT_OBJECT_0, "no available-bandwidth notification\n");
    ok(GetOverlappedResult(Qos, &Available, &Bytes, FALSE), "the notification failed with %lu\n", GetLastError());
    ok(Threshold >= 1, "available bandwidth %I64u\n", Threshold);

    Ret = QOSNotifyFlow(Qos, Flow, QOSNotifyUncongested, NULL, NULL, 0, &Uncongested);
    ok(Ret || GetLastError() == ERROR_IO_PENDING, "QOSNotifyFlow(uncongested) failed with %lu\n", GetLastError());
    ok(WaitForSingleObject(Uncongested.hEvent, 2000) == WAIT_OBJECT_0, "no uncongested notification\n");

    Ret = QOSNotifyFlow(Qos, Flow, QOSNotifyCongested, NULL, NULL, 0, &Congested);
    ok(!Ret && GetLastError() == ERROR_IO_PENDING, "QOSNotifyFlow(congested) returned %d, error %lu\n", Ret,
       GetLastError());
    if (Ret || GetLastError() != ERROR_IO_PENDING)
    {
        skip("No pending congestion notification\n");
        return;
    }
    SetLastError(0xdeadbeef);
    ok(!QOSNotifyFlow(Qos, Flow, QOSNotifyCongested, NULL, NULL, 0, NULL), "a second congestion request succeeded\n");
    ok_err(ERROR_ALREADY_EXISTS);
    ok(QOSCancel(Qos, &Congested), "QOSCancel failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!GetOverlappedResult(Qos, &Congested, &Bytes, TRUE), "the canceled notification succeeded\n");
    ok_err(ERROR_OPERATION_ABORTED);

    Size = sizeof(Bandwidth);
    SetLastError(0xdeadbeef);
    ok(!QOSNotifyFlow(Qos, Plain, QOSNotifyAvailable, &Size, &Bandwidth, 0, NULL), "a non-adaptive flow notified\n");
    ok_err(ERROR_NOT_SUPPORTED);

    CloseHandle(Available.hEvent);
    CloseHandle(Congested.hEvent);
    CloseHandle(Uncongested.hEvent);
}

START_TEST(QosAdaptive)
{
    QOS_VERSION Version = { 1, 0 };
    QOS_FLOWRATE_OUTGOING Rate = { 0, QOSShapeOnly, QOSFlowRateNotApplicable };
    QOS_FLOW_FUNDAMENTALS Fundamentals;
    SOCKADDR_IN Sink = { AF_INET }, Nobody;
    QOS_FLOWID Flow = 0, Joined, Plain = 0;
    CHAR Address[32];
    WSADATA WsaData;
    SOCKET Socket, Other;
    UINT64 Outgoing = 0;
    HANDLE Qos;
    ULONG Size;

    if (!GetEnvironmentVariableA("QWAVE_SINK", Address, sizeof(Address)))
    {
        skip("QWAVE_SINK names no LLTD QoS sink\n");
        return;
    }
    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");
    ok(QOSCreateHandle(&Version, &Qos), "QOSCreateHandle failed with %lu\n", GetLastError());
    Sink.sin_port = htons(9);
    Sink.sin_addr.s_addr = inet_addr(Address);
    Socket = UdpSocket();
    Other = UdpSocket();

    if (!QOSAddSocketToFlow(Qos, Socket, (PSOCKADDR)&Sink, QOSTrafficTypeVoice, 0, &Flow))
    {
        ok(FALSE, "QOSAddSocketToFlow(adaptive) failed with %lu\n", GetLastError());
        QOSCloseHandle(Qos);
        WSACleanup();
        return;
    }
    Joined = Flow;
    SetLastError(0xdeadbeef);
    ok(!QOSAddSocketToFlow(Qos, Other, (PSOCKADDR)&Sink, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &Joined),
       "a second socket joined an adaptive flow\n");
    ok_err(ERROR_INVALID_PARAMETER);

    Size = sizeof(Fundamentals);
    memset(&Fundamentals, 0, sizeof(Fundamentals));
    ok(QOSQueryFlow(Qos, Flow, QOSQueryFlowFundamentals, &Size, &Fundamentals, QOS_QUERYFLOW_FRESH, NULL),
       "QOSQueryFlow(fundamentals) failed with %lu\n", GetLastError());
    ok(Fundamentals.BottleneckBandwidthSet && Fundamentals.BottleneckBandwidth > 0, "bottleneck %d %I64u\n",
       Fundamentals.BottleneckBandwidthSet, Fundamentals.BottleneckBandwidth);
    ok(Fundamentals.AvailableBandwidthSet && Fundamentals.AvailableBandwidth <= Fundamentals.BottleneckBandwidth,
       "available %d %I64u\n", Fundamentals.AvailableBandwidthSet, Fundamentals.AvailableBandwidth);
    ok(Fundamentals.RTTSet, "no round-trip time\n");
    Size = sizeof(Fundamentals);
    ok(QOSQueryFlow(Qos, Flow, QOSQueryFlowFundamentals, &Size, &Fundamentals, 0, NULL),
       "QOSQueryFlow(cached fundamentals) failed with %lu\n", GetLastError());

    ok(PacketPriority(Qos, Flow) == 0, "the adaptive flow is prioritized before its rate is granted\n");
    sendto(Socket, "u", 1, 0, (PSOCKADDR)&Sink, sizeof(Sink));

    Rate.Bandwidth = Fundamentals.BottleneckBandwidth * 4 + 1;
    SetLastError(0xdeadbeef);
    ok(!QOSSetFlow(Qos, Flow, QOSSetOutgoingRate, sizeof(Rate), &Rate, 0, NULL), "an excessive rate was granted\n");
    ok_err(ERROR_NETWORK_BUSY);
    ok(PacketPriority(Qos, Flow) == 0, "a refused rate prioritized the flow\n");

    Rate.Bandwidth = 100000;
    ok(QOSSetFlow(Qos, Flow, QOSSetOutgoingRate, sizeof(Rate), &Rate, 0, NULL), "QOSSetFlow(rate) failed with %lu\n",
       GetLastError());
    ok(PacketPriority(Qos, Flow) == 0x38, "the granted flow is not marked as voice\n");
    Size = sizeof(Outgoing);
    ok(QOSQueryFlow(Qos, Flow, QOSQueryOutgoingRate, &Size, &Outgoing, 0, NULL) && Outgoing == 100000,
       "outgoing rate %I64u, error %lu\n", Outgoing, GetLastError());
    sendto(Socket, "m", 1, 0, (PSOCKADDR)&Sink, sizeof(Sink));

    ok(QOSAddSocketToFlow(Qos, Other, (PSOCKADDR)&Sink, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &Plain),
       "QOSAddSocketToFlow failed with %lu\n", GetLastError());
    TestNotifications(Qos, Flow, Plain);

    ok(QOSStartTrackingClient(Qos, (PSOCKADDR)&Sink, 0), "QOSStartTrackingClient failed with %lu\n", GetLastError());
    ok(QOSStopTrackingClient(Qos, (PSOCKADDR)&Sink, 0), "QOSStopTrackingClient failed with %lu\n", GetLastError());
    ok(!QOSStopTrackingClient(Qos, (PSOCKADDR)&Sink, 0), "an untracked client stopped being tracked\n");

    Nobody = Sink;
    Nobody.sin_addr.s_addr = htonl(ntohl(Sink.sin_addr.s_addr) + 200);
    Joined = 0;
    SetLastError(0xdeadbeef);
    ok(!QOSAddSocketToFlow(Qos, Other, (PSOCKADDR)&Nobody, QOSTrafficTypeVoice, 0, &Joined),
       "an adaptive flow to an address without a sink succeeded\n");
    ok(GetLastError() == ERROR_HOST_UNREACHABLE || GetLastError() == ERROR_NOT_SUPPORTED, "error %lu\n", GetLastError());

    ok(QOSRemoveSocketFromFlow(Qos, 0, Flow, 0), "QOSRemoveSocketFromFlow failed with %lu\n", GetLastError());
    ok(QOSCloseHandle(Qos), "QOSCloseHandle failed with %lu\n", GetLastError());
    closesocket(Socket);
    closesocket(Other);
    WSACleanup();
}
