/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Station management: scanning, authentication, association and data receive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rt2800usb.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define RT_RX_CONTEXT_TAG       'X82R'
#define RT_BSS_EXPIRY_100NS     (60ULL * 10000000ULL)
#define RT_RSN_CAP_MFPC         0x0080
#define RT_ACTION_SA_QUERY      8
#define RT_SA_QUERY_REQUEST     0
#define RT_SA_QUERY_RESPONSE    1
#define RT_STATUS_REFUSED_TEMPORARILY 30
#define RT_TIMEOUT_COMEBACK     3
#define RT_PROTECTED_MGMT_MAX   512
#define RT_SA_QUERY_TIMEOUT_100NS (1000ULL * 10000ULL)

typedef struct _RT_RX_PACKET
{
    DOT11_EXTSTA_RECV_CONTEXT Context;
    PMDL Mdl;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} RT_RX_PACKET, *PRT_RX_PACKET;

static const UCHAR RtBroadcast[RT_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static const UCHAR RtChannels2G[] = { 1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10 };
static const UCHAR RtChannels5G[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165 };

static const UCHAR RtRates2G[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
static const UCHAR RtExtRates2G[] = { 0x30, 0x48, 0x60, 0x6c };
static const UCHAR RtRates5G[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };

ULONG
RtChannelToFrequency(
    _In_ ULONG Channel)
{
    if (Channel == 14)
        return 2484;
    if (Channel < 14)
        return 2407 + Channel * 5;
    return 5000 + Channel * 5;
}

VOID
RtIndicateDot11Status(
    _In_ PRT_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode,
    _In_reads_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = StatusCode;
    Indication.StatusBuffer = Buffer;
    Indication.StatusBufferSize = BufferSize;
    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

VOID
RtIndicateLinkState(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = Connected ? RT_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed = Connected ? RT_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    RtIndicateDot11Status(Adapter, NDIS_STATUS_LINK_STATE, &LinkState, sizeof(LinkState));
}

static
const UCHAR*
RtFindIe(
    _In_reads_bytes_(Length) const UCHAR* Ies,
    _In_ ULONG Length,
    _In_ UCHAR Id)
{
    ULONG Offset = 0;

    while (Offset + 2 <= Length)
    {
        if (Offset + 2 + Ies[Offset + 1] > Length)
            return NULL;
        if (Ies[Offset] == Id)
            return Ies + Offset;
        Offset += 2 + Ies[Offset + 1];
    }

    return NULL;
}

static
USHORT
RtRsnCapabilities(
    _In_ PRT_BSS Bss)
{
    const UCHAR* Rsn = RtFindIe(Bss->Ies, Bss->IeLength, DOT11_IE_RSN);
    ULONG Length, Offset, Count;

    if (!Rsn)
        return 0;

    Length = Rsn[1];
    Rsn += 2;
    Offset = 2 + 4;
    if (Offset + 2 > Length)
        return 0;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2 + Count * 4;
    if (Offset + 2 > Length)
        return 0;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2 + Count * 4;
    if (Offset + 2 > Length)
        return 0;
    return (USHORT)(Rsn[Offset] | (Rsn[Offset + 1] << 8));
}

static
ULONG
RtBuildHeader(
    _In_ PRT_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Bssid);

static
NTSTATUS
RtSendManagement(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    UCHAR Protected[RT_PROTECTED_MGMT_MAX];
    UCHAR Tk[16];
    ULONGLONG Pn = 0;
    BOOLEAN Protect;

    NdisAcquireSpinLock(&Adapter->Lock);
    Protect = Adapter->PmfActive && Adapter->MgmtKeyValid && !(Frame[4] & 0x01) &&
              Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN <= sizeof(Protected);
    if (Protect)
    {
        Pn = ++Adapter->MgmtTxPn;
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Protect)
        return RtSendFrame(Adapter, Frame, Length, TRUE, NULL);

    RtlCopyMemory(Protected, Frame, DOT11_HEADER_LENGTH);
    Protected[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Protected, DOT11_HEADER_LENGTH, Pn, 0,
                    Frame + DOT11_HEADER_LENGTH, Length - DOT11_HEADER_LENGTH,
                    Protected + DOT11_HEADER_LENGTH);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    return RtSendFrame(Adapter, Protected, Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN, TRUE, NULL);
}

static
VOID
RtStartSaQuery(
    _In_ PRT_ADAPTER Adapter)
{
    UCHAR Query[DOT11_HEADER_LENGTH + 4];
    ULONG QueryLength;
    USHORT Id;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->SaQueryPending)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return;
    }
    Id = (USHORT)(KeQueryPerformanceCounter(NULL).QuadPart ^ (KeQueryInterruptTime() >> 4));
    Adapter->SaQueryPending = TRUE;
    Adapter->SaQueryId = Id;
    Adapter->SaQueryDeadline = KeQueryInterruptTime() + RT_SA_QUERY_TIMEOUT_100NS;
    NdisReleaseSpinLock(&Adapter->Lock);

    QueryLength = RtBuildHeader(Adapter, Query, DOT11_SUBTYPE_ACTION, Adapter->ConnectedBssid, Adapter->ConnectedBssid);
    Query[QueryLength++] = RT_ACTION_SA_QUERY;
    Query[QueryLength++] = RT_SA_QUERY_REQUEST;
    Query[QueryLength++] = (UCHAR)Id;
    Query[QueryLength++] = (UCHAR)(Id >> 8);
    RtSendManagement(Adapter, Query, QueryLength);
}

static
BOOLEAN
RtDecryptManagement(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _Out_writes_bytes_(RT_PROTECTED_MGMT_MAX) PUCHAR Plain,
    _Out_ PULONG PlainLength)
{
    UCHAR Tk[16];
    ULONGLONG Pn = 0;
    BOOLEAN Valid;

    if (Length < DOT11_HEADER_LENGTH + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN ||
        Length > RT_PROTECTED_MGMT_MAX)
    {
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->PmfActive && Adapter->MgmtKeyValid &&
            RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, RT_ADDRESS_LENGTH) &&
            RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH);
    if (Valid)
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaCcmpDecrypt(Tk, Frame, DOT11_HEADER_LENGTH,
                            Frame + DOT11_HEADER_LENGTH, Length - DOT11_HEADER_LENGTH,
                            Plain + DOT11_HEADER_LENGTH, &Pn) == 0;
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    if (!Valid)
    {
        DPRINT1("RT2800USB: protected management frame failed to decrypt\n");
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Pn > Adapter->MgmtRxPn;
    if (Valid)
        Adapter->MgmtRxPn = Pn;
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    RtlCopyMemory(Plain, Frame, DOT11_HEADER_LENGTH);
    Plain[1] &= ~DOT11_FC1_PROTECTED;
    *PlainLength = Length - RSNA_CCMP_HDR_LEN - RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
BOOLEAN
RtVerifyBip(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    UCHAR Igtk[16];
    ULONGLONG Ipn = 0;
    USHORT KeyId = 0;
    BOOLEAN Valid;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->IgtkValid;
    if (Valid)
        RtlCopyMemory(Igtk, Adapter->Igtk, sizeof(Igtk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaBipVerify(Igtk, Frame, Length, &KeyId, &Ipn) == 0;
    RtlSecureZeroMemory(Igtk, sizeof(Igtk));
    if (!Valid)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = KeyId == Adapter->IgtkKeyId && Ipn > Adapter->IgtkRxIpn;
    if (Valid)
        Adapter->IgtkRxIpn = Ipn;
    NdisReleaseSpinLock(&Adapter->Lock);
    return Valid;
}

static
VOID
RtUpdateBss(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi)
{
    const UCHAR *Ies, *Ssid, *Ds;
    ULONG IeLength, Channel, i, Slot = MAXULONG, Oldest = 0;
    ULONGLONG Now = KeQueryInterruptTime(), OldestTime = MAXULONGLONG;
    PRT_BSS Bss;

    if (Length < DOT11_HEADER_LENGTH + 12)
        return;

    Ies = Frame + DOT11_HEADER_LENGTH + 12;
    IeLength = Length - DOT11_HEADER_LENGTH - 12;
    Ssid = RtFindIe(Ies, IeLength, DOT11_IE_SSID);
    Ds = RtFindIe(Ies, IeLength, DOT11_IE_DS_PARAMS);
    Channel = (Ds && Ds[1] >= 1) ? Ds[2] : Adapter->CurrentChannel;

    if (Ds && Ds[1] >= 1 && Channel != Adapter->CurrentChannel && Adapter->CurrentChannel <= 14)
        return;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (RtlEqualMemory(Adapter->Bss[i].Bssid, Frame + 16, RT_ADDRESS_LENGTH))
        {
            Slot = i;
            break;
        }
        if (Adapter->Bss[i].LastSeen < OldestTime)
        {
            OldestTime = Adapter->Bss[i].LastSeen;
            Oldest = i;
        }
    }

    if (Slot == MAXULONG)
        Slot = Adapter->BssCount < RT_MAX_BSS ? Adapter->BssCount++ : Oldest;

    Bss = &Adapter->Bss[Slot];
    RtlCopyMemory(Bss->Bssid, Frame + 16, RT_ADDRESS_LENGTH);
    if (Ssid && Ssid[1] <= DOT11_SSID_MAX_LENGTH && (Ssid[1] || !Bss->SsidLength))
    {
        Bss->SsidLength = Ssid[1];
        RtlCopyMemory(Bss->Ssid, Ssid + 2, Ssid[1]);
    }
    Bss->Channel = Channel;
    Bss->Rssi = Rssi;
    RtlCopyMemory(&Bss->Timestamp, Frame + DOT11_HEADER_LENGTH, sizeof(Bss->Timestamp));
    Bss->BeaconPeriod = Frame[DOT11_HEADER_LENGTH + 8] | (Frame[DOT11_HEADER_LENGTH + 9] << 8);
    Bss->Capability = Frame[DOT11_HEADER_LENGTH + 10] | (Frame[DOT11_HEADER_LENGTH + 11] << 8);
    Bss->IeLength = min(IeLength, RT_MAX_IE_BYTES);
    RtlCopyMemory(Bss->Ies, Ies, Bss->IeLength);
    Bss->LastSeen = Now;

    if (Adapter->LinkState == RtLinkAssociated &&
        RtlEqualMemory(Bss->Bssid, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH))
    {
        Adapter->LastBeaconTime = Now;
        Adapter->ConnectedRssi = Rssi;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
RtMlmeReceive(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ BOOLEAN HwDecrypted)
{
    UCHAR Plain[RT_PROTECTED_MGMT_MAX];
    UCHAR Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    const UCHAR* Body = Frame + DOT11_HEADER_LENGTH;
    BOOLEAN Protected = HwDecrypted;
    BOOLEAN Pmf;
    ULONG PlainLength;

    if (Length < DOT11_HEADER_LENGTH)
        return;

    if (Frame[1] & DOT11_FC1_PROTECTED)
    {
        if (!RtDecryptManagement(Adapter, Frame, Length, Plain, &PlainLength))
            return;
        Frame = Plain;
        Length = PlainLength;
        Body = Frame + DOT11_HEADER_LENGTH;
        Protected = TRUE;
    }
    Pmf = Adapter->PmfActive && Adapter->MgmtKeyValid && Adapter->LinkState == RtLinkAssociated;

    switch (Subtype)
    {
        case DOT11_SUBTYPE_BEACON:
        case DOT11_SUBTYPE_PROBE_RESP:
            RtUpdateBss(Adapter, Frame, Length, Rssi);
            break;

        case DOT11_SUBTYPE_AUTH:
            if (Adapter->MlmeExpect == DOT11_SUBTYPE_AUTH &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH) &&
                (Body[2] | (Body[3] << 8)) == 2)
            {
                Adapter->MlmeStatus = Body[4] | (Body[5] << 8);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            break;

        case DOT11_SUBTYPE_ASSOC_RESP:
            if (Adapter->MlmeExpect == DOT11_SUBTYPE_ASSOC_RESP &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH))
            {
                Adapter->MlmeStatus = Body[2] | (Body[3] << 8);
                Adapter->MlmeAid = (Body[4] | (Body[5] << 8)) & 0x3fff;
                Adapter->AssocResponseIeLength = min(Length - DOT11_HEADER_LENGTH - 6, RT_MAX_IE_BYTES);
                RtlCopyMemory(Adapter->AssocResponseIes, Body + 6, Adapter->AssocResponseIeLength);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            break;

        case DOT11_SUBTYPE_DEAUTH:
        case DOT11_SUBTYPE_DISASSOC:
            if (Pmf &&
                (RtlEqualMemory(Frame + 4, RtBroadcast, RT_ADDRESS_LENGTH) ?
                 !RtVerifyBip(Adapter, Frame, Length) : !Protected))
            {
                DPRINT1("RT2800USB: unprotected %s, querying the AP\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation");
                if (RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH))
                    RtStartSaQuery(Adapter);
                break;
            }
            if (Adapter->LinkState == RtLinkAssociated &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH) &&
                (RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, RT_ADDRESS_LENGTH) ||
                 RtlEqualMemory(Frame + 4, RtBroadcast, RT_ADDRESS_LENGTH)))
            {
                DPRINT1("RT2800USB: AP sent %s reason %u\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation",
                        Length >= DOT11_HEADER_LENGTH + 2 ? (Body[0] | (Body[1] << 8)) : 0);
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->LastBeaconTime = 0;
                NdisReleaseSpinLock(&Adapter->Lock);
                RtWakeWorker(Adapter);
            }
            break;

        case DOT11_SUBTYPE_ACTION:
            if (Length >= DOT11_HEADER_LENGTH + 4 &&
                Body[0] == RT_ACTION_SA_QUERY &&
                Pmf && Protected &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH))
            {
                UCHAR Response[DOT11_HEADER_LENGTH + 4];
                ULONG ResponseLength;

                if (Body[1] == RT_SA_QUERY_REQUEST)
                {
                    ResponseLength = RtBuildHeader(Adapter, Response, DOT11_SUBTYPE_ACTION,
                                                   Adapter->ConnectedBssid, Adapter->ConnectedBssid);
                    Response[ResponseLength++] = RT_ACTION_SA_QUERY;
                    Response[ResponseLength++] = RT_SA_QUERY_RESPONSE;
                    Response[ResponseLength++] = Body[2];
                    Response[ResponseLength++] = Body[3];
                    RtSendManagement(Adapter, Response, ResponseLength);
                    DPRINT1("RT2800USB: answered SA Query %02x%02x\n", Body[2], Body[3]);
                }
                else if (Body[1] == RT_SA_QUERY_RESPONSE)
                {
                    NdisAcquireSpinLock(&Adapter->Lock);
                    if (Adapter->SaQueryPending &&
                        Adapter->SaQueryId == (USHORT)(Body[2] | (Body[3] << 8)))
                    {
                        Adapter->SaQueryPending = FALSE;
                    }
                    NdisReleaseSpinLock(&Adapter->Lock);
                }
            }
            break;
    }
}

static
VOID
RtFreeRxNbl(
    _In_ PNET_BUFFER_LIST Nbl)
{
    PRT_RX_PACKET Packet = Nbl->MiniportReserved[0];

    NdisFreeNetBufferList(Nbl);
    if (Packet)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, RT_RX_CONTEXT_TAG);
    }
}

