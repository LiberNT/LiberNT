/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Describe Apple silicon machines in SMBIOS from their device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <arm64.h>
#include <blefi.h>
#include <arc/loaderblk.h>
#include <Fdt.h>
#include <libernt/fdtlib.h>

#define SMBIOS3_ENTRY_LENGTH        0x18
#define SMBIOS_MAX_STRINGS          32
#define SMBIOS_MAX_TABLE            (64 * 1024)
#define SMBIOS_EXTRA_SPACE          1024
#define APPLE_MAX_NAME              96

typedef struct _APPLE_SMBIOS_IDENTITY
{
    CHAR Product[APPLE_MAX_NAME];
    CHAR Family[APPLE_MAX_NAME];
    CHAR Board[APPLE_MAX_NAME];
    CHAR Processor[APPLE_MAX_NAME];
    ULONG Cores;
    ULONG MaxMHz;
    UCHAR Chassis;
} APPLE_SMBIOS_IDENTITY, *PAPPLE_SMBIOS_IDENTITY;

typedef struct _APPLE_SMBIOS_FIELD
{
    UCHAR Type;
    UCHAR Offset;
    ULONG Source;
} APPLE_SMBIOS_FIELD;

enum
{
    AppleStringManufacturer,
    AppleStringProduct,
    AppleStringFamily,
    AppleStringBoard,
    AppleStringCpuVendor,
    AppleStringProcessor
};

static const struct
{
    PCSTR Compatible;
    PCSTR Name;
} AppleSocNames[] =
{
    { "apple,t8103", "M1" },
    { "apple,t6000", "M1 Pro" },
    { "apple,t6001", "M1 Max" },
    { "apple,t6002", "M1 Ultra" },
    { "apple,t8112", "M2" },
    { "apple,t6020", "M2 Pro" },
    { "apple,t6021", "M2 Max" },
    { "apple,t6022", "M2 Ultra" },
    { "apple,t8122", "M3" },
    { "apple,t6030", "M3 Pro" },
    { "apple,t6031", "M3 Max" },
    { "apple,t6034", "M3 Max" },
    { "apple,t8132", "M4" },
    { "apple,t6040", "M4 Pro" },
    { "apple,t6041", "M4 Max" },
};

static const struct
{
    PCSTR Name;
    UCHAR Type;
} AppleChassisTypes[] =
{
    { "desktop", 0x03 },
    { "laptop", 0x09 },
    { "convertible", 0x1F },
    { "server", 0x11 },
    { "tablet", 0x1E },
    { "handset", 0x0B },
    { "watch", 0x0B },
    { "embedded", 0x22 },
};

static const APPLE_SMBIOS_FIELD AppleSmbiosFields[] =
{
    { 1, 0x04, AppleStringManufacturer },
    { 1, 0x05, AppleStringProduct },
    { 1, 0x1A, AppleStringFamily },
    { 2, 0x04, AppleStringManufacturer },
    { 2, 0x05, AppleStringBoard },
    { 3, 0x04, AppleStringManufacturer },
    { 4, 0x07, AppleStringCpuVendor },
    { 4, 0x10, AppleStringProcessor },
};

static
ULONG
AppleCopyString(
    _Out_writes_z_(Size) PCHAR Destination,
    _In_ ULONG Size,
    _In_reads_(Length) const CHAR *Source,
    _In_ ULONG Length)
{
    if (Length >= Size)
        Length = Size - 1;
    RtlCopyMemory(Destination, Source, Length);
    Destination[Length] = ANSI_NULL;
    return Length;
}

static
ULONG
AppleFindPhandle(
    _In_ PFDT Fdt,
    _In_ ULONG Phandle)
{
    ULONG Node, Value;

    for (Node = FdtRootNode(Fdt); Node != FDT_NO_NODE && Node < Fdt->StructureSize;
         Node = FdtSkipToken(Fdt, Node))
    {
        if (FdtReadToken(Fdt, Node) == FDT_BEGIN_NODE && FdtReadU32(Fdt, Node, "phandle", &Value) &&
            Value == Phandle)
            return Node;
        if (FdtReadToken(Fdt, Node) == FDT_END)
            break;
    }
    return FDT_NO_NODE;
}

