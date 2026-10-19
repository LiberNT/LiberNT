/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI host framework shared definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NDIS6_WDI_H_
#define _NDIS6_WDI_H_

#include "ndis6_internal.h"
#include <windot11.h>

#define NDIS6_WDI_TAG           'iWNn'
#define NDIS6_WDI_SIGNATURE     'aWNn'

#define WDI_EXT_TID_NON_QOS     ((WDI_EXTENDED_TID)16)

#define WDI_TLV_STATUS                              0x1
#define WDI_TLV_BSSID                               0x2
#define WDI_TLV_CHANNEL_LIST                        0x4
#define WDI_TLV_SCAN_MODE                           0x6
#define WDI_TLV_SCAN_DWELL_TIME                     0x7
#define WDI_TLV_BSS_ENTRY                           0x8
#define WDI_TLV_PROBE_RESPONSE_FRAME                0x9
#define WDI_TLV_BEACON_FRAME                        0xA
#define WDI_TLV_BSS_ENTRY_SIGNAL_INFO               0xB
#define WDI_TLV_BSS_ENTRY_DEVICE_CONTEXT            0xD
#define WDI_TLV_INTERFACE_CAPABILITIES              0xF
#define WDI_TLV_VIRTUALIZATION_CAPABILITIES         0x10
#define WDI_TLV_STATION_CAPABILITIES                0x11
#define WDI_TLV_UNICAST_ALGORITHM_LIST              0x13
#define WDI_TLV_MULTICAST_DATA_ALGORITHM_LIST       0x14
#define WDI_TLV_BAND_CAPABILITIES                   0x1A
#define WDI_TLV_PHY_CAPABILITIES                    0x1B
#define WDI_TLV_INTERFACE_ATTRIBUTES                0x21
#define WDI_TLV_STATION_ATTRIBUTES                  0x22
#define WDI_TLV_VIRTUALIZATION_ATTRIBUTES           0x24
#define WDI_TLV_PHY_INFO                            0x26
#define WDI_TLV_BAND_INFO                           0x27
#define WDI_TLV_CREATE_PORT_PARAMETERS              0x28
#define WDI_TLV_PORT_ATTRIBUTES                     0x29
#define WDI_TLV_DELETE_PORT_PARAMETERS              0x2A
#define WDI_TLV_ASSOCIATION_RESULT_PARAMETERS       0x2D
#define WDI_TLV_ASSOCIATION_REQUEST_FRAME           0x2E
#define WDI_TLV_ASSOCIATION_RESPONSE_FRAME          0x2F
#define WDI_TLV_CONNECT_PARAMETERS                  0x33
#define WDI_TLV_CONNECT_BSS_ENTRY                   0x34
#define WDI_TLV_ASSOCIATION_RESULT                  0x35
#define WDI_TLV_DISCONNECT_PARAMETERS               0x36
#define WDI_TLV_BAND_CHANNEL                        0x2C
#define WDI_TLV_BANDID                              0x39
#define WDI_TLV_BSS_ENTRY_CHANNEL_INFO              0x3A
#define WDI_TLV_SSID                                0x3B
#define WDI_TLV_AUTH_ALGO_LIST                      0x3C
#define WDI_TLV_MULTICAST_CIPHER_ALGO_LIST          0x3D
#define WDI_TLV_UNICAST_CIPHER_ALGO_LIST            0x3E
#define WDI_TLV_CONNECTION_SETTINGS                 0x3F
#define WDI_TLV_CHANNEL_INFO_LIST                   0x41
#define WDI_TLV_PACKET_FILTER_PARAMETERS            0x47
#define WDI_TLV_PRIVACY_EXEMPTION_ENTRY             0x48
#define WDI_TLV_CIPHER_KEY_TKIP_KEY                 0x49
#define WDI_TLV_CIPHER_KEY_TKIP_MIC                 0x4A
#define WDI_TLV_CIPHER_KEY_TKIP_INFO                0x4B
#define WDI_TLV_PEER_MAC_ADDRESS                    0x4C
#define WDI_TLV_CIPHER_KEY_ID                       0x4D
#define WDI_TLV_CIPHER_KEY_TYPE_INFO                0x4E
#define WDI_TLV_CIPHER_KEY_CCMP_KEY                 0x50
#define WDI_TLV_CIPHER_KEY_BIP_KEY                  0x51
#define WDI_TLV_SET_CIPHER_KEY_INFO                 0x52
#define WDI_TLV_DELETE_CIPHER_KEY_INFO              0x53
#define WDI_TLV_DEFAULT_TX_KEY_ID_PARAMETERS        0x54
#define WDI_TLV_LINK_STATE_CHANGE_PARAMETERS        0x56
#define WDI_TLV_CIPHER_KEY_WEP_KEY                  0x58
#define WDI_TLV_PHY_TYPE_LIST                       0x19
#define WDI_TLV_MULTICAST_LIST                      0x6a
#define WDI_TLV_CONFIGURED_MAC_ADDRESS              0x99
#define WDI_TLV_RADIO_STATE_PARAMETERS              0xA0
#define WDI_TLV_RADIO_STATE                         0xA1
#define WDI_TLV_DOT11_RESET_PARAMETERS              0xA2
#define WDI_TLV_DATAPATH_ATTRIBUTES                 0xB8
#define WDI_TLV_DATAPATH_CAPABILITIES               0xB9
#define WDI_TLV_BSS_ENTRY_AGE_INFO                  0xBA
#define WDI_TLV_DISASSOCIATION_INDICATION_PARAMETERS 0xBC
#define WDI_TLV_ALLOWED_BSSIDS_LIST                 0xC2
#define WDI_TLV_LINK_QUALITY_BAR_MAP                0xD8
#define WDI_TLV_IHV_NON_WDI_OIDS_LIST               0x104
#define WDI_TLV_PLDR_SUPPORT                        0x11A
#define WDI_TLV_CIPHER_KEY_GCMP_KEY                 0x12F