static
VOID
RtReleaseHeldReceives(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Indicate)
{
    PNET_BUFFER_LIST Head, Next;
    ULONG Count;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->HoldRx = FALSE;
    Head = Adapter->HeldRxHead;
    Count = Adapter->HeldRxCount;
    Adapter->HeldRxHead = NULL;
    Adapter->HeldRxTail = NULL;
    Adapter->HeldRxCount = 0;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Head)
        return;

    if (Indicate && Adapter->DataPathRunning)
    {
        InterlockedExchangeAdd(&Adapter->RxIndicated, (LONG)Count);
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                           Head,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           Count,
                                           0);
        return;
    }

    for (; Head; Head = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Head);
        RtFreeRxNbl(Head);
    }
}

VOID
RtReceiveData(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ ULONG Rate)
{
    PNET_BUFFER_LIST Nbl;
    PRT_RX_PACKET Packet;
    UCHAR Subtype;

    UNREFERENCED_PARAMETER(Rate);

    if ((Adapter->LinkState != RtLinkAssociated && Adapter->LinkState != RtLinkAssociating) ||
        !Adapter->DataPathRunning || Length < DOT11_HEADER_LENGTH + 8)
        return;

    Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    if (Subtype & 0x40)
        return;

    if (!RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH))
        return;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(RT_RX_PACKET, Data) + Length, RT_RX_CONTEXT_TAG);
    if (!Packet)
    {
        Adapter->RxError++;
        return;
    }

    RtlCopyMemory(Packet->Data, Frame, Length);
    Packet->Length = Length;
    Packet->Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Packet->Data, Length);
    if (!Packet->Mdl)
    {
        ExFreePoolWithTag(Packet, RT_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Packet->Mdl, 0, Length);
    if (!Nbl)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, RT_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    RtlZeroMemory(&Packet->Context, sizeof(Packet->Context));
    Packet->Context.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Packet->Context.Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    Packet->Context.Header.Size = sizeof(DOT11_EXTSTA_RECV_CONTEXT);
    Packet->Context.uPhyId = 0;
    Packet->Context.uChCenterFrequency = RtChannelToFrequency(Adapter->CurrentChannel);
    Packet->Context.usNumberOfMPDUsReceived = 1;
    Packet->Context.lRSSI = Rssi;
    Packet->Context.ucDataRate = 0;

    Nbl->MiniportReserved[0] = Packet;
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = &Packet->Context;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;

    Adapter->RxOk++;
    Adapter->RxBytes += Length;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->HoldRx || Adapter->LinkState != RtLinkAssociated)
    {
        if (Adapter->HoldRx && Adapter->HeldRxCount < RT_HELD_RX_MAX)
        {
            if (Adapter->HeldRxTail)
                NET_BUFFER_LIST_NEXT_NBL(Adapter->HeldRxTail) = Nbl;
            else
                Adapter->HeldRxHead = Nbl;
            Adapter->HeldRxTail = Nbl;
            Adapter->HeldRxCount++;
            Nbl = NULL;
        }
        NdisReleaseSpinLock(&Adapter->Lock);
        if (Nbl)
            RtFreeRxNbl(Nbl);
        return;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    InterlockedIncrement(&Adapter->RxIndicated);
    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                       Nbl,
                                       NDIS_DEFAULT_PORT_NUMBER,
                                       1,
                                       NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
}

