/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Machine description the network debugger serves while the system keeps running
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kd.h"
#include <kdterm.h>
#include <reactos/buildno.h>

#define KDP_INFO_TEXT_SIZE      0x8000
#define KDP_INFO_MACHINE_SIZE   768
#define KDP_INFO_OPTIONS_SIZE   512
#define KDP_INFO_PCI_COUNT      192
#define KDP_INFO_ACPI_COUNT     64
#define KDP_INFO_MODULE_COUNT   1024
#define KDP_INFO_NAME_SIZE      64
#define KDP_INFO_STRING_SIZE    96

#define KDP_ACPI_HEADER_SIZE    36
#define KDP_ACPI_XSDT           0x54445358
#define KDP_ACPI_RSDT           0x54445352

#define KDP_SMBIOS_ENTRY_SIZE   32
#define KDP_SMBIOS_HEADER_SIZE  4
#define KDP_SMBIOS_TABLE_LIMIT  0x100000
#define KDP_SMBIOS_BIOS         0
#define KDP_SMBIOS_SYSTEM       1
#define KDP_SMBIOS_BOARD        2
#define KDP_SMBIOS_PROCESSOR    4
#define KDP_SMBIOS_END          127

typedef struct _KDP_INFO_PCI
{
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    UCHAR HeaderType;
    USHORT VendorId;
    USHORT DeviceId;
    UCHAR BaseClass;
    UCHAR SubClass;
    UCHAR ProgIf;
    UCHAR RevisionId;
    USHORT SubVendorId;
    USHORT SubSystemId;
    UCHAR SecondaryBus;
} KDP_INFO_PCI;

typedef struct _KDP_INFO_ACPI
{
    CHAR Signature[4];
    ULONG Length;
    UCHAR Revision;
    CHAR OemId[6];
    CHAR OemTableId[8];
    ULONG OemRevision;
} KDP_INFO_ACPI;

static CHAR KdpInfoText[KDP_INFO_TEXT_SIZE];
static ULONG KdpInfoLength;
static ULONG KdpInfoClass;
static CHAR KdpInfoMachine[KDP_INFO_MACHINE_SIZE];
static ULONG KdpInfoMachineLength;
static CHAR KdpInfoOptions[KDP_INFO_OPTIONS_SIZE];
static KDP_INFO_PCI KdpInfoPci[KDP_INFO_PCI_COUNT];
static ULONG KdpInfoPciCount;
static KDP_INFO_ACPI KdpInfoAcpi[KDP_INFO_ACPI_COUNT];
static ULONG KdpInfoAcpiCount;

static
VOID
KdpInfoAppend(
    _Inout_updates_(Size) PCHAR Text,
    _In_ ULONG Size,
    _Inout_ PULONG Length,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    va_list Arguments;
    int Written;

    if (*Length >= Size)
        return;

    va_start(Arguments, Format);
    Written = _vsnprintf(Text + *Length, Size - *Length, Format, Arguments);
    va_end(Arguments);
    if (Written < 0 || (ULONG)Written > Size - *Length)
        Written = Size - *Length;

    *Length += Written;
}

static
VOID
KdpInfoCopyText(
    _Out_writes_z_(Size) PCHAR Target,
    _In_ ULONG Size,
    _In_reads_(Length) const CHAR *Source,
    _In_ ULONG Length)
{
    ULONG Count = 0, i;
    CHAR Character;

    for (i = 0; i < Length && Source[i] && Count + 1 < Size; i++)
    {
        Character = Source[i];
        if (Character < ' ' || Character > '~')
            Character = ' ';

        if (Character == ' ' && (!Count || Target[Count - 1] == ' '))
            continue;

        Target[Count++] = Character;
    }

    while (Count && Target[Count - 1] == ' ')
        Count--;

    Target[Count] = ANSI_NULL;
}

