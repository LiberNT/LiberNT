/*
 * PROJECT:     LiberNT StorPort VirtIO block miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VirtIO 1.x PCI transport and split virtqueue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vioblk.h"

static
UCHAR
VblkCommonRead8(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    return StorPortReadRegisterUchar(Device, Device->CommonCfg + Offset);
}

static
USHORT
VblkCommonRead16(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    return StorPortReadRegisterUshort(Device, (PUSHORT)(Device->CommonCfg + Offset));
}

static
ULONG
VblkCommonRead32(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    return StorPortReadRegisterUlong(Device, (PULONG)(Device->CommonCfg + Offset));
}

static
VOID
VblkCommonWrite8(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ UCHAR Value)
{
    StorPortWriteRegisterUchar(Device, Device->CommonCfg + Offset, Value);
}

static
VOID
VblkCommonWrite16(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ USHORT Value)
{
    StorPortWriteRegisterUshort(Device, (PUSHORT)(Device->CommonCfg + Offset), Value);
}

static
VOID
VblkCommonWrite32(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONG Value)
{
    StorPortWriteRegisterUlong(Device, (PULONG)(Device->CommonCfg + Offset), Value);
}

static
VOID
VblkCommonWrite64(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset, _In_ ULONGLONG Value)
{
    VblkCommonWrite32(Device, Offset, (ULONG)Value);
    VblkCommonWrite32(Device, Offset + 4, (ULONG)(Value >> 32));
}

static
UCHAR
VblkConfigRead8(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    if (Offset + sizeof(UCHAR) > Device->DeviceCfgLength)
        return 0;
    return StorPortReadRegisterUchar(Device, Device->DeviceCfg + Offset);
}

static
USHORT
VblkConfigRead16(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    if (Offset + sizeof(USHORT) > Device->DeviceCfgLength)
        return 0;
    return StorPortReadRegisterUshort(Device, (PUSHORT)(Device->DeviceCfg + Offset));
}

static
ULONG
VblkConfigRead32(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONG Offset)
{
    if (Offset + sizeof(ULONG) > Device->DeviceCfgLength)
        return 0;
    return StorPortReadRegisterUlong(Device, (PULONG)(Device->DeviceCfg + Offset));
}

static
ULONG
VblkReadConfigDword(_In_reads_bytes_(256) PUCHAR Config, _In_ ULONG Offset)
{
    return (ULONG)Config[Offset] |
           ((ULONG)Config[Offset + 1] << 8) |
           ((ULONG)Config[Offset + 2] << 16) |
           ((ULONG)Config[Offset + 3] << 24);
}

static
PUCHAR
VblkMapCapability(_In_ PVBLK_DEVICE_EXTENSION Device,
                  _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                  _In_reads_bytes_(256) PUCHAR Config,
                  _In_ UCHAR Bar,
                  _In_ ULONG Offset,
                  _In_ ULONG Length,
                  _Inout_updates_(PCI_TYPE0_ADDRESSES) PUCHAR *Mappings)
{
    ULONG BarLow = VblkReadConfigDword(Config, FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses) + Bar * sizeof(ULONG));
    ULONGLONG Address;
    ULONG Index;

    if ((BarLow & PCI_ADDRESS_IO_SPACE) != 0)
        return NULL;

    Address = BarLow & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
    if ((BarLow & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
    {
        if (Bar + 1 >= PCI_TYPE0_ADDRESSES)
            return NULL;
        Address |= (ULONGLONG)VblkReadConfigDword(Config, FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses) +
                                                          (Bar + 1) * sizeof(ULONG)) << 32;
    }
    if (Address == 0)
        return NULL;

    for (Index = 0; Index < ConfigInfo->NumberOfAccessRanges && Index < PCI_TYPE0_ADDRESSES; Index++)
    {
        PACCESS_RANGE Range = &(*ConfigInfo->AccessRanges)[Index];
        ULONGLONG Start = (ULONGLONG)Range->RangeStart.QuadPart;
        ULONGLONG BarOffset;

        if (!Range->RangeInMemory || Range->RangeLength == 0)
            continue;
        if (Address < Start || Address - Start >= Range->RangeLength)
            continue;

        BarOffset = Address - Start;
        if ((ULONGLONG)Offset + Length > Range->RangeLength - BarOffset)
            return NULL;

        if (Mappings[Index] == NULL)
        {
            Mappings[Index] = (PUCHAR)StorPortGetDeviceBase(Device,
                                                            ConfigInfo->AdapterInterfaceType,
                                                            ConfigInfo->SystemIoBusNumber,
                                                            Range->RangeStart,
                                                            Range->RangeLength,
                                                            FALSE);
            if (Mappings[Index] == NULL)
                return NULL;
        }
        return Mappings[Index] + (ULONG_PTR)BarOffset + Offset;
    }

    return NULL;
}

BOOLEAN
VblkMapDevice(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    UCHAR Config[256];
    PUCHAR Mappings[PCI_TYPE0_ADDRESSES];
    USHORT VendorId;
    USHORT DeviceId;
    ULONG CapOffset;
    ULONG Guard;
    ULONG Found = 0;

    RtlZeroMemory(Mappings, sizeof(Mappings));
    if (StorPortGetBusData(Device, PCIConfiguration, Device->SystemIoBusNumber, Device->SlotNumber,
                           Config, sizeof(Config)) < sizeof(Config))
    {
        return FALSE;
    }

    VendorId = (USHORT)(Config[0] | (Config[1] << 8));
    DeviceId = (USHORT)(Config[2] | (Config[3] << 8));
    if (VendorId != VIRTIO_PCI_VENDOR_ID ||
        (DeviceId != VIRTIO_PCI_DEVICE_BLOCK_TRANSITIONAL && DeviceId != VIRTIO_PCI_DEVICE_BLOCK) ||
        (Config[FIELD_OFFSET(PCI_COMMON_HEADER, Status)] & PCI_STATUS_CAPABILITIES_LIST) == 0)
    {
        return FALSE;
    }

    CapOffset = Config[FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.CapabilitiesPtr)] & 0xFC;
    for (Guard = 0; CapOffset >= sizeof(PCI_COMMON_HEADER) && CapOffset < sizeof(Config) - 2 && Guard < 48; Guard++)
    {
        ULONG CapLength = Config[CapOffset + 2];

        if (Config[CapOffset] == PCI_CAPABILITY_ID_VENDOR_SPECIFIC &&
            CapLength >= VIRTIO_PCI_CAP_LENGTH + sizeof(ULONG) &&
            CapOffset + CapLength <= sizeof(Config))
        {
            UCHAR Type = Config[CapOffset + VIRTIO_PCI_CAP_CFG_TYPE];
            UCHAR Bar = Config[CapOffset + VIRTIO_PCI_CAP_BAR];
            ULONG Offset = VblkReadConfigDword(Config, CapOffset + VIRTIO_PCI_CAP_OFFSET);
            ULONG Length = VblkReadConfigDword(Config, CapOffset + VIRTIO_PCI_CAP_LENGTH);
            PUCHAR Base;

            if (Type >= VIRTIO_PCI_CAP_COMMON_CFG && Type <= VIRTIO_PCI_CAP_DEVICE_CFG &&
                (Found & (1UL << Type)) == 0 && Bar < PCI_TYPE0_ADDRESSES &&
                (Type != VIRTIO_PCI_CAP_NOTIFY_CFG || CapLength >= VIRTIO_PCI_NOTIFY_CAP_MULT + sizeof(ULONG)))
            {
                Base = VblkMapCapability(Device, ConfigInfo, Config, Bar, Offset, Length, Mappings);
                if (Base != NULL)
                {
                    switch (Type)
                    {
                        case VIRTIO_PCI_CAP_COMMON_CFG:
                            if (Length < VIRTIO_PCI_COMMON_CFG_MIN_LENGTH)
                                Base = NULL;
                            Device->CommonCfg = Base;
                            break;
                        case VIRTIO_PCI_CAP_NOTIFY_CFG:
                            Device->NotifyCfg = Base;
                            Device->NotifyLength = Length;
                            Device->NotifyMultiplier = VblkReadConfigDword(Config, CapOffset + VIRTIO_PCI_NOTIFY_CAP_MULT);
                            break;
                        case VIRTIO_PCI_CAP_ISR_CFG:
                            Device->IsrStatus = Base;
                            break;
                        case VIRTIO_PCI_CAP_DEVICE_CFG:
                            Device->DeviceCfg = Base;
                            Device->DeviceCfgLength = Length;
                            break;
                    }
                    if (Base != NULL)
                        Found |= 1UL << Type;
                }
            }
        }
        CapOffset = Config[CapOffset + 1] & 0xFC;
    }

    if (Device->CommonCfg == NULL || Device->NotifyCfg == NULL ||
        Device->IsrStatus == NULL || Device->DeviceCfg == NULL)
    {
        /* TODO: legacy (pre-1.0) virtio-pci I/O port transport */
        DPRINT1("vioblk: device %04x has no usable VirtIO 1.0 PCI capabilities\n", DeviceId);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
