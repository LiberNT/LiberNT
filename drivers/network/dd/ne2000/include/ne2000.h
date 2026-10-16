/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Novell Eagle 2000 driver
 * FILE:        include/ne2000.h
 * PURPOSE:     NE2000 driver definitions
 */

#ifndef _NE2000_PCH_
#define _NE2000_PCH_

#include <ndis.h>
#include <8390.h>

#include "debug.h"

/* Define NOCARD to test NDIS without a card */
//#define NOCARD

/* NE2000 specific constants */
#define NIC_DATA            0x10    /* Data register */
#define NIC_RESET           0x1F    /* Reset register */


/* Global constants */

#define DRIVER_MAX_MULTICAST_LIST_SIZE  8

#define DRIVER_MINIMUM_FRAME_SIZE   60
#define DRIVER_RECEIVE_NBL_COUNT    32
#define DRIVER_TAG                  'k2EN'

#define DRIVER_VENDOR_DESCRIPTION       "Novell Eagle 2000 Adapter."
#define DRIVER_VENDOR_DRIVER_VERSION    0x0100  /* 1.0 */

#define DRIVER_FRAME_SIZE           1514    /* Size of an ethernet frame */
#define DRIVER_HEADER_SIZE          14      /* Size of an ethernet header */
#define DRIVER_LENGTH_OF_ADDRESS    6       /* Size of an ethernet address */

/* Maximum lookahead buffer size */
#define DRIVER_MAXIMUM_LOOKAHEAD (252 - DRIVER_HEADER_SIZE)

/* Size of a block in a buffer ring */
#define DRIVER_BLOCK_SIZE   256


/* Default number of transmit buffers */
#define DRIVER_DEFAULT_TX_BUFFER_COUNT 12
#define BUFFERS_PER_TX_BUF 1

/* Interrupt Mask Register value */
#define DRIVER_INTERRUPT_MASK   IMR_ALLE - IMR_RDCE

/* Maximum number of interrupts handled per call to MiniportHandleInterrupt */
#define INTERRUPT_LIMIT 10

/* Global structures */

typedef UCHAR DRIVER_HARDWARE_ADDRESS[DRIVER_LENGTH_OF_ADDRESS];

