/*
 * PROJECT:     LiberNT Native WiFi (802.11) stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Media-converting filter edge on Native 802.11 miniports: lifecycle, OIDs, status
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"
#include <dot11wificxintf.h>

#define NDEBUG
#include <debug.h>

#define NWIFI_SYNC_OID_SIGNATURE 0x4F49574EUL

typedef struct _NWIFI_SYNC_OID_CONTEXT
{
    ULONG Signature;
    NDIS_OID_REQUEST Request;
    NDIS_EVENT Event;
    NDIS_STATUS Status;
} NWIFI_SYNC_OID_CONTEXT, *PNWIFI_SYNC_OID_CONTEXT;

NDIS_STATUS
NwifiDoRequestEx(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed,
    _Out_opt_ PULONG BytesNeeded)
{
    NWIFI_SYNC_OID_CONTEXT Context;
    PNDIS_OID_REQUEST Request = &Context.Request;
    NDIS_STATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Adapter->FilterHandle != NULL);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Signature = NWIFI_SYNC_OID_SIGNATURE;
    Context.Status = NDIS_STATUS_PENDING;
    NdisInitializeEvent(&Context.Event);

    Request->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Request->RequestType = RequestType;

    if (RequestType == NdisRequestSetInformation)
    {
        Request->DATA.SET_INFORMATION.Oid = Oid;
        Request->DATA.SET_INFORMATION.InformationBuffer = Buffer;
        Request->DATA.SET_INFORMATION.InformationBufferLength = Length;
    }
    else if (RequestType == NdisRequestMethod)
    {
        Request->DATA.METHOD_INFORMATION.Oid = Oid;
        Request->DATA.METHOD_INFORMATION.InformationBuffer = Buffer;
        Request->DATA.METHOD_INFORMATION.InputBufferLength = Length;
        Request->DATA.METHOD_INFORMATION.OutputBufferLength = Length;
    }
    else
    {
        Request->DATA.QUERY_INFORMATION.Oid = Oid;
        Request->DATA.QUERY_INFORMATION.InformationBuffer = Buffer;
        Request->DATA.QUERY_INFORMATION.InformationBufferLength = Length;
    }

    Request->RequestId = &Context;

    Status = NdisFOidRequest(Adapter->FilterHandle, Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        /* NdisWaitEvent is alertable in this tree; the miniport owns the request until it completes */
        while (!NdisWaitEvent(&Context.Event, 0))
        {
        }
        Status = Context.Status;
    }

    if (BytesProcessed != NULL)
    {
        if (RequestType == NdisRequestSetInformation)
            *BytesProcessed = Request->DATA.SET_INFORMATION.BytesRead;
        else if (RequestType == NdisRequestMethod)
            *BytesProcessed = Request->DATA.METHOD_INFORMATION.BytesWritten;
        else
            *BytesProcessed = Request->DATA.QUERY_INFORMATION.BytesWritten;
    }

    if (BytesNeeded != NULL)
    {
        if (RequestType == NdisRequestSetInformation)
            *BytesNeeded = Request->DATA.SET_INFORMATION.BytesNeeded;
        else if (RequestType == NdisRequestMethod)
            *BytesNeeded = Request->DATA.METHOD_INFORMATION.BytesNeeded;
        else
            *BytesNeeded = Request->DATA.QUERY_INFORMATION.BytesNeeded;
    }

    return Status;
}

NDIS_STATUS
NwifiDoRequest(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed)
{
    return NwifiDoRequestEx(Adapter, RequestType, Oid, Buffer, Length, BytesProcessed, NULL);
}

VOID
NTAPI
NwifiFilterOidRequestComplete(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;
    PNWIFI_SYNC_OID_CONTEXT Context;

    Context = CONTAINING_RECORD(OidRequest, NWIFI_SYNC_OID_CONTEXT, Request);
    if (OidRequest->RequestId == (PVOID)Context && Context->Signature == NWIFI_SYNC_OID_SIGNATURE)
    {
        Context->Status = Status;
        NdisSetEvent(&Context->Event);
        return;
    }

    NdisFOidRequestComplete(Adapter->FilterHandle, OidRequest, Status);
}

static
VOID
NwifiDestroyAdapter(
    _In_ PNWIFI_ADAPTER Adapter)
{
    ASSERT(Adapter->PendingSends == 0);
    ASSERT(Adapter->PendingReceives == 0);

    if (Adapter->TxNblPool != NULL)
        NdisFreeNetBufferListPool(Adapter->TxNblPool);
    if (Adapter->RxNblPool != NULL)
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    NdisFreeSpinLock(&Adapter->DataLock);
    NwifiFree(Adapter);
}

