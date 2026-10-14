/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Block device access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _DK_DEVICE
{
    PVOID Firmware;
    PUCHAR Memory;
    ULONGLONG Offset;
    ULONGLONG Length;
    ULONG BlockSize;
    ULONG DiskIndex;
    ULONG PartitionNumber;
} DK_DEVICE, *PDK_DEVICE;

BOOLEAN
DkRead(
    _In_ PDK_DEVICE Device,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer);

BOOLEAN
DkOpenBcdDevice(
    _In_ PBCD_DEVICE BcdDevice,
    _Out_ PDK_DEVICE Device);

BOOLEAN
GptFindPartition(
    _In_ PDK_DEVICE Disk,
    _In_reads_bytes_opt_(16) const UCHAR* DiskId,
    _In_reads_bytes_(16) const UCHAR* PartitionId,
    _Out_ PDK_DEVICE Partition);

PVOID
FsReadFile(
    _In_ PDK_DEVICE Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size);

PVOID
FsReadFileA(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Path,
    _Out_ PULONG Size);

BOOLEAN
GptGetDiskId(
    _In_ PDK_DEVICE Disk,
    _Out_writes_bytes_(16) PUCHAR DiskId);

BOOLEAN
DkLocateBootPartition(
    _Out_ PDK_DEVICE Partition);

PCWSTR
FsGetDriverName(
    _In_ PDK_DEVICE Device);

extern PVOID (*FsMemoryReader)(PCSTR Path, PULONG Size);

BOOLEAN
ExFatMount(
    _In_ PDK_DEVICE Device);

BOOLEAN
ExFatIsExFat(VOID);

PVOID
ExFatReadFile(
    _In_ PCSTR Path,
    _Out_ PULONG Size);
