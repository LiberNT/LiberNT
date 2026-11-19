/*
 * PROJECT:     LiberNT Storport AHCI miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AHCI 1.3.1 register, FIS and command structures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <storport.h>

#define AHCI_TAG 'icha'

#define AHCI_MAX_PORTS 32
#define AHCI_PORT_REGISTERS_OFFSET 0x100
#define AHCI_PORT_REGISTERS_SIZE 0x80
#define AHCI_ABAR_MIN_LENGTH (AHCI_PORT_REGISTERS_OFFSET + AHCI_PORT_REGISTERS_SIZE)
#define AHCI_PCI_ABAR_OFFSET 0x24
#define AHCI_ASC_SAVING_NOT_SUPPORTED 0x39

#define AHCI_MAX_TRANSFER_LENGTH (1024 * 1024)
#define AHCI_MAX_PRDT_ENTRIES ((AHCI_MAX_TRANSFER_LENGTH / PAGE_SIZE) + 1)
#define AHCI_PRD_MAX_BYTES (4 * 1024 * 1024)

/* Generic host control */
#define AHCI_HBA_CAP 0x00
#define AHCI_HBA_GHC 0x04
#define AHCI_HBA_IS 0x08
#define AHCI_HBA_PI 0x0C
#define AHCI_HBA_VS 0x10
#define AHCI_HBA_CAP2 0x24
#define AHCI_HBA_BOHC 0x28

#define AHCI_CAP_NP_MASK 0x1F
#define AHCI_CAP_SSS (1u << 27)
#define AHCI_CAP_SCLO (1u << 24)
#define AHCI_CAP_SAM (1u << 18)
#define AHCI_CAP_S64A (1u << 31)

#define AHCI_GHC_HR (1u << 0)
#define AHCI_GHC_IE (1u << 1)
#define AHCI_GHC_AE (1u << 31)

#define AHCI_CAP2_BOH (1u << 0)
#define AHCI_BOHC_BOS (1u << 0)
#define AHCI_BOHC_OOS (1u << 1)
#define AHCI_BOHC_BB (1u << 4)

/* Port registers */
#define AHCI_PX_CLB 0x00
#define AHCI_PX_CLBU 0x04
#define AHCI_PX_FB 0x08
#define AHCI_PX_FBU 0x0C
#define AHCI_PX_IS 0x10
#define AHCI_PX_IE 0x14
#define AHCI_PX_CMD 0x18
#define AHCI_PX_TFD 0x20
#define AHCI_PX_SIG 0x24
#define AHCI_PX_SSTS 0x28
#define AHCI_PX_SCTL 0x2C
#define AHCI_PX_SERR 0x30
#define AHCI_PX_SACT 0x34
#define AHCI_PX_CI 0x38

#define AHCI_PX_IS_DHRS (1u << 0)
#define AHCI_PX_IS_PSS (1u << 1)
#define AHCI_PX_IS_DSS (1u << 2)
#define AHCI_PX_IS_SDBS (1u << 3)
#define AHCI_PX_IS_UFS (1u << 4)
#define AHCI_PX_IS_DPS (1u << 5)
#define AHCI_PX_IS_PCS (1u << 6)
#define AHCI_PX_IS_PRCS (1u << 22)
#define AHCI_PX_IS_OFS (1u << 24)
#define AHCI_PX_IS_INFS (1u << 26)
#define AHCI_PX_IS_IFS (1u << 27)
#define AHCI_PX_IS_HBDS (1u << 28)
#define AHCI_PX_IS_HBFS (1u << 29)
#define AHCI_PX_IS_TFES (1u << 30)

#define AHCI_PX_IS_ERROR (AHCI_PX_IS_UFS | AHCI_PX_IS_OFS | AHCI_PX_IS_INFS | AHCI_PX_IS_IFS | \
                          AHCI_PX_IS_HBDS | AHCI_PX_IS_HBFS | AHCI_PX_IS_TFES)
#define AHCI_PX_IS_PRESENCE (AHCI_PX_IS_PCS | AHCI_PX_IS_PRCS)
#define AHCI_PX_IE_DEFAULT (AHCI_PX_IS_DHRS | AHCI_PX_IS_PSS | AHCI_PX_IS_DSS | AHCI_PX_IS_SDBS | \
                            AHCI_PX_IS_DPS | AHCI_PX_IS_ERROR | AHCI_PX_IS_PRESENCE)

#define AHCI_PX_CMD_ST (1u << 0)
#define AHCI_PX_CMD_SUD (1u << 1)
#define AHCI_PX_CMD_POD (1u << 2)
#define AHCI_PX_CMD_CLO (1u << 3)
#define AHCI_PX_CMD_FRE (1u << 4)
#define AHCI_PX_CMD_FR (1u << 14)
#define AHCI_PX_CMD_CR (1u << 15)
#define AHCI_PX_CMD_ATAPI (1u << 24)
#define AHCI_PX_CMD_ICC_MASK (0xFu << 28)
#define AHCI_PX_CMD_ICC_ACTIVE (1u << 28)

