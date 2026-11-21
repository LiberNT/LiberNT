/*
 * ntddstor.h
 *
 * Storage class IOCTL interface.
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef _NTDDSTOR_H_
#define _NTDDSTOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(DEFINE_GUID)

DEFINE_GUID(GUID_DEVINTERFACE_DISK,
  0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_CDROM,
  0x53f56308L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_PARTITION,
  0x53f5630aL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_TAPE,
  0x53f5630bL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_WRITEONCEDISK,
  0x53f5630cL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_VOLUME,
  0x53f5630dL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_MEDIUMCHANGER,
  0x53f56310L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_FLOPPY,
  0x53f56311L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_CDCHANGER,
  0x53f56312L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_STORAGEPORT,
  0x2accfe60L, 0xc130, 0x11d2, 0xb0, 0x82, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

DEFINE_GUID(GUID_DEVINTERFACE_HIDDEN_VOLUME,
  0x7f108a28L, 0x9833, 0x4b3b, 0xb7, 0x80, 0x2c, 0x6b, 0x5f, 0xa5, 0xc0, 0x62);

#define WDI_STORAGE_PREDICT_FAILURE_DPS_GUID \
  {0xe9f2d03aL, 0x747c, 0x41c2, {0xbb, 0x9a, 0x02, 0xc6, 0x2b, 0x6d, 0x5f, 0xcb}};

/* Aliases for storage guids */
#define DiskClassGuid               GUID_DEVINTERFACE_DISK
#define CdRomClassGuid              GUID_DEVINTERFACE_CDROM
#define PartitionClassGuid          GUID_DEVINTERFACE_PARTITION
#define TapeClassGuid               GUID_DEVINTERFACE_TAPE
#define WriteOnceDiskClassGuid      GUID_DEVINTERFACE_WRITEONCEDISK
#define VolumeClassGuid             GUID_DEVINTERFACE_VOLUME
#define MediumChangerClassGuid      GUID_DEVINTERFACE_MEDIUMCHANGER
#define FloppyClassGuid             GUID_DEVINTERFACE_FLOPPY
#define CdChangerClassGuid          GUID_DEVINTERFACE_CDCHANGER
#define StoragePortClassGuid        GUID_DEVINTERFACE_STORAGEPORT
#define HiddenVolumeClassGuid       GUID_DEVINTERFACE_HIDDEN_VOLUME

#endif /* defined(DEFINE_GUID) */

#if defined(DEFINE_DEVPROPKEY)
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Portable,           0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 2);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Removable_Media,    0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 3);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_System_Critical,    0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 4);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Disk_Number,        0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 5);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Partition_Number,   0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 6);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Mbr_Type,           0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 7);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Gpt_Type,           0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 8);
DEFINE_DEVPROPKEY(DEVPKEY_Storage_Gpt_Name,           0x4d1ebee8, 0x803, 0x4774, 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x2, 0x65, 0xe9, 9);
#endif

#ifndef _WINIOCTL_

#define IOCTL_STORAGE_BASE                FILE_DEVICE_MASS_STORAGE

#define IOCTL_STORAGE_CHECK_VERIFY \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0200, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_CHECK_VERIFY2 \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0200, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_MEDIA_REMOVAL \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0201, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_EJECT_MEDIA \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0202, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_LOAD_MEDIA \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0203, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_LOAD_MEDIA2 \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0203, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_RESERVE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0204, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_RELEASE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0205, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_FIND_NEW_DEVICES \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0206, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_EJECTION_CONTROL \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0250, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_MCN_CONTROL \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0251, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_MEDIA_TYPES \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0300, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_MEDIA_TYPES_EX \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0301, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_RESET_BUS \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0400, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_RESET_DEVICE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_GET_DEVICE_NUMBER \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0420, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_PREDICT_FAILURE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0440, METHOD_BUFFERED, FILE_ANY_ACCESS)

#endif /* _WINIOCTL_ */

#define IOCTL_STORAGE_GET_MEDIA_SERIAL_NUMBER \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0304, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_HOTPLUG_INFO \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0305, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_SET_HOTPLUG_INFO \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0306, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define OBSOLETE_IOCTL_STORAGE_RESET_BUS \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0400, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define OBSOLETE_IOCTL_STORAGE_RESET_DEVICE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_BREAK_RESERVATION \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_PERSISTENT_RESERVE_IN \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0406, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_PERSISTENT_RESERVE_OUT \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0407, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_READ_CAPACITY \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0450, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_SET_TEMPERATURE_THRESHOLD \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0480, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_QUERY_PROPERTY \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0501, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_GET_LB_PROVISIONING_MAP_RESOURCES \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0502, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_GET_BC_PROPERTIES \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0600, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_STORAGE_ALLOCATE_BC_STREAM \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0601, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_FREE_BC_STREAM \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0602, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_CHECK_PRIORITY_HINT_SUPPORT \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0620, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_START_DATA_INTEGRITY_CHECK \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0621, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_STOP_DATA_INTEGRITY_CHECK  \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0622, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_FIRMWARE_GET_INFO \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0700, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_FIRMWARE_DOWNLOAD \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0701, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_FIRMWARE_ACTIVATE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0702, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_ENABLE_IDLE_POWER \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0720, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_IDLE_POWERUP_REASON \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0721, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_POWER_ACTIVE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0722, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_POWER_IDLE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0723, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_EVENT_NOTIFICATION \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0724, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_DEVICE_POWER_CAP \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0725, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_RPMB_COMMAND \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0726, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_ATTRIBUTE_MANAGEMENT \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0727, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_STORAGE_DIAGNOSTIC \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0728, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_PHYSICAL_ELEMENT_STATUS \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0729, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_REMOVE_ELEMENT_AND_TRUNCATE \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0730, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_STORAGE_GET_DEVICE_INTERNAL_LOG \
  CTL_CODE(IOCTL_STORAGE_BASE, 0x0731, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define RECOVERED_WRITES_VALID         0x00000001
#define UNRECOVERED_WRITES_VALID       0x00000002
#define RECOVERED_READS_VALID          0x00000004
#define UNRECOVERED_READS_VALID        0x00000008
#define WRITE_COMPRESSION_INFO_VALID   0x00000010
#define READ_COMPRESSION_INFO_VALID    0x00000020

#define TAPE_RETURN_STATISTICS         0L
#define TAPE_RETURN_ENV_INFO           1L
#define TAPE_RESET_STATISTICS          2L

/* DEVICE_MEDIA_INFO.DeviceSpecific.DiskInfo.MediaCharacteristics constants */
#define MEDIA_ERASEABLE                   0x00000001
#define MEDIA_WRITE_ONCE                  0x00000002
#define MEDIA_READ_ONLY                   0x00000004
#define MEDIA_READ_WRITE                  0x00000008
#define MEDIA_WRITE_PROTECTED             0x00000100
#define MEDIA_CURRENTLY_MOUNTED           0x80000000

#define StorageIdTypeNAA StorageIdTypeFCPHName

#define DeviceDsmActionFlag_NonDestructive  0x80000000

#define IsDsmActionNonDestructive(_Action) ((BOOLEAN)((_Action & DeviceDsmActionFlag_NonDestructive) != 0))

#define DeviceDsmAction_None                    0x0u
#define DeviceDsmAction_Trim                    0x1u
#define DeviceDsmAction_Notification            (0x00000002u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_OffloadRead             (0x00000003u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_OffloadWrite            (0x00000004u)
#define DeviceDsmAction_Allocation              (0x00000005u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_Repair                  (0x00000006u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_Scrub                   (0x00000007u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_DrtQuery                (0x00000008u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_DrtClear                (0x00000009u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_DrtDisable              (0x0000000Au | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_TieringQuery            (0x0000000Bu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_Map                     (0x0000000Cu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_RegenerateParity        (0x0000000Du | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_NvCache_Change_Priority (0x0000000Eu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_NvCache_Evict           (0x0000000Fu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_TopologyIdQuery         (0x00000010u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_GetPhysicalAddresses    (0x00000011u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_ScopeRegen              (0x00000012u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_ReportZones             (0x00000013u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_OpenZone                (0x00000014u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_FinishZone              (0x00000015u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_CloseZone               (0x00000016u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_ResetWritePointer       (0x00000017u)
#define DeviceDsmAction_GetRangeErrorInfo       (0x00000018u | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_WriteZeroes             (0x00000019u)
#define DeviceDsmAction_LostQuery               (0x0000001Au | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_GetFreeSpace            (0x0000001Bu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_ConversionQuery         (0x0000001Cu | DeviceDsmActionFlag_NonDestructive)
#define DeviceDsmAction_VdtSet                  (0x0000001Du)

#define DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE    0x00000001

#define DEVICE_DSM_NOTIFY_FLAG_BEGIN             0x00000001
#define DEVICE_DSM_NOTIFY_FLAG_END               0x00000002

#define IOCTL_STORAGE_BC_VERSION                 1

#define STORAGE_PRIORITY_HINT_SUPPORTED          0x0001

typedef struct _STORAGE_HOTPLUG_INFO {
  ULONG Size;
  BOOLEAN MediaRemovable;
  BOOLEAN MediaHotplug;
  BOOLEAN DeviceHotplug;
  BOOLEAN WriteCacheEnableOverride;
} STORAGE_HOTPLUG_INFO, *PSTORAGE_HOTPLUG_INFO;

typedef struct _STORAGE_DEVICE_NUMBER {
  DEVICE_TYPE DeviceType;
  ULONG DeviceNumber;
  ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER, *PSTORAGE_DEVICE_NUMBER;

typedef struct _STORAGE_BUS_RESET_REQUEST {
  UCHAR PathId;
} STORAGE_BUS_RESET_REQUEST, *PSTORAGE_BUS_RESET_REQUEST;

typedef struct _STORAGE_BREAK_RESERVATION_REQUEST {
  ULONG Length;
  UCHAR _unused;
  UCHAR PathId;
  UCHAR TargetId;
  UCHAR Lun;
} STORAGE_BREAK_RESERVATION_REQUEST, *PSTORAGE_BREAK_RESERVATION_REQUEST;

#ifndef _WINIOCTL_
typedef struct _PREVENT_MEDIA_REMOVAL {
  BOOLEAN PreventMediaRemoval;
} PREVENT_MEDIA_REMOVAL, *PPREVENT_MEDIA_REMOVAL;
#endif

typedef struct _CLASS_MEDIA_CHANGE_CONTEXT {
  ULONG MediaChangeCount;
  ULONG NewState;
} CLASS_MEDIA_CHANGE_CONTEXT, *PCLASS_MEDIA_CHANGE_CONTEXT;

typedef struct _TAPE_STATISTICS {
  ULONG Version;
  ULONG Flags;
  LARGE_INTEGER RecoveredWrites;
  LARGE_INTEGER UnrecoveredWrites;
  LARGE_INTEGER RecoveredReads;
  LARGE_INTEGER UnrecoveredReads;
  UCHAR CompressionRatioReads;
  UCHAR CompressionRatioWrites;
} TAPE_STATISTICS, *PTAPE_STATISTICS;

typedef struct _TAPE_GET_STATISTICS {
  ULONG Operation;
} TAPE_GET_STATISTICS, *PTAPE_GET_STATISTICS;

typedef enum _STORAGE_MEDIA_TYPE {
  DDS_4mm = 0x20,
  MiniQic,
  Travan,
  QIC,
  MP_8mm,
  AME_8mm,
  AIT1_8mm,
  DLT,
  NCTP,
  IBM_3480,
  IBM_3490E,
  IBM_Magstar_3590,
  IBM_Magstar_MP,
  STK_DATA_D3,
  SONY_DTF,
  DV_6mm,
  DMI,
  SONY_D2,
  CLEANER_CARTRIDGE,
  CD_ROM,
  CD_R,
  CD_RW,
  DVD_ROM,
  DVD_R,
  DVD_RW,
  MO_3_RW,
  MO_5_WO,
  MO_5_RW,
  MO_5_LIMDOW,
  PC_5_WO,
  PC_5_RW,
  PD_5_RW,
  ABL_5_WO,
  PINNACLE_APEX_5_RW,
  SONY_12_WO,
  PHILIPS_12_WO,
  HITACHI_12_WO,
  CYGNET_12_WO,
  KODAK_14_WO,
  MO_NFR_525,
  NIKON_12_RW,
  IOMEGA_ZIP,
  IOMEGA_JAZ,
  SYQUEST_EZ135,
  SYQUEST_EZFLYER,
  SYQUEST_SYJET,
  AVATAR_F2,
  MP2_8mm,
  DST_S,
  DST_M,
  DST_L,
  VXATape_1,
  VXATape_2,
#if (NTDDI_VERSION < NTDDI_WINXP)
  STK_EAGLE,
#else
  STK_9840,
#endif
  LTO_Ultrium,
  LTO_Accelis,
  DVD_RAM,
  AIT_8mm,
  ADR_1,
  ADR_2,
  STK_9940,
  SAIT,
  VXATape
} STORAGE_MEDIA_TYPE, *PSTORAGE_MEDIA_TYPE;

typedef enum _STORAGE_BUS_TYPE {
  BusTypeUnknown = 0x00,
  BusTypeScsi,
  BusTypeAtapi,
  BusTypeAta,
  BusType1394,
  BusTypeSsa,
  BusTypeFibre,
  BusTypeUsb,
  BusTypeRAID,
  BusTypeiScsi,
  BusTypeSas,
  BusTypeSata,
  BusTypeSd,
  BusTypeMmc,
  BusTypeVirtual,
  BusTypeFileBackedVirtual,
  BusTypeSpaces,
  BusTypeNvme,
  BusTypeSCM,
  BusTypeUfs,
  BusTypeNvmeof,
  BusTypeMax,
  BusTypeMaxReserved = 0x7F
} STORAGE_BUS_TYPE, *PSTORAGE_BUS_TYPE;

typedef struct _DEVICE_MEDIA_INFO {
  union {
    struct {
      LARGE_INTEGER Cylinders;
      STORAGE_MEDIA_TYPE MediaType;
      ULONG TracksPerCylinder;
      ULONG SectorsPerTrack;
      ULONG BytesPerSector;
      ULONG NumberMediaSides;
      ULONG MediaCharacteristics;
    } DiskInfo;
    struct {
      LARGE_INTEGER Cylinders;
      STORAGE_MEDIA_TYPE MediaType;
      ULONG TracksPerCylinder;
      ULONG SectorsPerTrack;
      ULONG BytesPerSector;
      ULONG NumberMediaSides;
      ULONG MediaCharacteristics;
    } RemovableDiskInfo;
    struct {
      STORAGE_MEDIA_TYPE MediaType;
      ULONG MediaCharacteristics;
      ULONG CurrentBlockSize;
      STORAGE_BUS_TYPE BusType;
      union {
        struct {
          UCHAR MediumType;
          UCHAR DensityCode;
        } ScsiInformation;
      } BusSpecificData;
    } TapeInfo;
  } DeviceSpecific;
} DEVICE_MEDIA_INFO, *PDEVICE_MEDIA_INFO;

typedef struct _GET_MEDIA_TYPES {
  ULONG DeviceType;
  ULONG MediaInfoCount;
  DEVICE_MEDIA_INFO MediaInfo[1];
} GET_MEDIA_TYPES, *PGET_MEDIA_TYPES;

typedef struct _STORAGE_PREDICT_FAILURE {
  ULONG PredictFailure;
  UCHAR VendorSpecific[512];
} STORAGE_PREDICT_FAILURE, *PSTORAGE_PREDICT_FAILURE;

typedef enum _STORAGE_QUERY_TYPE {
  PropertyStandardQuery = 0,
  PropertyExistsQuery,
  PropertyMaskQuery,
  PropertyQueryMaxDefined
} STORAGE_QUERY_TYPE, *PSTORAGE_QUERY_TYPE;

typedef enum _STORAGE_PROPERTY_ID {
  StorageDeviceProperty = 0,
  StorageAdapterProperty,
  StorageDeviceIdProperty,
  StorageDeviceUniqueIdProperty,
  StorageDeviceWriteCacheProperty,
  StorageMiniportProperty,
  StorageAccessAlignmentProperty,
  StorageDeviceSeekPenaltyProperty,
  StorageDeviceTrimProperty,
  StorageDeviceWriteAggregationProperty,
  StorageDeviceDeviceTelemetryProperty,
  StorageDeviceLBProvisioningProperty,
  StorageDevicePowerProperty,
  StorageDeviceCopyOffloadProperty,
  StorageDeviceResiliencyProperty,
  StorageDeviceMediumProductType,
  StorageAdapterRpmbProperty,
  StorageAdapterCryptoProperty,
  StorageDeviceTieringProperty,
  StorageDeviceFaultDomainProperty,
  StorageDeviceClusportProperty,
  StorageDeviceDependantDevicesProperty,
  StorageDeviceIoCapabilityProperty = 48,
  StorageAdapterProtocolSpecificProperty,
  StorageDeviceProtocolSpecificProperty,
  StorageAdapterTemperatureProperty,
  StorageDeviceTemperatureProperty,
  StorageAdapterPhysicalTopologyProperty,
  StorageDevicePhysicalTopologyProperty,
  StorageDeviceAttributesProperty,
  StorageDeviceManagementStatus,
  StorageAdapterSerialNumberProperty,
  StorageDeviceLocationProperty,
  StorageDeviceNumaProperty,
  StorageDeviceZonedDeviceProperty,
  StorageDeviceUnsafeShutdownCount,
  StorageDeviceEnduranceProperty,
} STORAGE_PROPERTY_ID, *PSTORAGE_PROPERTY_ID;

typedef struct _STORAGE_PROPERTY_QUERY {
  STORAGE_PROPERTY_ID PropertyId;
  STORAGE_QUERY_TYPE QueryType;
  UCHAR AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY;

typedef _Struct_size_bytes_(Size) struct _STORAGE_DESCRIPTOR_HEADER {
  ULONG Version;
  ULONG Size;
} STORAGE_DESCRIPTOR_HEADER, *PSTORAGE_DESCRIPTOR_HEADER;

#define STORAGE_TEMPERATURE_VALUE_NOT_REPORTED 0x8000

typedef struct _STORAGE_TEMPERATURE_INFO {
  USHORT Index;
  SHORT Temperature;
  SHORT OverThreshold;
  SHORT UnderThreshold;
  BOOLEAN OverThresholdChangable;
  BOOLEAN UnderThresholdChangable;
  BOOLEAN EventGenerated;
  UCHAR Reserved0;
  ULONG Reserved1;
} STORAGE_TEMPERATURE_INFO, *PSTORAGE_TEMPERATURE_INFO;

typedef struct _STORAGE_TEMPERATURE_DATA_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  SHORT CriticalTemperature;
  SHORT WarningTemperature;
  USHORT InfoCount;
  UCHAR Reserved0[2];
  ULONG Reserved1[2];
  STORAGE_TEMPERATURE_INFO TemperatureInfo[ANYSIZE_ARRAY];
} STORAGE_TEMPERATURE_DATA_DESCRIPTOR, *PSTORAGE_TEMPERATURE_DATA_DESCRIPTOR;

#define STORAGE_TEMPERATURE_THRESHOLD_FLAG_ADAPTER_REQUEST 0x0001

typedef struct _STORAGE_TEMPERATURE_THRESHOLD {
  ULONG Version;
  ULONG Size;
  USHORT Flags;
  USHORT Index;
  SHORT Threshold;
  BOOLEAN OverThreshold;
  UCHAR Reserved;
} STORAGE_TEMPERATURE_THRESHOLD, *PSTORAGE_TEMPERATURE_THRESHOLD;

typedef _Struct_size_bytes_(Size) struct _STORAGE_DEVICE_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  UCHAR DeviceType;
  UCHAR DeviceTypeModifier;
  BOOLEAN RemovableMedia;
  BOOLEAN CommandQueueing;
  ULONG VendorIdOffset;
  ULONG ProductIdOffset;
  ULONG ProductRevisionOffset;
  ULONG SerialNumberOffset;
  STORAGE_BUS_TYPE BusType;
  ULONG RawPropertiesLength;
  UCHAR RawDeviceProperties[1];
} STORAGE_DEVICE_DESCRIPTOR, *PSTORAGE_DEVICE_DESCRIPTOR;

typedef _Struct_size_bytes_(Size) struct _STORAGE_ADAPTER_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG MaximumTransferLength;
  ULONG MaximumPhysicalPages;
  ULONG AlignmentMask;
  BOOLEAN AdapterUsesPio;
  BOOLEAN AdapterScansDown;
  BOOLEAN CommandQueueing;
  BOOLEAN AcceleratedTransfer;
#if (NTDDI_VERSION < NTDDI_WINXP)
  BOOLEAN BusType;
#else
  UCHAR BusType;
#endif
  USHORT BusMajorVersion;
  USHORT BusMinorVersion;
#if (NTDDI_VERSION >= NTDDI_WIN8)
  UCHAR SrbType;
  UCHAR AddressType;
#endif
} STORAGE_ADAPTER_DESCRIPTOR, *PSTORAGE_ADAPTER_DESCRIPTOR;

