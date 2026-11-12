/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the partition-table files say to one another
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntdddisk.h>

#pragma pack(push, 1)

#define EFI_PARTITION_TABLE_SIGNATURE 0x5452415020494645ULL
#define EFI_PARTITION_TABLE_REVISION  0x00010000
#define EFI_PARTITION_HEADER_LBA      1
#define EFI_PARTITION_ENTRY_SIZE      128
#define EFI_PARTITION_ENTRY_NAME_LENGTH 36

/* The least room the specification reserves for the entry array, in bytes */
#define EFI_PARTITION_ENTRY_ARRAY_MINIMUM 16384

typedef struct _EFI_PARTITION_HEADER
{
    ULONGLONG Signature;
    ULONG Revision;
    ULONG HeaderSize;
    ULONG HeaderCRC32;
    ULONG Reserved;
    ULONGLONG MyLBA;
    ULONGLONG AlternateLBA;
    ULONGLONG FirstUsableLBA;
    ULONGLONG LastUsableLBA;
    GUID DiskGUID;
    ULONGLONG PartitionEntryLBA;
    ULONG NumberOfEntries;
    ULONG SizeOfPartitionEntry;
    ULONG PartitionEntryCRC32;
} EFI_PARTITION_HEADER, *PEFI_PARTITION_HEADER;

typedef struct _EFI_PARTITION_ENTRY
{
    GUID PartitionType;
    GUID UniquePartition;
    ULONGLONG StartingLBA;
    ULONGLONG EndingLBA;
    ULONGLONG Attributes;
    WCHAR Name[EFI_PARTITION_ENTRY_NAME_LENGTH];
} EFI_PARTITION_ENTRY, *PEFI_PARTITION_ENTRY;

#pragma pack(pop)

/* What the loader said, for as long as the I/O manager is still holding it */
extern PLOADER_PARAMETER_BLOCK IopLoaderBlock;

typedef struct _FSTUB_DISK
{
    PDEVICE_OBJECT DeviceObject;
    ULONG BytesPerSector;
    ULONG SectorsPerTrack;
    ULONG TracksPerCylinder;
    MEDIA_TYPE MediaType;
    LARGE_INTEGER Cylinders;
    LARGE_INTEGER Length;
    ULONGLONG SectorCount;
    BOOLEAN Writable;
    PUCHAR Sector;
} FSTUB_DISK, *PFSTUB_DISK;

/* The number an MBR entry's starting sector and length are held in */
#define FSTUB_MBR_ENTRY_COUNT 4

/* Where the boot record keeps what the partition code reads out of it */
#define FSTUB_DISK_SIGNATURE_OFFSET 0x1B8
#define FSTUB_PARTITION_TABLE_OFFSET 0x1BE
#define FSTUB_BOOT_SIGNATURE_OFFSET 0x1FE
#define FSTUB_BOOT_SIGNATURE 0xAA55

/* Reading and writing whole sectors, and what the disk says about itself */

NTSTATUS
NTAPI
FstubGetDiskGeometry(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PDISK_GEOMETRY_EX Geometry);

NTSTATUS
NTAPI
FstubReadSector(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG SectorCount,
    _In_ ULONGLONG Lba,
    _Out_writes_bytes_(SectorCount * Disk->BytesPerSector) PVOID Buffer);

NTSTATUS
NTAPI
FstubWriteSector(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG SectorCount,
    _In_ ULONGLONG Lba,
    _In_reads_bytes_(SectorCount * Disk->BytesPerSector) PVOID Buffer);

/* The disk as the partition code holds it while it works on one */

NTSTATUS
NTAPI
FstubAllocateDiskInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Outptr_ PFSTUB_DISK *Disk,
    _In_opt_ PDISK_GEOMETRY_EX Geometry);

VOID
NTAPI
FstubFreeDiskInformation(
    _In_ PFSTUB_DISK Disk);

NTSTATUS
NTAPI
FstubDetectPartitionStyle(
    _In_ PFSTUB_DISK Disk,
    _Out_ PARTITION_STYLE *PartitionStyle);

