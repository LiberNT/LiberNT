/*
 * PROJECT:     LiberNT VMXNET3 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VMXNET3 device interface and adapter state
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

#define VMX3_TAG 'x3mV'

#define VMX3_PT_IMR 0x000
#define VMX3_PT_TXPROD 0x600
#define VMX3_PT_RXPROD 0x800

#define VMX3_VD_VRRS 0x00
#define VMX3_VD_UVRS 0x08
#define VMX3_VD_DSAL 0x10
#define VMX3_VD_DSAH 0x18
#define VMX3_VD_CMD 0x20
#define VMX3_VD_MACL 0x28
#define VMX3_VD_MACH 0x30
#define VMX3_VD_ICR 0x38
#define VMX3_VD_ECR 0x40

#define VMX3_CMD_ACTIVATE_DEV 0xCAFE0000
#define VMX3_CMD_QUIESCE_DEV 0xCAFE0001
#define VMX3_CMD_RESET_DEV 0xCAFE0002
#define VMX3_CMD_UPDATE_RX_MODE 0xCAFE0003
#define VMX3_CMD_GET_LINK 0xF00D0002
#define VMX3_CMD_GET_CONF_INTR 0xF00D0008

#define VMX3_ECR_LINK 0x00000004

#define VMX3_SHARED_MAGIC 0xBABEFEE1
#define VMX3_IC_DISABLE_ALL 0x00000001
#define VMX3_IMM_AUTO 0
#define VMX3_IML_ADAPTIVE 8
#define VMX3_GOS_BITS_32 1
#define VMX3_GOS_BITS_64 2

#define VMX3_RXM_UCAST 0x01
#define VMX3_RXM_BCAST 0x04
#define VMX3_RXM_ALL_MULTI 0x08
#define VMX3_RXM_PROMISC 0x10

#define VMX3_TXD_LENGTH_MASK 0x00003FFF
#define VMX3_TXD_GEN 0x00004000
#define VMX3_TXD_EOP 0x00001000
#define VMX3_TXD_CQ 0x00002000
#define VMX3_TXCD_GEN 0x80000000
#define VMX3_RXD_GEN 0x80000000
#define VMX3_RXCD_GEN 0x80000000
#define VMX3_RXCD_INDEX_MASK 0x00000FFF
#define VMX3_RXCD_EOP 0x00004000
#define VMX3_RXCD_SOP 0x00008000
#define VMX3_RXCD_LENGTH_MASK 0x00003FFF
#define VMX3_RXCD_ERROR 0x00004000
#define VMX3_TXCD_INDEX_MASK 0x00000FFF

#define VMX3_TX_RING_SIZE 256
#define VMX3_RX_RING_SIZE 256
#define VMX3_BUFFER_SIZE 2048
#define VMX3_MTU 1500
#define VMX3_FRAME_SIZE 1514
#define VMX3_RX_BUDGET 128
#define VMX3_MAX_MULTICAST 32
#define VMX3_DRIVER_VERSION 0x0100
#define VMX3_MAC_OPTIONS (NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | \
                          NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | \
                          NDIS_MAC_OPTION_NO_LOOPBACK)
#define VMX3_SUPPORTED_FILTERS (NDIS_PACKET_TYPE_DIRECTED | \
                                NDIS_PACKET_TYPE_MULTICAST | \
                                NDIS_PACKET_TYPE_ALL_MULTICAST | \
                                NDIS_PACKET_TYPE_BROADCAST | \
                                NDIS_PACKET_TYPE_PROMISCUOUS)

typedef struct _VMX3_TX_DESC
{
    ULONG64 Address;
    ULONG Flags[2];
} VMX3_TX_DESC, *PVMX3_TX_DESC;

typedef struct _VMX3_TX_COMP
{
    ULONG Index;
    ULONG Reserved[2];
    ULONG Flags;
} VMX3_TX_COMP, *PVMX3_TX_COMP;

typedef struct _VMX3_RX_DESC
{
    ULONG64 Address;
    ULONG Flags;
    ULONG Reserved;
} VMX3_RX_DESC, *PVMX3_RX_DESC;

typedef struct _VMX3_RX_COMP
{
    ULONG Index;
    ULONG RssHash;
    ULONG Length;
    ULONG Flags;
} VMX3_RX_COMP, *PVMX3_RX_COMP;

typedef struct _VMX3_TX_QUEUE_DESC
{
    ULONG NumDeferred;
    ULONG Threshold;
    ULONG64 Reserved0;
    ULONG64 DescAddress;
    ULONG64 DataAddress;
    ULONG64 CompAddress;
    ULONG64 DriverDataAddress;
    ULONG64 Reserved1;
    ULONG NumDesc;
    ULONG NumData;
    ULONG NumComp;
    ULONG DriverDataLength;
    UCHAR IntrIndex;
    UCHAR Reserved2[7];
    UCHAR Stopped;
    UCHAR Reserved3[3];
    ULONG Error;
    ULONG64 Stats[10];
    UCHAR Reserved4[88];
} VMX3_TX_QUEUE_DESC, *PVMX3_TX_QUEUE_DESC;

typedef struct _VMX3_RX_QUEUE_DESC
{
    UCHAR UpdateProducer;
    UCHAR Reserved0[7];
    ULONG64 Reserved1;
    ULONG64 DescAddress[2];
    ULONG64 CompAddress;
    ULONG64 DriverDataAddress;
    ULONG64 Reserved2;
    ULONG NumDesc[2];
    ULONG NumComp;
    ULONG DriverDataLength;
    UCHAR IntrIndex;
    UCHAR Reserved3[7];
    UCHAR Stopped;
    UCHAR Reserved4[3];
    ULONG Error;
    ULONG64 Stats[10];
    UCHAR Reserved5[88];
} VMX3_RX_QUEUE_DESC, *PVMX3_RX_QUEUE_DESC;

typedef struct _VMX3_QUEUES
{
    VMX3_TX_QUEUE_DESC Tx;
    VMX3_RX_QUEUE_DESC Rx;
} VMX3_QUEUES, *PVMX3_QUEUES;

typedef struct _VMX3_SHARED
{
    ULONG Magic;
    ULONG Reserved0;
    ULONG DriverVersion;
    ULONG GuestInfo;
    ULONG VersionSupport;
    ULONG UptVersionSupport;
    ULONG64 UptFeatures;
    ULONG64 DriverDataAddress;
    ULONG64 QueueDescAddress;
    ULONG DriverDataLength;
    ULONG QueueDescLength;
    ULONG Mtu;
    USHORT MaxNumRxSg;
    UCHAR NumTxQueues;
    UCHAR NumRxQueues;
    ULONG Reserved1[4];
    UCHAR AutoMask;
    UCHAR NumIntrs;
    UCHAR EventIntrIndex;
    UCHAR ModLevels[25];
    ULONG IntrCtrl;
    ULONG Reserved2[2];
    ULONG RxMode;
    USHORT MulticastTableLength;
    USHORT Reserved3;
    ULONG64 MulticastTableAddress;
    ULONG VlanFilter[128];
    ULONG RssVersion;
    ULONG RssLength;
    ULONG64 RssAddress;
    ULONG PmVersion;
    ULONG PmLength;
    ULONG64 PmAddress;
    ULONG PluginVersion;
    ULONG PluginLength;
    ULONG64 PluginAddress;
    ULONG Ecr;
    ULONG Reserved4[5];
} VMX3_SHARED, *PVMX3_SHARED;

C_ASSERT(sizeof(VMX3_TX_DESC) == 16);
C_ASSERT(sizeof(VMX3_TX_COMP) == 16);
C_ASSERT(sizeof(VMX3_RX_DESC) == 16);
C_ASSERT(sizeof(VMX3_RX_COMP) == 16);
C_ASSERT(sizeof(VMX3_TX_QUEUE_DESC) == 256);
C_ASSERT(sizeof(VMX3_RX_QUEUE_DESC) == 256);
C_ASSERT(FIELD_OFFSET(VMX3_SHARED, AutoMask) == 80);
C_ASSERT(FIELD_OFFSET(VMX3_SHARED, IntrCtrl) == 108);
C_ASSERT(FIELD_OFFSET(VMX3_SHARED, RxMode) == 120);
C_ASSERT(FIELD_OFFSET(VMX3_SHARED, RssVersion) == 648);
C_ASSERT(FIELD_OFFSET(VMX3_SHARED, Ecr) == 696);
C_ASSERT(sizeof(VMX3_SHARED) == 720);

#define VMX3_DMA_TX_RING 0
#define VMX3_DMA_TX_COMP (VMX3_DMA_TX_RING + VMX3_TX_RING_SIZE * sizeof(VMX3_TX_DESC))
#define VMX3_DMA_RX_RING (VMX3_DMA_TX_COMP + VMX3_TX_RING_SIZE * sizeof(VMX3_TX_COMP))
#define VMX3_DMA_RX_COMP (VMX3_DMA_RX_RING + VMX3_RX_RING_SIZE * sizeof(VMX3_RX_DESC))
#define VMX3_DMA_QUEUES (VMX3_DMA_RX_COMP + VMX3_RX_RING_SIZE * sizeof(VMX3_RX_COMP))
#define VMX3_DMA_SHARED (VMX3_DMA_QUEUES + sizeof(VMX3_QUEUES))
#define VMX3_DMA_LENGTH (VMX3_DMA_SHARED + sizeof(VMX3_SHARED))

C_ASSERT((VMX3_DMA_TX_COMP % 512) == 0);
C_ASSERT((VMX3_DMA_RX_RING % 512) == 0);
C_ASSERT((VMX3_DMA_RX_COMP % 512) == 0);
C_ASSERT((VMX3_DMA_QUEUES % 128) == 0);
C_ASSERT((PAGE_SIZE % VMX3_BUFFER_SIZE) == 0);

typedef struct _VMX3_BUFFER
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
} VMX3_BUFFER, *PVMX3_BUFFER;

typedef struct _VMX3_RX_SLOT
{
    VMX3_BUFFER Buffer;
    PMDL Mdl;
    PNET_BUFFER_LIST NetBufferList;
} VMX3_RX_SLOT, *PVMX3_RX_SLOT;

typedef struct _VMX3_ADAPTER
{
    NDIS_HANDLE MiniportHandle;
    NDIS_HANDLE InterruptHandle;
    NDIS_HANDLE RxNblPool;
    PUCHAR Pt;
    ULONG PtLength;
    NDIS_PHYSICAL_ADDRESS PtPhysical;
    PUCHAR Vd;
    ULONG VdLength;
    NDIS_PHYSICAL_ADDRESS VdPhysical;
    PUCHAR Dma;
    NDIS_PHYSICAL_ADDRESS DmaPhysical;
    PVMX3_TX_DESC TxRing;
    PVMX3_TX_COMP TxComp;
    PVMX3_RX_DESC RxRing;
    PVMX3_RX_COMP RxComp;
    PVMX3_QUEUES Queues;
    PVMX3_SHARED Shared;
    PVOID TxPages[VMX3_TX_RING_SIZE * VMX3_BUFFER_SIZE / PAGE_SIZE];
    PVOID RxPages[VMX3_RX_RING_SIZE * VMX3_BUFFER_SIZE / PAGE_SIZE];
    VMX3_BUFFER Tx[VMX3_TX_RING_SIZE];
    VMX3_RX_SLOT Rx[VMX3_RX_RING_SIZE];
    ULONG TxProduced;
    ULONG TxCompleted;
    ULONG RxProduced;
    ULONG RxCompleted;
    NDIS_SPIN_LOCK TxLock;
    NDIS_SPIN_LOCK RxLock;
    NDIS_SPIN_LOCK CommandLock;
    BOOLEAN Activated;
    BOOLEAN Paused;
    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR MulticastList[VMX3_MAX_MULTICAST * ETH_LENGTH_OF_ADDRESS];
    ULONG MulticastCount;
    ULONG PacketFilter;
    ULONG Lookahead;
    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    ULONG64 LinkSpeed;
    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;
} VMX3_ADAPTER, *PVMX3_ADAPTER;
