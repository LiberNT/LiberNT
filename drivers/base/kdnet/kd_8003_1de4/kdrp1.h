/*
 * PROJECT:     LiberNT Raspberry Pi RP1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cadence GEM register layout of the RP1 Ethernet and module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>

#define GEM_NCR                 0x0000
#define GEM_NCFGR               0x0004
#define GEM_NSR                 0x0008
#define GEM_DMACFG              0x0010
#define GEM_TSR                 0x0014
#define GEM_RBQP                0x0018
#define GEM_TBQP                0x001C
#define GEM_RSR                 0x0020
#define GEM_IDR                 0x002C
#define GEM_MAN                 0x0034
#define GEM_AMP                 0x0054
#define GEM_INTMOD              0x005C
#define GEM_SA1B                0x0088
#define GEM_SA1T                0x008C
#define GEM_DCFG6               0x0294
#define GEM_TBQPH               0x04C8
#define GEM_RBQPH               0x04D4

#define GEM_NCR_RE              0x00000004
#define GEM_NCR_TE              0x00000008
#define GEM_NCR_MPE             0x00000010
#define GEM_NCR_CLRSTAT         0x00000020
#define GEM_NCR_TSTART          0x00000200

#define GEM_NSR_IDLE            0x00000004

#define GEM_NCFGR_SPD           0x00000001
#define GEM_NCFGR_FD            0x00000002
#define GEM_NCFGR_BIG           0x00000100
#define GEM_NCFGR_GBE           0x00000400
#define GEM_NCFGR_DRFCS         0x00020000
#define GEM_NCFGR_CLK_DIV96     0x00140000
#define GEM_NCFGR_DBW128        0x00400000

#define GEM_DMACFG_CLEAR        0x00FF00DF
#define GEM_DMACFG_FBLDO_INCR16 0x00000010
#define GEM_DMACFG_RXBMS_FULL   0x00000300
#define GEM_DMACFG_TXPBMS       0x00000400
#define GEM_DMACFG_RXBS_2048    0x00200000
#define GEM_DMACFG_ADDR64       0x40000000

#define GEM_AMP_PIPE_MASK       0x0001FFFF
#define GEM_AMP_PIPE_8_FILL     0x00010808

#define GEM_DCFG6_DAW64         0x00800000

#define GEM_TSR_ALL             0x00000075
#define GEM_RSR_ALL             0x00000007

#define GEM_RX_USED             0x00000001
#define GEM_RX_WRAP             0x00000002
#define GEM_RX_LENGTH           0x00000FFF
#define GEM_RX_SOF              0x00004000
#define GEM_RX_EOF              0x00008000

#define GEM_TX_LAST             0x00008000
#define GEM_TX_WRAP             0x40000000
#define GEM_TX_USED             0x80000000

#define GEM_MAN_READ            0x60020000
#define GEM_MAN_WRITE           0x50020000
#define GEM_MAN_PHY_SHIFT       23
#define GEM_MAN_REGISTER_SHIFT  18

#define MII_BMCR                0
#define MII_BMSR                1
#define MII_PHYSID1             2
#define MII_PHYSID2             3
#define MII_ADVERTISE           4
#define MII_LPA                 5
#define MII_CTRL1000            9
#define MII_STAT1000            10
#define MII_MMD_CTRL            13
#define MII_MMD_DATA            14

#define BMCR_ANRESTART          0x0200
#define BMCR_ANENABLE           0x1000
#define BMSR_LSTATUS            0x0004
#define ADVERTISE_ALL           0x0DE1
#define ADVERTISE_10FULL        0x0040
#define ADVERTISE_100HALF       0x0080
#define ADVERTISE_100FULL       0x0100
#define ADVERTISE_1000FULL      0x0200
#define LPA_1000HALF            0x0400
#define LPA_1000FULL            0x0800

#define MII_MMD_CTRL_NOINCR     0x4000
#define MDIO_MMD_AN             7
#define MDIO_AN_EEE_ADV         60
#define MDIO_EEE_100TX          0x0002
#define MDIO_EEE_1000T          0x0004

#define PHY_ID_MODEL_MASK       0xFFFFFFF0
#define PHY_ID_BCM54210E        0x600D84A0

#define MII_BCM54XX_AUX_CTL             0x18
#define MII_BCM54XX_AUXCTL_ACTL_TX_6DB  0x0400
#define MII_BCM54XX_AUXCTL_ACTL_SMDSP   0x0800
#define MII_BCM54XX_AUXCTL_MISC         0x0007
#define MII_BCM54XX_AUXCTL_MISC_SKEW    0x0100
#define MII_BCM54XX_AUXCTL_MISC_WREN    0x8000
#define MII_BCM54XX_AUXCTL_READ_SHIFT   12
#define MII_BCM54XX_SHD                 0x1C
#define MII_BCM54XX_SHD_WRITE           0x8000
#define MII_BCM54XX_SHD_SHIFT           10
#define MII_BCM54XX_SHD_DATA            0x03FF
#define BCM54810_SHD_CLK_CTL            0x03
#define BCM54810_SHD_CLK_CTL_GTXCLK_EN  0x0200

typedef struct _GEM_DESCRIPTOR
{
    ULONG Address;
    ULONG Control;
    ULONG AddressHigh;
    ULONG Reserved;
} GEM_DESCRIPTOR, *PGEM_DESCRIPTOR;

C_ASSERT(sizeof(GEM_DESCRIPTOR) == 16);

#define GEM_RX_COUNT            64
#define GEM_TX_COUNT            16
#define GEM_BUFFER_SIZE         2048
#define GEM_MAX_FRAME           1514
#define GEM_MIN_FRAME           14
#define GEM_PHY_ADDRESS         1
#define GEM_PHY_COUNT           32

typedef struct _GEM_ADAPTER
{
    PUCHAR Registers;
    PKDNET_SHARED_DATA KdNet;
    volatile GEM_DESCRIPTOR *RxRing;
    volatile GEM_DESCRIPTOR *TxRing;
    PUCHAR RxBuffers;
    PUCHAR TxBuffers;
    ULONG RxNext;
    ULONG TxNext;
    ULONG LinkPoll;
    ULONG LinkSpeed;
    ULONG LinkDuplex;
    UCHAR PhyAddress;
    BOOLEAN PhyValid;
    BOOLEAN LinkUp;
    USHORT RxLength[GEM_RX_COUNT];
    BOOLEAN TxSubmitted[GEM_TX_COUNT];
} GEM_ADAPTER, *PGEM_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

ULONG
NTAPI
GemGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
GemInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
GemShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
GemGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
GemReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
GemGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
GemSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
GemGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
GemGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
