/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that redistributables Windows 11 does not ship are not owned by TrustedInstaller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

static const WCHAR *Redistributables[] = { L"vulkan-1.dll", L"d3dcompiler_43.dll", L"atl100.dll" };

static void
CheckFile(const WCHAR *Directory, const WCHAR *Name, PSID TrustedInstaller)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    WCHAR Path[MAX_PATH];
    PSID Owner = NULL;
    DWORD Error;

    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"%s\\%s", Directory, Name);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls is not present\n", Path);
        return;
    }

    Error = GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &Owner, NULL, NULL, NULL, &Descriptor);
    ok(Error == ERROR_SUCCESS, "%ls: GetNamedSecurityInfoW failed with %lu\n", Path, Error);
    if (Error == ERROR_SUCCESS)
        ok(!EqualSid(Owner, TrustedInstaller), "%ls is owned by TrustedInstaller\n", Path);
    LocalFree(Descriptor);
}

START_TEST(RedistributableFileOwner)
{
    WCHAR System[MAX_PATH], Wow64[MAX_PATH];
    PSID TrustedInstaller = NULL;
    ULONG Index;

    ok(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &TrustedInstaller),
       "ConvertStringSidToSidW failed with %lu\n", GetLastError());
    if (TrustedInstaller == NULL)
        return;

    ok(GetSystemDirectoryW(System, ARRAYSIZE(System)) != 0, "GetSystemDirectoryW failed with %lu\n", GetLastError());
    for (Index = 0; Index < ARRAYSIZE(Redistributables); Index++)
        CheckFile(System, Redistributables[Index], TrustedInstaller);

    if (GetSystemWow64DirectoryW(Wow64, ARRAYSIZE(Wow64)) != 0)
    {
        for (Index = 0; Index < ARRAYSIZE(Redistributables); Index++)
            CheckFile(Wow64, Redistributables[Index], TrustedInstaller);
    }

    LocalFree(TrustedInstaller);
}
