/*
 * PROJECT: LiberNT Realtek RTL8168/8111 Driver
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: RTL8168/8111H NDIS 6.30 miniport
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 *
 * Register offsets and descriptor ownership rules are taken from the
 * lawfully published FreeBSD re(4)/rl(4) headers (BSD-2-Clause). The
 * miniport itself is a LiberNT NDIS 6.30 reimplementation.
 */

#include "rtl8168.h"

static NDIS_HANDLE Rtl8168DriverHandle;

static const NDIS_OID Rtl8168SupportedOids[] =
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

static __inline UCHAR
Rtl8168Read8(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset);
}

static __inline USHORT
Rtl8168Read16(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __inline ULONG
Rtl8168Read32(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __inline VOID
Rtl8168Write8(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR((PUCHAR)Adapter->RegisterBase + Offset, Value);
}

static __inline VOID
Rtl8168Write16(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static __inline VOID
Rtl8168Write32(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static BOOLEAN
Rtl8168IsValidMacAddress(
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
Rtl8168AllocateMdl(
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PMDL Mdl;

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl)
        MmBuildMdlForNonPagedPool(Mdl);

    return Mdl;
}

static NDIS_STATUS
Rtl8168CopyQuery(
    _Inout_ PNDIS_OID_REQUEST OidRequest,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    PVOID InfoBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    UINT InfoBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;

    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = 0;
    OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = 0;

    if (InfoBufferLength < Length)
    {
        OidRequest->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(InfoBuffer, Source, Length);
    OidRequest->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static VOID
Rtl8168DisableInterrupts(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168Write16(Adapter, RL_IMR, 0);
    Rtl8168Write16(Adapter, RL_ISR, 0xffff);
}

static VOID
Rtl8168EnableInterrupts(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168Write16(Adapter, RL_ISR, 0xffff);
    Rtl8168Write16(Adapter, RL_IMR, RL_INTR_MASK);
}

static BOOLEAN
Rtl8168SoftReset(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG i;

    Rtl8168Write8(Adapter, RL_COMMAND, RL_CMD_RESET);
    for (i = 0; i < 1000; i++)
    {
        if (!(Rtl8168Read8(Adapter, RL_COMMAND) & RL_CMD_RESET))
            return TRUE;
        NdisStallExecution(100);
    }

    return FALSE;
}

static BOOLEAN
Rtl8168PhyWrite(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Value)
{
    ULONG i;

    Rtl8168Write32(Adapter,
                   RL_PHYAR,
                   RL_PHYAR_FLAG |
                   ((Reg & 0x1f) << RL_PHYAR_REG_SHIFT) |
                   Value);

    for (i = 0; i < 200; i++)
    {
        if (!(Rtl8168Read32(Adapter, RL_PHYAR) & RL_PHYAR_FLAG))
            return TRUE;
        NdisStallExecution(25);
    }

    return FALSE;
}

static VOID
Rtl8168WriteMacAddress(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG Low;
    ULONG High;

    Low = Adapter->CurrentMacAddress[0] |
          ((ULONG)Adapter->CurrentMacAddress[1] << 8) |
          ((ULONG)Adapter->CurrentMacAddress[2] << 16) |
          ((ULONG)Adapter->CurrentMacAddress[3] << 24);
    High = Adapter->CurrentMacAddress[4] |
           ((ULONG)Adapter->CurrentMacAddress[5] << 8);

    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_WRITECFG);
    Rtl8168Write32(Adapter, RL_IDR0, Low);
    Rtl8168Write32(Adapter, RL_IDR0 + 4, High);
    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_OFF);
}

static VOID
Rtl8168ReadMacAddress(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
        Adapter->PermanentMacAddress[i] = Rtl8168Read8(Adapter, RL_IDR0 + i);

    if (!Rtl8168IsValidMacAddress(Adapter->PermanentMacAddress))
    {
        static const UCHAR Fallback[ETH_LENGTH_OF_ADDRESS] =
            { 0x02, 0x00, 0x00, 0x81, 0x68, 0x01 };

        DPRINT1("rtl8168: invalid station address, using LAA fallback\n");
        RtlCopyMemory(Adapter->PermanentMacAddress, Fallback, ETH_LENGTH_OF_ADDRESS);
    }

    RtlCopyMemory(Adapter->CurrentMacAddress,
                  Adapter->PermanentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
}

static ULONG
Rtl8168EtherCrc32(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Crc = 0xffffffff;
    ULONG i;
    ULONG Bit;

    for (i = 0; i < Length; i++)
    {
        UCHAR Data = Buffer[i];
        for (Bit = 0; Bit < 8; Bit++)
        {
            if ((Crc ^ Data) & 1)
                Crc = (Crc >> 1) ^ 0xedb88320;
            else
                Crc >>= 1;
            Data >>= 1;
        }
    }

    return ~Crc;
}

static VOID
Rtl8168ApplyRxFilter(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG RxCfg = RL_RXCFG_BASE | RL_RXCFG_APM | RL_RXCFG_AB;
    ULONG Hash[2] = { 0, 0 };
    ULONG i;

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        RxCfg |= RL_RXCFG_AAP | RL_RXCFG_AM;
        Hash[0] = Hash[1] = 0xffffffff;
    }
    else if (Adapter->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        RxCfg |= RL_RXCFG_AM;
        Hash[0] = Hash[1] = 0xffffffff;
    }
    else if (Adapter->PacketFilter & NDIS_PACKET_TYPE_MULTICAST)
    {
        RxCfg |= RL_RXCFG_AM;
        for (i = 0; i < Adapter->MulticastCount; i++)
        {
            ULONG Crc = Rtl8168EtherCrc32(Adapter->MulticastList[i], ETH_LENGTH_OF_ADDRESS);
            ULONG Bit = (Crc >> 26) & 0x3f;
            Hash[Bit >> 5] |= 1u << (Bit & 31);
        }
    }

    if (!(Adapter->PacketFilter & NDIS_PACKET_TYPE_BROADCAST))
        RxCfg &= ~RL_RXCFG_AB;

    Rtl8168Write32(Adapter, RL_MAR0, Hash[0]);
    Rtl8168Write32(Adapter, RL_MAR0 + 4, Hash[1]);
    Rtl8168Write32(Adapter, RL_RXCFG, RxCfg);
}

static VOID
Rtl8168IndicateLinkState(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_LINK_STATE LinkState;
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    LinkState.MediaConnectState = Adapter->MediaConnectState;
    LinkState.MediaDuplexState = Adapter->MediaDuplexState;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(Indication);
    Indication.SourceHandle = Adapter->MiniportHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportHandle, &Indication);
}

static BOOLEAN
Rtl8168RefreshLink(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ BOOLEAN ForceIndicate)
{
    UCHAR Media = Rtl8168Read8(Adapter, RL_GMEDIASTAT);
    NDIS_MEDIA_CONNECT_STATE Connect;
    NDIS_MEDIA_DUPLEX_STATE Duplex;
    ULONG64 Speed;
    BOOLEAN Changed;

    if (Media & RL_GMEDIA_LINK)
    {
        Connect = MediaConnectStateConnected;
        Duplex = (Media & RL_GMEDIA_FDX) ? MediaDuplexStateFull : MediaDuplexStateHalf;
        if (Media & RL_GMEDIA_1000M)
            Speed = RTL8168_LINK_SPEED_1G;
        else if (Media & RL_GMEDIA_100M)
            Speed = RTL8168_LINK_SPEED_100M;
        else
            Speed = RTL8168_LINK_SPEED_10M;
    }
    else
    {
        Connect = MediaConnectStateDisconnected;
        Duplex = MediaDuplexStateUnknown;
        Speed = NDIS_LINK_SPEED_UNKNOWN;
    }

    Changed = ForceIndicate ||
              Connect != Adapter->MediaConnectState ||
              Duplex != Adapter->MediaDuplexState ||
              Speed != Adapter->LinkSpeed;

    Adapter->MediaConnectState = Connect;
    Adapter->MediaDuplexState = Duplex;
    Adapter->LinkSpeed = Speed;
    return Changed;
}

static VOID
Rtl8168RearmRxDescriptor(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Index)
{
    ULONG Cmd = RTL8168_BUFFER_SIZE | RL_DESC_OWN;

    if (Index == (RTL8168_RX_RING_SIZE - 1))
        Cmd |= RL_DESC_EOR;

    Adapter->RxRing[Index].VlanTag = 0;
    Adapter->RxRing[Index].AddrLow = Adapter->RxBuffers[Index].PhysicalAddress.LowPart;
    Adapter->RxRing[Index].AddrHigh = Adapter->RxBuffers[Index].PhysicalAddress.HighPart;
    KeMemoryBarrier();
    Adapter->RxRing[Index].CmdStat = Cmd;
}

static VOID
Rtl8168InitRings(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG i;

    RtlZeroMemory(Adapter->RxRing, Adapter->RxRingLength);
    RtlZeroMemory(Adapter->TxRing, Adapter->TxRingLength);

    for (i = 0; i < RTL8168_RX_RING_SIZE; i++)
        Rtl8168RearmRxDescriptor(Adapter, i);

    for (i = 0; i < RTL8168_TX_RING_SIZE; i++)
    {
        Adapter->TxRing[i].CmdStat = (i == (RTL8168_TX_RING_SIZE - 1)) ? RL_DESC_EOR : 0;
        Adapter->TxRing[i].VlanTag = 0;
        Adapter->TxRing[i].AddrLow = 0;
        Adapter->TxRing[i].AddrHigh = 0;
        Adapter->TxBuffers[i].Length = 0;
    }

    Adapter->RxTail = 0;
    Adapter->TxHead = 0;
    Adapter->TxTail = 0;
    Adapter->TxFree = RTL8168_TX_RING_SIZE;
}

static VOID
Rtl8168StopHardware(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168Write8(Adapter, RL_COMMAND, 0);
    Adapter->RingsOwnedByHardware = FALSE;
    NdisStallExecution(1000);
}

static VOID
Rtl8168StartHardware(
    _In_ PRTL8168_ADAPTER Adapter)
{
    USHORT Cplus;

    if (!Rtl8168SoftReset(Adapter))
        DPRINT1("rtl8168: soft reset timed out\n");

    Adapter->HwRev = Rtl8168Read32(Adapter, RL_TXCFG) & RL_TXCFG_HWREV_MASK;
    DPRINT("rtl8168: TXCFG hwrev=0x%08lx\n", Adapter->HwRev);

    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_WRITECFG);
    Rtl8168Write8(Adapter,
                  RL_CFG1,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_CFG1) | RL_CFG1_DRIVER_LOAD | RL_CFG1_PM_ENABLE));
    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_OFF);

    Rtl8168WriteMacAddress(Adapter);
    Rtl8168InitRings(Adapter);

    Rtl8168Write32(Adapter, RL_TNPDS_LO, Adapter->TxRingPhysical.LowPart);
    Rtl8168Write32(Adapter, RL_TNPDS_HI, Adapter->TxRingPhysical.HighPart);
    Rtl8168Write32(Adapter, RL_RDSAR_LO, Adapter->RxRingPhysical.LowPart);
    Rtl8168Write32(Adapter, RL_RDSAR_HI, Adapter->RxRingPhysical.HighPart);

    Cplus = (USHORT)(RL_CPLUSCMD_TXENB | RL_CPLUSCMD_RXENB | RL_CPLUSCMD_PCI_MRW);
    Rtl8168Write16(Adapter, RL_CPLUSCMD, Cplus);
    Rtl8168Write16(Adapter, RL_RMS, 0x1fff);
    Rtl8168Write32(Adapter, RL_TXCFG, RL_TXCFG_CONFIG);
    Rtl8168ApplyRxFilter(Adapter);

    Rtl8168PhyWrite(Adapter,
                    MII_ADVERTISE,
                    ADVERTISE_CSMA | ADVERTISE_10HALF | ADVERTISE_10FULL |
                    ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE);
    Rtl8168PhyWrite(Adapter, MII_CTRL1000, ADVERTISE_1000FULL);
    Rtl8168PhyWrite(Adapter, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

    Rtl8168Write8(Adapter, RL_COMMAND, RL_CMD_TX_ENB | RL_CMD_RX_ENB);
    Adapter->RingsOwnedByHardware = TRUE;
    Rtl8168RefreshLink(Adapter, TRUE);
}

