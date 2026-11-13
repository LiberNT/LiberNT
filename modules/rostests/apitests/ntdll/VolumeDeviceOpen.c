/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a volume device and its root directory answer when opened
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>

static
NTSTATUS
OpenVolume(
    _Out_ PHANDLE Handle,
    _In_ PCWSTR Path,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ ULONG OpenOptions)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, NULL);

    return NtOpenFile(Handle,
                      DesiredAccess,
                      &ObjectAttributes,
                      &IoStatusBlock,
                      ShareAccess,
                      OpenOptions);
}

START_TEST(VolumeDeviceOpen)
{
    WCHAR VolumeName[MAX_PATH], DeviceName[MAX_PATH], RootName[MAX_PATH];
    FILE_FS_SIZE_INFORMATION SizeInfo;
    VOLUME_DISK_EXTENTS Extents;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    HANDLE Handle;
    HANDLE Find;
    DWORD Returned;
    SIZE_T Length;

    Find = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
    if (Find == INVALID_HANDLE_VALUE)
    {
        skip("No volume to open (%lu)\n", GetLastError());
        return;
    }
    FindVolumeClose(Find);

    /* QueryDosDevice wants the name without its trailing backslash */
    Length = wcslen(VolumeName);
    if (Length != 0 && VolumeName[Length - 1] == L'\\')
        VolumeName[Length - 1] = UNICODE_NULL;

    if (QueryDosDeviceW(&VolumeName[4], DeviceName, ARRAYSIZE(DeviceName)) == 0)
    {
        skip("%ls has no device name (%lu)\n", VolumeName, GetLastError());
        return;
    }

    StringCchCopyW(RootName, ARRAYSIZE(RootName), DeviceName);
    StringCchCatW(RootName, ARRAYSIZE(RootName), L"\\");

    /* The volume device is not a directory, whatever is mounted on it */
    Handle = NULL;
    Status = OpenVolume(&Handle,
                        DeviceName,
                        SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    ok(Status == STATUS_NOT_A_DIRECTORY, "%ls as a directory: 0x%08lx\n", DeviceName, Status);
    if (NT_SUCCESS(Status))
        NtClose(Handle);

    /* Its root directory answers what the file system holds */
    Handle = NULL;
    Status = OpenVolume(&Handle,
                        RootName,
                        SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    ok(Status == STATUS_SUCCESS, "%ls: 0x%08lx\n", RootName, Status);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&SizeInfo, sizeof(SizeInfo));
        Status = NtQueryVolumeInformationFile(Handle,
                                              &IoStatusBlock,
                                              &SizeInfo,
                                              sizeof(SizeInfo),
                                              FileFsSizeInformation);
        ok(Status == STATUS_SUCCESS, "FileFsSizeInformation: 0x%08lx\n", Status);
        ok(SizeInfo.BytesPerSector != 0, "BytesPerSector is 0\n");
        ok(SizeInfo.TotalAllocationUnits.QuadPart != 0, "TotalAllocationUnits is 0\n");

        NtClose(Handle);
    }

    /* The volume device answers where it lives on its disk */
    Handle = NULL;
    Status = OpenVolume(&Handle,
                        DeviceName,
                        SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    ok(Status == STATUS_SUCCESS, "%ls: 0x%08lx\n", DeviceName, Status);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Extents, sizeof(Extents));
        Returned = 0;
        ok(DeviceIoControl(Handle,
                           IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                           NULL,
                           0,
                           &Extents,
                           sizeof(Extents),
                           &Returned,
                           NULL),
           "IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS: %lu\n", GetLastError());
        ok(Extents.NumberOfDiskExtents == 1, "NumberOfDiskExtents %lu\n", Extents.NumberOfDiskExtents);
        ok(Extents.Extents[0].ExtentLength.QuadPart != 0, "ExtentLength is 0\n");

        NtClose(Handle);
    }
}
