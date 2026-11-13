/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The free space NTFS reports follows every allocation and release and matches a fresh count after a remount
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <apitest.h>
#include <winioctl.h>
#include <strsafe.h>

#define TEST_VOLUME_LABEL L"NTFSTEST"
#define TEST_CLUSTERS 64

static WCHAR VolumeRoot[] = L"?:\\";
static WCHAR VolumeDevice[] = L"\\\\.\\?:";

static BOOL
FindTestVolume(VOID)
{
    WCHAR Label[MAX_PATH + 1];
    WCHAR FileSystem[MAX_PATH + 1];
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        VolumeRoot[0] = Letter;
        if (GetVolumeInformationW(VolumeRoot, Label, ARRAYSIZE(Label), NULL, NULL, NULL,
                                  FileSystem, ARRAYSIZE(FileSystem)) &&
            !wcscmp(Label, TEST_VOLUME_LABEL) &&
            !wcscmp(FileSystem, L"NTFS"))
        {
            VolumeDevice[4] = Letter;
            return TRUE;
        }
    }
    return FALSE;
}

static ULONGLONG
FreeClusters(_Out_opt_ PULONG ClusterBytes)
{
    DWORD SectorsPerCluster = 0, BytesPerSector = 0, Free = 0, Total = 0;

    ok(GetDiskFreeSpaceW(VolumeRoot, &SectorsPerCluster, &BytesPerSector, &Free, &Total),
       "GetDiskFreeSpaceW failed: %lu\n", GetLastError());
    if (ClusterBytes)
        *ClusterBytes = SectorsPerCluster * BytesPerSector;
    return Free;
}

static BOOL
SetAllocation(_In_ HANDLE File, _In_ LONGLONG Bytes)
{
    FILE_ALLOCATION_INFO Allocation;

    Allocation.AllocationSize.QuadPart = Bytes;
    return SetFileInformationByHandle(File, FileAllocationInfo, &Allocation, sizeof(Allocation));
}

static BOOL
Remount(VOID)
{
    DWORD Returned;
    HANDLE Volume;
    BOOL Done;

    Volume = CreateFileW(VolumeDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;
    Done = DeviceIoControl(Volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Returned, NULL) &&
           DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Returned, NULL);
    CloseHandle(Volume);
    return Done;
}

START_TEST(FreeClusters)
{
    WCHAR Path[MAX_PATH];
    FILE_STANDARD_INFO Standard;
    ULONGLONG Before, Grown, Shrunk, Remounted;
    ULONG ClusterBytes;
    HANDLE File;

    if (!FindTestVolume())
    {
        skip("no NTFS volume labelled %ls\n", TEST_VOLUME_LABEL);
        return;
    }

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sFreeClusters.bin", VolumeRoot);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed: %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    Before = FreeClusters(&ClusterBytes);
    ok(SetAllocation(File, (LONGLONG)TEST_CLUSTERS * ClusterBytes), "growing the allocation failed: %lu\n", GetLastError());
    ok(GetFileInformationByHandleEx(File, FileStandardInfo, &Standard, sizeof(Standard)),
       "FileStandardInfo failed: %lu\n", GetLastError());
    Grown = FreeClusters(NULL);
    ok(Before - Grown == (ULONGLONG)Standard.AllocationSize.QuadPart / ClusterBytes,
       "free clusters went from %I64u to %I64u for %I64u allocated bytes\n",
       Before, Grown, Standard.AllocationSize.QuadPart);

    ok(SetAllocation(File, 0), "releasing the allocation failed: %lu\n", GetLastError());
    Shrunk = FreeClusters(NULL);
    ok(Shrunk == Before, "free clusters are %I64u after the release, %I64u before the growth\n", Shrunk, Before);
    CloseHandle(File);

    Before = FreeClusters(NULL);
    if (!Remount())
    {
        skip("the test volume could not be dismounted: %lu\n", GetLastError());
        return;
    }
    Remounted = FreeClusters(NULL);
    ok(Remounted == Before, "a fresh count found %I64u free clusters, the volume reported %I64u\n", Remounted, Before);
}
