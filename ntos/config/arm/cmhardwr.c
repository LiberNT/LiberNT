/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 processor description in HARDWARE\DESCRIPTION\System
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
VOID
CmpArmSetValue(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) PVOID Data,
    _In_ ULONG Size)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, Type, Data, Size);
    if (!NT_SUCCESS(Status))
        DPRINT1("Could not set %S on the processor key: 0x%lx\n", Name, Status);
}

/* MIDR implementer codes (ARM ARM B4.1.105). */
static
PCWSTR
CmpArmVendorName(
    _In_ UCHAR Implementer)
{
    switch (Implementer)
    {
        case 0x41: return L"ARM";
        case 0x42: return L"Broadcom";
        case 0x51: return L"Qualcomm";
        case 0x56: return L"Marvell";
        case 0x69: return L"Intel";
        default: return L"ARM compatible";
    }
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE SystemHandle, KeyHandle;
    CHAR Identifier[64];
    WCHAR Name[64];
    PKPRCB Prcb;
    ULONG Processor, Disposition, Value;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(LoaderBlock);

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, L"CentralProcessor");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, SystemHandle, NULL);
    Status = NtCreateKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, &Disposition);
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

    CmpConfigurationData = ExAllocatePoolWithTag(PagedPool, CmpConfigurationAreaSize, TAG_CM);
    if (!CmpConfigurationData)
    {
        NtClose(SystemHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        Prcb = KiProcessorBlock[Processor];
        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type = CentralProcessor;
        ConfigData.ComponentEntry.Key = Processor;
        ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(Processor);
        RtlStringCbPrintfA(Identifier, sizeof(Identifier), "ARMv7 Family 7 Model %X Revision %X",
                           Prcb->ProcessorModel, Prcb->ProcessorRevision);
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;
        Status = CmpInitializeRegistryNode(&ConfigData, SystemHandle, &KeyHandle,
                                           InterfaceTypeUndefined, 0xFFFFFFFF, IndexTable);
        if (!NT_SUCCESS(Status))
            break;

        RtlStringCbCopyW(Name, sizeof(Name), CmpArmVendorName(Prcb->CpuVendor));
        CmpArmSetValue(KeyHandle, L"VendorIdentifier", REG_SZ, Name, (ULONG)((wcslen(Name) + 1) * sizeof(WCHAR)));
        RtlStringCbPrintfW(Name, sizeof(Name), L"%s ARMv7 Processor", CmpArmVendorName(Prcb->CpuVendor));
        CmpArmSetValue(KeyHandle, L"ProcessorNameString", REG_SZ, Name, (ULONG)((wcslen(Name) + 1) * sizeof(WCHAR)));
        Value = Prcb->MHz;
        CmpArmSetValue(KeyHandle, L"~MHz", REG_DWORD, &Value, sizeof(Value));
        Value = Prcb->FeatureBits;
        CmpArmSetValue(KeyHandle, L"FeatureSet", REG_DWORD, &Value, sizeof(Value));
        NtClose(KeyHandle);
    }

    ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    NtClose(SystemHandle);
    return Status;
}
