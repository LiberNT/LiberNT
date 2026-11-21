/*
 * PROJECT:     LiberNT Storport AHCI miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AHCI 1.3.1 host bus adapter: port bring-up, SCSI to ATA translation and ATAPI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "storahci.h"

#define NDEBUG
#include <debug.h>

/* Register access */

FORCEINLINE
ULONG
AhciRead(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return StorPortReadRegisterUlong(Adapter, (PULONG)(Adapter->Abar + Offset));
}

FORCEINLINE
VOID
AhciWrite(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    StorPortWriteRegisterUlong(Adapter, (PULONG)(Adapter->Abar + Offset), Value);
}

FORCEINLINE
ULONG
AhciPortRead(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Offset)
{
    return StorPortReadRegisterUlong(Adapter, (PULONG)(Port->Registers + Offset));
}

FORCEINLINE
VOID
AhciPortWrite(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    StorPortWriteRegisterUlong(Adapter, (PULONG)(Port->Registers + Offset), Value);
}

static
BOOLEAN
AhciWaitPort(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG Microseconds)
{
    ULONG Waited;

    for (Waited = 0; ; Waited += 10)
    {
        if ((AhciPortRead(Adapter, Port, Offset) & Mask) == Value)
            return TRUE;
        if (Waited >= Microseconds)
            return FALSE;
        StorPortStallExecution(10);
    }
}

/* Port state machine */

static
BOOLEAN
AhciStopCommandEngine(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Microseconds)
{
    ULONG Command = AhciPortRead(Adapter, Port, AHCI_PX_CMD);

    if (Command & AHCI_PX_CMD_ST)
        AhciPortWrite(Adapter, Port, AHCI_PX_CMD, Command & ~AHCI_PX_CMD_ST);

    return AhciWaitPort(Adapter, Port, AHCI_PX_CMD, AHCI_PX_CMD_CR, 0, Microseconds);
}

static
BOOLEAN
AhciStopFisReceive(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Microseconds)
{
    ULONG Command = AhciPortRead(Adapter, Port, AHCI_PX_CMD);

    ASSERT(!(Command & AHCI_PX_CMD_ST));
    if (Command & AHCI_PX_CMD_FRE)
        AhciPortWrite(Adapter, Port, AHCI_PX_CMD, Command & ~AHCI_PX_CMD_FRE);

    return AhciWaitPort(Adapter, Port, AHCI_PX_CMD, AHCI_PX_CMD_FR, 0, Microseconds);
}

static
VOID
AhciStartCommandEngine(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    ULONG Command = AhciPortRead(Adapter, Port, AHCI_PX_CMD);

    ASSERT(Command & AHCI_PX_CMD_FRE);
    AhciPortWrite(Adapter, Port, AHCI_PX_CMD, Command | AHCI_PX_CMD_ST);
}

static
VOID
AhciProgramPortMemory(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    AhciPortWrite(Adapter, Port, AHCI_PX_CLB, Port->CommandListAddress.LowPart);
    AhciPortWrite(Adapter, Port, AHCI_PX_CLBU, Adapter->Dma64 ? (ULONG)Port->CommandListAddress.HighPart : 0);
    AhciPortWrite(Adapter, Port, AHCI_PX_FB, Port->ReceivedFisAddress.LowPart);
    AhciPortWrite(Adapter, Port, AHCI_PX_FBU, Adapter->Dma64 ? (ULONG)Port->ReceivedFisAddress.HighPart : 0);
}

static
BOOLEAN
AhciResetLink(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    ULONG Control = AhciPortRead(Adapter, Port, AHCI_PX_SCTL) & ~AHCI_PX_SCTL_DET_MASK;

    ASSERT(!(AhciPortRead(Adapter, Port, AHCI_PX_CMD) & AHCI_PX_CMD_ST));

    AhciPortWrite(Adapter, Port, AHCI_PX_SCTL, Control | AHCI_PX_SCTL_DET_COMRESET);
    StorPortStallExecution(1000);
    AhciPortWrite(Adapter, Port, AHCI_PX_SCTL, Control);

    if (!AhciWaitPort(Adapter, Port, AHCI_PX_SSTS, AHCI_PX_SSTS_DET_MASK, AHCI_PX_SSTS_DET_PRESENT, 100 * 1000))
        return FALSE;

    AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);
    return TRUE;
}

static
BOOLEAN
AhciRecoverPort(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ BOOLEAN AllowLongWaits)
{
    ULONG Limit = AllowLongWaits ? 500 * 1000 : 500;
    ULONG TaskFile;

    if (!AhciStopCommandEngine(Adapter, Port, Limit))
        return FALSE;

    AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);
    AhciPortWrite(Adapter, Port, AHCI_PX_IS, MAXULONG);

    TaskFile = AhciPortRead(Adapter, Port, AHCI_PX_TFD);
    if ((TaskFile & (AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ)) && (Adapter->Capabilities & AHCI_CAP_SCLO))
    {
        AhciPortWrite(Adapter, Port, AHCI_PX_CMD, AhciPortRead(Adapter, Port, AHCI_PX_CMD) | AHCI_PX_CMD_CLO);
        AhciWaitPort(Adapter, Port, AHCI_PX_CMD, AHCI_PX_CMD_CLO, 0, Limit);
        TaskFile = AhciPortRead(Adapter, Port, AHCI_PX_TFD);
    }

    if (TaskFile & (AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ))
    {
        if (!AllowLongWaits || !AhciResetLink(Adapter, Port))
            return FALSE;
        if (!AhciWaitPort(Adapter, Port, AHCI_PX_TFD, AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ, 0, 5 * 1000 * 1000))
            return FALSE;
    }

    AhciStartCommandEngine(Adapter, Port);
    return TRUE;
}

/* Command construction */

static
PAHCI_FIS_H2D
AhciPrepareFis(
    _In_ PAHCI_PORT Port,
    _In_ UCHAR Command)
{
    PAHCI_FIS_H2D Fis = (PAHCI_FIS_H2D)Port->CommandTable->CommandFis;

    RtlZeroMemory(Port->CommandTable->CommandFis, sizeof(Port->CommandTable->CommandFis));
    RtlZeroMemory(Port->CommandTable->AtapiCommand, sizeof(Port->CommandTable->AtapiCommand));
    Fis->Type = AHCI_FIS_REGISTER_H2D;
    Fis->Flags = AHCI_FIS_H2D_COMMAND;
    Fis->Command = Command;
    return Fis;
}

static
VOID
AhciSetPrd(
    _In_ PAHCI_PRD Prd,
    _In_ ULONGLONG Address,
    _In_ ULONG Length)
{
    ASSERT(Length != 0 && Length <= AHCI_PRD_MAX_BYTES);
    ASSERT((Address & 1) == 0);

    Prd->DataAddress = (ULONG)Address;
    Prd->DataAddressUpper = (ULONG)(Address >> 32);
    Prd->Reserved = 0;
    /* AHCI byte counts are even; an odd tail is rounded up and the device sends no more than asked */
    Prd->ByteCount = ROUND_UP(Length, 2) - 1;
}

