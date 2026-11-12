/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the disk stack on VirtIO block devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <strsafe.h>

#define TEST_DISK_SIZE          (72ULL * 1024 * 1024)
#define TEST_DISK_SERIAL        "VIOBLKTEST"
#define READONLY_DISK_SIZE      (24ULL * 1024 * 1024)
#define READONLY_DISK_SERIAL    "VIOBLKRO"
#define LARGE_TRANSFER          (1024 * 1024)

static BOOL
QueryProperty(
    _In_ HANDLE Disk,
    _In_ STORAGE_PROPERTY_ID PropertyId,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ DWORD Size)
{
    STORAGE_PROPERTY_QUERY Query;
    DWORD Returned;

    ZeroMemory(&Query, sizeof(Query));
    ZeroMemory(Buffer, Size);
    Query.PropertyId = PropertyId;
    Query.QueryType = PropertyStandardQuery;
    return DeviceIoControl(Disk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), Buffer, Size, &Returned, NULL);
}

static void
GetSerial(
    _In_ HANDLE Disk,
    _Out_writes_(Size) PSTR Serial,
    _In_ ULONG Size)
{
    BYTE Buffer[1024];
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor = (PSTORAGE_DEVICE_DESCRIPTOR)Buffer;
    PCSTR Source;
    ULONG Length;

    Serial[0] = ANSI_NULL;
    if (!QueryProperty(Disk, StorageDeviceProperty, Buffer, sizeof(Buffer)) ||
        Descriptor->SerialNumberOffset == 0 || Descriptor->SerialNumberOffset >= sizeof(Buffer))
    {
        return;
    }

    Source = (PCSTR)Buffer + Descriptor->SerialNumberOffset;
    while (*Source == ' ')
        Source++;
    StringCchCopyNA(Serial, Size, Source, sizeof(Buffer) - (Source - (PCSTR)Buffer));
    for (Length = (ULONG)strlen(Serial); Length != 0 && Serial[Length - 1] == ' '; Length--)
        Serial[Length - 1] = ANSI_NULL;
}

static HANDLE
OpenDiskBySize(
    _In_ ULONGLONG Size,
    _In_ DWORD Access,
    _In_ DWORD Flags,
    _Out_ PULONG Number)
{
    WCHAR Path[MAX_PATH];
    GET_LENGTH_INFORMATION LengthInfo;
    DWORD Returned;
    HANDLE Disk;
    ULONG Index;

    for (Index = 0; Index < 32; Index++)
    {
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Index);
        Disk = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, Flags, NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;
        if (DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &LengthInfo, sizeof(LengthInfo), &Returned, NULL) &&
            (ULONGLONG)LengthInfo.Length.QuadPart == Size)
        {
            *Number = Index;
            return Disk;
        }
        CloseHandle(Disk);
    }

    *Number = MAXULONG;
    return INVALID_HANDLE_VALUE;
}

static BOOL
TransferAt(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ DWORD Length,
    _In_ BOOL Write,
    _Out_ PDWORD Transferred)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);
    *Transferred = 0;
    if (Write)
        return WriteFile(Disk, Buffer, Length, Transferred, &Overlapped);
    return ReadFile(Disk, Buffer, Length, Transferred, &Overlapped);
}

static void
FillPattern(
    _Out_writes_bytes_(Length) PBYTE Buffer,
    _In_ DWORD Length,
    _In_ ULONGLONG Offset)
{
    DWORD Index;

    for (Index = 0; Index < Length; Index++)
        Buffer[Index] = (BYTE)(((Offset + Index) * 131) >> 3);
}

