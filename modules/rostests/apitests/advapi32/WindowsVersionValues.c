/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the version numbers the registry gives under Windows NT\CurrentVersion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CURRENT_VERSION_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

static
VOID
TestView(
    _In_ REGSAM View,
    _In_ PCSTR ViewName,
    _In_ ULONG Major,
    _In_ ULONG Minor)
{
    DWORD Type, Data, Size;
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, CURRENT_VERSION_KEY, 0, KEY_QUERY_VALUE | View, &Key);
    ok(Error == ERROR_SUCCESS, "%s: RegOpenKeyExW returned %ld\n", ViewName, Error);
    if (Error != ERROR_SUCCESS)
        return;

    Data = MAXDWORD;
    Size = sizeof(Data);
    Error = RegQueryValueExW(Key, L"CurrentMajorVersionNumber", NULL, &Type, (PBYTE)&Data, &Size);
    ok(Error == ERROR_SUCCESS, "%s: CurrentMajorVersionNumber returned %ld\n", ViewName, Error);
    ok(Type == REG_DWORD && Size == sizeof(DWORD), "%s: CurrentMajorVersionNumber type %lu size %lu\n",
       ViewName, Type, Size);
    ok(Data == Major, "%s: CurrentMajorVersionNumber is %lu, expected %lu\n", ViewName, Data, Major);

    Data = MAXDWORD;
    Size = sizeof(Data);
    Error = RegQueryValueExW(Key, L"CurrentMinorVersionNumber", NULL, &Type, (PBYTE)&Data, &Size);
    ok(Error == ERROR_SUCCESS, "%s: CurrentMinorVersionNumber returned %ld\n", ViewName, Error);
    ok(Type == REG_DWORD && Size == sizeof(DWORD), "%s: CurrentMinorVersionNumber type %lu size %lu\n",
       ViewName, Type, Size);
    ok(Data == Minor, "%s: CurrentMinorVersionNumber is %lu, expected %lu\n", ViewName, Data, Minor);

    RegCloseKey(Key);
}

START_TEST(WindowsVersionValues)
{
    RTL_OSVERSIONINFOW Version = { sizeof(Version) };
    NTSTATUS Status;

    Status = RtlGetVersion(&Version);
    ok(Status == STATUS_SUCCESS, "RtlGetVersion returned 0x%08lx\n", Status);
    if (Status != STATUS_SUCCESS)
        return;

    TestView(KEY_WOW64_64KEY, "native", Version.dwMajorVersion, Version.dwMinorVersion);
    TestView(KEY_WOW64_32KEY, "32-bit", Version.dwMajorVersion, Version.dwMinorVersion);
}
