/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Machine-dependent registry configuration for LoongArch64
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Publishes HARDWARE\DESCRIPTION\System\CentralProcessor\<n> from the CPUCFG
 * record and the firmware's SMBIOS strings.
 *
 * CPUCFG0.PRID is documented as "Processor Identity" with no architectural
 * encoding ([V1-Table 3, word 0x0]), so the raw word is published and no
 * vendor or model is derived from it. Everything else is a CPUCFG word used
 * unchanged: word 1 (ARCH/PALEN/VALEN/UAL), word 2 (FP/LSX/LASX) and word 3
 * (CCDMA/ITLBT/RVA) are the facts this machine states about itself, and word
 * 4/5 give the crystal rate and its multiplier/divisor.
 */

#include <ntoskrnl.h>
/* The per-processor CPUCFG record and ESTAT decode live with the ke files. */
#include "../../ke/loongarch64/la64ke.h"
#define NDEBUG
#include <debug.h>

static
VOID
CmpLa64SetString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ, (PVOID)Value,
                           (ULONG)((wcslen(Value) + 1) * sizeof(WCHAR)));
    if (!NT_SUCCESS(Status))
        DPRINT1("LoongArch64: failed to set %S: 0x%lx\n", Name, Status);
}

static
VOID
CmpLa64SetDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static
VOID
CmpLa64SetQword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG64 Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_QWORD, &Value, sizeof(Value));
}

static
BOOLEAN
CmpLa64IsUsableSmbiosString(
    _In_z_ PCSTR String)
{
    return String[0] != ANSI_NULL &&
           strcmp(String, "Unknown") &&
           strncmp(String, "To Be Filled By O.E.M.", 22);
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    const PKI_LA64_PROCESSOR Processor = &KiLa64Processor;
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE SystemHandle, KeyHandle;
    CHAR SmbiosManufacturer[64];
    CHAR SmbiosVersion[64];
    CHAR Identifier[64];
    WCHAR VendorName[64];
    WCHAR ProcessorName[64];
    ULONG Index, Disposition;
    NTSTATUS Status;

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, L"CentralProcessor");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, SystemHandle, NULL);
    Status = NtCreateKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes, 0, NULL,
                         REG_OPTION_VOLATILE, &Disposition);
    if (!NT_SUCCESS(Status))
    {
        NtClose(SystemHandle);
        return Status;
    }
    NtClose(KeyHandle);
    if (Disposition != REG_CREATED_NEW_KEY)
    {
        /* Another pass already published this machine. */
        NtClose(SystemHandle);
        return STATUS_SUCCESS;
    }

    CmpGetSmbiosProcessorStrings(LoaderBlock,
                                 SmbiosManufacturer,
                                 sizeof(SmbiosManufacturer),
                                 SmbiosVersion,
                                 sizeof(SmbiosVersion));
    if (CmpLa64IsUsableSmbiosString(SmbiosManufacturer))
    {
        RtlStringCbPrintfW(VendorName, sizeof(VendorName), L"%hs", SmbiosManufacturer);
    }
    else
    {
        /* No firmware vendor string: this is the only platform the LoongArch64
         * HAL implements (hal/halloongarch64 is the 3A5000 platform HAL). */
        RtlStringCbCopyW(VendorName, sizeof(VendorName), L"Loongson");
    }
    if (CmpLa64IsUsableSmbiosString(SmbiosVersion))
        RtlStringCbPrintfW(ProcessorName, sizeof(ProcessorName), L"%hs", SmbiosVersion);
    else
        RtlStringCbPrintfW(ProcessorName, sizeof(ProcessorName),
                           L"LoongArch64 processor (PRID 0x%08lx)", Processor->ProcessorId);

    CmpConfigurationData = ExAllocatePoolWithTag(PagedPool, CmpConfigurationAreaSize, TAG_CM);
    if (!CmpConfigurationData)
    {
        NtClose(SystemHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type = CentralProcessor;
        ConfigData.ComponentEntry.Key = Index;
        /* CONFIGURATION_COMPONENT's mask field is 32 bits wide on every
         * architecture; only the represented processor's bit is published. */
        ConfigData.ComponentEntry.AffinityMask = (ULONG)AFFINITY_MASK(Index);
        RtlStringCbPrintfA(Identifier, sizeof(Identifier), "LoongArch64 CPU %lu", Index);
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;

        Status = CmpInitializeRegistryNode(&ConfigData, SystemHandle, &KeyHandle,
                                           InterfaceTypeUndefined, 0xFFFFFFFF, IndexTable);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
            NtClose(SystemHandle);
            return Status;
        }

        CmpLa64SetString(KeyHandle, L"VendorIdentifier", VendorName);
        CmpLa64SetString(KeyHandle, L"ProcessorNameString", ProcessorName);
        CmpLa64SetDword(KeyHandle, L"Identifier", Index);
        CmpLa64SetDword(KeyHandle, L"PRId", Processor->ProcessorId);
        CmpLa64SetDword(KeyHandle, L"Architecture", Processor->CpuCfg1);
        CmpLa64SetDword(KeyHandle, L"FeatureSet", Processor->CpuCfg2);
        CmpLa64SetDword(KeyHandle, L"Extension", Processor->CpuCfg3);
        CmpLa64SetDword(KeyHandle, L"CrystalClock", Processor->CpuCfg4);
        CmpLa64SetQword(KeyHandle, L"CounterFrequency", Processor->CounterFrequency);
        if (KiProcessorBlock[Index] && KiProcessorBlock[Index]->MHz)
            CmpLa64SetDword(KeyHandle, L"~MHz", KiProcessorBlock[Index]->MHz);
        NtClose(KeyHandle);
    }

    ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}

/* EOF */
