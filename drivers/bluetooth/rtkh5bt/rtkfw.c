/*
 * PROJECT:     LiberNT Realtek Bluetooth UART transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Realtek controller identification and patch download
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rtkh5bt.h"

#define NDEBUG
#include <debug.h>

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

static
NTSTATUS
RtkH5LoadFile(
    _In_ PCWSTR FileName,
    _Out_ PUCHAR *Data,
    _Out_ PULONG Length)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    HANDLE Handle;
    PUCHAR Buffer;
    ULONG Size;
    NTSTATUS Status;

    *Data = NULL;
    *Length = 0;
    RtlInitUnicodeString(&Path, FileName);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) || FileInfo.EndOfFile.QuadPart == 0 ||
        FileInfo.EndOfFile.QuadPart > RTKH5_FIRMWARE_MAX_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Size = (ULONG)FileInfo.EndOfFile.QuadPart;
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, RTKH5_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Size, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Size)
    {
        ExFreePoolWithTag(Buffer, RTKH5_TAG);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *Data = Buffer;
    *Length = Size;
    return STATUS_SUCCESS;
}

static
USHORT
RtkGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

static
ULONG
RtkGet32(
    _In_reads_(4) const UCHAR *Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static
VOID
RtkPut32(
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
RtkH5FixConfig(
    _Inout_updates_bytes_(Length) PUCHAR Config,
    _In_ ULONG Length)
{
    ULONG Offset = 6, End;

    if (Length < 6 || RtkGet32(Config) != RTK_CONFIG_SIGNATURE)
        return;
    End = min(Length, 6 + (ULONG)RtkGet16(Config + 4));
    while (Offset + 3 <= End)
    {
        USHORT Entry = RtkGet16(Config + Offset);
        UCHAR EntryLength = Config[Offset + 2];
        PUCHAR EntryData = Config + Offset + 3;

        if (Offset + 3 + EntryLength > End)
            break;
        if (Entry == RTK_CONFIG_UART_OFFSET && EntryLength >= 4)
        {
            RtkPut32(EntryData, RTK_UART_BAUD_115200);
            if (EntryLength > RTK_CONFIG_UART_FLOW_INDEX)
                EntryData[RTK_CONFIG_UART_FLOW_INDEX] &= (UCHAR)~0x04;
        }
        Offset += 3 + EntryLength;
    }
}

static
NTSTATUS
RtkH5BuildPatch(
    _In_reads_bytes_(FirmwareLength) const UCHAR *Firmware,
    _In_ ULONG FirmwareLength,
    _In_reads_bytes_opt_(ConfigLength) const UCHAR *Config,
    _In_ ULONG ConfigLength,
    _In_ UCHAR RomVersion,
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

    Version = RtkGet32(Firmware + 8);
    Count = RtkGet16(Firmware + 12);
    if (!Count || RTK_EPATCH_HEADER_LENGTH + (ULONG)Count * 8 > FirmwareLength)
        return STATUS_INVALID_IMAGE_FORMAT;

    for (Index = 0; Index < Count; Index++)
    {
        const UCHAR *Table = Firmware + RTK_EPATCH_HEADER_LENGTH;
        USHORT ChipId = RtkGet16(Table + Index * 2);
        USHORT Length = RtkGet16(Table + Count * 2 + Index * 2);
        ULONG Offset = RtkGet32(Table + Count * 4 + Index * 4);

        if (ChipId != RomVersion + 1)
            continue;
        if (Length < 4 || Offset > FirmwareLength || Length > FirmwareLength - Offset)
            return STATUS_INVALID_IMAGE_FORMAT;

        Buffer = ExAllocatePoolWithTag(PagedPool, Length + ConfigLength, RTKH5_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(Buffer, Firmware + Offset, Length);
        RtkPut32(Buffer + Length - 4, Version);
        if (Config && ConfigLength)
        {
            RtlCopyMemory(Buffer + Length, Config, ConfigLength);
            RtkH5FixConfig(Buffer + Length, ConfigLength);
        }
        *Patch = Buffer;
        *PatchLength = Length + ConfigLength;
        DPRINT1("RTKH5BT: patch for chip %u, version 0x%08lx, %lu bytes\n", ChipId, Version, *PatchLength);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
RtkH5Download(
    _In_ PRTKH5_DEVICE Dev,
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
        Status = RtkH5HciCommand(Dev, RTK_DOWNLOAD_PATCH, Chunk, (UCHAR)(1 + Size), Result, sizeof(Result), &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RTKH5BT: patch frame %lu of %lu failed 0x%08lx\n", Index, Count, Status);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtkH5ReadVersion(
    _In_ PRTKH5_DEVICE Dev)
{
    UCHAR Result[9];
    ULONG Length;
    NTSTATUS Status;

    Status = RtkH5HciCommand(Dev, HCI_READ_LOCAL_VERSION, NULL, 0, Result, sizeof(Result), &Length);
    if (!NT_SUCCESS(Status) || Length < sizeof(Result))
        return NT_SUCCESS(Status) ? STATUS_INVALID_PARAMETER : Status;
    Dev->HciRevision = RtkGet16(Result + 2);
    Dev->LmpSubversion = RtkGet16(Result + 7);
    DPRINT1("RTKH5BT: HCI version %u revision 0x%04x, LMP version %u subversion 0x%04x, manufacturer %u\n",
            Result[1], Dev->HciRevision, Result[4], Dev->LmpSubversion, RtkGet16(Result + 5));
    return STATUS_SUCCESS;
}

NTSTATUS
RtkH5InitializeController(
    _In_ PRTKH5_DEVICE Dev)
{
    PUCHAR Firmware = NULL, Config = NULL, Patch = NULL;
    ULONG FirmwareLength, ConfigLength = 0, PatchLength, Length;
    UCHAR Result[7];
    NTSTATUS Status;

    Status = RtkH5ReadVersion(Dev);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtkH5HciCommand(Dev, RTK_READ_ROM_VERSION, NULL, 0, Result, sizeof(Result), &Length);
    if (!NT_SUCCESS(Status) || Length < 2)
        return NT_SUCCESS(Status) ? STATUS_INVALID_PARAMETER : Status;
    Dev->RomVersion = Result[1];
    DPRINT1("RTKH5BT: ROM version %u\n", Dev->RomVersion);

    Status = RtkH5LoadFile(RTKH5_FIRMWARE_PATH, &Firmware, &FirmwareLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTKH5BT: patch %ws unavailable 0x%08lx\n", RTKH5_FIRMWARE_PATH, Status);
        return Status;
    }
    if (!NT_SUCCESS(RtkH5LoadFile(RTKH5_CONFIG_PATH, &Config, &ConfigLength)))
        ConfigLength = 0;

    Status = RtkH5BuildPatch(Firmware, FirmwareLength, Config, ConfigLength, Dev->RomVersion, &Patch, &PatchLength);
    if (NT_SUCCESS(Status))
        Status = RtkH5Download(Dev, Patch, PatchLength);
    if (Patch)
        ExFreePoolWithTag(Patch, RTKH5_TAG);
    if (Config)
        ExFreePoolWithTag(Config, RTKH5_TAG);
    ExFreePoolWithTag(Firmware, RTKH5_TAG);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtkH5ReadVersion(Dev);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtkH5HciCommand(Dev, HCI_READ_BD_ADDR, NULL, 0, Result, sizeof(Result), &Length);
    if (NT_SUCCESS(Status) && Length >= 7)
        DPRINT1("RTKH5BT: BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x\n",
                Result[6], Result[5], Result[4], Result[3], Result[2], Result[1]);
    return Status;
}