static
BOOLEAN
AhciBuildPrdtFromSrb(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _Out_ PULONG PrdCount)
{
    PSTOR_SCATTER_GATHER_LIST List;
    ULONG Count = 0;
    ULONG i;

    *PrdCount = 0;
    if (Srb->DataTransferLength == 0)
        return TRUE;

    List = StorPortGetScatterGatherList(Adapter, Srb);
    if (List == NULL)
        return FALSE;

    for (i = 0; i < List->NumberOfElements; i++)
    {
        ULONGLONG Address = List->List[i].PhysicalAddress.QuadPart;
        ULONG Length = List->List[i].Length;

        if (!Adapter->Dma64 && (Address >> 32) != 0)
            return FALSE;

        while (Length != 0)
        {
            ULONG Chunk = min(Length, AHCI_PRD_MAX_BYTES);

            if (Count == AHCI_MAX_PRDT_ENTRIES)
                return FALSE;
            AhciSetPrd(&Port->CommandTable->Prdt[Count++], Address, Chunk);
            Address += Chunk;
            Length -= Chunk;
        }
    }

    *PrdCount = Count;
    return TRUE;
}

static
VOID
AhciIssueCommand(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG HeaderFlags,
    _In_ ULONG PrdCount)
{
    PAHCI_COMMAND_HEADER Header = &Port->CommandList[0];

    /* TODO: NCQ over all command slots; every command uses slot 0, one at a time per port */
    ASSERT(PrdCount <= AHCI_MAX_PRDT_ENTRIES);
    ASSERT(!(AhciPortRead(Adapter, Port, AHCI_PX_CI) & 1));

    Header->Flags = (sizeof(AHCI_FIS_H2D) / sizeof(ULONG)) | HeaderFlags | (PrdCount << AHCI_HEADER_PRDTL_SHIFT);
    Header->TransferredBytes = 0;
    Header->TableAddress = Port->CommandTableAddress.LowPart;
    Header->TableAddressUpper = Adapter->Dma64 ? (ULONG)Port->CommandTableAddress.HighPart : 0;

    KeMemoryBarrier();
    AhciPortWrite(Adapter, Port, AHCI_PX_CI, 1);
}

/* Identify */

static
VOID
AhciCopyIdentifyString(
    _In_ PUSHORT Identify,
    _In_ ULONG Word,
    _In_ ULONG Words,
    _Out_writes_(Words * 2 + 1) PCHAR String)
{
    LONG Length;
    ULONG i;

    for (i = 0; i < Words; i++)
    {
        String[i * 2] = (CHAR)(Identify[Word + i] >> 8);
        String[i * 2 + 1] = (CHAR)(Identify[Word + i] & 0xFF);
    }
    String[Words * 2] = ANSI_NULL;

    for (Length = (LONG)(Words * 2) - 1; Length >= 0 && (String[Length] == ' ' || String[Length] == ANSI_NULL); Length--)
        String[Length] = ANSI_NULL;
}

static
VOID
AhciParseIdentify(
    _In_ PAHCI_PORT Port)
{
    PUSHORT Identify = Port->Identify;
    USHORT SectorInfo = Identify[ATA_ID_SECTOR_SIZE];

    AhciCopyIdentifyString(Identify, ATA_ID_SERIAL, ATA_ID_SERIAL_WORDS, Port->Serial);
    AhciCopyIdentifyString(Identify, ATA_ID_FIRMWARE, ATA_ID_FIRMWARE_WORDS, Port->Firmware);
    AhciCopyIdentifyString(Identify, ATA_ID_MODEL, ATA_ID_MODEL_WORDS, Port->Model);

    if (Port->DeviceType == AhciDeviceAtapi)
    {
        Port->Removable = (Identify[ATA_ID_CONFIG] & ATA_ID_CONFIG_REMOVABLE) != 0;
        Port->AtapiDeviceType = (Identify[ATA_ID_CONFIG] >> ATA_ID_CONFIG_ATAPI_TYPE_SHIFT) & ATA_ID_CONFIG_ATAPI_TYPE_MASK;
        Port->AtapiDma = (Identify[ATA_ID_CAPABILITIES] & ATA_ID_CAPABILITIES_DMA) != 0;
        return;
    }

    Port->Removable = (Identify[ATA_ID_CONFIG] & ATA_ID_CONFIG_REMOVABLE) != 0;
    Port->Lba48 = (Identify[ATA_ID_COMMAND_SET_ENABLED_2] & ATA_ID_COMMAND_SET_2_LBA48) != 0;
    Port->FlushExt = (Identify[ATA_ID_COMMAND_SET_ENABLED_2] & ATA_ID_COMMAND_SET_2_FLUSH_EXT) != 0;
    Port->WriteCacheEnabled = (Identify[ATA_ID_COMMAND_SET_ENABLED_1] & ATA_ID_COMMAND_SET_ENABLED_1_WCE) != 0;

    if (Port->Lba48)
    {
        Port->SectorCount = (ULONGLONG)Identify[ATA_ID_LBA48_SECTORS] |
                            ((ULONGLONG)Identify[ATA_ID_LBA48_SECTORS + 1] << 16) |
                            ((ULONGLONG)Identify[ATA_ID_LBA48_SECTORS + 2] << 32) |
                            ((ULONGLONG)Identify[ATA_ID_LBA48_SECTORS + 3] << 48);
    }
    else
    {
        Port->SectorCount = (ULONGLONG)Identify[ATA_ID_LBA28_SECTORS] |
                            ((ULONGLONG)Identify[ATA_ID_LBA28_SECTORS + 1] << 16);
    }

    Port->SectorSize = 512;
    Port->PhysicalSectorExponent = 0;
    if ((SectorInfo & ATA_ID_SECTOR_SIZE_VALID_MASK) == ATA_ID_SECTOR_SIZE_VALID)
    {
        if (SectorInfo & ATA_ID_SECTOR_SIZE_LARGE_LOGICAL)
        {
            ULONG Words = (ULONG)Identify[ATA_ID_LOGICAL_SECTOR_WORDS] |
                          ((ULONG)Identify[ATA_ID_LOGICAL_SECTOR_WORDS + 1] << 16);
            if (Words >= 256 && Words <= 32768)
                Port->SectorSize = Words * 2;
        }
        if (SectorInfo & ATA_ID_SECTOR_SIZE_MULTIPLE_PHYSICAL)
            Port->PhysicalSectorExponent = SectorInfo & ATA_ID_SECTOR_SIZE_EXPONENT_MASK;
    }

    Port->RotationRate = Identify[ATA_ID_ROTATION_RATE];
}

static
BOOLEAN
AhciPollCommand(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG Microseconds)
{
    ULONG Waited;

    for (Waited = 0; Waited <= Microseconds; Waited += 10)
    {
        if (AhciPortRead(Adapter, Port, AHCI_PX_IS) & AHCI_PX_IS_ERROR)
            return FALSE;
        if (!(AhciPortRead(Adapter, Port, AHCI_PX_CI) & 1))
            return !(AhciPortRead(Adapter, Port, AHCI_PX_TFD) & AHCI_PX_TFD_ERR);
        StorPortStallExecution(10);
    }

    return FALSE;
}

static
BOOLEAN
AhciIdentify(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ AHCI_DEVICE_TYPE Type)
{
    AhciPrepareFis(Port, Type == AhciDeviceAtapi ? ATA_CMD_IDENTIFY_PACKET_DEVICE : ATA_CMD_IDENTIFY_DEVICE);
    RtlZeroMemory(Port->Identify, AHCI_IDENTIFY_SIZE);
    AhciSetPrd(&Port->CommandTable->Prdt[0], Port->IdentifyAddress.QuadPart, AHCI_IDENTIFY_SIZE);
    AhciIssueCommand(Adapter, Port, 0, 1);

    if (AhciPollCommand(Adapter, Port, 5 * 1000 * 1000))
        return TRUE;

    AhciRecoverPort(Adapter, Port, TRUE);
    return FALSE;
}

