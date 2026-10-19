/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware services used by the common code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

extern const USHORT FwApplicationSubsystem;

typedef enum _FW_VOLUME_SCAN_STATE
{
    FwVolumeScanConnect,
    FwVolumeScanVolumes,
    FwVolumeScanDone
} FW_VOLUME_SCAN_STATE;

typedef struct _FW_VOLUME_SCAN
{
    FW_VOLUME_SCAN_STATE State;
    PVOID* Handles;
    ULONG HandleCount;
    ULONG Index;
} FW_VOLUME_SCAN, *PFW_VOLUME_SCAN;

PVOID
FwAllocate(
    _In_ SIZE_T Size);

VOID
FwFree(
    _In_ PVOID Memory);

VOID
FwPrint(
    _In_ PCWSTR Format,
    ...);

PVOID
FwReadBootFile(
    _In_ PCWSTR Path,
    _Out_ PULONG Size);

PVOID
FwReadDeviceFile(
    _In_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size);

BOOLEAN
FwScanFileVolumes(
    _In_ PCWSTR Path,
    _Inout_ PFW_VOLUME_SCAN Scan,
    _Out_ PVOID* Device);

BOOLEAN
FwStartFirmwareApplication(
    _In_ PVOID Device,
    _In_ PCWSTR Path);

DECLSPEC_NORETURN
VOID
FwHalt(VOID);

PVOID
FwAllocatePages(
    _In_ SIZE_T Size);

PVOID
FwAllocatePagesBelow(
    _In_ SIZE_T Size,
    _In_ ULONGLONG MaxAddress);

BOOLEAN
FwAllocatePagesAt(
    _In_ PVOID Address,
    _In_ SIZE_T Size);

VOID
FwFreePages(
    _In_ PVOID Memory,
    _In_ SIZE_T Size);

BOOLEAN
FwGetDisk(
    _In_ ULONG Index,
    _Out_ PDK_DEVICE Disk);

BOOLEAN
FwRescanDisks(VOID);

BOOLEAN
FwGetBootDevice(
    _Out_ PDK_DEVICE Device);

BOOLEAN
FwReadBlocks(
    _In_ PVOID Firmware,
    _In_ ULONGLONG Block,
    _In_ ULONG Count,
    _Out_ PVOID Buffer);

BOOLEAN
FwBufferAligned(
    _In_ PVOID Firmware,
    _In_ PVOID Buffer);

BOOLEAN
FwGetBootMedium(
    _Out_ PDK_DEVICE Device);

PVOID
FwDownloadFile(
    _In_ PCSTR Url,
    _In_opt_ PCSTR Address,
    _Out_ PULONG Size);

struct _FREELDR_MEMORY_DESCRIPTOR*
FwGetMemoryMap(
    _Out_ PULONG Count);

VOID
FwExitBootServices(VOID);

VOID
FwSetVirtualAddressMap(VOID);

struct _EFI_FIRMWARE_INFORMATION;
struct _VIRTUAL_EFI_RUNTIME_SERVICES;

BOOLEAN
FwPrepareRuntimeServices(
    _Inout_ struct _EFI_FIRMWARE_INFORMATION* Information,
    _Inout_ struct _VIRTUAL_EFI_RUNTIME_SERVICES* Services);

VOID
FwDebugPrint(
    _In_ PCSTR Text);

extern VOID (*FwLateDebugPutChar)(UCHAR Character);

PVOID
FwGetAcpiRootPointer(VOID);

PVOID
FwGetSmbiosEntryPoint(VOID);

struct _LOADER_FIRMWARE_RESOURCE;

BOOLEAN
FwGetFirmwareResource(
    _In_ ULONG Index,
    _Out_ struct _LOADER_FIRMWARE_RESOURCE* Resource);

struct _CM_FRAMEBUF_DEVICE_DATA;

BOOLEAN
FwGetFrameBuffer(
    _Out_ struct _CM_FRAMEBUF_DEVICE_DATA* Data,
    _Out_ PULONGLONG Base,
    _Out_ PULONG Size);

BOOLEAN
FwGetTime(
    _Out_ PTIME_FIELDS Time);

PCWSTR
FwGetLoadOptions(
    _Out_ PULONG Chars);

BOOLEAN
FwGetBootDiskIndex(
    _Out_ PULONG Index);

BOOLEAN
FwGetBootPartition(
    _Out_ PULONG PartitionNumber,
    _Out_writes_bytes_(16) PUCHAR Signature,
    _Out_ PBOOLEAN IsGpt);

BOOLEAN
FwStartImage(
    _In_reads_bytes_(Size) PVOID Image,
    _In_ ULONG Size,
    _In_ PCWSTR LoadOptions);

BOOLEAN
FwGetLoaderImage(
    _Out_ PVOID* Base,
    _Out_ PSIZE_T Size);

BOOLEAN
FwGetImageDirectory(
    _Out_writes_z_(Chars) PWSTR Directory,
    _In_ ULONG Chars);

BOOLEAN
FwGetEdid(
    _Out_writes_bytes_(Size) PUCHAR Edid,
    _In_ ULONG Size);

VOID
StallExecutionProcessor(
    ULONG Microseconds);
