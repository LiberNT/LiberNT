/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Station management: scanning, authentication, association and data receive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rtw8852bs.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define RTW_RX_CONTEXT_TAG       'X88R'
#define RTW_BSS_EXPIRY_100NS     (60ULL * 10000000ULL)
#define RTW_RSN_CAP_MFPC         0x0080
#define RTW_ACTION_SA_QUERY      8
#define RTW_SA_QUERY_REQUEST     0
#define RTW_SA_QUERY_RESPONSE    1
#define RTW_STATUS_REFUSED_TEMPORARILY 30
#define RTW_TIMEOUT_COMEBACK     3
#define RTW_PROTECTED_MGMT_MAX   512
#define RTW_SA_QUERY_TIMEOUT_100NS (1000ULL * 10000ULL)

typedef struct _RT_RX_PACKET
{
    DOT11_EXTSTA_RECV_CONTEXT Context;
    PMDL Mdl;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} RTW_RX_PACKET, *PRTW_RX_PACKET;

static const UCHAR RtwBroadcast[RTW_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static const UCHAR RtwChannels2G[] = { 1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10 };
static const UCHAR RtwChannels5G[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165 };

static const UCHAR RtwRates2G[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
static const UCHAR RtwExtRates2G[] = { 0x30, 0x48, 0x60, 0x6c };
static const UCHAR RtwRates5G[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };

ULONG
RtwFrameHeaderLength(
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    ULONG HeaderLength = DOT11_HEADER_LENGTH;

    if (Length < 2)
        return Length;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_CTRL)
        return min(Length, 16);

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA)
    {
        if ((Frame[1] & (DOT11_FC1_TODS | DOT11_FC1_FROMDS)) == (DOT11_FC1_TODS | DOT11_FC1_FROMDS))
            HeaderLength += RTW_ADDRESS_LENGTH;
        if (Frame[0] & DOT11_QOS_SUBTYPE)
        {
            HeaderLength += 2;
            if (Frame[1] & 0x80)
                HeaderLength += 4;
        }
    }
    else if (Frame[1] & 0x80)
    {
        HeaderLength += 4;
    }

    return min(HeaderLength, Length);
}

ULONG
RtwChannelToFrequency(
    _In_ ULONG Channel)
{
    if (Channel == 14)
        return 2484;
    if (Channel < 14)
        return 2407 + Channel * 5;
    return 5000 + Channel * 5;
}

VOID
RtwIndicateDot11Status(
    _In_ PRTW_ADAPTER Adapter,
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
RtwIndicateLinkState(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = Connected ? RTW_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed = Connected ? RTW_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_LINK_STATE, &LinkState, sizeof(LinkState));
}