static
VOID
AhciProbePort(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    AHCI_DEVICE_TYPE Type;
    ULONG Command, Signature;

    Port->DeviceType = AhciDeviceNone;

    AhciPortWrite(Adapter, Port, AHCI_PX_IE, 0);
    if (!AhciStopCommandEngine(Adapter, Port, 500 * 1000))
        return;
    if (!AhciStopFisReceive(Adapter, Port, 500 * 1000))
        return;

    AhciProgramPortMemory(Adapter, Port);
    AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);
    AhciPortWrite(Adapter, Port, AHCI_PX_IS, MAXULONG);

    Command = AhciPortRead(Adapter, Port, AHCI_PX_CMD) | AHCI_PX_CMD_FRE;
    if (Adapter->Capabilities & AHCI_CAP_SSS)
        Command |= AHCI_PX_CMD_SUD;
    AhciPortWrite(Adapter, Port, AHCI_PX_CMD, Command);

    if (!AhciWaitPort(Adapter, Port, AHCI_PX_SSTS, AHCI_PX_SSTS_DET_MASK, AHCI_PX_SSTS_DET_PRESENT, 20 * 1000) &&
        !AhciResetLink(Adapter, Port))
    {
        return;
    }
    AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);

    if (!AhciWaitPort(Adapter, Port, AHCI_PX_TFD, AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ, 0, 5 * 1000 * 1000))
    {
        if (!AhciResetLink(Adapter, Port) ||
            !AhciWaitPort(Adapter, Port, AHCI_PX_TFD, AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ, 0, 5 * 1000 * 1000))
        {
            return;
        }
    }

    Signature = AhciPortRead(Adapter, Port, AHCI_PX_SIG);
    if (Signature == AHCI_SIG_ATAPI)
        Type = AhciDeviceAtapi;
    else if (Signature == AHCI_SIG_ATA)
        Type = AhciDeviceAta;
    else
        /* TODO: port multipliers and enclosure management bridges */
        return;

    Command = AhciPortRead(Adapter, Port, AHCI_PX_CMD);
    if (Type == AhciDeviceAtapi)
        Command |= AHCI_PX_CMD_ATAPI;
    else
        Command &= ~AHCI_PX_CMD_ATAPI;
    AhciPortWrite(Adapter, Port, AHCI_PX_CMD, Command);
    AhciStartCommandEngine(Adapter, Port);

    if (!AhciIdentify(Adapter, Port, Type))
        return;

    Port->DeviceType = Type;
    AhciParseIdentify(Port);
    DPRINT1("AHCI port %lu: %s '%s' fw '%s' %I64u sectors of %lu bytes\n", Port->Index,
            Type == AhciDeviceAtapi ? "ATAPI" : "ATA", Port->Model, Port->Firmware,
            Port->SectorCount, Port->SectorSize);

    AhciPortWrite(Adapter, Port, AHCI_PX_IS, MAXULONG);
    AhciPortWrite(Adapter, Port, AHCI_PX_IE, AHCI_PX_IE_DEFAULT);
}

static
BOOLEAN
NTAPI
AhciPassiveInitialize(
    _In_ PVOID DeviceExtension)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;
    ULONG i;

    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        if (Adapter->PortsImplemented & (1u << i))
            AhciProbePort(Adapter, &Adapter->Ports[i]);
    }

    AhciWrite(Adapter, AHCI_HBA_IS, MAXULONG);
    AhciWrite(Adapter, AHCI_HBA_GHC, AhciRead(Adapter, AHCI_HBA_GHC) | AHCI_GHC_IE);
    return TRUE;
}

/* SCSI responses */

static
VOID
AhciCompleteSrb(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    StorPortNotification(RequestComplete, Adapter, Srb);
}

static
BOOLEAN
AhciWantsSense(
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    return Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength != 0 &&
           !(Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE);
}

static
VOID
AhciCompleteWithSenseData(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus,
    _In_reads_bytes_(Length) const VOID *Sense,
    _In_ ULONG Length)
{
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    if (AhciWantsSense(Srb))
    {
        ULONG Copy = min(Length, (ULONG)Srb->SenseInfoBufferLength);
        RtlCopyMemory(Srb->SenseInfoBuffer, Sense, Copy);
        Srb->SenseInfoBufferLength = (UCHAR)Copy;
        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    AhciCompleteSrb(Adapter, Srb, SrbStatus);
}

static
VOID
AhciCompleteWithSense(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR Key,
    _In_ UCHAR Asc,
    _In_ UCHAR Ascq)
{
    SENSE_DATA Sense;

    RtlZeroMemory(&Sense, sizeof(Sense));
    Sense.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
    Sense.SenseKey = Key;
    Sense.AdditionalSenseLength = sizeof(Sense) - RTL_SIZEOF_THROUGH_FIELD(SENSE_DATA, AdditionalSenseLength);
    Sense.AdditionalSenseCode = Asc;
    Sense.AdditionalSenseCodeQualifier = Ascq;
    AhciCompleteWithSenseData(Adapter, Srb, SRB_STATUS_ERROR, &Sense, sizeof(Sense));
}

static
VOID
AhciInvalidField(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
}

static
VOID
AhciReturnData(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    ULONG Copy = min(Length, Srb->DataTransferLength);

    if (Copy != 0)
    {
        ASSERT(Srb->DataBuffer != NULL);
        RtlCopyMemory(Srb->DataBuffer, Data, Copy);
    }
    Srb->DataTransferLength = Copy;
    AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
AhciPutBe16(_Out_writes_(2) PUCHAR Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 8);
    Buffer[1] = (UCHAR)Value;
}

static
VOID
AhciPutBe32(_Out_writes_(4) PUCHAR Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 24);
    Buffer[1] = (UCHAR)(Value >> 16);
    Buffer[2] = (UCHAR)(Value >> 8);
    Buffer[3] = (UCHAR)Value;
}

static
ULONG
AhciGetBe32(_In_reads_(4) const UCHAR *Buffer)
{
    return ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
}

static
VOID
AhciPadString(
    _Out_writes_(Length) PUCHAR Destination,
    _In_ PCSTR Source,
    _In_ ULONG SourceLength,
    _In_ ULONG Length)
{
    ULONG i;

    for (i = 0; i < Length; i++)
        Destination[i] = (i < SourceLength && Source[i] != ANSI_NULL) ? (UCHAR)Source[i] : ' ';
}

