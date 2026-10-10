/*
 * PROJECT:     LiberNT Time Zone Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Time zone registry helpers shared by setup and the control panel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include "tzlib.h"

#define TIME_ZONES_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones"
#define TIME_ZONE_INFORMATION_KEY L"SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation"

static
LONG
QueryTimeZoneString(
    _In_ HKEY hZoneKey,
    _In_ PCWSTR ValueName,
    _Out_opt_ PWCHAR Buffer,
    _Inout_opt_ PULONG BufferSize)
{
    DWORD Type;
    LONG Error;

    if (Buffer == NULL)
        return ERROR_SUCCESS;
    if (BufferSize == NULL || *BufferSize < sizeof(WCHAR))
        return ERROR_INVALID_PARAMETER;

    *BufferSize -= sizeof(WCHAR);
    Error = RegQueryValueExW(hZoneKey, ValueName, NULL, &Type, (LPBYTE)Buffer, BufferSize);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Type != REG_SZ && Type != REG_EXPAND_SZ)
        return ERROR_INVALID_DATA;

    Buffer[*BufferSize / sizeof(WCHAR)] = UNICODE_NULL;
    return ERROR_SUCCESS;
}

LONG
QueryTimeZoneData(
    _In_ HKEY hZoneKey,
    _Out_opt_ PULONG Index,
    _Out_ PREG_TZI_FORMAT TimeZoneInfo,
    _Out_opt_ PWCHAR Description,
    _Inout_opt_ PULONG DescriptionSize,
    _Out_opt_ PWCHAR StandardName,
    _Inout_opt_ PULONG StandardNameSize,
    _Out_opt_ PWCHAR DaylightName,
    _Inout_opt_ PULONG DaylightNameSize)
{
    DWORD Type;
    DWORD Size;
    LONG Error;

    if (Index != NULL)
    {
        Size = sizeof(*Index);
        Error = RegQueryValueExW(hZoneKey, L"Index", NULL, &Type, (LPBYTE)Index, &Size);
        if (Error != ERROR_SUCCESS)
            return Error;
        if (Type != REG_DWORD || Size != sizeof(*Index))
            return ERROR_INVALID_DATA;
    }

    Size = sizeof(*TimeZoneInfo);
    Error = RegQueryValueExW(hZoneKey, L"TZI", NULL, &Type, (LPBYTE)TimeZoneInfo, &Size);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (Type != REG_BINARY || Size != sizeof(*TimeZoneInfo))
        return ERROR_INVALID_DATA;

    Error = QueryTimeZoneString(hZoneKey, L"Display", Description, DescriptionSize);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = QueryTimeZoneString(hZoneKey, L"Std", StandardName, StandardNameSize);
    if (Error != ERROR_SUCCESS)
        return Error;

    return QueryTimeZoneString(hZoneKey, L"Dlt", DaylightName, DaylightNameSize);
}

VOID
EnumerateTimeZoneList(
    _In_ PENUM_TIMEZONE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    WCHAR KeyName[MAX_PATH];
    HKEY hZonesKey;
    HKEY hZoneKey;
    DWORD KeyNameLength;
    DWORD Index;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONES_KEY, 0, KEY_ENUMERATE_SUB_KEYS, &hZonesKey) != ERROR_SUCCESS)
        return;

    for (Index = 0; ; Index++)
    {
        KeyNameLength = ARRAYSIZE(KeyName);
        if (RegEnumKeyExW(hZonesKey, Index, KeyName, &KeyNameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExW(hZonesKey, KeyName, 0, KEY_QUERY_VALUE, &hZoneKey) != ERROR_SUCCESS)
            continue;

        Callback(hZoneKey, Context);
        RegCloseKey(hZoneKey);
    }

    RegCloseKey(hZonesKey);
}

static
BOOL
QueryZoneIndex(
    _In_ PCWSTR KeyName,
    _Out_ PULONG Index)
{
    REG_TZI_FORMAT TimeZoneInfo;
    HKEY hZonesKey;
    HKEY hZoneKey;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONES_KEY, 0, KEY_ENUMERATE_SUB_KEYS, &hZonesKey) != ERROR_SUCCESS)
        return FALSE;

    Error = RegOpenKeyExW(hZonesKey, KeyName, 0, KEY_QUERY_VALUE, &hZoneKey);
    RegCloseKey(hZonesKey);
    if (Error != ERROR_SUCCESS)
        return FALSE;

    Error = QueryTimeZoneData(hZoneKey, Index, &TimeZoneInfo, NULL, NULL, NULL, NULL, NULL, NULL);
    RegCloseKey(hZoneKey);
    return Error == ERROR_SUCCESS;
}

static
BOOL
FindZoneIndexByStandardName(
    _In_ PCWSTR StandardName,
    _Out_ PULONG Index)
{
    REG_TZI_FORMAT TimeZoneInfo;
    WCHAR KeyName[MAX_PATH];
    WCHAR ZoneName[64];
    HKEY hZonesKey;
    HKEY hZoneKey;
    DWORD KeyNameLength;
    ULONG ZoneNameSize;
    ULONG ZoneIndex;
    DWORD Enum;
    BOOL Found = FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONES_KEY, 0, KEY_ENUMERATE_SUB_KEYS, &hZonesKey) != ERROR_SUCCESS)
        return FALSE;

    for (Enum = 0; !Found; Enum++)
    {
        KeyNameLength = ARRAYSIZE(KeyName);
        if (RegEnumKeyExW(hZonesKey, Enum, KeyName, &KeyNameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExW(hZonesKey, KeyName, 0, KEY_QUERY_VALUE, &hZoneKey) != ERROR_SUCCESS)
            continue;

        ZoneNameSize = sizeof(ZoneName) - sizeof(WCHAR);
        if (QueryTimeZoneData(hZoneKey, &ZoneIndex, &TimeZoneInfo, NULL, NULL, ZoneName, &ZoneNameSize, NULL, NULL) == ERROR_SUCCESS &&
            _wcsicmp(ZoneName, StandardName) == 0)
        {
            *Index = ZoneIndex;
            Found = TRUE;
        }

        RegCloseKey(hZoneKey);
    }

    RegCloseKey(hZonesKey);
    return Found;
}

BOOL
GetTimeZoneListIndex(
    _Inout_ PULONG pIndex)
{
    TIME_ZONE_INFORMATION TimeZoneInformation;
    WCHAR KeyName[128];
    DWORD KeyNameSize = sizeof(KeyName) - sizeof(WCHAR);
    DWORD Type;
    HKEY hKey;
    ULONG Index;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONE_INFORMATION_KEY, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        LONG Error = RegQueryValueExW(hKey, L"TimeZoneKeyName", NULL, &Type, (LPBYTE)KeyName, &KeyNameSize);

        RegCloseKey(hKey);
        if (Error == ERROR_SUCCESS && Type == REG_SZ)
        {
            KeyName[KeyNameSize / sizeof(WCHAR)] = UNICODE_NULL;
            if (KeyName[0] != UNICODE_NULL && QueryZoneIndex(KeyName, &Index))
            {
                *pIndex = Index;
                return TRUE;
            }
        }
    }

    if (GetTimeZoneInformation(&TimeZoneInformation) == TIME_ZONE_ID_INVALID)
        return FALSE;

    if (!FindZoneIndexByStandardName(TimeZoneInformation.StandardName, &Index))
        return FALSE;

    *pIndex = Index;
    return TRUE;
}

BOOL
GetAutoDaylight(VOID)
{
    DWORD Disabled = 0;
    DWORD Size = sizeof(Disabled);
    DWORD Type;
    HKEY hKey;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONE_INFORMATION_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return TRUE;

    Error = RegQueryValueExW(hKey, L"DynamicDaylightTimeDisabled", NULL, &Type, (LPBYTE)&Disabled, &Size);
    RegCloseKey(hKey);

    if (Error != ERROR_SUCCESS || Type != REG_DWORD || Size != sizeof(Disabled))
        return TRUE;

    return Disabled == 0;
}

VOID
SetAutoDaylight(
    _In_ BOOL EnableAutoDaylightTime)
{
    DWORD Disabled = EnableAutoDaylightTime ? 0 : 1;
    HKEY hKey;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, TIME_ZONE_INFORMATION_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }

    RegSetValueExW(hKey, L"DynamicDaylightTimeDisabled", 0, REG_DWORD, (const BYTE *)&Disabled, sizeof(Disabled));
    RegCloseKey(hKey);
}
