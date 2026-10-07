/*
 * PROJECT:     LiberNT LAN95xx USB Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWC2 host and LAN95xx register layout, module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>
#include <usb100.h>
#include <usb200.h>

#define USB_HUB_FEATURE_PORT_RESET     4
#define USB_HUB_FEATURE_PORT_POWER     8
#define USB_HUB_FEATURE_C_PORT_RESET   20

#define DWC_GAHBCFG                 0x0008
#define DWC_GUSBCFG                 0x000C
#define DWC_GRSTCTL                 0x0010
#define DWC_GINTSTS                 0x0014
#define DWC_GINTMSK                 0x0018
#define DWC_GRXFSIZ                 0x0024
#define DWC_GNPTXFSIZ               0x0028
#define DWC_GSNPSID                 0x0040
#define DWC_GHWCFG2                 0x0048
#define DWC_GHWCFG3                 0x004C
#define DWC_HPTXFSIZ                0x0100
#define DWC_HCFG                    0x0400
#define DWC_HAINTMSK                0x0418
#define DWC_HPRT0                   0x0440
#define DWC_HCCHAR(Channel)         (0x0500 + 0x20 * (Channel))
#define DWC_HCSPLT(Channel)         (0x0504 + 0x20 * (Channel))
#define DWC_HCINT(Channel)          (0x0508 + 0x20 * (Channel))
#define DWC_HCINTMSK(Channel)       (0x050C + 0x20 * (Channel))
#define DWC_HCTSIZ(Channel)         (0x0510 + 0x20 * (Channel))
#define DWC_HCDMA(Channel)          (0x0514 + 0x20 * (Channel))
#define DWC_PCGCCTL                 0x0E00
#define DWC_REGISTER_SPACE          0x1000

#define DWC_GAHBCFG_GLOBAL_ENABLE   0x00000001
#define DWC_GAHBCFG_BURST_MASK      0x00000006
#define DWC_GAHBCFG_WAIT_AXI_WRITES 0x00000010
#define DWC_GAHBCFG_DMA_ENABLE      0x00000020
#define DWC_GUSBCFG_FORCE_HOST      0x20000000
#define DWC_GRSTCTL_CORE_RESET      0x00000001
#define DWC_GRSTCTL_RX_FLUSH        0x00000010
#define DWC_GRSTCTL_TX_FLUSH        0x00000020
#define DWC_GRSTCTL_TX_ALL          0x00000400
#define DWC_GRSTCTL_AHB_IDLE        0x80000000
#define DWC_GINTSTS_HOST_MODE       0x00000001
#define DWC_GSNPSID_MASK            0xFFFF0000
#define DWC_GSNPSID_VALUE           0x4F540000
#define DWC_GHWCFG2_CHANNELS_SHIFT  14
#define DWC_GHWCFG2_CHANNELS_MASK   0x0000000F
#define DWC_GHWCFG2_DYNAMIC_FIFO    0x00080000
#define DWC_GHWCFG3_DEPTH_SHIFT     16
#define DWC_HCFG_CLOCK_MASK         0x00000003

#define DWC_HPRT_CONNECTED          0x00000001
#define DWC_HPRT_CONNECT_CHANGE     0x00000002
#define DWC_HPRT_ENABLED            0x00000004
#define DWC_HPRT_ENABLE_CHANGE      0x00000008
#define DWC_HPRT_OVERCURRENT_CHANGE 0x00000020
#define DWC_HPRT_RESET              0x00000100
#define DWC_HPRT_POWER              0x00001000
#define DWC_HPRT_SPEED_SHIFT        17
#define DWC_HPRT_SPEED_MASK         0x00000003
#define DWC_HPRT_SPEED_HIGH         0
#define DWC_HPRT_WRITE_CLEAR        0x0000002E

#define DWC_HCCHAR_PACKET_MASK      0x000007FF
#define DWC_HCCHAR_ENDPOINT_SHIFT   11
#define DWC_HCCHAR_IN               0x00008000
#define DWC_HCCHAR_TYPE_SHIFT       18
#define DWC_HCCHAR_ONE_PER_FRAME    0x00100000
#define DWC_HCCHAR_ADDRESS_SHIFT    22
#define DWC_HCCHAR_DISABLE          0x40000000
#define DWC_HCCHAR_ENABLE           0x80000000

#define DWC_HCINT_COMPLETE          0x00000001
#define DWC_HCINT_HALTED            0x00000002
#define DWC_HCINT_AHB_ERROR         0x00000004
#define DWC_HCINT_STALL             0x00000008
#define DWC_HCINT_NAK               0x00000010
#define DWC_HCINT_NYET              0x00000040
#define DWC_HCINT_ALL               0x00003FFF
#define DWC_HCINT_ERRORS            0x0000078C
#define DWC_HCINT_REASONS           0x000007FD

#define DWC_HCTSIZ_SIZE_MASK        0x0007FFFF
#define DWC_HCTSIZ_PACKETS_SHIFT    19
#define DWC_HCTSIZ_PACKETS_MASK     0x000003FF
#define DWC_HCTSIZ_PID_SHIFT        29
#define DWC_PID_DATA0               0
#define DWC_PID_DATA1               2
#define DWC_PID_SETUP               3

#define DWC_RX_FIFO_SIZE            532
#define DWC_NPTX_FIFO_SIZE          0x100
#define DWC_PTX_FIFO_SIZE           0x200
#define DWC_MAX_CHANNELS            16
#define DWC_CHANNEL_CONTROL         0
#define DWC_CHANNEL_RECEIVE         1
#define DWC_CHANNEL_TRANSMIT        2
#define DWC_ERROR_LIMIT             3

#define SMSC_VENDOR_ID              0x0424
#define SMSC_WRITE_REGISTER         0xA0
#define SMSC_READ_REGISTER          0xA1
#define SMSC_INT_STS                0x0008
#define SMSC_TX_CFG                 0x0010
#define SMSC_HW_CFG                 0x0014
#define SMSC_LED_GPIO_CFG           0x0024
#define SMSC_AFC_CFG                0x002C
#define SMSC_E2P_CMD                0x0030
#define SMSC_E2P_DATA               0x0034
#define SMSC_BURST_CAP              0x0038
#define SMSC_BULK_IN_DLY            0x006C
#define SMSC_MAC_CR                 0x0100
#define SMSC_ADDRH                  0x0104
#define SMSC_ADDRL                  0x0108
#define SMSC_HASHH                  0x010C
#define SMSC_HASHL                  0x0110
#define SMSC_MII_ADDR               0x0114
#define SMSC_MII_DATA               0x0118
#define SMSC_FLOW                   0x011C
#define SMSC_VLAN1                  0x0120
#define SMSC_COE_CR                 0x0130

#define SMSC_HW_CFG_BCE             0x00000002
#define SMSC_HW_CFG_LRST            0x00000008
#define SMSC_HW_CFG_MEF             0x00000020
#define SMSC_HW_CFG_RXDOFF          0x00000600
#define SMSC_HW_CFG_BIR             0x00001000
#define SMSC_TX_CFG_ON              0x00000004
#define SMSC_LED_GPIO_ALL           0x01110000
#define SMSC_AFC_CFG_DEFAULT        0x00F830A1
#define SMSC_E2P_ADDRESS_MASK       0x000001FF
#define SMSC_E2P_TIMEOUT            0x00000400
#define SMSC_E2P_BUSY               0x80000000
#define SMSC_EEPROM_MAC_OFFSET      1
#define SMSC_MAC_CR_RXEN            0x00000004
#define SMSC_MAC_CR_TXEN            0x00000008
#define SMSC_MAC_CR_FILTER_MASK     0x800C2800
#define SMSC_MAC_CR_FDPX            0x00100000
#define SMSC_MAC_CR_RCVOWN          0x00800000
#define SMSC_MII_BUSY               0x00000001
#define SMSC_MII_WRITE              0x00000002
#define SMSC_MII_REGISTER_SHIFT     6
#define SMSC_MII_PHY_SHIFT          11
#define SMSC_PHY_ADDRESS            1
#define SMSC_TX_CMD_FIRST_LAST      0x00003000
#define SMSC_RX_STATUS_ERROR        0x00008000
#define SMSC_RX_STATUS_LENGTH_SHIFT 16
#define SMSC_RX_STATUS_LENGTH_MASK  0x00003FFF
#define SMSC_TX_HEADER_SIZE         8
#define SMSC_RX_HEADER_SIZE         4

#define MII_BMCR                    0
#define MII_BMSR                    1
#define MII_ADVERTISE               4
#define MII_SMSC_SPECIAL            31
#define BMCR_ANRESTART              0x0200
#define BMCR_ISOLATE                0x0400
#define BMCR_PDOWN                  0x0800
#define BMCR_ANENABLE               0x1000
#define BMCR_RESET                  0x8000
#define BMSR_LSTATUS                0x0004
#define ADVERTISE_ALL               0x01E1
#define SMSC_SPECIAL_SPEED          0x001C
#define SMSC_SPECIAL_10_HALF        0x0004
#define SMSC_SPECIAL_100_HALF       0x0008
#define SMSC_SPECIAL_10_FULL        0x0014

#define LAN_SETUP_SIZE              64
#define LAN_CONTROL_SIZE            512
#define LAN_BUFFER_SIZE             2048
#define LAN_TX_COUNT                2
#define LAN_MAX_FRAME               1514
#define LAN_MIN_FRAME               14
#define LAN_FCS_SIZE                4

typedef struct _LAN_PIPE
{
    UCHAR Channel;
    UCHAR Address;
    UCHAR Endpoint;
    UCHAR Type;
    USHORT MaxPacket;
    UCHAR Toggle;
} LAN_PIPE, *PLAN_PIPE;

typedef struct _LAN_ADAPTER
{
    PUCHAR Registers;
    PKDNET_SHARED_DATA KdNet;
    PUCHAR SetupBuffer;
    PUCHAR ControlBuffer;
    PUCHAR RxBuffer;
    PUCHAR TxBuffers;
    LAN_PIPE Device;
    LAN_PIPE BulkIn;
    LAN_PIPE BulkOut;
    ULONG Channels;
    ULONG MacControl;
    ULONG LinkPoll;
    ULONG TxNext;
    ULONG RxLength;
    BOOLEAN HighSpeed;
    BOOLEAN RxActive;
    BOOLEAN RxHeld;
    BOOLEAN LinkUp;
} LAN_ADAPTER, *PLAN_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

ULONG
LanRead(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register);

VOID
LanWrite(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value);

VOID
LanStall(
    _In_ ULONG Microseconds);

ULONG
LanPhysical(
    _In_ PVOID Address);

NTSTATUS
DwcInitialize(
    _In_ PLAN_ADAPTER Adapter);

VOID
DwcStop(
    _In_ PLAN_ADAPTER Adapter);

VOID
DwcStart(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ ULONG Pid,
    _In_ BOOLEAN In,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length);

BOOLEAN
DwcFinished(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _Out_ PULONG Status,
    _Out_ PULONG Remaining,
    _Out_ PULONG Packets);

VOID
DwcHalt(
    _In_ PLAN_ADAPTER Adapter,
    _In_ ULONG Channel);

NTSTATUS
DwcTransfer(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ BOOLEAN Setup,
    _In_ BOOLEAN In,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG Actual);

NTSTATUS
UsbControl(
    _In_ PLAN_ADAPTER Adapter,
    _In_ PLAN_PIPE Pipe,
    _In_ UCHAR RequestType,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _In_ USHORT Length,
    _Out_opt_ PULONG Actual);

NTSTATUS
UsbFindAdapter(
    _In_ PLAN_ADAPTER Adapter);

ULONG
NTAPI
LanGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
LanInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
LanShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
LanGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
LanReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
LanGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
LanSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
LanGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
LanGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