static
VOID
AhciAtaInquiry(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Data[64];
    PCSTR Model = Port->Model;
    PCSTR Product = Model;
    ULONG VendorLength, Length;
    PCSTR Space;

    RtlZeroMemory(Data, sizeof(Data));

    if (Srb->Cdb[1] & 0x01)
    {
        /* TODO: ATA Information VPD page 0x89, from which Windows 11 reports the full model with no vendor */
        switch (Srb->Cdb[2])
        {
            case 0x00:
                Data[1] = 0x00;
                Data[3] = 3;
                Data[4] = 0x00;
                Data[5] = 0x80;
                Data[6] = 0xB1;
                Length = 7;
                break;

            case 0x80:
                Length = (ULONG)strlen(Port->Serial);
                Data[1] = 0x80;
                Data[3] = (UCHAR)Length;
                RtlCopyMemory(&Data[4], Port->Serial, Length);
                Length += 4;
                break;

            case 0xB1:
                Data[1] = 0xB1;
                Data[3] = 0x3C;
                AhciPutBe16(&Data[4], Port->RotationRate);
                Length = 0x40;
                break;

            default:
                AhciInvalidField(Adapter, Srb);
                return;
        }

        AhciReturnData(Adapter, Srb, Data, Length);
        return;
    }

    if (Srb->Cdb[2] != 0)
    {
        AhciInvalidField(Adapter, Srb);
        return;
    }

    Data[0] = DIRECT_ACCESS_DEVICE;
    Data[1] = Port->Removable ? 0x80 : 0x00;
    Data[2] = 0x05;
    Data[3] = 0x02;
    Data[4] = 36 - 5;

    Space = strchr(Model, ' ');
    if (Space != NULL && Space - Model <= 8)
    {
        VendorLength = (ULONG)(Space - Model);
        Product = Space + 1;
    }
    else
    {
        VendorLength = min((ULONG)strlen(Model), 8);
        Product = Model + VendorLength;
    }
    AhciPadString(&Data[8], Model, VendorLength, 8);
    AhciPadString(&Data[16], Product, (ULONG)strlen(Product), 16);
    if (strlen(Port->Firmware) > 4)
        AhciPadString(&Data[32], Port->Firmware + strlen(Port->Firmware) - 4, 4, 4);
    else
        AhciPadString(&Data[32], Port->Firmware, (ULONG)strlen(Port->Firmware), 4);

    AhciReturnData(Adapter, Srb, Data, 36);
}

static
VOID
AhciAtaReadCapacity(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ BOOLEAN Long)
{
    UCHAR Data[32];
    ULONGLONG LastLba = Port->SectorCount ? Port->SectorCount - 1 : 0;

    RtlZeroMemory(Data, sizeof(Data));
    if (!Long)
    {
        AhciPutBe32(&Data[0], LastLba > MAXULONG ? MAXULONG : (ULONG)LastLba);
        AhciPutBe32(&Data[4], Port->SectorSize);
        AhciReturnData(Adapter, Srb, Data, 8);
        return;
    }

    AhciPutBe32(&Data[0], (ULONG)(LastLba >> 32));
    AhciPutBe32(&Data[4], (ULONG)LastLba);
    AhciPutBe32(&Data[8], Port->SectorSize);
    Data[13] = (UCHAR)(Port->PhysicalSectorExponent & 0x0F);
    AhciReturnData(Adapter, Srb, Data, sizeof(Data));
}

static
VOID
AhciAtaModeSense(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ BOOLEAN Ten)
{
    UCHAR Data[8 + 20];
    UCHAR PageCode = Srb->Cdb[2] & 0x3F;
    UCHAR PageControl = Srb->Cdb[2] >> 6;
    ULONG Header = Ten ? 8 : 4;
    PUCHAR Page = &Data[Header];
    ULONG Length;

    if (PageCode != MODE_PAGE_CACHING && PageCode != MODE_SENSE_RETURN_ALL)
    {
        AhciInvalidField(Adapter, Srb);
        return;
    }
    if (PageControl == 3)
    {
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ILLEGAL_REQUEST, AHCI_ASC_SAVING_NOT_SUPPORTED, 0);
        return;
    }

    RtlZeroMemory(Data, sizeof(Data));
    Data[Ten ? 3 : 2] = MODE_DSP_FUA_SUPPORTED;
    Page[0] = MODE_PAGE_CACHING;
    Page[1] = 0x12;
    if (PageControl != 1 && Port->WriteCacheEnabled)
        Page[2] = 0x04;
    Length = Header + 20;

    if (Ten)
        AhciPutBe16(&Data[0], Length - 2);
    else
        Data[0] = (UCHAR)(Length - 1);

    AhciReturnData(Adapter, Srb, Data, Length);
}

static
VOID
AhciReportLuns(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Data[16];

    RtlZeroMemory(Data, sizeof(Data));
    AhciPutBe32(&Data[0], 8);
    AhciReturnData(Adapter, Srb, Data, sizeof(Data));
}

static
VOID
AhciRequestSenseNoError(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    SENSE_DATA Sense;

    RtlZeroMemory(&Sense, sizeof(Sense));
    Sense.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
    Sense.AdditionalSenseLength = sizeof(Sense) - RTL_SIZEOF_THROUGH_FIELD(SENSE_DATA, AdditionalSenseLength);
    AhciReturnData(Adapter, Srb, &Sense, sizeof(Sense));
}

/* Issued commands */

static
VOID
AhciStartSrbCommand(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ AHCI_OPERATION Operation,
    _In_ ULONG HeaderFlags)
{
    ULONG PrdCount;

    if (!AhciBuildPrdtFromSrb(Adapter, Port, Srb, &PrdCount))
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    ASSERT(Port->ActiveKind == AhciCommandIdle);
    Port->ActiveKind = AhciCommandSrb;
    Port->ActiveSrb = Srb;
    Port->ActiveOperation = Operation;
    AhciIssueCommand(Adapter, Port, HeaderFlags, PrdCount);
}