VOID
NTAPI
RtMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;

    UNREFERENCED_PARAMETER(ReturnFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        RtFreeRxNbl(Nbl);
        InterlockedDecrement(&Adapter->RxIndicated);
    }
}

ULONG
RtBuildBssList(
    _In_ PRT_ADAPTER Adapter,
    _Out_writes_bytes_opt_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesNeeded)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PDOT11_BYTE_ARRAY Array;
    PDOT11_BSS_ENTRY Entry;
    ULONG Payload = 0, Total, i;
    PUCHAR Cursor;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (Now - Adapter->Bss[i].LastSeen <= RT_BSS_EXPIRY_100NS)
            Payload += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Adapter->Bss[i].IeLength;
    }

    Total = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Payload;
    *BytesNeeded = Total;
    if (!Buffer || BufferLength < Total)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return 0;
    }

    Array = (PDOT11_BYTE_ARRAY)Buffer;
    Array->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Array->Header.Revision = DOT11_BSS_ENTRY_BYTE_ARRAY_REVISION_1;
    Array->Header.Size = sizeof(DOT11_BYTE_ARRAY);
    Array->uNumOfBytes = Payload;
    Array->uTotalNumOfBytes = Payload;

    Cursor = Array->ucBuffer;
    for (i = 0; i < Adapter->BssCount; i++)
    {
        PRT_BSS Bss = &Adapter->Bss[i];

        if (Now - Bss->LastSeen > RT_BSS_EXPIRY_100NS)
            continue;

        Entry = (PDOT11_BSS_ENTRY)Cursor;
        RtlZeroMemory(Entry, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        Entry->uPhyId = 0;
        Entry->PhySpecificInfo.uChCenterFrequency = RtChannelToFrequency(Bss->Channel);
        RtlCopyMemory(Entry->dot11BSSID, Bss->Bssid, RT_ADDRESS_LENGTH);
        Entry->dot11BSSType = (Bss->Capability & 0x0002) ? dot11_BSS_type_independent : dot11_BSS_type_infrastructure;
        Entry->lRSSI = Bss->Rssi;
        Entry->uLinkQuality = Bss->Rssi >= -50 ? 100 : Bss->Rssi <= -100 ? 0 : (ULONG)(2 * (Bss->Rssi + 100));
        Entry->bInRegDomain = TRUE;
        Entry->usBeaconPeriod = Bss->BeaconPeriod ? Bss->BeaconPeriod : 100;
        Entry->ullTimestamp = Bss->Timestamp;
        Entry->ullHostTimestamp = Bss->LastSeen;
        Entry->usCapabilityInformation = Bss->Capability;
        Entry->uBufferLength = Bss->IeLength;
        RtlCopyMemory(Entry->ucBuffer, Bss->Ies, Bss->IeLength);
        Cursor += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Bss->IeLength;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    return Total;
}

static
ULONG
RtBuildHeader(
    _In_ PRT_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Bssid)
{
    RtlZeroMemory(Frame, DOT11_HEADER_LENGTH);
    Frame[0] = DOT11_FC0_TYPE_MGMT | Subtype;
    RtlCopyMemory(Frame + 4, Destination, RT_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 10, Adapter->CurrentAddress, RT_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 16, Bssid, RT_ADDRESS_LENGTH);
    return DOT11_HEADER_LENGTH;
}

static
ULONG
RtAppendRates(
    _In_ PRT_ADAPTER Adapter,
    _Out_writes_bytes_(32) PUCHAR Out)
{
    ULONG Offset = 0;

    if (Adapter->CurrentChannel > 14)
    {
        Out[Offset++] = DOT11_IE_RATES;
        Out[Offset++] = sizeof(RtRates5G);
        RtlCopyMemory(Out + Offset, RtRates5G, sizeof(RtRates5G));
        return Offset + sizeof(RtRates5G);
    }

    Out[Offset++] = DOT11_IE_RATES;
    Out[Offset++] = sizeof(RtRates2G);
    RtlCopyMemory(Out + Offset, RtRates2G, sizeof(RtRates2G));
    Offset += sizeof(RtRates2G);
    Out[Offset++] = DOT11_IE_EXT_RATES;
    Out[Offset++] = sizeof(RtExtRates2G);
    RtlCopyMemory(Out + Offset, RtExtRates2G, sizeof(RtExtRates2G));
    return Offset + sizeof(RtExtRates2G);
}

static
VOID
RtSendProbeRequest(
    _In_ PRT_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 2 + DOT11_SSID_MAX_LENGTH + 32];
    ULONG Length;

    Length = RtBuildHeader(Adapter, Frame, DOT11_SUBTYPE_PROBE_REQ, RtBroadcast, RtBroadcast);
    Frame[Length++] = DOT11_IE_SSID;
    if (Ssid && Ssid->uSSIDLength && Ssid->uSSIDLength <= DOT11_SSID_MAX_LENGTH)
    {
        Frame[Length++] = (UCHAR)Ssid->uSSIDLength;
        RtlCopyMemory(Frame + Length, Ssid->ucSSID, Ssid->uSSIDLength);
        Length += Ssid->uSSIDLength;
    }
    else
    {
        Frame[Length++] = 0;
    }
    Length += RtAppendRates(Adapter, Frame + Length);
    RtSendFrame(Adapter, Frame, Length, TRUE, NULL);
}

