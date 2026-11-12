/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test FindFirstFileNameW and FindNextFileNameW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HANDLE (WINAPI *PFN_FIND_FIRST_FILE_NAME)(LPCWSTR, DWORD, LPDWORD, PWSTR);
typedef BOOL (WINAPI *PFN_FIND_NEXT_FILE_NAME)(HANDLE, LPDWORD, PWSTR);

static PFN_FIND_FIRST_FILE_NAME pFindFirstFileNameW;
static PFN_FIND_NEXT_FILE_NAME pFindNextFileNameW;

static
BOOL
CreateTestFile(
    _In_ PCWSTR Path)
{
    HANDLE File;
    DWORD Written;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(File, "links", 5, &Written, NULL);
    CloseHandle(File);
    return TRUE;
}

static
VOID
TestTwoLinks(
    _In_ PCWSTR PathA,
    _In_ PCWSTR PathB)
{
    WCHAR Name[MAX_PATH];
    BOOLEAN SeenA = FALSE;
    BOOLEAN SeenB = FALSE;
    HANDLE Find;
    DWORD Length;
    BOOL Result;

    Length = RTL_NUMBER_OF(Name);
    Find = pFindFirstFileNameW(PathA, 0, &Length, Name);
    ok(Find != INVALID_HANDLE_VALUE, "FindFirstFileNameW failed with %lu\n", GetLastError());
    if (Find == INVALID_HANDLE_VALUE)
        return;

    if (!_wcsicmp(Name, PathA + 2))
        SeenA = TRUE;
    else if (!_wcsicmp(Name, PathB + 2))
        SeenB = TRUE;
    else
        ok(0, "FindFirstFileNameW returned %S\n", Name);

    Length = RTL_NUMBER_OF(Name);
    Result = pFindNextFileNameW(Find, &Length, Name);
    ok(Result, "FindNextFileNameW failed with %lu\n", GetLastError());
    if (Result)
    {
        if (!SeenA && !_wcsicmp(Name, PathA + 2))
            SeenA = TRUE;
        else if (!SeenB && !_wcsicmp(Name, PathB + 2))
            SeenB = TRUE;
        else
            ok(0, "FindNextFileNameW returned %S\n", Name);
    }
    ok(SeenA, "%S was not enumerated\n", PathA + 2);
    ok(SeenB, "%S was not enumerated\n", PathB + 2);

    Length = RTL_NUMBER_OF(Name);
    SetLastError(0xdeadbeef);
    Result = pFindNextFileNameW(Find, &Length, Name);
    ok(!Result, "FindNextFileNameW returned a third name %S\n", Name);
    ok(GetLastError() == ERROR_HANDLE_EOF, "FindNextFileNameW at the end failed with %lu\n", GetLastError());

    ok(FindClose(Find), "FindClose failed with %lu\n", GetLastError());
}

static
VOID
TestBufferSize(
    _In_ PCWSTR PathA,
    _In_ PCWSTR PathB)
{
    WCHAR Name[MAX_PATH];
    WCHAR First[MAX_PATH];
    DWORD Expected = (DWORD)wcslen(PathA + 2) + 1;
    HANDLE Find;
    DWORD Length;
    BOOL Result;

    ok(wcslen(PathA) == wcslen(PathB), "Test link names differ in length\n");

    Length = 1;
    SetLastError(0xdeadbeef);
    Find = pFindFirstFileNameW(PathA, 0, &Length, Name);
    ok(Find == INVALID_HANDLE_VALUE, "FindFirstFileNameW with a one-character buffer succeeded\n");
    ok(GetLastError() == ERROR_MORE_DATA, "FindFirstFileNameW with a one-character buffer failed with %lu\n", GetLastError());
    ok(Length == Expected, "FindFirstFileNameW asked for %lu characters, expected %lu\n", Length, Expected);
    if (Find != INVALID_HANDLE_VALUE)
        FindClose(Find);

    Length = Expected;
    Find = pFindFirstFileNameW(PathA, 0, &Length, First);
    ok(Find != INVALID_HANDLE_VALUE, "FindFirstFileNameW with an exact buffer failed with %lu\n", GetLastError());
    if (Find == INVALID_HANDLE_VALUE)
        return;

    Length = Expected - 1;
    SetLastError(0xdeadbeef);
    Result = pFindNextFileNameW(Find, &Length, Name);
    ok(!Result, "FindNextFileNameW with a short buffer succeeded\n");
    ok(GetLastError() == ERROR_MORE_DATA, "FindNextFileNameW with a short buffer failed with %lu\n", GetLastError());
    ok(Length == Expected, "FindNextFileNameW asked for %lu characters, expected %lu\n", Length, Expected);

    Length = Expected;
    Result = pFindNextFileNameW(Find, &Length, Name);
    ok(Result, "FindNextFileNameW after a short buffer failed with %lu\n", GetLastError());
    ok(Result && _wcsicmp(Name, First), "FindNextFileNameW returned the first name %S again\n", Name);

    FindClose(Find);
}