typedef _Struct_size_bytes_(Size) struct _STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG BytesPerCacheLine;
  ULONG BytesOffsetForCacheAlignment;
  ULONG BytesPerLogicalSector;
  ULONG BytesPerPhysicalSector;
  ULONG BytesOffsetForSectorAlignment;
} STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR, *PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR;

typedef _Struct_size_bytes_(Size) struct _STORAGE_MEDIUM_PRODUCT_TYPE_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG MediumProductType;
} STORAGE_MEDIUM_PRODUCT_TYPE_DESCRIPTOR, *PSTORAGE_MEDIUM_PRODUCT_TYPE_DESCRIPTOR;

typedef enum _STORAGE_PORT_CODE_SET {
  StoragePortCodeSetReserved = 0,
  StoragePortCodeSetStorport = 1,
  StoragePortCodeSetSCSIport = 2,
  StoragePortCodeSetSpaceport= 3,
  StoragePortCodeSetATAport  = 4,
  StoragePortCodeSetUSBport  = 5,
  StoragePortCodeSetSBP2port = 6,
  StoragePortCodeSetSDport   = 7
} STORAGE_PORT_CODE_SET, *PSTORAGE_PORT_CODE_SET;

typedef struct _STORAGE_MINIPORT_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  STORAGE_PORT_CODE_SET Portdriver;
  BOOLEAN LUNResetSupported;
  BOOLEAN TargetResetSupported;
#if (NTDDI_VERSION >= NTDDI_WIN8)
  USHORT IoTimeoutValue;
#endif
#if (NTDDI_VERSION >= NTDDI_WINBLUE)
  BOOLEAN ExtraIoInfoSupported;
#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
  union {
    _ANONYMOUS_STRUCT struct {
      UCHAR LogicalPoFxForDisk:1;
      UCHAR ForwardIo:1;
      UCHAR Reserved:6;
    } DUMMYSTRUCTNAME;
    UCHAR AsUCHAR;
  } Flags;
  UCHAR Reserved0[2];
#else
  UCHAR Reserved0[3];
#endif
  ULONG Reserved1;
#endif
} STORAGE_MINIPORT_DESCRIPTOR, *PSTORAGE_MINIPORT_DESCRIPTOR;

typedef struct _DEVICE_LB_PROVISIONING_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  UCHAR ThinProvisioningEnabled:1;
  UCHAR ThinProvisioningReadZeros:1;
  UCHAR AnchorSupported:3;
  UCHAR UnmapGranularityAlignmentValid:1;
  UCHAR Reserved0:2;
  UCHAR Reserved1[7];
  ULONGLONG OptimalUnmapGranularity;
  ULONGLONG UnmapGranularityAlignment;
#if (NTDDI_VERSION >= NTDDI_WINBLUE)
  ULONG MaxUnmapLbaCount;
  ULONG MaxUnmapBlockDescriptorCount;
#endif
} DEVICE_LB_PROVISIONING_DESCRIPTOR, *PDEVICE_LB_PROVISIONING_DESCRIPTOR;

#define DEVICE_LB_PROVISIONING_DESCRIPTOR_V1_SIZE RTL_SIZEOF_THROUGH_FIELD(DEVICE_LB_PROVISIONING_DESCRIPTOR, UnmapGranularityAlignment)

typedef struct _DEVICE_POWER_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  BOOLEAN DeviceAttentionSupported;
  BOOLEAN AsynchronousNotificationSupported;
  BOOLEAN IdlePowerManagementEnabled;
  BOOLEAN D3ColdEnabled;
  BOOLEAN D3ColdSupported;
  BOOLEAN NoVerifyDuringIdlePower;
  UCHAR Reserved[2];
  ULONG IdleTimeoutInMS;
} DEVICE_POWER_DESCRIPTOR, *PDEVICE_POWER_DESCRIPTOR;

typedef struct _DEVICE_COPY_OFFLOAD_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG MaximumTokenLifetime;
  ULONG DefaultTokenLifetime;
  ULONGLONG MaximumTransferSize;
  ULONGLONG OptimalTransferCount;
  ULONG MaximumDataDescriptors;
  ULONG MaximumTransferLengthPerDescriptor;
  ULONG OptimalTransferLengthPerDescriptor;
  USHORT OptimalTransferLengthGranularity;
  UCHAR Reserved[2];
} DEVICE_COPY_OFFLOAD_DESCRIPTOR, *PDEVICE_COPY_OFFLOAD_DESCRIPTOR;

typedef _Struct_size_bytes_(Size) struct _STORAGE_DEVICE_IO_CAPABILITY_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG LunMaxIoCount;
  ULONG AdapterMaxIoCount;
} STORAGE_DEVICE_IO_CAPABILITY_DESCRIPTOR, *PSTORAGE_DEVICE_IO_CAPABILITY_DESCRIPTOR;

typedef enum _STORAGE_IDENTIFIER_CODE_SET {
  StorageIdCodeSetReserved = 0,
  StorageIdCodeSetBinary = 1,
  StorageIdCodeSetAscii = 2,
  StorageIdCodeSetUtf8 = 3
} STORAGE_IDENTIFIER_CODE_SET, *PSTORAGE_IDENTIFIER_CODE_SET;

typedef enum _STORAGE_IDENTIFIER_TYPE {
  StorageIdTypeVendorSpecific = 0,
  StorageIdTypeVendorId = 1,
  StorageIdTypeEUI64 = 2,
  StorageIdTypeFCPHName = 3,
  StorageIdTypePortRelative = 4,
  StorageIdTypeTargetPortGroup = 5,
  StorageIdTypeLogicalUnitGroup = 6,
  StorageIdTypeMD5LogicalUnitIdentifier = 7,
  StorageIdTypeScsiNameString = 8
} STORAGE_IDENTIFIER_TYPE, *PSTORAGE_IDENTIFIER_TYPE;

typedef enum _STORAGE_ID_NAA_FORMAT {
  StorageIdNAAFormatIEEEExtended = 2,
  StorageIdNAAFormatIEEERegistered = 3,
  StorageIdNAAFormatIEEEERegisteredExtended = 5
} STORAGE_ID_NAA_FORMAT, *PSTORAGE_ID_NAA_FORMAT;

typedef enum _STORAGE_ASSOCIATION_TYPE {
  StorageIdAssocDevice = 0,
  StorageIdAssocPort = 1,
  StorageIdAssocTarget = 2
} STORAGE_ASSOCIATION_TYPE, *PSTORAGE_ASSOCIATION_TYPE;

typedef struct _STORAGE_IDENTIFIER {
  STORAGE_IDENTIFIER_CODE_SET CodeSet;
  STORAGE_IDENTIFIER_TYPE Type;
  USHORT IdentifierSize;
  USHORT NextOffset;
  STORAGE_ASSOCIATION_TYPE Association;
  UCHAR Identifier[1];
} STORAGE_IDENTIFIER, *PSTORAGE_IDENTIFIER;

typedef _Struct_size_bytes_(Size) struct _STORAGE_DEVICE_ID_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG NumberOfIdentifiers;
  UCHAR Identifiers[1];
} STORAGE_DEVICE_ID_DESCRIPTOR, *PSTORAGE_DEVICE_ID_DESCRIPTOR;

typedef struct _DEVICE_SEEK_PENALTY_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  BOOLEAN IncursSeekPenalty;
} DEVICE_SEEK_PENALTY_DESCRIPTOR, *PDEVICE_SEEK_PENALTY_DESCRIPTOR;

typedef struct _DEVICE_WRITE_AGGREGATION_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  BOOLEAN BenefitsFromWriteAggregation;
} DEVICE_WRITE_AGGREGATION_DESCRIPTOR, *PDEVICE_WRITE_AGGREGATION_DESCRIPTOR;

typedef struct _DEVICE_TRIM_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  BOOLEAN TrimEnabled;
} DEVICE_TRIM_DESCRIPTOR, *PDEVICE_TRIM_DESCRIPTOR;

typedef ULONG DEVICE_DATA_MANAGEMENT_SET_ACTION;

#define DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED 0x80000000

typedef struct _DEVICE_DATA_SET_RANGE {
  LONGLONG StartingOffset;
  ULONGLONG LengthInBytes;
} DEVICE_DATA_SET_RANGE, *PDEVICE_DATA_SET_RANGE;

typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES {
  ULONG Size;
  DEVICE_DATA_MANAGEMENT_SET_ACTION Action;
  ULONG Flags;
  ULONG ParameterBlockOffset;
  ULONG ParameterBlockLength;
  ULONG DataSetRangesOffset;
  ULONG DataSetRangesLength;
} DEVICE_MANAGE_DATA_SET_ATTRIBUTES, *PDEVICE_MANAGE_DATA_SET_ATTRIBUTES;

typedef struct _DEVICE_DSM_NOTIFICATION_PARAMETERS {
  ULONG Size;
  ULONG Flags;
  ULONG NumFileTypeIDs;
  GUID FileTypeID[1];
} DEVICE_DSM_NOTIFICATION_PARAMETERS, *PDEVICE_DSM_NOTIFICATION_PARAMETERS;

typedef struct _STORAGE_GET_BC_PROPERTIES_OUTPUT {
  ULONG MaximumRequestsPerPeriod;
  ULONG MinimumPeriod;
  ULONGLONG MaximumRequestSize;
  ULONG EstimatedTimePerRequest;
  ULONG NumOutStandingRequests;
  ULONGLONG RequestSize;
} STORAGE_GET_BC_PROPERTIES_OUTPUT, *PSTORAGE_GET_BC_PROPERTIES_OUTPUT;

typedef struct _STORAGE_ALLOCATE_BC_STREAM_INPUT {
  ULONG Version;
  ULONG RequestsPerPeriod;
  ULONG Period;
  BOOLEAN RetryFailures;
  BOOLEAN Discardable;
  BOOLEAN Reserved1[2];
  ULONG AccessType;
  ULONG AccessMode;
} STORAGE_ALLOCATE_BC_STREAM_INPUT, *PSTORAGE_ALLOCATE_BC_STREAM_INPUT;

typedef struct _STORAGE_ALLOCATE_BC_STREAM_OUTPUT {
  ULONGLONG RequestSize;
  ULONG NumOutStandingRequests;
} STORAGE_ALLOCATE_BC_STREAM_OUTPUT, *PSTORAGE_ALLOCATE_BC_STREAM_OUTPUT;

typedef struct _STORAGE_PRIORITY_HINT_SUPPORT {
  ULONG SupportFlags;
} STORAGE_PRIORITY_HINT_SUPPORT, *PSTORAGE_PRIORITY_HINT_SUPPORT;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4200)
#endif

// #if defined(_MSC_EXTENSIONS)

typedef struct _STORAGE_MEDIA_SERIAL_NUMBER_DATA {
  USHORT Reserved;
  USHORT SerialNumberLength;
  UCHAR SerialNumber[0];
} STORAGE_MEDIA_SERIAL_NUMBER_DATA, *PSTORAGE_MEDIA_SERIAL_NUMBER_DATA;

typedef struct _PERSISTENT_RESERVE_COMMAND {
  ULONG Version;
  ULONG Size;
  __MINGW_EXTENSION union {
    struct {
      UCHAR ServiceAction:5;
      UCHAR Reserved1:3;
      USHORT AllocationLength;
    } PR_IN;
    struct {
      UCHAR ServiceAction:5;
      UCHAR Reserved1:3;
      UCHAR Type:4;
      UCHAR Scope:4;
      UCHAR ParameterList[0];
    } PR_OUT;
  } DUMMYUNIONNAME;
} PERSISTENT_RESERVE_COMMAND, *PPERSISTENT_RESERVE_COMMAND;

