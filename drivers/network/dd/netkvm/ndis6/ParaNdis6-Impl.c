/*
 * PROJECT:     LiberNT VirtIO Network Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 send, receive and support routines of the NetKVM driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ParaNdis6.h"

#define SEND_ENTRY_NO_INDIRECT      0x0004

PVOID ParaNdis_AllocateMemory(PARANDIS_ADAPTER *pContext, ULONG ulRequiredSize)
{
    PVOID p;

    UNREFERENCED_PARAMETER(pContext);

    if (NdisAllocateMemoryWithTag(&p, ulRequiredSize, PARANDIS_MEMORY_TAG) != NDIS_STATUS_SUCCESS)
        p = NULL;
    if (!p)
    {
        DPrintf(0, ("[%s] failed (%d bytes)", __FUNCTION__, ulRequiredSize));
    }
    return p;
}

NDIS_HANDLE ParaNdis_OpenNICConfiguration(PARANDIS_ADAPTER *pContext)
{
    NDIS_CONFIGURATION_OBJECT co;
    NDIS_HANDLE cfg;
    NDIS_STATUS status;

    co.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    co.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    co.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    co.NdisHandle = pContext->MiniportHandle;
    co.Flags = 0;

    status = NdisOpenConfigurationEx(&co, &cfg);
    if (status != NDIS_STATUS_SUCCESS)
        cfg = NULL;
    return cfg;
}

void ParaNdis_RestoreDeviceConfigurationAfterReset(
    PARANDIS_ADAPTER *pContext)
{
    UNREFERENCED_PARAMETER(pContext);
}

VOID ParaNdis_IndicateConnect(PARANDIS_ADAPTER *pContext, BOOLEAN bConnected, BOOLEAN bForce)
{
    NDIS_STATUS_INDICATION indication;
    NDIS_LINK_STATE state;

    if (bConnected == pContext->bConnected && !bForce)
        return;

    pContext->bConnected = bConnected;
    ParaNdis_DebugHistory(pContext, hopConnectIndication, NULL, bConnected, 0, 0);

    NdisZeroMemory(&state, sizeof(state));
    state.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    state.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    state.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    if (bConnected)
    {
        state.MediaConnectState = MediaConnectStateConnected;
        state.MediaDuplexState = MediaDuplexStateFull;
        state.XmitLinkSpeed = PARANDIS_FORMAL_LINK_SPEED;
        state.RcvLinkSpeed = PARANDIS_FORMAL_LINK_SPEED;
    }
    else
    {
        state.MediaConnectState = MediaConnectStateDisconnected;
        state.MediaDuplexState = MediaDuplexStateUnknown;
        state.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        state.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    state.PauseFunctions = NdisPauseFunctionsUnsupported;

    NdisZeroMemory(&indication, sizeof(indication));
    indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    indication.SourceHandle = pContext->MiniportHandle;
    indication.StatusCode = NDIS_STATUS_LINK_STATE;
    indication.StatusBuffer = &state;
    indication.StatusBufferSize = sizeof(state);

    NdisMIndicateStatusEx(pContext->MiniportHandle, &indication);
}

VOID ParaNdis_SetPowerState(PARANDIS_ADAPTER *pContext, NDIS_DEVICE_POWER_STATE newState)
{
    pContext->powerState = newState;
}

BOOLEAN ParaNdis_SetTimer(NDIS_HANDLE timer, LONG millies)
{
    LARGE_INTEGER dueTime;

    dueTime.QuadPart = -(LONGLONG)millies * 10000;
    return NdisSetTimerObject(timer, dueTime, 0, NULL);
}

static VOID NTAPI OnConnectTimer(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    ParaNdis_ReportLinkStatus(pContext, FALSE);
}

static VOID NTAPI OnDPCPostProcessTimer(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)FunctionContext;
    ULONG requiresProcessing;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    requiresProcessing = ParaNdis_DPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
    if (requiresProcessing)
    {
        InterlockedOr(&pContext->InterruptStatus, requiresProcessing);
        ParaNdis_SetTimer(pContext->DPCPostProcessTimer, 10);
    }
}

BOOLEAN ParaNdis_InitialAllocatePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses)
{
    NdisMAllocateSharedMemory(
        pContext->MiniportHandle,
        pAddresses->size,
        (BOOLEAN)pAddresses->IsCached,
        &pAddresses->Virtual,
        &pAddresses->Physical);
    return pAddresses->Virtual != NULL;
}

VOID ParaNdis_FreePhysicalMemory(
    PARANDIS_ADAPTER *pContext,
    tCompletePhysicalAddress *pAddresses)
{
    NdisMFreeSharedMemory(
        pContext->MiniportHandle,
        pAddresses->size,
        (BOOLEAN)pAddresses->IsCached,
        pAddresses->Virtual,
        pAddresses->Physical);
}

static NDIS_STATUS AllocateTimer(
    PARANDIS_ADAPTER *pContext,
    PNDIS_TIMER_FUNCTION Function,
    PNDIS_HANDLE pTimer)
{
    NDIS_TIMER_CHARACTERISTICS chars;
    NDIS_STATUS status;

    NdisZeroMemory(&chars, sizeof(chars));
    chars.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
    chars.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
    chars.Header.Size = NDIS_SIZEOF_TIMER_CHARACTERISTICS_REVISION_1;
    chars.AllocationTag = PARANDIS_MEMORY_TAG;
    chars.TimerFunction = Function;
    chars.FunctionContext = pContext;

    status = NdisAllocateTimerObject(pContext->MiniportHandle, &chars, pTimer);
    if (status != NDIS_STATUS_SUCCESS)
        *pTimer = NULL;
    return status;
}

NDIS_STATUS NTAPI ParaNdis_FinishSpecificInitialization(
    PARANDIS_ADAPTER *pContext)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS ic;
    NET_BUFFER_LIST_POOL_PARAMETERS pool;
    NDIS_STATUS status;

    NdisInitializeEvent(&pContext->HaltEvent);
    InitializeListHead(&pContext->SendQueue);
    InitializeListHead(&pContext->TxWaitingList);

    pContext->bUseScatterGather = FALSE;
    pContext->bUseIndirect = FALSE;
    NdisZeroMemory(&pContext->Offload.flags, sizeof(pContext->Offload.flags));
    pContext->Offload.flagsValue = 0;

    status = AllocateTimer(pContext, OnConnectTimer, &pContext->ConnectTimer);
    if (status == NDIS_STATUS_SUCCESS)
        status = AllocateTimer(pContext, OnDPCPostProcessTimer, &pContext->DPCPostProcessTimer);

    if (status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(&pool, sizeof(pool));
        pool.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        pool.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        pool.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        pool.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
        pool.fAllocateNetBuffer = TRUE;
        pool.PoolTag = PARANDIS_MEMORY_TAG;
        pContext->ReceiveNblPool = NdisAllocateNetBufferListPool(pContext->MiniportHandle, &pool);
        if (!pContext->ReceiveNblPool)
            status = NDIS_STATUS_RESOURCES;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(&ic, sizeof(ic));
        ic.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
        ic.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
        ic.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
        ic.InterruptHandler = ParaNdis6_MiniportISR;
        ic.InterruptDpcHandler = ParaNdis6_MiniportDPC;
        ic.DisableInterruptHandler = ParaNdis6_DisableInterrupt;
        ic.EnableInterruptHandler = ParaNdis6_EnableInterrupt;
        status = NdisMRegisterInterruptEx(pContext->MiniportHandle, pContext, &ic, &pContext->InterruptHandle);
        if (status != NDIS_STATUS_SUCCESS)
            pContext->InterruptHandle = NULL;
    }

    DEBUG_EXIT_STATUS(status ? 0 : 2, status);
    return status;
}

VOID ParaNdis_FinalizeCleanup(PARANDIS_ADAPTER *pContext)
{
    if (pContext->InterruptHandle)
    {
        NdisMDeregisterInterruptEx(pContext->InterruptHandle);
        pContext->InterruptHandle = NULL;
    }
    if (pContext->ConnectTimer)
        NdisCancelTimerObject(pContext->ConnectTimer);
    if (pContext->DPCPostProcessTimer)
        NdisCancelTimerObject(pContext->DPCPostProcessTimer);
    KeFlushQueuedDpcs();
    if (pContext->ConnectTimer)
    {
        NdisFreeTimerObject(pContext->ConnectTimer);
        pContext->ConnectTimer = NULL;
    }
    if (pContext->DPCPostProcessTimer)
    {
        NdisFreeTimerObject(pContext->DPCPostProcessTimer);
        pContext->DPCPostProcessTimer = NULL;
    }
    if (pContext->ReceiveNblPool)
    {
        NdisFreeNetBufferListPool(pContext->ReceiveNblPool);
        pContext->ReceiveNblPool = NULL;
    }
}

static FORCEINLINE ULONG MaxNdisBufferDataSize(PARANDIS_ADAPTER *pContext, pIONetDescriptor pBufferDesc)
{
    ULONG size = pBufferDesc->DataInfo.size;
    if (pContext->bUseMergedBuffers) size -= pContext->nVirtioHeaderSize;
    return size;
}

BOOLEAN ParaNdis_BindBufferToPacket(
    PARANDIS_ADAPTER *pContext,
    pIONetDescriptor pBufferDesc)
{
    PNET_BUFFER_LIST nbl;
    PMDL pMdl;

    pMdl = NdisAllocateMdl(
        pContext->MiniportHandle,
        RtlOffsetToPointer(pBufferDesc->DataInfo.Virtual, pContext->bUseMergedBuffers ? pContext->nVirtioHeaderSize : 0),
        MaxNdisBufferDataSize(pContext, pBufferDesc));
    if (!pMdl)
        return FALSE;

    nbl = NdisAllocateNetBufferAndNetBufferList(
        pContext->ReceiveNblPool,
        0,
        0,
        pMdl,
        0,
        MaxNdisBufferDataSize(pContext, pBufferDesc));
    if (!nbl)
    {
        NdisFreeMdl(pMdl);
        return FALSE;
    }

    nbl->SourceHandle = pContext->MiniportHandle;
    PARANDIS_NBL_DESCRIPTOR(nbl) = pBufferDesc;
    pBufferDesc->pHolder = nbl;
    return TRUE;
}

void ParaNdis_UnbindBufferFromPacket(
    PARANDIS_ADAPTER *pContext,
    pIONetDescriptor pBufferDesc)
{
    PNET_BUFFER_LIST nbl = pBufferDesc->pHolder;
    PMDL pMdl;

    if (!nbl)
        return;

    pBufferDesc->pHolder = NULL;
    pMdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(nbl));
    NdisFreeNetBufferList(nbl);
    if (pMdl)
    {
        NdisAdjustMdlLength(pMdl, MaxNdisBufferDataSize(pContext, pBufferDesc));
        NdisFreeMdl(pMdl);
    }
}

static ULONG ReceiveFlags(VOID)
{
    return (KeGetCurrentIrql() == DISPATCH_LEVEL) ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0;
}

tPacketIndicationType ParaNdis_IndicateReceivedPacket(
    PARANDIS_ADAPTER *pContext,
    PVOID dataBuffer,
    PULONG pLength,
    BOOLEAN bPrepareOnly,
    pIONetDescriptor pBuffersDesc)
{
    PNET_BUFFER_LIST nbl = pBuffersDesc->pHolder;
    NDIS_NET_BUFFER_LIST_8021Q_INFO qInfo;
    ULONG length = *pLength;
    BOOLEAN bOK = (nbl != NULL);

    qInfo.Value = NULL;
    if (bOK &&
        ((pContext->ulPriorityVlanSetting && length > (ETH_PRIORITY_HEADER_OFFSET + ETH_PRIORITY_HEADER_SIZE)) ||
         length > pContext->MaxPacketSize.nMaxFullSizeOS))
    {
        PUCHAR pPriority = (PUCHAR)dataBuffer + ETH_PRIORITY_HEADER_OFFSET;
        if (ETH_HAS_PRIO_HEADER(dataBuffer))
        {
            if (IsPrioritySupported(pContext))
                qInfo.TagHeader.UserPriority = (pPriority[2] & 0xE0) >> 5;
            if (IsVlanSupported(pContext))
            {
                qInfo.TagHeader.VlanId = (((USHORT)(pPriority[2] & 0x0F)) << 8) | pPriority[3];
                if (pContext->VlanId && pContext->VlanId != qInfo.TagHeader.VlanId)
                {
                    pContext->extraStatistics.framesFilteredOut++;
                    bOK = FALSE;
                }
            }
            RtlMoveMemory(
                pPriority,
                pPriority + ETH_PRIORITY_HEADER_SIZE,
                length - ETH_PRIORITY_HEADER_OFFSET - ETH_PRIORITY_HEADER_SIZE);
            length -= ETH_PRIORITY_HEADER_SIZE;
            if (length > pContext->MaxPacketSize.nMaxFullSizeOS)
                bOK = FALSE;
            pContext->extraStatistics.framesRxPriority++;
        }
    }

    if (bOK)
    {
        PVOID headerBuffer = pContext->bUseMergedBuffers ? pBuffersDesc->DataInfo.Virtual : pBuffersDesc->HeaderInfo.Virtual;
        virtio_net_hdr_basic *pHeader = (virtio_net_hdr_basic *)headerBuffer;
        PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(nbl);
        PMDL pMdl = NET_BUFFER_FIRST_MDL(nb);
        tChecksumCheckResult csRes;

        ParaNdis_PadPacketReceived(dataBuffer, &length);
        NdisAdjustMdlLength(pMdl, length);
        NET_BUFFER_CURRENT_MDL(nb) = pMdl;
        NET_BUFFER_CURRENT_MDL_OFFSET(nb) = 0;
        NET_BUFFER_DATA_OFFSET(nb) = 0;
        NET_BUFFER_DATA_LENGTH(nb) = length;
        NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
        NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_INFO(nbl, Ieee8021QNetBufferListInfo) = qInfo.Value;
        NET_BUFFER_LIST_INFO(nbl, TcpIpChecksumNetBufferListInfo) = NULL;

        csRes = ParaNdis_CheckRxChecksum(pContext, pHeader->flags, dataBuffer, length);
        if (csRes.value)
        {
            NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO csInfo;
            csInfo.Value = NULL;
            csInfo.Receive.IpChecksumFailed = csRes.flags.IpFailed;
            csInfo.Receive.IpChecksumSucceeded = csRes.flags.IpOK;
            csInfo.Receive.TcpChecksumFailed = csRes.flags.TcpFailed;
            csInfo.Receive.TcpChecksumSucceeded = csRes.flags.TcpOK;
            csInfo.Receive.UdpChecksumFailed = csRes.flags.UdpFailed;
            csInfo.Receive.UdpChecksumSucceeded = csRes.flags.UdpOK;
            NET_BUFFER_LIST_INFO(nbl, TcpIpChecksumNetBufferListInfo) = csInfo.Value;
        }

        if (!bPrepareOnly)
        {
            NdisMIndicateReceiveNetBufferLists(
                pContext->MiniportHandle,
                nbl,
                NDIS_DEFAULT_PORT_NUMBER,
                1,
                ReceiveFlags());
        }
    }
    else
    {
        DPrintf(0, ("[%s] Error: %p(%d b.) with NBL %p", __FUNCTION__, pBuffersDesc, length, nbl));
        nbl = NULL;
    }

    *pLength = length;
    return nbl;
}

VOID ParaNdis_IndicateReceivedBatch(
    PARANDIS_ADAPTER *pContext,
    tPacketIndicationType *pBatch,
    ULONG nofPackets)
{
    ULONG i;

    if (!nofPackets)
        return;

    for (i = 0; i + 1 < nofPackets; ++i)
        NET_BUFFER_LIST_NEXT_NBL(pBatch[i]) = pBatch[i + 1];
    NET_BUFFER_LIST_NEXT_NBL(pBatch[nofPackets - 1]) = NULL;

    NdisMIndicateReceiveNetBufferLists(
        pContext->MiniportHandle,
        pBatch[0],
        NDIS_DEFAULT_PORT_NUMBER,
        nofPackets,
        ReceiveFlags());
}

VOID NTAPI ParaNdis6_ReturnNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN ULONG ReturnFlags)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;
    PNET_BUFFER_LIST nbl, next;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&pContext->ReceiveLock);
    for (nbl = NetBufferLists; nbl; nbl = next)
    {
        next = NET_BUFFER_LIST_NEXT_NBL(nbl);
        NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
        pContext->ReuseBufferProc(pContext, PARANDIS_NBL_DESCRIPTOR(nbl));
    }
    NdisReleaseSpinLock(&pContext->ReceiveLock);
}

static VOID AppendNbl(PNET_BUFFER_LIST nbl, PNET_BUFFER_LIST *pHead, PNET_BUFFER_LIST *pTail)
{
    NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
    if (*pTail)
        NET_BUFFER_LIST_NEXT_NBL(*pTail) = nbl;
    else
        *pHead = nbl;
    *pTail = nbl;
}

static VOID ReleaseNbl(PNET_BUFFER_LIST nbl, PNET_BUFFER_LIST *pHead, PNET_BUFFER_LIST *pTail)
{
    if (InterlockedDecrement(PARANDIS_NBL_PENDING(nbl)) == 0)
        AppendNbl(nbl, pHead, pTail);
}

static VOID ReleaseNetBuffer(
    PARANDIS_ADAPTER *pContext,
    PNET_BUFFER_LIST nbl,
    NDIS_STATUS status,
    PNET_BUFFER_LIST *pHead,
    PNET_BUFFER_LIST *pTail)
{
    if (status != NDIS_STATUS_SUCCESS)
        NET_BUFFER_LIST_STATUS(nbl) = status;
    NdisInterlockedDecrement(&pContext->NetTxPacketsToReturn);
    ReleaseNbl(nbl, pHead, pTail);
}

static VOID CompleteSendEntry(
    PARANDIS_ADAPTER *pContext,
    tSendEntry *pEntry,
    PNET_BUFFER_LIST *pHead,
    PNET_BUFFER_LIST *pTail)
{
    PNET_BUFFER_LIST nbl = pEntry->nbl;
    NDIS_STATUS status = pEntry->status;

    ParaNdis_DebugHistory(pContext, hopSendComplete, nbl, 0, pContext->NetTxPacketsToReturn, status);
    NdisFreeMemory(pEntry, 0, 0);
    ReleaseNetBuffer(pContext, nbl, status, pHead, pTail);
}

static VOID CompleteNbls(PARANDIS_ADAPTER *pContext, PNET_BUFFER_LIST head)
{
    if (!head)
        return;

    NdisMSendNetBufferListsComplete(
        pContext->MiniportHandle,
        head,
        (KeGetCurrentIrql() == DISPATCH_LEVEL) ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

tCopyPacketResult ParaNdis_PacketCopier(
    tPacketType packet, PVOID dest, ULONG maxSize, PVOID refValue, BOOLEAN bPreview)
{
    PNET_BUFFER nb = packet;
    PMDL pMdl = NET_BUFFER_CURRENT_MDL(nb);
    ULONG offset = NET_BUFFER_CURRENT_MDL_OFFSET(nb);
    ULONG ulToCopy = NET_BUFFER_DATA_LENGTH(nb);
    ULONG PriorityDataLong = ((tSendEntry *)refValue)->PriorityDataLong;
    ULONG nCopied = 0;
    tCopyPacketResult result;

    if (bPreview) PriorityDataLong = 0;
    if (ulToCopy > maxSize) ulToCopy = bPreview ? maxSize : 0;

    while (pMdl && ulToCopy)
    {
        PUCHAR VirtualAddress = NULL;
        ULONG uLength = 0;

        NdisQueryMdl(pMdl, (PVOID *)&VirtualAddress, &uLength, NormalPagePriority);
        if (!VirtualAddress)
        {
            nCopied = 0;
            break;
        }

        if (uLength > offset)
        {
            VirtualAddress += offset;
            uLength -= offset;
            offset = 0;
            if (uLength > ulToCopy) uLength = ulToCopy;
            ulToCopy -= uLength;
            if ((PriorityDataLong & 0xFFFF) &&
                nCopied < ETH_PRIORITY_HEADER_OFFSET &&
                (nCopied + uLength) >= ETH_PRIORITY_HEADER_OFFSET)
            {
                ULONG ulCopyNow = ETH_PRIORITY_HEADER_OFFSET - nCopied;
                NdisMoveMemory(dest, VirtualAddress, ulCopyNow);
                dest = (PUCHAR)dest + ulCopyNow;
                VirtualAddress += ulCopyNow;
                NdisMoveMemory(dest, &PriorityDataLong, 4);
                nCopied += 4;
                dest = (PUCHAR)dest + 4;
                ulCopyNow = uLength - ulCopyNow;
                if (ulCopyNow) NdisMoveMemory(dest, VirtualAddress, ulCopyNow);
                dest = (PUCHAR)dest + ulCopyNow;
                nCopied += uLength;
            }
            else
            {
                NdisMoveMemory(dest, VirtualAddress, uLength);
                nCopied += uLength;
                dest = (PUCHAR)dest + uLength;
            }
        }
        else
        {
            offset -= uLength;
        }

        NdisGetNextMdl(pMdl, &pMdl);
    }

    result.size = nCopied;
    result.error = cpeOK;
    return result;
}

VOID ParaNdis_OnTransmitBufferReleased(PARANDIS_ADAPTER *pContext, IONetDescriptor *pDesc)
{
    tSendEntry *pEntry = (tSendEntry *)pDesc->ReferenceValue;

    if (pEntry)
    {
        pEntry->flags |= SEND_ENTRY_FLAG_READY;
        pDesc->ReferenceValue = NULL;
        ParaNdis_DebugHistory(pContext, hopBufferSent, pEntry->nbl, 0, pContext->nofFreeHardwareBuffers, pContext->nofFreeTxDescriptors);
    }
    else
    {
        ParaNdis_DebugHistory(pContext, hopBufferSent, NULL, 0, pContext->nofFreeHardwareBuffers, pContext->nofFreeTxDescriptors);
        DPrintf(0, ("[%s] ERROR: Send Entry not set!", __FUNCTION__));
    }
}

VOID ParaNdis_PacketMapper(
    PARANDIS_ADAPTER *pContext,
    tPacketType packet,
    PVOID ReferenceValue,
    struct VirtIOBufferDescriptor *buffers,
    pIONetDescriptor pDesc,
    tMapperResult *pMapperResult)
{
    UNREFERENCED_PARAMETER(pContext);
    UNREFERENCED_PARAMETER(packet);
    UNREFERENCED_PARAMETER(ReferenceValue);
    UNREFERENCED_PARAMETER(buffers);
    UNREFERENCED_PARAMETER(pDesc);

    pMapperResult->usBuffersMapped = 0;
    pMapperResult->usBufferSpaceUsed = 0;
    pMapperResult->ulDataSize = 0;
}

static void InitializeTransferParameters(tTxOperationParameters *pParams, tSendEntry *pEntry)
{
    ULONG flags = 0;

    if (pEntry->flags & SEND_ENTRY_NO_INDIRECT) flags |= pcrNoIndirect;
    if (pEntry->PriorityDataLong) flags |= pcrPriorityTag;

    pParams->packet = pEntry->nb;
    pParams->ReferenceValue = pEntry;
    pParams->nofSGFragments = 0;
    pParams->ulDataSize = NET_BUFFER_DATA_LENGTH(pEntry->nb);
    pParams->offloadMss = 0;
    pParams->tcpHeaderOffset = 0;
    pParams->flags = flags;
}

BOOLEAN ParaNdis_ProcessTx(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN IsDpc,
    BOOLEAN IsInterrupt)
{
    LIST_ENTRY DoneList;
    PNET_BUFFER_LIST head = NULL, tail = NULL;
    BOOLEAN bDoKick = FALSE;
    UINT nBuffersSent = 0, nBytesSent = 0;
    tSendEntry *pEntry;
    ONPAUSECOMPLETEPROC CallbackToCall = NULL;

    UNREFERENCED_PARAMETER(IsDpc);

    InitializeListHead(&DoneList);
    NdisAcquireSpinLock(&pContext->SendLock);

    ParaNdis_DebugHistory(pContext, hopTxProcess, NULL, 1, pContext->nofFreeHardwareBuffers, pContext->nofFreeTxDescriptors);
    do
    {
        if (IsTimeToReleaseTx(pContext))
        {
            ParaNdis_VirtIONetReleaseTransmitBuffers(pContext);
        }
        pEntry = NULL;
        if (!IsListEmpty(&pContext->SendQueue))
        {
            tCopyPacketResult result;
            tTxOperationParameters Params;

            pEntry = (tSendEntry *)RemoveHeadList(&pContext->SendQueue);
            InitializeTransferParameters(&Params, pEntry);
            result = ParaNdis_DoSubmitPacket(pContext, &Params);
            if (result.error == cpeNoBuffer)
            {
                InsertHeadList(&pContext->SendQueue, &pEntry->list);
                pEntry = NULL;
            }
            else if (result.error == cpeNoIndirect)
            {
                InsertHeadList(&pContext->SendQueue, &pEntry->list);
                pEntry->flags |= SEND_ENTRY_NO_INDIRECT;
            }
            else
            {
                InsertTailList(&pContext->TxWaitingList, &pEntry->list);
                ParaNdis_DebugHistory(pContext, hopSubmittedPacket, pEntry->nbl, 0, result.error, Params.flags);
                if (!result.size)
                {
                    DPrintf(0, ("[%s] ERROR %d copying packet!", __FUNCTION__, result.error));
                    if (result.error == cpeTooLarge)
                    {
                        pEntry->status = NDIS_STATUS_BUFFER_OVERFLOW;
                        pContext->Statistics.ifOutErrors++;
                    }
                    else
                    {
                        pEntry->status = NDIS_STATUS_FAILURE;
                    }
                    pEntry->flags |= SEND_ENTRY_FLAG_READY;
                }
                else
                {
                    nBuffersSent++;
                    nBytesSent += result.size;
                }
            }
        }
    } while (pEntry);

    if (nBuffersSent)
    {
        if (IsInterrupt)
            bDoKick = TRUE;
        else
            virtqueue_kick(pContext->NetSendQueue);
    }

    while (!IsListEmpty(&pContext->TxWaitingList))
    {
        pEntry = (tSendEntry *)RemoveHeadList(&pContext->TxWaitingList);
        if (pEntry->flags & SEND_ENTRY_FLAG_READY)
        {
            InsertTailList(&DoneList, &pEntry->list);
        }
        else
        {
            InsertHeadList(&pContext->TxWaitingList, &pEntry->list);
            break;
        }
    }

    if (IsListEmpty(&pContext->TxWaitingList) && pContext->SendState == srsPausing && pContext->SendPauseCompletionProc)
    {
        CallbackToCall = pContext->SendPauseCompletionProc;
        pContext->SendPauseCompletionProc = NULL;
        pContext->SendState = srsDisabled;
        ParaNdis_DebugHistory(pContext, hopInternalSendPause, NULL, 0, 0, 0);
    }
    NdisReleaseSpinLock(&pContext->SendLock);

    while (!IsListEmpty(&DoneList))
    {
        pEntry = (tSendEntry *)RemoveHeadList(&DoneList);
        CompleteSendEntry(pContext, pEntry, &head, &tail);
    }
    CompleteNbls(pContext, head);

    if (CallbackToCall) CallbackToCall(pContext);

    return bDoKick;
}

static tSendEntry *PrepareSendEntry(
    PARANDIS_ADAPTER *pContext,
    PNET_BUFFER_LIST nbl,
    PNET_BUFFER nb,
    ULONG len)
{
    LPCSTR errorFmt = NULL;
    tSendEntry *pse = (tSendEntry *)ParaNdis_AllocateMemory(pContext, sizeof(tSendEntry));

    if (pse)
    {
        NDIS_NET_BUFFER_LIST_8021Q_INFO qInfo;

        NdisZeroMemory(pse, sizeof(*pse));
        pse->nbl = nbl;
        pse->nb = nb;
        pse->status = NDIS_STATUS_SUCCESS;
        pse->ipTransferUnit = len;

        qInfo.Value = pContext->ulPriorityVlanSetting ?
            NET_BUFFER_LIST_INFO(nbl, Ieee8021QNetBufferListInfo) : NULL;
        if (!qInfo.TagHeader.VlanId) qInfo.TagHeader.VlanId = pContext->VlanId;
        if (qInfo.TagHeader.CanonicalFormatId || !IsValidVlanId(pContext, qInfo.TagHeader.VlanId))
        {
            errorFmt = "invalid priority tag";
        }
        else if (qInfo.Value)
        {
            if (!IsPrioritySupported(pContext))
                qInfo.TagHeader.UserPriority = 0;
            if (!IsVlanSupported(pContext))
                qInfo.TagHeader.VlanId = 0;
            SetPriorityData(pse->PriorityData, qInfo.TagHeader.UserPriority, qInfo.TagHeader.VlanId);
        }

        if (!errorFmt && len > pContext->MaxPacketSize.nMaxFullSizeOS)
            errorFmt = "packet is bigger than we able to send";
    }

    if (errorFmt)
    {
        DPrintf(0, ("[%s] ERROR: %s (len %d)", __FUNCTION__, errorFmt, len));
        NdisFreeMemory(pse, 0, 0);
        pse = NULL;
    }
    return pse;
}

VOID NTAPI ParaNdis6_SendNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN NDIS_PORT_NUMBER PortNumber,
    IN ULONG SendFlags)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;
    PNET_BUFFER_LIST head = NULL, tail = NULL;
    PNET_BUFFER_LIST nbl, next;
    PNET_BUFFER nb;
    LONG count;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    ParaNdis_DebugHistory(pContext, hopSend, NULL, 1, 0, 0);

    NdisAcquireSpinLock(&pContext->SendLock);

    for (nbl = NetBufferLists; nbl; nbl = next)
    {
        next = NET_BUFFER_LIST_NEXT_NBL(nbl);
        NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
        NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_SUCCESS;

        count = 1;
        for (nb = NET_BUFFER_LIST_FIRST_NB(nbl); nb; nb = NET_BUFFER_NEXT_NB(nb))
            ++count;
        *PARANDIS_NBL_PENDING(nbl) = count;

        for (nb = NET_BUFFER_LIST_FIRST_NB(nbl); nb; nb = NET_BUFFER_NEXT_NB(nb))
        {
            ULONG len = NET_BUFFER_DATA_LENGTH(nb);

            NdisInterlockedIncrement(&pContext->NetTxPacketsToReturn);
            if (!pContext->bSurprizeRemoved && pContext->bConnected && pContext->SendState == srsEnabled && len)
            {
                tSendEntry *pse = PrepareSendEntry(pContext, nbl, nb, len);
                if (!pse)
                    ReleaseNetBuffer(pContext, nbl, NDIS_STATUS_FAILURE, &head, &tail);
                else
                    InsertTailList(&pContext->SendQueue, &pse->list);
            }
            else
            {
                NDIS_STATUS status = NDIS_STATUS_FAILURE;
                if (pContext->bSurprizeRemoved)
                    status = NDIS_STATUS_NOT_ACCEPTED;
                else if (pContext->SendState != srsEnabled)
                    status = NDIS_STATUS_PAUSED;
                ReleaseNetBuffer(pContext, nbl, status, &head, &tail);
            }
        }

        ReleaseNbl(nbl, &head, &tail);
    }

    NdisReleaseSpinLock(&pContext->SendLock);

    CompleteNbls(pContext, head);

    ParaNdis_ProcessTx(pContext, FALSE, FALSE);
}

VOID NTAPI ParaNdis6_CancelSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID CancelId)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;
    PNET_BUFFER_LIST head = NULL, tail = NULL;
    LIST_ENTRY DoneList, KeepList;
    tSendEntry *pEntry;

    InitializeListHead(&DoneList);
    InitializeListHead(&KeepList);

    NdisAcquireSpinLock(&pContext->SendLock);
    while (!IsListEmpty(&pContext->SendQueue))
    {
        pEntry = (tSendEntry *)RemoveHeadList(&pContext->SendQueue);
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(pEntry->nbl) == CancelId)
            InsertTailList(&DoneList, &pEntry->list);
        else
            InsertTailList(&KeepList, &pEntry->list);
    }
    while (!IsListEmpty(&KeepList))
    {
        pEntry = (tSendEntry *)RemoveHeadList(&KeepList);
        InsertTailList(&pContext->SendQueue, &pEntry->list);
    }
    NdisReleaseSpinLock(&pContext->SendLock);

    while (!IsListEmpty(&DoneList))
    {
        pEntry = (tSendEntry *)RemoveHeadList(&DoneList);
        pEntry->status = NDIS_STATUS_SEND_ABORTED;
        CompleteSendEntry(pContext, pEntry, &head, &tail);
    }
    CompleteNbls(pContext, head);
}

NDIS_STATUS ParaNdis6_StopSend(PARANDIS_ADAPTER *pContext, BOOLEAN bStop, ONPAUSECOMPLETEPROC Callback)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    if (bStop)
    {
        PNET_BUFFER_LIST head = NULL, tail = NULL;
        LIST_ENTRY DoneList;
        tSendEntry *pEntry;

        ParaNdis_DebugHistory(pContext, hopInternalSendPause, NULL, 1, 0, 0);
        InitializeListHead(&DoneList);
        NdisAcquireSpinLock(&pContext->SendLock);
        if (IsListEmpty(&pContext->TxWaitingList))
        {
            pContext->SendState = srsDisabled;
            while (!IsListEmpty(&pContext->SendQueue))
            {
                pEntry = (tSendEntry *)RemoveHeadList(&pContext->SendQueue);
                InsertTailList(&DoneList, &pEntry->list);
            }
            ParaNdis_DebugHistory(pContext, hopInternalSendPause, NULL, 0, 0, 0);
        }
        else
        {
            pContext->SendState = srsPausing;
            pContext->SendPauseCompletionProc = Callback;
            status = NDIS_STATUS_PENDING;
            while (!IsListEmpty(&pContext->SendQueue))
            {
                pEntry = (tSendEntry *)RemoveHeadList(&pContext->SendQueue);
                pEntry->flags |= SEND_ENTRY_FLAG_READY;
                pEntry->status = NDIS_STATUS_PAUSED;
                InsertTailList(&pContext->TxWaitingList, &pEntry->list);
            }
        }
        NdisReleaseSpinLock(&pContext->SendLock);

        while (!IsListEmpty(&DoneList))
        {
            pEntry = (tSendEntry *)RemoveHeadList(&DoneList);
            pEntry->status = NDIS_STATUS_PAUSED;
            CompleteSendEntry(pContext, pEntry, &head, &tail);
        }
        CompleteNbls(pContext, head);
    }
    else
    {
        pContext->SendState = srsEnabled;
        ParaNdis_DebugHistory(pContext, hopInternalSendResume, NULL, 0, 0, 0);
    }
    return status;
}

NDIS_STATUS ParaNdis6_StopReceive(
    PARANDIS_ADAPTER *pContext,
    BOOLEAN bStop,
    ONPAUSECOMPLETEPROC Callback)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    if (bStop)
    {
        ParaNdis_DebugHistory(pContext, hopInternalReceivePause, NULL, 1, 0, 0);
        NdisAcquireSpinLock(&pContext->ReceiveLock);
        if (IsListEmpty(&pContext->NetReceiveBuffersWaiting))
        {
            pContext->ReceiveState = srsDisabled;
            ParaNdis_DebugHistory(pContext, hopInternalReceivePause, NULL, 0, 0, 0);
        }
        else
        {
            pContext->ReceiveState = srsPausing;
            pContext->ReceivePauseCompletionProc = Callback;
            status = NDIS_STATUS_PENDING;
        }
        NdisReleaseSpinLock(&pContext->ReceiveLock);
    }
    else
    {
        pContext->ReceiveState = srsEnabled;
        ParaNdis_DebugHistory(pContext, hopInternalReceiveResume, NULL, 0, 0, 0);
    }
    return status;
}

BOOLEAN ParaNdis_SynchronizeWithInterrupt(
    PARANDIS_ADAPTER *pContext,
    ULONG messageId,
    tSynchronizedProcedure procedure,
    PVOID parameter)
{
    tSynchronizedContext SyncContext;

    SyncContext.pContext = pContext;
    SyncContext.Parameter = parameter;
    return NdisMSynchronizeWithInterruptEx(
        pContext->InterruptHandle,
        messageId,
        (MINIPORT_SYNCHRONIZE_INTERRUPT_HANDLER)procedure,
        &SyncContext);
}
