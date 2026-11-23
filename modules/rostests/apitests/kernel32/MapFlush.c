/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test flushing a writable file mapping back to its file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define FILE_SIZE (64 * 1024)

static
void
FillPattern(
    _Out_writes_bytes_(Size) PBYTE Buffer,
    _In_ SIZE_T Size,
    _In_ BYTE Seed)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
        Buffer[Index] = (BYTE)(Seed + Index * 3);
}

START_TEST(MapFlush)
{
    WCHAR TempPath[MAX_PATH];
    WCHAR TempName[MAX_PATH];
    BYTE Original[FILE_SIZE];
    BYTE Updated[FILE_SIZE];
    BYTE Readback[FILE_SIZE];
    HANDLE File, Mapping;
    PVOID View;
    DWORD Bytes;
    DWORD Length;
    BOOL Success;

    FillPattern(Original, sizeof(Original), 0x11);
    RtlCopyMemory(Updated, Original, sizeof(Updated));
    RtlFillMemory(Updated, 4096, 0x5A);
    RtlFillMemory(Updated + sizeof(Updated) - 4096, 4096, 0xA5);

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath),
       "GetTempPathW returned %lu, error %lu\n", Length, GetLastError());
    if (Length == 0 || Length >= RTL_NUMBER_OF(TempPath))
        return;

    ok(GetTempFileNameW(TempPath, L"mfl", 0, TempName) != 0,
       "GetTempFileNameW failed, error %lu\n", GetLastError());

    File = CreateFileW(TempName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY,
                       NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Bytes = 0;
    Success = WriteFile(File, Original, sizeof(Original), &Bytes, NULL);
    ok(Success && Bytes == sizeof(Original),
       "WriteFile returned %d, wrote %lu bytes, error %lu\n", Success, Bytes, GetLastError());

    ok(SetFilePointer(File, 0, NULL, FILE_BEGIN) == 0,
       "SetFilePointer failed, error %lu\n", GetLastError());

    Mapping = CreateFileMappingW(File, NULL, PAGE_READWRITE, 0, 0, NULL);
    ok(Mapping != NULL, "CreateFileMappingW failed, error %lu\n", GetLastError());

    View = (Mapping != NULL) ? MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0) : NULL;
    ok(View != NULL, "MapViewOfFile failed, error %lu\n", GetLastError());

    if (View != NULL)
    {
        ok(RtlEqualMemory(View, Original, sizeof(Original)),
           "the mapped view does not show the file contents\n");

        RtlCopyMemory(View, Updated, sizeof(Updated));

        SetLastError(0xDEADBEEF);
        Success = FlushViewOfFile(View, 0);
        ok(Success, "FlushViewOfFile failed, error %lu\n", GetLastError());

        Success = FlushViewOfFile(View, sizeof(Updated));
        ok(Success, "FlushViewOfFile of the written range failed, error %lu\n", GetLastError());

        ok(UnmapViewOfFile(View), "UnmapViewOfFile failed, error %lu\n", GetLastError());
    }

    if (Mapping != NULL)
        CloseHandle(Mapping);

    CloseHandle(File);

    File = CreateFileW(TempName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "reopening the file failed, error %lu\n", GetLastError());

    if (File != INVALID_HANDLE_VALUE)
    {
        RtlZeroMemory(Readback, sizeof(Readback));
        Bytes = 0;
        Success = ReadFile(File, Readback, sizeof(Readback), &Bytes, NULL);
        ok(Success && Bytes == sizeof(Readback),
           "ReadFile returned %d, read %lu bytes, error %lu\n", Success, Bytes, GetLastError());
        ok(RtlEqualMemory(Readback, Updated, sizeof(Updated)),
           "the flushed data does not match what was written through the view\n");

        CloseHandle(File);
    }

    DeleteFileW(TempName);
}
