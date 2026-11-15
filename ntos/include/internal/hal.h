/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel view of the hardware abstraction layer
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Pierre Schweitzer <pierre.schweitzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

VOID
NTAPI
xHalSetWakeAlarm(
    _In_ ULONGLONG AlartTime,
    _In_ ULONGLONG DcWakeTime);

#define DISK_SIGNATURE_OFFSET           0x1B8

#define PARTITION_TABLE_OFFSET          0x1BE

#define BOOT_SIGNATURE_OFFSET           (0x200 - 2)

#define BOOT_RECORD_SIGNATURE           0xAA55

#define NUM_PARTITION_TABLE_ENTRIES     4

#define GET_STARTING_SECTOR(p)                  \
    ((ULONG)((p)->StartingSectorLsb0) +         \
     (ULONG)((p)->StartingSectorLsb1 << 8 ) +   \
     (ULONG)((p)->StartingSectorMsb0 << 16) +   \
     (ULONG)((p)->StartingSectorMsb1 << 24))

#define GET_ENDING_S_OF_CHS(p)  \
    ((UCHAR)((p)->EndingCylinderLsb & 0x3F))

#define GET_PARTITION_LENGTH(p)                     \
    ((ULONG)((p)->PartitionLengthLsb0) +            \
     (ULONG)((p)->PartitionLengthLsb1 << 8 ) +      \
     (ULONG)((p)->PartitionLengthMsb0 << 16) +      \
     (ULONG)((p)->PartitionLengthMsb1 << 24))

#define SET_PARTITION_LENGTH(p, l)                  \
    ((p)->PartitionLengthLsb0 =  (l) & 0xFF,        \
     (p)->PartitionLengthLsb1 = ((l) >> 8 ) & 0xFF, \
     (p)->PartitionLengthMsb0 = ((l) >> 16) & 0xFF, \
     (p)->PartitionLengthMsb1 = ((l) >> 24) & 0xFF)

typedef struct _PARTITION_DESCRIPTOR
{
    UCHAR ActiveFlag;
    UCHAR StartingTrack;
    UCHAR StartingCylinderLsb;
    UCHAR StartingCylinderMsb;
    UCHAR PartitionType;
    UCHAR EndingTrack;
    UCHAR EndingCylinderLsb;
    UCHAR EndingCylinderMsb;
    UCHAR StartingSectorLsb0;
    UCHAR StartingSectorLsb1;
    UCHAR StartingSectorMsb0;
    UCHAR StartingSectorMsb1;
    UCHAR PartitionLengthLsb0;
    UCHAR PartitionLengthLsb1;
    UCHAR PartitionLengthMsb0;
    UCHAR PartitionLengthMsb1;
} PARTITION_DESCRIPTOR, *PPARTITION_DESCRIPTOR;

typedef struct _BOOT_SECTOR_INFO
{
    UCHAR JumpByte[1];
    UCHAR Ignore1[2];
    UCHAR OemData[8];
    UCHAR BytesPerSector[2];
    UCHAR Ignore2[6];
    UCHAR NumberOfSectors[2];
    UCHAR MediaByte[1];
    UCHAR Ignore3[2];
    UCHAR SectorsPerTrack[2];
    UCHAR NumberOfHeads[2];
} BOOT_SECTOR_INFO, *PBOOT_SECTOR_INFO;

typedef struct _PARTITION_TABLE
{
    PARTITION_INFORMATION PartitionEntry[4];
} PARTITION_TABLE, *PPARTITION_TABLE;

typedef struct _DISK_LAYOUT
{
    ULONG TableCount;
    ULONG Signature;
    PARTITION_TABLE PartitionTable[1];
} DISK_LAYOUT, *PDISK_LAYOUT;

typedef struct _PTE
{
    UCHAR ActiveFlag;
    UCHAR StartingTrack;
    USHORT StartingCylinder;
    UCHAR PartitionType;
    UCHAR EndingTrack;
    USHORT EndingCylinder;
    ULONG StartingSector;
    ULONG PartitionLength;
} PTE, *PPTE;

