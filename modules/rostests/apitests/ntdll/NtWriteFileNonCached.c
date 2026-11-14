/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for non-cached NtWriteFile overwrites and their coherence with cached I/O
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NC_FILE_BYTES (1024UL * 1024)
#define NC_TAIL_BYTES (64UL * 1024)
#define NC_MODEL_BYTES (NC_FILE_BYTES + NC_TAIL_BYTES)

static
VOID
NcFill(
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ UCHAR Seed)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Buffer[Index] = (UCHAR)((Offset + Index) * 7 + ((Offset + Index) >> 9) + Seed * 31);
}

static
BOOL
NcWriteAt(
    _In_ HANDLE File,
    _In_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    LARGE_INTEGER Position;
    LARGE_INTEGER After;
    DWORD Bytes = 0;
    BOOL Ok;

    Position.QuadPart = Offset;
    Ok = SetFilePointerEx(File, Position, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(%lu) failed: %lu\n", Offset, GetLastError());
    if (!Ok)
        return FALSE;
    Ok = WriteFile(File, Buffer, Length, &Bytes, NULL);
    ok(Ok, "WriteFile(%lu, %lu) failed: %lu\n", Offset, Length, GetLastError());
    ok(Bytes == Length, "WriteFile(%lu) wrote %lu, expected %lu\n", Offset, Bytes, Length);
    Position.QuadPart = 0;
    After.QuadPart = -1;
    ok(SetFilePointerEx(File, Position, &After, FILE_CURRENT), "SetFilePointerEx failed: %lu\n", GetLastError());
    ok(After.QuadPart == (LONGLONG)Offset + Length, "File pointer after write %I64d, expected %lu\n",
       After.QuadPart, Offset + Length);
    return Ok && Bytes == Length;
}

static
VOID
NcVerify(
    _In_ HANDLE File,
    _In_ PUCHAR Scratch,
    _In_ PUCHAR Model,
    _In_ ULONG Length,
    _In_ PCSTR What)
{
    LARGE_INTEGER Position;
    DWORD Bytes = 0;
    ULONG Index;
    BOOL Ok;

    Position.QuadPart = 0;
    ok(SetFilePointerEx(File, Position, NULL, FILE_BEGIN), "%s: SetFilePointerEx failed: %lu\n", What, GetLastError());
    RtlFillMemory(Scratch, Length, 0xA5);
    Ok = ReadFile(File, Scratch, Length, &Bytes, NULL);
    ok(Ok, "%s: ReadFile failed: %lu\n", What, GetLastError());
    ok(Bytes == Length, "%s: read %lu, expected %lu\n", What, Bytes, Length);
    for (Index = 0; Index < Length && Scratch[Index] == Model[Index]; Index++)
        ;
    ok(Index == Length, "%s: first difference at %lu (0x%02x, expected 0x%02x)\n", What, Index,
       Index < Length ? Scratch[Index] : 0, Index < Length ? Model[Index] : 0);
}

static
VOID
NcTestDirectory(
    _In_ PCWSTR Root,
    _In_ PCWSTR FileSystem,
    _In_ ULONG SectorSize,
    _In_ PUCHAR Model,
    _In_ PUCHAR Data,
    _In_ PUCHAR Scratch)
{
    WCHAR Path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA Attributes;
    FILETIME OldTime;
    SYSTEMTIME Old = { 2001, 1, 1, 1, 0, 0, 0, 0 };
    LARGE_INTEGER Size;
    ULARGE_INTEGER Written;
    HANDLE File;
    HANDLE NonCached;
    HANDLE Cached;
    DWORD Bytes;
    BOOL Ok;

    trace("Testing %ls (%ls, sector %lu)\n", Root, FileSystem, SectorSize);
    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%lsntwrite-noncached.bin", Root);
    DeleteFileW(Path);

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    NcFill(Model, 0, NC_FILE_BYTES, 1);
    Bytes = 0;
    ok(WriteFile(File, Model, NC_FILE_BYTES, &Bytes, NULL), "WriteFile failed: %lu\n", GetLastError());
    ok(Bytes == NC_FILE_BYTES, "Wrote %lu\n", Bytes);
    ok(FlushFileBuffers(File), "FlushFileBuffers failed: %lu\n", GetLastError());
    ok(SystemTimeToFileTime(&Old, &OldTime), "SystemTimeToFileTime failed\n");
    ok(SetFileTime(File, NULL, NULL, &OldTime), "SetFileTime failed: %lu\n", GetLastError());
    CloseHandle(File);

    ok(GetFileAttributesExW(Path, GetFileExInfoStandard, &Attributes), "GetFileAttributesExW failed: %lu\n", GetLastError());
    ok(CompareFileTime(&Attributes.ftLastWriteTime, &OldTime) == 0, "LastWriteTime was not set\n");

    Cached = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Cached != INVALID_HANDLE_VALUE, "Cached open failed: %lu\n", GetLastError());
    NonCached = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(NonCached != INVALID_HANDLE_VALUE, "Non-cached open failed: %lu\n", GetLastError());
    if (Cached == INVALID_HANDLE_VALUE || NonCached == INVALID_HANDLE_VALUE)
        goto Cleanup;

    NcVerify(Cached, Scratch, Model, NC_FILE_BYTES, "initial cached read");

    NcFill(Data, 256 * 1024, 256 * 1024, 2);
    if (NcWriteAt(NonCached, Data, 256 * 1024, 256 * 1024))
        RtlCopyMemory(Model + 256 * 1024, Data, 256 * 1024);

    NcFill(Data, 4096, 4096, 3);
    if (NcWriteAt(NonCached, Data, 4096, 4096))
        RtlCopyMemory(Model + 4096, Data, 4096);

    NcFill(Data, SectorSize, SectorSize, 4);
    if (NcWriteAt(NonCached, Data, SectorSize, SectorSize))
        RtlCopyMemory(Model + SectorSize, Data, SectorSize);

    NcVerify(Cached, Scratch, Model, NC_FILE_BYTES, "cached read after overwrites");
    NcVerify(NonCached, Scratch, Model, NC_FILE_BYTES, "non-cached read after overwrites");

    NcFill(Data, 512 * 1024, 64 * 1024, 6);
    Bytes = 0;
    Size.QuadPart = 512 * 1024;
    ok(SetFilePointerEx(Cached, Size, NULL, FILE_BEGIN), "SetFilePointerEx failed: %lu\n", GetLastError());
    ok(WriteFile(Cached, Data, 64 * 1024, &Bytes, NULL), "Cached WriteFile failed: %lu\n", GetLastError());
    ok(Bytes == 64 * 1024, "Cached write wrote %lu\n", Bytes);
    RtlCopyMemory(Model + 512 * 1024, Data, 64 * 1024);
    NcFill(Data, 528 * 1024, 16 * 1024, 7);
    if (NcWriteAt(NonCached, Data, 528 * 1024, 16 * 1024))
        RtlCopyMemory(Model + 528 * 1024, Data, 16 * 1024);

    NcVerify(Cached, Scratch, Model, NC_FILE_BYTES, "cached read after mixed writes");
    NcVerify(NonCached, Scratch, Model, NC_FILE_BYTES, "non-cached read after mixed writes");

    NcFill(Data, NC_FILE_BYTES - NC_TAIL_BYTES, 2 * NC_TAIL_BYTES, 5);
    if (NcWriteAt(NonCached, Data, NC_FILE_BYTES - NC_TAIL_BYTES, 2 * NC_TAIL_BYTES))
        RtlCopyMemory(Model + NC_FILE_BYTES - NC_TAIL_BYTES, Data, 2 * NC_TAIL_BYTES);
    Size.QuadPart = 0;
    ok(GetFileSizeEx(NonCached, &Size), "GetFileSizeEx failed: %lu\n", GetLastError());
    ok(Size.QuadPart == NC_MODEL_BYTES, "File size %I64d, expected %lu\n", Size.QuadPart, NC_MODEL_BYTES);

    NcVerify(Cached, Scratch, Model, NC_MODEL_BYTES, "cached read after extension");
    NcVerify(NonCached, Scratch, Model, NC_MODEL_BYTES, "non-cached read after extension");

Cleanup:
    if (NonCached != INVALID_HANDLE_VALUE)
        CloseHandle(NonCached);
    if (Cached != INVALID_HANDLE_VALUE)
        CloseHandle(Cached);

    ok(GetFileAttributesExW(Path, GetFileExInfoStandard, &Attributes), "GetFileAttributesExW failed: %lu\n", GetLastError());
    ok(CompareFileTime(&Attributes.ftLastWriteTime, &OldTime) > 0, "LastWriteTime was not updated by the writes\n");
    Written.LowPart = Attributes.nFileSizeLow;
    Written.HighPart = Attributes.nFileSizeHigh;
    ok(Written.QuadPart == NC_MODEL_BYTES, "Closed file size %I64u, expected %lu\n", Written.QuadPart, NC_MODEL_BYTES);

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Reopen failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        NcVerify(File, Scratch, Model, NC_MODEL_BYTES, "non-cached read after reopen");
        CloseHandle(File);
    }
    Ok = DeleteFileW(Path);
    ok(Ok, "DeleteFileW failed: %lu\n", GetLastError());
}

