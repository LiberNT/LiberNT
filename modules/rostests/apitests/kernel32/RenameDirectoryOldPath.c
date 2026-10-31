/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test opening a file through the old name of its renamed parent directory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static HANDLE OpenExisting(LPCWSTR Path)
{
    return CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
}

START_TEST(RenameDirectoryOldPath)
{
    WCHAR Temp[MAX_PATH], OldDir[MAX_PATH], NewDir[MAX_PATH], OldFile[MAX_PATH], NewFile[MAX_PATH];
    HANDLE File;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    StringCchPrintfW(OldDir, ARRAYSIZE(OldDir), L"%srendir_old", Temp);
    StringCchPrintfW(NewDir, ARRAYSIZE(NewDir), L"%srendir_new", Temp);
    StringCchPrintfW(OldFile, ARRAYSIZE(OldFile), L"%s\\file.txt", OldDir);
    StringCchPrintfW(NewFile, ARRAYSIZE(NewFile), L"%s\\file.txt", NewDir);
    DeleteFileW(OldFile);
    DeleteFileW(NewFile);
    RemoveDirectoryW(OldDir);
    RemoveDirectoryW(NewDir);

    ok(CreateDirectoryW(OldDir, NULL), "CreateDirectoryW(%S) failed with %lu\n", OldDir, GetLastError());
    File = CreateFileW(OldFile, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%S) failed with %lu\n", OldFile, GetLastError());
    CloseHandle(File);
    File = OpenExisting(OldFile);
    ok(File != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", OldFile, GetLastError());
    CloseHandle(File);

    ok(MoveFileW(OldDir, NewDir), "MoveFileW(%S, %S) failed with %lu\n", OldDir, NewDir, GetLastError());

    SetLastError(0xdeadbeef);
    File = OpenExisting(OldFile);
    ok(File == INVALID_HANDLE_VALUE, "Opening %S after the rename succeeded\n", OldFile);
    ok_long(GetLastError(), ERROR_PATH_NOT_FOUND);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    File = OpenExisting(NewFile);
    ok(File != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", NewFile, GetLastError());
    CloseHandle(File);

    ok(CreateDirectoryW(OldDir, NULL), "CreateDirectoryW(%S) failed with %lu\n", OldDir, GetLastError());
    SetLastError(0xdeadbeef);
    File = OpenExisting(OldFile);
    ok(File == INVALID_HANDLE_VALUE, "Opening %S in the recreated directory succeeded\n", OldFile);
    ok_long(GetLastError(), ERROR_FILE_NOT_FOUND);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);

    RemoveDirectoryW(OldDir);
    DeleteFileW(NewFile);
    RemoveDirectoryW(NewDir);
}