static
ULONG
AppleMaxOppMHz(
    _In_ PFDT Fdt,
    _In_ ULONG Table)
{
    const UCHAR *Hz;
    ULONG Opp, Length, MHz, Max = 0;
    ULONGLONG Value;

    for (Opp = FdtFirstChild(Fdt, Table); Opp != FDT_NO_NODE; Opp = FdtNextSibling(Fdt, Opp))
    {
        Hz = FdtGetProperty(Fdt, Opp, "opp-hz", &Length);
        if (Hz && Length == 8)
            Value = ((ULONGLONG)FdtReadBigEndian32(Hz) << 32) | FdtReadBigEndian32(Hz + 4);
        else if (Hz && Length == 4)
            Value = FdtReadBigEndian32(Hz);
        else
            continue;
        MHz = (ULONG)(Value / 1000000);
        if (MHz > Max && MHz <= 0xFFFF)
            Max = MHz;
    }
    return Max;
}

static
BOOLEAN
AppleReadIdentity(
    _In_ PFDT Fdt,
    _Out_ PAPPLE_SMBIOS_IDENTITY Identity)
{
    const CHAR *Compatible, *Entry, *Model, *Type;
    ULONG Root, Cpus, Child, Length, ModelLength, Cursor = 0, EntryLength, Index, TypeLength;
    BOOLEAN Apple = FALSE;
    PCSTR SocName = NULL;
    CHAR Soc[16] = "";

    RtlZeroMemory(Identity, sizeof(*Identity));
    Root = FdtRootNode(Fdt);
    Compatible = FdtGetProperty(Fdt, Root, "compatible", &Length);
    if (!FdtStringListContains(Compatible, Length, "apple,arm-platform"))
        return FALSE;
    while ((Entry = FdtNextString(Compatible, Length, &Cursor, &EntryLength)) != NULL)
    {
        if (EntryLength <= 6 || memcmp(Entry, "apple,", 6))
            continue;
        if (Entry[6] == 'j' && !Identity->Board[0])
        {
            AppleCopyString(Identity->Board, sizeof(Identity->Board), Entry + 6, EntryLength - 6);
            Identity->Board[0] = 'J';
            Apple = TRUE;
        }
        else if (Entry[6] == 't' && !Soc[0])
        {
            AppleCopyString(Soc, sizeof(Soc), Entry + 6, EntryLength - 6);
            for (Index = 0; Index < RTL_NUMBER_OF(AppleSocNames); ++Index)
            {
                if (strlen(AppleSocNames[Index].Compatible) == EntryLength &&
                    !memcmp(AppleSocNames[Index].Compatible, Entry, EntryLength))
                    SocName = AppleSocNames[Index].Name;
            }
        }
    }
    if (!Apple && !Soc[0])
        return FALSE;
    if (SocName)
    {
        RtlStringCbPrintfA(Identity->Processor, sizeof(Identity->Processor), "Apple %s", SocName);
    }
    else if (Soc[0])
    {
        Soc[0] = 'T';
        RtlStringCbPrintfA(Identity->Processor, sizeof(Identity->Processor), "Apple %s", Soc);
    }

    Type = FdtGetProperty(Fdt, Root, "chassis-type", &TypeLength);
    for (Index = 0; Type && Index < RTL_NUMBER_OF(AppleChassisTypes); ++Index)
    {
        if (TypeLength == strlen(AppleChassisTypes[Index].Name) + 1 &&
            !memcmp(Type, AppleChassisTypes[Index].Name, TypeLength))
            Identity->Chassis = AppleChassisTypes[Index].Type;
    }

    Model = FdtGetProperty(Fdt, Root, "model", &ModelLength);
    if (Model && ModelLength > 1 && Model[ModelLength - 1] == ANSI_NULL)
    {
        ModelLength--;
        if (ModelLength > 6 && !memcmp(Model, "Apple ", 6))
        {
            Model += 6;
            ModelLength -= 6;
        }
        AppleCopyString(Identity->Product, sizeof(Identity->Product), Model, ModelLength);
        for (Index = 0; Index < ModelLength && Model[Index] != '('; ++Index)
            ;
        while (Index && Model[Index - 1] == ' ')
            --Index;
        if (Index)
            AppleCopyString(Identity->Family, sizeof(Identity->Family), Model, Index);
    }

    if (!FdtFindNode(Fdt, "/cpus", &Cpus, NULL))
        Cpus = FDT_NO_NODE;
    for (Child = (Cpus == FDT_NO_NODE) ? FDT_NO_NODE : FdtFirstChild(Fdt, Cpus);
         Child != FDT_NO_NODE;
         Child = FdtNextSibling(Fdt, Child))
    {
        ULONG Opp, MHz;

        Type = FdtGetProperty(Fdt, Child, "device_type", &TypeLength);
        if (!Type || TypeLength != sizeof("cpu") || memcmp(Type, "cpu", sizeof("cpu")))
            continue;
        Identity->Cores++;
        if (FdtReadU32(Fdt, Child, "operating-points-v2", &Opp) &&
            (Opp = AppleFindPhandle(Fdt, Opp)) != FDT_NO_NODE &&
            (MHz = AppleMaxOppMHz(Fdt, Opp)) > Identity->MaxMHz)
            Identity->MaxMHz = MHz;
    }
    return TRUE;
}

