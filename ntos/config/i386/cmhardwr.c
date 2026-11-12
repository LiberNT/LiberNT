/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What an x86 processor says about itself, written to the registry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define CMP_X86_IDENTIFIER_MAX 64
#define CMP_X86_BRAND_MAX      64

static CHAR CmpX86Identifier[] = "AT/AT COMPATIBLE";

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
CmpX86SetString(
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

    NtSetValueKey(KeyHandle,
                  &Name,
                  0,
                  REG_SZ,
                  Wide.Buffer,
                  Wide.Length + sizeof(WCHAR));

    RtlFreeUnicodeString(&Wide);
}

static
VOID
CmpX86SetDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value)
{
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, ValueName);
    NtSetValueKey(KeyHandle, &Name, 0, REG_DWORD, &Value, sizeof(Value));
}

/*
 * The brand string the processor itself carries. Three leaves of extended
 * CPUID hold it as plain characters, and a processor that does not have it
 * says so by not claiming the leaves.
 */
static
BOOLEAN
CmpX86ReadBrandString(
    _Out_writes_(Size) PCHAR Buffer,
    _In_ SIZE_T Size)
{
    INT Registers[4];
    CHAR Brand[49];
    SIZE_T Length;
    PCHAR Start;
    ULONG Leaf;
    ULONG i;

    if (Size == 0) return FALSE;

    Buffer[0] = ANSI_NULL;

    __cpuid(Registers, 0x80000000);
    if ((ULONG)Registers[0] < 0x80000004) return FALSE;

    for (Leaf = 0; Leaf < 3; Leaf++)
    {
        __cpuid(Registers, 0x80000002 + Leaf);
        RtlCopyMemory(&Brand[Leaf * 16], Registers, 16);
    }

    Brand[48] = ANSI_NULL;

    Start = Brand;
    while (*Start == ' ') Start++;

    Length = strlen(Start);
    while (Length != 0 && Start[Length - 1] == ' ') Length--;

    if (Length >= Size) Length = Size - 1;

    for (i = 0; i < Length; i++) Buffer[i] = Start[i];
    Buffer[Length] = ANSI_NULL;
    return Length != 0;
}

static
VOID
CmpX86BuildIdentifier(
    _Out_writes_(Size) PCHAR Buffer,
    _In_ SIZE_T Size,
    _In_ PKPRCB Prcb)
{
    RtlStringCbPrintfA(Buffer,
                       Size,
                       "x86 Family %u Model %u Stepping %u",
                       (ULONG)(UCHAR)Prcb->CpuType,
                       (ULONG)((Prcb->CpuStep >> 8) & 0xFF),
                       (ULONG)(Prcb->CpuStep & 0xFF));
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Writes down the processors the machine has. The firmware tree describes the
 * board; only the processor can say what it is, so it is asked here.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    CHAR Identifier[CMP_X86_IDENTIFIER_MAX];
    CHAR Brand[CMP_X86_BRAND_MAX];
    CHAR Vendor[16];
    UNICODE_STRING KeyName;
    HANDLE SystemHandle;
    HANDLE KeyHandle;
    NTSTATUS Status;
    ULONG Processor;
    PKPRCB Prcb;

    UNREFERENCED_PARAMETER(LoaderBlock);

    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    CmpX86SetString(SystemHandle, L"Identifier", CmpX86Identifier);

    CmpX86ReadBrandString(Brand, sizeof(Brand));

    Identifier[0] = ANSI_NULL;
    if (KiProcessorBlock[0] != NULL)
    {
        CmpX86BuildIdentifier(Identifier,
                              sizeof(Identifier),
                              KiProcessorBlock[0]);
    }

    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        Prcb = KiProcessorBlock[Processor];
        if (Prcb == NULL) continue;

        RtlZeroMemory(Vendor, sizeof(Vendor));
        RtlCopyMemory(Vendor, Prcb->VendorString, sizeof(Prcb->VendorString));
        Vendor[sizeof(Vendor) - 1] = ANSI_NULL;

        CmpX86BuildIdentifier(Identifier, sizeof(Identifier), Prcb);

        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type = CentralProcessor;
        ConfigData.ComponentEntry.Key = Processor;
        ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(Processor);
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength =
            (ULONG)strlen(Identifier) + 1;

        Status = CmpInitializeRegistryNode(&ConfigData,
                                           SystemHandle,
                                           &KeyHandle,
                                           InterfaceTypeUndefined,
                                           0xFFFFFFFF,
                                           IndexTable);
        if (!NT_SUCCESS(Status))
        {
            NtClose(SystemHandle);
            return Status;
        }

        CmpX86SetString(KeyHandle, L"VendorIdentifier", Vendor);
        CmpX86SetString(KeyHandle, L"ProcessorNameString", Brand);
        CmpX86SetDword(KeyHandle, L"FeatureSet", (ULONG)Prcb->FeatureBits);
        if (Prcb->MHz != 0) CmpX86SetDword(KeyHandle, L"~MHz", Prcb->MHz);

        NtClose(KeyHandle);
    }

    /* Every processor this kernel runs on has a floating point unit of its
       own, so the separate coprocessor key only records that it is there */
    RtlZeroMemory(IndexTable, sizeof(IndexTable));
    RtlZeroMemory(&ConfigData, sizeof(ConfigData));
    ConfigData.ComponentEntry.Class = ProcessorClass;
    ConfigData.ComponentEntry.Type = FloatingPointProcessor;
    ConfigData.ComponentEntry.Key = 0;
    ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(0);
    ConfigData.ComponentEntry.Identifier = Identifier;
    ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;

    Status = CmpInitializeRegistryNode(&ConfigData,
                                       SystemHandle,
                                       &KeyHandle,
                                       InterfaceTypeUndefined,
                                       0xFFFFFFFF,
                                       IndexTable);
    if (NT_SUCCESS(Status)) NtClose(KeyHandle);

    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}