static
BOOLEAN
RtPassiveChannel(
    _In_ ULONG Channel)
{
    return Channel >= 52 && Channel <= 144;
}

static
VOID
RtScanChannels(
    _In_ PRT_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(RtChannels2G) && !Adapter->Halting; i++)
    {
        RtSetChannel(Adapter, RtChannels2G[i]);
        RtSendProbeRequest(Adapter, Ssid);
        RtSleep(RT_SCAN_DWELL_MS);
    }

    for (i = 0; Adapter->DualBand && i < RTL_NUMBER_OF(RtChannels5G) && !Adapter->Halting; i++)
    {
        RtSetChannel(Adapter, RtChannels5G[i]);
        if (!RtPassiveChannel(RtChannels5G[i]))
            RtSendProbeRequest(Adapter, Ssid);
        RtSleep(RtPassiveChannel(RtChannels5G[i]) ? RT_SCAN_DWELL_MS + 30 : RT_SCAN_DWELL_MS);
    }
}

static
VOID
RtDoScan(
    _In_ PRT_ADAPTER Adapter)
{
    NDIS_STATUS ScanStatus = NDIS_STATUS_SUCCESS;
    DOT11_SSID Ssid;
    BOOLEAN Associated;

    NdisAcquireSpinLock(&Adapter->Lock);
    Ssid = Adapter->ScanSsid;
    Associated = Adapter->LinkState == RtLinkAssociated;
    Adapter->Scanning = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Associated)
        RtScanChannels(Adapter, Ssid.uSSIDLength ? &Ssid : NULL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Scanning = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    DPRINT1("RT2800USB: scan done, %lu networks\n", Adapter->BssCount);
    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
}