#define WDI_SCAN_TYPE_ACTIVE_ONLY                   1
#define WDI_SCAN_TYPE_PASSIVE_ONLY                  2
#define WDI_SCAN_TYPE_AUTO                          3
#define WDI_SCAN_TRIGGER_MANUAL                     1
#define WDI_BAND_ID_2400                            1
#define WDI_BAND_ID_5000                            2
#define WDI_BAND_ID_6000                            5
#define WDI_AUTH_ALGO_80211_OPEN                    1
#define WDI_CIPHER_ALGO_NONE                        0x00
#define WDI_CIPHER_ALGO_WEP40                       0x01
#define WDI_CIPHER_ALGO_TKIP                        0x02
#define WDI_CIPHER_ALGO_CCMP                        0x04
#define WDI_CIPHER_ALGO_WEP104                      0x05
#define WDI_CIPHER_ALGO_BIP                         0x06
#define WDI_CIPHER_ALGO_GCMP                        0x08
#define WDI_CIPHER_ALGO_WEP                         0x101
#define WDI_CIPHER_KEY_DIRECTION_INBOUND            1
#define WDI_CIPHER_KEY_DIRECTION_BOTH               3
#define WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY            1
#define WDI_CIPHER_KEY_TYPE_GROUP_KEY               2
#define WDI_CIPHER_KEY_TYPE_IGTK                    3
#define WDI_DS_CHANGED                              1
#define WDI_DS_UNCHANGED                            2
#define WDI_ASSOC_STATUS_SUCCESS                    0
#define WDI_ASSOC_STATUS_FAILURE                    1
#define WDI_ASSOC_STATUS_UNREACHABLE                2
#define WDI_ASSOC_STATUS_RADIO_OFF                  3
#define WDI_ASSOC_STATUS_PHY_DISABLED               4
#define WDI_ASSOC_STATUS_ABORTED                    5
#define WDI_ASSOC_STATUS_CANDIDATE_LIST_EXHAUSTED   6
#define WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST      7
#define WDI_ASSOC_STATUS_ROAMING_BETTER_AP_FOUND    10
#define WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST   11
#define WDI_ASSOC_STATUS_PEER_DEAUTHENTICATED       13
#define WDI_ASSOC_STATUS_PEER_DISASSOCIATED         14
#define WDI_ASSOC_STATUS_ASSOC_FAILED_BY_PEER       54
#define WDI_ASSOC_STATUS_DISASSOCIATE_BY_DEVICE_RESET 60

