/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS and Native 802.11 OID handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rt2800usb.h"

#define NDEBUG
#include <debug.h>

NDIS_OID RtSupportedOids[] =
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
    OID_DOT11_OPERATIONAL_RATE_SET,
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
    OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
    OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
    OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
    OID_DOT11_CIPHER_DEFAULT_KEY,
    OID_DOT11_CIPHER_DEFAULT_KEY_ID,
    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
    OID_WDI_SET_SAE_AUTH_PARAMS,
};

ULONG RtSupportedOidCount = RTL_NUMBER_OF(RtSupportedOids);

static
NDIS_STATUS
RtQueryBuffer(
    _In_ PNDIS_OID_REQUEST Request,
    _In_reads_bytes_(Length) const VOID* Data,
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
RtQueryUlong(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Value)
{
    return RtQueryBuffer(Request, &Value, sizeof(Value));
}

static
NDIS_STATUS
RtQueryCounter(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG64 Value)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof(ULONG64))
        return RtQueryBuffer(Request, &Value, sizeof(ULONG64));

    return RtQueryUlong(Request, (ULONG)Value);
}

static
NDIS_STATUS
RtQueryStatistics(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_STATISTICS_INFO Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));
    Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Stats.Header.Size = sizeof(Stats);
    Stats.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT;
    Stats.ifHCInOctets = Adapter->RxBytes;
    Stats.ifHCInUcastPkts = Adapter->RxOk;
    Stats.ifInErrors = Adapter->RxError;
    Stats.ifHCOutOctets = Adapter->TxBytes;
    Stats.ifHCOutUcastPkts = Adapter->TxOk;
    Stats.ifOutErrors = Adapter->TxError;
    return RtQueryBuffer(Request, &Stats, sizeof(Stats));
}

static
NDIS_STATUS
RtQueryEnumBssList(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_opt_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Written,
    _Out_ PULONG Needed)
{
    *Written = RtBuildBssList(Adapter, Buffer, Length, Needed);
    return *Written ? NDIS_STATUS_SUCCESS : NDIS_STATUS_BUFFER_TOO_SHORT;
}

