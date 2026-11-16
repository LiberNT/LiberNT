/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every device interface key opens by the name its parent lists
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static ULONG Checked;
static ULONG NetReferences;

static void CheckKeys(HKEY Parent, PCWSTR Path, ULONG Depth)
{
    WCHAR Name[256];
    DWORD Index, Size;
    HKEY Key;
    LONG Error;

    for (Index = 0;; Index++)
    {
        Size = ARRAYSIZE(Name);
        Error = RegEnumKeyExW(Parent, Index, Name, &Size, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS)
            break;
        ok(Error == ERROR_SUCCESS, "%ls: RegEnumKeyExW(%lu) failed with %ld\n", Path, Index, Error);
        if (Error != ERROR_SUCCESS)
            break;
        ok(Size == wcslen(Name), "%ls\\%ls: the name is %lu characters but holds %Iu\n", Path, Name, Size, wcslen(Name));

        Error = RegOpenKeyExW(Parent, Name, 0, KEY_READ, &Key);
        /* Windows 11 keeps the Properties keys from administrators; the key is still found */
        ok(Error == ERROR_SUCCESS || Error == ERROR_ACCESS_DENIED, "%ls\\%ls: RegOpenKeyExW failed with %ld\n", Path,
           Name, Error);
        Checked++;
        if (Error != ERROR_SUCCESS)
            continue;
        if (Depth == 2 && Name[0] == L'#' && wcsstr(Path, L"{cac88484-7515-4c03-82e6-71a87abac361}"))
            NetReferences++;
        if (Depth < 2)
        {
            WCHAR Child[512];

            StringCchPrintfW(Child, ARRAYSIZE(Child), L"%ls\\%ls", Path, Name);
            CheckKeys(Key, Child, Depth + 1);
        }
        RegCloseKey(Key);
    }
}

START_TEST(DeviceClassKeys)
{
    HKEY Key;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses", 0, KEY_READ,
                          &Key);
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW failed with %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    CheckKeys(Key, L"DeviceClasses", 0);
    RegCloseKey(Key);
    ok(Checked != 0, "No keys under DeviceClasses\n");
    trace("%lu keys, %lu network interface reference keys\n", Checked, NetReferences);
}