static
VOID
AhciAtaReadWrite(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR Cdb = Srb->Cdb;
    ULONGLONG Lba;
    ULONG Count;
    BOOLEAN Write;
    BOOLEAN ForceUnitAccess;
    PAHCI_FIS_H2D Fis;

    switch (Cdb[0])
    {
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
            Lba = ((ULONG)(Cdb[1] & 0x1F) << 16) | ((ULONG)Cdb[2] << 8) | Cdb[3];
            Count = Cdb[4] ? Cdb[4] : 256;
            break;

        case SCSIOP_READ:
        case SCSIOP_WRITE:
            Lba = AhciGetBe32(&Cdb[2]);
            Count = ((ULONG)Cdb[7] << 8) | Cdb[8];
            break;

        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
            Lba = AhciGetBe32(&Cdb[2]);
            Count = AhciGetBe32(&Cdb[6]);
            break;

        default:
            Lba = ((ULONGLONG)AhciGetBe32(&Cdb[2]) << 32) | AhciGetBe32(&Cdb[6]);
            Count = AhciGetBe32(&Cdb[10]);
            break;
    }

    Write = (Cdb[0] == SCSIOP_WRITE6 || Cdb[0] == SCSIOP_WRITE ||
             Cdb[0] == SCSIOP_WRITE12 || Cdb[0] == SCSIOP_WRITE16);
    ForceUnitAccess = Write && Cdb[0] != SCSIOP_WRITE6 && (Cdb[1] & CDB_FORCE_MEDIA_ACCESS) && Port->WriteCacheEnabled;

    if (Count == 0)
    {
        Srb->DataTransferLength = 0;
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    if (Lba >= Port->SectorCount || Count > Port->SectorCount - Lba)
    {
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
        return;
    }

    if ((ULONGLONG)Count * Port->SectorSize != Srb->DataTransferLength ||
        Count > (Port->Lba48 ? 65536u : 256u))
    {
        /* FIXME: 28-bit only disks reject transfers above 256 sectors instead of splitting them */
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    if (Port->Lba48)
    {
        Fis = AhciPrepareFis(Port, Write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
        Fis->Device = ATA_DEVICE_LBA;
        Fis->Lba3 = (UCHAR)(Lba >> 24);
        Fis->Lba4 = (UCHAR)(Lba >> 32);
        Fis->Lba5 = (UCHAR)(Lba >> 40);
        Fis->CountExp = (UCHAR)(Count >> 8);
    }
    else
    {
        Fis = AhciPrepareFis(Port, Write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
        Fis->Device = ATA_DEVICE_LBA | (UCHAR)((Lba >> 24) & 0x0F);
    }
    Fis->Lba0 = (UCHAR)Lba;
    Fis->Lba1 = (UCHAR)(Lba >> 8);
    Fis->Lba2 = (UCHAR)(Lba >> 16);
    Fis->Count = (UCHAR)Count;

    /* TODO: WRITE DMA FUA EXT where IDENTIFY advertises it; a cache flush after the write covers every device */
    AhciStartSrbCommand(Adapter, Port, Srb, ForceUnitAccess ? AhciOperationWriteThenFlush : AhciOperationData,
                        Write ? AHCI_HEADER_WRITE : 0);
}

static
VOID
AhciAtaFlush(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_FIS_H2D Fis = AhciPrepareFis(Port, Port->FlushExt ? ATA_CMD_FLUSH_CACHE_EXT : ATA_CMD_FLUSH_CACHE);

    Fis->Device = ATA_DEVICE_LBA;
    Srb->DataTransferLength = 0;
    AhciStartSrbCommand(Adapter, Port, Srb, AhciOperationData, 0);
}

/* The flush that completes a forced-unit-access write keeps the request's transfer length */
static
VOID
AhciIssueSrbFlush(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_FIS_H2D Fis = AhciPrepareFis(Port, Port->FlushExt ? ATA_CMD_FLUSH_CACHE_EXT : ATA_CMD_FLUSH_CACHE);

    Fis->Device = ATA_DEVICE_LBA;
    ASSERT(Port->ActiveKind == AhciCommandIdle);
    ASSERT(Port->ActiveSrb == NULL);
    Port->ActiveKind = AhciCommandSrb;
    Port->ActiveSrb = Srb;
    Port->ActiveOperation = AhciOperationData;
    AhciIssueCommand(Adapter, Port, 0, 0);
}

static
VOID
AhciAtaPassThrough(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR Cdb = Srb->Cdb;
    UCHAR Protocol = (Cdb[1] >> 1) & 0x0F;
    BOOLEAN FromDevice = (Cdb[2] & 0x08) != 0;
    BOOLEAN NoData = (Protocol == 3);
    PAHCI_FIS_H2D Fis;

    if (Protocol != 3 && Protocol != 4 && Protocol != 5 && Protocol != 6 && Protocol != 10 && Protocol != 11)
    {
        AhciInvalidField(Adapter, Srb);
        return;
    }
    if (NoData != (Srb->DataTransferLength == 0))
    {
        AhciInvalidField(Adapter, Srb);
        return;
    }

    if (Cdb[0] == SCSIOP_ATA_PASSTHROUGH16)
    {
        Fis = AhciPrepareFis(Port, Cdb[14]);
        Fis->FeaturesExp = Cdb[3];
        Fis->Features = Cdb[4];
        Fis->CountExp = Cdb[5];
        Fis->Count = Cdb[6];
        Fis->Lba3 = Cdb[7];
        Fis->Lba0 = Cdb[8];
        Fis->Lba4 = Cdb[9];
        Fis->Lba1 = Cdb[10];
        Fis->Lba5 = Cdb[11];
        Fis->Lba2 = Cdb[12];
        Fis->Device = Cdb[13];
    }
    else
    {
        Fis = AhciPrepareFis(Port, Cdb[9]);
        Fis->Features = Cdb[3];
        Fis->Count = Cdb[4];
        Fis->Lba0 = Cdb[5];
        Fis->Lba1 = Cdb[6];
        Fis->Lba2 = Cdb[7];
        Fis->Device = Cdb[8];
    }

    Port->PassThroughCheckCondition = (Cdb[2] & 0x20) != 0;
    AhciStartSrbCommand(Adapter, Port, Srb, AhciOperationPassThrough,
                        (!NoData && !FromDevice) ? AHCI_HEADER_WRITE : 0);
}

static
VOID
AhciAtapiExecute(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_FIS_H2D Fis;
    ULONG Limit;

    if (Srb->CdbLength > sizeof(Port->CommandTable->AtapiCommand))
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
        return;
    }

    Fis = AhciPrepareFis(Port, ATA_CMD_PACKET);
    RtlCopyMemory(Port->CommandTable->AtapiCommand, Srb->Cdb, Srb->CdbLength);
    if (Srb->DataTransferLength != 0 && Port->AtapiDma)
        Fis->Features = 0x01;
    Limit = min(Srb->DataTransferLength, 0xFFFEu);
    Fis->Lba1 = (UCHAR)Limit;
    Fis->Lba2 = (UCHAR)(Limit >> 8);

    AhciStartSrbCommand(Adapter, Port, Srb, AhciOperationAtapi,
                        AHCI_HEADER_ATAPI | ((Srb->SrbFlags & SRB_FLAGS_DATA_OUT) ? AHCI_HEADER_WRITE : 0));
}

static
VOID
AhciAtapiRequestSense(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    PAHCI_FIS_H2D Fis = AhciPrepareFis(Port, ATA_CMD_PACKET);
    ULONG Length = min((ULONG)Port->ActiveSrb->SenseInfoBufferLength, (ULONG)AHCI_SENSE_BUFFER_SIZE);

    ASSERT(Port->ActiveKind == AhciCommandSrb);
    Length &= ~1u;
    if (Length < 18)
        Length = 18;

    Port->CommandTable->AtapiCommand[0] = SCSIOP_REQUEST_SENSE;
    Port->CommandTable->AtapiCommand[4] = (UCHAR)Length;
    Fis->Lba1 = (UCHAR)Length;
    Fis->Lba2 = 0;
    RtlZeroMemory(Port->SenseBuffer, AHCI_SENSE_BUFFER_SIZE);
    AhciSetPrd(&Port->CommandTable->Prdt[0], Port->SenseBufferAddress.QuadPart, Length);

    Port->ActiveKind = AhciCommandSense;
    Port->ActiveOperation = AhciOperationSense;
    AhciIssueCommand(Adapter, Port, AHCI_HEADER_ATAPI, 1);
}

static
VOID
AhciCompletePassThrough(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ BOOLEAN Failed,
    _In_ ULONG TaskFile)
{
    PAHCI_FIS_D2H D2h = (PAHCI_FIS_D2H)(Port->ReceivedFis + AHCI_RFIS_D2H_OFFSET);
    UCHAR Sense[8 + 14];

    if (!Failed && !Port->PassThroughCheckCondition)
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    RtlZeroMemory(Sense, sizeof(Sense));
    Sense[0] = 0x72;
    Sense[1] = Failed ? SCSI_SENSE_ABORTED_COMMAND : SCSI_SENSE_RECOVERED_ERROR;
    Sense[3] = Failed ? 0x00 : 0x1D;
    Sense[7] = 14;
    Sense[8] = 0x09;
    Sense[9] = 0x0C;
    Sense[10] = (Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH16) ? (Srb->Cdb[1] & 0x01) : 0;
    Sense[11] = (UCHAR)(TaskFile >> AHCI_PX_TFD_ERROR_SHIFT);
    Sense[12] = D2h->CountExp;
    Sense[13] = D2h->Count;
    Sense[14] = D2h->Lba3;
    Sense[15] = D2h->Lba0;
    Sense[16] = D2h->Lba4;
    Sense[17] = D2h->Lba1;
    Sense[18] = D2h->Lba5;
    Sense[19] = D2h->Lba2;
    Sense[20] = D2h->Device;
    Sense[21] = (UCHAR)TaskFile;

    AhciCompleteWithSenseData(Adapter, Srb, SRB_STATUS_ERROR, Sense, sizeof(Sense));
}

static
VOID
AhciCompleteAtaError(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR Error)
{
    if (Error & ATA_ERROR_ICRC)
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ABORTED_COMMAND, 0x47, 0x00);
    else if (Error & ATA_ERROR_UNC)
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_UNRECOVERED_ERROR, 0x00);
    else if (Error & ATA_ERROR_IDNF)
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_BLOCK, 0x00);
    else
        AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ABORTED_COMMAND, 0x00, 0x00);
}

