/*
 * PROJECT:     LiberNT StorPort VirtIO block miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver-private declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <storport.h>

#define NDEBUG
#include <debug.h>

#define VIRTIO_PCI_VENDOR_ID                    0x1AF4
#define VIRTIO_PCI_DEVICE_BLOCK_TRANSITIONAL    0x1001
#define VIRTIO_PCI_DEVICE_BLOCK                 0x1042

#define PCI_CAPABILITY_ID_VENDOR_SPECIFIC       0x09

#define VIRTIO_PCI_CAP_COMMON_CFG               1
#define VIRTIO_PCI_CAP_NOTIFY_CFG               2
#define VIRTIO_PCI_CAP_ISR_CFG                  3
#define VIRTIO_PCI_CAP_DEVICE_CFG               4

#define VIRTIO_PCI_CAP_CFG_TYPE                 3
#define VIRTIO_PCI_CAP_BAR                      4
#define VIRTIO_PCI_CAP_OFFSET                   8
#define VIRTIO_PCI_CAP_LENGTH                   12
#define VIRTIO_PCI_NOTIFY_CAP_MULT              16

#define VIRTIO_PCI_COMMON_DFSELECT              0
#define VIRTIO_PCI_COMMON_DF                    4
#define VIRTIO_PCI_COMMON_GFSELECT              8
#define VIRTIO_PCI_COMMON_GF                    12
#define VIRTIO_PCI_COMMON_MSIX                  16
#define VIRTIO_PCI_COMMON_NUMQ                  18
#define VIRTIO_PCI_COMMON_STATUS                20
#define VIRTIO_PCI_COMMON_CFGGENERATION         21
#define VIRTIO_PCI_COMMON_Q_SELECT              22
#define VIRTIO_PCI_COMMON_Q_SIZE                24
#define VIRTIO_PCI_COMMON_Q_MSIX                26
#define VIRTIO_PCI_COMMON_Q_ENABLE              28
#define VIRTIO_PCI_COMMON_Q_NOFF                30
#define VIRTIO_PCI_COMMON_Q_DESCLO              32
#define VIRTIO_PCI_COMMON_Q_DESCHI              36
#define VIRTIO_PCI_COMMON_Q_AVAILLO             40
#define VIRTIO_PCI_COMMON_Q_AVAILHI             44
#define VIRTIO_PCI_COMMON_Q_USEDLO              48
#define VIRTIO_PCI_COMMON_Q_USEDHI              52
#define VIRTIO_PCI_COMMON_CFG_MIN_LENGTH        56

#define VIRTIO_MSI_NO_VECTOR                    0xFFFF

#define VIRTIO_ISR_QUEUE                        0x01
#define VIRTIO_ISR_CONFIG                       0x02

#define VIRTIO_STATUS_ACKNOWLEDGE               0x01
#define VIRTIO_STATUS_DRIVER                    0x02
#define VIRTIO_STATUS_DRIVER_OK                 0x04
#define VIRTIO_STATUS_FEATURES_OK               0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET        0x40
#define VIRTIO_STATUS_FAILED                    0x80

#define VIRTIO_FEATURE(Bit)                     (1ULL << (Bit))

#define VIRTIO_F_RING_INDIRECT_DESC             28
#define VIRTIO_F_VERSION_1                      32
#define VIRTIO_F_ACCESS_PLATFORM                33
#define VIRTIO_F_ORDER_PLATFORM                 36

#define VIRTIO_BLK_F_SIZE_MAX                   1
#define VIRTIO_BLK_F_SEG_MAX                    2
#define VIRTIO_BLK_F_RO                         5
#define VIRTIO_BLK_F_BLK_SIZE                   6
#define VIRTIO_BLK_F_FLUSH                      9
#define VIRTIO_BLK_F_TOPOLOGY                   10
#define VIRTIO_BLK_F_CONFIG_WCE                 11
#define VIRTIO_BLK_F_DISCARD                    13

#define VIRTIO_BLK_CFG_CAPACITY                 0
#define VIRTIO_BLK_CFG_SIZE_MAX                 8
#define VIRTIO_BLK_CFG_SEG_MAX                  12
#define VIRTIO_BLK_CFG_BLK_SIZE                 20
#define VIRTIO_BLK_CFG_PHYSICAL_BLOCK_EXP       24
#define VIRTIO_BLK_CFG_ALIGNMENT_OFFSET         25
#define VIRTIO_BLK_CFG_MIN_IO_SIZE              26
#define VIRTIO_BLK_CFG_OPT_IO_SIZE              28
#define VIRTIO_BLK_CFG_WRITEBACK                32
#define VIRTIO_BLK_CFG_MAX_DISCARD_SECTORS      36
#define VIRTIO_BLK_CFG_MAX_DISCARD_SEG          40
#define VIRTIO_BLK_CFG_DISCARD_SECTOR_ALIGNMENT 44

#define VIRTIO_BLK_T_IN                         0
#define VIRTIO_BLK_T_OUT                        1
#define VIRTIO_BLK_T_FLUSH                      4
#define VIRTIO_BLK_T_GET_ID                     8
#define VIRTIO_BLK_T_DISCARD                    11

#define VIRTIO_BLK_S_OK                         0
#define VIRTIO_BLK_S_IOERR                      1
#define VIRTIO_BLK_S_UNSUPP                     2

#define VIRTIO_BLK_ID_BYTES                     20
#define VIRTIO_BLK_SECTOR_SHIFT                 9

#define VIRTQ_DESC_F_NEXT                       1
#define VIRTQ_DESC_F_WRITE                      2
#define VIRTQ_DESC_F_INDIRECT                   4

#define VIRTQ_USED_F_NO_NOTIFY                  1

#define VBLK_MAX_QUEUE_SIZE                     128
#define VBLK_MAX_DATA_DESCRIPTORS               128
#define VBLK_MAX_DISCARD_RANGES                 16
#define VBLK_DIRECT_MIN_REQUESTS                4U
#define VBLK_RESET_TIMEOUT_US                   1000000

#define VBLK_VENDOR_ID                          "VirtIO  "
#define VBLK_PRODUCT_ID                         "Block Device    "
#define VBLK_PRODUCT_REVISION                   "1.0 "

typedef struct _VIRTQ_DESC
{
    ULONGLONG Address;
    ULONG Length;
    USHORT Flags;
    USHORT Next;
} VIRTQ_DESC, *PVIRTQ_DESC;

typedef struct _VIRTQ_AVAIL
{
    USHORT Flags;
    USHORT Index;
    USHORT Ring[ANYSIZE_ARRAY];
} VIRTQ_AVAIL, *PVIRTQ_AVAIL;

typedef struct _VIRTQ_USED_ELEMENT
{
    ULONG Id;
    ULONG Length;
} VIRTQ_USED_ELEMENT, *PVIRTQ_USED_ELEMENT;

typedef struct _VIRTQ_USED
{
    USHORT Flags;
    USHORT Index;
    VIRTQ_USED_ELEMENT Ring[ANYSIZE_ARRAY];
} VIRTQ_USED, *PVIRTQ_USED;

typedef struct _VIRTIO_BLK_REQUEST_HEADER
{
    ULONG Type;
    ULONG Reserved;
    ULONGLONG Sector;
} VIRTIO_BLK_REQUEST_HEADER, *PVIRTIO_BLK_REQUEST_HEADER;

typedef struct _VIRTIO_BLK_DISCARD_RANGE
{
    ULONGLONG Sector;
    ULONG NumberOfSectors;
    ULONG Flags;
} VIRTIO_BLK_DISCARD_RANGE, *PVIRTIO_BLK_DISCARD_RANGE;

C_ASSERT(sizeof(VIRTQ_DESC) == 16);
C_ASSERT(sizeof(VIRTQ_USED_ELEMENT) == 8);
C_ASSERT(sizeof(VIRTIO_BLK_REQUEST_HEADER) == 16);
C_ASSERT(sizeof(VIRTIO_BLK_DISCARD_RANGE) == 16);

typedef enum _VBLK_REQUEST_KIND
{
    VblkRequestRead,
    VblkRequestWrite,
    VblkRequestFlush,
    VblkRequestDiscard,
    VblkRequestGetId
} VBLK_REQUEST_KIND;

typedef struct _VBLK_REQUEST_DMA
{
    VIRTQ_DESC Table[VBLK_MAX_DATA_DESCRIPTORS + 2];
    VIRTIO_BLK_REQUEST_HEADER Header;
    union
    {
        VIRTIO_BLK_DISCARD_RANGE Ranges[VBLK_MAX_DISCARD_RANGES];
        UCHAR Id[VIRTIO_BLK_ID_BYTES];
    } Data;
    UCHAR Status;
} VBLK_REQUEST_DMA, *PVBLK_REQUEST_DMA;

typedef struct _VBLK_SRB_EXTENSION
{
    VBLK_REQUEST_DMA Dma;
    VBLK_REQUEST_KIND Kind;
    BOOLEAN FlushAfterWrite;
} VBLK_SRB_EXTENSION, *PVBLK_SRB_EXTENSION;

C_ASSERT(sizeof(VBLK_SRB_EXTENSION) <= PAGE_SIZE);

typedef struct _VBLK_DEVICE_EXTENSION
{
    ULONG SystemIoBusNumber;
    ULONG SlotNumber;

    PUCHAR CommonCfg;
    PUCHAR NotifyCfg;
    PUCHAR IsrStatus;
    PUCHAR DeviceCfg;
    ULONG NotifyLength;
    ULONG NotifyMultiplier;
    ULONG DeviceCfgLength;
    volatile USHORT *QueueNotify;

    ULONGLONG DeviceFeatures;
    ULONGLONG DriverFeatures;

    volatile VIRTQ_DESC *Desc;
    volatile VIRTQ_AVAIL *Avail;
    volatile VIRTQ_USED *Used;
    ULONGLONG DescPhysical;
    ULONGLONG AvailPhysical;
    ULONGLONG UsedPhysical;
    ULONG RingBytes;
    USHORT QueueSize;
    USHORT FreeHead;
    USHORT FreeCount;
    USHORT AvailIndex;
    USHORT LastUsedIndex;
    PSCSI_REQUEST_BLOCK Requests[VBLK_MAX_QUEUE_SIZE];

    ULONGLONG CapacitySectors;
    ULONGLONG Blocks;
    ULONG BlockSize;
    UCHAR BlockShift;
    UCHAR PhysicalBlockExponent;
    USHORT LowestAlignedBlock;
    ULONG OptimalGranularityBlocks;
    ULONG OptimalTransferBlocks;
    ULONG SegmentSizeLimit;
    ULONG MaxDataDescriptors;
    ULONG MaxRequests;
    ULONG MaxTransferLength;
    ULONG MaxDiscardSectors;
    ULONG MaxDiscardRanges;
    ULONG DiscardAlignmentSectors;

    BOOLEAN ReadOnly;
    BOOLEAN Indirect;
    BOOLEAN Flush;
    BOOLEAN ConfigWce;
    BOOLEAN Discard;
    BOOLEAN WriteCacheEnabled;
    BOOLEAN DefaultWriteCache;

    BOOLEAN MessageInterrupts;
    USHORT ConfigVector;
    USHORT QueueVector;

    BOOLEAN DeviceReady;
    BOOLEAN CapacityChanged;
    UCHAR ConfigGeneration;

    BOOLEAN SerialValid;
    UCHAR SerialLength;
    CHAR Serial[VIRTIO_BLK_ID_BYTES];
} VBLK_DEVICE_EXTENSION, *PVBLK_DEVICE_EXTENSION;

/* virtio.c */
BOOLEAN
VblkMapDevice(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo);