static VOID
Rtl8168DrainTxCompletions(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG Freed = 0;

    NdisAcquireSpinLock(&Adapter->TxLock);
    while (Adapter->TxFree < RTL8168_TX_RING_SIZE)
    {
        ULONG Index = Adapter->TxTail;
        ULONG Cmd = Adapter->TxRing[Index].CmdStat;

        KeMemoryBarrier();
        if (Cmd & RL_DESC_OWN)
            break;

        if (Cmd & RL_TDESC_LS)
        {
            if (Cmd & 0x00c00000)
                Adapter->TxErrors++;
            else
            {
                Adapter->TxPackets++;
                Adapter->TxBytes += Adapter->TxBuffers[Index].Length;
            }
        }

        Adapter->TxBuffers[Index].Length = 0;
        Adapter->TxRing[Index].CmdStat = (Index == (RTL8168_TX_RING_SIZE - 1)) ? RL_DESC_EOR : 0;
        Adapter->TxTail = (Index + 1) % RTL8168_TX_RING_SIZE;
        Adapter->TxFree++;
        Freed++;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);

    UNREFERENCED_PARAMETER(Freed);
}

static NDIS_STATUS
Rtl8168CopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG Copied)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Offset = 0;
    PMDL Mdl;
    ULONG MdlOffset;

    *Copied = 0;
    if (Length > BufferLength)
        return NDIS_STATUS_INVALID_LENGTH;

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);

    while (Mdl && Offset < Length)
    {
        PUCHAR Va;
        ULONG MdlLength;
        ULONG Copy;

        NdisQueryMdl(Mdl, &Va, &MdlLength, NormalPagePriority);
        if (!Va)
            return NDIS_STATUS_RESOURCES;

        if (MdlOffset >= MdlLength)
        {
            MdlOffset -= MdlLength;
            Mdl = NDIS_MDL_LINKAGE(Mdl);
            continue;
        }

        Copy = min(MdlLength - MdlOffset, Length - Offset);
        RtlCopyMemory(Buffer + Offset, Va + MdlOffset, Copy);
        Offset += Copy;
        MdlOffset = 0;
        Mdl = NDIS_MDL_LINKAGE(Mdl);
    }

    if (Offset < ETH_LENGTH_OF_ADDRESS)
        return NDIS_STATUS_INVALID_LENGTH;

    if (Offset < 60)
    {
        RtlZeroMemory(Buffer + Offset, 60 - Offset);
        Offset = 60;
    }

    *Copied = Offset;
    return NDIS_STATUS_SUCCESS;
}