static
VOID
KdpInfoSmbiosString(
    _In_reads_bytes_(TableEnd - Structure) const UCHAR *Structure,
    _In_ const UCHAR *TableEnd,
    _In_ ULONG Offset,
    _Out_writes_z_(KDP_INFO_STRING_SIZE) PCHAR Text)
{
    const UCHAR *String;
    ULONG Number, Index, Length;

    *Text = ANSI_NULL;
    if (Offset >= Structure[1])
        return;

    Number = Structure[Offset];
    String = Structure + Structure[1];
    for (Index = 1; Number && String < TableEnd; Index++)
    {
        for (Length = 0; String + Length < TableEnd && String[Length]; Length++)
            NOTHING;

        if (!Length)
            return;

        if (Index == Number)
        {
            KdpInfoCopyText(Text, KDP_INFO_STRING_SIZE, (const CHAR *)String, Length);
            return;
        }

        String += Length + 1;
    }
}

static
VOID
KdpInfoMachineLine(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(TableEnd - Structure) const UCHAR *Structure,
    _In_ const UCHAR *TableEnd,
    _In_ ULONG Offset)
{
    CHAR Text[KDP_INFO_STRING_SIZE];

    KdpInfoSmbiosString(Structure, TableEnd, Offset, Text);
    if (Text[0])
        KdpInfoAppend(KdpInfoMachine, sizeof(KdpInfoMachine), &KdpInfoMachineLength, "%s=%s\n", Name, Text);
}

static
VOID
KdpInfoCaptureMachine(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHYSICAL_ADDRESS Address;
    const UCHAR *Entry, *Table, *TableEnd, *Structure, *Next;
    BOOLEAN Seen[KDP_SMBIOS_PROCESSOR + 1] = {FALSE};
    ULONG Size = 0, EntryLength = 0, i;
    UCHAR Checksum = 0;

    if (!LoaderBlock->Extension || !LoaderBlock->Extension->SMBiosEPSHeader)
        return;

    Address.QuadPart = (ULONG_PTR)LoaderBlock->Extension->SMBiosEPSHeader;
    Entry = MmMapIoSpace(Address, KDP_SMBIOS_ENTRY_SIZE, MmCached);
    if (!Entry)
        return;

    Address.QuadPart = 0;
    if (RtlCompareMemory(Entry, "_SM3_", 5) == 5)
    {
        EntryLength = Entry[6];
        Size = *(UNALIGNED const ULONG *)(Entry + 12);
        Address.QuadPart = *(UNALIGNED const ULONGLONG *)(Entry + 16);
    }
    else if (RtlCompareMemory(Entry, "_SM_", 4) == 4)
    {
        EntryLength = Entry[5];
        Size = *(UNALIGNED const USHORT *)(Entry + 22);
        Address.QuadPart = *(UNALIGNED const ULONG *)(Entry + 24);
    }

    if (EntryLength > KDP_SMBIOS_ENTRY_SIZE)
        EntryLength = 0;

    for (i = 0; i < EntryLength; i++)
        Checksum += Entry[i];

    MmUnmapIoSpace((PVOID)Entry, KDP_SMBIOS_ENTRY_SIZE);
    if (!EntryLength || Checksum)
        return;

    if (!Address.QuadPart || Size < KDP_SMBIOS_HEADER_SIZE || Size > KDP_SMBIOS_TABLE_LIMIT)
        return;

    Table = MmMapIoSpace(Address, Size, MmCached);
    if (!Table)
        return;

    TableEnd = Table + Size;
    for (Structure = Table;
         Structure + KDP_SMBIOS_HEADER_SIZE <= TableEnd && Structure[1] >= KDP_SMBIOS_HEADER_SIZE &&
         Structure + Structure[1] <= TableEnd && Structure[0] != KDP_SMBIOS_END;
         Structure = Next + 2)
    {
        for (Next = Structure + Structure[1]; Next + 1 < TableEnd && (Next[0] || Next[1]); Next++)
            NOTHING;

        if (Next + 1 >= TableEnd)
            TableEnd = Next;

        if (Structure[0] > KDP_SMBIOS_PROCESSOR || Seen[Structure[0]])
            continue;

        Seen[Structure[0]] = TRUE;
        if (Structure[0] == KDP_SMBIOS_BIOS)
        {
            KdpInfoMachineLine("firmware_vendor", Structure, TableEnd, 4);
            KdpInfoMachineLine("firmware_version", Structure, TableEnd, 5);
            KdpInfoMachineLine("firmware_date", Structure, TableEnd, 8);
        }
        else if (Structure[0] == KDP_SMBIOS_SYSTEM)
        {
            KdpInfoMachineLine("manufacturer", Structure, TableEnd, 4);
            KdpInfoMachineLine("product", Structure, TableEnd, 5);
            KdpInfoMachineLine("version", Structure, TableEnd, 6);
        }
        else if (Structure[0] == KDP_SMBIOS_BOARD)
        {
            KdpInfoMachineLine("board_manufacturer", Structure, TableEnd, 4);
            KdpInfoMachineLine("board", Structure, TableEnd, 5);
        }
        else if (Structure[0] == KDP_SMBIOS_PROCESSOR)
        {
            KdpInfoMachineLine("processor", Structure, TableEnd, 16);
        }
    }

    MmUnmapIoSpace((PVOID)Table, Size);
}