// #endif /* defined(_MSC_EXTENSIONS) */

#ifdef _MSC_VER
#pragma warning(pop) /* disable:4200 */
#endif

typedef _Struct_size_bytes_(Size) struct _STORAGE_READ_CAPACITY {
  ULONG Version;
  ULONG Size;
  ULONG BlockLength;
  LARGE_INTEGER NumberOfBlocks;
  LARGE_INTEGER DiskLength;
} STORAGE_READ_CAPACITY, *PSTORAGE_READ_CAPACITY;

typedef enum _WRITE_CACHE_TYPE {
  WriteCacheTypeUnknown,
  WriteCacheTypeNone,
  WriteCacheTypeWriteBack,
  WriteCacheTypeWriteThrough
} WRITE_CACHE_TYPE;

typedef enum _WRITE_CACHE_ENABLE {
  WriteCacheEnableUnknown,
  WriteCacheDisabled,
  WriteCacheEnabled
} WRITE_CACHE_ENABLE;

typedef enum _WRITE_CACHE_CHANGE {
  WriteCacheChangeUnknown,
  WriteCacheNotChangeable,
  WriteCacheChangeable
} WRITE_CACHE_CHANGE;

typedef enum _WRITE_THROUGH {
  WriteThroughUnknown,
  WriteThroughNotSupported,
  WriteThroughSupported
} WRITE_THROUGH;

typedef _Struct_size_bytes_(Size) struct _STORAGE_WRITE_CACHE_PROPERTY {
  ULONG Version;
  ULONG Size;
  WRITE_CACHE_TYPE WriteCacheType;
  WRITE_CACHE_ENABLE WriteCacheEnabled;
  WRITE_CACHE_CHANGE WriteCacheChangeable;
  WRITE_THROUGH WriteThroughSupported;
  BOOLEAN FlushCacheSupported;
  BOOLEAN UserDefinedPowerProtection;
  BOOLEAN NVCacheEnabled;
} STORAGE_WRITE_CACHE_PROPERTY, *PSTORAGE_WRITE_CACHE_PROPERTY;

typedef struct _STORAGE_LB_PROVISIONING_MAP_RESOURCES {
  ULONG Size;
  ULONG Version;
  UCHAR AvailableMappingResourcesValid:1;
  UCHAR UsedMappingResourcesValid:1;
  UCHAR Reserved0:6;
  UCHAR Reserved1[3];
  UCHAR AvailableMappingResourcesScope:2;
  UCHAR UsedMappingResourcesScope:2;
  UCHAR Reserved2:4;
  UCHAR Reserved3[3];
  ULONGLONG AvailableMappingResources;
  ULONGLONG UsedMappingResources;
} STORAGE_LB_PROVISIONING_MAP_RESOURCES, *PSTORAGE_LB_PROVISIONING_MAP_RESOURCES;

typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT {
  ULONG Size;
  DEVICE_DATA_MANAGEMENT_SET_ACTION Action;
  ULONG Flags;
  ULONG OperationStatus;
  ULONG ExtendedError;
  ULONG TargetDetailedError;
  ULONG ReservedStatus;
  ULONG OutputBlockOffset;
  ULONG OutputBlockLength;
} DEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT, *PDEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT;

typedef struct _DEVICE_DATA_SET_LB_PROVISIONING_STATE {
  ULONG Size;
  ULONG Version;
  ULONGLONG SlabSizeInBytes;
  ULONG SlabOffsetDeltaInBytes;
  ULONG SlabAllocationBitMapBitCount;
  ULONG SlabAllocationBitMapLength;
  ULONG SlabAllocationBitMap[ANYSIZE_ARRAY];
} DEVICE_DATA_SET_LB_PROVISIONING_STATE, *PDEVICE_DATA_SET_LB_PROVISIONING_STATE,
  DEVICE_DSM_ALLOCATION_OUTPUT, *PDEVICE_DSM_ALLOCATION_OUTPUT;

#define DEVICE_DSM_ALLOCATION_OUTPUT_V1 (sizeof(DEVICE_DSM_ALLOCATION_OUTPUT))
#define DEVICE_DATA_SET_LB_PROVISIONING_STATE_VERSION_V1 DEVICE_DSM_ALLOCATION_OUTPUT_V1

typedef struct _DEVICE_DATA_SET_LB_PROVISIONING_STATE_V2 {
  ULONG Size;
  ULONG Version;
  ULONGLONG SlabSizeInBytes;
  ULONGLONG SlabOffsetDeltaInBytes;
  ULONG SlabAllocationBitMapBitCount;
  ULONG SlabAllocationBitMapLength;
  ULONG SlabAllocationBitMap[ANYSIZE_ARRAY];
} DEVICE_DATA_SET_LB_PROVISIONING_STATE_V2, *PDEVICE_DATA_SET_LB_PROVISIONING_STATE_V2,
  DEVICE_DSM_ALLOCATION_OUTPUT2, *PDEVICE_DSM_ALLOCATION_OUTPUT2;

#define DEVICE_DSM_ALLOCATION_OUTPUT_V2 (sizeof(DEVICE_DSM_ALLOCATION_OUTPUT2))
#define DEVICE_DATA_SET_LB_PROVISIONING_STATE_VERSION_V2 DEVICE_DSM_ALLOCATION_OUTPUT_V2

#define DeviceDsmDefinition_Allocation {DeviceDsmAction_Allocation,                  \
                                        TRUE,                                        \
                                        __alignof(DEVICE_DSM_ALLOCATION_PARAMETERS), \
                                        sizeof(DEVICE_DSM_ALLOCATION_PARAMETERS),    \
                                        TRUE,                                        \
                                        __alignof(DEVICE_DSM_ALLOCATION_OUTPUT2),    \
                                        sizeof(DEVICE_DSM_ALLOCATION_OUTPUT2)}

#define DEVICE_DSM_FLAG_ALLOCATION_CONSOLIDATEABLE_ONLY 0x40000000

typedef struct _DEVICE_DATA_SET_LBP_STATE_PARAMETERS {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG OutputVersion;
} DEVICE_DATA_SET_LBP_STATE_PARAMETERS, *PDEVICE_DATA_SET_LBP_STATE_PARAMETERS,
  DEVICE_DSM_ALLOCATION_PARAMETERS, *PDEVICE_DSM_ALLOCATION_PARAMETERS;

typedef struct _STORAGE_EVENT_NOTIFICATION {
  ULONG Version;
  ULONG Size;
  ULONGLONG Events;
} STORAGE_EVENT_NOTIFICATION, *PSTORAGE_EVENT_NOTIFICATION;

#define STORAGE_EVENT_NOTIFICATION_VERSION_V1 1

#define STORAGE_EVENT_MEDIA_STATUS     0x0000000000000001
#define STORAGE_EVENT_DEVICE_STATUS    0x0000000000000002
#define STORAGE_EVENT_DEVICE_OPERATION 0x0000000000000004
#define STORAGE_EVENT_ALL (STORAGE_EVENT_MEDIA_STATUS | STORAGE_EVENT_DEVICE_STATUS | STORAGE_EVENT_DEVICE_OPERATION)

#define STORAGE_OFFLOAD_MAX_TOKEN_LENGTH        512
#define STORAGE_OFFLOAD_TOKEN_ID_LENGTH         0x1F8
#define STORAGE_OFFLOAD_TOKEN_TYPE_ZERO_DATA    0xFFFF0001

typedef struct _STORAGE_OFFLOAD_TOKEN {
  UCHAR TokenType[4];
  UCHAR Reserved[2];
  UCHAR TokenIdLength[2];
  union {
      struct {
          UCHAR Reserved2[STORAGE_OFFLOAD_TOKEN_ID_LENGTH];
      } StorageOffloadZeroDataToken;
      UCHAR Token[STORAGE_OFFLOAD_TOKEN_ID_LENGTH];
  } DUMMYUNIONNAME;
} STORAGE_OFFLOAD_TOKEN, *PSTORAGE_OFFLOAD_TOKEN;

#define MAKE_ZERO_TOKEN(T) (                                  \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[0] = 0xFF,         \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[1] = 0xFF,         \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[2] = 0x00,         \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[3] = 0x01,         \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenIdLength[0] = 0x01,     \
    ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenIdLength[1] = 0xF8      \
)

#define IS_ZERO_TOKEN(T) (                                    \
    (((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[0] == 0xFF     && \
     ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[1] == 0xFF     && \
     ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[2] == 0x00     && \
     ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenType[3] == 0x01     && \
     ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenIdLength[0] == 0x01 && \
     ((PSTORAGE_OFFLOAD_TOKEN)T)->TokenIdLength[1] == 0xF8)   \
)

typedef struct _DEVICE_DSM_OFFLOAD_READ_PARAMETERS {
  ULONG Flags;
  ULONG TimeToLive;
  ULONG Reserved[2];
} DEVICE_DSM_OFFLOAD_READ_PARAMETERS, *PDEVICE_DSM_OFFLOAD_READ_PARAMETERS;

#define STORAGE_OFFLOAD_READ_RANGE_TRUNCATED 0x00000001

typedef struct _STORAGE_OFFLOAD_READ_OUTPUT {
  ULONG OffloadReadFlags;
  ULONG Reserved;
  ULONGLONG LengthProtected;
  ULONG TokenLength;
  STORAGE_OFFLOAD_TOKEN Token;
} STORAGE_OFFLOAD_READ_OUTPUT, *PSTORAGE_OFFLOAD_READ_OUTPUT;

#define DeviceDsmDefinition_OffloadRead {DeviceDsmAction_OffloadRead,                   \
                                         FALSE,                                         \
                                         __alignof(DEVICE_DSM_OFFLOAD_READ_PARAMETERS), \
                                         sizeof(DEVICE_DSM_OFFLOAD_READ_PARAMETERS),    \
                                         FALSE,                                         \
                                         __alignof(STORAGE_OFFLOAD_READ_OUTPUT),        \
                                         sizeof(STORAGE_OFFLOAD_READ_OUTPUT)}

typedef struct _DEVICE_DSM_OFFLOAD_WRITE_PARAMETERS {
  ULONG Flags;
  ULONG Reserved;
  ULONGLONG TokenOffset;
  STORAGE_OFFLOAD_TOKEN Token;
} DEVICE_DSM_OFFLOAD_WRITE_PARAMETERS, *PDEVICE_DSM_OFFLOAD_WRITE_PARAMETERS;

#define STORAGE_OFFLOAD_WRITE_RANGE_TRUNCATED   0x0001
#define STORAGE_OFFLOAD_TOKEN_INVALID           0x0002

typedef struct _STORAGE_OFFLOAD_WRITE_OUTPUT {
  ULONG OffloadWriteFlags;
  ULONG Reserved;
  ULONGLONG LengthCopied;
} STORAGE_OFFLOAD_WRITE_OUTPUT, *PSTORAGE_OFFLOAD_WRITE_OUTPUT;

#define DeviceDsmDefinition_OffloadWrite {DeviceDsmAction_OffloadWrite,                   \
                                          FALSE,                                          \
                                          __alignof(DEVICE_DSM_OFFLOAD_WRITE_PARAMETERS), \
                                          sizeof(DEVICE_DSM_OFFLOAD_WRITE_PARAMETERS),    \
                                          FALSE,                                          \
                                          __alignof(STORAGE_OFFLOAD_WRITE_OUTPUT),        \
                                          sizeof(STORAGE_OFFLOAD_WRITE_OUTPUT)}


#define READ_COPY_NUMBER_KEY                    0x52434e00  //'RCN'
#define READ_COPY_NUMBER_BYPASS_CACHE_FLAG      0x00000100

#define IsKeyReadCopyNumber(_k)                 (((_k) & 0xFFFFFE00) == READ_COPY_NUMBER_KEY)

#define IsKeyReadCopyNumberBypassCache(_k)      ((_k) & READ_COPY_NUMBER_BYPASS_CACHE_FLAG)
#define SetReadCopyNumberBypassCacheToKey(_k)   ((_k) |= READ_COPY_NUMBER_BYPASS_CACHE_FLAG)

#define ReadCopyNumberToKey(_c)                 (READ_COPY_NUMBER_KEY | (UCHAR)(_c))
#define ReadCopyNumberFromKey(_k)               (UCHAR)((_k) & 0x000000FF)

typedef struct _STORAGE_IDLE_POWER {
  ULONG Version;
  ULONG Size;
  ULONG WakeCapableHint:1;
  ULONG D3ColdSupported:1;
  ULONG Reserved:30;
  ULONG D3IdleTimeout;
} STORAGE_IDLE_POWER, *PSTORAGE_IDLE_POWER;


// for IOCTL_STORAGE_GET_IDLE_POWERUP_REASON

typedef enum _STORAGE_POWERUP_REASON_TYPE {
  StoragePowerupUnknown = 0,
  StoragePowerupIO,
  StoragePowerupDeviceAttention
} STORAGE_POWERUP_REASON_TYPE, *PSTORAGE_POWERUP_REASON_TYPE;

typedef struct _STORAGE_IDLE_POWERUP_REASON
{
  ULONG Version;
  ULONG Size;
  STORAGE_POWERUP_REASON_TYPE PowerupReason;
} STORAGE_IDLE_POWERUP_REASON, *PSTORAGE_IDLE_POWERUP_REASON;

#define STORAGE_IDLE_POWERUP_REASON_VERSION_V1 1

#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)

#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER                     0x00000001
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT                   0x00000002
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT                  0x00000004
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE    0x80000000

typedef struct _STORAGE_HW_FIRMWARE_INFO_QUERY {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG Reserved;
} STORAGE_HW_FIRMWARE_INFO_QUERY, *PSTORAGE_HW_FIRMWARE_INFO_QUERY;

#define STORAGE_HW_FIRMWARE_INVALID_SLOT              0xFF

#define STORAGE_HW_FIRMWARE_REVISION_LENGTH             16

typedef struct _STORAGE_HW_FIRMWARE_SLOT_INFO {
  ULONG Version;
  ULONG Size;
  UCHAR SlotNumber;
  UCHAR ReadOnly:1;
  UCHAR Reserved0:7;
  UCHAR Reserved1[6];
  UCHAR Revision[STORAGE_HW_FIRMWARE_REVISION_LENGTH];
} STORAGE_HW_FIRMWARE_SLOT_INFO, *PSTORAGE_HW_FIRMWARE_SLOT_INFO;

typedef struct _STORAGE_HW_FIRMWARE_INFO {
  ULONG Version;
  ULONG Size;
  UCHAR SupportUpgrade:1;
  UCHAR Reserved0:7;
  UCHAR SlotCount;
  UCHAR ActiveSlot;
  UCHAR PendingActivateSlot;
  BOOLEAN FirmwareShared;
  UCHAR Reserved[3];
  ULONG ImagePayloadAlignment;
  ULONG ImagePayloadMaxSize;
  STORAGE_HW_FIRMWARE_SLOT_INFO Slot[ANYSIZE_ARRAY];
} STORAGE_HW_FIRMWARE_INFO, *PSTORAGE_HW_FIRMWARE_INFO;

typedef struct _STORAGE_HW_FIRMWARE_DOWNLOAD {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  UCHAR Slot;
  UCHAR Reserved[3];
  ULONGLONG Offset;
  ULONGLONG BufferSize;
  UCHAR ImageBuffer[ANYSIZE_ARRAY];
} STORAGE_HW_FIRMWARE_DOWNLOAD, *PSTORAGE_HW_FIRMWARE_DOWNLOAD;

typedef struct _STORAGE_HW_FIRMWARE_ACTIVATE {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  UCHAR Slot;
  UCHAR Reserved0[3];
} STORAGE_HW_FIRMWARE_ACTIVATE, *PSTORAGE_HW_FIRMWARE_ACTIVATE;

#endif /* NTDDI_VERSION >= NTDDI_WINTHRESHOLD */

#define IOCTL_STORAGE_FAILURE_PREDICTION_CONFIG CTL_CODE(IOCTL_STORAGE_BASE, 0x0441, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _STORAGE_FAILURE_PREDICTION_CONFIG {
    ULONG Version;
    ULONG Size;
    BOOLEAN Set;
    BOOLEAN Enabled;
    USHORT Reserved;
} STORAGE_FAILURE_PREDICTION_CONFIG, *PSTORAGE_FAILURE_PREDICTION_CONFIG;

#define STORAGE_FAILURE_PREDICTION_CONFIG_V1 1

