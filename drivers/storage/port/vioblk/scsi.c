/*
 * PROJECT:     LiberNT StorPort VirtIO block miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SCSI command set over VirtIO block requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vioblk.h"

static
VOID
VblkPut16(_Out_writes_bytes_(2) PUCHAR Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 8);
    Buffer[1] = (UCHAR)Value;
}

static
VOID
VblkPut32(_Out_writes_bytes_(4) PUCHAR Buffer, _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 24);
    Buffer[1] = (UCHAR)(Value >> 16);
    Buffer[2] = (UCHAR)(Value >> 8);
    Buffer[3] = (UCHAR)Value;
}

static
VOID
VblkPut64(_Out_writes_bytes_(8) PUCHAR Buffer, _In_ ULONGLONG Value)
{
    VblkPut32(Buffer, (ULONG)(Value >> 32));
    VblkPut32(Buffer + 4, (ULONG)Value);
}

static
ULONG
VblkGet16(_In_reads_bytes_(2) PUCHAR Buffer)
{
    return ((ULONG)Buffer[0] << 8) | Buffer[1];
}

static
ULONG
VblkGet32(_In_reads_bytes_(4) PUCHAR Buffer)
{
    return ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
}

static
ULONGLONG
VblkGet64(_In_reads_bytes_(8) PUCHAR Buffer)
{
    return ((ULONGLONG)VblkGet32(Buffer) << 32) | VblkGet32(Buffer + 4);
}

VOID
VblkUpdateGeometry(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    Device->Blocks = Device->CapacitySectors >> (Device->BlockShift - VIRTIO_BLK_SECTOR_SHIFT);
}

VOID
VblkCompleteSrb(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    if (SRB_STATUS(SrbStatus) == SRB_STATUS_SUCCESS)
        Srb->ScsiStatus = SCSISTAT_GOOD;
    StorPortNotification(RequestComplete, Device, Srb);
}

static
VOID
VblkBuildSense(_Out_writes_bytes_(18) PUCHAR Sense, _In_ UCHAR SenseKey, _In_ UCHAR Asc, _In_ UCHAR Ascq)
{
    RtlZeroMemory(Sense, 18);
    Sense[0] = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
    Sense[2] = SenseKey;
    Sense[7] = 18 - 8;
    Sense[12] = Asc;
    Sense[13] = Ascq;
}

static
VOID
VblkCompleteWithSense(_In_ PVBLK_DEVICE_EXTENSION Device,
                      _In_ PSCSI_REQUEST_BLOCK Srb,
                      _In_ UCHAR SenseKey,
                      _In_ UCHAR Asc,
                      _In_ UCHAR Ascq)
{
    UCHAR SrbStatus = SRB_STATUS_ERROR;
    UCHAR Sense[18];

    if (Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength != 0 &&
        (Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) == 0)
    {
        VblkBuildSense(Sense, SenseKey, Asc, Ascq);
        Srb->SenseInfoBufferLength = (UCHAR)min(sizeof(Sense), Srb->SenseInfoBufferLength);
        RtlCopyMemory(Srb->SenseInfoBuffer, Sense, Srb->SenseInfoBufferLength);
        SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    }
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    VblkCompleteSrb(Device, Srb, SrbStatus);
}

static
VOID
VblkCompleteIllegal(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR Asc)
{
    VblkCompleteWithSense(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, Asc, 0);
}

static
VOID
VblkReturnData(_In_ PVBLK_DEVICE_EXTENSION Device,
               _In_ PSCSI_REQUEST_BLOCK Srb,
               _In_reads_bytes_(Length) PVOID Data,
               _In_ ULONG Length)
{
    ULONG Copied = min(Length, Srb->DataTransferLength);

    if (Copied != 0)
    {
        if (Srb->DataBuffer == NULL)
        {
            VblkCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
            return;
        }
        RtlCopyMemory(Srb->DataBuffer, Data, Copied);
    }
    Srb->DataTransferLength = Copied;
    VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
VblkSubmitGetId(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    UCHAR Status;

    Extension->Kind = VblkRequestGetId;
    Extension->FlushAfterWrite = FALSE;
    RtlZeroMemory(Extension->Dma.Data.Id, sizeof(Extension->Dma.Data.Id));
    Status = VblkQueueRequest(Device, Srb, VIRTIO_BLK_T_GET_ID, 0, 0, VIRTIO_BLK_ID_BYTES, TRUE);
    if (Status != SRB_STATUS_PENDING)
        VblkCompleteSrb(Device, Srb, Status);
}

static
VOID
VblkStoreSerial(_In_ PVBLK_DEVICE_EXTENSION Device, _In_reads_bytes_(VIRTIO_BLK_ID_BYTES) PUCHAR Id, _In_ BOOLEAN Valid)
{
    ULONG Index;

    Device->SerialLength = 0;
    for (Index = 0; Valid && Index < VIRTIO_BLK_ID_BYTES && Id[Index] != 0; Index++)
    {
        if (Id[Index] >= 0x20 && Id[Index] < 0x7F)
            Device->Serial[Device->SerialLength++] = (CHAR)Id[Index];
    }
    while (Device->SerialLength != 0 && Device->Serial[Device->SerialLength - 1] == ' ')
        Device->SerialLength--;
    Device->SerialValid = TRUE;
}

static
VOID
VblkHandleInquiry(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR Cdb = Srb->Cdb;
    ULONG AllocationLength = VblkGet16(&Cdb[3]);
    UCHAR Buffer[64];
    ULONG Length;

    RtlZeroMemory(Buffer, sizeof(Buffer));

    if ((Cdb[1] & 0x01) == 0)
    {
        PINQUIRYDATA Inquiry = (PINQUIRYDATA)Buffer;

        if (Cdb[2] != 0)
        {
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
            return;
        }
        Inquiry->DeviceType = DIRECT_ACCESS_DEVICE;
        Inquiry->Versions = 5;
        Inquiry->ResponseDataFormat = 2;
        Inquiry->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
        Inquiry->CommandQueue = 1;
        RtlCopyMemory(Inquiry->VendorId, VBLK_VENDOR_ID, sizeof(Inquiry->VendorId));
        RtlCopyMemory(Inquiry->ProductId, VBLK_PRODUCT_ID, sizeof(Inquiry->ProductId));
        RtlCopyMemory(Inquiry->ProductRevisionLevel, VBLK_PRODUCT_REVISION, sizeof(Inquiry->ProductRevisionLevel));
        VblkReturnData(Device, Srb, Buffer, min(INQUIRYDATABUFFERSIZE, AllocationLength));
        return;
    }

    Buffer[1] = Cdb[2];
    switch (Cdb[2])
    {
        case VPD_SUPPORTED_PAGES:
            Length = 4;
            Buffer[Length++] = VPD_SUPPORTED_PAGES;
            Buffer[Length++] = VPD_SERIAL_NUMBER;
            Buffer[Length++] = VPD_DEVICE_IDENTIFIERS;
            Buffer[Length++] = VPD_BLOCK_LIMITS;
            if (Device->Discard)
                Buffer[Length++] = VPD_LOGICAL_BLOCK_PROVISIONING;
            Buffer[3] = (UCHAR)(Length - 4);
            break;

        case VPD_SERIAL_NUMBER:
            if (!Device->SerialValid)
            {
                VblkSubmitGetId(Device, Srb);
                return;
            }
            Buffer[3] = Device->SerialLength;
            RtlCopyMemory(&Buffer[4], Device->Serial, Device->SerialLength);
            Length = 4 + Device->SerialLength;
            break;

        case VPD_DEVICE_IDENTIFIERS:
        {
            PUCHAR Designator = &Buffer[4];
            ULONG IdentifierLength;

            if (!Device->SerialValid)
            {
                VblkSubmitGetId(Device, Srb);
                return;
            }
            Designator[0] = 0x02;
            Designator[1] = 0x01;
            RtlCopyMemory(&Designator[4], VBLK_VENDOR_ID, 8);
            if (Device->SerialLength != 0)
            {
                RtlCopyMemory(&Designator[12], Device->Serial, Device->SerialLength);
                IdentifierLength = 8 + Device->SerialLength;
            }
            else
            {
                RtlCopyMemory(&Designator[12], VBLK_PRODUCT_ID, 16);
                IdentifierLength = 8 + 16;
            }
            Designator[3] = (UCHAR)IdentifierLength;
            Length = 4 + 4 + IdentifierLength;
            VblkPut16(&Buffer[2], Length - 4);
            break;
        }

        case VPD_BLOCK_LIMITS:
        {
            ULONG MaximumBlocks = Device->MaxTransferLength >> Device->BlockShift;

            VblkPut16(&Buffer[2], 0x3C);
            VblkPut16(&Buffer[6], min(Device->OptimalGranularityBlocks, 0xFFFF));
            VblkPut32(&Buffer[8], MaximumBlocks);
            VblkPut32(&Buffer[12], min(Device->OptimalTransferBlocks, MaximumBlocks));
            if (Device->Discard)
            {
                VblkPut32(&Buffer[20], Device->MaxDiscardSectors >> (Device->BlockShift - VIRTIO_BLK_SECTOR_SHIFT));
                VblkPut32(&Buffer[24], Device->MaxDiscardRanges);
                VblkPut32(&Buffer[28], Device->DiscardAlignmentSectors >> (Device->BlockShift - VIRTIO_BLK_SECTOR_SHIFT));
            }
            Length = 4 + 0x3C;
            break;
        }

        case VPD_LOGICAL_BLOCK_PROVISIONING:
            if (!Device->Discard)
            {
                VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
                return;
            }
            Buffer[3] = 4;
            Buffer[5] = 0x80;
            Buffer[6] = 0x02;
            Length = 8;
            break;

        default:
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
            return;
    }

    VblkReturnData(Device, Srb, Buffer, min(Length, AllocationLength));
}

static
VOID
VblkHandleReadCapacity(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ BOOLEAN Extended)
{
    ULONGLONG LastBlock = Device->Blocks - 1;
    UCHAR Buffer[32];

    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (!Extended)
    {
        VblkPut32(&Buffer[0], LastBlock > MAXULONG ? MAXULONG : (ULONG)LastBlock);
        VblkPut32(&Buffer[4], Device->BlockSize);
        VblkReturnData(Device, Srb, Buffer, 8);
        return;
    }

    VblkPut64(&Buffer[0], LastBlock);
    VblkPut32(&Buffer[8], Device->BlockSize);
    Buffer[13] = Device->PhysicalBlockExponent;
    VblkPut16(&Buffer[14], Device->LowestAlignedBlock & 0x3FFF);
    if (Device->Discard)
        Buffer[14] |= 0x80;
    VblkReturnData(Device, Srb, Buffer, min(sizeof(Buffer), VblkGet32(&Srb->Cdb[10])));
}

static
VOID
VblkHandleModeSense(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR Cdb = Srb->Cdb;
    BOOLEAN Ten = Cdb[0] == SCSIOP_MODE_SENSE10;
    ULONG HeaderLength = Ten ? 8 : 4;
    ULONG AllocationLength = Ten ? VblkGet16(&Cdb[7]) : Cdb[4];
    UCHAR PageCode = Cdb[2] & 0x3F;
    UCHAR PageControl = Cdb[2] >> 6;
    BOOLEAN BlockDescriptor = (Cdb[1] & 0x08) == 0;
    UCHAR DeviceSpecific = Device->ReadOnly ? MODE_DSP_WRITE_PROTECT : 0;
    UCHAR Buffer[8 + 8 + 20];
    ULONG Length = HeaderLength;

    if (Cdb[3] != 0 || (PageCode != MODE_PAGE_CACHING && PageCode != MODE_SENSE_RETURN_ALL))
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
        return;
    }

    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (BlockDescriptor)
    {
        PUCHAR Descriptor = &Buffer[Length];

        Descriptor[1] = (UCHAR)((Device->Blocks > 0xFFFFFF ? 0xFFFFFF : Device->Blocks) >> 16);
        Descriptor[2] = (UCHAR)((Device->Blocks > 0xFFFFFF ? 0xFFFFFF : Device->Blocks) >> 8);
        Descriptor[3] = (UCHAR)(Device->Blocks > 0xFFFFFF ? 0xFFFFFF : Device->Blocks);
        Descriptor[5] = (UCHAR)(Device->BlockSize >> 16);
        Descriptor[6] = (UCHAR)(Device->BlockSize >> 8);
        Descriptor[7] = (UCHAR)Device->BlockSize;
        Length += 8;
    }

    Buffer[Length + 0] = MODE_PAGE_CACHING;
    Buffer[Length + 1] = 0x12;
    switch (PageControl)
    {
        case 1:
            if (Device->ConfigWce && Device->Flush)
                Buffer[Length + 2] = 0x04;
            break;
        case 2:
            if (Device->DefaultWriteCache)
                Buffer[Length + 2] = 0x04;
            break;
        default:
            if (Device->WriteCacheEnabled)
                Buffer[Length + 2] = 0x04;
            break;
    }
    Length += 0x14;

    if (Ten)
    {
        VblkPut16(&Buffer[0], Length - 2);
        Buffer[3] = DeviceSpecific;
        VblkPut16(&Buffer[6], BlockDescriptor ? 8 : 0);
    }
    else
    {
        Buffer[0] = (UCHAR)(Length - 1);
        Buffer[2] = DeviceSpecific;
        Buffer[3] = BlockDescriptor ? 8 : 0;
    }

    VblkReturnData(Device, Srb, Buffer, min(Length, AllocationLength));
}

static
VOID
VblkHandleModeSelect(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUCHAR Cdb = Srb->Cdb;
    BOOLEAN Ten = Cdb[0] == SCSIOP_MODE_SELECT10;
    ULONG HeaderLength = Ten ? 8 : 4;
    ULONG ParameterLength = Ten ? VblkGet16(&Cdb[7]) : Cdb[4];
    PUCHAR Data = (PUCHAR)Srb->DataBuffer;
    ULONG Offset;

    if (ParameterLength == 0)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }
    if (ParameterLength > Srb->DataTransferLength || Data == NULL || ParameterLength < HeaderLength)
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_PARAMETER_LIST_LENGTH);
        return;
    }

    Offset = HeaderLength + (Ten ? VblkGet16(&Data[6]) : Data[3]);
    while (Offset < ParameterLength)
    {
        UCHAR PageCode;
        ULONG PageLength;
        BOOLEAN WantWriteCache;

        if (ParameterLength - Offset < 2)
        {
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_PARAMETER_LIST_LENGTH);
            return;
        }
        PageCode = Data[Offset] & 0x3F;
        PageLength = Data[Offset + 1];
        if (PageCode != MODE_PAGE_CACHING || PageLength < 1 || ParameterLength - Offset - 2 < PageLength)
        {
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST);
            return;
        }

        WantWriteCache = (Data[Offset + 2] & 0x04) != 0;
        if (WantWriteCache != Device->WriteCacheEnabled)
        {
            if (!Device->ConfigWce || !Device->Flush)
            {
                VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST);
                return;
            }
            VblkWriteWriteback(Device, WantWriteCache);
            Device->WriteCacheEnabled = WantWriteCache;
        }
        Offset += 2 + PageLength;
    }

    VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
VblkHandleRequestSense(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Sense[18];

    if (Device->CapacityChanged)
    {
        Device->CapacityChanged = FALSE;
        VblkBuildSense(Sense, SCSI_SENSE_UNIT_ATTENTION, SCSI_ADSENSE_PARAMETERS_CHANGED, SCSI_SENSEQ_CAPACITY_DATA_CHANGED);
    }
    else
    {
        VblkBuildSense(Sense, SCSI_SENSE_NO_SENSE, 0, 0);
    }
    VblkReturnData(Device, Srb, Sense, min(sizeof(Sense), Srb->Cdb[4]));
}

static
VOID
VblkHandleReportLuns(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Buffer[16];

    RtlZeroMemory(Buffer, sizeof(Buffer));
    VblkPut32(&Buffer[0], 8);
    VblkReturnData(Device, Srb, Buffer, min(sizeof(Buffer), VblkGet32(&Srb->Cdb[6])));
}

static
VOID
VblkDecodeLba(_In_ PSCSI_REQUEST_BLOCK Srb, _Out_ PULONGLONG Lba, _Out_ PULONG Blocks)
{
    PUCHAR Cdb = Srb->Cdb;

    switch (Cdb[0])
    {
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
            *Lba = ((ULONG)(Cdb[1] & 0x1F) << 16) | ((ULONG)Cdb[2] << 8) | Cdb[3];
            *Blocks = Cdb[4] != 0 ? Cdb[4] : 256;
            break;
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_VERIFY12:
            *Lba = VblkGet32(&Cdb[2]);
            *Blocks = VblkGet32(&Cdb[6]);
            break;
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_VERIFY16:
            *Lba = VblkGet64(&Cdb[2]);
            *Blocks = VblkGet32(&Cdb[10]);
            break;
        default:
            *Lba = VblkGet32(&Cdb[2]);
            *Blocks = VblkGet16(&Cdb[7]);
            break;
    }
}

static
BOOLEAN
VblkRangeValid(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONGLONG Lba, _In_ ULONGLONG Blocks)
{
    return Lba <= Device->Blocks && Blocks <= Device->Blocks - Lba;
}

static
VOID
VblkSubmitReadWrite(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    PSTOR_SCATTER_GATHER_LIST Sgl;
    UCHAR Opcode = Srb->Cdb[0];
    BOOLEAN Write = Opcode == SCSIOP_WRITE6 || Opcode == SCSIOP_WRITE ||
                    Opcode == SCSIOP_WRITE12 || Opcode == SCSIOP_WRITE16;
    BOOLEAN ForceUnitAccess = Opcode != SCSIOP_READ6 && Opcode != SCSIOP_WRITE6 &&
                              (Srb->Cdb[1] & CDB_FORCE_MEDIA_ACCESS) != 0;
    ULONGLONG Lba;
    ULONG Blocks;
    ULONG Count = 0;
    ULONG Total = 0;
    ULONG Index;
    UCHAR Status;

    VblkDecodeLba(Srb, &Lba, &Blocks);
    if (!VblkRangeValid(Device, Lba, Blocks))
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_BLOCK);
        return;
    }
    if (Blocks == 0)
    {
        Srb->DataTransferLength = 0;
        VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }
    if (((ULONGLONG)Blocks << Device->BlockShift) != Srb->DataTransferLength ||
        Srb->DataTransferLength > Device->MaxTransferLength)
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
        return;
    }
    if (Write && Device->ReadOnly)
    {
        VblkCompleteWithSense(Device, Srb, SCSI_SENSE_DATA_PROTECT, SCSI_ADSENSE_WRITE_PROTECT, 0);
        return;
    }

    Sgl = StorPortGetScatterGatherList(Device, Srb);
    if (Sgl == NULL)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return;
    }

    for (Index = 0; Index < Sgl->NumberOfElements; Index++)
    {
        ULONGLONG Address = (ULONGLONG)Sgl->List[Index].PhysicalAddress.QuadPart;
        ULONG Remaining = Sgl->List[Index].Length;

        while (Remaining != 0)
        {
            ULONG Chunk = Remaining;

            if (Device->SegmentSizeLimit != 0 && Chunk > Device->SegmentSizeLimit)
                Chunk = Device->SegmentSizeLimit;
            if (Count >= Device->MaxDataDescriptors)
            {
                DPRINT1("vioblk: a %lu-byte transfer needs more than %lu segments\n",
                        Srb->DataTransferLength, Device->MaxDataDescriptors);
                VblkCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
                return;
            }
            Extension->Dma.Table[1 + Count].Address = Address;
            Extension->Dma.Table[1 + Count].Length = Chunk;
            Extension->Dma.Table[1 + Count].Flags = Write ? 0 : VIRTQ_DESC_F_WRITE;
            Count++;
            Address += Chunk;
            Remaining -= Chunk;
            Total += Chunk;
        }
    }
    if (Total != Srb->DataTransferLength)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return;
    }

    Extension->Kind = Write ? VblkRequestWrite : VblkRequestRead;
    Extension->FlushAfterWrite = Write && ForceUnitAccess && Device->Flush && Device->WriteCacheEnabled;
    Status = VblkQueueRequest(Device,
                              Srb,
                              Write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN,
                              Lba << (Device->BlockShift - VIRTIO_BLK_SECTOR_SHIFT),
                              Count,
                              0,
                              FALSE);
    if (Status != SRB_STATUS_PENDING)
        VblkCompleteSrb(Device, Srb, Status);
}

static
VOID
VblkSubmitFlush(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    UCHAR Status;

    if (!Device->Flush || !Device->WriteCacheEnabled || Extension == NULL)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    Extension->Kind = VblkRequestFlush;
    Extension->FlushAfterWrite = FALSE;
    Status = VblkQueueRequest(Device, Srb, VIRTIO_BLK_T_FLUSH, 0, 0, 0, FALSE);
    if (Status != SRB_STATUS_PENDING)
        VblkCompleteSrb(Device, Srb, Status);
}

static
VOID
VblkHandleUnmap(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    PUCHAR Data = (PUCHAR)Srb->DataBuffer;
    ULONG ParameterLength = VblkGet16(&Srb->Cdb[7]);
    ULONG SectorShift = Device->BlockShift - VIRTIO_BLK_SECTOR_SHIFT;
    ULONGLONG MaximumBlocks = Device->MaxDiscardSectors >> SectorShift;
    ULONGLONG TotalBlocks = 0;
    ULONG DescriptorLength;
    ULONG Offset;
    ULONG Count = 0;
    UCHAR Status;

    if (!Device->Discard)
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_COMMAND);
        return;
    }
    if ((Srb->Cdb[1] & 0x01) != 0)
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
        return;
    }
    if (Device->ReadOnly)
    {
        VblkCompleteWithSense(Device, Srb, SCSI_SENSE_DATA_PROTECT, SCSI_ADSENSE_WRITE_PROTECT, 0);
        return;
    }
    if (ParameterLength == 0)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }
    if (ParameterLength < 8 || ParameterLength > Srb->DataTransferLength || Data == NULL)
    {
        VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_PARAMETER_LIST_LENGTH);
        return;
    }

    DescriptorLength = min(VblkGet16(&Data[2]), ParameterLength - 8) & ~15UL;
    for (Offset = 8; Offset < 8 + DescriptorLength; Offset += 16)
    {
        ULONGLONG Lba = VblkGet64(&Data[Offset]);
        ULONG Blocks = VblkGet32(&Data[Offset + 8]);

        if (Blocks == 0)
            continue;
        if (!VblkRangeValid(Device, Lba, Blocks))
        {
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_BLOCK);
            return;
        }
        TotalBlocks += Blocks;
        if (Count >= Device->MaxDiscardRanges || TotalBlocks > MaximumBlocks)
        {
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST);
            return;
        }
        Extension->Dma.Data.Ranges[Count].Sector = Lba << SectorShift;
        Extension->Dma.Data.Ranges[Count].NumberOfSectors = Blocks << SectorShift;
        Extension->Dma.Data.Ranges[Count].Flags = 0;
        Count++;
    }

    if (Count == 0)
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    Extension->Kind = VblkRequestDiscard;
    Extension->FlushAfterWrite = FALSE;
    Status = VblkQueueRequest(Device, Srb, VIRTIO_BLK_T_DISCARD, 0, 0,
                              Count * sizeof(VIRTIO_BLK_DISCARD_RANGE), FALSE);
    if (Status != SRB_STATUS_PENDING)
        VblkCompleteSrb(Device, Srb, Status);
}

VOID
VblkCompleteRequest(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    UCHAR DeviceStatus = Extension->Dma.Status;
    UCHAR Status;

    if (Extension->Kind == VblkRequestGetId)
    {
        VblkStoreSerial(Device, Extension->Dma.Data.Id, DeviceStatus == VIRTIO_BLK_S_OK);
        VblkHandleInquiry(Device, Srb);
        return;
    }

    switch (DeviceStatus)
    {
        case VIRTIO_BLK_S_OK:
            if (Extension->FlushAfterWrite)
            {
                Extension->FlushAfterWrite = FALSE;
                Extension->Kind = VblkRequestFlush;
                Status = VblkQueueRequest(Device, Srb, VIRTIO_BLK_T_FLUSH, 0, 0, 0, FALSE);
                if (Status != SRB_STATUS_PENDING)
                    VblkCompleteSrb(Device, Srb, Status);
                return;
            }
            VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            break;

        case VIRTIO_BLK_S_UNSUPP:
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_COMMAND);
            break;

        default:
            VblkCompleteWithSense(Device,
                                  Srb,
                                  SCSI_SENSE_MEDIUM_ERROR,
                                  Extension->Kind == VblkRequestRead ? SCSI_ADSENSE_UNRECOVERED_ERROR : SCSI_ADSENSE_WRITE_ERROR,
                                  0);
            break;
    }
}

static
BOOLEAN
VblkNeedsMedium(_In_ UCHAR Opcode)
{
    switch (Opcode)
    {
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_SERVICE_ACTION_IN16:
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY12:
        case SCSIOP_VERIFY16:
        case SCSIOP_UNMAP:
            return TRUE;
        default:
            return FALSE;
    }
}

static
VOID
VblkExecuteScsi(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Opcode = Srb->Cdb[0];
    ULONGLONG Lba;
    ULONG Blocks;

    if (Srb->PathId != 0 || Srb->TargetId != 0 || (Srb->Lun != 0 && Opcode != SCSIOP_REPORT_LUNS))
    {
        VblkCompleteSrb(Device, Srb, SRB_STATUS_NO_DEVICE);
        return;
    }

    switch (Opcode)
    {
        case SCSIOP_INQUIRY:
            VblkHandleInquiry(Device, Srb);
            return;
        case SCSIOP_REPORT_LUNS:
            VblkHandleReportLuns(Device, Srb);
            return;
        case SCSIOP_REQUEST_SENSE:
            VblkHandleRequestSense(Device, Srb);
            return;
    }

    if (Device->CapacityChanged)
    {
        Device->CapacityChanged = FALSE;
        VblkCompleteWithSense(Device, Srb, SCSI_SENSE_UNIT_ATTENTION,
                              SCSI_ADSENSE_PARAMETERS_CHANGED, SCSI_SENSEQ_CAPACITY_DATA_CHANGED);
        return;
    }

    if (Device->Blocks == 0 && VblkNeedsMedium(Opcode))
    {
        VblkCompleteWithSense(Device, Srb, SCSI_SENSE_NOT_READY, SCSI_ADSENSE_NO_MEDIA_IN_DEVICE, 0);
        return;
    }

    switch (Opcode)
    {
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_START_STOP_UNIT:
        case SCSIOP_MEDIUM_REMOVAL:
        case SCSIOP_RESERVE_UNIT:
        case SCSIOP_RELEASE_UNIT:
            VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            break;

        case SCSIOP_READ_CAPACITY:
            VblkHandleReadCapacity(Device, Srb, FALSE);
            break;

        case SCSIOP_SERVICE_ACTION_IN16:
            if ((Srb->Cdb[1] & 0x1F) == SERVICE_ACTION_READ_CAPACITY16)
                VblkHandleReadCapacity(Device, Srb, TRUE);
            else
                VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_INVALID_CDB);
            break;

        case SCSIOP_MODE_SENSE:
        case SCSIOP_MODE_SENSE10:
            VblkHandleModeSense(Device, Srb);
            break;

        case SCSIOP_MODE_SELECT:
        case SCSIOP_MODE_SELECT10:
            VblkHandleModeSelect(Device, Srb);
            break;

        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
            VblkSubmitReadWrite(Device, Srb);
            break;

        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY12:
        case SCSIOP_VERIFY16:
            VblkDecodeLba(Srb, &Lba, &Blocks);
            if (VblkRangeValid(Device, Lba, Blocks))
                VblkCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            else
                VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_BLOCK);
            break;

        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            VblkSubmitFlush(Device, Srb);
            break;

        case SCSIOP_UNMAP:
            VblkHandleUnmap(Device, Srb);
            break;

        default:
            VblkCompleteIllegal(Device, Srb, SCSI_ADSENSE_ILLEGAL_COMMAND);
            break;
    }
}

BOOLEAN
NTAPI
VblkHwStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVBLK_DEVICE_EXTENSION Device = (PVBLK_DEVICE_EXTENSION)DeviceExtension;
    STOR_LOCK_HANDLE Lock;

    StorPortAcquireSpinLock(Device, InterruptLock, NULL, &Lock);
    switch (Srb->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            if (Srb->SrbExtension == NULL)
                VblkCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
            else
                VblkExecuteScsi(Device, Srb);
            break;

        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
            VblkSubmitFlush(Device, Srb);
            break;

        default:
            VblkCompleteSrb(Device, Srb, SRB_STATUS_INVALID_REQUEST);
            break;
    }
    StorPortReleaseSpinLock(Device, &Lock);
    return TRUE;
}
