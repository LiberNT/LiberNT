/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The owner of a file reads and rewrites its DACL even when the DACL denies everyone everything
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>

START_TEST(FileSecurityOwner)
{
    PSECURITY_DESCRIPTOR DenyAll = NULL, AllowAll = NULL;
    BYTE Buffer[512];
    DWORD Needed = 0;
    BOOL Present = FALSE, Defaulted = FALSE;
    PACL Dacl = NULL;
    ACCESS_DENIED_ACE *Ace;
    WCHAR Path[MAX_PATH];
    HANDLE hFile;

    GetTempPathW(MAX_PATH, Path);
    StringCchCatW(Path, MAX_PATH, L"lntfilesecowner.txt");
    hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "CreateFileW failed %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(hFile);

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;FA;;;WD)", SDDL_REVISION_1, &DenyAll, NULL),
       "ConvertStringSecurityDescriptorToSecurityDescriptorW failed %lu\n", GetLastError());
    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &AllowAll, NULL),
       "ConvertStringSecurityDescriptorToSecurityDescriptorW failed %lu\n", GetLastError());
    ok(SetFileSecurityW(Path, DACL_SECURITY_INFORMATION, DenyAll), "SetFileSecurityW(deny all) failed %lu\n", GetLastError());

    hFile = CreateFileW(Path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(hFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED, "Reading the data was not denied (%lu)\n", GetLastError());
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);

    SetLastError(0xdeadbeef);
    ok(GetFileSecurityW(Path, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)Buffer, sizeof(Buffer), &Needed),
       "The owner cannot read the DACL: %lu\n", GetLastError());
    if (GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR)Buffer, &Present, &Dacl, &Defaulted) && Present && Dacl)
    {
        ok(Dacl->AceCount == 1, "%u entries\n", Dacl->AceCount);
        if (Dacl->AceCount == 1 && GetAce(Dacl, 0, (PVOID *)&Ace))
        {
            ok(Ace->Header.AceType == ACCESS_DENIED_ACE_TYPE, "Entry type %u\n", Ace->Header.AceType);
            ok(Ace->Mask == FILE_ALL_ACCESS, "Entry mask %lx\n", Ace->Mask);
        }
    }

    SetLastError(0xdeadbeef);
    ok(SetFileSecurityW(Path, DACL_SECURITY_INFORMATION, AllowAll), "The owner cannot rewrite the DACL: %lu\n", GetLastError());
    ok(DeleteFileW(Path), "DeleteFileW failed %lu\n", GetLastError());

    LocalFree(AllowAll);
    LocalFree(DenyAll);
}