static
PRT_BSS
RtSelectBss(
    _In_ PRT_ADAPTER Adapter,
    _Out_ PRT_BSS Copy)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PRT_BSS Best = NULL, Bss;
    ULONG i;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        Bss = &Adapter->Bss[i];
        if (Now - Bss->LastSeen > RT_BSS_EXPIRY_100NS)
            continue;
        if (!(Bss->Capability & 0x0001))
            continue;
        if (Adapter->HaveDesiredSsid &&
            (Bss->SsidLength != Adapter->DesiredSsid.uSSIDLength ||
             !RtlEqualMemory(Bss->Ssid, Adapter->DesiredSsid.ucSSID, Bss->SsidLength)))
        {
            continue;
        }
        if (Adapter->HaveDesiredBssid && !RtlEqualMemory(Bss->Bssid, Adapter->DesiredBssid, RT_ADDRESS_LENGTH))
            continue;
        if (Bss->Channel > 14 && !Adapter->DualBand)
            continue;
        if (!Best || Bss->Rssi > Best->Rssi)
            Best = Bss;
    }

    if (Best)
        *Copy = *Best;
    NdisReleaseSpinLock(&Adapter->Lock);

    return Best ? Copy : NULL;
}

static
BOOLEAN
RtWaitMlme(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return KeWaitForSingleObject(&Adapter->MlmeEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static
BOOLEAN
RtAuthenticate(
    _In_ PRT_ADAPTER Adapter,
    _In_ PRT_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 6];
    ULONG Length, Attempt;

    for (Attempt = 0; Attempt < RT_MLME_RETRIES; Attempt++)
    {
        Length = RtBuildHeader(Adapter, Frame, DOT11_SUBTYPE_AUTH, Bss->Bssid, Bss->Bssid);
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 1;
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 0;

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_AUTH;
        RtSendFrame(Adapter, Frame, Length, TRUE, NULL);
        if (RtWaitMlme(Adapter, RT_AUTH_TIMEOUT_MS))
        {
            DPRINT1("RT2800USB: authentication status %u\n", Adapter->MlmeStatus);
            return Adapter->MlmeStatus == 0;
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("RT2800USB: no authentication response\n");
    return FALSE;
}

static
ULONG
RtBuildRsnIe(
    _In_ PRT_ADAPTER Adapter,
    _Out_writes_bytes_(22) PUCHAR Out)
{
    static const UCHAR Template[22] =
    {
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
        0x00, 0x00
    };

    RtlCopyMemory(Out, Template, sizeof(Template));
    Out[7] = Adapter->MulticastCipher == DOT11_CIPHER_ALGO_TKIP ? 2 : 4;
    Out[13] = Adapter->UnicastCipher == DOT11_CIPHER_ALGO_TKIP ? 2 : 4;
    if (Adapter->MulticastCipher == DOT11_CIPHER_ALGO_CCMP &&
        Adapter->UnicastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        Out[20] = (UCHAR)RT_RSN_CAP_MFPC;
    }
    return sizeof(Template);
}

static
BOOLEAN
RtAssociate(
    _In_ PRT_ADAPTER Adapter,
    _In_ PRT_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 4 + 2 + DOT11_SSID_MAX_LENGTH + 32 + 24];
    ULONG Length, IeStart, Attempt;
    USHORT Capability;
    BOOLEAN Secure = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK;

    for (Attempt = 0; Attempt < RT_MLME_RETRIES; Attempt++)
    {
        Length = RtBuildHeader(Adapter, Frame, DOT11_SUBTYPE_ASSOC_REQ, Bss->Bssid, Bss->Bssid);
        Capability = 0x0001 | (Bss->Capability & 0x0020);
        if (Secure)
            Capability |= 0x0010;
        if (Bss->Channel <= 14)
            Capability |= Bss->Capability & 0x0400;
        Frame[Length++] = (UCHAR)Capability;
        Frame[Length++] = (UCHAR)(Capability >> 8);
        Frame[Length++] = 10;
        Frame[Length++] = 0;

        IeStart = Length;
        Frame[Length++] = DOT11_IE_SSID;
        Frame[Length++] = (UCHAR)Bss->SsidLength;
        RtlCopyMemory(Frame + Length, Bss->Ssid, Bss->SsidLength);
        Length += Bss->SsidLength;
        Length += RtAppendRates(Adapter, Frame + Length);
        if (Secure)
            Length += RtBuildRsnIe(Adapter, Frame + Length);

        Adapter->AssocRequestIeLength = Length - IeStart;
        RtlCopyMemory(Adapter->AssocRequestIes, Frame + IeStart, Adapter->AssocRequestIeLength);

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_ASSOC_RESP;
        RtSendFrame(Adapter, Frame, Length, TRUE, NULL);
        if (RtWaitMlme(Adapter, RT_ASSOC_TIMEOUT_MS))
        {
            const UCHAR* Timeout;
            ULONG Comeback;

            DPRINT1("RT2800USB: association status %u aid %u\n", Adapter->MlmeStatus, Adapter->MlmeAid);
            if (Adapter->MlmeStatus != RT_STATUS_REFUSED_TEMPORARILY)
                return Adapter->MlmeStatus == 0;

            Timeout = RtFindIe(Adapter->AssocResponseIes, Adapter->AssocResponseIeLength,
                               DOT11_IE_TIMEOUT_INTERVAL);
            Comeback = 1000;
            if (Timeout && Timeout[1] >= 5 && Timeout[2] == RT_TIMEOUT_COMEBACK)
            {
                Comeback = Timeout[3] | (Timeout[4] << 8) | (Timeout[5] << 16) | ((ULONG)Timeout[6] << 24);
                Comeback = (ULONG)min((ULONGLONG)Comeback * 1024 / 1000, 10000);
            }
            DPRINT1("RT2800USB: association comeback in %lu ms\n", Comeback);
            RtSleep(Comeback);
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("RT2800USB: no association response\n");
    return FALSE;
}

static
VOID
RtIndicateConnectionStart(
    _In_ PRT_ADAPTER Adapter)
{
    DOT11_CONNECTION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.BSSType = dot11_BSS_type_infrastructure;
    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_START, &Params, sizeof(Params));
}

static
VOID
RtIndicateAssociationStart(
    _In_ PRT_ADAPTER Adapter,
    _In_ PRT_BSS Bss)
{
    DOT11_ASSOCIATION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bss->Bssid, RT_ADDRESS_LENGTH);
    Params.SSID.uSSIDLength = Bss->SsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Bss->Ssid, Bss->SsidLength);
    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_START, &Params, sizeof(Params));
}

