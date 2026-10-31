/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER after a drive letter is removed
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static BOOL
NextDriveLetter(
    _In_ HANDLE MountMgr,
    _In_ PCWSTR DeviceName,
    _Out_ PMOUNTMGR_DRIVE_LETTER_INFORMATION Info)
{
    BYTE Buffer[sizeof(MOUNTMGR_DRIVE_LETTER_TARGET) + MAX_PATH * sizeof(WCHAR)];
    PMOUNTMGR_DRIVE_LETTER_TARGET Target = (PMOUNTMGR_DRIVE_LETTER_TARGET)Buffer;
    DWORD Returned;

    ZeroMemory(Buffer, sizeof(Buffer));
    ZeroMemory(Info, sizeof(*Info));
    Target->DeviceNameLength = (USHORT)(wcslen(DeviceName) * sizeof(WCHAR));
    CopyMemory(Target->DeviceName, DeviceName, Target->DeviceNameLength);
    return DeviceIoControl(MountMgr, IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER, Target,
                           FIELD_OFFSET(MOUNTMGR_DRIVE_LETTER_TARGET, DeviceName) + Target->DeviceNameLength,
                           Info, sizeof(*Info), &Returned, NULL);
}

START_TEST(NextDriveLetter)
{
    WCHAR Letter[8] = { 0 }, Drive[4], Root[4], Volume[MAX_PATH], DeviceName[MAX_PATH], System[MAX_PATH];
    MOUNTMGR_DRIVE_LETTER_INFORMATION Info;
    HANDLE MountMgr;
    DWORD Length;

    Length = GetEnvironmentVariableW(L"MOUNTMGR_TEST_VOLUME", Letter, ARRAYSIZE(Letter));
    if (Length != 1 || !iswalpha(Letter[0]))
    {
        skip("Set MOUNTMGR_TEST_VOLUME to the letter of a volume whose drive letter the test may remove\n");
        return;
    }
    StringCchPrintfW(Drive, ARRAYSIZE(Drive), L"%c:", towupper(Letter[0]));
    StringCchPrintfW(Root, ARRAYSIZE(Root), L"%c:\\", towupper(Letter[0]));
    GetSystemDirectoryW(System, ARRAYSIZE(System));
    if (towupper(System[0]) == Drive[0])
    {
        skip("%S is the system volume\n", Drive);
        return;
    }
    if (!GetVolumeNameForVolumeMountPointW(Root, Volume, ARRAYSIZE(Volume)) ||
        !QueryDosDeviceW(Drive, DeviceName, ARRAYSIZE(DeviceName)))
    {
        skip("%S is not a mounted volume: %lu\n", Drive, GetLastError());
        return;
    }
    MountMgr = GetMountMgrHandle(FILE_READ_DATA | FILE_WRITE_DATA);
    if (!MountMgr)
    {
        skip("The mount manager cannot be opened\n");
        return;
    }

    ok(NextDriveLetter(MountMgr, DeviceName, &Info), "NEXT_DRIVE_LETTER failed with %lu\n", GetLastError());
    ok(!Info.DriveLetterWasAssigned, "a letter was assigned to a volume that has one\n");
    ok(towupper(Info.CurrentDriveLetter) == Drive[0], "CurrentDriveLetter is '%c'\n", Info.CurrentDriveLetter);

    ok(DeleteVolumeMountPointW(Root), "DeleteVolumeMountPoint failed with %lu\n", GetLastError());
    ok(NextDriveLetter(MountMgr, DeviceName, &Info), "NEXT_DRIVE_LETTER failed with %lu\n", GetLastError());
    ok(!Info.DriveLetterWasAssigned, "a letter was assigned after the letter was removed\n");
    ok(Info.CurrentDriveLetter == 0, "CurrentDriveLetter is '%c'\n", Info.CurrentDriveLetter);
    ok(GetDriveTypeW(Root) == DRIVE_NO_ROOT_DIR, "%S still exists\n", Root);

    if (!SetVolumeMountPointW(Root, Volume))
    {
        ok(0, "SetVolumeMountPoint failed with %lu; restore it with: mountvol %S %S\n", GetLastError(), Drive,
           Volume);
        CloseHandle(MountMgr);
        return;
    }
    ok(NextDriveLetter(MountMgr, DeviceName, &Info), "NEXT_DRIVE_LETTER failed with %lu\n", GetLastError());
    ok(!Info.DriveLetterWasAssigned, "a letter was assigned to a volume that has one\n");
    ok(towupper(Info.CurrentDriveLetter) == Drive[0], "CurrentDriveLetter is '%c'\n", Info.CurrentDriveLetter);

    CloseHandle(MountMgr);
}