static
NDIS_STATUS
RtDot11Query(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_DOT11_MPDU_MAX_LENGTH:
            return RtQueryUlong(Request, RT_MAX_80211_FRAME);

        case OID_DOT11_OPERATION_MODE_CAPABILITY:
        {
            DOT11_OPERATION_MODE_CAPABILITY Capability;

            RtlZeroMemory(&Capability, sizeof(Capability));
            Capability.uMajorVersion = 2;
            Capability.uNumOfTXBuffers = RT_TX_URBS;
            Capability.uNumOfRXBuffers = RT_RX_URBS;
            Capability.uOpModeCapability = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            return RtQueryBuffer(Request, &Capability, sizeof(Capability));
        }

        case OID_DOT11_CURRENT_OPERATION_MODE:
        {
            DOT11_CURRENT_OPERATION_MODE Mode;

            RtlZeroMemory(&Mode, sizeof(Mode));
            Mode.uCurrentOpMode = Adapter->OperationMode;
            return RtQueryBuffer(Request, &Mode, sizeof(Mode));
        }

        case OID_DOT11_CURRENT_PHY_TYPE:
            return RtQueryUlong(Request, (ULONG)Adapter->CurrentPhyType);

        case OID_DOT11_SUPPORTED_PHY_TYPES:
        {
            UCHAR Buffer[FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + 2 * sizeof(DOT11_PHY_TYPE)];
            PDOT11_PHY_TYPE_LIST List = (PDOT11_PHY_TYPE_LIST)Buffer;

            RtlZeroMemory(Buffer, sizeof(Buffer));
            List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List->Header.Revision = DOT11_PHY_TYPE_LIST_REVISION_1;
            List->Header.Size = sizeof(DOT11_PHY_TYPE_LIST);
            List->uNumOfEntries = Adapter->DualBand ? 2 : 1;
            List->uTotalNumOfEntries = List->uNumOfEntries;
            List->dot11PhyType[0] = dot11_phy_type_erp;
            if (Adapter->DualBand)
                List->dot11PhyType[1] = dot11_phy_type_ofdm;
            return RtQueryBuffer(Request,
                                 Buffer,
                                 FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + List->uNumOfEntries * sizeof(DOT11_PHY_TYPE));
        }

        case OID_DOT11_CURRENT_PACKET_FILTER:
            return RtQueryUlong(Request, Adapter->PacketFilter);

        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
            return RtQueryUlong(Request, Adapter->CurrentChannel);

        case OID_DOT11_RTS_THRESHOLD:
            return RtQueryUlong(Request, Adapter->RtsThreshold);

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            return RtQueryUlong(Request, Adapter->FragmentationThreshold);

        case OID_DOT11_BEACON_PERIOD:
            return RtQueryUlong(Request, 100);

        case OID_DOT11_NIC_POWER_STATE:
        case OID_DOT11_HARDWARE_PHY_STATE:
        {
            BOOLEAN On = Adapter->RadioOn;
            return RtQueryBuffer(Request, &On, sizeof(On));
        }

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            return RtQueryUlong(Request, Adapter->AutoConfigEnabled ? 1 : 0);

        case OID_DOT11_MAC_ADDRESS:
        case OID_DOT11_CURRENT_ADDRESS:
            return RtQueryBuffer(Request, Adapter->CurrentAddress, RT_ADDRESS_LENGTH);

        case OID_DOT11_PERMANENT_ADDRESS:
            return RtQueryBuffer(Request, Adapter->PermanentAddress, RT_ADDRESS_LENGTH);

        case OID_DOT11_OPERATIONAL_RATE_SET:
        {
            static const UCHAR Rates[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
            DOT11_RATE_SET RateSet;

            RtlZeroMemory(&RateSet, sizeof(RateSet));
            RateSet.uRateSetLength = sizeof(Rates);
            RtlCopyMemory(RateSet.ucRateSet, Rates, sizeof(Rates));
            return RtQueryBuffer(Request, &RateSet, sizeof(RateSet));
        }

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return RtQueryUlong(Request, (ULONG)Adapter->AuthAlgorithm);

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return RtQueryUlong(Request, (ULONG)Adapter->UnicastCipher);

        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return RtQueryUlong(Request, (ULONG)Adapter->MulticastCipher);

        case OID_DOT11_DESIRED_BSS_TYPE:
            return RtQueryUlong(Request, (ULONG)Adapter->DesiredBssType);

        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written, Needed;
            NDIS_STATUS Status;

            Status = RtQueryEnumBssList(Adapter,
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
RtOidQuery(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return RtQueryBuffer(Request, RtSupportedOids, RtSupportedOidCount * sizeof(NDIS_OID));

        case OID_GEN_HARDWARE_STATUS:
            return RtQueryUlong(Request, Adapter->HardwareReady ? NdisHardwareStatusReady : NdisHardwareStatusInitializing);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return RtQueryUlong(Request, NdisMediumNative802_11);

        case OID_GEN_PHYSICAL_MEDIUM:
            return RtQueryUlong(Request, NdisPhysicalMediumNative802_11);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return RtQueryUlong(Request, RT_MTU_SIZE);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
            return RtQueryUlong(Request, RT_MAX_80211_FRAME);

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            return RtQueryUlong(Request, RT_MAX_80211_FRAME * RT_TX_URBS);

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            return RtQueryUlong(Request, RT_RX_BUFFER_SIZE * RT_RX_URBS);

        case OID_GEN_VENDOR_ID:
            return RtQueryUlong(Request,
                                ((ULONG)Adapter->PermanentAddress[0] << 16) |
                                    ((ULONG)Adapter->PermanentAddress[1] << 8) |
                                    Adapter->PermanentAddress[2]);

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "Ralink RT2870/RT2770 Wireless LAN USB Adapter";
            return RtQueryBuffer(Request, Description, sizeof(Description));
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
            return RtQueryUlong(Request, 0x00010000);

        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = (6 << 8) | 20;
            return RtQueryBuffer(Request, &Version, sizeof(Version));
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
            return RtQueryUlong(Request, Adapter->PacketFilter);

        case OID_GEN_LINK_SPEED:
            return RtQueryUlong(Request, Adapter->LinkState == RtLinkAssociated ? (ULONG)(RT_LINK_SPEED_BPS / 100) : 0);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            return RtQueryUlong(Request, Adapter->LinkState == RtLinkAssociated ? NdisMediaStateConnected : NdisMediaStateDisconnected);

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return RtQueryUlong(Request, RT_TX_URBS);

        case OID_GEN_MAC_OPTIONS:
            return RtQueryUlong(Request,
                                NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                    NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                    NDIS_MAC_OPTION_NO_LOOPBACK);

        case OID_GEN_XMIT_OK:
            return RtQueryCounter(Request, Adapter->TxOk);
        case OID_GEN_RCV_OK:
            return RtQueryCounter(Request, Adapter->RxOk);
        case OID_GEN_XMIT_ERROR:
            return RtQueryCounter(Request, Adapter->TxError);
        case OID_GEN_RCV_ERROR:
            return RtQueryCounter(Request, Adapter->RxError);
        case OID_GEN_RCV_NO_BUFFER:
            return RtQueryCounter(Request, 0);
        case OID_GEN_STATISTICS:
            return RtQueryStatistics(Adapter, Request);

        default:
            return RtDot11Query(Adapter, Request);
    }
}

static
ULONG
RtCipherFromAlgorithm(
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ ULONG KeyLength)
{
    switch (Algorithm)
    {
        case DOT11_CIPHER_ALGO_CCMP:
            return RT_CIPHER_AES;
        case DOT11_CIPHER_ALGO_TKIP:
            return RT_CIPHER_TKIP;
        case DOT11_CIPHER_ALGO_WEP40:
            return RT_CIPHER_WEP64;
        case DOT11_CIPHER_ALGO_WEP104:
            return RT_CIPHER_WEP128;
        case DOT11_CIPHER_ALGO_WEP:
            return KeyLength == 5 ? RT_CIPHER_WEP64 : RT_CIPHER_WEP128;
        default:
            return RT_CIPHER_NONE;
    }
}

static
BOOLEAN
RtDecodeKey(
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(Length) const UCHAR* Encoded,
    _In_ ULONG Length,
    _Out_writes_bytes_(32) PUCHAR Key,
    _Out_ PULONG KeyLength)
{
    if (Algorithm == DOT11_CIPHER_ALGO_CCMP)
    {
        PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Encoded;

        if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey) + 16 || Ccmp->ulCCMPKeyLength != 16)
            return FALSE;
        RtlCopyMemory(Key, Ccmp->ucCCMPKey, 16);
        *KeyLength = 16;
        return TRUE;
    }

    if (Algorithm == DOT11_CIPHER_ALGO_TKIP)
    {
        PDOT11_KEY_ALGO_TKIP_MIC Tkip = (PDOT11_KEY_ALGO_TKIP_MIC)Encoded;

        if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC, ucTKIPMICKeys) + 32 ||
            Tkip->ulTKIPKeyLength != 16 || Tkip->ulMICKeyLength != 16)
        {
            return FALSE;
        }
        RtlCopyMemory(Key, Tkip->ucTKIPMICKeys, 16);
        RtlCopyMemory(Key + 16, Tkip->ucTKIPMICKeys + 24, 8);
        RtlCopyMemory(Key + 24, Tkip->ucTKIPMICKeys + 16, 8);
        *KeyLength = 32;
        return TRUE;
    }

    if (Length == 5 || Length == 13)
    {
        RtlCopyMemory(Key, Encoded, Length);
        *KeyLength = Length;
        return TRUE;
    }

    return FALSE;
}

