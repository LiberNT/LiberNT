/*
 * PROJECT: LiberNT Realtek RTL8168/8111 Driver
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: RTL8168/8111H NDIS 6.30 miniport
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 *
 * Register offsets and descriptor ownership rules are taken from the
 * lawfully published FreeBSD re(4)/rl(4) headers (BSD-2-Clause).
 * RTL8168H MAC bring-up is LiberNT-owned register programming from those
 * quirks. PHY firmware is unmodified Realtek rtl_nic blobs under fw/,
 * redistributed under LICENCE.rtlwifi_firmware.txt and applied by a
 * LiberNT-owned opcode interpreter. The miniport itself is a LiberNT
 * NDIS 6.30 reimplementation.
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
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION,
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

static BOOLEAN
Rtl8168PhyRead(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _Out_ PUSHORT Value)
{
    ULONG i;
    ULONG Data;

    *Value = 0xffff;
    Rtl8168Write32(Adapter,
                   RL_PHYAR,
                   (Reg & 0x1f) << RL_PHYAR_REG_SHIFT);

    for (i = 0; i < 200; i++)
    {
        Data = Rtl8168Read32(Adapter, RL_PHYAR);
        if (Data & RL_PHYAR_FLAG)
        {
            *Value = (USHORT)(Data & RL_PHYAR_DATA_MASK);
            return TRUE;
        }
        NdisStallExecution(25);
    }

    return FALSE;
}

static BOOLEAN
Rtl8168EphyWrite(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Value)
{
    ULONG i;

    Rtl8168Write32(Adapter,
                   RL_EPHYAR,
                   RL_EPHYAR_WRITE |
                   ((Reg & RL_EPHYAR_REG_MASK) << RL_EPHYAR_REG_SHIFT) |
                   Value);

    for (i = 0; i < 100; i++)
    {
        if (!(Rtl8168Read32(Adapter, RL_EPHYAR) & RL_EPHYAR_FLAG))
        {
            NdisStallExecution(10);
            return TRUE;
        }
        NdisStallExecution(10);
    }

    return FALSE;
}

static BOOLEAN
Rtl8168EphyRead(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _Out_ PUSHORT Value)
{
    ULONG i;
    ULONG Data;

    *Value = 0xffff;
    Rtl8168Write32(Adapter,
                   RL_EPHYAR,
                   (Reg & RL_EPHYAR_REG_MASK) << RL_EPHYAR_REG_SHIFT);

    for (i = 0; i < 100; i++)
    {
        Data = Rtl8168Read32(Adapter, RL_EPHYAR);
        if (Data & RL_EPHYAR_FLAG)
        {
            *Value = (USHORT)(Data & RL_EPHYAR_DATA_MASK);
            NdisStallExecution(10);
            return TRUE;
        }
        NdisStallExecution(10);
    }

    return FALSE;
}

static VOID
Rtl8168EphyModify(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Clear,
    _In_ USHORT Set)
{
    USHORT Value;

    if (!Rtl8168EphyRead(Adapter, Reg, &Value))
        return;

    Value = (USHORT)((Value & ~Clear) | Set);
    Rtl8168EphyWrite(Adapter, Reg, Value);
}

static BOOLEAN
Rtl8168EriWrite(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    ULONG i;

    Rtl8168Write32(Adapter, RL_ERIDR, Value);
    Rtl8168Write32(Adapter,
                   RL_ERIAR,
                   RL_ERIAR_WRITE | RL_ERIAR_EXGMAC | Mask | (Address & 0xfff));

    for (i = 0; i < 100; i++)
    {
        if (!(Rtl8168Read32(Adapter, RL_ERIAR) & RL_ERIAR_FLAG))
            return TRUE;
        NdisStallExecution(100);
    }

    return FALSE;
}

static BOOLEAN
Rtl8168EriRead(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    ULONG i;

    *Value = 0xffffffff;
    Rtl8168Write32(Adapter,
                   RL_ERIAR,
                   RL_ERIAR_EXGMAC | RL_ERIAR_MASK_1111 | (Address & 0xfff));

    for (i = 0; i < 100; i++)
    {
        if (Rtl8168Read32(Adapter, RL_ERIAR) & RL_ERIAR_FLAG)
        {
            *Value = Rtl8168Read32(Adapter, RL_ERIDR);
            return TRUE;
        }
        NdisStallExecution(100);
    }

    return FALSE;
}

static VOID
Rtl8168EriModify(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Set,
    _In_ ULONG Clear)
{
    ULONG Value;

    if (!Rtl8168EriRead(Adapter, Address, &Value))
        return;

    Rtl8168EriWrite(Adapter, Address, Mask, (Value & ~Clear) | Set);
}

static VOID
Rtl8168MacOcpWrite(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Value)
{
    ULONG i;

    Rtl8168Write32(Adapter,
                   RL_MACOCP,
                   RL_MACOCP_FLAG |
                   ((Reg & 0x7fff) << RL_MACOCP_ADDR_SHIFT) |
                   Value);
    for (i = 0; i < 100; i++)
    {
        if (!(Rtl8168Read32(Adapter, RL_MACOCP) & RL_MACOCP_FLAG))
            return;
        NdisStallExecution(10);
    }
}

static USHORT
Rtl8168MacOcpRead(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg)
{
    ULONG i;

    Rtl8168Write32(Adapter,
                   RL_MACOCP,
                   (Reg & 0x7fff) << RL_MACOCP_ADDR_SHIFT);
    for (i = 0; i < 100; i++)
    {
        ULONG Data = Rtl8168Read32(Adapter, RL_MACOCP);

        if (Data & RL_MACOCP_FLAG)
            return (USHORT)(Data & RL_MACOCP_DATA_MASK);
        NdisStallExecution(10);
    }

    return (USHORT)(Rtl8168Read32(Adapter, RL_MACOCP) & RL_MACOCP_DATA_MASK);
}

static VOID
Rtl8168MacOcpModify(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Clear,
    _In_ USHORT Set)
{
    USHORT Value;

    Value = Rtl8168MacOcpRead(Adapter, Reg);
    Rtl8168MacOcpWrite(Adapter, Reg, (USHORT)((Value & ~Clear) | Set));
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
    ULONG Low;
    ULONG High;

    Low = Rtl8168Read32(Adapter, RL_IDR0);
    High = Rtl8168Read32(Adapter, RL_IDR0 + 4);
    Adapter->PermanentMacAddress[0] = (UCHAR)(Low & 0xff);
    Adapter->PermanentMacAddress[1] = (UCHAR)((Low >> 8) & 0xff);
    Adapter->PermanentMacAddress[2] = (UCHAR)((Low >> 16) & 0xff);
    Adapter->PermanentMacAddress[3] = (UCHAR)((Low >> 24) & 0xff);
    Adapter->PermanentMacAddress[4] = (UCHAR)(High & 0xff);
    Adapter->PermanentMacAddress[5] = (UCHAR)((High >> 8) & 0xff);

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
Rtl8168IdentifyHardware(
    _Inout_ PRTL8168_ADAPTER Adapter)
{
    Adapter->HwRev = Rtl8168Read32(Adapter, RL_TXCFG) & RL_TXCFG_HWREV_MASK;
    Adapter->Rtl8168H = (Adapter->HwRev == RL_HWREV_8168H);
    Adapter->Rtl8168GPlus =
        (Adapter->HwRev == RL_HWREV_8168G) ||
        (Adapter->HwRev == RL_HWREV_8168GU) ||
        Adapter->Rtl8168H ||
        (Adapter->HwRev == RL_HWREV_8168EP) ||
        (Adapter->HwRev == RL_HWREV_8168FP) ||
        (Adapter->HwRev == RL_HWREV_8411B);
    Adapter->MacStatCplus = Adapter->Rtl8168GPlus;
    Adapter->PcieHashSwap = TRUE;

    DPRINT1("rtl8168: hwrev=0x%08lx gplus=%u h=%u macstat=%u\n",
            Adapter->HwRev,
            Adapter->Rtl8168GPlus ? 1 : 0,
            Adapter->Rtl8168H ? 1 : 0,
            Adapter->MacStatCplus ? 1 : 0);
}

static UCHAR
Rtl8168FindPcieCapability(
    _In_ PRTL8168_ADAPTER Adapter)
{
    UCHAR CapPtr = 0;
    UCHAR CapId;
    ULONG Bytes;

    if (Adapter->PcieCapOffset)
        return Adapter->PcieCapOffset;

    Bytes = NdisMGetBusData(Adapter->MiniportHandle,
                            PCI_WHICHSPACE_CONFIG,
                            0x34,
                            &CapPtr,
                            sizeof(CapPtr));
    if (Bytes != sizeof(CapPtr))
        return 0;

    CapPtr &= 0xfc;
    while (CapPtr >= 0x40)
    {
        Bytes = NdisMGetBusData(Adapter->MiniportHandle,
                                PCI_WHICHSPACE_CONFIG,
                                CapPtr,
                                &CapId,
                                sizeof(CapId));
        if (Bytes != sizeof(CapId) || CapId == 0xff)
            break;

        if (CapId == PCI_CAPABILITY_ID_PCI_EXPRESS)
        {
            Adapter->PcieCapOffset = CapPtr;
            return CapPtr;
        }

        Bytes = NdisMGetBusData(Adapter->MiniportHandle,
                                PCI_WHICHSPACE_CONFIG,
                                CapPtr + 1,
                                &CapPtr,
                                sizeof(CapPtr));
        if (Bytes != sizeof(CapPtr))
            break;
        CapPtr &= 0xfc;
    }

    return 0;
}

static VOID
Rtl8168DisableAspm(
    _In_ PRTL8168_ADAPTER Adapter)
{
    UCHAR CapPtr;
    USHORT LinkControl;
    ULONG Bytes;

    CapPtr = Rtl8168FindPcieCapability(Adapter);
    if (!CapPtr)
        return;

    Bytes = NdisMGetBusData(Adapter->MiniportHandle,
                            PCI_WHICHSPACE_CONFIG,
                            CapPtr + 0x10,
                            &LinkControl,
                            sizeof(LinkControl));
    if (Bytes != sizeof(LinkControl))
        return;

    if (LinkControl & (PCI_EXPRESS_LCTL_ASPMC | PCI_EXPRESS_LCTL_CLKREQ_EN))
    {
        LinkControl &= (USHORT)~(PCI_EXPRESS_LCTL_ASPMC |
                                 PCI_EXPRESS_LCTL_CLKREQ_EN);
        NdisMSetBusData(Adapter->MiniportHandle,
                        PCI_WHICHSPACE_CONFIG,
                        CapPtr + 0x10,
                        &LinkControl,
                        sizeof(LinkControl));
        DPRINT1("rtl8168: disabled PCIe ASPM/CLKREQ\n");
    }
}

static VOID
Rtl8168SetMaxReadRequest(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG MrssEncoding)
{
    UCHAR CapPtr;
    USHORT DeviceControl;
    ULONG Bytes;

    CapPtr = Rtl8168FindPcieCapability(Adapter);
    if (!CapPtr)
        return;

    Bytes = NdisMGetBusData(Adapter->MiniportHandle,
                            PCI_WHICHSPACE_CONFIG,
                            CapPtr + 0x08,
                            &DeviceControl,
                            sizeof(DeviceControl));
    if (Bytes != sizeof(DeviceControl))
        return;

    DeviceControl &= (USHORT)~PCI_EXPRESS_DCTL_MAX_READ_REQUEST_MASK;
    DeviceControl |= (USHORT)((MrssEncoding << PCI_EXPRESS_DCTL_MAX_READ_REQUEST_SHIFT) &
                              PCI_EXPRESS_DCTL_MAX_READ_REQUEST_MASK);
    NdisMSetBusData(Adapter->MiniportHandle,
                    PCI_WHICHSPACE_CONFIG,
                    CapPtr + 0x08,
                    &DeviceControl,
                    sizeof(DeviceControl));
    DPRINT1("rtl8168: set PCIe MaxReadRequest encoding %lu\n", MrssEncoding);
}

static VOID
Rtl8168SetPcieEntryLatency(
    _In_ PRTL8168_ADAPTER Adapter)
{
    UCHAR Latency = PCI_EXPRESS_L0S_L1_ENTRY_LATENCY;
    ULONG Bytes;
    ULONG Csi;

    Bytes = NdisMSetBusData(Adapter->MiniportHandle,
                            PCI_WHICHSPACE_CONFIG,
                            PCI_EXPRESS_EXTENDED_CFG_LATENCY,
                            &Latency,
                            sizeof(Latency));
    if (Bytes == sizeof(Latency))
        return;

    Rtl8168Write32(Adapter,
                   RL_CSIAR,
                   (0x070c & RL_CSIAR_ADDR_MASK) | RL_CSIAR_BYTE_ENABLE);
    NdisStallExecution(10);
    if (!(Rtl8168Read32(Adapter, RL_CSIAR) & RL_CSIAR_FLAG))
        return;

    Csi = Rtl8168Read32(Adapter, RL_CSIDR) & 0x00ffffff;
    Csi |= ((ULONG)Latency << 24);
    Rtl8168Write32(Adapter, RL_CSIDR, Csi);
    Rtl8168Write32(Adapter,
                   RL_CSIAR,
                   RL_CSIAR_WRITE | (0x070c & RL_CSIAR_ADDR_MASK) | RL_CSIAR_BYTE_ENABLE);
    NdisStallExecution(10);
}

static VOID
Rtl8168PhyWake(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168Write8(Adapter,
                  RL_PMCH,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_PMCH) | RL_PMCH_PHY_WAKE));
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
    Rtl8168PhyWrite(Adapter, 0x0e, 0x0000);
}


static VOID
Rtl8168PhyModify(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ ULONG Reg,
    _In_ USHORT Clear,
    _In_ USHORT Set)
{
    USHORT Value;

    if (!Rtl8168PhyRead(Adapter, Reg, &Value))
        return;

    Rtl8168PhyWrite(Adapter, Reg, (USHORT)((Value & ~Clear) | Set));
}

static VOID
Rtl8168PhySoftReset(
    _In_ PRTL8168_ADAPTER Adapter)
{
    USHORT Value;
    ULONG i;

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
    Rtl8168PhyWrite(Adapter, MII_BMCR, BMCR_RESET);
    for (i = 0; i < 2500; i++)
    {
        if (Rtl8168PhyRead(Adapter, MII_BMCR, &Value) && !(Value & BMCR_RESET))
            return;
        NdisStallExecution(1000);
    }

    DPRINT1("rtl8168: PHY soft reset timed out\n");
}

static VOID
Rtl8168FwScriptWrite(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ BOOLEAN MacOcp,
    _In_ ULONG Reg,
    _In_ USHORT Value)
{
    if (MacOcp)
        Rtl8168MacOcpWrite(Adapter, Reg, Value);
    else
        Rtl8168PhyWrite(Adapter, Reg, Value);
}

static BOOLEAN
Rtl8168FwScriptRead(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ BOOLEAN MacOcp,
    _In_ ULONG Reg,
    _Out_ PUSHORT Value)
{
    if (MacOcp)
    {
        *Value = Rtl8168MacOcpRead(Adapter, Reg);
        return TRUE;
    }

    return Rtl8168PhyRead(Adapter, Reg, Value);
}

static VOID
Rtl8168Apply8168hPhyScript(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_reads_(OpCount) const ULONG *Ops,
    _In_ ULONG OpCount)
{
    ULONG Index;
    ULONG Predata = 0;
    ULONG Count = 0;
    BOOLEAN MacOcp = FALSE;

    for (Index = 0; Index < OpCount; Index++)
    {
        ULONG Action = Ops[Index];
        ULONG Opcode = Action >> 28;
        ULONG Reg = (Action >> 16) & 0x0fff;
        USHORT Value = (USHORT)(Action & 0xffff);
        USHORT Data;

        switch (Opcode)
        {
            case RL_FW_PHY_READ:
                if (Rtl8168FwScriptRead(Adapter, MacOcp, Reg, &Data))
                {
                    Predata = Data;
                    Count++;
                }
                break;

            case RL_FW_PHY_OR:
                Predata |= Value;
                break;

            case RL_FW_PHY_AND:
                Predata &= Value;
                break;

            case RL_FW_PHY_BJMPN:
                if (Reg <= Index)
                    Index -= Reg;
                break;

            case RL_FW_PHY_MDIO_CHG:
                MacOcp = (Value == 1);
                break;

            case RL_FW_PHY_CLEAR_READCOUNT:
                Count = 0;
                break;

            case RL_FW_PHY_WRITE:
                Rtl8168FwScriptWrite(Adapter, MacOcp, Reg, Value);
                break;

            case RL_FW_PHY_READCOUNT_EQ_SKIP:
                if (Count == Value)
                    Index++;
                break;

            case RL_FW_PHY_COMP_EQ_SKIPN:
                if (Predata == Value)
                    Index += Reg;
                break;

            case RL_FW_PHY_COMP_NEQ_SKIPN:
                if (Predata != Value)
                    Index += Reg;
                break;

            case RL_FW_PHY_WRITE_PREVIOUS:
                Rtl8168FwScriptWrite(Adapter, MacOcp, Reg, (USHORT)Predata);
                break;

            case RL_FW_PHY_SKIPN:
                Index += Reg;
                break;

            case RL_FW_PHY_DELAY_MS:
                NdisMSleep(Value);
                break;

            default:
                DPRINT1("rtl8168: unknown PHY script opcode 0x%lx\n", Opcode);
                return;
        }
    }
}

static VOID
Rtl8168FirmwareCandidates(
    _In_ ULONG HwRev,
    _Out_writes_(2) PCWSTR *Names)
{
    Names[0] = NULL;
    Names[1] = NULL;

    switch (HwRev)
    {
        case RL_HWREV_8168H:
            Names[0] = L"rtl8168h-2.fw";
            Names[1] = L"rtl8168h-1.fw";
            break;
        case RL_HWREV_8168G:
            Names[0] = L"rtl8168g-2.fw";
            Names[1] = L"rtl8168g-3.fw";
            break;
        case RL_HWREV_8168GU:
            Names[0] = L"rtl8168g-3.fw";
            Names[1] = L"rtl8168g-2.fw";
            break;
        case RL_HWREV_8168FP:
            Names[0] = L"rtl8168fp-3.fw";
            break;
        case RL_HWREV_8411B:
            Names[0] = L"rtl8411-2.fw";
            break;
        default:
            break;
    }
}

static NTSTATUS
Rtl8168LoadAndApplyFirmwareFile(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ PCWSTR FileName)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    WCHAR PathBuffer[RTL8168_FW_PATH_CHARS];
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Buffer;
    ULONG Length;
    ULONG Start;
    ULONG OpCount;
    ULONG i;
    UCHAR Sum;
    ULONG DirLen = 0;
    ULONG NameLen = 0;

    while (RTL8168_FW_DIR[DirLen] != L'\0')
        DirLen++;
    while (FileName[NameLen] != L'\0')
        NameLen++;
    if (DirLen + NameLen + 1 > RTL_NUMBER_OF(PathBuffer))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(PathBuffer, RTL8168_FW_DIR, DirLen * sizeof(WCHAR));
    RtlCopyMemory(PathBuffer + DirLen, FileName, (NameLen + 1) * sizeof(WCHAR));

    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes,
                               &Path,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
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
        DPRINT1("rtl8168: firmware %ws unavailable 0x%08lx\n", PathBuffer, Status);
        return Status;
    }

    Status = ZwQueryInformationFile(Handle,
                                    &IoStatus,
                                    &FileInfo,
                                    sizeof(FileInfo),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status) ||
        FileInfo.EndOfFile.QuadPart < RTL8168_FW_MIN_SIZE ||
        FileInfo.EndOfFile.QuadPart > RTL8168_FW_MAX_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Length = (ULONG)FileInfo.EndOfFile.QuadPart;
    Buffer = ExAllocatePoolWithTag(PagedPool, Length, RTL8168_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Length, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Length)
    {
        ExFreePoolWithTag(Buffer, RTL8168_TAG);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    Sum = 0;
    for (i = 0; i < Length; i++)
        Sum = (UCHAR)(Sum + Buffer[i]);
    if (Sum != 0)
    {
        ExFreePoolWithTag(Buffer, RTL8168_TAG);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    RtlCopyMemory(&Start, Buffer + RTL8168_FW_START_OFFSET, sizeof(Start));
    RtlCopyMemory(&OpCount, Buffer + RTL8168_FW_LEN_OFFSET, sizeof(OpCount));
    if ((Start & 3) != 0 ||
        Start < RTL8168_FW_MIN_SIZE ||
        OpCount == 0 ||
        Start > Length ||
        OpCount > (Length - Start) / sizeof(ULONG))
    {
        ExFreePoolWithTag(Buffer, RTL8168_TAG);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    Rtl8168Apply8168hPhyScript(Adapter,
                               (const ULONG *)(Buffer + Start),
                               OpCount);
    ExFreePoolWithTag(Buffer, RTL8168_TAG);
    DPRINT1("rtl8168: applied %ws (%lu ops)\n", FileName, OpCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
Rtl8168LoadAndApplyFirmware(
    _In_ PRTL8168_ADAPTER Adapter)
{
    PCWSTR Names[2];
    NTSTATUS Status = STATUS_NOT_FOUND;
    ULONG i;

    Rtl8168FirmwareCandidates(Adapter->HwRev, Names);
    for (i = 0; i < RTL_NUMBER_OF(Names); i++)
    {
        if (!Names[i])
            continue;
        Status = Rtl8168LoadAndApplyFirmwareFile(Adapter, Names[i]);
        if (NT_SUCCESS(Status))
            return Status;
    }

    return Status;
}

static VOID
Rtl8168Configure8168hPhy(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG Data;
    USHORT IoffsetP3;
    USHORT IoffsetP2;
    USHORT IoffsetP1;
    USHORT IoffsetP0;
    USHORT Rlen;
    USHORT PhyVal;
    NTSTATUS Status;

    Rtl8168PhySoftReset(Adapter);
    Status = Rtl8168LoadAndApplyFirmware(Adapter);
    if (!NT_SUCCESS(Status))
        DPRINT1("rtl8168: 8168H firmware apply failed 0x%08lx\n", Status);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a43);
    Rtl8168PhyWrite(Adapter, 0x13, 0x808a);
    Rtl8168PhyModify(Adapter, 0x14, 0x003f, 0x000a);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a43);
    Rtl8168PhyWrite(Adapter, 0x13, 0x0811);
    Rtl8168PhyModify(Adapter, 0x14, 0x0000, 0x0800);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a42);
    Rtl8168PhyModify(Adapter, 0x16, 0x0000, 0x0002);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a44);
    Rtl8168PhyModify(Adapter, 0x11, 0x0000, 0x0800);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168MacOcpWrite(Adapter, 0xdd02, 0x807d);
    Data = Rtl8168MacOcpRead(Adapter, 0xdd02);
    IoffsetP3 = (USHORT)((Data & 0x80) >> 7);
    IoffsetP3 <<= 3;

    Data = Rtl8168MacOcpRead(Adapter, 0xdd00);
    IoffsetP3 |= (USHORT)((Data & 0xe000) >> 13);
    IoffsetP2 = (USHORT)((Data & 0x1e00) >> 9);
    IoffsetP1 = (USHORT)((Data & 0x01e0) >> 5);
    IoffsetP0 = (USHORT)((Data & 0x0010) >> 4);
    IoffsetP0 <<= 3;
    IoffsetP0 |= (USHORT)(Data & 0x07);
    PhyVal = (USHORT)((IoffsetP3 << 12) | (IoffsetP2 << 8) | (IoffsetP1 << 4) | IoffsetP0);

    if (IoffsetP3 != 0x0f || IoffsetP2 != 0x0f || IoffsetP1 != 0x0f || IoffsetP0 != 0x0f)
    {
        Rtl8168PhyWrite(Adapter, 0x1f, 0x0bcf);
        Rtl8168PhyWrite(Adapter, 0x16, PhyVal);
        Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
    }

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0bcd);
    if (Rtl8168PhyRead(Adapter, 0x16, &PhyVal))
    {
        PhyVal &= 0x000f;
        Rlen = 0;
        if (PhyVal > 3)
            Rlen = (USHORT)(PhyVal - 3);
        PhyVal = (USHORT)(Rlen | (Rlen << 4) | (Rlen << 8) | (Rlen << 12));
        Rtl8168PhyWrite(Adapter, 0x17, PhyVal);
    }
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a44);
    Rtl8168PhyModify(Adapter, 0x14, 0x0080, 0x0000);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0a43);
    if (Rtl8168PhyRead(Adapter, 0x10, &PhyVal) && (PhyVal & 0x0004))
        Rtl8168PhyModify(Adapter, 0x10, 0x0004, 0x0000);
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
}

static VOID
Rtl8168DisableL2L3(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168EriModify(Adapter, 0x74, RL_ERIAR_MASK_0001, 0x00, 0x07);
}

static VOID
Rtl8168Configure8168H(
    _In_ PRTL8168_ADAPTER Adapter)
{
    USHORT SawCount;
    USHORT SwCount;

    static const struct
    {
        ULONG Reg;
        USHORT Clear;
        USHORT Set;
    } EphyTable[] =
    {
        { 0x1e, 0x0800, 0x0001 },
        { 0x1d, 0x0000, 0x0800 },
        { 0x05, 0xffff, 0x2089 },
        { 0x06, 0xffff, 0x5881 },
        { 0x04, 0xffff, 0x854a },
        { 0x01, 0xffff, 0x068b },
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(EphyTable); i++)
        Rtl8168EphyModify(Adapter, EphyTable[i].Reg, EphyTable[i].Clear, EphyTable[i].Set);

    Rtl8168Write32(Adapter,
                   RL_TXCFG,
                   Rtl8168Read32(Adapter, RL_TXCFG) | RL_TXCFG_AUTO_FIFO);

    Rtl8168EriWrite(Adapter, 0xc8, RL_ERIAR_MASK_1111, RL_8168H_ERI_FIFO_RX);
    Rtl8168EriWrite(Adapter, 0xcc, RL_ERIAR_MASK_0001, RL_8168H_ERI_PAUSE_LO);
    Rtl8168EriWrite(Adapter, 0xd0, RL_ERIAR_MASK_0001, RL_8168H_ERI_PAUSE_HI);
    Rtl8168EriWrite(Adapter, 0xe8, RL_ERIAR_MASK_1111, RL_8168H_ERI_FIFO_TX);

    Rtl8168SetPcieEntryLatency(Adapter);
    Rtl8168SetMaxReadRequest(Adapter, PCI_EXPRESS_MRRS_4096);

    Rtl8168EriModify(Adapter, 0xdc, RL_ERIAR_MASK_0001, 0x00, 0x01);
    Rtl8168EriModify(Adapter, 0xdc, RL_ERIAR_MASK_0001, 0x01, 0x00);
    Rtl8168EriModify(Adapter, 0xdc, RL_ERIAR_MASK_1111, RL_8168H_ERI_DC_SET, 0x00);
    Rtl8168EriWrite(Adapter, 0x5f0, RL_ERIAR_MASK_0011, 0x4f87);

    Rtl8168Write32(Adapter,
                   RL_MISC,
                   Rtl8168Read32(Adapter, RL_MISC) & ~RL_MISC_RXDV_GATE);

    Rtl8168EriWrite(Adapter, 0xc0, RL_ERIAR_MASK_0011, 0x0000);
    Rtl8168EriWrite(Adapter, 0xb8, RL_ERIAR_MASK_0011, 0x0000);

    Rtl8168Write8(Adapter,
                  RL_EEE_LED,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_EEE_LED) & ~0x07));
    Rtl8168EriModify(Adapter, 0x1b0, RL_ERIAR_MASK_0011, RL_8168H_ERI_EEE_SET, 0x00);
    Rtl8168Write8(Adapter,
                  RL_DLLPR,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_DLLPR) &
                          ~(RL_DLLPR_PFM_EN | RL_DLLPR_TX_10M_PS_EN)));
    Rtl8168Write8(Adapter,
                  RL_MISC1,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_MISC1) & ~RL_MISC1_PFM_D3COLD_EN));

    Rtl8168EriModify(Adapter, 0x1b0, RL_ERIAR_MASK_0011, 0x0000, RL_8168H_ERI_EEE_CLR);
    Rtl8168DisableL2L3(Adapter);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0c42);
    if (Rtl8168PhyRead(Adapter, 0x13, &SawCount) && SawCount != 0)
    {
        SwCount = (USHORT)((16000000u / SawCount) & 0x0fff);
        Rtl8168MacOcpModify(Adapter, 0xd412, 0x0fff, SwCount);
    }
    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);

    Rtl8168MacOcpModify(Adapter,
                        0xe056,
                        RL_8168H_MACOCP_E056_CLR,
                        RL_8168H_MACOCP_E056_SET);
    Rtl8168MacOcpModify(Adapter,
                        0xe052,
                        RL_8168H_MACOCP_E052_CLR,
                        RL_8168H_MACOCP_E052_SET);
    Rtl8168MacOcpModify(Adapter, 0xe0d6, 0x01ff, 0x017f);
    Rtl8168MacOcpModify(Adapter, 0xd420, 0x0fff, 0x047f);
    Rtl8168MacOcpWrite(Adapter, 0xe63e, 0x0001);
    Rtl8168MacOcpWrite(Adapter, 0xe63e, 0x0000);
    Rtl8168MacOcpWrite(Adapter, 0xc094, 0x0000);
    Rtl8168MacOcpWrite(Adapter, 0xc09e, 0x0000);

    DPRINT1("rtl8168: applied 8168H bring-up quirks\n");
}

static VOID
Rtl8168ApplyRxFilter(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG Filter = Adapter->PacketFilter;
    ULONG RxCfg = RL_RXCFG_BASE | RL_RXCFG_APM | RL_RXCFG_AB;
    ULONG Hash[2] = { 0, 0 };
    ULONG i;
    BOOLEAN InGrace = (KeQueryInterruptTime() < Adapter->LinkGraceTicks);

    if (Filter == 0)
        Filter = RTL8168_DEFAULT_PACKET_FILTER;

    if (Adapter->Rtl8168GPlus)
        RxCfg |= RL_RXCFG_EARLYOFFV2;

    if (Filter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        RxCfg |= RL_RXCFG_AAP | RL_RXCFG_AM;
        Hash[0] = Hash[1] = 0xffffffff;
    }
    else if (Filter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        RxCfg |= RL_RXCFG_AM;
        Hash[0] = Hash[1] = 0xffffffff;
    }
    else if (Filter & NDIS_PACKET_TYPE_MULTICAST)
    {
        RxCfg |= RL_RXCFG_AM;
        for (i = 0; i < Adapter->MulticastCount; i++)
        {
            ULONG Crc = Rtl8168EtherCrc32(Adapter->MulticastList[i], ETH_LENGTH_OF_ADDRESS);
            ULONG Bit = (Crc >> 26) & 0x3f;
            Hash[Bit >> 5] |= 1u << (Bit & 31);
        }
    }

    if (!(Filter & NDIS_PACKET_TYPE_BROADCAST) && !InGrace)
        RxCfg &= ~RL_RXCFG_AB;

    if (Adapter->PcieHashSwap && (Hash[0] | Hash[1]))
    {
        ULONG Tmp = RtlUlongByteSwap(Hash[0]);
        Hash[0] = RtlUlongByteSwap(Hash[1]);
        Hash[1] = Tmp;
    }

    Rtl8168Write32(Adapter, RL_MAR0, Hash[0]);
    Rtl8168Write32(Adapter, RL_MAR0 + 4, Hash[1]);
    Rtl8168Write32(Adapter, RL_RXCFG, RxCfg);
}

static VOID
Rtl8168NoteLinkActivity(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Adapter->LinkHoldTicks = KeQueryInterruptTime() + RTL8168_LINK_HOLD_100NS;
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
    LinkState.PauseFunctions = NdisPauseFunctionsSendAndReceive;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
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
    BOOLEAN LinkUp;
    BOOLEAN HeldByTraffic;

    HeldByTraffic = (KeQueryInterruptTime() < Adapter->LinkHoldTicks);
    LinkUp = ((Media & RL_GMEDIA_LINK) != 0);

    if (!LinkUp &&
        (Media & (RL_GMEDIA_10M | RL_GMEDIA_100M | RL_GMEDIA_1000M)))
        LinkUp = TRUE;

    if (!LinkUp)
    {
        USHORT Bmsr = 0;

        Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
        Rtl8168PhyRead(Adapter, MII_BMSR, &Bmsr);
        Rtl8168PhyRead(Adapter, MII_BMSR, &Bmsr);
        LinkUp = ((Bmsr & BMSR_LSTATUS) != 0);
        if (LinkUp)
            DPRINT1("rtl8168: BMSR link while media=0x%02x bmsr=0x%04x\n", Media, Bmsr);
    }

    if (!LinkUp && HeldByTraffic)
        LinkUp = TRUE;

    if (LinkUp)
    {
        Adapter->LinkDownPolls = 0;
        Connect = MediaConnectStateConnected;
        Duplex = (Media & RL_GMEDIA_FDX) ? MediaDuplexStateFull : MediaDuplexStateHalf;
        if (!(Media & (RL_GMEDIA_FDX | RL_GMEDIA_10M | RL_GMEDIA_100M | RL_GMEDIA_1000M)))
            Duplex = MediaDuplexStateFull;
        if (Media & RL_GMEDIA_1000M)
            Speed = RTL8168_LINK_SPEED_1G;
        else if (Media & RL_GMEDIA_100M)
            Speed = RTL8168_LINK_SPEED_100M;
        else if (Media & RL_GMEDIA_10M)
            Speed = RTL8168_LINK_SPEED_10M;
        else if (Adapter->LinkSpeed != NDIS_LINK_SPEED_UNKNOWN)
            Speed = Adapter->LinkSpeed;
        else
            Speed = RTL8168_LINK_SPEED_1G;
    }
    else if (KeQueryInterruptTime() < Adapter->LinkGraceTicks)
    {
        Connect = MediaConnectStateUnknown;
        Duplex = MediaDuplexStateFull;
        Speed = (Adapter->LinkSpeed != NDIS_LINK_SPEED_UNKNOWN) ?
                Adapter->LinkSpeed : RTL8168_LINK_SPEED_1G;
    }
    else if (Adapter->MediaConnectState == MediaConnectStateConnected &&
             Adapter->LinkDownPolls < RTL8168_LINK_DOWN_POLLS)
    {
        Adapter->LinkDownPolls++;
        Connect = MediaConnectStateConnected;
        Duplex = Adapter->MediaDuplexState;
        Speed = Adapter->LinkSpeed;
    }
    else
    {
        Adapter->LinkDownPolls = 0;
        Connect = MediaConnectStateDisconnected;
        Duplex = MediaDuplexStateUnknown;
        Speed = NDIS_LINK_SPEED_UNKNOWN;
    }

    Changed = ForceIndicate ||
              Connect != Adapter->MediaConnectState ||
              Duplex != Adapter->MediaDuplexState ||
              Speed != Adapter->LinkSpeed;

    if (Changed)
    {
        DPRINT1("rtl8168: link %s media=0x%02x hold=%u grace=%u downpolls=%lu speed=%I64u\n",
                (Connect == MediaConnectStateConnected) ? "up" : "down",
                Media,
                HeldByTraffic ? 1 : 0,
                (KeQueryInterruptTime() < Adapter->LinkGraceTicks) ? 1 : 0,
                Adapter->LinkDownPolls,
                Speed);
    }

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
    {
        if (Adapter->RxBuffers[i].Indicated)
        {
            Adapter->RxRing[i].VlanTag = 0;
            Adapter->RxRing[i].AddrLow = Adapter->RxBuffers[i].PhysicalAddress.LowPart;
            Adapter->RxRing[i].AddrHigh = Adapter->RxBuffers[i].PhysicalAddress.HighPart;
            Adapter->RxRing[i].CmdStat =
                (i == (RTL8168_RX_RING_SIZE - 1)) ? RL_DESC_EOR : 0;
        }
        else
        {
            Rtl8168RearmRxDescriptor(Adapter, i);
        }
    }

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
    Adapter->TxFree = RTL8168_TX_RING_SIZE - 1;
    Adapter->TxHangChecks = 0;
}

static VOID
Rtl8168StopHardware(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG RxCfg;
    ULONG i;

    Rtl8168DisableInterrupts(Adapter);

    RxCfg = Rtl8168Read32(Adapter, RL_RXCFG);
    Rtl8168Write32(Adapter,
                   RL_RXCFG,
                   RxCfg & ~(RL_RXCFG_AAP | RL_RXCFG_APM | RL_RXCFG_AM | RL_RXCFG_AB));

    if (Adapter->Rtl8168GPlus)
    {
        Rtl8168Write32(Adapter,
                       RL_MISC,
                       Rtl8168Read32(Adapter, RL_MISC) | RL_MISC_RXDV_GATE);
        NdisStallExecution(2000);

        Rtl8168Write8(Adapter,
                      RL_COMMAND,
                      RL_CMD_STOPREQ | RL_CMD_TX_ENB | RL_CMD_RX_ENB);
        for (i = 0; i < 1000; i++)
        {
            if (Rtl8168Read32(Adapter, RL_TXCFG) & RL_TXCFG_QUEUE_EMPTY)
                break;
            NdisStallExecution(100);
        }

        Rtl8168Write8(Adapter, RL_COMMAND, 0);
        for (i = 0; i < 3000; i++)
        {
            UCHAR Mcu = Rtl8168Read8(Adapter, RL_MCU_CMD);
            if ((Mcu & (RL_MCU_TXFIFO_EMPTY | RL_MCU_RXFIFO_EMPTY)) ==
                (RL_MCU_TXFIFO_EMPTY | RL_MCU_RXFIFO_EMPTY))
                break;
            NdisStallExecution(20);
        }
    }
    else
    {
        Rtl8168Write8(Adapter, RL_COMMAND, 0);
    }

    Adapter->RingsOwnedByHardware = FALSE;
    NdisStallExecution(1000);

    if (!Rtl8168SoftReset(Adapter))
        DPRINT1("rtl8168: stop reset timed out\n");
}

static VOID
Rtl8168ProgramDatapath(
    _In_ PRTL8168_ADAPTER Adapter,
    _In_ BOOLEAN EnableMac)
{
    USHORT Cplus;

    Cplus = RL_CPLUSCMD_PCI_MRW | RL_CPLUSCMD_PCI_DAC | RL_CPLUSCMD_RXCSUM;
    if (Adapter->MacStatCplus)
        Cplus |= RL_CPLUSCMD_MACSTAT_DIS | RL_CPLUSCMD_INTT_1;
    else
        Cplus |= RL_CPLUSCMD_TXENB | RL_CPLUSCMD_RXENB;
    Rtl8168Write16(Adapter, RL_CPLUSCMD, Cplus);

    Rtl8168Write32(Adapter, RL_RDSAR_HI, Adapter->RxRingPhysical.HighPart);
    Rtl8168Write32(Adapter, RL_RDSAR_LO, Adapter->RxRingPhysical.LowPart);
    Rtl8168Write32(Adapter, RL_TNPDS_HI, Adapter->TxRingPhysical.HighPart);
    Rtl8168Write32(Adapter, RL_TNPDS_LO, Adapter->TxRingPhysical.LowPart);

    if (Adapter->Rtl8168GPlus)
    {
        Rtl8168Write32(Adapter,
                       RL_MISC,
                       Rtl8168Read32(Adapter, RL_MISC) & ~RL_MISC_RXDV_GATE);
    }

    if (!Adapter->Rtl8168GPlus)
        Rtl8168Write8(Adapter, RL_COMMAND, RL_CMD_TX_ENB | RL_CMD_RX_ENB);

    if (Adapter->Rtl8168GPlus)
    {
        Rtl8168Write32(Adapter,
                       RL_TXCFG,
                       (Rtl8168Read32(Adapter, RL_TXCFG) & RL_TXCFG_HWREV_MASK) |
                       RL_TXCFG_CONFIG | RL_TXCFG_AUTO_FIFO);
        Rtl8168Write8(Adapter, RL_EARLY_TX_THRESH, RL_MAX_TX_PACKET_SIZE);
    }
    else
    {
        Rtl8168Write32(Adapter,
                       RL_TXCFG,
                       (Rtl8168Read32(Adapter, RL_TXCFG) & RL_TXCFG_HWREV_MASK) |
                       RL_TXCFG_CONFIG);
        Rtl8168Write8(Adapter, RL_EARLY_TX_THRESH, 16);
    }

    Rtl8168Write8(Adapter, RL_TDFNR, 0x08);
    Rtl8168Write16(Adapter, RL_RMS, RTL8168_BUFFER_SIZE);
    Rtl8168ApplyRxFilter(Adapter);
    Rtl8168Write16(Adapter, RL_INTRMOD, RTL8168_INTR_MITIGATE);

    if (EnableMac && Adapter->Rtl8168GPlus)
        Rtl8168Write8(Adapter, RL_COMMAND, RL_CMD_TX_ENB | RL_CMD_RX_ENB);

    Adapter->RingsOwnedByHardware = TRUE;
}

static VOID
Rtl8168StartHardware(
    _In_ PRTL8168_ADAPTER Adapter)
{
    Rtl8168DisableAspm(Adapter);

    if (!Rtl8168SoftReset(Adapter))
        DPRINT1("rtl8168: soft reset timed out\n");

    Rtl8168IdentifyHardware(Adapter);
    Rtl8168PhyWake(Adapter);

    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_WRITECFG);
    Rtl8168Write8(Adapter,
                  RL_CFG1,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_CFG1) | RL_CFG1_DRIVER_LOAD | RL_CFG1_PM_ENABLE));
    Rtl8168Write8(Adapter,
                  RL_CFG2,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_CFG2) & ~RL_CFG2_CLKREQ_EN));
    Rtl8168Write8(Adapter,
                  RL_CFG5,
                  (UCHAR)(Rtl8168Read8(Adapter, RL_CFG5) & ~RL_CFG5_ASPM_EN));
    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_OFF);

    if (Adapter->Rtl8168H)
    {
        Rtl8168Configure8168H(Adapter);
        Rtl8168Configure8168hPhy(Adapter);
    }
    else
    {
        Rtl8168SetMaxReadRequest(Adapter, PCI_EXPRESS_MRRS_4096);
        if (Adapter->Rtl8168GPlus)
        {
            NTSTATUS FwStatus;

            Rtl8168SetPcieEntryLatency(Adapter);
            Rtl8168PhySoftReset(Adapter);
            FwStatus = Rtl8168LoadAndApplyFirmware(Adapter);
            if (!NT_SUCCESS(FwStatus) && FwStatus != STATUS_NOT_FOUND)
                DPRINT1("rtl8168: firmware apply failed 0x%08lx\n", FwStatus);
        }
    }

    Rtl8168InitRings(Adapter);
    Rtl8168WriteMacAddress(Adapter);
    Rtl8168ProgramDatapath(Adapter, FALSE);

    Rtl8168PhyWrite(Adapter, 0x1f, 0x0000);
    Rtl8168PhyWrite(Adapter,
                    MII_ADVERTISE,
                    ADVERTISE_CSMA | ADVERTISE_10HALF | ADVERTISE_10FULL |
                    ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE);
    Rtl8168PhyWrite(Adapter, MII_CTRL1000, ADVERTISE_1000FULL);
    Rtl8168PhyWrite(Adapter, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    NdisMSleep(10);
    Rtl8168PhyWrite(Adapter, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

    if (Adapter->Rtl8168GPlus)
        Rtl8168Write8(Adapter, RL_COMMAND, RL_CMD_TX_ENB | RL_CMD_RX_ENB);

    Rtl8168RefreshLink(Adapter, TRUE);
}

static VOID
Rtl8168DrainTxCompletions(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG Freed = 0;

    NdisAcquireSpinLock(&Adapter->TxLock);
    while (Adapter->TxFree < (RTL8168_TX_RING_SIZE - 1))
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
Rtl8168CopyOneNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Inout_ PULONG Offset)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Copied = 0;
    PMDL Mdl;
    ULONG MdlOffset;

    if (Length > (BufferLength - *Offset))
        return NDIS_STATUS_INVALID_LENGTH;

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);

    while (Mdl && Copied < Length)
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

        Copy = min(MdlLength - MdlOffset, Length - Copied);
        RtlCopyMemory(Buffer + *Offset + Copied, Va + MdlOffset, Copy);
        Copied += Copy;
        MdlOffset = 0;
        Mdl = NDIS_MDL_LINKAGE(Mdl);
    }

    if (Copied < Length)
        return NDIS_STATUS_INVALID_LENGTH;

    *Offset += Copied;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rtl8168CopyNetBufferList(
    _In_ PNET_BUFFER_LIST Nbl,
    _Out_writes_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG Copied)
{
    PNET_BUFFER NetBuffer;
    ULONG Offset = 0;
    NDIS_STATUS Status;

    *Copied = 0;
    for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
         NetBuffer;
         NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
    {
        Status = Rtl8168CopyOneNetBuffer(NetBuffer, Buffer, BufferLength, &Offset);
        if (Status != NDIS_STATUS_SUCCESS)
            return Status;
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
            Adapter->RxBuffers[Index].NetBufferList == Nbl &&
            Adapter->RxBuffers[Index].Indicated)
        {
            Adapter->RxBuffers[Index].Indicated = FALSE;
            Rtl8168RearmRxDescriptor(Adapter, Index);
            InterlockedDecrement(&Adapter->RxOutstanding);
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

    if (!Adapter->DatapathReady || Adapter->SurpriseRemoved)
        return;

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

        if (Adapter->RxBuffers[Index].Indicated)
            break;

        Length = Cmd & RL_RDESC_LEN_MASK;
        if (Length >= 4)
            Length -= 4;

        if ((Cmd & (RL_RDESC_FS | RL_RDESC_LS)) != (RL_RDESC_FS | RL_RDESC_LS) ||
            (Cmd & RL_RDESC_ERR_MASK) ||
            Length < ETH_LENGTH_OF_ADDRESS ||
            Length > RTL8168_FRAME_SIZE)
        {
            Adapter->RxErrors++;
            DPRINT1("rtl8168: drop rx cmd=0x%08lx len=%lu err=%I64u\n",
                    Cmd,
                    Length,
                    Adapter->RxErrors);
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

        if (Adapter->RxBuffers[Index].Mdl)
            NdisFlushBuffer(Adapter->RxBuffers[Index].Mdl, FALSE);

        {
            PNET_BUFFER Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);

            NET_BUFFER_CURRENT_MDL(Nb) = Adapter->RxBuffers[Index].Mdl;
            NET_BUFFER_CURRENT_MDL_OFFSET(Nb) = 0;
            NET_BUFFER_DATA_OFFSET(Nb) = 0;
            NET_BUFFER_DATA_LENGTH(Nb) = Length;
        }

        Nbl->SourceHandle = Adapter->MiniportHandle;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = (PVOID)(ULONG_PTR)Index;

        Adapter->RxBuffers[Index].Indicated = TRUE;
        InterlockedIncrement(&Adapter->RxOutstanding);
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
        ULONG Flags = 0;

        if (KeGetCurrentIrql() == DISPATCH_LEVEL)
            Flags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;

        Rtl8168NoteLinkActivity(Adapter);
        if (Rtl8168RefreshLink(Adapter, FALSE))
            Rtl8168IndicateLinkState(Adapter);

        DPRINT("rtl8168: indicate %lu rx frame(s) total=%I64u\n",
               Count,
               Adapter->RxPackets);
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle,
                                           IndicateHead,
                                           0,
                                           Count,
                                           Flags);
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

static BOOLEAN
NTAPI
Rtl8168MessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    UNREFERENCED_PARAMETER(MessageId);
    return Rtl8168Interrupt(MiniportInterruptContext,
                            QueueDefaultInterruptDpc,
                            TargetProcessors);
}

static VOID
NTAPI
Rtl8168MessageInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved1)
{
    UNREFERENCED_PARAMETER(MessageId);
    Rtl8168InterruptDpc(MiniportInterruptContext,
                        MiniportDpcContext,
                        ReceiveThrottleParameters,
                        NdisReserved1);
}

static VOID
NTAPI
Rtl8168DisableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
    Rtl8168DisableInterrupts((PRTL8168_ADAPTER)MiniportInterruptContext);
}

static VOID
NTAPI
Rtl8168EnableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
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

    DueTime.QuadPart = -10000LL * 200;
    NdisSetTimerObject(Adapter->LinkTimer, DueTime, 200, NULL);
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
        if (Adapter->TxBuffers[i].Mdl)
        {
            IoFreeMdl(Adapter->TxBuffers[i].Mdl);
            Adapter->TxBuffers[i].Mdl = NULL;
        }
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

    if (Adapter->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(Adapter->DmaHandle);
        Adapter->DmaHandle = NULL;
    }

    Adapter->DatapathReady = FALSE;
}

static NDIS_STATUS
Rtl8168RegisterDma(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NDIS_SG_DMA_DESCRIPTION DmaDescription;
    NDIS_STATUS Status;

    if (Adapter->DmaHandle)
        return NDIS_STATUS_SUCCESS;

    RtlZeroMemory(&DmaDescription, sizeof(DmaDescription));
    DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
    DmaDescription.MaximumPhysicalMapping = RTL8168_BUFFER_SIZE;
    DmaDescription.ProcessSGListHandler = NULL;
    DmaDescription.SharedMemAllocateCompleteHandler = NULL;

    Status = NdisMRegisterScatterGatherDma(Adapter->MiniportHandle,
                                           &DmaDescription,
                                           &Adapter->DmaHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Adapter->DmaHandle = NULL;
        DPRINT1("rtl8168: SG DMA register failed 0x%08x\n", Status);
    }

    return Status;
}

static NDIS_STATUS
Rtl8168AllocateDatapath(
    _In_ PRTL8168_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;
    NDIS_STATUS Status;

    Status = Rtl8168RegisterDma(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

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

        Adapter->TxBuffers[i].Mdl =
            Rtl8168AllocateMdl(Adapter->TxBuffers[i].VirtualAddress, RTL8168_BUFFER_SIZE);
        if (!Adapter->TxBuffers[i].Mdl)
            return NDIS_STATUS_RESOURCES;
    }

    DPRINT1("rtl8168: rings rx=%lx:%08lx tx=%lx:%08lx\n",
            Adapter->RxRingPhysical.HighPart,
            Adapter->RxRingPhysical.LowPart,
            Adapter->TxRingPhysical.HighPart,
            Adapter->TxRingPhysical.LowPart);

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
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsSendAndReceive;
    GenAttrs.SupportedOidList = (PNDIS_OID)Rtl8168SupportedOids;
    GenAttrs.SupportedOidListLength = sizeof(Rtl8168SupportedOids);
    GenAttrs.SupportedStatistics =
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
        NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
        NDIS_STATISTICS_BYTES_XMIT_SUPPORTED |
        NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;

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
    IntChars.MsiSupported = TRUE;
    IntChars.MsiSyncWithAllMessages = TRUE;
    IntChars.MessageInterruptHandler = Rtl8168MessageInterrupt;
    IntChars.MessageInterruptDpcHandler = Rtl8168MessageInterruptDpc;
    IntChars.DisableMessageInterruptHandler = Rtl8168DisableMessageInterrupt;
    IntChars.EnableMessageInterruptHandler = Rtl8168EnableMessageInterrupt;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle,
                                      Adapter,
                                      &IntChars,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        IntChars.MsiSupported = FALSE;
        IntChars.MessageInterruptHandler = NULL;
        IntChars.MessageInterruptDpcHandler = NULL;
        IntChars.DisableMessageInterruptHandler = NULL;
        IntChars.EnableMessageInterruptHandler = NULL;
        Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle,
                                          Adapter,
                                          &IntChars,
                                          &Adapter->InterruptHandle);
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("rtl8168: interrupt registration failed 0x%08x\n", Status);
        return Status;
    }

    Adapter->MessageInterrupt =
        (IntChars.InterruptType == NDIS_CONNECT_MESSAGE_BASED);
    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_WRITECFG);
    if (Adapter->MessageInterrupt)
    {
        Rtl8168Write8(Adapter,
                      RL_CFG2,
                      (UCHAR)(Rtl8168Read8(Adapter, RL_CFG2) | RL_CFG2_MSI_ENABLE));
    }
    else
    {
        Rtl8168Write8(Adapter,
                      RL_CFG2,
                      (UCHAR)(Rtl8168Read8(Adapter, RL_CFG2) & ~RL_CFG2_MSI_ENABLE));
    }
    Rtl8168Write8(Adapter, RL_EECMD, RL_EEMODE_OFF);

    DPRINT1("rtl8168: %s interrupt connected\n",
            Adapter->MessageInterrupt ? "message" : "line");
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
        NDIS_STATISTICS_INFO Statistics;
        NDIS_INTERRUPT_MODERATION_PARAMETERS InterruptModeration;
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
            if (Adapter->MediaConnectState == MediaConnectStateConnected)
                Data.MediaState = NdisMediaStateConnected;
            else if (Adapter->MediaConnectState == MediaConnectStateUnknown)
                Data.MediaState = NdisMediaStateConnected;
            else
                Data.MediaState = NdisMediaStateDisconnected;
            return Rtl8168CopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsSendAndReceive;
            return Rtl8168CopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_STATISTICS:
            Data.Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Data.Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            Data.Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            Data.Statistics.ifHCInUcastPkts = Adapter->RxPackets;
            Data.Statistics.ifHCOutUcastPkts = Adapter->TxPackets;
            Data.Statistics.ifHCInOctets = Adapter->RxBytes;
            Data.Statistics.ifHCOutOctets = Adapter->TxBytes;
            Data.Statistics.ifInDiscards = Adapter->RxNoBuffer;
            Data.Statistics.ifInErrors = Adapter->RxErrors;
            Data.Statistics.ifOutErrors = Adapter->TxErrors;
            return Rtl8168CopyQuery(OidRequest, &Data.Statistics, sizeof(Data.Statistics));

        case OID_GEN_INTERRUPT_MODERATION:
            Data.InterruptModeration.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.InterruptModeration.Header.Revision =
                NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Data.InterruptModeration.Header.Size =
                NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Data.InterruptModeration.Flags = 0;
            Data.InterruptModeration.InterruptModeration = NdisInterruptModerationEnabled;
            return Rtl8168CopyQuery(OidRequest,
                                    &Data.InterruptModeration,
                                    sizeof(Data.InterruptModeration));

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
            {
                ULONG Filter = *(PULONG)Buffer;

                if (Filter & ~(RTL8168_SUPPORTED_FILTERS))
                    return NDIS_STATUS_NOT_SUPPORTED;
                if (Filter == 0)
                    Filter = RTL8168_DEFAULT_PACKET_FILTER;
                Adapter->PacketFilter = Filter;
            }
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
    Adapter->PacketFilter = RTL8168_DEFAULT_PACKET_FILTER;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateUnknown;
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

static VOID
Rtl8168WaitForReturnedReceives(
    _In_ PRTL8168_ADAPTER Adapter)
{
    ULONG ElapsedMs = 0;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        return;

    while (InterlockedCompareExchange(&Adapter->RxOutstanding, 0, 0) != 0 &&
           ElapsedMs < RTL8168_PAUSE_RX_WAIT_MS)
    {
        NdisMSleep(10000);
        ElapsedMs += 10;
    }

    if (InterlockedCompareExchange(&Adapter->RxOutstanding, 0, 0) != 0)
        DPRINT1("rtl8168: pause with %ld rx outstanding\n", Adapter->RxOutstanding);
}

static NDIS_STATUS
NTAPI
Rtl8168Pause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->DatapathReady = FALSE;
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168WaitForReturnedReceives(Adapter);
    Rtl8168Write32(Adapter,
                   RL_RXCFG,
                   Rtl8168Read32(Adapter, RL_RXCFG) &
                   ~(RL_RXCFG_AAP | RL_RXCFG_APM | RL_RXCFG_AM | RL_RXCFG_AB));
    Rtl8168Write8(Adapter, RL_COMMAND, 0);
    Adapter->RingsOwnedByHardware = FALSE;
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

    if (Adapter->SurpriseRemoved)
        return NDIS_STATUS_NOT_ACCEPTED;

    Adapter->LinkGraceTicks = KeQueryInterruptTime() + RTL8168_LINK_GRACE_100NS;
    Adapter->LinkDownPolls = 0;
    if (Adapter->PacketFilter == 0)
        Adapter->PacketFilter = RTL8168_DEFAULT_PACKET_FILTER;

    Rtl8168WriteMacAddress(Adapter);
    Rtl8168ProgramDatapath(Adapter, TRUE);
    Adapter->DatapathReady = TRUE;
    Rtl8168EnableInterrupts(Adapter);
    NdisMSleep(200000);
    Rtl8168RefreshLink(Adapter, TRUE);
    DPRINT1("rtl8168: restart link=%s media=0x%02x speed=%I64u\n",
            (Adapter->MediaConnectState == MediaConnectStateConnected) ? "up" :
            (Adapter->MediaConnectState == MediaConnectStateUnknown) ? "unknown" : "down",
            Rtl8168Read8(Adapter, RL_GMEDIASTAT),
            Adapter->LinkSpeed);
    Rtl8168IndicateLinkState(Adapter);
    Rtl8168PollReceive(Adapter, RTL8168_RX_BUDGET);
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

        if (!Adapter->DatapathReady)
        {
            Status = NDIS_STATUS_PAUSED;
            goto FailNbl;
        }

        if (Adapter->SurpriseRemoved)
        {
            Status = NDIS_STATUS_NOT_ACCEPTED;
            goto FailNbl;
        }

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        if (!NetBuffer)
        {
            Status = NDIS_STATUS_INVALID_PACKET;
            goto FailNbl;
        }

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxFree == 0)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            Rtl8168DrainTxCompletions(Adapter);
            NdisAcquireSpinLock(&Adapter->TxLock);
            if (Adapter->TxFree == 0)
            {
                NdisReleaseSpinLock(&Adapter->TxLock);
                Status = NDIS_STATUS_RESOURCES;
                goto FailNbl;
            }
        }

        Index = Adapter->TxHead;
        Status = Rtl8168CopyNetBufferList(Nbl,
                                          Adapter->TxBuffers[Index].VirtualAddress,
                                          RTL8168_BUFFER_SIZE,
                                          &Length);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            goto FailNbl;
        }

        Adapter->TxBuffers[Index].Length = Length;
        if (Adapter->TxBuffers[Index].Mdl)
            NdisFlushBuffer(Adapter->TxBuffers[Index].Mdl, TRUE);
        Adapter->TxRing[Index].AddrLow = Adapter->TxBuffers[Index].PhysicalAddress.LowPart;
        Adapter->TxRing[Index].AddrHigh = Adapter->TxBuffers[Index].PhysicalAddress.HighPart;
        Adapter->TxRing[Index].VlanTag = 0;
        Cmd = Length | RL_TDESC_FS | RL_TDESC_LS | RL_DESC_OWN;
        if (Index == (RTL8168_TX_RING_SIZE - 1))
            Cmd |= RL_DESC_EOR;
        KeMemoryBarrier();
        Adapter->TxRing[Index].CmdStat = Cmd;
        KeMemoryBarrier();

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
        if (Status != NDIS_STATUS_PAUSED && Status != NDIS_STATUS_RESOURCES)
            Adapter->TxErrors++;
        if (!FailHead)
            FailHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(FailTail) = Nbl;
        FailTail = Nbl;
    }

    if (KickTx)
    {
        KeMemoryBarrier();
        (VOID)Rtl8168Read8(Adapter, RL_COMMAND);
        Rtl8168Write8(Adapter, RL_GTXPOLL, RL_GTXPOLL_NPQ);
        Rtl8168NoteLinkActivity(Adapter);
        if (Rtl8168RefreshLink(Adapter, FALSE))
            Rtl8168IndicateLinkState(Adapter);
        DPRINT("rtl8168: tx kick total=%I64u err=%I64u free=%lu media=%s\n",
               Adapter->TxPackets,
               Adapter->TxErrors,
               Adapter->TxFree,
               (Adapter->MediaConnectState == MediaConnectStateConnected) ? "up" : "down");
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

static VOID
NTAPI
Rtl8168ReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ReturnFlags);
    Rtl8168ReleaseReceiveNetBufferLists(Adapter, NetBufferLists);
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
    BOOLEAN Hung = FALSE;

    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168PollReceive(Adapter, RTL8168_RX_BUDGET);
    if (Rtl8168RefreshLink(Adapter, FALSE))
        Rtl8168IndicateLinkState(Adapter);

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (Adapter->DatapathReady &&
        Adapter->TxFree < (RTL8168_TX_RING_SIZE - 1) &&
        (Adapter->TxRing[Adapter->TxTail].CmdStat & RL_DESC_OWN))
    {
        Adapter->TxHangChecks++;
        if (Adapter->TxHangChecks >= RTL8168_TX_HANG_THRESHOLD)
        {
            DPRINT1("rtl8168: tx hang free=%lu tail=%lu\n",
                    Adapter->TxFree,
                    Adapter->TxTail);
            Adapter->TxHangChecks = 0;
            Hung = TRUE;
        }
    }
    else
    {
        Adapter->TxHangChecks = 0;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);

    return Hung;
}

static NDIS_STATUS
NTAPI
Rtl8168Reset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;
    BOOLEAN WasReady = Adapter->DatapathReady;

    if (AddressingReset)
        *AddressingReset = TRUE;

    Adapter->DatapathReady = FALSE;
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168DrainTxCompletions(Adapter);
    Rtl8168WaitForReturnedReceives(Adapter);
    Rtl8168StartHardware(Adapter);
    Adapter->DatapathReady = WasReady && !Adapter->SurpriseRemoved;
    if (Adapter->DatapathReady)
        Rtl8168EnableInterrupts(Adapter);
    Rtl8168RefreshLink(Adapter, TRUE);
    Rtl8168IndicateLinkState(Adapter);
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
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    if (!Adapter || !NetDevicePnPEvent)
        return;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->SurpriseRemoved = TRUE;
        Adapter->DatapathReady = FALSE;
        Rtl8168DisableInterrupts(Adapter);
        Rtl8168Write8(Adapter, RL_COMMAND, 0);
        DPRINT1("rtl8168: surprise removed\n");
    }
}

static VOID
NTAPI
Rtl8168Shutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRTL8168_ADAPTER Adapter = (PRTL8168_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->DatapathReady = FALSE;
    Rtl8168DisableInterrupts(Adapter);
    Rtl8168Write32(Adapter,
                   RL_RXCFG,
                   Rtl8168Read32(Adapter, RL_RXCFG) &
                   ~(RL_RXCFG_AAP | RL_RXCFG_APM | RL_RXCFG_AM | RL_RXCFG_AB));
    Rtl8168Write8(Adapter, RL_COMMAND, 0);
    if (Adapter->Rtl8168GPlus)
    {
        Rtl8168Write32(Adapter,
                       RL_MISC,
                       Rtl8168Read32(Adapter, RL_MISC) | RL_MISC_RXDV_GATE);
    }
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
