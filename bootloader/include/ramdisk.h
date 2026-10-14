/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RAM disk volume builder
 * COPYRIGHT:   Copyright 2025-2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <fat.h>

typedef struct _RAMDISK_FAT32_LAYOUT
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG ReservedSectors;
    ULONG NumberOfFats;
    ULONG FatSizeSectors;
    ULONG FirstDataSector;
    ULONG RootDirFirstCluster;
    ULONG TotalSectors;
    ULONG HiddenSectors;
    ULONGLONG VolumeSizeBytes;
} RAMDISK_FAT32_LAYOUT, *PRAMDISK_FAT32_LAYOUT;

typedef struct _FAT32_WRITER
{
    PUCHAR VolumeBase;
    ULONGLONG VolumeSize;
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG BytesPerCluster;
    ULONG ReservedSectors;
    ULONG NumberOfFats;
    ULONG FatSizeSectors;
    ULONG FirstDataSector;
    ULONG RootDirCluster;
    ULONG TotalClusters;
    PULONG Fat0;
    PULONG Fat1;
    ULONG NextFreeCluster;
    USHORT FatDate;
    USHORT FatTime;
} FAT32_WRITER, *PFAT32_WRITER;

FORCEINLINE
ULONG
RamDiskDeriveDiskSignature(
    _In_ PVOID BaseAddress,
    _In_ ULONGLONG DiskSize)
{
    ULONGLONG Address = (ULONG_PTR)BaseAddress;
    ULONG Signature = 0x52444B58;

    Signature ^= (ULONG)Address;
    Signature ^= (ULONG)(Address >> 32);
    Signature ^= (ULONG)DiskSize;
    Signature ^= (ULONG)(DiskSize >> 32);

    if (Signature == 0 || Signature == 0xFFFFFFFF)
        Signature ^= 0xA5A5A5A5;

    return Signature;
}

BOOLEAN
RamDiskFormatFat32(
    _In_ PVOID BaseAddress,
    _In_ ULONGLONG DiskSize,
    _Out_opt_ PRAMDISK_FAT32_LAYOUT Layout);

BOOLEAN
Fat32WriterInit(
    _Out_ PFAT32_WRITER Writer,
    _In_ PVOID VolumeBase,
    _In_ ULONGLONG VolumeSize,
    _In_ PRAMDISK_FAT32_LAYOUT Layout);

BOOLEAN
Fat32CreateDirectory(
    _Inout_ PFAT32_WRITER Writer,
    _In_ PCSTR Path);

BOOLEAN
Fat32CreateFileEx(
    _Inout_ PFAT32_WRITER Writer,
    _In_ PCSTR Path,
    _In_ ULONG FileSize,
    _Out_ PUCHAR *DataPointer);

PUCHAR
Fat32FindFile(
    _In_ PFAT32_WRITER Writer,
    _In_ PCSTR Path,
    _Out_ PULONG FileSize);
