/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */
#ifdef DEFINE_GUID

#ifndef FAR
#define FAR
typedef enum _NV_SEP_WRITE_CACHE_TYPE {
    NVSEPWriteCacheTypeUnknown = 0,
    NVSEPWriteCacheTypeNone = 1,
    NVSEPWriteCacheTypeWriteBack = 2,
    NVSEPWriteCacheTypeWriteThrough = 3
} NV_SEP_WRITE_CACHE_TYPE, *PNV_SEP_WRITE_CACHE_TYPE;

typedef enum _MP_STORAGE_DIAGNOSTIC_LEVEL {
    MpStorageDiagnosticLevelDefault = 0,
    MpStorageDiagnosticLevelMax = 1
} MP_STORAGE_DIAGNOSTIC_LEVEL, *PMP_STORAGE_DIAGNOSTIC_LEVEL;

typedef enum _MP_STORAGE_DIAGNOSTIC_TARGET_TYPE {
    MpStorageDiagnosticTargetTypeUndefined = 0,
    MpStorageDiagnosticTargetTypeMiniport = 2,
    MpStorageDiagnosticTargetTypeHbaFirmware = 3,
    MpStorageDiagnosticTargetTypeMax = 4
} MP_STORAGE_DIAGNOSTIC_TARGET_TYPE, *PMP_STORAGE_DIAGNOSTIC_TARGET_TYPE;

#endif

DEFINE_GUID(ScsiRawInterfaceGuid,0x53f56309L,0xb6bf,0x11d0,0x94,0xf2,0x00,0xa0,0xc9,0x1e,0xfb,0x8b);
DEFINE_GUID(WmiScsiAddressGuid,0x53f5630fL,0xb6bf,0x11d0,0x94,0xf2,0x00,0xa0,0xc9,0x1e,0xfb,0x8b);
#endif /* DEFINE_GUID */

#ifndef _NTDDSCSIH_
#define _NTDDSCSIH_

