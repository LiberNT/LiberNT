/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI host framework: Native 802.11 upper edge
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_wdi.h"

#define NDIS6_WDI_BSS_LIFETIME      (120ULL * 10000000ULL)
#define NDIS6_WDI_BEACON_PERIOD     100
#define NDIS6_WDI_REASON_LEAVING    3

static NDIS_OID Ndis6WdiSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_DOT11_MPDU_MAX_LENGTH,
    OID_DOT11_OPERATION_MODE_CAPABILITY,
    OID_DOT11_CURRENT_OPERATION_MODE,
    OID_DOT11_CURRENT_PACKET_FILTER,
    OID_DOT11_CURRENT_PHY_TYPE,
    OID_DOT11_SUPPORTED_PHY_TYPES,
    OID_DOT11_NIC_POWER_STATE,
    OID_DOT11_HARDWARE_PHY_STATE,
    OID_DOT11_MAC_ADDRESS,
    OID_DOT11_PERMANENT_ADDRESS,
    OID_DOT11_CURRENT_ADDRESS,
    OID_DOT11_MULTICAST_LIST,
    OID_DOT11_BEACON_PERIOD,
    OID_DOT11_CURRENT_CHANNEL_NUMBER,
    OID_DOT11_RTS_THRESHOLD,
    OID_DOT11_FRAGMENTATION_THRESHOLD,
    OID_DOT11_AUTO_CONFIG_ENABLED,
    OID_DOT11_SCAN_REQUEST,
    OID_DOT11_ENUM_BSS_LIST,
    OID_DOT11_FLUSH_BSS_LIST,
    OID_DOT11_DESIRED_SSID_LIST,
    OID_DOT11_DESIRED_BSSID_LIST,
    OID_DOT11_DESIRED_BSS_TYPE,
    OID_DOT11_CONNECT_REQUEST,
    OID_DOT11_DISCONNECT_REQUEST,
    OID_DOT11_RESET_REQUEST,
    OID_DOT11_EXCLUDE_UNENCRYPTED,
    OID_DOT11_PRIVACY_EXEMPTION_LIST,
    OID_DOT11_HIDDEN_NETWORK_ENABLED,
    OID_DOT11_CURRENT_PHY_ID,
    OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
    OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
    OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
    OID_DOT11_SUPPORTED_UNICAST_ALGORITHM_PAIR,
    OID_DOT11_SUPPORTED_MULTICAST_ALGORITHM_PAIR,
    OID_DOT11_CIPHER_DEFAULT_KEY,
    OID_DOT11_CIPHER_DEFAULT_KEY_ID,
    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
};

static
ULONG
Ndis6WdiChannelToFrequency(
    _In_ ULONG Channel,
    _In_ ULONG BandId)
{
    if (Channel == 0)
        return 0;
    if (BandId == WDI_BAND_ID_5000)
        return 5000 + Channel * 5;
    if (BandId == WDI_BAND_ID_6000)
        return 5950 + Channel * 5;
    if (Channel == 14)
        return 2484;
    return 2407 + Channel * 5;
}

static
DOT11_ASSOC_STATUS
Ndis6WdiAssocStatus(
    _In_ ULONG Status,
    _In_ ULONG StatusCode)
{
    switch (Status)
    {
        case WDI_ASSOC_STATUS_SUCCESS: return DOT11_ASSOC_STATUS_SUCCESS;
        case WDI_ASSOC_STATUS_UNREACHABLE: return DOT11_ASSOC_STATUS_UNREACHABLE;
        case WDI_ASSOC_STATUS_RADIO_OFF: return DOT11_ASSOC_STATUS_RADIO_OFF;
        case WDI_ASSOC_STATUS_PHY_DISABLED: return DOT11_ASSOC_STATUS_PHY_DISABLED;
        case WDI_ASSOC_STATUS_ABORTED: return DOT11_ASSOC_STATUS_CANCELLED;
        case WDI_ASSOC_STATUS_CANDIDATE_LIST_EXHAUSTED: return DOT11_ASSOC_STATUS_CANDIDATE_LIST_EXHAUSTED;
        case WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST: return DOT11_ASSOC_STATUS_DISASSOCIATED_BY_OS;
        case WDI_ASSOC_STATUS_DISASSOCIATE_BY_DEVICE_RESET: return DOT11_ASSOC_STATUS_DISASSOCIATED_BY_RESET;
        case WDI_ASSOC_STATUS_ROAMING_BETTER_AP_FOUND: return DOT11_ASSOC_STATUS_ROAMING_BETTER_AP_FOUND;
        case WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST: return DOT11_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST;
        case WDI_ASSOC_STATUS_PEER_DEAUTHENTICATED: return DOT11_ASSOC_STATUS_PEER_DEAUTHENTICATED | (StatusCode & DOT11_ASSOC_STATUS_REASON_CODE_MASK);
        case WDI_ASSOC_STATUS_PEER_DISASSOCIATED: return DOT11_ASSOC_STATUS_PEER_DISASSOCIATED | (StatusCode & DOT11_ASSOC_STATUS_REASON_CODE_MASK);
        case WDI_ASSOC_STATUS_ASSOC_FAILED_BY_PEER: return DOT11_ASSOC_STATUS_ASSOCIATION_RESPONSE | (StatusCode & DOT11_ASSOC_STATUS_REASON_CODE_MASK);
        default: return DOT11_ASSOC_STATUS_FAILURE;
    }
}

static
VOID
Ndis6WdiFreeBss(
    _In_ PNDIS6_WDI_BSS Bss)
{
    if (Bss->Frame != NULL)
        ExFreePoolWithTag(Bss->Frame, NDIS6_WDI_TAG);
    if (Bss->DeviceContext != NULL)
        ExFreePoolWithTag(Bss->DeviceContext, NDIS6_WDI_TAG);
    ExFreePoolWithTag(Bss, NDIS6_WDI_TAG);
}

static
PUCHAR
Ndis6WdiDuplicate(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PUCHAR Copy;

    if (Length == 0)
        return NULL;

    Copy = ExAllocatePoolWithTag(NonPagedPool, Length, NDIS6_WDI_TAG);
    if (Copy != NULL)
        RtlCopyMemory(Copy, Data, Length);
    return Copy;
}

static
VOID
Ndis6WdiParseBssFrame(
    _Inout_ PNDIS6_WDI_BSS Bss)
{
    PUCHAR Body = Bss->Frame;
    ULONG Length = Bss->FrameLength;
    ULONG Offset;

    Bss->Ies = NULL;
    Bss->IesLength = 0;
    Bss->SsidLength = 0;

    if (Body == NULL)
        return;

    if (Length >= 24 + 12 && (Body[0] & 0x0C) == 0 && RtlEqualMemory(Body + 16, Bss->Bssid, sizeof(DOT11_MAC_ADDRESS)))
    {
        Body += 24;
        Length -= 24;
    }

    if (Length < 12)
        return;

    Bss->Timestamp = Ndis6WdiRead64(Body);
    Bss->BeaconPeriod = (USHORT)Ndis6WdiRead16(Body + 8);
    Bss->Capability = (USHORT)Ndis6WdiRead16(Body + 10);
    Bss->Ies = Body + 12;
    Bss->IesLength = Length - 12;

    for (Offset = 0; Offset + 2 <= Bss->IesLength; Offset += 2 + Bss->Ies[Offset + 1])
    {
        if (Offset + 2 + Bss->Ies[Offset + 1] > Bss->IesLength)
            break;
        if (Bss->Ies[Offset] == 0 && Bss->Ies[Offset + 1] <= DOT11_SSID_MAX_LENGTH)
        {
            Bss->SsidLength = Bss->Ies[Offset + 1];
            RtlCopyMemory(Bss->Ssid, Bss->Ies + Offset + 2, Bss->SsidLength);
            break;
        }
    }
}

static
VOID
Ndis6WdiBssUpdate(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(Length) PUCHAR Entry,
    _In_ ULONG Length)
{
    PNDIS6_WDI_BSS Bss;
    PNDIS6_WDI_BSS Old = NULL;
    PNDIS6_WDI_BSS Evict = NULL;
    NDIS6_WDI_TLV Tlv;
    PLIST_ENTRY Link;
    KIRQL OldIrql;

    if (!Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BSSID, &Tlv) || Tlv.Length < sizeof(DOT11_MAC_ADDRESS))
        return;

    Bss = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Bss), NDIS6_WDI_TAG);
    if (Bss == NULL)
        return;
    RtlZeroMemory(Bss, sizeof(*Bss));
    RtlCopyMemory(Bss->Bssid, Tlv.Value, sizeof(DOT11_MAC_ADDRESS));

    if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BSS_ENTRY_SIGNAL_INFO, &Tlv) && Tlv.Length >= 8)
    {
        Bss->Rssi = (LONG)Ndis6WdiRead32(Tlv.Value);
        Bss->LinkQuality = Ndis6WdiRead32(Tlv.Value + 4);
    }

    if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BSS_ENTRY_CHANNEL_INFO, &Tlv) && Tlv.Length >= 8)
    {
        Bss->Channel = Ndis6WdiRead32(Tlv.Value);
        Bss->BandId = Ndis6WdiRead32(Tlv.Value + 4);
    }

    if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_PROBE_RESPONSE_FRAME, &Tlv) && Tlv.Length != 0)
        Bss->FrameTlv = WDI_TLV_PROBE_RESPONSE_FRAME;
    else if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BEACON_FRAME, &Tlv) && Tlv.Length != 0)
        Bss->FrameTlv = WDI_TLV_BEACON_FRAME;

    if (Bss->FrameTlv != 0)
    {
        Bss->Frame = Ndis6WdiDuplicate(Tlv.Value, Tlv.Length);
        Bss->FrameLength = Bss->Frame != NULL ? Tlv.Length : 0;
    }

    if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BSS_ENTRY_DEVICE_CONTEXT, &Tlv) && Tlv.Length != 0)
    {
        Bss->DeviceContext = Ndis6WdiDuplicate(Tlv.Value, Tlv.Length);
        Bss->DeviceContextLength = Bss->DeviceContext != NULL ? Tlv.Length : 0;
    }

    Bss->Updated = KeQueryInterruptTime();

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
    {
        PNDIS6_WDI_BSS Candidate = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);

        if (RtlEqualMemory(Candidate->Bssid, Bss->Bssid, sizeof(DOT11_MAC_ADDRESS)))
        {
            Old = Candidate;
            break;
        }
    }

    if (Old != NULL)
    {
        RemoveEntryList(&Old->Link);
        Wdi->BssCount--;
        if (Bss->Frame == NULL && Old->Frame != NULL)
        {
            Bss->Frame = Old->Frame;
            Bss->FrameLength = Old->FrameLength;
            Bss->FrameTlv = Old->FrameTlv;
            Old->Frame = NULL;
        }
        if (Bss->DeviceContext == NULL && Old->DeviceContext != NULL)
        {
            Bss->DeviceContext = Old->DeviceContext;
            Bss->DeviceContextLength = Old->DeviceContextLength;
            Old->DeviceContext = NULL;
        }
    }
    else if (Wdi->BssCount >= NDIS6_WDI_MAX_BSS)
    {
        for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
        {
            PNDIS6_WDI_BSS Candidate = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);

            if (Evict == NULL || Candidate->Updated < Evict->Updated)
                Evict = Candidate;
        }
        if (Evict != NULL)
        {
            RemoveEntryList(&Evict->Link);
            Wdi->BssCount--;
        }
    }

    Ndis6WdiParseBssFrame(Bss);
    InsertTailList(&Wdi->BssList, &Bss->Link);
    Wdi->BssCount++;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (Old != NULL)
        Ndis6WdiFreeBss(Old);
    if (Evict != NULL)
        Ndis6WdiFreeBss(Evict);
}

static
VOID
Ndis6WdiFlushBssList(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    LIST_ENTRY Free;
    PLIST_ENTRY Link;
    KIRQL OldIrql;

    InitializeListHead(&Free);
    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    while (!IsListEmpty(&Wdi->BssList))
    {
        Link = RemoveHeadList(&Wdi->BssList);
        InsertTailList(&Free, Link);
    }
    Wdi->BssCount = 0;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    while (!IsListEmpty(&Free))
    {
        Link = RemoveHeadList(&Free);
        Ndis6WdiFreeBss(CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link));
    }
}

static
BOOLEAN
Ndis6WdiBssFresh(
    _In_ PNDIS6_WDI_BSS Bss,
    _In_ ULONGLONG Now)
{
    return Now - Bss->Updated <= NDIS6_WDI_BSS_LIFETIME;
}

static
NDIS_STATUS
Ndis6WdiBuildBssList(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Out_writes_bytes_(Length) PUCHAR Output,
    _In_ ULONG Length,
    _Out_ PULONG Written,
    _Out_ PULONG Needed)
{
    DOT11_BSS_ENTRY Header;
    DOT11_BYTE_ARRAY Array;
    ULONGLONG Now = KeQueryInterruptTime();
    PUCHAR Snapshot;
    ULONG Payload = 0;
    ULONG Capacity;
    ULONG Offset = 0;
    PLIST_ENTRY Link;
    KIRQL OldIrql;

    *Written = 0;
    *Needed = 0;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
    {
        PNDIS6_WDI_BSS Bss = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);

        if (Ndis6WdiBssFresh(Bss, Now))
            Payload += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Bss->IesLength;
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    *Needed = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Payload;
    if (Output == NULL || Length < *Needed)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    Capacity = Payload;
    Snapshot = Capacity != 0 ? ExAllocatePoolWithTag(NonPagedPool, Capacity, NDIS6_WDI_TAG) : NULL;
    if (Capacity != 0 && Snapshot == NULL)
        return NDIS_STATUS_RESOURCES;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
    {
        PNDIS6_WDI_BSS Bss = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);
        ULONG EntrySize = FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Bss->IesLength;

        if (!Ndis6WdiBssFresh(Bss, Now) || Offset + EntrySize > Capacity)
            continue;

        RtlZeroMemory(&Header, sizeof(Header));
        Header.uPhyId = 0;
        Header.PhySpecificInfo.uChCenterFrequency = Ndis6WdiChannelToFrequency(Bss->Channel, Bss->BandId);
        RtlCopyMemory(Header.dot11BSSID, Bss->Bssid, sizeof(DOT11_MAC_ADDRESS));
        Header.dot11BSSType = (Bss->Capability & 0x0002) ? dot11_BSS_type_independent : dot11_BSS_type_infrastructure;
        Header.lRSSI = Bss->Rssi;
        Header.uLinkQuality = Bss->LinkQuality;
        Header.bInRegDomain = TRUE;
        Header.usBeaconPeriod = Bss->BeaconPeriod != 0 ? Bss->BeaconPeriod : NDIS6_WDI_BEACON_PERIOD;
        Header.ullTimestamp = Bss->Timestamp;
        Header.ullHostTimestamp = Bss->Updated;
        Header.usCapabilityInformation = Bss->Capability;
        Header.uBufferLength = Bss->IesLength;

        RtlCopyMemory(Snapshot + Offset, &Header, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        if (Bss->IesLength != 0)
            RtlCopyMemory(Snapshot + Offset + FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer), Bss->Ies, Bss->IesLength);
        Offset += EntrySize;
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    RtlZeroMemory(&Array, sizeof(Array));
    Array.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Array.Header.Revision = DOT11_BSS_ENTRY_BYTE_ARRAY_REVISION_1;
    Array.Header.Size = sizeof(DOT11_BYTE_ARRAY);
    Array.uNumOfBytes = Offset;
    Array.uTotalNumOfBytes = Offset;
    RtlCopyMemory(Output, &Array, FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer));
    if (Offset != 0)
        RtlCopyMemory(Output + FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer), Snapshot, Offset);
    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, NDIS6_WDI_TAG);

    *Written = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Offset;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6WdiDot11Initialize(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    PNDIS6_WDI_DOT11 Dot11 = &Wdi->Dot11;

    Dot11->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Dot11->AutoConfig = TRUE;
    Dot11->AutoConfigFlags = DOT11_PHY_AUTO_CONFIG_ENABLED_FLAG | DOT11_MAC_AUTO_CONFIG_ENABLED_FLAG;
    Dot11->CurrentPhyType = dot11_phy_type_any;
    Dot11->RtsThreshold = 2347;
    Dot11->FragmentationThreshold = 2346;
    Dot11->DesiredBssType = dot11_BSS_type_infrastructure;
    Dot11->AuthCount = 1;
    Dot11->Auth[0] = DOT11_AUTH_ALGO_80211_OPEN;
    Dot11->UnicastCount = 1;
    Dot11->Unicast[0] = DOT11_CIPHER_ALGO_NONE;
    Dot11->MulticastCipherCount = 1;
    Dot11->MulticastCipher[0] = DOT11_CIPHER_ALGO_NONE;
    return NDIS_STATUS_SUCCESS;
}

