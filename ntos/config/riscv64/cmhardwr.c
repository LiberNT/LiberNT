/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Machine-dependent registry configuration for RISC-V64
 *
 * Publishes HARDWARE\DESCRIPTION\System\CentralProcessor\<n> from the
 * device-tree/SBI processor record. RISC-V has no CPUID/MIDR:
 * identity comes from firmware, so the values below are firmware strings.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
VOID
CmpRiscvSetString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ, (PVOID)Value, (ULONG)((wcslen(Value) + 1) * sizeof(WCHAR)));
    if (!NT_SUCCESS(Status))
        DPRINT1("RISC-V: failed to set %S: 0x%lx\n", Name, Status);
}

static
VOID
CmpRiscvSetDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

typedef struct _CMP_RISCV_VENDOR
{
    PCSTR Prefix;
    ULONG_PTR MachineVendorId;
    PCWSTR Name;
} CMP_RISCV_VENDOR;

static const CMP_RISCV_VENDOR CmpRiscvVendors[] =
{
    { "andestech", 0x31E, L"Andes" },
    { "microchip", 0x029, L"Microchip" },
    { "sifive", 0x489, L"SiFive" },
    { "spacemit", 0, L"SpacemiT" },
    { "thead", 0x5B7, L"T-Head" },
};

static
const CMP_RISCV_VENDOR *
CmpRiscvFindVendor(
    _In_ const KI_RISCV_PROCESSOR_FEATURES *Features)
{
    PCSTR Comma = strchr(Features->Compatible, ',');
    ULONG Index;

    for (Index = 0; Comma && Index < RTL_NUMBER_OF(CmpRiscvVendors); Index++)
    {
        if (strlen(CmpRiscvVendors[Index].Prefix) == (SIZE_T)(Comma - Features->Compatible) &&
            !strncmp(Features->Compatible, CmpRiscvVendors[Index].Prefix, Comma - Features->Compatible))
        {
            return &CmpRiscvVendors[Index];
        }
    }
    for (Index = 0; Index < RTL_NUMBER_OF(CmpRiscvVendors); Index++)
    {
        if (CmpRiscvVendors[Index].MachineVendorId != 0 &&
            CmpRiscvVendors[Index].MachineVendorId == Features->MachineVendorId)
        {
            return &CmpRiscvVendors[Index];
        }
    }
    return NULL;
}

static
BOOLEAN
CmpRiscvIsUsableSmbiosString(
    _In_z_ PCSTR String)
{
    return String[0] != ANSI_NULL &&
           strcmp(String, "Unknown") &&
           strncmp(String, "rv32", 4) &&
           strncmp(String, "rv64", 4);
}

static
VOID
CmpRiscvBuildProcessorName(
    _Out_writes_bytes_(BufferSize) PWCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_ const KI_RISCV_PROCESSOR_FEATURES *Features,
    _In_opt_ const CMP_RISCV_VENDOR *Vendor)
{
    PCSTR Model = strchr(Features->Compatible, ',');
    CHAR Upper[sizeof(Features->Compatible)];
    SIZE_T Index;

    if (Model == NULL || Model[1] == ANSI_NULL)
    {
        RtlStringCbPrintfW(Buffer, BufferSize, L"RISC-V %S processor",
                           Features->Valid && Features->IsaBase[0] ? Features->IsaBase : "rv64");
        return;
    }

    for (Index = 0; Model[Index + 1] != ANSI_NULL && Index + 1 < sizeof(Upper); Index++)
        Upper[Index] = (CHAR)toupper((UCHAR)Model[Index + 1]);
    Upper[Index] = ANSI_NULL;
    if (Vendor)
    {
        RtlStringCbPrintfW(Buffer, BufferSize, L"%s %S", Vendor->Name, Upper);
    }
    else
    {
        RtlStringCbPrintfW(Buffer, BufferSize, L"%.*S %S",
                           (int)(Model - Features->Compatible), Features->Compatible, Upper);
    }
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    const KI_RISCV_PROCESSOR_FEATURES *Features = &KiRiscvProcessorFeatures;
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE SystemHandle, KeyHandle;
    const CMP_RISCV_VENDOR *Vendor = CmpRiscvFindVendor(Features);
    CHAR SmbiosManufacturer[64];
    CHAR SmbiosVersion[64];
    CHAR Identifier[64];
    WCHAR Wide[KI_RISCV_EXTENSION_LIST_SIZE];
    WCHAR VendorName[64];
    WCHAR ProcessorName[64];
    ULONG Processor, Disposition;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, L"CentralProcessor");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, SystemHandle, NULL);
    Status = NtCreateKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes, 0, NULL, 0, &Disposition);
    if (!NT_SUCCESS(Status))
    {
        NtClose(SystemHandle);
        return Status;
    }
    NtClose(KeyHandle);
    if (Disposition != REG_CREATED_NEW_KEY)
    {
        NtClose(SystemHandle);
        return STATUS_SUCCESS;
    }

    CmpGetSmbiosProcessorStrings(LoaderBlock,
                                 SmbiosManufacturer,
                                 sizeof(SmbiosManufacturer),
                                 SmbiosVersion,
                                 sizeof(SmbiosVersion));
    if (CmpRiscvIsUsableSmbiosString(SmbiosManufacturer))
        RtlStringCbPrintfW(VendorName, sizeof(VendorName), L"%hs", SmbiosManufacturer);
    else
        RtlStringCbCopyW(VendorName, sizeof(VendorName), Vendor ? Vendor->Name : L"RISC-V");
    if (CmpRiscvIsUsableSmbiosString(SmbiosVersion))
        RtlStringCbPrintfW(ProcessorName, sizeof(ProcessorName), L"%hs", SmbiosVersion);
    else
        CmpRiscvBuildProcessorName(ProcessorName, sizeof(ProcessorName), Features, Vendor);

    CmpConfigurationData = ExAllocatePoolWithTag(PagedPool, CmpConfigurationAreaSize, TAG_CM);
    if (!CmpConfigurationData)
    {
        NtClose(SystemHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type = CentralProcessor;
        ConfigData.ComponentEntry.Key = Processor;
        ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(Processor);
        RtlStringCbPrintfA(Identifier, sizeof(Identifier), "RISC-V %s Hart %I64u",
                           Features->Valid ? Features->IsaBase : "rv64", Features->HartId);
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;

        Status = CmpInitializeRegistryNode(&ConfigData, SystemHandle, &KeyHandle, InterfaceTypeUndefined, 0xFFFFFFFF, IndexTable);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
            NtClose(SystemHandle);
            return Status;
        }

        CmpRiscvSetString(KeyHandle, L"VendorIdentifier", VendorName);
        CmpRiscvSetString(KeyHandle, L"ProcessorNameString", ProcessorName);
        RtlStringCbPrintfW(Wide, sizeof(Wide), L"%S", Features->Extensions);
        CmpRiscvSetString(KeyHandle, L"Extensions", Wide);
        CmpRiscvSetDword(KeyHandle, L"FeatureSet", Features->Flags);
        CmpRiscvSetDword(KeyHandle, L"SbiSpecVersion", Features->SbiSpecVersion);
        if (KiProcessorBlock[Processor]->MHz)
            CmpRiscvSetDword(KeyHandle, L"~MHz", KiProcessorBlock[Processor]->MHz);
        NtClose(KeyHandle);
    }

    ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}