#define NDIS6_WDI_MAX_BSS           64
#define NDIS6_WDI_MAX_CANDIDATES    8
#define NDIS6_WDI_MAX_ALGO_PAIRS    32
#define NDIS6_WDI_MAX_PHY_TYPES     8
#define NDIS6_WDI_MAX_LIST          8
#define NDIS6_WDI_MAX_MULTICAST     32
#define NDIS6_WDI_MAX_NON_WDI_OIDS  64
#define NDIS6_WDI_MAX_PEERS         16
#define NDIS6_WDI_MAX_BANDS         4
#define NDIS6_WDI_MAX_BAND_CHANNELS 64
#define NDIS6_WDI_TX_SLOTS          1024
#define NDIS6_WDI_TX_SLOT_BITS      10
#define NDIS6_WDI_TX_QUEUE_LIMIT    512

typedef struct _NDIS6_WDI_ADAPTER NDIS6_WDI_ADAPTER, *PNDIS6_WDI_ADAPTER;

typedef struct _NDIS6_WDI_TLV
{
    USHORT Type;
    USHORT Length;
    PUCHAR Value;
} NDIS6_WDI_TLV, *PNDIS6_WDI_TLV;

typedef struct _NDIS6_WDI_TLV_BUILDER
{
    PUCHAR Buffer;
    ULONG Length;
    ULONG Capacity;
    BOOLEAN Failed;
} NDIS6_WDI_TLV_BUILDER, *PNDIS6_WDI_TLV_BUILDER;

typedef VOID (*NDIS6_WDI_TASK_CALLBACK)(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(Length) PUCHAR Tlvs,
    _In_ ULONG Length,
    _In_opt_ PVOID Context);

#define NDIS6_WDI_CMD_M3_DONE       0x1
#define NDIS6_WDI_CMD_M4_DONE       0x2
#define NDIS6_WDI_CMD_ABANDONED     0x4
#define NDIS6_WDI_CMD_ISSUED        0x8

typedef struct _NDIS6_WDI_COMMAND
{
    LIST_ENTRY Link;
    NDIS_OID_REQUEST Request;
    PUCHAR Buffer;
    ULONG BufferLength;
    ULONG MessageId;
    NDIS_STATUS CompletionCode;
    UINT32 TransactionId;
    ULONG Flags;
    BOOLEAN Listed;
    BOOLEAN Synchronous;
    KEVENT M3Event;
    KEVENT M4Event;
    NDIS_STATUS M3Status;
    NDIS_STATUS M4Status;
    PUCHAR M4Tlvs;
    ULONG M4Length;
    ULONGLONG Deadline;
    NDIS6_WDI_TASK_CALLBACK Callback;
    PVOID CallbackContext;
} NDIS6_WDI_COMMAND, *PNDIS6_WDI_COMMAND;

typedef struct _NDIS6_WDI_JOB
{
    LIST_ENTRY Link;
    PNDIS_OID_REQUEST Request;
} NDIS6_WDI_JOB, *PNDIS6_WDI_JOB;

typedef struct _NDIS6_WDI_INDICATION
{
    LIST_ENTRY Link;
    NDIS_STATUS Code;
    ULONG Length;
    UCHAR Data[1];
} NDIS6_WDI_INDICATION, *PNDIS6_WDI_INDICATION;

typedef struct _NDIS6_WDI_BSS
{
    LIST_ENTRY Link;
    DOT11_MAC_ADDRESS Bssid;
    LONG Rssi;
    ULONG LinkQuality;
    ULONG Channel;
    ULONG BandId;
    ULONGLONG Updated;
    USHORT FrameTlv;
    PUCHAR Frame;
    ULONG FrameLength;
    PUCHAR DeviceContext;
    ULONG DeviceContextLength;
    ULONGLONG Timestamp;
    USHORT BeaconPeriod;
    USHORT Capability;
    PUCHAR Ies;
    ULONG IesLength;
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
} NDIS6_WDI_BSS, *PNDIS6_WDI_BSS;

