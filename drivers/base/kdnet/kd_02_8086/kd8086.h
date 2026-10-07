/*
 * PROJECT:     LiberNT Intel Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intel 8254x and 82574 register layout and module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>

#define E1K_REG_CTRL    0x0000
#define E1K_REG_STATUS  0x0008
#define E1K_REG_EECD    0x0010
#define E1K_REG_EERD    0x0014
#define E1K_REG_MDIC    0x0020
#define E1K_REG_FCAL    0x0028
#define E1K_REG_FCAH    0x002C
#define E1K_REG_FCT     0x0030
#define E1K_REG_ICR     0x00C0
#define E1K_REG_IMC     0x00D8
#define E1K_REG_RCTL    0x0100
#define E1K_REG_FCTTV   0x0170
#define E1K_REG_TCTL    0x0400
#define E1K_REG_TIPG    0x0410
#define E1K_REG_EIMC    0x1528
#define E1K_REG_RX      0x2800
#define E1K_REG_TX      0x3800
#define E1K_REG_RX_IGB  0xC000
#define E1K_REG_TX_IGB  0xE000

#define E1K_QUEUE_BAL       0x00
#define E1K_QUEUE_BAH       0x04
#define E1K_QUEUE_LEN       0x08
#define E1K_QUEUE_SRRCTL    0x0C
#define E1K_QUEUE_HEAD      0x10
#define E1K_QUEUE_TAIL      0x18
#define E1K_QUEUE_DCTL      0x28
#define E1K_REG_MTA     0x5200
#define E1K_REG_RAL     0x5400
#define E1K_REG_RAH     0x5404

#define E1K_CTRL_LRST       0x00000008
#define E1K_CTRL_ASDE       0x00000020
#define E1K_CTRL_SLU        0x00000040
#define E1K_CTRL_ILOS       0x00000080
#define E1K_CTRL_RST        0x04000000
#define E1K_CTRL_VME        0x40000000
#define E1K_CTRL_PHY_RST    0x80000000

#define E1K_STATUS_FD       0x00000001
#define E1K_STATUS_LU       0x00000002
#define E1K_STATUS_SPEED    0x000000C0
#define E1K_STATUS_SPEED_10   0x00000000
#define E1K_STATUS_SPEED_100  0x00000040

#define E1K_EECD_AUTO_RD     0x00000200

#define E1K_EERD_START      0x00000001

#define E1K_MDIC_REGISTER   16
#define E1K_MDIC_ADDRESS    0x00200000
#define E1K_MDIC_WRITE      0x04000000
#define E1K_MDIC_READ       0x08000000
#define E1K_MDIC_READY      0x10000000
#define E1K_MDIC_ERROR      0x40000000

#define E1K_PHY_CONTROL             0
#define E1K_PHY_CONTROL_RESTART     0x0200
#define E1K_PHY_CONTROL_AUTONEG     0x1000

#define E1K_SRRCTL_BSIZEPACKET  0x0000007F
#define E1K_SRRCTL_DESCTYPE     0x0E000000
#define E1K_SRRCTL_2K           0x00000002
#define E1K_SRRCTL_ADVANCED     0x02000000

#define E1K_DCTL_ENABLE     0x02000000

#define E1K_RCTL_EN         0x00000002
#define E1K_RCTL_BAM        0x00008000
#define E1K_RCTL_SECRC      0x04000000

#define E1K_TCTL_EN         0x00000002
#define E1K_TCTL_PSP        0x00000008
#define E1K_TCTL_CT         0x000000F0
#define E1K_TCTL_COLD       0x00040000

#define E1K_TIPG_COPPER     0x0060200A

#define E1K_RAH_AV          0x80000000

#define E1K_MTA_ENTRIES     128

#define E1K_RXD_STATUS_DD   0x01
#define E1K_RXD_STATUS_EOP  0x02
#define E1K_RXD_ADVANCED_ERROR  0x80000000

#define E1K_TXD_CMD_EOP     0x01
#define E1K_TXD_CMD_IFCS    0x02
#define E1K_TXD_CMD_RS      0x08
#define E1K_TXD_STATUS_DD   0x01

#include <pshpack1.h>

typedef struct _E1K_RX_DESCRIPTOR
{
    ULONG64 Address;
    USHORT Length;
    USHORT Checksum;
    UCHAR Status;
    UCHAR Errors;
    USHORT Special;
} E1K_RX_DESCRIPTOR, *PE1K_RX_DESCRIPTOR;

typedef struct _E1K_TX_DESCRIPTOR
{
    ULONG64 Address;
    USHORT Length;
    UCHAR ChecksumOffset;
    UCHAR Command;
    UCHAR Status;
    UCHAR ChecksumStart;
    USHORT Special;
} E1K_TX_DESCRIPTOR, *PE1K_TX_DESCRIPTOR;

#include <poppack.h>

typedef struct _E1K_RX_ADVANCED_DESCRIPTOR
{
    ULONG64 Address;
    ULONG StatusError;
    USHORT Length;
    USHORT Vlan;
} E1K_RX_ADVANCED_DESCRIPTOR, *PE1K_RX_ADVANCED_DESCRIPTOR;

C_ASSERT(sizeof(E1K_RX_DESCRIPTOR) == 16);
C_ASSERT(sizeof(E1K_RX_ADVANCED_DESCRIPTOR) == 16);
C_ASSERT(sizeof(E1K_TX_DESCRIPTOR) == 16);

#define E1K_RX_COUNT        64
#define E1K_TX_COUNT        16
#define E1K_BUFFER_SIZE     2048
#define E1K_MAX_FRAME       1514
#define E1K_MIN_FRAME       14

typedef enum _E1K_CLASS
{
    E1kClassNone,
    E1kClass8254x,
    E1kClass82574,
    E1kClassIgb
} E1K_CLASS;

typedef struct _E1K_ADAPTER
{
    PUCHAR Registers;
    E1K_CLASS Class;
    ULONG RxBase;
    ULONG TxBase;
    PKDNET_SHARED_DATA KdNet;
    volatile E1K_RX_DESCRIPTOR *RxRing;
    volatile E1K_TX_DESCRIPTOR *TxRing;
    PUCHAR RxBuffers;
    PUCHAR TxBuffers;
    ULONG RxNext;
    ULONG TxNext;
    ULONG LinkPoll;
    USHORT RxLength[E1K_RX_COUNT];
    BOOLEAN TxSubmitted[E1K_TX_COUNT];
} E1K_ADAPTER, *PE1K_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

E1K_CLASS
E1kClassifyDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

ULONG
NTAPI
E1kGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
E1kInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
E1kShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
E1kGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
E1kReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
E1kGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
E1kSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
E1kGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
E1kGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
