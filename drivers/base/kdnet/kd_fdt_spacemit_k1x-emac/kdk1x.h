/*
 * PROJECT:     LiberNT SpacemiT K1 Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SpacemiT K1 EMAC register layout and module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>
#include <kdfdt.h>

#define K1X_APMU_BASE                   0xD4282800ULL
#define K1X_APMU_SPAN                   0x400
#define K1X_MFPR_BASE                   0xD401E000ULL
#define K1X_MFPR_SPAN                   0x200
#define K1X_MFPR_FUNCTION_MASK          0x7
#define K1X_MFPR_GMAC                   0x1041
#define K1X_GMAC_DATA_PINS              15

#define APMU_EMAC_BUS_CLOCK_ENABLE      0x00000001
#define APMU_EMAC_RESET_RELEASE         0x00000002
#define APMU_EMAC_RGMII                 0x00000004
#define APMU_EMAC_RMII_REFERENCE_SOC    0x00000008
#define APMU_EMAC_RGMII_TX_CLOCK_SOC    0x00000100
#define APMU_EMAC_AXI_SINGLE_ID         0x00002000

#define APMU_DLINE_RX_ENABLE            0x00000001
#define APMU_DLINE_RX_CODE_SHIFT        8
#define APMU_DLINE_TX_ENABLE            0x00010000
#define APMU_DLINE_TX_CODE_SHIFT        24
#define APMU_DLINE_CODE_MASK            0xFF00FF00

#define EMAC_DMA_CONFIGURATION          0x0000
#define EMAC_DMA_CONTROL                0x0004
#define EMAC_DMA_STATUS                 0x0008
#define EMAC_DMA_INTERRUPT_ENABLE       0x000C
#define EMAC_DMA_TX_AUTO_POLL           0x0010
#define EMAC_DMA_TX_POLL_DEMAND         0x0014
#define EMAC_DMA_RX_POLL_DEMAND         0x0018
#define EMAC_DMA_TX_BASE                0x001C
#define EMAC_DMA_RX_BASE                0x0020
#define EMAC_MAC_GLOBAL_CONTROL         0x0100
#define EMAC_MAC_TX_CONTROL             0x0104
#define EMAC_MAC_RX_CONTROL             0x0108
#define EMAC_MAC_ADDRESS_CONTROL        0x0118
#define EMAC_MAC_ADDRESS1_HIGH          0x0120
#define EMAC_MAC_ADDRESS1_MED           0x0124
#define EMAC_MAC_ADDRESS1_LOW           0x0128
#define EMAC_MAC_HASH_TABLE1            0x0150
#define EMAC_MAC_FLOW_CONTROL           0x0160
#define EMAC_MAC_MDIO_CONTROL           0x01A0
#define EMAC_MAC_MDIO_DATA              0x01A4
#define EMAC_MAC_TX_FIFO_ALMOST_FULL    0x01C0
#define EMAC_MAC_TX_START_THRESHOLD     0x01C4
#define EMAC_MAC_RX_START_THRESHOLD     0x01C8
#define EMAC_MAC_INTERRUPT_ENABLE       0x01E4
#define EMAC_REGISTER_SPACE             0x01E8

#define EMAC_DMA_CONFIGURATION_RESET    0x00000001
#define EMAC_DMA_CONFIGURATION_BURST_16 0x00000020
#define EMAC_DMA_CONFIGURATION_SKIP_12  0x00000C00
#define EMAC_DMA_CONFIGURATION_STRICT   0x00020000
#define EMAC_DMA_CONFIGURATION_64BIT    0x00040000
#define EMAC_DMA_CONTROL_START_TX       0x00000001
#define EMAC_DMA_CONTROL_START_RX       0x00000002
#define EMAC_DMA_INT_ALL                0x000001F7

#define EMAC_MAC_GLOBAL_SPEED_MASK      0x00000003
#define EMAC_MAC_GLOBAL_SPEED_100       0x00000001
#define EMAC_MAC_GLOBAL_SPEED_1000      0x00000002
#define EMAC_MAC_GLOBAL_FULL_DUPLEX     0x00000004
#define EMAC_MAC_GLOBAL_RESET_COUNTERS  0x00000018
#define EMAC_MAC_TX_ENABLE              0x00000001
#define EMAC_MAC_TX_AUTO_RETRY          0x00000008
#define EMAC_MAC_TX_IFG_MASK            0x00000070
#define EMAC_MAC_RX_ENABLE              0x00000001
#define EMAC_MAC_RX_STORE_FORWARD       0x00000008
#define EMAC_MAC_ADDRESS1_ENABLE        0x00000001
#define EMAC_MAC_FLOW_DECODE            0x00000001
#define EMAC_MDIO_REGISTER_SHIFT        5
#define EMAC_MDIO_READ                  0x00000400
#define EMAC_MDIO_START                 0x00008000
#define EMAC_TX_FIFO_ALMOST_FULL_VALUE  0x000001F8
#define EMAC_TX_STORE_FORWARD           0x000005EE
#define EMAC_RX_START_VALUE             0x0000000C

#define EMAC_DESC_OWN                   0x80000000
#define EMAC_DESC_END_OF_RING           0x04000000
#define EMAC_RX_STATUS_FIRST            0x40000000
#define EMAC_RX_STATUS_LAST             0x20000000
#define EMAC_RX_STATUS_LENGTH           0x00003FFF
#define EMAC_RX_STATUS_ERRORS           0x00F08000
#define EMAC_TX_CONTROL_FIRST           0x20000000
#define EMAC_TX_CONTROL_LAST            0x40000000

#define MII_BMCR                        0
#define MII_BMSR                        1
#define MII_PHYSID1                     2
#define MII_PHYSID2                     3
#define MII_ADVERTISE                   4
#define MII_LPA                         5
#define MII_CTRL1000                    9
#define MII_STAT1000                    10

#define BMCR_ANRESTART                  0x0200
#define BMCR_ANENABLE                   0x1000
#define BMSR_LSTATUS                    0x0004
#define ADVERTISE_ALL                   0x01E1
#define ADVERTISE_10FULL                0x0040
#define ADVERTISE_100HALF               0x0080
#define ADVERTISE_100FULL               0x0100
#define ADVERTISE_1000FULL              0x0200
#define LPA_1000HALF                    0x0400
#define LPA_1000FULL                    0x0800

#define PHY_ID_RTL8211F                 0x001CC916
#define RTL8211F_PAGE_SELECT            0x1F
#define RTL8211F_PAGE_RGMII             0xD08
#define RTL8211F_TX_DELAY_REGISTER      0x11
#define RTL8211F_TX_DELAY               0x0100
#define RTL8211F_RX_DELAY_REGISTER      0x15
#define RTL8211F_RX_DELAY               0x0008

#define K1X_CACHE_LINE                  64
#define K1X_RX_COUNT                    64
#define K1X_TX_COUNT                    16
#define K1X_BUFFER_SIZE                 2048
#define K1X_MAX_FRAME                   1514
#define K1X_MIN_FRAME                   14
#define K1X_FCS_SIZE                    4
#define K1X_PHY_COUNT                   32

typedef struct _K1X_DESCRIPTOR
{
    volatile ULONG Status;
    volatile ULONG Control;
    volatile ULONG Buffer1;
    volatile ULONG Buffer2;
    ULONG Skipped[12];
} K1X_DESCRIPTOR, *PK1X_DESCRIPTOR;

C_ASSERT(sizeof(K1X_DESCRIPTOR) == K1X_CACHE_LINE);

typedef struct _K1X_ADAPTER
{
    PUCHAR Registers;
    PKDNET_SHARED_DATA KdNet;
    PK1X_DESCRIPTOR RxRing;
    PK1X_DESCRIPTOR TxRing;
    PUCHAR RxBuffers;
    PUCHAR TxBuffers;
    ULONG RxNext;
    ULONG TxNext;
    ULONG LinkPoll;
    ULONG LinkSpeed;
    ULONG LinkDuplex;
    ULONG PhyId;
    ULONG Port;
    ULONG ControlRegister;
    ULONG DelayLineRegister;
    ULONG TxPhase;
    ULONG RxPhase;
    UCHAR PhyAddress;
    BOOLEAN LinkUp;
    BOOLEAN Rgmii;
    BOOLEAN PhyTxDelay;
    BOOLEAN PhyRxDelay;
    BOOLEAN ReferenceClockFromPhy;
    BOOLEAN DelayLineTuning;
    USHORT RxLength[K1X_RX_COUNT];
    BOOLEAN TxSubmitted[K1X_TX_COUNT];
} K1X_ADAPTER, *PK1X_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

ULONG
NTAPI
K1xGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
K1xInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
K1xShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
K1xGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
K1xReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
K1xGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
K1xSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
K1xGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
K1xGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
