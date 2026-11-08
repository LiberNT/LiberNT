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
    HANDLE hFile;
    PBYTE Data;
    DWORD Read;
    ULONG i;
    BOOL Found = FALSE;

    hFile = CreateFileW(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
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
}
