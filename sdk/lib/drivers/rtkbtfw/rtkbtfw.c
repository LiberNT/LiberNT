/*
 * PROJECT:     LiberNT Realtek Bluetooth firmware library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Realtek controller identification and patch download shared by the Bluetooth transports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntstrsafe.h>
#include "rtkbtfw.h"

#define NDEBUG
#include <debug.h>

#define RTKBT_TAG                   'fBkR'
#define RTKBT_FIRMWARE_DIRECTORY    L"\\SystemRoot\\System32\\drivers\\realtek\\"
#define RTKBT_FIRMWARE_MAX_SIZE     (512 * 1024)

#define HCI_READ_LOCAL_VERSION      0x1001
#define HCI_READ_BD_ADDR            0x1009
#define RTK_READ_ROM_VERSION        0xFC6D
#define RTK_DOWNLOAD_PATCH          0xFC20

#define RTK_EPATCH_SIGNATURE        "Realtech"
#define RTK_EPATCH_HEADER_LENGTH    14
#define RTK_CONFIG_SIGNATURE        0x8723AB55
#define RTK_CONFIG_UART_OFFSET      0x000C
#define RTK_CONFIG_UART_FLOW_INDEX  12
#define RTK_UART_BAUD_115200        0x0252C014
#define RTK_DOWNLOAD_CHUNK          252

typedef struct _RTKBT_CHIP
{
    USHORT LmpSubversion;
    USHORT HciRevision;
    UCHAR HciVersion;
    RTKBT_BUS Bus;
    PCWSTR Name;
} RTKBT_CHIP;

static const RTKBT_CHIP RtkBtChips[] =
{
    { 0x8723, 0x000b, 0x06, RtkBtBusUsb,  L"rtl8723b" },
    { 0x8723, 0x000d, 0x08, RtkBtBusUsb,  L"rtl8723d" },
    { 0x8821, 0x000a, 0x06, RtkBtBusUsb,  L"rtl8821a" },
    { 0x8821, 0x000c, 0x08, RtkBtBusUsb,  L"rtl8821c" },
    { 0x8761, 0x000b, 0x0a, RtkBtBusUsb,  L"rtl8761bu" },
    { 0x8822, 0x000c, 0x0a, RtkBtBusUsb,  L"rtl8822cu" },
    { 0x8852, 0x000a, 0x0b, RtkBtBusUsb,  L"rtl8852au" },
    { 0x8852, 0x000b, 0x0b, RtkBtBusUsb,  L"rtl8852bu" },
    { 0x8852, 0x000b, 0x0b, RtkBtBusUart, L"rtl8852bs" },
};

static
NTSTATUS
RtkBtLoadFile(
    _In_ PCWSTR Name,
    _In_ PCWSTR Suffix,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    WCHAR PathBuffer[128];
    HANDLE Handle;
    PUCHAR Buffer;
    ULONG Size;
    NTSTATUS Status;

    *Data = NULL;
    *Length = 0;
    Status = RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer), L"%s%s%s", RTKBT_FIRMWARE_DIRECTORY, Name, Suffix);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) || FileInfo.EndOfFile.QuadPart == 0 ||
        FileInfo.EndOfFile.QuadPart > RTKBT_FIRMWARE_MAX_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Size = (ULONG)FileInfo.EndOfFile.QuadPart;
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, RTKBT_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Size, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Size)
    {
        ExFreePoolWithTag(Buffer, RTKBT_TAG);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *Data = Buffer;
    *Length = Size;
    return STATUS_SUCCESS;
}

static
USHORT
RtkBtGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static
ULONG
RtkBtGet32(
    _In_reads_(4) const UCHAR *Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static
VOID
RtkBtPut32(
    _Out_writes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
}

static
VOID
RtkBtFixUartConfig(
    _Inout_updates_bytes_(Length) PUCHAR Config,
    _In_ ULONG Length)
{
    ULONG Offset = 6, End;

    if (Length < 6 || RtkBtGet32(Config) != RTK_CONFIG_SIGNATURE)
        return;
    End = min(Length, 6 + (ULONG)RtkBtGet16(Config + 4));
    while (Offset + 3 <= End)
    {
        USHORT Entry = RtkBtGet16(Config + Offset);
        UCHAR EntryLength = Config[Offset + 2];
        PUCHAR EntryData = Config + Offset + 3;

        if (Offset + 3 + EntryLength > End)
            break;
        if (Entry == RTK_CONFIG_UART_OFFSET && EntryLength >= 4)
        {
            RtkBtPut32(EntryData, RTK_UART_BAUD_115200);
            if (EntryLength > RTK_CONFIG_UART_FLOW_INDEX)
                EntryData[RTK_CONFIG_UART_FLOW_INDEX] &= (UCHAR)~0x04;
        }
        Offset += 3 + EntryLength;
    }
}

static
NTSTATUS
RtkBtBuildPatch(
    _In_reads_bytes_(FirmwareLength) const UCHAR *Firmware,
    _In_ ULONG FirmwareLength,
    _In_reads_bytes_opt_(ConfigLength) const UCHAR *Config,
    _In_ ULONG ConfigLength,
    _In_ UCHAR RomVersion,
    _In_ RTKBT_BUS Bus,
    _Out_ PUCHAR *Patch,
    _Out_ PULONG PatchLength)
{
    USHORT Count, Index;
    ULONG Version;
    PUCHAR Buffer;

    *Patch = NULL;
    *PatchLength = 0;
    if (FirmwareLength < RTK_EPATCH_HEADER_LENGTH ||
        !RtlEqualMemory(Firmware, RTK_EPATCH_SIGNATURE, sizeof(RTK_EPATCH_SIGNATURE) - 1))
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    Version = RtkBtGet32(Firmware + 8);
    Count = RtkBtGet16(Firmware + 12);
    if (!Count || RTK_EPATCH_HEADER_LENGTH + (ULONG)Count * 8 > FirmwareLength)
        return STATUS_INVALID_IMAGE_FORMAT;

    for (Index = 0; Index < Count; Index++)
    {
        const UCHAR *Table = Firmware + RTK_EPATCH_HEADER_LENGTH;
        USHORT ChipId = RtkBtGet16(Table + Index * 2);
        USHORT Length = RtkBtGet16(Table + Count * 2 + Index * 2);
        ULONG Offset = RtkBtGet32(Table + Count * 4 + Index * 4);

        if (ChipId != RomVersion + 1)
            continue;
        if (Length < 4 || Offset > FirmwareLength || Length > FirmwareLength - Offset)
            return STATUS_INVALID_IMAGE_FORMAT;

        Buffer = ExAllocatePoolWithTag(PagedPool, Length + ConfigLength, RTKBT_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(Buffer, Firmware + Offset, Length);
        RtkBtPut32(Buffer + Length - 4, Version);
        if (Config && ConfigLength)
        {
            RtlCopyMemory(Buffer + Length, Config, ConfigLength);
            if (Bus == RtkBtBusUart)
                RtkBtFixUartConfig(Buffer + Length, ConfigLength);
        }
        *Patch = Buffer;
        *PatchLength = Length + ConfigLength;
        DPRINT1("RTKBTFW: patch for chip %u, version 0x%08lx, %lu bytes\n", ChipId, Version, *PatchLength);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
RtkBtDownload(
    _In_ PRTKBT_HCI_COMMAND Command,
    _In_ PVOID Context,
    _In_reads_bytes_(Length) const UCHAR *Patch,
    _In_ ULONG Length)
{
    UCHAR Chunk[1 + RTK_DOWNLOAD_CHUNK];
    UCHAR Result[2];
    ULONG Count, Index, ResultLength;
    NTSTATUS Status;

    Count = Length / RTK_DOWNLOAD_CHUNK + 1;
    for (Index = 0; Index < Count; Index++)
    {
        ULONG Size = Index == Count - 1 ? Length % RTK_DOWNLOAD_CHUNK : RTK_DOWNLOAD_CHUNK;
        UCHAR Frame = (UCHAR)(Index <= 0x7F ? Index : ((Index - 0x80) % 0x7F) + 1);

        if (Index == Count - 1)
            Frame |= 0x80;
        Chunk[0] = Frame;
        RtlCopyMemory(Chunk + 1, Patch + Index * RTK_DOWNLOAD_CHUNK, Size);
        Status = Command(Context, RTK_DOWNLOAD_PATCH, Chunk, (UCHAR)(1 + Size), Result, sizeof(Result), &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RTKBTFW: patch frame %lu of %lu failed 0x%08lx\n", Index, Count, Status);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
RtkBtReadVersion(
    _In_ PRTKBT_HCI_COMMAND Command,
    _In_ PVOID Context,
    _Out_ PRTKBT_VERSION Version)
{
    UCHAR Result[9];
    ULONG Length;
    NTSTATUS Status;

    RtlZeroMemory(Version, sizeof(*Version));
    Status = Command(Context, HCI_READ_LOCAL_VERSION, NULL, 0, Result, sizeof(Result), &Length);
    if (!NT_SUCCESS(Status) || Length < sizeof(Result))
        return NT_SUCCESS(Status) ? STATUS_INVALID_PARAMETER : Status;
    Version->HciVersion = Result[1];
    Version->HciRevision = RtkBtGet16(Result + 2);
    Version->LmpVersion = Result[4];
    Version->Manufacturer = RtkBtGet16(Result + 5);
    Version->LmpSubversion = RtkBtGet16(Result + 7);
    DPRINT1("RTKBTFW: HCI version %u revision 0x%04x, LMP version %u subversion 0x%04x, manufacturer %u\n",
            Version->HciVersion, Version->HciRevision, Version->LmpVersion, Version->LmpSubversion,
            Version->Manufacturer);
    return STATUS_SUCCESS;
}

NTSTATUS
RtkBtLoadPatch(
    _In_ PRTKBT_HCI_COMMAND Command,
    _In_ PVOID Context,
    _In_ RTKBT_BUS Bus,
    _Out_ PRTKBT_VERSION Version)
{
    PUCHAR Firmware = NULL, Config = NULL, Patch = NULL;
    ULONG FirmwareLength, ConfigLength = 0, PatchLength, Length, Index;
    const RTKBT_CHIP *Chip = NULL;
    UCHAR Result[7];
    NTSTATUS Status;

    Status = RtkBtReadVersion(Command, Context, Version);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Version->Manufacturer != RTKBT_MANUFACTURER_REALTEK)
        return STATUS_SUCCESS;

    for (Index = 0; Index < RTL_NUMBER_OF(RtkBtChips); Index++)
    {
        if (RtkBtChips[Index].LmpSubversion == Version->LmpSubversion &&
            RtkBtChips[Index].HciRevision == Version->HciRevision &&
            RtkBtChips[Index].HciVersion == Version->HciVersion &&
            RtkBtChips[Index].Bus == Bus)
        {
            Chip = &RtkBtChips[Index];
            break;
        }
    }
    if (!Chip)
    {
        DPRINT1("RTKBTFW: no patch needed or known for this controller\n");
        return STATUS_SUCCESS;
    }

    Status = Command(Context, RTK_READ_ROM_VERSION, NULL, 0, Result, sizeof(Result), &Length);
    if (!NT_SUCCESS(Status) || Length < 2)
        return NT_SUCCESS(Status) ? STATUS_INVALID_PARAMETER : Status;
    DPRINT1("RTKBTFW: %ws ROM version %u\n", Chip->Name, Result[1]);

    Status = RtkBtLoadFile(Chip->Name, L"_fw.bin", &Firmware, &FirmwareLength);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        DPRINT1("RTKBTFW: %ws_fw.bin not installed, the controller keeps its ROM firmware\n", Chip->Name);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTKBTFW: %ws_fw.bin unavailable 0x%08lx\n", Chip->Name, Status);
        return Status;
    }
    if (!NT_SUCCESS(RtkBtLoadFile(Chip->Name, L"_config.bin", &Config, &ConfigLength)))
        ConfigLength = 0;

    Status = RtkBtBuildPatch(Firmware, FirmwareLength, Config, ConfigLength, Result[1], Bus, &Patch, &PatchLength);
    if (NT_SUCCESS(Status))
        Status = RtkBtDownload(Command, Context, Patch, PatchLength);
    if (Patch)
        ExFreePoolWithTag(Patch, RTKBT_TAG);
    if (Config)
        ExFreePoolWithTag(Config, RTKBT_TAG);
    ExFreePoolWithTag(Firmware, RTKBT_TAG);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtkBtReadVersion(Command, Context, Version);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Command(Context, HCI_READ_BD_ADDR, NULL, 0, Result, sizeof(Result), &Length);
    if (NT_SUCCESS(Status) && Length >= 7)
        DPRINT1("RTKBTFW: BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x\n",
                Result[6], Result[5], Result[4], Result[3], Result[2], Result[1]);
    return Status;
}
