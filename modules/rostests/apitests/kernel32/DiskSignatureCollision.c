/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that of the disks sharing one signature only one is online
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>

#define MAX_DISKS 32

typedef struct _DISK_IDENTITY
{
    BOOL Present;
    BOOL Offline;
    PARTITION_STYLE Style;
    DWORD Signature;
    GUID DiskId;
} DISK_IDENTITY, *PDISK_IDENTITY;

static
DWORD
VolumeDisk(
    _In_ PCWSTR Volume)
{
    VOLUME_DISK_EXTENTS Extents;
    DWORD Bytes, Disk = MAXDWORD;
    HANDLE Handle;

    Handle = CreateFileW(Volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return MAXDWORD;
    if (DeviceIoControl(Handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &Extents, sizeof(Extents), &Bytes, NULL) &&
        Extents.NumberOfDiskExtents == 1)
    {
        Disk = Extents.Extents[0].DiskNumber;
    }
    CloseHandle(Handle);
    return Disk;
}

static
BOOL
DiskHasVolume(
    _In_ DWORD Disk)
{
    WCHAR Name[MAX_PATH];
    HANDLE Find;
    SIZE_T Length;
    BOOL Found = FALSE;

    Find = FindFirstVolumeW(Name, RTL_NUMBER_OF(Name));
    if (Find == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        Length = wcslen(Name);
        if (Length != 0 && Name[Length - 1] == L'\\')
            Name[Length - 1] = UNICODE_NULL;
        Found = (VolumeDisk(Name) == Disk);
    } while (!Found && FindNextVolumeW(Find, Name, RTL_NUMBER_OF(Name)));
    FindVolumeClose(Find);
    return Found;
}

static
DWORD
SystemPartitionDisk(VOID)
{
    WCHAR Partition[MAX_PATH], Path[MAX_PATH + 16];
    DWORD Size = sizeof(Partition) - sizeof(WCHAR), Type;
    HKEY Key;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return MAXDWORD;
    ZeroMemory(Partition, sizeof(Partition));
    Error = RegQueryValueExW(Key, L"SystemPartition", NULL, &Type, (PBYTE)Partition, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_SZ)
        return MAXDWORD;

    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"\\\\?\\GLOBALROOT%s", Partition);
    return VolumeDisk(Path);
}

