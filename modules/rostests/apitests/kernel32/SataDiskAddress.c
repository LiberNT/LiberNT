/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the SCSI address and identity an AHCI port reports for its disk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <ntddscsi.h>

static ULONG SataPaths;

static VOID
TestDisk(ULONG Number)
{
    WCHAR Path[64];
    HANDLE hDisk;
    SCSI_ADDRESS Address;
    STORAGE_PROPERTY_QUERY Query;
    BYTE Buffer[1024];
    PSTORAGE_DEVICE_DESCRIPTOR Device = (PSTORAGE_DEVICE_DESCRIPTOR)Buffer;
    DWORD Returned;

    StringCchPrintfW(Path, _countof(Path), L"\\\\.\\PhysicalDrive%lu", Number);
    hDisk = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE)
        return;

    ZeroMemory(&Query, sizeof(Query));
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;
    ZeroMemory(Buffer, sizeof(Buffer));
    if (!DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), Buffer, sizeof(Buffer), &Returned, NULL))
        Device->BusType = BusTypeUnknown;
    else
    {
        trace("%ls vendor '%s' product '%s' revision '%s' bus type %u\n", Path,
              Device->VendorIdOffset ? (PCSTR)Buffer + Device->VendorIdOffset : "",
              Device->ProductIdOffset ? (PCSTR)Buffer + Device->ProductIdOffset : "",
              Device->ProductRevisionOffset ? (PCSTR)Buffer + Device->ProductRevisionOffset : "",
              Device->BusType);
    }

    if (DeviceIoControl(hDisk, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &Address, sizeof(Address), &Returned, NULL))
    {
        trace("%ls port %u path %u target %u lun %u\n", Path, Address.PortNumber, Address.PathId,
              Address.TargetId, Address.Lun);
        if (Device->BusType == BusTypeSata)
        {
            ok(Address.TargetId == 0, "%ls target %u\n", Path, Address.TargetId);
            ok(Address.Lun == 0, "%ls lun %u\n", Path, Address.Lun);
            ok(Address.PathId < 32, "%ls path %u\n", Path, Address.PathId);
            if (Address.PathId < 32)
            {
                ok(!(SataPaths & (1u << Address.PathId)), "%ls shares path %u with another disk\n", Path, Address.PathId);
                SataPaths |= 1u << Address.PathId;
            }
        }
    }
    else
    {
        trace("%ls IOCTL_SCSI_GET_ADDRESS failed (%lu)\n", Path, GetLastError());
    }

    CloseHandle(hDisk);
}

START_TEST(SataDiskAddress)
{
    for (ULONG i = 0; i < 8; i++)
        TestDisk(i);
    if (SataPaths == 0)
        skip("No SATA disk\n");
}