VOID
Ndis6WdiDot11Cleanup(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    Ndis6WdiFlushBssList(Wdi);
}

static
VOID
Ndis6WdiAddPhyType(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG PhyType)
{
    ULONG i;

    for (i = 0; i < Wdi->PhyTypeCount; i++)
    {
        if ((ULONG)Wdi->PhyTypes[i] == PhyType)
            return;
    }
    if (Wdi->PhyTypeCount < NDIS6_WDI_MAX_PHY_TYPES)
        Wdi->PhyTypes[Wdi->PhyTypeCount++] = (DOT11_PHY_TYPE)PhyType;
}

static
ULONG
Ndis6WdiParsePairs(
    _In_ PNDIS6_WDI_TLV Tlv,
    _Out_writes_(NDIS6_WDI_MAX_ALGO_PAIRS) PDOT11_AUTH_CIPHER_PAIR Pairs)
{
    ULONG Count = 0;
    ULONG Offset;

    for (Offset = 0; Offset + 8 <= Tlv->Length && Count < NDIS6_WDI_MAX_ALGO_PAIRS; Offset += 8)
    {
        Pairs[Count].AuthAlgoId = (DOT11_AUTH_ALGORITHM)Ndis6WdiRead32(Tlv->Value + Offset);
        Pairs[Count].CipherAlgoId = (DOT11_CIPHER_ALGORITHM)Ndis6WdiRead32(Tlv->Value + Offset + 4);
        Count++;
    }
    return Count;
}

NDIS_STATUS
Ndis6WdiDot11ParseCapabilities(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(Length) PUCHAR Tlvs,
    _In_ ULONG Length)
{
    NDIS6_WDI_TLV Container;
    NDIS6_WDI_TLV Tlv;
    UCHAR Caps[32];
    PUCHAR Cursor;
    ULONG Offset;

    if (Tlvs == NULL || Length == 0)
        return NDIS_STATUS_INVALID_DATA;

    if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_INTERFACE_ATTRIBUTES, &Container))
    {
        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_INTERFACE_CAPABILITIES, &Tlv))
        {
            RtlZeroMemory(Caps, sizeof(Caps));
            RtlCopyMemory(Caps, Tlv.Value, min(Tlv.Length, sizeof(Caps)));
            Wdi->Mtu = Ndis6WdiRead32(Caps);
            Wdi->MaxMulticastListSize = Ndis6WdiRead32(Caps + 4);
            Wdi->BackFillSize = (USHORT)Ndis6WdiRead16(Caps + 8);
            RtlCopyMemory(Wdi->PermanentAddress, Caps + 10, 6);
            Wdi->MaxTxRateKbps = Ndis6WdiRead32(Caps + 16);
            Wdi->MaxRxRateKbps = Ndis6WdiRead32(Caps + 20);
            if (Tlv.Length >= 26)
            {
                Wdi->HardwareRadioOn = Caps[24] != 0;
                Wdi->SoftwareRadioOn = Caps[25] != 0;
            }
            if (Tlv.Length >= 50)
                Wdi->BssTransitionSupported = Tlv.Value[49] != 0;
        }

        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_IHV_NON_WDI_OIDS_LIST, &Tlv))
        {
            for (Offset = 0; Offset + 4 <= Tlv.Length && Wdi->NonWdiOidCount < NDIS6_WDI_MAX_NON_WDI_OIDS; Offset += 4)
                Wdi->NonWdiOids[Wdi->NonWdiOidCount++] = Ndis6WdiRead32(Tlv.Value + Offset);
        }
    }

    if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_STATION_ATTRIBUTES, &Container))
    {
        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_UNICAST_ALGORITHM_LIST, &Tlv))
            Wdi->UnicastPairCount = Ndis6WdiParsePairs(&Tlv, Wdi->UnicastPairs);
        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_MULTICAST_DATA_ALGORITHM_LIST, &Tlv))
            Wdi->MulticastPairCount = Ndis6WdiParsePairs(&Tlv, Wdi->MulticastPairs);
    }

    Wdi->TxRxCaps.InterconnectType = WDI_INTERCONNECT_UNKNOWN;
    Wdi->TxRxCaps.TransmitCapabilities.MaxScatterGatherElementsPerFrame = 32;
    Wdi->TxRxCaps.TransmitCapabilities.FrameSizeGranularity = 1;
    Wdi->MaxNumPeers = NDIS6_WDI_MAX_PEERS;
    if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_DATAPATH_ATTRIBUTES, &Container) &&
        Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_DATAPATH_CAPABILITIES, &Tlv))
    {
        RtlZeroMemory(Caps, sizeof(Caps));
        RtlCopyMemory(Caps, Tlv.Value, min(Tlv.Length, sizeof(Caps)));
        Wdi->TxRxCaps.InterconnectType = (WDI_INTERCONNECT_TYPE)Ndis6WdiRead32(Caps);
        Wdi->MaxNumPeers = Caps[4];
        Wdi->TxRxCaps.TransmitCapabilities.TargetPriorityQueueing = Caps[5] != 0;
        Wdi->TxRxCaps.TransmitCapabilities.MaxScatterGatherElementsPerFrame = (UINT16)Ndis6WdiRead16(Caps + 6);
        Wdi->TxRxCaps.TransmitCapabilities.ExplicitSendCompleteFlagRequired = Caps[8] != 0;
        Wdi->TxRxCaps.TransmitCapabilities.MinEffectiveSize = (UINT16)Ndis6WdiRead16(Caps + 9);
        Wdi->TxRxCaps.TransmitCapabilities.FrameSizeGranularity = (UINT16)Ndis6WdiRead16(Caps + 11);
        Wdi->TxRxCaps.ReceiveCapabilities.RxTxForwarding = Caps[13] != 0;
        Wdi->TxRxCaps.ReceiveCapabilities.MaxThroughput = Ndis6WdiRead32(Caps + 14);
    }

    Wdi->MaxNumPorts = 1;
    if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_VIRTUALIZATION_ATTRIBUTES, &Container) &&
        Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_VIRTUALIZATION_CAPABILITIES, &Tlv) &&
        Tlv.Length >= 3)
    {
        ULONG Ports = (ULONG)Tlv.Value[0] + Tlv.Value[1] + Tlv.Value[2];

        Wdi->MaxNumPorts = (UCHAR)(Ports == 0 ? 1 : min(Ports, 255));
    }

    Cursor = Tlvs;
    while (Ndis6WdiTlvNext(&Cursor, Tlvs + Length, &Container))
    {
        PNDIS6_WDI_BAND Band;

        if (Container.Type == WDI_TLV_PHY_INFO)
        {
            if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_PHY_CAPABILITIES, &Tlv) && Tlv.Length >= 4)
                Ndis6WdiAddPhyType(Wdi, Ndis6WdiRead32(Tlv.Value));
            continue;
        }

        if (Container.Type != WDI_TLV_BAND_INFO || Wdi->BandCount >= NDIS6_WDI_MAX_BANDS)
            continue;

        if (!Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_BAND_CAPABILITIES, &Tlv) || Tlv.Length < 4)
            continue;

        Band = &Wdi->Bands[Wdi->BandCount++];
        Band->BandId = Ndis6WdiRead32(Tlv.Value);
        Band->Enabled = Tlv.Length < 5 || Tlv.Value[4] != 0;

        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_CHANNEL_LIST, &Tlv))
        {
            for (Offset = 0; Offset + 8 <= Tlv.Length && Band->ChannelCount < NDIS6_WDI_MAX_BAND_CHANNELS; Offset += 8)
                Band->Channels[Band->ChannelCount++] = Ndis6WdiRead32(Tlv.Value + Offset);
        }

        if (Ndis6WdiTlvFind(Container.Value, Container.Length, WDI_TLV_PHY_TYPE_LIST, &Tlv))
        {
            for (Offset = 0; Offset + 4 <= Tlv.Length; Offset += 4)
                Ndis6WdiAddPhyType(Wdi, Ndis6WdiRead32(Tlv.Value + Offset));
        }

    }
    if (Wdi->PhyTypeCount == 0)
        Ndis6WdiAddPhyType(Wdi, dot11_phy_type_erp);

    if (Wdi->Mtu == 0)
        Wdi->Mtu = 1500;
    RtlCopyMemory(Wdi->CurrentAddress, Wdi->PermanentAddress, sizeof(Wdi->CurrentAddress));
    Wdi->Dot11.CurrentPhyType = Wdi->PhyTypes[0];

    DbgPrint("WDI: capabilities mtu %lu tx %lu kbps queueing %s peers %u pairs %lu phys %lu\n",
             Wdi->Mtu, Wdi->MaxTxRateKbps,
             Wdi->TxRxCaps.TransmitCapabilities.TargetPriorityQueueing ? "port" : "peer-tid",
             Wdi->MaxNumPeers, Wdi->UnicastPairCount, Wdi->PhyTypeCount);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6WdiDot11SetGeneralAttributes(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = sizeof(General);
    General.MediaType = NdisMediumNative802_11;
    General.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    General.MtuSize = Wdi->Mtu;
    General.MaxXmitLinkSpeed = (ULONG64)Wdi->MaxTxRateKbps * 1000;
    General.MaxRcvLinkSpeed = (ULONG64)Wdi->MaxRxRateKbps * 1000;
    General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateFull;
    General.LookaheadSize = DOT11_MAX_PDU_SIZE;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST |
                                     NDIS_PACKET_TYPE_PROMISCUOUS;
    General.MaxMulticastListSize = Wdi->MaxMulticastListSize != 0 ? Wdi->MaxMulticastListSize : NDIS6_WDI_MAX_MULTICAST;
    General.MacAddressLength = sizeof(DOT11_MAC_ADDRESS);
    RtlCopyMemory(General.PermanentMacAddress, Wdi->PermanentAddress, sizeof(DOT11_MAC_ADDRESS));
    RtlCopyMemory(General.CurrentMacAddress, Wdi->CurrentAddress, sizeof(DOT11_MAC_ADDRESS));
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_IEEE80211;
    General.IfConnectorPresent = TRUE;
    General.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                  NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                  NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_ERROR_SUPPORTED;
    General.SupportedOidList = Ndis6WdiSupportedOids;
    General.SupportedOidListLength = sizeof(Ndis6WdiSupportedOids);

    return NdisMSetMiniportAttributes(Wdi->Adapter, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
NDIS_STATUS
Ndis6WdiQueryBuffer(
    _In_ PNDIS_OID_REQUEST Request,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, Data, Length);
    Request->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
Ndis6WdiQueryUlong(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Value)
{
    return Ndis6WdiQueryBuffer(Request, &Value, sizeof(Value));
}

static
NDIS_STATUS
Ndis6WdiQueryBoolean(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ BOOLEAN Value)
{
    return Ndis6WdiQueryBuffer(Request, &Value, sizeof(Value));
}

static
NDIS_STATUS
Ndis6WdiQueryCounter(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG64 Value)
{
    ULONG Length = Request->DATA.QUERY_INFORMATION.InformationBufferLength;

    if (Length >= sizeof(ULONG64))
        return Ndis6WdiQueryBuffer(Request, &Value, sizeof(ULONG64));
    if (Length >= sizeof(ULONG))
        return Ndis6WdiQueryUlong(Request, (ULONG)Value);

    Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG64);
    return NDIS_STATUS_BUFFER_TOO_SHORT;
}

static
NDIS_STATUS
Ndis6WdiQueryAlgorithmList(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Count,
    _In_reads_(Count) const ULONG *Ids,
    _In_ UCHAR Revision)
{
    UCHAR Buffer[FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) + NDIS6_WDI_MAX_LIST * sizeof(ULONG)];
    PDOT11_AUTH_ALGORITHM_LIST List = (PDOT11_AUTH_ALGORITHM_LIST)Buffer;

    C_ASSERT(FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) == FIELD_OFFSET(DOT11_CIPHER_ALGORITHM_LIST, AlgorithmIds));

    RtlZeroMemory(Buffer, sizeof(Buffer));
    List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List->Header.Revision = Revision;
    List->Header.Size = sizeof(DOT11_AUTH_ALGORITHM_LIST);
    List->uNumOfEntries = Count;
    List->uTotalNumOfEntries = Count;
    RtlCopyMemory(List->AlgorithmIds, Ids, Count * sizeof(ULONG));
    return Ndis6WdiQueryBuffer(Request, Buffer, FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) + Count * sizeof(ULONG));
}