static
NDIS_STATUS
RtSetDefaultKey(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value = Buffer;
    UCHAR Key[32];
    ULONG KeyLength, Cipher;
    NTSTATUS Status;

    if (Length < FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) ||
        Length < FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + Value->usKeyLength)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    if (Value->AlgorithmId == DOT11_CIPHER_ALGO_NONE && !Value->bDelete)
        return NDIS_STATUS_NOT_SUPPORTED;

    if (Value->bDelete)
        return NDIS_STATUS_SUCCESS;

    if (Value->AlgorithmId == DOT11_CIPHER_ALGO_BIP)
    {
        PDOT11_KEY_ALGO_BIP Bip = (PDOT11_KEY_ALGO_BIP)Value->ucKey;
        ULONG i;

        if ((Value->uKeyIndex != 4 && Value->uKeyIndex != 5) ||
            Value->usKeyLength < FIELD_OFFSET(DOT11_KEY_ALGO_BIP, ucBIPKey) + 16 ||
            Bip->ulBIPKeyLength != 16)
        {
            return NDIS_STATUS_INVALID_DATA;
        }

        NdisAcquireSpinLock(&Adapter->Lock);
        RtlCopyMemory(Adapter->Igtk, Bip->ucBIPKey, 16);
        Adapter->IgtkKeyId = (USHORT)Value->uKeyIndex;
        Adapter->IgtkRxIpn = 0;
        for (i = 0; i < 6; i++)
            Adapter->IgtkRxIpn |= (ULONGLONG)Bip->ucIPN[i] << (8 * i);
        Adapter->IgtkValid = TRUE;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("RT2800USB: IGTK %lu installed\n", Value->uKeyIndex);
        return NDIS_STATUS_SUCCESS;
    }

    Cipher = RtCipherFromAlgorithm(Value->AlgorithmId, Value->usKeyLength);
    if (Cipher == RT_CIPHER_NONE ||
        !RtDecodeKey(Value->AlgorithmId, Value->ucKey, Value->usKeyLength, Key, &KeyLength))
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Adapter->SoftwareCrypto)
    {
        if (Cipher != RT_CIPHER_AES || KeyLength != 16 || Value->uKeyIndex >= 4)
        {
            RtlSecureZeroMemory(Key, sizeof(Key));
            return NDIS_STATUS_NOT_SUPPORTED;
        }
        NdisAcquireSpinLock(&Adapter->Lock);
        RtlCopyMemory(Adapter->GroupKey[Value->uKeyIndex], Key, 16);
        Adapter->GroupRxPn[Value->uKeyIndex] = 0;
        Adapter->GroupKeyValid[Value->uKeyIndex] = TRUE;
        NdisReleaseSpinLock(&Adapter->Lock);
        RtlSecureZeroMemory(Key, sizeof(Key));
        DPRINT1("RT2800USB: software group key %lu installed\n", Value->uKeyIndex);
        return NDIS_STATUS_SUCCESS;
    }

    Status = RtInstallKey(Adapter, FALSE, Value->uKeyIndex, Cipher, Key, KeyLength, NULL);
    RtlSecureZeroMemory(Key, sizeof(Key));
    DPRINT1("RT2800USB: group key %lu cipher %lu -> 0x%08lx\n", Value->uKeyIndex, Cipher, Status);
    return NT_SUCCESS(Status) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
}

