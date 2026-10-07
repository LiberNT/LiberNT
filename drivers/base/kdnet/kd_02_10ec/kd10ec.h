/*
 * PROJECT:     LiberNT Realtek Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Realtek C+ descriptor mode register layout and module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>

#define RTL_REG_IDR0            0x00
#define RTL_REG_MAR0            0x08
#define RTL_REG_TNPDS           0x20
#define RTL_REG_CR              0x37
#define RTL_REG_TPPOLL_8169     0x38
#define RTL_REG_IMR             0x3C
#define RTL_REG_ISR             0x3E
#define RTL_REG_TCR             0x40
#define RTL_REG_RCR             0x44
#define RTL_REG_9346CR          0x50
#define RTL_REG_MSR_8139        0x58
#define RTL_REG_BMCR_8139       0x62
#define RTL_REG_PHYAR           0x60
#define RTL_REG_PHYSTATUS_8169  0x6C
#define RTL_REG_ERIDR           0x70
#define RTL_REG_ERIAR           0x74
#define RTL_REG_OCPDR           0xB0
#define RTL_REG_GPHY_OCP        0xB8
#define RTL_REG_MCU             0xD3
#define RTL_REG_TPPOLL_8139     0xD9
#define RTL_REG_RMS             0xDA
#define RTL_REG_CPCR            0xE0
#define RTL_REG_RDSAR           0xE4
#define RTL_REG_MTPS            0xEC
#define RTL_REG_MISC            0xF0

#define RTL_CR_TE               0x04
#define RTL_CR_RE               0x08
#define RTL_CR_RST              0x10

#define RTL_TPPOLL_NPQ          0x40

#define RTL_TCR_DEFAULT         0x03000700
#define RTL_TCR_AUTO_FIFO       0x00000080
#define RTL_TCR_EMPTY           0x00000800
#define RTL_TCR_ID_SHIFT        20
#define RTL_TCR_ID_MASK         0x00000FCF

#define RTL_RCR_APM             0x00000002
#define RTL_RCR_AB              0x00000008
#define RTL_RCR_MXDMA           0x00000700
#define RTL_RCR_EARLY_OFF       0x00000800
#define RTL_RCR_MULTI           0x00004000
#define RTL_RCR_RX128           0x00008000
#define RTL_RCR_RXFTH           0x0000E000
#define RTL_RCR_FILTER          0x0000003F

#define RTL_9346CR_UNLOCK       0xC0
#define RTL_9346CR_LOCK         0x00

#define RTL_FLAG                0x80000000
#define RTL_PHYAR_REGISTER      16
#define RTL_OCP_REGISTER        15
#define RTL_OCP_PHY_BASE        0xA400
#define RTL_OCP_LINK_LIST       0xE8DE
#define RTL_OCP_LINK_LIST_HOLD  0x4000
#define RTL_OCP_LINK_LIST_GO    0x8000
#define RTL_ERIAR_MASK_SHIFT    12
#define RTL_ERI_ALL             0xF
#define RTL_ERI_WORD            0x3
#define RTL_ERI_RX_TIMER        0xB8
#define RTL_ERI_TX_TIMER        0xC0
#define RTL_ERI_RX_FIFO         0xC8
#define RTL_ERI_FILTER          0xDC
#define RTL_ERI_ADDRESS         0xE0
#define RTL_ERI_TX_FIFO         0xE8
#define RTL_ERI_ADDRESS_COPY    0xF0
#define RTL_ERI_PHY_INTERRUPTS  0x1A8
#define RTL_ERI_PHY_INTERRUPT_BITS 0xFC000000
#define RTL_ERI_TX_FIFO_VALUE   0x00100006
#define RTL_ERI_RX_FIFO_GATED   0x00080002
#define RTL_ERI_RX_FIFO_VALUE   0x00100002

#define RTL_MCU_LINK_LIST_READY 0x02
#define RTL_MCU_EMPTY           0x30
#define RTL_MCU_OUT_OF_BAND     0x80
#define RTL_MISC_RECEIVE_GATE   0x00080000

#define RTL_MTPS_DEFAULT        0x3F
#define RTL_MTPS_EARLY          0x27

#define RTL_PHY_BMCR            0
#define RTL_BMCR_ANRESTART      0x0200
#define RTL_BMCR_ISOLATE        0x0400
#define RTL_BMCR_PDOWN          0x0800
#define RTL_BMCR_ANENABLE       0x1000

#define RTL_CHIP_THRESHOLD      0x01
#define RTL_CHIP_MULTI          0x02
#define RTL_CHIP_EXTENDED       0x04
#define RTL_CHIP_GATED          0x08
#define RTL_CHIP_NO_PHY         0x10

#define RTL_ID_8168EVL_MASK     0x07C8
#define RTL_ID_8168EVL          0x02C8
#define RTL_ID_8168G            0x04C0

#define RTL_MSR_LINKB           0x04
#define RTL_MSR_SPEED_10        0x08
#define RTL_BMCR_DUPLEX         0x0100

#define RTL_PHYSTATUS_FULLDUP   0x01
#define RTL_PHYSTATUS_LINK      0x02
#define RTL_PHYSTATUS_10M       0x04
#define RTL_PHYSTATUS_100M      0x08

#define RTL_CPCR_TXEN           0x0001
#define RTL_CPCR_RXEN           0x0002
#define RTL_CPCR_RXCHKSUM       0x0020
#define RTL_CPCR_RXVLAN         0x0040

#define RTL_DESC_OWN            0x80000000
#define RTL_DESC_EOR            0x40000000
#define RTL_DESC_FS             0x20000000
#define RTL_DESC_LS             0x10000000
#define RTL_DESC_RES_8139       0x00100000
#define RTL_DESC_RES_8169       0x00200000
#define RTL_DESC_LENGTH_8139    0x00001FFF
#define RTL_DESC_LENGTH_8169    0x00003FFF

typedef struct _RTL_DESCRIPTOR
{
    ULONG Flags;
    ULONG Vlan;
    ULONG AddressLow;
    ULONG AddressHigh;
} RTL_DESCRIPTOR, *PRTL_DESCRIPTOR;

C_ASSERT(sizeof(RTL_DESCRIPTOR) == 16);

#define RTL_RX_COUNT            64
#define RTL_TX_COUNT            16
#define RTL_BUFFER_SIZE         2048
#define RTL_MAX_FRAME           1514
#define RTL_MIN_FRAME           60
#define RTL_CRC_SIZE            4
#define RTL_RECEIVE_SIZE        1536

typedef enum _RTL_FAMILY
{
    RtlFamilyNone,
    RtlFamily8139,
    RtlFamily8169
} RTL_FAMILY;

typedef struct _RTL_ADAPTER
{
    PUCHAR Base;
    BOOLEAN Mapped;
    RTL_FAMILY Family;
    USHORT ChipId;
    UCHAR ChipFlags;
    PKDNET_SHARED_DATA KdNet;
    volatile RTL_DESCRIPTOR *RxRing;
    volatile RTL_DESCRIPTOR *TxRing;
    PUCHAR RxBuffers;
    PUCHAR TxBuffers;
    ULONG RxNext;
    ULONG TxNext;
    ULONG LinkPoll;
    USHORT RxLength[RTL_RX_COUNT];
    BOOLEAN TxSubmitted[RTL_TX_COUNT];
} RTL_ADAPTER, *PRTL_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

RTL_FAMILY
RtlIdentifyDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

ULONG
NTAPI
RtlGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
RtlInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
RtlShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
RtlGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
RtlReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
RtlGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
RtlSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
RtlGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
RtlGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