static
PCSTR
AppleIdentityString(
    _In_ PAPPLE_SMBIOS_IDENTITY Identity,
    _In_ ULONG Source)
{
    switch (Source)
    {
        case AppleStringManufacturer: return "Apple Inc.";
        case AppleStringProduct: return Identity->Product;
        case AppleStringFamily: return Identity->Family;
        case AppleStringBoard: return Identity->Board;
        case AppleStringCpuVendor: return "Apple";
        case AppleStringProcessor: return Identity->Processor;
        default: return "";
    }
}

static
BOOLEAN
AppleLocateTable(
    _In_ const UCHAR *Entry,
    _Out_ const UCHAR **Table,
    _Out_ PULONG TableSize,
    _Out_ PUCHAR Major,
    _Out_ PUCHAR Minor,
    _Out_ PUCHAR DocRev)
{
    if (!memcmp(Entry, "_SM3_", 5))
    {
        *Major = Entry[0x07];
        *Minor = Entry[0x08];
        *DocRev = Entry[0x09];
        *TableSize = *(const ULONG UNALIGNED *)(Entry + 0x0C);
        *Table = (const UCHAR *)(ULONG_PTR)*(const ULONGLONG UNALIGNED *)(Entry + 0x10);
    }
    else if (!memcmp(Entry, "_SM_", 4))
    {
        *Major = Entry[0x06];
        *Minor = Entry[0x07];
        *DocRev = 0;
        *TableSize = *(const USHORT UNALIGNED *)(Entry + 0x16);
        *Table = (const UCHAR *)(ULONG_PTR)*(const ULONG UNALIGNED *)(Entry + 0x18);
    }
    else
    {
        return FALSE;
    }
    return *Table && *TableSize >= 4 && *TableSize <= SMBIOS_MAX_TABLE;
}

