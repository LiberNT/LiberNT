/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a file deleted or renamed through a differently cased path is gone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
VOID
SwapCase(
    _Inout_ PWSTR Path)
{
    for (; *Path; Path++)
    {
        if (*Path >= L'a' && *Path <= L'z')
            *Path -= L'a' - L'A';
        else if (*Path >= L'A' && *Path <= L'Z')
            *Path += L'a' - L'A';
    }
}

static
BOOL
WriteNewFile(
    _In_ PCWSTR Path,
    _In_ PCSTR Text)
{
    HANDLE File;
    DWORD Written;
    BOOL Success;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Success = WriteFile(File, Text, lstrlenA(Text), &Written, NULL);
    CloseHandle(File);
    return Success;
}

static
VOID
ExpectMissing(
    _In_ PCWSTR Path,
    _In_ PCSTR Step)
{
    DWORD Attributes;

    SetLastError(0xdeadbeef);
    Attributes = GetFileAttributesW(Path);
    ok(Attributes == INVALID_FILE_ATTRIBUTES, "%s: attributes 0x%lx\n", Step, Attributes);
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "%s: error %lu\n", Step, GetLastError());
}

START_TEST(DeleteFileCase)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    WCHAR Other[MAX_PATH];
    WCHAR Renamed[MAX_PATH];

    ok(GetTempPathW(_countof(Directory), Directory) != 0, "GetTempPathW failed with %lu\n", GetLastError());
    StringCchPrintfW(Path, _countof(Path), L"%sDelCase.tmp", Directory);
    StringCchPrintfW(Renamed, _countof(Renamed), L"%sDelCaseRenamed.tmp", Directory);
    StringCchCopyW(Other, _countof(Other), Path);
    SwapCase(Other);
    DeleteFileW(Path);
    DeleteFileW(Renamed);

    ok(WriteNewFile(Path, "first"), "create failed with %lu\n", GetLastError());
    ok(GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES, "created file missing, error %lu\n", GetLastError());
    ok(DeleteFileW(Other), "delete through the other case failed with %lu\n", GetLastError());
    ExpectMissing(Path, "after delete");
    ExpectMissing(Other, "after delete, other case");

    ok(WriteNewFile(Path, "second"), "re-create failed with %lu\n", GetLastError());
    ok(GetFileAttributesW(Other) != INVALID_FILE_ATTRIBUTES, "re-created file missing, error %lu\n", GetLastError());
    ok(DeleteFileW(Path), "delete failed with %lu\n", GetLastError());
    ExpectMissing(Other, "after second delete");

    ok(WriteNewFile(Path, "third"), "create for rename failed with %lu\n", GetLastError());
    ok(GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES, "file to rename missing, error %lu\n", GetLastError());
    ok(MoveFileW(Other, Renamed), "rename through the other case failed with %lu\n", GetLastError());
    ExpectMissing(Path, "after rename");
    ok(GetFileAttributesW(Renamed) != INVALID_FILE_ATTRIBUTES, "renamed file missing, error %lu\n", GetLastError());
    ok(DeleteFileW(Renamed), "delete of the renamed file failed with %lu\n", GetLastError());
}