static
NDIS_STATUS
NwifiCreateNblPools(
    _In_ PNWIFI_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = NWIFI_TAG;
    PoolParams.ContextSize = NWIFI_NBL_CONTEXT_SIZE;

    Adapter->TxNblPool = NdisAllocateNetBufferListPool(Adapter->FilterHandle, &PoolParams);
    if (Adapter->TxNblPool == NULL)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->FilterHandle, &PoolParams);
    if (Adapter->RxNblPool == NULL)
    {
        NdisFreeNetBufferListPool(Adapter->TxNblPool);
        Adapter->TxNblPool = NULL;
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
NwifiGuidFromDeviceName(
    _In_ PCUNICODE_STRING DeviceName,
    _Out_ GUID *Guid)
{
    UNICODE_STRING GuidString;
    USHORT Start;

    RtlZeroMemory(Guid, sizeof(*Guid));
    for (Start = 0; Start < DeviceName->Length / sizeof(WCHAR); Start++)
    {
        if (DeviceName->Buffer[Start] == L'{')
            break;
    }

    GuidString.Buffer = DeviceName->Buffer + Start;
    GuidString.Length = DeviceName->Length - Start * sizeof(WCHAR);
    GuidString.MaximumLength = GuidString.Length;
    if (GuidString.Length != 38 * sizeof(WCHAR) || !NT_SUCCESS(RtlGUIDFromString(&GuidString, Guid)))
        RtlZeroMemory(Guid, sizeof(*Guid));
}

NDIS_STATUS
NTAPI
NwifiFilterSetOptions(
    _In_ NDIS_HANDLE NdisFilterDriverHandle,
    _In_ NDIS_HANDLE FilterDriverContext)
{
    UNREFERENCED_PARAMETER(FilterDriverContext);

    /* Attach can run before NdisFRegisterFilterDriver returns the handle */
    gNwifi.FilterDriverHandle = NdisFilterDriverHandle;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NwifiFilterAttach(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NDIS_HANDLE FilterDriverContext,
    _In_ PNDIS_FILTER_ATTACH_PARAMETERS AttachParameters)
{
    NDIS_FILTER_ATTRIBUTES Attributes;
    PNWIFI_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(FilterDriverContext);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (AttachParameters->MiniportMediaType != NdisMediumNative802_11)
        return NDIS_STATUS_NOT_SUPPORTED;

    DPRINT1("NWIFI: attaching to '%wZ'\n", AttachParameters->BaseMiniportName);

    Adapter = (PNWIFI_ADAPTER)NwifiAllocate(sizeof(NWIFI_ADAPTER));
    if (Adapter == NULL)
        return NDIS_STATUS_RESOURCES;

    InitializeListHead(&Adapter->Link);
    NdisAllocateSpinLock(&Adapter->DataLock);
    Adapter->FilterHandle = NdisFilterHandle;
    Adapter->State = NwifiStatePaused;
    Adapter->CurrentLookahead = ETH_MAX_FRAME;
    Adapter->InterfaceIndex = (ULONG)InterlockedIncrement(&gNwifi.NextInterfaceIndex) - 1;
    if (AttachParameters->MacAddressLength == IEEE80211_ADDR_LEN)
        RtlCopyMemory(Adapter->MacAddress, AttachParameters->CurrentMacAddress, IEEE80211_ADDR_LEN);
    if (AttachParameters->BaseMiniportName != NULL)
        NwifiGuidFromDeviceName(AttachParameters->BaseMiniportName, &Adapter->AdapterGuid);
    Adapter->InterfaceGuid = Adapter->AdapterGuid;

    Status = NwifiCreateNblPools(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = NDIS_SIZEOF_FILTER_ATTRIBUTES_REVISION_1;
    Status = NdisFSetAttributes(NdisFilterHandle, (NDIS_HANDLE)Adapter, &Attributes);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = NwifiMsmCreate(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NwifiMsmCreate failed 0x%08X\n", Status);
        goto Fail;
    }

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    InsertTailList(&gNwifi.AdapterList, &Adapter->Link);
    NdisReleaseSpinLock(&gNwifi.AdapterLock);
    return NDIS_STATUS_SUCCESS;

Fail:
    NwifiDestroyAdapter(Adapter);
    return Status;
}

VOID
NTAPI
NwifiFilterDetach(
    _In_ NDIS_HANDLE FilterModuleContext)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Adapter->State != NwifiStateRunning);
    DPRINT1("NWIFI: detaching interface %u\n", Adapter->InterfaceIndex);

    Adapter->State = NwifiStateHalting;
    if (Adapter->Announced)
        NwifiMsmInterfaceRemoval(Adapter);
    NwifiMsmDestroy(Adapter);

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    RemoveEntryList(&Adapter->Link);
    NdisReleaseSpinLock(&gNwifi.AdapterLock);

    NwifiDestroyAdapter(Adapter);
}

static
VOID
NwifiPrepareLowerAdapter(
    _In_ PNWIFI_ADAPTER Adapter)
{
    DOT11_MAC_ADDRESS QueriedMac;
    DOT11_CURRENT_OPERATION_MODE OpMode;
    ULONG Dot11PacketFilter;
    ULONG Bytes = 0;
    NDIS_STATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Status = NwifiDoRequest(Adapter, NdisRequestQueryInformation, OID_DOT11_MAC_ADDRESS,
                            &QueriedMac, sizeof(QueriedMac), &Bytes);
    if (Status == NDIS_STATUS_SUCCESS && Bytes >= IEEE80211_ADDR_LEN)
        RtlCopyMemory(Adapter->MacAddress, QueriedMac, IEEE80211_ADDR_LEN);

    RtlZeroMemory(&OpMode, sizeof(OpMode));
    OpMode.uCurrentOpMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Status = NwifiDoRequest(Adapter, NdisRequestSetInformation, OID_DOT11_CURRENT_OPERATION_MODE,
                            &OpMode, sizeof(OpMode), &Bytes);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("NWIFI: set ExtSTA op-mode failed 0x%08X (continuing)\n", Status);

    /* The Ethernet packet filter is enforced here after translation; the radio receives all data frames */
    Dot11PacketFilter = NDIS_PACKET_TYPE_802_11_DIRECTED_DATA |
                        NDIS_PACKET_TYPE_802_11_MULTICAST_DATA |
                        NDIS_PACKET_TYPE_802_11_ALL_MULTICAST_DATA |
                        NDIS_PACKET_TYPE_802_11_BROADCAST_DATA;
    Status = NwifiDoRequest(Adapter, NdisRequestSetInformation, OID_GEN_CURRENT_PACKET_FILTER,
                            &Dot11PacketFilter, sizeof(Dot11PacketFilter), &Bytes);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("NWIFI: set lower packet filter failed 0x%08X\n", Status);

    DPRINT1("NWIFI: interface %u MAC %02x:%02x:%02x:%02x:%02x:%02x ready\n",
            Adapter->InterfaceIndex,
            Adapter->MacAddress[0], Adapter->MacAddress[1], Adapter->MacAddress[2],
            Adapter->MacAddress[3], Adapter->MacAddress[4], Adapter->MacAddress[5]);
}

static
VOID
NwifiCompleteRestart(
    _In_ PNWIFI_ADAPTER Adapter)
{
    BOOLEAN Connected;

    NdisAcquireSpinLock(&Adapter->DataLock);
    Adapter->State = NwifiStateRunning;
    Connected = Adapter->MediaConnected;
    NdisReleaseSpinLock(&Adapter->DataLock);

    if (!Adapter->Announced)
    {
        Adapter->Announced = TRUE;
        NwifiMsmInterfaceArrival(Adapter);
    }
    NwifiIndicateLinkState(Adapter, Connected);
}

static
VOID
NTAPI
NwifiRestartWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)WorkItemContext;

    ASSERT(Adapter != NULL);
    NwifiPrepareLowerAdapter(Adapter);
    Adapter->LowerPrepared = TRUE;
    NwifiCompleteRestart(Adapter);
    NdisFreeIoWorkItem(NdisIoWorkItemHandle);
    NdisFRestartComplete(Adapter->FilterHandle, NDIS_STATUS_SUCCESS);
}