static
ULONG
AppleRewriteStructure(
    _In_ PAPPLE_SMBIOS_IDENTITY Identity,
    _In_ const UCHAR *Structure,
    _In_ const UCHAR *TableEnd,
    _Out_writes_bytes_(OutSize) PUCHAR Out,
    _In_ ULONG OutSize,
    _Out_ PULONG InSize)
{
    const CHAR *Strings[SMBIOS_MAX_STRINGS];
    ULONG Lengths[SMBIOS_MAX_STRINGS];
    ULONG Count = 0, Index, Used, Length;
    const UCHAR *Cursor;
    UCHAR Formatted = Structure[1];
    PCSTR Value;

    *InSize = 0;
    if (Structure + Formatted > TableEnd || Formatted < 4 || Formatted > OutSize)
        return 0;
    for (Cursor = Structure + Formatted; Cursor < TableEnd && *Cursor; )
    {
        for (Length = 0; Cursor + Length < TableEnd && Cursor[Length]; ++Length)
            ;
        if (Cursor + Length >= TableEnd || Count >= SMBIOS_MAX_STRINGS)
            return 0;
        Strings[Count] = (const CHAR *)Cursor;
        Lengths[Count++] = Length;
        Cursor += Length + 1;
    }
    if (Cursor + 1 > TableEnd)
        return 0;
    *InSize = (ULONG)(Cursor + (Count ? 1 : 2) - Structure);
    if (Structure + *InSize > TableEnd)
        return 0;

    RtlCopyMemory(Out, Structure, Formatted);
    for (Index = 0; Index < RTL_NUMBER_OF(AppleSmbiosFields); ++Index)
    {
        if (AppleSmbiosFields[Index].Type != Structure[0] || AppleSmbiosFields[Index].Offset >= Formatted)
            continue;
        Value = AppleIdentityString(Identity, AppleSmbiosFields[Index].Source);
        if (!Value[0] || Count >= SMBIOS_MAX_STRINGS)
            continue;
        Strings[Count] = Value;
        Lengths[Count++] = (ULONG)strlen(Value);
        Out[AppleSmbiosFields[Index].Offset] = (UCHAR)Count;
    }
    if (Structure[0] == 3 && Formatted > 0x05 && Identity->Chassis)
        Out[0x05] = (UCHAR)((Out[0x05] & 0x80) | Identity->Chassis);
    if (Structure[0] == 4 && Formatted > 0x18)
    {
        if (Out[0x05] == 0 || Out[0x05] == 2)
            Out[0x05] = 3;
        if (Identity->MaxMHz && !*(USHORT UNALIGNED *)(Out + 0x14))
            *(USHORT UNALIGNED *)(Out + 0x14) = (USHORT)Identity->MaxMHz;
        if (Identity->MaxMHz && !*(USHORT UNALIGNED *)(Out + 0x16))
            *(USHORT UNALIGNED *)(Out + 0x16) = (USHORT)Identity->MaxMHz;
        if (!Out[0x18])
            Out[0x18] = 0x41;
    }
    if (Structure[0] == 4 && Formatted >= 0x26 && Identity->Cores && Identity->Cores <= 0xFF)
    {
        if (!Out[0x23])
            Out[0x23] = (UCHAR)Identity->Cores;
        if (!Out[0x24])
            Out[0x24] = (UCHAR)Identity->Cores;
        if (!Out[0x25])
            Out[0x25] = (UCHAR)Identity->Cores;
    }

    Used = Formatted;
    for (Index = 0; Index < Count; ++Index)
    {
        if (Used + Lengths[Index] + 1 > OutSize)
            return 0;
        RtlCopyMemory(Out + Used, Strings[Index], Lengths[Index]);
        Used += Lengths[Index];
        Out[Used++] = ANSI_NULL;
    }
    if (Used + (Count ? 1 : 2) > OutSize)
        return 0;
    Out[Used++] = ANSI_NULL;
    if (!Count)
        Out[Used++] = ANSI_NULL;
    return Used;
}

