/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the profile owner can write its user hive without administrator rights
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

static
VOID
TestUserHiveWrite(VOID)
{
    DWORD Value = 1;
    HKEY User = NULL;
    HKEY Key = NULL;
    LONG Error;

    Error = RegOpenCurrentUser(MAXIMUM_ALLOWED, &User);
    ok(Error == ERROR_SUCCESS, "RegOpenCurrentUser returned %ld\n", Error);
    if (Error != ERROR_SUCCESS) return;

    Error = RegCreateKeyExW(User, L"Software\\LiberNTUserHiveAccess", 0, NULL, REG_OPTION_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW returned %ld\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Key, L"Value", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
        ok(Error == ERROR_SUCCESS, "RegSetValueExW returned %ld\n", Error);
        RegCloseKey(Key);

        Error = RegDeleteKeyW(User, L"Software\\LiberNTUserHiveAccess");
        ok(Error == ERROR_SUCCESS, "RegDeleteKeyW returned %ld\n", Error);
    }

    Error = RegOpenKeyExW(User, L"Control Panel", 0, KEY_SET_VALUE, &Key);
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW(Control Panel, KEY_SET_VALUE) returned %ld\n", Error);
    if (Error == ERROR_SUCCESS) RegCloseKey(Key);

    RegCloseKey(User);
}

START_TEST(UserHiveAccess)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    SID_AND_ATTRIBUTES DenyOnly;
    HANDLE Process = NULL;
    HANDLE Restricted = NULL;
    HANDLE Impersonation = NULL;
    PSID Admins = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &Process))
    {
        skip("OpenProcessToken failed with %lu\n", GetLastError());
        return;
    }

    if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &Admins))
    {
        skip("AllocateAndInitializeSid failed with %lu\n", GetLastError());
        CloseHandle(Process);
        return;
    }

    DenyOnly.Sid = Admins;
    DenyOnly.Attributes = 0;
    ok(CreateRestrictedToken(Process, 0, 1, &DenyOnly, 0, NULL, 0, NULL, &Restricted),
       "CreateRestrictedToken failed with %lu\n", GetLastError());
    if (Restricted != NULL)
    {
        ok(DuplicateTokenEx(Restricted, TOKEN_IMPERSONATE | TOKEN_QUERY, NULL, SecurityImpersonation,
                            TokenImpersonation, &Impersonation),
           "DuplicateTokenEx failed with %lu\n", GetLastError());
    }

    if (Impersonation != NULL)
    {
        ok(SetThreadToken(NULL, Impersonation), "SetThreadToken failed with %lu\n", GetLastError());
        TestUserHiveWrite();
        ok(RevertToSelf(), "RevertToSelf failed with %lu\n", GetLastError());
        CloseHandle(Impersonation);
    }

    if (Restricted != NULL) CloseHandle(Restricted);
    FreeSid(Admins);
    CloseHandle(Process);
}