typedef struct _NDIS6_WDI_PEER
{
    BOOLEAN Valid;
    WDI_PORT_ID PortId;
    WDI_PEER_ID PeerId;
    ULONG PauseReasons;
    DOT11_MAC_ADDRESS Address;
} NDIS6_WDI_PEER, *PNDIS6_WDI_PEER;

typedef struct _NDIS6_WDI_TX_FRAME
{
    PWDI_FRAME_METADATA Metadata;
    BOOLEAN InTarget;
    BOOLEAN Injected;
    BOOLEAN TransferDone;
    BOOLEAN SendDone;
    BOOLEAN SendExpected;
    WDI_TX_FRAME_STATUS Status;
} NDIS6_WDI_TX_FRAME, *PNDIS6_WDI_TX_FRAME;

typedef struct _NDIS6_WDI_BAND
{
    ULONG BandId;
    BOOLEAN Enabled;
    ULONG ChannelCount;
    ULONG Channels[NDIS6_WDI_MAX_BAND_CHANNELS];
} NDIS6_WDI_BAND, *PNDIS6_WDI_BAND;

typedef struct _NDIS6_WDI_DOT11
{
    ULONG OperationMode;
    ULONG PacketFilter;
    BOOLEAN AutoConfig;
    ULONG AutoConfigFlags;
    DOT11_PHY_TYPE CurrentPhyType;
    ULONG CurrentChannel;
    ULONG RtsThreshold;
    ULONG FragmentationThreshold;
    DOT11_BSS_TYPE DesiredBssType;
    DOT11_SSID DesiredSsid;
    BOOLEAN HaveDesiredSsid;
    ULONG DesiredBssidCount;
    DOT11_MAC_ADDRESS DesiredBssids[NDIS6_WDI_MAX_LIST];
    ULONG AuthCount;
    DOT11_AUTH_ALGORITHM Auth[NDIS6_WDI_MAX_LIST];
    ULONG UnicastCount;
    DOT11_CIPHER_ALGORITHM Unicast[NDIS6_WDI_MAX_LIST];
    ULONG MulticastCipherCount;
    DOT11_CIPHER_ALGORITHM MulticastCipher[NDIS6_WDI_MAX_LIST];
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN HiddenNetwork;
    ULONG ExemptionCount;
    DOT11_PRIVACY_EXEMPTION Exemptions[NDIS6_WDI_MAX_LIST];
    ULONG DefaultKeyId;
    ULONG MulticastCount;
    DOT11_MAC_ADDRESS MulticastList[NDIS6_WDI_MAX_MULTICAST];
    BOOLEAN Connecting;
    BOOLEAN Associated;
    BOOLEAN AssociationIndicated;
    DOT11_MAC_ADDRESS Bssid;
    DOT11_SSID Ssid;
    ULONG BandId;
    ULONG64 TxLinkSpeed;
    ULONG64 RxLinkSpeed;
    UCHAR LinkQuality;
    BOOLEAN Qos;
    BOOLEAN PeerConfigured;
} NDIS6_WDI_DOT11, *PNDIS6_WDI_DOT11;

struct _NDIS6_WDI_ADAPTER
{
    ULONG Signature;
    PLOGICAL_ADAPTER Adapter;
    PNDIS6_DRIVER_BLOCK DriverBlock;
    PNDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS Wdi;
    PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Ihv;
    NDIS_HANDLE IhvContext;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES IhvRegistration;
    NDIS_WDI_INIT_PARAMETERS InitParameters;
    NDIS_WDI_DATA_API DataApi;
    NDIS_MINIPORT_WDI_DATA_HANDLERS Tal;
    TAL_TXRX_HANDLE TalContext;
    TAL_TXRX_PARAMETERS TalParameters;
    UINT32 MetadataExtraSpace;
    ULONG MetadataSize;
    ULONG RecvContextOffset;
    NPAGED_LOOKASIDE_LIST MetadataLookaside;
    BOOLEAN LookasideReady;

    KEVENT OpenEvent;
    NDIS_STATUS OpenStatus;
    KEVENT CloseEvent;
    NDIS_STATUS CloseStatus;
    BOOLEAN Allocated;
    BOOLEAN Opened;
    BOOLEAN TalInitialized;
    BOOLEAN TalStarted;
    BOOLEAN PortCreated;
    BOOLEAN Started;
    volatile LONG Halting;

