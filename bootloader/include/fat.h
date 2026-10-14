/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FAT32 and master boot record on-disk structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define FAT_ATTR_READONLY   0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUMENAME 0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  (FAT_ATTR_READONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUMENAME)

#pragma pack(push, 1)

typedef struct _FAT32_BOOTSECTOR
{
    UCHAR JumpBoot[3];
    CHAR OemName[8];
    USHORT BytesPerSector;
    UCHAR SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR NumberOfFats;
    USHORT RootDirEntries;
    USHORT TotalSectors;
    UCHAR MediaDescriptor;
    USHORT SectorsPerFat;
    USHORT SectorsPerTrack;
    USHORT NumberOfHeads;
    ULONG HiddenSectors;
    ULONG TotalSectorsBig;
    ULONG SectorsPerFatBig;
    USHORT ExtendedFlags;
    USHORT FileSystemVersion;
    ULONG RootDirStartCluster;
    USHORT FsInfo;
    USHORT BackupBootSector;
    UCHAR Reserved[12];
    UCHAR DriveNumber;
    UCHAR Reserved1;
    UCHAR BootSignature;
    ULONG VolumeSerialNumber;
    CHAR VolumeLabel[11];
    CHAR FileSystemType[8];
    UCHAR BootCodeAndData[420];
    USHORT BootSectorMagic;
} FAT32_BOOTSECTOR, *PFAT32_BOOTSECTOR;

typedef struct _DIRENTRY
{
    CHAR FileName[11];
    UCHAR Attr;
    UCHAR ReservedNT;
    UCHAR TimeInTenths;
    USHORT CreateTime;
    USHORT CreateDate;
    USHORT LastAccessDate;
    USHORT ClusterHigh;
    USHORT Time;
    USHORT Date;
    USHORT ClusterLow;
    ULONG Size;
} DIRENTRY, *PDIRENTRY;

typedef struct _LFN_DIRENTRY
{
    UCHAR SequenceNumber;
    WCHAR Name0_4[5];
    UCHAR EntryAttributes;
    UCHAR Reserved;
    UCHAR AliasChecksum;
    WCHAR Name5_10[6];
    USHORT StartCluster;
    WCHAR Name11_12[2];
} LFN_DIRENTRY, *PLFN_DIRENTRY;

typedef struct _PARTITION_TABLE_ENTRY
{
    UCHAR BootIndicator;
    UCHAR StartHead;
    UCHAR StartSector;
    UCHAR StartCylinder;
    UCHAR SystemIndicator;
    UCHAR EndHead;
    UCHAR EndSector;
    UCHAR EndCylinder;
    ULONG SectorCountBeforePartition;
    ULONG PartitionSectorCount;
} PARTITION_TABLE_ENTRY, *PPARTITION_TABLE_ENTRY;

typedef struct _MASTER_BOOT_RECORD
{
    UCHAR MasterBootRecordCodeAndData[0x1B8];
    ULONG Signature;
    USHORT Reserved;
    PARTITION_TABLE_ENTRY PartitionTable[4];
    USHORT MasterBootRecordMagic;
} MASTER_BOOT_RECORD, *PMASTER_BOOT_RECORD;

#pragma pack(pop)

C_ASSERT(sizeof(FAT32_BOOTSECTOR) == 512);
C_ASSERT(sizeof(DIRENTRY) == 32);
C_ASSERT(sizeof(LFN_DIRENTRY) == 32);
C_ASSERT(sizeof(MASTER_BOOT_RECORD) == 512);
