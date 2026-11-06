/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test GetFileInformationByHandleEx with FileStorageInfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(FileStorageInfo)
{
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    FILE_STORAGE_INFO Info;
    HANDLE File;
    BOOL Result;
    DWORD Error;

    GetTempPathW(RTL_NUMBER_OF(Directory), Directory);
    ok(GetTempFileNameW(Directory, L"fsi", 0, Path) != 0, "GetTempFileName failed with %lu\n", GetLastError());

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    memset(&Info, 0xCC, sizeof(Info));
    Result = GetFileInformationByHandleEx(File, FileStorageInfo, &Info, sizeof(Info));
    ok(Result, "FileStorageInfo failed with %lu\n", GetLastError());
    if (Result)
    {
        trace("logical %lu atomicity %lu performance %lu effective %lu flags 0x%lx sector offset 0x%lx partition offset 0x%lx\n",
              Info.LogicalBytesPerSector, Info.PhysicalBytesPerSectorForAtomicity,
              Info.PhysicalBytesPerSectorForPerformance, Info.FileSystemEffectivePhysicalBytesPerSectorForAtomicity,
              Info.Flags, Info.ByteOffsetForSectorAlignment, Info.ByteOffsetForPartitionAlignment);
        ok(Info.LogicalBytesPerSector >= 512 && (Info.LogicalBytesPerSector & (Info.LogicalBytesPerSector - 1)) == 0,
           "LogicalBytesPerSector %lu\n", Info.LogicalBytesPerSector);
        ok(Info.PhysicalBytesPerSectorForAtomicity >= Info.LogicalBytesPerSector,
           "PhysicalBytesPerSectorForAtomicity %lu\n", Info.PhysicalBytesPerSectorForAtomicity);
        ok(Info.PhysicalBytesPerSectorForPerformance >= Info.LogicalBytesPerSector,
           "PhysicalBytesPerSectorForPerformance %lu\n", Info.PhysicalBytesPerSectorForPerformance);
        ok(Info.FileSystemEffectivePhysicalBytesPerSectorForAtomicity >= Info.LogicalBytesPerSector,
           "FileSystemEffectivePhysicalBytesPerSectorForAtomicity %lu\n",
           Info.FileSystemEffectivePhysicalBytesPerSectorForAtomicity);
    }

    SetLastError(0xdeadbeef);
    Result = GetFileInformationByHandleEx(File, FileStorageInfo, &Info, sizeof(Info) - 1);
    Error = GetLastError();
    ok(!Result, "Short buffer succeeded\n");
    ok(Error == ERROR_BAD_LENGTH, "Short buffer failed with %lu\n", Error);

    CloseHandle(File);
}
