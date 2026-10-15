/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware file parsing and WCPU firmware download
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

#define RTW_MFW_SIGNATURE               0xFF
#define RTW_MFW_HEADER_LENGTH           16
#define RTW_MFW_ENTRY_LENGTH            16
#define RTW_FW_HEADER_LENGTH            32
#define RTW_FW_SECTION_LENGTH           16
#define RTW_FW_SECTION_CHECKSUM_LENGTH  8
#define RTW_FW_SECURITY_SECTION         9
#define RTW_FW_SECURE_SECTION_LENGTH    960

#define RTW_WD_CHANNEL_DMA_SHIFT        16
#define RTW_WD_FW_DL                    RTW_BIT(20)
#define RTW_WD_TXPKT_SIZE_MASK          0x3FFF

#define RTW_H2C_TYPE_SHIFT              16
#define RTW_H2C_CLASS_SHIFT             2
#define RTW_H2C_FUNC_SHIFT              8
#define RTW_H2C_SEQ_SHIFT               24
#define RTW_H2C_CAT_MAC                 1
#define RTW_H2C_CL_MAC_FWDL             3
#define RTW_H2C_FUNC_MAC_FWHDR_DL       0

typedef struct _RTW_FW_SECTION
{
    const UCHAR *Data;
    ULONG Length;
} RTW_FW_SECTION, *PRTW_FW_SECTION;