#ifdef __cplusplus
extern "C" {
#endif

#define IOCTL_SCSI_BASE     FILE_DEVICE_CONTROLLER

#define DD_SCSI_DEVICE_NAME "\\Device\\ScsiPort"
#define DD_SCSI_DEVICE_NAME_U  L"\\Device\\ScsiPort"

#define IOCTL_SCSI_PASS_THROUGH CTL_CODE(IOCTL_SCSI_BASE,0x0401,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SCSI_MINIPORT CTL_CODE(IOCTL_SCSI_BASE,0x0402,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SCSI_GET_INQUIRY_DATA CTL_CODE(IOCTL_SCSI_BASE,0x0403,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_GET_CAPABILITIES CTL_CODE(IOCTL_SCSI_BASE,0x0404,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_PASS_THROUGH_DIRECT CTL_CODE(IOCTL_SCSI_BASE,0x0405,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SCSI_GET_ADDRESS CTL_CODE(IOCTL_SCSI_BASE,0x0406,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_RESCAN_BUS CTL_CODE(IOCTL_SCSI_BASE,0x0407,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_GET_DUMP_POINTERS CTL_CODE(IOCTL_SCSI_BASE,0x0408,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_FREE_DUMP_POINTERS CTL_CODE(IOCTL_SCSI_BASE,0x0409,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SCSI_PASS_THROUGH_EX CTL_CODE(IOCTL_SCSI_BASE, 0x0411, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SCSI_PASS_THROUGH_DIRECT_EX CTL_CODE(IOCTL_SCSI_BASE, 0x0412, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_IDE_PASS_THROUGH CTL_CODE(IOCTL_SCSI_BASE,0x040a,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_ATA_PASS_THROUGH CTL_CODE(IOCTL_SCSI_BASE,0x040b,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_ATA_PASS_THROUGH_DIRECT CTL_CODE(IOCTL_SCSI_BASE,0x040c,METHOD_BUFFERED,FILE_READ_ACCESS | FILE_WRITE_ACCESS)

  typedef struct _SCSI_PASS_THROUGH {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG_PTR DataBufferOffset;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
  }SCSI_PASS_THROUGH,*PSCSI_PASS_THROUGH;

  typedef struct _SCSI_PASS_THROUGH_DIRECT {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    PVOID DataBuffer;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
  }SCSI_PASS_THROUGH_DIRECT,*PSCSI_PASS_THROUGH_DIRECT;

#if defined(_WIN64)
  typedef struct _SCSI_PASS_THROUGH32 {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG32 DataBufferOffset;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
  } SCSI_PASS_THROUGH32,*PSCSI_PASS_THROUGH32;

  typedef struct _SCSI_PASS_THROUGH_DIRECT32 {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    VOID * POINTER_32 DataBuffer;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
  } SCSI_PASS_THROUGH_DIRECT32,*PSCSI_PASS_THROUGH_DIRECT32;
#endif /* _WIN64 */


  typedef struct _SCSI_PASS_THROUGH_EX {
    ULONG Version;
    ULONG Length;
    ULONG CdbLength;
    ULONG StorAddressLength;
    UCHAR ScsiStatus;
    UCHAR SenseInfoLength;
    UCHAR DataDirection;
    UCHAR Reserved;
    ULONG TimeOutValue;
    ULONG StorAddressOffset;
    ULONG SenseInfoOffset;
    ULONG DataOutTransferLength;
    ULONG DataInTransferLength;
    ULONG_PTR DataOutBufferOffset;
    ULONG_PTR DataInBufferOffset;
    UCHAR Cdb[ANYSIZE_ARRAY];
  } SCSI_PASS_THROUGH_EX, *PSCSI_PASS_THROUGH_EX;

typedef struct _SCSI_PASS_THROUGH_DIRECT_EX
{
  ULONG Version;
  ULONG Length;
  ULONG CdbLength;
  ULONG StorAddressLength;
  UCHAR ScsiStatus;
  UCHAR SenseInfoLength;
  UCHAR DataDirection;
  UCHAR Reserved;
  ULONG TimeOutValue;
  ULONG StorAddressOffset;
  ULONG SenseInfoOffset;
  ULONG DataOutTransferLength;
  ULONG DataInTransferLength;
  PVOID DataOutBuffer;
  PVOID DataInBuffer;
  UCHAR Cdb[ANYSIZE_ARRAY];
} SCSI_PASS_THROUGH_DIRECT_EX, *PSCSI_PASS_THROUGH_DIRECT_EX;

#if defined(_WIN64)
typedef struct _SCSI_PASS_THROUGH32_EX
{
  ULONG Version;
  ULONG Length;
  ULONG CdbLength;
  ULONG StorAddressLength;
  UCHAR ScsiStatus;
  UCHAR SenseInfoLength;
  UCHAR DataDirection;
  UCHAR Reserved;
  ULONG TimeOutValue;
  ULONG StorAddressOffset;
  ULONG SenseInfoOffset;
  ULONG DataOutTransferLength;
  ULONG DataInTransferLength;
  ULONG32 DataOutBufferOffset;
  ULONG32 DataInBufferOffset;
  UCHAR Cdb[ANYSIZE_ARRAY];
} SCSI_PASS_THROUGH32_EX, *PSCSI_PASS_THROUGH32_EX;

typedef struct _SCSI_PASS_THROUGH_DIRECT32_EX
{
  ULONG Version;
  ULONG Length;
  ULONG CdbLength;
  ULONG StorAddressLength;
  UCHAR ScsiStatus;
  UCHAR SenseInfoLength;
  UCHAR DataDirection;
  UCHAR Reserved;
  ULONG TimeOutValue;
  ULONG StorAddressOffset;
  ULONG SenseInfoOffset;
  ULONG DataOutTransferLength;
  ULONG DataInTransferLength;
  VOID * POINTER_32 DataOutBuffer;
  VOID * POINTER_32 DataInBuffer;
  UCHAR Cdb[ANYSIZE_ARRAY];
} SCSI_PASS_THROUGH_DIRECT32_EX, *PSCSI_PASS_THROUGH_DIRECT32_EX;
#endif

  typedef struct _ATA_PASS_THROUGH_EX {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR ReservedAsUchar;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG ReservedAsUlong;
    ULONG_PTR DataBufferOffset;
    UCHAR PreviousTaskFile[8];
    UCHAR CurrentTaskFile[8];
  } ATA_PASS_THROUGH_EX,*PATA_PASS_THROUGH_EX;

  typedef struct _ATA_PASS_THROUGH_DIRECT {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR ReservedAsUchar;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG ReservedAsUlong;
    PVOID DataBuffer;
    UCHAR PreviousTaskFile[8];
    UCHAR CurrentTaskFile[8];
  } ATA_PASS_THROUGH_DIRECT,*PATA_PASS_THROUGH_DIRECT;

#if defined(_WIN64)

  typedef struct _ATA_PASS_THROUGH_EX32 {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR ReservedAsUchar;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG ReservedAsUlong;
    ULONG32 DataBufferOffset;
    UCHAR PreviousTaskFile[8];
    UCHAR CurrentTaskFile[8];
  } ATA_PASS_THROUGH_EX32,*PATA_PASS_THROUGH_EX32;

  typedef struct _ATA_PASS_THROUGH_DIRECT32 {
    USHORT Length;
    USHORT AtaFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR ReservedAsUchar;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG ReservedAsUlong;
    VOID * POINTER_32 DataBuffer;
    UCHAR PreviousTaskFile[8];
    UCHAR CurrentTaskFile[8];
  } ATA_PASS_THROUGH_DIRECT32,*PATA_PASS_THROUGH_DIRECT32;
#endif /* _WIN64 */

#define ATA_FLAGS_DRDY_REQUIRED (1 << 0)
#define ATA_FLAGS_DATA_IN (1 << 1)
#define ATA_FLAGS_DATA_OUT (1 << 2)
#define ATA_FLAGS_48BIT_COMMAND (1 << 3)
#define ATA_FLAGS_USE_DMA (1 << 4)
#define ATA_FLAGS_NO_MULTIPLE (1 << 5)

  typedef struct _SCSI_BUS_DATA {
    UCHAR NumberOfLogicalUnits;
    UCHAR InitiatorBusId;
    ULONG InquiryDataOffset;
  }SCSI_BUS_DATA,*PSCSI_BUS_DATA;

  typedef struct _SCSI_ADAPTER_BUS_INFO {
    UCHAR NumberOfBuses;
    SCSI_BUS_DATA BusData[1];
  } SCSI_ADAPTER_BUS_INFO,*PSCSI_ADAPTER_BUS_INFO;

  typedef struct _SCSI_INQUIRY_DATA {
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    BOOLEAN DeviceClaimed;
    ULONG InquiryDataLength;
    ULONG NextInquiryDataOffset;
    UCHAR InquiryData[1];
  }SCSI_INQUIRY_DATA,*PSCSI_INQUIRY_DATA;

  typedef struct _SRB_IO_CONTROL {
    ULONG HeaderLength;
    UCHAR Signature[8];
    ULONG Timeout;
    ULONG ControlCode;
    ULONG ReturnCode;
    ULONG Length;
  } SRB_IO_CONTROL,*PSRB_IO_CONTROL;

  typedef struct _IO_SCSI_CAPABILITIES {
    ULONG Length;
    ULONG MaximumTransferLength;
    ULONG MaximumPhysicalPages;
    ULONG SupportedAsynchronousEvents;
    ULONG AlignmentMask;
    BOOLEAN TaggedQueuing;
    BOOLEAN AdapterScansDown;
    BOOLEAN AdapterUsesPio;
  } IO_SCSI_CAPABILITIES,*PIO_SCSI_CAPABILITIES;

  typedef struct _SCSI_ADDRESS {
    ULONG Length;
    UCHAR PortNumber;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
  } SCSI_ADDRESS,*PSCSI_ADDRESS;

  struct _ADAPTER_OBJECT;

  typedef struct _DUMP_POINTERS {
    struct _ADAPTER_OBJECT *AdapterObject;
    PVOID MappedRegisterBase;
    PVOID DumpData;
    PVOID CommonBufferVa;
    LARGE_INTEGER CommonBufferPa;
    ULONG CommonBufferSize;
    BOOLEAN AllocateCommonBuffers;
    BOOLEAN UseDiskDump;
    UCHAR Spare1[2];
    PVOID DeviceObject;
  } DUMP_POINTERS,*PDUMP_POINTERS;

#define SCSI_IOCTL_DATA_OUT 0
#define SCSI_IOCTL_DATA_IN 1
#define SCSI_IOCTL_DATA_UNSPECIFIED 2

#define IOCTL_SCSI_MINIPORT_NVCACHE           ((FILE_DEVICE_SCSI << 16) + 0x0600)

#define IOCTL_SCSI_MINIPORT_HYBRID            ((FILE_DEVICE_SCSI << 16) + 0x0620)

#define IOCTL_SCSI_MINIPORT_FIRMWARE          ((FILE_DEVICE_SCSI << 16) + 0x0780)

#define IOCTL_MINIPORT_SIGNATURE_QUERY_PROTOCOL     "PROTOCOL"

#define IOCTL_MINIPORT_SIGNATURE_QUERY_TEMPERATURE  "TEMPERAT"

#define IOCTL_MINIPORT_SIGNATURE_QUERY_PHYSICAL_TOPOLOGY    "TOPOLOGY"

typedef struct _NVCACHE_REQUEST_BLOCK {
    ULONG           NRBSize;
    USHORT          Function;
    ULONG           NRBFlags;
    ULONG           NRBStatus;
    ULONG           Count;
    ULONGLONG       LBA;
    ULONG           DataBufSize;
    ULONG           NVCacheStatus;
    ULONG           NVCacheSubStatus;
} NVCACHE_REQUEST_BLOCK, *PNVCACHE_REQUEST_BLOCK;

#define NRB_FUNCTION_NVCACHE_INFO               0xEC

#define NRB_FUNCTION_SPINDLE_STATUS                 0xE5

#define NRB_FUNCTION_NVCACHE_POWER_MODE_SET         0x00

#define NRB_FUNCTION_NVCACHE_POWER_MODE_RETURN  0x01

#define NRB_FUNCTION_FLUSH_NVCACHE              0x14

#define NRB_FUNCTION_QUERY_PINNED_SET           0x12

#define NRB_FUNCTION_QUERY_CACHE_MISS           0x13

#define NRB_FUNCTION_ADD_LBAS_PINNED_SET        0x10

#define NRB_FUNCTION_REMOVE_LBAS_PINNED_SET     0x11

#define NRB_FUNCTION_QUERY_ASCENDER_STATUS      0xD0

#define NRB_FUNCTION_QUERY_HYBRID_DISK_STATUS   0xD1

#define NRB_SUCCESS                             0

#define NRB_ILLEGAL_REQUEST                     1

#define NRB_INVALID_PARAMETER                   2

typedef struct _NV_FEATURE_PARAMETER{
        USHORT NVPowerModeEnabled;
        USHORT NVParameterReserv1;
        USHORT NVCmdEnabled;
        USHORT NVParameterReserv2;
        USHORT NVPowerModeVer;
        USHORT NVCmdVer;
        ULONG  NVSize;
        USHORT NVReadSpeed;
        USHORT NVWrtSpeed;
        ULONG  DeviceSpinUpTime;
} NV_FEATURE_PARAMETER, *PNV_FEATURE_PARAMETER;

#define HYBRID_FUNCTION_GET_INFO                            0x01

#define HYBRID_FUNCTION_DISABLE_CACHING_MEDIUM              0x10

#define HYBRID_FUNCTION_ENABLE_CACHING_MEDIUM               0x11

#define HYBRID_FUNCTION_SET_DIRTY_THRESHOLD                 0x12

#define HYBRID_FUNCTION_DEMOTE_BY_SIZE                      0x13

#define HYBRID_STATUS_ILLEGAL_REQUEST                     0x1

#define HYBRID_STATUS_INVALID_PARAMETER                   0x2

#define HYBRID_STATUS_OUTPUT_BUFFER_TOO_SMALL             0x3

#define HYBRID_STATUS_ENABLE_REFCOUNT_HOLD                0x10

typedef struct _HYBRID_REQUEST_BLOCK {
    ULONG   Version;
    ULONG   Size;
    ULONG   Function;
    ULONG   Flags;
    ULONG   DataBufferOffset;
    ULONG   DataBufferLength;
} HYBRID_REQUEST_BLOCK, *PHYBRID_REQUEST_BLOCK;

typedef enum _NVCACHE_TYPE {
    NvCacheTypeUnknown        = 0,
    NvCacheTypeNone           = 1,
    NvCacheTypeWriteBack      = 2,
    NvCacheTypeWriteThrough   = 3
} NVCACHE_TYPE;

typedef enum _NVCACHE_STATUS {
    NvCacheStatusUnknown     = 0,
    NvCacheStatusDisabling   = 1,
    NvCacheStatusDisabled    = 2,
    NvCacheStatusEnabled     = 3
} NVCACHE_STATUS;

typedef struct _NVCACHE_PRIORITY_LEVEL_DESCRIPTOR {
    UCHAR   PriorityLevel;
    UCHAR   Reserved0[3];
    ULONG   ConsumedNVMSizeFraction;
    ULONG   ConsumedMappingResourcesFraction;
    ULONG   ConsumedNVMSizeForDirtyDataFraction;
    ULONG   ConsumedMappingResourcesForDirtyDataFraction;
    ULONG   Reserved1;
} NVCACHE_PRIORITY_LEVEL_DESCRIPTOR, *PNVCACHE_PRIORITY_LEVEL_DESCRIPTOR;

#define HYBRID_REQUEST_INFO_STRUCTURE_VERSION           0x1

typedef struct _HYBRID_INFORMATION {
    ULONG           Version;
    ULONG           Size;
    BOOLEAN         HybridSupported;
    NVCACHE_STATUS  Status;
    NVCACHE_TYPE    CacheTypeEffective;
    NVCACHE_TYPE    CacheTypeDefault;
    ULONG           FractionBase;
    ULONGLONG       CacheSize;
    struct {
        ULONG   WriteCacheChangeable    : 1;
        ULONG   WriteThroughIoSupported : 1;
        ULONG   FlushCacheSupported     : 1;
        ULONG   Removable               : 1;
        ULONG   ReservedBits            : 28;
    } Attributes;
    struct {
        UCHAR     PriorityLevelCount;
        BOOLEAN   MaxPriorityBehavior;
        UCHAR     OptimalWriteGranularity;
        UCHAR     Reserved;
        ULONG     DirtyThresholdLow;
        ULONG     DirtyThresholdHigh;
        struct {
            ULONG   CacheDisable                : 1;
            ULONG   SetDirtyThreshold           : 1;
            ULONG   PriorityDemoteBySize        : 1;
            ULONG   PriorityChangeByLbaRange    : 1;
            ULONG   Evict                       : 1;
            ULONG   ReservedBits                : 27;
            ULONG   MaxEvictCommands;
            ULONG   MaxLbaRangeCountForEvict;
            ULONG   MaxLbaRangeCountForChangeLba;
        } SupportedCommands;
        NVCACHE_PRIORITY_LEVEL_DESCRIPTOR   Priority[0];
    } Priorities;
} HYBRID_INFORMATION, *PHYBRID_INFORMATION;

typedef struct _HYBRID_DIRTY_THRESHOLDS {
    ULONG   Version;
    ULONG   Size;
    ULONG   DirtyLowThreshold;
    ULONG   DirtyHighThreshold;
} HYBRID_DIRTY_THRESHOLDS, *PHYBRID_DIRTY_THRESHOLDS;

typedef struct _HYBRID_DEMOTE_BY_SIZE {
    ULONG       Version;
    ULONG       Size;
    UCHAR       SourcePriority;
    UCHAR       TargetPriority;
    USHORT      Reserved0;
    ULONG       Reserved1;
    ULONGLONG   LbaCount;
} HYBRID_DEMOTE_BY_SIZE, *PHYBRID_DEMOTE_BY_SIZE;

#define FIRMWARE_FUNCTION_GET_INFO                          0x01

#define FIRMWARE_FUNCTION_DOWNLOAD                          0x02

#define FIRMWARE_FUNCTION_ACTIVATE                          0x03

#define FIRMWARE_STATUS_SUCCESS                             0x0

#define FIRMWARE_STATUS_ERROR                               0x1

#define FIRMWARE_STATUS_INVALID_PARAMETER                   0x3

#define FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL             0x5

#define FIRMWARE_STATUS_DEVICE_ERROR                        0x40

#define FIRMWARE_STATUS_INTERFACE_CRC_ERROR                 0x80

#define FIRMWARE_STATUS_MEDIA_CHANGE                        0x82

#define FIRMWARE_STATUS_ID_NOT_FOUND                        0x83

#define FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST                0x84

#define FIRMWARE_STATUS_COMMAND_ABORT                       0x85

#define FIRMWARE_STATUS_END_OF_MEDIA                        0x86

#define FIRMWARE_STATUS_ILLEGAL_LENGTH                      0x87

#define FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION            0x1

typedef struct _FIRMWARE_REQUEST_BLOCK {
    ULONG   Version;
    ULONG   Size;
    ULONG   Function;
    ULONG   Flags;
    ULONG   DataBufferOffset;
    ULONG   DataBufferLength;
} FIRMWARE_REQUEST_BLOCK, *PFIRMWARE_REQUEST_BLOCK;

#define STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2      0x2

#define STORAGE_FIRMWARE_INFO_INVALID_SLOT              0xFF

#define STORAGE_FIRMWARE_SLOT_INFO_V2_REVISION_LENGTH   16

typedef struct _STORAGE_FIRMWARE_SLOT_INFO_V2 {
    UCHAR   SlotNumber;
    BOOLEAN ReadOnly;
    UCHAR   Reserved[6];
    UCHAR   Revision[STORAGE_FIRMWARE_SLOT_INFO_V2_REVISION_LENGTH];
} STORAGE_FIRMWARE_SLOT_INFO_V2, *PSTORAGE_FIRMWARE_SLOT_INFO_V2;

typedef struct _STORAGE_FIRMWARE_INFO_V2 {
    ULONG   Version;
    ULONG   Size;
    BOOLEAN UpgradeSupport;
    UCHAR   SlotCount;
    UCHAR   ActiveSlot;
    UCHAR   PendingActivateSlot;
    BOOLEAN FirmwareShared;
    UCHAR   Reserved[3];
    ULONG   ImagePayloadAlignment;
    ULONG   ImagePayloadMaxSize;
    STORAGE_FIRMWARE_SLOT_INFO_V2 Slot[0];
} STORAGE_FIRMWARE_INFO_V2, *PSTORAGE_FIRMWARE_INFO_V2;

#ifdef __cplusplus
}
#endif

