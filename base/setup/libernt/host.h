/*
 * PROJECT:     LiberNT Setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Setup started on Windows: install alongside it or upgrade LiberNT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "update.h"

#define HOST_STAGE_DIRECTORY    L"$LiberNT.~BT"
#define HOST_STAGE_IMAGE        L"LiberNT.iso"
#define HOST_SETUP_FILE         L"setup.ini"
#define HOST_SETUP_SECTION      L"Setup"
#define HOST_SETUP_STORE_DIR    L"\\EFI\\LiberNT\\Setup"
#define HOST_SETUP_MANAGER      HOST_SETUP_STORE_DIR L"\\bootmgfw.efi"
#define HOST_SETUP_LOADER       HOST_SETUP_STORE_DIR L"\\winload.efi"
#define HOST_SETUP_STORE        HOST_SETUP_STORE_DIR L"\\BCD"
#define HOST_SETUP_DESCRIPTION  L"LiberNT Setup"
#define HOST_LIBERNT_MANAGER    L"\\EFI\\LiberNT\\Boot\\bootmgfw.efi"
#define HOST_LIBERNT_STORE      L"\\EFI\\LiberNT\\Boot\\BCD"
#define HOST_LIBERNT_DESCRIPTION L"LiberNT Boot Manager"
#define HOST_IMAGE_PATH         L"\\" HOST_STAGE_DIRECTORY L"\\" HOST_STAGE_IMAGE
#define HOST_SHA256_CHARS       65
#define HOST_VERSION_CHARS      128
#define HOST_LOG_DIRECTORY      L"\\Logs\\LiberNT"
#define HOST_LOG_FILE           L"\\setup.log"

typedef struct _HOST_PARTITION
{
    WCHAR DeviceName[MAX_PATH];
    ULONG DiskNumber;
    ULONG PartitionNumber;
    ULONGLONG Offset;
    ULONGLONG Length;
    PARTITION_STYLE Style;
    GUID PartitionId;
    GUID PartitionType;
    GUID DiskId;
    ULONG SectorSize;
} HOST_PARTITION, *PHOST_PARTITION;

typedef BOOL (CALLBACK *PHOST_PROGRESS)(PVOID Context, ULONGLONG Done, ULONGLONG Total);

VOID
HostLog(
    _In_ PCWSTR Format,
    ...);

BOOL
HostIsWindows(VOID);

BOOL
HostPath(
    _In_ PCWSTR DeviceName,
    _In_opt_ PCWSTR Relative,
    _Out_writes_(MAX_PATH) PWSTR Path);

DWORD
HostQueryPartition(
    _In_ PCWSTR DeviceName,
    _Out_ PHOST_PARTITION Partition);

DWORD
HostGetWindowsPartition(
    _Out_ PHOST_PARTITION Partition);

DWORD
HostGetSystemPartition(
    _Out_ PHOST_PARTITION Partition);

ULONGLONG
HostGetFreeBytesAfter(
    _In_ PHOST_PARTITION Partition);

DWORD
HostFindPreparedPartition(
    _Out_ PHOST_PARTITION Partition);

ULONG
HostFindLiberNT(
    _In_ WORD Machine,
    _Out_writes_(MaxCount) PHOST_PARTITION Partitions,
    _Out_writes_(MaxCount) WCHAR (*Versions)[HOST_VERSION_CHARS],
    _In_ ULONG MaxCount);

DWORD
HostReadImageSize(
    _In_ PCWSTR SourceRoot,
    _Out_ PULONGLONG Bytes);

DWORD
HostGetSupportedSize(
    _In_ PHOST_PARTITION Partition,
    _Out_ PULONGLONG MinBytes,
    _Out_ PULONGLONG MaxBytes);

DWORD
HostResizePartition(
    _In_ PHOST_PARTITION Partition,
    _In_ ULONGLONG Bytes);

DWORD
HostCreatePartition(
    _In_ PHOST_PARTITION After,
    _In_ ULONGLONG Bytes,
    _Out_ PHOST_PARTITION Created);

DWORD
HostFormatPartition(
    _In_ PHOST_PARTITION Partition,
    _In_ PCWSTR Label);

DWORD
HostDeletePartition(
    _In_ PHOST_PARTITION Partition);

DWORD
HostCopyImage(
    _In_ PCWSTR SourceRoot,
    _In_ ULONGLONG Bytes,
    _In_ PCWSTR Target,
    _In_opt_ PHOST_PROGRESS Progress,
    _In_opt_ PVOID Context,
    _Out_writes_(HOST_SHA256_CHARS) PWSTR Hash);

DWORD
HostHashFile(
    _In_ PCWSTR Path,
    _In_opt_ PHOST_PROGRESS Progress,
    _In_opt_ PVOID Context,
    _Out_writes_(HOST_SHA256_CHARS) PWSTR Hash);

DWORD
HostCopyFileVerified(
    _In_ PCWSTR Source,
    _In_ PCWSTR Target);

DWORD
HostCreateStageDirectory(
    _In_ PHOST_PARTITION Target);

DWORD
HostInstallSetupBoot(
    _In_ PCWSTR SourceRoot,
    _In_ WORD Machine,
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PHOST_PARTITION Target);

DWORD
HostSetBootNext(
    _In_ PHOST_PARTITION SystemPartition,
    _In_ PCWSTR FilePath,
    _In_ PCWSTR Description);

DWORD
HostRemoveSetupBoot(
    _In_ PHOST_PARTITION SystemPartition);

PCWSTR
HostGetRemovableBootName(
    _In_ WORD Machine);

BOOL
HostSetFirmwareSetupRequest(VOID);

BOOL
HostIsFirmwareSetupSupported(VOID);

BOOL
HostIsSecureBootOn(VOID);

DWORD
HostReadUpgradeStage(
    _In_ PHOST_PARTITION Target,
    _Out_writes_(Chars) PWSTR Stage,
    _In_ DWORD Chars);

DWORD
HostFindLiberNTStore(
    _In_ PHOST_PARTITION Target,
    _In_ PHOST_PARTITION WindowsSystemPartition,
    _Out_ PHOST_PARTITION StorePartition);

DWORD
HostClearUpgradeStage(
    _In_ PHOST_PARTITION StorePartition,
    _In_ PHOST_PARTITION Target);

DWORD
HostWriteUpgradeRequest(
    _In_ PHOST_PARTITION Target,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value);

DWORD
HostAddUpgradeEntry(
    _In_ PHOST_PARTITION StorePartition,
    _In_ PHOST_PARTITION Target,
    _In_ PCWSTR ImageHash,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR EntryId,
    _Out_writes_(UPDATE_ID_CHARS) PWSTR PreviousDefault);

DWORD
HostSetCompletionCommand(
    _In_ PHOST_PARTITION Target);

INT
RunHostSetup(
    _Inout_ PSETUPDATA pSetupData,
    _In_ PCWSTR CommandLine);