BOOLEAN
VblkResetDevice(
    _In_ PVBLK_DEVICE_EXTENSION Device);

BOOLEAN
VblkProbeDevice(
    _In_ PVBLK_DEVICE_EXTENSION Device);

VOID
VblkSetRingMemory(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ PUCHAR Virtual,
    _In_ ULONGLONG Physical);

BOOLEAN
VblkStartDevice(
    _In_ PVBLK_DEVICE_EXTENSION Device);

VOID
VblkStopDevice(
    _In_ PVBLK_DEVICE_EXTENSION Device);

ULONGLONG
VblkReadCapacity(
    _In_ PVBLK_DEVICE_EXTENSION Device);

UCHAR
VblkReadConfigGeneration(
    _In_ PVBLK_DEVICE_EXTENSION Device);

VOID
VblkWriteWriteback(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ BOOLEAN Enable);

UCHAR
VblkQueueRequest(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ ULONG Type,
    _In_ ULONGLONG Sector,
    _In_ ULONG DataDescriptors,
    _In_ ULONG InternalDataLength,
    _In_ BOOLEAN DeviceWritesInternalData);

PSCSI_REQUEST_BLOCK
VblkTakeCompletedRequest(
    _In_ PVBLK_DEVICE_EXTENSION Device);

/* scsi.c */
VOID
VblkUpdateGeometry(
    _In_ PVBLK_DEVICE_EXTENSION Device);

VOID
VblkCompleteRequest(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb);

VOID
VblkCompleteSrb(
    _In_ PVBLK_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus);

BOOLEAN
NTAPI
VblkHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb);