VblkResetDevice(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    ULONG Waited;

    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, 0);
    for (Waited = 0; Waited < VBLK_RESET_TIMEOUT_US; Waited += 10)
    {
        if (VblkCommonRead8(Device, VIRTIO_PCI_COMMON_STATUS) == 0)
            return TRUE;
        StorPortStallExecution(10);
    }

    DPRINT1("vioblk: device did not complete its reset\n");
    return FALSE;
}

static
VOID
VblkFailDevice(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    UCHAR Status = VblkCommonRead8(Device, VIRTIO_PCI_COMMON_STATUS);

    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, Status | VIRTIO_STATUS_FAILED);
}

static
ULONGLONG
VblkReadDeviceFeatures(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    ULONG Low;
    ULONG High;

    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_DFSELECT, 0);
    Low = VblkCommonRead32(Device, VIRTIO_PCI_COMMON_DF);
    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_DFSELECT, 1);
    High = VblkCommonRead32(Device, VIRTIO_PCI_COMMON_DF);
    return ((ULONGLONG)High << 32) | Low;
}

static
VOID
VblkWriteDriverFeatures(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ ULONGLONG Features)
{
    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_GFSELECT, 0);
    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_GF, (ULONG)Features);
    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_GFSELECT, 1);
    VblkCommonWrite32(Device, VIRTIO_PCI_COMMON_GF, (ULONG)(Features >> 32));
}

