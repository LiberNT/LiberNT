/*
 * PROJECT:     ReactOS nVidia nForce Ethernet Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Packet sending
 * COPYRIGHT:   Copyright 2021-2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "nvnet.h"

#define NDEBUG
#include "debug.h"

/* FUNCTIONS ******************************************************************/

VOID
NvNetTransmitPacket32(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ PNVNET_TCB Tcb,
    _In_ PSCATTER_GATHER_LIST SgList)
{
    NVNET_TBD Tbd, LastTbd = {0};
    ULONG i, Flags;
    ULONG Slots;

    Flags = 0;
    Slots = 0;
    Tbd = Adapter->Send.CurrentTbd;

    for (i = 0; i < SgList->NumberOfElements; ++i)
    {
        ULONG Address = NdisGetPhysicalAddressLow(SgList->Elements[i].Address);
        ULONG Length = SgList->Elements[i].Length;

        if (Length > NV_MAXIMUM_SG_SIZE)
        {
            ULONG ImplicitEntries = NV_IMPLICIT_ENTRIES(Length);

            do
            {
                ++Slots;

                Tbd.x32->Address = Address;
                Tbd.x32->FlagsLength = Flags | (NV_MAXIMUM_SG_SIZE - 1);
                LastTbd = Tbd;
                Tbd = NV_NEXT_TBD_32(Adapter, Tbd);

                Flags = NV_TX_VALID;

                Length -= NV_MAXIMUM_SG_SIZE;
                Address += NV_MAXIMUM_SG_SIZE;

                --ImplicitEntries;
            }
            while (ImplicitEntries);
        }

        ++Slots;

        Tbd.x32->Address = Address;
        Tbd.x32->FlagsLength = Flags | (Length - 1);
        LastTbd = Tbd;
        Tbd = NV_NEXT_TBD_32(Adapter, Tbd);

        Flags = NV_TX_VALID;
    }

    Tcb->Slots = Slots;
    Tcb->Tbd = LastTbd;

    if (Adapter->Features & DEV_HAS_LARGEDESC)
    {
        LastTbd.x32->FlagsLength |= NV_TX2_LASTPACKET;
    }
    else
    {
        LastTbd.x32->FlagsLength |= NV_TX_LASTPACKET;
    }

    if (Tcb->Flags & NV_TCB_LARGE_SEND)
    {
        Flags |= (Tcb->Mss << NV_TX2_TSO_SHIFT) | NV_TX2_TSO;
    }
    else
    {
        if (Tcb->Flags & NV_TCB_CHECKSUM_IP)
        {
            Flags |= NV_TX2_CHECKSUM_L3;
        }
        if (Tcb->Flags & (NV_TCB_CHECKSUM_TCP | NV_TCB_CHECKSUM_UDP))
        {
            Flags |= NV_TX2_CHECKSUM_L4;
        }
    }

    Adapter->Send.CurrentTbd.x32->FlagsLength |= Flags;
    Adapter->Send.CurrentTbd = Tbd;

    NV_WRITE(Adapter, NvRegTxRxControl, Adapter->TxRxControl | NVREG_TXRXCTL_KICK);
}

VOID
NvNetTransmitPacket64(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ PNVNET_TCB Tcb,
    _In_ PSCATTER_GATHER_LIST SgList)
{
    NVNET_TBD Tbd, LastTbd = {0};
    ULONG i, Flags;
    ULONG Slots;

    Flags = 0;
    Slots = 0;
    Tbd = Adapter->Send.CurrentTbd;

    for (i = 0; i < SgList->NumberOfElements; ++i)
    {
        ULONG64 Address = SgList->Elements[i].Address.QuadPart;
        ULONG Length = SgList->Elements[i].Length;

        if (Length > NV_MAXIMUM_SG_SIZE)
        {
            ULONG ImplicitEntries = NV_IMPLICIT_ENTRIES(Length);

            do
            {
                ++Slots;

                Tbd.x64->AddressLow = (ULONG)Address;
                Tbd.x64->AddressHigh = Address >> 32;
                Tbd.x64->VlanTag = 0;
                Tbd.x64->FlagsLength = Flags | (NV_MAXIMUM_SG_SIZE - 1);
                LastTbd = Tbd;
                Tbd = NV_NEXT_TBD_64(Adapter, Tbd);

                Flags = NV_TX2_VALID;

                Length -= NV_MAXIMUM_SG_SIZE;
                Address += NV_MAXIMUM_SG_SIZE;

                --ImplicitEntries;
            }
            while (ImplicitEntries);
        }

        ++Slots;

        Tbd.x64->AddressLow = (ULONG)Address;
        Tbd.x64->AddressHigh = Address >> 32;
        Tbd.x64->VlanTag = 0;
        Tbd.x64->FlagsLength = Flags | (Length - 1);
        LastTbd = Tbd;
        Tbd = NV_NEXT_TBD_64(Adapter, Tbd);

        Flags = NV_TX2_VALID;
    }

    Tcb->Slots = Slots;
    Tcb->Tbd = LastTbd;

    LastTbd.x64->FlagsLength |= NV_TX2_LASTPACKET;

    if (Adapter->Flags & NV_SEND_ERRATA_PRESENT)
    {
        if (Adapter->Send.PacketsCount == NV_TX_LIMIT_COUNT)
        {
            Tcb->DeferredTbd = Adapter->Send.CurrentTbd;

            if (!Adapter->Send.DeferredTcb)
            {
                Adapter->Send.DeferredTcb = Tcb;
            }

            Flags = 0;
        }
        else
        {
            ++Adapter->Send.PacketsCount;
        }
    }

    if (Tcb->Flags & NV_TCB_LARGE_SEND)
    {
        Flags |= (Tcb->Mss << NV_TX2_TSO_SHIFT) | NV_TX2_TSO;
    }
    else
    {
        if (Tcb->Flags & NV_TCB_CHECKSUM_IP)
        {
            Flags |= NV_TX2_CHECKSUM_L3;
        }
        if (Tcb->Flags & (NV_TCB_CHECKSUM_TCP | NV_TCB_CHECKSUM_UDP))
        {
            Flags |= NV_TX2_CHECKSUM_L4;
        }
    }

    // Adapter->Send.CurrentTbd.x64->VlanTag = NV_TX3_VLAN_TAG_PRESENT; TODO
    Adapter->Send.CurrentTbd.x64->FlagsLength |= Flags;
    Adapter->Send.CurrentTbd = Tbd;

    NV_WRITE(Adapter, NvRegTxRxControl, Adapter->TxRxControl | NVREG_TXRXCTL_KICK);
}

