/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the disk attribute requests and the disks an OFFLINEALL start keeps offline
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>

#define BOTH_ATTRIBUTES (DISK_ATTRIBUTE_OFFLINE | DISK_ATTRIBUTE_READ_ONLY)
#define MAX_DISKS 64

static
BOOL
StartedOfflineAll(VOID)
{
    WCHAR Options[1024];
    DWORD Size = sizeof(Options) - sizeof(WCHAR);
    PCWSTR Text;
    HKEY Key;
    LONG Error;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;
    ZeroMemory(Options, sizeof(Options));
    Error = RegQueryValueExW(Key, L"SystemStartOptions", NULL, NULL, (PBYTE)Options, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return FALSE;

    for (Text = Options; *Text; Text++)
    {
        if ((Text == Options || Text[-1] == L' ') &&
            _wcsnicmp(Text, L"SANPOLICY=OFFLINEALL", 20) == 0 &&
            (Text[20] == UNICODE_NULL || Text[20] == L' '))
        {
            return TRUE;
        }
    }
    return FALSE;
}

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
DWORD
DriveDisk(
    _In_ PCWSTR Path)
{
    WCHAR Volume[] = L"\\\\.\\X:";

    Volume[4] = Path[0];
    return VolumeDisk(Volume);
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
BOOL
WaitForVolume(
    _In_ DWORD Disk,
    _In_ BOOL Present)
{
    ULONG Try;

    for (Try = 0; Try < 100; Try++)
    {
        if (DiskHasVolume(Disk) == Present)
            return TRUE;
        Sleep(50);
    }
    return FALSE;
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
BOOL
GetAttributes(
    _In_ HANDLE Handle,
    _Out_ PGET_DISK_ATTRIBUTES Attributes)
{
    DWORD Bytes = 0;
    BOOL Success;

    FillMemory(Attributes, sizeof(*Attributes), 0xCC);
    Success = DeviceIoControl(Handle, IOCTL_DISK_GET_DISK_ATTRIBUTES, NULL, 0, Attributes, sizeof(*Attributes), &Bytes, NULL);
    ok(!Success || Bytes == sizeof(*Attributes), "Returned %lu bytes\n", Bytes);
    return Success;
}

static
BOOL
SetAttributes(
    _In_ HANDLE Handle,
    _In_ ULONG Version,
    _In_ ULONGLONG Attributes,
    _In_ ULONGLONG Mask)
{
    SET_DISK_ATTRIBUTES Set;
    DWORD Bytes;

    ZeroMemory(&Set, sizeof(Set));
    Set.Version = Version;
    Set.Persist = FALSE;
    Set.Attributes = Attributes;
    Set.AttributesMask = Mask;
    return DeviceIoControl(Handle, IOCTL_DISK_SET_DISK_ATTRIBUTES, &Set, sizeof(Set), NULL, 0, &Bytes, NULL);
}

static
VOID
TestEveryDisk(
    _In_ DWORD SystemDisk,
    _In_ BOOL OfflineAll)
{
    GET_DISK_ATTRIBUTES Attributes;
    DWORD Disk, Bytes, Seen = 0;
    HANDLE Handle;
    BOOL Isolated;

    for (Disk = 0; Disk < MAX_DISKS; Disk++)
    {
        Handle = OpenDisk(Disk, 0);
        if (Handle == INVALID_HANDLE_VALUE)
            continue;
        Seen++;

        ok(GetAttributes(Handle, &Attributes), "Disk %lu: attributes not returned, error %lu\n", Disk, GetLastError());
        ok(Attributes.Version == sizeof(Attributes), "Disk %lu: version %lu\n", Disk, Attributes.Version);

        Isolated = OfflineAll && Disk != SystemDisk;
        ok((Attributes.Attributes & BOTH_ATTRIBUTES) == (Isolated ? BOTH_ATTRIBUTES : 0),
           "Disk %lu: attributes 0x%I64x\n", Disk, Attributes.Attributes);
        if (Isolated)
        {
            ok(!DiskHasVolume(Disk), "Disk %lu: a volume of an offline disk is present\n", Disk);
            SetLastError(0xdeadbeef);
            ok(!DeviceIoControl(Handle, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &Bytes, NULL) &&
               GetLastError() == ERROR_WRITE_PROTECT,
               "Disk %lu: writable check error %lu\n", Disk, GetLastError());
        }

        SetLastError(0xdeadbeef);
        ok(!DeviceIoControl(Handle, IOCTL_DISK_GET_DISK_ATTRIBUTES, NULL, 0, &Attributes, sizeof(ULONG), &Bytes, NULL),
           "Disk %lu: a short buffer was accepted\n", Disk);
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Disk %lu: short buffer error %lu\n", Disk, GetLastError());

        CloseHandle(Handle);
    }

    ok(Seen != 0, "No disk could be opened\n");
}

static
VOID
TestSetWithoutChange(
    _In_ DWORD SystemDisk)
{
    GET_DISK_ATTRIBUTES Before, After;
    HANDLE Handle;

    Handle = OpenDisk(SystemDisk, GENERIC_READ | GENERIC_WRITE);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        skip("Disk %lu cannot be opened for writing, error %lu\n", SystemDisk, GetLastError());
        return;
    }

    ok(GetAttributes(Handle, &Before), "Attributes not returned, error %lu\n", GetLastError());
    ok(SetAttributes(Handle, sizeof(SET_DISK_ATTRIBUTES), 0, 0), "An empty change failed, error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!SetAttributes(Handle, 0, 0, 0), "Version 0 was accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Version 0 error %lu\n", GetLastError());

    ok(GetAttributes(Handle, &After), "Attributes not returned, error %lu\n", GetLastError());
    ok(After.Attributes == Before.Attributes, "Attributes 0x%I64x became 0x%I64x\n", Before.Attributes, After.Attributes);
    CloseHandle(Handle);
}

static
VOID
TestOfflineAndBack(
    _In_ DWORD SystemDisk,
    _In_ DWORD TestDisk)
{
    GET_DISK_ATTRIBUTES Attributes;
    DWORD Disk, Bytes;
    HANDLE Handle = INVALID_HANDLE_VALUE;

    for (Disk = 0; Disk < MAX_DISKS; Disk++)
    {
        if (Disk == SystemDisk || Disk == TestDisk || !DiskHasVolume(Disk))
            continue;
        Handle = OpenDisk(Disk, GENERIC_READ | GENERIC_WRITE);
        if (Handle != INVALID_HANDLE_VALUE)
            break;
    }
    if (Handle == INVALID_HANDLE_VALUE)
    {
        skip("No spare disk with a volume to take offline\n");
        return;
    }

    ok(SetAttributes(Handle, sizeof(SET_DISK_ATTRIBUTES), BOTH_ATTRIBUTES, BOTH_ATTRIBUTES),
       "Disk %lu: going offline failed, error %lu\n", Disk, GetLastError());
    ok(GetAttributes(Handle, &Attributes), "Disk %lu: attributes not returned, error %lu\n", Disk, GetLastError());
    ok((Attributes.Attributes & BOTH_ATTRIBUTES) == BOTH_ATTRIBUTES, "Disk %lu: attributes 0x%I64x\n", Disk, Attributes.Attributes);
    SetLastError(0xdeadbeef);
    ok(!DeviceIoControl(Handle, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &Bytes, NULL) && GetLastError() == ERROR_WRITE_PROTECT,
       "Disk %lu: writable check error %lu\n", Disk, GetLastError());
    ok(WaitForVolume(Disk, FALSE), "Disk %lu: its volume stayed\n", Disk);

    ok(SetAttributes(Handle, sizeof(SET_DISK_ATTRIBUTES), 0, BOTH_ATTRIBUTES),
       "Disk %lu: going online failed, error %lu\n", Disk, GetLastError());
    ok(GetAttributes(Handle, &Attributes), "Disk %lu: attributes not returned, error %lu\n", Disk, GetLastError());
    ok((Attributes.Attributes & BOTH_ATTRIBUTES) == 0, "Disk %lu: attributes 0x%I64x\n", Disk, Attributes.Attributes);
    ok(DeviceIoControl(Handle, IOCTL_DISK_IS_WRITABLE, NULL, 0, NULL, 0, &Bytes, NULL),
       "Disk %lu: not writable, error %lu\n", Disk, GetLastError());
    ok(WaitForVolume(Disk, TRUE), "Disk %lu: its volume did not come back\n", Disk);

    CloseHandle(Handle);
}

START_TEST(DiskAttributes)
{
    WCHAR Path[MAX_PATH];
    DWORD SystemDisk, TestDisk;
    BOOL OfflineAll = StartedOfflineAll();

    ok(GetSystemDirectoryW(Path, RTL_NUMBER_OF(Path)) != 0, "No system directory, error %lu\n", GetLastError());
    SystemDisk = DriveDisk(Path);
    ok(GetModuleFileNameW(NULL, Path, RTL_NUMBER_OF(Path)) != 0, "No module name, error %lu\n", GetLastError());
    TestDisk = DriveDisk(Path);
    trace("System disk %lu, test disk %lu, offline-all start %d\n", SystemDisk, TestDisk, OfflineAll);

    TestEveryDisk(SystemDisk, OfflineAll);
    TestSetWithoutChange(SystemDisk);
    if (!OfflineAll)
        TestOfflineAndBack(SystemDisk, TestDisk);
}