static
const UCHAR*
RtwFindIe(
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
ULONG
RtwBssRateMask(
    _In_ PRTW_BSS Bss)
{
    static const UCHAR Rates[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    static const UCHAR Ids[] = { DOT11_IE_RATES, DOT11_IE_EXT_RATES };
    const UCHAR* Ie;
    ULONG Mask = 0;
    ULONG i, j, k;

    for (k = 0; k < RTL_NUMBER_OF(Ids); k++)
    {
        Ie = RtwFindIe(Bss->Ies, Bss->IeLength, Ids[k]);
        if (!Ie)
            continue;
        for (i = 0; i < Ie[1]; i++)
        {
            for (j = 0; j < RTL_NUMBER_OF(Rates); j++)
            {
                if ((Ie[2 + i] & 0x7F) == Rates[j])
                    Mask |= 1UL << j;
            }
        }
    }

    if (Bss->Channel > 14)
        Mask &= RTW_RATE_MASK_OFDM;
    return Mask;
}

static
USHORT
RtwRsnCapabilities(
    _In_ PRTW_BSS Bss)
{
    const UCHAR* Rsn = RtwFindIe(Bss->Ies, Bss->IeLength, DOT11_IE_RSN);
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
RtwBuildHeader(
    _In_ PRTW_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Bssid);

static
NTSTATUS
RtwSendManagement(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    UCHAR Protected[RTW_PROTECTED_MGMT_MAX];
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
        return RtwTxFrame(Adapter, Frame, Length, TRUE);

    RtlCopyMemory(Protected, Frame, DOT11_HEADER_LENGTH);
    Protected[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Protected, DOT11_HEADER_LENGTH, Pn, 0,
                    Frame + DOT11_HEADER_LENGTH, Length - DOT11_HEADER_LENGTH,
                    Protected + DOT11_HEADER_LENGTH);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    return RtwTxFrame(Adapter, Protected, Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN, TRUE);
}

static
VOID
RtwStartSaQuery(
    _In_ PRTW_ADAPTER Adapter)
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
    Adapter->SaQueryDeadline = KeQueryInterruptTime() + RTW_SA_QUERY_TIMEOUT_100NS;
    NdisReleaseSpinLock(&Adapter->Lock);

    QueryLength = RtwBuildHeader(Adapter, Query, DOT11_SUBTYPE_ACTION, Adapter->ConnectedBssid, Adapter->ConnectedBssid);
    Query[QueryLength++] = RTW_ACTION_SA_QUERY;
    Query[QueryLength++] = RTW_SA_QUERY_REQUEST;
    Query[QueryLength++] = (UCHAR)Id;
    Query[QueryLength++] = (UCHAR)(Id >> 8);
    RtwSendManagement(Adapter, Query, QueryLength);
}

static
BOOLEAN
RtwDecryptManagement(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _Out_writes_bytes_(RTW_PROTECTED_MGMT_MAX) PUCHAR Plain,
    _Out_ PULONG PlainLength)
{
    UCHAR Tk[16];
    ULONGLONG Pn = 0;
    BOOLEAN Valid;

    if (Length < DOT11_HEADER_LENGTH + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN ||
        Length > RTW_PROTECTED_MGMT_MAX)
    {
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->PmfActive && Adapter->MgmtKeyValid &&
            RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, RTW_ADDRESS_LENGTH) &&
            RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH);
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
        DPRINT1("RTW8852BS: protected management frame failed to decrypt\n");
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
RtwVerifyBip(
    _In_ PRTW_ADAPTER Adapter,
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
RtwUpdateBss(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi)
{
    const UCHAR *Ies, *Ssid, *Ds;
    ULONG IeLength, Channel, i, Slot = MAXULONG, Oldest = 0;
    ULONGLONG Now = KeQueryInterruptTime(), OldestTime = MAXULONGLONG;
    PRTW_BSS Bss;

    if (Length < DOT11_HEADER_LENGTH + 12)
        return;

    Ies = Frame + DOT11_HEADER_LENGTH + 12;
    IeLength = Length - DOT11_HEADER_LENGTH - 12;
    Ssid = RtwFindIe(Ies, IeLength, DOT11_IE_SSID);
    Ds = RtwFindIe(Ies, IeLength, DOT11_IE_DS_PARAMS);
    Channel = (Ds && Ds[1] >= 1) ? Ds[2] : Adapter->CurrentChannel;

    if (Ds && Ds[1] >= 1 && Channel != Adapter->CurrentChannel && Adapter->CurrentChannel <= 14)
        return;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (RtlEqualMemory(Adapter->Bss[i].Bssid, Frame + 16, RTW_ADDRESS_LENGTH))
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
        Slot = Adapter->BssCount < RTW_MAX_BSS ? Adapter->BssCount++ : Oldest;

    Bss = &Adapter->Bss[Slot];
    RtlCopyMemory(Bss->Bssid, Frame + 16, RTW_ADDRESS_LENGTH);
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
    Bss->IeLength = min(IeLength, RTW_MAX_IE_BYTES);
    RtlCopyMemory(Bss->Ies, Ies, Bss->IeLength);
    Bss->LastSeen = Now;

    if (Adapter->LinkState == RtwLinkAssociated &&
        RtlEqualMemory(Bss->Bssid, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH))
    {
        Adapter->LastBeaconTime = Now;
        Adapter->ConnectedRssi = Rssi;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
RtwMlmeReceive(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ BOOLEAN HwDecrypted)
{
    UCHAR Plain[RTW_PROTECTED_MGMT_MAX];
    UCHAR Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    const UCHAR* Body = Frame + DOT11_HEADER_LENGTH;
    BOOLEAN Protected = HwDecrypted;
    BOOLEAN Pmf;
    ULONG PlainLength;

    if (Length < DOT11_HEADER_LENGTH)
        return;

    if (Frame[1] & DOT11_FC1_PROTECTED)
    {
        if (!RtwDecryptManagement(Adapter, Frame, Length, Plain, &PlainLength))
            return;
        Frame = Plain;
        Length = PlainLength;
        Body = Frame + DOT11_HEADER_LENGTH;
        Protected = TRUE;
    }
    Pmf = Adapter->PmfActive && Adapter->MgmtKeyValid && Adapter->LinkState == RtwLinkAssociated;

    switch (Subtype)
    {
        case DOT11_SUBTYPE_BEACON:
        case DOT11_SUBTYPE_PROBE_RESP:
            RtwUpdateBss(Adapter, Frame, Length, Rssi);
            break;

        case DOT11_SUBTYPE_AUTH:
            if (Adapter->MlmeExpect == DOT11_SUBTYPE_AUTH &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH) &&
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
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH))
            {
                Adapter->MlmeStatus = Body[2] | (Body[3] << 8);
                Adapter->MlmeAid = (Body[4] | (Body[5] << 8)) & 0x3fff;
                Adapter->AssocResponseIeLength = min(Length - DOT11_HEADER_LENGTH - 6, RTW_MAX_IE_BYTES);
                RtlCopyMemory(Adapter->AssocResponseIes, Body + 6, Adapter->AssocResponseIeLength);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            break;

        case DOT11_SUBTYPE_DEAUTH:
        case DOT11_SUBTYPE_DISASSOC:
            if (Pmf &&
                (RtlEqualMemory(Frame + 4, RtwBroadcast, RTW_ADDRESS_LENGTH) ?
                 !RtwVerifyBip(Adapter, Frame, Length) : !Protected))
            {
                DPRINT1("RTW8852BS: unprotected %s, querying the AP\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation");
                if (RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH))
                    RtwStartSaQuery(Adapter);
                break;
            }
            if (Adapter->LinkState == RtwLinkAssociated &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH) &&
                (RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, RTW_ADDRESS_LENGTH) ||
                 RtlEqualMemory(Frame + 4, RtwBroadcast, RTW_ADDRESS_LENGTH)))
            {
                DPRINT1("RTW8852BS: AP sent %s reason %u\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation",
                        Length >= DOT11_HEADER_LENGTH + 2 ? (Body[0] | (Body[1] << 8)) : 0);
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->LastBeaconTime = 0;
                NdisReleaseSpinLock(&Adapter->Lock);
                RtwWakeCommand(Adapter);
            }
            break;

        case DOT11_SUBTYPE_ACTION:
            if (Length >= DOT11_HEADER_LENGTH + 4 &&
                Body[0] == RTW_ACTION_SA_QUERY &&
                Pmf && Protected &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH))
            {
                UCHAR Response[DOT11_HEADER_LENGTH + 4];
                ULONG ResponseLength;

                if (Body[1] == RTW_SA_QUERY_REQUEST)
                {
                    ResponseLength = RtwBuildHeader(Adapter, Response, DOT11_SUBTYPE_ACTION,
                                                   Adapter->ConnectedBssid, Adapter->ConnectedBssid);
                    Response[ResponseLength++] = RTW_ACTION_SA_QUERY;
                    Response[ResponseLength++] = RTW_SA_QUERY_RESPONSE;
                    Response[ResponseLength++] = Body[2];
                    Response[ResponseLength++] = Body[3];
                    RtwSendManagement(Adapter, Response, ResponseLength);
                    DPRINT1("RTW8852BS: answered SA Query %02x%02x\n", Body[2], Body[3]);
                }
                else if (Body[1] == RTW_SA_QUERY_RESPONSE)
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
RtwFreeRxNbl(
    _In_ PNET_BUFFER_LIST Nbl)
{
    PRTW_RX_PACKET Packet = Nbl->MiniportReserved[0];

    NdisFreeNetBufferList(Nbl);
    if (Packet)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, RTW_RX_CONTEXT_TAG);
    }
}

static
VOID
RtwReleaseHeldReceives(
    _In_ PRTW_ADAPTER Adapter,
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
        RtwFreeRxNbl(Head);
    }
}

