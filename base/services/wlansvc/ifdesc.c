/*
 * PROJECT:     LiberNT WLAN Service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter description lookup in the network class registry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <stdio.h>
#include <wchar.h>

BOOL
WlanSvcGetAdapterDescription(
    _In_ const GUID *AdapterGuid,
    _Out_writes_(Length) PWCHAR Description,
    _In_ ULONG Length)
{
    static const WCHAR NetClassKey[] =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";
    WCHAR GuidText[40], SubKeyName[64], InstanceId[40], Text[256];
    HKEY ClassKey, InstanceKey;
    DWORD Index, Size, Type;
    BOOL Found = FALSE;

    if (Length == 0)
        return FALSE;

    _snwprintf(GuidText, ARRAYSIZE(GuidText),
               L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               AdapterGuid->Data1, AdapterGuid->Data2, AdapterGuid->Data3,
               AdapterGuid->Data4[0], AdapterGuid->Data4[1], AdapterGuid->Data4[2], AdapterGuid->Data4[3],
               AdapterGuid->Data4[4], AdapterGuid->Data4[5], AdapterGuid->Data4[6], AdapterGuid->Data4[7]);
    GuidText[ARRAYSIZE(GuidText) - 1] = L'\0';

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NetClassKey, 0, KEY_READ, &ClassKey) != ERROR_SUCCESS)
        return FALSE;

    for (Index = 0; !Found; Index++)
    {
        Size = ARRAYSIZE(SubKeyName);
        if (RegEnumKeyExW(ClassKey, Index, SubKeyName, &Size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(ClassKey, SubKeyName, 0, KEY_READ, &InstanceKey) != ERROR_SUCCESS)
            continue;

        Size = sizeof(InstanceId);
        if (RegQueryValueExW(InstanceKey, L"NetCfgInstanceId", NULL, &Type, (LPBYTE)InstanceId, &Size) == ERROR_SUCCESS &&
            Type == REG_SZ && _wcsnicmp(InstanceId, GuidText, ARRAYSIZE(GuidText)) == 0)
        {
            Size = sizeof(Text) - sizeof(WCHAR);
            if (RegQueryValueExW(InstanceKey, L"DriverDesc", NULL, &Type, (LPBYTE)Text, &Size) == ERROR_SUCCESS &&
                Type == REG_SZ && Size >= sizeof(WCHAR))
            {
                Text[Size / sizeof(WCHAR)] = L'\0';
                wcsncpy(Description, Text, Length - 1);
                Description[Length - 1] = L'\0';
                Found = Description[0] != L'\0';
            }
        }
        RegCloseKey(InstanceKey);
    }

    RegCloseKey(ClassKey);
    return Found;
}