NTSTATUS
NTAPI
FstubAdjustPartitionCount(
    _In_ ULONG PartitionStyle,
    _In_ ULONG PartitionCount,
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX *Layout);

/* The master boot record */

NTSTATUS
NTAPI
FstubReadPartitionTableMBR(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReturnRecognizedPartitions,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *Layout);

NTSTATUS
NTAPI
FstubWritePartitionTableMBR(
    _In_ PFSTUB_DISK Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout);

NTSTATUS
NTAPI
FstubCreateDiskMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCREATE_DISK_MBR DiskInfo);

NTSTATUS
NTAPI
FstubCreateDiskRaw(
    _In_ PDEVICE_OBJECT DeviceObject);

/* Flattens the extended chain of an MBR disk into a plain layout */
PDRIVE_LAYOUT_INFORMATION
NTAPI
FstubConvertExtendedToLayout(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX LayoutEx);

/* The GUID partition table */

NTSTATUS
NTAPI
FstubReadHeaderEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReadBackupTable,
    _Out_ PEFI_PARTITION_HEADER Header);

NTSTATUS
NTAPI
FstubWriteHeaderEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG PartitionEntryArraySectors,
    _In_ LPCGUID DiskGuid,
    _In_ ULONG NumberOfEntries,
    _In_ ULONGLONG FirstUsableLBA,
    _In_ ULONGLONG LastUsableLBA,
    _In_ ULONG ArrayCRC32,
    _In_ BOOLEAN WriteBackupTable);

NTSTATUS
NTAPI
FstubReadPartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReadBackupTable,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *Layout);

NTSTATUS
NTAPI
FstubWritePartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout);

NTSTATUS
NTAPI
FstubVerifyPartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN FixErrors);

VOID
NTAPI
FstubCopyEntryEFI(
    _Out_ PPARTITION_INFORMATION_EX PartitionEntry,
    _In_ PEFI_PARTITION_ENTRY Entry,
    _In_ ULONG BytesPerSector);

VOID
NTAPI
FstubWriteEntryEFI(
    _Out_ PEFI_PARTITION_ENTRY Entry,
    _In_ PPARTITION_INFORMATION_EX PartitionEntry,
    _In_ ULONG BytesPerSector);

NTSTATUS
NTAPI
FstubWriteBootSectorEFI(
    _In_ PFSTUB_DISK Disk);

NTSTATUS
NTAPI
FstubCreateDiskEFI(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCREATE_DISK_GPT DiskInfo);

NTSTATUS
NTAPI
FstubSetPartitionInformationEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG PartitionNumber,
    _In_ SET_PARTITION_INFORMATION_GPT *PartitionInfo);

/* Turns an extended partition entry back into one the firmware understands */
VOID
NTAPI
FstubFixupEfiPartition(
    _In_ PPARTITION_DESCRIPTOR PartitionDescriptor,
    _In_ ULONGLONG MaxOffset);

/* Mapping for the debugger before the memory manager can be asked */
PVOID
NTAPI
xKdMapPhysicalMemory64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB);

/* The geometry the legacy partition routines make do with */
VOID
FASTCALL
xHalGetPartialGeometry(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PULONG ConfigurationInformationSectors,
    _Out_ PULONGLONG MaxOffset);

/* The interrupt translator that changes nothing */
NTSTATUS
NTAPI
FstubTranslateResource(
    _Inout_opt_ PVOID Context,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Source,
    _In_ RESOURCE_TRANSLATION_DIRECTION Direction,
    _In_opt_ ULONG AlternativesCount,
    _In_reads_opt_(AlternativesCount) IO_RESOURCE_DESCRIPTOR Alternatives[],
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Target);

NTSTATUS
NTAPI
FstubTranslateRequirement(
    _Inout_opt_ PVOID Context,
    _In_ PIO_RESOURCE_DESCRIPTOR Source,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG TargetCount,
    _Out_writes_(*TargetCount) PIO_RESOURCE_DESCRIPTOR *Target);

VOID
NTAPI
FstubTranslatorNull(
    _In_opt_ PVOID Context);

/* EOF */
