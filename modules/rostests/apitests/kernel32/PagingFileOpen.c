/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the active paging file refuses other data opens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

static
VOID
CheckRefused(
    _In_ PCWSTR Path,
    _In_ DWORD Access)
{
    HANDLE File;
    DWORD Error;

    SetLastError(0xdeadbeef);
    File = CreateFileW(Path, Access, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
    Error = GetLastError();
    ok(File == INVALID_HANDLE_VALUE, "Access 0x%lx was granted\n", Access);
    ok(Error == ERROR_SHARING_VIOLATION, "Access 0x%lx failed with %lu\n", Access, Error);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

START_TEST(PagingFileOpen)
{
    WIN32_FIND_DATAW FindData;
    WCHAR Path[MAX_PATH];
    HANDLE Find;
    DWORD Attributes;
    DWORD Length;

    Length = GetEnvironmentVariableW(L"SystemDrive", Path, RTL_NUMBER_OF(Path));
    ok(Length != 0 && Length < RTL_NUMBER_OF(Path) - 16, "SystemDrive length %lu\n", Length);
    if (Length == 0 || Length >= RTL_NUMBER_OF(Path) - 16)
        return;

    wcscat(Path, L"\\pagefile.sys");
    Find = FindFirstFileW(Path, &FindData);
    if (Find == INVALID_HANDLE_VALUE)
    {
        skip("No paging file at %ls\n", Path);
        return;
    }
    FindClose(Find);
    ok(FindData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN, "Attributes 0x%lx\n", FindData.dwFileAttributes);
    ok(FindData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM, "Attributes 0x%lx\n", FindData.dwFileAttributes);

    CheckRefused(Path, GENERIC_READ);
    CheckRefused(Path, GENERIC_WRITE);
    CheckRefused(Path, GENERIC_READ | GENERIC_WRITE);
    CheckRefused(Path, FILE_WRITE_DATA);
    CheckRefused(Path, FILE_APPEND_DATA);
    CheckRefused(Path, FILE_READ_ATTRIBUTES);
    CheckRefused(Path, 0);

    SetLastError(0xdeadbeef);
    Attributes = GetFileAttributesW(Path);
    ok(Attributes == INVALID_FILE_ATTRIBUTES, "GetFileAttributes returned 0x%lx\n", Attributes);
    ok(GetLastError() == ERROR_SHARING_VIOLATION, "GetFileAttributes failed with %lu\n", GetLastError());
}
