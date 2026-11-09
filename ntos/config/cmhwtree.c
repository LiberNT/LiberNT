/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Writing down what the firmware said the machine is made of
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

/*
 * The fixed part of an SMBIOS structure, and the offsets inside a processor
 * structure that the DMTF specification gives for the strings and the speed.
 */
typedef struct _CM_SMBIOS_HEADER
{
    UCHAR Type;
    UCHAR Length;
    USHORT Handle;
} CM_SMBIOS_HEADER, *PCM_SMBIOS_HEADER;

#define CM_SMBIOS_TYPE_BIOS             0
#define CM_SMBIOS_TYPE_PROCESSOR        4
#define CM_SMBIOS_TYPE_END              127

#define CM_SMBIOS_BIOS_VENDOR            0x04
#define CM_SMBIOS_BIOS_VERSION           0x05
#define CM_SMBIOS_BIOS_RELEASE_DATE      0x08

#define CM_SMBIOS_PROCESSOR_MANUFACTURER 0x07
#define CM_SMBIOS_PROCESSOR_VERSION      0x10
#define CM_SMBIOS_PROCESSOR_SPEED        0x16

static UNICODE_STRING CmpDescriptionPath = RTL_CONSTANT_STRING(
    L"\\REGISTRY\\MACHINE\\HARDWARE\\DESCRIPTION\\SYSTEM");

static USHORT CmpProcessorMHz = 0;

/* PRIVATE FUNCTIONS **********************************************************/

static
PCSTR
CmpSmbiosString(
    _In_ PUCHAR Strings,
    _In_ PUCHAR End,
    _In_ UCHAR Number)
{
    PUCHAR Current = Strings;
    UCHAR Index = 1;

    if (Number == 0) return NULL;

    while (Current < End && *Current != 0)
    {
        if (Index == Number) return (PCSTR)Current;

        while (Current < End && *Current != 0) Current++;
        Current++;
        Index++;
    }

    return NULL;
}

static
VOID
CmpCopyAnsi(
    _Out_writes_(Size) PCHAR Destination,
    _In_ SIZE_T Size,
    _In_opt_ PCSTR Source)
{
    SIZE_T i;

    if (Size == 0) return;

    Destination[0] = ANSI_NULL;
    if (Source == NULL) return;

    for (i = 0; i < Size - 1 && Source[i] != ANSI_NULL; i++)
    {
        Destination[i] = Source[i];
    }

    Destination[i] = ANSI_NULL;
}

static
PUCHAR
CmpMapSmbiosTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PSIZE_T Size)
{
    PSMBIOS3_TABLE_HEADER Entry;
    PSMBIOS_TABLE_HEADER Legacy;
    PHYSICAL_ADDRESS Physical;
    PHYSICAL_ADDRESS Anchor;
    PUCHAR Table = NULL;

    *Size = 0;

    if (LoaderBlock == NULL || LoaderBlock->Extension == NULL) return NULL;
    if (LoaderBlock->Extension->SMBiosEPSHeader == NULL) return NULL;

    /*
     * The loader reports where the firmware's entry point lies, not a mapping
     * of it, so the entry point is read through a mapping of its own.
     */
    Anchor.QuadPart =
        (LONGLONG)(ULONG_PTR)LoaderBlock->Extension->SMBiosEPSHeader;

    Entry = MmMapIoSpace(Anchor, sizeof(SMBIOS3_TABLE_HEADER), MmCached);
    if (Entry == NULL) return NULL;

    Physical.QuadPart = 0;

    if (RtlCompareMemory(Entry->Signature, "_SM3_", 5) == 5)
    {
        Physical.QuadPart = (LONGLONG)Entry->StructureTableAddress;
        *Size = Entry->StructureTableMaximumSize;
    }
    else if (RtlCompareMemory(Entry->Signature, "_SM_", 4) == 4)
    {
        Legacy = (PSMBIOS_TABLE_HEADER)Entry;
        Physical.QuadPart = (LONGLONG)Legacy->StructureTableAddress;
        *Size = Legacy->StructureTableLength;
    }

    MmUnmapIoSpace(Entry, sizeof(SMBIOS3_TABLE_HEADER));

    if (Physical.QuadPart == 0 || *Size == 0)
    {
        *Size = 0;
        return NULL;
    }

    Table = MmMapIoSpace(Physical, *Size, MmCached);
    if (Table == NULL) *Size = 0;

    return Table;
}

