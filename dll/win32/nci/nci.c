/*
 * PROJECT:     LiberNT Network Connection Interface
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Names of network connections
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <windows.h>
#include <strsafe.h>

#define NETWORK_CLASS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define MAX_CONNECTION_NAME 256

static BOOL
FormatGuid(_In_ const GUID *Guid, _Out_writes_(39) PWSTR Text)
{
    return SUCCEEDED(StringCchPrintfW(Text, 39, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                                      Guid->Data1, Guid->Data2, Guid->Data3,
                                      Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
                                      Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]));
}

static LSTATUS
CheckNameIsFree(_In_ HKEY Network, _In_ PCWSTR Self, _In_ PCWSTR Name)
{
    WCHAR Connection[64], Existing[MAX_CONNECTION_NAME];
    DWORD Index, Length, Size;
    LSTATUS Error;

    for (Index = 0; ; Index++)
    {
        Length = ARRAYSIZE(Connection);
        Error = RegEnumKeyExW(Network, Index, Connection, &Length, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS)
            return ERROR_SUCCESS;
        if (Error != ERROR_SUCCESS)
            return Error;
        if (!_wcsicmp(Connection, Self))
            continue;
        if (FAILED(StringCchCatW(Connection, ARRAYSIZE(Connection), L"\\Connection")))
            return ERROR_INVALID_DATA;
        Size = sizeof(Existing);
        Error = RegGetValueW(Network, Connection, L"Name", RRF_RT_REG_SZ, NULL, Existing, &Size);
        /* Network also holds keys of components that are no connections */
        if (Error == ERROR_FILE_NOT_FOUND)
            continue;
        if (Error != ERROR_SUCCESS)
            return Error;
        if (!_wcsicmp(Existing, Name))
            return ERROR_DUP_NAME;
    }
}

DWORD
WINAPI
NciSetConnectionName(
    _In_ const GUID *Guid,
    _In_z_ LPCWSTR NewName)
{
    WCHAR Id[39], Path[64];
    HKEY Network, Connection;
    HANDLE Lock;
    size_t Length;
    LSTATUS Error;
    DWORD Wait;

    if (Guid == NULL || NewName == NULL ||
        FAILED(StringCchLengthW(NewName, MAX_CONNECTION_NAME, &Length)) || Length == 0)
    {
        return ERROR_INVALID_PARAMETER;
    }
    if (!FormatGuid(Guid, Id) || FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\Connection", Id)))
        return ERROR_INVALID_PARAMETER;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, NETWORK_CLASS_KEY, 0, NULL, 0,
                            KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY, NULL, &Network, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;

    /* Two renames must not both find the same name free */
    Lock = CreateMutexW(NULL, FALSE, L"Global\\NetworkConnectionNames");
    if (!Lock)
    {
        Error = GetLastError();
        RegCloseKey(Network);
        return Error;
    }
    Wait = WaitForSingleObject(Lock, INFINITE);
    if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED)
    {
        CloseHandle(Lock);
        RegCloseKey(Network);
        return ERROR_GEN_FAILURE;
    }

    Error = CheckNameIsFree(Network, Id, NewName);
    if (Error == ERROR_SUCCESS)
        Error = RegCreateKeyExW(Network, Path, 0, NULL, 0, KEY_SET_VALUE, NULL, &Connection, NULL);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Connection, L"Name", 0, REG_SZ, (const BYTE *)NewName,
                               (DWORD)((Length + 1) * sizeof(WCHAR)));
        RegCloseKey(Connection);
    }

    ReleaseMutex(Lock);
    CloseHandle(Lock);
    RegCloseKey(Network);
    return Error;
}

DWORD
WINAPI
NciGetConnectionName(
    _In_ const GUID *Guid,
    _Out_writes_bytes_opt_(InDestNameBytes) LPWSTR Name,
    _In_ DWORD InDestNameBytes,
    _Out_opt_ DWORD *OutDestNameBytes)
{
    WCHAR Id[39], Path[ARRAYSIZE(NETWORK_CLASS_KEY) + 64];
    DWORD Size = InDestNameBytes;
    LSTATUS Error;

    if (OutDestNameBytes)
        *OutDestNameBytes = 0;
    if (Guid == NULL || (Name == NULL && InDestNameBytes != 0) || !FormatGuid(Guid, Id) ||
        FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), NETWORK_CLASS_KEY L"\\%s\\Connection", Id)))
    {
        return ERROR_INVALID_PARAMETER;
    }

    Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Name", RRF_RT_REG_SZ, NULL, Name, &Size);
    if (Error == ERROR_SUCCESS || Error == ERROR_MORE_DATA)
    {
        /* Windows 11 answers a size query without a buffer with success */
        if (OutDestNameBytes)
            *OutDestNameBytes = Size;
    }
    return Error;
}
