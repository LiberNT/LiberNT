/*
 * PROJECT:     LiberNT VMXNET3 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 miniport for the VMware VMXNET3 virtual Ethernet adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vmx3.h"

static NDIS_HANDLE Vmx3DriverHandle;

static const NDIS_OID Vmx3SupportedOids[] =
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
Vmx3ReadVd(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Adapter->Vd + Offset));
}

static __inline VOID
Vmx3WriteVd(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Adapter->Vd + Offset), Value);
}

static __inline VOID
Vmx3WritePt(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Adapter->Pt + Offset), Value);
}

static ULONG
Vmx3Command(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ ULONG Command)
{
    ULONG Result;

    NdisAcquireSpinLock(&Adapter->CommandLock);
    Vmx3WriteVd(Adapter, VMX3_VD_CMD, Command);
    Result = Vmx3ReadVd(Adapter, VMX3_VD_CMD);
    NdisReleaseSpinLock(&Adapter->CommandLock);
    return Result;
}

static PVOID
Vmx3AllocateContiguous(
    _In_ SIZE_T Length,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID VirtualAddress;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;

    VirtualAddress = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary, CacheType);
    Physical->QuadPart = VirtualAddress ? MmGetPhysicalAddress(VirtualAddress).QuadPart : 0;
    return VirtualAddress;
}

static ULONG
Vmx3RxMode(
    _In_ ULONG PacketFilter)
{
    ULONG Mode = 0;

    if (PacketFilter & NDIS_PACKET_TYPE_DIRECTED)
        Mode |= VMX3_RXM_UCAST;
    if (PacketFilter & NDIS_PACKET_TYPE_BROADCAST)
        Mode |= VMX3_RXM_BCAST;
    if (PacketFilter & (NDIS_PACKET_TYPE_MULTICAST | NDIS_PACKET_TYPE_ALL_MULTICAST))
        Mode |= VMX3_RXM_ALL_MULTI;
    if (PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
        Mode |= VMX3_RXM_PROMISC | VMX3_RXM_UCAST | VMX3_RXM_BCAST | VMX3_RXM_ALL_MULTI;

    return Mode;
}

static VOID
Vmx3ApplyRxMode(
    _In_ PVMX3_ADAPTER Adapter)
{
    Adapter->Shared->RxMode = Vmx3RxMode(Adapter->PacketFilter);
    KeMemoryBarrier();
    if (Adapter->Activated)
        Vmx3Command(Adapter, VMX3_CMD_UPDATE_RX_MODE);
}

static VOID
Vmx3IndicateLinkState(
    _In_ PVMX3_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION StatusIndication;
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Adapter->MediaConnectState;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
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
Vmx3UpdateLink(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ BOOLEAN Indicate)
{
    ULONG State = Vmx3Command(Adapter, VMX3_CMD_GET_LINK);
    NDIS_MEDIA_CONNECT_STATE ConnectState;
    ULONG64 LinkSpeed;

    if (State & 1)
    {
        ConnectState = MediaConnectStateConnected;
        LinkSpeed = (ULONG64)(State >> 16) * 1000000ULL;
    }
    else
    {
        ConnectState = MediaConnectStateDisconnected;
        LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }

    if (ConnectState == Adapter->MediaConnectState && LinkSpeed == Adapter->LinkSpeed)
        return;

    Adapter->MediaConnectState = ConnectState;
    Adapter->LinkSpeed = LinkSpeed;
    if (Indicate)
        Vmx3IndicateLinkState(Adapter);
}

static VOID
Vmx3EnableInterrupts(
    _In_ PVMX3_ADAPTER Adapter)
{
    Adapter->Shared->IntrCtrl &= ~VMX3_IC_DISABLE_ALL;
    KeMemoryBarrier();
    Vmx3WritePt(Adapter, VMX3_PT_IMR, 0);
}

static VOID
Vmx3DisableInterrupts(
    _In_ PVMX3_ADAPTER Adapter)
{
    Adapter->Shared->IntrCtrl |= VMX3_IC_DISABLE_ALL;
    KeMemoryBarrier();
    Vmx3WritePt(Adapter, VMX3_PT_IMR, 1);
}

static VOID
Vmx3PostReceive(
    _In_ PVMX3_ADAPTER Adapter)
{
    ULONG Slot = Adapter->RxProduced % VMX3_RX_RING_SIZE;
    PVMX3_RX_DESC Descriptor = &Adapter->RxRing[Slot];
    ULONG Generation = ((Adapter->RxProduced / VMX3_RX_RING_SIZE) & 1) ? 0 : VMX3_RXD_GEN;

    Descriptor->Address = (ULONG64)Adapter->Rx[Slot].Buffer.PhysicalAddress.QuadPart;
    Descriptor->Reserved = 0;
    KeMemoryBarrier();
    Descriptor->Flags = Generation | VMX3_BUFFER_SIZE;
    Adapter->RxProduced++;
}

static BOOLEAN
Vmx3ReceivePending(
    _In_ PVMX3_ADAPTER Adapter)
{
    PVMX3_RX_COMP Completion = &Adapter->RxComp[Adapter->RxCompleted % VMX3_RX_RING_SIZE];
    ULONG Generation = ((Adapter->RxCompleted / VMX3_RX_RING_SIZE) & 1) ? 0 : VMX3_RXCD_GEN;

    return (*(volatile ULONG *)&Completion->Flags & VMX3_RXCD_GEN) == Generation;
}

static BOOLEAN
Vmx3IsValidReceive(
    _In_ ULONG IndexWord,
    _In_ ULONG LengthWord)
{
    ULONG Frame = LengthWord & VMX3_RXCD_LENGTH_MASK;

    if ((IndexWord & VMX3_RXCD_INDEX_MASK) >= VMX3_RX_RING_SIZE)
        return FALSE;
    if ((IndexWord & (VMX3_RXCD_SOP | VMX3_RXCD_EOP)) != (VMX3_RXCD_SOP | VMX3_RXCD_EOP))
        return FALSE;
    if (LengthWord & VMX3_RXCD_ERROR)
        return FALSE;

    return Frame >= (ETH_LENGTH_OF_ADDRESS * 2 + 2) && Frame <= VMX3_BUFFER_SIZE;
}

static VOID
Vmx3ProcessReceives(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ ULONG Budget)
{
    PNET_BUFFER_LIST Head = NULL;
    PNET_BUFFER_LIST Tail = NULL;
    ULONG Indicated = 0;
    ULONG Consumed = 0;
    ULONG Flags;

    NdisAcquireSpinLock(&Adapter->RxLock);
    while (Indicated < Budget && Vmx3ReceivePending(Adapter))
    {
        PVMX3_RX_COMP Completion = &Adapter->RxComp[Adapter->RxCompleted % VMX3_RX_RING_SIZE];
        ULONG Index;
        ULONG LengthWord;
        PVMX3_RX_SLOT Slot;
        PNET_BUFFER NetBuffer;

        KeMemoryBarrier();
        Index = Completion->Index;
        LengthWord = Completion->Length;
        Adapter->RxCompleted++;
        Consumed++;

        if (Adapter->Paused || !Adapter->PacketFilter)
            continue;

        if (!Vmx3IsValidReceive(Index, LengthWord))
        {
            Adapter->RxErrors++;
            continue;
        }

        Slot = &Adapter->Rx[Index & VMX3_RXCD_INDEX_MASK];
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Slot->NetBufferList);
        NET_BUFFER_CURRENT_MDL(NetBuffer) = Slot->Mdl;
        NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) = 0;
        NET_BUFFER_DATA_OFFSET(NetBuffer) = 0;
        NET_BUFFER_DATA_LENGTH(NetBuffer) = LengthWord & VMX3_RXCD_LENGTH_MASK;
        NET_BUFFER_LIST_STATUS(Slot->NetBufferList) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_NEXT_NBL(Slot->NetBufferList) = NULL;
        Slot->NetBufferList->SourceHandle = Adapter->MiniportHandle;

        if (Tail)
            NET_BUFFER_LIST_NEXT_NBL(Tail) = Slot->NetBufferList;
        else
            Head = Slot->NetBufferList;
        Tail = Slot->NetBufferList;
        Indicated++;
        Adapter->RxPackets++;
    }
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Head)
    {
        Flags = NDIS_RECEIVE_FLAGS_RESOURCES;
        if (KeGetCurrentIrql() == DISPATCH_LEVEL)
            Flags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle,
                                           Head,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           Indicated,
                                           Flags);
    }

    if (Consumed)
    {
        NdisAcquireSpinLock(&Adapter->RxLock);
        while (Consumed--)
            Vmx3PostReceive(Adapter);
        KeMemoryBarrier();
        Vmx3WritePt(Adapter, VMX3_PT_RXPROD, Adapter->RxProduced % VMX3_RX_RING_SIZE);
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
}

static VOID
Vmx3CompleteTransmits(
    _In_ PVMX3_ADAPTER Adapter)
{
    NdisAcquireSpinLock(&Adapter->TxLock);
    while (Adapter->TxCompleted != Adapter->TxProduced)
    {
        PVMX3_TX_COMP Completion = &Adapter->TxComp[Adapter->TxCompleted % VMX3_TX_RING_SIZE];
        ULONG Generation = ((Adapter->TxCompleted / VMX3_TX_RING_SIZE) & 1) ? 0 : VMX3_TXCD_GEN;

        if ((*(volatile ULONG *)&Completion->Flags & VMX3_TXCD_GEN) != Generation)
            break;

        Adapter->TxCompleted++;
        Adapter->TxPackets++;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);
}

static NDIS_STATUS
Vmx3CopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(DestinationLength) PUCHAR Destination,
    _In_ ULONG DestinationLength,
    _Out_ PULONG BytesCopied)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Copied = 0;

    *BytesCopied = 0;
    if (Remaining > DestinationLength)
        return NDIS_STATUS_INVALID_LENGTH;

    while (Remaining)
    {
        PUCHAR Source;
        ULONG Chunk;

        if (!Mdl)
            return NDIS_STATUS_INVALID_PACKET;

        if (Offset >= MmGetMdlByteCount(Mdl))
        {
            Offset -= MmGetMdlByteCount(Mdl);
            Mdl = Mdl->Next;
            continue;
        }

        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!Source)
            return NDIS_STATUS_RESOURCES;

        Chunk = min(MmGetMdlByteCount(Mdl) - Offset, Remaining);
        RtlCopyMemory(Destination + Copied, Source + Offset, Chunk);
        Copied += Chunk;
        Remaining -= Chunk;
        Offset = 0;
        Mdl = Mdl->Next;
    }

    *BytesCopied = Copied;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Vmx3QueueNetBufferList(
    _In_ PVMX3_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    PNET_BUFFER NetBuffer;
    ULONG Count = 0;

    if (Adapter->Paused)
        return NDIS_STATUS_PAUSED;
    if (!Adapter->Activated || Adapter->MediaConnectState != MediaConnectStateConnected)
        return NDIS_STATUS_MEDIA_DISCONNECTED;

    for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList); NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
    {
        if (NET_BUFFER_DATA_LENGTH(NetBuffer) < (ETH_LENGTH_OF_ADDRESS * 2 + 2) ||
            NET_BUFFER_DATA_LENGTH(NetBuffer) > VMX3_FRAME_SIZE)
        {
            return NDIS_STATUS_INVALID_LENGTH;
        }
        Count++;
    }

    if (!Count)
        return NDIS_STATUS_INVALID_PACKET;

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (Adapter->TxProduced - Adapter->TxCompleted + Count > VMX3_TX_RING_SIZE - 1)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        return NDIS_STATUS_RESOURCES;
    }

    for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
         NetBuffer && Status == NDIS_STATUS_SUCCESS;
         NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
    {
        ULONG Slot = Adapter->TxProduced % VMX3_TX_RING_SIZE;
        ULONG Length;

        Status = Vmx3CopyNetBuffer(NetBuffer, Adapter->Tx[Slot].VirtualAddress, VMX3_BUFFER_SIZE, &Length);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            PVMX3_TX_DESC Descriptor = &Adapter->TxRing[Slot];
            ULONG Generation = ((Adapter->TxProduced / VMX3_TX_RING_SIZE) & 1) ? 0 : VMX3_TXD_GEN;

            Descriptor->Address = (ULONG64)Adapter->Tx[Slot].PhysicalAddress.QuadPart;
            Descriptor->Flags[1] = VMX3_TXD_CQ | VMX3_TXD_EOP;
            KeMemoryBarrier();
            Descriptor->Flags[0] = Generation | (Length & VMX3_TXD_LENGTH_MASK);
            Adapter->TxProduced++;
        }
    }

    KeMemoryBarrier();
    Vmx3WritePt(Adapter, VMX3_PT_TXPROD, Adapter->TxProduced % VMX3_TX_RING_SIZE);
    NdisReleaseSpinLock(&Adapter->TxLock);
    return Status;
}

static NDIS_STATUS
Vmx3SetRegistrationAttributes(
    _In_ PVMX3_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Attributes;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = sizeof(Attributes);
    Attributes.MiniportAdapterContext = Adapter;
    Attributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                                NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                                NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    Attributes.CheckForHangTimeInSeconds = 4;
    Attributes.InterfaceType = NdisInterfacePci;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static NDIS_STATUS
Vmx3SetGeneralAttributes(
    _In_ PVMX3_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Attributes;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.MediaType = NdisMedium802_3;
    Attributes.PhysicalMediumType = NdisPhysicalMedium802_3;
    Attributes.MtuSize = VMX3_MTU;
    Attributes.MaxXmitLinkSpeed = 10000000000ULL;
    Attributes.MaxRcvLinkSpeed = 10000000000ULL;
    Attributes.XmitLinkSpeed = Adapter->LinkSpeed;
    Attributes.RcvLinkSpeed = Adapter->LinkSpeed;
    Attributes.MediaConnectState = Adapter->MediaConnectState;
    Attributes.MediaDuplexState = MediaDuplexStateFull;
    Attributes.LookaheadSize = Adapter->Lookahead;
    Attributes.MacOptions = VMX3_MAC_OPTIONS;
    Attributes.SupportedPacketFilters = VMX3_SUPPORTED_FILTERS;
    Attributes.MaxMulticastListSize = VMX3_MAX_MULTICAST;
    Attributes.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(Attributes.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(Attributes.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    Attributes.AccessType = NET_IF_ACCESS_BROADCAST;
    Attributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Attributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Attributes.IfType = IF_TYPE_ETHERNET_CSMACD;
    Attributes.IfConnectorPresent = TRUE;
    Attributes.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    Attributes.SupportedOidList = (PNDIS_OID)Vmx3SupportedOids;
    Attributes.SupportedOidListLength = sizeof(Vmx3SupportedOids);
    Attributes.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                     NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                     NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static NDIS_STATUS
Vmx3MapResources(
    _In_ PVMX3_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST ResourceList)
{
    ULONG i;

    if (!ResourceList)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < ResourceList->Count && !Adapter->Vd; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &ResourceList->PartialDescriptors[i];
        NDIS_STATUS Status;

        if (Descriptor->Type != CmResourceTypeMemory)
            continue;

        if (!Adapter->Pt)
        {
            Adapter->PtPhysical = Descriptor->u.Memory.Start;
            Adapter->PtLength = Descriptor->u.Memory.Length;
            Status = NdisMMapIoSpace((PVOID *)&Adapter->Pt, Adapter->MiniportHandle, Adapter->PtPhysical, Adapter->PtLength);
        }
        else
        {
            Adapter->VdPhysical = Descriptor->u.Memory.Start;
            Adapter->VdLength = Descriptor->u.Memory.Length;
            Status = NdisMMapIoSpace((PVOID *)&Adapter->Vd, Adapter->MiniportHandle, Adapter->VdPhysical, Adapter->VdLength);
        }

        if (Status != NDIS_STATUS_SUCCESS)
            return Status;
    }

    if (!Adapter->Pt || !Adapter->Vd ||
        Adapter->PtLength < VMX3_PT_RXPROD + sizeof(ULONG) ||
        Adapter->VdLength < VMX3_VD_ECR + sizeof(ULONG))
    {
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Vmx3ProbeDevice(
    _In_ PVMX3_ADAPTER Adapter)
{
    ULONG Low;
    ULONG High;

    if (!(Vmx3ReadVd(Adapter, VMX3_VD_VRRS) & 1))
        return NDIS_STATUS_NOT_SUPPORTED;
    Vmx3WriteVd(Adapter, VMX3_VD_VRRS, 1);

    if (!(Vmx3ReadVd(Adapter, VMX3_VD_UVRS) & 1))
        return NDIS_STATUS_NOT_SUPPORTED;
    Vmx3WriteVd(Adapter, VMX3_VD_UVRS, 1);

    Vmx3Command(Adapter, VMX3_CMD_RESET_DEV);

    Low = Vmx3ReadVd(Adapter, VMX3_VD_MACL);
    High = Vmx3ReadVd(Adapter, VMX3_VD_MACH);
    Adapter->PermanentMacAddress[0] = (UCHAR)Low;
    Adapter->PermanentMacAddress[1] = (UCHAR)(Low >> 8);
    Adapter->PermanentMacAddress[2] = (UCHAR)(Low >> 16);
    Adapter->PermanentMacAddress[3] = (UCHAR)(Low >> 24);
    Adapter->PermanentMacAddress[4] = (UCHAR)High;
    Adapter->PermanentMacAddress[5] = (UCHAR)(High >> 8);
    RtlCopyMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Vmx3AllocateBuffers(
    _In_ PVOID *Pages,
    _In_ ULONG PageCount,
    _Out_writes_(PageCount * (PAGE_SIZE / VMX3_BUFFER_SIZE)) PVMX3_BUFFER Buffers,
    _In_ SIZE_T BufferStride)
{
    ULONG Page;

    for (Page = 0; Page < PageCount; Page++)
    {
        NDIS_PHYSICAL_ADDRESS Physical;
        ULONG Part;

        Pages[Page] = Vmx3AllocateContiguous(PAGE_SIZE, MmCached, &Physical);
        if (!Pages[Page])
            return NDIS_STATUS_RESOURCES;

        for (Part = 0; Part < PAGE_SIZE / VMX3_BUFFER_SIZE; Part++)
        {
            PVMX3_BUFFER Buffer = (PVMX3_BUFFER)((PUCHAR)Buffers + (Page * (PAGE_SIZE / VMX3_BUFFER_SIZE) + Part) * BufferStride);

            Buffer->VirtualAddress = (PUCHAR)Pages[Page] + Part * VMX3_BUFFER_SIZE;
            Buffer->PhysicalAddress.QuadPart = Physical.QuadPart + Part * VMX3_BUFFER_SIZE;
        }
    }

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Vmx3AllocateDatapath(
    _In_ PVMX3_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    NDIS_STATUS Status;
    ULONG i;

    Adapter->Dma = Vmx3AllocateContiguous(ROUND_TO_PAGES(VMX3_DMA_LENGTH), MmNonCached, &Adapter->DmaPhysical);
    if (!Adapter->Dma)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->Dma, ROUND_TO_PAGES(VMX3_DMA_LENGTH));
    Adapter->TxRing = (PVMX3_TX_DESC)(Adapter->Dma + VMX3_DMA_TX_RING);
    Adapter->TxComp = (PVMX3_TX_COMP)(Adapter->Dma + VMX3_DMA_TX_COMP);
    Adapter->RxRing = (PVMX3_RX_DESC)(Adapter->Dma + VMX3_DMA_RX_RING);
    Adapter->RxComp = (PVMX3_RX_COMP)(Adapter->Dma + VMX3_DMA_RX_COMP);
    Adapter->Queues = (PVMX3_QUEUES)(Adapter->Dma + VMX3_DMA_QUEUES);
    Adapter->Shared = (PVMX3_SHARED)(Adapter->Dma + VMX3_DMA_SHARED);

    Status = Vmx3AllocateBuffers(Adapter->TxPages, ARRAYSIZE(Adapter->TxPages), Adapter->Tx, sizeof(VMX3_BUFFER));
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Vmx3AllocateBuffers(Adapter->RxPages, ARRAYSIZE(Adapter->RxPages), &Adapter->Rx[0].Buffer, sizeof(VMX3_RX_SLOT));
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = sizeof(PoolParameters);
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = VMX3_TAG;
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle, &PoolParameters);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < VMX3_RX_RING_SIZE; i++)
    {
        PVMX3_RX_SLOT Slot = &Adapter->Rx[i];

        Slot->Mdl = IoAllocateMdl(Slot->Buffer.VirtualAddress, VMX3_BUFFER_SIZE, FALSE, FALSE, NULL);
        if (!Slot->Mdl)
            return NDIS_STATUS_RESOURCES;
        MmBuildMdlForNonPagedPool(Slot->Mdl);

        Slot->NetBufferList = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Slot->Mdl, 0, 0);
        if (!Slot->NetBufferList)
            return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static VOID
Vmx3FreeDatapath(
    _In_ PVMX3_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < VMX3_RX_RING_SIZE; i++)
    {
        if (Adapter->Rx[i].NetBufferList)
            NdisFreeNetBufferList(Adapter->Rx[i].NetBufferList);
        if (Adapter->Rx[i].Mdl)
            IoFreeMdl(Adapter->Rx[i].Mdl);
    }

    if (Adapter->RxNblPool)
        NdisFreeNetBufferListPool(Adapter->RxNblPool);

    for (i = 0; i < ARRAYSIZE(Adapter->TxPages); i++)
    {
        if (Adapter->TxPages[i])
            MmFreeContiguousMemorySpecifyCache(Adapter->TxPages[i], PAGE_SIZE, MmCached);
    }

    for (i = 0; i < ARRAYSIZE(Adapter->RxPages); i++)
    {
        if (Adapter->RxPages[i])
            MmFreeContiguousMemorySpecifyCache(Adapter->RxPages[i], PAGE_SIZE, MmCached);
    }

    if (Adapter->Dma)
        MmFreeContiguousMemorySpecifyCache(Adapter->Dma, ROUND_TO_PAGES(VMX3_DMA_LENGTH), MmNonCached);
}

static NDIS_STATUS
Vmx3Activate(
    _In_ PVMX3_ADAPTER Adapter)
{
    PVMX3_QUEUES Queues = Adapter->Queues;
    PVMX3_SHARED Shared = Adapter->Shared;
    ULONG64 Base = (ULONG64)Adapter->DmaPhysical.QuadPart;
    ULONG64 SharedAddress = Base + VMX3_DMA_SHARED;
    ULONG InterruptConfiguration;
    ULONG i;

    Queues->Tx.DescAddress = Base + VMX3_DMA_TX_RING;
    Queues->Tx.CompAddress = Base + VMX3_DMA_TX_COMP;
    Queues->Tx.NumDesc = VMX3_TX_RING_SIZE;
    Queues->Tx.NumComp = VMX3_TX_RING_SIZE;
    Queues->Tx.IntrIndex = 0;
    Queues->Rx.DescAddress[0] = Base + VMX3_DMA_RX_RING;
    Queues->Rx.NumDesc[0] = VMX3_RX_RING_SIZE;
    Queues->Rx.CompAddress = Base + VMX3_DMA_RX_COMP;
    Queues->Rx.NumComp = VMX3_RX_RING_SIZE;
    Queues->Rx.IntrIndex = 0;

    InterruptConfiguration = Vmx3Command(Adapter, VMX3_CMD_GET_CONF_INTR);

    Shared->Magic = VMX3_SHARED_MAGIC;
    Shared->DriverVersion = VMX3_DRIVER_VERSION;
    Shared->GuestInfo = (sizeof(PVOID) == sizeof(ULONG64)) ? VMX3_GOS_BITS_64 : VMX3_GOS_BITS_32;
    Shared->VersionSupport = 1;
    Shared->UptVersionSupport = 1;
    Shared->QueueDescAddress = Base + VMX3_DMA_QUEUES;
    Shared->QueueDescLength = sizeof(VMX3_QUEUES);
    Shared->Mtu = VMX3_MTU;
    Shared->NumTxQueues = 1;
    Shared->NumRxQueues = 1;
    Shared->AutoMask = (((InterruptConfiguration >> 2) & 3) == VMX3_IMM_AUTO);
    Shared->NumIntrs = 1;
    Shared->EventIntrIndex = 0;
    Shared->ModLevels[0] = VMX3_IML_ADAPTIVE;
    Shared->IntrCtrl = VMX3_IC_DISABLE_ALL;
    Shared->RxMode = Vmx3RxMode(Adapter->PacketFilter);

    Adapter->TxProduced = 0;
    Adapter->TxCompleted = 0;
    Adapter->RxProduced = 0;
    Adapter->RxCompleted = 0;
    for (i = 0; i < VMX3_RX_RING_SIZE - 1; i++)
        Vmx3PostReceive(Adapter);

    KeMemoryBarrier();
    Vmx3WriteVd(Adapter, VMX3_VD_DSAL, (ULONG)SharedAddress);
    Vmx3WriteVd(Adapter, VMX3_VD_DSAH, (ULONG)(SharedAddress >> 32));
    if (Vmx3Command(Adapter, VMX3_CMD_ACTIVATE_DEV) != 0)
        return NDIS_STATUS_ADAPTER_NOT_FOUND;

    Adapter->Activated = TRUE;
    Vmx3WritePt(Adapter, VMX3_PT_RXPROD, Adapter->RxProduced % VMX3_RX_RING_SIZE);
    return NDIS_STATUS_SUCCESS;
}

static VOID
Vmx3Deactivate(
    _In_ PVMX3_ADAPTER Adapter)
{
    if (!Adapter->Activated)
        return;

    Vmx3DisableInterrupts(Adapter);
    Vmx3Command(Adapter, VMX3_CMD_QUIESCE_DEV);
    Vmx3Command(Adapter, VMX3_CMD_RESET_DEV);
    Adapter->Activated = FALSE;
}

static BOOLEAN NTAPI
Vmx3Interrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportInterruptContext;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;

    if (!Adapter->Activated || !Vmx3ReadVd(Adapter, VMX3_VD_ICR))
        return FALSE;

    Vmx3WritePt(Adapter, VMX3_PT_IMR, 1);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static VOID NTAPI
Vmx3InterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportInterruptContext;
    PNDIS_RECEIVE_THROTTLE_PARAMETERS Throttle = (PNDIS_RECEIVE_THROTTLE_PARAMETERS)ReceiveThrottleParameters;
    ULONG Budget = VMX3_RX_BUDGET;
    ULONG Events;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved2);

    if (Throttle)
    {
        Throttle->MoreNblsPending = FALSE;
        if (Throttle->MaxNblsToIndicate != NDIS_INDICATE_ALL_NBLS)
            Budget = max(1, min(Budget, Throttle->MaxNblsToIndicate));
    }

    Events = *(volatile ULONG *)&Adapter->Shared->Ecr;
    if (Events)
    {
        Vmx3WriteVd(Adapter, VMX3_VD_ECR, Events);
        if (Events & VMX3_ECR_LINK)
            Vmx3UpdateLink(Adapter, TRUE);
    }

    Vmx3CompleteTransmits(Adapter);
    Vmx3ProcessReceives(Adapter, Budget);

    if (Throttle && Throttle->MaxNblsToIndicate != NDIS_INDICATE_ALL_NBLS && Vmx3ReceivePending(Adapter))
    {
        Throttle->MoreNblsPending = TRUE;
        return;
    }

    if (Adapter->Activated)
        Vmx3WritePt(Adapter, VMX3_PT_IMR, 0);
}

static NDIS_STATUS
Vmx3RegisterInterrupt(
    _In_ PVMX3_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Characteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    Characteristics.InterruptHandler = Vmx3Interrupt;
    Characteristics.InterruptDpcHandler = Vmx3InterruptDpc;
    Characteristics.InterruptType = NDIS_CONNECT_LINE_BASED;
    Characteristics.MsiSupported = FALSE;

    return NdisMRegisterInterruptEx(Adapter->MiniportHandle, Adapter, &Characteristics, &Adapter->InterruptHandle);
}

static VOID
Vmx3Release(
    _In_ PVMX3_ADAPTER Adapter)
{
    Vmx3Deactivate(Adapter);

    if (Adapter->InterruptHandle)
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);

    Vmx3FreeDatapath(Adapter);

    if (Adapter->Pt)
        NdisMUnmapIoSpace(Adapter->MiniportHandle, Adapter->Pt, Adapter->PtLength);
    if (Adapter->Vd)
        NdisMUnmapIoSpace(Adapter->MiniportHandle, Adapter->Vd, Adapter->VdLength);

    NdisFreeSpinLock(&Adapter->CommandLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    NdisFreeSpinLock(&Adapter->TxLock);
    ExFreePoolWithTag(Adapter, VMX3_TAG);
}

static NDIS_STATUS
Vmx3CopyQuery(
    _Inout_ PNDIS_OID_REQUEST OidRequest,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = 0;
    OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = 0;

    if (OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
    {
        OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(OidRequest->DATA.QUERY_INFORMATION.InformationBuffer, Source, Length);
    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Vmx3QueryInformation(
    _In_ PVMX3_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
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
    } Data;

    RtlZeroMemory(&Data, sizeof(Data));

    switch (OidRequest->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return Vmx3CopyQuery(OidRequest, Vmx3SupportedOids, sizeof(Vmx3SupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Vmx3CopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Vmx3CopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Vmx3CopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = VMX3_MTU;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_CURRENT_LOOKAHEAD:
            Data.Ulong = Adapter->Lookahead;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = VMX3_FRAME_SIZE;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (Adapter->LinkSpeed == NDIS_LINK_SPEED_UNKNOWN) ? 0 : (ULONG)(Adapter->LinkSpeed / 100);
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "VMXNET3 Ethernet Adapter";
            return Vmx3CopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = VMX3_DRIVER_VERSION;
            return Vmx3CopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = VMX3_MAC_OPTIONS;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = (Adapter->MediaConnectState == MediaConnectStateConnected) ?
                              NdisMediaStateConnected : NdisMediaStateDisconnected;
            return Vmx3CopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = MediaDuplexStateFull;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            return Vmx3CopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->TxErrors;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            return Vmx3CopyQuery(OidRequest, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            return Vmx3CopyQuery(OidRequest, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return Vmx3CopyQuery(OidRequest, Adapter->MulticastList, Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = VMX3_MAX_MULTICAST;
            return Vmx3CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

static NDIS_STATUS
Vmx3SetInformation(
    _In_ PVMX3_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PVOID Buffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;

    OidRequest->DATA.SET_INFORMATION.BytesRead = 0;
    OidRequest->DATA.SET_INFORMATION.BytesNeeded = 0;

    switch (OidRequest->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (*(PULONG)Buffer & ~VMX3_SUPPORTED_FILTERS)
                return NDIS_STATUS_NOT_SUPPORTED;
            Adapter->PacketFilter = *(PULONG)Buffer;
            Vmx3ApplyRxMode(Adapter);
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Lookahead = min(*(PULONG)Buffer, VMX3_MTU);
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if ((Length % ETH_LENGTH_OF_ADDRESS) != 0)
                return NDIS_STATUS_INVALID_LENGTH;
            if (Length > sizeof(Adapter->MulticastList))
                return NDIS_STATUS_MULTICAST_FULL;
            RtlCopyMemory(Adapter->MulticastList, Buffer, Length);
            Adapter->MulticastCount = Length / ETH_LENGTH_OF_ADDRESS;
            OidRequest->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

static NDIS_STATUS NTAPI
Vmx3InitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PVMX3_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), VMX3_TAG);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Paused = TRUE;
    Adapter->Lookahead = VMX3_MTU;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->CommandLock);

    Status = Vmx3SetRegistrationAttributes(Adapter);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Vmx3MapResources(Adapter, (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Vmx3ProbeDevice(Adapter);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Vmx3AllocateDatapath(Adapter);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Vmx3RegisterInterrupt(Adapter);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Vmx3Activate(Adapter);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        Vmx3UpdateLink(Adapter, FALSE);
        Status = Vmx3SetGeneralAttributes(Adapter);
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("VMX3: initialization failed 0x%08lx\n", Status);
        Vmx3Release(Adapter);
        return Status;
    }

    Vmx3EnableInterrupts(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Vmx3HaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(HaltAction);
    Vmx3Release((PVMX3_ADAPTER)MiniportAdapterContext);
}

static NDIS_STATUS NTAPI
Vmx3Pause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(PauseParameters);
    ((PVMX3_ADAPTER)MiniportAdapterContext)->Paused = TRUE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Vmx3Restart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(RestartParameters);
    ((PVMX3_ADAPTER)MiniportAdapterContext)->Paused = FALSE;
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Vmx3SendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST NetBufferList = NetBufferLists;
    PNET_BUFFER_LIST Head = NULL;
    PNET_BUFFER_LIST Tail = NULL;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;

    Vmx3CompleteTransmits(Adapter);

    while (NetBufferList)
    {
        PNET_BUFFER_LIST Next = NET_BUFFER_LIST_NEXT_NBL(NetBufferList);
        NDIS_STATUS Status;

        NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
        Status = Vmx3QueueNetBufferList(Adapter, NetBufferList);
        NET_BUFFER_LIST_STATUS(NetBufferList) = Status;
        if (Status != NDIS_STATUS_SUCCESS)
            Adapter->TxErrors++;

        if (Tail)
            NET_BUFFER_LIST_NEXT_NBL(Tail) = NetBufferList;
        else
            Head = NetBufferList;
        Tail = NetBufferList;
        NetBufferList = Next;
    }

    if (Head)
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, Head, CompleteFlags);
}

static VOID NTAPI
Vmx3ReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static VOID NTAPI
Vmx3CancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN NTAPI
Vmx3CheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportAdapterContext;

    if (Adapter->Activated)
    {
        Vmx3UpdateLink(Adapter, TRUE);
        Vmx3CompleteTransmits(Adapter);
    }

    return FALSE;
}

static NDIS_STATUS NTAPI
Vmx3Reset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    *AddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Vmx3OidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Vmx3QueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return Vmx3SetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID NTAPI
Vmx3CancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID NTAPI
Vmx3DevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ struct _NET_DEVICE_PNP_EVENT *NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID NTAPI
Vmx3Shutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PVMX3_ADAPTER Adapter = (PVMX3_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    if (Adapter->Activated)
    {
        Vmx3WritePt(Adapter, VMX3_PT_IMR, 1);
        Vmx3WriteVd(Adapter, VMX3_VD_CMD, VMX3_CMD_QUIESCE_DEV);
        Vmx3WriteVd(Adapter, VMX3_VD_CMD, VMX3_CMD_RESET_DEV);
    }
}

static VOID NTAPI
Vmx3Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (Vmx3DriverHandle)
    {
        NdisMDeregisterMiniportDriver(Vmx3DriverHandle);
        Vmx3DriverHandle = NULL;
    }
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 30;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = Vmx3InitializeEx;
    Characteristics.HaltHandlerEx = Vmx3HaltEx;
    Characteristics.UnloadHandler = Vmx3Unload;
    Characteristics.PauseHandler = Vmx3Pause;
    Characteristics.RestartHandler = Vmx3Restart;
    Characteristics.OidRequestHandler = Vmx3OidRequest;
    Characteristics.SendNetBufferListsHandler = Vmx3SendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = Vmx3ReturnNetBufferLists;
    Characteristics.CancelSendHandler = Vmx3CancelSend;
    Characteristics.CheckForHangHandlerEx = Vmx3CheckForHang;
    Characteristics.ResetHandlerEx = Vmx3Reset;
    Characteristics.DevicePnPEventNotifyHandler = Vmx3DevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = Vmx3Shutdown;
    Characteristics.CancelOidRequestHandler = Vmx3CancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &Vmx3DriverHandle);
}