#define AHCI_PX_TFD_ERR (1u << 0)
#define AHCI_PX_TFD_DRQ (1u << 3)
#define AHCI_PX_TFD_BSY (1u << 7)
#define AHCI_PX_TFD_ERROR_SHIFT 8

#define AHCI_PX_SSTS_DET_MASK 0xF
#define AHCI_PX_SSTS_DET_PRESENT 0x3
#define AHCI_PX_SCTL_DET_MASK 0xF
#define AHCI_PX_SCTL_DET_COMRESET 0x1
#define AHCI_PX_SCTL_IPM_DISABLED (0x3u << 8)

#define AHCI_SIG_ATA 0x00000101
#define AHCI_SIG_ATAPI 0xEB140101

/* FIS types */
#define AHCI_FIS_REGISTER_H2D 0x27
#define AHCI_FIS_H2D_COMMAND 0x80
#define AHCI_RFIS_D2H_OFFSET 0x40
#define AHCI_RFIS_PIO_SETUP_OFFSET 0x20
#define AHCI_RECEIVED_FIS_SIZE 256
#define AHCI_COMMAND_LIST_SIZE 1024

/* ATA commands */
#define ATA_CMD_READ_DMA 0xC8
#define ATA_CMD_WRITE_DMA 0xCA
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_FLUSH_CACHE 0xE7
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA
#define ATA_CMD_IDENTIFY_DEVICE 0xEC
#define ATA_CMD_IDENTIFY_PACKET_DEVICE 0xA1
#define ATA_CMD_PACKET 0xA0
#define ATA_DEVICE_LBA 0x40

#define ATA_ERROR_ICRC 0x80
#define ATA_ERROR_UNC 0x40
#define ATA_ERROR_IDNF 0x10
#define ATA_ERROR_ABRT 0x04

/* IDENTIFY (PACKET) DEVICE word indices */
#define ATA_ID_CONFIG 0
#define ATA_ID_SERIAL 10
#define ATA_ID_SERIAL_WORDS 10
#define ATA_ID_FIRMWARE 23
#define ATA_ID_FIRMWARE_WORDS 4
#define ATA_ID_MODEL 27
#define ATA_ID_MODEL_WORDS 20
#define ATA_ID_CAPABILITIES 49
#define ATA_ID_LBA28_SECTORS 60
#define ATA_ID_COMMAND_SET_2 83
#define ATA_ID_COMMAND_SET_ENABLED_1 85
#define ATA_ID_COMMAND_SET_ENABLED_2 86
#define ATA_ID_LBA48_SECTORS 100
#define ATA_ID_SECTOR_SIZE 106
#define ATA_ID_LOGICAL_SECTOR_WORDS 117
#define ATA_ID_ROTATION_RATE 217

#define ATA_ID_CONFIG_ATAPI_MASK 0xC000
#define ATA_ID_CONFIG_ATAPI 0x8000
#define ATA_ID_CONFIG_REMOVABLE 0x0080
#define ATA_ID_CONFIG_ATAPI_TYPE_SHIFT 8
#define ATA_ID_CONFIG_ATAPI_TYPE_MASK 0x1F
#define ATA_ID_CAPABILITIES_DMA 0x0100
#define ATA_ID_COMMAND_SET_2_LBA48 0x0400
#define ATA_ID_COMMAND_SET_2_FLUSH_EXT 0x2000
#define ATA_ID_COMMAND_SET_ENABLED_1_WCE 0x0020
#define ATA_ID_SECTOR_SIZE_VALID_MASK 0xC000
#define ATA_ID_SECTOR_SIZE_VALID 0x4000
#define ATA_ID_SECTOR_SIZE_LARGE_LOGICAL 0x1000
#define ATA_ID_SECTOR_SIZE_MULTIPLE_PHYSICAL 0x2000
#define ATA_ID_SECTOR_SIZE_EXPONENT_MASK 0x000F

#pragma pack(push, 1)

typedef struct _AHCI_COMMAND_HEADER
{
    ULONG Flags;
    ULONG TransferredBytes;
    ULONG TableAddress;
    ULONG TableAddressUpper;
    ULONG Reserved[4];
} AHCI_COMMAND_HEADER, *PAHCI_COMMAND_HEADER;

#define AHCI_HEADER_CFL_MASK 0x1F
#define AHCI_HEADER_ATAPI (1u << 5)
#define AHCI_HEADER_WRITE (1u << 6)
#define AHCI_HEADER_PRDTL_SHIFT 16

typedef struct _AHCI_PRD
{
    ULONG DataAddress;
    ULONG DataAddressUpper;
    ULONG Reserved;
    ULONG ByteCount;
} AHCI_PRD, *PAHCI_PRD;

#define AHCI_PRD_INTERRUPT (1u << 31)

typedef struct _AHCI_FIS_H2D
{
    UCHAR Type;
    UCHAR Flags;
    UCHAR Command;
    UCHAR Features;
    UCHAR Lba0;
    UCHAR Lba1;
    UCHAR Lba2;
    UCHAR Device;
    UCHAR Lba3;
    UCHAR Lba4;
    UCHAR Lba5;
    UCHAR FeaturesExp;
    UCHAR Count;
    UCHAR CountExp;
    UCHAR Icc;
    UCHAR Control;
    ULONG Reserved;
} AHCI_FIS_H2D, *PAHCI_FIS_H2D;