static void
TestDescriptors(
    _In_ HANDLE Disk)
{
    STORAGE_ADAPTER_DESCRIPTOR Adapter;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR Alignment;
    DEVICE_TRIM_DESCRIPTOR Trim;
    BYTE Buffer[1024];
    PSTORAGE_DEVICE_DESCRIPTOR Device = (PSTORAGE_DEVICE_DESCRIPTOR)Buffer;
    DISK_GEOMETRY_EX Geometry;
    DISK_CACHE_INFORMATION Cache;
    CHAR Serial[64];
    DWORD Returned;
    BOOL Result;

    GetSerial(Disk, Serial, ARRAYSIZE(Serial));
    ok(strcmp(Serial, TEST_DISK_SERIAL) == 0, "Serial number '%s'\n", Serial);

    ok(QueryProperty(Disk, StorageAdapterProperty, &Adapter, sizeof(Adapter)),
       "StorageAdapterProperty failed with %lu\n", GetLastError());
    ok(Adapter.BusType == BusTypeScsi, "Adapter bus type %u\n", Adapter.BusType);
    trace("Adapter: max transfer %lu, physical pages %lu, alignment mask 0x%lx, command queueing %u\n",
          Adapter.MaximumTransferLength, Adapter.MaximumPhysicalPages, Adapter.AlignmentMask, Adapter.CommandQueueing);

    ok(QueryProperty(Disk, StorageDeviceProperty, Buffer, sizeof(Buffer)),
       "StorageDeviceProperty failed with %lu\n", GetLastError());
    ok(Device->DeviceType == 0, "Device type %u\n", Device->DeviceType);
    ok(Device->RemovableMedia == FALSE, "Removable media %u\n", Device->RemovableMedia);
    ok(Device->BusType == BusTypeScsi, "Device bus type %u\n", Device->BusType);

    ok(DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &Geometry, sizeof(Geometry), &Returned, NULL),
       "IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed with %lu\n", GetLastError());
    ok(Geometry.Geometry.MediaType == FixedMedia, "Media type %u\n", Geometry.Geometry.MediaType);
    ok(Geometry.Geometry.BytesPerSector == 512, "Bytes per sector %lu\n", Geometry.Geometry.BytesPerSector);
    ok((ULONGLONG)Geometry.DiskSize.QuadPart == TEST_DISK_SIZE, "Disk size %I64u\n", Geometry.DiskSize.QuadPart);

    ok(QueryProperty(Disk, StorageAccessAlignmentProperty, &Alignment, sizeof(Alignment)),
       "StorageAccessAlignmentProperty failed with %lu\n", GetLastError());
    ok(Alignment.BytesPerLogicalSector == 512, "Logical sector %lu\n", Alignment.BytesPerLogicalSector);
    ok(Alignment.BytesPerPhysicalSector == 4096, "Physical sector %lu\n", Alignment.BytesPerPhysicalSector);
    ok(Alignment.BytesOffsetForSectorAlignment == 0, "Sector alignment offset %lu\n",
       Alignment.BytesOffsetForSectorAlignment);

    ok(QueryProperty(Disk, StorageDeviceTrimProperty, &Trim, sizeof(Trim)),
       "StorageDeviceTrimProperty failed with %lu\n", GetLastError());
    ok(Trim.TrimEnabled == TRUE, "Trim enabled %u\n", Trim.TrimEnabled);

    ok(DeviceIoControl(Disk, IOCTL_DISK_GET_CACHE_INFORMATION, NULL, 0, &Cache, sizeof(Cache), &Returned, NULL),
       "IOCTL_DISK_GET_CACHE_INFORMATION failed with %lu\n", GetLastError());
    ok(Cache.WriteCacheEnabled == TRUE, "Write cache enabled %u\n", Cache.WriteCacheEnabled);

    Cache.WriteCacheEnabled = FALSE;
    Result = DeviceIoControl(Disk, IOCTL_DISK_SET_CACHE_INFORMATION, &Cache, sizeof(Cache), NULL, 0, &Returned, NULL);
    trace("Disabling the write cache: %d, error %lu\n", Result, Result ? 0 : GetLastError());
    ZeroMemory(&Cache, sizeof(Cache));
    ok(DeviceIoControl(Disk, IOCTL_DISK_GET_CACHE_INFORMATION, NULL, 0, &Cache, sizeof(Cache), &Returned, NULL),
       "IOCTL_DISK_GET_CACHE_INFORMATION failed with %lu\n", GetLastError());
    ok(Cache.WriteCacheEnabled == !Result, "Write cache enabled %u after disabling it returned %d\n",
       Cache.WriteCacheEnabled, Result);
    if (!Result)
        return;
    Cache.WriteCacheEnabled = TRUE;
    ok(DeviceIoControl(Disk, IOCTL_DISK_SET_CACHE_INFORMATION, &Cache, sizeof(Cache), NULL, 0, &Returned, NULL),
       "Enabling the write cache failed with %lu\n", GetLastError());
    ZeroMemory(&Cache, sizeof(Cache));
    ok(DeviceIoControl(Disk, IOCTL_DISK_GET_CACHE_INFORMATION, NULL, 0, &Cache, sizeof(Cache), &Returned, NULL),
       "IOCTL_DISK_GET_CACHE_INFORMATION failed with %lu\n", GetLastError());
    ok(Cache.WriteCacheEnabled == TRUE, "Write cache enabled %u after enabling it\n", Cache.WriteCacheEnabled);
}