static
NDIS_STATUS
Ndis6WdiQueryPairList(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Count,
    _In_reads_(Count) const DOT11_AUTH_CIPHER_PAIR *Pairs)
{
    PDOT11_AUTH_CIPHER_PAIR_LIST List;
    ULONG Size = FIELD_OFFSET(DOT11_AUTH_CIPHER_PAIR_LIST, AuthCipherPairs) + Count * sizeof(DOT11_AUTH_CIPHER_PAIR);
    NDIS_STATUS Status;

    List = ExAllocatePoolWithTag(NonPagedPool, Size, NDIS6_WDI_TAG);
    if (List == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(List, Size);
    List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List->Header.Revision = DOT11_AUTH_CIPHER_PAIR_LIST_REVISION_1;
    List->Header.Size = sizeof(DOT11_AUTH_CIPHER_PAIR_LIST);
    List->uNumOfEntries = Count;
    List->uTotalNumOfEntries = Count;
    if (Count != 0)
        RtlCopyMemory(List->AuthCipherPairs, Pairs, Count * sizeof(DOT11_AUTH_CIPHER_PAIR));
    Status = Ndis6WdiQueryBuffer(Request, List, Size);
    ExFreePoolWithTag(List, NDIS6_WDI_TAG);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiQueryStatistics(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_STATISTICS_INFO Statistics;

    RtlZeroMemory(&Statistics, sizeof(Statistics));
    Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Statistics.Header.Size = sizeof(Statistics);
    Statistics.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                                     NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                                     NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                     NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
    Statistics.ifHCInOctets = Wdi->RxBytes;
    Statistics.ifHCInUcastPkts = Wdi->RxOk;
    Statistics.ifInErrors = Wdi->RxError;
    Statistics.ifHCOutOctets = Wdi->TxBytes;
    Statistics.ifHCOutUcastPkts = Wdi->TxOk;
    Statistics.ifOutErrors = Wdi->TxError;
    return Ndis6WdiQueryBuffer(Request, &Statistics, sizeof(Statistics));
}

static
NDIS_STATUS
Ndis6WdiQuery(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    PNDIS6_WDI_DOT11 Dot11 = &Wdi->Dot11;
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return Ndis6WdiQueryBuffer(Request, Ndis6WdiSupportedOids, sizeof(Ndis6WdiSupportedOids));
        case OID_GEN_HARDWARE_STATUS:
            return Ndis6WdiQueryUlong(Request, NdisHardwareStatusReady);
        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return Ndis6WdiQueryUlong(Request, NdisMediumNative802_11);
        case OID_GEN_PHYSICAL_MEDIUM:
            return Ndis6WdiQueryUlong(Request, NdisPhysicalMediumNative802_11);
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return Ndis6WdiQueryUlong(Request, Wdi->Mtu);
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_DOT11_MPDU_MAX_LENGTH:
            return Ndis6WdiQueryUlong(Request, DOT11_MAX_PDU_SIZE);
        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
            return Ndis6WdiQueryUlong(Request, DOT11_MAX_PDU_SIZE * NDIS6_WDI_TX_QUEUE_LIMIT);
        case OID_GEN_VENDOR_ID:
            return Ndis6WdiQueryUlong(Request, ((ULONG)Wdi->PermanentAddress[0] << 16) |
                                               ((ULONG)Wdi->PermanentAddress[1] << 8) |
                                               Wdi->PermanentAddress[2]);
        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "Wireless LAN Adapter";
            return Ndis6WdiQueryBuffer(Request, Description, sizeof(Description));
        }
        case OID_GEN_VENDOR_DRIVER_VERSION:
            return Ndis6WdiQueryUlong(Request, ((ULONG)Wdi->Ihv->MajorDriverVersion << 16) | Wdi->Ihv->MinorDriverVersion);
        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = (USHORT)((Wdi->Ihv->MajorNdisVersion << 8) | Wdi->Ihv->MinorNdisVersion);
            return Ndis6WdiQueryBuffer(Request, &Version, sizeof(Version));
        }
        case OID_GEN_CURRENT_PACKET_FILTER:
        case OID_DOT11_CURRENT_PACKET_FILTER:
            return Ndis6WdiQueryUlong(Request, Dot11->PacketFilter);
        case OID_GEN_LINK_SPEED:
            return Ndis6WdiQueryUlong(Request, Dot11->Associated ? (ULONG)(Dot11->TxLinkSpeed / 100) : 0);
        case OID_GEN_MEDIA_CONNECT_STATUS:
            return Ndis6WdiQueryUlong(Request, Dot11->Associated ? NdisMediaStateConnected : NdisMediaStateDisconnected);
        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return Ndis6WdiQueryUlong(Request, 1);
        case OID_GEN_MAC_OPTIONS:
            return Ndis6WdiQueryUlong(Request, NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                               NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                               NDIS_MAC_OPTION_NO_LOOPBACK);
        case OID_GEN_XMIT_OK:
            return Ndis6WdiQueryCounter(Request, Wdi->TxOk);
        case OID_GEN_RCV_OK:
            return Ndis6WdiQueryCounter(Request, Wdi->RxOk);
        case OID_GEN_XMIT_ERROR:
            return Ndis6WdiQueryCounter(Request, Wdi->TxError);
        case OID_GEN_RCV_ERROR:
            return Ndis6WdiQueryCounter(Request, Wdi->RxError);
        case OID_GEN_RCV_NO_BUFFER:
            return Ndis6WdiQueryCounter(Request, 0);
        case OID_GEN_STATISTICS:
            return Ndis6WdiQueryStatistics(Wdi, Request);
        case OID_DOT11_OPERATION_MODE_CAPABILITY:
        {
            DOT11_OPERATION_MODE_CAPABILITY Capability;

            RtlZeroMemory(&Capability, sizeof(Capability));
            Capability.uMajorVersion = 2;
            Capability.uNumOfTXBuffers = NDIS6_WDI_TX_QUEUE_LIMIT;
            Capability.uNumOfRXBuffers = NDIS6_WDI_TX_QUEUE_LIMIT;
            Capability.uOpModeCapability = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            return Ndis6WdiQueryBuffer(Request, &Capability, sizeof(Capability));
        }
        case OID_DOT11_CURRENT_OPERATION_MODE:
        {
            DOT11_CURRENT_OPERATION_MODE Mode;

            RtlZeroMemory(&Mode, sizeof(Mode));
            Mode.uCurrentOpMode = Dot11->OperationMode;
            return Ndis6WdiQueryBuffer(Request, &Mode, sizeof(Mode));
        }
        case OID_DOT11_CURRENT_PHY_TYPE:
            return Ndis6WdiQueryUlong(Request, (ULONG)Dot11->CurrentPhyType);
        case OID_DOT11_SUPPORTED_PHY_TYPES:
        {
            UCHAR Buffer[FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + NDIS6_WDI_MAX_PHY_TYPES * sizeof(DOT11_PHY_TYPE)];
            PDOT11_PHY_TYPE_LIST List = (PDOT11_PHY_TYPE_LIST)Buffer;

            RtlZeroMemory(Buffer, sizeof(Buffer));
            List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List->Header.Revision = DOT11_PHY_TYPE_LIST_REVISION_1;
            List->Header.Size = sizeof(DOT11_PHY_TYPE_LIST);
            List->uNumOfEntries = Wdi->PhyTypeCount;
            List->uTotalNumOfEntries = Wdi->PhyTypeCount;
            RtlCopyMemory(List->dot11PhyType, Wdi->PhyTypes, Wdi->PhyTypeCount * sizeof(DOT11_PHY_TYPE));
            return Ndis6WdiQueryBuffer(Request, Buffer, FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + Wdi->PhyTypeCount * sizeof(DOT11_PHY_TYPE));
        }
        case OID_DOT11_CURRENT_PHY_ID:
            return Ndis6WdiQueryUlong(Request, 0);
        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
            return Ndis6WdiQueryUlong(Request, Dot11->CurrentChannel);
        case OID_DOT11_RTS_THRESHOLD:
            return Ndis6WdiQueryUlong(Request, Dot11->RtsThreshold);
        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            return Ndis6WdiQueryUlong(Request, Dot11->FragmentationThreshold);
        case OID_DOT11_BEACON_PERIOD:
            return Ndis6WdiQueryUlong(Request, NDIS6_WDI_BEACON_PERIOD);
        case OID_DOT11_NIC_POWER_STATE:
            return Ndis6WdiQueryBoolean(Request, Wdi->SoftwareRadioOn);
        case OID_DOT11_HARDWARE_PHY_STATE:
            return Ndis6WdiQueryBoolean(Request, Wdi->HardwareRadioOn);
        case OID_DOT11_AUTO_CONFIG_ENABLED:
            return Ndis6WdiQueryUlong(Request, Dot11->AutoConfig ? Dot11->AutoConfigFlags : 0);
        case OID_DOT11_MAC_ADDRESS:
        case OID_DOT11_CURRENT_ADDRESS:
            return Ndis6WdiQueryBuffer(Request, Wdi->CurrentAddress, sizeof(DOT11_MAC_ADDRESS));
        case OID_DOT11_PERMANENT_ADDRESS:
            return Ndis6WdiQueryBuffer(Request, Wdi->PermanentAddress, sizeof(DOT11_MAC_ADDRESS));
        case OID_DOT11_MULTICAST_LIST:
            return Ndis6WdiQueryBuffer(Request, Dot11->MulticastList, Dot11->MulticastCount * sizeof(DOT11_MAC_ADDRESS));
        case OID_DOT11_DESIRED_BSS_TYPE:
            return Ndis6WdiQueryUlong(Request, (ULONG)Dot11->DesiredBssType);
        case OID_DOT11_EXCLUDE_UNENCRYPTED:
            return Ndis6WdiQueryBoolean(Request, Dot11->ExcludeUnencrypted);
        case OID_DOT11_HIDDEN_NETWORK_ENABLED:
            return Ndis6WdiQueryBoolean(Request, Dot11->HiddenNetwork);
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            return Ndis6WdiQueryUlong(Request, Dot11->DefaultKeyId);
        case OID_DOT11_PRIVACY_EXEMPTION_LIST:
        {
            UCHAR Buffer[FIELD_OFFSET(DOT11_PRIVACY_EXEMPTION_LIST, PrivacyExemptionEntries) + NDIS6_WDI_MAX_LIST * sizeof(DOT11_PRIVACY_EXEMPTION)];
            PDOT11_PRIVACY_EXEMPTION_LIST List = (PDOT11_PRIVACY_EXEMPTION_LIST)Buffer;
            ULONG Size;
            KIRQL OldIrql;

            RtlZeroMemory(Buffer, sizeof(Buffer));
            List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List->Header.Revision = DOT11_PRIVACY_EXEMPTION_LIST_REVISION_1;
            List->Header.Size = sizeof(DOT11_PRIVACY_EXEMPTION_LIST);
            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            List->uNumOfEntries = Dot11->ExemptionCount;
            List->uTotalNumOfEntries = Dot11->ExemptionCount;
            RtlCopyMemory(List->PrivacyExemptionEntries, Dot11->Exemptions, Dot11->ExemptionCount * sizeof(DOT11_PRIVACY_EXEMPTION));
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
            Size = FIELD_OFFSET(DOT11_PRIVACY_EXEMPTION_LIST, PrivacyExemptionEntries) + List->uNumOfEntries * sizeof(DOT11_PRIVACY_EXEMPTION);
            if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Size)
            {
                Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
                Request->DATA.QUERY_INFORMATION.BytesNeeded = Size;
                return NDIS_STATUS_BUFFER_OVERFLOW;
            }
            return Ndis6WdiQueryBuffer(Request, Buffer, Size);
        }
        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return Ndis6WdiQueryAlgorithmList(Request, Dot11->AuthCount, (const ULONG *)Dot11->Auth, DOT11_AUTH_ALGORITHM_LIST_REVISION_1);
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return Ndis6WdiQueryAlgorithmList(Request, Dot11->UnicastCount, (const ULONG *)Dot11->Unicast, DOT11_CIPHER_ALGORITHM_LIST_REVISION_1);
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return Ndis6WdiQueryAlgorithmList(Request, Dot11->MulticastCipherCount, (const ULONG *)Dot11->MulticastCipher, DOT11_CIPHER_ALGORITHM_LIST_REVISION_1);
        case OID_DOT11_SUPPORTED_UNICAST_ALGORITHM_PAIR:
            if (Wdi->UnicastPairCount == 0)
                return NDIS_STATUS_NOT_SUPPORTED;
            return Ndis6WdiQueryPairList(Request, Wdi->UnicastPairCount, Wdi->UnicastPairs);
        case OID_DOT11_SUPPORTED_MULTICAST_ALGORITHM_PAIR:
            if (Wdi->MulticastPairCount == 0)
                return NDIS_STATUS_NOT_SUPPORTED;
            return Ndis6WdiQueryPairList(Request, Wdi->MulticastPairCount, Wdi->MulticastPairs);
        case OID_DOT11_DESIRED_SSID_LIST:
        {
            DOT11_SSID_LIST List;

            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_SSID_LIST_REVISION_1;
            List.Header.Size = sizeof(List);
            List.uNumOfEntries = Dot11->HaveDesiredSsid ? 1 : 0;
            List.uTotalNumOfEntries = List.uNumOfEntries;
            List.SSIDs[0] = Dot11->DesiredSsid;
            return Ndis6WdiQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written;
            ULONG Needed;
            NDIS_STATUS Status;

            Status = Ndis6WdiBuildBssList(Wdi,
                                          Request->DATA.QUERY_INFORMATION.InformationBuffer,
                                          Request->DATA.QUERY_INFORMATION.InformationBufferLength,
                                          &Written,
                                          &Needed);
            Request->DATA.QUERY_INFORMATION.BytesWritten = Written;
            Request->DATA.QUERY_INFORMATION.BytesNeeded = Needed;
            return Status;
        }
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
Ndis6WdiSetAlgorithms(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request,
    _Out_writes_(NDIS6_WDI_MAX_LIST) PULONG Ids,
    _Out_ PULONG Count)
{
    PDOT11_AUTH_ALGORITHM_LIST List = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    ULONG Entries;
    KIRQL OldIrql;

    if (Length < FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) + sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Entries = List->uNumOfEntries;
    if (Entries > (Length - FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds)) / sizeof(ULONG))
        return NDIS_STATUS_INVALID_DATA;
    if (Entries > NDIS6_WDI_MAX_LIST)
        Entries = NDIS6_WDI_MAX_LIST;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    RtlCopyMemory(Ids, List->AlgorithmIds, Entries * sizeof(ULONG));
    *Count = Entries;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    Request->DATA.SET_INFORMATION.BytesRead = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