static ULONG
Rtl8168ReleaseReceiveNetBufferLists(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST Next;
    ULONG Released = 0;

    NdisAcquireSpinLock(&Adapter->RxLock);
    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        ULONG Index = PtrToUlong(NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0]);

        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (Index < RTL8168_RX_RING_SIZE &&
            Adapter->RxBuffers[Index].NetBufferList == Nbl)
        {
            Adapter->RxBuffers[Index].Indicated = FALSE;
            Rtl8168RearmRxDescriptor(Adapter, Index);
            Released++;
        }
    }
    NdisReleaseSpinLock(&Adapter->RxLock);

    return Released;
}

static VOID
Rtl8168PollReceive(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Budget)
{
    PNET_BUFFER_LIST IndicateHead = NULL;
    PNET_BUFFER_LIST IndicateTail = NULL;
    ULONG Count = 0;

    NdisAcquireSpinLock(&Adapter->RxLock);
    while (Count < Budget)
    {
        ULONG Index = Adapter->RxTail;
        ULONG Cmd = Adapter->RxRing[Index].CmdStat;
        ULONG Length;
        PNET_BUFFER_LIST Nbl;

        KeMemoryBarrier();
        if (Cmd & RL_DESC_OWN)
            break;

        Length = Cmd & RL_RDESC_LEN_MASK;
        if (Length >= 4)
            Length -= 4;

        if ((Cmd & (RL_RDESC_FS | RL_RDESC_LS)) != (RL_RDESC_FS | RL_RDESC_LS) ||
            (Cmd & RL_RDESC_ERR_MASK) ||
            Length < ETH_LENGTH_OF_ADDRESS ||
            Length > RTL8168_FRAME_SIZE ||
            Adapter->RxBuffers[Index].Indicated)
        {
            Adapter->RxErrors++;
            Rtl8168RearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RTL8168_RX_RING_SIZE;
            continue;
        }

        Nbl = Adapter->RxBuffers[Index].NetBufferList;
        if (!Nbl)
        {
            Adapter->RxNoBuffer++;
            Rtl8168RearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RTL8168_RX_RING_SIZE;
            continue;
        }

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_DATA_LENGTH(NET_BUFFER_LIST_FIRST_NB(Nbl)) = Length;
        NET_BUFFER_DATA_OFFSET(NET_BUFFER_LIST_FIRST_NB(Nbl)) = 0;
        NET_BUFFER_CURRENT_MDL_OFFSET(NET_BUFFER_LIST_FIRST_NB(Nbl)) = 0;
        NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = UlongToPtr(Index);

        Adapter->RxBuffers[Index].Indicated = TRUE;
        Adapter->RxPackets++;
        Adapter->RxBytes += Length;
        Adapter->RxTail = (Index + 1) % RTL8168_RX_RING_SIZE;
        Count++;

        if (!IndicateHead)
            IndicateHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(IndicateTail) = Nbl;
        IndicateTail = Nbl;
    }
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (IndicateHead)
    {
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle,
                                           IndicateHead,
                                           0,
                                           Count,
                                           NDIS_RECEIVE_FLAGS_RESOURCES);
        Rtl8168ReleaseReceiveNetBufferLists(Adapter, IndicateHead);
    }
}