static
VOID
AhciCommandDone(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port)
{
    PSCSI_REQUEST_BLOCK Srb = Port->ActiveSrb;
    ULONG Transferred = Port->CommandList[0].TransferredBytes;
    AHCI_OPERATION Operation = Port->ActiveOperation;

    ASSERT(Srb != NULL);
    Port->ActiveKind = AhciCommandIdle;
    Port->ActiveSrb = NULL;

    switch (Operation)
    {
        case AhciOperationData:
            AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        case AhciOperationWriteThenFlush:
            AhciIssueSrbFlush(Adapter, Port, Srb);
            break;

        case AhciOperationPassThrough:
            AhciCompletePassThrough(Adapter, Port, Srb, FALSE, AhciPortRead(Adapter, Port, AHCI_PX_TFD));
            break;

        case AhciOperationAtapi:
            if (Transferred != 0 && Transferred < Srb->DataTransferLength)
            {
                Srb->DataTransferLength = Transferred;
                AhciCompleteSrb(Adapter, Srb, SRB_STATUS_DATA_OVERRUN);
            }
            else
            {
                AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            }
            break;

        case AhciOperationSense:
            Transferred = Transferred ? min(Transferred, (ULONG)AHCI_SENSE_BUFFER_SIZE) : 18;
            Srb->DataTransferLength = 0;
            AhciCompleteWithSenseData(Adapter, Srb, SRB_STATUS_ERROR, Port->SenseBuffer, Transferred);
            break;
    }
}

static
VOID
AhciCommandFailed(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PAHCI_PORT Port,
    _In_ ULONG TaskFile)
{
    PSCSI_REQUEST_BLOCK Srb = Port->ActiveSrb;
    AHCI_OPERATION Operation = Port->ActiveOperation;
    UCHAR Error = (UCHAR)(TaskFile >> AHCI_PX_TFD_ERROR_SHIFT);
    BOOLEAN Recovered;

    ASSERT(Srb != NULL);
    Recovered = AhciRecoverPort(Adapter, Port, FALSE);

    if (Operation == AhciOperationAtapi && Recovered && AhciWantsSense(Srb))
    {
        Port->FailedError = Error;
        AhciAtapiRequestSense(Adapter, Port);
        return;
    }

    Port->ActiveKind = AhciCommandIdle;
    Port->ActiveSrb = NULL;
    if (!Recovered)
        AhciRecoverPort(Adapter, Port, TRUE);

    switch (Operation)
    {
        case AhciOperationAtapi:
            AhciCompleteWithSense(Adapter, Srb, Error >> 4, 0, 0);
            break;

        case AhciOperationSense:
            AhciCompleteWithSense(Adapter, Srb, Port->FailedError >> 4, 0, 0);
            break;

        case AhciOperationPassThrough:
            AhciCompletePassThrough(Adapter, Port, Srb, TRUE, TaskFile);
            break;

        default:
            AhciCompleteAtaError(Adapter, Srb, Error);
            break;
    }
}

/* Storport entry points */

static
BOOLEAN
NTAPI
AhciHwInterrupt(
    _In_ PVOID DeviceExtension)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;
    ULONG Pending = AhciRead(Adapter, AHCI_HBA_IS) & Adapter->PortsImplemented;
    ULONG i;

    if (Pending == 0)
        return FALSE;

    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        PAHCI_PORT Port = &Adapter->Ports[i];
        ULONG Status;

        if (!(Pending & (1u << i)))
            continue;

        Status = AhciPortRead(Adapter, Port, AHCI_PX_IS);
        AhciPortWrite(Adapter, Port, AHCI_PX_IS, Status);
        AhciWrite(Adapter, AHCI_HBA_IS, 1u << i);

        if (Status & AHCI_PX_IS_PRESENCE)
        {
            /* TODO: probe devices attached after start; only the change is reported */
            StorPortNotification(BusChangeDetected, Adapter, (UCHAR)i);
        }

        if (Port->ActiveKind == AhciCommandIdle)
            continue;

        if (Status & AHCI_PX_IS_ERROR)
            AhciCommandFailed(Adapter, Port, AhciPortRead(Adapter, Port, AHCI_PX_TFD));
        else if (!(AhciPortRead(Adapter, Port, AHCI_PX_CI) & 1))
            AhciCommandDone(Adapter, Port);
    }

    return TRUE;
}

static
BOOLEAN
NTAPI
AhciHwMSInterrupt(
    _In_ PVOID DeviceExtension,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
    return AhciHwInterrupt(DeviceExtension);
}

static
VOID
AhciExecuteScsi(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_PORT Port;

    if (Srb->PathId >= AHCI_MAX_PORTS || !(Adapter->PortsImplemented & (1u << Srb->PathId)))
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_PATH_ID);
        return;
    }

    Port = &Adapter->Ports[Srb->PathId];
    if (Srb->TargetId != 0 || Port->DeviceType == AhciDeviceNone)
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SELECTION_TIMEOUT);
        return;
    }
    if (Srb->Lun != 0)
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_LUN);
        return;
    }
    if (Port->ActiveKind != AhciCommandIdle)
    {
        AhciCompleteSrb(Adapter, Srb, SRB_STATUS_BUSY);
        return;
    }

    if (Port->DeviceType == AhciDeviceAtapi)
    {
        if (Srb->Cdb[0] == SCSIOP_ATA_PASSTHROUGH16)
            AhciAtaPassThrough(Adapter, Port, Srb);
        else
            AhciAtapiExecute(Adapter, Port, Srb);
        return;
    }

    switch (Srb->Cdb[0])
    {
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_START_STOP_UNIT:
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY16:
        case SCSIOP_SEEK:
            Srb->DataTransferLength = 0;
            AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        case SCSIOP_REQUEST_SENSE:
            AhciRequestSenseNoError(Adapter, Srb);
            break;

        case SCSIOP_INQUIRY:
            AhciAtaInquiry(Adapter, Port, Srb);
            break;

        case SCSIOP_READ_CAPACITY:
            AhciAtaReadCapacity(Adapter, Port, Srb, FALSE);
            break;

        case SCSIOP_READ_CAPACITY16:
            if ((Srb->Cdb[1] & 0x1F) == SERVICE_ACTION_READ_CAPACITY16)
                AhciAtaReadCapacity(Adapter, Port, Srb, TRUE);
            else
                AhciInvalidField(Adapter, Srb);
            break;

        case SCSIOP_MODE_SENSE:
            AhciAtaModeSense(Adapter, Port, Srb, FALSE);
            break;

        case SCSIOP_MODE_SENSE10:
            AhciAtaModeSense(Adapter, Port, Srb, TRUE);
            break;

        case SCSIOP_REPORT_LUNS:
            AhciReportLuns(Adapter, Srb);
            break;

        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
            AhciAtaReadWrite(Adapter, Port, Srb);
            break;

        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            AhciAtaFlush(Adapter, Port, Srb);
            break;

        case SCSIOP_ATA_PASSTHROUGH12:
        case SCSIOP_ATA_PASSTHROUGH16:
            AhciAtaPassThrough(Adapter, Port, Srb);
            break;

        default:
            AhciCompleteWithSense(Adapter, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
            break;
    }
}

