/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that reading a file leaves it clean, so an image section can still be created
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hello@astroshippers.com>
 */

#include "precomp.h"

static
BOOL
FindTestImage(
    _Out_writes_(MAX_PATH) PWSTR Path)
{
    WCHAR Drives[128], Label[MAX_PATH], FileSystem[MAX_PATH];
    PWSTR Drive;

    if (!GetLogicalDriveStringsW(ARRAYSIZE(Drives), Drives))
        return FALSE;

    for (Drive = Drives; *Drive; Drive += wcslen(Drive) + 1)
    {
        if (!GetVolumeInformationW(Drive, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                   FileSystem, ARRAYSIZE(FileSystem)))
        {
            continue;
        }
        if (_wcsicmp(FileSystem, L"exFAT") || _wcsicmp(Label, L"EXFATTEST"))
            continue;

        wcscpy(Path, Drive);
        wcscat(Path, L"image.dll");
        return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
    }

    return FALSE;
}

static
BOOL
ReadWhole(
    _In_ HANDLE File,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ DWORD Size)
{
    LARGE_INTEGER Zero = { 0 };
    DWORD Read = 0;

    if (!SetFilePointerEx(File, Zero, NULL, FILE_BEGIN))
        return FALSE;
    return ReadFile(File, Buffer, Size, &Read, NULL) && Read == Size;
}

START_TEST(NtCreateSection_CachedRead)
{
    WCHAR Path[MAX_PATH];
    FILE_BASIC_INFO Before, After;
    LARGE_INTEGER FileSize;
    PUCHAR First = NULL, Second = NULL;
    PUCHAR View;
    HANDLE File, Mapping, Section;
    PVOID Base = NULL;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    if (!FindTestImage(Path))
    {
        skip("No exFAT volume labelled EXFATTEST holding image.dll\n");
        return;
    }

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    ok(GetFileSizeEx(File, &FileSize), "GetFileSizeEx failed %lu\n", GetLastError());
    ok(FileSize.QuadPart > 2 * 4096 && FileSize.QuadPart < 64 * 1024 * 1024,
       "Unexpected size %I64d\n", FileSize.QuadPart);
    ok(GetFileInformationByHandleEx(File, FileBasicInfo, &Before, sizeof(Before)),
       "GetFileInformationByHandleEx failed %lu\n", GetLastError());

    First = HeapAlloc(GetProcessHeap(), 0, FileSize.LowPart);
    Second = HeapAlloc(GetProcessHeap(), 0, FileSize.LowPart);
    if (!First || !Second)
    {
        skip("Out of memory\n");
        goto Cleanup;
    }
    ok(ReadWhole(File, First, FileSize.LowPart), "ReadFile failed %lu\n", GetLastError());

    Mapping = CreateFileMappingW(File, NULL, PAGE_READONLY, 0, 0, NULL);
    ok(Mapping != NULL, "CreateFileMappingW failed %lu\n", GetLastError());
    if (Mapping)
    {
        View = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        ok(View != NULL, "MapViewOfFile failed %lu\n", GetLastError());
        if (View)
        {
            ok(!memcmp(View, First, FileSize.LowPart), "Mapped view differs from ReadFile data\n");
            UnmapViewOfFile(View);
        }
        CloseHandle(Mapping);
    }

    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, NULL, NULL, PAGE_READONLY, SEC_IMAGE, File);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, NULL, &ViewSize,
                                    ViewUnmap, 0, PAGE_READONLY);
        ok(Status == STATUS_SUCCESS || Status == STATUS_IMAGE_NOT_AT_BASE ||
           Status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH,
           "NtMapViewOfSection returned 0x%lx\n", Status);
        if (NT_SUCCESS(Status))
            NtUnmapViewOfSection(NtCurrentProcess(), Base);
        NtClose(Section);
    }

    ok(ReadWhole(File, Second, FileSize.LowPart), "ReadFile failed %lu\n", GetLastError());
    ok(!memcmp(First, Second, FileSize.LowPart), "File contents changed\n");
    ok(GetFileInformationByHandleEx(File, FileBasicInfo, &After, sizeof(After)),
       "GetFileInformationByHandleEx failed %lu\n", GetLastError());
    ok(After.LastWriteTime.QuadPart == Before.LastWriteTime.QuadPart,
       "LastWriteTime changed from %I64x to %I64x\n",
       Before.LastWriteTime.QuadPart, After.LastWriteTime.QuadPart);
    ok(After.ChangeTime.QuadPart == Before.ChangeTime.QuadPart,
       "ChangeTime changed from %I64x to %I64x\n",
       Before.ChangeTime.QuadPart, After.ChangeTime.QuadPart);

Cleanup:
    if (Second)
        HeapFree(GetProcessHeap(), 0, Second);
    if (First)
        HeapFree(GetProcessHeap(), 0, First);
    CloseHandle(File);
}
