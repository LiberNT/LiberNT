/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests IOCTL_AFD_SELECT as issued by wepoll through an AFD helper handle
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#include <mswsock.h>

typedef struct _TEST_POLL_HANDLE
{
    HANDLE Handle;
    ULONG Events;
    NTSTATUS Status;
} TEST_POLL_HANDLE;

typedef struct _TEST_POLL_INFO
{
    LARGE_INTEGER Timeout;
    ULONG NumberOfHandles;
    ULONG Exclusive;
    TEST_POLL_HANDLE Handles[1];
} TEST_POLL_INFO;

C_ASSERT(FIELD_OFFSET(TEST_POLL_INFO, Handles) == 16);

static HANDLE
OpenHelper(void)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Afd\\Wepoll");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, NULL);
    Status = NtCreateFile(&Handle, SYNCHRONIZE, &ObjectAttributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    ok(Status == STATUS_SUCCESS, "NtCreateFile(\\Device\\Afd\\Wepoll) returned 0x%lx\n", Status);
    return NT_SUCCESS(Status) ? Handle : NULL;
}

static SOCKET
BoundUdpSocket(
    _Out_ SOCKADDR_IN *Address)
{
    int Length = sizeof(*Address);
    SOCKET Socket;

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Socket, (SOCKADDR *)Address, sizeof(*Address)) == 0, "bind failed, error %d\n", WSAGetLastError());
    ok(getsockname(Socket, (SOCKADDR *)Address, &Length) == 0, "getsockname failed, error %d\n", WSAGetLastError());
    return Socket;
}

static HANDLE
BaseHandle(
    _In_ SOCKET Socket)
{
    SOCKET Base = INVALID_SOCKET;
    DWORD Bytes = 0;

    ok(WSAIoctl(Socket, SIO_BASE_HANDLE, NULL, 0, &Base, sizeof(Base), &Bytes, NULL, NULL) == 0,
       "SIO_BASE_HANDLE failed, error %d\n", WSAGetLastError());
    return (HANDLE)Base;
}

static NTSTATUS
StartPoll(
    _In_ HANDLE Helper,
    _In_ HANDLE Event,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _Inout_ TEST_POLL_INFO *Poll,
    _In_ HANDLE Socket,
    _In_ ULONG Events,
    _In_ LONGLONG Timeout)
{
    ZeroMemory(Poll, sizeof(*Poll));
    Poll->Timeout.QuadPart = Timeout;
    Poll->NumberOfHandles = 1;
    Poll->Exclusive = FALSE;
    Poll->Handles[0].Handle = Socket;
    Poll->Handles[0].Events = Events;
    IoStatus->Status = STATUS_PENDING;
    IoStatus->Information = 0;
    return NtDeviceIoControlFile(Helper, Event, NULL, NULL, IoStatus, IOCTL_AFD_SELECT,
                                 Poll, sizeof(*Poll), Poll, sizeof(*Poll));
}

static NTSTATUS
FinishPoll(
    _In_ HANDLE Event,
    _In_ NTSTATUS Status,
    _In_ PIO_STATUS_BLOCK IoStatus,
    _In_ DWORD Milliseconds)
{
    if (Status != STATUS_PENDING)
        return Status;
    if (WaitForSingleObject(Event, Milliseconds) != WAIT_OBJECT_0)
        return STATUS_PENDING;
    return IoStatus->Status;
}

START_TEST(poll)
{
    TEST_POLL_INFO Poll;
    IO_STATUS_BLOCK IoStatus;
    SOCKADDR_IN Address;
    WSADATA WsaData;
    HANDLE Helper, Event, Base;
    SOCKET Socket, Sender;
    NTSTATUS Status;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    Helper = OpenHelper();
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    Socket = BoundUdpSocket(&Address);
    if (!Helper || !Event || Socket == INVALID_SOCKET)
    {
        skip("No helper handle, event or socket\n");
        goto Cleanup;
    }
    Base = BaseHandle(Socket);

    ResetEvent(Event);
    Status = StartPoll(Helper, Event, &IoStatus, &Poll, Base, AFD_EVENT_RECEIVE | AFD_EVENT_SEND, MAXLONGLONG);
    Status = FinishPoll(Event, Status, &IoStatus, 5000);
    ok(Status == STATUS_SUCCESS, "writable poll returned 0x%lx\n", Status);
    ok(IoStatus.Information == sizeof(Poll), "writable poll information %lu\n", (ULONG)IoStatus.Information);
    ok(Poll.NumberOfHandles == 1, "writable poll reported %lu handles\n", Poll.NumberOfHandles);
    ok(Poll.Handles[0].Handle == Base, "writable poll handle %p, expected %p\n", Poll.Handles[0].Handle, Base);
    ok(Poll.Handles[0].Events == AFD_EVENT_SEND, "writable poll events 0x%lx\n", Poll.Handles[0].Events);
    ok(Poll.Handles[0].Status == STATUS_SUCCESS, "writable poll handle status 0x%lx\n", Poll.Handles[0].Status);

    ResetEvent(Event);
    Status = StartPoll(Helper, Event, &IoStatus, &Poll, Base, AFD_EVENT_RECEIVE, MAXLONGLONG);
    ok(Status == STATUS_PENDING, "receive poll returned 0x%lx before data\n", Status);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Sender != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (Sender != INVALID_SOCKET)
    {
        ok(sendto(Sender, "x", 1, 0, (SOCKADDR *)&Address, sizeof(Address)) == 1,
           "sendto failed, error %d\n", WSAGetLastError());
        closesocket(Sender);
    }
    Status = FinishPoll(Event, Status, &IoStatus, 5000);
    ok(Status == STATUS_SUCCESS, "receive poll returned 0x%lx\n", Status);
    ok(Poll.NumberOfHandles == 1, "receive poll reported %lu handles\n", Poll.NumberOfHandles);
    ok(Poll.Handles[0].Events == AFD_EVENT_RECEIVE, "receive poll events 0x%lx\n", Poll.Handles[0].Events);

    ResetEvent(Event);
    Status = StartPoll(Helper, Event, &IoStatus, &Poll, Base, AFD_EVENT_OOB_RECEIVE, -100000);
    Status = FinishPoll(Event, Status, &IoStatus, 5000);
    ok(Status == STATUS_TIMEOUT, "timed-out poll returned 0x%lx\n", Status);
    ok(Poll.NumberOfHandles == 0, "timed-out poll reported %lu handles\n", Poll.NumberOfHandles);
    ok(IoStatus.Information == FIELD_OFFSET(TEST_POLL_INFO, Handles), "timed-out poll information %lu\n", (ULONG)IoStatus.Information);

Cleanup:
    if (Socket != INVALID_SOCKET)
        closesocket(Socket);
    if (Event)
        CloseHandle(Event);
    if (Helper)
        NtClose(Helper);
    WSACleanup();
}