#endif /* _NTDDSCSIH_ */

#ifndef BOOT_PARTITION_FUNCTION_ACTIVATE
#define BOOT_PARTITION_FUNCTION_ACTIVATE 0x02
#endif
#ifndef BOOT_PARTITION_FUNCTION_DOWNLOAD
#define BOOT_PARTITION_FUNCTION_DOWNLOAD 0x01
#endif
#ifndef BOOT_PARTITION_FUNCTION_GET_INFO
#define BOOT_PARTITION_FUNCTION_GET_INFO 0x03
#endif
#ifndef BOOT_PARTITION_REQUEST_BLOCK_STRUCTURE_VERSION
#define BOOT_PARTITION_REQUEST_BLOCK_STRUCTURE_VERSION 0x1
#endif
#ifndef BOOT_PARTITION_STATUS_ERROR
#define BOOT_PARTITION_STATUS_ERROR 0x1
#endif
#ifndef BOOT_PARTITION_STATUS_ILLEGAL_REQUEST
#define BOOT_PARTITION_STATUS_ILLEGAL_REQUEST 0x2
#endif
#ifndef BOOT_PARTITION_STATUS_INPUT_BUFFER_TOO_BIG
#define BOOT_PARTITION_STATUS_INPUT_BUFFER_TOO_BIG 0x4
#endif
#ifndef BOOT_PARTITION_STATUS_INVALID_ALIGNMENT
#define BOOT_PARTITION_STATUS_INVALID_ALIGNMENT 0x8
#endif
#ifndef BOOT_PARTITION_STATUS_INVALID_IMAGE
#define BOOT_PARTITION_STATUS_INVALID_IMAGE 0x7
#endif
#ifndef BOOT_PARTITION_STATUS_INVALID_PARAMETER
#define BOOT_PARTITION_STATUS_INVALID_PARAMETER 0x3
#endif
#ifndef BOOT_PARTITION_STATUS_INVALID_SLOT
#define BOOT_PARTITION_STATUS_INVALID_SLOT 0x6
#endif
#ifndef BOOT_PARTITION_STATUS_OUTPUT_BUFFER_TOO_SMALL
#define BOOT_PARTITION_STATUS_OUTPUT_BUFFER_TOO_SMALL 0x5
#endif
#ifndef BOOT_PARTITION_STATUS_SUCCESS
#define BOOT_PARTITION_STATUS_SUCCESS 0x0
#endif
#ifndef BOOT_PARTITION_STATUS_WRITE_PROHIBITED
#define BOOT_PARTITION_STATUS_WRITE_PROHIBITED 0x9
#endif
#ifndef DUMP_DRIVER_NAME_LENGTH
#define DUMP_DRIVER_NAME_LENGTH 15
#endif
#ifndef DUMP_EX_FLAG_DRIVER_FULL_PATH_SUPPORT
#define DUMP_EX_FLAG_DRIVER_FULL_PATH_SUPPORT 0x00000008
#endif
#ifndef DUMP_EX_FLAG_RESUME_SUPPORT
#define DUMP_EX_FLAG_RESUME_SUPPORT 0x00000004
#endif
#ifndef DUMP_EX_FLAG_SUPPORT_64BITMEMORY
#define DUMP_EX_FLAG_SUPPORT_64BITMEMORY 0x00000001
#endif
#ifndef DUMP_EX_FLAG_SUPPORT_DD_TELEMETRY
#define DUMP_EX_FLAG_SUPPORT_DD_TELEMETRY 0x00000002
#endif
#ifndef DUMP_POINTERS_VERSION_1
#define DUMP_POINTERS_VERSION_1 1
#endif
#ifndef DUMP_POINTERS_VERSION_2
#define DUMP_POINTERS_VERSION_2 2
#endif
#ifndef DUMP_POINTERS_VERSION_3
#define DUMP_POINTERS_VERSION_3 3
#endif
#ifndef DUMP_POINTERS_VERSION_4
#define DUMP_POINTERS_VERSION_4 4
#endif
#ifndef FIRMWARE_REQUEST_FLAG_CONTROLLER
#define FIRMWARE_REQUEST_FLAG_CONTROLLER 0x00000001
#endif
#ifndef FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT
#define FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT 0x00000004
#endif
#ifndef FIRMWARE_REQUEST_FLAG_LAST_SEGMENT
#define FIRMWARE_REQUEST_FLAG_LAST_SEGMENT 0x00000002
#endif
#ifndef FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET
#define FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET 0x20000000
#endif
#ifndef FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE
#define FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE 0x40000000
#endif
#ifndef FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE
#define FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE 0x80000000
#endif
#ifndef FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET
#define FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET 0x10000000
#endif
#ifndef FIRMWARE_STATUS_CONTROLLER_ERROR
#define FIRMWARE_STATUS_CONTROLLER_ERROR 0x10
#endif
#ifndef FIRMWARE_STATUS_ILLEGAL_REQUEST
#define FIRMWARE_STATUS_ILLEGAL_REQUEST 0x2
#endif
#ifndef FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG
#define FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG 0x4
#endif
#ifndef FIRMWARE_STATUS_INVALID_IMAGE
#define FIRMWARE_STATUS_INVALID_IMAGE 0x7
#endif
#ifndef FIRMWARE_STATUS_INVALID_SLOT
#define FIRMWARE_STATUS_INVALID_SLOT 0x6
#endif
#ifndef FIRMWARE_STATUS_POWER_CYCLE_REQUIRED
#define FIRMWARE_STATUS_POWER_CYCLE_REQUIRED 0x20
#endif
#ifndef FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR
#define FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR 0x81
#endif
#ifndef HYBRID_REQUEST_BLOCK_STRUCTURE_VERSION
#define HYBRID_REQUEST_BLOCK_STRUCTURE_VERSION 0x1
#endif
#ifndef HYBRID_STATUS_SUCCESS
#define HYBRID_STATUS_SUCCESS 0x0
#endif
#ifndef MINIPORT_DSM_NOTIFICATION_VERSION_1
#define MINIPORT_DSM_NOTIFICATION_VERSION_1 1
#endif
#ifndef MINIPORT_DSM_NOTIFY_FLAG_BEGIN
#define MINIPORT_DSM_NOTIFY_FLAG_BEGIN 0x00000001
#endif
#ifndef MINIPORT_DSM_NOTIFY_FLAG_END
#define MINIPORT_DSM_NOTIFY_FLAG_END 0x00000002
#endif
#ifndef MINIPORT_DSM_PROFILE_CRASHDUMP_FILE
#define MINIPORT_DSM_PROFILE_CRASHDUMP_FILE 3
#endif
#ifndef MINIPORT_DSM_PROFILE_HIBERNATION_FILE
#define MINIPORT_DSM_PROFILE_HIBERNATION_FILE 2
#endif
#ifndef MINIPORT_DSM_PROFILE_PAGE_FILE
#define MINIPORT_DSM_PROFILE_PAGE_FILE 1
#endif
#ifndef MINIPORT_DSM_PROFILE_UNKNOWN
#define MINIPORT_DSM_PROFILE_UNKNOWN 0
#endif
#ifndef MPIO_IOCTL_FLAG_INVOLVE_DSM
#define MPIO_IOCTL_FLAG_INVOLVE_DSM 4
#endif
#ifndef MPIO_IOCTL_FLAG_USE_PATHID
#define MPIO_IOCTL_FLAG_USE_PATHID 1
#endif
#ifndef MPIO_IOCTL_FLAG_USE_SCSIADDRESS
#define MPIO_IOCTL_FLAG_USE_SCSIADDRESS 2
#endif
#ifndef NRB_FUNCTION_NVSEPARATED_FLUSH
#define NRB_FUNCTION_NVSEPARATED_FLUSH 0xc1
#endif
#ifndef NRB_FUNCTION_NVSEPARATED_INFO
#define NRB_FUNCTION_NVSEPARATED_INFO 0xc0
#endif
#ifndef NRB_FUNCTION_NVSEPARATED_WB_DISABLE
#define NRB_FUNCTION_NVSEPARATED_WB_DISABLE 0xc2
#endif
#ifndef NRB_FUNCTION_NVSEPARATED_WB_REVERT_DEFAULT
#define NRB_FUNCTION_NVSEPARATED_WB_REVERT_DEFAULT 0xc3
#endif
#ifndef NRB_FUNCTION_PASS_HINT_PAYLOAD
#define NRB_FUNCTION_PASS_HINT_PAYLOAD 0xE0
#endif
#ifndef NRB_INPUT_DATA_OVERRUN
#define NRB_INPUT_DATA_OVERRUN 3
#endif
#ifndef NRB_INPUT_DATA_UNDERRUN
#define NRB_INPUT_DATA_UNDERRUN 4
#endif
#ifndef NRB_OUTPUT_DATA_OVERRUN
#define NRB_OUTPUT_DATA_OVERRUN 5
#endif
#ifndef NRB_OUTPUT_DATA_UNDERRUN
#define NRB_OUTPUT_DATA_UNDERRUN 6
#endif
#ifndef NV_SEP_CACHE_PARAMETER_VERSION_1
#define NV_SEP_CACHE_PARAMETER_VERSION_1 1
#endif
#ifndef SCSI_IOCTL_DATA_BIDIRECTIONAL
#define SCSI_IOCTL_DATA_BIDIRECTIONAL 3
#endif
#ifndef STORAGE_BOOT_PARTITION_ACTIVATE_STRUCTURE_VERSION
#define STORAGE_BOOT_PARTITION_ACTIVATE_STRUCTURE_VERSION 0x1
#endif
#ifndef STORAGE_BOOT_PARTITION_DOWNLOAD_STRUCTURE_VERSION
#define STORAGE_BOOT_PARTITION_DOWNLOAD_STRUCTURE_VERSION 0x1
#endif
#ifndef STORAGE_BOOT_PARTITION_GET_INFO_STRUCTURE_VERSION_V1
#define STORAGE_BOOT_PARTITION_GET_INFO_STRUCTURE_VERSION_V1 0x1
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_BUFFER_TOO_SMALL
#define STORAGE_DIAGNOSTIC_STATUS_BUFFER_TOO_SMALL 0x1
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_INVALID_PARAMETER
#define STORAGE_DIAGNOSTIC_STATUS_INVALID_PARAMETER 0x3
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_INVALID_SIGNATURE
#define STORAGE_DIAGNOSTIC_STATUS_INVALID_SIGNATURE 0x4
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_INVALID_TARGET_TYPE
#define STORAGE_DIAGNOSTIC_STATUS_INVALID_TARGET_TYPE 0x5
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_MORE_DATA
#define STORAGE_DIAGNOSTIC_STATUS_MORE_DATA 0x6
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_SUCCESS
#define STORAGE_DIAGNOSTIC_STATUS_SUCCESS 0
#endif
#ifndef STORAGE_DIAGNOSTIC_STATUS_UNSUPPORTED_VERSION
#define STORAGE_DIAGNOSTIC_STATUS_UNSUPPORTED_VERSION 0x2
#endif
#ifndef STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION
#define STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION 0x1
#endif
#ifndef STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION
#define STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION 0x1
#endif
#ifndef STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2
#define STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2 0x2
#endif
#ifndef STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION
#define STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION 0x1
#endif
