/*
 * PROJECT:     LiberNT Network API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sorted display information of the accounts of a SAM account domain
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "netapi32.h"

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

#define NETP_DISPLAY_MAX_ENTRIES 100

static
NET_API_STATUS
NetpDisplayClassFromLevel(
    _In_ DWORD Level,
    _Out_ DOMAIN_DISPLAY_INFORMATION *DisplayClass)
{
    switch (Level)
    {
        case 1:
            *DisplayClass = DomainDisplayUser;
            return NERR_Success;

        case 2:
            *DisplayClass = DomainDisplayMachine;
            return NERR_Success;

        case 3:
            *DisplayClass = DomainDisplayGroup;
            return NERR_Success;
    }

    return ERROR_INVALID_LEVEL;
}

static
NTSTATUS
NetpOpenDisplayDomain(
    _In_opt_ LPCWSTR ServerName,
    _Out_ SAM_HANDLE *ServerHandle,
    _Out_ SAM_HANDLE *DomainHandle)
{
    UNICODE_STRING Server;
    PUNICODE_STRING ServerString = NULL;
    NTSTATUS Status;

    *ServerHandle = NULL;
    *DomainHandle = NULL;

    if (ServerName != NULL)
    {
        RtlInitUnicodeString(&Server, ServerName);
        ServerString = &Server;
    }

    Status = SamConnect(ServerString,
                        ServerHandle,
                        SAM_SERVER_CONNECT | SAM_SERVER_LOOKUP_DOMAIN,
                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = OpenAccountDomain(*ServerHandle,
                               ServerString,
                               DOMAIN_LIST_ACCOUNTS,
                               DomainHandle);
    if (!NT_SUCCESS(Status))
    {
        SamCloseHandle(*ServerHandle);
        *ServerHandle = NULL;
    }

    return Status;
}

static
VOID
NetpCloseDisplayDomain(
    _In_ SAM_HANDLE ServerHandle,
    _In_ SAM_HANDLE DomainHandle)
{
    SamCloseHandle(DomainHandle);
    SamCloseHandle(ServerHandle);
}

static
SIZE_T
NetpDisplayStringSize(
    _In_ PCUNICODE_STRING String)
{
    ASSERT((String->Length & 1) == 0);
    return (SIZE_T)String->Length + sizeof(WCHAR);
}

static
LPWSTR
NetpPackDisplayString(
    _In_ PCUNICODE_STRING String,
    _Inout_ PBYTE *Cursor)
{
    LPWSTR Destination = (LPWSTR)*Cursor;

    if (String->Length != 0)
        RtlCopyMemory(Destination, String->Buffer, String->Length);
    Destination[String->Length / sizeof(WCHAR)] = UNICODE_NULL;
    *Cursor += NetpDisplayStringSize(String);
    return Destination;
}

static
NET_API_STATUS
NetpBuildDisplayBuffer(
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayClass,
    _In_ ULONG Count,
    _In_ PVOID SamBuffer,
    _Out_ PVOID *Buffer)
{
    SIZE_T Size = 0;
    PBYTE Cursor;
    PVOID Output;
    ULONG i;

    *Buffer = NULL;
    if (Count == 0)
        return NERR_Success;

    for (i = 0; i < Count; i++)
    {
        switch (DisplayClass)
        {
            case DomainDisplayUser:
            {
                PDOMAIN_DISPLAY_USER User = &((PDOMAIN_DISPLAY_USER)SamBuffer)[i];
                Size += sizeof(NET_DISPLAY_USER) +
                        NetpDisplayStringSize(&User->LogonName) +
                        NetpDisplayStringSize(&User->AdminComment) +
                        NetpDisplayStringSize(&User->FullName);
                break;
            }

            case DomainDisplayMachine:
            {
                PDOMAIN_DISPLAY_MACHINE Machine = &((PDOMAIN_DISPLAY_MACHINE)SamBuffer)[i];
                Size += sizeof(NET_DISPLAY_MACHINE) +
                        NetpDisplayStringSize(&Machine->Machine) +
                        NetpDisplayStringSize(&Machine->Comment);
                break;
            }

            default:
            {
                PDOMAIN_DISPLAY_GROUP Group = &((PDOMAIN_DISPLAY_GROUP)SamBuffer)[i];
                Size += sizeof(NET_DISPLAY_GROUP) +
                        NetpDisplayStringSize(&Group->Group) +
                        NetpDisplayStringSize(&Group->Comment);
                break;
            }
        }
    }

    if (Size > MAXDWORD)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (NetApiBufferAllocate((DWORD)Size, &Output) != NERR_Success)
        return ERROR_NOT_ENOUGH_MEMORY;

    switch (DisplayClass)
    {
        case DomainDisplayUser:
        {
            PNET_DISPLAY_USER Entries = Output;
            Cursor = (PBYTE)&Entries[Count];
            for (i = 0; i < Count; i++)
            {
                PDOMAIN_DISPLAY_USER User = &((PDOMAIN_DISPLAY_USER)SamBuffer)[i];
                Entries[i].usri1_name = NetpPackDisplayString(&User->LogonName, &Cursor);
                Entries[i].usri1_comment = NetpPackDisplayString(&User->AdminComment, &Cursor);
                Entries[i].usri1_flags = GetAccountFlags(User->AccountControl, NULL);
                Entries[i].usri1_full_name = NetpPackDisplayString(&User->FullName, &Cursor);
                Entries[i].usri1_user_id = User->Rid;
                Entries[i].usri1_next_index = User->Index;
            }
            break;
        }

        case DomainDisplayMachine:
        {
            PNET_DISPLAY_MACHINE Entries = Output;
            Cursor = (PBYTE)&Entries[Count];
            for (i = 0; i < Count; i++)
            {
                PDOMAIN_DISPLAY_MACHINE Machine = &((PDOMAIN_DISPLAY_MACHINE)SamBuffer)[i];
                Entries[i].usri2_name = NetpPackDisplayString(&Machine->Machine, &Cursor);
                Entries[i].usri2_comment = NetpPackDisplayString(&Machine->Comment, &Cursor);
                Entries[i].usri2_flags = GetAccountFlags(Machine->AccountControl, NULL);
                Entries[i].usri2_user_id = Machine->Rid;
                Entries[i].usri2_next_index = Machine->Index;
            }
            break;
        }

        default:
        {
            PNET_DISPLAY_GROUP Entries = Output;
            Cursor = (PBYTE)&Entries[Count];
            for (i = 0; i < Count; i++)
            {
                PDOMAIN_DISPLAY_GROUP Group = &((PDOMAIN_DISPLAY_GROUP)SamBuffer)[i];
                Entries[i].grpi3_name = NetpPackDisplayString(&Group->Group, &Cursor);
                Entries[i].grpi3_comment = NetpPackDisplayString(&Group->Comment, &Cursor);
                Entries[i].grpi3_group_id = Group->Rid;
                Entries[i].grpi3_attributes = Group->Attributes;
                Entries[i].grpi3_next_index = Group->Index;
            }
            break;
        }
    }

    ASSERT(Cursor == (PBYTE)Output + Size);
    *Buffer = Output;
    return NERR_Success;
}

NET_API_STATUS
WINAPI
NetQueryDisplayInformation(
    _In_opt_ LPCWSTR ServerName,
    _In_ DWORD Level,
    _In_ DWORD Index,
    _In_ DWORD EntriesRequested,
    _In_ DWORD PreferredMaximumLength,
    _Out_ LPDWORD ReturnedEntryCount,
    _Outptr_result_maybenull_ PVOID *SortedBuffer)
{
    DOMAIN_DISPLAY_INFORMATION DisplayClass;
    SAM_HANDLE ServerHandle, DomainHandle;
    ULONG TotalAvailable, TotalReturned, Returned = 0;
    PVOID SamBuffer = NULL;
    NET_API_STATUS ApiStatus;
    NTSTATUS Status;
    BOOL MoreEntries;

    TRACE("NetQueryDisplayInformation(%s %lu %lu %lu %lu %p %p)\n", debugstr_w(ServerName),
          Level, Index, EntriesRequested, PreferredMaximumLength, ReturnedEntryCount, SortedBuffer);

    if (ReturnedEntryCount == NULL || SortedBuffer == NULL)
        return ERROR_INVALID_PARAMETER;

    *ReturnedEntryCount = 0;
    *SortedBuffer = NULL;

    ApiStatus = NetpDisplayClassFromLevel(Level, &DisplayClass);
    if (ApiStatus != NERR_Success)
        return ApiStatus;

    Status = NetpOpenDisplayDomain(ServerName, &ServerHandle, &DomainHandle);
    if (!NT_SUCCESS(Status))
        return NetpNtStatusToApiStatus(Status);

    Status = SamQueryDisplayInformation(DomainHandle,
                                        DisplayClass,
                                        Index,
                                        min(EntriesRequested, NETP_DISPLAY_MAX_ENTRIES),
                                        PreferredMaximumLength,
                                        &TotalAvailable,
                                        &TotalReturned,
                                        &Returned,
                                        &SamBuffer);
    if (!NT_SUCCESS(Status))
    {
        NetpCloseDisplayDomain(ServerHandle, DomainHandle);
        return NetpNtStatusToApiStatus(Status);
    }

    MoreEntries = (Status == STATUS_MORE_ENTRIES);
    ApiStatus = NetpBuildDisplayBuffer(DisplayClass, Returned, SamBuffer, SortedBuffer);
    if (SamBuffer != NULL)
        SamFreeMemory(SamBuffer);
    NetpCloseDisplayDomain(ServerHandle, DomainHandle);

    if (ApiStatus != NERR_Success)
        return ApiStatus;

    *ReturnedEntryCount = Returned;
    return MoreEntries ? ERROR_MORE_DATA : NERR_Success;
}

NET_API_STATUS
WINAPI
NetGetDisplayInformationIndex(
    _In_opt_ LPCWSTR ServerName,
    _In_ DWORD Level,
    _In_ LPCWSTR Prefix,
    _Out_ LPDWORD Index)
{
    DOMAIN_DISPLAY_INFORMATION DisplayClass;
    SAM_HANDLE ServerHandle, DomainHandle;
    UNICODE_STRING PrefixString;
    NET_API_STATUS ApiStatus;
    NTSTATUS Status;

    TRACE("NetGetDisplayInformationIndex(%s %lu %s %p)\n",
          debugstr_w(ServerName), Level, debugstr_w(Prefix), Index);

    if (Prefix == NULL || Index == NULL)
        return ERROR_INVALID_PARAMETER;

    ApiStatus = NetpDisplayClassFromLevel(Level, &DisplayClass);
    if (ApiStatus != NERR_Success)
        return ApiStatus;

    Status = NetpOpenDisplayDomain(ServerName, &ServerHandle, &DomainHandle);
    if (!NT_SUCCESS(Status))
        return NetpNtStatusToApiStatus(Status);

    RtlInitUnicodeString(&PrefixString, Prefix);
    Status = SamGetDisplayEnumerationIndex(DomainHandle, DisplayClass, &PrefixString, Index);
    NetpCloseDisplayDomain(ServerHandle, DomainHandle);

    if (Status == STATUS_NO_MORE_ENTRIES)
        return ERROR_NO_MORE_ITEMS;
    return NetpNtStatusToApiStatus(Status);
}