static
VOID
RtIndicateAssociationCompletion(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ DOT11_ASSOC_STATUS Status)
{
    PDOT11_ASSOCIATION_COMPLETION_PARAMETERS Params;
    ULONG Size;
    PUCHAR Tail;

    Size = sizeof(*Params) + Adapter->AssocRequestIeLength + Adapter->AssocResponseIeLength;
    Params = RtAllocate(Size);
    if (!Params)
        return;

    Params->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params->Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Params->Header.Size = sizeof(*Params);
    RtlCopyMemory(Params->MacAddr, Bssid, RT_ADDRESS_LENGTH);
    Params->uStatus = Status;

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Params->bReAssocReq = FALSE;
        Params->bReAssocResp = FALSE;
        Params->AuthAlgo = Adapter->AuthAlgorithm;
        Params->UnicastCipher = Adapter->UnicastCipher;
        Params->MulticastCipher = Adapter->MulticastCipher;
        Params->bFourAddressSupported = FALSE;
        Params->bPortAuthorized = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_80211_OPEN;
        Params->DSInfo = DOT11_DS_UNKNOWN;
        Params->uEncapTableOffset = 0;
        Params->uEncapTableSize = 0;
        Tail = (PUCHAR)(Params + 1);
        Params->uAssocReqOffset = sizeof(*Params);
        Params->uAssocReqSize = Adapter->AssocRequestIeLength;
        RtlCopyMemory(Tail, Adapter->AssocRequestIes, Adapter->AssocRequestIeLength);
        Params->uAssocRespOffset = sizeof(*Params) + Adapter->AssocRequestIeLength;
        Params->uAssocRespSize = Adapter->AssocResponseIeLength;
        RtlCopyMemory(Tail + Adapter->AssocRequestIeLength, Adapter->AssocResponseIes, Adapter->AssocResponseIeLength);
    }
    else
    {
        Size = sizeof(*Params);
    }

    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION, Params, Size);
    RtFree(Params);
}