VOID
RtwReceiveData(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ ULONG Rate)
{
    PNET_BUFFER_LIST Nbl;
    PRTW_RX_PACKET Packet;
    UCHAR Subtype;

    UNREFERENCED_PARAMETER(Rate);

    if ((Adapter->LinkState != RtwLinkAssociated && Adapter->LinkState != RtwLinkAssociating) ||
        !Adapter->DataPathRunning || Length < DOT11_HEADER_LENGTH + 8)
        return;

    Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    if (Subtype & 0x40)
        return;

    if (!RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH))
        return;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(RTW_RX_PACKET, Data) + Length, RTW_RX_CONTEXT_TAG);
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
        ExFreePoolWithTag(Packet, RTW_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Packet->Mdl, 0, Length);
    if (!Nbl)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, RTW_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    RtlZeroMemory(&Packet->Context, sizeof(Packet->Context));
    Packet->Context.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Packet->Context.Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    Packet->Context.Header.Size = sizeof(DOT11_EXTSTA_RECV_CONTEXT);
    Packet->Context.uPhyId = 0;
    Packet->Context.uChCenterFrequency = RtwChannelToFrequency(Adapter->CurrentChannel);
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
    if (Adapter->HoldRx || Adapter->LinkState != RtwLinkAssociated)
    {
        if (Adapter->HoldRx && Adapter->HeldRxCount < RTW_HELD_RX_MAX)
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
            RtwFreeRxNbl(Nbl);
        return;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    InterlockedIncrement(&Adapter->RxIndicated);
    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                       Nbl,
                                       NDIS_DEFAULT_PORT_NUMBER,
                                       1,
                                       KeGetCurrentIrql() >= DISPATCH_LEVEL ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0);
}

