/*
 * PROJECT:     LiberNT DNS Client service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resolves the names NETIO's kernel WSK clients ask for
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <drivers/netio/wsknameio.h>

#define NDEBUG
#include <debug.h>

static HANDLE WskNameDevice = INVALID_HANDLE_VALUE;
static HANDLE WskNameThread;
static volatile LONG WskNameStopping;

static
VOID
WskNameAnswerAddress(
    _In_ const WSKNAME_REQUEST *Request,
    _Out_ PWSKNAME_REPLY Reply)
{
    ADDRINFOW Hints;
    PADDRINFOW Head = NULL, Entry;
    PCWSTR Service = Request->Service[0] != UNICODE_NULL ? Request->Service : NULL;
    PCWSTR Node = Request->Node[0] != UNICODE_NULL ? Request->Node : NULL;
    INT Error;

    ZeroMemory(&Hints, sizeof(Hints));
    Hints.ai_family = Request->Family;
    Hints.ai_socktype = Request->SocketType;
    Hints.ai_protocol = Request->Protocol;
    Hints.ai_flags = Request->Flags;

    Error = GetAddrInfoW(Node, Service, &Hints, &Head);
    if (Error != 0)
    {
        DPRINT("GetAddrInfoW(%S) failed: %d\n", Node, Error);
        Reply->Status = STATUS_OBJECT_NAME_NOT_FOUND;
        return;
    }

    for (Entry = Head; Entry != NULL; Entry = Entry->ai_next)
    {
        if (Reply->AddressCount >= WSKNAME_MAX_REPLY_ADDRESSES)
            break;
        if (Entry->ai_addrlen > sizeof(SOCKADDR_INET))
            continue;
        CopyMemory(&Reply->Address[Reply->AddressCount], Entry->ai_addr, Entry->ai_addrlen);
        Reply->AddressCount++;
    }
    if (Head->ai_canonname != NULL)
        StringCchCopyW(Reply->Name, WSKNAME_MAX_HOST, Head->ai_canonname);

    FreeAddrInfoW(Head);
    Reply->Status = Reply->AddressCount != 0 ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
}

static
VOID
WskNameAnswerName(
    _In_ const WSKNAME_REQUEST *Request,
    _Out_ PWSKNAME_REPLY Reply)
{
    socklen_t Length = Request->Address.si_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN);
    INT Error;

    Error = GetNameInfoW((const SOCKADDR *)&Request->Address,
                         Length,
                         Reply->Name,
                         WSKNAME_MAX_HOST,
                         Reply->Service,
                         WSKNAME_MAX_SERVICE,
                         Request->Flags);
    if (Error != 0)
    {
        DPRINT("GetNameInfoW failed: %d\n", Error);
        Reply->Status = STATUS_OBJECT_NAME_NOT_FOUND;
        return;
    }
    Reply->Status = STATUS_SUCCESS;
}

/* NETIO may still be starting, so the device is opened here and not at service start. */
static
BOOL
WskNameOpen(VOID)
{
    while (InterlockedCompareExchange(&WskNameStopping, 0, 0) == 0)
    {
        WskNameDevice = CreateFileW(L"\\\\.\\NetioNameResolver",
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    NULL,
                                    OPEN_EXISTING,
                                    0,
                                    NULL);
        if (WskNameDevice != INVALID_HANDLE_VALUE)
            return TRUE;
        Sleep(1000);
    }
    return FALSE;
}

static
DWORD
WINAPI
WskNameRoutine(
    _In_ LPVOID Parameter)
{
    WSKNAME_REQUEST Request;
    PWSKNAME_REPLY Reply;
    DWORD Returned;

    UNREFERENCED_PARAMETER(Parameter);

    if (!WskNameOpen())
        return 0;

    Reply = HeapAlloc(GetProcessHeap(), 0, sizeof(*Reply));
    if (Reply == NULL)
        return 0;

    while (InterlockedCompareExchange(&WskNameStopping, 0, 0) == 0)
    {
        if (!DeviceIoControl(WskNameDevice,
                             IOCTL_WSKNAME_GET_REQUEST,
                             NULL,
                             0,
                             &Request,
                             sizeof(Request),
                             &Returned,
                             NULL))
        {
            if (GetLastError() == ERROR_OPERATION_ABORTED)
                break;
            DPRINT1("DNSRSLVR: the name resolver could not take a request (%lu)\n", GetLastError());
            break;
        }
        if (Returned != sizeof(Request))
            continue;

        ZeroMemory(Reply, sizeof(*Reply));
        Reply->Id = Request.Id;
        Request.Node[WSKNAME_MAX_HOST - 1] = UNICODE_NULL;
        Request.Service[WSKNAME_MAX_SERVICE - 1] = UNICODE_NULL;

        if (Request.Operation == WskNameAddressInfo)
            WskNameAnswerAddress(&Request, Reply);
        else if (Request.Operation == WskNameNameInfo)
            WskNameAnswerName(&Request, Reply);
        else
            Reply->Status = STATUS_INVALID_PARAMETER;

        DeviceIoControl(WskNameDevice,
                        IOCTL_WSKNAME_COMPLETE_REQUEST,
                        Reply,
                        sizeof(*Reply),
                        NULL,
                        0,
                        &Returned,
                        NULL);
    }

    HeapFree(GetProcessHeap(), 0, Reply);
    return 0;
}

VOID
WskNameStart(VOID)
{
    WskNameThread = CreateThread(NULL, 0, WskNameRoutine, NULL, 0, NULL);
}

VOID
WskNameStop(VOID)
{
    if (WskNameThread == NULL)
        return;

    InterlockedExchange(&WskNameStopping, 1);
    if (WskNameDevice != INVALID_HANDLE_VALUE)
        CancelIoEx(WskNameDevice, NULL);
    WaitForSingleObject(WskNameThread, 5000);
    CloseHandle(WskNameThread);
    WskNameThread = NULL;
    if (WskNameDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(WskNameDevice);
        WskNameDevice = INVALID_HANDLE_VALUE;
    }
}