START_TEST(NtWriteFileNonCached)
{
    WCHAR Drives[256];
    WCHAR FileSystem[MAX_PATH];
    PWSTR Root;
    PUCHAR Model;
    PUCHAR Data;
    PUCHAR Scratch;
    DWORD SectorsPerCluster;
    DWORD SectorSize;
    DWORD FreeClusters;
    DWORD TotalClusters;
    ULONG Tested = 0;

    Model = VirtualAlloc(NULL, NC_MODEL_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Data = VirtualAlloc(NULL, NC_MODEL_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Scratch = VirtualAlloc(NULL, NC_MODEL_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Model && Data && Scratch, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!Model || !Data || !Scratch)
        return;

    ok(GetLogicalDriveStringsW(RTL_NUMBER_OF(Drives), Drives) != 0, "GetLogicalDriveStringsW failed: %lu\n", GetLastError());
    for (Root = Drives; *Root; Root += wcslen(Root) + 1)
    {
        if (GetDriveTypeW(Root) != DRIVE_FIXED)
            continue;
        if (!GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, RTL_NUMBER_OF(FileSystem)))
            continue;
        if (_wcsicmp(FileSystem, L"NTFS") != 0 && _wcsicmp(FileSystem, L"FAT32") != 0 && _wcsicmp(FileSystem, L"FAT") != 0)
            continue;
        if (!GetDiskFreeSpaceW(Root, &SectorsPerCluster, &SectorSize, &FreeClusters, &TotalClusters) ||
            SectorSize == 0 || SectorSize > 64 * 1024 || 4096 % SectorSize != 0)
            continue;
        NcTestDirectory(Root, FileSystem, SectorSize, Model, Data, Scratch);
        Tested++;
    }
    ok(Tested != 0, "No fixed NTFS or FAT volume was tested\n");

    VirtualFree(Scratch, 0, MEM_RELEASE);
    VirtualFree(Data, 0, MEM_RELEASE);
    VirtualFree(Model, 0, MEM_RELEASE);
}
