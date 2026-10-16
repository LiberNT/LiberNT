/*
 * PROJECT:     LiberNT VirtIO Network Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 OID requests and adapter attributes of the NetKVM driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ParaNdis6.h"
#include "ParaNdis-Oid.h"

#define OIDENTRY(oid, flags) { oid, 2, 0, 4, flags, NULL, NULL }
#define OIDENTRYPROC(oid, flags, setproc) { oid, 2, 0, 4, flags, setproc, NULL }

typedef struct _tagPowerWorkItem
{
    PPARANDIS_ADAPTER       pContext;
    NDIS_DEVICE_POWER_STATE state;
} tPowerWorkItem;

static NDIS_OID SupportedOids[] = {
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_VLAN_ID,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_RCV_CRC_ERROR,
    OID_GEN_TRANSMIT_QUEUE_LENGTH,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAC_OPTIONS,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_802_3_XMIT_DEFERRED,
    OID_802_3_XMIT_MAX_COLLISIONS,
    OID_802_3_RCV_OVERRUN,
    OID_802_3_XMIT_UNDERRUN,
    OID_802_3_XMIT_HEARTBEAT_FAILURE,
    OID_802_3_XMIT_TIMES_CRS_LOST,
    OID_802_3_XMIT_LATE_COLLISIONS,
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_PNP_ADD_WAKE_UP_PATTERN,
    OID_PNP_REMOVE_WAKE_UP_PATTERN,
    OID_PNP_ENABLE_WAKE_UP
};

static const tOidWhatToDo OidsDB[] =
{
    OIDENTRY(OID_GEN_SUPPORTED_LIST,                ohfQueryStat),
    OIDENTRY(OID_GEN_HARDWARE_STATUS,               ohfQuery),
    OIDENTRY(OID_GEN_MEDIA_SUPPORTED,               ohfQuery),
    OIDENTRY(OID_GEN_MEDIA_IN_USE,                  ohfQuery),
    OIDENTRY(OID_GEN_MAXIMUM_LOOKAHEAD,             ohfQuery),
    OIDENTRY(OID_GEN_MAXIMUM_FRAME_SIZE,            ohfQuery),
    OIDENTRY(OID_GEN_LINK_SPEED,                    ohfQuery),
    OIDENTRY(OID_GEN_TRANSMIT_BUFFER_SPACE,         ohfQuery),
    OIDENTRY(OID_GEN_RECEIVE_BUFFER_SPACE,          ohfQuery),
    OIDENTRY(OID_GEN_TRANSMIT_BLOCK_SIZE,           ohfQuery),
    OIDENTRY(OID_GEN_RECEIVE_BLOCK_SIZE,            ohfQuery),
    OIDENTRY(OID_GEN_VENDOR_ID,                     ohfQueryStat),
    OIDENTRY(OID_GEN_VENDOR_DESCRIPTION,            ohfQuery),
    OIDENTRYPROC(OID_GEN_CURRENT_PACKET_FILTER,     ohfQuerySet, ParaNdis_OnSetPacketFilter),
    OIDENTRYPROC(OID_GEN_CURRENT_LOOKAHEAD,         ohfQuerySet, ParaNdis_OnSetLookahead),
    OIDENTRY(OID_GEN_DRIVER_VERSION,                ohfQuery),
    OIDENTRY(OID_GEN_MAXIMUM_TOTAL_SIZE,            ohfQuery),
    OIDENTRY(OID_GEN_MAC_OPTIONS,                   ohfQuery),
    OIDENTRY(OID_GEN_MEDIA_CONNECT_STATUS,          ohfQuery),
    OIDENTRY(OID_GEN_MAXIMUM_SEND_PACKETS,          ohfQuery),
    OIDENTRY(OID_GEN_VENDOR_DRIVER_VERSION,         ohfQuery),
    OIDENTRY(OID_GEN_PHYSICAL_MEDIUM,               ohfQuery),
    OIDENTRY(OID_GEN_XMIT_OK,                       ohfQuery3264),
    OIDENTRY(OID_GEN_RCV_OK,                        ohfQuery3264),
    OIDENTRY(OID_GEN_XMIT_ERROR,                    ohfQuery3264),
    OIDENTRY(OID_GEN_RCV_ERROR,                     ohfQuery3264),
    OIDENTRY(OID_GEN_RCV_NO_BUFFER,                 ohfQuery3264),
    OIDENTRY(OID_GEN_RCV_CRC_ERROR,                 ohfQuery3264),
    OIDENTRY(OID_GEN_TRANSMIT_QUEUE_LENGTH,         ohfQuery),
    OIDENTRY(OID_GEN_STATISTICS,                    ohfQueryStat),
    OIDENTRY(OID_GEN_INTERRUPT_MODERATION,          ohfQuery),
    OIDENTRY(OID_PNP_CAPABILITIES,                  ohfQuery),
    OIDENTRY(OID_PNP_QUERY_POWER,                   ohfQuery),
    OIDENTRY(OID_802_3_PERMANENT_ADDRESS,           ohfQueryStat),
    OIDENTRY(OID_802_3_CURRENT_ADDRESS,             ohfQueryStat),
    OIDENTRY(OID_802_3_MAXIMUM_LIST_SIZE,           ohfQuery),
    OIDENTRY(OID_802_3_MAC_OPTIONS,                 ohfQuery),
    OIDENTRY(OID_802_3_RCV_ERROR_ALIGNMENT,         ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_ONE_COLLISION,          ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_MORE_COLLISIONS,        ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_DEFERRED,               ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_MAX_COLLISIONS,         ohfQuery3264),
    OIDENTRY(OID_802_3_RCV_OVERRUN,                 ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_UNDERRUN,               ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_HEARTBEAT_FAILURE,      ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_TIMES_CRS_LOST,         ohfQuery3264),
    OIDENTRY(OID_802_3_XMIT_LATE_COLLISIONS,        ohfQuery3264),
    OIDENTRYPROC(OID_PNP_ADD_WAKE_UP_PATTERN,       ohfSet,                 ParaNdis_OnAddWakeupPattern),
    OIDENTRYPROC(OID_PNP_REMOVE_WAKE_UP_PATTERN,    ohfSet,                 ParaNdis_OnRemoveWakeupPattern),
    OIDENTRYPROC(OID_PNP_ENABLE_WAKE_UP,            ohfQuerySet,            ParaNdis_OnEnableWakeup),
    OIDENTRYPROC(OID_PNP_SET_POWER,                 ohfSet | ohfSetMoreOK,  ParaNdis_OnSetPower),
    OIDENTRYPROC(OID_802_3_MULTICAST_LIST,          ohfQuerySet,            ParaNdis_OnOidSetMulticastList),
    OIDENTRYPROC(OID_GEN_VLAN_ID,                   ohfQuerySet,            ParaNdis_OnSetVlanId),
    OIDENTRY(0,                                     0),
};

void ParaNdis_GetSupportedOid(PVOID *pOidsArray, PULONG pLength)
{
    *pOidsArray = SupportedOids;
    *pLength = sizeof(SupportedOids);
}

static NDIS_STATUS ParaNdis_OidQuery(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    PVOID pInfo = NULL;
    ULONG ulSize = 0;
    union
    {
        ULONG ul;
        NDIS_STATISTICS_INFO Statistics;
        NDIS_INTERRUPT_MODERATION_PARAMETERS Moderation;
    } u;

    switch (pOid->Oid)
    {
        case OID_GEN_LINK_SPEED:
            u.ul = (ULONG)(PARANDIS_FORMAL_LINK_SPEED / 100);
            pInfo = &u.ul;
            ulSize = sizeof(u.ul);
            break;

        case OID_GEN_STATISTICS:
            u.Statistics = pContext->Statistics;
            u.Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            u.Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            u.Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            u.Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_XMIT;
            pInfo = &u.Statistics;
            ulSize = sizeof(u.Statistics);
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            NdisZeroMemory(&u.Moderation, sizeof(u.Moderation));
            u.Moderation.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            u.Moderation.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            u.Moderation.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            u.Moderation.InterruptModeration = NdisInterruptModerationNotSupported;
            pInfo = &u.Moderation;
            ulSize = sizeof(u.Moderation);
            break;

        default:
            return ParaNdis_OidQueryCommon(pContext, pOid);
    }

    return ParaNdis_OidQueryCopy(pOid, pInfo, ulSize, FALSE);
}

static VOID CompletePendingOid(PARANDIS_ADAPTER *pContext, NDIS_STATUS status)
{
    PNDIS_OID_REQUEST request;

    request = InterlockedExchangePointer((PVOID *)&pContext->PendingOidRequest, NULL);
    if (request)
        NdisMOidRequestComplete(pContext->MiniportHandle, request, status);
}

static VOID NTAPI OnSetPowerWorkItem(PVOID WorkItemContext, NDIS_HANDLE NdisIoWorkItemHandle)
{
    tPowerWorkItem *pwi = (tPowerWorkItem *)WorkItemContext;
    PARANDIS_ADAPTER *pContext = pwi->pContext;
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    if (pwi->state == NdisDeviceStateD0)
        status = ParaNdis_PowerOn(pContext);
    else
        ParaNdis_PowerOff(pContext);

    NdisFreeIoWorkItem(NdisIoWorkItemHandle);
    NdisFreeMemory(pwi, 0, 0);
    ParaNdis_DebugHistory(pContext, hopOidRequest, NULL, OID_PNP_SET_POWER, 0, 2);
    CompletePendingOid(pContext, status);
}

NDIS_STATUS ParaNdis_OnSetPower(PARANDIS_ADAPTER *pContext, tOidDesc *pOid)
{
    NDIS_DEVICE_POWER_STATE newState;
    NDIS_HANDLE workItem;
    tPowerWorkItem *pwi;
    NDIS_STATUS status;

    status = ParaNdis_OidSetCopy(pOid, &newState, sizeof(newState));
    if (status != NDIS_STATUS_SUCCESS)
        return status;

    pwi = ParaNdis_AllocateMemory(pContext, sizeof(tPowerWorkItem));
    if (!pwi)
        return NDIS_STATUS_RESOURCES;

    workItem = NdisAllocateIoWorkItem(pContext->MiniportHandle);
    if (!workItem)
    {
        NdisFreeMemory(pwi, 0, 0);
        return NDIS_STATUS_RESOURCES;
    }

    pwi->pContext = pContext;
    pwi->state = newState;
    NdisQueueIoWorkItem(workItem, OnSetPowerWorkItem, pwi);
    return NDIS_STATUS_PENDING;
}

NDIS_STATUS NTAPI ParaNdis6_OidRequest(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_OID_REQUEST OidRequest)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;
    NDIS_STATUS status = NDIS_STATUS_NOT_SUPPORTED;
    tOidWhatToDo Rules;
    tOidDesc oid;

    NdisZeroMemory(&oid, sizeof(oid));
    oid.Reserved = OidRequest;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            oid.Oid = OidRequest->DATA.QUERY_INFORMATION.Oid;
            oid.InformationBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
            oid.InformationBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
            oid.pBytesWritten = (PUINT)&OidRequest->DATA.QUERY_INFORMATION.BytesWritten;
            oid.pBytesNeeded = (PUINT)&OidRequest->DATA.QUERY_INFORMATION.BytesNeeded;
            oid.pBytesRead = oid.pBytesWritten;
            *oid.pBytesWritten = 0;
            *oid.pBytesNeeded = 0;
            ParaNdis_GetOidSupportRules(oid.Oid, &Rules, OidsDB);
            oid.ulToDoFlags = Rules.Flags;
            if (pContext->bSurprizeRemoved)
                status = NDIS_STATUS_NOT_ACCEPTED;
            else if (Rules.Flags & ohfQuery)
                status = ParaNdis_OidQuery(pContext, &oid);
            break;

        case NdisRequestSetInformation:
            oid.Oid = OidRequest->DATA.SET_INFORMATION.Oid;
            oid.InformationBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
            oid.InformationBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
            oid.pBytesRead = (PUINT)&OidRequest->DATA.SET_INFORMATION.BytesRead;
            oid.pBytesNeeded = (PUINT)&OidRequest->DATA.SET_INFORMATION.BytesNeeded;
            oid.pBytesWritten = oid.pBytesRead;
            *oid.pBytesRead = 0;
            *oid.pBytesNeeded = 0;
            ParaNdis_GetOidSupportRules(oid.Oid, &Rules, OidsDB);
            oid.ulToDoFlags = Rules.Flags;
            if (pContext->bSurprizeRemoved)
            {
                status = NDIS_STATUS_NOT_ACCEPTED;
            }
            else if ((Rules.Flags & ohfSet) && Rules.OidSetProc)
            {
                InterlockedExchangePointer((PVOID *)&pContext->PendingOidRequest, OidRequest);
                status = Rules.OidSetProc(pContext, &oid);
                if (status != NDIS_STATUS_PENDING)
                    InterlockedExchangePointer((PVOID *)&pContext->PendingOidRequest, NULL);
            }
            break;

        default:
            break;
    }

    ParaNdis_DebugHistory(pContext, hopOidRequest, NULL, oid.Oid, status, 0);
    return status;
}

VOID NTAPI ParaNdis6_CancelOidRequest(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

NDIS_STATUS ParaNdis6_SetGeneralAttributes(PARANDIS_ADAPTER *pContext)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES ga;
    ULONG options;

    options = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
              NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
              NDIS_MAC_OPTION_NO_LOOPBACK;
    if (IsPrioritySupported(pContext))
        options |= NDIS_MAC_OPTION_8021P_PRIORITY;
    if (IsVlanSupported(pContext))
        options |= NDIS_MAC_OPTION_8021Q_VLAN;

    NdisZeroMemory(&ga, sizeof(ga));
    ga.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    ga.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    ga.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    ga.MediaType = NdisMedium802_3;
    ga.PhysicalMediumType = NdisPhysicalMedium802_3;
    ga.MtuSize = pContext->MaxPacketSize.nMaxDataSize;
    ga.MaxXmitLinkSpeed = PARANDIS_MAXIMUM_TRANSMIT_SPEED;
    ga.MaxRcvLinkSpeed = PARANDIS_MAXIMUM_RECEIVE_SPEED;
    if (pContext->bConnected)
    {
        ga.XmitLinkSpeed = PARANDIS_FORMAL_LINK_SPEED;
        ga.RcvLinkSpeed = PARANDIS_FORMAL_LINK_SPEED;
        ga.MediaConnectState = MediaConnectStateConnected;
        ga.MediaDuplexState = MediaDuplexStateFull;
    }
    else
    {
        ga.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        ga.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        ga.MediaConnectState = MediaConnectStateDisconnected;
        ga.MediaDuplexState = MediaDuplexStateUnknown;
    }
    ga.LookaheadSize = pContext->MaxPacketSize.nMaxFullSizeOS;
    ga.MacOptions = options;
    ga.SupportedPacketFilters = PARANDIS_PACKET_FILTERS;
    ga.MaxMulticastListSize = PARANDIS_MULTICAST_LIST_SIZE;
    ga.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    NdisMoveMemory(ga.PermanentMacAddress, pContext->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(ga.CurrentMacAddress, pContext->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    ga.AccessType = NET_IF_ACCESS_BROADCAST;
    ga.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    ga.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    ga.IfType = IF_TYPE_ETHERNET_CSMACD;
    ga.IfConnectorPresent = TRUE;
    ga.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                             NDIS_STATISTICS_RCV_OK_SUPPORTED |
                             NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                             NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                             NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED |
                             NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED |
                             NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED |
                             NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED |
                             NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED |
                             NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED |
                             NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED |
                             NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED |
                             NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
                             NDIS_STATISTICS_BYTES_XMIT_SUPPORTED |
                             NDIS_STATISTICS_RCV_DISCARDS_SUPPORTED |
                             NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED |
                             NDIS_STATISTICS_XMIT_DISCARDS_SUPPORTED;
    ga.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    ga.SupportedOidList = SupportedOids;
    ga.SupportedOidListLength = sizeof(SupportedOids);

    return NdisMSetMiniportAttributes(pContext->MiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&ga);
}