Ndis6WdiSet(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request,
    _Out_ PBOOLEAN Queue)
{
    PNDIS6_WDI_DOT11 Dot11 = &Wdi->Dot11;
    NDIS_OID Oid = Request->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    KIRQL OldIrql;

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
        case OID_DOT11_CONNECT_REQUEST:
        case OID_DOT11_DISCONNECT_REQUEST:
        case OID_DOT11_RESET_REQUEST:
        case OID_DOT11_NIC_POWER_STATE:
            *Queue = TRUE;
            return NDIS_STATUS_PENDING;

        case OID_GEN_CURRENT_PACKET_FILTER:
        case OID_DOT11_CURRENT_PACKET_FILTER:
        case OID_DOT11_MULTICAST_LIST:
        case OID_802_3_MULTICAST_LIST:
        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
        case OID_DOT11_CIPHER_DEFAULT_KEY:
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
        case OID_DOT11_PRIVACY_EXEMPTION_LIST:
            *Queue = TRUE;
            return NDIS_STATUS_PENDING;

        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length < sizeof(DOT11_CURRENT_OPERATION_MODE))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(DOT11_CURRENT_OPERATION_MODE);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode != DOT11_OPERATION_MODE_EXTENSIBLE_STATION)
                return NDIS_STATUS_NOT_SUPPORTED;
            Dot11->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(DOT11_CURRENT_OPERATION_MODE);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CURRENT_PHY_TYPE:
        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
        case OID_DOT11_RTS_THRESHOLD:
        case OID_DOT11_FRAGMENTATION_THRESHOLD:
        case OID_DOT11_AUTO_CONFIG_ENABLED:
        case OID_DOT11_DESIRED_BSS_TYPE:
        {
            ULONG Value;

            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Value = *(PULONG)Buffer;
            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            if (Oid == OID_DOT11_CURRENT_PHY_TYPE)
                Dot11->CurrentPhyType = (DOT11_PHY_TYPE)Value;
            else if (Oid == OID_DOT11_CURRENT_CHANNEL_NUMBER)
                Dot11->CurrentChannel = Value;
            else if (Oid == OID_DOT11_RTS_THRESHOLD)
                Dot11->RtsThreshold = Value;
            else if (Oid == OID_DOT11_FRAGMENTATION_THRESHOLD)
                Dot11->FragmentationThreshold = Value;
            else if (Oid == OID_DOT11_AUTO_CONFIG_ENABLED)
            {
                Dot11->AutoConfig = Value != 0;
                Dot11->AutoConfigFlags = Value;
            }
            else
                Dot11->DesiredBssType = (DOT11_BSS_TYPE)Value;
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_EXCLUDE_UNENCRYPTED:
        case OID_DOT11_HIDDEN_NETWORK_ENABLED:
            if (Length < sizeof(BOOLEAN))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(BOOLEAN);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (Oid == OID_DOT11_EXCLUDE_UNENCRYPTED)
                Dot11->ExcludeUnencrypted = *(PBOOLEAN)Buffer != FALSE;
            else
                Dot11->HiddenNetwork = *(PBOOLEAN)Buffer != FALSE;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(BOOLEAN);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_DESIRED_SSID_LIST:
        {
            PDOT11_SSID_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_SSID_LIST, SSIDs))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(DOT11_SSID_LIST);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            if (List->uNumOfEntries >= 1 &&
                Length >= FIELD_OFFSET(DOT11_SSID_LIST, SSIDs) + sizeof(DOT11_SSID) &&
                List->SSIDs[0].uSSIDLength <= DOT11_SSID_MAX_LENGTH)
            {
                Dot11->DesiredSsid = List->SSIDs[0];
                Dot11->HaveDesiredSsid = List->SSIDs[0].uSSIDLength != 0;
            }
            else
            {
                RtlZeroMemory(&Dot11->DesiredSsid, sizeof(Dot11->DesiredSsid));
                Dot11->HaveDesiredSsid = FALSE;
            }
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_DESIRED_BSSID_LIST:
        {
            PDOT11_BSSID_LIST List = Buffer;
            ULONG Entries;
            ULONG i;

            if (Length < FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(DOT11_BSSID_LIST);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Entries = List->uNumOfEntries;
            if (Entries > (Length - FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs)) / sizeof(DOT11_MAC_ADDRESS))
                return NDIS_STATUS_INVALID_DATA;

            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            Dot11->DesiredBssidCount = 0;
            for (i = 0; i < Entries && Dot11->DesiredBssidCount < NDIS6_WDI_MAX_LIST; i++)
            {
                static const DOT11_MAC_ADDRESS Broadcast = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

                if (RtlEqualMemory(List->BSSIDs[i], Broadcast, sizeof(DOT11_MAC_ADDRESS)))
                {
                    Dot11->DesiredBssidCount = 0;
                    break;
                }
                RtlCopyMemory(Dot11->DesiredBssids[Dot11->DesiredBssidCount++], List->BSSIDs[i], sizeof(DOT11_MAC_ADDRESS));
            }
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return Ndis6WdiSetAlgorithms(Wdi, Request, (PULONG)Dot11->Auth, &Dot11->AuthCount);
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return Ndis6WdiSetAlgorithms(Wdi, Request, (PULONG)Dot11->Unicast, &Dot11->UnicastCount);
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return Ndis6WdiSetAlgorithms(Wdi, Request, (PULONG)Dot11->MulticastCipher, &Dot11->MulticastCipherCount);

        case OID_DOT11_FLUSH_BSS_LIST:
            Ndis6WdiFlushBssList(Wdi);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_POWER_MGMT_REQUEST:
        case OID_DOT11_MEDIA_STREAMING_ENABLED:
        {
            BOOLEAN Changed;
            BOOLEAN Associated;

            if (Length < (Oid == OID_DOT11_POWER_MGMT_REQUEST ? sizeof(ULONG) : sizeof(BOOLEAN)))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = Oid == OID_DOT11_POWER_MGMT_REQUEST ? sizeof(ULONG) : sizeof(BOOLEAN);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            if (Oid == OID_DOT11_POWER_MGMT_REQUEST)
            {
                Changed = Dot11->PowerSavingLevel != *(PULONG)Buffer;
                Dot11->PowerSavingLevel = *(PULONG)Buffer;
            }
            else
            {
                Changed = Dot11->MediaStreaming != (*(PBOOLEAN)Buffer != FALSE);
                Dot11->MediaStreaming = *(PBOOLEAN)Buffer != FALSE;
            }
            Associated = Dot11->Associated;
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

            if (Changed && Associated)
                Ndis6WdiQueueInternalJob(Wdi, Ndis6WdiJobConnectionQuality);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_OPERATIONAL_RATE_SET:
        case OID_DOT11_BEACON_PERIOD:
        case OID_DOT11_CURRENT_PHY_ID:
        case OID_DOT11_UNICAST_USE_GROUP_ENABLED:
        case OID_DOT11_SAFE_MODE_ENABLED:
        case OID_DOT11_PMKID_LIST:
        case OID_DOT11_DESIRED_PHY_LIST:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_NETWORK_LAYER_ADDRESSES:
        case OID_PNP_SET_POWER:
        case OID_PNP_ADD_WAKE_UP_PATTERN:
        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
Ndis6WdiMethod(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request,
    _Out_ PBOOLEAN Queue)
{
    NDIS_OID Oid = Request->DATA.METHOD_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
        case OID_DOT11_CONNECT_REQUEST:
        case OID_DOT11_DISCONNECT_REQUEST:
        case OID_DOT11_RESET_REQUEST:
            *Queue = TRUE;
            return NDIS_STATUS_PENDING;

        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written;
            ULONG Needed;
            NDIS_STATUS Status;

            Status = Ndis6WdiBuildBssList(Wdi,
                                          Request->DATA.METHOD_INFORMATION.InformationBuffer,
                                          Request->DATA.METHOD_INFORMATION.OutputBufferLength,
                                          &Written,
                                          &Needed);
            Request->DATA.METHOD_INFORMATION.BytesWritten = Written;
            Request->DATA.METHOD_INFORMATION.BytesNeeded = Needed;
            Request->DATA.METHOD_INFORMATION.BytesRead = Request->DATA.METHOD_INFORMATION.InputBufferLength;
            return Status;
        }

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
Ndis6WdiDot11OidRequest(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request,
    _Out_ PBOOLEAN Queue)
{
    *Queue = FALSE;

    switch (Request->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Ndis6WdiQuery(Wdi, Request);
        case NdisRequestSetInformation:
            return Ndis6WdiSet(Wdi, Request, Queue);
        case NdisRequestMethod:
            return Ndis6WdiMethod(Wdi, Request, Queue);
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
PVOID
Ndis6WdiInput(
    _In_ PNDIS_OID_REQUEST Request,
    _Out_ PULONG Length)
{
    if (Request->RequestType == NdisRequestMethod)
    {
        *Length = Request->DATA.METHOD_INFORMATION.InputBufferLength;
        return Request->DATA.METHOD_INFORMATION.InformationBuffer;
    }

    *Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    return Request->DATA.SET_INFORMATION.InformationBuffer;
}

static
VOID
Ndis6WdiConsumed(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Length)
{
    if (Request->RequestType == NdisRequestMethod)
        Request->DATA.METHOD_INFORMATION.BytesRead = Length;
    else
        Request->DATA.SET_INFORMATION.BytesRead = Length;
}

static
VOID
Ndis6WdiScanComplete(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(Length) PUCHAR Tlvs,
    _In_ ULONG Length,
    _In_opt_ PVOID Context)
{
    DOT11_STATUS_INDICATION Confirm;

    UNREFERENCED_PARAMETER(Tlvs);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Context);

    RtlZeroMemory(&Confirm, sizeof(Confirm));
    Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
    Confirm.ndisStatus = Status;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_SCAN_CONFIRM, &Confirm, sizeof(Confirm));
}

static
VOID
Ndis6WdiBuildScanTlvs(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_reads_bytes_(6) const UCHAR *Bssid,
    _In_ ULONG SsidCount,
    _In_reads_(SsidCount) const DOT11_SSID *Ssids,
    _In_ ULONG ScanType)
{
    ULONG Offset;
    ULONG i;

    Ndis6WdiTlvAdd(Tlvs, WDI_TLV_BSSID, Bssid, sizeof(DOT11_MAC_ADDRESS));
    if (SsidCount == 0)
        Ndis6WdiTlvAdd(Tlvs, WDI_TLV_SSID, NULL, 0);
    for (i = 0; i < SsidCount; i++)
        Ndis6WdiTlvAdd(Tlvs, WDI_TLV_SSID, Ssids[i].ucSSID, Ssids[i].uSSIDLength);

    Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_SCAN_MODE);
    Ndis6WdiTlvAppend8(Tlvs, 1);
    Ndis6WdiTlvAppend32(Tlvs, ScanType);
    Ndis6WdiTlvAppend8(Tlvs, TRUE);
    Ndis6WdiTlvAppend32(Tlvs, WDI_SCAN_TRIGGER_MANUAL);
    Ndis6WdiTlvClose(Tlvs, Offset);

    Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_SCAN_DWELL_TIME);
    Ndis6WdiTlvAppend32(Tlvs, 30);
    Ndis6WdiTlvAppend32(Tlvs, 110);
    Ndis6WdiTlvAppend32(Tlvs, 10000);
    Ndis6WdiTlvClose(Tlvs, Offset);

    for (i = 0; i < Wdi->BandCount; i++)
    {
        PNDIS6_WDI_BAND Band = &Wdi->Bands[i];
        ULONG Inner;
        ULONG Channel;

        if (!Band->Enabled || Band->ChannelCount == 0)
            continue;

        Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_BAND_CHANNEL);
        Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_BANDID);
        Ndis6WdiTlvAppend32(Tlvs, Band->BandId);
        Ndis6WdiTlvClose(Tlvs, Inner);
        Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CHANNEL_INFO_LIST);
        for (Channel = 0; Channel < Band->ChannelCount; Channel++)
            Ndis6WdiTlvAppend32(Tlvs, Band->Channels[Channel]);
        Ndis6WdiTlvClose(Tlvs, Inner);
        Ndis6WdiTlvClose(Tlvs, Offset);
    }
}

static
NDIS_STATUS
Ndis6WdiJobScan(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    DOT11_SSID Ssids[NDIS6_WDI_MAX_LIST];
    PDOT11_SCAN_REQUEST_V2 Scan;
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG SsidCount = 0;
    ULONG ScanType;
    ULONG Length;
    ULONG Offset;
    ULONG i;
    NDIS_STATUS Status;

    Scan = Ndis6WdiInput(Request, &Length);
    if (Scan == NULL || Length < FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer))
        return NDIS_STATUS_INVALID_LENGTH;

    for (i = 0; i < Scan->uNumOfdot11SSIDs && SsidCount < NDIS6_WDI_MAX_LIST; i++)
    {
        Offset = FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) + Scan->udot11SSIDsOffset + i * sizeof(DOT11_SSID);
        if (Offset < Scan->udot11SSIDsOffset || Offset > Length || Length - Offset < sizeof(DOT11_SSID))
            break;
        RtlCopyMemory(&Ssids[SsidCount], (PUCHAR)Scan + Offset, sizeof(DOT11_SSID));
        if (Ssids[SsidCount].uSSIDLength > DOT11_SSID_MAX_LENGTH)
            continue;
        SsidCount++;
    }

    ScanType = (ULONG)Scan->dot11ScanType & ~(ULONG)dot11_scan_type_forced;
    if (ScanType < WDI_SCAN_TYPE_ACTIVE_ONLY || ScanType > WDI_SCAN_TYPE_AUTO)
        ScanType = WDI_SCAN_TYPE_AUTO;

    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiBuildScanTlvs(Wdi, &Tlvs, Scan->dot11BSSID, SsidCount, Ssids, ScanType);
    Status = Ndis6WdiTask(Wdi, WDI_TASK_SCAN, Wdi->PortId, &Tlvs, FALSE, Ndis6WdiScanComplete, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    Ndis6WdiConsumed(Request, Length);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiSendPrivacyExemptions(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    DOT11_PRIVACY_EXEMPTION Exemptions[NDIS6_WDI_MAX_LIST];
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Count;
    ULONG Offset;
    ULONG i;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Count = Wdi->Dot11.ExemptionCount;
    RtlCopyMemory(Exemptions, Wdi->Dot11.Exemptions, Count * sizeof(DOT11_PRIVACY_EXEMPTION));
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    Ndis6WdiTlvInit(&Tlvs);
    for (i = 0; i < Count; i++)
    {
        Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_PRIVACY_EXEMPTION_ENTRY);
        Ndis6WdiTlvAppend16(&Tlvs, Exemptions[i].usEtherType);
        Ndis6WdiTlvAppend32(&Tlvs, Exemptions[i].usExemptionActionType);
        Ndis6WdiTlvAppend32(&Tlvs, Exemptions[i].usExemptionPacketType);
        Ndis6WdiTlvClose(&Tlvs, Offset);
    }
    Status = Ndis6WdiProperty(Wdi, WDI_SET_PRIVACY_EXEMPTION_LIST, Wdi->PortId, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobPrivacyExemption(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    PDOT11_PRIVACY_EXEMPTION_LIST List;
    ULONG Length;
    ULONG Count;
    KIRQL OldIrql;

    List = Ndis6WdiInput(Request, &Length);
    if (List == NULL || Length < FIELD_OFFSET(DOT11_PRIVACY_EXEMPTION_LIST, PrivacyExemptionEntries))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(DOT11_PRIVACY_EXEMPTION_LIST);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Count = List->uNumOfEntries;
    if (Count > NDIS6_WDI_MAX_LIST ||
        Length < FIELD_OFFSET(DOT11_PRIVACY_EXEMPTION_LIST, PrivacyExemptionEntries) + Count * sizeof(DOT11_PRIVACY_EXEMPTION))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded =
            FIELD_OFFSET(DOT11_PRIVACY_EXEMPTION_LIST, PrivacyExemptionEntries) + Count * sizeof(DOT11_PRIVACY_EXEMPTION);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.ExemptionCount = Count;
    RtlCopyMemory(Wdi->Dot11.Exemptions, List->PrivacyExemptionEntries, Count * sizeof(DOT11_PRIVACY_EXEMPTION));
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    Ndis6WdiConsumed(Request, Length);
    return Ndis6WdiSendPrivacyExemptions(Wdi);
}

static
VOID
Ndis6WdiIndicateConnectionCompletion(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Completion;

    RtlZeroMemory(&Completion, sizeof(Completion));
    Completion.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Completion.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Completion.Header.Size = sizeof(Completion);
    Completion.uStatus = Status;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_CONNECTION_COMPLETION, &Completion, sizeof(Completion));
}

static
VOID
Ndis6WdiConnectComplete(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(Length) PUCHAR Tlvs,
    _In_ ULONG Length,
    _In_opt_ PVOID Context)
{
    BOOLEAN Associated;
    BOOLEAN Attempted;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Tlvs);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Context);

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.Connecting = FALSE;
    Associated = Wdi->Dot11.Associated;
    Attempted = Wdi->Dot11.AssociationIndicated;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (Status == NDIS_STATUS_SUCCESS && Associated)
    {
        Ndis6WdiQueueInternalJob(Wdi, Ndis6WdiJobConnectionQuality);
        Ndis6WdiIndicateConnectionCompletion(Wdi, DOT11_CONNECTION_STATUS_SUCCESS);
        Ndis6WdiIndicateLinkState(Wdi, TRUE);
        return;
    }

    Ndis6WdiIndicateConnectionCompletion(Wdi, Attempted ? DOT11_CONNECTION_STATUS_FAILURE
                                                        : DOT11_CONNECTION_STATUS_CANDIDATE_LIST_EXHAUSTED);
}

static
BOOLEAN
Ndis6WdiBssMatches(
    _In_ PNDIS6_WDI_DOT11 Dot11,
    _In_ PNDIS6_WDI_BSS Bss)
{
    ULONG i;

    if (Dot11->HaveDesiredSsid &&
        (Bss->SsidLength != Dot11->DesiredSsid.uSSIDLength ||
         !RtlEqualMemory(Bss->Ssid, Dot11->DesiredSsid.ucSSID, Bss->SsidLength)))
    {
        return FALSE;
    }

    if (Dot11->DesiredBssType == dot11_BSS_type_infrastructure && (Bss->Capability & 0x0002))
        return FALSE;

    if (Dot11->DesiredBssidCount == 0)
        return TRUE;

    for (i = 0; i < Dot11->DesiredBssidCount; i++)
    {
        if (RtlEqualMemory(Dot11->DesiredBssids[i], Bss->Bssid, sizeof(DOT11_MAC_ADDRESS)))
            return TRUE;
    }
    return FALSE;
}