static
VOID
RtIndicateConnectionCompletion(
    _In_ PRT_ADAPTER Adapter,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.uStatus = Status;
    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_COMPLETION, &Params, sizeof(Params));
}

static
VOID
RtIndicateDisassociation(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(RT_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ ULONG Reason)
{
    DOT11_DISASSOCIATION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bssid, RT_ADDRESS_LENGTH);
    Params.uReason = Reason;
    RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_DISASSOCIATION, &Params, sizeof(Params));
}

static
VOID
RtLeave(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN SendDeauth,
    _In_ ULONG Reason)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 2];
    UCHAR Bssid[RT_ADDRESS_LENGTH];
    ULONG Length;

    if (Adapter->LinkState != RtLinkAssociated)
        return;

    RtlCopyMemory(Bssid, Adapter->ConnectedBssid, RT_ADDRESS_LENGTH);

    if (SendDeauth)
    {
        Length = RtBuildHeader(Adapter, Frame, DOT11_SUBTYPE_DEAUTH, Bssid, Bssid);
        Frame[Length++] = 3;
        Frame[Length++] = 0;
        RtSendManagement(Adapter, Frame, Length);
        RtSleep(20);
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = RtLinkIdle;
    Adapter->PmfActive = FALSE;
    Adapter->SoftwareCrypto = FALSE;
    Adapter->SaQueryPending = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtRemoveKeys(Adapter);
    RtSetAssociated(Adapter, FALSE, 0);
    RtSetBssid(Adapter, NULL);

    RtIndicateDisassociation(Adapter, Bssid, Reason);
    RtIndicateLinkState(Adapter, FALSE);
    DPRINT1("RT2800USB: left %02x:%02x:%02x:%02x:%02x:%02x reason %lx\n",
            Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5], Reason);
}

