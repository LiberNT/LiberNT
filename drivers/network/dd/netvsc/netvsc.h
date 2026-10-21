/*
 * PROJECT:     LiberNT Hyper-V Network Adapter Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Definitions for the NVSP/RNDIS miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ndis.h>
#include <vmbusapi.h>

#define NETVSC_TAG                 'svNL'
#define NETVSC_RING_SIZE           (256 * 1024)
#define NETVSC_RECEIVE_BUFFER_SIZE (4 * 1024 * 1024)
#define NETVSC_SEND_BUFFER_SIZE    (1024 * 1024)
#define NETVSC_RECEIVE_BUFFER_ID   0xCAFE
#define NETVSC_SEND_BUFFER_ID      0xFACE
#define NETVSC_PACKET_BUFFER_SIZE  (16 * 1024)
#define NETVSC_MAX_SEND_SECTIONS   1024
#define NETVSC_RECEIVE_NBL_COUNT   256
#define NETVSC_FRAME_BUFFER_SIZE   1536
#define NETVSC_MAX_FRAME_SIZE      1514
#define NETVSC_MTU                 1500
#define NETVSC_ETH_HEADER_SIZE     14
#define NETVSC_MAX_MULTICAST       32
#define NETVSC_WAIT_MS             10000

#define NETVSC_TID_CONTROL 0x8000000000000000ULL
#define NETVSC_TID_SECTION 0x4000000000000000ULL

#define NVSP_VERSION_1  0x00000002
#define NVSP_VERSION_2  0x00030002
#define NVSP_VERSION_4  0x00040000
#define NVSP_VERSION_5  0x00050000

#define NVSP_MSG_INIT                       1
#define NVSP_MSG_INIT_COMPLETE              2
#define NVSP_MSG1_SEND_NDIS_VERSION         100
#define NVSP_MSG1_SEND_RECEIVE_BUFFER       101
#define NVSP_MSG1_SEND_RECEIVE_BUFFER_DONE  102
#define NVSP_MSG1_REVOKE_RECEIVE_BUFFER     103
#define NVSP_MSG1_SEND_SEND_BUFFER          104
#define NVSP_MSG1_SEND_SEND_BUFFER_DONE     105
#define NVSP_MSG1_REVOKE_SEND_BUFFER        106
#define NVSP_MSG1_SEND_RNDIS_PACKET         107
#define NVSP_MSG1_SEND_RNDIS_PACKET_DONE    108
#define NVSP_MSG2_SEND_NDIS_CONFIG          125

#define NVSP_STATUS_SUCCESS  1

#define NVSP_CHANNEL_DATA     0
#define NVSP_CHANNEL_CONTROL  1

#define NVSP_MESSAGE_SIZE 40

#define RNDIS_MSG_PACKET            0x00000001
#define RNDIS_MSG_INITIALIZE        0x00000002
#define RNDIS_MSG_HALT              0x00000003
#define RNDIS_MSG_QUERY             0x00000004
#define RNDIS_MSG_SET               0x00000005
#define RNDIS_MSG_INDICATE_STATUS   0x00000007
#define RNDIS_MSG_KEEPALIVE         0x00000008
#define RNDIS_MSG_INITIALIZE_CMPLT  0x80000002
#define RNDIS_MSG_QUERY_CMPLT       0x80000004
#define RNDIS_MSG_SET_CMPLT         0x80000005
#define RNDIS_MSG_KEEPALIVE_CMPLT   0x80000008

#define RNDIS_STATUS_SUCCESS          0x00000000
#define RNDIS_STATUS_MEDIA_CONNECT    0x4001000B
#define RNDIS_STATUS_MEDIA_DISCONNECT 0x4001000C

#define RNDIS_PACKET_HEADER_SIZE 44
#define RNDIS_RESPONSE_SIZE      256

typedef struct _NETVSC_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_SPIN_LOCK Lock;
    PVMBUS_CHANNEL Channel;
    BOOLEAN ChannelOpen;
    BOOLEAN Ready;
    BOOLEAN Paused;
    BOOLEAN PausePending;

    ULONG NvspVersion;
    PUCHAR PacketBuffer;

    PUCHAR ReceiveBuffer;
    ULONG ReceiveGpadl;
    BOOLEAN ReceiveBufferPosted;

    PUCHAR SendBuffer;
    ULONG SendGpadl;
    BOOLEAN SendBufferPosted;
    ULONG SendSectionSize;
    ULONG SendSectionCount;
    ULONG SendSectionsUsed;
    ULONG SendSectionBitmap[NETVSC_MAX_SEND_SECTIONS / 32];

    NDIS_EVENT NvspEvent;
    volatile ULONG64 NvspExpectedTid;
    UCHAR NvspResponse[NVSP_MESSAGE_SIZE];

    NDIS_EVENT RndisEvent;
    ULONG RndisRequestId;
    volatile ULONG RndisPendingId;
    PNDIS_OID_REQUEST PendingOidRequest;
    UCHAR RndisResponse[RNDIS_RESPONSE_SIZE];
    ULONG RndisResponseLength;

    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR MulticastList[NETVSC_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    ULONG MulticastCount;
    ULONG PacketFilter;
    ULONG MediaState;
    ULONG64 LinkSpeed;
    BOOLEAN LinkChange;

    NDIS_HANDLE ReceiveNblPool;
    PNET_BUFFER_LIST FreeReceiveNbls;
    PNET_BUFFER_LIST ReceiveNbls[NETVSC_RECEIVE_NBL_COUNT];
    ULONG ReceiveNblsOutstanding;

    PNET_BUFFER_LIST SendQueueHead;
    PNET_BUFFER_LIST SendQueueTail;

    ULONG64 TransmitOk;
    ULONG64 TransmitError;
    ULONG64 ReceiveOk;
    ULONG64 ReceiveError;
    ULONG64 ReceiveNoBuffer;
} NETVSC_ADAPTER, *PNETVSC_ADAPTER;

extern NDIS_OID NvscSupportedOids[];
extern ULONG NvscSupportedOidCount;

NDIS_STATUS
NvscStartDevice(
    _Inout_ PNETVSC_ADAPTER Adapter);

VOID
NvscStopDevice(
    _Inout_ PNETVSC_ADAPTER Adapter);

NDIS_STATUS
NvscRndisSetPacketFilter(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _In_ ULONG Filter,
    _In_opt_ PNDIS_OID_REQUEST OidRequest);

VOID
NvscProcessSendQueue(
    _Inout_ PNETVSC_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST *CompleteHead,
    _Inout_ PNET_BUFFER_LIST *CompleteTail);

VOID
NvscCompleteSends(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST CompleteHead,
    _In_ BOOLEAN DispatchLevel);

VOID
NvscIndicateLinkState(
    _In_ PNETVSC_ADAPTER Adapter);

MINIPORT_OID_REQUEST NvscOidRequest;
MINIPORT_CANCEL_OID_REQUEST NvscCancelOidRequest;
