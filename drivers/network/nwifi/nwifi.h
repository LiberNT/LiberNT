/*
 * PROJECT:     LiberNT Native WiFi (802.11) stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native WiFi intermediate driver shared definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * NDIS 6.20 media-converting filter: it attaches to each Native-802.11 (dot11)
 * miniport and presents it to the protocols above as 802.3, translating frames.
 */

#ifndef _NWIFI_H_
#define _NWIFI_H_

#include <ndis.h>
#include <windot11.h>
#include <libernt/nwifi_ioctl.h>

#define NWIFI_TAG  'FIWN'   /* 'NWIF' */
#define NWIFI_BSS_LIST_ATTEMPTS 3
#define NWIFI_BSS_LIST_MAX_BYTES (1024 * 1024)

/* Link speed reported to TCP/IP for the Wi-Fi adapter until the MSM
 * learns the real PHY rate.  54 Mbit/s (classic 802.11g) is a safe stand-in. */
#define NWIFI_DEFAULT_LINK_SPEED    (54ULL * 1000000ULL)
#define NWIFI_MAX_LINK_SPEED        433300000ULL

#include "nwifi_frame.h"
#define NWIFI_MAX_MULTICAST_ADDRESSES 32
#define NWIFI_SUPPORTED_PACKET_FILTERS \
    (NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_MULTICAST | \
     NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_BROADCAST)

/* EtherTypes that, per RFC 1042 / IEEE 802.1H, use the bridge-tunnel SNAP OUI
 * (00-00-F8) instead of the RFC 1042 OUI (00-00-00). */
#define ETHERTYPE_AARP              0x80F3
#define ETHERTYPE_IPX              0x8137

/* ===========================================================================
 *  Driver-wide state
 * ===========================================================================
 */
typedef struct _NWIFI_GLOBALS
{
    NDIS_HANDLE   FilterDriverHandle;       /* NdisFRegisterFilterDriver */
    PDRIVER_OBJECT DriverObject;

    /* All attached Wi-Fi adapters (NWIFI_ADAPTER.Link), guarded by AdapterLock. */
    LIST_ENTRY    AdapterList;
    NDIS_SPIN_LOCK AdapterLock;
    volatile LONG NextInterfaceIndex;

    /* Control device for the wlansvc IOCTL channel (\Device\Nwifi). */
    PDEVICE_OBJECT ControlDeviceObject;

    /* Pended IOCTL_NWIFI_GET_NOTIFICATION IRPs plus a small ring buffering
     * events that fired with no IRP waiting.  Guarded by NotifyLock. */
    LIST_ENTRY     NotifyIrpQueue;
    KSPIN_LOCK     NotifyLock;
#define NWIFI_NOTIFY_RING   16
    NWIFI_NOTIFICATION NotifyRing[NWIFI_NOTIFY_RING];
    ULONG          NotifyHead;      /* next slot to write */
    ULONG          NotifyTail;      /* next slot to read */
    ULONG          NotifyCount;     /* queued events with no waiter */
} NWIFI_GLOBALS, *PNWIFI_GLOBALS;

extern NWIFI_GLOBALS gNwifi;

typedef enum _NWIFI_STATE
{
    NwifiStateInitializing = 0,
    NwifiStatePaused,
    NwifiStateRunning,
    NwifiStateHalting
} NWIFI_STATE;

/* One dot11 miniport this filter is attached to. */
typedef struct _NWIFI_ADAPTER
{
    LIST_ENTRY    Link;
    ULONG         InterfaceIndex;
    volatile LONG RefCount;

    NDIS_HANDLE   FilterHandle;
    BOOLEAN       LowerPrepared;
    BOOLEAN       Announced;

    /* ---- Identity / state ---- */
    UCHAR         MacAddress[IEEE80211_ADDR_LEN];
    GUID          InterfaceGuid;
    GUID          AdapterGuid;
    NWIFI_STATE   State;
    ULONG         CurrentPacketFilter;
    ULONG         CurrentLookahead;
    ULONG         MulticastAddressCount;
    UCHAR         MulticastAddresses[NWIFI_MAX_MULTICAST_ADDRESSES][ETH_ADDR_LEN];
    BOOLEAN       MediaConnected;
    ULONG64       LinkSpeedBps;

    /* Association state, owned by the MSM.  ToDS frames use Bssid as Address1. */
    UCHAR         Bssid[IEEE80211_ADDR_LEN];
    DOT11_SSID    Ssid;
    BOOLEAN       Associated;
    volatile LONG SequenceNumber;           /* 802.11 sequence counter (<<4) */

    /* ---- Data-path resources ---- */
    NDIS_HANDLE   TxNblPool;                /* NBLs we build to send down */
    NDIS_HANDLE   RxNblPool;                /* NBLs we indicate up */
    NDIS_SPIN_LOCK DataLock;

    /* In-flight accounting so Pause/Detach can drain safely. */
    volatile LONG PendingSends;             /* NBLs sent down, not completed */
    volatile LONG PendingReceives;          /* NBLs indicated up, not returned */

    /* ---- Statistics ---- */
    volatile LONG64 TxOk;
    volatile LONG64 RxOk;
    volatile LONG64 TxError;
    volatile LONG64 RxError;
    volatile LONG64 TxBytes;
    volatile LONG64 RxBytes;

    /* Station management object; typed as PNWIFI_MSM (see msm.h), kept as
     * PVOID so this header need not pull in MSM internals. */
    PVOID         MsmContext;
    PVOID         MsmZombie;   /* torn down by destroy, freed with the last reference */
} NWIFI_ADAPTER, *PNWIFI_ADAPTER;

