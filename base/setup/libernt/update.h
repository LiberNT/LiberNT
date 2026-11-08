/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Staging of a downloaded LiberNT update
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define UPDATE_KEY              L"SOFTWARE\\LiberNT\\Update"
#define UPDATE_STAGE_DIRECTORY  L"$LiberNT.~BT"
#define UPDATE_STAGE_IMAGE      L"LiberNT.iso"
#define UPDATE_STAGE_LOADER     L"winload.efi"
#define UPDATE_STAGE_REQUEST    L"upgrade.ini"
#define UPDATE_STAGE_SECTION    L"Upgrade"
#define UPDATE_IMAGE_LOADER     L"Windows\\system32\\winload.efi"
#define UPDATE_ID_CHARS         39
#define BCD_DEVICE_ELEMENT_MAX  1024

typedef BOOL (CALLBACK *PUPDATE_PROGRESS)(PVOID Context, ULONGLONG Done, ULONGLONG Total);

DWORD
OpenBcdStore(
    _Out_ PHKEY Store);

DWORD
AddBcdUpgradeEntry(
    _In_ HKEY Store,
    _In_ PCWSTR Description,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR ImagePath,
    _In_ PCWSTR ImageHash,
    _In_opt_ PCWSTR SystemVolume,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault);

DWORD
RemoveBcdUpgradeEntries(
    _In_ HKEY Store,
    _In_ PCWSTR LoaderPath,
    _In_opt_ PCWSTR PreviousDefault);

DWORD
OpenBcdStoreFile(
    _In_ PCWSTR Path,
    _Out_ PHKEY Store);

DWORD
BuildBcdPartitionDevice(
    _In_ const GUID* PartitionId,
    _In_ const GUID* DiskId,
    _Out_writes_bytes_(*Size) PBYTE Element,
    _Inout_ PDWORD Size);

DWORD
BuildBcdRamdiskDevice(
    _In_ const GUID* PartitionId,
    _In_ const GUID* DiskId,
    _In_ PCWSTR ImagePath,
    _Out_writes_bytes_(*Size) PBYTE Element,
    _Inout_ PDWORD Size);

DWORD
WriteSetupBootEntry(
    _In_ HKEY Store,
    _In_ PCWSTR ManagerPath,
    _In_ PCWSTR LoaderPath,
    _In_ PCWSTR Description,
    _In_reads_bytes_(SystemSize) const BYTE* SystemDevice,
    _In_ DWORD SystemSize,
    _In_reads_bytes_(OsSize) const BYTE* OsDevice,
    _In_ DWORD OsSize);

BOOL
IsLiveSystem(VOID);

ULONG
ReadInventoryBuild(
    _In_ PCWSTR Path);

DWORD
ExtractZipImage(
    _In_ PCWSTR ZipPath,
    _In_ PCWSTR OutPath,
    _In_opt_ PUPDATE_PROGRESS Progress,
    _In_opt_ PVOID Context);

DWORD
ExtractImageFile(
    _In_ PCWSTR ImagePath,
    _In_ PCWSTR FileName,
    _In_ PCWSTR OutPath);
