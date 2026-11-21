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

typedef struct _RESOLVER
{
    HANDLE Device;
    HANDLE Thread;
    HANDLE Ready;
    BOOL Declines;
    volatile LONG Served;
} RESOLVER, *PRESOLVER;

static RESOLVER Declining = { INVALID_HANDLE_VALUE, NULL, NULL, TRUE, 0 };
static RESOLVER Answering = { INVALID_HANDLE_VALUE, NULL, NULL, FALSE, 0 };
static volatile LONG ResolverStopping;

/* The same two IOCTLs the DNS Client service pumps, so the contract is the one it uses. */
static
DWORD
WINAPI
ResolverRoutine(
    _In_ LPVOID Parameter)
{
    PRESOLVER Resolver = Parameter;
    WSKNAME_REQUEST Request;
    WSKNAME_REPLY Reply;
    DWORD Returned;

    while (InterlockedCompareExchange(&ResolverStopping, 0, 0) == 0)
    {
        SetEvent(Resolver->Ready);
        if (!DeviceIoControl(Resolver->Device, IOCTL_WSKNAME_GET_REQUEST, NULL, 0, &Request, sizeof(Request),
                             &Returned, NULL))
        {
            break;
        }
        if (Returned != sizeof(Request))
            continue;

        ZeroMemory(&Reply, sizeof(Reply));
        Reply.Id = Request.Id;
        Reply.Status = STATUS_OBJECT_NAME_NOT_FOUND;

        if (Resolver->Declines)
        {
            /* This one has no answer for any name, so the kernel has to go on to the next resolver. */
        }
        else if (Request.Operation == WskNameAddressInfo && wcscmp(Request.Node, WSKNAME_TEST_NODE) == 0)
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

        InterlockedIncrement(&Resolver->Served);
        DeviceIoControl(Resolver->Device, IOCTL_WSKNAME_COMPLETE_REQUEST, &Reply, sizeof(Reply), NULL, 0, &Returned,
                        NULL);
    }
    return 0;
}

static
BOOL
StartResolver(
    _Inout_ PRESOLVER Resolver)
{
    Resolver->Device = CreateFileW(L"\\\\.\\NetioNameResolver", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                   0, NULL);
    if (Resolver->Device == INVALID_HANDLE_VALUE)
    {
        trace("NETIO has no name resolver device (%lu)\n", GetLastError());
        return FALSE;
    }

    Resolver->Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    Resolver->Thread = Resolver->Ready != NULL ? CreateThread(NULL, 0, ResolverRoutine, Resolver, 0, NULL) : NULL;
    if (Resolver->Thread == NULL)
    {
        if (Resolver->Ready != NULL)
            CloseHandle(Resolver->Ready);
        CloseHandle(Resolver->Device);
        Resolver->Device = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    /* The driver counts a handle as a server once its first request is in, so let that one land. */
    WaitForSingleObject(Resolver->Ready, 5000);
    Sleep(50);
    return TRUE;
}

static
VOID
StopResolver(
    _Inout_ PRESOLVER Resolver)
{
    if (Resolver->Device == INVALID_HANDLE_VALUE)
        return;

    InterlockedExchange(&ResolverStopping, 1);
    CancelIoEx(Resolver->Device, NULL);
    WaitForSingleObject(Resolver->Thread, 5000);
    CloseHandle(Resolver->Thread);
    CloseHandle(Resolver->Ready);
    CloseHandle(Resolver->Device);
    Resolver->Ready = NULL;
    Resolver->Thread = NULL;
    Resolver->Device = INVALID_HANDLE_VALUE;
}

START_TEST(WskName)
{
    BOOL Serving;
    WSADATA Data;
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"WskName", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    /* The resolver that answers no name waits first, so a request reaches it before the one that answers. */
    Serving = WSAStartup(MAKEWORD(2, 2), &Data) == 0 && StartResolver(&Declining) && StartResolver(&Answering);
    ok(Serving, "The test resolvers did not open the NETIO device\n");

    Error = KmtSendToDriver(IOCTL_WSKNAME_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);

    if (Serving)
    {
        ok(Answering.Served > 0, "The kernel asked the resolver nothing\n");
        ok(Declining.Served > 0, "The kernel did not ask the resolver that answers no name\n");
    }
    StopResolver(&Answering);
    StopResolver(&Declining);

    KmtCloseDriver();
    KmtUnloadDriver();
}