UCHAR
VblkReadConfigGeneration(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    return VblkCommonRead8(Device, VIRTIO_PCI_COMMON_CFGGENERATION);
}

ULONGLONG
VblkReadCapacity(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    ULONG Low;
    ULONG High;
    UCHAR Generation;
    ULONG Attempt;

    for (Attempt = 0; ; Attempt++)
    {
        Generation = VblkReadConfigGeneration(Device);
        Low = VblkConfigRead32(Device, VIRTIO_BLK_CFG_CAPACITY);
        High = VblkConfigRead32(Device, VIRTIO_BLK_CFG_CAPACITY + sizeof(ULONG));
        if (Generation == VblkReadConfigGeneration(Device) || Attempt >= 16)
            break;
    }

    return ((ULONGLONG)High << 32) | Low;
}

VOID
VblkWriteWriteback(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ BOOLEAN Enable)
{
    if (VIRTIO_BLK_CFG_WRITEBACK + sizeof(UCHAR) > Device->DeviceCfgLength)
        return;
    StorPortWriteRegisterUchar(Device, Device->DeviceCfg + VIRTIO_BLK_CFG_WRITEBACK, Enable ? 1 : 0);
}

static
ULONG
VblkRingLayout(_In_ USHORT QueueSize, _Out_ PULONG AvailOffset, _Out_ PULONG UsedOffset)
{
    *AvailOffset = QueueSize * sizeof(VIRTQ_DESC);
    *UsedOffset = (*AvailOffset + FIELD_OFFSET(VIRTQ_AVAIL, Ring) + QueueSize * sizeof(USHORT) + sizeof(USHORT) + 3) & ~3UL;
    return *UsedOffset + FIELD_OFFSET(VIRTQ_USED, Ring) + QueueSize * sizeof(VIRTQ_USED_ELEMENT) + sizeof(USHORT);
}