VOID
NTAPI
RtwMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;

    UNREFERENCED_PARAMETER(ReturnFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        RtwFreeRxNbl(Nbl);
        InterlockedDecrement(&Adapter->RxIndicated);
    }
}

ULONG
RtwBuildBssList(
    _In_ PRTW_ADAPTER Adapter,
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
        if (Now - Adapter->Bss[i].LastSeen <= RTW_BSS_EXPIRY_100NS)
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
        PRTW_BSS Bss = &Adapter->Bss[i];

        if (Now - Bss->LastSeen > RTW_BSS_EXPIRY_100NS)
            continue;

        Entry = (PDOT11_BSS_ENTRY)Cursor;
        RtlZeroMemory(Entry, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        Entry->uPhyId = 0;
        Entry->PhySpecificInfo.uChCenterFrequency = RtwChannelToFrequency(Bss->Channel);
        RtlCopyMemory(Entry->dot11BSSID, Bss->Bssid, RTW_ADDRESS_LENGTH);
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
RtwBuildHeader(
    _In_ PRTW_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Bssid)
{
    RtlZeroMemory(Frame, DOT11_HEADER_LENGTH);
    Frame[0] = DOT11_FC0_TYPE_MGMT | Subtype;
    RtlCopyMemory(Frame + 4, Destination, RTW_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 10, Adapter->CurrentAddress, RTW_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 16, Bssid, RTW_ADDRESS_LENGTH);
    return DOT11_HEADER_LENGTH;
}

static
ULONG
RtwAppendRates(
    _In_ PRTW_ADAPTER Adapter,
    _Out_writes_bytes_(32) PUCHAR Out)
{
    ULONG Offset = 0;

    if (Adapter->CurrentChannel > 14)
    {
        Out[Offset++] = DOT11_IE_RATES;
        Out[Offset++] = sizeof(RtwRates5G);
        RtlCopyMemory(Out + Offset, RtwRates5G, sizeof(RtwRates5G));
        return Offset + sizeof(RtwRates5G);
    }

    Out[Offset++] = DOT11_IE_RATES;
    Out[Offset++] = sizeof(RtwRates2G);
    RtlCopyMemory(Out + Offset, RtwRates2G, sizeof(RtwRates2G));
    Offset += sizeof(RtwRates2G);
    Out[Offset++] = DOT11_IE_EXT_RATES;
    Out[Offset++] = sizeof(RtwExtRates2G);
    RtlCopyMemory(Out + Offset, RtwExtRates2G, sizeof(RtwExtRates2G));
    return Offset + sizeof(RtwExtRates2G);
}

static
VOID
RtwSendProbeRequest(
    _In_ PRTW_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 2 + DOT11_SSID_MAX_LENGTH + 32];
    ULONG Length;

    Length = RtwBuildHeader(Adapter, Frame, DOT11_SUBTYPE_PROBE_REQ, RtwBroadcast, RtwBroadcast);
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
    Length += RtwAppendRates(Adapter, Frame + Length);
    RtwTxFrame(Adapter, Frame, Length, TRUE);
}

static
BOOLEAN
RtwPassiveChannel(
    _In_ ULONG Channel)
{
    return Channel >= 52 && Channel <= 144;
}

static
VOID
RtwScanChannels(
    _In_ PRTW_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(RtwChannels2G) && !Adapter->Halting; i++)
    {
        RtwSetChannel(Adapter, RtwChannels2G[i]);
        RtwSendProbeRequest(Adapter, Ssid);
        RtwSleep(RTW_SCAN_DWELL_MS);
    }

    for (i = 0; i < RTL_NUMBER_OF(RtwChannels5G) && !Adapter->Halting; i++)
    {
        RtwSetChannel(Adapter, RtwChannels5G[i]);
        if (!RtwPassiveChannel(RtwChannels5G[i]))
            RtwSendProbeRequest(Adapter, Ssid);
        RtwSleep(RtwPassiveChannel(RtwChannels5G[i]) ? RTW_SCAN_DWELL_MS + 30 : RTW_SCAN_DWELL_MS);
    }
}

