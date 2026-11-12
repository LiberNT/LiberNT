/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the system partition the kernel records under HKLM\SYSTEM\Setup at boot
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winreg.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <strsafe.h>

START_TEST(SystemPartition)
{
    static const WCHAR VolumePrefix[] = L"\\Device\\HarddiskVolume";
    WCHAR Partition[MAX_PATH], Path[MAX_PATH + 16];
    STORAGE_DEVICE_NUMBER Number;
    DWORD Size = sizeof(Partition), Type = REG_NONE, Returned;
    HANDLE Volume;
    LONG Error;
    BOOL Result;

    Error = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", L"SystemPartition", RRF_RT_ANY, &Type, Partition, &Size);
    ok(Error == ERROR_SUCCESS, "Reading SystemPartition failed with %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    ok(Type == REG_SZ, "SystemPartition type %lu\n", Type);
    ok(!_wcsnicmp(Partition, VolumePrefix, ARRAYSIZE(VolumePrefix) - 1), "SystemPartition is %S\n", Partition);

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\?\\GLOBALROOT%s", Partition);
    Volume = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Volume != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", Path, GetLastError());
    if (Volume == INVALID_HANDLE_VALUE)
        return;

    Result = DeviceIoControl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned, NULL);
    ok(Result, "IOCTL_STORAGE_GET_DEVICE_NUMBER failed with %lu\n", GetLastError());
    if (Result)
    {
        ok(Number.DeviceType == FILE_DEVICE_DISK, "Device type %lu\n", Number.DeviceType);
        ok(Number.PartitionNumber != 0, "The system partition is partition 0 of disk %lu\n", Number.DeviceNumber);
    }

    CloseHandle(Volume);
}