VOID
VblkSetRingMemory(_In_ PVBLK_DEVICE_EXTENSION Device, _In_ PUCHAR Virtual, _In_ ULONGLONG Physical)
{
    ULONG AvailOffset;
    ULONG UsedOffset;

    VblkRingLayout(Device->QueueSize, &AvailOffset, &UsedOffset);
    Device->Desc = (volatile VIRTQ_DESC *)Virtual;
    Device->Avail = (volatile VIRTQ_AVAIL *)(Virtual + AvailOffset);
    Device->Used = (volatile VIRTQ_USED *)(Virtual + UsedOffset);
    Device->DescPhysical = Physical;
    Device->AvailPhysical = Physical + AvailOffset;
    Device->UsedPhysical = Physical + UsedOffset;
}

BOOLEAN
VblkProbeDevice(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    const ULONGLONG Wanted = VIRTIO_FEATURE(VIRTIO_F_VERSION_1) |
                             VIRTIO_FEATURE(VIRTIO_F_ACCESS_PLATFORM) |
                             VIRTIO_FEATURE(VIRTIO_F_ORDER_PLATFORM) |
                             VIRTIO_FEATURE(VIRTIO_F_RING_INDIRECT_DESC) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_SIZE_MAX) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_SEG_MAX) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_RO) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_BLK_SIZE) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_FLUSH) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_TOPOLOGY) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_CONFIG_WCE) |
                             VIRTIO_FEATURE(VIRTIO_BLK_F_DISCARD);
    ULONGLONG Features;
    ULONG AvailOffset;
    ULONG UsedOffset;
    ULONG Value;
    ULONG Limit;
    ULONG PiecesPerPage;
    ULONG Pages;
    USHORT QueueMax;

    if (!VblkResetDevice(Device))
        return FALSE;
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    Device->DeviceFeatures = VblkReadDeviceFeatures(Device);
    if ((Device->DeviceFeatures & VIRTIO_FEATURE(VIRTIO_F_VERSION_1)) == 0)
    {
        /* TODO: legacy (pre-1.0) virtio-pci I/O port transport */
        DPRINT1("vioblk: device does not offer VirtIO 1.0\n");
        goto Fail;
    }

    Features = Device->DeviceFeatures & Wanted;
    Device->DriverFeatures = Features;
    Device->Indirect = (Features & VIRTIO_FEATURE(VIRTIO_F_RING_INDIRECT_DESC)) != 0;
    Device->ReadOnly = (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_RO)) != 0;
    Device->Flush = (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_FLUSH)) != 0;
    Device->ConfigWce = (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_CONFIG_WCE)) != 0;
    Device->Discard = (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_DISCARD)) != 0;

    Device->CapacitySectors = VblkReadCapacity(Device);

    Device->BlockSize = 1UL << VIRTIO_BLK_SECTOR_SHIFT;
    if (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_BLK_SIZE))
    {
        Value = VblkConfigRead32(Device, VIRTIO_BLK_CFG_BLK_SIZE);
        if (Value >= (1UL << VIRTIO_BLK_SECTOR_SHIFT) && Value <= 0x10000 && (Value & (Value - 1)) == 0)
            Device->BlockSize = Value;
    }
    for (Device->BlockShift = 0; (1UL << Device->BlockShift) < Device->BlockSize; Device->BlockShift++)
        NOTHING;

    if (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_TOPOLOGY))
    {
        Device->PhysicalBlockExponent = VblkConfigRead8(Device, VIRTIO_BLK_CFG_PHYSICAL_BLOCK_EXP);
        if (Device->PhysicalBlockExponent > 0xF)
            Device->PhysicalBlockExponent = 0;
        Device->LowestAlignedBlock = VblkConfigRead8(Device, VIRTIO_BLK_CFG_ALIGNMENT_OFFSET);
        Device->OptimalGranularityBlocks = VblkConfigRead16(Device, VIRTIO_BLK_CFG_MIN_IO_SIZE);
        Device->OptimalTransferBlocks = VblkConfigRead32(Device, VIRTIO_BLK_CFG_OPT_IO_SIZE);
    }

    if (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_SIZE_MAX))
        Device->SegmentSizeLimit = VblkConfigRead32(Device, VIRTIO_BLK_CFG_SIZE_MAX);

    if (Device->ConfigWce)
        Device->WriteCacheEnabled = VblkConfigRead8(Device, VIRTIO_BLK_CFG_WRITEBACK) != 0;
    else
        Device->WriteCacheEnabled = Device->Flush;
    Device->DefaultWriteCache = Device->WriteCacheEnabled;

    if (Device->Discard)
    {
        Device->MaxDiscardSectors = VblkConfigRead32(Device, VIRTIO_BLK_CFG_MAX_DISCARD_SECTORS);
        Device->MaxDiscardRanges = min(VblkConfigRead32(Device, VIRTIO_BLK_CFG_MAX_DISCARD_SEG), VBLK_MAX_DISCARD_RANGES);
        Device->DiscardAlignmentSectors = VblkConfigRead32(Device, VIRTIO_BLK_CFG_DISCARD_SECTOR_ALIGNMENT);
        if (Device->MaxDiscardSectors < (Device->BlockSize >> VIRTIO_BLK_SECTOR_SHIFT) || Device->MaxDiscardRanges == 0)
            Device->Discard = FALSE;
    }

    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_Q_SELECT, 0);
    QueueMax = VblkCommonRead16(Device, VIRTIO_PCI_COMMON_Q_SIZE);
    for (Device->QueueSize = VBLK_MAX_QUEUE_SIZE; Device->QueueSize > QueueMax; Device->QueueSize >>= 1)
        NOTHING;
    if (Device->QueueSize < (Device->Indirect ? 2 : 4))
    {
        DPRINT1("vioblk: request queue of %u entries is too small\n", QueueMax);
        goto Fail;
    }

    Limit = min(VBLK_MAX_DATA_DESCRIPTORS, (ULONG)Device->QueueSize - 2);
    if (!Device->Indirect)
        Limit = min(Limit, Device->QueueSize / min(VBLK_DIRECT_MIN_REQUESTS, Device->QueueSize / 3U) - 2);
    if (Features & VIRTIO_FEATURE(VIRTIO_BLK_F_SEG_MAX))
    {
        Value = VblkConfigRead32(Device, VIRTIO_BLK_CFG_SEG_MAX);
        if (Value != 0)
            Limit = min(Limit, Value);
    }
    PiecesPerPage = 1;
    if (Device->SegmentSizeLimit != 0 && Device->SegmentSizeLimit < PAGE_SIZE)
        PiecesPerPage = (PAGE_SIZE + Device->SegmentSizeLimit - 1) / Device->SegmentSizeLimit + 1;
    Pages = Limit / PiecesPerPage;
    Device->MaxDataDescriptors = Limit;
    Device->MaxRequests = Device->Indirect ? Device->QueueSize : Device->QueueSize / (Limit + 2);
    Device->MaxTransferLength = Pages > 1 ? ((Pages - 1) * PAGE_SIZE) & ~(Device->BlockSize - 1) : 0;
    if (Device->MaxTransferLength < Device->BlockSize)
    {
        DPRINT1("vioblk: %lu segments of %lu bytes cannot carry one %lu-byte block\n",
                Limit, Device->SegmentSizeLimit, Device->BlockSize);
        goto Fail;
    }

    Device->RingBytes = VblkRingLayout(Device->QueueSize, &AvailOffset, &UsedOffset);
    VblkUpdateGeometry(Device);

    return VblkResetDevice(Device);

