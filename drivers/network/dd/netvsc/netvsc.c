/*
 * PROJECT:     LiberNT Hyper-V Network Adapter Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 miniport for the Hyper-V synthetic network adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "netvsc.h"

#define NDEBUG
#include <debug.h>

static NDIS_HANDLE NvscDriverHandle;

NDIS_OID NvscSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
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
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION
};

ULONG NvscSupportedOidCount = RTL_NUMBER_OF(NvscSupportedOids);

static
VOID
NvscAppendNbl(
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _In_ PNET_BUFFER_LIST Nbl)
{
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    if (*Tail != NULL)
        NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
    else
        *Head = Nbl;
    *Tail = Nbl;
}

static
PNET_BUFFER_LIST
NvscFailNblChain(
    _In_opt_ PNET_BUFFER_LIST Nbl,
    _In_ NDIS_STATUS Status)
{
    PNET_BUFFER_LIST Current;

    for (Current = Nbl; Current != NULL; Current = NET_BUFFER_LIST_NEXT_NBL(Current))
        NET_BUFFER_LIST_STATUS(Current) = Status;

    return Nbl;
}

VOID
NvscCompleteSends(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST CompleteHead,
    _In_ BOOLEAN DispatchLevel)
{
    if (CompleteHead != NULL)
    {
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        CompleteHead,
                                        DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

VOID
NvscIndicateLinkState(
    _In_ PNETVSC_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;

    NdisZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    if (Adapter->MediaState == NdisMediaStateConnected)
    {
        LinkState.MediaConnectState = MediaConnectStateConnected;
        LinkState.MediaDuplexState = MediaDuplexStateFull;
        LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
        LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    }
    else
    {
        LinkState.MediaConnectState = MediaConnectStateDisconnected;
        LinkState.MediaDuplexState = MediaDuplexStateUnknown;
        LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    NdisZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

static
VOID
NTAPI
MiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;
    BOOLEAN DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, Next;

    UNREFERENCED_PARAMETER(PortNumber);

    NdisAcquireSpinLock(&Adapter->Lock);

    if (Adapter->Paused || !Adapter->Ready)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        NvscCompleteSends(Adapter,
                          NvscFailNblChain(NetBufferLists,
                                           Adapter->Paused ? NDIS_STATUS_PAUSED : NDIS_STATUS_RESET_IN_PROGRESS),
                          DispatchLevel);
        return;
    }

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        Nbl->MiniportReserved[0] = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NvscAppendNbl(&Adapter->SendQueueHead, &Adapter->SendQueueTail, Nbl);
    }

    NvscProcessSendQueue(Adapter, &CompleteHead, &CompleteTail);
    NdisReleaseSpinLock(&Adapter->Lock);

    NvscCompleteSends(Adapter, CompleteHead, DispatchLevel);
}

static
VOID
NTAPI
MiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, Next, KeepHead = NULL, KeepTail = NULL;

    NdisAcquireSpinLock(&Adapter->Lock);

    Nbl = Adapter->SendQueueHead;
    if (Nbl != NULL && Nbl->MiniportReserved[0] != NET_BUFFER_LIST_FIRST_NB(Nbl))
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NvscAppendNbl(&KeepHead, &KeepTail, Nbl);
        Nbl = Next;
    }

    for (; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
            NvscAppendNbl(&CompleteHead, &CompleteTail, Nbl);
        }
        else
        {
            NvscAppendNbl(&KeepHead, &KeepTail, Nbl);
        }
    }

    Adapter->SendQueueHead = KeepHead;
    Adapter->SendQueueTail = KeepTail;
    NdisReleaseSpinLock(&Adapter->Lock);

    NvscCompleteSends(Adapter, CompleteHead, FALSE);
}

static
VOID
NTAPI
MiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;
    BOOLEAN CompletePause = FALSE;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&Adapter->Lock);
    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
        Adapter->ReceiveNblsOutstanding--;
    }

    if (Adapter->PausePending && Adapter->ReceiveNblsOutstanding == 0)
    {
        Adapter->PausePending = FALSE;
        CompletePause = TRUE;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (CompletePause)
        NdisMPauseComplete(Adapter->MiniportAdapterHandle);
}

static
NDIS_STATUS
NTAPI
MiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Pending;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(PauseParameters);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Paused = TRUE;
    Pending = Adapter->SendQueueHead;
    Adapter->SendQueueHead = NULL;
    Adapter->SendQueueTail = NULL;
    if (Adapter->ReceiveNblsOutstanding != 0)
    {
        Adapter->PausePending = TRUE;
        Status = NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    NvscCompleteSends(Adapter, NvscFailNblChain(Pending, NDIS_STATUS_PAUSED), FALSE);
    return Status;
}

static
NDIS_STATUS
NTAPI
MiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    NvscIndicateLinkState(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NvscFreeReceiveNbls(
    _Inout_ PNETVSC_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;
    PMDL Mdl;
    ULONG Index;

    for (Index = 0; Index < NETVSC_RECEIVE_NBL_COUNT; Index++)
    {
        Nbl = Adapter->ReceiveNbls[Index];
        if (Nbl == NULL)
            continue;

        Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
        if (Nbl->MiniportReserved[0] != NULL)
            NdisFreeMemory(Nbl->MiniportReserved[0], 0, 0);
        NdisFreeNetBufferList(Nbl);
        if (Mdl != NULL)
            NdisFreeMdl(Mdl);
        Adapter->ReceiveNbls[Index] = NULL;
    }
    Adapter->FreeReceiveNbls = NULL;

    if (Adapter->ReceiveNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->ReceiveNblPool);
        Adapter->ReceiveNblPool = NULL;
    }
}

static
NDIS_STATUS
NvscAllocateReceiveNbls(
    _Inout_ PNETVSC_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PNET_BUFFER_LIST Nbl;
    PUCHAR Buffer;
    PMDL Mdl;
    ULONG Index;

    NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = NETVSC_TAG;

    Adapter->ReceiveNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportAdapterHandle, &PoolParameters);
    if (Adapter->ReceiveNblPool == NULL)
        return NDIS_STATUS_RESOURCES;

    for (Index = 0; Index < NETVSC_RECEIVE_NBL_COUNT; Index++)
    {
        Buffer = NdisAllocateMemoryWithTagPriority(Adapter->MiniportAdapterHandle,
                                                   NETVSC_FRAME_BUFFER_SIZE,
                                                   NETVSC_TAG,
                                                   NormalPoolPriority);
        if (Buffer == NULL)
            return NDIS_STATUS_RESOURCES;

        Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Buffer, NETVSC_FRAME_BUFFER_SIZE);
        if (Mdl == NULL)
        {
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->ReceiveNblPool, 0, 0, Mdl, 0,
                                                    NETVSC_FRAME_BUFFER_SIZE);
        if (Nbl == NULL)
        {
            NdisFreeMdl(Mdl);
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl->MiniportReserved[0] = Buffer;
        Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
        Adapter->ReceiveNbls[Index] = Nbl;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
MiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    NvscStopDevice(Adapter);
    NvscFreeReceiveNbls(Adapter);
    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeMemory(Adapter, 0, 0);
}

static
NDIS_STATUS
NvscSetRegistrationAttributes(
    _In_ PNETVSC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;

    NdisZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE;
    Registration.InterfaceType = NdisInterfaceInternal;

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
}

static
NDIS_STATUS
NvscSetGeneralAttributes(
    _In_ PNETVSC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    NdisZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMedium802_3;
    General.MtuSize = NETVSC_MTU;
    General.MaxXmitLinkSpeed = Adapter->LinkSpeed;
    General.MaxRcvLinkSpeed = Adapter->LinkSpeed;
    if (Adapter->MediaState == NdisMediaStateConnected)
    {
        General.XmitLinkSpeed = Adapter->LinkSpeed;
        General.RcvLinkSpeed = Adapter->LinkSpeed;
        General.MediaConnectState = MediaConnectStateConnected;
        General.MediaDuplexState = MediaDuplexStateFull;
    }
    else
    {
        General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        General.MediaConnectState = MediaConnectStateDisconnected;
        General.MediaDuplexState = MediaDuplexStateUnknown;
    }
    General.LookaheadSize = NETVSC_MTU;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST |
                                     NDIS_PACKET_TYPE_PROMISCUOUS;
    General.MaxMulticastListSize = NETVSC_MAX_MULTICAST;
    General.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    NdisMoveMemory(General.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(General.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    General.RecvScaleCapabilities = NULL;
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    General.IfConnectorPresent = TRUE;
    General.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                  NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                  NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;
    General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    General.AutoNegotiationFlags = NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
    General.SupportedOidList = NvscSupportedOids;
    General.SupportedOidListLength = NvscSupportedOidCount * sizeof(NDIS_OID);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
NDIS_STATUS
NTAPI
MiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PDEVICE_OBJECT PhysicalDeviceObject = NULL;
    PNETVSC_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    Adapter = NdisAllocateMemoryWithTagPriority(MiniportAdapterHandle, sizeof(*Adapter), NETVSC_TAG,
                                                NormalPoolPriority);
    if (Adapter == NULL)
        return NDIS_STATUS_RESOURCES;

    NdisZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    Adapter->Paused = TRUE;
    NdisAllocateSpinLock(&Adapter->Lock);
    NdisInitializeEvent(&Adapter->NvspEvent);
    NdisInitializeEvent(&Adapter->RndisEvent);

    NdisMGetDeviceProperty(MiniportAdapterHandle, &PhysicalDeviceObject, NULL, NULL, NULL, NULL);
    Adapter->Channel = (PhysicalDeviceObject != NULL) ? VmbChannelFromDevice(PhysicalDeviceObject) : NULL;
    if (Adapter->Channel == NULL)
    {
        DPRINT1("NETVSC: no VMBus channel behind PDO %p\n", PhysicalDeviceObject);
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Cleanup;
    }

    Status = NvscSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    Status = NvscStartDevice(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    Status = NvscSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    Status = NvscAllocateReceiveNbls(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    return NDIS_STATUS_SUCCESS;

Cleanup:
    MiniportHaltEx(Adapter, NdisHaltDeviceInitializationFailed);
    return Status;
}

static
NDIS_STATUS
NvscQueryInformation(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _Out_writes_bytes_(InformationBufferLength) PVOID InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _Out_ PULONG BytesWritten,
    _Out_ PULONG BytesNeeded)
{
    static const NDIS_MEDIUM Medium = NdisMedium802_3;
    static const UCHAR VendorDescription[] = "Microsoft Hyper-V Network Adapter";
    static const USHORT DriverVersion = (NDIS_MINIPORT_MAJOR_VERSION << 8) + NDIS_MINIPORT_MINOR_VERSION;
    NDIS_INTERRUPT_MODERATION_PARAMETERS Moderation;
    NDIS_STATISTICS_INFO Statistics;
    ULONG64 Generic64;
    ULONG Generic;
    const VOID *Source = &Generic;
    ULONG Length = sizeof(ULONG);
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    NdisAcquireSpinLock(&Adapter->Lock);

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            Source = NvscSupportedOids;
            Length = NvscSupportedOidCount * sizeof(NDIS_OID);
            break;

        case OID_GEN_STATISTICS:
            NdisZeroMemory(&Statistics, sizeof(Statistics));
            Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            Statistics.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                             NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                             NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                                             NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                                             NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            Statistics.ifInDiscards = Adapter->ReceiveNoBuffer + Adapter->ReceiveError;
            Statistics.ifInErrors = Adapter->ReceiveError;
            Statistics.ifHCInUcastPkts = Adapter->ReceiveOk;
            Statistics.ifHCOutUcastPkts = Adapter->TransmitOk;
            Statistics.ifOutErrors = Adapter->TransmitError;
            Source = &Statistics;
            Length = sizeof(Statistics);
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            NdisZeroMemory(&Moderation, sizeof(Moderation));
            Moderation.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Moderation.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Moderation.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Moderation.InterruptModeration = NdisInterruptModerationNotSupported;
            Source = &Moderation;
            Length = sizeof(Moderation);
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            Generic = Adapter->PacketFilter;
            break;

        case OID_GEN_HARDWARE_STATUS:
            Generic = Adapter->Ready ? NdisHardwareStatusReady : NdisHardwareStatusNotReady;
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Source = &Medium;
            Length = sizeof(Medium);
            break;

        case OID_GEN_RECEIVE_BLOCK_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Generic = NETVSC_MTU;
            break;

        case OID_GEN_LINK_SPEED:
            Generic = (ULONG)(Adapter->LinkSpeed / 100);
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            Generic = Adapter->SendSectionCount * Adapter->SendSectionSize;
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            Generic = NETVSC_RECEIVE_BUFFER_SIZE;
            break;

        case OID_GEN_VENDOR_ID:
            Generic = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                      ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                      Adapter->PermanentMacAddress[2];
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            Source = VendorDescription;
            Length = sizeof(VendorDescription);
            break;

        case OID_GEN_VENDOR_DRIVER_VERSION:
            Generic = Adapter->NvspVersion;
            break;

        case OID_GEN_DRIVER_VERSION:
            Source = &DriverVersion;
            Length = sizeof(DriverVersion);
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            Generic = NETVSC_MAX_FRAME_SIZE;
            break;

        case OID_GEN_MAC_OPTIONS:
            Generic = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                      NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                      NDIS_MAC_OPTION_NO_LOOPBACK;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Generic = Adapter->MediaState;
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            Generic = Adapter->SendSectionCount;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            Source = Adapter->CurrentMacAddress;
            Length = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            Source = Adapter->PermanentMacAddress;
            Length = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MULTICAST_LIST:
            Source = Adapter->MulticastList;
            Length = Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Generic = NETVSC_MAX_MULTICAST;
            break;

        case OID_GEN_PHYSICAL_MEDIUM:
            Generic = NdisPhysicalMedium802_3;
            break;

        case OID_GEN_XMIT_OK:
        case OID_GEN_RCV_OK:
        case OID_GEN_XMIT_ERROR:
        case OID_GEN_RCV_ERROR:
        case OID_GEN_RCV_NO_BUFFER:
            Generic64 = (Oid == OID_GEN_XMIT_OK) ? Adapter->TransmitOk :
                        (Oid == OID_GEN_RCV_OK) ? Adapter->ReceiveOk :
                        (Oid == OID_GEN_XMIT_ERROR) ? Adapter->TransmitError :
                        (Oid == OID_GEN_RCV_ERROR) ? Adapter->ReceiveError : Adapter->ReceiveNoBuffer;
            Generic = (ULONG)Generic64;
            if (InformationBufferLength >= sizeof(ULONG64))
            {
                Source = &Generic64;
                Length = sizeof(ULONG64);
            }
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (Length > InformationBufferLength)
        {
            *BytesNeeded = Length;
            *BytesWritten = 0;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, Source, Length);
            *BytesWritten = Length;
            *BytesNeeded = Length;
        }
    }
    else
    {
        *BytesWritten = 0;
        *BytesNeeded = 0;
    }

    NdisReleaseSpinLock(&Adapter->Lock);
    return Status;
}

static
NDIS_STATUS
NvscSetInformation(
    _In_ PNETVSC_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID InformationBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG InformationBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PULONG BytesRead = (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesRead;
    PULONG BytesNeeded = (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesNeeded;
    ULONG Filter;
    ULONG HostFilter;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            NdisMoveMemory(&Filter, InformationBuffer, sizeof(ULONG));
            if (Filter & ~(NDIS_PACKET_TYPE_DIRECTED |
                           NDIS_PACKET_TYPE_MULTICAST |
                           NDIS_PACKET_TYPE_ALL_MULTICAST |
                           NDIS_PACKET_TYPE_BROADCAST |
                           NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            *BytesRead = sizeof(ULONG);
            Adapter->PacketFilter = Filter;
            HostFilter = Filter;
            if (HostFilter & NDIS_PACKET_TYPE_MULTICAST)
                HostFilter |= NDIS_PACKET_TYPE_ALL_MULTICAST;

            return NvscRndisSetPacketFilter(Adapter,
                                            HostFilter,
                                            KeGetCurrentIrql() == PASSIVE_LEVEL ? NULL : OidRequest);

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if (InformationBufferLength % ETH_LENGTH_OF_ADDRESS)
                return NDIS_STATUS_INVALID_LENGTH;

            if (InformationBufferLength / ETH_LENGTH_OF_ADDRESS > NETVSC_MAX_MULTICAST)
            {
                *BytesNeeded = NETVSC_MAX_MULTICAST * ETH_LENGTH_OF_ADDRESS;
                return NDIS_STATUS_MULTICAST_FULL;
            }

            NdisAcquireSpinLock(&Adapter->Lock);
            NdisMoveMemory(Adapter->MulticastList, InformationBuffer, InformationBufferLength);
            Adapter->MulticastCount = InformationBufferLength / ETH_LENGTH_OF_ADDRESS;
            NdisReleaseSpinLock(&Adapter->Lock);

            *BytesRead = InformationBufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NTAPI
NvscOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return NvscQueryInformation(Adapter,
                                        OidRequest->DATA.QUERY_INFORMATION.Oid,
                                        OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                                        OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                                        (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesWritten,
                                        (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesNeeded);

        case NdisRequestSetInformation:
            return NvscSetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
NvscCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static
VOID
NTAPI
MiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->Ready = FALSE;
}

static
VOID
NTAPI
MiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNETVSC_ADAPTER Adapter = MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
        Adapter->Ready = FALSE;
}

static
VOID
NTAPI
MiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(NvscDriverHandle);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.InitializeHandlerEx = MiniportInitializeEx;
    Characteristics.HaltHandlerEx = MiniportHaltEx;
    Characteristics.UnloadHandler = MiniportUnload;
    Characteristics.PauseHandler = MiniportPause;
    Characteristics.RestartHandler = MiniportRestart;
    Characteristics.OidRequestHandler = NvscOidRequest;
    Characteristics.SendNetBufferListsHandler = MiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = MiniportCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = MiniportDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = MiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = NvscCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &NvscDriverHandle);
}