static
BOOLEAN
NTAPI
AhciHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;
    PAHCI_PORT Port;

    switch (Srb->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            AhciExecuteScsi(Adapter, Srb);
            break;

        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
            if (Srb->PathId < AHCI_MAX_PORTS &&
                (Adapter->PortsImplemented & (1u << Srb->PathId)) &&
                Adapter->Ports[Srb->PathId].DeviceType == AhciDeviceAta &&
                Adapter->Ports[Srb->PathId].ActiveKind == AhciCommandIdle)
            {
                AhciAtaFlush(Adapter, &Adapter->Ports[Srb->PathId], Srb);
            }
            else
            {
                AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            }
            break;

        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            if (Srb->PathId < AHCI_MAX_PORTS && (Adapter->PortsImplemented & (1u << Srb->PathId)))
            {
                Port = &Adapter->Ports[Srb->PathId];
                if (Port->ActiveKind == AhciCommandIdle && Port->DeviceType != AhciDeviceNone)
                    AhciRecoverPort(Adapter, Port, TRUE);
            }
            AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
        case SRB_FUNCTION_ABORT_COMMAND:
            AhciCompleteSrb(Adapter, Srb, SRB_STATUS_SUCCESS);
            break;

        default:
            AhciCompleteSrb(Adapter, Srb, SRB_STATUS_INVALID_REQUEST);
            break;
    }

    return TRUE;
}

static
BOOLEAN
NTAPI
AhciHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;
    PAHCI_PORT Port;

    if (PathId >= AHCI_MAX_PORTS || !(Adapter->PortsImplemented & (1u << PathId)))
        return TRUE;

    Port = &Adapter->Ports[PathId];
    Port->ActiveKind = AhciCommandIdle;
    Port->ActiveSrb = NULL;

    if (Port->DeviceType != AhciDeviceNone && AhciStopCommandEngine(Adapter, Port, 500 * 1000))
    {
        AhciResetLink(Adapter, Port);
        AhciRecoverPort(Adapter, Port, TRUE);
    }

    StorPortCompleteRequest(Adapter, (UCHAR)PathId, SP_UNTAGGED, SP_UNTAGGED, SRB_STATUS_BUS_RESET);
    return TRUE;
}

static
VOID
AhciStopAdapter(
    _In_ PAHCI_ADAPTER Adapter)
{
    ULONG i;

    AhciWrite(Adapter, AHCI_HBA_GHC, AhciRead(Adapter, AHCI_HBA_GHC) & ~AHCI_GHC_IE);
    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        PAHCI_PORT Port = &Adapter->Ports[i];

        if (!(Adapter->PortsImplemented & (1u << i)))
            continue;

        AhciPortWrite(Adapter, Port, AHCI_PX_IE, 0);
        if (AhciStopCommandEngine(Adapter, Port, 500 * 1000))
            AhciStopFisReceive(Adapter, Port, 500 * 1000);
        Port->ActiveKind = AhciCommandIdle;
        Port->ActiveSrb = NULL;
    }
}

static
VOID
AhciRestartAdapter(
    _In_ PAHCI_ADAPTER Adapter)
{
    ULONG i;

    AhciWrite(Adapter, AHCI_HBA_GHC, AhciRead(Adapter, AHCI_HBA_GHC) | AHCI_GHC_AE);
    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        PAHCI_PORT Port = &Adapter->Ports[i];

        if (!(Adapter->PortsImplemented & (1u << i)) || Port->DeviceType == AhciDeviceNone)
            continue;

        AhciProgramPortMemory(Adapter, Port);
        AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);
        AhciPortWrite(Adapter, Port, AHCI_PX_IS, MAXULONG);
        AhciPortWrite(Adapter, Port, AHCI_PX_CMD,
                      AhciPortRead(Adapter, Port, AHCI_PX_CMD) | AHCI_PX_CMD_FRE |
                      ((Adapter->Capabilities & AHCI_CAP_SSS) ? AHCI_PX_CMD_SUD : 0) |
                      (Port->DeviceType == AhciDeviceAtapi ? AHCI_PX_CMD_ATAPI : 0));

        if (!AhciWaitPort(Adapter, Port, AHCI_PX_SSTS, AHCI_PX_SSTS_DET_MASK, AHCI_PX_SSTS_DET_PRESENT, 20 * 1000) ||
            !AhciWaitPort(Adapter, Port, AHCI_PX_TFD, AHCI_PX_TFD_BSY | AHCI_PX_TFD_DRQ, 0, 1000 * 1000))
        {
            Port->DeviceType = AhciDeviceNone;
            continue;
        }

        AhciPortWrite(Adapter, Port, AHCI_PX_SERR, MAXULONG);
        AhciStartCommandEngine(Adapter, Port);
        AhciPortWrite(Adapter, Port, AHCI_PX_IE, AHCI_PX_IE_DEFAULT);
    }

    AhciWrite(Adapter, AHCI_HBA_IS, MAXULONG);
    AhciWrite(Adapter, AHCI_HBA_GHC, AhciRead(Adapter, AHCI_HBA_GHC) | AHCI_GHC_IE);
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
AhciHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
        {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST List = Parameters;

            if (List->MaxControlType > ScsiQuerySupportedControlTypes)
                List->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            if (List->MaxControlType > ScsiStopAdapter)
                List->SupportedTypeList[ScsiStopAdapter] = TRUE;
            if (List->MaxControlType > ScsiRestartAdapter)
                List->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            return ScsiAdapterControlSuccess;
        }

        case ScsiStopAdapter:
            AhciStopAdapter(Adapter);
            return ScsiAdapterControlSuccess;

        case ScsiRestartAdapter:
            AhciRestartAdapter(Adapter);
            return ScsiAdapterControlSuccess;

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

static
BOOLEAN
NTAPI
AhciHwInitialize(
    _In_ PVOID DeviceExtension)
{
    if (!StorPortEnablePassiveInitialization(DeviceExtension, AhciPassiveInitialize))
        return AhciPassiveInitialize(DeviceExtension);
    return TRUE;
}

static
PACCESS_RANGE
AhciFindAbarRange(
    _In_ PAHCI_ADAPTER Adapter,
    _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    PACCESS_RANGE Ranges = *ConfigInfo->AccessRanges;
    PACCESS_RANGE Candidate = NULL;
    ULONG Bar5 = 0;
    ULONG i;

    if (ConfigInfo->AdapterInterfaceType == PCIBus)
    {
        UCHAR Config[AHCI_PCI_ABAR_OFFSET + sizeof(ULONG)];

        if (StorPortGetBusData(Adapter, PCIConfiguration, ConfigInfo->SystemIoBusNumber, ConfigInfo->SlotNumber,
                               Config, sizeof(Config)) == sizeof(Config))
        {
            RtlCopyMemory(&Bar5, &Config[AHCI_PCI_ABAR_OFFSET], sizeof(Bar5));
            Bar5 &= ~0xFu;
        }
    }

    for (i = 0; i < ConfigInfo->NumberOfAccessRanges; i++)
    {
        if (!Ranges[i].RangeInMemory || Ranges[i].RangeLength < AHCI_ABAR_MIN_LENGTH)
            continue;
        if (Bar5 != 0 && Ranges[i].RangeStart.QuadPart == (LONGLONG)Bar5)
            return &Ranges[i];
        Candidate = &Ranges[i];
    }

    return Candidate;
}