static BOOLEAN
NTAPI
Rtl8168Interrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportInterruptContext;
    USHORT Status;

    UNREFERENCED_PARAMETER(TargetProcessors);

    *QueueDefaultInterruptDpc = FALSE;
    Status = Rtl8168Read16(Adapter, RL_ISR);
    if (Status == 0 || Status == 0xffff)
        return FALSE;

    Rtl8168Write16(Adapter, RL_IMR, 0);
    Rtl8168Write16(Adapter, RL_ISR, Status);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static VOID
NTAPI
Rtl8168InterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved1)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved1);

    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168PollReceive(Adapter, RTL8168_RX_BUDGET);

    if (Rtl8168RefreshLink(Adapter, FALSE))
        Rtl8168IndicateLinkState(Adapter);

    Rtl8168EnableInterrupts(Adapter);
}

static VOID
NTAPI
Rtl8168DisableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    Rtl8168DisableInterrupts((PRTL8168_ADAPTER)MiniportInterruptContext);
}

static VOID
NTAPI
Rtl8168EnableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    Rtl8168EnableInterrupts((PRTL8168_ADAPTER)MiniportInterruptContext);
}

static VOID
NTAPI
Rtl8168LinkTimer(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (Rtl8168RefreshLink(Adapter, FALSE))
        Rtl8168IndicateLinkState(Adapter);

    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168PollReceive(Adapter, RTL8168_RX_BUDGET);
}

