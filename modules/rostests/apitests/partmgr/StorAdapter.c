/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the storage adapter descriptor of StorPort disks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ntddstor.h>

START_TEST(StorAdapter)
{
    STORAGE_PROPERTY_QUERY Query = { StorageAdapterProperty, PropertyStandardQuery };
    STORAGE_ADAPTER_DESCRIPTOR Adapter;
    CHAR Path[32];
    ULONG Number, Nvme = 0;
    DWORD Returned;
    HANDLE Disk;

    for (Number = 0; Number < 32; Number++)
    {
        sprintf(Path, "\\\\.\\PhysicalDrive%lu", Number);
        Disk = CreateFileA(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;
        ZeroMemory(&Adapter, sizeof(Adapter));
        ok(DeviceIoControl(Disk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), &Adapter, sizeof(Adapter),
                           &Returned, NULL), "StorageAdapterProperty of %s failed: %lu\n", Path, GetLastError());
        CloseHandle(Disk);
        if (Adapter.BusType != BusTypeNvme)
            continue;
        Nvme++;
        ok(Adapter.BusMajorVersion == 2 && Adapter.BusMinorVersion == 0,
           "The NVMe adapter of %s reports bus version %u.%u\n", Path, Adapter.BusMajorVersion,
           Adapter.BusMinorVersion);
    }
    if (!Nvme)
        skip("No NVMe disk is present\n");
}
