/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Realtek 8139 driver
 * FILE:        rtl8139.h
 * PURPOSE:     RTL8139 driver definitions
 */

#ifndef _RTL8139_PCH_
#define _RTL8139_PCH_

#include <ndis.h>

#include "rtlhw.h"

#define ADAPTER_TAG 'Altr'
#define RECEIVE_TAG 'Rltr'
#define RECEIVE_NBL_COUNT 64

#define MAX_RESET_ATTEMPTS 25
#define RECEIVE_BUFFER_SIZE      (32768)
// The full receive window requires 16 bytes of padding
// plus 2048 for receive overflows (because of WRAP)
#define FULL_RECEIVE_BUFFER_SIZE (32768 + 16 + 2048)
#define RECV_CRC_LENGTH 4

#define MINIMUM_FRAME_SIZE 60
#define MAXIMUM_FRAME_SIZE 1514
// RTL8139 transmit start-address registers and buffers are DWORD-aligned.
#define TRANSMIT_BUFFER_STRIDE ((MAXIMUM_FRAME_SIZE + sizeof(ULONG) - 1) & ~(sizeof(ULONG) - 1))
#define TRANSMIT_BUFFER_LENGTH (TRANSMIT_BUFFER_STRIDE * TX_DESC_COUNT)

#define DRIVER_VERSION 1

// 1/2 packet early RX, 512 byte FIFO threshold, 32K RX buffer, unlimited DMA bursts, WRAP
#define RC_VAL (0x800B780)

// 2048 byte DMA bursts
#define TC_VAL (0x700)

typedef struct _RTL_ADAPTER {
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_SPIN_LOCK Lock;

    ULONG IoRangeStart;
    ULONG IoRangeLength;

    ULONG InterruptVector;
    ULONG InterruptLevel;
    BOOLEAN InterruptShared;
    ULONG InterruptFlags;

    PUCHAR IoBase;
    NDIS_HANDLE InterruptHandle;
    NDIS_HANDLE DmaHandle;
    BOOLEAN DmaActive;
    BOOLEAN Paused;
    BOOLEAN PausePending;

    NDIS_HANDLE ReceiveNblPool;
    PNET_BUFFER_LIST FreeReceiveNbls;
    PNET_BUFFER_LIST ReceiveNbls[RECEIVE_NBL_COUNT];
    ULONG ReceiveNblsOutstanding;

    PNET_BUFFER_LIST SendQueueHead;
    PNET_BUFFER_LIST SendQueueTail;

    UCHAR PermanentMacAddress[IEEE_802_ADDR_LENGTH];
    UCHAR CurrentMacAddress[IEEE_802_ADDR_LENGTH];
    struct {
        UCHAR MacAddress[IEEE_802_ADDR_LENGTH];
    } MulticastList[MAXIMUM_MULTICAST_ADDRESSES];

    ULONG ReceiveBufferLength;
    PUCHAR ReceiveBuffer;
    NDIS_PHYSICAL_ADDRESS ReceiveBufferPa;
    USHORT ReceiveOffset;

    ULONG LinkSpeedMbps;
    ULONG MediaState;
    BOOLEAN LinkChange;

    ULONG PacketFilter;

    USHORT InterruptMask;
    USHORT InterruptPending;

    UCHAR DirtyTxDesc;
    UCHAR CurrentTxDesc;
    BOOLEAN TxFull;
    PUCHAR TransmitBuffers;
    NDIS_PHYSICAL_ADDRESS TransmitBuffersPa;

    ULONG ReceiveOk;
    ULONG TransmitOk;
    ULONG ReceiveError;
    ULONG TransmitError;
    ULONG ReceiveNoBufferSpace;
    ULONG ReceiveCrcError;
    ULONG ReceiveAlignmentError;
    ULONG TransmitOneCollision;
    ULONG TransmitMoreCollisions;

} RTL_ADAPTER, *PRTL_ADAPTER;

NDIS_STATUS
NTAPI
NICPowerOn (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICSoftReset (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICRegisterReceiveBuffer (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICRemoveReceiveBuffer (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICEnableTxRx (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICGetPermanentMacAddress (
    IN PRTL_ADAPTER Adapter,
    OUT PUCHAR MacAddress
    );

NDIS_STATUS
NTAPI
NICApplyPacketFilter (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICApplyInterruptMask (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICDisableInterrupts (
    IN PRTL_ADAPTER Adapter
    );

USHORT
NTAPI
NICInterruptRecognized (
    IN PRTL_ADAPTER Adapter,
    OUT PBOOLEAN InterruptRecognized
    );

VOID
NTAPI
NICAcknowledgeInterrupts (
    IN PRTL_ADAPTER Adapter
    );

VOID
NTAPI
NICUpdateLinkStatus (
    IN PRTL_ADAPTER Adapter
    );

NDIS_STATUS
NTAPI
NICTransmitPacket (
    IN PRTL_ADAPTER Adapter,
    IN UCHAR TxDesc,
    IN ULONG PhysicalAddress,
    IN ULONG Length
    );

extern NDIS_OID RtlSupportedOids[];
extern ULONG RtlSupportedOidCount;

MINIPORT_OID_REQUEST MiniportOidRequest;
MINIPORT_CANCEL_OID_REQUEST MiniportCancelOidRequest;
MINIPORT_ISR MiniportIsr;
MINIPORT_INTERRUPT_DPC MiniportInterruptDpc;
MINIPORT_DISABLE_INTERRUPT MiniportDisableInterrupt;
MINIPORT_ENABLE_INTERRUPT MiniportEnableInterrupt;

VOID
RtlIndicateLinkState (
    IN PRTL_ADAPTER Adapter
    );

VOID
RtlProcessSendQueue (
    IN PRTL_ADAPTER Adapter,
    IN OUT PNET_BUFFER_LIST *CompleteHead,
    IN OUT PNET_BUFFER_LIST *CompleteTail
    );

VOID
RtlCompleteSends (
    IN PRTL_ADAPTER Adapter,
    IN PNET_BUFFER_LIST CompleteHead,
    IN BOOLEAN DispatchLevel
    );

#endif /* _RTL8139_PCH_ */