static
VOID
KdpInfoCaptureOptions(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const CHAR Secret[] = "ENCRYPTION_KEY=";
    PCHAR Options = LoaderBlock->LoadOptions, Found;

    if (!Options)
        return;

    KdpInfoCopyText(KdpInfoOptions, sizeof(KdpInfoOptions), Options, (ULONG)strlen(Options));
    for (Found = KdpInfoOptions; *Found; Found++)
    {
        if (_strnicmp(Found, Secret, sizeof(Secret) - 1))
            continue;

        for (Found += sizeof(Secret) - 1; *Found && *Found != ' '; Found++)
            *Found = '*';

        break;
    }
}

static
VOID
KdpInfoCapturePci(VOID)
{
    PCI_COMMON_HEADER Config;
    PCI_SLOT_NUMBER Slot;
    KDP_INFO_PCI *Entry;
    ULONG Bus, Device, Function, Length;

    for (Bus = 0; Bus < PCI_MAX_BRIDGE_NUMBER + 1; Bus++)
    {
        for (Device = 0; Device < PCI_MAX_DEVICES; Device++)
        {
            for (Function = 0; Function < PCI_MAX_FUNCTION; Function++)
            {
                Slot.u.AsULONG = 0;
                Slot.u.bits.DeviceNumber = Device;
                Slot.u.bits.FunctionNumber = Function;
                Length = HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &Config, 0,
                                               PCI_COMMON_HDR_LENGTH);
                if (!Length)
                    goto NextBus;

                if (Length < PCI_COMMON_HDR_LENGTH || Config.VendorID == PCI_INVALID_VENDORID ||
                    Config.VendorID == 0)
                {
                    if (!Function)
                        break;

                    continue;
                }

                if (KdpInfoPciCount == KDP_INFO_PCI_COUNT)
                    return;

                Entry = &KdpInfoPci[KdpInfoPciCount++];
                Entry->Bus = (UCHAR)Bus;
                Entry->Device = (UCHAR)Device;
                Entry->Function = (UCHAR)Function;
                Entry->HeaderType = Config.HeaderType;
                Entry->VendorId = Config.VendorID;
                Entry->DeviceId = Config.DeviceID;
                Entry->BaseClass = Config.BaseClass;
                Entry->SubClass = Config.SubClass;
                Entry->ProgIf = Config.ProgIf;
                Entry->RevisionId = Config.RevisionID;
                if (PCI_CONFIGURATION_TYPE(&Config) == PCI_DEVICE_TYPE)
                {
                    Entry->SubVendorId = Config.u.type0.SubVendorID;
                    Entry->SubSystemId = Config.u.type0.SubSystemID;
                }
                else if (PCI_CONFIGURATION_TYPE(&Config) == PCI_BRIDGE_TYPE)
                {
                    Entry->SecondaryBus = Config.u.type1.SecondaryBus;
                }

                if (!Function && !PCI_MULTIFUNCTION_DEVICE(&Config))
                    break;
            }
        }
NextBus:
        NOTHING;
    }
}

