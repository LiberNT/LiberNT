/*
 * PROJECT:     LiberNT StarFive JH7110 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StarFive JH7110 GMAC (Synopsys DWC Ethernet QoS 5.x) NDIS 6.30 miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "jh7110gmac.h"

static NDIS_HANDLE JhGmacDriverHandle;

static const NDIS_OID JhGmacSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_LINK_STATE,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE
};

static const struct
{
    UCHAR Ldo;
    UCHAR Strength;
    USHORT Microamp;
} JhGmacYt8531DriveStrength[] =
{
    { YTPHY_LDO_1V8, 0, 1200 }, { YTPHY_LDO_1V8, 1, 2100 }, { YTPHY_LDO_1V8, 2, 2700 },
    { YTPHY_LDO_1V8, 3, 2910 }, { YTPHY_LDO_1V8, 4, 3110 }, { YTPHY_LDO_1V8, 5, 3600 },
    { YTPHY_LDO_1V8, 6, 3970 }, { YTPHY_LDO_1V8, 7, 4350 },
    { YTPHY_LDO_3V3, 0, 3070 }, { YTPHY_LDO_3V3, 1, 4080 }, { YTPHY_LDO_3V3, 2, 4370 },
    { YTPHY_LDO_3V3, 3, 4680 }, { YTPHY_LDO_3V3, 4, 5020 }, { YTPHY_LDO_3V3, 5, 5450 },
    { YTPHY_LDO_3V3, 6, 5740 }, { YTPHY_LDO_3V3, 7, 6140 },
};

static __inline ULONG
JhGmacRead32(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Offset));
}

static __inline VOID
JhGmacWrite32(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Offset), Value);
}

static BOOLEAN
JhGmacIsValidMacAddress(
    _In_reads_(ETH_LENGTH_OF_ADDRESS) const UCHAR *MacAddress)
{
    ULONG i;
    BOOLEAN AllZero = TRUE;
    BOOLEAN AllOnes = TRUE;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
    {
        if (MacAddress[i] != 0x00)
            AllZero = FALSE;
        if (MacAddress[i] != 0xff)
            AllOnes = FALSE;
    }

    return !AllZero && !AllOnes && !(MacAddress[0] & 0x01);
}

static PMDL
JhGmacAllocateMdl(
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PMDL Mdl;

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl)
        MmBuildMdlForNonPagedPool(Mdl);

    return Mdl;
}

static __inline ULONG
JhGmacDescriptorAddress(
    _In_ NDIS_PHYSICAL_ADDRESS RingPhysical,
    _In_ ULONG Index)
{
    return RingPhysical.LowPart + Index * sizeof(JHGMAC_DESCRIPTOR);
}

static VOID
JhGmacRearmRxDescriptor(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PJHGMAC_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];

    Descriptor->Des0 = Adapter->RxBuffers[Index].PhysicalAddress.LowPart;
    Descriptor->Des1 = 0;
    Descriptor->Des2 = 0;
    KeMemoryBarrier();
    Descriptor->Des3 = DES3_OWN | RDES3_INTERRUPT_ON_COMPLETION | RDES3_BUFFER1_VALID;
    KeMemoryBarrier();
}

static VOID
JhGmacKickRx(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    JhGmacWrite32(Adapter, DMA_CH0_RX_TAIL,
                  JhGmacDescriptorAddress(Adapter->RxRingPhysical, JHGMAC_RX_RING_SIZE));
}

static VOID
JhGmacApplyPacketFilter(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG Filter = 0;

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
        Filter |= GMAC_PACKET_FILTER_PR;
    if (Adapter->PacketFilter & (NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_MULTICAST))
        Filter |= GMAC_PACKET_FILTER_PM;

    JhGmacWrite32(Adapter, GMAC_PACKET_FILTER, Filter);
}

static VOID
JhGmacWriteMacAddress(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    const UCHAR *Mac = Adapter->CurrentMacAddress;

    JhGmacWrite32(Adapter, GMAC_ADDR_HIGH0, GMAC_ADDR_ENABLE | ((ULONG)Mac[5] << 8) | Mac[4]);
    JhGmacWrite32(Adapter, GMAC_ADDR_LOW0,
                  ((ULONG)Mac[3] << 24) | ((ULONG)Mac[2] << 16) | ((ULONG)Mac[1] << 8) | Mac[0]);
}

static VOID
JhGmacEnableInterrupts(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    JhGmacWrite32(Adapter, DMA_CH0_INTERRUPT_ENABLE, JHGMAC_INT_MASK);
}

static VOID
JhGmacDisableInterrupts(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    JhGmacWrite32(Adapter, DMA_CH0_INTERRUPT_ENABLE, 0);
}

static VOID
JhGmacStopHardware(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    if (!Adapter->RegisterBase)
        return;

    JhGmacWrite32(Adapter, DMA_CH0_INTERRUPT_ENABLE, 0);
    JhGmacWrite32(Adapter, GMAC_INT_EN, 0);
    JhGmacWrite32(Adapter, DMA_CH0_TX_CONTROL,
                  JhGmacRead32(Adapter, DMA_CH0_TX_CONTROL) & ~DMA_CHANNEL_START);
    JhGmacWrite32(Adapter, GMAC_CONFIG,
                  JhGmacRead32(Adapter, GMAC_CONFIG) & ~(GMAC_CONFIG_TE | GMAC_CONFIG_RE));
    JhGmacWrite32(Adapter, DMA_CH0_RX_CONTROL,
                  JhGmacRead32(Adapter, DMA_CH0_RX_CONTROL) & ~DMA_CHANNEL_START);
    JhGmacWrite32(Adapter, DMA_CH0_STATUS, DMA_CH_STATUS_ALL);
}

static VOID
JhGmacUnmapResources(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    if (Adapter->RegisterBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->RegisterBase,
                          Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
    }
}

static VOID
JhGmacFreeDatapath(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG i;

    Adapter->DatapathReady = FALSE;

    for (i = 0; i < JHGMAC_TX_RING_SIZE; i++)
    {
        if (Adapter->TxBuffers[i].VirtualAddress)
        {
            MmFreeContiguousMemorySpecifyCache(Adapter->TxBuffers[i].VirtualAddress,
                                               JHGMAC_BUFFER_SIZE,
                                               MmCached);
            Adapter->TxBuffers[i].VirtualAddress = NULL;
        }
    }

    for (i = 0; i < JHGMAC_RX_RING_SIZE; i++)
    {
        if (Adapter->RxBuffers[i].NetBufferList)
        {
            NdisFreeNetBufferList(Adapter->RxBuffers[i].NetBufferList);
            Adapter->RxBuffers[i].NetBufferList = NULL;
            Adapter->RxBuffers[i].Indicated = FALSE;
        }
        if (Adapter->RxBuffers[i].Mdl)
        {
            IoFreeMdl(Adapter->RxBuffers[i].Mdl);
            Adapter->RxBuffers[i].Mdl = NULL;
        }
        if (Adapter->RxBuffers[i].VirtualAddress)
        {
            MmFreeContiguousMemorySpecifyCache(Adapter->RxBuffers[i].VirtualAddress,
                                               JHGMAC_BUFFER_SIZE,
                                               MmCached);
            Adapter->RxBuffers[i].VirtualAddress = NULL;
        }
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    if (Adapter->RxRing)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->RxRing, Adapter->RxRingLength, MmCached);
        Adapter->RxRing = NULL;
    }

    if (Adapter->TxRing)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->TxRing, Adapter->TxRingLength, MmCached);
        Adapter->TxRing = NULL;
    }
}

static PVOID
JhGmacAllocateDmaMemory(
    _In_ ULONG Length,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Va;

    Low.QuadPart = 0;
    High.QuadPart = JHGMAC_DMA_LIMIT;
    Boundary.QuadPart = 0;
    Physical->QuadPart = 0;

    Va = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary, MmCached);
    if (!Va)
        return NULL;

    *Physical = MmGetPhysicalAddress(Va);
    if ((ULONG64)Physical->QuadPart + Length - 1 > JHGMAC_DMA_LIMIT)
    {
        MmFreeContiguousMemorySpecifyCache(Va, Length, MmCached);
        Physical->QuadPart = 0;
        return NULL;
    }

    return Va;
}

static NDIS_STATUS
JhGmacAllocateReceivePath(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = JHGMAC_TAG;
    PoolParams.DataSize = 0;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle, &PoolParams);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxRingLength = sizeof(JHGMAC_DESCRIPTOR) * JHGMAC_RX_RING_SIZE;
    Adapter->RxRing = JhGmacAllocateDmaMemory(Adapter->RxRingLength, &Adapter->RxRingPhysical);
    if (!Adapter->RxRing)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->RxRing, Adapter->RxRingLength);
    for (i = 0; i < JHGMAC_RX_RING_SIZE; i++)
    {
        Adapter->RxBuffers[i].VirtualAddress =
            JhGmacAllocateDmaMemory(JHGMAC_BUFFER_SIZE, &Adapter->RxBuffers[i].PhysicalAddress);
        if (!Adapter->RxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].Mdl =
            JhGmacAllocateMdl(Adapter->RxBuffers[i].VirtualAddress, JHGMAC_BUFFER_SIZE);
        if (!Adapter->RxBuffers[i].Mdl)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].NetBufferList =
            NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0,
                                                  Adapter->RxBuffers[i].Mdl, 0, 0);
        if (!Adapter->RxBuffers[i].NetBufferList)
            return NDIS_STATUS_RESOURCES;
        Adapter->RxBuffers[i].NetBufferList->SourceHandle = Adapter->MiniportHandle;

        JhGmacRearmRxDescriptor(Adapter, i);
    }

    Adapter->RxTail = 0;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
JhGmacAllocateTransmitPath(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG i;

    Adapter->TxRingLength = sizeof(JHGMAC_DESCRIPTOR) * JHGMAC_TX_RING_SIZE;
    Adapter->TxRing = JhGmacAllocateDmaMemory(Adapter->TxRingLength, &Adapter->TxRingPhysical);
    if (!Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->TxRing, Adapter->TxRingLength);
    for (i = 0; i < JHGMAC_TX_RING_SIZE; i++)
    {
        Adapter->TxBuffers[i].VirtualAddress =
            JhGmacAllocateDmaMemory(JHGMAC_BUFFER_SIZE, &Adapter->TxBuffers[i].PhysicalAddress);
        if (!Adapter->TxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;
    }
    KeMemoryBarrier();

    Adapter->TxHead = 0;
    Adapter->TxTail = 0;
    Adapter->TxFree = JHGMAC_TX_RING_SIZE - 1;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
JhGmacInitializeDatapath(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    ULONG Value;

    Status = JhGmacAllocateReceivePath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacAllocateTransmitPath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    JhGmacWrite32(Adapter, DMA_CH0_TX_BASE_HIGH, 0);
    JhGmacWrite32(Adapter, DMA_CH0_TX_BASE, Adapter->TxRingPhysical.LowPart);
    JhGmacWrite32(Adapter, DMA_CH0_TX_RING_LENGTH, JHGMAC_TX_RING_SIZE - 1);
    JhGmacWrite32(Adapter, DMA_CH0_TX_TAIL, Adapter->TxRingPhysical.LowPart);
    JhGmacWrite32(Adapter, DMA_CH0_RX_BASE_HIGH, 0);
    JhGmacWrite32(Adapter, DMA_CH0_RX_BASE, Adapter->RxRingPhysical.LowPart);
    JhGmacWrite32(Adapter, DMA_CH0_RX_RING_LENGTH, JHGMAC_RX_RING_SIZE - 1);

    Value = JhGmacRead32(Adapter, DMA_CH0_TX_CONTROL);
    JhGmacWrite32(Adapter, DMA_CH0_TX_CONTROL, Value | DMA_CHANNEL_START);
    Value = JhGmacRead32(Adapter, DMA_CH0_RX_CONTROL);
    JhGmacWrite32(Adapter, DMA_CH0_RX_CONTROL, Value | DMA_CHANNEL_START);
    JhGmacKickRx(Adapter);

    Value = JhGmacRead32(Adapter, GMAC_CONFIG);
    JhGmacWrite32(Adapter, GMAC_CONFIG, Value | GMAC_CONFIG_TE | GMAC_CONFIG_RE);

    Adapter->DatapathReady = TRUE;
    return NDIS_STATUS_SUCCESS;

Failure:
    JhGmacFreeDatapath(Adapter);
    return Status;
}

static BOOLEAN
JhGmacWaitMdioIdle(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < 1000; i++)
    {
        if (!(JhGmacRead32(Adapter, GMAC_MDIO_ADDR) & GMAC_MDIO_BUSY))
            return TRUE;

        KeStallExecutionProcessor(10);
    }

    return FALSE;
}

static NDIS_STATUS
JhGmacMdioRead(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value)
{
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    *Value = 0xffff;
    NdisAcquireSpinLock(&Adapter->MdioLock);
    if (JhGmacWaitMdioIdle(Adapter))
    {
        JhGmacWrite32(Adapter, GMAC_MDIO_ADDR,
                      ((ULONG)(PhyAddress & 0x1f) << GMAC_MDIO_PHY_SHIFT) |
                      ((ULONG)(Register & 0x1f) << GMAC_MDIO_REGISTER_SHIFT) |
                      GMAC_MDIO_CSR_CLOCK(GMAC_MDIO_CSR_CLOCK_250_300M) |
                      GMAC_MDIO_READ | GMAC_MDIO_BUSY);
        if (JhGmacWaitMdioIdle(Adapter))
        {
            *Value = (USHORT)JhGmacRead32(Adapter, GMAC_MDIO_DATA);
            Status = NDIS_STATUS_SUCCESS;
        }
    }
    NdisReleaseSpinLock(&Adapter->MdioLock);
    return Status;
}

static NDIS_STATUS
JhGmacMdioWrite(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _In_ UCHAR Register,
    _In_ USHORT Value)
{
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    NdisAcquireSpinLock(&Adapter->MdioLock);
    if (JhGmacWaitMdioIdle(Adapter))
    {
        JhGmacWrite32(Adapter, GMAC_MDIO_DATA, Value);
        JhGmacWrite32(Adapter, GMAC_MDIO_ADDR,
                      ((ULONG)(PhyAddress & 0x1f) << GMAC_MDIO_PHY_SHIFT) |
                      ((ULONG)(Register & 0x1f) << GMAC_MDIO_REGISTER_SHIFT) |
                      GMAC_MDIO_CSR_CLOCK(GMAC_MDIO_CSR_CLOCK_250_300M) |
                      GMAC_MDIO_WRITE | GMAC_MDIO_BUSY);
        if (JhGmacWaitMdioIdle(Adapter))
            Status = NDIS_STATUS_SUCCESS;
    }
    NdisReleaseSpinLock(&Adapter->MdioLock);
    return Status;
}

static NDIS_STATUS
JhGmacYtRead(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ USHORT Register,
    _Out_ PUSHORT Value)
{
    *Value = 0xffff;
    if (JhGmacMdioWrite(Adapter, Adapter->PhyAddress, YTPHY_EXT_ADDRESS, Register) != NDIS_STATUS_SUCCESS)
        return NDIS_STATUS_FAILURE;

    return JhGmacMdioRead(Adapter, Adapter->PhyAddress, YTPHY_EXT_DATA, Value);
}

static NDIS_STATUS
JhGmacYtModify(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ USHORT Register,
    _In_ USHORT Mask,
    _In_ USHORT Set)
{
    USHORT Value;

    if (JhGmacYtRead(Adapter, Register, &Value) != NDIS_STATUS_SUCCESS)
        return NDIS_STATUS_FAILURE;

    Value = (Value & ~Mask) | (Set & Mask);
    if (JhGmacMdioWrite(Adapter, Adapter->PhyAddress, YTPHY_EXT_ADDRESS, Register) != NDIS_STATUS_SUCCESS)
        return NDIS_STATUS_FAILURE;

    return JhGmacMdioWrite(Adapter, Adapter->PhyAddress, YTPHY_EXT_DATA, Value);
}

static BOOLEAN
JhGmacReadPhyId(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _Out_ PULONG PhyId)
{
    USHORT Id1, Id2;

    *PhyId = 0;
    if (JhGmacMdioRead(Adapter, PhyAddress, MII_PHYSID1, &Id1) != NDIS_STATUS_SUCCESS ||
        JhGmacMdioRead(Adapter, PhyAddress, MII_PHYSID2, &Id2) != NDIS_STATUS_SUCCESS)
    {
        return FALSE;
    }

    *PhyId = ((ULONG)Id1 << 16) | Id2;
    return (Id1 != 0x0000 || Id2 != 0x0000) && (Id1 != 0xffff || Id2 != 0xffff);
}

static VOID
JhGmacProbePhy(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    UCHAR Address;

    if (JhGmacReadPhyId(Adapter, Adapter->PhyAddress, &Adapter->PhyId))
        return;

    for (Address = 0; Address < 32; Address++)
    {
        if (JhGmacReadPhyId(Adapter, Address, &Adapter->PhyId))
        {
            Adapter->PhyAddress = Address;
            return;
        }
    }

    Adapter->PhyId = 0;
}

static ULONG
JhGmacYtDelayValue(
    _In_ ULONG DelayPs,
    _Out_opt_ PBOOLEAN RxClockDelay)
{
    if (RxClockDelay)
        *RxClockDelay = FALSE;

    if (DelayPs % YTPHY_RGMII_DELAY_STEP_PS == 0 &&
        DelayPs / YTPHY_RGMII_DELAY_STEP_PS <= YTPHY_RGMII_DELAY_MAX)
    {
        return DelayPs / YTPHY_RGMII_DELAY_STEP_PS;
    }

    if (RxClockDelay && DelayPs >= YTPHY_RGMII_RXC_DELAY_PS &&
        (DelayPs - YTPHY_RGMII_RXC_DELAY_PS) % YTPHY_RGMII_DELAY_STEP_PS == 0 &&
        (DelayPs - YTPHY_RGMII_RXC_DELAY_PS) / YTPHY_RGMII_DELAY_STEP_PS <= YTPHY_RGMII_DELAY_MAX)
    {
        *RxClockDelay = TRUE;
        return (DelayPs - YTPHY_RGMII_RXC_DELAY_PS) / YTPHY_RGMII_DELAY_STEP_PS;
    }

    return YTPHY_RGMII_DELAY_DEFAULT;
}

static BOOLEAN
JhGmacYtDriveStrength(
    _In_ UCHAR Ldo,
    _In_ ULONG Microamp,
    _Out_ PUSHORT Strength)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(JhGmacYt8531DriveStrength); i++)
    {
        if (JhGmacYt8531DriveStrength[i].Ldo == Ldo &&
            JhGmacYt8531DriveStrength[i].Microamp == Microamp)
        {
            *Strength = JhGmacYt8531DriveStrength[i].Strength;
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
JhGmacConfigureYt8531(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    BOOLEAN RxClockDelay = FALSE;
    USHORT Value = 0, Mask = 0, ChipConfig, Strength;
    UCHAR Ldo;

    if (Adapter->PhyRxDelay)
    {
        Value |= (USHORT)(JhGmacYtDelayValue(Adapter->PhyRxDelayPs, &RxClockDelay) << YTPHY_RGMII_RX_DELAY_SHIFT);
        Mask |= YTPHY_RGMII_RX_DELAY_MASK;
    }
    if (Adapter->PhyTxDelay)
    {
        Value |= (USHORT)JhGmacYtDelayValue(Adapter->PhyTxDelayPs, NULL);
        Mask |= YTPHY_RGMII_GE_TX_DELAY_MASK;
    }

    JhGmacYtModify(Adapter, YTPHY_CHIP_CONFIG, YTPHY_CHIP_CONFIG_RXC_DELAY,
                   RxClockDelay ? YTPHY_CHIP_CONFIG_RXC_DELAY : 0);
    if (Mask)
        JhGmacYtModify(Adapter, YTPHY_RGMII_CONFIG1, Mask, Value);

    if (JhGmacYtRead(Adapter, YTPHY_CHIP_CONFIG, &ChipConfig) != NDIS_STATUS_SUCCESS)
        return;
    Ldo = (UCHAR)((ChipConfig >> YTPHY_CHIP_CONFIG_LDO_SHIFT) & YTPHY_CHIP_CONFIG_LDO_MASK);
    if (Ldo > YTPHY_LDO_1V8)
        Ldo = YTPHY_LDO_1V8;

    if (Adapter->PhyRxClockMicroamp &&
        JhGmacYtDriveStrength(Ldo, Adapter->PhyRxClockMicroamp, &Strength))
    {
        JhGmacYtModify(Adapter, YTPHY_PAD_DRIVE_STRENGTH, YTPHY_RXC_DS_MASK,
                       (USHORT)(Strength << YTPHY_RXC_DS_SHIFT));
    }
    if (Adapter->PhyRxDataMicroamp &&
        JhGmacYtDriveStrength(Ldo, Adapter->PhyRxDataMicroamp, &Strength))
    {
        JhGmacYtModify(Adapter, YTPHY_PAD_DRIVE_STRENGTH,
                       YTPHY_RXD_DS_HIGH | YTPHY_RXD_DS_LOW_MASK,
                       (USHORT)(((Strength & 0x4) ? YTPHY_RXD_DS_HIGH : 0) |
                                ((Strength & 0x3) << YTPHY_RXD_DS_LOW_SHIFT)));
    }
}

static VOID
JhGmacConfigurePhy(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    USHORT Value;
    BOOLEAN Restart = FALSE;

    if (!Adapter->PhyId)
        return;

    if (Adapter->PhyId == PHY_ID_YT8531)
        JhGmacConfigureYt8531(Adapter);

    if (JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_ADVERTISE, &Value) == NDIS_STATUS_SUCCESS &&
        (Value & (ADVERTISE_ALL | ADVERTISE_CSMA)) != (ADVERTISE_ALL | ADVERTISE_CSMA))
    {
        JhGmacMdioWrite(Adapter, Adapter->PhyAddress, MII_ADVERTISE, Value | ADVERTISE_ALL | ADVERTISE_CSMA);
        Restart = TRUE;
    }

    if (JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_CTRL1000, &Value) == NDIS_STATUS_SUCCESS &&
        !(Value & ADVERTISE_1000FULL))
    {
        JhGmacMdioWrite(Adapter, Adapter->PhyAddress, MII_CTRL1000, Value | ADVERTISE_1000FULL);
        Restart = TRUE;
    }

    if (JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Value) == NDIS_STATUS_SUCCESS &&
        (Restart || !(Value & BMCR_ANENABLE)))
    {
        JhGmacMdioWrite(Adapter, Adapter->PhyAddress, MII_BMCR, Value | BMCR_ANENABLE | BMCR_ANRESTART);
    }
}

static VOID
JhGmacApplyLinkState(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG Config;
    BOOLEAN Inverted = FALSE;

    if (Adapter->MediaConnectState != MediaConnectStateConnected)
        return;

    Config = JhGmacRead32(Adapter, GMAC_CONFIG);
    Config &= ~(GMAC_CONFIG_PS | GMAC_CONFIG_FES | GMAC_CONFIG_DM);
    if (Adapter->LinkSpeed == JHGMAC_LINK_SPEED_1G)
    {
        Inverted = Adapter->PhyTxClockInverted1000;
    }
    else if (Adapter->LinkSpeed == JHGMAC_LINK_SPEED_100M)
    {
        Config |= GMAC_CONFIG_PS | GMAC_CONFIG_FES;
        Inverted = Adapter->PhyTxClockInverted100;
    }
    else
    {
        Config |= GMAC_CONFIG_PS;
        Inverted = Adapter->PhyTxClockInverted10;
    }
    if (Adapter->MediaDuplexState == MediaDuplexStateFull)
        Config |= GMAC_CONFIG_DM;
    JhGmacWrite32(Adapter, GMAC_CONFIG, Config);

    if (Adapter->PhyId == PHY_ID_YT8531 && Adapter->PhyTxClockAdjust)
    {
        JhGmacYtModify(Adapter, YTPHY_RGMII_CONFIG1, YTPHY_RGMII_TX_CLOCK_INVERTED,
                       Inverted ? YTPHY_RGMII_TX_CLOCK_INVERTED : 0);
    }
}

static BOOLEAN
JhGmacRefreshLink(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NDIS_MEDIA_CONNECT_STATE OldConnectState = Adapter->MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE OldDuplexState = Adapter->MediaDuplexState;
    ULONG64 OldLinkSpeed = Adapter->LinkSpeed;
    USHORT Bmcr = 0, Bmsr = 0, Lpa = 0, Stat1000 = 0, YtStatus = 0;
    BOOLEAN LinkChanged;

    if (!Adapter->PhyId ||
        JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Bmcr) != NDIS_STATUS_SUCCESS ||
        (Bmcr & BMCR_ANRESTART) ||
        JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr) != NDIS_STATUS_SUCCESS ||
        JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr) != NDIS_STATUS_SUCCESS ||
        !(Bmsr & BMSR_LSTATUS) ||
        (Adapter->PhyId == PHY_ID_YT8531 &&
         (JhGmacMdioRead(Adapter, Adapter->PhyAddress, YTPHY_SPECIFIC_STATUS, &YtStatus) != NDIS_STATUS_SUCCESS ||
          !(YtStatus & YTPHY_STATUS_RESOLVED))))
    {
        Adapter->MediaConnectState = MediaConnectStateDisconnected;
        Adapter->MediaDuplexState = MediaDuplexStateUnknown;
        Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    else if (Adapter->PhyId == PHY_ID_YT8531)
    {
        ULONG Speed = (YtStatus >> YTPHY_STATUS_SPEED_SHIFT) & YTPHY_STATUS_SPEED_MASK;

        Adapter->MediaConnectState = MediaConnectStateConnected;
        Adapter->MediaDuplexState = (YtStatus & YTPHY_STATUS_DUPLEX) ? MediaDuplexStateFull : MediaDuplexStateHalf;
        Adapter->LinkSpeed = (Speed == 2) ? JHGMAC_LINK_SPEED_1G :
                             (Speed == 1) ? JHGMAC_LINK_SPEED_100M : JHGMAC_LINK_SPEED_10M;
    }
    else
    {
        Adapter->MediaConnectState = MediaConnectStateConnected;
        Adapter->MediaDuplexState = MediaDuplexStateHalf;
        Adapter->LinkSpeed = JHGMAC_LINK_SPEED_10M;

        JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_LPA, &Lpa);
        JhGmacMdioRead(Adapter, Adapter->PhyAddress, MII_STAT1000, &Stat1000);

        if (Stat1000 & LPA_1000FULL)
        {
            Adapter->LinkSpeed = JHGMAC_LINK_SPEED_1G;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Stat1000 & LPA_1000HALF)
        {
            Adapter->LinkSpeed = JHGMAC_LINK_SPEED_1G;
        }
        else if (Lpa & ADVERTISE_100FULL)
        {
            Adapter->LinkSpeed = JHGMAC_LINK_SPEED_100M;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Lpa & ADVERTISE_100HALF)
        {
            Adapter->LinkSpeed = JHGMAC_LINK_SPEED_100M;
        }
        else if (Lpa & ADVERTISE_10FULL)
        {
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
    }

    LinkChanged = OldConnectState != Adapter->MediaConnectState ||
                  OldDuplexState != Adapter->MediaDuplexState ||
                  OldLinkSpeed != Adapter->LinkSpeed;
    if (LinkChanged)
    {
        JhGmacApplyLinkState(Adapter);
        DPRINT1("JH7110GMAC: PHY%u link %s speed %I64u duplex %s\n",
                Adapter->PhyAddress,
                (Adapter->MediaConnectState == MediaConnectStateConnected) ? "up" : "down",
                Adapter->LinkSpeed,
                (Adapter->MediaDuplexState == MediaDuplexStateFull) ? "full" : "half");
    }

    return LinkChanged;
}

static VOID
JhGmacIndicateLinkState(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION StatusIndication;
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Adapter->MediaConnectState;
    LinkState.MediaDuplexState = Adapter->MediaDuplexState;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    RtlZeroMemory(&StatusIndication, sizeof(StatusIndication));
    StatusIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    StatusIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    StatusIndication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    StatusIndication.SourceHandle = Adapter->MiniportHandle;
    StatusIndication.StatusCode = NDIS_STATUS_LINK_STATE;
    StatusIndication.StatusBuffer = &LinkState;
    StatusIndication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportHandle, &StatusIndication);
}

static VOID
JhGmacDrainTxCompletions(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    if (!Adapter->DatapathReady)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);

    while (Adapter->TxTail != Adapter->TxHead)
    {
        ULONG Index = Adapter->TxTail;
        ULONG Des3;

        KeMemoryBarrier();
        Des3 = Adapter->TxRing[Index].Des3;
        if (Des3 & DES3_OWN)
            break;

        if (Des3 & TDES3_ERROR_SUMMARY)
        {
            Adapter->TxErrors++;
        }
        else
        {
            Adapter->TxPackets++;
            Adapter->TxBytes += Adapter->TxBuffers[Index].Length;
        }
        Adapter->TxBuffers[Index].Length = 0;
        Adapter->TxTail = (Index + 1) % JHGMAC_TX_RING_SIZE;
        Adapter->TxFree++;
    }

    NdisReleaseSpinLock(&Adapter->TxLock);
}

static NDIS_STATUS
JhGmacCopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(DestinationLength) PVOID Destination,
    _In_ ULONG DestinationLength,
    _Out_ PULONG BytesCopied)
{
    PMDL Mdl;
    ULONG MdlOffset;
    SIZE_T Remaining;
    ULONG Copied;

    *BytesCopied = 0;
    Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    if (Remaining > DestinationLength)
        return NDIS_STATUS_BUFFER_OVERFLOW;

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    Copied = 0;

    while (Remaining)
    {
        PVOID Source;
        ULONG MdlLength;
        ULONG CopyLength;

        if (!Mdl)
            return NDIS_STATUS_INVALID_PACKET;

        MdlLength = MmGetMdlByteCount(Mdl);
        if (MdlOffset >= MdlLength)
        {
            Mdl = Mdl->Next;
            MdlOffset = 0;
            continue;
        }

        CopyLength = MdlLength - MdlOffset;
        if (CopyLength > Remaining)
            CopyLength = (ULONG)Remaining;

        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!Source)
            return NDIS_STATUS_RESOURCES;

        RtlCopyMemory((PUCHAR)Destination + Copied,
                      (PUCHAR)Source + MdlOffset,
                      CopyLength);

        Copied += CopyLength;
        Remaining -= CopyLength;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    *BytesCopied = Copied;
    return NDIS_STATUS_SUCCESS;
}

static BOOLEAN
JhGmacReceivePending(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    BOOLEAN Pending;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->RxLock);
    KeMemoryBarrier();
    Pending = !Adapter->RxBuffers[Adapter->RxTail].Indicated &&
              !(Adapter->RxRing[Adapter->RxTail].Des3 & DES3_OWN);
    NdisReleaseSpinLock(&Adapter->RxLock);
    return Pending;
}

static VOID
JhGmacPollReceive(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ ULONG Budget)
{
    PNET_BUFFER_LIST NblChain = NULL;
    PNET_BUFFER_LIST LastNbl = NULL;
    ULONG Received = 0;
    BOOLEAN Rearmed = FALSE;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return;

    NdisAcquireSpinLock(&Adapter->RxLock);

    while (Budget-- > 0)
    {
        ULONG Index = Adapter->RxTail;
        PJHGMAC_RX_BUFFER RxBuffer = &Adapter->RxBuffers[Index];
        PNET_BUFFER_LIST Nbl;
        PNET_BUFFER Nb;
        ULONG Des3;
        ULONG Length;

        if (RxBuffer->Indicated)
            break;

        KeMemoryBarrier();
        Des3 = Adapter->RxRing[Index].Des3;
        if (Des3 & DES3_OWN)
            break;
        KeMemoryBarrier();

        Length = Des3 & RDES3_PACKET_LENGTH_MASK;
        if ((Des3 & (RDES3_FIRST_DESCRIPTOR | RDES3_LAST_DESCRIPTOR | RDES3_CONTEXT_DESCRIPTOR)) !=
                (RDES3_FIRST_DESCRIPTOR | RDES3_LAST_DESCRIPTOR) ||
            (Des3 & RDES3_ERROR_SUMMARY) ||
            Length < ETH_LENGTH_OF_ADDRESS * 2 + 2 + JHGMAC_FCS_SIZE ||
            Length > JHGMAC_FRAME_SIZE + JHGMAC_FCS_SIZE)
        {
            Adapter->RxErrors++;
            JhGmacRearmRxDescriptor(Adapter, Index);
            Rearmed = TRUE;
            Adapter->RxTail = (Index + 1) % JHGMAC_RX_RING_SIZE;
            continue;
        }

        Length -= JHGMAC_FCS_SIZE;

        Nbl = RxBuffer->NetBufferList;
        Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NET_BUFFER_CURRENT_MDL(Nb) = RxBuffer->Mdl;
        NET_BUFFER_CURRENT_MDL_OFFSET(Nb) = 0;
        NET_BUFFER_DATA_LENGTH(Nb) = Length;
        RxBuffer->Indicated = TRUE;
        Nbl->SourceHandle = Adapter->MiniportHandle;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = (PVOID)(ULONG_PTR)Index;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (!NblChain)
            NblChain = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(LastNbl) = Nbl;
        LastNbl = Nbl;
        Received++;
        Adapter->RxPackets++;
        Adapter->RxBytes += Length;
        Adapter->RxTail = (Index + 1) % JHGMAC_RX_RING_SIZE;
    }

    if (Rearmed)
        JhGmacKickRx(Adapter);

    NdisReleaseSpinLock(&Adapter->RxLock);

    if (NblChain)
    {
        ULONG Flags = 0;

        if (KeGetCurrentIrql() == DISPATCH_LEVEL)
            Flags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;

        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle,
                                           NblChain,
                                           0,
                                           Received,
                                           Flags);
    }
}

static BOOLEAN NTAPI
JhGmacInterrupt(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)ServiceContext;
    ULONG Status;

    UNREFERENCED_PARAMETER(Interrupt);

    Status = JhGmacRead32(Adapter, DMA_CH0_STATUS);
    if (!(Status & DMA_CH_STATUS_ALL))
        return FALSE;

    JhGmacDisableInterrupts(Adapter);
    JhGmacWrite32(Adapter, DMA_CH0_STATUS, Status & DMA_CH_STATUS_ALL);
    Adapter->InterruptCount++;
    InterlockedOr(&Adapter->InterruptPending, (LONG)(Status & DMA_CH_STATUS_ALL));
    KeInsertQueueDpc(&Adapter->InterruptDpc, NULL, NULL);
    return TRUE;
}

static VOID NTAPI
JhGmacInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)DeferredContext;
    ULONG Pending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!Adapter || !Adapter->DatapathReady)
        return;

    Pending = (ULONG)InterlockedExchange(&Adapter->InterruptPending, 0);
    if (Pending & DMA_CH_RBU)
        Adapter->RxNoBuffer++;
    if (Pending & DMA_CH_FBE)
        DPRINT1("JH7110GMAC: fatal bus error, status 0x%08lx\n", Pending);

    JhGmacPollReceive(Adapter, JHGMAC_RX_BUDGET);
    JhGmacDrainTxCompletions(Adapter);
    JhGmacEnableInterrupts(Adapter);

    if (JhGmacReceivePending(Adapter))
        KeInsertQueueDpc(&Adapter->InterruptDpc, NULL, NULL);
}

static VOID NTAPI
JhGmacLinkTimer(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (!Adapter || !Adapter->RegisterBase)
        return;

    if (JhGmacRefreshLink(Adapter))
        JhGmacIndicateLinkState(Adapter);

    if (Adapter->DatapathReady)
    {
        JhGmacPollReceive(Adapter, JHGMAC_RX_BUDGET);
        JhGmacDrainTxCompletions(Adapter);
    }
}

static VOID
JhGmacStopLinkTimer(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    BOOLEAN Cancelled;

    if (Adapter->LinkTimerInitialized)
    {
        NdisMCancelTimer(&Adapter->LinkTimer, &Cancelled);
        Adapter->LinkTimerInitialized = FALSE;
        if (!Cancelled)
            KeFlushQueuedDpcs();
    }
}

static BOOLEAN
JhGmacReadProperty(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG Length)
{
    UCHAR Storage[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 64];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG ResultLength;

    *Length = 0;
    if (!Key)
        return FALSE;

    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(ZwQueryValueKey(Key,
                                    &ValueName,
                                    KeyValuePartialInformation,
                                    Information,
                                    sizeof(Storage),
                                    &ResultLength)) ||
        Information->DataLength > BufferLength)
    {
        return FALSE;
    }

    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return TRUE;
}

static BOOLEAN
JhGmacReadCells(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count)
{
    UCHAR Cells[4 * sizeof(ULONG)];
    ULONG Length, i;

    if (Count > RTL_NUMBER_OF(Cells) / sizeof(ULONG) ||
        !JhGmacReadProperty(Key, Name, Cells, sizeof(Cells), &Length) ||
        Length != Count * sizeof(ULONG))
    {
        return FALSE;
    }

    for (i = 0; i < Count; i++)
    {
        const UCHAR *Cell = &Cells[i * sizeof(ULONG)];

        Values[i] = ((ULONG)Cell[0] << 24) | ((ULONG)Cell[1] << 16) | ((ULONG)Cell[2] << 8) | Cell[3];
    }
    return TRUE;
}

static BOOLEAN
JhGmacHasProperty(
    _In_ HANDLE Key,
    _In_ PCWSTR Name)
{
    UCHAR Unused[4];
    ULONG Length;

    return JhGmacReadProperty(Key, Name, Unused, sizeof(Unused), &Length);
}

static HANDLE
JhGmacOpenSubkey(
    _In_ HANDLE Parent,
    _In_ PCWSTR Name)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING KeyName;
    HANDLE Key;

    if (!Parent)
        return NULL;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parent, NULL);
    return NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &Attributes)) ? Key : NULL;
}

static HANDLE
JhGmacOpenPhyKey(
    _In_ HANDLE DeviceKey)
{
    UCHAR Storage[sizeof(KEY_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Information = (PKEY_BASIC_INFORMATION)Storage;
    WCHAR Name[65];
    HANDLE Mdio, Phy = NULL;
    ULONG Index, ResultLength;

    Mdio = JhGmacOpenSubkey(DeviceKey, L"mdio");
    if (!Mdio)
        return NULL;

    for (Index = 0; !Phy; Index++)
    {
        if (!NT_SUCCESS(ZwEnumerateKey(Mdio, Index, KeyBasicInformation, Information,
                                       sizeof(Storage) - sizeof(WCHAR), &ResultLength)))
            break;
        if (Information->NameLength >= sizeof(Name))
            continue;
        RtlCopyMemory(Name, Information->Name, Information->NameLength);
        Name[Information->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        if (_wcsnicmp(Name, L"ethernet-phy", 12) == 0)
            Phy = JhGmacOpenSubkey(Mdio, Name);
    }

    ZwClose(Mdio);
    return Phy;
}

static NDIS_STATUS
JhGmacReadConfiguration(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    static const UCHAR FallbackAddress[ETH_LENGTH_OF_ADDRESS] = { 0x02, 0x4a, 0x48, 0x00, 0x00, 0x00 };
    CHAR PhyMode[16];
    HANDLE Key = NULL, PhyKey;
    ULONG Length, Cells[3];

    if ((ULONG64)Adapter->RegisterPhysical.QuadPart != JH7110_GMAC0_BASE)
        return NDIS_STATUS_ADAPTER_NOT_FOUND;

    Adapter->PhyAddress = 0;
    Adapter->SysconOffset = 0xc;
    Adapter->SysconShift = 18;
    Adapter->TxFifoSize = 2048;
    Adapter->RxFifoSize = 2048;
    Adapter->TxPbl = 16;
    Adapter->RxPbl = 16;

    if (Adapter->PhysicalDeviceObject &&
        !NT_SUCCESS(IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                            PLUGPLAY_REGKEY_DEVICE,
                                            KEY_READ,
                                            &Key)))
    {
        Key = NULL;
    }

    if (JhGmacReadCells(Key, L"starfive,syscon", Cells, 3) &&
        Cells[1] <= JH7110_AON_SYSCON_SPAN - sizeof(ULONG) && Cells[2] <= 29)
    {
        Adapter->SysconOffset = Cells[1];
        Adapter->SysconShift = Cells[2];
    }
    if (JhGmacReadCells(Key, L"tx-fifo-depth", Cells, 1) && Cells[0] >= MTL_QUEUE_UNIT && Cells[0] <= 0x20000)
        Adapter->TxFifoSize = Cells[0];
    if (JhGmacReadCells(Key, L"rx-fifo-depth", Cells, 1) && Cells[0] >= MTL_QUEUE_UNIT && Cells[0] <= 0x40000)
        Adapter->RxFifoSize = Cells[0];
    if (JhGmacReadCells(Key, L"snps,txpbl", Cells, 1) && Cells[0] && Cells[0] <= 32)
        Adapter->TxPbl = Cells[0];
    if (JhGmacReadCells(Key, L"snps,rxpbl", Cells, 1) && Cells[0] && Cells[0] <= 32)
        Adapter->RxPbl = Cells[0];
    Adapter->TxUsesRgmiiClock = JhGmacHasProperty(Key, L"starfive,tx-use-rgmii-clk");

    Adapter->PhyTxDelay = TRUE;
    Adapter->PhyRxDelay = TRUE;
    RtlZeroMemory(PhyMode, sizeof(PhyMode));
    if (JhGmacReadProperty(Key, L"phy-mode", (PUCHAR)PhyMode, sizeof(PhyMode) - 1, &Length))
    {
        Adapter->PhyTxDelay = !strcmp(PhyMode, "rgmii-id") || !strcmp(PhyMode, "rgmii-txid");
        Adapter->PhyRxDelay = !strcmp(PhyMode, "rgmii-id") || !strcmp(PhyMode, "rgmii-rxid");
    }

    Adapter->PhyTxDelayPs = 1950;
    Adapter->PhyRxDelayPs = 1950;
    PhyKey = JhGmacOpenPhyKey(Key);
    if (PhyKey)
    {
        if (JhGmacReadCells(PhyKey, L"reg", Cells, 1) && Cells[0] < 32)
            Adapter->PhyAddress = (UCHAR)Cells[0];
        if (JhGmacReadCells(PhyKey, L"tx-internal-delay-ps", Cells, 1))
            Adapter->PhyTxDelayPs = Cells[0];
        if (JhGmacReadCells(PhyKey, L"rx-internal-delay-ps", Cells, 1))
            Adapter->PhyRxDelayPs = Cells[0];
        if (JhGmacReadCells(PhyKey, L"motorcomm,rx-clk-drv-microamp", Cells, 1))
            Adapter->PhyRxClockMicroamp = Cells[0];
        if (JhGmacReadCells(PhyKey, L"motorcomm,rx-data-drv-microamp", Cells, 1))
            Adapter->PhyRxDataMicroamp = Cells[0];
        Adapter->PhyTxClockAdjust = JhGmacHasProperty(PhyKey, L"motorcomm,tx-clk-adj-enabled");
        Adapter->PhyTxClockInverted10 = JhGmacHasProperty(PhyKey, L"motorcomm,tx-clk-10-inverted");
        Adapter->PhyTxClockInverted100 = JhGmacHasProperty(PhyKey, L"motorcomm,tx-clk-100-inverted");
        Adapter->PhyTxClockInverted1000 = JhGmacHasProperty(PhyKey, L"motorcomm,tx-clk-1000-inverted");
        ZwClose(PhyKey);
    }

    if ((!JhGmacReadProperty(Key, L"local-mac-address", Adapter->PermanentMacAddress,
                             ETH_LENGTH_OF_ADDRESS, &Length) ||
         Length != ETH_LENGTH_OF_ADDRESS ||
         !JhGmacIsValidMacAddress(Adapter->PermanentMacAddress)) &&
        (!JhGmacReadProperty(Key, L"mac-address", Adapter->PermanentMacAddress,
                             ETH_LENGTH_OF_ADDRESS, &Length) ||
         Length != ETH_LENGTH_OF_ADDRESS ||
         !JhGmacIsValidMacAddress(Adapter->PermanentMacAddress)))
    {
        RtlCopyMemory(Adapter->PermanentMacAddress, FallbackAddress, ETH_LENGTH_OF_ADDRESS);
    }
    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    if (Key)
        ZwClose(Key);
    return NDIS_STATUS_SUCCESS;
}

static VOID
JhGmacSetClock(
    _In_ PUCHAR Crg,
    _In_ ULONG Id,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    PULONG Register = (PULONG)(Crg + Id * sizeof(ULONG));

    WRITE_REGISTER_ULONG(Register, (READ_REGISTER_ULONG(Register) & ~Mask) | (Value & Mask));
}

static NDIS_STATUS
JhGmacPowerOn(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS Physical;
    PUCHAR Aon, Sys, Syscon;
    PULONG Mode;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG i;

    Physical.QuadPart = JH7110_AONCRG_BASE;
    Aon = MmMapIoSpace(Physical, JH7110_AONCRG_SPAN, MmNonCached);
    Physical.QuadPart = JH7110_SYSCRG_BASE;
    Sys = MmMapIoSpace(Physical, JH7110_SYSCRG_SPAN, MmNonCached);
    Physical.QuadPart = JH7110_AON_SYSCON_BASE;
    Syscon = MmMapIoSpace(Physical, JH7110_AON_SYSCON_SPAN, MmNonCached);
    if (!Aon || !Sys || !Syscon)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Done;
    }

    JhGmacSetClock(Sys, JH7110_SYSCLK_GMAC0_GTXCLK, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    JhGmacSetClock(Sys, JH7110_SYSCLK_GMAC0_PTP, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    JhGmacSetClock(Sys, JH7110_SYSCLK_GMAC0_GTXC, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    JhGmacSetClock(Aon, JH7110_AONCLK_GMAC0_AHB, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    JhGmacSetClock(Aon, JH7110_AONCLK_GMAC0_AXI, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    if (Adapter->TxUsesRgmiiClock)
    {
        JhGmacSetClock(Aon, JH7110_AONCLK_GMAC0_RMII_RTX, JH7110_CLK_DIV_MASK, 1);
        JhGmacSetClock(Aon, JH7110_AONCLK_GMAC0_TX, JH7110_CLK_MUX_MASK | JH7110_CLK_ENABLE,
                       (JH7110_AONCLK_GMAC0_TX_MUX_RMII_RTX << JH7110_CLK_MUX_SHIFT) | JH7110_CLK_ENABLE);
    }
    else
    {
        JhGmacSetClock(Aon, JH7110_AONCLK_GMAC0_TX, JH7110_CLK_MUX_MASK | JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
    }

    Mode = (PULONG)(Syscon + Adapter->SysconOffset);
    WRITE_REGISTER_ULONG(Mode,
                         (READ_REGISTER_ULONG(Mode) & ~(JH7110_GMAC_PHY_MODE_FIELD << Adapter->SysconShift)) |
                         (JH7110_GMAC_PHY_MODE_RGMII << Adapter->SysconShift));

    WRITE_REGISTER_ULONG((PULONG)(Aon + JH7110_AONRST_ASSERT),
                         READ_REGISTER_ULONG((PULONG)(Aon + JH7110_AONRST_ASSERT)) & ~JH7110_AONRST_GMAC0);
    for (i = 0; i < 1000; i++)
    {
        if ((READ_REGISTER_ULONG((PULONG)(Aon + JH7110_AONRST_STATUS)) & JH7110_AONRST_GMAC0) == JH7110_AONRST_GMAC0)
            break;
        KeStallExecutionProcessor(10);
    }
    if (i == 1000)
    {
        DPRINT1("JH7110GMAC: GMAC0 reset did not release\n");
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

Done:
    if (Syscon)
        MmUnmapIoSpace(Syscon, JH7110_AON_SYSCON_SPAN);
    if (Sys)
        MmUnmapIoSpace(Sys, JH7110_SYSCRG_SPAN);
    if (Aon)
        MmUnmapIoSpace(Aon, JH7110_AONCRG_SPAN);
    return Status;
}

static NDIS_STATUS
JhGmacInitializeHardware(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    ULONG i, Value;

    JhGmacWrite32(Adapter, DMA_BUS_MODE, DMA_BUS_MODE_SOFTWARE_RESET);
    for (i = 0; i < 10000; i++)
    {
        if (!(JhGmacRead32(Adapter, DMA_BUS_MODE) & DMA_BUS_MODE_SOFTWARE_RESET))
            break;
        KeStallExecutionProcessor(10);
    }
    if (i == 10000)
    {
        DPRINT1("JH7110GMAC: DMA software reset did not complete\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    JhGmacWrite32(Adapter, DMA_SYS_BUS_MODE,
                  DMA_SYS_BUS_FIXED_BURST |
                  DMA_SYS_BUS_BLEN32 | DMA_SYS_BUS_BLEN64 | DMA_SYS_BUS_BLEN128 | DMA_SYS_BUS_BLEN256 |
                  (DMA_SYS_BUS_OSR_MAX << DMA_SYS_BUS_RD_OSR_SHIFT) |
                  (DMA_SYS_BUS_OSR_MAX << DMA_SYS_BUS_WR_OSR_SHIFT));
    JhGmacWrite32(Adapter, DMA_CH0_CONTROL, 0);

    Value = JhGmacRead32(Adapter, DMA_CH0_TX_CONTROL) & ~(DMA_CHANNEL_PBL_MASK | DMA_CHANNEL_START);
    JhGmacWrite32(Adapter, DMA_CH0_TX_CONTROL, Value | (Adapter->TxPbl << DMA_CHANNEL_PBL_SHIFT));
    Value = JhGmacRead32(Adapter, DMA_CH0_RX_CONTROL) &
            ~(DMA_CHANNEL_PBL_MASK | DMA_RX_BUFFER_SIZE_MASK | DMA_CHANNEL_START);
    JhGmacWrite32(Adapter, DMA_CH0_RX_CONTROL,
                  Value | (Adapter->RxPbl << DMA_CHANNEL_PBL_SHIFT) |
                  ((JHGMAC_BUFFER_SIZE << DMA_RX_BUFFER_SIZE_SHIFT) & DMA_RX_BUFFER_SIZE_MASK));

    JhGmacWrite32(Adapter, MTL_TXQ0_OPERATION_MODE,
                  MTL_TXQ_TSF | MTL_TXQ_ENABLE |
                  ((Adapter->TxFifoSize / MTL_QUEUE_UNIT - 1) << MTL_TXQ_SIZE_SHIFT));
    JhGmacWrite32(Adapter, MTL_RXQ0_OPERATION_MODE,
                  MTL_RXQ_RSF | ((Adapter->RxFifoSize / MTL_QUEUE_UNIT - 1) << MTL_RXQ_SIZE_SHIFT));
    JhGmacWrite32(Adapter, GMAC_RXQ_CTRL0, GMAC_RXQ0_ENABLE_GENERIC);

    JhGmacWrite32(Adapter, GMAC_INT_EN, 0);
    JhGmacWrite32(Adapter, GMAC_CONFIG, GMAC_CONFIG_PS | GMAC_CONFIG_DM);
    JhGmacWriteMacAddress(Adapter);
    JhGmacApplyPacketFilter(Adapter);
    JhGmacWrite32(Adapter, DMA_CH0_STATUS, DMA_CH_STATUS_ALL);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
JhGmacSetRegistrationAttributes(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;

    RtlZeroMemory(&RegAttrs, sizeof(RegAttrs));
    RegAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttrs.Header.Size = sizeof(RegAttrs);
    RegAttrs.MiniportAdapterContext = Adapter;
    RegAttrs.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                              NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    RegAttrs.CheckForHangTimeInSeconds = 4;
    RegAttrs.InterfaceType = NdisInterfaceInternal;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttrs);
}

static NDIS_STATUS
JhGmacSetGeneralAttributes(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;

    RtlZeroMemory(&GenAttrs, sizeof(GenAttrs));
    GenAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = NdisPhysicalMedium802_3;
    GenAttrs.MtuSize = JHGMAC_MTU;
    GenAttrs.MaxXmitLinkSpeed = JHGMAC_LINK_SPEED_1G;
    GenAttrs.MaxRcvLinkSpeed = JHGMAC_LINK_SPEED_1G;
    GenAttrs.XmitLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.RcvLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.MediaConnectState = Adapter->MediaConnectState;
    GenAttrs.MediaDuplexState = Adapter->MediaDuplexState;
    GenAttrs.LookaheadSize = Adapter->Lookahead;
    GenAttrs.MacOptions = JHGMAC_MAC_OPTIONS;
    GenAttrs.SupportedPacketFilters = JHGMAC_SUPPORTED_FILTERS;
    GenAttrs.MaxMulticastListSize = JHGMAC_MAX_MULTICAST;
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(GenAttrs.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(GenAttrs.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    GenAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
    GenAttrs.IfConnectorPresent = TRUE;
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    GenAttrs.SupportedOidList = (PNDIS_OID)JhGmacSupportedOids;
    GenAttrs.SupportedOidListLength = sizeof(JhGmacSupportedOids);
    GenAttrs.SupportedStatistics =
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttrs);
}

static NDIS_STATUS
JhGmacMapResources(
    _In_ PJHGMAC_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;
    NDIS_STATUS Status;

    if (!ResourceList)
        return NDIS_STATUS_RESOURCES;

    Descriptor = &ResourceList->PartialDescriptors[0];
    for (i = 0; i < ResourceList->Count; i++, Descriptor++)
    {
        if (Descriptor->Type == CmResourceTypeMemory && !Adapter->RegisterBase)
        {
            Adapter->RegisterPhysical = Descriptor->u.Memory.Start;
            Adapter->RegisterLength = Descriptor->u.Memory.Length;
            Status = NdisMMapIoSpace((PVOID *)&Adapter->RegisterBase,
                                     Adapter->MiniportHandle,
                                     Adapter->RegisterPhysical,
                                     Adapter->RegisterLength);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Adapter->RegisterBase = NULL;
                return Status;
            }
        }
        else if (Descriptor->Type == CmResourceTypeInterrupt && !Adapter->InterruptSource)
        {
            Adapter->InterruptSource = Descriptor->u.Interrupt.Vector;
        }
    }

    return (Adapter->RegisterBase && Adapter->InterruptSource) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static NDIS_STATUS
JhGmacConnectInterrupt(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    KAFFINITY Affinity = 0;
    KIRQL Irql = 0;
    ULONG Vector;
    NTSTATUS Status;

    KeInitializeDpc(&Adapter->InterruptDpc, JhGmacInterruptDpc, Adapter);
    Vector = HalGetInterruptVector(Internal,
                                   0,
                                   Adapter->InterruptSource,
                                   Adapter->InterruptSource,
                                   &Irql,
                                   &Affinity);
    if (!Vector)
        return NDIS_STATUS_RESOURCES;

    Status = IoConnectInterrupt(&Adapter->InterruptObject,
                                JhGmacInterrupt,
                                Adapter,
                                NULL,
                                Vector,
                                Irql,
                                Irql,
                                LevelSensitive,
                                FALSE,
                                Affinity,
                                FALSE);
    if (!NT_SUCCESS(Status))
    {
        Adapter->InterruptObject = NULL;
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static VOID
JhGmacDisconnectInterrupt(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    if (Adapter->InterruptObject)
    {
        JhGmacDisableInterrupts(Adapter);
        IoDisconnectInterrupt(Adapter->InterruptObject);
        Adapter->InterruptObject = NULL;
        KeRemoveQueueDpc(&Adapter->InterruptDpc);
        KeFlushQueuedDpcs();
    }
}

static NDIS_STATUS
JhGmacCopyQuery(
    _Inout_ PNDIS_OID_REQUEST OidRequest,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    PVOID InfoBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    UINT InfoBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
    PUINT BytesWritten = &OidRequest->DATA.QUERY_INFORMATION.BytesWritten;
    PUINT BytesNeeded = &OidRequest->DATA.QUERY_INFORMATION.BytesNeeded;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    if (InfoBufferLength < Length)
    {
        *BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(InfoBuffer, Source, Length);
    *BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
JhGmacQueryInformation(
    _In_ PJHGMAC_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.QUERY_INFORMATION.Oid;
    union
    {
        ULONG Ulong;
        USHORT Ushort;
        ULONG64 Ulong64;
        NDIS_MEDIUM Medium;
        NDIS_HARDWARE_STATUS HardwareStatus;
        NDIS_MEDIA_STATE MediaState;
        NDIS_PHYSICAL_MEDIUM PhysicalMedium;
        NDIS_LINK_STATE LinkState;
        UCHAR Mac[ETH_LENGTH_OF_ADDRESS];
    } Data;

    RtlZeroMemory(&Data, sizeof(Data));

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return JhGmacCopyQuery(OidRequest, JhGmacSupportedOids, sizeof(JhGmacSupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return JhGmacCopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return JhGmacCopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return JhGmacCopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = JHGMAC_MTU;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = JHGMAC_FRAME_SIZE;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "StarFive JH7110 Ethernet";
            return JhGmacCopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = JHGMAC_DRIVER_VERSION;
            return JhGmacCopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = JHGMAC_MAC_OPTIONS;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = (Adapter->MediaConnectState == MediaConnectStateConnected) ?
                              NdisMediaStateConnected :
                              NdisMediaStateDisconnected;
            return JhGmacCopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            return JhGmacCopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->TxErrors;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return JhGmacCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return JhGmacCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return JhGmacCopyQuery(OidRequest,
                                   Adapter->MulticastList,
                                   Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = JHGMAC_MAX_MULTICAST;
            return JhGmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS
JhGmacSetInformation(
    _In_ PJHGMAC_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID InfoBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    UINT InfoBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PUINT BytesRead = &OidRequest->DATA.SET_INFORMATION.BytesRead;
    PUINT BytesNeeded = &OidRequest->DATA.SET_INFORMATION.BytesNeeded;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->PacketFilter = *(PULONG)InfoBuffer;
            JhGmacApplyPacketFilter(Adapter);
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->Lookahead = min(*(PULONG)InfoBuffer, JHGMAC_MTU);
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if ((InfoBufferLength % ETH_LENGTH_OF_ADDRESS) != 0)
                return NDIS_STATUS_INVALID_LENGTH;

            if (InfoBufferLength > sizeof(Adapter->MulticastList))
            {
                *BytesNeeded = sizeof(Adapter->MulticastList);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->MulticastCount = InfoBufferLength / ETH_LENGTH_OF_ADDRESS;
            RtlCopyMemory(Adapter->MulticastList, InfoBuffer, InfoBufferLength);
            JhGmacApplyPacketFilter(Adapter);
            *BytesRead = InfoBufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID
JhGmacFreeAdapter(
    _In_ PJHGMAC_ADAPTER Adapter)
{
    JhGmacDisconnectInterrupt(Adapter);
    JhGmacStopHardware(Adapter);
    JhGmacFreeDatapath(Adapter);
    JhGmacUnmapResources(Adapter);
    NdisFreeSpinLock(&Adapter->MdioLock);
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    ExFreePoolWithTag(Adapter, JHGMAC_TAG);
}

static NDIS_STATUS NTAPI
JhGmacInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PJHGMAC_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), JHGMAC_TAG);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = JHGMAC_MTU;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->MdioLock);

    Status = JhGmacSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,
                           NULL,
                           NULL,
                           NULL);

    Status = JhGmacMapResources(Adapter,
                                (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacPowerOn(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacInitializeHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    JhGmacProbePhy(Adapter);
    JhGmacConfigurePhy(Adapter);
    DPRINT1("JH7110GMAC: version 0x%08lx MMIO 0x%I64x irq %lu PHY%u id 0x%08lx MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            JhGmacRead32(Adapter, GMAC_VERSION),
            Adapter->RegisterPhysical.QuadPart,
            Adapter->InterruptSource,
            Adapter->PhyAddress,
            Adapter->PhyId,
            Adapter->CurrentMacAddress[0],
            Adapter->CurrentMacAddress[1],
            Adapter->CurrentMacAddress[2],
            Adapter->CurrentMacAddress[3],
            Adapter->CurrentMacAddress[4],
            Adapter->CurrentMacAddress[5]);
    JhGmacRefreshLink(Adapter);

    Status = JhGmacInitializeDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacConnectInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = JhGmacSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    JhGmacWrite32(Adapter, DMA_CH0_STATUS, DMA_CH_STATUS_ALL);
    JhGmacEnableInterrupts(Adapter);

    NdisMInitializeTimer(&Adapter->LinkTimer, Adapter->MiniportHandle, JhGmacLinkTimer, Adapter);
    Adapter->LinkTimerInitialized = TRUE;
    NdisMSetPeriodicTimer(&Adapter->LinkTimer, 1000);
    return NDIS_STATUS_SUCCESS;

Failure:
    DPRINT1("JH7110GMAC: initialization failed 0x%08x\n", Status);
    JhGmacFreeAdapter(Adapter);
    return Status;
}

static VOID NTAPI
JhGmacHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    if (!Adapter)
        return;

    JhGmacStopLinkTimer(Adapter);
    JhGmacFreeAdapter(Adapter);
}

static NDIS_STATUS NTAPI
JhGmacPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
JhGmacRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
JhGmacSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER_LIST FailHead = NULL;
    PNET_BUFFER_LIST FailTail = NULL;
    PNET_BUFFER_LIST OkHead = NULL;
    PNET_BUFFER_LIST OkTail = NULL;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;

    JhGmacDrainTxCompletions(Adapter);

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        PNET_BUFFER NetBuffer;
        NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
        ULONG Index;
        ULONG Length = 0;
        ULONG NetBufferCount = 0;
        ULONG Offset;
        ULONG StartIndex;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        if (!Adapter->DatapathReady ||
            Adapter->MediaConnectState != MediaConnectStateConnected)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
            goto FailNbl;
        }

        if (!NetBuffer)
        {
            Status = NDIS_STATUS_INVALID_PACKET;
            goto FailNbl;
        }

        for (; NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            if (NET_BUFFER_DATA_LENGTH(NetBuffer) > JHGMAC_FRAME_SIZE ||
                NET_BUFFER_DATA_LENGTH(NetBuffer) < ETH_LENGTH_OF_ADDRESS)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                goto FailNbl;
            }

            NetBufferCount++;
            if (NetBufferCount >= JHGMAC_TX_RING_SIZE)
            {
                Status = NDIS_STATUS_RESOURCES;
                goto FailNbl;
            }
        }

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxFree < NetBufferCount)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            Status = NDIS_STATUS_RESOURCES;
            goto FailNbl;
        }

        StartIndex = Adapter->TxHead;
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        for (Offset = 0; NetBuffer; Offset++, NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Index = (StartIndex + Offset) % JHGMAC_TX_RING_SIZE;
            Status = JhGmacCopyNetBuffer(NetBuffer,
                                         Adapter->TxBuffers[Index].VirtualAddress,
                                         JHGMAC_BUFFER_SIZE,
                                         &Length);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                while (Offset > 0)
                {
                    Offset--;
                    Index = (StartIndex + Offset) % JHGMAC_TX_RING_SIZE;
                    Adapter->TxBuffers[Index].Length = 0;
                }
                NdisReleaseSpinLock(&Adapter->TxLock);
                goto FailNbl;
            }

            Adapter->TxBuffers[Index].Length = Length;
        }

        KeMemoryBarrier();
        for (Offset = 0; Offset < NetBufferCount; Offset++)
        {
            PJHGMAC_DESCRIPTOR Descriptor;

            Index = (StartIndex + Offset) % JHGMAC_TX_RING_SIZE;
            Descriptor = &Adapter->TxRing[Index];
            Length = Adapter->TxBuffers[Index].Length;

            Descriptor->Des0 = Adapter->TxBuffers[Index].PhysicalAddress.LowPart;
            Descriptor->Des1 = 0;
            Descriptor->Des2 = (Length & TDES2_BUFFER1_LENGTH_MASK) | TDES2_INTERRUPT_ON_COMPLETION;
            KeMemoryBarrier();
            Descriptor->Des3 = DES3_OWN | TDES3_FIRST_DESCRIPTOR | TDES3_LAST_DESCRIPTOR |
                               (Length & TDES3_FRAME_LENGTH_MASK);
        }

        Adapter->TxHead = (StartIndex + NetBufferCount) % JHGMAC_TX_RING_SIZE;
        Adapter->TxFree -= NetBufferCount;
        KeMemoryBarrier();
        JhGmacWrite32(Adapter, DMA_CH0_TX_TAIL,
                      JhGmacDescriptorAddress(Adapter->TxRingPhysical, Adapter->TxHead));

        NdisReleaseSpinLock(&Adapter->TxLock);

        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        if (!OkHead)
            OkHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(OkTail) = Nbl;
        OkTail = Nbl;
        continue;

FailNbl:
        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        Adapter->TxErrors++;
        if (!FailHead)
            FailHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(FailTail) = Nbl;
        FailTail = Nbl;
    }

    if (OkHead)
    {
        NET_BUFFER_LIST_NEXT_NBL(OkTail) = NULL;
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, OkHead, CompleteFlags);
    }

    if (FailHead)
    {
        NET_BUFFER_LIST_NEXT_NBL(FailTail) = NULL;
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, FailHead, CompleteFlags);
    }
}

static VOID NTAPI
JhGmacReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;

    UNREFERENCED_PARAMETER(ReturnFlags);

    if (!Adapter)
        return;

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        ULONG Index;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        Index = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0];

        NdisAcquireSpinLock(&Adapter->RxLock);
        if (Index < JHGMAC_RX_RING_SIZE &&
            Adapter->RxBuffers[Index].NetBufferList == Nbl &&
            Adapter->RxBuffers[Index].Indicated)
        {
            Adapter->RxBuffers[Index].Indicated = FALSE;
            JhGmacRearmRxDescriptor(Adapter, Index);
            if (Adapter->DatapathReady)
                JhGmacKickRx(Adapter);
        }
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
}

static VOID NTAPI
JhGmacCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN NTAPI
JhGmacCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    return FALSE;
}

static NDIS_STATUS NTAPI
JhGmacReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    if (AddressingReset)
        *AddressingReset = FALSE;

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
JhGmacOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return JhGmacQueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return JhGmacSetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID NTAPI
JhGmacCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID NTAPI
JhGmacDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ struct _NET_DEVICE_PNP_EVENT *NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID NTAPI
JhGmacShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PJHGMAC_ADAPTER Adapter = (PJHGMAC_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    if (Adapter)
        JhGmacStopHardware(Adapter);
}

static VOID NTAPI
JhGmacUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (JhGmacDriverHandle)
    {
        NdisMDeregisterMiniportDriver(JhGmacDriverHandle);
        JhGmacDriverHandle = NULL;
    }
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 30;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;
    Chars.InitializeHandlerEx = JhGmacInitializeEx;
    Chars.HaltHandlerEx = JhGmacHaltEx;
    Chars.UnloadHandler = JhGmacUnload;
    Chars.PauseHandler = JhGmacPause;
    Chars.RestartHandler = JhGmacRestart;
    Chars.OidRequestHandler = JhGmacOidRequest;
    Chars.SendNetBufferListsHandler = JhGmacSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = JhGmacReturnNetBufferLists;
    Chars.CancelSendHandler = JhGmacCancelSend;
    Chars.CheckForHangHandlerEx = JhGmacCheckForHang;
    Chars.ResetHandlerEx = JhGmacReset;
    Chars.DevicePnPEventNotifyHandler = JhGmacDevicePnPEventNotify;
    Chars.ShutdownHandlerEx = JhGmacShutdown;
    Chars.CancelOidRequestHandler = JhGmacCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Chars,
                                       &JhGmacDriverHandle);
}