static
BOOLEAN
NvNetCopyPacket(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ PNVNET_TX_BUFFER Buffer)
{
    PMDL CurrentMdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG CurrentLength;
    PUCHAR Address;
    PUCHAR Destination;

    UNREFERENCED_PARAMETER(Adapter);

    Destination = Buffer->VirtualAddress;

    while (CurrentMdl && Remaining)
    {
        NdisQueryMdl(CurrentMdl, (PVOID*)&Address, &CurrentLength, HighPagePriority);
        if (!Address)
            return FALSE;

        if (CurrentLength > Offset)
        {
            CurrentLength = min(CurrentLength - Offset, Remaining);
            NdisMoveMemory(Destination, Address + Offset, CurrentLength);
            Destination += CurrentLength;
            Remaining -= CurrentLength;
            Offset = 0;
        }
        else
        {
            Offset -= CurrentLength;
        }

        NdisGetNextMdl(CurrentMdl, &CurrentMdl);
    }

    return (Remaining == 0);
}

static
NDIS_STATUS
NvNetSendPacket(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST Nbl,
    _In_ PNET_BUFFER NetBuffer)
{
    union
    {
        SCATTER_GATHER_LIST List;
        UCHAR Buffer[FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) + sizeof(SCATTER_GATHER_ELEMENT)];
    } LocalSgList;
    PNVNET_TX_BUFFER CoalesceBuffer;
    PNVNET_TCB Tcb;
    ULONG TotalLength, Slots;

    TotalLength = NET_BUFFER_DATA_LENGTH(NetBuffer);
    if (TotalLength == 0 || TotalLength > Adapter->MaximumFrameSize)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Slots = 1;
    if (TotalLength > NV_MAXIMUM_SG_SIZE)
    {
        Slots += NV_IMPLICIT_ENTRIES(TotalLength);
    }

    if (!Adapter->Send.TcbSlots || Slots > Adapter->Send.TbdSlots || !Adapter->Send.BufferList.Next)
    {
        return NDIS_STATUS_RESOURCES;
    }

    CoalesceBuffer = (PNVNET_TX_BUFFER)PopEntryList(&Adapter->Send.BufferList);

    if (!NvNetCopyPacket(Adapter, NetBuffer, CoalesceBuffer))
    {
        PushEntryList(&Adapter->Send.BufferList, &CoalesceBuffer->Link);
        return NDIS_STATUS_FAILURE;
    }

    --Adapter->Send.TcbSlots;

    LocalSgList.List.NumberOfElements = 1;
    LocalSgList.List.Elements[0].Address = CoalesceBuffer->PhysicalAddress;
    LocalSgList.List.Elements[0].Length = TotalLength;

    Tcb = Adapter->Send.CurrentTcb;
    Tcb->Buffer = CoalesceBuffer;
    Tcb->Nbl = Nbl;
    Tcb->Flags = NV_TCB_COALESCE;

    Adapter->TransmitPacket(Adapter, Tcb, &LocalSgList.List);

    ASSERT(Adapter->Send.TbdSlots >= Tcb->Slots);
    Adapter->Send.TbdSlots -= Tcb->Slots;

    Adapter->Send.CurrentTcb = NV_NEXT_TCB(Adapter, Tcb);

    return NDIS_STATUS_SUCCESS;
}

