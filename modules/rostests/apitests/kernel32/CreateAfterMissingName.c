/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that names looked up while missing are found once created, whatever spelling looked them up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static void CheckMissing(LPCWSTR Path, int Line)
{
    SetLastError(0xdeadbeef);
    ok(GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES, "Line %d: %S exists\n", Line, Path);
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "Line %d: %S error %lu\n", Line, Path, GetLastError());
}

static void CheckPresent(LPCWSTR Path, int Line)
{
    ok(GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES, "Line %d: %S not found, error %lu\n", Line, Path,
       GetLastError());
}

START_TEST(CreateAfterMissingName)
{
    WCHAR Temp[MAX_PATH], Dir[MAX_PATH], Sub[MAX_PATH], Upper[MAX_PATH], Lower[MAX_PATH], Dotted[MAX_PATH];
    WCHAR Other[MAX_PATH], Renamed[MAX_PATH], Up[MAX_PATH];
    HANDLE File;
    int i;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    StringCchPrintfW(Dir, ARRAYSIZE(Dir), L"%smissname", Temp);
    StringCchPrintfW(Sub, ARRAYSIZE(Sub), L"%s\\sub", Dir);
    StringCchPrintfW(Upper, ARRAYSIZE(Upper), L"%s\\NAME-0.TXT", Dir);
    StringCchPrintfW(Lower, ARRAYSIZE(Lower), L"%s\\name-0.txt", Dir);
    StringCchPrintfW(Dotted, ARRAYSIZE(Dotted), L"%s\\sub\\..\\.\\Name-0.Txt", Dir);
    StringCchPrintfW(Other, ARRAYSIZE(Other), L"%s\\name-0.txt", Sub);
    StringCchPrintfW(Renamed, ARRAYSIZE(Renamed), L"%s\\name-1.txt", Dir);
    StringCchPrintfW(Up, ARRAYSIZE(Up), L"%s\\NAME-1.TXT", Dir);
    DeleteFileW(Lower);
    DeleteFileW(Renamed);
    DeleteFileW(Other);
    RemoveDirectoryW(Sub);
    RemoveDirectoryW(Dir);

    ok(CreateDirectoryW(Dir, NULL), "CreateDirectoryW(%S) failed with %lu\n", Dir, GetLastError());
    ok(CreateDirectoryW(Sub, NULL), "CreateDirectoryW(%S) failed with %lu\n", Sub, GetLastError());
    for (i = 0; i < 300; i++)
    {
        WCHAR Filler[MAX_PATH];

        StringCchPrintfW(Filler, ARRAYSIZE(Filler), L"%s\\fill-%03d.txt", Dir, i);
        GetFileAttributesW(Filler);
    }
    CheckMissing(Upper, __LINE__);
    CheckMissing(Dotted, __LINE__);
    CheckMissing(Other, __LINE__);
    CheckMissing(Up, __LINE__);

    File = CreateFileW(Lower, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%S) failed with %lu\n", Lower, GetLastError());
    CloseHandle(File);
    CheckPresent(Upper, __LINE__);
    CheckPresent(Dotted, __LINE__);
    CheckPresent(Lower, __LINE__);
    CheckMissing(Other, __LINE__);

    ok(MoveFileW(Lower, Renamed), "MoveFileW(%S, %S) failed with %lu\n", Lower, Renamed, GetLastError());
    CheckPresent(Up, __LINE__);
    CheckMissing(Upper, __LINE__);

    DeleteFileW(Renamed);
    RemoveDirectoryW(Sub);
    RemoveDirectoryW(Dir);
}