typedef enum _STORAGE_PROTOCOL_TYPE {
    ProtocolTypeUnknown = 0x00,
    ProtocolTypeScsi,
    ProtocolTypeAta,
    ProtocolTypeNvme,
    ProtocolTypeSd,
    ProtocolTypeUfs,
    ProtocolTypeProprietary = 0x7E,
    ProtocolTypeMaxReserved = 0x7F
} STORAGE_PROTOCOL_TYPE, *PSTORAGE_PROTOCOL_TYPE;

typedef enum _STORAGE_PROTOCOL_ATA_DATA_TYPE {
    AtaDataTypeUnknown = 0,
    AtaDataTypeIdentify,
    AtaDataTypeLogPage,
} STORAGE_PROTOCOL_ATA_DATA_TYPE, *PSTORAGE_PROTOCOL_ATA_DATA_TYPE;

typedef struct _STORAGE_PROTOCOL_SPECIFIC_DATA {
    STORAGE_PROTOCOL_TYPE ProtocolType;
    ULONG   DataType;
    ULONG   ProtocolDataRequestValue;
    ULONG   ProtocolDataRequestSubValue;
    ULONG   ProtocolDataOffset;
    ULONG   ProtocolDataLength;
    ULONG   FixedProtocolReturnData;
    ULONG   ProtocolDataRequestSubValue2;
    ULONG   ProtocolDataRequestSubValue3;
    ULONG   ProtocolDataRequestSubValue4;
} STORAGE_PROTOCOL_SPECIFIC_DATA, *PSTORAGE_PROTOCOL_SPECIFIC_DATA;

typedef struct _STORAGE_PROTOCOL_DATA_DESCRIPTOR {
    ULONG   Version;
    ULONG   Size;
    STORAGE_PROTOCOL_SPECIFIC_DATA ProtocolSpecificData;
} STORAGE_PROTOCOL_DATA_DESCRIPTOR, *PSTORAGE_PROTOCOL_DATA_DESCRIPTOR;

#define STORAGE_COMPONENT_ROLE_DATA         0x00000004

typedef enum _STORAGE_DEVICE_FORM_FACTOR {
    FormFactorUnknown = 0,
    FormFactor3_5,
    FormFactor2_5,
    FormFactor1_8,
    FormFactor1_8Less,
    FormFactorEmbedded,
    FormFactorMemoryCard,
    FormFactormSata,
    FormFactorM_2,
    FormFactorPCIeBoard,
    FormFactorDimm,
} STORAGE_DEVICE_FORM_FACTOR, *PSTORAGE_DEVICE_FORM_FACTOR;

typedef enum _STORAGE_COMPONENT_HEALTH_STATUS {
    HealthStatusUnknown = 0,
    HealthStatusNormal,
    HealthStatusThrottled,
    HealthStatusWarning,
    HealthStatusDisabled,
    HealthStatusFailed,
} STORAGE_COMPONENT_HEALTH_STATUS, *PSTORAGE_COMPONENT_HEALTH_STATUS;

typedef union _STORAGE_SPEC_VERSION {
    struct {
        union {
            struct {
                UCHAR   SubMinor;
                UCHAR   Minor;
            } DUMMYSTRUCTNAME;
            USHORT  AsUshort;
        } MinorVersion;
        USHORT  MajorVersion;
    } DUMMYSTRUCTNAME;
    ULONG   AsUlong;
} STORAGE_SPEC_VERSION, *PSTORAGE_SPEC_VERSION;

typedef struct _STORAGE_PHYSICAL_DEVICE_DATA {
    ULONG       DeviceId;
    ULONG       Role;
    STORAGE_COMPONENT_HEALTH_STATUS HealthStatus;
    STORAGE_PROTOCOL_TYPE           CommandProtocol;
    STORAGE_SPEC_VERSION            SpecVersion;
    STORAGE_DEVICE_FORM_FACTOR      FormFactor;
    UCHAR       Vendor[8];
    UCHAR       Model[40];
    UCHAR       FirmwareRevision[16];
    ULONGLONG   Capacity;
    UCHAR       PhysicalLocation[32];
    ULONG       Reserved[2];
} STORAGE_PHYSICAL_DEVICE_DATA, *PSTORAGE_PHYSICAL_DEVICE_DATA;

typedef struct _STORAGE_PHYSICAL_ADAPTER_DATA {
    ULONG       AdapterId;
    STORAGE_COMPONENT_HEALTH_STATUS HealthStatus;
    STORAGE_PROTOCOL_TYPE           CommandProtocol;
    STORAGE_SPEC_VERSION            SpecVersion;
    UCHAR       Vendor[8];
    UCHAR       Model[40];
    UCHAR       FirmwareRevision[16];
    UCHAR       PhysicalLocation[32];
    BOOLEAN     ExpanderConnected;
    UCHAR       Reserved0[3];
    ULONG       Reserved1[3];
} STORAGE_PHYSICAL_ADAPTER_DATA, *PSTORAGE_PHYSICAL_ADAPTER_DATA;

typedef struct _STORAGE_PHYSICAL_NODE_DATA {
    ULONG       NodeId;
    ULONG       AdapterCount;
    ULONG       AdapterDataLength;
    ULONG       AdapterDataOffset;
    ULONG       DeviceCount;
    ULONG       DeviceDataLength;
    ULONG       DeviceDataOffset;
    ULONG       Reserved[3];
} STORAGE_PHYSICAL_NODE_DATA, *PSTORAGE_PHYSICAL_NODE_DATA;

typedef struct _STORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR {
    ULONG       Version;
    ULONG       Size;
    ULONG       NodeCount;
    ULONG       Reserved;
    STORAGE_PHYSICAL_NODE_DATA Node[ANYSIZE_ARRAY];
} STORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR, *PSTORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR;

typedef struct _DEVICE_DSM_NVCACHE_CHANGE_PRIORITY_PARAMETERS {
    ULONG Size;
    UCHAR TargetPriority;
    UCHAR Reserved[3];
} DEVICE_DSM_NVCACHE_CHANGE_PRIORITY_PARAMETERS, *PDEVICE_DSM_NVCACHE_CHANGE_PRIORITY_PARAMETERS;

#define TC_PUBLIC_DEVICEDUMP_CONTENT_GPLOG_MAX  16

#define DEVICE_DSM_PARAMETERS_V1                        1

#define DEVICE_DATA_SET_LBP_STATE_PARAMETERS_VERSION_V1 DEVICE_DSM_PARAMETERS_V1

#ifdef __cplusplus
}
#endif

#ifndef CDB_SIZE
#define CDB_SIZE 16
#endif
#ifndef DDUMP_FLAG_DATA_READ_FROM_DEVICE
#define DDUMP_FLAG_DATA_READ_FROM_DEVICE 0x0001
#endif
#ifndef DEVICEDUMP_CAP_RESTRICTED_SECTION
#define DEVICEDUMP_CAP_RESTRICTED_SECTION 0x00000002
#endif
#ifndef DEVICEDUMP_MAX_IDSTRING
#define DEVICEDUMP_MAX_IDSTRING 32
#endif
#ifndef DEVICEDUMP_STRUCTURE_VERSION_V1
#define DEVICEDUMP_STRUCTURE_VERSION_V1 1
#endif
#ifndef DEVICE_DSM_FLAG_REPAIR_INPUT_TOPOLOGY_ID_PRESENT
#define DEVICE_DSM_FLAG_REPAIR_INPUT_TOPOLOGY_ID_PRESENT 0x40000000
#endif
#ifndef DEVICE_DSM_FLAG_REPAIR_OUTPUT_PARITY_EXTENT
#define DEVICE_DSM_FLAG_REPAIR_OUTPUT_PARITY_EXTENT 0x20000000
#endif
#ifndef DEVICE_DSM_FLAG_SCRUB_OUTPUT_PARITY_EXTENT
#define DEVICE_DSM_FLAG_SCRUB_OUTPUT_PARITY_EXTENT 0x20000000
#endif
#ifndef DEVICE_DSM_FLAG_SCRUB_SKIP_IN_SYNC
#define DEVICE_DSM_FLAG_SCRUB_SKIP_IN_SYNC 0x10000000
#endif
#ifndef DEVICE_DSM_FLAG_TRIM_BYPASS_RZAT
#define DEVICE_DSM_FLAG_TRIM_BYPASS_RZAT 0x40000000
#endif
#ifndef DEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT_V1
#define DEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT_V1 1
#endif
#ifndef DEVICE_DSM_RANGE_ERROR_OUTPUT_V1
#define DEVICE_DSM_RANGE_ERROR_OUTPUT_V1 1
#endif
#ifndef ERROR_HISTORY_DIRECTORY_ENTRY_DEFAULT_COUNT
#define ERROR_HISTORY_DIRECTORY_ENTRY_DEFAULT_COUNT 8
#endif
#ifndef FW_ISSUEID_NO_ISSUE
#define FW_ISSUEID_NO_ISSUE 0x00000000
#endif
#ifndef FW_ISSUEID_UNKNOWN
#define FW_ISSUEID_UNKNOWN 0xFFFFFFFF
#endif
#ifndef MAX_FW_BUCKET_ID_LENGTH
#define MAX_FW_BUCKET_ID_LENGTH 132
#endif
#ifndef STORAGE_ADAPTER_SERIAL_NUMBER_V1_MAX_LENGTH
#define STORAGE_ADAPTER_SERIAL_NUMBER_V1_MAX_LENGTH 128
#endif
#ifndef STORAGE_ATTRIBUTE_ASYNC_EVENT_NOTIFICATION
#define STORAGE_ATTRIBUTE_ASYNC_EVENT_NOTIFICATION 0x10
#endif
#ifndef STORAGE_ATTRIBUTE_BLOCK_IO
#define STORAGE_ATTRIBUTE_BLOCK_IO 0x02
#endif
#ifndef STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO
#define STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO 0x01
#endif
#ifndef STORAGE_ATTRIBUTE_DYNAMIC_PERSISTENCE
#define STORAGE_ATTRIBUTE_DYNAMIC_PERSISTENCE 0x04
#endif
#ifndef STORAGE_ATTRIBUTE_PERF_SIZE_INDEPENDENT
#define STORAGE_ATTRIBUTE_PERF_SIZE_INDEPENDENT 0x20
#endif
#ifndef STORAGE_ATTRIBUTE_VOLATILE
#define STORAGE_ATTRIBUTE_VOLATILE 0x08
#endif
#ifndef STORAGE_COMPONENT_ROLE_CACHE
#define STORAGE_COMPONENT_ROLE_CACHE 0x00000001
#endif
#ifndef STORAGE_COMPONENT_ROLE_TIERING
#define STORAGE_COMPONENT_ROLE_TIERING 0x00000002
#endif
#ifndef STORAGE_CRYPTO_CAPABILITY_VERSION_1
#define STORAGE_CRYPTO_CAPABILITY_VERSION_1 1
#endif
#ifndef STORAGE_CRYPTO_CAPABILITY_VERSION_2
#define STORAGE_CRYPTO_CAPABILITY_VERSION_2 2
#endif
#ifndef STORAGE_CRYPTO_DESCRIPTOR_VERSION_1
#define STORAGE_CRYPTO_DESCRIPTOR_VERSION_1 1
#endif
#ifndef STORAGE_CRYPTO_DESCRIPTOR_VERSION_2
#define STORAGE_CRYPTO_DESCRIPTOR_VERSION_2 2
#endif
#ifndef STORAGE_DEVICE_FLAGS_PAGE_83_DEVICEGUID
#define STORAGE_DEVICE_FLAGS_PAGE_83_DEVICEGUID 0x4
#endif
#ifndef STORAGE_DEVICE_FLAGS_RANDOM_DEVICEGUID_REASON_CONFLICT
#define STORAGE_DEVICE_FLAGS_RANDOM_DEVICEGUID_REASON_CONFLICT 0x1
#endif
#ifndef STORAGE_DEVICE_FLAGS_RANDOM_DEVICEGUID_REASON_NOHWID
#define STORAGE_DEVICE_FLAGS_RANDOM_DEVICEGUID_REASON_NOHWID 0x2
#endif
#ifndef STORAGE_DEVICE_MAX_OPERATIONAL_STATUS
#define STORAGE_DEVICE_MAX_OPERATIONAL_STATUS 16
#endif
#ifndef STORAGE_DEVICE_POWER_CAP_VERSION_V1
#define STORAGE_DEVICE_POWER_CAP_VERSION_V1 1
#endif
#ifndef STORAGE_DIAGNOSTIC_FLAG_ADAPTER_REQUEST
#define STORAGE_DIAGNOSTIC_FLAG_ADAPTER_REQUEST 0x00000001
#endif
#ifndef STORAGE_FEATURE_SUPPORT_V1
#define STORAGE_FEATURE_SUPPORT_V1 0x1
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_ACTIVATE_STRUCTURE_VERSION
#define STORAGE_HW_BOOT_PARTITION_ACTIVATE_STRUCTURE_VERSION 0x01
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_DOWNLOAD_STRUCTURE_VERSION
#define STORAGE_HW_BOOT_PARTITION_DOWNLOAD_STRUCTURE_VERSION 0x01
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_INFO_STRUCTURE_VERSION_V1
#define STORAGE_HW_BOOT_PARTITION_INFO_STRUCTURE_VERSION_V1 0x01
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_REQUEST_ACTIVATE_EXISTING_BOOT_PARTITION
#define STORAGE_HW_BOOT_PARTITION_REQUEST_ACTIVATE_EXISTING_BOOT_PARTITION 0x80000000
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_REQUEST_FLAG_CONTROLLER
#define STORAGE_HW_BOOT_PARTITION_REQUEST_FLAG_CONTROLLER 0x00000001
#endif
#ifndef STORAGE_HW_BOOT_PARTITION_REQUEST_REPLACE_EXISTING_BOOT_PARTITION
#define STORAGE_HW_BOOT_PARTITION_REQUEST_REPLACE_EXISTING_BOOT_PARTITION 0x40000000
#endif
#ifndef STORAGE_HW_CRYPTO_CAPABILITY_VERSION_1
#define STORAGE_HW_CRYPTO_CAPABILITY_VERSION_1 1
#endif
#ifndef STORAGE_HW_CRYPTO_DESCRIPTOR_VERSION_1
#define STORAGE_HW_CRYPTO_DESCRIPTOR_VERSION_1 1
#endif
#ifndef STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET 0x20000000
#endif
#ifndef STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE 0x40000000
#endif
#ifndef STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET
#define STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET 0x10000000
#endif
#ifndef STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST
#define STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST 0x80000000
#endif
#ifndef STORAGE_PROTOCOL_COMMAND_LENGTH_NVME
#define STORAGE_PROTOCOL_COMMAND_LENGTH_NVME 0x40
#endif
#ifndef STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT_VERSION
#define STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT_VERSION 1
#endif
#ifndef STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND
#define STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND 0x01
#endif
#ifndef STORAGE_PROTOCOL_SPECIFIC_NVME_NVM_COMMAND
#define STORAGE_PROTOCOL_SPECIFIC_NVME_NVM_COMMAND 0x02
#endif
#ifndef STORAGE_PROTOCOL_STATUS_BUSY
#define STORAGE_PROTOCOL_STATUS_BUSY 0x5
#endif
#ifndef STORAGE_PROTOCOL_STATUS_DATA_OVERRUN
#define STORAGE_PROTOCOL_STATUS_DATA_OVERRUN 0x6
#endif
#ifndef STORAGE_PROTOCOL_STATUS_ERROR
#define STORAGE_PROTOCOL_STATUS_ERROR 0x2
#endif
#ifndef STORAGE_PROTOCOL_STATUS_INSUFFICIENT_RESOURCES
#define STORAGE_PROTOCOL_STATUS_INSUFFICIENT_RESOURCES 0x7
#endif
#ifndef STORAGE_PROTOCOL_STATUS_INVALID_REQUEST
#define STORAGE_PROTOCOL_STATUS_INVALID_REQUEST 0x3
#endif
#ifndef STORAGE_PROTOCOL_STATUS_NOT_SUPPORTED
#define STORAGE_PROTOCOL_STATUS_NOT_SUPPORTED 0xFF
#endif
#ifndef STORAGE_PROTOCOL_STATUS_NO_DEVICE
#define STORAGE_PROTOCOL_STATUS_NO_DEVICE 0x4
#endif
#ifndef STORAGE_PROTOCOL_STATUS_PENDING
#define STORAGE_PROTOCOL_STATUS_PENDING 0x0
#endif
#ifndef STORAGE_PROTOCOL_STATUS_SUCCESS
#define STORAGE_PROTOCOL_STATUS_SUCCESS 0x1
#endif
#ifndef STORAGE_PROTOCOL_STATUS_THROTTLED_REQUEST
#define STORAGE_PROTOCOL_STATUS_THROTTLED_REQUEST 0x8
#endif
#ifndef STORAGE_PROTOCOL_STRUCTURE_VERSION
#define STORAGE_PROTOCOL_STRUCTURE_VERSION 0x1
#endif
#ifndef STORAGE_RPMB_DESCRIPTOR_VERSION_1
#define STORAGE_RPMB_DESCRIPTOR_VERSION_1 1
#endif
#ifndef STORAGE_RPMB_MINIMUM_RELIABLE_WRITE_SIZE
#define STORAGE_RPMB_MINIMUM_RELIABLE_WRITE_SIZE 512
#endif
#ifndef STORAGE_SUPPORTED_FEATURES_BYPASS_IO
#define STORAGE_SUPPORTED_FEATURES_BYPASS_IO 0x00000001
#endif
#ifndef STORAGE_TIER_DESCRIPTION_LENGTH
#define STORAGE_TIER_DESCRIPTION_LENGTH 512
#endif
#ifndef STORAGE_TIER_FLAG_NO_SEEK_PENALTY
#define STORAGE_TIER_FLAG_NO_SEEK_PENALTY 0x00020000
#endif
#ifndef STORAGE_TIER_FLAG_PARITY
#define STORAGE_TIER_FLAG_PARITY 0x00800000
#endif
#ifndef STORAGE_TIER_FLAG_READ_CACHE
#define STORAGE_TIER_FLAG_READ_CACHE 0x00400000
#endif
#ifndef STORAGE_TIER_FLAG_SMR
#define STORAGE_TIER_FLAG_SMR 0x01000000
#endif
#ifndef STORAGE_TIER_FLAG_WRITE_BACK_CACHE
#define STORAGE_TIER_FLAG_WRITE_BACK_CACHE 0x00200000
#endif
#ifndef STORAGE_TIER_NAME_LENGTH
#define STORAGE_TIER_NAME_LENGTH 256
#endif
#ifndef STORATTRIBUTE_MANAGEMENT_STATE
#define STORATTRIBUTE_MANAGEMENT_STATE 1
#endif
#ifndef STORATTRIBUTE_NONE
#define STORATTRIBUTE_NONE 0
#endif
#ifndef TC_DEVICEDUMP_SUBSECTION_DESC_LENGTH
#define TC_DEVICEDUMP_SUBSECTION_DESC_LENGTH 16
#endif
#ifndef TC_PUBLIC_DEVICEDUMP_CONTENT_GPLOG
#define TC_PUBLIC_DEVICEDUMP_CONTENT_GPLOG 0x02
#endif
#ifndef TC_PUBLIC_DEVICEDUMP_CONTENT_SMART
#define TC_PUBLIC_DEVICEDUMP_CONTENT_SMART 0x01
#endif
#ifndef TELEMETRY_COMMAND_SIZE
#define TELEMETRY_COMMAND_SIZE 16
#endif