static
HANDLE
OpenDisk(
    _In_ DWORD Disk,
    _In_ DWORD Access)
{
    WCHAR Name[32];

    StringCchPrintfW(Name, RTL_NUMBER_OF(Name), L"\\\\.\\PhysicalDrive%lu", Disk);
    return CreateFileW(Name, Access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static
VOID
ReadIdentity(
    _In_ DWORD Disk,
    _Out_ PDISK_IDENTITY Identity)
{
    union
    {
        DRIVE_LAYOUT_INFORMATION_EX Layout;
        BYTE Buffer[sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX)];
    } Data;
    GET_DISK_ATTRIBUTES Attributes;
    DWORD Bytes;
    HANDLE Handle;

    ZeroMemory(Identity, sizeof(*Identity));
    ZeroMemory(&Attributes, sizeof(Attributes));
    Handle = OpenDisk(Disk, GENERIC_READ);
    if (Handle == INVALID_HANDLE_VALUE)
        return;

    if (DeviceIoControl(Handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, &Data, sizeof(Data), &Bytes, NULL))
    {
        Identity->Present = TRUE;
        Identity->Style = Data.Layout.PartitionStyle;
        if (Identity->Style == PARTITION_STYLE_MBR)
            Identity->Signature = Data.Layout.Mbr.Signature;
        else if (Identity->Style == PARTITION_STYLE_GPT)
            Identity->DiskId = Data.Layout.Gpt.DiskId;
    }

    ok(DeviceIoControl(Handle, IOCTL_DISK_GET_DISK_ATTRIBUTES, NULL, 0, &Attributes, sizeof(Attributes), &Bytes, NULL),
       "Disk %lu: attributes not returned, error %lu\n", Disk, GetLastError());
    Identity->Offline = (Attributes.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0;
    CloseHandle(Handle);
}

static
BOOL
SameIdentity(
    _In_ PDISK_IDENTITY First,
    _In_ PDISK_IDENTITY Second)
{
    static const GUID Zero = { 0 };

    if (!First->Present || !Second->Present || First->Style != Second->Style)
        return FALSE;
    if (First->Style == PARTITION_STYLE_MBR)
        return First->Signature != 0 && First->Signature == Second->Signature;
    if (First->Style == PARTITION_STYLE_GPT)
        return !IsEqualGUID(&First->DiskId, &Zero) && IsEqualGUID(&First->DiskId, &Second->DiskId);
    return FALSE;
}

static
VOID
TestTwinComesOnline(
    _In_ DWORD Disk,
    _In_ PDISK_IDENTITY Online)
{
    SET_DISK_ATTRIBUTES Set;
    DISK_IDENTITY After;
    DWORD Bytes;
    HANDLE Handle;
    ULONG Try;

    ok(!DiskHasVolume(Disk), "Disk %lu: a volume of the offline twin is present\n", Disk);

    Handle = OpenDisk(Disk, GENERIC_READ | GENERIC_WRITE);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        skip("Disk %lu cannot be opened for writing, error %lu\n", Disk, GetLastError());
        return;
    }

    ZeroMemory(&Set, sizeof(Set));
    Set.Version = sizeof(Set);
    Set.AttributesMask = DISK_ATTRIBUTE_OFFLINE;
    ok(DeviceIoControl(Handle, IOCTL_DISK_SET_DISK_ATTRIBUTES, &Set, sizeof(Set), NULL, 0, &Bytes, NULL),
       "Disk %lu: bringing the twin online failed, error %lu\n", Disk, GetLastError());
    CloseHandle(Handle);

    ReadIdentity(Disk, &After);
    ok(!After.Offline, "Disk %lu: the twin is still offline\n", Disk);
    ok(After.Present && After.Style == Online->Style, "Disk %lu: partition style %u\n", Disk, After.Style);
    ok(!SameIdentity(&After, Online), "Disk %lu: the twin kept the signature of the online disk\n", Disk);
    ok(After.Style != PARTITION_STYLE_MBR || After.Signature != 0, "Disk %lu: the new signature is 0\n", Disk);

    for (Try = 0; Try < 50 && !DiskHasVolume(Disk); Try++)
        Sleep(100);
    ok(DiskHasVolume(Disk), "Disk %lu: no volume of the twin appeared\n", Disk);
}

START_TEST(DiskSignatureCollision)
{
    DISK_IDENTITY Disks[MAX_DISKS];
    WCHAR Windows[MAX_PATH], Volume[] = L"\\\\.\\X:";
    DWORD Disk, Twin, Online, OnlineDisk, Count, Groups = 0, SystemDisk, PartitionDisk;
    BOOL Seen[MAX_DISKS];

    for (Disk = 0; Disk < MAX_DISKS; Disk++)
        ReadIdentity(Disk, &Disks[Disk]);
    ZeroMemory(Seen, sizeof(Seen));

    GetSystemWindowsDirectoryW(Windows, RTL_NUMBER_OF(Windows));
    Volume[4] = Windows[0];
    SystemDisk = VolumeDisk(Volume);
    PartitionDisk = SystemPartitionDisk();
    ok(SystemDisk < MAX_DISKS, "The system volume is on no disk\n");

    for (Disk = 0; Disk < MAX_DISKS; Disk++)
    {
        if (Seen[Disk] || !Disks[Disk].Present)
            continue;

        Count = Online = 0;
        OnlineDisk = Disk;
        for (Twin = Disk; Twin < MAX_DISKS; Twin++)
        {
            if (Twin != Disk && !SameIdentity(&Disks[Disk], &Disks[Twin]))
                continue;
            Seen[Twin] = TRUE;
            Count++;
            if (!Disks[Twin].Offline)
            {
                Online++;
                OnlineDisk = Twin;
            }
        }
        if (Count < 2)
            continue;

        Groups++;
        trace("Disk %lu shares its signature with %lu other disk(s)\n", Disk, Count - 1);
        ok(Online == 1, "Disk %lu: %lu of %lu disks with the same signature are online\n", Disk, Online, Count);

        for (Twin = Disk; Twin < MAX_DISKS; Twin++)
        {
            if (Twin != Disk && !SameIdentity(&Disks[Disk], &Disks[Twin]))
                continue;
            if (Twin == SystemDisk)
                ok(!Disks[Twin].Offline, "Disk %lu holds the system volume and is offline\n", Twin);
            if (Twin == PartitionDisk)
                ok(!Disks[Twin].Offline, "Disk %lu holds the system partition and is offline\n", Twin);
            if (Disks[Twin].Offline && Online == 1)
                TestTwinComesOnline(Twin, &Disks[OnlineDisk]);
        }
    }

    if (Groups == 0)
        skip("No two disks share a signature\n");
}