VOID
NTAPI
xHalHaltSystem(VOID);

NTSTATUS
FASTCALL
xHalIoReadPartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ BOOLEAN ReturnRecognizedPartitions,
    _Out_ PDRIVE_LAYOUT_INFORMATION *PartitionBuffer);

UCHAR
NTAPI
xHalVectorToIDTEntry(
    _In_ ULONG Vector);

VOID
FASTCALL
xHalIoAssignDriveLetters(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PSTRING NtDeviceName,
    _Out_ PUCHAR NtSystemPath,
    _Out_ PSTRING NtSystemPathString);

NTSTATUS
NTAPI
xHalGetInterruptTranslator(
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ INTERFACE_TYPE BridgeInterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_ PTRANSLATOR_INTERFACE Translator,
    _Out_ PULONG BridgeBusNumber);

NTSTATUS
NTAPI
xHalAllocateMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Unknown,
    _In_ ULONG Unknown2,
    PMAP_REGISTER_ENTRY Registers);

NTSTATUS
NTAPI
xHalEndMirroring(
    _In_ ULONG PassNumber);

VOID
NTAPI
xHalEndOfBoot(VOID);

PBUS_HANDLER
FASTCALL
xHalHandlerForBus(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber);

NTSTATUS
NTAPI
xHalInitPnpDriver(VOID);

NTSTATUS
NTAPI
xHalInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE *PmHalDispatchTable);

VOID
NTAPI
xHalLocateHiberRanges(
    _In_ PVOID MemoryMap);

NTSTATUS
NTAPI
xHalMirrorPhysicalMemory(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ LARGE_INTEGER NumberOfBytes);

NTSTATUS
NTAPI
xHalQueryBusSlots(
    _In_ PBUS_HANDLER BusHandler,
    _In_ ULONG BufferSize,
    _Out_ PULONG SlotNumbers,
    _Out_ PULONG ReturnedLength);

NTSTATUS
NTAPI
xHalQuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength);

VOID
FASTCALL
xHalReferenceHandler(
    _In_ PBUS_HANDLER BusHandler);

NTSTATUS
NTAPI
xHalRegisterBusHandler(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ BUS_DATA_TYPE ConfigSpace,
    _In_ ULONG BusNumber,
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ ULONG ContextSize,
    _In_ PINSTALL_BUS_HANDLER InstallCallback,
    _Out_ PBUS_HANDLER *BusHandler);

NTSTATUS
NTAPI
xHalSetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer);

VOID
NTAPI
xHalSetWakeEnable(
    _In_ BOOLEAN Enable);

NTSTATUS
NTAPI
xHalStartMirroring(VOID);

BOOLEAN
NTAPI
xHalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress);

PVOID
NTAPI
xKdGetAcpiTablePhase(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock,
    _In_ ULONG Signature);

NTSTATUS
NTAPI
xKdReleasePciDeviceForDebugging(
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR PciDevice);

NTSTATUS
NTAPI
xKdSetupPciDeviceForDebugging(
    _In_opt_ PVOID LoaderBlock,
    _Inout_ PDEBUG_DEVICE_DESCRIPTOR PciDevice);

VOID
NTAPI
xKdUnmapVirtualAddress(
    _In_ PVOID VirtualAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB);

VOID
FASTCALL
xHalExamineMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG MbrTypeIdentifier,
    _Out_ PVOID *MbrBuffer);

NTSTATUS
FASTCALL
xHalIoSetPartitionInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _In_ ULONG PartitionType);

NTSTATUS
FASTCALL
xHalIoWritePartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG SectorsPerTrack,
    _In_ ULONG NumberOfHeads,
    _In_ PDRIVE_LAYOUT_INFORMATION PartitionBuffer);

PVOID
NTAPI
MatchAll(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG NumberPages,
    _In_ BOOLEAN FlushCurrentTLB);