typedef enum _BPIO_INFLAGS {
    BPIO_INFL_NONE = 0
} BPIO_INFLAGS;

typedef enum _BPIO_OPERATIONS {
    BPIO_OP_ENABLE = 1,
    BPIO_OP_DISABLE = 2,
    BPIO_OP_QUERY = 3
} BPIO_OPERATIONS;

typedef enum _BPIO_OUTFLAGS {
    BPIO_OUTFL_NONE = 0
} BPIO_OUTFLAGS;

typedef enum _DEVICE_INTERNAL_STATUS_DATA_REQUEST_TYPE {
    DeviceInternalStatusDataRequestTypeUndefined = 0,
    DeviceCurrentInternalStatusDataHeader = 1,
    DeviceCurrentInternalStatusData = 2,
    DeviceSavedInternalStatusDataHeader = 3,
    DeviceSavedInternalStatusData = 4
} DEVICE_INTERNAL_STATUS_DATA_REQUEST_TYPE, *PDEVICE_INTERNAL_STATUS_DATA_REQUEST_TYPE;

typedef enum _DEVICE_INTERNAL_STATUS_DATA_SET {
    DeviceStatusDataSetUndefined = 0,
    DeviceStatusDataSet1 = 1,
    DeviceStatusDataSet2 = 2,
    DeviceStatusDataSet3 = 3,
    DeviceStatusDataSet4 = 4,
    DeviceStatusDataSetMax = 5
} DEVICE_INTERNAL_STATUS_DATA_SET, *PDEVICE_INTERNAL_STATUS_DATA_SET;

typedef enum _DEVICEDUMP_COLLECTION_TYPE {
    TCCollectionBugCheck = 1,
    TCCollectionApplicationRequested = 2,
    TCCollectionDeviceRequested = 3
} DEVICEDUMP_COLLECTION_TYPEIDE_NOTIFICATION_TYPE, *PDEVICEDUMP_COLLECTION_TYPE;

typedef enum _STORAGE_ATTRIBUTE_MGMT_ACTION {
    StorAttributeMgmt_ClearAttribute = 0,
    StorAttributeMgmt_SetAttribute = 1,
    StorAttributeMgmt_ResetAttribute = 2
} STORAGE_ATTRIBUTE_MGMT_ACTION, *PSTORAGE_ATTRIBUTE_MGMT_ACTION;

typedef enum _STORAGE_COUNTER_TYPE {
    StorageCounterTypeUnknown = 0,
    StorageCounterTypeTemperatureCelsius = 1,
    StorageCounterTypeTemperatureCelsiusMax = 2,
    StorageCounterTypeReadErrorsTotal = 3,
    StorageCounterTypeReadErrorsCorrected = 4,
    StorageCounterTypeReadErrorsUncorrected = 5,
    StorageCounterTypeWriteErrorsTotal = 6,
    StorageCounterTypeWriteErrorsCorrected = 7,
    StorageCounterTypeWriteErrorsUncorrected = 8,
    StorageCounterTypeManufactureDate = 9,
    StorageCounterTypeStartStopCycleCount = 10,
    StorageCounterTypeStartStopCycleCountMax = 11,
    StorageCounterTypeLoadUnloadCycleCount = 12,
    StorageCounterTypeLoadUnloadCycleCountMax = 13,
    StorageCounterTypeWearPercentage = 14,
    StorageCounterTypeWearPercentageWarning = 15,
    StorageCounterTypeWearPercentageMax = 16,
    StorageCounterTypePowerOnHours = 17,
    StorageCounterTypeReadLatency100NSMax = 18,
    StorageCounterTypeWriteLatency100NSMax = 19,
    StorageCounterTypeFlushLatency100NSMax = 20,
    StorageCounterTypeMax = 21
} STORAGE_COUNTER_TYPE, *PSTORAGE_COUNTER_TYPE;

typedef enum _STORAGE_DEVICE_POWER_CAP_UNITS {
    StorageDevicePowerCapUnitsPercent = 0,
    StorageDevicePowerCapUnitsMilliwatts = 1
} STORAGE_DEVICE_POWER_CAP_UNITS, *PSTORAGE_DEVICE_POWER_CAP_UNITS;

typedef enum _STORAGE_DIAGNOSTIC_LEVEL {
    StorageDiagnosticLevelDefault = 0,
    StorageDiagnosticLevelMax = 1
} STORAGE_DIAGNOSTIC_LEVEL, *PSTORAGE_DIAGNOSTIC_LEVEL;

typedef enum _STORAGE_DIAGNOSTIC_TARGET_TYPE {
    StorageDiagnosticTargetTypeUndefined = 0,
    StorageDiagnosticTargetTypePort = 1,
    StorageDiagnosticTargetTypeMiniport = 2,
    StorageDiagnosticTargetTypeHbaFirmware = 3,
    StorageDiagnosticTargetTypeMax = 4
} STORAGE_DIAGNOSTIC_TARGET_TYPE, *PSTORAGE_DIAGNOSTIC_TARGET_TYPE;

typedef enum _STORAGE_DISK_HEALTH_STATUS {
    DiskHealthUnknown = 0,
    DiskHealthUnhealthy = 1,
    DiskHealthWarning = 2,
    DiskHealthHealthy = 3,
    DiskHealthMax = 4
} STORAGE_DISK_HEALTH_STATUS, *PSTORAGE_DISK_HEALTH_STATUS;

typedef enum _STORAGE_DISK_OPERATIONAL_STATUS {
    DiskOpStatusNone = 0,
    DiskOpStatusUnknown = 1,
    DiskOpStatusOk = 2,
    DiskOpStatusPredictingFailure = 3,
    DiskOpStatusInService = 4,
    DiskOpStatusHardwareError = 5,
    DiskOpStatusNotUsable = 6,
    DiskOpStatusTransientError = 7,
    DiskOpStatusMissing = 8
} STORAGE_DISK_OPERATIONAL_STATUS, *PSTORAGE_DISK_OPERATIONAL_STATUS;

typedef enum _STORAGE_ENCRYPTION_TYPE {
    StorageEncryptionTypeUnknown = 0,
    StorageEncryptionTypeEDrive = 1,
    StorageEncryptionTypeTcgOpal = 2
} STORAGE_ENCRYPTION_TYPE, *PSTORAGE_ENCRYPTION_TYPE;

typedef enum _STORAGE_ICE_TYPE {
    StorageIceTypeUnknown = 0,
    StorageIceTypeUfs = 1,
    StorageIceTypeNvme = 2
} STORAGE_ICE_TYPE, *PSTORAGE_ICE_TYPE;

typedef enum _STORAGE_OPERATIONAL_STATUS_REASON {
    DiskOpReasonUnknown = 0,
    DiskOpReasonScsiSenseCode = 1,
    DiskOpReasonMedia = 2,
    DiskOpReasonIo = 3,
    DiskOpReasonThresholdExceeded = 4,
    DiskOpReasonLostData = 5,
    DiskOpReasonEnergySource = 6,
    DiskOpReasonConfiguration = 7,
    DiskOpReasonDeviceController = 8,
    DiskOpReasonMediaController = 9,
    DiskOpReasonComponent = 10,
    DiskOpReasonNVDIMM_N = 11,
    DiskOpReasonBackgroundOperation = 12,
    DiskOpReasonInvalidFirmware = 13,
    DiskOpReasonHealthCheck = 14,
    DiskOpReasonLostDataPersistence = 15,
    DiskOpReasonDisabledByPlatform = 16,
    DiskOpReasonLostWritePersistence = 17,
    DiskOpReasonDataPersistenceLossImminent = 18,
    DiskOpReasonWritePersistenceLossImminent = 19,
    DiskOpReasonMax = 20
} STORAGE_OPERATIONAL_STATUS_REASON, *PSTORAGE_OPERATIONAL_STATUS_REASON;

typedef enum _STORAGE_PROTOCOL_NVME_DATA_TYPE {
    NVMeDataTypeUnknown = 0,
    NVMeDataTypeIdentify = 1,
    NVMeDataTypeLogPage = 2,
    NVMeDataTypeFeature = 3,
    NVMeDataTypeLogPageEx = 4,
    NVMeDataTypeFeatureEx = 5
} STORAGE_PROTOCOL_NVME_DATA_TYPE, *PSTORAGE_PROTOCOL_NVME_DATA_TYPE;

typedef enum _STORAGE_PROTOCOL_UFS_DATA_TYPE {
    UfsDataTypeUnknown = 0,
    UfsDataTypeQueryDescriptor = 1,
    UfsDataTypeQueryAttribute = 2,
    UfsDataTypeQueryFlag = 3,
    UfsDataTypeQueryDmeAttribute = 4,
    UfsDataTypeQueryDmePeerAttribute = 5,
    UfsDataTypeMax = 6
} STORAGE_PROTOCOL_UFS_DATA_TYPE, *PSTORAGE_PROTOCOL_UFS_DATA_TYPE;

typedef enum _STORAGE_RPMB_COMMAND_TYPE {
    StorRpmbProgramAuthKey = 1,
    StorRpmbQueryWriteCounter = 2,
    StorRpmbAuthenticatedWrite = 3,
    StorRpmbAuthenticatedRead = 4,
    StorRpmbReadResultRequest = 5,
    StorRpmbAuthenticatedDeviceConfigWrite = 6,
    StorRpmbAuthenticatedDeviceConfigRead = 7
} STORAGE_RPMB_COMMAND_TYPE, *PSTORAGE_RPMB_COMMAND_TYPE;

typedef enum _STORAGE_RPMB_FRAME_TYPE {
    StorageRpmbFrameTypeUnknown = 0,
    StorageRpmbFrameTypeStandard = 1,
    StorageRpmbFrameTypeMax = 2
} STORAGE_RPMB_FRAME_TYPE, *PSTORAGE_RPMB_FRAME_TYPE;

typedef enum _STORAGE_SANITIZE_METHOD {
    StorageSanitizeMethodDefault = 0,
    StorageSanitizeMethodBlockErase = 1,
    StorageSanitizeMethodCryptoErase = 2
} STORAGE_SANITIZE_METHOD, *PSTORAGE_SANITIZE_METHOD;

typedef enum _STORAGE_SET_TYPE {
    PropertyStandardSet = 0,
    PropertyExistsSet = 1,
    PropertySetMaxDefined = 2
} STORAGE_SET_TYPE, *PSTORAGE_SET_TYPE;

typedef enum _STORAGE_STACK_TYPE {
    StorageStackTypeUnknown = 0,
    StorageStackTypeScsi = 1,
    StorageStackTypeNVMe = 2
} STORAGE_STACK_TYPE, *PSTORAGE_STACK_TYPE;

typedef enum _STORAGE_TIER_CLASS {
    StorageTierClassUnspecified = 0,
    StorageTierClassCapacity = 1,
    StorageTierClassPerformance = 2,
    StorageTierClassMax = 3
} STORAGE_TIER_CLASS, *PSTORAGE_TIER_CLASS;

typedef enum _STORAGE_TIER_MEDIA_TYPE {
    StorageTierMediaTypeUnspecified = 0,
    StorageTierMediaTypeDisk = 1,
    StorageTierMediaTypeSsd = 2,
    StorageTierMediaTypeScm = 4,
    StorageTierMediaTypeMax = 5
} STORAGE_TIER_MEDIA_TYPE, *PSTORAGE_TIER_MEDIA_TYPE;

typedef enum _STORAGE_ZONE_CONDITION {
    ZoneConditionConventional = 0,
    ZoneConditionEmpty = 1,
    ZoneConditionImplicitlyOpened = 2,
    ZoneConditionExplicitlyOpened = 3,
    ZoneConditionClosed = 4,
    ZoneConditionReadOnly = 13,
    ZoneConditionFull = 14,
    ZoneConditionOffline = 15
} STORAGE_ZONE_CONDITION, *PSTORAGE_ZONE_CONDITION;

typedef enum _STORAGE_ZONE_TYPES {
    ZoneTypeUnknown = 0,
    ZoneTypeConventional = 1,
    ZoneTypeSequentialWriteRequired = 2,
    ZoneTypeSequentialWritePreferred = 3,
    ZoneTypeMax = 4
} STORAGE_ZONE_TYPES, *PSTORAGE_ZONE_TYPES;

typedef enum _STORAGE_ZONED_DEVICE_TYPES {
    ZonedDeviceTypeUnknown = 0,
    ZonedDeviceTypeHostManaged = 1,
    ZonedDeviceTypeHostAware = 2,
    ZonedDeviceTypeDeviceManaged = 3
} STORAGE_ZONED_DEVICE_TYPES, *PSTORAGE_ZONED_DEVICE_TYPES;

typedef enum _STORAGE_ZONES_ATTRIBUTES {
    ZonesAttributeTypeAndLengthMayDifferent = 0,
    ZonesAttributeTypeSameLengthSame = 1,
    ZonesAttributeTypeSameLastZoneLengthDifferent = 2,
    ZonesAttributeTypeMayDifferentLengthSame = 3
} STORAGE_ZONES_ATTRIBUTES, *PSTORAGE_ZONES_ATTRIBUTES;

