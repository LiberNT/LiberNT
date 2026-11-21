/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between NETIO's WSK name resolution and the DNS Client service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WSKNAME_DEVICE_NAME L"\\Device\\NetioNameResolver"
#define WSKNAME_DOS_DEVICE_NAME L"\\DosDevices\\NetioNameResolver"

#define WSKNAME_IOCTL(Function) CTL_CODE(FILE_DEVICE_NETWORK, 0xA00 + (Function), METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Output: WSKNAME_REQUEST. Pends until NETIO has a name the service must resolve. */
#define IOCTL_WSKNAME_GET_REQUEST WSKNAME_IOCTL(0)
/* Input: WSKNAME_REPLY for a request this handle received. */
#define IOCTL_WSKNAME_COMPLETE_REQUEST WSKNAME_IOCTL(1)

#define WSKNAME_MAX_HOST 1025
#define WSKNAME_MAX_SERVICE 64
#define WSKNAME_MAX_REPLY_ADDRESSES 64

typedef enum _WSKNAME_OPERATION
{
    WskNameAddressInfo = 1,
    WskNameNameInfo = 2
} WSKNAME_OPERATION;

typedef struct _WSKNAME_REQUEST
{
    ULONG Id;
    ULONG Operation;
    ULONG NameSpace;
    LONG Family;
    LONG Flags;
    LONG SocketType;
    LONG Protocol;
    SOCKADDR_INET Address;
    WCHAR Node[WSKNAME_MAX_HOST];
    WCHAR Service[WSKNAME_MAX_SERVICE];
} WSKNAME_REQUEST, *PWSKNAME_REQUEST;

typedef struct _WSKNAME_REPLY
{
    ULONG Id;
    LONG Status;
    ULONG AddressCount;
    SOCKADDR_INET Address[WSKNAME_MAX_REPLY_ADDRESSES];
    WCHAR Name[WSKNAME_MAX_HOST];
    WCHAR Service[WSKNAME_MAX_SERVICE];
} WSKNAME_REPLY, *PWSKNAME_REPLY;