static
VOID
RtwDoScan(
    _In_ PRTW_ADAPTER Adapter)
{
    NDIS_STATUS ScanStatus = NDIS_STATUS_SUCCESS;
    DOT11_SSID Ssid;
    BOOLEAN Associated;

    NdisAcquireSpinLock(&Adapter->Lock);
    Ssid = Adapter->ScanSsid;
    Associated = Adapter->LinkState == RtwLinkAssociated;
    Adapter->Scanning = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Associated)
        RtwScanChannels(Adapter, Ssid.uSSIDLength ? &Ssid : NULL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Scanning = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    DPRINT1("RTW8852BS: scan done, %lu networks\n", Adapter->BssCount);
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
}

static
PRTW_BSS
RtwSelectBss(
    _In_ PRTW_ADAPTER Adapter,
    _Out_ PRTW_BSS Copy)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PRTW_BSS Best = NULL, Bss;
    ULONG i;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        Bss = &Adapter->Bss[i];
        if (Now - Bss->LastSeen > RTW_BSS_EXPIRY_100NS)
            continue;
        if (!(Bss->Capability & 0x0001))
            continue;
        if (Adapter->HaveDesiredSsid &&
            (Bss->SsidLength != Adapter->DesiredSsid.uSSIDLength ||
             !RtlEqualMemory(Bss->Ssid, Adapter->DesiredSsid.ucSSID, Bss->SsidLength)))
        {
            continue;
        }
        if (Adapter->HaveDesiredBssid && !RtlEqualMemory(Bss->Bssid, Adapter->DesiredBssid, RTW_ADDRESS_LENGTH))
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
RtwWaitMlme(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return KeWaitForSingleObject(&Adapter->MlmeEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static
BOOLEAN
RtwAuthenticate(
    _In_ PRTW_ADAPTER Adapter,
    _In_ PRTW_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 6];
    ULONG Length, Attempt;

    for (Attempt = 0; Attempt < RTW_MLME_RETRIES; Attempt++)
    {
        Length = RtwBuildHeader(Adapter, Frame, DOT11_SUBTYPE_AUTH, Bss->Bssid, Bss->Bssid);
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 1;
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 0;

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_AUTH;
        RtwTxFrame(Adapter, Frame, Length, TRUE);
        if (RtwWaitMlme(Adapter, RTW_AUTH_TIMEOUT_MS))
        {
            DPRINT1("RTW8852BS: authentication status %u\n", Adapter->MlmeStatus);
            return Adapter->MlmeStatus == 0;
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("RTW8852BS: no authentication response\n");
    return FALSE;
}

static
ULONG
RtwBuildRsnIe(
    _In_ PRTW_ADAPTER Adapter,
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
        Out[20] = (UCHAR)RTW_RSN_CAP_MFPC;
    }
    return sizeof(Template);
}

static
BOOLEAN
RtwAssociate(
    _In_ PRTW_ADAPTER Adapter,
    _In_ PRTW_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 4 + 2 + DOT11_SSID_MAX_LENGTH + 32 + 24];
    ULONG Length, IeStart, Attempt, Comebacks = 0;
    USHORT Capability;
    BOOLEAN Secure = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK;

    for (Attempt = 0; Attempt < RTW_MLME_RETRIES; Attempt++)
    {
        Length = RtwBuildHeader(Adapter, Frame, DOT11_SUBTYPE_ASSOC_REQ, Bss->Bssid, Bss->Bssid);
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
        Length += RtwAppendRates(Adapter, Frame + Length);
        if (Secure)
            Length += RtwBuildRsnIe(Adapter, Frame + Length);

        Adapter->AssocRequestIeLength = Length - IeStart;
        RtlCopyMemory(Adapter->AssocRequestIes, Frame + IeStart, Adapter->AssocRequestIeLength);

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_ASSOC_RESP;
        RtwTxFrame(Adapter, Frame, Length, TRUE);
        if (RtwWaitMlme(Adapter, RTW_ASSOC_TIMEOUT_MS))
        {
            const UCHAR* Timeout;
            ULONG Comeback;

            DPRINT1("RTW8852BS: association status %u aid %u\n", Adapter->MlmeStatus, Adapter->MlmeAid);
            if (Adapter->MlmeStatus != RTW_STATUS_REFUSED_TEMPORARILY)
                return Adapter->MlmeStatus == 0;

            Timeout = RtwFindIe(Adapter->AssocResponseIes, Adapter->AssocResponseIeLength,
                               DOT11_IE_TIMEOUT_INTERVAL);
            Comeback = 1000;
            if (Timeout && Timeout[1] >= 5 && Timeout[2] == RTW_TIMEOUT_COMEBACK)
            {
                Comeback = Timeout[3] | (Timeout[4] << 8) | (Timeout[5] << 16) | ((ULONG)Timeout[6] << 24);
                Comeback = (ULONG)min((ULONGLONG)Comeback * 1024 / 1000, 10000);
            }
            if (Comebacks)
                Comeback = max(Comeback, RTW_SA_QUERY_MAX_TIMEOUT_MS);
            DPRINT1("RTW8852BS: association comeback in %lu ms\n", Comeback);
            RtwSleep(Comeback);
            if (++Comebacks < RTW_ASSOC_COMEBACK_MAX)
                Attempt--;
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("RTW8852BS: no association response\n");
    return FALSE;
}

static
VOID
RtwIndicateConnectionStart(
    _In_ PRTW_ADAPTER Adapter)
{
    DOT11_CONNECTION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.BSSType = dot11_BSS_type_infrastructure;
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_START, &Params, sizeof(Params));
}

static
VOID
RtwIndicateAssociationStart(
    _In_ PRTW_ADAPTER Adapter,
    _In_ PRTW_BSS Bss)
{
    DOT11_ASSOCIATION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bss->Bssid, RTW_ADDRESS_LENGTH);
    Params.SSID.uSSIDLength = Bss->SsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Bss->Ssid, Bss->SsidLength);
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_START, &Params, sizeof(Params));
}