/* Walks the firmware's own table of what the machine is made of */
static
PUCHAR
CmpFindSmbiosStructure(
    _In_ PUCHAR Base,
    _In_ SIZE_T Size,
    _In_ UCHAR Type,
    _Out_ PUCHAR *Strings)
{
    PCM_SMBIOS_HEADER Structure;
    PUCHAR Current = Base;
    PUCHAR End = Base + Size;

    *Strings = NULL;

    while (Current + sizeof(CM_SMBIOS_HEADER) <= End)
    {
        Structure = (PCM_SMBIOS_HEADER)Current;

        if (Structure->Type == CM_SMBIOS_TYPE_END) break;
        if (Structure->Length < sizeof(CM_SMBIOS_HEADER)) break;
        if (Current + Structure->Length > End) break;

        *Strings = Current + Structure->Length;

        if (Structure->Type == Type) return Current;

        Current = *Strings;

        while (Current + 1 < End && (Current[0] != 0 || Current[1] != 0))
        {
            Current++;
        }

        Current += 2;
    }

    *Strings = NULL;
    return NULL;
}

static
BOOLEAN
CmpReadSmbiosProcessor(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_writes_opt_(ManufacturerSize) PCHAR Manufacturer,
    _In_ SIZE_T ManufacturerSize,
    _Out_writes_opt_(VersionSize) PCHAR Version,
    _In_ SIZE_T VersionSize,
    _Out_opt_ PUSHORT CurrentSpeed)
{
    PCM_SMBIOS_HEADER Header;
    PUCHAR Structure;
    PUCHAR Strings;
    PUCHAR Mapped;
    SIZE_T Size;

    if (Manufacturer != NULL && ManufacturerSize != 0)
    {
        Manufacturer[0] = ANSI_NULL;
    }

    if (Version != NULL && VersionSize != 0) Version[0] = ANSI_NULL;
    if (CurrentSpeed != NULL) *CurrentSpeed = 0;

    Mapped = CmpMapSmbiosTable(LoaderBlock, &Size);
    if (Mapped == NULL) return FALSE;

    Structure = CmpFindSmbiosStructure(Mapped,
                                       Size,
                                       CM_SMBIOS_TYPE_PROCESSOR,
                                       &Strings);
    if (Structure == NULL)
    {
        MmUnmapIoSpace(Mapped, Size);
        return FALSE;
    }

    Header = (PCM_SMBIOS_HEADER)Structure;

    if (Manufacturer != NULL &&
        Header->Length > CM_SMBIOS_PROCESSOR_MANUFACTURER)
    {
        CmpCopyAnsi(Manufacturer,
                    ManufacturerSize,
                    CmpSmbiosString(Strings,
                                    Mapped + Size,
                                    Structure[CM_SMBIOS_PROCESSOR_MANUFACTURER]));
    }

    if (Version != NULL && Header->Length > CM_SMBIOS_PROCESSOR_VERSION)
    {
        CmpCopyAnsi(Version,
                    VersionSize,
                    CmpSmbiosString(Strings,
                                    Mapped + Size,
                                    Structure[CM_SMBIOS_PROCESSOR_VERSION]));
    }

    if (CurrentSpeed != NULL &&
        Header->Length > CM_SMBIOS_PROCESSOR_SPEED + 1)
    {
        *CurrentSpeed = *(PUSHORT)(Structure + CM_SMBIOS_PROCESSOR_SPEED);
    }

    MmUnmapIoSpace(Mapped, Size);
    return TRUE;
}