typedef ULONG DEVICE_DSM_ACTION;
typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES DEVICE_DSM_INPUT;
typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT DEVICE_DSM_OUTPUT;
typedef struct _DEVICE_DATA_SET_RANGE DEVICE_DSM_RANGE;
typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES * PDEVICE_DSM_INPUT;
typedef struct _DEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT * PDEVICE_DSM_OUTPUT;
typedef struct _DEVICE_DATA_SET_RANGE * PDEVICE_DSM_RANGE;

typedef struct _BPIO_INPUT {
  ULONG Version;
  ULONG Size;
  BPIO_OPERATIONS Operation;
  BPIO_INFLAGS InFlags;
  ULONGLONG Reserved2;
} BPIO_INPUT, *PBPIO_INPUT;

typedef struct _BPIO_RESULTS {
  LONG OpStatus;
  USHORT FailingDriverNameLen;
  WCHAR FailingDriverName[32];
  USHORT FailureReasonLen;
  WCHAR FailureReason[128];
} BPIO_RESULTS, *PBPIO_RESULTS;

typedef struct _DEVICE_DATA_SET_REPAIR_OUTPUT {
  DEVICE_DSM_RANGE ParityExtent;
} DEVICE_DATA_SET_REPAIR_OUTPUT, *PDEVICE_DATA_SET_REPAIR_OUTPUT, DEVICE_DSM_REPAIR_OUTPUT, *PDEVICE_DSM_REPAIR_OUTPUT;

typedef struct _DEVICE_DATA_SET_REPAIR_PARAMETERS {
  ULONG NumberOfRepairCopies;
  ULONG SourceCopy;
  ULONG RepairCopies[1];
} DEVICE_DATA_SET_REPAIR_PARAMETERS, *PDEVICE_DATA_SET_REPAIR_PARAMETERS, DEVICE_DSM_REPAIR_PARAMETERS, *PDEVICE_DSM_REPAIR_PARAMETERS;

typedef struct _DEVICE_DATA_SET_SCRUB_EX_OUTPUT {
  ULONGLONG BytesProcessed;
  ULONGLONG BytesRepaired;
  ULONGLONG BytesFailed;
  DEVICE_DSM_RANGE ParityExtent;
  ULONGLONG BytesScrubbed;
} DEVICE_DATA_SET_SCRUB_EX_OUTPUT, *PDEVICE_DATA_SET_SCRUB_EX_OUTPUT, DEVICE_DSM_SCRUB_OUTPUT2, *PDEVICE_DSM_SCRUB_OUTPUT2;

typedef struct _DEVICE_DATA_SET_SCRUB_OUTPUT {
  ULONGLONG BytesProcessed;
  ULONGLONG BytesRepaired;
  ULONGLONG BytesFailed;
} DEVICE_DATA_SET_SCRUB_OUTPUT, *PDEVICE_DATA_SET_SCRUB_OUTPUT, DEVICE_DSM_SCRUB_OUTPUT, *PDEVICE_DSM_SCRUB_OUTPUT;

typedef struct _DEVICE_DATA_SET_TOPOLOGY_ID_QUERY_OUTPUT {
  ULONGLONG TopologyRangeBytes;
  UCHAR TopologyId[16];
} DEVICE_DATA_SET_TOPOLOGY_ID_QUERY_OUTPUT, *PDEVICE_DATA_SET_TOPOLOGY_ID_QUERY_OUTPUT, DEVICE_DSM_TOPOLOGY_ID_QUERY_OUTPUT, *PDEVICE_DSM_TOPOLOGY_ID_QUERY_OUTPUT;

typedef struct _DEVICE_DSM_CONVERSION_OUTPUT {
  ULONG Version;
  GUID Source;
} DEVICE_DSM_CONVERSION_OUTPUT, *PDEVICE_DSM_CONVERSION_OUTPUT;

typedef struct _DEVICE_DSM_DEFINITION {
  DEVICE_DSM_ACTION Action;
  BOOLEAN SingleRange;
  ULONG ParameterBlockAlignment;
  ULONG ParameterBlockLength;
  BOOLEAN HasOutput;
  ULONG OutputBlockAlignment;
  ULONG OutputBlockLength;
} DEVICE_DSM_DEFINITION, *PDEVICE_DSM_DEFINITION;

typedef struct _DEVICE_DSM_FREE_SPACE_OUTPUT {
  ULONG Version;
  ULONGLONG FreeSpace;
} DEVICE_DSM_FREE_SPACE_OUTPUT, *PDEVICE_DSM_FREE_SPACE_OUTPUT;

typedef struct _DEVICE_DSM_LOST_QUERY_OUTPUT {
  ULONG Version;
  ULONG Size;
  ULONGLONG Alignment;
  ULONG NumberOfBits;
  ULONG BitMap[1];
} DEVICE_DSM_LOST_QUERY_OUTPUT, *PDEVICE_DSM_LOST_QUERY_OUTPUT;

typedef struct _DEVICE_DSM_LOST_QUERY_PARAMETERS {
  ULONG Version;
  ULONGLONG Granularity;
} DEVICE_DSM_LOST_QUERY_PARAMETERS, *PDEVICE_DSM_LOST_QUERY_PARAMETERS;

typedef struct _DEVICE_DSM_QUERY_PREFER_LOCAL_REPAIR_OUTPUT {
  ULONG Version;
  BOOLEAN PreferLocalRepair;
} DEVICE_DSM_QUERY_PREFER_LOCAL_REPAIR_OUTPUT, *PDEVICE_DSM_QUERY_PREFER_LOCAL_REPAIR_OUTPUT;

typedef struct _DEVICE_DSM_REPORT_ZONES_PARAMETERS {
  ULONG Size;
  UCHAR ReportOption;
  UCHAR Partial;
  UCHAR Reserved[2];
} DEVICE_DSM_REPORT_ZONES_PARAMETERS, *PDEVICE_DSM_REPORT_ZONES_PARAMETERS;

typedef struct _DEVICE_DSM_TIERING_QUERY_INPUT {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG NumberOfTierIds;
  GUID TierIds[1];
} DEVICE_DSM_TIERING_QUERY_INPUT, *PDEVICE_DSM_TIERING_QUERY_INPUT, DEVICE_DSM_TIERING_QUERY_PARAMETERS, *PDEVICE_DSM_TIERING_QUERY_PARAMETERS;

typedef struct _DEVICE_INTERNAL_STATUS_DATA {
  ULONG Version;
  ULONG Size;
  ULONGLONG T10VendorId;
  ULONG DataSet1Length;
  ULONG DataSet2Length;
  ULONG DataSet3Length;
  ULONG DataSet4Length;
  UCHAR StatusDataVersion;
  UCHAR Reserved[3];
  UCHAR ReasonIdentifier[128];
  ULONG StatusDataLength;
  UCHAR StatusData[1];
} DEVICE_INTERNAL_STATUS_DATA, *PDEVICE_INTERNAL_STATUS_DATA;

typedef struct _DEVICE_STORAGE_ADDRESS_RANGE {
  LONGLONG StartAddress;
  ULONGLONG LengthInBytes;
} DEVICE_STORAGE_ADDRESS_RANGE, *PDEVICE_STORAGE_ADDRESS_RANGE;

typedef struct _DEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT {
  ULONG Version;
  ULONG Flags;
  ULONG TotalNumberOfRanges;
  ULONG NumberOfRangesReturned;
  DEVICE_STORAGE_ADDRESS_RANGE Ranges[1];
} DEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT, *PDEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT;

typedef struct _DEVICEDUMP_RESTRICTED_SUBSECTION {
  UCHAR bData[1];
} DEVICEDUMP_RESTRICTED_SUBSECTION, *PDEVICEDUMP_RESTRICTED_SUBSECTION;

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_SECTION_HEADER {
  GUID guidDeviceDataId;
  UCHAR sOrganizationID[16];
  ULONG dwFirmwareRevision;
  UCHAR sModelNumber[32];
  UCHAR szDeviceManufacturingID[32];
  ULONG dwFlags;
  ULONG bRestrictedPrivateDataVersion;
  ULONG dwFirmwareIssueId;
  UCHAR szIssueDescriptionString[132];
} DEVICEDUMP_SECTION_HEADER, *PDEVICEDUMP_SECTION_HEADER;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_STRUCTURE_VERSION {
  ULONG dwSignature;
  ULONG dwVersion;
  ULONG dwSize;
} DEVICEDUMP_STRUCTURE_VERSION, *PDEVICEDUMP_STRUCTURE_VERSION;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_SUBSECTION_POINTER {
  ULONG dwSize;
  ULONG dwFlags;
  ULONG dwOffset;
} DEVICEDUMP_SUBSECTION_POINTER, *PDEVICEDUMP_SUBSECTION_POINTER;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_STORAGEDEVICE_DATA {
  DEVICEDUMP_STRUCTURE_VERSION Descriptor;
  DEVICEDUMP_SECTION_HEADER SectionHeader;
  ULONG dwBufferSize;
  ULONG dwReasonForCollection;
  DEVICEDUMP_SUBSECTION_POINTER PublicData;
  DEVICEDUMP_SUBSECTION_POINTER RestrictedData;
  DEVICEDUMP_SUBSECTION_POINTER PrivateData;
} DEVICEDUMP_STORAGEDEVICE_DATA, *PDEVICEDUMP_STORAGEDEVICE_DATA;
#pragma pack(pop)

typedef struct _GET_DEVICE_INTERNAL_STATUS_DATA_REQUEST {
  ULONG Version;
  ULONG Size;
  DEVICE_INTERNAL_STATUS_DATA_REQUEST_TYPE RequestDataType;
  DEVICE_INTERNAL_STATUS_DATA_SET RequestDataSet;
} GET_DEVICE_INTERNAL_STATUS_DATA_REQUEST, *PGET_DEVICE_INTERNAL_STATUS_DATA_REQUEST;

#pragma pack(push, 1)
typedef struct _GP_LOG_PAGE_DESCRIPTOR {
  USHORT LogAddress;
  USHORT LogSectors;
} GP_LOG_PAGE_DESCRIPTOR, *PGP_LOG_PAGE_DESCRIPTOR;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_PRIVATE_SUBSECTION {
  ULONG dwFlags;
  GP_LOG_PAGE_DESCRIPTOR GPLogId;
  UCHAR bData[1];
} DEVICEDUMP_PRIVATE_SUBSECTION, *PDEVICEDUMP_PRIVATE_SUBSECTION;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _DEVICEDUMP_PUBLIC_SUBSECTION {
  ULONG dwFlags;
  GP_LOG_PAGE_DESCRIPTOR GPLogTable[16];
  CHAR szDescription[16];
  UCHAR bData[1];
} DEVICEDUMP_PUBLIC_SUBSECTION, *PDEVICEDUMP_PUBLIC_SUBSECTION;
#pragma pack(pop)

typedef struct _PHYSICAL_ELEMENT_STATUS_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG ElementIdentifier;
  UCHAR PhysicalElementType;
  UCHAR PhysicalElementHealth;
  UCHAR Reserved1[2];
  ULONGLONG AssociatedCapacity;
  ULONG Reserved2[4];
} PHYSICAL_ELEMENT_STATUS_DESCRIPTOR, *PPHYSICAL_ELEMENT_STATUS_DESCRIPTOR;

typedef struct _PHYSICAL_ELEMENT_STATUS {
  ULONG Version;
  ULONG Size;
  ULONG DescriptorCount;
  ULONG ReturnedDescriptorCount;
  ULONG ElementIdentifierBeingDepoped;
  ULONG Reserved;
  PHYSICAL_ELEMENT_STATUS_DESCRIPTOR Descriptors[1];
} PHYSICAL_ELEMENT_STATUS, *PPHYSICAL_ELEMENT_STATUS;

typedef struct _PHYSICAL_ELEMENT_STATUS_REQUEST {
  ULONG Version;
  ULONG Size;
  ULONG StartingElement;
  UCHAR Filter;
  UCHAR ReportType;
  UCHAR Reserved[2];
} PHYSICAL_ELEMENT_STATUS_REQUEST, *PPHYSICAL_ELEMENT_STATUS_REQUEST;

typedef struct _REMOVE_ELEMENT_AND_TRUNCATE_REQUEST {
  ULONG Version;
  ULONG Size;
  ULONGLONG RequestCapacity;
  ULONG ElementIdentifier;
  ULONG Reserved;
} REMOVE_ELEMENT_AND_TRUNCATE_REQUEST, *PREMOVE_ELEMENT_AND_TRUNCATE_REQUEST;

typedef struct _STORAGE_ADAPTER_SERIAL_NUMBER {
  ULONG Version;
  ULONG Size;
  WCHAR SerialNumber[128];
} STORAGE_ADAPTER_SERIAL_NUMBER, *PSTORAGE_ADAPTER_SERIAL_NUMBER;

typedef struct _STORAGE_ATTRIBUTE_MGMT {
  ULONG Version;
  ULONG Size;
  STORAGE_ATTRIBUTE_MGMT_ACTION Action;
  ULONG Attribute;
} STORAGE_ATTRIBUTE_MGMT, *PSTORAGE_ATTRIBUTE_MGMT;

typedef struct _STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG64 Attributes;
} STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR, *PSTORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_FAULT_DOMAIN_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG NumberOfFaultDomains;
  GUID FaultDomainIds[1];
} STORAGE_DEVICE_FAULT_DOMAIN_DESCRIPTOR, *PSTORAGE_DEVICE_FAULT_DOMAIN_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_LED_STATE_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONGLONG State;
} STORAGE_DEVICE_LED_STATE_DESCRIPTOR, *PSTORAGE_DEVICE_LED_STATE_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_NUMA_PROPERTY {
  ULONG Version;
  ULONG Size;
  ULONG NumaNode;
} STORAGE_DEVICE_NUMA_PROPERTY, *PSTORAGE_DEVICE_NUMA_PROPERTY;

typedef struct _STORAGE_DEVICE_NUMBER_EX {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG DeviceType;
  ULONG DeviceNumber;
  GUID DeviceGuid;
  ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_EX, *PSTORAGE_DEVICE_NUMBER_EX;

typedef struct _STORAGE_DEVICE_NUMBERS {
  ULONG Version;
  ULONG Size;
  ULONG NumberOfDevices;
  STORAGE_DEVICE_NUMBER Devices[1];
} STORAGE_DEVICE_NUMBERS, *PSTORAGE_DEVICE_NUMBERS;

typedef struct _STORAGE_DEVICE_POWER_CAP {
  ULONG Version;
  ULONG Size;
  STORAGE_DEVICE_POWER_CAP_UNITS Units;
  ULONGLONG MaxPower;
} STORAGE_DEVICE_POWER_CAP, *PSTORAGE_DEVICE_POWER_CAP;

typedef struct _STORAGE_DEVICE_RESILIENCY_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG NameOffset;
  ULONG NumberOfLogicalCopies;
  ULONG NumberOfPhysicalCopies;
  ULONG PhysicalDiskRedundancy;
  ULONG NumberOfColumns;
  ULONG Interleave;
} STORAGE_DEVICE_RESILIENCY_DESCRIPTOR, *PSTORAGE_DEVICE_RESILIENCY_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY {
  ULONG Version;
  ULONG Size;
  BOOLEAN SupportsSelfEncryption;
} STORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY, *PSTORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY;

typedef struct _STORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY_V2 {
  ULONG Version;
  ULONG Size;
  BOOLEAN SupportsSelfEncryption;
  STORAGE_ENCRYPTION_TYPE EncryptionType;
} STORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY_V2, *PSTORAGE_DEVICE_SELF_ENCRYPTION_PROPERTY_V2;

typedef struct _STORAGE_DEVICE_UNSAFE_SHUTDOWN_COUNT {
  ULONG Version;
  ULONG Size;
  ULONG UnsafeShutdownCount;
} STORAGE_DEVICE_UNSAFE_SHUTDOWN_COUNT, *PSTORAGE_DEVICE_UNSAFE_SHUTDOWN_COUNT;

typedef struct _STORAGE_DIAGNOSTIC_DATA {
  ULONG Version;
  ULONG Size;
  GUID ProviderId;
  ULONG BufferSize;
  ULONG Reserved;
  UCHAR DiagnosticDataBuffer[1];
} STORAGE_DIAGNOSTIC_DATA, *PSTORAGE_DIAGNOSTIC_DATA;