    KMUTEX CommandMutex;
    KSPIN_LOCK CommandLock;
    LIST_ENTRY CommandList;
    volatile LONG NextTransactionId;
    PNDIS6_WDI_COMMAND ActiveTask;
    KEVENT TaskIdleEvent;

    KSPIN_LOCK JobLock;
    LIST_ENTRY JobList;
    PNDIS_OID_REQUEST RunningJob;
    KEVENT WorkerEvent;
    PKTHREAD WorkerThread;
    volatile LONG WorkerStop;

    KSPIN_LOCK IndicationLock;
    LIST_ENTRY IndicationList;
    KEVENT IndicationEvent;
    PKTHREAD IndicationThread;
    volatile LONG IndicationStop;

    UCHAR PermanentAddress[6];
    UCHAR CurrentAddress[6];
    ULONG Mtu;
    ULONG MaxMulticastListSize;
    USHORT BackFillSize;
    ULONG MaxTxRateKbps;
    ULONG MaxRxRateKbps;
    BOOLEAN HardwareRadioOn;
    BOOLEAN SoftwareRadioOn;
    WDI_TXRX_CAPABILITIES TxRxCaps;
    UCHAR MaxNumPeers;
    UCHAR MaxNumPorts;
    ULONG UnicastPairCount;
    DOT11_AUTH_CIPHER_PAIR UnicastPairs[NDIS6_WDI_MAX_ALGO_PAIRS];
    ULONG MulticastPairCount;
    DOT11_AUTH_CIPHER_PAIR MulticastPairs[NDIS6_WDI_MAX_ALGO_PAIRS];
    ULONG PhyTypeCount;
    DOT11_PHY_TYPE PhyTypes[NDIS6_WDI_MAX_PHY_TYPES];
    ULONG BandCount;
    NDIS6_WDI_BAND Bands[NDIS6_WDI_MAX_BANDS];
    ULONG NonWdiOidCount;
    NDIS_OID NonWdiOids[NDIS6_WDI_MAX_NON_WDI_OIDS];
    WDI_PORT_ID PortId;

    KSPIN_LOCK StateLock;
    NDIS6_WDI_DOT11 Dot11;
    LIST_ENTRY BssList;
    ULONG BssCount;

    KSPIN_LOCK TxLock;
    LIST_ENTRY TxQueue;
    ULONG TxQueueLength;
    ULONG TxPauseReasons;
    volatile LONG TxSending;
    volatile LONG TxKickPending;
    WDI_PEER_ID TxSendPeer;
    WDI_EXTENDED_TID TxSendTid;
    BOOLEAN TxSendAll;
    KSPIN_LOCK TxDescLock;
    NDIS6_WDI_TX_FRAME TxFrames[NDIS6_WDI_TX_SLOTS];
    ULONG TxSlotCursor;
    UCHAR TxGeneration[NDIS6_WDI_TX_SLOTS];
    volatile LONG TxOutstanding;
    KEVENT TxIdleEvent;
    NDIS6_WDI_PEER Peers[NDIS6_WDI_MAX_PEERS];
    volatile LONG RxOutstanding;
    KEVENT RxIdleEvent;
    volatile LONG DataPathRunning;
    KEVENT TxAbortEvent;
    KEVENT RxStopEvent;
    KEVENT RxFlushEvent;

    ULONG64 TxOk;
    ULONG64 TxError;
    ULONG64 TxBytes;
    ULONG64 RxOk;
    ULONG64 RxBytes;
    ULONG64 RxError;
};