static
ULONG
Ndis6WdiBuildCandidates(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _Out_ PDOT11_SSID Ssid,
    _In_reads_bytes_opt_(6) const UCHAR *Exclude,
    _In_reads_opt_(PreferredCount) const DOT11_MAC_ADDRESS *Preferred,
    _In_ ULONG PreferredCount,
    _Out_writes_bytes_opt_(6) PUCHAR First)
{
    PNDIS6_WDI_BSS Picked[NDIS6_WDI_MAX_CANDIDATES];
    ULONG Rank[NDIS6_WDI_MAX_CANDIDATES];
    ULONGLONG Now = KeQueryInterruptTime();
    ULONG Count = 0;
    ULONG Offset;
    PLIST_ENTRY Link;
    KIRQL OldIrql;
    ULONG i;
    ULONG j;
    ULONG k;

    RtlZeroMemory(Ssid, sizeof(*Ssid));

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
    {
        PNDIS6_WDI_BSS Bss = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);

        if (!Ndis6WdiBssFresh(Bss, Now) || !Ndis6WdiBssMatches(&Wdi->Dot11, Bss))
            continue;
        if (Exclude != NULL && RtlEqualMemory(Bss->Bssid, Exclude, sizeof(DOT11_MAC_ADDRESS)))
            continue;
        for (i = 0; i < PreferredCount; i++)
        {
            if (RtlEqualMemory(Bss->Bssid, Preferred[i], sizeof(DOT11_MAC_ADDRESS)))
                break;
        }
        if (PreferredCount != 0 && i == PreferredCount)
            continue;

        if (Count < NDIS6_WDI_MAX_CANDIDATES)
        {
            Rank[Count] = i;
            Picked[Count++] = Bss;
        }
        else
        {
            for (k = 0, j = 0; k < Count; k++)
            {
                if (Rank[k] > Rank[j] || (Rank[k] == Rank[j] && Picked[k]->Rssi < Picked[j]->Rssi))
                    j = k;
            }
            if (i < Rank[j] || (i == Rank[j] && Bss->Rssi > Picked[j]->Rssi))
            {
                Rank[j] = i;
                Picked[j] = Bss;
            }
        }
    }

    for (i = 1; i < Count; i++)
    {
        PNDIS6_WDI_BSS Key = Picked[i];
        ULONG KeyRank = Rank[i];

        for (j = i; j > 0 && (Rank[j - 1] > KeyRank || (Rank[j - 1] == KeyRank && Picked[j - 1]->Rssi < Key->Rssi)); j--)
        {
            Picked[j] = Picked[j - 1];
            Rank[j] = Rank[j - 1];
        }
        Picked[j] = Key;
        Rank[j] = KeyRank;
    }
    if (First != NULL && Count != 0)
        RtlCopyMemory(First, Picked[0]->Bssid, sizeof(DOT11_MAC_ADDRESS));

    if (Wdi->Dot11.HaveDesiredSsid)
    {
        *Ssid = Wdi->Dot11.DesiredSsid;
    }
    else if (Count != 0)
    {
        Ssid->uSSIDLength = Picked[0]->SsidLength;
        RtlCopyMemory(Ssid->ucSSID, Picked[0]->Ssid, Picked[0]->SsidLength);
    }

    for (i = 0; i < Count; i++)
    {
        PNDIS6_WDI_BSS Bss = Picked[i];

        Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CONNECT_BSS_ENTRY);
        Ndis6WdiTlvAdd(Tlvs, WDI_TLV_BSSID, Bss->Bssid, sizeof(DOT11_MAC_ADDRESS));
        if (Bss->Frame != NULL)
            Ndis6WdiTlvAdd(Tlvs, Bss->FrameTlv, Bss->Frame, Bss->FrameLength);
        {
            ULONG Signal = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_BSS_ENTRY_SIGNAL_INFO);
            Ndis6WdiTlvAppend32(Tlvs, (ULONG)Bss->Rssi);
            Ndis6WdiTlvAppend32(Tlvs, Bss->LinkQuality);
            Ndis6WdiTlvClose(Tlvs, Signal);
        }
        {
            ULONG Channel = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_BSS_ENTRY_CHANNEL_INFO);
            Ndis6WdiTlvAppend32(Tlvs, Bss->Channel);
            Ndis6WdiTlvAppend32(Tlvs, Bss->BandId);
            Ndis6WdiTlvClose(Tlvs, Channel);
        }
        if (Bss->DeviceContext != NULL)
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_BSS_ENTRY_DEVICE_CONTEXT, Bss->DeviceContext, Bss->DeviceContextLength);
        Ndis6WdiTlvClose(Tlvs, Offset);
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    return Count;
}

static
VOID
Ndis6WdiBuildConnectParameters(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ PDOT11_SSID Ssid,
    _In_ BOOLEAN Roam,
    _In_ ULONG RoamReason,
    _In_ ULONG RoamTrigger)
{
    PNDIS6_WDI_DOT11 Dot11 = &Wdi->Dot11;
    ULONG Outer;
    ULONG Inner;
    ULONG i;

    Outer = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CONNECT_PARAMETERS);

    Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CONNECTION_SETTINGS);
    Ndis6WdiTlvAppend8(Tlvs, Roam);
    Ndis6WdiTlvAppend8(Tlvs, Dot11->HiddenNetwork);
    Ndis6WdiTlvAppend8(Tlvs, Dot11->ExcludeUnencrypted);
    Ndis6WdiTlvAppend8(Tlvs, FALSE);
    Ndis6WdiTlvAppend8(Tlvs, FALSE);
    Ndis6WdiTlvAppend32(Tlvs, RoamReason);
    Ndis6WdiTlvAppend32(Tlvs, RoamTrigger);
    Ndis6WdiTlvAppend8(Tlvs, Wdi->BssTransitionSupported);
    Ndis6WdiTlvClose(Tlvs, Inner);

    Ndis6WdiTlvAdd(Tlvs, WDI_TLV_SSID, Ssid->ucSSID, Ssid->uSSIDLength);

    Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_AUTH_ALGO_LIST);
    for (i = 0; i < Dot11->AuthCount; i++)
        Ndis6WdiTlvAppend32(Tlvs, (ULONG)Dot11->Auth[i]);
    Ndis6WdiTlvClose(Tlvs, Inner);

    Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_MULTICAST_CIPHER_ALGO_LIST);
    for (i = 0; i < Dot11->MulticastCipherCount; i++)
        Ndis6WdiTlvAppend32(Tlvs, (ULONG)Dot11->MulticastCipher[i]);
    Ndis6WdiTlvClose(Tlvs, Inner);

    Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_UNICAST_CIPHER_ALGO_LIST);
    for (i = 0; i < Dot11->UnicastCount; i++)
        Ndis6WdiTlvAppend32(Tlvs, (ULONG)Dot11->Unicast[i]);
    Ndis6WdiTlvClose(Tlvs, Inner);

    if (Dot11->DesiredBssidCount != 0)
        Ndis6WdiTlvAdd(Tlvs, WDI_TLV_ALLOWED_BSSIDS_LIST, Dot11->DesiredBssids, Dot11->DesiredBssidCount * sizeof(DOT11_MAC_ADDRESS));

    Ndis6WdiTlvClose(Tlvs, Outer);
}

static
NDIS_STATUS
Ndis6WdiJobConnect(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    DOT11_CONNECTION_START_PARAMETERS Start;
    NDIS6_WDI_TLV_BUILDER Candidates;
    NDIS6_WDI_TLV_BUILDER Tlvs;
    DOT11_SSID Ssid;
    ULONG Count;
    ULONG Length;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    (VOID)Ndis6WdiInput(Request, &Length);
    Ndis6WdiConsumed(Request, Length);

    Ndis6WdiTlvInit(&Candidates);
    Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, NULL, NULL, 0, NULL);
    if (Count == 0 && Wdi->Dot11.HaveDesiredSsid)
    {
        static const DOT11_MAC_ADDRESS Broadcast = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

        Ndis6WdiTlvInit(&Tlvs);
        Ndis6WdiBuildScanTlvs(Wdi, &Tlvs, Broadcast, 1, &Wdi->Dot11.DesiredSsid, WDI_SCAN_TYPE_AUTO);
        (VOID)Ndis6WdiTask(Wdi, WDI_TASK_SCAN, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
        Ndis6WdiTlvFree(&Tlvs);

        Ndis6WdiTlvFree(&Candidates);
        Ndis6WdiTlvInit(&Candidates);
        Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, NULL, NULL, 0, NULL);
    }

    RtlZeroMemory(&Start, sizeof(Start));
    Start.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Start.Header.Revision = DOT11_CONNECTION_START_PARAMETERS_REVISION_1;
    Start.Header.Size = sizeof(Start);
    Start.BSSType = dot11_BSS_type_infrastructure;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_CONNECTION_START, &Start, sizeof(Start));

    if (Count == 0 || Candidates.Failed)
    {
        DbgPrint("WDI: no candidate network for connect\n");
        Ndis6WdiTlvFree(&Candidates);
        Ndis6WdiIndicateConnectionCompletion(Wdi, DOT11_CONNECTION_STATUS_CANDIDATE_LIST_EXHAUSTED);
        return NDIS_STATUS_SUCCESS;
    }

    (VOID)Ndis6WdiSendPrivacyExemptions(Wdi);

    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiBuildConnectParameters(Wdi, &Tlvs, &Ssid, FALSE, 0, WDI_ROAM_TRIGGER_OTHER);
    Ndis6WdiTlvAppend(&Tlvs, Candidates.Buffer, Candidates.Length);
    Ndis6WdiTlvFree(&Candidates);

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.Connecting = TRUE;
    Wdi->Dot11.Associated = FALSE;
    Wdi->Dot11.AssociationIndicated = FALSE;
    Wdi->Dot11.Ssid = Ssid;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    DbgPrint("WDI: connect to \"%.*s\" with %lu candidates\n", (int)Ssid.uSSIDLength, Ssid.ucSSID, Count);
    Status = Ndis6WdiTask(Wdi, WDI_TASK_CONNECT, Wdi->PortId, &Tlvs, FALSE, Ndis6WdiConnectComplete, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Wdi->Dot11.Connecting = FALSE;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
        Ndis6WdiIndicateConnectionCompletion(Wdi, DOT11_CONNECTION_STATUS_FAILURE);
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
Ndis6WdiIndicateDisassociation(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(6) const UCHAR *Address,
    _In_ DOT11_ASSOC_STATUS Reason)
{
    DOT11_DISASSOCIATION_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Parameters.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Parameters.Header.Size = sizeof(Parameters);
    RtlCopyMemory(Parameters.MacAddr, Address, sizeof(DOT11_MAC_ADDRESS));
    Parameters.uReason = Reason;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_DISASSOCIATION, &Parameters, sizeof(Parameters));
}

static
BOOLEAN
Ndis6WdiTakeAssociation(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Out_writes_bytes_(6) PUCHAR Bssid)
{
    BOOLEAN Associated;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Associated = Wdi->Dot11.Associated;
    RtlCopyMemory(Bssid, Wdi->Dot11.Bssid, sizeof(DOT11_MAC_ADDRESS));
    Wdi->Dot11.Associated = FALSE;
    Wdi->Dot11.PeerConfigured = FALSE;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    return Associated;
}

static
VOID
Ndis6WdiSetDisconnecting(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ BOOLEAN Disconnecting)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.Disconnecting = Disconnecting;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
}

static
VOID
Ndis6WdiDisconnectComplete(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status,
    _In_reads_bytes_opt_(Length) PUCHAR Tlvs,
    _In_ ULONG Length,
    _In_opt_ PVOID Context)
{
    DOT11_MAC_ADDRESS Bssid;

    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Tlvs);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Context);

    if (Ndis6WdiTakeAssociation(Wdi, Bssid))
    {
        Ndis6WdiIndicateDisassociation(Wdi, Bssid, DOT11_ASSOC_STATUS_DISASSOCIATED_BY_OS);
        Ndis6WdiIndicateLinkState(Wdi, FALSE);
    }
    Ndis6WdiSetDisconnecting(Wdi, FALSE);
    Ndis6WdiDataFlushQueue(Wdi, NDIS_STATUS_MEDIA_DISCONNECTED);
}

static
VOID
Ndis6WdiBuildDisconnectTlvs(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs)
{
    ULONG Offset;
    KIRQL OldIrql;
    DOT11_MAC_ADDRESS Bssid;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    RtlCopyMemory(Bssid, Wdi->Dot11.Bssid, sizeof(Bssid));
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_DISCONNECT_PARAMETERS);
    Ndis6WdiTlvAppend(Tlvs, Bssid, sizeof(Bssid));
    Ndis6WdiTlvAppend16(Tlvs, NDIS6_WDI_REASON_LEAVING);
    Ndis6WdiTlvClose(Tlvs, Offset);
}

static
NDIS_STATUS
Ndis6WdiJobDisconnect(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Length;
    NDIS_STATUS Status;

    (VOID)Ndis6WdiInput(Request, &Length);
    Ndis6WdiConsumed(Request, Length);

    if (!Wdi->Dot11.Associated && !Wdi->Dot11.Connecting)
        return NDIS_STATUS_SUCCESS;

    Ndis6WdiSetDisconnecting(Wdi, TRUE);
    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiBuildDisconnectTlvs(Wdi, &Tlvs);
    Status = Ndis6WdiTask(Wdi, WDI_TASK_DISCONNECT, Wdi->PortId, &Tlvs, FALSE, Ndis6WdiDisconnectComplete, NULL);
    Ndis6WdiTlvFree(&Tlvs);
    if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_PENDING)
        Ndis6WdiSetDisconnecting(Wdi, FALSE);
    return Status;
}

VOID
Ndis6WdiDot11HaltDisconnect(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    DOT11_MAC_ADDRESS Bssid;

    if (!Wdi->Dot11.Associated)
        return;

    Ndis6WdiSetDisconnecting(Wdi, TRUE);
    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiBuildDisconnectTlvs(Wdi, &Tlvs);
    (VOID)Ndis6WdiTask(Wdi, WDI_TASK_DISCONNECT, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
    (VOID)Ndis6WdiTakeAssociation(Wdi, Bssid);
    Ndis6WdiSetDisconnecting(Wdi, FALSE);
}

static
NDIS_STATUS
Ndis6WdiJobReset(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    static const DOT11_MAC_ADDRESS Zero = { 0 };
    PDOT11_RESET_REQUEST Reset;
    NDIS6_WDI_TLV_BUILDER Tlvs;
    DOT11_MAC_ADDRESS Bssid;
    BOOLEAN SetDefaultMib = TRUE;
    ULONG Offset;
    ULONG Length;
    NDIS_STATUS Status;

    Reset = Ndis6WdiInput(Request, &Length);
    if (Reset != NULL && Length >= sizeof(DOT11_RESET_REQUEST))
        SetDefaultMib = Reset->bSetDefaultMIB;

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_DOT11_RESET_PARAMETERS);
    Ndis6WdiTlvAppend8(&Tlvs, SetDefaultMib);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    if (Reset != NULL && Length >= sizeof(DOT11_RESET_REQUEST) &&
        !RtlEqualMemory(Reset->dot11MacAddress, Zero, sizeof(DOT11_MAC_ADDRESS)))
    {
        Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_CONFIGURED_MAC_ADDRESS, Reset->dot11MacAddress, sizeof(DOT11_MAC_ADDRESS));
    }
    Ndis6WdiSetDisconnecting(Wdi, TRUE);
    Status = Ndis6WdiTask(Wdi, WDI_TASK_DOT11_RESET, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    (VOID)Ndis6WdiTakeAssociation(Wdi, Bssid);
    Ndis6WdiSetDisconnecting(Wdi, FALSE);
    Ndis6WdiDataFlushQueue(Wdi, NDIS_STATUS_RESET_IN_PROGRESS);

    if (Status == NDIS_STATUS_SUCCESS && SetDefaultMib)
    {
        NDIS6_WDI_DOT11 Defaults;
        KIRQL OldIrql;

        RtlZeroMemory(&Defaults, sizeof(Defaults));
        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Defaults.PacketFilter = Wdi->Dot11.PacketFilter;
        Defaults.MulticastCount = Wdi->Dot11.MulticastCount;
        RtlCopyMemory(Defaults.MulticastList, Wdi->Dot11.MulticastList, sizeof(Defaults.MulticastList));
        Wdi->Dot11 = Defaults;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
        Ndis6WdiDot11Initialize(Wdi);
        Wdi->Dot11.CurrentPhyType = Wdi->PhyTypes[0];
    }

    Ndis6WdiConsumed(Request, Length);
    if (Request->RequestType == NdisRequestMethod)
    {
        DOT11_STATUS_INDICATION Confirm;

        RtlZeroMemory(&Confirm, sizeof(Confirm));
        Confirm.uStatusType = DOT11_STATUS_RESET_CONFIRM;
        Confirm.ndisStatus = Status;
        if (Request->DATA.METHOD_INFORMATION.OutputBufferLength >= sizeof(Confirm))
        {
            RtlCopyMemory(Request->DATA.METHOD_INFORMATION.InformationBuffer, &Confirm, sizeof(Confirm));
            Request->DATA.METHOD_INFORMATION.BytesWritten = sizeof(Confirm);
        }
        return NDIS_STATUS_SUCCESS;
    }

    return Status;
}

