/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which BCD partition devices name a partition that still exists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <strsafe.h>

#define ELEMENT_SIZE     0x58
#define TYPE_OFFSET      0x10
#define SIZE_OFFSET      0x18
#define PARTITION_OFFSET 0x20
#define STYLE_OFFSET     0x34
#define DISK_OFFSET      0x38
#define DEVICE_PARTITION 6
#define BODY_SIZE        0x48
#define STYLE_MBR        1

static
BOOLEAN
GetSystemPartitionElement(
    _Out_writes_bytes_(ELEMENT_SIZE) PUCHAR Element,
    _Out_ PBOOLEAN Gpt)
{
    union
    {
        DRIVE_LAYOUT_INFORMATION_EX Layout;
        UCHAR Buffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + 128 * sizeof(PARTITION_INFORMATION_EX)];
    } Drive;
    PARTITION_INFORMATION_EX Partition;
    STORAGE_DEVICE_NUMBER Number;
    WCHAR Volume[] = L"\\\\.\\?:", Path[MAX_PATH], Windows[MAX_PATH];
    HANDLE Handle;
    DWORD Bytes;
    ULONG Type = DEVICE_PARTITION, Size = BODY_SIZE, Style = STYLE_MBR;
    BOOL Ok;

    if (!GetSystemWindowsDirectoryW(Windows, MAX_PATH))
        return FALSE;
    Volume[4] = Windows[0];

    Handle = CreateFileW(Volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = DeviceIoControl(Handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &Partition, sizeof(Partition), &Bytes, NULL) &&
         DeviceIoControl(Handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Bytes, NULL);
    CloseHandle(Handle);
    if (!Ok)
        return FALSE;

    StringCchPrintfW(Path, MAX_PATH, L"\\\\.\\PhysicalDrive%lu", Number.DeviceNumber);
    Handle = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = DeviceIoControl(Handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, &Drive, sizeof(Drive), &Bytes, NULL);
    CloseHandle(Handle);
    if (!Ok)
        return FALSE;

    ZeroMemory(Element, ELEMENT_SIZE);
    CopyMemory(Element + TYPE_OFFSET, &Type, sizeof(Type));
    CopyMemory(Element + SIZE_OFFSET, &Size, sizeof(Size));
    *Gpt = (Partition.PartitionStyle == PARTITION_STYLE_GPT);
    if (*Gpt)
    {
        CopyMemory(Element + PARTITION_OFFSET, &Partition.Gpt.PartitionId, sizeof(GUID));
        CopyMemory(Element + DISK_OFFSET, &Drive.Layout.Gpt.DiskId, sizeof(GUID));
    }
    else
    {
        CopyMemory(Element + PARTITION_OFFSET, &Partition.StartingOffset, sizeof(ULONGLONG));
        CopyMemory(Element + STYLE_OFFSET, &Style, sizeof(Style));
        CopyMemory(Element + DISK_OFFSET, &Drive.Layout.Mbr.Signature, sizeof(ULONG));
    }
    return TRUE;
}

START_TEST(BcdPartitionPresent)
{
    UCHAR Element[ELEMENT_SIZE], Changed[ELEMENT_SIZE];
    ULONG Type = 5;
    BOOLEAN Gpt;

    if (!GetSystemPartitionElement(Element, &Gpt))
    {
        skip("The disk of the system volume cannot be read as Setup reads it (%lu)\n", GetLastError());
        return;
    }

    ok(IsBcdPartitionElementPresent(Element), "The system partition is not found\n");

    CopyMemory(Changed, Element, sizeof(Changed));
    Changed[PARTITION_OFFSET + 1] ^= 0x5A;
    ok(!IsBcdPartitionElementPresent(Changed), "A partition that does not exist is found (GPT %d)\n", Gpt);

    CopyMemory(Changed, Element, sizeof(Changed));
    Changed[DISK_OFFSET + 1] ^= 0x5A;
    ok(!IsBcdPartitionElementPresent(Changed), "A partition of a disk that does not exist is found (GPT %d)\n", Gpt);

    CopyMemory(Changed, Element, sizeof(Changed));
    CopyMemory(Changed + TYPE_OFFSET, &Type, sizeof(Type));
    Changed[PARTITION_OFFSET + 1] ^= 0x5A;
    ok(IsBcdPartitionElementPresent(Changed), "A device that is not a partition is taken as missing\n");
}
