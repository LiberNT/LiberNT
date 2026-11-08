/*
 * PROJECT:     LiberNT ARM64
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 ACPI table definitions shared by boot and HAL code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/acpi/acpi.h>

#define ARM64_ACPI_MADT_TYPE_INTERRUPT_OVERRIDE     0x02
#define ARM64_ACPI_MADT_TYPE_NMI_SOURCE             0x03
#define ARM64_ACPI_MADT_TYPE_GENERIC_INTERRUPT      0x0B
#define ARM64_ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR    0x0C
#define ARM64_ACPI_MADT_TYPE_GENERIC_MSI_FRAME      0x0D
#define ARM64_ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR  0x0E
#define ARM64_ACPI_MADT_TYPE_GENERIC_TRANSLATOR     0x0F

#pragma pack(push, 1)

typedef struct _ARM64_ACPI_MADT_SUBTABLE
{
    UCHAR Type;
    UCHAR Length;
} ARM64_ACPI_MADT_SUBTABLE, *PARM64_ACPI_MADT_SUBTABLE;

typedef struct _ARM64_ACPI_MADT
{
    DESCRIPTION_HEADER Header;
    ULONG LocalInterruptControllerAddress;
    ULONG Flags;
} ARM64_ACPI_MADT, *PARM64_ACPI_MADT;

typedef struct _ARM64_ACPI_MADT_GENERIC_DISTRIBUTOR
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT Reserved;
    ULONG GicId;
    ULONGLONG BaseAddress;
    ULONG SystemVectorBase;
    UCHAR GicVersion;
    UCHAR Reserved2[3];
} ARM64_ACPI_MADT_GENERIC_DISTRIBUTOR, *PARM64_ACPI_MADT_GENERIC_DISTRIBUTOR;

typedef struct _ARM64_ACPI_MADT_GENERIC_MSI_FRAME
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT Reserved;
    ULONG MsiFrameId;
    ULONGLONG BaseAddress;
    ULONG Flags;
    USHORT SpiCount;
    USHORT SpiBase;
} ARM64_ACPI_MADT_GENERIC_MSI_FRAME, *PARM64_ACPI_MADT_GENERIC_MSI_FRAME;

typedef struct _ARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT Reserved;
    ULONGLONG BaseAddress;
    ULONG Length;
} ARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR, *PARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR;

typedef struct _ARM64_ACPI_MADT_GENERIC_INTERRUPT
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT Reserved;
    ULONG CpuInterfaceNumber;
    ULONG AcpiProcessorUid;
    ULONG Flags;
    ULONG ParkingProtocolVersion;
    ULONG PerformanceInterrupt;
    ULONGLONG ParkedAddress;
    ULONGLONG BaseAddress;
    ULONGLONG GicvBaseAddress;
    ULONGLONG GichBaseAddress;
    ULONG VgicMaintenanceInterrupt;
    ULONGLONG GicrBaseAddress;
    ULONGLONG Mpidr;
    UCHAR EfficiencyClass;
    UCHAR Reserved2[1];
    USHORT SpeInterrupt;
} ARM64_ACPI_MADT_GENERIC_INTERRUPT, *PARM64_ACPI_MADT_GENERIC_INTERRUPT;

typedef struct _ARM64_ACPI_MADT_GENERIC_TRANSLATOR
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT Reserved;
    ULONG TranslationId;
    ULONGLONG BaseAddress;
    ULONG Reserved2;
} ARM64_ACPI_MADT_GENERIC_TRANSLATOR, *PARM64_ACPI_MADT_GENERIC_TRANSLATOR;

typedef struct _ARM64_ACPI_MADT_INTERRUPT_OVERRIDE
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    UCHAR Bus;
    UCHAR SourceIrq;
    ULONG GlobalSystemInterrupt;
    USHORT IntiFlags;
} ARM64_ACPI_MADT_INTERRUPT_OVERRIDE, *PARM64_ACPI_MADT_INTERRUPT_OVERRIDE;

typedef struct _ARM64_ACPI_MADT_NMI_SOURCE
{
    ARM64_ACPI_MADT_SUBTABLE Header;
    USHORT IntiFlags;
    ULONG GlobalSystemInterrupt;
} ARM64_ACPI_MADT_NMI_SOURCE, *PARM64_ACPI_MADT_NMI_SOURCE;

typedef struct _ARM64_ACPI_GTDT
{
    DESCRIPTION_HEADER Header;
    ULONGLONG CounterBlockAddress;
    ULONG Reserved;
    ULONG SecureEl1Interrupt;
    ULONG SecureEl1Flags;
    ULONG NonSecureEl1Interrupt;
    ULONG NonSecureEl1Flags;
    ULONG VirtualTimerInterrupt;
    ULONG VirtualTimerFlags;
    ULONG NonSecureEl2Interrupt;
    ULONG NonSecureEl2Flags;
    ULONGLONG CounterReadBlockAddress;
    ULONG PlatformTimerCount;
    ULONG PlatformTimerOffset;
} ARM64_ACPI_GTDT, *PARM64_ACPI_GTDT;

typedef struct _ARM64_ACPI_SPCR
{
    DESCRIPTION_HEADER Header;
    UCHAR InterfaceType;
    UCHAR Reserved[3];
    GEN_ADDR SerialPort;
    UCHAR InterruptType;
    UCHAR PcInterrupt;
    ULONG Interrupt;
    UCHAR BaudRate;
    UCHAR Parity;
    UCHAR StopBits;
    UCHAR FlowControl;
    UCHAR TerminalType;
    UCHAR Reserved1;
    USHORT PciDeviceId;
    USHORT PciVendorId;
    UCHAR PciBus;
    UCHAR PciDevice;
    UCHAR PciFunction;
    ULONG PciFlags;
    UCHAR PciSegment;
    ULONG Reserved2;
} ARM64_ACPI_SPCR, *PARM64_ACPI_SPCR;

#pragma pack(pop)
