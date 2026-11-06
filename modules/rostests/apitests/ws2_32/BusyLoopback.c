/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test loopback round trips while normal-priority threads use every CPU
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define PINGS 20
#define MAX_MEDIAN_MS 20.0

static volatile LONG StopSpinning;
static SOCKET Listener;

static DWORD WINAPI
SpinThread(PVOID Parameter)
{
    volatile ULONG Count = 0;

    UNREFERENCED_PARAMETER(Parameter);

    while (!StopSpinning)
        Count++;
    return 0;
}

static DWORD WINAPI
TcpEchoThread(PVOID Parameter)
{
    SOCKET Client;
    char Byte;

    UNREFERENCED_PARAMETER(Parameter);

    Client = accept(Listener, NULL, NULL);
    if (Client == INVALID_SOCKET)
        return 1;
    while (recv(Client, &Byte, 1, 0) == 1)
        send(Client, &Byte, 1, 0);
    closesocket(Client);
    return 0;
}

static DWORD WINAPI
UdpEchoThread(PVOID Parameter)
{
    SOCKET Socket = (SOCKET)Parameter;
    struct sockaddr_in From;
    int Length;
    char Byte;

    for (;;)
    {
        Length = sizeof(From);
        if (recvfrom(Socket, &Byte, 1, 0, (struct sockaddr *)&From, &Length) != 1 || Byte == 'q')
            break;
        sendto(Socket, &Byte, 1, 0, (struct sockaddr *)&From, Length);
    }
    return 0;
}

static int __cdecl
CompareDouble(const void *Left, const void *Right)
{
    double A = *(const double *)Left, B = *(const double *)Right;

    return (A > B) - (A < B);
}

static double
Median(double *Values, ULONG Count)
{
    qsort(Values, Count, sizeof(*Values), CompareDouble);
    return Values[Count / 2];
}

static double
ElapsedMs(LARGE_INTEGER Start, LARGE_INTEGER End, LARGE_INTEGER Frequency)
{
    return (double)(End.QuadPart - Start.QuadPart) * 1000.0 / (double)Frequency.QuadPart;
}

static void
Test_TcpRoundTrip(LARGE_INTEGER Frequency)
{
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address);
    LARGE_INTEGER Start, End;
    double Times[PINGS];
    ULONG Done = 0, Index;
    HANDLE Thread;
    SOCKET Client;
    char Byte = 'x';

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(Listener, (struct sockaddr *)&Address, sizeof(Address)), "bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Listener, (struct sockaddr *)&Address, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    ok(!listen(Listener, 1), "listen failed, error %d\n", WSAGetLastError());

    Thread = CreateThread(NULL, 0, TcpEchoThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(!connect(Client, (struct sockaddr *)&Address, sizeof(Address)), "connect failed, error %d\n", WSAGetLastError());
    for (Index = 0; Index < PINGS; Index++)
    {
        QueryPerformanceCounter(&Start);
        if (send(Client, &Byte, 1, 0) != 1 || recv(Client, &Byte, 1, 0) != 1)
            break;
        QueryPerformanceCounter(&End);
        Times[Done++] = ElapsedMs(Start, End, Frequency);
    }
    closesocket(Client);

    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "The echo thread did not exit\n");
        CloseHandle(Thread);
    }
    closesocket(Listener);

    ok(Done == PINGS, "Only %lu of %u TCP round trips completed\n", Done, PINGS);
    if (Done)
    {
        double Value = Median(Times, Done);
        ok(Value < MAX_MEDIAN_MS, "TCP loopback median round trip %.3f ms\n", Value);
    }
}

static void
Test_UdpRoundTrip(LARGE_INTEGER Frequency)
{
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address);
    LARGE_INTEGER Start, End;
    double Times[PINGS];
    ULONG Done = 0, Index;
    SOCKET Server, Client;
    HANDLE Thread;
    char Byte = 'x';

    Server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Server != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(Server, (struct sockaddr *)&Address, sizeof(Address)), "bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Server, (struct sockaddr *)&Address, &Length), "getsockname failed, error %d\n", WSAGetLastError());

    Thread = CreateThread(NULL, 0, UdpEchoThread, (PVOID)Server, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());

    Client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    for (Index = 0; Index < PINGS; Index++)
    {
        QueryPerformanceCounter(&Start);
        if (sendto(Client, &Byte, 1, 0, (struct sockaddr *)&Address, sizeof(Address)) != 1 ||
            recv(Client, &Byte, 1, 0) != 1)
        {
            break;
        }
        QueryPerformanceCounter(&End);
        Times[Done++] = ElapsedMs(Start, End, Frequency);
    }

    Byte = 'q';
    sendto(Client, &Byte, 1, 0, (struct sockaddr *)&Address, sizeof(Address));
    if (Thread)
    {
        ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "The echo thread did not exit\n");
        CloseHandle(Thread);
    }
    closesocket(Client);
    closesocket(Server);

    ok(Done == PINGS, "Only %lu of %u UDP round trips completed\n", Done, PINGS);
    if (Done)
    {
        double Value = Median(Times, Done);
        ok(Value < MAX_MEDIAN_MS, "UDP loopback median round trip %.3f ms\n", Value);
    }
}

START_TEST(BusyLoopback)
{
    HANDLE Spinners[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER Frequency;
    SYSTEM_INFO Info;
    ULONG Count, Index;
    WSADATA Data;

    ok(!WSAStartup(MAKEWORD(2, 2), &Data), "WSAStartup failed\n");
    QueryPerformanceFrequency(&Frequency);
    GetSystemInfo(&Info);
    Count = min(Info.dwNumberOfProcessors, MAXIMUM_WAIT_OBJECTS);

    StopSpinning = 0;
    for (Index = 0; Index < Count; Index++)
    {
        Spinners[Index] = CreateThread(NULL, 0, SpinThread, NULL, 0, NULL);
        ok(Spinners[Index] != NULL, "CreateThread failed, error %lu\n", GetLastError());
        if (!Spinners[Index])
            break;
    }
    Count = Index;

    Test_TcpRoundTrip(Frequency);
    Test_UdpRoundTrip(Frequency);

    InterlockedExchange(&StopSpinning, 1);
    for (Index = 0; Index < Count; Index++)
    {
        ok(WaitForSingleObject(Spinners[Index], 10000) == WAIT_OBJECT_0, "Spinner %lu did not exit\n", Index);
        CloseHandle(Spinners[Index]);
    }
    WSACleanup();
}
