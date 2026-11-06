/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the mount manager leaves no remote database on the EFI system partition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/* C12A7328-F81F-11D2-BA4B-00A0C93EC93B, PARTITION_SYSTEM_GUID in diskguid.h */
static const GUID EfiSystemPartition =
    { 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };

/* The UEFI specification's system partition type on an MBR disk. */
#define MBR_EFI_SYSTEM_PARTITION 0xEF

static BOOL
IsEfiSystemPartition(
    _In_ PWSTR Volume)
{
    PARTITION_INFORMATION_EX Info;
    size_t Length = wcslen(Volume);
    HANDLE Handle;
    DWORD Returned;
    BOOL Result;

    Volume[Length - 1] = UNICODE_NULL;
    Handle = CreateFileW(Volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Volume[Length - 1] = L'\\';
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    Result = DeviceIoControl(Handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                             &Info, sizeof(Info), &Returned, NULL);
    CloseHandle(Handle);
    if (!Result)
        return FALSE;
    if (Info.PartitionStyle == PARTITION_STYLE_GPT)
        return IsEqualGUID(&Info.Gpt.PartitionType, &EfiSystemPartition);
    return Info.PartitionStyle == PARTITION_STYLE_MBR && Info.Mbr.PartitionType == MBR_EFI_SYSTEM_PARTITION;
}

START_TEST(RemoteDatabase)
{
    WCHAR Volume[MAX_PATH], Path[MAX_PATH + 64];
    ULONG Checked = 0;
    HANDLE Find;

    Find = FindFirstVolumeW(Volume, ARRAYSIZE(Volume));
    if (Find == INVALID_HANDLE_VALUE)
    {
        skip("FindFirstVolumeW failed: %lu\n", GetLastError());
        return;
    }

    do
    {
        DWORD Attributes, Error;

        if (!IsEfiSystemPartition(Volume))
            continue;

        StringCchPrintfW(Path, ARRAYSIZE(Path), L"%sSystem Volume Information\\MountPointManagerRemoteDatabase", Volume);
        SetLastError(ERROR_SUCCESS);
        Attributes = GetFileAttributesW(Path);
        Error = GetLastError();
        if (Attributes == INVALID_FILE_ATTRIBUTES && Error == ERROR_ACCESS_DENIED)
        {
            skip("%ls cannot be read\n", Volume);
            continue;
        }
        ok(Attributes == INVALID_FILE_ATTRIBUTES &&
           (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND),
           "%ls holds a remote database (attributes 0x%lx, error %lu)\n", Volume, Attributes, Error);
        Checked++;
    } while (FindNextVolumeW(Find, Volume, ARRAYSIZE(Volume)));
    FindVolumeClose(Find);

    if (Checked == 0)
        skip("No readable EFI system partition\n");
}