typedef struct _AHCI_FIS_D2H
{
    UCHAR Type;
    UCHAR Flags;
    UCHAR Status;
    UCHAR Error;
    UCHAR Lba0;
    UCHAR Lba1;
    UCHAR Lba2;
    UCHAR Device;
    UCHAR Lba3;
    UCHAR Lba4;
    UCHAR Lba5;
    UCHAR Reserved0;
    UCHAR Count;
    UCHAR CountExp;
    UCHAR Reserved1[6];
} AHCI_FIS_D2H, *PAHCI_FIS_D2H;

typedef struct _AHCI_COMMAND_TABLE
{
    UCHAR CommandFis[64];
    UCHAR AtapiCommand[16];
    UCHAR Reserved[48];
    AHCI_PRD Prdt[AHCI_MAX_PRDT_ENTRIES];
} AHCI_COMMAND_TABLE, *PAHCI_COMMAND_TABLE;

#pragma pack(pop)

C_ASSERT(sizeof(AHCI_COMMAND_HEADER) == 32);
C_ASSERT(sizeof(AHCI_PRD) == 16);
C_ASSERT(sizeof(AHCI_FIS_H2D) == 20);
C_ASSERT(sizeof(AHCI_FIS_D2H) == 20);
C_ASSERT(FIELD_OFFSET(AHCI_COMMAND_TABLE, AtapiCommand) == 0x40);
C_ASSERT(FIELD_OFFSET(AHCI_COMMAND_TABLE, Prdt) == 0x80);

#ifndef ROUND_UP
#define ROUND_UP(n, align) ((((n) + (align) - 1) / (align)) * (align))
#endif

#define AHCI_SENSE_BUFFER_SIZE 64
#define AHCI_IDENTIFY_SIZE 512

#define AHCI_PORT_DMA_SIZE ROUND_UP(AHCI_COMMAND_LIST_SIZE + AHCI_RECEIVED_FIS_SIZE + \
                                    sizeof(AHCI_COMMAND_TABLE) + AHCI_SENSE_BUFFER_SIZE + \
                                    AHCI_IDENTIFY_SIZE, AHCI_COMMAND_LIST_SIZE)

typedef enum _AHCI_DEVICE_TYPE
{
    AhciDeviceNone,
    AhciDeviceAta,
    AhciDeviceAtapi
} AHCI_DEVICE_TYPE;

typedef enum _AHCI_OPERATION
{
    AhciOperationData,
    AhciOperationPassThrough,
    AhciOperationAtapi,
    AhciOperationSense
} AHCI_OPERATION;

typedef enum _AHCI_COMMAND_KIND
{
    AhciCommandIdle,
    AhciCommandSrb,
    AhciCommandSense
} AHCI_COMMAND_KIND;

typedef struct _AHCI_PORT
{
    ULONG Index;
    PUCHAR Registers;
    PAHCI_COMMAND_HEADER CommandList;
    PHYSICAL_ADDRESS CommandListAddress;
    PUCHAR ReceivedFis;
    PHYSICAL_ADDRESS ReceivedFisAddress;
    PAHCI_COMMAND_TABLE CommandTable;
    PHYSICAL_ADDRESS CommandTableAddress;
    PUCHAR SenseBuffer;
    PHYSICAL_ADDRESS SenseBufferAddress;
    PUSHORT Identify;
    PHYSICAL_ADDRESS IdentifyAddress;

    AHCI_DEVICE_TYPE DeviceType;
    BOOLEAN Lba48;
    BOOLEAN FlushExt;
    BOOLEAN WriteCacheEnabled;
    BOOLEAN AtapiDma;
    BOOLEAN Removable;
    UCHAR AtapiDeviceType;
    ULONG SectorSize;
    ULONG PhysicalSectorExponent;
    ULONGLONG SectorCount;
    USHORT RotationRate;
    CHAR Model[ATA_ID_MODEL_WORDS * 2 + 1];
    CHAR Serial[ATA_ID_SERIAL_WORDS * 2 + 1];
    CHAR Firmware[ATA_ID_FIRMWARE_WORDS * 2 + 1];

    AHCI_COMMAND_KIND ActiveKind;
    AHCI_OPERATION ActiveOperation;
    PSCSI_REQUEST_BLOCK ActiveSrb;
    UCHAR FailedError;
    BOOLEAN PassThroughCheckCondition;
} AHCI_PORT, *PAHCI_PORT;

typedef struct _AHCI_ADAPTER
{
    PUCHAR Abar;
    ULONG Capabilities;
    ULONG Capabilities2;
    ULONG PortsImplemented;
    ULONG BusCount;
    BOOLEAN Dma64;
    AHCI_PORT Ports[AHCI_MAX_PORTS];
} AHCI_ADAPTER, *PAHCI_ADAPTER;
