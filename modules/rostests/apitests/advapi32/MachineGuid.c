/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the machine GUID that setup creates for each installation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CRYPTOGRAPHY_KEY L"SOFTWARE\\Microsoft\\Cryptography"
#define MACHINE_GUID_CHARS 36

static
BOOL
IsGuidString(
    _In_ PCWSTR Text)
{
    ULONG i;

    for (i = 0; i < MACHINE_GUID_CHARS; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (Text[i] != L'-')
                return FALSE;
        }
        else if (!((Text[i] >= L'0' && Text[i] <= L'9') || (Text[i] >= L'a' && Text[i] <= L'f')))
        {
            return FALSE;
        }
    }

    return Text[MACHINE_GUID_CHARS] == UNICODE_NULL;
}

static
VOID
TestNativeView(VOID)
{
    WCHAR Guid[64];
    DWORD Type, Size;
    HKEY hKey;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, CRYPTOGRAPHY_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &hKey);
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW returned %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    ZeroMemory(Guid, sizeof(Guid));
    Type = REG_NONE;
    Size = sizeof(Guid) - sizeof(WCHAR);
    Error = RegQueryValueExW(hKey, L"MachineGuid", NULL, &Type, (PBYTE)Guid, &Size);
    RegCloseKey(hKey);
    ok(Error == ERROR_SUCCESS, "RegQueryValueExW returned %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    ok(Type == REG_SZ, "MachineGuid type is %lu\n", Type);
    ok(Size == (MACHINE_GUID_CHARS + 1) * sizeof(WCHAR), "MachineGuid size is %lu\n", Size);
    ok(IsGuidString(Guid), "MachineGuid is '%ls', not a lower-case GUID without braces\n", Guid);
    ok(wcscmp(Guid, L"00000000-0000-0000-0000-000000000000") != 0, "MachineGuid is the nil GUID\n");
    ok(wcscmp(Guid, L"7f3b3cd3-9e91-4a48-92cb-e0ec4e8da130") != 0,
       "MachineGuid is the value built into every image instead of one created for this installation\n");
}

static
VOID
TestWow64View(VOID)
{
    BOOL IsWow64 = FALSE;
    DWORD Size;
    HKEY hKey;
    LONG Error;

    if (sizeof(PVOID) == sizeof(ULONG) && (!IsWow64Process(GetCurrentProcess(), &IsWow64) || !IsWow64))
    {
        skip("No 32-bit registry view on a 32-bit system\n");
        return;
    }

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, CRYPTOGRAPHY_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey);
    ok(Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND, "RegOpenKeyExW returned %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    Size = 0;
    Error = RegQueryValueExW(hKey, L"MachineGuid", NULL, NULL, NULL, &Size);
    RegCloseKey(hKey);
    ok(Error == ERROR_FILE_NOT_FOUND, "MachineGuid in the 32-bit view: RegQueryValueExW returned %ld\n", Error);
}

START_TEST(MachineGuid)
{
    TestNativeView();
    TestWow64View();
}