static
VOID
TestOneLink(
    _In_ PCWSTR Path)
{
    WCHAR Name[MAX_PATH];
    HANDLE Find;
    DWORD Length;
    BOOL Result;

    Length = RTL_NUMBER_OF(Name);
    Find = pFindFirstFileNameW(Path, 0, &Length, Name);
    ok(Find != INVALID_HANDLE_VALUE, "FindFirstFileNameW on a single link failed with %lu\n", GetLastError());
    if (Find == INVALID_HANDLE_VALUE)
        return;
    ok(!_wcsicmp(Name, Path + 2), "FindFirstFileNameW returned %S, expected %S\n", Name, Path + 2);

    Length = RTL_NUMBER_OF(Name);
    SetLastError(0xdeadbeef);
    Result = pFindNextFileNameW(Find, &Length, Name);
    ok(!Result, "FindNextFileNameW on a single link returned %S\n", Name);
    ok(GetLastError() == ERROR_HANDLE_EOF, "FindNextFileNameW on a single link failed with %lu\n", GetLastError());
    FindClose(Find);
}

START_TEST(FindFileNames)
{
    WCHAR TempPath[MAX_PATH];
    WCHAR LongPath[MAX_PATH];
    WCHAR Directory[MAX_PATH];
    WCHAR PathA[MAX_PATH];
    WCHAR PathB[MAX_PATH];
    WCHAR Missing[MAX_PATH];
    WCHAR Name[MAX_PATH];
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    HANDLE Find;
    DWORD Length;
    BOOL Result;

    pFindFirstFileNameW = (PFN_FIND_FIRST_FILE_NAME)GetProcAddress(Kernel32, "FindFirstFileNameW");
    pFindNextFileNameW = (PFN_FIND_NEXT_FILE_NAME)GetProcAddress(Kernel32, "FindNextFileNameW");
    ok(pFindFirstFileNameW != NULL, "kernel32 does not export FindFirstFileNameW\n");
    ok(pFindNextFileNameW != NULL, "kernel32 does not export FindNextFileNameW\n");
    ok(KernelBase && GetProcAddress(KernelBase, "FindFirstFileNameW") != NULL,
       "kernelbase does not export FindFirstFileNameW\n");
    ok(KernelBase && GetProcAddress(KernelBase, "FindNextFileNameW") != NULL,
       "kernelbase does not export FindNextFileNameW\n");
    if (!pFindFirstFileNameW || !pFindNextFileNameW)
        return;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    Length = GetLongPathNameW(TempPath, LongPath, RTL_NUMBER_OF(LongPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(LongPath), "GetLongPathName failed with %lu\n", GetLastError());
    StringCchPrintfW(Directory, RTL_NUMBER_OF(Directory), L"%sffnames%lx", LongPath, GetCurrentProcessId());
    ok(CreateDirectoryW(Directory, NULL), "CreateDirectory(%S) failed with %lu\n", Directory, GetLastError());
    StringCchPrintfW(PathA, RTL_NUMBER_OF(PathA), L"%s\\linka.txt", Directory);
    StringCchPrintfW(PathB, RTL_NUMBER_OF(PathB), L"%s\\linkb.txt", Directory);
    StringCchPrintfW(Missing, RTL_NUMBER_OF(Missing), L"%s\\missing.txt", Directory);

    if (CreateTestFile(PathA))
    {
        TestOneLink(PathA);

        ok(CreateHardLinkW(PathB, PathA, NULL), "CreateHardLink failed with %lu\n", GetLastError());
        TestTwoLinks(PathA, PathB);
        TestTwoLinks(PathB, PathA);
        TestBufferSize(PathA, PathB);

        ok(DeleteFileW(PathB), "DeleteFile(%S) failed with %lu\n", PathB, GetLastError());
        TestOneLink(PathA);
        ok(DeleteFileW(PathA), "DeleteFile(%S) failed with %lu\n", PathA, GetLastError());
    }

    Length = RTL_NUMBER_OF(Name);
    SetLastError(0xdeadbeef);
    Find = pFindFirstFileNameW(Missing, 0, &Length, Name);
    ok(Find == INVALID_HANDLE_VALUE, "FindFirstFileNameW on a missing file succeeded\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "FindFirstFileNameW on a missing file failed with %lu\n", GetLastError());

    Length = RTL_NUMBER_OF(Name);
    SetLastError(0xdeadbeef);
    Result = pFindNextFileNameW(INVALID_HANDLE_VALUE, &Length, Name);
    ok(!Result, "FindNextFileNameW on INVALID_HANDLE_VALUE succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "FindNextFileNameW on INVALID_HANDLE_VALUE failed with %lu\n", GetLastError());

    ok(RemoveDirectoryW(Directory), "RemoveDirectory(%S) failed with %lu\n", Directory, GetLastError());
}