static
VOID
RtDoConnect(
    _In_ PRT_ADAPTER Adapter)
{
    DOT11_ASSOC_STATUS Status = DOT11_ASSOC_STATUS_FAILURE;
    RT_BSS Candidate;
    PRT_BSS Bss;

    RtLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    RtIndicateConnectionStart(Adapter);

    Bss = RtSelectBss(Adapter, &Candidate);
    if (!Bss)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ScanSsid = Adapter->DesiredSsid;
        NdisReleaseSpinLock(&Adapter->Lock);
        RtScanChannels(Adapter, Adapter->HaveDesiredSsid ? &Adapter->DesiredSsid : NULL);
        Bss = RtSelectBss(Adapter, &Candidate);
    }

    if (!Bss)
    {
        DPRINT1("RT2800USB: desired network not found\n");
        RtIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_UNREACHABLE);
        return;
    }

    DPRINT1("RT2800USB: joining %.*s on channel %lu rssi %ld\n", Bss->SsidLength, Bss->Ssid, Bss->Channel, Bss->Rssi);

    RtSetChannel(Adapter, Bss->Channel);
    RtSetBssid(Adapter, Bss->Bssid);

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Adapter->ConnectedBssid, Bss->Bssid, RT_ADDRESS_LENGTH);
    Adapter->ConnectedChannel = Bss->Channel;
    Adapter->LinkState = RtLinkAuthenticating;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtIndicateAssociationStart(Adapter, Bss);

    if (RtAuthenticate(Adapter, Bss))
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->HoldRx = TRUE;
        Adapter->LinkState = RtLinkAssociating;
        NdisReleaseSpinLock(&Adapter->Lock);
        if (RtAssociate(Adapter, Bss))
            Status = DOT11_ASSOC_STATUS_SUCCESS;
    }

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->AssociationId = Adapter->MlmeAid;
        Adapter->ConnectedRssi = Bss->Rssi;
        Adapter->LastBeaconTime = KeQueryInterruptTime();
        Adapter->LinkState = RtLinkAssociated;
        Adapter->PmfActive = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK &&
                             Adapter->MulticastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             Adapter->UnicastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             (RtRsnCapabilities(Bss) & RT_RSN_CAP_MFPC) != 0;
        Adapter->SoftwareCrypto = Adapter->PmfActive;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("RT2800USB: management frame protection %s\n", Adapter->PmfActive ? "active" : "off");

        RtSetAssociated(Adapter, TRUE, Bss->BeaconPeriod);
        RtIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_SUCCESS);
        RtReleaseHeldReceives(Adapter, TRUE);
        RtIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_SUCCESS);
        RtIndicateLinkState(Adapter, TRUE);
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = RtLinkIdle;
    NdisReleaseSpinLock(&Adapter->Lock);
    RtReleaseHeldReceives(Adapter, FALSE);
    RtSetBssid(Adapter, NULL);
    RtIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_FAILURE);
    RtIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
}

static
VOID
RtCheckLink(
    _In_ PRT_ADAPTER Adapter)
{
    ULONGLONG Now = KeQueryInterruptTime(), Last;
    BOOLEAN Lost;

    NdisAcquireSpinLock(&Adapter->Lock);
    Last = Adapter->LastBeaconTime;
    if (Adapter->SaQueryPending && Now > Adapter->SaQueryDeadline)
    {
        Adapter->SaQueryPending = FALSE;
        Last = 0;
    }
    Lost = Adapter->LinkState == RtLinkAssociated &&
           (Last == 0 || Now - Last > (ULONGLONG)RT_BEACON_LOSS_MS * 10000);
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Lost)
        RtLeave(Adapter, Last != 0, Last == 0 ? DOT11_ASSOC_STATUS_PEER_DEAUTHENTICATED : DOT11_ASSOC_STATUS_UNREACHABLE);

}

VOID
NTAPI
RtWorkerThread(
    _In_ PVOID Context)
{
    PRT_ADAPTER Adapter = Context;
    LARGE_INTEGER Timeout;
    BOOLEAN Scan, Connect, Disconnect;

    Timeout.QuadPart = -10000000LL;

    while (!Adapter->WorkerStop)
    {
        KeWaitForSingleObject(&Adapter->WorkerEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Adapter->WorkerStop)
            break;

        NdisAcquireSpinLock(&Adapter->Lock);
        Scan = Adapter->ScanQueued;
        Connect = Adapter->ConnectQueued;
        Disconnect = Adapter->DisconnectQueued;
        Adapter->ScanQueued = FALSE;
        Adapter->ConnectQueued = FALSE;
        Adapter->DisconnectQueued = FALSE;
        NdisReleaseSpinLock(&Adapter->Lock);

        if (!Adapter->HardwareReady)
        {
            if (Scan)
            {
                NDIS_STATUS ScanStatus = NDIS_STATUS_FAILURE;
                RtIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
            }
            if (Connect)
                RtIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
            continue;
        }

        if (Disconnect)
            RtLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
        if (Connect)
            RtDoConnect(Adapter);
        if (Scan)
            RtDoScan(Adapter);

        RtCheckLink(Adapter);
    }

    RtLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    KeSetEvent(&Adapter->WorkerStopped, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
