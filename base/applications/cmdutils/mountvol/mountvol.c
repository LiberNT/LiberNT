/*
 * PROJECT:     LiberNT Mount Volume Command
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Creates, deletes and lists volume mount points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <winnls.h>
#include <mountmgr.h>

#include <conutils.h>

#include "resource.h"

static const GUID MountVolEfiSystemPartitionGuid =
    { 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };

static
VOID
PrintError(DWORD ErrorCode)
{
    ConMsgPuts(StdErr, FORMAT_MESSAGE_FROM_SYSTEM, NULL, ErrorCode, LANG_USER_DEFAULT);
}

static
BOOL
IsOption(PCWSTR Argument, PCWSTR Option)
{
    return (Argument[0] == L'/' || Argument[0] == L'-') && _wcsicmp(Argument + 1, Option) == 0;
}

static
VOID
MakeMountPath(PCWSTR Argument, PWSTR Path, SIZE_T Count)
{
    SIZE_T Length;

    wcsncpy(Path, Argument, Count - 2);
    Path[Count - 2] = UNICODE_NULL;
    Length = wcslen(Path);
    if (Length > 0 && Path[Length - 1] != L'\\')
    {
        Path[Length] = L'\\';
        Path[Length + 1] = UNICODE_NULL;
    }
}

static
VOID
ListVolumes(VOID)
{
    HANDLE Find;
    DWORD Length;
    PWSTR Paths;
    PWSTR Path;
    WCHAR VolumeName[MAX_PATH];

    ConResPuts(StdOut, IDS_VOLUME_LIST);

    Find = FindFirstVolumeW(VolumeName, _countof(VolumeName));
    if (Find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        ConPrintf(StdOut, L"    %s\n", VolumeName);

        Length = 0;
        GetVolumePathNamesForVolumeNameW(VolumeName, NULL, 0, &Length);
        Paths = NULL;
        if (Length > 0)
            Paths = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length * sizeof(WCHAR));

        if (Paths != NULL &&
            GetVolumePathNamesForVolumeNameW(VolumeName, Paths, Length, &Length) &&
            Paths[0] != UNICODE_NULL)
        {
            for (Path = Paths; *Path != UNICODE_NULL; Path += wcslen(Path) + 1)
                ConPrintf(StdOut, L"        %s\n", Path);
        }
        else
        {
            ConResPuts(StdOut, IDS_NO_MOUNT_POINTS);
        }

        if (Paths != NULL)
            HeapFree(GetProcessHeap(), 0, Paths);

        ConPuts(StdOut, L"\n");
    } while (FindNextVolumeW(Find, VolumeName, _countof(VolumeName)));

    FindVolumeClose(Find);
}

static
BOOL
MountMgrControl(DWORD IoControlCode, PVOID Input, DWORD InputLength)
{
    BOOL Ret;
    HANDLE MountMgr;
    DWORD BytesReturned;

    MountMgr = CreateFileW(MOUNTMGR_DOS_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (MountMgr == INVALID_HANDLE_VALUE)
        return FALSE;

    Ret = DeviceIoControl(MountMgr, IoControlCode, Input, InputLength, NULL, 0, &BytesReturned, NULL);
    CloseHandle(MountMgr);
    return Ret;
}

static
BOOL
SetAutoMount(MOUNTMGR_AUTO_MOUNT_STATE State)
{
    MOUNTMGR_SET_AUTO_MOUNT AutoMount;

    AutoMount.NewState = State;
    return MountMgrControl(IOCTL_MOUNTMGR_SET_AUTO_MOUNT, &AutoMount, sizeof(AutoMount));
}

static
BOOL
IsEfiSystemPartition(PCWSTR VolumeName)
{
    BOOL Ret = FALSE;
    HANDLE Volume;
    DWORD BytesReturned;
    WCHAR DevicePath[MAX_PATH];
    PARTITION_INFORMATION_EX PartitionInfo;

    wcsncpy(DevicePath, VolumeName, _countof(DevicePath) - 1);
    DevicePath[_countof(DevicePath) - 1] = UNICODE_NULL;
    if (wcslen(DevicePath) > 0 && DevicePath[wcslen(DevicePath) - 1] == L'\\')
        DevicePath[wcslen(DevicePath) - 1] = UNICODE_NULL;

    Volume = CreateFileW(DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        return FALSE;

    if (DeviceIoControl(Volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                        &PartitionInfo, sizeof(PartitionInfo), &BytesReturned, NULL))
    {
        if (PartitionInfo.PartitionStyle == PARTITION_STYLE_GPT)
            Ret = IsEqualGUID(&PartitionInfo.Gpt.PartitionType, &MountVolEfiSystemPartitionGuid);
        else if (PartitionInfo.PartitionStyle == PARTITION_STYLE_MBR)
            Ret = (PartitionInfo.Mbr.PartitionType == 0xEF);
    }

    CloseHandle(Volume);
    return Ret;
}

static
BOOL
MountEfiSystemPartition(PCWSTR MountPath)
{
    HANDLE Find;
    WCHAR VolumeName[MAX_PATH];

    Find = FindFirstVolumeW(VolumeName, _countof(VolumeName));
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (IsEfiSystemPartition(VolumeName))
        {
            FindVolumeClose(Find);
            return SetVolumeMountPointW(MountPath, VolumeName);
        }
    } while (FindNextVolumeW(Find, VolumeName, _countof(VolumeName)));

    FindVolumeClose(Find);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}

int wmain(int argc, WCHAR *argv[])
{
    BOOL Ret;
    WCHAR MountPath[MAX_PATH];
    WCHAR VolumeName[MAX_PATH];

    ConInitStdStreams();

    if (argc < 2 || IsOption(argv[1], L"?"))
    {
        ConResPuts(StdOut, IDS_USAGE);
        ListVolumes();
        return 0;
    }

    if (argc == 2)
    {
        if (IsOption(argv[1], L"R"))
            Ret = MountMgrControl(IOCTL_MOUNTMGR_SCRUB_REGISTRY, NULL, 0);
        else if (IsOption(argv[1], L"N"))
            Ret = SetAutoMount(Disabled);
        else if (IsOption(argv[1], L"E"))
            Ret = SetAutoMount(Enabled);
        else
        {
            ConResPuts(StdErr, IDS_INVALID_PARAMETERS);
            return 1;
        }
    }
    else if (argc == 3)
    {
        MakeMountPath(argv[1], MountPath, _countof(MountPath));

        if (IsOption(argv[2], L"D"))
        {
            Ret = DeleteVolumeMountPointW(MountPath);
        }
        else if (IsOption(argv[2], L"L"))
        {
            Ret = GetVolumeNameForVolumeMountPointW(MountPath, VolumeName, _countof(VolumeName));
            if (Ret)
                ConPrintf(StdOut, L"    %s\n", VolumeName);
        }
        else if (IsOption(argv[2], L"S"))
        {
            Ret = MountEfiSystemPartition(MountPath);
        }
        else if (argv[2][0] == L'/' || argv[2][0] == L'-')
        {
            ConResPuts(StdErr, IDS_INVALID_PARAMETERS);
            return 1;
        }
        else
        {
            Ret = SetVolumeMountPointW(MountPath, argv[2]);
        }
    }
    else
    {
        ConResPuts(StdErr, IDS_INVALID_PARAMETERS);
        return 1;
    }

    if (!Ret)
    {
        PrintError(GetLastError());
        return 1;
    }

    return 0;
}