static
VOID
RtwIndicateAssociationCompletion(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ DOT11_ASSOC_STATUS Status)
{
    PDOT11_ASSOCIATION_COMPLETION_PARAMETERS Params;
    ULONG Size;
    PUCHAR Tail;

    Size = sizeof(*Params) + Adapter->AssocRequestIeLength + Adapter->AssocResponseIeLength;
    Params = RtwAllocate(Size);
    if (!Params)
        return;

    Params->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params->Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Params->Header.Size = sizeof(*Params);
    RtlCopyMemory(Params->MacAddr, Bssid, RTW_ADDRESS_LENGTH);
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

    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION, Params, Size);
    RtwFree(Params);
}

static
VOID
RtwIndicateConnectionCompletion(
    _In_ PRTW_ADAPTER Adapter,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.uStatus = Status;
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_COMPLETION, &Params, sizeof(Params));
}

static
VOID
RtwIndicateDisassociation(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ ULONG Reason)
{
    DOT11_DISASSOCIATION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bssid, RTW_ADDRESS_LENGTH);
    Params.uReason = Reason;
    RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_DISASSOCIATION, &Params, sizeof(Params));
}

static
VOID
RtwLeave(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN SendDeauth,
    _In_ ULONG Reason)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 2];
    UCHAR Bssid[RTW_ADDRESS_LENGTH];
    ULONG Length;

    if (Adapter->LinkState != RtwLinkAssociated)
        return;

    RtlCopyMemory(Bssid, Adapter->ConnectedBssid, RTW_ADDRESS_LENGTH);

    if (SendDeauth)
    {
        Length = RtwBuildHeader(Adapter, Frame, DOT11_SUBTYPE_DEAUTH, Bssid, Bssid);
        Frame[Length++] = 3;
        Frame[Length++] = 0;
        RtwSendManagement(Adapter, Frame, Length);
        RtwSleep(20);
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = RtwLinkIdle;
    Adapter->PmfActive = FALSE;
    Adapter->SoftwareCrypto = FALSE;
    Adapter->SaQueryPending = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtwClearKeys(Adapter);
    RtwVifSetLink(Adapter, NULL, FALSE, 0, 0);
    RtwSetRxFilter(Adapter, TRUE);

    RtwIndicateDisassociation(Adapter, Bssid, Reason);
    RtwIndicateLinkState(Adapter, FALSE);
    DPRINT1("RTW8852BS: left %02x:%02x:%02x:%02x:%02x:%02x reason %lx\n",
            Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5], Reason);
}