static
const VOID *
AppleFindDeviceTree(
    _Out_ PULONG DeviceTreeSize)
{
    EFI_GUID DeviceTreeGuid = FDT_TABLE_GUID;
    UINTN Index;

    if (!GlobalSystemTable || !GlobalSystemTable->ConfigurationTable)
        return NULL;
    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &DeviceTreeGuid, sizeof(DeviceTreeGuid)))
        {
            if (!FdtValidateHeader(Entry->VendorTable, FDT_MAXIMUM_SIZE, DeviceTreeSize))
                return NULL;
            return Entry->VendorTable;
        }
    }
    return NULL;
}

VOID
Arm64InstallAppleSmbios(VOID)
{
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    APPLE_SMBIOS_IDENTITY Identity;
    const UCHAR *Entry, *Table, *TableEnd, *Structure;
    UCHAR Major, Minor, DocRev, Checksum = 0, Type;
    ULONG TableSize, OutSize, Used = 0, Written, Consumed, Index, DeviceTreeSize;
    EFI_PHYSICAL_ADDRESS Address;
    UINTN Pages;
    const VOID *DeviceTree;
    PUCHAR Out, NewEntry;
    FDT Fdt;

    DeviceTree = AppleFindDeviceTree(&DeviceTreeSize);
    if (!DeviceTree || !FdtOpen(DeviceTree, DeviceTreeSize, &Fdt) || !AppleReadIdentity(&Fdt, &Identity))
        return;
    Entry = FwGetSmbiosEntryPoint();
    if (!Entry || !AppleLocateTable(Entry, &Table, &TableSize, &Major, &Minor, &DocRev))
    {
        DbgPrint("winload: no firmware SMBIOS to describe this Apple machine in\n");
        return;
    }

    OutSize = TableSize + SMBIOS_EXTRA_SPACE;
    Pages = EFI_SIZE_TO_PAGES(OutSize + SMBIOS3_ENTRY_LENGTH);
    if (EFI_ERROR(GlobalSystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, Pages,
                                                                &Address)))
        return;
    Out = (PUCHAR)(ULONG_PTR)Address;
    TableEnd = Table + TableSize;
    for (Structure = Table; Structure + 4 <= TableEnd; Structure += Consumed)
    {
        Type = Structure[0];
        Written = AppleRewriteStructure(&Identity, Structure, TableEnd, Out + Used, OutSize - Used, &Consumed);
        if (!Written || !Consumed)
        {
            DbgPrint("winload: firmware SMBIOS structure type %u at offset %lu is malformed, keeping the firmware table\n",
                     Type, (ULONG)(Structure - Table));
            GlobalSystemTable->BootServices->FreePages(Address, Pages);
            return;
        }
        Used += Written;
        if (Type == 127)
            break;
    }

    NewEntry = Out + OutSize;
    RtlZeroMemory(NewEntry, SMBIOS3_ENTRY_LENGTH);
    RtlCopyMemory(NewEntry, "_SM3_", 5);
    NewEntry[0x06] = SMBIOS3_ENTRY_LENGTH;
    NewEntry[0x07] = Major < 3 ? 3 : Major;
    NewEntry[0x08] = Major < 3 ? 0 : Minor;
    NewEntry[0x09] = DocRev;
    NewEntry[0x0A] = 1;
    *(ULONG UNALIGNED *)(NewEntry + 0x0C) = Used;
    *(ULONGLONG UNALIGNED *)(NewEntry + 0x10) = (ULONGLONG)(ULONG_PTR)Out;
    for (Index = 0; Index < SMBIOS3_ENTRY_LENGTH; ++Index)
        Checksum += NewEntry[Index];
    NewEntry[0x05] = (UCHAR)(0 - Checksum);
    if (EFI_ERROR(GlobalSystemTable->BootServices->InstallConfigurationTable(&Smbios3Guid, NewEntry)))
    {
        GlobalSystemTable->BootServices->FreePages(Address, Pages);
        return;
    }
    DbgPrint("winload: SMBIOS describes Apple Inc. %s (board %s) with an %s, %lu cores up to %lu MHz\n",
             Identity.Product, Identity.Board, Identity.Processor, Identity.Cores, Identity.MaxMHz);
}
