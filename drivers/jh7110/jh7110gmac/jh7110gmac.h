/*
 * PROJECT:     LiberNT StarFive JH7110 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StarFive JH7110 GMAC (Synopsys DWC Ethernet QoS 5.x) NDIS 6.30 miniport header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ifdef.h>
#include <ipifcons.h>

#define NDEBUG
#include <reactos/debug.h>

#ifndef ETH_LENGTH_OF_ADDRESS
#define ETH_LENGTH_OF_ADDRESS 6
#endif

#define JHGMAC_TAG 'CMHJ'
#define JHGMAC_DRIVER_VERSION 0x0100
#define JHGMAC_MAX_MULTICAST 32
#define JHGMAC_MTU 1500
#define JHGMAC_FRAME_SIZE 1514
#define JHGMAC_FCS_SIZE 4
#define JHGMAC_BUFFER_SIZE 1536
#define JHGMAC_RX_RING_SIZE 512
#define JHGMAC_TX_RING_SIZE 256
#define JHGMAC_RX_BUDGET 128
#define JHGMAC_DMA_LIMIT 0xFFFFFFFFULL
#define JHGMAC_LINK_SPEED_10M 10000000ULL
#define JHGMAC_LINK_SPEED_100M 100000000ULL
#define JHGMAC_LINK_SPEED_1G 1000000000ULL
#define JHGMAC_MAC_OPTIONS (NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | \
                            NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | \
                            NDIS_MAC_OPTION_NO_LOOPBACK)
#define JHGMAC_SUPPORTED_FILTERS (NDIS_PACKET_TYPE_DIRECTED | \
                                  NDIS_PACKET_TYPE_MULTICAST | \
                                  NDIS_PACKET_TYPE_ALL_MULTICAST | \
                                  NDIS_PACKET_TYPE_BROADCAST | \
                                  NDIS_PACKET_TYPE_PROMISCUOUS)

#define JH7110_GMAC0_BASE 0x16030000ULL
#define JH7110_AONCRG_BASE 0x17000000ULL
#define JH7110_AONCRG_SPAN 0x40
#define JH7110_SYSCRG_BASE 0x13020000ULL
#define JH7110_SYSCRG_SPAN 0x400
#define JH7110_AON_SYSCON_BASE 0x17010000ULL
#define JH7110_AON_SYSCON_SPAN 0x40

#define JH7110_CLK_ENABLE (1u << 31)
#define JH7110_CLK_MUX_SHIFT 24
#define JH7110_CLK_MUX_MASK (0xfu << 24)
#define JH7110_CLK_DIV_MASK 0xffffffu

#define JH7110_AONCLK_GMAC0_AHB 2
#define JH7110_AONCLK_GMAC0_AXI 3
#define JH7110_AONCLK_GMAC0_RMII_RTX 4
#define JH7110_AONCLK_GMAC0_TX 5
#define JH7110_AONCLK_GMAC0_TX_MUX_RMII_RTX 1
#define JH7110_AONRST_ASSERT 0x38
#define JH7110_AONRST_STATUS 0x3c
#define JH7110_AONRST_GMAC0 ((1u << 0) | (1u << 1))

#define JH7110_SYSCLK_GMAC0_GTXCLK 108
#define JH7110_SYSCLK_GMAC0_PTP 109
#define JH7110_SYSCLK_GMAC0_GTXC 111

#define JH7110_GMAC_PHY_MODE_RGMII 1
#define JH7110_GMAC_PHY_MODE_FIELD 0x7u

#define GMAC_CONFIG 0x0000
#define GMAC_PACKET_FILTER 0x0008
#define GMAC_RXQ_CTRL0 0x00a0
#define GMAC_INT_EN 0x00b4
#define GMAC_VERSION 0x0110
#define GMAC_MDIO_ADDR 0x0200
#define GMAC_MDIO_DATA 0x0204
#define GMAC_ADDR_HIGH0 0x0300
#define GMAC_ADDR_LOW0 0x0304

#define GMAC_CONFIG_RE (1u << 0)
#define GMAC_CONFIG_TE (1u << 1)
#define GMAC_CONFIG_DM (1u << 13)
#define GMAC_CONFIG_FES (1u << 14)
#define GMAC_CONFIG_PS (1u << 15)

#define GMAC_PACKET_FILTER_PR (1u << 0)
#define GMAC_PACKET_FILTER_PM (1u << 4)

#define GMAC_RXQ0_ENABLE_GENERIC (1u << 1)
#define GMAC_ADDR_ENABLE (1u << 31)

#define GMAC_MDIO_BUSY (1u << 0)
#define GMAC_MDIO_WRITE (1u << 2)
#define GMAC_MDIO_READ (3u << 2)
#define GMAC_MDIO_CSR_CLOCK(_Range) ((ULONG)(_Range) << 8)
#define GMAC_MDIO_CSR_CLOCK_250_300M 5
#define GMAC_MDIO_REGISTER_SHIFT 16
#define GMAC_MDIO_PHY_SHIFT 21

#define MTL_TXQ0_OPERATION_MODE 0x0d00
#define MTL_RXQ0_OPERATION_MODE 0x0d30
#define MTL_TXQ_TSF (1u << 1)
#define MTL_TXQ_ENABLE (1u << 3)
#define MTL_TXQ_SIZE_SHIFT 16
#define MTL_RXQ_RSF (1u << 5)
#define MTL_RXQ_SIZE_SHIFT 20
#define MTL_QUEUE_UNIT 256

#define DMA_BUS_MODE 0x1000
#define DMA_SYS_BUS_MODE 0x1004
#define DMA_CH0_CONTROL 0x1100
#define DMA_CH0_TX_CONTROL 0x1104
#define DMA_CH0_RX_CONTROL 0x1108
#define DMA_CH0_TX_BASE_HIGH 0x1110
#define DMA_CH0_TX_BASE 0x1114
#define DMA_CH0_RX_BASE_HIGH 0x1118
#define DMA_CH0_RX_BASE 0x111c
#define DMA_CH0_TX_TAIL 0x1120
#define DMA_CH0_RX_TAIL 0x1128
#define DMA_CH0_TX_RING_LENGTH 0x112c
#define DMA_CH0_RX_RING_LENGTH 0x1130
#define DMA_CH0_INTERRUPT_ENABLE 0x1134
#define DMA_CH0_STATUS 0x1160

#define DMA_BUS_MODE_SOFTWARE_RESET (1u << 0)
#define DMA_SYS_BUS_FIXED_BURST (1u << 0)
#define DMA_SYS_BUS_BLEN32 (1u << 4)
#define DMA_SYS_BUS_BLEN64 (1u << 5)
#define DMA_SYS_BUS_BLEN128 (1u << 6)
#define DMA_SYS_BUS_BLEN256 (1u << 7)
#define DMA_SYS_BUS_RD_OSR_SHIFT 16
#define DMA_SYS_BUS_WR_OSR_SHIFT 24
#define DMA_SYS_BUS_OSR_MAX 0xfu
#define DMA_CHANNEL_PBL_SHIFT 16
#define DMA_CHANNEL_PBL_MASK (0x3fu << 16)
#define DMA_CHANNEL_START (1u << 0)
#define DMA_RX_BUFFER_SIZE_SHIFT 1
#define DMA_RX_BUFFER_SIZE_MASK (0x3fffu << 1)

#define DMA_CH_TI (1u << 0)
#define DMA_CH_TPS (1u << 1)
#define DMA_CH_TBU (1u << 2)
#define DMA_CH_RI (1u << 6)
#define DMA_CH_RBU (1u << 7)
#define DMA_CH_RPS (1u << 8)
#define DMA_CH_FBE (1u << 12)
#define DMA_CH_AIS (1u << 14)
#define DMA_CH_NIS (1u << 15)
#define DMA_CH_STATUS_ALL (DMA_CH_TI | DMA_CH_TPS | DMA_CH_TBU | DMA_CH_RI | DMA_CH_RBU | \
                           DMA_CH_RPS | DMA_CH_FBE | DMA_CH_AIS | DMA_CH_NIS)
#define JHGMAC_INT_MASK (DMA_CH_TI | DMA_CH_RI | DMA_CH_RBU | DMA_CH_FBE | DMA_CH_AIS | DMA_CH_NIS)

#define TDES2_BUFFER1_LENGTH_MASK 0x3fffu
#define TDES2_INTERRUPT_ON_COMPLETION (1u << 31)
#define TDES3_FRAME_LENGTH_MASK 0x7fffu
#define TDES3_LAST_DESCRIPTOR (1u << 28)
#define TDES3_FIRST_DESCRIPTOR (1u << 29)
#define TDES3_ERROR_SUMMARY (1u << 15)
#define DES3_OWN (1u << 31)
#define RDES3_BUFFER1_VALID (1u << 24)
#define RDES3_INTERRUPT_ON_COMPLETION (1u << 30)
#define RDES3_PACKET_LENGTH_MASK 0x7fffu
#define RDES3_ERROR_SUMMARY (1u << 15)
#define RDES3_LAST_DESCRIPTOR (1u << 28)
#define RDES3_FIRST_DESCRIPTOR (1u << 29)
#define RDES3_CONTEXT_DESCRIPTOR (1u << 30)

#define MII_BMCR 0
#define MII_BMSR 1
#define MII_PHYSID1 2
#define MII_PHYSID2 3
#define MII_ADVERTISE 4
#define MII_LPA 5
#define MII_CTRL1000 9
#define MII_STAT1000 10

#define BMCR_ANRESTART 0x0200
#define BMCR_ANENABLE 0x1000
#define BMSR_LSTATUS 0x0004
#define ADVERTISE_CSMA 0x0001
#define ADVERTISE_10HALF 0x0020
#define ADVERTISE_10FULL 0x0040
#define ADVERTISE_100HALF 0x0080
#define ADVERTISE_100FULL 0x0100
#define ADVERTISE_ALL (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                       ADVERTISE_100HALF | ADVERTISE_100FULL)
#define ADVERTISE_1000FULL 0x0200
#define LPA_1000HALF 0x0400
#define LPA_1000FULL 0x0800

#define PHY_ID_YT8531 0x4f51e91b
#define YTPHY_SPECIFIC_STATUS 0x11
#define YTPHY_STATUS_SPEED_SHIFT 14
#define YTPHY_STATUS_SPEED_MASK 0x3u
#define YTPHY_STATUS_DUPLEX (1u << 13)
#define YTPHY_STATUS_RESOLVED (1u << 11)
#define YTPHY_STATUS_LINK (1u << 10)
#define YTPHY_EXT_ADDRESS 0x1e
#define YTPHY_EXT_DATA 0x1f
#define YTPHY_CHIP_CONFIG 0xa001
#define YTPHY_CHIP_CONFIG_RXC_DELAY (1u << 8)
#define YTPHY_CHIP_CONFIG_LDO_SHIFT 4
#define YTPHY_CHIP_CONFIG_LDO_MASK 0x3u
#define YTPHY_LDO_3V3 0
#define YTPHY_LDO_1V8 2
#define YTPHY_RGMII_CONFIG1 0xa003
#define YTPHY_RGMII_TX_CLOCK_INVERTED (1u << 14)
#define YTPHY_RGMII_RX_DELAY_SHIFT 10
#define YTPHY_RGMII_RX_DELAY_MASK (0xfu << 10)
#define YTPHY_RGMII_GE_TX_DELAY_MASK 0xfu
#define YTPHY_RGMII_DELAY_STEP_PS 150
#define YTPHY_RGMII_DELAY_MAX 15
#define YTPHY_RGMII_RXC_DELAY_PS 1900
#define YTPHY_RGMII_DELAY_DEFAULT 13
#define YTPHY_PAD_DRIVE_STRENGTH 0xa010
#define YTPHY_RXC_DS_SHIFT 13
#define YTPHY_RXC_DS_MASK (0x7u << 13)
#define YTPHY_RXD_DS_HIGH (1u << 12)
#define YTPHY_RXD_DS_LOW_SHIFT 4
#define YTPHY_RXD_DS_LOW_MASK (0x3u << 4)

typedef struct _JHGMAC_DESCRIPTOR
{
    volatile ULONG Des0;
    volatile ULONG Des1;
    volatile ULONG Des2;
    volatile ULONG Des3;
} JHGMAC_DESCRIPTOR, *PJHGMAC_DESCRIPTOR;

C_ASSERT(sizeof(JHGMAC_DESCRIPTOR) == 16);

typedef struct _JHGMAC_RX_BUFFER
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    PMDL Mdl;
    PNET_BUFFER_LIST NetBufferList;
    BOOLEAN Indicated;
} JHGMAC_RX_BUFFER, *PJHGMAC_RX_BUFFER;

typedef struct _JHGMAC_TX_BUFFER
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
} JHGMAC_TX_BUFFER, *PJHGMAC_TX_BUFFER;

typedef struct _JHGMAC_ADAPTER
{
    NDIS_HANDLE MiniportHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;

    PUCHAR RegisterBase;
    ULONG RegisterLength;
    PHYSICAL_ADDRESS RegisterPhysical;

    ULONG InterruptSource;
    PKINTERRUPT InterruptObject;
    KDPC InterruptDpc;
    volatile LONG InterruptPending;

    NDIS_HANDLE RxNblPool;
    NDIS_SPIN_LOCK RxLock;
    NDIS_SPIN_LOCK TxLock;
    NDIS_SPIN_LOCK MdioLock;

    PJHGMAC_DESCRIPTOR RxRing;
    NDIS_PHYSICAL_ADDRESS RxRingPhysical;
    ULONG RxRingLength;
    JHGMAC_RX_BUFFER RxBuffers[JHGMAC_RX_RING_SIZE];
    ULONG RxTail;

    PJHGMAC_DESCRIPTOR TxRing;
    NDIS_PHYSICAL_ADDRESS TxRingPhysical;
    ULONG TxRingLength;
    JHGMAC_TX_BUFFER TxBuffers[JHGMAC_TX_RING_SIZE];
    ULONG TxHead;
    ULONG TxTail;
    ULONG TxFree;

    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR MulticastList[JHGMAC_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    ULONG MulticastCount;
    ULONG PacketFilter;
    ULONG Lookahead;
    ULONG64 LinkSpeed;
    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
    NDIS_MINIPORT_TIMER LinkTimer;
    BOOLEAN LinkTimerInitialized;
    BOOLEAN DatapathReady;

    ULONG SysconOffset;
    ULONG SysconShift;
    ULONG TxFifoSize;
    ULONG RxFifoSize;
    ULONG TxPbl;
    ULONG RxPbl;
    BOOLEAN TxUsesRgmiiClock;
    BOOLEAN PhyTxDelay;
    BOOLEAN PhyRxDelay;
    ULONG PhyTxDelayPs;
    ULONG PhyRxDelayPs;
    ULONG PhyRxClockMicroamp;
    ULONG PhyRxDataMicroamp;
    BOOLEAN PhyTxClockAdjust;
    BOOLEAN PhyTxClockInverted10;
    BOOLEAN PhyTxClockInverted100;
    BOOLEAN PhyTxClockInverted1000;
    UCHAR PhyAddress;
    ULONG PhyId;

    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxBytes;
    ULONG64 RxBytes;
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;
    ULONG InterruptCount;
} JHGMAC_ADAPTER, *PJHGMAC_ADAPTER;