static
BOOLEAN
CmpReadSmbiosBios(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_writes_(VendorSize) PCHAR Vendor,
    _In_ SIZE_T VendorSize,
    _Out_writes_(VersionSize) PCHAR Version,
    _In_ SIZE_T VersionSize,
    _Out_writes_(DateSize) PCHAR Date,
    _In_ SIZE_T DateSize)
{
    PCM_SMBIOS_HEADER Header;
    PUCHAR Structure;
    PUCHAR Strings;
    PUCHAR Mapped;
    SIZE_T Size;

    if (VendorSize != 0) Vendor[0] = ANSI_NULL;
    if (VersionSize != 0) Version[0] = ANSI_NULL;
    if (DateSize != 0) Date[0] = ANSI_NULL;

    Mapped = CmpMapSmbiosTable(LoaderBlock, &Size);
    if (Mapped == NULL) return FALSE;

    Structure = CmpFindSmbiosStructure(Mapped,
                                       Size,
                                       CM_SMBIOS_TYPE_BIOS,
                                       &Strings);
    if (Structure == NULL)
    {
        MmUnmapIoSpace(Mapped, Size);
        return FALSE;
    }

    Header = (PCM_SMBIOS_HEADER)Structure;

    if (Header->Length > CM_SMBIOS_BIOS_VENDOR)
    {
        CmpCopyAnsi(Vendor,
                    VendorSize,
                    CmpSmbiosString(Strings,
                                    Mapped + Size,
                                    Structure[CM_SMBIOS_BIOS_VENDOR]));
    }

    if (Header->Length > CM_SMBIOS_BIOS_VERSION)
    {
        CmpCopyAnsi(Version,
                    VersionSize,
                    CmpSmbiosString(Strings,
                                    Mapped + Size,
                                    Structure[CM_SMBIOS_BIOS_VERSION]));
    }

    if (Header->Length > CM_SMBIOS_BIOS_RELEASE_DATE)
    {
        CmpCopyAnsi(Date,
                    DateSize,
                    CmpSmbiosString(Strings,
                                    Mapped + Size,
                                    Structure[CM_SMBIOS_BIOS_RELEASE_DATE]));
    }

    MmUnmapIoSpace(Mapped, Size);
    return TRUE;
}

static
NTSTATUS
CmpCreateHardwareKey(
    _In_opt_ HANDLE Parent,
    _In_ PCUNICODE_STRING Name,
    _Out_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               Parent,
                               NULL);

    return ZwCreateKey(KeyHandle,
                       KEY_ALL_ACCESS,
                       &ObjectAttributes,
                       0,
                       NULL,
                       REG_OPTION_VOLATILE,
                       NULL);
}

static
VOID
CmpWriteAnsiValue(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ PCSTR Value)
{
    UNICODE_STRING Name;
    UNICODE_STRING Wide;
    ANSI_STRING Narrow;

    if (Value == NULL || Value[0] == ANSI_NULL) return;

    RtlInitUnicodeString(&Name, ValueName);
    RtlInitAnsiString(&Narrow, Value);

    if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&Wide, &Narrow, TRUE))) return;

    ZwSetValueKey(KeyHandle,
                  &Name,
                  0,
                  REG_SZ,
                  Wide.Buffer,
                  Wide.Length + sizeof(WCHAR));

    RtlFreeUnicodeString(&Wide);
}

static
VOID
CmpWriteDwordValue(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);
    ZwSetValueKey(KeyHandle, &Name, 0, REG_DWORD, &Value, sizeof(Value));
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
CmpGetSmbiosProcessorStrings(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_writes_(ManufacturerSize) PCHAR Manufacturer,
    _In_ SIZE_T ManufacturerSize,
    _Out_writes_(VersionSize) PCHAR Version,
    _In_ SIZE_T VersionSize)
{
    return CmpReadSmbiosProcessor(LoaderBlock,
                                  Manufacturer,
                                  ManufacturerSize,
                                  Version,
                                  VersionSize,
                                  NULL);
}

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeProcessorClockFromFirmware(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    USHORT Speed = 0;

    CmpReadSmbiosProcessor(LoaderBlock, NULL, 0, NULL, 0, &Speed);
    CmpProcessorMHz = Speed;
}