Fail:
    VblkFailDevice(Device);
    return FALSE;
}

static
VOID
VblkInitializeRing(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    USHORT Index;

    RtlZeroMemory((PVOID)Device->Desc, Device->RingBytes);
    for (Index = 0; Index < Device->QueueSize; Index++)
        Device->Desc[Index].Next = (USHORT)(Index + 1);
    RtlZeroMemory(Device->Requests, sizeof(Device->Requests));
    Device->FreeHead = 0;
    Device->FreeCount = Device->QueueSize;
    Device->AvailIndex = 0;
    Device->LastUsedIndex = 0;
}

BOOLEAN
VblkStartDevice(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    UCHAR Status;
    ULONG NotifyOffset;

    Device->DeviceReady = FALSE;
    if (!VblkResetDevice(Device))
        return FALSE;

    Status = VIRTIO_STATUS_ACKNOWLEDGE;
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, Status);
    Status |= VIRTIO_STATUS_DRIVER;
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, Status);

    if ((Device->DriverFeatures & ~VblkReadDeviceFeatures(Device)) != 0)
    {
        DPRINT1("vioblk: device withdrew features it offered before\n");
        goto Fail;
    }
    VblkWriteDriverFeatures(Device, Device->DriverFeatures);
    Status |= VIRTIO_STATUS_FEATURES_OK;
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, Status);
    if ((VblkCommonRead8(Device, VIRTIO_PCI_COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0)
    {
        DPRINT1("vioblk: device rejected features %I64x\n", Device->DriverFeatures);
        goto Fail;
    }

    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_MSIX, Device->ConfigVector);
    if (VblkCommonRead16(Device, VIRTIO_PCI_COMMON_MSIX) != Device->ConfigVector)
    {
        DPRINT1("vioblk: device refused MSI-X vector %u for configuration changes\n", Device->ConfigVector);
        goto Fail;
    }

    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_Q_SELECT, 0);
    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_Q_SIZE, Device->QueueSize);
    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_Q_MSIX, Device->QueueVector);
    if (VblkCommonRead16(Device, VIRTIO_PCI_COMMON_Q_MSIX) != Device->QueueVector)
    {
        DPRINT1("vioblk: device refused MSI-X vector %u for the request queue\n", Device->QueueVector);
        goto Fail;
    }

    VblkInitializeRing(Device);
    VblkCommonWrite64(Device, VIRTIO_PCI_COMMON_Q_DESCLO, Device->DescPhysical);
    VblkCommonWrite64(Device, VIRTIO_PCI_COMMON_Q_AVAILLO, Device->AvailPhysical);
    VblkCommonWrite64(Device, VIRTIO_PCI_COMMON_Q_USEDLO, Device->UsedPhysical);

    NotifyOffset = (ULONG)VblkCommonRead16(Device, VIRTIO_PCI_COMMON_Q_NOFF) * Device->NotifyMultiplier;
    if (NotifyOffset > Device->NotifyLength || Device->NotifyLength - NotifyOffset < sizeof(USHORT))
    {
        DPRINT1("vioblk: queue notification offset %lu is outside the notification area\n", NotifyOffset);
        goto Fail;
    }
    Device->QueueNotify = (volatile USHORT *)(Device->NotifyCfg + NotifyOffset);

    VblkCommonWrite16(Device, VIRTIO_PCI_COMMON_Q_ENABLE, 1);
    Status |= VIRTIO_STATUS_DRIVER_OK;
    VblkCommonWrite8(Device, VIRTIO_PCI_COMMON_STATUS, Status);

    if (Device->ConfigWce)
        VblkWriteWriteback(Device, Device->WriteCacheEnabled);

    Device->ConfigGeneration = VblkReadConfigGeneration(Device);
    Device->CapacitySectors = VblkReadCapacity(Device);
    VblkUpdateGeometry(Device);
    Device->DeviceReady = TRUE;
    return TRUE;

