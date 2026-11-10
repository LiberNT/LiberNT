/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test metadata updates in a large directory index
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <strsafe.h>

#define FILE_COUNT 1200

static VOID
MakePath(PWSTR Buffer, PCWSTR Directory, ULONG Index)
{
    StringCchPrintfW(Buffer, MAX_PATH, L"%s\\resource_%04lu_long_filename.dat",
                     Directory, Index);
}

START_TEST(IndexMetadata)
{
    WCHAR Temp[MAX_PATH], Directory[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW Data;
    FILETIME Expected = {0x8f0b8000, 0x01c08f89};
    ULONG Index, Created = 0;
    HANDLE File, Search;
    BOOL Result;
    const ULONG Samples[] = {0, 149, 599, 1049, FILE_COUNT - 1};

    if (!GetTempPathW(ARRAYSIZE(Temp), Temp) ||
        !GetTempFileNameW(Temp, L"nti", 0, Directory))
    {
        skip("Cannot create a temporary test directory, error %lu\n", GetLastError());
        return;
    }
    DeleteFileW(Directory);
    if (!CreateDirectoryW(Directory, NULL))
    {
        skip("Cannot create test directory, error %lu\n", GetLastError());
        return;
    }

    for (Index = 0; Index < FILE_COUNT; Index++)
    {
        MakePath(Path, Directory, Index);
        File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (File == INVALID_HANDLE_VALUE)
        {
            ok(0, "Create %lu failed: %lu\n", Index, GetLastError());
            break;
        }
        CloseHandle(File);
        Created++;
    }

    for (Index = 0; Index < ARRAYSIZE(Samples); Index++)
    {
        if (Samples[Index] >= Created)
            continue;
        MakePath(Path, Directory, Samples[Index]);
        File = CreateFileW(Path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        ok(File != INVALID_HANDLE_VALUE, "Open %lu failed: %lu\n", Samples[Index], GetLastError());
        if (File == INVALID_HANDLE_VALUE)
            continue;
        Result = SetFileTime(File, NULL, NULL, &Expected);
        ok(Result, "SetFileTime %lu failed: %lu\n", Samples[Index], GetLastError());
        CloseHandle(File);
        Search = FindFirstFileW(Path, &Data);
        ok(Search != INVALID_HANDLE_VALUE, "Find %lu failed: %lu\n", Samples[Index], GetLastError());
        if (Search != INVALID_HANDLE_VALUE)
        {
            ok(Data.ftLastWriteTime.dwLowDateTime == Expected.dwLowDateTime &&
               Data.ftLastWriteTime.dwHighDateTime == Expected.dwHighDateTime,
               "Incorrect directory timestamp for %lu: %08lx:%08lx\n", Samples[Index],
               Data.ftLastWriteTime.dwHighDateTime, Data.ftLastWriteTime.dwLowDateTime);
            FindClose(Search);
        }
    }

    for (Index = 0; Index < Created; Index++)
    {
        MakePath(Path, Directory, Index);
        DeleteFileW(Path);
    }
    RemoveDirectoryW(Directory);
}