/*
 * Writes one component the firmware described: a key named after what kind of
 * thing it is, holding a numbered key for each one of that kind.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeRegistryNode(
    _In_ PCONFIGURATION_COMPONENT_DATA CurrentEntry,
    _In_ HANDLE NodeHandle,
    _Out_ PHANDLE NewHandle,
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PUSHORT DeviceIndexTable)
{
    PCONFIGURATION_COMPONENT Component = &CurrentEntry->ComponentEntry;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    CM_COMPONENT_INFORMATION Information;
    UNICODE_STRING ValueName;
    UNICODE_STRING IndexName;
    WCHAR IndexBuffer[16];
    HANDLE TypeHandle;
    HANDLE IndexHandle;
    NTSTATUS Status;
    ULONG FlagBits;
    ULONG Length;
    USHORT Index;

    *NewHandle = NULL;

    if (Component->Type > MaximumType) return STATUS_INVALID_PARAMETER;

    Status = CmpCreateHardwareKey(NodeHandle,
                                  &CmTypeName[Component->Type],
                                  &TypeHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Index = DeviceIndexTable[Component->Type];
    DeviceIndexTable[Component->Type] = Index + 1;

    IndexBuffer[0] = (WCHAR)(L'0' + ((Index / 10) % 10));
    IndexBuffer[1] = (WCHAR)(L'0' + (Index % 10));
    IndexBuffer[2] = UNICODE_NULL;

    RtlInitUnicodeString(&IndexName,
                         (Index < 10) ? &IndexBuffer[1] : IndexBuffer);

    Status = CmpCreateHardwareKey(TypeHandle, &IndexName, &IndexHandle);
    ZwClose(TypeHandle);

    if (!NT_SUCCESS(Status)) return Status;

    FlagBits = (ULONG)Component->Flags;
    RtlCopyMemory(&Information.Flags, &FlagBits, sizeof(ULONG));
    Information.Version = Component->Version;
    Information.Key = Component->Key;
    Information.AffinityMask = Component->AffinityMask;

    RtlInitUnicodeString(&ValueName, L"Component Information");
    ZwSetValueKey(IndexHandle,
                  &ValueName,
                  0,
                  REG_BINARY,
                  &Information,
                  sizeof(Information));

    if (Component->IdentifierLength != 0 && Component->Identifier != NULL)
    {
        CmpWriteAnsiValue(IndexHandle, L"Identifier", Component->Identifier);
    }

    if (Component->Type == CentralProcessor && CmpProcessorMHz != 0)
    {
        CmpWriteDwordValue(IndexHandle, L"~MHz", CmpProcessorMHz);
    }

    if (Component->ConfigurationDataLength != 0 &&
        CurrentEntry->ConfigurationData != NULL)
    {
        Length = (ULONG)FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                     PartialResourceList) +
                 Component->ConfigurationDataLength;

        if (CmpConfigurationData != NULL && Length <= CmpConfigurationAreaSize)
        {
            Full = CmpConfigurationData;
            RtlZeroMemory(Full, Length);
        }
        else
        {
            Full = ExAllocatePoolZero(PagedPool, Length, TAG_CM);
        }

        if (Full != NULL)
        {
            Full->InterfaceType = InterfaceType;
            Full->BusNumber = BusNumber;
            RtlCopyMemory(&Full->PartialResourceList,
                          CurrentEntry->ConfigurationData,
                          Component->ConfigurationDataLength);

            RtlInitUnicodeString(&ValueName, L"Configuration Data");
            ZwSetValueKey(IndexHandle,
                          &ValueName,
                          0,
                          REG_FULL_RESOURCE_DESCRIPTOR,
                          Full,
                          Length);

            if (Full != CmpConfigurationData) ExFreePool(Full);
        }
    }

    *NewHandle = IndexHandle;
    return STATUS_SUCCESS;
}

/*
 * Walks the tree the firmware handed over, writing each component down and
 * carrying the bus a component belongs to into everything beneath it.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpSetupConfigurationTree(
    _In_ PCONFIGURATION_COMPONENT_DATA CurrentEntry,
    _In_ HANDLE NodeHandle,
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PUSHORT DeviceIndexTable)
{
    PCONFIGURATION_COMPONENT Component;
    INTERFACE_TYPE ChildInterface;
    USHORT ChildIndexTable[MaximumType + 1];
    PCMP_MF_TYPE Multifunction;
    ULONG ChildBusNumber;
    HANDLE NewHandle;
    NTSTATUS Status;

    while (CurrentEntry != NULL)
    {
        Component = &CurrentEntry->ComponentEntry;
        ChildInterface = InterfaceType;
        ChildBusNumber = BusNumber;

        if (Component->Type == MultiFunctionAdapter &&
            Component->Identifier != NULL)
        {
            ChildInterface = InterfaceTypeUndefined;

            for (Multifunction = CmpMultifunctionTypes;
                 Multifunction->Identifier != NULL;
                 Multifunction++)
            {
                if (_stricmp(Multifunction->Identifier,
                             Component->Identifier) != 0)
                {
                    continue;
                }

                ChildInterface = (INTERFACE_TYPE)Multifunction->InterfaceType;
                ChildBusNumber = Multifunction->Count;
                Multifunction->Count++;
                break;
            }

            if (ChildInterface == InterfaceTypeUndefined)
            {
                ChildBusNumber = CmpUnknownBusCount;
                CmpUnknownBusCount++;
            }
        }

        Status = CmpInitializeRegistryNode(CurrentEntry,
                                           NodeHandle,
                                           &NewHandle,
                                           InterfaceType,
                                           BusNumber,
                                           DeviceIndexTable);
        if (!NT_SUCCESS(Status)) return Status;

        if (CurrentEntry->Child != NULL)
        {
            RtlZeroMemory(ChildIndexTable, sizeof(ChildIndexTable));

            Status = CmpSetupConfigurationTree(CurrentEntry->Child,
                                               NewHandle,
                                               ChildInterface,
                                               ChildBusNumber,
                                               ChildIndexTable);
            if (!NT_SUCCESS(Status))
            {
                ZwClose(NewHandle);
                return Status;
            }
        }

        ZwClose(NewHandle);
        CurrentEntry = CurrentEntry->Sibling;
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeHardwareConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    USHORT DeviceIndexTable[MaximumType + 1];
    CHAR BiosVendor[64];
    CHAR BiosVersion[64];
    CHAR BiosDate[32];
    HANDLE DescriptionHandle;
    NTSTATUS Status;

    RtlZeroMemory(DeviceIndexTable, sizeof(DeviceIndexTable));
    RtlZeroMemory(CmpTypeCount, sizeof(CmpTypeCount));

    CmpInitializeProcessorClockFromFirmware(LoaderBlock);

    Status = CmpCreateHardwareKey(NULL,
                                  &CmpDescriptionPath,
                                  &DescriptionHandle);
    if (!NT_SUCCESS(Status)) return Status;

    CmpReadSmbiosBios(LoaderBlock,
                      BiosVendor,
                      sizeof(BiosVendor),
                      BiosVersion,
                      sizeof(BiosVersion),
                      BiosDate,
                      sizeof(BiosDate));

    CmpWriteAnsiValue(DescriptionHandle, L"Identifier", BiosVendor);
    CmpWriteAnsiValue(DescriptionHandle, L"SystemBiosVersion", BiosVersion);
    CmpWriteAnsiValue(DescriptionHandle, L"SystemBiosDate", BiosDate);

    if (LoaderBlock->ConfigurationRoot != NULL)
    {
        Status = CmpSetupConfigurationTree(LoaderBlock->ConfigurationRoot,
                                           DescriptionHandle,
                                           Internal,
                                           0,
                                           DeviceIndexTable);
    }

    ZwClose(DescriptionHandle);

    if (!NT_SUCCESS(Status)) return Status;

    return CmpInitializeMachineDependentConfiguration(LoaderBlock);
}
