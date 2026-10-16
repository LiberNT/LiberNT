/*
 * ReactOS Realtek 8139 Driver
 *
 * Copyright (C) 2013 Cameron Gutman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "nic.h"

#define NDEBUG
#include <debug.h>

ULONG DebugTraceLevel = MIN_TRACE;

static NDIS_HANDLE RtlDriverHandle;

static
BOOLEAN
CopyNetBufferToTransmitBuffer(
    IN PNET_BUFFER NetBuffer,
    OUT PUCHAR Destination,
    OUT PULONG TransmitLength)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG PacketLength = Remaining;
    ULONG Copied = 0;
    PUCHAR Source;
    ULONG Length;

    if (PacketLength == 0 || PacketLength > MAXIMUM_FRAME_SIZE)
        return FALSE;

    while (Mdl != NULL && Remaining != 0)
    {
        NdisQueryMdl(Mdl, (PVOID*)&Source, &Length, NormalPagePriority);
        if (Source == NULL || Length <= Offset)
        {
            if (Source == NULL)
                return FALSE;
            Offset -= Length;
            NdisGetNextMdl(Mdl, &Mdl);
            continue;
        }

        Length = min(Length - Offset, Remaining);
        RtlCopyMemory(Destination + Copied, Source + Offset, Length);
        Copied += Length;
        Remaining -= Length;
        Offset = 0;
        NdisGetNextMdl(Mdl, &Mdl);
    }

    if (Remaining != 0)
        return FALSE;

    *TransmitLength = max(PacketLength, MINIMUM_FRAME_SIZE);
    if (PacketLength < *TransmitLength)
        RtlFillMemory(Destination + PacketLength, *TransmitLength - PacketLength, 0x00);

    return TRUE;
}

static
VOID
AppendNbl(
    IN OUT PNET_BUFFER_LIST *Head,
    IN OUT PNET_BUFFER_LIST *Tail,
    IN PNET_BUFFER_LIST Nbl)
{
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    if (*Tail)
        NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
    else
        *Head = Nbl;
    *Tail = Nbl;
}

VOID
RtlProcessSendQueue(
    IN PRTL_ADAPTER Adapter,
    IN OUT PNET_BUFFER_LIST *CompleteHead,
    IN OUT PNET_BUFFER_LIST *CompleteTail)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    PUCHAR TransmitBufferVa;
    ULONG TransmitBuffer;
    ULONG TransmitLength;

    while ((Nbl = Adapter->SendQueueHead) != NULL)
    {
        NetBuffer = (PNET_BUFFER)Nbl->MiniportReserved[0];

        while (NetBuffer != NULL)
        {
            if (Adapter->TxFull)
                return;

            TransmitBufferVa = Adapter->TransmitBuffers + (TRANSMIT_BUFFER_STRIDE * Adapter->CurrentTxDesc);
            if (!CopyNetBufferToTransmitBuffer(NetBuffer, TransmitBufferVa, &TransmitLength))
            {
                NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_INVALID_LENGTH;
                Adapter->TransmitError++;
            }
            else
            {
                TransmitBuffer = Adapter->TransmitBuffersPa.LowPart + (TRANSMIT_BUFFER_STRIDE * Adapter->CurrentTxDesc);
                NICTransmitPacket(Adapter, Adapter->CurrentTxDesc, TransmitBuffer, TransmitLength);

                Adapter->CurrentTxDesc++;
                Adapter->CurrentTxDesc %= TX_DESC_COUNT;
                if (Adapter->CurrentTxDesc == Adapter->DirtyTxDesc)
                    Adapter->TxFull = TRUE;
            }

            NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
            Nbl->MiniportReserved[0] = NetBuffer;
        }

        Adapter->SendQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (Adapter->SendQueueHead == NULL)
            Adapter->SendQueueTail = NULL;
        AppendNbl(CompleteHead, CompleteTail, Nbl);
    }
}

VOID
RtlCompleteSends(
    IN PRTL_ADAPTER Adapter,
    IN PNET_BUFFER_LIST CompleteHead,
    IN BOOLEAN DispatchLevel)
{
    if (CompleteHead != NULL)
    {
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        CompleteHead,
                                        DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

static
PNET_BUFFER_LIST
RtlFailNblChain(
    IN PNET_BUFFER_LIST Nbl,
    IN NDIS_STATUS Status)
{
    PNET_BUFFER_LIST Current;

    for (Current = Nbl; Current != NULL; Current = NET_BUFFER_LIST_NEXT_NBL(Current))
        NET_BUFFER_LIST_STATUS(Current) = Status;

    return Nbl;
}

static
VOID
NTAPI
MiniportSendNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN NDIS_PORT_NUMBER PortNumber,
    IN ULONG SendFlags)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;
    BOOLEAN DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, Next;

    UNREFERENCED_PARAMETER(PortNumber);

    NdisAcquireSpinLock(&Adapter->Lock);

    if (Adapter->Paused)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        RtlCompleteSends(Adapter, RtlFailNblChain(NetBufferLists, NDIS_STATUS_PAUSED), DispatchLevel);
        return;
    }

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        Nbl->MiniportReserved[0] = NET_BUFFER_LIST_FIRST_NB(Nbl);
        AppendNbl(&Adapter->SendQueueHead, &Adapter->SendQueueTail, Nbl);
    }

    RtlProcessSendQueue(Adapter, &CompleteHead, &CompleteTail);
    NdisReleaseSpinLock(&Adapter->Lock);

    RtlCompleteSends(Adapter, CompleteHead, DispatchLevel);
}

static
VOID
NTAPI
MiniportCancelSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID CancelId)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, Next, KeepHead = NULL, KeepTail = NULL;

    NdisAcquireSpinLock(&Adapter->Lock);

    Nbl = Adapter->SendQueueHead;
    if (Nbl != NULL && Nbl->MiniportReserved[0] != NET_BUFFER_LIST_FIRST_NB(Nbl))
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        AppendNbl(&KeepHead, &KeepTail, Nbl);
        Nbl = Next;
    }

    for (; Nbl != NULL; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
            AppendNbl(&CompleteHead, &CompleteTail, Nbl);
        }
        else
        {
            AppendNbl(&KeepHead, &KeepTail, Nbl);
        }
    }

    Adapter->SendQueueHead = KeepHead;
    Adapter->SendQueueTail = KeepTail;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtlCompleteSends(Adapter, CompleteHead, FALSE);
}

static
VOID
NTAPI
MiniportReturnNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN ULONG ReturnFlags)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;
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

VOID
RtlIndicateLinkState(
    IN PRTL_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;
    ULONG64 Speed = (ULONG64)Adapter->LinkSpeedMbps * 1000000;

    NdisZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    if (Adapter->MediaState == NdisMediaStateConnected)
    {
        LinkState.MediaConnectState = MediaConnectStateConnected;
        LinkState.MediaDuplexState = MediaDuplexStateFull;
        LinkState.XmitLinkSpeed = Speed;
        LinkState.RcvLinkSpeed = Speed;
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
NDIS_STATUS
NTAPI
MiniportPause(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;
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

    RtlCompleteSends(Adapter, RtlFailNblChain(Pending, NDIS_STATUS_PAUSED), FALSE);
    return Status;
}

static
NDIS_STATUS
NTAPI
MiniportRestart(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    RtlIndicateLinkState(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
RtlFreeReceiveNbls(
    IN PRTL_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;
    PMDL Mdl;
    ULONG i;

    for (i = 0; i < RECEIVE_NBL_COUNT; i++)
    {
        Nbl = Adapter->ReceiveNbls[i];
        if (Nbl == NULL)
            continue;

        Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
        if (Nbl->MiniportReserved[0] != NULL)
            NdisFreeMemory(Nbl->MiniportReserved[0], 0, 0);
        NdisFreeNetBufferList(Nbl);
        if (Mdl != NULL)
            NdisFreeMdl(Mdl);
        Adapter->ReceiveNbls[i] = NULL;
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
RtlAllocateReceiveNbls(
    IN PRTL_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PNET_BUFFER_LIST Nbl;
    PUCHAR Buffer;
    PMDL Mdl;
    ULONG i;

    NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = RECEIVE_TAG;

    Adapter->ReceiveNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportAdapterHandle, &PoolParameters);
    if (Adapter->ReceiveNblPool == NULL)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < RECEIVE_NBL_COUNT; i++)
    {
        Buffer = NdisAllocateMemoryWithTagPriority(Adapter->MiniportAdapterHandle,
                                                   MAXIMUM_FRAME_SIZE,
                                                   RECEIVE_TAG,
                                                   NormalPoolPriority);
        if (Buffer == NULL)
            return NDIS_STATUS_RESOURCES;

        Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Buffer, MAXIMUM_FRAME_SIZE);
        if (Mdl == NULL)
        {
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->ReceiveNblPool, 0, 0, Mdl, 0, MAXIMUM_FRAME_SIZE);
        if (Nbl == NULL)
        {
            NdisFreeMdl(Mdl);
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl->MiniportReserved[0] = Buffer;
        Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
        Adapter->ReceiveNbls[i] = Nbl;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
MiniportHaltEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HALT_ACTION HaltAction)
{
    PRTL_ADAPTER adapter = (PRTL_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS stopStatus;
    BOOLEAN freeDmaBuffers = TRUE;

    UNREFERENCED_PARAMETER(HaltAction);

    if (adapter->IoBase != NULL)
        NICDisableInterrupts(adapter);

    if (adapter->InterruptHandle != NULL)
    {
        NdisMDeregisterInterruptEx(adapter->InterruptHandle);
        adapter->InterruptHandle = NULL;
    }

    if (adapter->IoBase != NULL && adapter->DmaActive)
    {
        stopStatus = NICSoftReset(adapter);
        if (stopStatus == NDIS_STATUS_SUCCESS)
            adapter->DmaActive = FALSE;
        else
            freeDmaBuffers = FALSE;
    }

    if (adapter->ReceiveBuffer != NULL && freeDmaBuffers)
    {
        if (adapter->IoBase != NULL)
            NICRemoveReceiveBuffer(adapter);

        NdisMFreeSharedMemory(adapter->MiniportAdapterHandle, adapter->ReceiveBufferLength, FALSE, adapter->ReceiveBuffer, adapter->ReceiveBufferPa);
    }

    if (adapter->TransmitBuffers != NULL && freeDmaBuffers)
        NdisMFreeSharedMemory(adapter->MiniportAdapterHandle, TRANSMIT_BUFFER_LENGTH, FALSE, adapter->TransmitBuffers, adapter->TransmitBuffersPa);

    if (adapter->DmaHandle != NULL)
        NdisMDeregisterScatterGatherDma(adapter->DmaHandle);

    RtlFreeReceiveNbls(adapter);

    if (adapter->IoBase != NULL)
    {
        NdisMDeregisterIoPortRange(adapter->MiniportAdapterHandle,
                                   adapter->IoRangeStart,
                                   adapter->IoRangeLength,
                                   adapter->IoBase);
    }

    NdisFreeSpinLock(&adapter->Lock);
    NdisFreeMemory(adapter, 0, 0);
}

static
NDIS_STATUS
RtlSetAttributes(
    IN PRTL_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;
    NDIS_STATUS Status;
    ULONG64 Speed = (ULONG64)Adapter->LinkSpeedMbps * 1000000;

    NdisZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE | NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    Registration.InterfaceType = NdisInterfacePci;

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NdisZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMedium802_3;
    General.MtuSize = MAXIMUM_FRAME_SIZE - sizeof(ETH_HEADER);
    General.MaxXmitLinkSpeed = 100000000;
    General.MaxRcvLinkSpeed = 100000000;
    if (Adapter->MediaState == NdisMediaStateConnected)
    {
        General.XmitLinkSpeed = Speed;
        General.RcvLinkSpeed = Speed;
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
    General.LookaheadSize = MAXIMUM_FRAME_SIZE - sizeof(ETH_HEADER);
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST |
                                     NDIS_PACKET_TYPE_PROMISCUOUS;
    General.MaxMulticastListSize = MAXIMUM_MULTICAST_ADDRESSES;
    General.MacAddressLength = IEEE_802_ADDR_LENGTH;
    NdisMoveMemory(General.PermanentMacAddress, Adapter->PermanentMacAddress, IEEE_802_ADDR_LENGTH);
    NdisMoveMemory(General.CurrentMacAddress, Adapter->CurrentMacAddress, IEEE_802_ADDR_LENGTH);
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
                                  NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                                  NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED;
    General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    General.AutoNegotiationFlags = NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                                   NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                                   NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
    General.SupportedOidList = RtlSupportedOids;
    General.SupportedOidListLength = RtlSupportedOidCount * sizeof(NDIS_OID);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
NDIS_STATUS
NTAPI
MiniportInitializeEx(
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE MiniportDriverContext,
    IN PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS InterruptCharacteristics;
    NDIS_SG_DMA_DESCRIPTION DmaDescription;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PRTL_ADAPTER adapter;
    NDIS_STATUS status;
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    adapter = NdisAllocateMemoryWithTagPriority(MiniportAdapterHandle, sizeof(*adapter), ADAPTER_TAG, NormalPoolPriority);
    if (adapter == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(adapter, sizeof(*adapter));
    adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    adapter->Paused = TRUE;
    NdisAllocateSpinLock(&adapter->Lock);

    ResourceList = MiniportInitParameters->AllocatedResources;
    if (ResourceList == NULL)
    {
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    for (i = 0; i < ResourceList->Count; i++)
    {
        Descriptor = &ResourceList->PartialDescriptors[i];
        switch (Descriptor->Type)
        {
            case CmResourceTypePort:
                if (adapter->IoRangeStart == 0 && Descriptor->u.Port.Start.HighPart == 0)
                {
                    adapter->IoRangeStart = Descriptor->u.Port.Start.LowPart;
                    adapter->IoRangeLength = Descriptor->u.Port.Length;
                }
                break;

            case CmResourceTypeInterrupt:
                adapter->InterruptVector = Descriptor->u.Interrupt.Vector;
                adapter->InterruptLevel = Descriptor->u.Interrupt.Level;
                adapter->InterruptShared = (Descriptor->ShareDisposition == CmResourceShareShared);
                adapter->InterruptFlags = Descriptor->Flags;
                break;

            default:
                break;
        }
    }

    if (adapter->IoRangeStart == 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Adapter didn't receive an I/O port range\n"));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    status = NdisMRegisterIoPortRange((PVOID*)&adapter->IoBase,
                                      MiniportAdapterHandle,
                                      adapter->IoRangeStart,
                                      adapter->IoRangeLength);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    status = NICPowerOn(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    status = NICSoftReset(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    status = NICGetPermanentMacAddress(adapter, adapter->PermanentMacAddress);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    RtlCopyMemory(adapter->CurrentMacAddress, adapter->PermanentMacAddress, IEEE_802_ADDR_LENGTH);
    NICUpdateLinkStatus(adapter);

    status = RtlSetAttributes(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    NdisZeroMemory(&DmaDescription, sizeof(DmaDescription));
    DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.MaximumPhysicalMapping = MAXIMUM_FRAME_SIZE;
    status = NdisMRegisterScatterGatherDma(MiniportAdapterHandle, &DmaDescription, &adapter->DmaHandle);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    adapter->ReceiveBufferLength = FULL_RECEIVE_BUFFER_SIZE;
    NdisMAllocateSharedMemory(MiniportAdapterHandle,
                              adapter->ReceiveBufferLength,
                              FALSE,
                              (PVOID*)&adapter->ReceiveBuffer,
                              &adapter->ReceiveBufferPa);
    if (adapter->ReceiveBuffer == NULL ||
        adapter->ReceiveBufferPa.HighPart != 0 ||
        adapter->ReceiveBufferPa.LowPart > MAXULONG - (adapter->ReceiveBufferLength - 1))
    {
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    NdisMAllocateSharedMemory(MiniportAdapterHandle, TRANSMIT_BUFFER_LENGTH, FALSE, (PVOID*)&adapter->TransmitBuffers, &adapter->TransmitBuffersPa);
    if (adapter->TransmitBuffers == NULL ||
        adapter->TransmitBuffersPa.HighPart != 0 ||
        adapter->TransmitBuffersPa.LowPart > MAXULONG - (TRANSMIT_BUFFER_LENGTH - 1) ||
        (adapter->TransmitBuffersPa.LowPart & (sizeof(ULONG) - 1)) != 0)
    {
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    status = RtlAllocateReceiveNbls(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    status = NICRegisterReceiveBuffer(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    NdisZeroMemory(&InterruptCharacteristics, sizeof(InterruptCharacteristics));
    InterruptCharacteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    InterruptCharacteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    InterruptCharacteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    InterruptCharacteristics.InterruptHandler = MiniportIsr;
    InterruptCharacteristics.InterruptDpcHandler = MiniportInterruptDpc;
    InterruptCharacteristics.DisableInterruptHandler = MiniportDisableInterrupt;
    InterruptCharacteristics.EnableInterruptHandler = MiniportEnableInterrupt;
    status = NdisMRegisterInterruptEx(MiniportAdapterHandle, adapter, &InterruptCharacteristics, &adapter->InterruptHandle);
    if (status != NDIS_STATUS_SUCCESS)
    {
        adapter->InterruptHandle = NULL;
        goto Cleanup;
    }

    adapter->InterruptMask = DEFAULT_INTERRUPT_MASK;
    status = NICApplyInterruptMask(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    adapter->DmaActive = TRUE;
    status = NICEnableTxRx(adapter);
    if (status != NDIS_STATUS_SUCCESS)
        goto Cleanup;

    return NDIS_STATUS_SUCCESS;

Cleanup:
    MiniportHaltEx(adapter, NdisHaltDeviceInitializationFailed);
    return status;
}

static
VOID
NTAPI
MiniportShutdownEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRTL_ADAPTER Adapter = (PRTL_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    if (Adapter->IoBase != NULL)
    {
        NICDisableInterrupts(Adapter);
        NICSoftReset(Adapter);
    }
}

static
VOID
NTAPI
MiniportDevicePnPEventNotify(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
NTAPI
MiniportUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(RtlDriverHandle);
}

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    Characteristics.MajorDriverVersion = DRIVER_VERSION;
    Characteristics.InitializeHandlerEx = MiniportInitializeEx;
    Characteristics.HaltHandlerEx = MiniportHaltEx;
    Characteristics.UnloadHandler = MiniportUnload;
    Characteristics.PauseHandler = MiniportPause;
    Characteristics.RestartHandler = MiniportRestart;
    Characteristics.OidRequestHandler = MiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = MiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = MiniportCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = MiniportDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = MiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = MiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &RtlDriverHandle);
}