static
NDIS_STATUS
RtSetKeyMappingKey(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_BYTE_ARRAY Array = Buffer;
    PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value;
    UCHAR Key[32];
    ULONG KeyLength, Cipher;
    NTSTATUS Status;

    if (Length < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) ||
        Length < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Array->uNumOfBytes ||
        Array->uNumOfBytes < FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey))
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)Array->ucBuffer;
    if (Array->uNumOfBytes < FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + Value->usKeyLength)
        return NDIS_STATUS_INVALID_LENGTH;

    if (Value->bDelete)
    {
        RtRemoveKeys(Adapter);
        return NDIS_STATUS_SUCCESS;
    }

    Cipher = RtCipherFromAlgorithm(Value->AlgorithmId, Value->usKeyLength);
    if (Cipher == RT_CIPHER_NONE ||
        !RtDecodeKey(Value->AlgorithmId, Value->ucKey, Value->usKeyLength, Key, &KeyLength))
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Adapter->SoftwareCrypto)
        Status = (Cipher == RT_CIPHER_AES && KeyLength == 16) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    else
        Status = RtInstallKey(Adapter, TRUE, 0, Cipher, Key, KeyLength, Value->PeerMacAddr);
    if (NT_SUCCESS(Status) && Cipher == RT_CIPHER_AES && KeyLength == 16)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        RtlCopyMemory(Adapter->MgmtTk, Key, 16);
        Adapter->MgmtTxPn = 0;
        Adapter->MgmtRxPn = 0;
        Adapter->DataRxPn = 0;
        Adapter->MgmtKeyValid = TRUE;
        NdisReleaseSpinLock(&Adapter->Lock);
    }
    RtlSecureZeroMemory(Key, sizeof(Key));
    DPRINT1("RT2800USB: pairwise key cipher %lu -> 0x%08lx\n", Cipher, Status);
    return NT_SUCCESS(Status) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
}