NTSTATUS
RtwLoadFirmware(
    _In_ PRTW_ADAPTER Adapter)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Buffer;
    ULONG Length;

    if (Adapter->Firmware)
        return STATUS_SUCCESS;

    RtlInitUnicodeString(&Path, RTW_FIRMWARE_PATH);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle,
                          GENERIC_READ | SYNCHRONIZE,
                          &Attributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: firmware %ws unavailable 0x%08lx\n", RTW_FIRMWARE_PATH, Status);
        return Status;
    }

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) ||
        FileInfo.EndOfFile.QuadPart <= RTW_MFW_HEADER_LENGTH ||
        FileInfo.EndOfFile.QuadPart > RTW_FIRMWARE_MAX_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Length = (ULONG)FileInfo.EndOfFile.QuadPart;
    Buffer = ExAllocatePoolWithTag(PagedPool, Length, RTW_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Length, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Length)
    {
        ExFreePoolWithTag(Buffer, RTW_TAG);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    Adapter->Firmware = Buffer;
    Adapter->FirmwareLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtwSelectFirmware(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Type,
    _Outptr_result_bytebuffer_(*Length) const UCHAR **Image,
    _Out_ PULONG Length)
{
    const UCHAR *File = Adapter->Firmware;
    const UCHAR *Entry, *Best = NULL;
    ULONG Count, Index, Shift, Size;

    *Image = NULL;
    *Length = 0;

    if (File[0] != RTW_MFW_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

    Count = File[1];
    if (RTW_MFW_HEADER_LENGTH + Count * RTW_MFW_ENTRY_LENGTH > Adapter->FirmwareLength)
        return STATUS_INVALID_IMAGE_FORMAT;

    for (Index = 0; Index < Count; Index++)
    {
        Entry = File + RTW_MFW_HEADER_LENGTH + Index * RTW_MFW_ENTRY_LENGTH;
        if (Entry[1] != Type || Entry[2] != 0 || Entry[0] > Adapter->ChipVersion)
            continue;
        if (!Best || Best[0] < Entry[0])
            Best = Entry;
    }

    if (!Best)
        return STATUS_NOT_FOUND;

    Shift = RtwLe32(Best + 4);
    Size = RtwLe32(Best + 8);
    if (Shift > Adapter->FirmwareLength || Size > Adapter->FirmwareLength - Shift || Size < RTW_FW_HEADER_LENGTH)
        return STATUS_INVALID_IMAGE_FORMAT;

    *Image = File + Shift;
    *Length = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtwSendH2cPacket(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN FirmwareData,
    _In_ ULONG PayloadLength)
{
    ULONG Length = RTW_WD_BODY_LENGTH + PayloadLength;
    ULONG Required, Available, Poll;
    PUCHAR Wd = Adapter->TxBuffer;

    if (Length > RTW_TX_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Wd, RTW_WD_BODY_LENGTH);
    RtwPutLe32(Wd, ((ULONG)RTW_DMA_CH_H2C << RTW_WD_CHANNEL_DMA_SHIFT) | (FirmwareData ? RTW_WD_FW_DL : 0));
    RtwPutLe32(Wd + 8, PayloadLength & RTW_WD_TXPKT_SIZE_MASK);

    Required = ((PayloadLength + 127) / 128) * 2;
    for (Poll = 0; ; Poll++)
    {
        Available = (RtwRead32(Adapter, R_AX_SDIO_TXPG_WP) >> B_AX_SDIO_ACH12_AVAL_PG_SHIFT) & B_AX_SDIO_AVAL_PG_MASK;
        if (Available >= Required)
            break;
        if (Adapter->IoError || Poll >= 1000)
        {
            DPRINT1("RTW8852BS: H2C queue full, %lu of %lu pages\n", Available, Required);
            return STATUS_DEVICE_BUSY;
        }
        KeStallExecutionProcessor(50);
    }

    return RtwSdioWritePacket(Adapter, RTW_DMA_CH_H2C, Length);
}

static
NTSTATUS
RtwWaitFirmwarePath(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Bit)
{
    ULONG Poll;

    for (Poll = 0; Poll < 4000; Poll++)
    {
        if (RtwRead8(Adapter, R_AX_WCPU_FW_CTRL) & Bit)
            return STATUS_SUCCESS;
        if (Adapter->IoError)
            break;
        KeStallExecutionProcessor(100);
    }

    return STATUS_IO_TIMEOUT;
}

static
UCHAR
RtwFirmwareStatus(
    _In_ PRTW_ADAPTER Adapter)
{
    return (UCHAR)((RtwRead8(Adapter, R_AX_WCPU_FW_CTRL) & B_AX_WCPU_FWDL_STS_MASK) >> B_AX_WCPU_FWDL_STS_SHIFT);
}

static
VOID
RtwDisableCpu(
    _In_ PRTW_ADAPTER Adapter)
{
    RtwWrite32Clear(Adapter, R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
    RtwWrite32Clear(Adapter, R_AX_WCPU_FW_CTRL, B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
    RtwWrite32Clear(Adapter, R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);
    RtwWrite32Clear(Adapter, R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
    RtwWrite32Set(Adapter, R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
    RtwWrite32Clear(Adapter, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    RtwWrite32Set(Adapter, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
}

static
NTSTATUS
RtwEnableCpuForDownload(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Value;

    if (RtwRead32(Adapter, R_AX_PLATFORM_ENABLE) & B_AX_WCPU_EN)
        return STATUS_DEVICE_BUSY;

    RtwWrite32(Adapter, R_AX_UDM1, 0);
    RtwWrite32(Adapter, R_AX_UDM2, 0);
    RtwWrite32(Adapter, R_AX_HALT_H2C_CTRL, 0);
    RtwWrite32(Adapter, R_AX_HALT_C2H_CTRL, 0);
    RtwWrite32(Adapter, R_AX_HALT_H2C, 0);
    RtwWrite32(Adapter, R_AX_HALT_C2H, 0);

    RtwWrite32Set(Adapter, R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);

    Value = RtwRead32(Adapter, R_AX_WCPU_FW_CTRL);
    Value &= ~(B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY | B_AX_WCPU_FWDL_STS_MASK);
    Value |= (RTW_FWDL_INITIAL_STATE << B_AX_WCPU_FWDL_STS_SHIFT) | B_AX_WCPU_FWDL_EN;
    RtwWrite32(Adapter, R_AX_WCPU_FW_CTRL, Value);

    RtwWrite32Mask(Adapter, R_AX_SEC_CTRL, B_AX_SEC_IDMEM_SIZE_CONFIG_MASK, 0x2);
    RtwWrite16(Adapter, R_AX_BOOT_REASON, RtwRead16(Adapter, R_AX_BOOT_REASON) & ~B_AX_BOOT_REASON_MASK);
    RtwWrite32Set(Adapter, R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtwDownloadImage(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(ImageLength) const UCHAR *Image,
    _In_ ULONG ImageLength)
{
    RTW_FW_SECTION Sections[RTW_FW_SECTION_MAX];
    ULONG SectionCount, HeaderLength, BaseLength, Index, Offset, Chunk, Word;
    const UCHAR *Section, *Data;
    PUCHAR Payload;
    NTSTATUS Status;
    UCHAR State;

    SectionCount = (RtwLe32(Image + 24) >> 8) & 0xFF;
    if (SectionCount == 0 || SectionCount > RTW_FW_SECTION_MAX)
        return STATUS_INVALID_IMAGE_FORMAT;

    BaseLength = RTW_FW_HEADER_LENGTH + SectionCount * RTW_FW_SECTION_LENGTH;
    HeaderLength = (RtwLe32(Image + 28) & RTW_BIT(16)) ? (RtwLe32(Image + 12) >> 16) & 0xFF : BaseLength;
    if (HeaderLength < BaseLength || HeaderLength > ImageLength)
        return STATUS_INVALID_IMAGE_FORMAT;

    Data = Image + HeaderLength;
    for (Index = 0; Index < SectionCount; Index++)
    {
        Section = Image + RTW_FW_HEADER_LENGTH + Index * RTW_FW_SECTION_LENGTH;
        Word = RtwLe32(Section + 4);
        Sections[Index].Data = Data;
        Sections[Index].Length = (Word & 0x00FFFFFF) + ((Word & RTW_BIT(28)) ? RTW_FW_SECTION_CHECKSUM_LENGTH : 0);
        if (((Word >> 24) & 0xF) == RTW_FW_SECURITY_SECTION && RtwLe32(Section + 8) != 0)
        {
            DPRINT1("RTW8852BS: signed security sections are not supported\n");
            return STATUS_NOT_SUPPORTED;
        }
        if ((ULONG)(Data - Image) + Sections[Index].Length > ImageLength)
            return STATUS_INVALID_IMAGE_FORMAT;
        Data += Sections[Index].Length;
        if (((Word >> 24) & 0xF) == RTW_FW_SECURITY_SECTION && Adapter->SecureBoot)
            Sections[Index].Length = min(Sections[Index].Length, RTW_FW_SECURE_SECTION_LENGTH);
    }
    if (Data != Image + ImageLength)
        return STATUS_INVALID_IMAGE_FORMAT;

    RtwDisableCpu(Adapter);
    Status = RtwEnableCpuForDownload(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtwWaitFirmwarePath(Adapter, B_AX_H2C_PATH_RDY);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: H2C path not ready, FW_CTRL 0x%08lx\n", RtwRead32(Adapter, R_AX_WCPU_FW_CTRL));
        return Status;
    }

    Payload = Adapter->TxBuffer + RTW_WD_BODY_LENGTH;
    RtwPutLe32(Payload,
               ((ULONG)RTW_H2C_CAT_MAC) |
               ((ULONG)RTW_H2C_CL_MAC_FWDL << RTW_H2C_CLASS_SHIFT) |
               ((ULONG)RTW_H2C_FUNC_MAC_FWHDR_DL << RTW_H2C_FUNC_SHIFT) |
               ((ULONG)Adapter->H2cSequence << RTW_H2C_SEQ_SHIFT));
    RtwPutLe32(Payload + 4, (BaseLength + RTW_H2C_HEADER_LENGTH) & 0x3FFF);
    RtlCopyMemory(Payload + RTW_H2C_HEADER_LENGTH, Image, BaseLength);
    Word = RtwLe32(Payload + RTW_H2C_HEADER_LENGTH + 28);
    RtwPutLe32(Payload + RTW_H2C_HEADER_LENGTH + 28, (Word & ~0xFFFFUL) | RTW_FWDL_PART_SIZE);
    if (Adapter->SecureBoot)
    {
        for (Index = 0; Index < SectionCount; Index++)
        {
            PUCHAR Entry = Payload + RTW_H2C_HEADER_LENGTH + RTW_FW_HEADER_LENGTH + Index * RTW_FW_SECTION_LENGTH;

            Word = RtwLe32(Entry + 4);
            if (((Word >> 24) & 0xF) == RTW_FW_SECURITY_SECTION)
                RtwPutLe32(Entry + 4, (Word & ~0x00FFFFFFUL) | Sections[Index].Length);
        }
    }

    Status = RtwSendH2cPacket(Adapter, FALSE, BaseLength + RTW_H2C_HEADER_LENGTH);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtwWaitFirmwarePath(Adapter, B_AX_FWDL_PATH_RDY);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: FWDL path not ready, FW_CTRL 0x%08lx\n", RtwRead32(Adapter, R_AX_WCPU_FW_CTRL));
        return Status;
    }

    RtwWrite32(Adapter, R_AX_HALT_H2C_CTRL, 0);
    RtwWrite32(Adapter, R_AX_HALT_C2H_CTRL, 0);

    for (Index = 0; Index < SectionCount; Index++)
    {
        for (Offset = 0; Offset < Sections[Index].Length; Offset += Chunk)
        {
            Chunk = min(Sections[Index].Length - Offset, (ULONG)RTW_FWDL_PART_SIZE);
            RtlCopyMemory(Payload, Sections[Index].Data + Offset, Chunk);
            Status = RtwSendH2cPacket(Adapter, TRUE, Chunk);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    RtwSleep(5);
    for (Index = 0; Index < 400; Index++)
    {
        State = RtwFirmwareStatus(Adapter);
        if (State == RTW_FWDL_WCPU_FW_INIT_RDY)
            return STATUS_SUCCESS;
        if (State == RTW_FWDL_CHECKSUM_FAIL || State == RTW_FWDL_SECURITY_FAIL || State == RTW_FWDL_CV_NOT_MATCH)
            break;
        RtwSleep(1);
    }

    DPRINT1("RTW8852BS: firmware did not start, status %u FW_CTRL 0x%08lx BOOT_DBG 0x%08lx\n",
            State, RtwRead32(Adapter, R_AX_WCPU_FW_CTRL), RtwRead32(Adapter, R_AX_BOOT_DBG));
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
RtwDownloadFirmware(
    _In_ PRTW_ADAPTER Adapter)
{
    const UCHAR *Image;
    ULONG ImageLength;
    NTSTATUS Status;
    ULONG Attempt;

    Status = RtwSelectFirmware(Adapter, RTW_FW_TYPE_NORMAL_CE, &Image, &ImageLength);
    if (!NT_SUCCESS(Status))
        Status = RtwSelectFirmware(Adapter, RTW_FW_TYPE_NORMAL, &Image, &ImageLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: no firmware for chip cut %u (0x%08lx)\n", Adapter->ChipVersion, Status);
        return Status;
    }

    for (Attempt = 0; Attempt < 3; Attempt++)
    {
        Status = RtwMacPreInit(Adapter);
        if (NT_SUCCESS(Status))
            Status = RtwDownloadImage(Adapter, Image, ImageLength);
        if (NT_SUCCESS(Status))
        {
            Adapter->H2cSequence = 0;
            DPRINT1("RTW8852BS: firmware %u.%u.%u.%u running\n",
                    Image[4], Image[5], Image[6], Image[7]);
            return STATUS_SUCCESS;
        }
        DPRINT1("RTW8852BS: firmware download attempt %lu failed 0x%08lx\n", Attempt + 1, Status);
    }

    return Status;
}