static
VOID
Ndis6WdiIndicatePhyState(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    DOT11_PHY_STATE_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Parameters.Header.Revision = DOT11_PHY_STATE_PARAMETERS_REVISION_1;
    Parameters.Header.Size = sizeof(Parameters);
    Parameters.uPhyId = 0;
    Parameters.bHardwarePhyState = Wdi->HardwareRadioOn;
    Parameters.bSoftwarePhyState = Wdi->SoftwareRadioOn;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_PHY_STATE_CHANGED, &Parameters, sizeof(Parameters));
}

static
NDIS_STATUS
Ndis6WdiJobRadio(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    PBOOLEAN On;
    ULONG Length;
    UCHAR State;
    NDIS_STATUS Status;

    On = Ndis6WdiInput(Request, &Length);
    if (On == NULL || Length < sizeof(BOOLEAN))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(BOOLEAN);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    State = *On ? 1 : 0;
    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_RADIO_STATE_PARAMETERS, &State, sizeof(State));
    Status = Ndis6WdiTask(Wdi, WDI_TASK_SET_RADIO_STATE, WDI_PORT_ID_ADAPTER, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        Wdi->SoftwareRadioOn = State != 0;
        Ndis6WdiIndicatePhyState(Wdi);
    }

    Ndis6WdiConsumed(Request, sizeof(BOOLEAN));
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobPacketFilter(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Length;
    PULONG Filter;
    ULONG Offset;
    NDIS_STATUS Status;

    Filter = Ndis6WdiInput(Request, &Length);
    if (Filter == NULL || Length < sizeof(ULONG))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_PACKET_FILTER_PARAMETERS);
    Ndis6WdiTlvAppend32(&Tlvs, *Filter);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    Status = Ndis6WdiProperty(Wdi, WDI_SET_RECEIVE_PACKET_FILTER, Wdi->PortId, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status == NDIS_STATUS_SUCCESS)
        Wdi->Dot11.PacketFilter = *Filter;
    Ndis6WdiConsumed(Request, sizeof(ULONG));
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobMulticast(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    PUCHAR List;
    ULONG Length;
    ULONG Count;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    List = Ndis6WdiInput(Request, &Length);
    if (Length % sizeof(DOT11_MAC_ADDRESS) != 0)
        return NDIS_STATUS_INVALID_LENGTH;

    Count = Length / sizeof(DOT11_MAC_ADDRESS);
    if (Count > NDIS6_WDI_MAX_MULTICAST)
        return NDIS_STATUS_MULTICAST_FULL;

    Ndis6WdiTlvInit(&Tlvs);
    if (Count != 0)
        Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_MULTICAST_LIST, List, Length);
    Status = Ndis6WdiProperty(Wdi, WDI_SET_MULTICAST_LIST, Wdi->PortId, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Wdi->Dot11.MulticastCount = Count;
        if (Count != 0)
            RtlCopyMemory(Wdi->Dot11.MulticastList, List, Length);
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    }

    Ndis6WdiConsumed(Request, Length);
    return Status;
}

static
BOOLEAN
Ndis6WdiAppendKeyMaterial(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(Length) PUCHAR Key,
    _In_ ULONG Length)
{
    ULONG Offset;

    switch (Algorithm)
    {
        case DOT11_CIPHER_ALGO_CCMP:
        {
            PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Key;

            if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey) ||
                Ccmp->ulCCMPKeyLength > Length - FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey))
            {
                return FALSE;
            }
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_CCMP_KEY, Ccmp->ucCCMPKey, Ccmp->ulCCMPKeyLength);
            return TRUE;
        }

        case DOT11_CIPHER_ALGO_GCMP:
        {
            PDOT11_KEY_ALGO_GCMP Gcmp = (PDOT11_KEY_ALGO_GCMP)Key;

            if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_GCMP, ucGCMPKey) ||
                Gcmp->ulGCMPKeyLength > Length - FIELD_OFFSET(DOT11_KEY_ALGO_GCMP, ucGCMPKey))
            {
                return FALSE;
            }
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_GCMP_KEY, Gcmp->ucGCMPKey, Gcmp->ulGCMPKeyLength);
            return TRUE;
        }

        case DOT11_CIPHER_ALGO_TKIP:
        {
            PDOT11_KEY_ALGO_TKIP_MIC Tkip = (PDOT11_KEY_ALGO_TKIP_MIC)Key;

            if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC, ucTKIPMICKeys) ||
                Tkip->ulTKIPKeyLength > Length - FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC, ucTKIPMICKeys) ||
                Tkip->ulMICKeyLength > Length - FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC, ucTKIPMICKeys) - Tkip->ulTKIPKeyLength)
            {
                return FALSE;
            }
            Offset = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CIPHER_KEY_TKIP_INFO);
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_TKIP_KEY, Tkip->ucTKIPMICKeys, Tkip->ulTKIPKeyLength);
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_TKIP_MIC, Tkip->ucTKIPMICKeys + Tkip->ulTKIPKeyLength, Tkip->ulMICKeyLength);
            Ndis6WdiTlvClose(Tlvs, Offset);
            return TRUE;
        }

        case DOT11_CIPHER_ALGO_BIP:
        {
            PDOT11_KEY_ALGO_BIP Bip = (PDOT11_KEY_ALGO_BIP)Key;

            if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_BIP, ucBIPKey) ||
                Bip->ulBIPKeyLength > Length - FIELD_OFFSET(DOT11_KEY_ALGO_BIP, ucBIPKey))
            {
                return FALSE;
            }
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_BIP_KEY, Bip->ucBIPKey, Bip->ulBIPKeyLength);
            return TRUE;
        }

        case DOT11_CIPHER_ALGO_WEP40:
        case DOT11_CIPHER_ALGO_WEP104:
        case DOT11_CIPHER_ALGO_WEP:
            if (Length == 0)
                return FALSE;
            Ndis6WdiTlvAdd(Tlvs, WDI_TLV_CIPHER_KEY_WEP_KEY, Key, Length);
            return TRUE;

        default:
            return FALSE;
    }
}

static
BOOLEAN
Ndis6WdiAppendKey(
    _Inout_ PNDIS6_WDI_TLV_BUILDER Tlvs,
    _In_ BOOLEAN Delete,
    _In_opt_ PUCHAR Peer,
    _In_ BOOLEAN HaveKeyId,
    _In_ ULONG KeyId,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ ULONG Direction,
    _In_ BOOLEAN Static,
    _In_ ULONG KeyType,
    _In_reads_bytes_opt_(Length) PUCHAR Key,
    _In_ ULONG Length)
{
    ULONG Outer;
    ULONG Inner;
    BOOLEAN Valid = TRUE;

    Outer = Ndis6WdiTlvOpen(Tlvs, Delete ? WDI_TLV_DELETE_CIPHER_KEY_INFO : WDI_TLV_SET_CIPHER_KEY_INFO);
    if (Peer != NULL)
        Ndis6WdiTlvAdd(Tlvs, WDI_TLV_PEER_MAC_ADDRESS, Peer, sizeof(DOT11_MAC_ADDRESS));
    if (HaveKeyId)
    {
        Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CIPHER_KEY_ID);
        Ndis6WdiTlvAppend32(Tlvs, KeyId);
        Ndis6WdiTlvClose(Tlvs, Inner);
    }
    Inner = Ndis6WdiTlvOpen(Tlvs, WDI_TLV_CIPHER_KEY_TYPE_INFO);
    Ndis6WdiTlvAppend32(Tlvs, (ULONG)Algorithm);
    Ndis6WdiTlvAppend32(Tlvs, Direction);
    Ndis6WdiTlvAppend8(Tlvs, Static);
    Ndis6WdiTlvAppend32(Tlvs, KeyType);
    Ndis6WdiTlvClose(Tlvs, Inner);
    if (!Delete)
        Valid = Key != NULL && Ndis6WdiAppendKeyMaterial(Tlvs, Algorithm, Key, Length);
    Ndis6WdiTlvClose(Tlvs, Outer);
    return Valid;
}

static
NDIS_STATUS
Ndis6WdiSendKeys(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS6_WDI_TLV_BUILDER Add,
    _In_ PNDIS6_WDI_TLV_BUILDER Delete)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    if (Add->Failed || Delete->Failed)
        return NDIS_STATUS_RESOURCES;

    if (Delete->Length != 0)
        Status = Ndis6WdiProperty(Wdi, WDI_SET_DELETE_CIPHER_KEYS, Wdi->PortId, Delete, NULL, NULL);
    if (Status == NDIS_STATUS_SUCCESS && Add->Length != 0)
        Status = Ndis6WdiProperty(Wdi, WDI_SET_ADD_CIPHER_KEYS, Wdi->PortId, Add, NULL, NULL);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobKeyMapping(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Add;
    NDIS6_WDI_TLV_BUILDER Delete;
    PDOT11_BYTE_ARRAY Array;
    ULONG Length;
    ULONG Offset = 0;
    ULONG Bytes;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    Array = Ndis6WdiInput(Request, &Length);
    if (Array == NULL || Length < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer))
        return NDIS_STATUS_INVALID_LENGTH;

    Bytes = Array->uNumOfBytes;
    if (Bytes > Length - FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer))
        return NDIS_STATUS_INVALID_DATA;

    Ndis6WdiTlvInit(&Add);
    Ndis6WdiTlvInit(&Delete);
    while (Offset + FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) <= Bytes)
    {
        PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)(Array->ucBuffer + Offset);
        ULONG Size = FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + Value->usKeyLength;

        if (Offset + Size > Bytes)
        {
            Status = NDIS_STATUS_INVALID_DATA;
            break;
        }

        if (!Ndis6WdiAppendKey(Value->bDelete ? &Delete : &Add,
                               Value->bDelete,
                               Value->PeerMacAddr,
                               FALSE,
                               0,
                               Value->AlgorithmId,
                               (ULONG)Value->Direction,
                               Value->bStatic,
                               WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY,
                               Value->ucKey,
                               Value->usKeyLength))
        {
            Status = NDIS_STATUS_INVALID_DATA;
            break;
        }
        Offset += Size;
    }

    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6WdiSendKeys(Wdi, &Add, &Delete);
    Ndis6WdiTlvFree(&Add);
    Ndis6WdiTlvFree(&Delete);

    Ndis6WdiConsumed(Request, Length);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobDefaultKey(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    static const DOT11_MAC_ADDRESS Zero = { 0 };
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    NDIS6_WDI_TLV_BUILDER Add;
    NDIS6_WDI_TLV_BUILDER Delete;
    ULONG Length;
    ULONG KeyType;
    ULONG Direction;
    BOOLEAN Wep;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    Value = Ndis6WdiInput(Request, &Length);
    if (Value == NULL || Length < FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) ||
        Value->usKeyLength > Length - FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey))
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Wep = Value->AlgorithmId == DOT11_CIPHER_ALGO_WEP40 ||
          Value->AlgorithmId == DOT11_CIPHER_ALGO_WEP104 ||
          Value->AlgorithmId == DOT11_CIPHER_ALGO_WEP;
    KeyType = Value->AlgorithmId == DOT11_CIPHER_ALGO_BIP ? WDI_CIPHER_KEY_TYPE_IGTK : WDI_CIPHER_KEY_TYPE_GROUP_KEY;
    Direction = Wep ? WDI_CIPHER_KEY_DIRECTION_BOTH : WDI_CIPHER_KEY_DIRECTION_INBOUND;

    Ndis6WdiTlvInit(&Add);
    Ndis6WdiTlvInit(&Delete);
    if (!Ndis6WdiAppendKey(Value->bDelete ? &Delete : &Add,
                           Value->bDelete,
                           RtlEqualMemory(Value->MacAddr, Zero, sizeof(DOT11_MAC_ADDRESS)) ? NULL : Value->MacAddr,
                           TRUE,
                           Value->uKeyIndex,
                           Value->AlgorithmId,
                           Direction,
                           Value->bStatic,
                           KeyType,
                           Value->ucKey,
                           Value->usKeyLength))
    {
        Status = NDIS_STATUS_INVALID_DATA;
    }

    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6WdiSendKeys(Wdi, &Add, &Delete);
    Ndis6WdiTlvFree(&Add);
    Ndis6WdiTlvFree(&Delete);

    Ndis6WdiConsumed(Request, Length);
    return Status;
}

static
NDIS_STATUS
Ndis6WdiJobDefaultKeyId(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    PULONG KeyId;
    ULONG Length;
    ULONG Offset;
    NDIS_STATUS Status;

    KeyId = Ndis6WdiInput(Request, &Length);
    if (KeyId == NULL || Length < sizeof(ULONG))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_DEFAULT_TX_KEY_ID_PARAMETERS);
    Ndis6WdiTlvAppend32(&Tlvs, *KeyId);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    Status = Ndis6WdiProperty(Wdi, WDI_SET_DEFAULT_KEY_ID, Wdi->PortId, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);

    if (Status == NDIS_STATUS_SUCCESS)
        Wdi->Dot11.DefaultKeyId = *KeyId;
    Ndis6WdiConsumed(Request, sizeof(ULONG));
    return Status;
}

static
BOOLEAN
Ndis6WdiRoamAllowed(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(6) const UCHAR *Current,
    _In_reads_(ListedCount) const DOT11_MAC_ADDRESS *Listed,
    _In_ ULONG ListedCount)
{
    BOOLEAN Allowed;
    KIRQL OldIrql;
    ULONG i;
    ULONG j;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Allowed = Wdi->Dot11.DesiredBssidCount == 0;
    for (i = 0; !Allowed && i < Wdi->Dot11.DesiredBssidCount; i++)
    {
        if (RtlEqualMemory(Wdi->Dot11.DesiredBssids[i], Current, sizeof(DOT11_MAC_ADDRESS)))
            continue;
        if (ListedCount == 0)
            Allowed = TRUE;
        for (j = 0; !Allowed && j < ListedCount; j++)
            Allowed = RtlEqualMemory(Wdi->Dot11.DesiredBssids[i], Listed[j], sizeof(DOT11_MAC_ADDRESS));
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    return Allowed;
}

static
BOOLEAN
Ndis6WdiRoamBegin(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG Reason)
{
    DOT11_ROAMING_START_PARAMETERS Start;
    BOOLEAN Associated;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Associated = Wdi->Dot11.Associated && !Wdi->Dot11.Connecting && !Wdi->Dot11.Roaming;
    Wdi->Dot11.Roaming = Associated;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    if (!Associated)
        return FALSE;

    RtlZeroMemory(&Start, sizeof(Start));
    Start.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Start.Header.Revision = DOT11_ROAMING_START_PARAMETERS_REVISION_1;
    Start.Header.Size = sizeof(Start);
    Start.uRoamingReason = Ndis6WdiAssocStatus(Reason, 0);
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ROAMING_START, &Start, sizeof(Start));
    return TRUE;
}