static NDIS_STATUS
Rtl8168StartLinkTimer(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_TIMER_CHARACTERISTICS TimerChars;
    NDIS_STATUS Status;
    LARGE_INTEGER DueTime;

    RtlZeroMemory(&TimerChars, sizeof(TimerChars));
    TimerChars.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
    TimerChars.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
    TimerChars.Header.Size = NDIS_SIZEOF_TIMER_CHARACTERISTICS_REVISION_1;
    TimerChars.AllocationTag = RTL8168_TAG;
    TimerChars.TimerFunction = Rtl8168LinkTimer;
    TimerChars.FunctionContext = Adapter;

    Status = NdisAllocateTimerObject(Adapter->MiniportHandle,
                                     &TimerChars,
                                     &Adapter->LinkTimer);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    DueTime.QuadPart = -10000LL * 1000;
    NdisSetTimerObject(Adapter->LinkTimer, DueTime, 1000, NULL);
    return NDIS_STATUS_SUCCESS;
}

static VOID
Rtl8168StopLinkTimer(
    _In_ PRTL8168_ADAPTER Adapter)
{
    if (Adapter->LinkTimer)
    {
        NdisCancelTimerObject(Adapter->LinkTimer);
        NdisFreeTimerObject(Adapter->LinkTimer);
        Adapter->LinkTimer = NULL;
    }
}

static VOID
Rtl8168FreeDatapath(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < RTL8168_RX_RING_SIZE; i++)
    {
        if (Adapter->RxBuffers[i].NetBufferList)
        {
            NdisFreeNetBufferList(Adapter->RxBuffers[i].NetBufferList);
            Adapter->RxBuffers[i].NetBufferList = NULL;
        }
        if (Adapter->RxBuffers[i].Mdl)
        {
            IoFreeMdl(Adapter->RxBuffers[i].Mdl);
            Adapter->RxBuffers[i].Mdl = NULL;
        }
        if (Adapter->RxBuffers[i].VirtualAddress)
        {
            NdisMFreeSharedMemory(Adapter->MiniportHandle,
                                  RTL8168_BUFFER_SIZE,
                                  FALSE,
                                  Adapter->RxBuffers[i].VirtualAddress,
                                  Adapter->RxBuffers[i].PhysicalAddress);
            Adapter->RxBuffers[i].VirtualAddress = NULL;
        }
    }

    for (i = 0; i < RTL8168_TX_RING_SIZE; i++)
    {
        if (Adapter->TxBuffers[i].VirtualAddress)
        {
            NdisMFreeSharedMemory(Adapter->MiniportHandle,
                                  RTL8168_BUFFER_SIZE,
                                  FALSE,
                                  Adapter->TxBuffers[i].VirtualAddress,
                                  Adapter->TxBuffers[i].PhysicalAddress);
            Adapter->TxBuffers[i].VirtualAddress = NULL;
        }
    }

    if (Adapter->RxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxRingLength,
                              FALSE,
                              Adapter->RxRing,
                              Adapter->RxRingPhysical);
        Adapter->RxRing = NULL;
    }

    if (Adapter->TxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxRingLength,
                              FALSE,
                              Adapter->TxRing,
                              Adapter->TxRingPhysical);
        Adapter->TxRing = NULL;
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    Adapter->DatapathReady = FALSE;
}

