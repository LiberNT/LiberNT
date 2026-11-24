/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that flushed registry data reaches the hive file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define VALUE_BYTES 12000

static
VOID
FillPattern(
    _Out_writes_bytes_(VALUE_BYTES) PBYTE Data,
    _In_ BYTE Seed)
{
    ULONG i;

    for (i = 0; i < VALUE_BYTES; i++)
        Data[i] = (BYTE)(Seed * 31 + i * 7 + (i >> 8));
}

static
BOOL
FileContains(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Size) const BYTE* Pattern,
    _In_ ULONG Size)
{
    LARGE_INTEGER FileSize;
    ULONGLONG Deadline;
    HANDLE hFile;
    PBYTE Data;
    DWORD Read;
    ULONG i;
    BOOL Found = FALSE;

    /* The last close unloads an application hive in the background, so its file is let go a moment later */
    Deadline = GetTickCount64() + 2000;
    do
    {
        hFile = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
        Sleep(1);
    } while (GetTickCount64() < Deadline);
    ok(hFile != INVALID_HANDLE_VALUE, "the hive file could not be opened (%lu)\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (GetFileSizeEx(hFile, &FileSize) && FileSize.QuadPart >= Size && FileSize.QuadPart < 0x1000000)
    {
        Data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)FileSize.QuadPart);
        if (Data && ReadFile(hFile, Data, (DWORD)FileSize.QuadPart, &Read, NULL) && Read == FileSize.QuadPart)
        {
            for (i = 0; i + Size <= Read && !Found; i++)
                Found = memcmp(Data + i, Pattern, Size) == 0;
        }
        if (Data)
            HeapFree(GetProcessHeap(), 0, Data);
    }
    CloseHandle(hFile);
    return Found;
}

static
VOID
TestFlushWithoutLog(VOID)
{
    static const PCWSTR Logs[] = { L".LOG", L".LOG1", L".LOG2" };
    WCHAR Dir[MAX_PATH], Hive[MAX_PATH], Log[MAX_PATH];
    DWORD Value = 0x5AA5C33C;
    HKEY hkApp;
    ULONG i;
    LONG lRes;

    GetTempPathW(MAX_PATH, Dir);
    StringCchCatW(Dir, MAX_PATH, L"LiberNTRegFlushNoLog");
    CreateDirectoryW(Dir, NULL);
    StringCchPrintfW(Hive, MAX_PATH, L"%s\\hive", Dir);
    DeleteFileW(Hive);

    /* A directory in the place of each log file keeps the hive from having one */
    for (i = 0; i < _countof(Logs); i++)
    {
        StringCchPrintfW(Log, MAX_PATH, L"%s%s", Hive, Logs[i]);
        DeleteFileW(Log);
        CreateDirectoryW(Log, NULL);
    }

    lRes = RegLoadAppKeyW(Hive, &hkApp, KEY_ALL_ACCESS, 0, 0);
    ok_int(lRes, ERROR_SUCCESS);
    if (!lRes)
    {
        ok_int(RegSetValueExW(hkApp, L"NoLog", 0, REG_DWORD, (const BYTE*)&Value, sizeof(Value)), ERROR_SUCCESS);
        ok_int(RegFlushKey(hkApp), ERROR_SUCCESS);
        RegCloseKey(hkApp);
        ok(FileContains(Hive, (const BYTE*)&Value, sizeof(Value)), "the hive file does not hold the value flushed without a log\n");
    }

    for (i = 0; i < _countof(Logs); i++)
    {
        StringCchPrintfW(Log, MAX_PATH, L"%s%s", Hive, Logs[i]);
        RemoveDirectoryW(Log);
        DeleteFileW(Log);
    }
    DeleteFileW(Hive);
    RemoveDirectoryW(Dir);
}

START_TEST(RegFlushKey)
{
    static BYTE First[VALUE_BYTES], Second[VALUE_BYTES], Data[VALUE_BYTES];
    WCHAR Path[MAX_PATH];
    DWORD Size, Type;
    HKEY hkApp;
    LONG lRes;

    FillPattern(First, 1);
    FillPattern(Second, 2);
    GetTempPathW(MAX_PATH, Path);
    wcscat(Path, L"LiberNTRegFlushKey.dat");
    DeleteFileW(Path);

    lRes = RegLoadAppKeyW(Path, &hkApp, KEY_ALL_ACCESS, 0, 0);
    ok_int(lRes, ERROR_SUCCESS);
    if (lRes)
        return;
    lRes = RegSetValueExW(hkApp, L"Big", 0, REG_BINARY, First, VALUE_BYTES);
    ok_int(lRes, ERROR_SUCCESS);
    ok_int(RegFlushKey(hkApp), ERROR_SUCCESS);
    RegCloseKey(hkApp);
    ok(FileContains(Path, First, VALUE_BYTES), "the hive file does not hold the whole value\n");

    lRes = RegLoadAppKeyW(Path, &hkApp, KEY_ALL_ACCESS, 0, 0);
    ok_int(lRes, ERROR_SUCCESS);
    if (lRes)
        return;
    /* A value of the same size is rewritten in its existing cell, which spans several 4 KB blocks */
    lRes = RegSetValueExW(hkApp, L"Big", 0, REG_BINARY, Second, VALUE_BYTES);
    ok_int(lRes, ERROR_SUCCESS);
    ok_int(RegFlushKey(hkApp), ERROR_SUCCESS);
    RegCloseKey(hkApp);

    ok(FileContains(Path, Second, VALUE_BYTES), "the hive file does not hold the whole rewritten value\n");

    lRes = RegLoadAppKeyW(Path, &hkApp, KEY_READ, 0, 0);
    ok_int(lRes, ERROR_SUCCESS);
    if (!lRes)
    {
        Size = sizeof(Data);
        lRes = RegQueryValueExW(hkApp, L"Big", NULL, &Type, Data, &Size);
        ok_int(lRes, ERROR_SUCCESS);
        ok_int(Type, REG_BINARY);
        ok_int(Size, VALUE_BYTES);
        ok(memcmp(Data, Second, VALUE_BYTES) == 0, "the reloaded value differs from the one written\n");
        RegCloseKey(hkApp);
    }
    DeleteFileW(Path);

    TestFlushWithoutLog();
}