/* Per-NBL bookkeeping for locally built NBLs, stored in the pool's per-NBL
 * context area so the free path can recover the original buffers. */
typedef struct _NWIFI_NBL_CONTEXT
{
    PNWIFI_ADAPTER Adapter;
    PNET_BUFFER_LIST OriginalNbl;   /* upper NBL that triggered a send-down */
    PUCHAR        DataBuffer;        /* allocated frame buffer for this NBL */
    ULONG         DataLength;
    PMDL          Mdl;
} NWIFI_NBL_CONTEXT, *PNWIFI_NBL_CONTEXT;

#define NWIFI_NBL_CONTEXT_SIZE \
    ((USHORT)ALIGN_UP_BY(sizeof(NWIFI_NBL_CONTEXT), MEMORY_ALLOCATION_ALIGNMENT))

/* ===========================================================================
 *  Prototypes
 * ===========================================================================
 */

/* --- nwifi.c : DriverEntry + registration --------------------------------- */
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     NwifiUnload;

/* --- filter.c : filter edge on the dot11 miniport ------------------------ */
FILTER_SET_OPTIONS                      NwifiFilterSetOptions;
FILTER_ATTACH                           NwifiFilterAttach;
FILTER_DETACH                           NwifiFilterDetach;
FILTER_RESTART                          NwifiFilterRestart;
FILTER_PAUSE                            NwifiFilterPause;
FILTER_STATUS                           NwifiFilterStatus;
FILTER_NET_PNP_EVENT                    NwifiFilterNetPnPEvent;
FILTER_DEVICE_PNP_EVENT_NOTIFY          NwifiFilterDevicePnPEventNotify;
FILTER_OID_REQUEST_COMPLETE             NwifiFilterOidRequestComplete;
FILTER_RECEIVE_NET_BUFFER_LISTS         NwifiFilterReceiveNetBufferLists;
FILTER_SEND_NET_BUFFER_LISTS_COMPLETE   NwifiFilterSendNetBufferListsComplete;
FILTER_CANCEL_SEND_NET_BUFFER_LISTS     NwifiFilterCancelSendNetBufferLists;

NDIS_STATUS NwifiDoRequest(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed);

NDIS_STATUS NwifiDoRequestEx(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed,
    _Out_opt_ PULONG BytesNeeded);

NDIS_STATUS NwifiDoMethodRequest(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _Inout_updates_bytes_(OutputLength) PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG BytesWritten,
    _Out_ PULONG BytesNeeded);

/* --- datapath.c : 802.3 <-> 802.11 translation ---------------------------- */
FILTER_SEND_NET_BUFFER_LISTS            NwifiFilterSendNetBufferLists;
FILTER_RETURN_NET_BUFFER_LISTS          NwifiFilterReturnNetBufferLists;

VOID NwifiReceiveFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags);

VOID NwifiSendCompleteFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags);

VOID NwifiIndicateLinkState(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ BOOLEAN Connected);

/* --- oid.c : OID translation/forwarding ----------------------------------- */
FILTER_OID_REQUEST          NwifiFilterOidRequest;
FILTER_CANCEL_OID_REQUEST   NwifiFilterCancelOidRequest;

extern NDIS_OID NwifiSupportedOids[];
extern ULONG    NwifiSupportedOidCount;

/* --- helpers (nwifi.c) ---------------------------------------------------- */
PVOID NwifiAllocate(_In_ SIZE_T Size);
VOID  NwifiFree(_In_ PVOID Buffer);

/* Find an attached adapter by its nwifi-local interface index.  Returns NULL if no
 * such interface is bound.  Caller must hold no locks; runs at <= DISPATCH. */
PNWIFI_ADAPTER NwifiReferenceAdapterByIndex(_In_ ULONG InterfaceIndex);
VOID NwifiDereferenceAdapter(_In_ PNWIFI_ADAPTER Adapter);
VOID NTAPI NwifiDestroyAdapter(_In_ PNWIFI_ADAPTER Adapter);

/* --- control.c : \Device\Nwifi control device + IOCTL dispatch ------------- */
NDIS_STATUS NwifiCreateControlDevice(VOID);
VOID        NwifiDeleteControlDevice(VOID);

/* Post an asynchronous interface event to the wlansvc notify channel.  Either
 * completes a pended IOCTL_NWIFI_GET_NOTIFICATION IRP, or rings the event for
 * the next re-issue.  Safe at <= DISPATCH_LEVEL. */
VOID NwifiQueueNotification(
    _In_ ULONG InterfaceIndex,
    _In_ NWIFI_NOTIFICATION_CODE Code,
    _In_ NDIS_STATUS StatusCode);

/* --- datapath.c : raw 802.11 TX inject (used by the supplicant for EAPOL) -- *
 * Build an 802.11 data frame around an L2 payload carrying its own EtherType
 * (e.g. EAPOL 0x888E) and send it down to the lower miniport. */
NDIS_STATUS NwifiInjectL2Frame(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_reads_bytes_(6) PUCHAR DestMac,
    _In_ USHORT EtherType,
    _In_reads_bytes_(PayloadLength) PUCHAR Payload,
    _In_ ULONG PayloadLength,
    _In_ BOOLEAN SendUnencrypted);

#include "msm.h"

#endif /* _NWIFI_H_ */