static
VOID
KdpInfoCaptureAcpi(VOID)
{
    const UCHAR *Root, *Header;
    PHYSICAL_ADDRESS Address;
    KDP_INFO_ACPI *Entry;
    ULONG Length, Width, Offset;

    if (!HalGetCachedAcpiTable)
        return;

    Width = sizeof(ULONGLONG);
    Root = HalGetCachedAcpiTable(KDP_ACPI_XSDT, NULL, NULL);
    if (!Root)
    {
        Width = sizeof(ULONG);
        Root = HalGetCachedAcpiTable(KDP_ACPI_RSDT, NULL, NULL);
    }

    if (!Root)
        return;

    Length = *(UNALIGNED const ULONG *)(Root + 4);
    for (Offset = KDP_ACPI_HEADER_SIZE;
         Offset + Width <= Length && KdpInfoAcpiCount < KDP_INFO_ACPI_COUNT;
         Offset += Width)
    {
        Address.QuadPart = Width == sizeof(ULONG) ? *(UNALIGNED const ULONG *)(Root + Offset) :
                                                    *(UNALIGNED const ULONGLONG *)(Root + Offset);
        if (!Address.QuadPart)
            continue;

        Header = MmMapIoSpace(Address, KDP_ACPI_HEADER_SIZE, MmCached);
        if (!Header)
            continue;

        Entry = &KdpInfoAcpi[KdpInfoAcpiCount++];
        RtlCopyMemory(Entry->Signature, Header, sizeof(Entry->Signature));
        Entry->Length = *(UNALIGNED const ULONG *)(Header + 4);
        Entry->Revision = Header[8];
        RtlCopyMemory(Entry->OemId, Header + 10, sizeof(Entry->OemId));
        RtlCopyMemory(Entry->OemTableId, Header + 16, sizeof(Entry->OemTableId));
        Entry->OemRevision = *(UNALIGNED const ULONG *)(Header + 24);
        MmUnmapIoSpace((PVOID)Header, KDP_ACPI_HEADER_SIZE);
    }
}

static
VOID
KdpInfoBuildSystem(VOID)
{
    ULONG Length = 0;

    KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length,
                  "os=LiberNT %s\nbuild=%s\nrevision=%s\nnt=%lu.%lu.%lu\narchitecture=%u\n"
                  "processors=%lu\nmemory_mb=%lu\nuptime=%lu\n",
                  KERNEL_VERSION_STR, NtBuildLab, KERNEL_VERSION_COMMIT_HASH,
                  SharedUserData->NtMajorVersion, SharedUserData->NtMinorVersion, NtBuildNumber & 0xFFFF,
                  KeProcessorArchitecture, (ULONG)KeNumberProcessors,
                  (ULONG)(((ULONGLONG)MmNumberOfPhysicalPages * PAGE_SIZE) >> 20),
                  (ULONG)(KeQueryInterruptTime() / 10000000));
    if (KeBugCheckActive)
    {
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, "bugcheck=%08lx %p %p %p %p\n",
                      (ULONG)KiBugCheckData[0], (PVOID)KiBugCheckData[1], (PVOID)KiBugCheckData[2],
                      (PVOID)KiBugCheckData[3], (PVOID)KiBugCheckData[4]);
    }

    if (KdpInfoOptions[0])
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, "options=%s\n", KdpInfoOptions);

    if (KdpInfoMachineLength)
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, "%s", KdpInfoMachine);

    KdpInfoLength = Length;
}

static
VOID
KdpInfoBuildPci(VOID)
{
    const KDP_INFO_PCI *Entry;
    ULONG Length = 0, i;

    for (i = 0; i < KdpInfoPciCount; i++)
    {
        Entry = &KdpInfoPci[i];
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length,
                      "%02x:%02x.%x %04x:%04x class=%02x%02x%02x revision=%02x header=%02x",
                      Entry->Bus, Entry->Device, Entry->Function, Entry->VendorId, Entry->DeviceId,
                      Entry->BaseClass, Entry->SubClass, Entry->ProgIf, Entry->RevisionId,
                      Entry->HeaderType);
        if ((Entry->HeaderType & ~PCI_MULTIFUNCTION) == PCI_BRIDGE_TYPE)
        {
            KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, " secondary=%02x\n",
                          Entry->SecondaryBus);
        }
        else
        {
            KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, " subsystem=%04x:%04x\n",
                          Entry->SubVendorId, Entry->SubSystemId);
        }
    }

    KdpInfoLength = Length;
}