static
NDIS_STATUS
Ndis6WdiRoamAttempt(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Candidates,
    _In_ ULONG Count,
    _In_ PDOT11_SSID Ssid,
    _In_reads_bytes_(6) const UCHAR *From,
    _In_ ULONG Reason,
    _In_ ULONG Trigger,
    _In_ ULONG Task)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    NDIS_STATUS Status = NDIS_STATUS_NOT_ACCEPTED;

    if (Count != 0 && !Candidates->Failed)
    {
        Ndis6WdiTlvInit(&Tlvs);
        Ndis6WdiBuildConnectParameters(Wdi, &Tlvs, Ssid, TRUE, Reason, Trigger);
        Ndis6WdiTlvAppend(&Tlvs, Candidates->Buffer, Candidates->Length);

        DbgPrint("WDI: roaming from %02x:%02x:%02x:%02x:%02x:%02x (reason %lu) with %lu candidates\n",
                 From[0], From[1], From[2], From[3], From[4], From[5], Reason, Count);
        Status = Ndis6WdiTask(Wdi, Task, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
        Ndis6WdiTlvFree(&Tlvs);
    }
    Ndis6WdiTlvFree(Candidates);
    return Status;
}

static
VOID
Ndis6WdiRoamFinish(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS_STATUS Status)
{
    DOT11_ROAMING_COMPLETION_PARAMETERS Completion;
    DOT11_MAC_ADDRESS Current;
    BOOLEAN Associated;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.Roaming = FALSE;
    Associated = Wdi->Dot11.Associated;
    RtlCopyMemory(Current, Wdi->Dot11.Bssid, sizeof(Current));
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    DbgPrint("WDI: roam completed 0x%08lx, %s %02x:%02x:%02x:%02x:%02x:%02x\n", Status,
             Associated ? "associated with" : "disconnected from",
             Current[0], Current[1], Current[2], Current[3], Current[4], Current[5]);

    RtlZeroMemory(&Completion, sizeof(Completion));
    Completion.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Completion.Header.Revision = DOT11_ROAMING_COMPLETION_PARAMETERS_REVISION_1;
    Completion.Header.Size = sizeof(Completion);
    Completion.uStatus = Associated ? DOT11_ASSOC_STATUS_SUCCESS : DOT11_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST;
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ROAMING_COMPLETION, &Completion, sizeof(Completion));

    if (Associated)
        Ndis6WdiDot11RunInternalJob(Wdi, Ndis6WdiJobConnectionQuality);
    else
        Ndis6WdiIndicateLinkState(Wdi, FALSE);
}

static
VOID
Ndis6WdiRoamTo(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _Inout_ PNDIS6_WDI_TLV_BUILDER Candidates,
    _In_ ULONG Count,
    _In_ PDOT11_SSID Ssid,
    _In_reads_bytes_(6) const UCHAR *From,
    _In_ ULONG Reason,
    _In_ ULONG Trigger)
{
    if (!Ndis6WdiRoamBegin(Wdi, Reason))
    {
        Ndis6WdiTlvFree(Candidates);
        return;
    }
    Ndis6WdiRoamFinish(Wdi, Ndis6WdiRoamAttempt(Wdi, Candidates, Count, Ssid, From, Reason, Trigger, WDI_TASK_ROAM));
}

static
VOID
Ndis6WdiRoamScan(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PDOT11_SSID Ssid)
{
    static const DOT11_MAC_ADDRESS Broadcast = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    NDIS6_WDI_TLV_BUILDER Tlvs;

    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiBuildScanTlvs(Wdi, &Tlvs, Broadcast, 1, Ssid, WDI_SCAN_TYPE_AUTO);
    (VOID)Ndis6WdiTask(Wdi, WDI_TASK_SCAN, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
}

static
VOID
Ndis6WdiRoam(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS6_WDI_TLV_BUILDER Candidates;
    DOT11_MAC_ADDRESS Current;
    DOT11_SSID Ssid;
    ULONG Reason;
    ULONG Count;
    BOOLEAN Associated;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.RoamQueued = FALSE;
    Associated = Wdi->Dot11.Associated && !Wdi->Dot11.Connecting;
    RtlCopyMemory(Current, Wdi->Dot11.Bssid, sizeof(Current));
    Ssid = Wdi->Dot11.Ssid;
    Reason = Wdi->Dot11.RoamReason;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (!Associated)
        return;

    if (Ndis6WdiRoamAllowed(Wdi, Current, NULL, 0))
        Ndis6WdiRoamScan(Wdi, &Ssid);

    Ndis6WdiTlvInit(&Candidates);
    Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, Current, NULL, 0, NULL);
    if (Count == 0 || Candidates.Failed)
    {
        DbgPrint("WDI: roaming needed (reason %lu) but no other candidate\n", Reason);
        Ndis6WdiTlvFree(&Candidates);
        return;
    }

    Ndis6WdiRoamTo(Wdi, &Candidates, Count, &Ssid, Current, Reason, WDI_ROAM_TRIGGER_OTHER);
}

static
VOID
Ndis6WdiReconnect(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    NDIS6_WDI_TLV_BUILDER Candidates;
    DOT11_MAC_ADDRESS From;
    DOT11_SSID Ssid;
    LARGE_INTEGER Delay;
    NDIS_STATUS Status = NDIS_STATUS_NOT_ACCEPTED;
    ULONG Attempt;
    ULONG Count;
    BOOLEAN Roaming;
    BOOLEAN Pending;
    KIRQL OldIrql;

    for (Attempt = 0; Attempt < NDIS6_WDI_RECONNECT_ATTEMPTS; Attempt++)
    {
        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Roaming = Wdi->Dot11.Roaming && !Wdi->Dot11.Associated;
        RtlCopyMemory(From, Wdi->Dot11.Bssid, sizeof(From));
        Ssid = Wdi->Dot11.Ssid;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
        if (!Roaming)
            return;

        if (Attempt != 0)
        {
            KeAcquireSpinLock(&Wdi->JobLock, &OldIrql);
            Pending = !IsListEmpty(&Wdi->JobList);
            KeReleaseSpinLock(&Wdi->JobLock, OldIrql);
            if (Pending || Wdi->WorkerStop)
                break;
            Delay.QuadPart = -(LONGLONG)NDIS6_WDI_RECONNECT_DELAY_MS * 10000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }

        Ndis6WdiRoamScan(Wdi, &Ssid);

        Ndis6WdiTlvInit(&Candidates);
        Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, NULL, NULL, 0, NULL);
        if (Count == 0 || Candidates.Failed)
            DbgPrint("WDI: association lost and no candidate to roam to\n");
        Status = Ndis6WdiRoamAttempt(Wdi, &Candidates, Count, &Ssid, From,
                                     WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST, WDI_ROAM_TRIGGER_OTHER, WDI_TASK_CONNECT);

        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Roaming = !Wdi->Dot11.Associated;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
        if (!Roaming)
            break;
    }

    Ndis6WdiRoamFinish(Wdi, Status);
}

static
VOID
Ndis6WdiSendBssTransitionResponse(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(6) const UCHAR *Peer,
    _In_ UCHAR Token,
    _In_ UCHAR Status,
    _In_reads_bytes_(6) const UCHAR *Target)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    PLIST_ENTRY Link;
    UCHAR Body[11];
    ULONG Channel = 0;
    ULONG BandId;
    ULONG Offset;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    BandId = Wdi->Dot11.BandId;
    for (Link = Wdi->BssList.Flink; Link != &Wdi->BssList; Link = Link->Flink)
    {
        PNDIS6_WDI_BSS Bss = CONTAINING_RECORD(Link, NDIS6_WDI_BSS, Link);

        if (RtlEqualMemory(Bss->Bssid, Peer, sizeof(DOT11_MAC_ADDRESS)))
        {
            Channel = Bss->Channel;
            BandId = Bss->BandId;
            break;
        }
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    Body[0] = WDI_ACTION_FRAME_CATEGORY_WNM;
    Body[1] = WDI_WNM_ACTION_BSS_TRANSITION_MANAGEMENT_RESPONSE;
    Body[2] = Token;
    Body[3] = Status;
    Body[4] = 0;
    RtlCopyMemory(Body + 5, Target, sizeof(DOT11_MAC_ADDRESS));

    Ndis6WdiTlvInit(&Tlvs);
    Offset = Ndis6WdiTlvOpen(&Tlvs, WDI_TLV_SEND_ACTION_FRAME_RESPONSE_PARAMETERS);
    Ndis6WdiTlvAppend32(&Tlvs, Channel);
    Ndis6WdiTlvAppend32(&Tlvs, BandId);
    Ndis6WdiTlvAppend(&Tlvs, Peer, sizeof(DOT11_MAC_ADDRESS));
    Ndis6WdiTlvAppend32(&Tlvs, 100);
    Ndis6WdiTlvAppend32(&Tlvs, 0);
    Ndis6WdiTlvClose(&Tlvs, Offset);
    Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_ACTION_FRAME_BODY, Body, Status == WDI_BSS_TRANSITION_ACCEPT ? sizeof(Body) : 5);
    (VOID)Ndis6WdiTask(Wdi, WDI_TASK_SEND_RESPONSE_ACTION_FRAME, Wdi->PortId, &Tlvs, TRUE, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
}

static
VOID
Ndis6WdiBssTransition(
    _In_ PNDIS6_WDI_ADAPTER Wdi)
{
    DOT11_MAC_ADDRESS Listed[NDIS6_WDI_MAX_CANDIDATES];
    DOT11_MAC_ADDRESS Current;
    DOT11_MAC_ADDRESS Target;
    NDIS6_WDI_TLV_BUILDER Candidates;
    DOT11_SSID Ssid;
    ULONG ListedCount;
    ULONG Count;
    UCHAR Token;
    UCHAR Mode;
    UCHAR Status;
    BOOLEAN Associated;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.BtmQueued = FALSE;
    Associated = Wdi->Dot11.Associated && !Wdi->Dot11.Connecting && !Wdi->Dot11.Roaming;
    RtlCopyMemory(Current, Wdi->Dot11.Bssid, sizeof(Current));
    Ssid = Wdi->Dot11.Ssid;
    Token = Wdi->Dot11.BtmToken;
    Mode = Wdi->Dot11.BtmMode;
    ListedCount = Wdi->Dot11.BtmCandidateCount;
    RtlCopyMemory(Listed, Wdi->Dot11.BtmCandidates, ListedCount * sizeof(DOT11_MAC_ADDRESS));
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (!Associated)
        return;

    RtlZeroMemory(Target, sizeof(Target));
    Ndis6WdiTlvInit(&Candidates);
    Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, Current, Listed, ListedCount, Target);
    if ((Count == 0 || Candidates.Failed) && Ndis6WdiRoamAllowed(Wdi, Current, Listed, ListedCount))
    {
        Ndis6WdiTlvFree(&Candidates);
        Ndis6WdiRoamScan(Wdi, &Ssid);
        Ndis6WdiTlvInit(&Candidates);
        Count = Ndis6WdiBuildCandidates(Wdi, &Candidates, &Ssid, Current, Listed, ListedCount, Target);
    }
    Status = (Count == 0 || Candidates.Failed) ? WDI_BSS_TRANSITION_REJECT_NO_SUITABLE_BSS_CANDIDATES : WDI_BSS_TRANSITION_ACCEPT;

    DbgPrint("WDI: BSS transition request from %02x:%02x:%02x:%02x:%02x:%02x (mode 0x%02x, %lu listed), %s\n",
             Current[0], Current[1], Current[2], Current[3], Current[4], Current[5], Mode, ListedCount,
             Status == WDI_BSS_TRANSITION_ACCEPT ? "accepted" : "rejected, no suitable candidate");
    Ndis6WdiSendBssTransitionResponse(Wdi, Current, Token, Status, Target);

    if (Status != WDI_BSS_TRANSITION_ACCEPT)
    {
        Ndis6WdiTlvFree(&Candidates);
        return;
    }

    Ndis6WdiRoamTo(Wdi, &Candidates, Count, &Ssid, Current, WDI_ASSOC_STATUS_ROAMING_BETTER_AP_FOUND,
                   (Mode & 0x04) ? WDI_ROAM_TRIGGER_CRITICAL_BSS_TRANSITION_REQUEST : WDI_ROAM_TRIGGER_OTHER);
}

VOID
Ndis6WdiDot11RunInternalJob(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ NDIS6_WDI_INTERNAL_JOB Internal)
{
    NDIS6_WDI_TLV_BUILDER Tlvs;
    ULONG Hint;
    BOOLEAN Associated;
    KIRQL OldIrql;

    if (Internal == Ndis6WdiJobRoam)
    {
        Ndis6WdiRoam(Wdi);
        return;
    }
    if (Internal == Ndis6WdiJobBssTransition)
    {
        Ndis6WdiBssTransition(Wdi);
        return;
    }
    if (Internal == Ndis6WdiJobReconnect)
    {
        Ndis6WdiReconnect(Wdi);
        return;
    }

    if (Internal != Ndis6WdiJobConnectionQuality)
        return;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Associated = Wdi->Dot11.Associated;
    if (Wdi->Dot11.MediaStreaming)
        Hint = WDI_CONNECTION_QUALITY_LOW_LATENCY;
    else if (Wdi->Dot11.PowerSavingLevel == DOT11_POWER_SAVING_NO_POWER_SAVING)
        Hint = WDI_CONNECTION_QUALITY_NO_POWER_SAVE;
    else
        Hint = WDI_CONNECTION_QUALITY_AUTO_POWER_SAVE;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (!Associated)
        return;

    Ndis6WdiTlvInit(&Tlvs);
    Ndis6WdiTlvAdd(&Tlvs, WDI_TLV_CONNECTION_QUALITY_PARAMETERS, &Hint, sizeof(ULONG));
    (VOID)Ndis6WdiProperty(Wdi, WDI_SET_CONNECTION_QUALITY, Wdi->PortId, &Tlvs, NULL, NULL);
    Ndis6WdiTlvFree(&Tlvs);
}

VOID
Ndis6WdiDot11RunJob(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.SET_INFORMATION.Oid;
    NDIS_STATUS Status;

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
            Status = Ndis6WdiJobScan(Wdi, Request);
            break;
        case OID_DOT11_CONNECT_REQUEST:
            Status = Ndis6WdiJobConnect(Wdi, Request);
            break;
        case OID_DOT11_DISCONNECT_REQUEST:
            Status = Ndis6WdiJobDisconnect(Wdi, Request);
            break;
        case OID_DOT11_RESET_REQUEST:
            Status = Ndis6WdiJobReset(Wdi, Request);
            break;
        case OID_DOT11_NIC_POWER_STATE:
            Status = Ndis6WdiJobRadio(Wdi, Request);
            break;
        case OID_GEN_CURRENT_PACKET_FILTER:
        case OID_DOT11_CURRENT_PACKET_FILTER:
            Status = Ndis6WdiJobPacketFilter(Wdi, Request);
            break;
        case OID_DOT11_MULTICAST_LIST:
        case OID_802_3_MULTICAST_LIST:
            Status = Ndis6WdiJobMulticast(Wdi, Request);
            break;
        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
            Status = Ndis6WdiJobKeyMapping(Wdi, Request);
            break;
        case OID_DOT11_CIPHER_DEFAULT_KEY:
            Status = Ndis6WdiJobDefaultKey(Wdi, Request);
            break;
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            Status = Ndis6WdiJobDefaultKeyId(Wdi, Request);
            break;
        case OID_DOT11_PRIVACY_EXEMPTION_LIST:
            Status = Ndis6WdiJobPrivacyExemption(Wdi, Request);
            break;
        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    NdisMOidRequestComplete(Wdi->Adapter, Request, Status);
}