static
VOID
RtQueueCommand(
    _In_ PRT_ADAPTER Adapter,
    _In_ RT_COMMAND Command)
{
    NdisAcquireSpinLock(&Adapter->Lock);
    switch (Command)
    {
        case RtCommandScan:
            Adapter->ScanQueued = TRUE;
            break;
        case RtCommandConnect:
            Adapter->ConnectQueued = TRUE;
            Adapter->DisconnectQueued = FALSE;
            break;
        case RtCommandDisconnect:
            Adapter->DisconnectQueued = TRUE;
            Adapter->ConnectQueued = FALSE;
            break;
        default:
            break;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    RtWakeWorker(Adapter);
}

static
NDIS_STATUS
RtSetScanRequest(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_SCAN_REQUEST_V2 Scan = Buffer;
    PDOT11_SSID Ssid;

    if (Length < FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer))
        return NDIS_STATUS_INVALID_LENGTH;

    if (!Adapter->RadioOn)
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;

    Ssid = NULL;
    if (Scan->uNumOfdot11SSIDs >= 1)
    {
        if (Scan->udot11SSIDsOffset <= Length - FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) &&
            Length - FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) - Scan->udot11SSIDsOffset >= sizeof(DOT11_SSID))
        {
            Ssid = (PDOT11_SSID)(Scan->ucBuffer + Scan->udot11SSIDsOffset);
        }
        else if (Scan->udot11SSIDsOffset >= FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) &&
                 Scan->udot11SSIDsOffset <= Length &&
                 Length - Scan->udot11SSIDsOffset >= sizeof(DOT11_SSID))
        {
            Ssid = (PDOT11_SSID)((PUCHAR)Scan + Scan->udot11SSIDsOffset);
        }
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlZeroMemory(&Adapter->ScanSsid, sizeof(Adapter->ScanSsid));
    if (Ssid && Ssid->uSSIDLength <= DOT11_SSID_MAX_LENGTH)
        Adapter->ScanSsid = *Ssid;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtQueueCommand(Adapter, RtCommandScan);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
RtDot11Set(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    switch (Request->DATA.SET_INFORMATION.Oid)
    {
        case OID_DOT11_CURRENT_PHY_TYPE:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->CurrentPhyType = (DOT11_PHY_TYPE)*(PULONG)Buffer;
            break;

        case OID_DOT11_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->PacketFilter = *(PULONG)Buffer;
            break;

        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length < sizeof(DOT11_CURRENT_OPERATION_MODE))
                return NDIS_STATUS_INVALID_LENGTH;
            if (((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode != DOT11_OPERATION_MODE_EXTENSIBLE_STATION)
                return NDIS_STATUS_INVALID_DATA;
            Adapter->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            break;

        case OID_DOT11_NIC_POWER_STATE:
            if (Length < sizeof(BOOLEAN))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->RadioOn = *(PBOOLEAN)Buffer;
            if (!Adapter->RadioOn)
                RtQueueCommand(Adapter, RtCommandDisconnect);
            break;

        case OID_DOT11_RTS_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->RtsThreshold = *(PULONG)Buffer;
            break;

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->FragmentationThreshold = *(PULONG)Buffer;
            break;

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->AutoConfigEnabled = *(PULONG)Buffer != 0;
            break;

        case OID_DOT11_BEACON_PERIOD:
        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
        case OID_DOT11_OPERATIONAL_RATE_SET:
        case OID_DOT11_FLUSH_BSS_LIST:
            break;

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
        {
            PDOT11_AUTH_ALGORITHM_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) + sizeof(DOT11_AUTH_ALGORITHM) ||
                List->uNumOfEntries < 1)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (List->AlgorithmIds[0] != DOT11_AUTH_ALGO_80211_OPEN &&
                List->AlgorithmIds[0] != DOT11_AUTH_ALGO_RSNA_PSK &&
                List->AlgorithmIds[0] != DOT11_AUTH_ALGO_WPA3_SAE)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->AuthAlgorithm = List->AlgorithmIds[0];
            break;
        }

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
        {
            PDOT11_CIPHER_ALGORITHM_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_CIPHER_ALGORITHM_LIST, AlgorithmIds) + sizeof(DOT11_CIPHER_ALGORITHM) ||
                List->uNumOfEntries < 1)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (Request->DATA.SET_INFORMATION.Oid == OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM)
                Adapter->UnicastCipher = List->AlgorithmIds[0];
            else
                Adapter->MulticastCipher = List->AlgorithmIds[0];
            break;
        }

        case OID_DOT11_DESIRED_BSS_TYPE:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            if (*(PULONG)Buffer != dot11_BSS_type_infrastructure && *(PULONG)Buffer != dot11_BSS_type_any)
                return NDIS_STATUS_NOT_SUPPORTED;
            Adapter->DesiredBssType = (DOT11_BSS_TYPE)*(PULONG)Buffer;
            break;

        case OID_DOT11_DESIRED_SSID_LIST:
        {
            PDOT11_SSID_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_SSID_LIST, SSIDs))
                return NDIS_STATUS_INVALID_LENGTH;
            NdisAcquireSpinLock(&Adapter->Lock);
            if (List->uNumOfEntries >= 1 &&
                Length >= FIELD_OFFSET(DOT11_SSID_LIST, SSIDs) + sizeof(DOT11_SSID) &&
                List->SSIDs[0].uSSIDLength <= DOT11_SSID_MAX_LENGTH)
            {
                Adapter->DesiredSsid = List->SSIDs[0];
                Adapter->HaveDesiredSsid = List->SSIDs[0].uSSIDLength != 0;
            }
            else
            {
                RtlZeroMemory(&Adapter->DesiredSsid, sizeof(Adapter->DesiredSsid));
                Adapter->HaveDesiredSsid = FALSE;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }

        case OID_DOT11_DESIRED_BSSID_LIST:
        {
            PDOT11_BSSID_LIST List = Buffer;
            static const UCHAR Any[RT_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

            if (Length < FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs))
                return NDIS_STATUS_INVALID_LENGTH;
            NdisAcquireSpinLock(&Adapter->Lock);
            if (List->uNumOfEntries >= 1 &&
                Length >= FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs) + sizeof(DOT11_MAC_ADDRESS) &&
                !RtlEqualMemory(List->BSSIDs[0], Any, RT_ADDRESS_LENGTH))
            {
                RtlCopyMemory(Adapter->DesiredBssid, List->BSSIDs[0], RT_ADDRESS_LENGTH);
                Adapter->HaveDesiredBssid = TRUE;
            }
            else
            {
                Adapter->HaveDesiredBssid = FALSE;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }

        case OID_DOT11_SCAN_REQUEST:
            Status = RtSetScanRequest(Adapter, Buffer, Length);
            break;

        case OID_DOT11_CONNECT_REQUEST:
            if (!Adapter->RadioOn)
                return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
            RtQueueCommand(Adapter, RtCommandConnect);
            break;

        case OID_DOT11_DISCONNECT_REQUEST:
            RtQueueCommand(Adapter, RtCommandDisconnect);
            break;

        case OID_DOT11_RESET_REQUEST:
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->HaveDesiredSsid = FALSE;
            Adapter->HaveDesiredBssid = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            RtQueueCommand(Adapter, RtCommandDisconnect);
            break;

        case OID_DOT11_CIPHER_DEFAULT_KEY:
            Status = RtSetDefaultKey(Adapter, Buffer, Length);
            break;

        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
            Status = RtSetKeyMappingKey(Adapter, Buffer, Length);
            break;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Status == NDIS_STATUS_SUCCESS)
        Request->DATA.SET_INFORMATION.BytesRead = Length;

    return Status;
}