typedef struct _TRIM_REQUEST
{
    DEVICE_MANAGE_DATA_SET_ATTRIBUTES Attributes;
    DEVICE_DATA_SET_RANGE Range;
} TRIM_REQUEST, *PTRIM_REQUEST;

static void
TestTransfers(
    _In_ HANDLE Disk)
{
    PTRIM_REQUEST TrimRequest;
    PBYTE Pattern;
    PBYTE Readback;
    DWORD Transferred;
    DWORD Returned;
    BOOL Result;

    Pattern = VirtualAlloc(NULL, LARGE_TRANSFER, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Readback = VirtualAlloc(NULL, LARGE_TRANSFER, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    TrimRequest = VirtualAlloc(NULL, sizeof(*TrimRequest), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Pattern || !Readback || !TrimRequest)
    {
        skip("Out of memory\n");
        goto Cleanup;
    }

    FillPattern(Pattern, 64 * 1024, 1024 * 1024);
    Result = TransferAt(Disk, 1024 * 1024, Pattern, 64 * 1024, TRUE, &Transferred);
    ok(Result && Transferred == 64 * 1024, "64 KiB write: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    Result = TransferAt(Disk, 1024 * 1024, Readback, 64 * 1024, FALSE, &Transferred);
    ok(Result && Transferred == 64 * 1024, "64 KiB read: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    ok(memcmp(Pattern, Readback, 64 * 1024) == 0, "64 KiB read returned different data\n");

    FillPattern(Pattern, LARGE_TRANSFER, 8 * 1024 * 1024);
    Result = TransferAt(Disk, 8 * 1024 * 1024, Pattern, LARGE_TRANSFER, TRUE, &Transferred);
    ok(Result && Transferred == LARGE_TRANSFER, "1 MiB write: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    ok(FlushFileBuffers(Disk), "FlushFileBuffers failed with %lu\n", GetLastError());
    ZeroMemory(Readback, LARGE_TRANSFER);
    Result = TransferAt(Disk, 8 * 1024 * 1024, Readback, LARGE_TRANSFER, FALSE, &Transferred);
    ok(Result && Transferred == LARGE_TRANSFER, "1 MiB read: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    ok(memcmp(Pattern, Readback, LARGE_TRANSFER) == 0, "1 MiB read returned different data\n");

    FillPattern(Pattern, 512, TEST_DISK_SIZE - 512);
    Result = TransferAt(Disk, TEST_DISK_SIZE - 512, Pattern, 512, TRUE, &Transferred);
    ok(Result && Transferred == 512, "Last sector write: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    Result = TransferAt(Disk, TEST_DISK_SIZE - 512, Readback, 512, FALSE, &Transferred);
    ok(Result && Transferred == 512, "Last sector read: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
    ok(memcmp(Pattern, Readback, 512) == 0, "Last sector read returned different data\n");

    SetLastError(0xdeadbeef);
    Result = TransferAt(Disk, TEST_DISK_SIZE, Readback, 512, FALSE, &Transferred);
    ok(!Result && Transferred == 0, "Read past the end: %d, %lu bytes\n", Result, Transferred);
    trace("Read past the end failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Result = TransferAt(Disk, TEST_DISK_SIZE - 512, Readback, 1024, FALSE, &Transferred);
    ok(!Result && Transferred == 0, "Read across the end: %d, %lu bytes\n", Result, Transferred);
    trace("Read across the end failed with %lu\n", GetLastError());

    ZeroMemory(TrimRequest, sizeof(*TrimRequest));
    TrimRequest->Attributes.Size = sizeof(TrimRequest->Attributes);
    TrimRequest->Attributes.Action = DeviceDsmAction_Trim;
    TrimRequest->Attributes.DataSetRangesOffset = FIELD_OFFSET(TRIM_REQUEST, Range);
    TrimRequest->Attributes.DataSetRangesLength = sizeof(TrimRequest->Range);
    TrimRequest->Range.StartingOffset = 16 * 1024 * 1024;
    TrimRequest->Range.LengthInBytes = 1024 * 1024;
    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES, TrimRequest, sizeof(*TrimRequest),
                             NULL, 0, &Returned, NULL);
    ok(!Result && GetLastError() == ERROR_FILE_LEVEL_TRIM_NOT_SUPPORTED, "File-level trim: %d, error %lu\n",
       Result, GetLastError());
    TrimRequest->Attributes.Flags = DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED;
    ok(DeviceIoControl(Disk, IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES, TrimRequest, sizeof(*TrimRequest),
                       NULL, 0, &Returned, NULL),
       "Trim failed with %lu\n", GetLastError());
    Result = TransferAt(Disk, 16 * 1024 * 1024, Readback, 64 * 1024, FALSE, &Transferred);
    ok(Result && Transferred == 64 * 1024, "Read after trim: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());

Cleanup:
    if (TrimRequest)
        VirtualFree(TrimRequest, 0, MEM_RELEASE);
    if (Readback)
        VirtualFree(Readback, 0, MEM_RELEASE);
    if (Pattern)
        VirtualFree(Pattern, 0, MEM_RELEASE);
}

static void
TestReadOnlyDisk(void)
{
    CHAR Serial[64];
    PBYTE Sector;
    DWORD Transferred;
    DWORD Returned;
    ULONG Number;
    HANDLE Disk;
    BOOL Result;

    Disk = OpenDiskBySize(READONLY_DISK_SIZE, GENERIC_READ | GENERIC_WRITE,
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, &Number);
    if (Disk == INVALID_HANDLE_VALUE)
    {
        skip("No 24 MiB read-only VirtIO test disk\n");
        return;
    }

    GetSerial(Disk, Serial, ARRAYSIZE(Serial));
    ok(strcmp(Serial, READONLY_DISK_SERIAL) == 0, "Read-only disk serial number '%s'\n", Serial);

    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &Returned, NULL);
    ok(!Result && GetLastError() == ERROR_WRITE_PROTECT, "IOCTL_DISK_IS_WRITABLE: %d, error %lu\n", Result, GetLastError());

    Sector = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Sector)
    {
        Result = TransferAt(Disk, 4096, Sector, 4096, FALSE, &Transferred);
        ok(Result && Transferred == 4096, "Read-only disk read: %d, %lu bytes, error %lu\n", Result, Transferred, GetLastError());
        SetLastError(0xdeadbeef);
        Result = TransferAt(Disk, 4096, Sector, 4096, TRUE, &Transferred);
        ok(!Result && Transferred == 0, "Read-only disk write: %d, %lu bytes\n", Result, Transferred);
        trace("Read-only disk write failed with %lu\n", GetLastError());
        VirtualFree(Sector, 0, MEM_RELEASE);
    }

    CloseHandle(Disk);
}

START_TEST(VirtioDisk)
{
    ULONG Number;
    HANDLE Disk;

    Disk = OpenDiskBySize(TEST_DISK_SIZE, GENERIC_READ | GENERIC_WRITE,
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, &Number);
    if (Disk == INVALID_HANDLE_VALUE)
    {
        skip("No 72 MiB VirtIO test disk\n");
        return;
    }

    trace("VirtIO test disk is PhysicalDrive%lu\n", Number);
    TestDescriptors(Disk);
    TestTransfers(Disk);
    CloseHandle(Disk);

    TestReadOnlyDisk();
}
