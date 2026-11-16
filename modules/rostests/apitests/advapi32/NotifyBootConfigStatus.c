/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NotifyBootConfigStatus with Last Known Good off
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static BOOL
LastKnownGoodEnabled(VOID)
{
    DWORD Enabled = 0, Size = sizeof(Enabled), Type = 0;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Configuration Manager\\LastKnownGood",
                      0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    if (RegQueryValueExW(Key, L"Enabled", NULL, &Type, (LPBYTE)&Enabled, &Size) != ERROR_SUCCESS || Type != REG_DWORD)
        Enabled = 0;
    RegCloseKey(Key);
    return Enabled == 1;
}

START_TEST(NotifyBootConfigStatus)
{
    BOOL Ok;

    if (LastKnownGoodEnabled())
    {
        skip("Last Known Good is enabled\n");
        return;
    }

    /* Without Last Known Good there is nothing to save, and every report succeeds. */
    SetLastError(0xdeadbeef);
    Ok = NotifyBootConfigStatus(TRUE);
    ok(Ok, "first report failed, error %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Ok = NotifyBootConfigStatus(TRUE);
    ok(Ok, "second report failed, error %lu\n", GetLastError());
}