VOID
NvNetCompleteNetBuffer(
    _In_ PNET_BUFFER_LIST Nbl,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    if (--NV_PENDING_FROM_NBL(Nbl) != 0)
        return;

    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    if (*CompleteTail)
        NET_BUFFER_LIST_NEXT_NBL(*CompleteTail) = Nbl;
    else
        *CompleteHead = Nbl;
    *CompleteTail = Nbl;
}

VOID
NvNetCompleteSends(
    _In_ PNVNET_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST CompleteHead,
    _In_ BOOLEAN DispatchLevel)
{
    if (!CompleteHead)
        return;

    NdisMSendNetBufferListsComplete(Adapter->AdapterHandle,
                                    CompleteHead,
                                    DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

VOID
NvNetProcessSendQueue(
    _In_ PNVNET_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;

    while ((Nbl = Adapter->Send.QueueHead) != NULL)
    {
        NetBuffer = NV_NEXT_NB_FROM_NBL(Nbl);

        Status = NvNetSendPacket(Adapter, Nbl, NetBuffer);
        if (Status == NDIS_STATUS_RESOURCES)
            break;

        NV_NEXT_NB_FROM_NBL(Nbl) = NET_BUFFER_NEXT_NB(NetBuffer);
        if (!NV_NEXT_NB_FROM_NBL(Nbl))
        {
            Adapter->Send.QueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
            if (!Adapter->Send.QueueHead)
                Adapter->Send.QueueTail = NULL;
        }

        if (Status != NDIS_STATUS_SUCCESS)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = Status;
            NvNetCompleteNetBuffer(Nbl, CompleteHead, CompleteTail);
        }
    }
}

VOID
NvNetFailNetBufferLists(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ NDIS_STATUS CompleteStatus)
{
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PNET_BUFFER NetBuffer;

    NdisAcquireSpinLock(&Adapter->Send.Lock);

    for (Nbl = Adapter->Send.QueueHead; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        NET_BUFFER_LIST_STATUS(Nbl) = CompleteStatus;
        for (NetBuffer = NV_NEXT_NB_FROM_NBL(Nbl);
             NetBuffer;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            NvNetCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
        }
    }
    Adapter->Send.QueueHead = NULL;
    Adapter->Send.QueueTail = NULL;

    NdisReleaseSpinLock(&Adapter->Send.Lock);

    NvNetCompleteSends(Adapter, CompleteHead, FALSE);
}

VOID
NTAPI
MiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;
    LONG_PTR Count;

    UNREFERENCED_PARAMETER(PortNumber);

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    NdisAcquireSpinLock(&Adapter->Send.Lock);

    if (Adapter->Paused)
        Status = NDIS_STATUS_PAUSED;
    else if (!(Adapter->Flags & NV_ACTIVE))
        Status = NDIS_STATUS_FAILURE;
    else
        Status = NDIS_STATUS_SUCCESS;

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        NET_BUFFER_LIST_STATUS(Nbl) = Status;

        Count = 0;
        for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
             NetBuffer;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            ++Count;
        }

        if (Status != NDIS_STATUS_SUCCESS || !Count)
        {
            NV_PENDING_FROM_NBL(Nbl) = 1;
            NvNetCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
            continue;
        }

        NV_PENDING_FROM_NBL(Nbl) = Count;
        NV_NEXT_NB_FROM_NBL(Nbl) = NET_BUFFER_LIST_FIRST_NB(Nbl);

        if (Adapter->Send.QueueTail)
            NET_BUFFER_LIST_NEXT_NBL(Adapter->Send.QueueTail) = Nbl;
        else
            Adapter->Send.QueueHead = Nbl;
        Adapter->Send.QueueTail = Nbl;
    }

    if (Status == NDIS_STATUS_SUCCESS)
        NvNetProcessSendQueue(Adapter, &CompleteHead, &CompleteTail);

    NdisReleaseSpinLock(&Adapter->Send.Lock);

    NvNetCompleteSends(Adapter, CompleteHead, NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags));
}

VOID
NTAPI
MiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl, KeepHead = NULL, KeepTail = NULL;
    PNET_BUFFER NetBuffer;

    NdisAcquireSpinLock(&Adapter->Send.Lock);

    for (Nbl = Adapter->Send.QueueHead; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId &&
            NV_NEXT_NB_FROM_NBL(Nbl) == NET_BUFFER_LIST_FIRST_NB(Nbl))
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;

            for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
                 NetBuffer;
                 NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
            {
                NvNetCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
            }
            continue;
        }

        if (KeepTail)
            NET_BUFFER_LIST_NEXT_NBL(KeepTail) = Nbl;
        else
            KeepHead = Nbl;
        KeepTail = Nbl;
    }

    Adapter->Send.QueueHead = KeepHead;
    Adapter->Send.QueueTail = KeepTail;

    NdisReleaseSpinLock(&Adapter->Send.Lock);

    NvNetCompleteSends(Adapter, CompleteHead, FALSE);
}