static
VOID
Ndis6WdiIndicateLinkQuality(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(6) const UCHAR *Peer,
    _In_ UCHAR LinkQuality)
{
    UCHAR Buffer[sizeof(DOT11_LINK_QUALITY_PARAMETERS) + sizeof(DOT11_LINK_QUALITY_ENTRY)];
    PDOT11_LINK_QUALITY_PARAMETERS Quality = (PDOT11_LINK_QUALITY_PARAMETERS)Buffer;
    DOT11_LINK_QUALITY_ENTRY Entry;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Quality->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Quality->Header.Revision = DOT11_LINK_QUALITY_PARAMETERS_REVISION_1;
    Quality->Header.Size = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    Quality->uLinkQualityListSize = 1;
    Quality->uLinkQualityListOffset = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    RtlZeroMemory(&Entry, sizeof(Entry));
    RtlCopyMemory(Entry.PeerMacAddr, Peer, sizeof(DOT11_MAC_ADDRESS));
    Entry.ucLinkQuality = LinkQuality;
    RtlCopyMemory(Buffer + sizeof(DOT11_LINK_QUALITY_PARAMETERS), &Entry, sizeof(Entry));
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_LINK_QUALITY, Buffer, sizeof(Buffer));
}

static
VOID
Ndis6WdiHandleAssociationResult(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(Length) PUCHAR Entry,
    _In_ ULONG Length)
{
    PDOT11_ASSOCIATION_COMPLETION_PARAMETERS Completion;
    DOT11_ASSOCIATION_START_PARAMETERS Start;
    NDIS6_WDI_TLV Tlv;
    NDIS6_WDI_TLV Request = { 0 };
    NDIS6_WDI_TLV Response = { 0 };
    DOT11_ASSOC_STATUS Status;
    UCHAR Params[44];
    UCHAR Bssid[6];
    ULONG Size;
    ULONG DsInfo;
    KIRQL OldIrql;

    if (!Ndis6WdiTlvFind(Entry, Length, WDI_TLV_BSSID, &Tlv) || Tlv.Length < sizeof(Bssid))
        return;
    RtlCopyMemory(Bssid, Tlv.Value, sizeof(Bssid));

    RtlZeroMemory(Params, sizeof(Params));
    Params[0] = WDI_ASSOC_STATUS_FAILURE;
    if (Ndis6WdiTlvFind(Entry, Length, WDI_TLV_ASSOCIATION_RESULT_PARAMETERS, &Tlv))
        RtlCopyMemory(Params, Tlv.Value, min(Tlv.Length, sizeof(Params)));
    (VOID)Ndis6WdiTlvFind(Entry, Length, WDI_TLV_ASSOCIATION_REQUEST_FRAME, &Request);
    (VOID)Ndis6WdiTlvFind(Entry, Length, WDI_TLV_ASSOCIATION_RESPONSE_FRAME, &Response);

    Status = Ndis6WdiAssocStatus(Ndis6WdiRead32(Params), Ndis6WdiRead32(Params + 4));

    RtlZeroMemory(&Start, sizeof(Start));
    Start.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Start.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Start.Header.Size = sizeof(Start);
    RtlCopyMemory(Start.MacAddr, Bssid, sizeof(Bssid));
    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Start.SSID = Wdi->Dot11.Ssid;
    Wdi->Dot11.AssociationIndicated = TRUE;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ASSOCIATION_START, &Start, sizeof(Start));

    Size = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS) + Request.Length + Response.Length;
    Completion = ExAllocatePoolWithTag(NonPagedPool, Size, NDIS6_WDI_TAG);
    if (Completion == NULL)
        return;

    RtlZeroMemory(Completion, Size);
    Completion->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Completion->Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Completion->Header.Size = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS);
    RtlCopyMemory(Completion->MacAddr, Bssid, sizeof(Bssid));
    Completion->uStatus = Status;
    Completion->bReAssocReq = Params[8] != 0;
    Completion->bReAssocResp = Params[8] != 0;
    if (Request.Length != 0)
    {
        Completion->uAssocReqOffset = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS);
        Completion->uAssocReqSize = Request.Length;
        RtlCopyMemory((PUCHAR)Completion + Completion->uAssocReqOffset, Request.Value, Request.Length);
    }
    if (Response.Length != 0)
    {
        Completion->uAssocRespOffset = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS) + Request.Length;
        Completion->uAssocRespSize = Response.Length;
        RtlCopyMemory((PUCHAR)Completion + Completion->uAssocRespOffset, Response.Value, Response.Length);
    }
    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Completion->AuthAlgo = (DOT11_AUTH_ALGORITHM)Ndis6WdiRead32(Params + 9);
        Completion->UnicastCipher = (DOT11_CIPHER_ALGORITHM)Ndis6WdiRead32(Params + 13);
        Completion->MulticastCipher = (DOT11_CIPHER_ALGORITHM)Ndis6WdiRead32(Params + 17);
        Completion->bFourAddressSupported = Params[25] != 0;
        Completion->bPortAuthorized = Params[26] != 0;
        Completion->ucActiveQoSProtocol = Params[27] ? DOT11_QOS_PROTOCOL_FLAG_WMM : 0;
        DsInfo = Ndis6WdiRead32(Params + 28);
        Completion->DSInfo = DsInfo == WDI_DS_CHANGED ? DOT11_DS_CHANGED :
                             DsInfo == WDI_DS_UNCHANGED ? DOT11_DS_UNCHANGED : DOT11_DS_UNKNOWN;

        KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
        Wdi->Dot11.Associated = TRUE;
        RtlCopyMemory(Wdi->Dot11.Bssid, Bssid, sizeof(Bssid));
        Wdi->Dot11.Qos = Params[27] != 0;
        Wdi->Dot11.BandId = Ndis6WdiRead32(Params + 36);
        Wdi->Dot11.PeerConfigured = FALSE;
        KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
    }

    DbgPrint("WDI: association with %02x:%02x:%02x:%02x:%02x:%02x status 0x%08lx\n",
             Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5], Status);
    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION, Completion, Size);
    ExFreePoolWithTag(Completion, NDIS6_WDI_TAG);

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
        Ndis6WdiDataPeerConfigure(Wdi);
}

static
VOID
Ndis6WdiHandleLinkState(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(Length) PUCHAR Tlvs,
    _In_ ULONG Length)
{
    NDIS6_WDI_TLV Tlv;
    BOOLEAN Associated;
    KIRQL OldIrql;

    if (!Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_LINK_STATE_CHANGE_PARAMETERS, &Tlv) || Tlv.Length < 15)
        return;

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Wdi->Dot11.TxLinkSpeed = (ULONG64)Ndis6WdiRead32(Tlv.Value + 6) * 1000;
    Wdi->Dot11.RxLinkSpeed = (ULONG64)Ndis6WdiRead32(Tlv.Value + 10) * 1000;
    Wdi->Dot11.LinkQuality = Tlv.Value[14];
    Associated = Wdi->Dot11.Associated;
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (!Associated)
        return;

    Ndis6WdiIndicateLinkState(Wdi, TRUE);
    Ndis6WdiIndicateLinkQuality(Wdi, Tlv.Value, Tlv.Value[14]);
}

static
VOID
Ndis6WdiHandleActionFrame(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_reads_bytes_(Length) PUCHAR Tlvs,
    _In_ ULONG Length)
{
    DOT11_MAC_ADDRESS Listed[NDIS6_WDI_MAX_CANDIDATES];
    UCHAR Preference[NDIS6_WDI_MAX_CANDIDATES];
    NDIS6_WDI_TLV Body;
    NDIS6_WDI_TLV Source;
    PUCHAR Cursor;
    PUCHAR End;
    ULONG Count = 0;
    BOOLEAN Queue;
    UCHAR Mode;
    KIRQL OldIrql;

    if (!Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_ACTION_FRAME_BODY, &Body) ||
        !Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_BSSID, &Source) ||
        Source.Length < sizeof(DOT11_MAC_ADDRESS) || Body.Length < 7 ||
        Body.Value[0] != WDI_ACTION_FRAME_CATEGORY_WNM ||
        Body.Value[1] != WDI_WNM_ACTION_BSS_TRANSITION_MANAGEMENT_REQUEST)
    {
        return;
    }

    Mode = Body.Value[3];
    Cursor = Body.Value + 7;
    End = Body.Value + Body.Length;
    if (Mode & 0x08)
    {
        if (End - Cursor < 12)
            return;
        Cursor += 12;
    }
    if (Mode & 0x10)
    {
        if (End - Cursor < 1 || End - Cursor < 1 + Cursor[0])
            return;
        Cursor += 1 + Cursor[0];
    }

    while ((Mode & 0x01) && End - Cursor >= 2 && End - Cursor >= 2 + Cursor[1])
    {
        if (Cursor[0] == 52 && Cursor[1] >= 13)
        {
            PUCHAR Sub = Cursor + 2 + 13;
            PUCHAR SubEnd = Cursor + 2 + Cursor[1];
            UCHAR Value = 1;
            ULONG i;

            while (SubEnd - Sub >= 2 && SubEnd - Sub >= 2 + Sub[1])
            {
                if (Sub[0] == 3 && Sub[1] >= 1)
                    Value = Sub[2];
                Sub += 2 + Sub[1];
            }

            if (Value != 0 && Count < NDIS6_WDI_MAX_CANDIDATES)
            {
                for (i = Count; i > 0 && Preference[i - 1] < Value; i--)
                {
                    RtlCopyMemory(Listed[i], Listed[i - 1], sizeof(DOT11_MAC_ADDRESS));
                    Preference[i] = Preference[i - 1];
                }
                RtlCopyMemory(Listed[i], Cursor + 2, sizeof(DOT11_MAC_ADDRESS));
                Preference[i] = Value;
                Count++;
            }
        }
        Cursor += 2 + Cursor[1];
    }

    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
    Queue = Wdi->Dot11.Associated && !Wdi->Dot11.Connecting && !Wdi->Dot11.Roaming && !Wdi->Dot11.BtmQueued &&
            RtlEqualMemory(Wdi->Dot11.Bssid, Source.Value, sizeof(DOT11_MAC_ADDRESS));
    if (Queue)
    {
        Wdi->Dot11.BtmQueued = TRUE;
        Wdi->Dot11.BtmToken = Body.Value[2];
        Wdi->Dot11.BtmMode = Mode;
        Wdi->Dot11.BtmCandidateCount = Count;
        RtlCopyMemory(Wdi->Dot11.BtmCandidates, Listed, Count * sizeof(DOT11_MAC_ADDRESS));
    }
    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

    if (Queue)
        Ndis6WdiQueueInternalJob(Wdi, Ndis6WdiJobBssTransition);
}

VOID
Ndis6WdiDot11Indication(
    _In_ PNDIS6_WDI_ADAPTER Wdi,
    _In_ ULONG MessageId,
    _In_reads_bytes_(Length) PUCHAR Tlvs,
    _In_ ULONG Length)
{
    NDIS6_WDI_TLV Tlv;
    PUCHAR Cursor = Tlvs;

    switch (MessageId)
    {
        case WDI_INDICATION_BSS_ENTRY_LIST:
            while (Ndis6WdiTlvNext(&Cursor, Tlvs + Length, &Tlv))
            {
                if (Tlv.Type == WDI_TLV_BSS_ENTRY)
                    Ndis6WdiBssUpdate(Wdi, Tlv.Value, Tlv.Length);
            }
            break;

        case WDI_INDICATION_ASSOCIATION_RESULT:
            while (Ndis6WdiTlvNext(&Cursor, Tlvs + Length, &Tlv))
            {
                if (Tlv.Type == WDI_TLV_ASSOCIATION_RESULT)
                    Ndis6WdiHandleAssociationResult(Wdi, Tlv.Value, Tlv.Length);
            }
            break;

        case WDI_INDICATION_DISASSOCIATION:
            if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_DISASSOCIATION_INDICATION_PARAMETERS, &Tlv) && Tlv.Length >= 10)
            {
                DOT11_MAC_ADDRESS Bssid;
                BOOLEAN WasAssociated = Ndis6WdiTakeAssociation(Wdi, Bssid);
                BOOLEAN Roaming;
                BOOLEAN Lost;
                KIRQL OldIrql;

                KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
                Roaming = Wdi->Dot11.Roaming;
                Lost = WasAssociated && !Roaming && !Wdi->Dot11.Connecting && !Wdi->Dot11.Disconnecting;
                if (Lost)
                    Wdi->Dot11.Roaming = TRUE;
                KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

                if (Lost)
                {
                    DOT11_ROAMING_START_PARAMETERS Start;

                    RtlZeroMemory(&Start, sizeof(Start));
                    Start.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
                    Start.Header.Revision = DOT11_ROAMING_START_PARAMETERS_REVISION_1;
                    Start.Header.Size = sizeof(Start);
                    Start.uRoamingReason = DOT11_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST;
                    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ROAMING_START, &Start, sizeof(Start));
                }

                DbgPrint("WDI: disassociated, reason %lu\n", Ndis6WdiRead32(Tlv.Value + 6));
                Ndis6WdiIndicateDisassociation(Wdi, Tlv.Value, Ndis6WdiAssocStatus(Ndis6WdiRead32(Tlv.Value + 6), 0));
                if (Lost && !Ndis6WdiQueueInternalJob(Wdi, Ndis6WdiJobReconnect))
                {
                    DOT11_ROAMING_COMPLETION_PARAMETERS Completion;

                    KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
                    Wdi->Dot11.Roaming = FALSE;
                    KeReleaseSpinLock(&Wdi->StateLock, OldIrql);
                    RtlZeroMemory(&Completion, sizeof(Completion));
                    Completion.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
                    Completion.Header.Revision = DOT11_ROAMING_COMPLETION_PARAMETERS_REVISION_1;
                    Completion.Header.Size = sizeof(Completion);
                    Completion.uStatus = DOT11_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST;
                    Ndis6WdiIndicateStatusUp(Wdi, NDIS_STATUS_DOT11_ROAMING_COMPLETION, &Completion, sizeof(Completion));
                    Lost = FALSE;
                    Roaming = FALSE;
                }
                if (WasAssociated && !Roaming && !Lost)
                    Ndis6WdiIndicateLinkState(Wdi, FALSE);
            }
            break;

        case WDI_INDICATION_LINK_STATE_CHANGE:
            Ndis6WdiHandleLinkState(Wdi, Tlvs, Length);
            break;

        case WDI_INDICATION_RADIO_STATUS:
            if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_RADIO_STATE, &Tlv) && Tlv.Length >= 2)
            {
                Wdi->HardwareRadioOn = Tlv.Value[0] != 0;
                Wdi->SoftwareRadioOn = Tlv.Value[1] != 0;
                Ndis6WdiIndicatePhyState(Wdi);
            }
            break;

        case WDI_INDICATION_ROAMING_NEEDED:
        {
            ULONG Reason = WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST;
            BOOLEAN Queue;
            KIRQL OldIrql;

            if (Ndis6WdiTlvFind(Tlvs, Length, WDI_TLV_ROAMING_NEEDED_PARAMETERS, &Tlv) && Tlv.Length >= sizeof(ULONG))
                Reason = Ndis6WdiRead32(Tlv.Value);

            KeAcquireSpinLock(&Wdi->StateLock, &OldIrql);
            Queue = Wdi->Dot11.Associated && !Wdi->Dot11.Connecting && !Wdi->Dot11.Roaming && !Wdi->Dot11.RoamQueued;
            if (Queue)
            {
                Wdi->Dot11.RoamQueued = TRUE;
                Wdi->Dot11.RoamReason = Reason;
            }
            KeReleaseSpinLock(&Wdi->StateLock, OldIrql);

            if (Queue)
                Ndis6WdiQueueInternalJob(Wdi, Ndis6WdiJobRoam);
            break;
        }

        case WDI_INDICATION_ACTION_FRAME_RECEIVED:
            Ndis6WdiHandleActionFrame(Wdi, Tlvs, Length);
            break;

        case WDI_INDICATION_IHV_EVENT:
        case WDI_INDICATION_ADAPTER_STATE:
            break;

        default:
            DbgPrint("WDI: unhandled indication %lu\n", MessageId);
            break;
    }
}