/* Information about an adapter */
typedef struct _NIC_ADAPTER
{
    /* Entry on global adapter list */
    LIST_ENTRY ListEntry;
    /* Adapter handle */
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_HANDLE InterruptHandle;
    NDIS_SPIN_LOCK Lock;
    BOOLEAN Paused;
    BOOLEAN PausePending;
    BOOLEAN InterruptsDisabled;

    /* I/O base address and interrupt number of adapter */
    ULONG_PTR IoBaseAddress;

    /* Mapped address of the I/O base port */
    PUCHAR IOBase;

    /* TRUE if the NIC can transfer in word mode */
    BOOLEAN WordMode;

    /* Base address and size of the onboard memory window */
    PUCHAR RamBase;
    UINT RamSize;

    /* Station Address PROM (SAPROM) */
    UCHAR SAPROM[16];

    /* Onboard ethernet address from the manufacturer */
    DRIVER_HARDWARE_ADDRESS PermanentAddress;

    /* Ethernet address currently in use */
    DRIVER_HARDWARE_ADDRESS StationAddress;

    /* Maximum number of multicast addresses this adapter supports */
    ULONG MaxMulticastListSize;

    /* List of multicast addresses in use */
    DRIVER_HARDWARE_ADDRESS Addresses[DRIVER_MAX_MULTICAST_LIST_SIZE];

    /* Current multicast address mask */
    UCHAR MulticastAddressMask[8];

    /* Masked interrupts (IMR value) */
    ULONG InterruptMask;

    /* Interrupts that have occurred */
    UCHAR InterruptStatus;

    /* Current packet filter */
    ULONG PacketFilter;

    /* Lookahead buffer */
    UINT LookaheadSize;

    /* Receive buffer ring */
    UINT PageStart;
    UINT PageStop;
    UINT CurrentPage;
    UINT NextPacket;

    /* TRUE if there was a buffer overflow */
    BOOLEAN BufferOverflow;

    /* TRUE if an error occurred during reception of a packet */
    BOOLEAN ReceiveError;

    /* TRUE if an error occurred during transmission of a packet */
    BOOLEAN TransmitError;

    /* TRUE if a transmit interrupt is pending */
    BOOLEAN TransmitPending;

    /* Received packet header */
    PACKET_HEADER PacketHeader;

    /* Offset in onboard RAM of received packet */
    ULONG PacketOffset;

    NDIS_HANDLE ReceiveNblPool;
    PNET_BUFFER_LIST FreeReceiveNbls;
    PNET_BUFFER_LIST ReceiveNbls[DRIVER_RECEIVE_NBL_COUNT];
    ULONG ReceiveNblsOutstanding;
    PNET_BUFFER_LIST ReceiveHead;
    PNET_BUFFER_LIST ReceiveTail;
    ULONG ReceiveCount;

    /* Transmit buffers */
    UINT TXStart;   /* Start block of transmit buffer ring */
    UINT TXCount;   /* Number of blocks in transmit buffer ring */
    UINT TXFree;    /* Number of free transmit buffers */
    UINT TXNext;    /* Next buffer to use */
    /* Length of packet. 0 means buffer is unused */
    UINT TXSize[DRIVER_DEFAULT_TX_BUFFER_COUNT];
    INT TXCurrent;  /* Current buffer beeing transmitted. -1 means none */

    /* Head of transmit queue */
    PNET_BUFFER_LIST TXQueueHead;
    /* Tail of transmit queue */
    PNET_BUFFER_LIST TXQueueTail;
    PNET_BUFFER_LIST CompleteHead;
    PNET_BUFFER_LIST CompleteTail;
    UCHAR TXBuffer[DRIVER_FRAME_SIZE];

    /* Statistics */
    ULONG FrameAlignmentErrors;
    ULONG CrcErrors;
    ULONG MissedPackets;
    ULONG ReceiveOk;
    ULONG ReceiveNoBuffers;
    ULONG TransmitOk;
    ULONG TransmitErrors;

    /* Flags used for driver cleanup */
    BOOLEAN IOPortRangeRegistered;
} NIC_ADAPTER, *PNIC_ADAPTER;

/* Global driver information */
typedef struct _DRIVER_INFORMATION
{
    NDIS_HANDLE NdisDriverHandle;
    LIST_ENTRY  AdapterListHead;    /* Adapters this driver control */
} DRIVER_INFORMATION, *PDRIVER_INFORMATION;



/* Global variable */

extern DRIVER_INFORMATION DriverInfo;



/* Prototypes */

BOOLEAN NICCheck(
    PNIC_ADAPTER Adapter);

NDIS_STATUS NICInitialize(
    PNIC_ADAPTER Adapter);

NDIS_STATUS NICSetup(
    PNIC_ADAPTER Adapter);

NDIS_STATUS NICStart(
    PNIC_ADAPTER Adapter);

NDIS_STATUS NICStop(
    PNIC_ADAPTER Adapter);

NDIS_STATUS NICReset(
    PNIC_ADAPTER Adapter);

VOID NICUpdateCounters(
    PNIC_ADAPTER Adapter);

VOID NICReadDataAlign(
    PNIC_ADAPTER Adapter,
    PUSHORT Target,
    ULONG_PTR Source,
    USHORT Length);

VOID NICWriteDataAlign(
    PNIC_ADAPTER Adapter,
    ULONG_PTR Target,
    PUSHORT Source,
    USHORT Length);

VOID NICReadData(
    PNIC_ADAPTER Adapter,
    PUCHAR Target,
    ULONG_PTR Source,
    USHORT Length);

VOID NICWriteData(
    PNIC_ADAPTER Adapter,
    ULONG_PTR Target,
    PUCHAR Source,
    USHORT Length);

VOID NICTransmit(
    PNIC_ADAPTER Adapter);

VOID NICAppendNbl(
    PNET_BUFFER_LIST *Head,
    PNET_BUFFER_LIST *Tail,
    PNET_BUFFER_LIST Nbl);

#endif /* _NE2000_PCH_ */
