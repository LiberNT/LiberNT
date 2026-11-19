/*
 * PROJECT:     LiberNT Network API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service statistics, domain name change notification and string helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "netapi32.h"

#include <lmconfig.h>
#include "srvsvc_c.h"
#include "wkssvc_c.h"

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

#define NETP_DOMAIN_NAME_NOTIFICATION PolicyNotifyDnsDomainInformation
#define NETP_SERVICE_WORKSTATION L"LanmanWorkstation"
#define NETP_SERVICE_SERVER L"LanmanServer"

PSTR
WINAPI
NetpAllocAnsiStrFromWStr(
    _In_ PWSTR InString)
{
    PSTR OutString;
    int Length;

    ASSERT(InString != NULL);

    Length = WideCharToMultiByte(CP_ACP, 0, InString, -1, NULL, 0, NULL, NULL);
    if (Length <= 0)
        return NULL;

    if (NetApiBufferAllocate((DWORD)Length, (PVOID *)&OutString) != NERR_Success)
        return NULL;

    if (WideCharToMultiByte(CP_ACP, 0, InString, -1, OutString, Length, NULL, NULL) != Length)
    {
        NetApiBufferFree(OutString);
        return NULL;
    }

    return OutString;
}

PWSTR
WINAPI
NetpAllocWStrFromAnsiStr(
    _In_ PSTR InString)
{
    PWSTR OutString;
    int Length;

    ASSERT(InString != NULL);

    Length = MultiByteToWideChar(CP_ACP, 0, InString, -1, NULL, 0);
    if (Length <= 0)
        return NULL;

    if (NetApiBufferAllocate((DWORD)Length * sizeof(WCHAR), (PVOID *)&OutString) != NERR_Success)
        return NULL;

    if (MultiByteToWideChar(CP_ACP, 0, InString, -1, OutString, Length) != Length)
    {
        NetApiBufferFree(OutString);
        return NULL;
    }

    return OutString;
}

NET_API_STATUS
WINAPI
NetpNtStatusToApiStatus(
    _In_ NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
            return NERR_Success;

        case STATUS_NO_SUCH_USER:
            return NERR_UserNotFound;

        case STATUS_NO_SUCH_GROUP:
        case STATUS_NO_SUCH_ALIAS:
            return NERR_GroupNotFound;

        case STATUS_USER_EXISTS:
            return NERR_UserExists;

        case STATUS_GROUP_EXISTS:
            return NERR_GroupExists;

        case STATUS_INVALID_ACCOUNT_NAME:
            return NERR_BadUsername;

        case STATUS_MEMBER_NOT_IN_GROUP:
            return NERR_UserNotInGroup;

        case STATUS_PASSWORD_RESTRICTION:
            return NERR_PasswordTooShort;
    }

    return RtlNtStatusToDosError(Status);
}

NET_API_STATUS
WINAPI
NetStatisticsGet(
    _In_opt_ LMSTR ServerName,
    _In_ LMSTR Service,
    _In_ DWORD Level,
    _In_ DWORD Options,
    _Out_ LPBYTE *Bufptr)
{
    NET_API_STATUS ApiStatus;

    TRACE("NetStatisticsGet(%s %s %lu %lu %p)\n",
          debugstr_w(ServerName), debugstr_w(Service), Level, Options, Bufptr);

    if (Service == NULL || Bufptr == NULL)
        return ERROR_INVALID_PARAMETER;

    *Bufptr = NULL;

    if (_wcsicmp(Service, NETP_SERVICE_WORKSTATION) == 0)
    {
        RpcTryExcept
        {
            ApiStatus = NetrWorkstationStatisticsGet(ServerName,
                                                     Service,
                                                     Level,
                                                     Options,
                                                     (LPSTAT_WORKSTATION_0 *)Bufptr);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
            ApiStatus = I_RpcMapWin32Status(RpcExceptionCode());
        }
        RpcEndExcept;

        return ApiStatus;
    }

    if (_wcsicmp(Service, NETP_SERVICE_SERVER) == 0)
    {
        RpcTryExcept
        {
            ApiStatus = NetrServerStatisticsGet(ServerName,
                                                Service,
                                                Level,
                                                Options,
                                                (LPSTAT_SERVER_0 *)Bufptr);
        }
        RpcExcept(EXCEPTION_EXECUTE_HANDLER)
        {
            ApiStatus = I_RpcMapWin32Status(RpcExceptionCode());
        }
        RpcEndExcept;

        return ApiStatus;
    }

    return ERROR_NOT_SUPPORTED;
}

NET_API_STATUS
WINAPI
NetRegisterDomainNameChangeNotification(
    _Out_ PHANDLE NotificationEventHandle)
{
    HANDLE EventHandle;
    NTSTATUS Status;

    TRACE("NetRegisterDomainNameChangeNotification(%p)\n", NotificationEventHandle);

    if (NotificationEventHandle == NULL)
        return ERROR_INVALID_PARAMETER;

    *NotificationEventHandle = NULL;

    EventHandle = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (EventHandle == NULL)
        return GetLastError();

    Status = LsaRegisterPolicyChangeNotification(NETP_DOMAIN_NAME_NOTIFICATION, EventHandle);
    if (!NT_SUCCESS(Status))
    {
        CloseHandle(EventHandle);
        return NetpNtStatusToApiStatus(Status);
    }

    *NotificationEventHandle = EventHandle;
    return NERR_Success;
}

NET_API_STATUS
WINAPI
NetUnregisterDomainNameChangeNotification(
    _In_ HANDLE NotificationEventHandle)
{
    NTSTATUS Status;

    TRACE("NetUnregisterDomainNameChangeNotification(%p)\n", NotificationEventHandle);

    if (NotificationEventHandle == NULL)
        return ERROR_INVALID_PARAMETER;

    Status = LsaUnregisterPolicyChangeNotification(NETP_DOMAIN_NAME_NOTIFICATION, NotificationEventHandle);
    CloseHandle(NotificationEventHandle);
    return NetpNtStatusToApiStatus(Status);
}
