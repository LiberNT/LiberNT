/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WSK name resolution through NETIO, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <drivers/netio/wsknameio.h>

#include "wskname.h"

static HANDLE ResolverDevice = INVALID_HANDLE_VALUE;
static HANDLE ResolverThread;
static HANDLE ResolverReady;
static volatile LONG ResolverStopping;
static volatile LONG ResolverServed;

/* The same two IOCTLs the DNS Client service pumps, so the contract is the one it uses. */
static
DWORD
WINAPI
ResolverRoutine(
    _In_ LPVOID Parameter)
{
    WSKNAME_REQUEST Request;
    WSKNAME_REPLY Reply;
    DWORD Returned;

    UNREFERENCED_PARAMETER(Parameter);

    while (InterlockedCompareExchange(&ResolverStopping, 0, 0) == 0)
    {
        SetEvent(ResolverReady);
        if (!DeviceIoControl(ResolverDevice, IOCTL_WSKNAME_GET_REQUEST, NULL, 0, &Request, sizeof(Request),
                             &Returned, NULL))
        {
            break;
        }
        if (Returned != sizeof(Request))
            continue;

        ZeroMemory(&Reply, sizeof(Reply));
        Reply.Id = Request.Id;
        Reply.Status = STATUS_OBJECT_NAME_NOT_FOUND;

        if (Request.Operation == WskNameAddressInfo && wcscmp(Request.Node, WSKNAME_TEST_NODE) == 0)
        {
            /* The name the driver asks for is answered here, so no real lookup decides the result. */
            Reply.AddressCount = 1;
            Reply.Address[0].Ipv4.sin_family = AF_INET;
            Reply.Address[0].Ipv4.sin_addr.s_addr = htonl(WSKNAME_TEST_ADDRESS);
            Reply.Address[0].Ipv4.sin_port = htons(WSKNAME_TEST_PORT);
            Reply.Status = STATUS_SUCCESS;
        }
        else if (Request.Operation == WskNameAddressInfo)
        {
            ADDRINFOW Hints;
            PADDRINFOW Head = NULL, Entry;

            ZeroMemory(&Hints, sizeof(Hints));
            Hints.ai_family = Request.Family;
            Hints.ai_socktype = Request.SocketType;
            Hints.ai_protocol = Request.Protocol;
            Hints.ai_flags = Request.Flags;
            if (GetAddrInfoW(Request.Node, Request.Service[0] ? Request.Service : NULL, &Hints, &Head) == 0)
            {
                for (Entry = Head; Entry != NULL && Reply.AddressCount < WSKNAME_MAX_REPLY_ADDRESSES;
                     Entry = Entry->ai_next)
                {
                    if (Entry->ai_addrlen > sizeof(SOCKADDR_INET))
                        continue;
                    CopyMemory(&Reply.Address[Reply.AddressCount], Entry->ai_addr, Entry->ai_addrlen);
                    Reply.AddressCount++;
                }
                if (Head != NULL && Head->ai_canonname != NULL)
                    StringCchCopyW(Reply.Name, WSKNAME_MAX_HOST, Head->ai_canonname);
                if (Head != NULL)
                    FreeAddrInfoW(Head);
                if (Reply.AddressCount != 0)
                    Reply.Status = STATUS_SUCCESS;
            }
        }
        else if (Request.Operation == WskNameNameInfo)
        {
            INT Length = Request.Address.si_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN);

            if (GetNameInfoW((const SOCKADDR *)&Request.Address, Length, Reply.Name, WSKNAME_MAX_HOST, Reply.Service,
                             WSKNAME_MAX_SERVICE, Request.Flags) == 0)
            {
                Reply.Status = STATUS_SUCCESS;
            }
        }

        InterlockedIncrement(&ResolverServed);
        DeviceIoControl(ResolverDevice, IOCTL_WSKNAME_COMPLETE_REQUEST, &Reply, sizeof(Reply), NULL, 0, &Returned,
                        NULL);
    }
    return 0;
}

static
BOOL
StartResolver(VOID)
{
    WSADATA Data;

    if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
        return FALSE;

    ResolverDevice = CreateFileW(L"\\\\.\\NetioNameResolver", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                 0, NULL);
    if (ResolverDevice == INVALID_HANDLE_VALUE)
    {
        trace("NETIO has no name resolver device (%lu)\n", GetLastError());
        return FALSE;
    }

    ResolverReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    ResolverThread = ResolverReady != NULL ? CreateThread(NULL, 0, ResolverRoutine, NULL, 0, NULL) : NULL;
    if (ResolverThread == NULL)
    {
        if (ResolverReady != NULL)
            CloseHandle(ResolverReady);
        CloseHandle(ResolverDevice);
        ResolverDevice = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    /* The driver counts a handle as a server once its first request is in, so let that one land. */
    WaitForSingleObject(ResolverReady, 5000);
    Sleep(50);
    return TRUE;
}

static
VOID
StopResolver(VOID)
{
    if (ResolverDevice == INVALID_HANDLE_VALUE)
        return;

    InterlockedExchange(&ResolverStopping, 1);
    CancelIoEx(ResolverDevice, NULL);
    WaitForSingleObject(ResolverThread, 5000);
    CloseHandle(ResolverThread);
    CloseHandle(ResolverReady);
    CloseHandle(ResolverDevice);
    ResolverReady = NULL;
    ResolverThread = NULL;
    ResolverDevice = INVALID_HANDLE_VALUE;
}

START_TEST(WskName)
{
    BOOL Serving;
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"WskName", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Serving = StartResolver();
    ok(Serving, "The test resolver did not open the NETIO device\n");

    Error = KmtSendToDriver(IOCTL_WSKNAME_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);

    if (Serving)
    {
        ok(ResolverServed > 0, "The kernel asked the resolver nothing\n");
        StopResolver();
    }

    KmtCloseDriver();
    KmtUnloadDriver();
}
