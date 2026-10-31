/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CreateDirectoryW on the root directory of a volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static void TestRoot(LPCWSTR Root, LPCWSTR Relative)
{
    WCHAR Saved[MAX_PATH], Path[MAX_PATH];
    BOOL Success;

    SetLastError(0xdeadbeef);
    Success = CreateDirectoryW(Root, NULL);
    ok(!Success, "CreateDirectoryW(%S) succeeded\n", Root);
    ok_long(GetLastError(), ERROR_ACCESS_DENIED);

    if (!Relative)
        return;

    GetCurrentDirectoryW(ARRAYSIZE(Saved), Saved);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%scdroot_test", Root);
    ok(CreateDirectoryW(Path, NULL), "CreateDirectoryW(%S) failed with %lu\n", Path, GetLastError());
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\a");
    ok(CreateDirectoryW(Path, NULL), "CreateDirectoryW(%S) failed with %lu\n", Path, GetLastError());
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\b");
    ok(CreateDirectoryW(Path, NULL), "CreateDirectoryW(%S) failed with %lu\n", Path, GetLastError());
    ok(SetCurrentDirectoryW(Path), "SetCurrentDirectoryW(%S) failed with %lu\n", Path, GetLastError());

    SetLastError(0xdeadbeef);
    Success = CreateDirectoryW(Relative, NULL);
    ok(!Success, "CreateDirectoryW(%S) succeeded\n", Relative);
    ok_long(GetLastError(), ERROR_ACCESS_DENIED);

    SetLastError(0xdeadbeef);
    Success = CreateDirectoryW(L"..\\..", NULL);
    ok(!Success, "CreateDirectoryW(..\\..) succeeded\n");
    ok_long(GetLastError(), ERROR_ALREADY_EXISTS);

    SetCurrentDirectoryW(Saved);
    RemoveDirectoryW(Path);
    Path[wcslen(Path) - 2] = UNICODE_NULL;
    RemoveDirectoryW(Path);
    Path[wcslen(Path) - 2] = UNICODE_NULL;
    ok(RemoveDirectoryW(Path), "RemoveDirectoryW(%S) failed with %lu\n", Path, GetLastError());
}

START_TEST(CreateDirectoryRoot)
{
    WCHAR Module[MAX_PATH], Root[4], FileSystem[MAX_PATH];

    GetModuleFileNameW(NULL, Module, ARRAYSIZE(Module));
    StringCchCopyNW(Root, ARRAYSIZE(Root), Module, 3);
    if (GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) &&
        !_wcsnicmp(FileSystem, L"FAT", 3))
        TestRoot(Root, L"..\\..\\..");
    else
        skip("%S is not a FAT volume\n", Root);

    GetSystemWindowsDirectoryW(Module, ARRAYSIZE(Module));
    StringCchCopyNW(Root, ARRAYSIZE(Root), Module, 3);
    if (GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)) &&
        !_wcsicmp(FileSystem, L"NTFS"))
        TestRoot(Root, NULL);
    else
        skip("%S is not an NTFS volume\n", Root);
}