static
VOID
KdpInfoBuildAcpi(VOID)
{
    CHAR Signature[5], OemId[7], OemTableId[9];
    const KDP_INFO_ACPI *Entry;
    ULONG Length = 0, i;

    for (i = 0; i < KdpInfoAcpiCount; i++)
    {
        Entry = &KdpInfoAcpi[i];
        KdpInfoCopyText(Signature, sizeof(Signature), Entry->Signature, sizeof(Entry->Signature));
        KdpInfoCopyText(OemId, sizeof(OemId), Entry->OemId, sizeof(Entry->OemId));
        KdpInfoCopyText(OemTableId, sizeof(OemTableId), Entry->OemTableId, sizeof(Entry->OemTableId));
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length,
                      "%s length=%lu revision=%u oem=%s table=%s oem_revision=%08lx\n",
                      Signature, Entry->Length, Entry->Revision, OemId, OemTableId, Entry->OemRevision);
    }

    KdpInfoLength = Length;
}

static
BOOLEAN
KdpInfoBuildModules(VOID)
{
    CHAR Name[KDP_INFO_NAME_SIZE];
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Link;
    ULONG Length = 0, Count = 0, Characters, i;
    PWCHAR Buffer;

    if (!PsLoadedModuleList.Flink || !KeTryToAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock))
        return FALSE;

    for (Link = PsLoadedModuleList.Flink;
         Link != &PsLoadedModuleList && Count < KDP_INFO_MODULE_COUNT;
         Link = Link->Flink, Count++)
    {
        Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!MmIsAddressValid(Entry) || !MmIsAddressValid((PUCHAR)(Entry + 1) - 1))
            break;

        Buffer = Entry->BaseDllName.Buffer;
        Characters = min(Entry->BaseDllName.Length / sizeof(WCHAR), sizeof(Name) - 1);
        if (!Characters || !MmIsAddressValid(Buffer) || !MmIsAddressValid((PUCHAR)(Buffer + Characters) - 1))
            Characters = 0;

        for (i = 0; i < Characters; i++)
            Name[i] = (Buffer[i] > ' ' && Buffer[i] <= '~') ? (CHAR)Buffer[i] : '?';

        Name[Characters] = ANSI_NULL;
        KdpInfoAppend(KdpInfoText, sizeof(KdpInfoText), &Length, "%p %08lx %s\n",
                      Entry->DllBase, Entry->SizeOfImage, Name);
    }

    KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);
    KdpInfoLength = Length;
    return TRUE;
}

ULONG
NTAPI
KdpInfoQuery(
    _In_ ULONG Class,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size,
    _Out_ PULONG Total)
{
    *Total = 0;
    if (!Offset || Class != KdpInfoClass)
    {
        KdpInfoClass = 0;
        KdpInfoLength = 0;
        if (Class == KD_TERMINAL_INFORMATION_SYSTEM)
            KdpInfoBuildSystem();
        else if (Class == KD_TERMINAL_INFORMATION_PCI)
            KdpInfoBuildPci();
        else if (Class == KD_TERMINAL_INFORMATION_ACPI)
            KdpInfoBuildAcpi();
        else if (Class != KD_TERMINAL_INFORMATION_MODULES || !KdpInfoBuildModules())
            return 0;

        KdpInfoClass = Class;
    }

    *Total = KdpInfoLength;
    if (Offset >= KdpInfoLength)
        return 0;

    Size = min(Size, KdpInfoLength - Offset);
    RtlCopyMemory(Buffer, KdpInfoText + Offset, Size);
    return Size;
}

ULONG
NTAPI
KdpInfoState(VOID)
{
    return KeBugCheckActive ? KD_TERMINAL_STATE_CRASHED : 0;
}

VOID
KdpInfoInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!LoaderBlock)
        return;

    KdpInfoCaptureOptions(LoaderBlock);
    KdpInfoCaptureMachine(LoaderBlock);
    KdpInfoCapturePci();
    KdpInfoCaptureAcpi();
}