static NDIS_STATUS
Rtl8168AllocateDatapath(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = RTL8168_TAG;
    PoolParams.DataSize = 0;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle, &PoolParams);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxRingLength = sizeof(RTL8168_DESC) * RTL8168_RX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxRingLength,
                              FALSE,
                              (PVOID *)&Adapter->RxRing,
                              &Adapter->RxRingPhysical);
    if (!Adapter->RxRing)
        return NDIS_STATUS_RESOURCES;

    Adapter->TxRingLength = sizeof(RTL8168_DESC) * RTL8168_TX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxRingLength,
                              FALSE,
                              (PVOID *)&Adapter->TxRing,
                              &Adapter->TxRingPhysical);
    if (!Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < RTL8168_RX_RING_SIZE; i++)
    {
        NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                                  RTL8168_BUFFER_SIZE,
                                  FALSE,
                                  &Adapter->RxBuffers[i].VirtualAddress,
                                  &Adapter->RxBuffers[i].PhysicalAddress);
        if (!Adapter->RxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].Mdl =
            Rtl8168AllocateMdl(Adapter->RxBuffers[i].VirtualAddress, RTL8168_BUFFER_SIZE);
        if (!Adapter->RxBuffers[i].Mdl)
            return NDIS_STATUS_RESOURCES;

        Adapter->RxBuffers[i].NetBufferList =
            NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool,
                                                  0,
                                                  0,
                                                  Adapter->RxBuffers[i].Mdl,
                                                  0,
                                                  RTL8168_BUFFER_SIZE);
        if (!Adapter->RxBuffers[i].NetBufferList)
            return NDIS_STATUS_RESOURCES;
    }

    for (i = 0; i < RTL8168_TX_RING_SIZE; i++)
    {
        NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                                  RTL8168_BUFFER_SIZE,
                                  FALSE,
                                  &Adapter->TxBuffers[i].VirtualAddress,
                                  &Adapter->TxBuffers[i].PhysicalAddress);
        if (!Adapter->TxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;
    }

    Adapter->DatapathReady = TRUE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rtl8168MapResources(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR BestMemory = NULL;
    ULONG i;
    NDIS_STATUS Status;

    if (!ResourceList)
        return NDIS_STATUS_RESOURCES;

    Descriptor = ResourceList->PartialDescriptors;
    for (i = 0; i < ResourceList->Count; i++, Descriptor++)
    {
        if (Descriptor->Type == CmResourceTypeMemory)
        {
            if (!BestMemory ||
                (BestMemory->u.Memory.Length != 0x1000 &&
                 Descriptor->u.Memory.Length == 0x1000) ||
                (Descriptor->u.Memory.Length >= 0x100 &&
                 Descriptor->u.Memory.Length < BestMemory->u.Memory.Length))
            {
                BestMemory = Descriptor;
            }
        }
        else if (Descriptor->Type == CmResourceTypeInterrupt)
        {
            Adapter->InterruptLevel = Descriptor->u.Interrupt.Level;
            Adapter->InterruptVector = Descriptor->u.Interrupt.Vector;
            Adapter->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
        }
    }

    if (!BestMemory)
        return NDIS_STATUS_RESOURCE_CONFLICT;

    Adapter->RegisterPhysical = BestMemory->u.Memory.Start;
    Adapter->RegisterLength = BestMemory->u.Memory.Length;
    Status = NdisMMapIoSpace(&Adapter->RegisterBase,
                             Adapter->MiniportHandle,
                             Adapter->RegisterPhysical,
                             Adapter->RegisterLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("rtl8168: NdisMMapIoSpace failed 0x%08x\n", Status);
        return Status;
    }

    DPRINT("rtl8168: MMIO phys=0x%I64x len=0x%lx va=%p\n",
           Adapter->RegisterPhysical.QuadPart,
           Adapter->RegisterLength,
           Adapter->RegisterBase);
    return NDIS_STATUS_SUCCESS;
}

static VOID
Rtl8168UnmapResources(
    _In_ PRTL8168_ADAPTER Adapter)
{
    if (Adapter->RegisterBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->RegisterBase,
                          Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
    }
}

static NDIS_STATUS
Rtl8168SetRegistrationAttributes(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;

    RtlZeroMemory(&RegAttrs, sizeof(RegAttrs));
    RegAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttrs.Header.Size = sizeof(RegAttrs);
    RegAttrs.MiniportAdapterContext = Adapter;
    RegAttrs.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                              NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                              NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttrs.CheckForHangTimeInSeconds = 4;
    RegAttrs.InterfaceType = NdisInterfacePci;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttrs);
}