static
ULONG
NTAPI
AhciHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_z_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Reserved3)
{
    PAHCI_ADAPTER Adapter = DeviceExtension;
    PACCESS_RANGE Range;
    PUCHAR Base;
    ULONG Size, Count, Offset, Length, i;
    ULONG Waited;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    *Reserved3 = FALSE;

    Range = AhciFindAbarRange(Adapter, ConfigInfo);
    if (Range == NULL)
        return SP_RETURN_NOT_FOUND;

    Adapter->Abar = StorPortGetDeviceBase(Adapter, ConfigInfo->AdapterInterfaceType, ConfigInfo->SystemIoBusNumber,
                                          Range->RangeStart, Range->RangeLength, FALSE);
    if (Adapter->Abar == NULL)
        return SP_RETURN_ERROR;

    AhciWrite(Adapter, AHCI_HBA_GHC, AhciRead(Adapter, AHCI_HBA_GHC) | AHCI_GHC_AE);
    Adapter->Capabilities = AhciRead(Adapter, AHCI_HBA_CAP);
    Adapter->Capabilities2 = AhciRead(Adapter, AHCI_HBA_CAP2);

    if (Adapter->Capabilities2 & AHCI_CAP2_BOH)
    {
        AhciWrite(Adapter, AHCI_HBA_BOHC, AhciRead(Adapter, AHCI_HBA_BOHC) | AHCI_BOHC_OOS);
        for (Waited = 0; Waited < 2 * 1000 * 1000 && (AhciRead(Adapter, AHCI_HBA_BOHC) & (AHCI_BOHC_BOS | AHCI_BOHC_BB));
             Waited += 100)
        {
            StorPortStallExecution(100);
        }
    }

    Adapter->PortsImplemented = AhciRead(Adapter, AHCI_HBA_PI);
    if (Adapter->PortsImplemented == 0)
        Adapter->PortsImplemented = (ULONG)((2ull << (Adapter->Capabilities & AHCI_CAP_NP_MASK)) - 1);

    Adapter->BusCount = 0;
    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        if (!(Adapter->PortsImplemented & (1u << i)))
            continue;
        if (AHCI_PORT_REGISTERS_OFFSET + (i + 1) * AHCI_PORT_REGISTERS_SIZE > Range->RangeLength)
        {
            Adapter->PortsImplemented &= ~(1u << i);
            continue;
        }
        Adapter->BusCount = i + 1;
    }
    if (Adapter->BusCount == 0)
        return SP_RETURN_NOT_FOUND;

    Adapter->Dma64 = (Adapter->Capabilities & AHCI_CAP_S64A) &&
                     (ConfigInfo->Dma64BitAddresses & SCSI_DMA64_SYSTEM_SUPPORTED);
    ConfigInfo->Dma64BitAddresses = Adapter->Dma64 ? SCSI_DMA64_MINIPORT_SUPPORTED : 0;

    ConfigInfo->MaximumTransferLength = AHCI_MAX_TRANSFER_LENGTH;
    ConfigInfo->NumberOfPhysicalBreaks = AHCI_MAX_PRDT_ENTRIES - 1;
    ConfigInfo->AlignmentMask = 0x1;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = TRUE;
    ConfigInfo->NumberOfBuses = (UCHAR)Adapter->BusCount;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->ResetTargetSupported = TRUE;
    ConfigInfo->SynchronizationModel = StorSynchronizeHalfDuplex;
    ConfigInfo->HwMSInterruptRoutine = AhciHwMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;

    Count = 0;
    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        if (Adapter->PortsImplemented & (1u << i))
            Count++;
    }

    Size = Count * AHCI_PORT_DMA_SIZE;
    Base = StorPortGetUncachedExtension(Adapter, ConfigInfo, Size);
    if (Base == NULL)
        return SP_RETURN_ERROR;
    RtlZeroMemory(Base, Size);

    for (i = 0; i < AHCI_MAX_PORTS; i++)
    {
        PAHCI_PORT Port = &Adapter->Ports[i];
        PHYSICAL_ADDRESS Address;

        Port->Index = i;
        Port->Registers = Adapter->Abar + AHCI_PORT_REGISTERS_OFFSET + i * AHCI_PORT_REGISTERS_SIZE;
        if (!(Adapter->PortsImplemented & (1u << i)))
            continue;

        Address = StorPortGetPhysicalAddress(Adapter, NULL, Base, &Length);
        if (Length < AHCI_PORT_DMA_SIZE || (Address.QuadPart & (AHCI_COMMAND_LIST_SIZE - 1)) != 0 ||
            (!Adapter->Dma64 && Address.HighPart != 0))
        {
            return SP_RETURN_ERROR;
        }

        Offset = 0;
        Port->CommandList = (PAHCI_COMMAND_HEADER)(Base + Offset);
        Port->CommandListAddress.QuadPart = Address.QuadPart + Offset;
        Offset += AHCI_COMMAND_LIST_SIZE;
        Port->ReceivedFis = Base + Offset;
        Port->ReceivedFisAddress.QuadPart = Address.QuadPart + Offset;
        Offset += AHCI_RECEIVED_FIS_SIZE;
        Port->CommandTable = (PAHCI_COMMAND_TABLE)(Base + Offset);
        Port->CommandTableAddress.QuadPart = Address.QuadPart + Offset;
        Offset += sizeof(AHCI_COMMAND_TABLE);
        Port->SenseBuffer = Base + Offset;
        Port->SenseBufferAddress.QuadPart = Address.QuadPart + Offset;
        Offset += AHCI_SENSE_BUFFER_SIZE;
        Port->Identify = (PUSHORT)(Base + Offset);
        Port->IdentifyAddress.QuadPart = Address.QuadPart + Offset;
        Offset += AHCI_IDENTIFY_SIZE;

        ASSERT(Offset <= AHCI_PORT_DMA_SIZE);
        ASSERT((Port->ReceivedFisAddress.QuadPart & (AHCI_RECEIVED_FIS_SIZE - 1)) == 0);
        ASSERT((Port->CommandTableAddress.QuadPart & 0x7F) == 0);
        Base += AHCI_PORT_DMA_SIZE;
    }

    return SP_RETURN_FOUND;
}

ULONG
NTAPI
DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwInitialize = AhciHwInitialize;
    InitData.HwStartIo = AhciHwStartIo;
    InitData.HwInterrupt = AhciHwInterrupt;
    InitData.HwFindAdapter = AhciHwFindAdapter;
    InitData.HwResetBus = AhciHwResetBus;
    InitData.HwAdapterControl = AhciHwAdapterControl;
    InitData.DeviceExtensionSize = sizeof(AHCI_ADAPTER);
    InitData.NumberOfAccessRanges = 6;
    InitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = FALSE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = FALSE;

    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
