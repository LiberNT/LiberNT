/*
 * PROJECT:     LiberNT SpacemiT K1 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 EMAC NDIS 6.30 miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "k1xemac.h"

static NDIS_HANDLE K1xEmacDriverHandle;

static const struct
{
    ULONG64 RegisterBase;
    ULONG ControlRegister;
    ULONG DelayLineRegister;
    ULONG InterruptSource;
    ULONG FirstDataPin;
    ULONG ReferenceClockPin;
} K1xEmacPorts[] =
{
    { 0xCAC80000ULL, 0x3e4, 0x3e8, 131, 0, 45 },
    { 0xCAC81000ULL, 0x3ec, 0x3f0, 133, 29, 46 },
};

static const NDIS_OID K1xEmacSupportedOids[] =
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

static __inline ULONG
K1xEmacRead32(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Offset));
}

static __inline VOID
K1xEmacWrite32(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Offset), Value);
}

static VOID
K1xEmacCacheClean(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Base & ~(ULONG_PTR)(K1XEMAC_CACHE_LINE - 1);
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    KeMemoryBarrier();
    for (; Address < End; Address += K1XEMAC_CACHE_LINE)
        __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 1" :: "r"(Address) : "memory");
    KeMemoryBarrier();
}

static VOID
K1xEmacCacheFlush(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Base & ~(ULONG_PTR)(K1XEMAC_CACHE_LINE - 1);
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    KeMemoryBarrier();
    for (; Address < End; Address += K1XEMAC_CACHE_LINE)
        __asm__ __volatile__(".insn i 0x0f, 2, x0, %0, 2" :: "r"(Address) : "memory");
    KeMemoryBarrier();
}

static BOOLEAN
K1xEmacIsValidMacAddress(
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
K1xEmacAllocateMdl(
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PMDL Mdl;

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl)
        MmBuildMdlForNonPagedPool(Mdl);

    return Mdl;
}

static VOID
K1xEmacRearmRxDescriptor(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PK1XEMAC_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];

    K1xEmacCacheFlush(Adapter->RxBuffers[Index].VirtualAddress, K1XEMAC_BUFFER_SIZE);
    Descriptor->Buffer2 = 0;
    Descriptor->Buffer1 = Adapter->RxBuffers[Index].PhysicalAddress.LowPart;
    Descriptor->Control = K1XEMAC_BUFFER_SIZE |
                          ((Index == (K1XEMAC_RX_RING_SIZE - 1)) ? EMAC_DESC_END_OF_RING : 0);
    KeMemoryBarrier();
    Descriptor->Status = EMAC_DESC_OWN;
    K1xEmacCacheClean(Descriptor, sizeof(*Descriptor));
}

static ULONG
K1xEmacReadRxStatus(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PK1XEMAC_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];

    K1xEmacCacheFlush(Descriptor, sizeof(*Descriptor));
    return Descriptor->Status;
}

static ULONG
K1xEmacHashCrc(
    _In_reads_(ETH_LENGTH_OF_ADDRESS) const UCHAR *MacAddress)
{
    ULONG Crc = 0xffffffff;
    ULONG Byte, Bit;

    for (Byte = 0; Byte < ETH_LENGTH_OF_ADDRESS; Byte++)
    {
        for (Bit = 0; Bit < 8; Bit++)
        {
            ULONG Carry = ((Crc >> 31) ^ (MacAddress[Byte] >> Bit)) & 1;

            Crc <<= 1;
            if (Carry)
                Crc ^= 0x04c11db7;
        }
    }

    return Crc;
}

static VOID
K1xEmacApplyPacketFilter(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    ULONG Hash[4] = { 0, 0, 0, 0 };
    ULONG AddressControl = EMAC_MAC_ADDRESS1_ENABLE;
    ULONG i;

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
        AddressControl |= EMAC_MAC_PROMISCUOUS;

    if (Adapter->PacketFilter & (NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_PROMISCUOUS))
    {
        for (i = 0; i < 4; i++)
            Hash[i] = 0xffff;
    }
    else if (Adapter->PacketFilter & NDIS_PACKET_TYPE_MULTICAST)
    {
        for (i = 0; i < Adapter->MulticastCount; i++)
        {
            ULONG Value = K1xEmacHashCrc(Adapter->MulticastList[i]) >> 26;

            Hash[Value / 16] |= 1u << (Value % 16);
        }
    }

    for (i = 0; i < 4; i++)
        K1xEmacWrite32(Adapter, EMAC_MAC_HASH_TABLE1 + i * sizeof(ULONG), Hash[i]);
    K1xEmacWrite32(Adapter, EMAC_MAC_ADDRESS_CONTROL, AddressControl);
}

static VOID
K1xEmacApplyLinkState(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    ULONG Control;

    if (Adapter->MediaConnectState != MediaConnectStateConnected)
        return;

    Control = K1xEmacRead32(Adapter, EMAC_MAC_GLOBAL_CONTROL);
    Control &= ~(EMAC_MAC_GLOBAL_SPEED_MASK | EMAC_MAC_GLOBAL_FULL_DUPLEX);
    if (Adapter->LinkSpeed == K1XEMAC_LINK_SPEED_1G)
        Control |= EMAC_MAC_GLOBAL_SPEED_1000;
    else if (Adapter->LinkSpeed == K1XEMAC_LINK_SPEED_100M)
        Control |= EMAC_MAC_GLOBAL_SPEED_100;
    if (Adapter->MediaDuplexState == MediaDuplexStateFull)
        Control |= EMAC_MAC_GLOBAL_FULL_DUPLEX;
    K1xEmacWrite32(Adapter, EMAC_MAC_GLOBAL_CONTROL, Control);
}

static VOID
K1xEmacWriteMacAddress(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    const UCHAR *Mac = Adapter->CurrentMacAddress;

    K1xEmacWrite32(Adapter, EMAC_MAC_ADDRESS1_HIGH, ((ULONG)Mac[1] << 8) | Mac[0]);
    K1xEmacWrite32(Adapter, EMAC_MAC_ADDRESS1_MED, ((ULONG)Mac[3] << 8) | Mac[2]);
    K1xEmacWrite32(Adapter, EMAC_MAC_ADDRESS1_LOW, ((ULONG)Mac[5] << 8) | Mac[4]);
}

static VOID
K1xEmacEnableInterrupts(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    K1xEmacWrite32(Adapter, EMAC_DMA_INTERRUPT_ENABLE, K1XEMAC_INT_MASK);
}

static VOID
K1xEmacDisableInterrupts(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    K1xEmacWrite32(Adapter, EMAC_DMA_INTERRUPT_ENABLE, 0);
}

static VOID
K1xEmacStopHardware(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    if (!Adapter->RegisterBase)
        return;

    K1xEmacWrite32(Adapter, EMAC_MAC_INTERRUPT_ENABLE, 0);
    K1xEmacWrite32(Adapter, EMAC_DMA_INTERRUPT_ENABLE, 0);
    K1xEmacWrite32(Adapter, EMAC_MAC_RX_CONTROL, 0);
    K1xEmacWrite32(Adapter, EMAC_MAC_TX_CONTROL, 0);
    K1xEmacWrite32(Adapter, EMAC_DMA_CONTROL, 0);
    K1xEmacWrite32(Adapter, EMAC_MAC_GLOBAL_CONTROL, EMAC_MAC_GLOBAL_RESET_COUNTERS);
    K1xEmacWrite32(Adapter, EMAC_MAC_GLOBAL_CONTROL, 0);
    K1xEmacWrite32(Adapter, EMAC_DMA_STATUS, EMAC_DMA_INT_ALL);
}

static VOID
K1xEmacUnmapResources(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    if (Adapter->ApmuBase)
    {
        MmUnmapIoSpace(Adapter->ApmuBase, K1X_APMU_SPAN);
        Adapter->ApmuBase = NULL;
    }

    if (Adapter->RegisterBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->RegisterBase,
                          Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
    }
}

static VOID
K1xEmacFreeDatapath(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    ULONG i;

    Adapter->DatapathReady = FALSE;

    for (i = 0; i < K1XEMAC_TX_RING_SIZE; i++)
    {
        if (Adapter->TxBuffers[i].VirtualAddress)
        {
            MmFreeContiguousMemorySpecifyCache(Adapter->TxBuffers[i].VirtualAddress,
                                               K1XEMAC_BUFFER_SIZE,
                                               MmCached);
            Adapter->TxBuffers[i].VirtualAddress = NULL;
        }
    }

    for (i = 0; i < K1XEMAC_RX_RING_SIZE; i++)
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
                                               K1XEMAC_BUFFER_SIZE,
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
K1xEmacAllocateDmaMemory(
    _In_ ULONG Length,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Va;

    Low.QuadPart = 0;
    High.QuadPart = K1XEMAC_DMA_LIMIT;
    Boundary.QuadPart = 0;
    Physical->QuadPart = 0;

    Va = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary, MmCached);
    if (!Va)
        return NULL;

    *Physical = MmGetPhysicalAddress(Va);
    if ((ULONG64)Physical->QuadPart + Length - 1 > K1XEMAC_DMA_LIMIT)
    {
        MmFreeContiguousMemorySpecifyCache(Va, Length, MmCached);
        Physical->QuadPart = 0;
        return NULL;
    }

    return Va;
}

static NDIS_STATUS
K1xEmacAllocateReceivePath(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = K1XEMAC_TAG;
    PoolParams.DataSize = 0;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle, &PoolParams);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxRingLength = sizeof(K1XEMAC_DESCRIPTOR) * K1XEMAC_RX_RING_SIZE;
    Adapter->RxRing = K1xEmacAllocateDmaMemory(Adapter->RxRingLength, &Adapter->RxRingPhysical);
    if (!Adapter->RxRing)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->RxRing, Adapter->RxRingLength);
    for (i = 0; i < K1XEMAC_RX_RING_SIZE; i++)
    {
        Adapter->RxBuffers[i].VirtualAddress =
            K1xEmacAllocateDmaMemory(K1XEMAC_BUFFER_SIZE, &Adapter->RxBuffers[i].PhysicalAddress);
        if (!Adapter->RxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].Mdl =
            K1xEmacAllocateMdl(Adapter->RxBuffers[i].VirtualAddress, K1XEMAC_BUFFER_SIZE);
        if (!Adapter->RxBuffers[i].Mdl)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].NetBufferList =
            NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0,
                                                  Adapter->RxBuffers[i].Mdl, 0, 0);
        if (!Adapter->RxBuffers[i].NetBufferList)
            return NDIS_STATUS_RESOURCES;
        Adapter->RxBuffers[i].NetBufferList->SourceHandle = Adapter->MiniportHandle;

        K1xEmacRearmRxDescriptor(Adapter, i);
    }

    Adapter->RxTail = 0;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
K1xEmacAllocateTransmitPath(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    ULONG i;

    Adapter->TxRingLength = sizeof(K1XEMAC_DESCRIPTOR) * K1XEMAC_TX_RING_SIZE;
    Adapter->TxRing = K1xEmacAllocateDmaMemory(Adapter->TxRingLength, &Adapter->TxRingPhysical);
    if (!Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->TxRing, Adapter->TxRingLength);
    for (i = 0; i < K1XEMAC_TX_RING_SIZE; i++)
    {
        Adapter->TxBuffers[i].VirtualAddress =
            K1xEmacAllocateDmaMemory(K1XEMAC_BUFFER_SIZE, &Adapter->TxBuffers[i].PhysicalAddress);
        if (!Adapter->TxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        if (i == (K1XEMAC_TX_RING_SIZE - 1))
            Adapter->TxRing[i].Control = EMAC_DESC_END_OF_RING;
    }
    K1xEmacCacheFlush(Adapter->TxRing, Adapter->TxRingLength);

    Adapter->TxHead = 0;
    Adapter->TxTail = 0;
    Adapter->TxFree = K1XEMAC_TX_RING_SIZE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
K1xEmacInitializeDatapath(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    ULONG Value;

    Status = K1xEmacAllocateReceivePath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = K1xEmacAllocateTransmitPath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    K1xEmacWrite32(Adapter, EMAC_DMA_TX_BASE, Adapter->TxRingPhysical.LowPart);
    K1xEmacWrite32(Adapter, EMAC_DMA_RX_BASE, Adapter->RxRingPhysical.LowPart);

    Value = K1xEmacRead32(Adapter, EMAC_MAC_TX_CONTROL);
    Value &= ~EMAC_MAC_TX_IFG_MASK;
    Value |= EMAC_MAC_TX_ENABLE | EMAC_MAC_TX_AUTO_RETRY;
    K1xEmacWrite32(Adapter, EMAC_MAC_TX_CONTROL, Value);
    K1xEmacWrite32(Adapter, EMAC_DMA_TX_AUTO_POLL, 0);

    Value = K1xEmacRead32(Adapter, EMAC_MAC_RX_CONTROL);
    Value |= EMAC_MAC_RX_ENABLE | EMAC_MAC_RX_STORE_FORWARD;
    K1xEmacWrite32(Adapter, EMAC_MAC_RX_CONTROL, Value);

    K1xEmacWrite32(Adapter, EMAC_DMA_CONTROL, EMAC_DMA_CONTROL_START_TX | EMAC_DMA_CONTROL_START_RX);
    Adapter->DatapathReady = TRUE;
    return NDIS_STATUS_SUCCESS;

Failure:
    K1xEmacFreeDatapath(Adapter);
    return Status;
}

static BOOLEAN
K1xEmacWaitMdioIdle(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < 1000; i++)
    {
        if (!(K1xEmacRead32(Adapter, EMAC_MAC_MDIO_CONTROL) & EMAC_MDIO_START))
            return TRUE;

        KeStallExecutionProcessor(10);
    }

    return FALSE;
}

static NDIS_STATUS
K1xEmacMdioRead(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value)
{
    *Value = 0xffff;
    if (!K1xEmacWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    K1xEmacWrite32(Adapter, EMAC_MAC_MDIO_DATA, 0);
    K1xEmacWrite32(Adapter,
                   EMAC_MAC_MDIO_CONTROL,
                   (PhyAddress & 0x1f) |
                   ((ULONG)(Register & 0x1f) << EMAC_MDIO_REGISTER_SHIFT) |
                   EMAC_MDIO_READ | EMAC_MDIO_START);
    if (!K1xEmacWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    *Value = (USHORT)K1xEmacRead32(Adapter, EMAC_MAC_MDIO_DATA);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
K1xEmacMdioWrite(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _In_ UCHAR Register,
    _In_ USHORT Value)
{
    if (!K1xEmacWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    K1xEmacWrite32(Adapter, EMAC_MAC_MDIO_DATA, Value);
    K1xEmacWrite32(Adapter,
                   EMAC_MAC_MDIO_CONTROL,
                   (PhyAddress & 0x1f) |
                   ((ULONG)(Register & 0x1f) << EMAC_MDIO_REGISTER_SHIFT) |
                   EMAC_MDIO_START);
    return K1xEmacWaitMdioIdle(Adapter) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
}

static BOOLEAN
K1xEmacReadPhyId(
    _In_ PK1XEMAC_ADAPTER Adapter,
    _In_ UCHAR PhyAddress,
    _Out_ PULONG PhyId)
{
    USHORT Id1, Id2;

    *PhyId = 0;
    if (K1xEmacMdioRead(Adapter, PhyAddress, MII_PHYSID1, &Id1) != NDIS_STATUS_SUCCESS ||
        K1xEmacMdioRead(Adapter, PhyAddress, MII_PHYSID2, &Id2) != NDIS_STATUS_SUCCESS)
    {
        return FALSE;
    }

    *PhyId = ((ULONG)Id1 << 16) | Id2;
    return (Id1 != 0x0000 || Id2 != 0x0000) && (Id1 != 0xffff || Id2 != 0xffff);
}

static VOID
K1xEmacProbePhy(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    UCHAR Address;

    if (K1xEmacReadPhyId(Adapter, Adapter->PhyAddress, &Adapter->PhyId))
        return;

    for (Address = 0; Address < 32; Address++)
    {
        if (K1xEmacReadPhyId(Adapter, Address, &Adapter->PhyId))
        {
            Adapter->PhyAddress = Address;
            return;
        }
    }

    Adapter->PhyId = 0;
}

static VOID
K1xEmacConfigurePhy(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    USHORT Value, Wanted;
    BOOLEAN Restart = FALSE;

    if (!Adapter->PhyId)
        return;

    if (Adapter->PhyId == PHY_ID_RTL8211F && Adapter->Rgmii)
    {
        K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, RTL8211F_PAGE_SELECT, RTL8211F_PAGE_RGMII);
        if (K1xEmacMdioRead(Adapter, Adapter->PhyAddress, RTL8211F_TX_DELAY_REGISTER, &Value) == NDIS_STATUS_SUCCESS)
        {
            Wanted = Adapter->PhyTxDelay ? (Value | RTL8211F_TX_DELAY) : (Value & ~RTL8211F_TX_DELAY);
            if (Wanted != Value)
                K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, RTL8211F_TX_DELAY_REGISTER, Wanted);
        }
        if (K1xEmacMdioRead(Adapter, Adapter->PhyAddress, RTL8211F_RX_DELAY_REGISTER, &Value) == NDIS_STATUS_SUCCESS)
        {
            Wanted = Adapter->PhyRxDelay ? (Value | RTL8211F_RX_DELAY) : (Value & ~RTL8211F_RX_DELAY);
            if (Wanted != Value)
                K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, RTL8211F_RX_DELAY_REGISTER, Wanted);
        }
        K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, RTL8211F_PAGE_SELECT, 0);
    }

    if (K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_ADVERTISE, &Value) == NDIS_STATUS_SUCCESS &&
        (Value & (ADVERTISE_ALL | ADVERTISE_CSMA)) != (ADVERTISE_ALL | ADVERTISE_CSMA))
    {
        K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, MII_ADVERTISE, Value | ADVERTISE_ALL | ADVERTISE_CSMA);
        Restart = TRUE;
    }

    if (Adapter->Rgmii &&
        K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_CTRL1000, &Value) == NDIS_STATUS_SUCCESS &&
        !(Value & ADVERTISE_1000FULL))
    {
        K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, MII_CTRL1000, Value | ADVERTISE_1000FULL);
        Restart = TRUE;
    }

    if (K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Value) == NDIS_STATUS_SUCCESS &&
        (Restart || !(Value & BMCR_ANENABLE)))
    {
        K1xEmacMdioWrite(Adapter, Adapter->PhyAddress, MII_BMCR, Value | BMCR_ANENABLE | BMCR_ANRESTART);
    }
}

static BOOLEAN
K1xEmacRefreshLink(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    NDIS_MEDIA_CONNECT_STATE OldConnectState = Adapter->MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE OldDuplexState = Adapter->MediaDuplexState;
    ULONG64 OldLinkSpeed = Adapter->LinkSpeed;
    USHORT Bmcr = 0, Bmsr = 0, Lpa = 0, Stat1000 = 0;
    BOOLEAN LinkChanged;

    if (!Adapter->PhyId ||
        K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Bmcr) != NDIS_STATUS_SUCCESS ||
        (Bmcr & BMCR_ANRESTART) ||
        K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr) != NDIS_STATUS_SUCCESS ||
        K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr) != NDIS_STATUS_SUCCESS ||
        !(Bmsr & BMSR_LSTATUS))
    {
        Adapter->MediaConnectState = MediaConnectStateDisconnected;
        Adapter->MediaDuplexState = MediaDuplexStateUnknown;
        Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    else
    {
        Adapter->MediaConnectState = MediaConnectStateConnected;
        Adapter->MediaDuplexState = MediaDuplexStateHalf;
        Adapter->LinkSpeed = K1XEMAC_LINK_SPEED_10M;

        K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_LPA, &Lpa);
        if (Adapter->Rgmii)
            K1xEmacMdioRead(Adapter, Adapter->PhyAddress, MII_STAT1000, &Stat1000);

        if (Stat1000 & LPA_1000FULL)
        {
            Adapter->LinkSpeed = K1XEMAC_LINK_SPEED_1G;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Stat1000 & LPA_1000HALF)
        {
            Adapter->LinkSpeed = K1XEMAC_LINK_SPEED_1G;
        }
        else if (Lpa & ADVERTISE_100FULL)
        {
            Adapter->LinkSpeed = K1XEMAC_LINK_SPEED_100M;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Lpa & ADVERTISE_100HALF)
        {
            Adapter->LinkSpeed = K1XEMAC_LINK_SPEED_100M;
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
        K1xEmacApplyLinkState(Adapter);
        DPRINT("K1XEMAC: PHY%u link %s speed %I64u duplex %s\n",
                Adapter->PhyAddress,
                (Adapter->MediaConnectState == MediaConnectStateConnected) ? "up" : "down",
                Adapter->LinkSpeed,
                (Adapter->MediaDuplexState == MediaDuplexStateFull) ? "full" : "half");
    }

    return LinkChanged;
}

static VOID
K1xEmacIndicateLinkState(
    _In_ PK1XEMAC_ADAPTER Adapter)
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
K1xEmacDrainTxCompletions(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    if (!Adapter->DatapathReady)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);

    while (Adapter->TxFree < K1XEMAC_TX_RING_SIZE)
    {
        ULONG Index = Adapter->TxTail;
        PK1XEMAC_DESCRIPTOR Descriptor = &Adapter->TxRing[Index];

        K1xEmacCacheFlush(Descriptor, sizeof(*Descriptor));
        if (Descriptor->Status & EMAC_DESC_OWN)
            break;

        Adapter->TxPackets++;
        Adapter->TxBytes += Adapter->TxBuffers[Index].Length;
        Adapter->TxBuffers[Index].Length = 0;
        Adapter->TxTail = (Index + 1) % K1XEMAC_TX_RING_SIZE;
        Adapter->TxFree++;
    }

    NdisReleaseSpinLock(&Adapter->TxLock);
}

static NDIS_STATUS
K1xEmacCopyNetBuffer(
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
K1xEmacReceivePending(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    BOOLEAN Pending;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->RxLock);
    Pending = !Adapter->RxBuffers[Adapter->RxTail].Indicated &&
              !(K1xEmacReadRxStatus(Adapter, Adapter->RxTail) & EMAC_DESC_OWN);
    NdisReleaseSpinLock(&Adapter->RxLock);
    return Pending;
}

static VOID
K1xEmacPollReceive(
    _In_ PK1XEMAC_ADAPTER Adapter,
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
        PK1XEMAC_RX_BUFFER RxBuffer = &Adapter->RxBuffers[Index];
        PNET_BUFFER_LIST Nbl;
        PNET_BUFFER Nb;
        ULONG Status;
        ULONG Length;

        if (RxBuffer->Indicated)
            break;

        Status = K1xEmacReadRxStatus(Adapter, Index);
        if (Status & EMAC_DESC_OWN)
            break;

        Length = Status & EMAC_RX_STATUS_LENGTH_MASK;
        if ((Status & (EMAC_RX_STATUS_FIRST | EMAC_RX_STATUS_LAST)) !=
                (EMAC_RX_STATUS_FIRST | EMAC_RX_STATUS_LAST) ||
            (Status & EMAC_RX_STATUS_ERRORS) ||
            Length < ETH_LENGTH_OF_ADDRESS * 2 + 2 + K1XEMAC_FCS_SIZE ||
            Length > K1XEMAC_FRAME_SIZE + K1XEMAC_FCS_SIZE)
        {
            Adapter->RxErrors++;
            K1xEmacRearmRxDescriptor(Adapter, Index);
            Rearmed = TRUE;
            Adapter->RxTail = (Index + 1) % K1XEMAC_RX_RING_SIZE;
            continue;
        }

        Length -= K1XEMAC_FCS_SIZE;
        K1xEmacCacheFlush(RxBuffer->VirtualAddress, Length);

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
        Adapter->RxTail = (Index + 1) % K1XEMAC_RX_RING_SIZE;
    }

    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Rearmed)
        K1xEmacWrite32(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xff);

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
K1xEmacInterrupt(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)ServiceContext;
    ULONG Status;

    UNREFERENCED_PARAMETER(Interrupt);

    Status = K1xEmacRead32(Adapter, EMAC_DMA_STATUS);
    if (!(Status & EMAC_DMA_INT_ALL))
        return FALSE;

    K1xEmacDisableInterrupts(Adapter);
    K1xEmacWrite32(Adapter, EMAC_DMA_STATUS, Status & EMAC_DMA_INT_ALL);
    Adapter->InterruptCount++;
    InterlockedOr(&Adapter->InterruptPending, (LONG)(Status & EMAC_DMA_INT_ALL));
    KeInsertQueueDpc(&Adapter->InterruptDpc, NULL, NULL);
    return TRUE;
}

static VOID NTAPI
K1xEmacInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)DeferredContext;
    ULONG Pending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    if (!Adapter || !Adapter->DatapathReady)
        return;

    Pending = (ULONG)InterlockedExchange(&Adapter->InterruptPending, 0);
    if (Pending & EMAC_DMA_INT_RX_MISSED)
        Adapter->RxNoBuffer++;

    K1xEmacPollReceive(Adapter, K1XEMAC_RX_BUDGET);
    K1xEmacDrainTxCompletions(Adapter);
    K1xEmacEnableInterrupts(Adapter);

    if (K1xEmacReceivePending(Adapter))
        KeInsertQueueDpc(&Adapter->InterruptDpc, NULL, NULL);
}

static VOID NTAPI
K1xEmacLinkTimer(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (!Adapter || !Adapter->RegisterBase)
        return;

    if (K1xEmacRefreshLink(Adapter))
        K1xEmacIndicateLinkState(Adapter);

    if (Adapter->DatapathReady)
    {
        K1xEmacPollReceive(Adapter, K1XEMAC_RX_BUDGET);
        K1xEmacDrainTxCompletions(Adapter);
    }
}

static VOID
K1xEmacStopLinkTimer(
    _In_ PK1XEMAC_ADAPTER Adapter)
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
K1xEmacReadProperty(
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
K1xEmacReadCell(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    UCHAR Cell[sizeof(ULONG)];
    ULONG Length;

    if (!K1xEmacReadProperty(Key, Name, Cell, sizeof(Cell), &Length) || Length != sizeof(Cell))
        return FALSE;

    *Value = ((ULONG)Cell[0] << 24) | ((ULONG)Cell[1] << 16) | ((ULONG)Cell[2] << 8) | Cell[3];
    return TRUE;
}

static BOOLEAN
K1xEmacHasProperty(
    _In_ HANDLE Key,
    _In_ PCWSTR Name)
{
    UCHAR Unused[4];
    ULONG Length;

    return K1xEmacReadProperty(Key, Name, Unused, sizeof(Unused), &Length);
}

static NDIS_STATUS
K1xEmacReadConfiguration(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    static const UCHAR FallbackAddress[ETH_LENGTH_OF_ADDRESS] = { 0x02, 0x4b, 0x31, 0x00, 0x00, 0x00 };
    CHAR PhyMode[16];
    HANDLE Key = NULL;
    ULONG Length, Port;

    for (Port = 0; Port < RTL_NUMBER_OF(K1xEmacPorts); Port++)
    {
        if (K1xEmacPorts[Port].RegisterBase == (ULONG64)Adapter->RegisterPhysical.QuadPart)
            break;
    }
    if (Port == RTL_NUMBER_OF(K1xEmacPorts))
        return NDIS_STATUS_ADAPTER_NOT_FOUND;

    Adapter->Port = Port;
    Adapter->ControlRegister = K1xEmacPorts[Port].ControlRegister;
    Adapter->DelayLineRegister = K1xEmacPorts[Port].DelayLineRegister;
    if (!Adapter->InterruptSource)
        Adapter->InterruptSource = K1xEmacPorts[Port].InterruptSource;
    Adapter->Rgmii = TRUE;
    Adapter->PhyAddress = 1;

    if (Adapter->PhysicalDeviceObject &&
        !NT_SUCCESS(IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                            PLUGPLAY_REGKEY_DEVICE,
                                            KEY_QUERY_VALUE,
                                            &Key)))
    {
        Key = NULL;
    }

    K1xEmacReadCell(Key, L"ctrl-reg", &Adapter->ControlRegister);
    K1xEmacReadCell(Key, L"dline-reg", &Adapter->DelayLineRegister);
    if (Adapter->ControlRegister > K1X_APMU_SPAN - sizeof(ULONG) ||
        Adapter->DelayLineRegister > K1X_APMU_SPAN - sizeof(ULONG))
    {
        if (Key)
            ZwClose(Key);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    if (K1xEmacReadCell(Key, L"phy-addr", &Length) && Length < 32)
        Adapter->PhyAddress = (UCHAR)Length;

    RtlZeroMemory(PhyMode, sizeof(PhyMode));
    if (K1xEmacReadProperty(Key, L"phy-mode", (PUCHAR)PhyMode, sizeof(PhyMode) - 1, &Length))
    {
        Adapter->Rgmii = (PhyMode[0] == 'r' && PhyMode[1] == 'g');
        Adapter->PhyTxDelay = !strcmp(PhyMode, "rgmii-id") || !strcmp(PhyMode, "rgmii-txid");
        Adapter->PhyRxDelay = !strcmp(PhyMode, "rgmii-id") || !strcmp(PhyMode, "rgmii-rxid");
    }

    Adapter->ReferenceClockFromPhy = K1xEmacHasProperty(Key, L"ref-clock-from-phy");
    Adapter->DelayLineTuning =
        (K1xEmacHasProperty(Key, L"clk_tuning_enable") || K1xEmacHasProperty(Key, L"clk-tuning-enable")) &&
        K1xEmacHasProperty(Key, L"clk-tuning-by-delayline") &&
        K1xEmacReadCell(Key, L"tx-phase", &Adapter->TxPhase) &&
        K1xEmacReadCell(Key, L"rx-phase", &Adapter->RxPhase) &&
        Adapter->TxPhase <= 0xff && Adapter->RxPhase <= 0xff;

    if ((!K1xEmacReadProperty(Key, L"local-mac-address", Adapter->PermanentMacAddress,
                              ETH_LENGTH_OF_ADDRESS, &Length) ||
         Length != ETH_LENGTH_OF_ADDRESS ||
         !K1xEmacIsValidMacAddress(Adapter->PermanentMacAddress)) &&
        (!K1xEmacReadProperty(Key, L"mac-address", Adapter->PermanentMacAddress,
                              ETH_LENGTH_OF_ADDRESS, &Length) ||
         Length != ETH_LENGTH_OF_ADDRESS ||
         !K1xEmacIsValidMacAddress(Adapter->PermanentMacAddress)))
    {
        RtlCopyMemory(Adapter->PermanentMacAddress, FallbackAddress, ETH_LENGTH_OF_ADDRESS);
        Adapter->PermanentMacAddress[ETH_LENGTH_OF_ADDRESS - 1] = (UCHAR)Port;
    }
    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    if (Key)
        ZwClose(Key);
    return NDIS_STATUS_SUCCESS;
}

static VOID
K1xEmacSelectPin(
    _In_ PUCHAR MfprBase,
    _In_ ULONG Gpio)
{
    PULONG Register = (PULONG)(MfprBase + K1X_MFPR_OFFSET(Gpio));

    if ((READ_REGISTER_ULONG(Register) & K1X_MFPR_FUNCTION_MASK) != (K1X_MFPR_GMAC & K1X_MFPR_FUNCTION_MASK))
        WRITE_REGISTER_ULONG(Register, K1X_MFPR_GMAC);
}

static NDIS_STATUS
K1xEmacSelectPins(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS MfprPhysical;
    PUCHAR MfprBase;
    ULONG Pin;

    MfprPhysical.QuadPart = K1X_MFPR_BASE;
    MfprBase = MmMapIoSpace(MfprPhysical, K1X_MFPR_SPAN, MmNonCached);
    if (!MfprBase)
        return NDIS_STATUS_RESOURCES;

    for (Pin = 0; Pin < K1X_GMAC_DATA_PINS; Pin++)
        K1xEmacSelectPin(MfprBase, K1xEmacPorts[Adapter->Port].FirstDataPin + Pin);
    K1xEmacSelectPin(MfprBase, K1xEmacPorts[Adapter->Port].ReferenceClockPin);

    MmUnmapIoSpace(MfprBase, K1X_MFPR_SPAN);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
K1xEmacPowerOn(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS ApmuPhysical;
    PULONG Control, DelayLine;
    NDIS_STATUS Status;
    ULONG Value;

    Status = K1xEmacSelectPins(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    ApmuPhysical.QuadPart = K1X_APMU_BASE;
    Adapter->ApmuBase = MmMapIoSpace(ApmuPhysical, K1X_APMU_SPAN, MmNonCached);
    if (!Adapter->ApmuBase)
        return NDIS_STATUS_RESOURCES;

    Control = (PULONG)(Adapter->ApmuBase + Adapter->ControlRegister);
    DelayLine = (PULONG)(Adapter->ApmuBase + Adapter->DelayLineRegister);

    Value = READ_REGISTER_ULONG(Control);
    Value |= APMU_EMAC_BUS_CLOCK_ENABLE;
    WRITE_REGISTER_ULONG(Control, Value);
    KeStallExecutionProcessor(100);
    Value |= APMU_EMAC_RESET_RELEASE | APMU_EMAC_AXI_SINGLE_ID;
    if (Adapter->Rgmii)
    {
        Value |= APMU_EMAC_RGMII;
        if (Adapter->ReferenceClockFromPhy)
            Value &= ~APMU_EMAC_RGMII_TX_CLOCK_FROM_SOC;
        else
            Value |= APMU_EMAC_RGMII_TX_CLOCK_FROM_SOC;
    }
    else
    {
        Value &= ~APMU_EMAC_RGMII;
        if (Adapter->ReferenceClockFromPhy)
            Value &= ~APMU_EMAC_RMII_REFERENCE_FROM_SOC;
        else
            Value |= APMU_EMAC_RMII_REFERENCE_FROM_SOC;
    }
    WRITE_REGISTER_ULONG(Control, Value);
    KeStallExecutionProcessor(1000);

    if (Adapter->Rgmii && Adapter->DelayLineTuning)
    {
        Value = READ_REGISTER_ULONG(DelayLine);
        Value &= ~(APMU_DLINE_TX_CODE_MASK | APMU_DLINE_RX_CODE_MASK);
        Value |= (Adapter->TxPhase << APMU_DLINE_TX_CODE_SHIFT) | APMU_DLINE_TX_ENABLE |
                 (Adapter->RxPhase << APMU_DLINE_RX_CODE_SHIFT) | APMU_DLINE_RX_ENABLE;
        WRITE_REGISTER_ULONG(DelayLine, Value);
    }

    DPRINT("K1XEMAC: control 0x%08lx delay line 0x%08lx\n",
            READ_REGISTER_ULONG(Control),
            READ_REGISTER_ULONG(DelayLine));
    return NDIS_STATUS_SUCCESS;
}

static VOID
K1xEmacInitializeHardware(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    K1xEmacStopHardware(Adapter);

    K1xEmacWrite32(Adapter, EMAC_MAC_TX_FIFO_ALMOST_FULL, EMAC_TX_FIFO_ALMOST_FULL_DEFAULT);
    K1xEmacWrite32(Adapter, EMAC_MAC_TX_START_THRESHOLD, EMAC_TX_STORE_FORWARD);
    K1xEmacWrite32(Adapter, EMAC_MAC_RX_START_THRESHOLD, EMAC_RX_START_DEFAULT);
    K1xEmacWrite32(Adapter, EMAC_MAC_FLOW_CONTROL, EMAC_MAC_FLOW_DECODE);

    K1xEmacWrite32(Adapter, EMAC_DMA_CONFIGURATION, EMAC_DMA_CONFIGURATION_RESET);
    KeStallExecutionProcessor(10000);
    K1xEmacWrite32(Adapter, EMAC_DMA_CONFIGURATION, 0);
    KeStallExecutionProcessor(10000);
    K1xEmacWrite32(Adapter,
                   EMAC_DMA_CONFIGURATION,
                   EMAC_DMA_CONFIGURATION_STRICT_BURST |
                   EMAC_DMA_CONFIGURATION_64BIT |
                   EMAC_DMA_CONFIGURATION_BURST_16 |
                   EMAC_DMA_CONFIGURATION_SKIP(K1XEMAC_DESCRIPTOR_SKIP_WORDS));

    K1xEmacWriteMacAddress(Adapter);
    K1xEmacApplyPacketFilter(Adapter);
}

static NDIS_STATUS
K1xEmacSetRegistrationAttributes(
    _In_ PK1XEMAC_ADAPTER Adapter)
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
K1xEmacSetGeneralAttributes(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;

    RtlZeroMemory(&GenAttrs, sizeof(GenAttrs));
    GenAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = NdisPhysicalMedium802_3;
    GenAttrs.MtuSize = K1XEMAC_MTU;
    GenAttrs.MaxXmitLinkSpeed = K1XEMAC_LINK_SPEED_1G;
    GenAttrs.MaxRcvLinkSpeed = K1XEMAC_LINK_SPEED_1G;
    GenAttrs.XmitLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.RcvLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.MediaConnectState = Adapter->MediaConnectState;
    GenAttrs.MediaDuplexState = Adapter->MediaDuplexState;
    GenAttrs.LookaheadSize = Adapter->Lookahead;
    GenAttrs.MacOptions = K1XEMAC_MAC_OPTIONS;
    GenAttrs.SupportedPacketFilters = K1XEMAC_SUPPORTED_FILTERS;
    GenAttrs.MaxMulticastListSize = K1XEMAC_MAX_MULTICAST;
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(GenAttrs.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(GenAttrs.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    GenAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
    GenAttrs.IfConnectorPresent = TRUE;
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    GenAttrs.SupportedOidList = (PNDIS_OID)K1xEmacSupportedOids;
    GenAttrs.SupportedOidListLength = sizeof(K1xEmacSupportedOids);
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
K1xEmacMapResources(
    _In_ PK1XEMAC_ADAPTER Adapter,
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

    return Adapter->RegisterBase ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static NDIS_STATUS
K1xEmacConnectInterrupt(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    KAFFINITY Affinity = 0;
    KIRQL Irql = 0;
    ULONG Vector;
    NTSTATUS Status;

    KeInitializeDpc(&Adapter->InterruptDpc, K1xEmacInterruptDpc, Adapter);
    Vector = HalGetInterruptVector(Internal,
                                   0,
                                   Adapter->InterruptSource,
                                   Adapter->InterruptSource,
                                   &Irql,
                                   &Affinity);
    if (!Vector)
        return NDIS_STATUS_RESOURCES;

    Status = IoConnectInterrupt(&Adapter->InterruptObject,
                                K1xEmacInterrupt,
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
K1xEmacDisconnectInterrupt(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    if (Adapter->InterruptObject)
    {
        K1xEmacDisableInterrupts(Adapter);
        IoDisconnectInterrupt(Adapter->InterruptObject);
        Adapter->InterruptObject = NULL;
        KeRemoveQueueDpc(&Adapter->InterruptDpc);
        KeFlushQueuedDpcs();
    }
}

static NDIS_STATUS
K1xEmacCopyQuery(
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
K1xEmacQueryInformation(
    _In_ PK1XEMAC_ADAPTER Adapter,
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
            return K1xEmacCopyQuery(OidRequest, K1xEmacSupportedOids, sizeof(K1xEmacSupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return K1xEmacCopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return K1xEmacCopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return K1xEmacCopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = K1XEMAC_MTU;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = K1XEMAC_FRAME_SIZE;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "SpacemiT K1 Ethernet";
            return K1xEmacCopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = K1XEMAC_DRIVER_VERSION;
            return K1xEmacCopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = K1XEMAC_MAC_OPTIONS;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = (Adapter->MediaConnectState == MediaConnectStateConnected) ?
                              NdisMediaStateConnected :
                              NdisMediaStateDisconnected;
            return K1xEmacCopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            return K1xEmacCopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->TxErrors;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return K1xEmacCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return K1xEmacCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return K1xEmacCopyQuery(OidRequest,
                                    Adapter->MulticastList,
                                    Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = K1XEMAC_MAX_MULTICAST;
            return K1xEmacCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS
K1xEmacSetInformation(
    _In_ PK1XEMAC_ADAPTER Adapter,
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
            K1xEmacApplyPacketFilter(Adapter);
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->Lookahead = min(*(PULONG)InfoBuffer, K1XEMAC_MTU);
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
            K1xEmacApplyPacketFilter(Adapter);
            *BytesRead = InfoBufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID
K1xEmacFreeAdapter(
    _In_ PK1XEMAC_ADAPTER Adapter)
{
    K1xEmacDisconnectInterrupt(Adapter);
    K1xEmacStopHardware(Adapter);
    K1xEmacFreeDatapath(Adapter);
    K1xEmacUnmapResources(Adapter);
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    ExFreePoolWithTag(Adapter, K1XEMAC_TAG);
}

static NDIS_STATUS NTAPI
K1xEmacInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PK1XEMAC_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), K1XEMAC_TAG);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = K1XEMAC_MTU;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->TxLock);

    Status = K1xEmacSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,
                           NULL,
                           NULL,
                           NULL);

    Status = K1xEmacMapResources(Adapter,
                                 (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = K1xEmacReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = K1xEmacPowerOn(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    K1xEmacInitializeHardware(Adapter);
    K1xEmacProbePhy(Adapter);
    K1xEmacConfigurePhy(Adapter);
    DPRINT("K1XEMAC: MMIO 0x%I64x irq %lu PHY%u id 0x%08lx MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
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
    K1xEmacRefreshLink(Adapter);

    Status = K1xEmacInitializeDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = K1xEmacConnectInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = K1xEmacSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    K1xEmacWrite32(Adapter, EMAC_DMA_STATUS, EMAC_DMA_INT_ALL);
    K1xEmacEnableInterrupts(Adapter);
    K1xEmacWrite32(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xff);

    NdisMInitializeTimer(&Adapter->LinkTimer, Adapter->MiniportHandle, K1xEmacLinkTimer, Adapter);
    Adapter->LinkTimerInitialized = TRUE;
    NdisMSetPeriodicTimer(&Adapter->LinkTimer, 1000);
    return NDIS_STATUS_SUCCESS;

Failure:
    DPRINT1("K1XEMAC: initialization failed 0x%08x\n", Status);
    K1xEmacFreeAdapter(Adapter);
    return Status;
}

static VOID NTAPI
K1xEmacHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    if (!Adapter)
        return;

    K1xEmacStopLinkTimer(Adapter);
    K1xEmacFreeAdapter(Adapter);
}

static NDIS_STATUS NTAPI
K1xEmacPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
K1xEmacRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
K1xEmacSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER_LIST FailHead = NULL;
    PNET_BUFFER_LIST FailTail = NULL;
    PNET_BUFFER_LIST OkHead = NULL;
    PNET_BUFFER_LIST OkTail = NULL;
    ULONG CompleteFlags = 0;
    BOOLEAN KickTx = FALSE;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;

    K1xEmacDrainTxCompletions(Adapter);

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
            if (NET_BUFFER_DATA_LENGTH(NetBuffer) > K1XEMAC_FRAME_SIZE ||
                NET_BUFFER_DATA_LENGTH(NetBuffer) < ETH_LENGTH_OF_ADDRESS)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                goto FailNbl;
            }

            NetBufferCount++;
            if (NetBufferCount > K1XEMAC_TX_RING_SIZE)
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
            Index = (StartIndex + Offset) % K1XEMAC_TX_RING_SIZE;
            Status = K1xEmacCopyNetBuffer(NetBuffer,
                                          Adapter->TxBuffers[Index].VirtualAddress,
                                          K1XEMAC_BUFFER_SIZE,
                                          &Length);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                while (Offset > 0)
                {
                    Offset--;
                    Index = (StartIndex + Offset) % K1XEMAC_TX_RING_SIZE;
                    Adapter->TxBuffers[Index].Length = 0;
                }
                NdisReleaseSpinLock(&Adapter->TxLock);
                goto FailNbl;
            }

            Adapter->TxBuffers[Index].Length = Length;
        }

        for (Offset = 0; Offset < NetBufferCount; Offset++)
        {
            PK1XEMAC_DESCRIPTOR Descriptor;

            Index = (StartIndex + Offset) % K1XEMAC_TX_RING_SIZE;
            Descriptor = &Adapter->TxRing[Index];
            Length = Adapter->TxBuffers[Index].Length;
            K1xEmacCacheClean(Adapter->TxBuffers[Index].VirtualAddress, Length);

            Descriptor->Buffer2 = 0;
            Descriptor->Buffer1 = Adapter->TxBuffers[Index].PhysicalAddress.LowPart;
            Descriptor->Control = Length |
                                  EMAC_TX_CONTROL_FIRST |
                                  EMAC_TX_CONTROL_LAST |
                                  EMAC_TX_CONTROL_INTERRUPT |
                                  ((Index == (K1XEMAC_TX_RING_SIZE - 1)) ? EMAC_DESC_END_OF_RING : 0);
            KeMemoryBarrier();
            Descriptor->Status = EMAC_DESC_OWN;
            K1xEmacCacheClean(Descriptor, sizeof(*Descriptor));
        }

        Adapter->TxHead = (StartIndex + NetBufferCount) % K1XEMAC_TX_RING_SIZE;
        Adapter->TxFree -= NetBufferCount;

        NdisReleaseSpinLock(&Adapter->TxLock);

        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        if (!OkHead)
            OkHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(OkTail) = Nbl;
        OkTail = Nbl;
        KickTx = TRUE;
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

    if (KickTx)
        K1xEmacWrite32(Adapter, EMAC_DMA_TX_POLL_DEMAND, 0xff);

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
K1xEmacReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;
    BOOLEAN Rearmed = FALSE;

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
        if (Index < K1XEMAC_RX_RING_SIZE &&
            Adapter->RxBuffers[Index].NetBufferList == Nbl &&
            Adapter->RxBuffers[Index].Indicated)
        {
            Adapter->RxBuffers[Index].Indicated = FALSE;
            K1xEmacRearmRxDescriptor(Adapter, Index);
            Rearmed = TRUE;
        }
        NdisReleaseSpinLock(&Adapter->RxLock);
    }

    if (Rearmed && Adapter->DatapathReady)
        K1xEmacWrite32(Adapter, EMAC_DMA_RX_POLL_DEMAND, 0xff);
}

static VOID NTAPI
K1xEmacCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN NTAPI
K1xEmacCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    return FALSE;
}

static NDIS_STATUS NTAPI
K1xEmacReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    if (AddressingReset)
        *AddressingReset = FALSE;

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
K1xEmacOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return K1xEmacQueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return K1xEmacSetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID NTAPI
K1xEmacCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID NTAPI
K1xEmacDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ struct _NET_DEVICE_PNP_EVENT *NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID NTAPI
K1xEmacShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PK1XEMAC_ADAPTER Adapter = (PK1XEMAC_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    if (Adapter)
        K1xEmacStopHardware(Adapter);
}

static VOID NTAPI
K1xEmacUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (K1xEmacDriverHandle)
    {
        NdisMDeregisterMiniportDriver(K1xEmacDriverHandle);
        K1xEmacDriverHandle = NULL;
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
    Chars.InitializeHandlerEx = K1xEmacInitializeEx;
    Chars.HaltHandlerEx = K1xEmacHaltEx;
    Chars.UnloadHandler = K1xEmacUnload;
    Chars.PauseHandler = K1xEmacPause;
    Chars.RestartHandler = K1xEmacRestart;
    Chars.OidRequestHandler = K1xEmacOidRequest;
    Chars.SendNetBufferListsHandler = K1xEmacSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = K1xEmacReturnNetBufferLists;
    Chars.CancelSendHandler = K1xEmacCancelSend;
    Chars.CheckForHangHandlerEx = K1xEmacCheckForHang;
    Chars.ResetHandlerEx = K1xEmacReset;
    Chars.DevicePnPEventNotifyHandler = K1xEmacDevicePnPEventNotify;
    Chars.ShutdownHandlerEx = K1xEmacShutdown;
    Chars.CancelOidRequestHandler = K1xEmacCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Chars,
                                       &K1xEmacDriverHandle);
}