static NDIS_STATUS
Rtl8168SetGeneralAttributes(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;

    RtlZeroMemory(&GenAttrs, sizeof(GenAttrs));
    GenAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = NdisPhysicalMedium802_3;
    GenAttrs.MtuSize = RTL8168_MTU;
    GenAttrs.MaxXmitLinkSpeed = RTL8168_LINK_SPEED_1G;
    GenAttrs.MaxRcvLinkSpeed = RTL8168_LINK_SPEED_1G;
    GenAttrs.XmitLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.RcvLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.MediaConnectState = Adapter->MediaConnectState;
    GenAttrs.MediaDuplexState = Adapter->MediaDuplexState;
    GenAttrs.LookaheadSize = Adapter->Lookahead;
    GenAttrs.MacOptions = RTL8168_MAC_OPTIONS;
    GenAttrs.SupportedPacketFilters = RTL8168_SUPPORTED_FILTERS;
    GenAttrs.MaxMulticastListSize = RTL8168_MAX_MULTICAST;
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(GenAttrs.PermanentMacAddress,
                  Adapter->PermanentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(GenAttrs.CurrentMacAddress,
                  Adapter->CurrentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    GenAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
    GenAttrs.IfConnectorPresent = TRUE;
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    GenAttrs.SupportedOidList = (PNDIS_OID)Rtl8168SupportedOids;
    GenAttrs.SupportedOidListLength = sizeof(Rtl8168SupportedOids);
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
Rtl8168RegisterInterrupt(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS IntChars;
    NDIS_STATUS Status;

    RtlZeroMemory(&IntChars, sizeof(IntChars));
    IntChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    IntChars.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    IntChars.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    IntChars.InterruptHandler = Rtl8168Interrupt;
    IntChars.InterruptDpcHandler = Rtl8168InterruptDpc;
    IntChars.DisableInterruptHandler = Rtl8168DisableInterruptEx;
    IntChars.EnableInterruptHandler = Rtl8168EnableInterruptEx;
    IntChars.InterruptType = NDIS_CONNECT_LINE_BASED;
    IntChars.MsiSupported = TRUE;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle,
                                      Adapter,
                                      &IntChars,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        IntChars.MsiSupported = FALSE;
        IntChars.InterruptType = NDIS_CONNECT_LINE_BASED;
        Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle,
                                          Adapter,
                                          &IntChars,
                                          &Adapter->InterruptHandle);
    }

    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("rtl8168: interrupt registration failed 0x%08x\n", Status);

    return Status;
}

static VOID
Rtl8168DeregisterInterrupt(
    _In_ PRTL8168_ADAPTER Adapter)
{
    if (Adapter->InterruptHandle)
    {
        Rtl8168DisableInterrupts(Adapter);
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;
    }
}

static NDIS_STATUS
Rtl8168QueryInformation(
    _In_ PRTL8168_ADAPTER Adapter,
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
            return Rtl8168CopyQuery(OidRequest, Rtl8168SupportedOids, sizeof(Rtl8168SupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Rtl8168CopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Rtl8168CopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Rtl8168CopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = RTL8168_MTU;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = RTL8168_FRAME_SIZE;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = 0x10EC;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "LiberNT Realtek RTL8168/8111 Gigabit Ethernet";
            return Rtl8168CopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = RTL8168_DRIVER_VERSION;
            return Rtl8168CopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = RTL8168_MAC_OPTIONS;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = (Adapter->MediaConnectState == MediaConnectStateConnected) ?
                              NdisMediaStateConnected : NdisMediaStateDisconnected;
            return Rtl8168CopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            return Rtl8168CopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->TxErrors;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rtl8168CopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rtl8168CopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return Rtl8168CopyQuery(OidRequest,
                                    Adapter->MulticastList,
                                    Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = RTL8168_MAX_MULTICAST;
            return Rtl8168CopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS
Rtl8168SetInformation(
    _In_ PRTL8168_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    UINT Length = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;

    OidRequest->DATA.SET_INFORMATION.BytesRead = 0;
    OidRequest->DATA.SET_INFORMATION.BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            Adapter->PacketFilter = *(PULONG)Buffer;
            Rtl8168ApplyRxFilter(Adapter);
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            Adapter->Lookahead = *(PULONG)Buffer;
            OidRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if (Length % ETH_LENGTH_OF_ADDRESS)
                return NDIS_STATUS_INVALID_LENGTH;
            if ((Length / ETH_LENGTH_OF_ADDRESS) > RTL8168_MAX_MULTICAST)
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded =
                    RTL8168_MAX_MULTICAST * ETH_LENGTH_OF_ADDRESS;
                return NDIS_STATUS_MULTICAST_FULL;
            }
            Adapter->MulticastCount = Length / ETH_LENGTH_OF_ADDRESS;
            if (Length)
                RtlCopyMemory(Adapter->MulticastList, Buffer, Length);
            Rtl8168ApplyRxFilter(Adapter);
            OidRequest->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_CURRENT_ADDRESS:
            if (Length < ETH_LENGTH_OF_ADDRESS)
            {
                OidRequest->DATA.SET_INFORMATION.BytesNeeded = ETH_LENGTH_OF_ADDRESS;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            if (!Rtl8168IsValidMacAddress(Buffer))
                return NDIS_STATUS_INVALID_DATA;
            RtlCopyMemory(Adapter->CurrentMacAddress, Buffer, ETH_LENGTH_OF_ADDRESS);
            Rtl8168WriteMacAddress(Adapter);
            OidRequest->DATA.SET_INFORMATION.BytesRead = ETH_LENGTH_OF_ADDRESS;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static NDIS_STATUS
NTAPI
Rtl8168InitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PRTL8168_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), RTL8168_TAG);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = RTL8168_MTU;
    Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->TxLock);

    Status = Rtl8168SetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,
                           NULL,
                           NULL,
                           NULL);

    Status = Rtl8168MapResources(
        Adapter,
        (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Rtl8168ReadMacAddress(Adapter);

    Status = Rtl8168AllocateDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Rtl8168StartHardware(Adapter);

    Status = Rtl8168RegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rtl8168SetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rtl8168StartLinkTimer(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("rtl8168: link timer unavailable 0x%08x\n", Status);

    Rtl8168EnableInterrupts(Adapter);
    Rtl8168IndicateLinkState(Adapter);
    DPRINT("rtl8168: initialized mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           Adapter->CurrentMacAddress[0],
           Adapter->CurrentMacAddress[1],
           Adapter->CurrentMacAddress[2],
           Adapter->CurrentMacAddress[3],
           Adapter->CurrentMacAddress[4],
           Adapter->CurrentMacAddress[5]);
    return NDIS_STATUS_SUCCESS;

Fail:
    DPRINT1("rtl8168: InitializeEx failed 0x%08x\n", Status);
    Rtl8168StopLinkTimer(Adapter);
    Rtl8168DeregisterInterrupt(Adapter);
    Rtl8168StopHardware(Adapter);
    Rtl8168FreeDatapath(Adapter);
    Rtl8168UnmapResources(Adapter);
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    ExFreePoolWithTag(Adapter, RTL8168_TAG);
    return Status;
}

static VOID
NTAPI
Rtl8168HaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    if (!Adapter)
        return;

    Rtl8168StopLinkTimer(Adapter);
    Rtl8168DeregisterInterrupt(Adapter);
    Rtl8168StopHardware(Adapter);
    Rtl8168FreeDatapath(Adapter);
    Rtl8168UnmapResources(Adapter);
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    ExFreePoolWithTag(Adapter, RTL8168_TAG);
}

static NDIS_STATUS
NTAPI
Rtl8168Pause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PauseParameters);
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168Write8(Adapter, RL_COMMAND, 0);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
NTAPI
Rtl8168Restart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);
    Rtl8168StartHardware(Adapter);
    Rtl8168EnableInterrupts(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static VOID
NTAPI
Rtl8168SendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;
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

    Rtl8168DrainTxCompletions(Adapter);

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        PNET_BUFFER NetBuffer;
        NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
        ULONG Index;
        ULONG Length = 0;
        ULONG Cmd;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (!Adapter->DatapathReady ||
            Adapter->MediaConnectState != MediaConnectStateConnected)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
            goto FailNbl;
        }

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        if (!NetBuffer || NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Status = NDIS_STATUS_INVALID_PACKET;
            goto FailNbl;
        }

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxFree == 0)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            Status = NDIS_STATUS_RESOURCES;
            goto FailNbl;
        }

        Index = Adapter->TxHead;
        Status = Rtl8168CopyNetBuffer(NetBuffer,
                                      Adapter->TxBuffers[Index].VirtualAddress,
                                      RTL8168_BUFFER_SIZE,
                                      &Length);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            goto FailNbl;
        }

        Adapter->TxBuffers[Index].Length = Length;
        Adapter->TxRing[Index].AddrLow = Adapter->TxBuffers[Index].PhysicalAddress.LowPart;
        Adapter->TxRing[Index].AddrHigh = Adapter->TxBuffers[Index].PhysicalAddress.HighPart;
        Adapter->TxRing[Index].VlanTag = 0;
        Cmd = Length | RL_TDESC_FS | RL_TDESC_LS | RL_DESC_OWN;
        if (Index == (RTL8168_TX_RING_SIZE - 1))
            Cmd |= RL_DESC_EOR;
        KeMemoryBarrier();
        Adapter->TxRing[Index].CmdStat = Cmd;

        Adapter->TxHead = (Index + 1) % RTL8168_TX_RING_SIZE;
        Adapter->TxFree--;
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
        Rtl8168Write8(Adapter, RL_GTXPOLL, RL_GTXPOLL_NPQ);

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