ULONG Ndis6WdiRead16(_In_reads_bytes_(2) const UCHAR *Data);
ULONG Ndis6WdiRead32(_In_reads_bytes_(4) const UCHAR *Data);
ULONG64 Ndis6WdiRead64(_In_reads_bytes_(8) const UCHAR *Data);
BOOLEAN Ndis6WdiTlvNext(_Inout_ PUCHAR *Cursor, _In_ PUCHAR End, _Out_ PNDIS6_WDI_TLV Tlv);
BOOLEAN Ndis6WdiTlvFind(_In_reads_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length, _In_ USHORT Type, _Out_ PNDIS6_WDI_TLV Tlv);
VOID Ndis6WdiTlvInit(_Out_ PNDIS6_WDI_TLV_BUILDER Builder);
VOID Ndis6WdiTlvFree(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder);
VOID Ndis6WdiTlvAppend(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_reads_bytes_opt_(Length) const VOID *Data, _In_ ULONG Length);
VOID Ndis6WdiTlvAppend8(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ UCHAR Value);
VOID Ndis6WdiTlvAppend16(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ USHORT Value);
VOID Ndis6WdiTlvAppend32(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ ULONG Value);
ULONG Ndis6WdiTlvOpen(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ USHORT Type);
VOID Ndis6WdiTlvClose(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ ULONG Offset);
VOID Ndis6WdiTlvAdd(_Inout_ PNDIS6_WDI_TLV_BUILDER Builder, _In_ USHORT Type, _In_reads_bytes_opt_(Length) const VOID *Data, _In_ ULONG Length);

NDIS_STATUS
Ndis6WdiProperty(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_ WDI_PORT_ID PortId,
    _In_opt_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _Outptr_opt_result_bytebuffer_(*ResponseLength) PUCHAR *Response,
    _Out_opt_ PULONG ResponseLength);

NDIS_STATUS
Ndis6WdiTask(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_ WDI_PORT_ID PortId,
    _In_opt_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ BOOLEAN Wait,
    _In_opt_ NDIS6_WDI_TASK_CALLBACK Callback,
    _In_opt_ PVOID Context);

VOID Ndis6WdiIndicateStatusUp(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ NDIS_STATUS Code, _In_reads_bytes_(Length) PVOID Buffer, _In_ ULONG Length);
VOID Ndis6WdiIndicateLinkState(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ BOOLEAN Connected);

NDIS_STATUS Ndis6WdiDot11Initialize(_In_ PNDIS6_WDI_ADAPTER Wdi);
VOID Ndis6WdiDot11Cleanup(_In_ PNDIS6_WDI_ADAPTER Wdi);
NDIS_STATUS Ndis6WdiDot11ParseCapabilities(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_reads_bytes_(Length) PUCHAR Tlvs, _In_ ULONG Length);
NDIS_STATUS Ndis6WdiDot11SetGeneralAttributes(_In_ PNDIS6_WDI_ADAPTER Wdi);
NDIS_STATUS Ndis6WdiDot11OidRequest(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ PNDIS_OID_REQUEST Request, _Out_ PBOOLEAN Queue);
VOID Ndis6WdiDot11RunJob(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ PNDIS_OID_REQUEST Request);
VOID Ndis6WdiDot11Indication(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ ULONG MessageId, _In_reads_bytes_(Length) PUCHAR Tlvs, _In_ ULONG Length);
VOID Ndis6WdiDot11HaltDisconnect(_In_ PNDIS6_WDI_ADAPTER Wdi);

NDIS_STATUS Ndis6WdiDataInitialize(_In_ PNDIS6_WDI_ADAPTER Wdi);
NDIS_STATUS Ndis6WdiDataStart(_In_ PNDIS6_WDI_ADAPTER Wdi);
VOID Ndis6WdiDataCleanup(_In_ PNDIS6_WDI_ADAPTER Wdi);
VOID Ndis6WdiDataPause(_In_ PNDIS6_WDI_ADAPTER Wdi);
VOID Ndis6WdiDataRestart(_In_ PNDIS6_WDI_ADAPTER Wdi);
VOID Ndis6WdiDataFlushQueue(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_ NDIS_STATUS Status);
VOID Ndis6WdiDataPeerConfigure(_In_ PNDIS6_WDI_ADAPTER Wdi);
BOOLEAN Ndis6WdiDataFindPeer(_In_ PNDIS6_WDI_ADAPTER Wdi, _In_reads_bytes_(6) const UCHAR *Address, _Out_ WDI_PEER_ID *PeerId);

MINIPORT_SEND_NET_BUFFER_LISTS Ndis6WdiSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS Ndis6WdiReturnNetBufferLists;
MINIPORT_CANCEL_SEND Ndis6WdiCancelSend;

#endif