static
NDIS_STATUS
RtOidSet(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    switch (Request->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_WDI_SET_SAE_AUTH_PARAMS:
        {
            NWIFI_SAE_PARAMS Params;

            if (!NwifiSaeParseParams(Buffer, Length, &Params))
                return NDIS_STATUS_INVALID_DATA;
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->SaeParams = Params;
            Adapter->SaeParamsValid = TRUE;
            NdisReleaseSpinLock(&Adapter->Lock);
            RtlSecureZeroMemory(&Params, sizeof(Params));
            KeSetEvent(&Adapter->SaeEvent, IO_NETWORK_INCREMENT, FALSE);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_GEN_NETWORK_LAYER_ADDRESSES:
        case OID_PNP_SET_POWER:
        case OID_PNP_ADD_WAKE_UP_PATTERN:
        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return RtDot11Set(Adapter, Request);
    }
}

static
NDIS_STATUS
RtOidMethod(
    _In_ PRT_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.METHOD_INFORMATION.Oid)
    {
        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written, Needed;
            NDIS_STATUS Status;

            Status = RtQueryEnumBssList(Adapter,
                                        Request->DATA.METHOD_INFORMATION.InformationBuffer,
                                        Request->DATA.METHOD_INFORMATION.OutputBufferLength,
                                        &Written,
                                        &Needed);
            Request->DATA.METHOD_INFORMATION.BytesWritten = Written;
            Request->DATA.METHOD_INFORMATION.BytesNeeded = Needed;
            return Status;
        }

        case OID_DOT11_SCAN_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return RtSetScanRequest(Adapter,
                                    Request->DATA.METHOD_INFORMATION.InformationBuffer,
                                    Request->DATA.METHOD_INFORMATION.InputBufferLength);

        case OID_DOT11_CONNECT_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            RtQueueCommand(Adapter, RtCommandConnect);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_DISCONNECT_REQUEST:
        case OID_DOT11_RESET_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            RtQueueCommand(Adapter, RtCommandDisconnect);
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NTAPI
RtMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return RtOidQuery(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return RtOidSet(Adapter, OidRequest);

        case NdisRequestMethod:
            return RtOidMethod(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
RtMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