typedef struct _STORAGE_DIAGNOSTIC_REQUEST {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  STORAGE_DIAGNOSTIC_TARGET_TYPE TargetType;
  STORAGE_DIAGNOSTIC_LEVEL Level;
} STORAGE_DIAGNOSTIC_REQUEST, *PSTORAGE_DIAGNOSTIC_REQUEST;

typedef struct _STORAGE_FRU_ID_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG IdentifierSize;
  UCHAR Identifier[1];
} STORAGE_FRU_ID_DESCRIPTOR, *PSTORAGE_FRU_ID_DESCRIPTOR;

typedef struct _STORAGE_HW_BOOT_PARTITION_ACTIVATE {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  UCHAR BPID;
  UCHAR Reserved[3];
} STORAGE_HW_BOOT_PARTITION_ACTIVATE, *PSTORAGE_HW_BOOT_PARTITION_ACTIVATE;

typedef struct _STORAGE_HW_BOOT_PARTITION_DOWNLOAD {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  UCHAR BPID;
  UCHAR Reserved[3];
  ULONGLONG Offset;
  ULONGLONG BufferSize;
  UCHAR ImageBuffer[1];
} STORAGE_HW_BOOT_PARTITION_DOWNLOAD, *PSTORAGE_HW_BOOT_PARTITION_DOWNLOAD;

typedef struct _STORAGE_HW_BOOT_PARTITION_INFO {
  ULONG Version;
  ULONG Size;
  ULONGLONG BPSZ;
  ULONG Flags;
  ULONG ImagePayloadAlignment;
  ULONG ImagePayloadMaxSize;
  UCHAR SlotCount;
  UCHAR ABPID;
} STORAGE_HW_BOOT_PARTITION_INFO, *PSTORAGE_HW_BOOT_PARTITION_INFO;

typedef struct _STORAGE_HW_FIRMWARE_DOWNLOAD_V2 {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  UCHAR Slot;
  UCHAR Reserved[3];
  ULONGLONG Offset;
  ULONGLONG BufferSize;
  ULONG ImageSize;
  ULONG Reserved2;
  UCHAR ImageBuffer[1];
} STORAGE_HW_FIRMWARE_DOWNLOAD_V2, *PSTORAGE_HW_FIRMWARE_DOWNLOAD_V2;

typedef struct _STORAGE_PROPERTY_SET {
  STORAGE_PROPERTY_ID PropertyId;
  STORAGE_SET_TYPE SetType;
  UCHAR AdditionalParameters[1];
} STORAGE_PROPERTY_SET, *PSTORAGE_PROPERTY_SET;

typedef struct _STORAGE_PROTOCOL_COMMAND {
  ULONG Version;
  ULONG Length;
  STORAGE_PROTOCOL_TYPE ProtocolType;
  ULONG Flags;
  ULONG ReturnStatus;
  ULONG ErrorCode;
  ULONG CommandLength;
  ULONG ErrorInfoLength;
  ULONG DataToDeviceTransferLength;
  ULONG DataFromDeviceTransferLength;
  ULONG TimeOutValue;
  ULONG ErrorInfoOffset;
  ULONG DataToDeviceBufferOffset;
  ULONG DataFromDeviceBufferOffset;
  ULONG CommandSpecific;
  ULONG Reserved0;
  ULONG FixedProtocolReturnData;
  ULONG FixedProtocolReturnData2;
  ULONG Reserved1[2];
  UCHAR Command[1];
} STORAGE_PROTOCOL_COMMAND, *PSTORAGE_PROTOCOL_COMMAND;

typedef struct _STORAGE_PROTOCOL_SPECIFIC_DATA_EXT {
  STORAGE_PROTOCOL_TYPE ProtocolType;
  ULONG DataType;
  ULONG ProtocolDataValue;
  ULONG ProtocolDataSubValue;
  ULONG ProtocolDataOffset;
  ULONG ProtocolDataLength;
  ULONG FixedProtocolReturnData;
  ULONG ProtocolDataSubValue2;
  ULONG ProtocolDataSubValue3;
  ULONG ProtocolDataSubValue4;
  ULONG ProtocolDataSubValue5;
  ULONG ProtocolDataSubValue6;
  ULONG Reserved[4];
} STORAGE_PROTOCOL_SPECIFIC_DATA_EXT, *PSTORAGE_PROTOCOL_SPECIFIC_DATA_EXT;

typedef struct _STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT {
  ULONG Version;
  ULONG Size;
  STORAGE_PROTOCOL_SPECIFIC_DATA_EXT ProtocolSpecificData;
} STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT, *PSTORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT;

typedef struct _STORAGE_RPMB_DATA_FRAME {
  UCHAR Stuff[196];
  UCHAR KeyOrMAC[32];
  UCHAR Data[256];
  UCHAR Nonce[16];
  UCHAR WriteCounter[4];
  UCHAR Address[2];
  UCHAR BlockCount[2];
  UCHAR OperationResult[2];
  UCHAR RequestOrResponseType[2];
} STORAGE_RPMB_DATA_FRAME, *PSTORAGE_RPMB_DATA_FRAME;

typedef struct _STORAGE_RPMB_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG SizeInBytes;
  ULONG MaxReliableWriteSizeInBytes;
  STORAGE_RPMB_FRAME_TYPE FrameFormat;
} STORAGE_RPMB_DESCRIPTOR, *PSTORAGE_RPMB_DESCRIPTOR;

typedef struct _STORAGE_STACK_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  STORAGE_STACK_TYPE StorageStackType;
} STORAGE_STACK_DESCRIPTOR, *PSTORAGE_STACK_DESCRIPTOR;

typedef struct _STORAGE_TIER {
  GUID Id;
  WCHAR Name[256];
  WCHAR Description[256];
  ULONGLONG Flags;
  ULONGLONG ProvisionedCapacity;
  STORAGE_TIER_MEDIA_TYPE MediaType;
  STORAGE_TIER_CLASS Class;
} STORAGE_TIER, *PSTORAGE_TIER;

typedef struct _STORAGE_DEVICE_TIERING_DESCRIPTOR {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG TotalNumberOfTiers;
  ULONG NumberOfTiersReturned;
  STORAGE_TIER Tiers[1];
} STORAGE_DEVICE_TIERING_DESCRIPTOR, *PSTORAGE_DEVICE_TIERING_DESCRIPTOR;

typedef struct _STORAGE_TIER_REGION {
  GUID TierId;
  ULONGLONG Offset;
  ULONGLONG Length;
} STORAGE_TIER_REGION, *PSTORAGE_TIER_REGION;

typedef struct _DEVICE_DSM_TIERING_QUERY_OUTPUT {
  ULONG Version;
  ULONG Size;
  ULONG Flags;
  ULONG Reserved;
  ULONGLONG Alignment;
  ULONG TotalNumberOfRegions;
  ULONG NumberOfRegionsReturned;
  STORAGE_TIER_REGION Regions[1];
} DEVICE_DSM_TIERING_QUERY_OUTPUT, *PDEVICE_DSM_TIERING_QUERY_OUTPUT;

typedef struct _STORAGE_ZONE_DESCRIPTOR {
  ULONG Size;
  STORAGE_ZONE_TYPES ZoneType;
  STORAGE_ZONE_CONDITION ZoneCondition;
  BOOLEAN ResetWritePointerRecommend;
  UCHAR Reserved0[3];
  ULONGLONG ZoneSize;
  ULONGLONG WritePointerOffset;
} STORAGE_ZONE_DESCRIPTOR, *PSTORAGE_ZONE_DESCRIPTOR;

typedef struct _DEVICE_DSM_REPORT_ZONES_DATA {
  ULONG Size;
  ULONG ZoneCount;
  STORAGE_ZONES_ATTRIBUTES Attributes;
  ULONG Reserved0;
  STORAGE_ZONE_DESCRIPTOR ZoneDescriptors[1];
} DEVICE_DSM_REPORT_ZONES_DATA, *PDEVICE_DSM_REPORT_ZONES_DATA, DEVICE_DSM_REPORT_ZONES_OUTPUT, *PDEVICE_DSM_REPORT_ZONES_OUTPUT;

typedef struct _STORAGE_ZONE_GROUP {
  ULONG ZoneCount;
  STORAGE_ZONE_TYPES ZoneType;
  ULONGLONG ZoneSize;
} STORAGE_ZONE_GROUP, *PSTORAGE_ZONE_GROUP;

#define BPIO_OUTPUT_DISABLE_SIZE 0x18
#define BPIO_OUTPUT_ENABLE_SIZE 0x160
#define BPIO_OUTPUT_QUERY_SIZE 0x160
#define BPIO_OUTPUT_VOLUME_DISABLE_SIZE 0x18
#define DEVICE_DSM_FLAG_PHYSICAL_ADDRESSES_OMIT_TOTAL_RANGES 0x10000000
#define DEVICE_DSM_PHYSICAL_ADDRESS_HAS_MEMORY_ERROR ((LONGLONG)(-1))
#define DEVICE_DSM_PHYSICAL_ADDRESSES_OUTPUT_VERSION_V1 1
#define DEVICE_DSM_RANGE_ERROR_INFO_VERSION_V1 1
#define DEVICE_STORAGE_NO_ERRORS 0x1
#define DeviceDsmAction_QueryPreferLocalRepair 0x8000001E
#define DeviceDsmDefinition_CloseZone { DeviceDsmAction_CloseZone, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_ConversionQuery { DeviceDsmAction_ConversionQuery, TRUE, 0, 0, TRUE, __alignof(DEVICE_DSM_CONVERSION_OUTPUT), sizeof (DEVICE_DSM_CONVERSION_OUTPUT) }
#define DeviceDsmDefinition_DrtClear { DeviceDsmAction_DrtClear, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_DrtDisable { DeviceDsmAction_DrtDisable, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_DrtQuery { DeviceDsmAction_DrtQuery, FALSE, 0, 0, TRUE, 0, 0 }
#define DeviceDsmDefinition_FinishZone { DeviceDsmAction_FinishZone, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_GetFreeSpace { DeviceDsmAction_GetFreeSpace, FALSE, 0, 0, TRUE, __alignof(DEVICE_DSM_FREE_SPACE_OUTPUT), sizeof (DEVICE_DSM_FREE_SPACE_OUTPUT) }
#define DeviceDsmDefinition_GetPhysicalAddresses 0
#define DeviceDsmDefinition_GetRangeErrorInfo 0
#define DeviceDsmDefinition_LostQuery { DeviceDsmAction_LostQuery, TRUE, __alignof(DEVICE_DSM_LOST_QUERY_PARAMETERS), sizeof (DEVICE_DSM_LOST_QUERY_PARAMETERS), TRUE, __alignof(DEVICE_DSM_LOST_QUERY_OUTPUT), sizeof (DEVICE_DSM_LOST_QUERY_OUTPUT) }
#define DeviceDsmDefinition_Map { DeviceDsmAction_Map, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_None 0
#define DeviceDsmDefinition_Notification 0
#define DeviceDsmDefinition_NvCache_Change_Priority { DeviceDsmAction_NvCache_Change_Priority, FALSE, __alignof(DEVICE_DSM_NVCACHE_CHANGE_PRIORITY_PARAMETERS), sizeof (DEVICE_DSM_NVCACHE_CHANGE_PRIORITY_PARAMETERS), FALSE, 0, 0 }
#define DeviceDsmDefinition_NvCache_Evict { DeviceDsmAction_NvCache_Evict, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_OpenZone { DeviceDsmAction_OpenZone, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_QueryPreferLocalRepair { DeviceDsmAction_QueryPreferLocalRepair, FALSE, 0, 0, TRUE, __alignof(DEVICE_DSM_QUERY_PREFER_LOCAL_REPAIR_OUTPUT), sizeof (DEVICE_DSM_QUERY_PREFER_LOCAL_REPAIR_OUTPUT) }
#define DeviceDsmDefinition_RegenerateParity { DeviceDsmAction_RegenerateParity, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_Repair 0
#define DeviceDsmDefinition_ReportZones { DeviceDsmAction_ReportZones, FALSE, __alignof(DEVICE_DSM_REPORT_ZONES_PARAMETERS), sizeof (DEVICE_DSM_REPORT_ZONES_PARAMETERS), TRUE, __alignof(DEVICE_DSM_REPORT_ZONES_OUTPUT), sizeof (DEVICE_DSM_REPORT_ZONES_OUTPUT) }
#define DeviceDsmDefinition_ResetWritePointer { DeviceDsmAction_ResetWritePointer, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_ScopeRegen { DeviceDsmAction_ScopeRegen, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_Scrub 0
#define DeviceDsmDefinition_TieringQuery { DeviceDsmAction_TieringQuery, TRUE, __alignof(DEVICE_DSM_TIERING_QUERY_PARAMETERS), sizeof (DEVICE_DSM_TIERING_QUERY_PARAMETERS), TRUE, __alignof(DEVICE_DSM_TIERING_QUERY_OUTPUT), sizeof (DEVICE_DSM_TIERING_QUERY_OUTPUT) }
#define DeviceDsmDefinition_TopologyIdQuery { DeviceDsmAction_TopologyIdQuery, FALSE, 0, 0, TRUE, __alignof(DEVICE_DSM_TOPOLOGY_ID_QUERY_OUTPUT), sizeof (DEVICE_DSM_TOPOLOGY_ID_QUERY_OUTPUT) }
#define DeviceDsmDefinition_Trim 0
#define DeviceDsmDefinition_VdtSet { DeviceDsmAction_VdtSet, FALSE, 0, 0, FALSE, 0, 0 }
#define DeviceDsmDefinition_WriteZeroes { DeviceDsmAction_WriteZeroes, FALSE, 0, 0, FALSE, 0, 0 }
#define DEVICEDUMP_CAP_PRIVATE_SECTION 0x00000001
#define IOCTL_STORAGE_BOOT_PARTITION_ACTIVATE 0x2DDC10
#define IOCTL_STORAGE_BOOT_PARTITION_DOWNLOAD 0x2DDC0C
#define IOCTL_STORAGE_BOOT_PARTITION_GET_INFO 0x2D1C14
#define IOCTL_STORAGE_GET_SYSTEM_FEATURE_SUPPORT 0x2D4C1C
#define IOCTL_STORAGE_MANAGE_BYPASS_IO 0x2D08C0
#define IOCTL_STORAGE_MINIPORT_PASSTHROUGH_REQUEST 0x2DD050
#define IOCTL_STORAGE_SET_PROPERTY 0x2D93FC
#define NO_SRBTYPE_ADAPTER_DESCRIPTOR_SIZE 0x1E
#define STORAGE_ADAPTER_SERIAL_NUMBER_V1_SIZE 0x108
#define STORAGE_ADAPTER_SERIAL_NUMBER_V1_VERSION 0x108
#define STORAGE_COUNTERS_VERSION_V1 0x20
#define STORAGE_CRASH_TELEMETRY_REGKEY 0
#define STORAGE_DEVICE_NUMA_NODE_UNKNOWN 0xFFFFFFFF
#define STORAGE_DEVICE_TELEMETRY_REGKEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Storage\\StorageTelemetry"
#define STORAGE_MINIPORT_DESCRIPTOR_V1_SIZE 0x10
#define STORAGE_SUPPORTED_FEATURES_MASK 1
#define TC_PUBLIC_DATA_TYPE_ATAGP 0
#define TC_PUBLIC_DATA_TYPE_ATASMART "ATASMARTPages"
#define TCRecordStorportSrbFunction 0

struct _BPIO_OUTPUT
{
    ULONG Version;
    ULONG Size;
    BPIO_OPERATIONS Operation;
    BPIO_OUTFLAGS OutFlags;
    ULONGLONG Reserved2;
    union
    {
        BPIO_RESULTS Enable;
        BPIO_RESULTS Query;
    };
};

struct _DEVICE_LOCATION
{
    ULONG Socket;
    ULONG Slot;
    ULONG Adapter;
    ULONG Port;
    union
    {
        struct
        {
            ULONG Channel;
            ULONG Device;
        };
        struct
        {
            ULONG Target;
            ULONG Lun;
        };
    };
};

struct _DEVICE_STORAGE_RANGE_ATTRIBUTES
{
    ULONGLONG LengthInBytes;
    union
    {
        ULONG AllFlags;
        struct
        {
            ULONG IsRangeBad : 1;
        };
    };
    ULONG Reserved;
};