static
VOID
RtwDoConnect(
    _In_ PRTW_ADAPTER Adapter)
{
    DOT11_ASSOC_STATUS Status = DOT11_ASSOC_STATUS_FAILURE;
    RTW_BSS Candidate;
    PRTW_BSS Bss;
    ULONG Join;

    RtwLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    RtwIndicateConnectionStart(Adapter);

    Bss = RtwSelectBss(Adapter, &Candidate);
    if (!Bss)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ScanSsid = Adapter->DesiredSsid;
        NdisReleaseSpinLock(&Adapter->Lock);
        RtwScanChannels(Adapter, Adapter->HaveDesiredSsid ? &Adapter->DesiredSsid : NULL);
        Bss = RtwSelectBss(Adapter, &Candidate);
    }

    if (!Bss)
    {
        DPRINT1("RTW8852BS: desired network not found\n");
        RtwIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_UNREACHABLE);
        return;
    }

    DPRINT1("RTW8852BS: joining %.*s on channel %lu rssi %ld\n", Bss->SsidLength, Bss->Ssid, Bss->Channel, Bss->Rssi);

    RtwSetChannel(Adapter, (UCHAR)Bss->Channel);
    RtwVifSetLink(Adapter, Bss->Bssid, FALSE, 0, Bss->BeaconPeriod);

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Adapter->ConnectedBssid, Bss->Bssid, RTW_ADDRESS_LENGTH);
    Adapter->ConnectedChannel = Bss->Channel;
    Adapter->LinkState = RtwLinkAuthenticating;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtwIndicateAssociationStart(Adapter, Bss);

    for (Join = 0; Join < RTW_JOIN_ATTEMPTS && Status != DOT11_ASSOC_STATUS_SUCCESS && !Adapter->Halting; Join++)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->LinkState = RtwLinkAuthenticating;
        NdisReleaseSpinLock(&Adapter->Lock);
        if (!RtwAuthenticate(Adapter, Bss))
            continue;
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->HoldRx = TRUE;
        Adapter->LinkState = RtwLinkAssociating;
        NdisReleaseSpinLock(&Adapter->Lock);
        if (RtwAssociate(Adapter, Bss))
            Status = DOT11_ASSOC_STATUS_SUCCESS;
    }

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->AssociationId = Adapter->MlmeAid;
        Adapter->ConnectedRssi = Bss->Rssi;
        Adapter->LastBeaconTime = KeQueryInterruptTime();
        Adapter->LinkState = RtwLinkAssociated;
        Adapter->PmfActive = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK &&
                             Adapter->MulticastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             Adapter->UnicastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             (RtwRsnCapabilities(Bss) & RTW_RSN_CAP_MFPC) != 0;
        Adapter->SoftwareCrypto = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("RTW8852BS: management frame protection %s\n", Adapter->PmfActive ? "active" : "off");

        RtwVifSetLink(Adapter, Bss->Bssid, TRUE, Adapter->AssociationId, Bss->BeaconPeriod);
        RtwVifSetRates(Adapter, RtwBssRateMask(Bss));
        RtwSetRxFilter(Adapter, FALSE);
        RtwIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_SUCCESS);
        RtwReleaseHeldReceives(Adapter, TRUE);
        RtwIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_SUCCESS);
        RtwIndicateLinkState(Adapter, TRUE);
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = RtwLinkIdle;
    NdisReleaseSpinLock(&Adapter->Lock);
    RtwReleaseHeldReceives(Adapter, FALSE);
    RtwVifSetLink(Adapter, NULL, FALSE, 0, 0);
    RtwIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_FAILURE);
    RtwIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
}