NDIS_STATUS
NTAPI
NwifiFilterRestart(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNDIS_FILTER_RESTART_PARAMETERS RestartParameters)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;
    NDIS_HANDLE WorkItem;

    UNREFERENCED_PARAMETER(RestartParameters);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Adapter->State == NwifiStatePaused);

    if (!Adapter->LowerPrepared)
    {
        WorkItem = NdisAllocateIoWorkItem(gNwifi.FilterDriverHandle);
        if (WorkItem != NULL)
        {
            NdisQueueIoWorkItem(WorkItem, NwifiRestartWorker, Adapter);
            return NDIS_STATUS_PENDING;
        }
        NwifiPrepareLowerAdapter(Adapter);
        Adapter->LowerPrepared = TRUE;
    }

    NwifiCompleteRestart(Adapter);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NwifiFilterPause(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNDIS_FILTER_PAUSE_PARAMETERS PauseParameters)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    UNREFERENCED_PARAMETER(PauseParameters);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisAcquireSpinLock(&Adapter->DataLock);
    if (Adapter->State != NwifiStateHalting)
        Adapter->State = NwifiStatePaused;
    NdisReleaseSpinLock(&Adapter->DataLock);

    while (Adapter->PendingSends != 0 || Adapter->PendingReceives != 0)
    {
        NdisMSleep(1000);
    }
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NwifiFilterStatus(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    switch (StatusIndication->StatusCode)
    {
        case NDIS_STATUS_LINK_STATE:
            if (StatusIndication->StatusBuffer != NULL &&
                StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
            {
                PNDIS_LINK_STATE LinkState = (PNDIS_LINK_STATE)StatusIndication->StatusBuffer;
                BOOLEAN Connected = (LinkState->MediaConnectState == MediaConnectStateConnected);
                BOOLEAN UpperConnected;
                ULONG64 SpeedBps = 0;
                PNWIFI_MSM Msm = (PNWIFI_MSM)Adapter->MsmContext;

                if (Connected)
                {
                    SpeedBps = max(LinkState->XmitLinkSpeed, LinkState->RcvLinkSpeed);
                    if (SpeedBps == 0 || SpeedBps == NDIS_LINK_SPEED_UNKNOWN)
                        SpeedBps = NWIFI_DEFAULT_LINK_SPEED;
                }
                NdisAcquireSpinLock(&Adapter->DataLock);
                Adapter->LinkSpeedBps = SpeedBps;
                UpperConnected = Adapter->MediaConnected;
                NdisReleaseSpinLock(&Adapter->DataLock);

                if (Msm != NULL)
                {
                    NdisAcquireSpinLock(&Msm->Lock);
                    Msm->LinkSpeedKbps = (SpeedBps / 1000 > MAXULONG) ? MAXULONG : (ULONG)(SpeedBps / 1000);
                    NdisReleaseSpinLock(&Msm->Lock);
                }
                if (Connected && UpperConnected)
                    NwifiIndicateLinkState(Adapter, TRUE);
            }
            break;

        case NDIS_STATUS_MEDIA_CONNECT:
        case NDIS_STATUS_MEDIA_DISCONNECT:
            break;

        case NDIS_STATUS_DOT11_SCAN_CONFIRM:
        case NDIS_STATUS_DOT11_ASSOCIATION_START:
        case NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION:
        case NDIS_STATUS_DOT11_CONNECTION_START:
        case NDIS_STATUS_DOT11_CONNECTION_COMPLETION:
        case NDIS_STATUS_DOT11_ROAMING_START:
        case NDIS_STATUS_DOT11_ROAMING_COMPLETION:
        case NDIS_STATUS_DOT11_DISASSOCIATION:
        case NDIS_STATUS_DOT11_LINK_QUALITY:
            NwifiMsmIndicateStatus(Adapter, StatusIndication);
            break;

        case NDIS_STATUS_DOT11_PHY_STATE_CHANGED:
            break;

        case NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED:
            if (Adapter->MsmContext != NULL && StatusIndication->StatusBuffer != NULL)
            {
                NwifiSupplicantQueueSaeIndication((PNWIFI_MSM)Adapter->MsmContext,
                                                  StatusIndication->StatusBuffer,
                                                  StatusIndication->StatusBufferSize);
            }
            break;

        default:
            NdisFIndicateStatus(Adapter->FilterHandle, StatusIndication);
            break;
    }
}

NDIS_STATUS
NTAPI
NwifiFilterNetPnPEvent(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    return NdisFNetPnPEvent(Adapter->FilterHandle, NetPnPEventNotification);
}

VOID
NTAPI
NwifiFilterDevicePnPEventNotify(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    NdisFDevicePnPEventNotify(Adapter->FilterHandle, NetDevicePnPEvent);
}

VOID
NTAPI
NwifiFilterReceiveNetBufferLists(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    UNREFERENCED_PARAMETER(PortNumber);

    NwifiReceiveFromLower((PNWIFI_ADAPTER)FilterModuleContext, NetBufferLists, NumberOfNetBufferLists, ReceiveFlags);
}

VOID
NTAPI
NwifiFilterSendNetBufferListsComplete(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    NwifiSendCompleteFromLower((PNWIFI_ADAPTER)FilterModuleContext, NetBufferLists, SendCompleteFlags);
}

VOID
NTAPI
NwifiFilterCancelSendNetBufferLists(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ PVOID CancelId)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)FilterModuleContext;

    NdisFCancelSendNetBufferLists(Adapter->FilterHandle, CancelId);
}