static VOID
NTAPI
Rtl8168ReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static VOID
NTAPI
Rtl8168CancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN
NTAPI
Rtl8168CheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168PollReceive(Adapter, RTL8168_RX_BUDGET);
    if (Rtl8168RefreshLink(Adapter, FALSE))
        Rtl8168IndicateLinkState(Adapter);
    return FALSE;
}

static NDIS_STATUS
NTAPI
Rtl8168Reset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    if (AddressingReset)
        *AddressingReset = TRUE;

    Rtl8168DisableInterrupts(Adapter);
    Rtl8168StartHardware(Adapter);
    Rtl8168EnableInterrupts(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
NTAPI
Rtl8168OidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Rtl8168QueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return Rtl8168SetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID
NTAPI
Rtl8168CancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID
NTAPI
Rtl8168DevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ struct _NET_DEVICE_PNP_EVENT *NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID
NTAPI
Rtl8168Shutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168Write8(Adapter, RL_COMMAND, 0);
}

static VOID
NTAPI
Rtl8168Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (Rtl8168DriverHandle)
    {
        NdisMDeregisterMiniportDriver(Rtl8168DriverHandle);
        Rtl8168DriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;
    NDIS_STATUS Status;

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 30;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;
    Chars.InitializeHandlerEx = Rtl8168InitializeEx;
    Chars.HaltHandlerEx = Rtl8168HaltEx;
    Chars.UnloadHandler = Rtl8168Unload;
    Chars.PauseHandler = Rtl8168Pause;
    Chars.RestartHandler = Rtl8168Restart;
    Chars.OidRequestHandler = Rtl8168OidRequest;
    Chars.SendNetBufferListsHandler = Rtl8168SendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = Rtl8168ReturnNetBufferLists;
    Chars.CancelSendHandler = Rtl8168CancelSend;
    Chars.CheckForHangHandlerEx = Rtl8168CheckForHang;
    Chars.ResetHandlerEx = Rtl8168Reset;
    Chars.DevicePnPEventNotifyHandler = Rtl8168DevicePnPEventNotify;
    Chars.ShutdownHandlerEx = Rtl8168Shutdown;
    Chars.CancelOidRequestHandler = Rtl8168CancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Chars,
                                         &Rtl8168DriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("rtl8168: NdisMRegisterMiniportDriver failed 0x%08x\n", Status);

    return Status;
}