static
VOID
RtwCheckLink(
    _In_ PRTW_ADAPTER Adapter)
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
    Lost = Adapter->LinkState == RtwLinkAssociated &&
           (Last == 0 || Now - Last > (ULONGLONG)RTW_BEACON_LOSS_MS * 10000);
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Lost)
        RtwLeave(Adapter, Last != 0, Last == 0 ? DOT11_ASSOC_STATUS_PEER_DEAUTHENTICATED : DOT11_ASSOC_STATUS_UNREACHABLE);

}

VOID
NTAPI
RtwCommandThread(
    _In_ PVOID Context)
{
    PRTW_ADAPTER Adapter = Context;
    LARGE_INTEGER Timeout;
    BOOLEAN Scan, Connect, Disconnect;

    Timeout.QuadPart = -10000000LL;

    while (!Adapter->CommandStop)
    {
        KeWaitForSingleObject(&Adapter->CommandEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Adapter->CommandStop)
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
                RtwIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
            }
            if (Connect)
                RtwIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
            continue;
        }

        if (Disconnect)
            RtwLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
        if (Connect)
            RtwDoConnect(Adapter);
        if (Scan)
            RtwDoScan(Adapter);

        RtwCheckLink(Adapter);
    }

    RtwLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    KeSetEvent(&Adapter->CommandStopped, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
