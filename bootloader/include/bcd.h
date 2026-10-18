/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot Configuration Data store access
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BCD_OBJECT_BOOTMGR  0x10100002
#define BCD_OBJECT_OSLOADER 0x10200003

#define BCD_ID_BOOTMGR  L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define BCD_ID_CHARS    39

#define BCD_MAX_INHERIT_DEPTH 8

typedef enum _BCD_ELEMENT
{
    BcdLibraryDevice_ApplicationDevice       = 0x11000001,
    BcdLibraryString_ApplicationPath         = 0x12000002,
    BcdLibraryString_Description             = 0x12000004,
    BcdLibraryObjectList_InheritedObjects    = 0x14000006,
    BcdLibraryInteger_SerialDebuggerPort     = 0x15000013,
    BcdLibraryInteger_SerialDebuggerBaudRate = 0x15000014,
    BcdLibraryString_LoadOptionsString       = 0x12000030,

    BcdOSLoaderDevice_OSDevice               = 0x21000001,
    BcdOSLoaderString_SystemRoot             = 0x22000002,
    BcdOSLoaderBoolean_DetectKernelAndHal    = 0x26000010,
    BcdOSLoaderString_KernelPath             = 0x22000011,
    BcdOSLoaderString_HalPath                = 0x22000012,
    BcdOSLoaderBoolean_WinPEMode             = 0x26000022,
    BcdOSLoaderBoolean_UseLastGoodSettings   = 0x26000025,
    BcdOSLoaderBoolean_UseVgaDriver          = 0x26000040,
    BcdOSLoaderBoolean_DisableBootDisplay    = 0x26000041,
    BcdOSLoaderBoolean_UseBootProcessorOnly  = 0x26000060,
    BcdOSLoaderInteger_NumberOfProcessors    = 0x25000061,
    BcdOSLoaderBoolean_BootLogInitialization = 0x26000090,
    BcdOSLoaderBoolean_VerboseObjectLoadMode = 0x26000091,
    BcdOSLoaderBoolean_KernelDebuggerEnabled = 0x260000a0,
    BcdOSLoaderBoolean_EmsEnabled            = 0x260000b0,

    BcdBootMgrObjectList_DisplayOrder        = 0x24000001,
    BcdBootMgrObject_DefaultObject           = 0x23000003,
    BcdBootMgrInteger_Timeout                = 0x25000004,
    BcdBootMgrBoolean_DisplayBootMenu        = 0x26000020
} BCD_ELEMENT;

typedef struct _BCD_OBJECT
{
    HCELL_INDEX Elements;
    ULONG Type;
} BCD_OBJECT, *PBCD_OBJECT;

#define BCD_PATH_CHARS 260

typedef enum _BCD_DEVICE_TYPE
{
    BcdDeviceBoot,
    BcdDeviceGptPartition,
    BcdDeviceMbrPartition
} BCD_DEVICE_TYPE;

typedef struct _BCD_DEVICE
{
    BCD_DEVICE_TYPE Type;
    UCHAR Partition[16];
    UCHAR Disk[16];
    ULONGLONG PartitionOffset;
    ULONG Signature;
    BOOLEAN Ramdisk;
    WCHAR RamdiskPath[BCD_PATH_CHARS];
} BCD_DEVICE, *PBCD_DEVICE;

PVOID
BcdReadStoreFile(
    _In_opt_ PVOID Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size);

BOOLEAN
BcdOpenStore(
    _In_reads_bytes_(Size) PVOID HiveBase,
    _In_ ULONG Size);

BOOLEAN
BcdOpenObject(
    _In_ PCWSTR Id,
    _Out_ PBCD_OBJECT Object);

BOOLEAN
BcdGetString(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_writes_z_(Chars) PWSTR Buffer,
    _In_ ULONG Chars);

BOOLEAN
BcdGetObjectId(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _In_ ULONG Index,
    _Out_writes_z_(BCD_ID_CHARS) PWSTR Id);

BOOLEAN
BcdGetInteger(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PULONGLONG Value);

BOOLEAN
BcdGetBoolean(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PBOOLEAN Value);

BOOLEAN
BcdGetDevice(
    _In_ PBCD_OBJECT Object,
    _In_ ULONG Element,
    _Out_ PBCD_DEVICE Device);

HCELL_INDEX
BlRegSubKey(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name);

PUCHAR
BlRegValue(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Type,
    _Out_ PULONG Length);