Fail:
    VblkFailDevice(Device);
    return FALSE;
}

VOID
VblkStopDevice(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    Device->DeviceReady = FALSE;
    VblkResetDevice(Device);
}

UCHAR
VblkQueueRequest(_In_ PVBLK_DEVICE_EXTENSION Device,
                 _In_ PSCSI_REQUEST_BLOCK Srb,
                 _In_ ULONG Type,
                 _In_ ULONGLONG Sector,
                 _In_ ULONG DataDescriptors,
                 _In_ ULONG InternalDataLength,
                 _In_ BOOLEAN DeviceWritesInternalData)
{
    PVBLK_SRB_EXTENSION Extension = (PVBLK_SRB_EXTENSION)Srb->SrbExtension;
    PVBLK_REQUEST_DMA Dma = &Extension->Dma;
    STOR_PHYSICAL_ADDRESS Physical;
    ULONGLONG Base;
    ULONG Length;
    ULONG Count;
    ULONG Index;
    USHORT Head;
    USHORT Slot;

    if (!Device->DeviceReady)
        return SRB_STATUS_BUSY;

    Physical = StorPortGetPhysicalAddress(Device, NULL, Dma, &Length);
    if (Physical.QuadPart == 0 || Length < sizeof(*Dma))
        return SRB_STATUS_ERROR;
    Base = (ULONGLONG)Physical.QuadPart;

    if (InternalDataLength != 0)
    {
        Dma->Table[1].Address = Base + FIELD_OFFSET(VBLK_REQUEST_DMA, Data);
        Dma->Table[1].Length = InternalDataLength;
        Dma->Table[1].Flags = DeviceWritesInternalData ? VIRTQ_DESC_F_WRITE : 0;
        DataDescriptors = 1;
    }

    Count = DataDescriptors + 2;
    Dma->Header.Type = Type;
    Dma->Header.Reserved = 0;
    Dma->Header.Sector = Sector;
    Dma->Status = MAXUCHAR;

    Dma->Table[0].Address = Base + FIELD_OFFSET(VBLK_REQUEST_DMA, Header);
    Dma->Table[0].Length = sizeof(Dma->Header);
    Dma->Table[0].Flags = 0;
    Dma->Table[Count - 1].Address = Base + FIELD_OFFSET(VBLK_REQUEST_DMA, Status);
    Dma->Table[Count - 1].Length = sizeof(Dma->Status);
    Dma->Table[Count - 1].Flags = VIRTQ_DESC_F_WRITE;
    for (Index = 0; Index < Count; Index++)
    {
        if (Index + 1 < Count)
            Dma->Table[Index].Flags |= VIRTQ_DESC_F_NEXT;
        Dma->Table[Index].Next = (USHORT)(Index + 1 < Count ? Index + 1 : 0);
    }

    if (Device->Indirect)
    {
        if (Device->FreeCount == 0)
            return SRB_STATUS_BUSY;
        Head = Device->FreeHead;
        Device->FreeHead = Device->Desc[Head].Next;
        Device->FreeCount--;
        Device->Desc[Head].Address = Base + FIELD_OFFSET(VBLK_REQUEST_DMA, Table);
        Device->Desc[Head].Length = Count * sizeof(VIRTQ_DESC);
        Device->Desc[Head].Flags = VIRTQ_DESC_F_INDIRECT;
    }
    else
    {
        if (Device->FreeCount < Count)
            return SRB_STATUS_BUSY;
        Head = Device->FreeHead;
        Slot = Head;
        for (Index = 0; Index < Count; Index++)
        {
            Device->Desc[Slot].Address = Dma->Table[Index].Address;
            Device->Desc[Slot].Length = Dma->Table[Index].Length;
            Device->Desc[Slot].Flags = Dma->Table[Index].Flags;
            Slot = Device->Desc[Slot].Next;
        }
        Device->FreeHead = Slot;
        Device->FreeCount -= (USHORT)Count;
    }

    Device->Requests[Head] = Srb;
    Device->Avail->Ring[Device->AvailIndex & (Device->QueueSize - 1)] = Head;
    KeMemoryBarrier();
    Device->AvailIndex++;
    Device->Avail->Index = Device->AvailIndex;
    KeMemoryBarrier();
    if ((Device->Used->Flags & VIRTQ_USED_F_NO_NOTIFY) == 0)
        StorPortWriteRegisterUshort(Device, (PUSHORT)Device->QueueNotify, 0);

    return SRB_STATUS_PENDING;
}

PSCSI_REQUEST_BLOCK
VblkTakeCompletedRequest(_In_ PVBLK_DEVICE_EXTENSION Device)
{
    while (Device->LastUsedIndex != Device->Used->Index)
    {
        PSCSI_REQUEST_BLOCK Srb;
        ULONG Id;
        USHORT Slot;
        USHORT Count;

        KeMemoryBarrier();
        Id = Device->Used->Ring[Device->LastUsedIndex & (Device->QueueSize - 1)].Id;
        Device->LastUsedIndex++;

        if (Id >= Device->QueueSize || Device->Requests[Id] == NULL)
        {
            DPRINT1("vioblk: device returned unknown descriptor %lu\n", Id);
            continue;
        }

        Srb = Device->Requests[Id];
        Device->Requests[Id] = NULL;

        Slot = (USHORT)Id;
        for (Count = 1; (Device->Desc[Slot].Flags & VIRTQ_DESC_F_NEXT) && Count < Device->QueueSize; Count++)
            Slot = Device->Desc[Slot].Next;
        Device->Desc[Slot].Next = Device->FreeHead;
        Device->FreeHead = (USHORT)Id;
        Device->FreeCount += Count;
        return Srb;
    }

    return NULL;
}