#pragma pack(push, 1)
struct _DEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD
{
    UCHAR Cdb[16];
    UCHAR Command[16];
    ULONGLONG StartTime;
    ULONGLONG EndTime;
    ULONG OperationStatus;
    ULONG OperationError;
    union
    {
        struct
        {
            ULONG dwReserved;
        } ExternalStack;
        struct
        {
            ULONG dwAtaPortSpecific;
        } AtaPort;
        struct
        {
            ULONG SrbTag;
        } StorPort;
    } StackSpecific;
};
#pragma pack(pop)

struct _STORAGE_COUNTER
{
    STORAGE_COUNTER_TYPE Type;
    union
    {
        struct
        {
            ULONG Week;
            ULONG Year;
        } ManufactureDate;
        ULONGLONG AsUlonglong;
    } Value;
};

struct _STORAGE_CRYPTO_CAPABILITY_V2
{
    ULONG Version;
    ULONG Size;
    ULONG CryptoCapabilityIndex;
    STORAGE_CRYPTO_ALGORITHM_ID AlgorithmId;
    STORAGE_CRYPTO_KEY_SIZE KeySize;
    ULONG DataUnitSizeBitmask;
    USHORT MaxIVBitSize;
    USHORT Reserved;
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;
};

struct _STORAGE_FEATURE_SUPPORT
{
    ULONG Size;
    ULONG Version;
    union
    {
        struct
        {
            ULONGLONG StorMQMiniportsSupported : 1;
            ULONGLONG Reserved : 63;
        };
        ULONGLONG AsUlonglong;
    } Flags;
    ULONGLONG Reserved[6];
};

struct _STORAGE_HW_CRYPTO_CAPABILITY
{
    ULONG Version;
    ULONG Size;
    ULONG CryptoCapabilityIndex;
    STORAGE_CRYPTO_ALGORITHM_ID AlgorithmId;
    STORAGE_CRYPTO_KEY_SIZE KeySize;
    ULONG DataUnitSizeBitmask;
    USHORT MaxIVBitSize;
    USHORT Reserved;
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;
};

struct _STORAGE_HW_CRYPTO_DESCRIPTOR
{
    STORAGE_DESCRIPTOR_HEADER Header;
    ULONG NumKeysSupported;
    ULONG NumCryptoCapabilities;
    ULONG OffsetToCryptoCapabilities;
    ULONG SizeOfCryptoCapability;
    STORAGE_ICE_TYPE IceType;
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;
    STORAGE_CRYPTO_KEY_TYPE KeyTypeBitmask;
};

struct _STORAGE_HW_ENDURANCE_INFO
{
    ULONG ValidFields;
    ULONG GroupId;
    struct
    {
        ULONG Shared : 1;
        ULONG Reserved : 31;
    } Flags;
    ULONG LifePercentage;
    UCHAR BytesReadCount[16];
    UCHAR ByteWriteCount[16];
};

struct _STORAGE_OPERATIONAL_REASON
{
    ULONG Version;
    ULONG Size;
    STORAGE_OPERATIONAL_STATUS_REASON Reason;
    union
    {
        struct
        {
            UCHAR SenseKey;
            UCHAR ASC;
            UCHAR ASCQ;
            UCHAR Reserved;
        } ScsiSenseKey;
        struct
        {
            UCHAR CriticalHealth;
            UCHAR ModuleHealth[2];
            UCHAR ErrorThresholdStatus;
        } NVDIMM_N;
        ULONG AsUlong;
    } RawBytes;
};

union _STORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE
{
    struct
    {
        ULONG RetainAsynEvent : 1;
        ULONG LogSpecificField : 4;
        ULONG Reserved0 : 3;
        ULONG UUIDIndex : 7;
        ULONG Reserved : 17;
    };
    ULONG AsUlong;
};

struct _STORAGE_REINITIALIZE_MEDIA
{
    ULONG Version;
    ULONG Size;
    ULONG TimeoutInSeconds;
    struct
    {
        ULONG SanitizeMethod : 4;
        ULONG DisallowUnrestrictedSanitizeExit : 1;
        ULONG Reserved : 27;
    } SanitizeOption;
};

struct _STORAGE_ZONED_DEVICE_DESCRIPTOR
{
    ULONG Version;
    ULONG Size;
    STORAGE_ZONED_DEVICE_TYPES DeviceType;
    ULONG ZoneCount;
    union
    {
        struct
        {
            ULONG MaxOpenZoneCount;
            BOOLEAN UnrestrictedRead;
            UCHAR Reserved[3];
        } SequentialRequiredZone;
        struct
        {
            ULONG OptimalOpenZoneCount;
            ULONG Reserved;
        } SequentialPreferredZone;
    } ZoneAttributes;
    ULONG ZoneGroupCount;
    STORAGE_ZONE_GROUP ZoneGroup[1];
};

typedef struct _BPIO_OUTPUT BPIO_OUTPUT;

typedef struct _DEVICE_LOCATION DEVICE_LOCATION;

struct _STORAGE_DEVICE_LOCATION_DESCRIPTOR
{
    ULONG Version;
    ULONG Size;
    DEVICE_LOCATION Location;
    ULONG StringOffset;
};

typedef struct _DEVICE_STORAGE_RANGE_ATTRIBUTES DEVICE_STORAGE_RANGE_ATTRIBUTES;

struct _DEVICE_DSM_RANGE_ERROR_INFO
{
    ULONG Version;
    ULONG Flags;
    ULONG TotalNumberOfRanges;
    ULONG NumberOfRangesReturned;
    DEVICE_STORAGE_RANGE_ATTRIBUTES Ranges[1];
};

typedef struct _DEVICE_DSM_RANGE_ERROR_INFO DEVICE_DSM_RANGE_ERROR_INFO;

typedef struct _DEVICE_DSM_RANGE_ERROR_INFO DEVICE_DSM_RANGE_ERROR_OUTPUT;

typedef struct _DEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD DEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD;

#pragma pack(push, 1)
struct _DEVICEDUMP_STORAGESTACK_PUBLIC_DUMP
{
    DEVICEDUMP_STRUCTURE_VERSION Descriptor;
    ULONG dwReasonForCollection;
    UCHAR cDriverName[16];
    ULONG uiNumRecords;
    DEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD RecordArray[1];
};
#pragma pack(pop)

typedef struct _DEVICEDUMP_STORAGESTACK_PUBLIC_DUMP DEVICEDUMP_STORAGESTACK_PUBLIC_DUMP;

typedef struct _BPIO_OUTPUT *PBPIO_OUTPUT;

typedef struct _DEVICE_DSM_RANGE_ERROR_INFO *PDEVICE_DSM_RANGE_ERROR_INFO;

typedef struct _DEVICE_DSM_RANGE_ERROR_INFO *PDEVICE_DSM_RANGE_ERROR_OUTPUT;

typedef struct _DEVICE_LOCATION *PDEVICE_LOCATION;

typedef struct _DEVICE_STORAGE_RANGE_ATTRIBUTES *PDEVICE_STORAGE_RANGE_ATTRIBUTES;

typedef struct _DEVICEDUMP_STORAGESTACK_PUBLIC_DUMP *PDEVICEDUMP_STORAGESTACK_PUBLIC_DUMP;

typedef struct _DEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD *PDEVICEDUMP_STORAGESTACK_PUBLIC_STATE_RECORD;

typedef struct _STORAGE_COUNTER *PSTORAGE_COUNTER;

typedef struct _STORAGE_CRYPTO_CAPABILITY_V2 *PSTORAGE_CRYPTO_CAPABILITY_V2;

typedef struct _STORAGE_DEVICE_LOCATION_DESCRIPTOR *PSTORAGE_DEVICE_LOCATION_DESCRIPTOR;

typedef struct _STORAGE_FEATURE_SUPPORT *PSTORAGE_FEATURE_SUPPORT;

typedef struct _STORAGE_HW_CRYPTO_CAPABILITY *PSTORAGE_HW_CRYPTO_CAPABILITY;

typedef struct _STORAGE_HW_CRYPTO_DESCRIPTOR *PSTORAGE_HW_CRYPTO_DESCRIPTOR;

typedef struct _STORAGE_HW_ENDURANCE_INFO *PSTORAGE_HW_ENDURANCE_INFO;

typedef struct _STORAGE_OPERATIONAL_REASON *PSTORAGE_OPERATIONAL_REASON;

typedef union _STORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE *PSTORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE;

typedef struct _STORAGE_REINITIALIZE_MEDIA *PSTORAGE_REINITIALIZE_MEDIA;

typedef struct _STORAGE_ZONED_DEVICE_DESCRIPTOR *PSTORAGE_ZONED_DEVICE_DESCRIPTOR;

typedef struct _STORAGE_COUNTER STORAGE_COUNTER;

struct _STORAGE_COUNTERS
{
    ULONG Version;
    ULONG Size;
    ULONG NumberOfCounters;
    STORAGE_COUNTER Counters[1];
};

typedef struct _STORAGE_COUNTERS *PSTORAGE_COUNTERS;

typedef struct _STORAGE_COUNTERS STORAGE_COUNTERS;

typedef struct _STORAGE_CRYPTO_CAPABILITY_V2 STORAGE_CRYPTO_CAPABILITY_V2;

struct _STORAGE_CRYPTO_DESCRIPTOR_V2
{
    ULONG Version;
    ULONG Size;
    ULONG NumKeysSupported;
    ULONG NumCryptoCapabilities;
    STORAGE_ICE_TYPE IceType;
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;
    STORAGE_CRYPTO_CAPABILITY_V2 CryptoCapabilities[1];
};

typedef struct _STORAGE_CRYPTO_DESCRIPTOR_V2 *PSTORAGE_CRYPTO_DESCRIPTOR_V2;

typedef struct _STORAGE_CRYPTO_DESCRIPTOR_V2 STORAGE_CRYPTO_DESCRIPTOR_V2;

typedef struct _STORAGE_DEVICE_LOCATION_DESCRIPTOR STORAGE_DEVICE_LOCATION_DESCRIPTOR;

typedef struct _STORAGE_FEATURE_SUPPORT STORAGE_FEATURE_SUPPORT;

typedef struct _STORAGE_HW_CRYPTO_CAPABILITY STORAGE_HW_CRYPTO_CAPABILITY;

typedef struct _STORAGE_HW_CRYPTO_DESCRIPTOR STORAGE_HW_CRYPTO_DESCRIPTOR;

typedef struct _STORAGE_HW_ENDURANCE_INFO STORAGE_HW_ENDURANCE_INFO;

struct _STORAGE_HW_ENDURANCE_DATA_DESCRIPTOR
{
    ULONG Version;
    ULONG Size;
    STORAGE_HW_ENDURANCE_INFO EnduranceInfo;
};

typedef struct _STORAGE_HW_ENDURANCE_DATA_DESCRIPTOR *PSTORAGE_HW_ENDURANCE_DATA_DESCRIPTOR;

typedef struct _STORAGE_HW_ENDURANCE_DATA_DESCRIPTOR STORAGE_HW_ENDURANCE_DATA_DESCRIPTOR;

typedef struct _STORAGE_OPERATIONAL_REASON STORAGE_OPERATIONAL_REASON;

struct _STORAGE_DEVICE_MANAGEMENT_STATUS
{
    ULONG Version;
    ULONG Size;
    STORAGE_DISK_HEALTH_STATUS Health;
    ULONG NumberOfOperationalStatus;
    ULONG NumberOfAdditionalReasons;
    STORAGE_DISK_OPERATIONAL_STATUS OperationalStatus[16];
    STORAGE_OPERATIONAL_REASON AdditionalReasons[1];
};

typedef struct _STORAGE_DEVICE_MANAGEMENT_STATUS *PSTORAGE_DEVICE_MANAGEMENT_STATUS;

typedef struct _STORAGE_DEVICE_MANAGEMENT_STATUS STORAGE_DEVICE_MANAGEMENT_STATUS;

typedef union _STORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE STORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE;

typedef struct _STORAGE_REINITIALIZE_MEDIA STORAGE_REINITIALIZE_MEDIA;

typedef struct _STORAGE_ZONED_DEVICE_DESCRIPTOR STORAGE_ZONED_DEVICE_DESCRIPTOR;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
BOOLEAN DeviceDsmAddDataSetRange(PDEVICE_DSM_INPUT Input, ULONG InputLength, LONGLONG Offset, ULONGLONG Length);
PDEVICE_DSM_RANGE DeviceDsmDataSetRanges(PDEVICE_DSM_INPUT Input);
ULONG DeviceDsmGetInputLength(PDEVICE_DSM_DEFINITION Definition, ULONG ParameterBlockLength, ULONG NumberOfDataSetRanges);
ULONG DeviceDsmGetNumberOfDataSetRanges(PDEVICE_DSM_DEFINITION Definition, ULONG InputLength, ULONG ParameterBlockLength);
ULONG DeviceDsmGetOutputBlockLength(PDEVICE_DSM_DEFINITION Definition, ULONG OutputLength);
ULONG DeviceDsmGetOutputLength(PDEVICE_DSM_DEFINITION Definition, ULONG OutputBlockLength);
void DeviceDsmInitializeInput(PDEVICE_DSM_DEFINITION Definition, PDEVICE_DSM_INPUT Input, ULONG InputLength, ULONG Flags, PVOID Parameters, ULONG ParameterBlockLength);
void DeviceDsmInitializeOutput(PDEVICE_DSM_DEFINITION Definition, PDEVICE_DSM_OUTPUT Output, ULONG OutputLength, ULONG Flags);
ULONG DeviceDsmNumberOfDataSetRanges(PDEVICE_DSM_INPUT Input);
PVOID DeviceDsmOutputBlock(PDEVICE_DSM_OUTPUT Output);
PVOID DeviceDsmParameterBlock(PDEVICE_DSM_INPUT Input);
BOOLEAN DeviceDsmValidateInput(PDEVICE_DSM_DEFINITION Definition, PDEVICE_DSM_INPUT Input, ULONG InputLength);
BOOLEAN DeviceDsmValidateOutput(PDEVICE_DSM_DEFINITION Definition, PDEVICE_DSM_OUTPUT Output, ULONG OutputLength);
BOOLEAN DeviceDsmValidateOutputLength(PDEVICE_DSM_DEFINITION Definition, ULONG OutputLength);
const STORAGE_HW_CRYPTO_CAPABILITY * GetStorageHwCryptoCapability(const STORAGE_HW_CRYPTO_DESCRIPTOR *CryptoDescriptor, ULONG Index);
STORAGE_HW_CRYPTO_CAPABILITY * GetStorageHwCryptoCapabilityMut(STORAGE_HW_CRYPTO_DESCRIPTOR *CryptoDescriptor, ULONG Index);

#ifdef __cplusplus
}
#endif



struct _STORAGE_CRYPTO_CAPABILITY
{
    ULONG Version;
    ULONG Size;
    ULONG CryptoCapabilityIndex;
    STORAGE_CRYPTO_ALGORITHM_ID AlgorithmId;
    STORAGE_CRYPTO_KEY_SIZE KeySize;
    ULONG DataUnitSizeBitmask;
};

typedef struct _STORAGE_CRYPTO_CAPABILITY *PSTORAGE_CRYPTO_CAPABILITY;

typedef struct _STORAGE_CRYPTO_DESCRIPTOR *PSTORAGE_CRYPTO_DESCRIPTOR;

typedef struct _STORAGE_CRYPTO_CAPABILITY STORAGE_CRYPTO_CAPABILITY;

struct _STORAGE_CRYPTO_DESCRIPTOR
{
    ULONG Version;
    ULONG Size;
    ULONG NumKeysSupported;
    ULONG NumCryptoCapabilities;
    STORAGE_CRYPTO_CAPABILITY CryptoCapabilities[1];
};

typedef struct _STORAGE_CRYPTO_DESCRIPTOR STORAGE_CRYPTO_DESCRIPTOR;

#define DEVICE_DSM_ROUND_DN(_a, _b) (((_a)) / (_b) * (_b))
#define DEVICE_DSM_ROUND_UP(_a, _b) (((_a) + ((_b) - 1)) / (_b) * (_b))
#define GET_REPAIR_TOPOLOGY_ID(R) RtlOffsetToPointer(R, ALIGN_UP_BY(FIELD_OFFSET(DEVICE_DATA_SET_REPAIR_PARAMETERS, RepairCopies) + sizeof (ULONG) * R -> NumberOfRepairCopies, 8))

#endif /* _NTDDSTOR_H_ */
