/*
 * PROJECT:     NetAPI DLL
 * LICENSE:     GPL-2.0 (https://spdx.org/licenses/GPL-2.0)
 * PURPOSE:     SAM service interface code
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "netapi32.h"

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

/* FUNCTIONS *****************************************************************/

static
DWORD
DisplayStringSize(
    _In_ PUNICODE_STRING String)
{
    return String->Length + sizeof(WCHAR);
}


static
LPWSTR
CopyDisplayString(
    _In_ PUNICODE_STRING String,
    _Inout_ LPWSTR *Ptr)
{
    LPWSTR Result = *Ptr;

    if (String->Length != 0)
        memcpy(Result, String->Buffer, String->Length);
    Result[String->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *Ptr = (LPWSTR)((ULONG_PTR)Result + String->Length + sizeof(WCHAR));

    return Result;
}


static
VOID
FreeSamDisplayBuffer(
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformation,
    _In_ PVOID SamBuffer,
    _In_ ULONG EntryCount)
{
    PDOMAIN_DISPLAY_USER Users = SamBuffer;
    PDOMAIN_DISPLAY_MACHINE Machines = SamBuffer;
    PDOMAIN_DISPLAY_GROUP Groups = SamBuffer;
    ULONG i;

    if (SamBuffer == NULL)
        return;

    for (i = 0; i < EntryCount; i++)
    {
        switch (DisplayInformation)
        {
            case DomainDisplayUser:
                SamFreeMemory(Users[i].LogonName.Buffer);
                SamFreeMemory(Users[i].AdminComment.Buffer);
                SamFreeMemory(Users[i].FullName.Buffer);
                break;

            case DomainDisplayMachine:
                SamFreeMemory(Machines[i].Machine.Buffer);
                SamFreeMemory(Machines[i].Comment.Buffer);
                break;

            default:
                SamFreeMemory(Groups[i].Group.Buffer);
                SamFreeMemory(Groups[i].Comment.Buffer);
                break;
        }
    }

    SamFreeMemory(SamBuffer);
}


static
NET_API_STATUS
BuildDisplayBuffer(
    _In_ DOMAIN_DISPLAY_INFORMATION DisplayInformation,
    _In_ PVOID SamBuffer,
    _In_ ULONG EntryCount,
    _Out_ PVOID *Buffer)
{
    PDOMAIN_DISPLAY_USER Users = SamBuffer;
    PDOMAIN_DISPLAY_MACHINE Machines = SamBuffer;
    PDOMAIN_DISPLAY_GROUP Groups = SamBuffer;
    PNET_DISPLAY_USER NetUsers;
    PNET_DISPLAY_MACHINE NetMachines;
    PNET_DISPLAY_GROUP NetGroups;
    PVOID LocalBuffer = NULL;
    LPWSTR Ptr;
    DWORD Size = 0;
    ULONG i;
    NET_API_STATUS ApiStatus;

    *Buffer = NULL;

    for (i = 0; i < EntryCount; i++)
    {
        switch (DisplayInformation)
        {
            case DomainDisplayUser:
                Size += sizeof(NET_DISPLAY_USER) +
                        DisplayStringSize(&Users[i].LogonName) +
                        DisplayStringSize(&Users[i].AdminComment) +
                        DisplayStringSize(&Users[i].FullName);
                break;

            case DomainDisplayMachine:
                Size += sizeof(NET_DISPLAY_MACHINE) +
                        DisplayStringSize(&Machines[i].Machine) +
                        DisplayStringSize(&Machines[i].Comment);
                break;

            default:
                Size += sizeof(NET_DISPLAY_GROUP) +
                        DisplayStringSize(&Groups[i].Group) +
                        DisplayStringSize(&Groups[i].Comment);
                break;
        }
    }

    ApiStatus = NetApiBufferAllocate(Size, &LocalBuffer);
    if (ApiStatus != NERR_Success)
        return ApiStatus;

    NetUsers = LocalBuffer;
    NetMachines = LocalBuffer;
    NetGroups = LocalBuffer;

    switch (DisplayInformation)
    {
        case DomainDisplayUser:
            Ptr = (LPWSTR)&NetUsers[EntryCount];
            for (i = 0; i < EntryCount; i++)
            {
                NetUsers[i].usri1_name = CopyDisplayString(&Users[i].LogonName, &Ptr);
                NetUsers[i].usri1_comment = CopyDisplayString(&Users[i].AdminComment, &Ptr);
                NetUsers[i].usri1_flags = GetAccountFlags(Users[i].AccountControl, NULL);
                NetUsers[i].usri1_full_name = CopyDisplayString(&Users[i].FullName, &Ptr);
                NetUsers[i].usri1_user_id = Users[i].Rid;
                NetUsers[i].usri1_next_index = Users[i].Index;
            }
            break;

        case DomainDisplayMachine:
            Ptr = (LPWSTR)&NetMachines[EntryCount];
            for (i = 0; i < EntryCount; i++)
            {
                NetMachines[i].usri2_name = CopyDisplayString(&Machines[i].Machine, &Ptr);
                NetMachines[i].usri2_comment = CopyDisplayString(&Machines[i].Comment, &Ptr);
                NetMachines[i].usri2_flags = GetAccountFlags(Machines[i].AccountControl, NULL);
                NetMachines[i].usri2_user_id = Machines[i].Rid;
                NetMachines[i].usri2_next_index = Machines[i].Index;
            }
            break;

        default:
            Ptr = (LPWSTR)&NetGroups[EntryCount];
            for (i = 0; i < EntryCount; i++)
            {
                NetGroups[i].grpi3_name = CopyDisplayString(&Groups[i].Group, &Ptr);
                NetGroups[i].grpi3_comment = CopyDisplayString(&Groups[i].Comment, &Ptr);
                NetGroups[i].grpi3_group_id = Groups[i].Rid;
                NetGroups[i].grpi3_attributes = Groups[i].Attributes;
                NetGroups[i].grpi3_next_index = Groups[i].Index;
            }
            break;
    }

    *Buffer = LocalBuffer;

    return NERR_Success;
}

/* PUBLIC FUNCTIONS **********************************************************/

NET_API_STATUS
WINAPI
NetQueryDisplayInformation(
    _In_ LPCWSTR ServerName,
    _In_ DWORD Level,
    _In_ DWORD Index,
    _In_ DWORD EntriesRequested,
    _In_ DWORD PreferredMaximumLength,
    _Out_ LPDWORD ReturnedEntryCount,
    _Out_ PVOID *SortedBuffer)
{
    UNICODE_STRING ServerNameString;
    SAM_HANDLE ServerHandle = NULL;
    SAM_HANDLE DomainHandle = NULL;
    DOMAIN_DISPLAY_INFORMATION DisplayInformation;
    DWORD LocalTotalBytesAvailable;
    DWORD LocalTotalBytesReturned;
    DWORD LocalReturnedEntryCount;
    PVOID LocalSortedBuffer;
    NET_API_STATUS ApiStatus = NERR_Success;
    NTSTATUS Status;

    TRACE("NetQueryDisplayInformation(%s, %ld, %ld, %ld, %ld, %p, %p)\n",
          debugstr_w(ServerName), Level, Index, EntriesRequested,
          PreferredMaximumLength, ReturnedEntryCount, SortedBuffer);

    *ReturnedEntryCount = 0;
    *SortedBuffer = NULL;

    switch (Level)
    {
        case 1:
            DisplayInformation = DomainDisplayUser;
            break;

        case 2:
            DisplayInformation = DomainDisplayMachine;
            break;

        case 3:
            DisplayInformation = DomainDisplayGroup;
            break;

        default:
            return ERROR_INVALID_LEVEL;
    }

    if (ServerName != NULL)
        RtlInitUnicodeString(&ServerNameString, ServerName);

    /* Connect to the SAM Server */
    Status = SamConnect((ServerName != NULL) ? &ServerNameString : NULL,
                        &ServerHandle,
                        SAM_SERVER_CONNECT | SAM_SERVER_LOOKUP_DOMAIN,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        ERR("SamConnect failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
        goto done;
    }

    /* Open the Account Domain */
    Status = OpenAccountDomain(ServerHandle,
                               (ServerName != NULL) ? &ServerNameString : NULL,
                               DOMAIN_LIST_ACCOUNTS,
                               &DomainHandle);
    if (!NT_SUCCESS(Status))
    {
        ERR("OpenAccountDomain failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
        goto done;
    }

    /* Query the information */
    Status = SamQueryDisplayInformation(DomainHandle,
                                        DisplayInformation,
                                        Index,
                                        EntriesRequested,
                                        PreferredMaximumLength,
                                        &LocalTotalBytesAvailable,
                                        &LocalTotalBytesReturned,
                                        &LocalReturnedEntryCount,
                                        &LocalSortedBuffer);
    if (!NT_SUCCESS(Status))
    {
        ERR("SamQueryDisplayInformation failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
        goto done;
    }

    if (LocalReturnedEntryCount != 0)
    {
        ApiStatus = BuildDisplayBuffer(DisplayInformation,
                                       LocalSortedBuffer,
                                       LocalReturnedEntryCount,
                                       SortedBuffer);
        if (ApiStatus == NERR_Success)
            *ReturnedEntryCount = LocalReturnedEntryCount;
    }

    FreeSamDisplayBuffer(DisplayInformation,
                         LocalSortedBuffer,
                         LocalReturnedEntryCount);

    if (ApiStatus == NERR_Success && Status == STATUS_MORE_ENTRIES)
        ApiStatus = ERROR_MORE_DATA;

done:
    if (DomainHandle != NULL)
        SamCloseHandle(DomainHandle);

    if (ServerHandle != NULL)
        SamCloseHandle(ServerHandle);

    return ApiStatus;
}


NET_API_STATUS
WINAPI
NetGetDisplayInformationIndex(
    _In_ LPCWSTR ServerName,
    _In_ DWORD Level,
    _In_ LPCWSTR Prefix,
    _Out_ LPDWORD Index)
{
    UNICODE_STRING ServerNameString, PrefixString;
    SAM_HANDLE ServerHandle = NULL;
    SAM_HANDLE DomainHandle = NULL;
    DOMAIN_DISPLAY_INFORMATION DisplayInformation;
    NET_API_STATUS ApiStatus = NERR_Success;
    NTSTATUS Status;

    TRACE("NetGetDisplayInformationIndex(%s %ld %s %p)\n",
          debugstr_w(ServerName), Level, debugstr_w(Prefix), Index);

    switch (Level)
    {
        case 1:
            DisplayInformation = DomainDisplayUser;
            break;

        case 2:
            DisplayInformation = DomainDisplayMachine;
            break;

        case 3:
            DisplayInformation = DomainDisplayGroup;
            break;

        default:
            return ERROR_INVALID_LEVEL;
    }

    if (ServerName != NULL)
        RtlInitUnicodeString(&ServerNameString, ServerName);

    /* Connect to the SAM Server */
    Status = SamConnect((ServerName != NULL) ? &ServerNameString : NULL,
                        &ServerHandle,
                        SAM_SERVER_CONNECT | SAM_SERVER_LOOKUP_DOMAIN,
                        NULL);
    if (!NT_SUCCESS(Status))
    {
        ERR("SamConnect failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
        goto done;
    }

    /* Open the Account Domain */
    Status = OpenAccountDomain(ServerHandle,
                               (ServerName != NULL) ? &ServerNameString : NULL,
                               DOMAIN_LIST_ACCOUNTS,
                               &DomainHandle);
    if (!NT_SUCCESS(Status))
    {
        ERR("OpenAccountDomain failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
        goto done;
    }

    RtlInitUnicodeString(&PrefixString, Prefix);

    /* Get the index */
    Status = SamGetDisplayEnumerationIndex(DomainHandle,
                                           DisplayInformation,
                                           &PrefixString,
                                           Index);
    if (!NT_SUCCESS(Status))
    {
        ERR("SamGetDisplayEnumerationIndex failed (Status %08lx)\n", Status);
        ApiStatus = NetpNtStatusToApiStatus(Status);
    }

done:
    if (DomainHandle != NULL)
        SamCloseHandle(DomainHandle);

    if (ServerHandle != NULL)
        SamCloseHandle(ServerHandle);

    return ApiStatus;
}

/* EOF */
