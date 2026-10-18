/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Operating system loader internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <bl.h>
#include <internal/cmboot.h>
#include <drivers/acpi/acpi.h>
#include <drivers/bootvid/framebuf.h>
#include <loader_framebuffer.h>
#include <loader_firmware.h>

#define OSL_PATH_CHARS    260
#define OSL_OPTIONS_CHARS BM_LOAD_OPTIONS_CHARS
#define OSL_NT_VERSION    _WIN32_WINNT

typedef struct _OSL_SYSTEM_BLOCK
{
    LOADER_PARAMETER_BLOCK LoaderBlock;
    LOADER_PARAMETER_EXTENSION Extension;
    NLS_DATA_BLOCK NlsDataBlock;
    CHAR LoadOptions[OSL_OPTIONS_CHARS];
    CHAR ArcBootDeviceName[OSL_PATH_CHARS];
    CHAR ArcHalDeviceName[OSL_PATH_CHARS];
    CHAR NtBootPathName[OSL_PATH_CHARS];
    CHAR NtHalPathName[OSL_PATH_CHARS];
    ARC_DISK_INFORMATION ArcDiskInformation;
    LOADER_PERFORMANCE_DATA LoaderPerformanceData;
} OSL_SYSTEM_BLOCK, *POSL_SYSTEM_BLOCK;

extern POSL_SYSTEM_BLOCK OslSystemBlock;
extern PCM_FRAMEBUF_DEVICE_DATA FrameBufferData;
extern PHHIVE OslSystemHive;
extern HCELL_INDEX OslControlSet;

VOID
OslConvertList(
    _Inout_ PLIST_ENTRY ListHead);

PLOADER_PARAMETER_BLOCK
OslAllocateLoaderBlock(VOID);

BOOLEAN
OslFinalizeLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE OsDevice,
    _In_ PCSTR SystemRoot,
    _In_ PCSTR LoadOptions);

PCONFIGURATION_COMPONENT_DATA
OslCreateComponent(
    _In_opt_ PCONFIGURATION_COMPONENT_DATA Parent,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_ IDENTIFIER_FLAG Flags,
    _In_ PCSTR Identifier,
    _In_opt_ PVOID Data,
    _In_ ULONG DataSize);

PCM_PARTIAL_RESOURCE_LIST
OslAllocateResourceList(
    _In_ ULONG Descriptors,
    _In_ ULONG ExtraSize,
    _Out_ PULONG Size);

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System);

PCONFIGURATION_COMPONENT_DATA
OslBuildConfigurationTree(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
OslConvertConfigurationTree(VOID);

BOOLEAN
OslGetSystemIdentifier(
    _Out_writes_z_(Size) PSTR Buffer,
    _In_ SIZE_T Size);

BOOLEAN
OslGetMonitorData(
    _Out_ PCM_MONITOR_DEVICE_DATA Monitor,
    _Out_ PULONG RefreshRate);

BOOLEAN
OslLoadSystemHive(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot);

BOOLEAN
OslLoadNlsData(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot);

BOOLEAN
OslLoadBootDrivers(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot);

BOOLEAN
WinLdrSetupMemoryLayout(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
OslLoadDriverDatabase(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR SystemRoot);

VOID
AppendBootTimeOptions(
    _Inout_updates_z_(BootOptionsSize) PSTR BootOptions,
    _In_ SIZE_T BootOptionsSize,
    _In_ PBM_STARTUP Startup);

BOOLEAN
OslBuildRamdisk(
    _In_ PBCD_DEVICE BcdDevice,
    _In_reads_bytes_opt_(ImageSize) PVOID Image,
    _In_ ULONG ImageSize,
    _In_ ULONGLONG ExtraBytes,
    _Out_ PDK_DEVICE Device);

BOOLEAN
OslGetRamdisk(
    _Out_ PUCHAR* Base,
    _Out_ PULONGLONG Size);

VOID
OslArchInitialize(VOID);

VOID
OslArchSwitchStack(VOID);

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

extern const PCSTR OslArchSystemIdentifier;

TYPE_OF_MEMORY
OslArchDescriptorType(
    _In_ PFN_NUMBER Page,
    _In_ TYPE_OF_MEMORY Type);

BOOLEAN
OslArchMapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart);

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion);

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping);

VOID
MempDump(VOID);
