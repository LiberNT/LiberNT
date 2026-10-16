/*
 * PROJECT:     ReactOS DC21x4 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Packet sending
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "dc21x4.h"

#include <debug.h>

/* FUNCTIONS ******************************************************************/

static
VOID
DcTransmitPacket(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ PDC_TCB Tcb,
    _In_ PSCATTER_GATHER_LIST SgList)
{
    PDC_TBD Tbd, NextTbd = NULL, FirstTbd, LastTbd = NULL;
    ULONG i, TbdStatus;

    TbdStatus = 0;
    Tbd = Adapter->CurrentTbd;

    for (i = 0; i < SgList->NumberOfElements; ++i)
    {
        ULONG Address, Length;

        LastTbd = Tbd;

        /* Not owned by NIC */
        ASSERT(!(((i % 2) == 0) && (Tbd->Status & DC_TBD_STATUS_OWNED)));

        Tbd->Status = TbdStatus;

        /* 32-bit DMA */
        ASSERT(SgList->Elements[i].Address.HighPart == 0);

        Address = SgList->Elements[i].Address.LowPart;
        Length = SgList->Elements[i].Length;

        /* Two data buffers per descriptor */
        if ((i % 2) == 0)
        {
            Tbd->Control &= DC_TBD_CONTROL_END_OF_RING;

            Tbd->Control |= Length;
            Tbd->Address1 = Address;

            NextTbd = DC_NEXT_TBD(Adapter, Tbd);
        }
        else
        {
            Tbd->Control |= Length << DC_TBD_CONTROL_LENGTH_2_SHIFT;
            Tbd->Address2 = Address;

            Tbd = NextTbd;
            TbdStatus = DC_TBD_STATUS_OWNED;
        }
    }

    /* Enable IRQ on last element */
    LastTbd->Control |= DC_TBD_CONTROL_LAST_FRAGMENT | DC_TBD_CONTROL_REQUEST_INTERRUPT;

    Tcb->Tbd = LastTbd;

    FirstTbd = Adapter->CurrentTbd;
    Adapter->CurrentTbd = NextTbd;

    /* Not owned by NIC */
    ASSERT(!(FirstTbd->Status & DC_TBD_STATUS_OWNED));

    FirstTbd->Control |= DC_TBD_CONTROL_FIRST_FRAGMENT;
    DC_WRITE_BARRIER();
    FirstTbd->Status = DC_TBD_STATUS_OWNED;
}

static
BOOLEAN
DcCopyPacket(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ PDC_COALESCE_BUFFER Buffer)
{
    PMDL CurrentMdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG CurrentLength;
    PUCHAR Address;
    PUCHAR Destination;

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
DcSendPacket(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST Nbl,
    _In_ PNET_BUFFER NetBuffer)
{
    PSCATTER_GATHER_LIST SgList;
    PDC_COALESCE_BUFFER CoalesceBuffer;
    PDC_TCB Tcb;
    ULONG PacketLength;

    if (!Adapter->TcbSlots || !Adapter->TbdSlots || !Adapter->SendBufferList.Next)
        return NDIS_STATUS_RESOURCES;

    PacketLength = NET_BUFFER_DATA_LENGTH(NetBuffer);
    if (PacketLength == 0 || PacketLength > DC_TRANSMIT_BLOCK_SIZE)
        return NDIS_STATUS_INVALID_LENGTH;

    CoalesceBuffer = (PDC_COALESCE_BUFFER)PopEntryList(&Adapter->SendBufferList);

    if (!DcCopyPacket(Adapter, NetBuffer, CoalesceBuffer))
    {
        PushEntryList(&Adapter->SendBufferList, &CoalesceBuffer->ListEntry);
        return NDIS_STATUS_FAILURE;
    }

    SgList = (PSCATTER_GATHER_LIST)&Adapter->LocalSgList;
    SgList->Elements[0].Address.LowPart = CoalesceBuffer->PhysicalAddress;
    SgList->Elements[0].Length = PacketLength;
    SgList->NumberOfElements = 1;

    Tcb = Adapter->CurrentTcb;
    Tcb->SlotsUsed = 1;
    Tcb->Buffer = CoalesceBuffer;

    --Adapter->TcbSlots;

    Adapter->CurrentTcb = DC_NEXT_TCB(Adapter, Tcb);

    Tcb->Nbl = Nbl;

    ASSERT(Adapter->TbdSlots >= Tcb->SlotsUsed);
    Adapter->TbdSlots -= Tcb->SlotsUsed;

    DcTransmitPacket(Adapter, Tcb, SgList);

    DC_WRITE(Adapter, DcCsr1_TxPoll, DC_TX_POLL_DOORBELL);

    return NDIS_STATUS_SUCCESS;
}

VOID
DcCompleteNetBuffer(
    _In_ PNET_BUFFER_LIST Nbl,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    if (--DC_PENDING_FROM_NBL(Nbl) != 0)
        return;

    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    if (*CompleteTail)
        NET_BUFFER_LIST_NEXT_NBL(*CompleteTail) = Nbl;
    else
        *CompleteHead = Nbl;
    *CompleteTail = Nbl;
}

VOID
DcCompleteSends(
    _In_ PDC21X4_ADAPTER Adapter,
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
DcProcessPendingPackets(
    _In_ PDC21X4_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;

    while ((Nbl = Adapter->SendQueueHead) != NULL)
    {
        NetBuffer = DC_NEXT_NB_FROM_NBL(Nbl);

        Status = DcSendPacket(Adapter, Nbl, NetBuffer);
        if (Status == NDIS_STATUS_RESOURCES)
            break;

        DC_NEXT_NB_FROM_NBL(Nbl) = NET_BUFFER_NEXT_NB(NetBuffer);
        if (!DC_NEXT_NB_FROM_NBL(Nbl))
        {
            Adapter->SendQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
            if (!Adapter->SendQueueHead)
                Adapter->SendQueueTail = NULL;
        }

        if (Status != NDIS_STATUS_SUCCESS)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = Status;
            DcCompleteNetBuffer(Nbl, CompleteHead, CompleteTail);
        }
    }
}

VOID
NTAPI
DcSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;
    LONG_PTR Count;

    UNREFERENCED_PARAMETER(PortNumber);

    NdisAcquireSpinLock(&Adapter->SendLock);

    if (Adapter->Paused)
        Status = NDIS_STATUS_PAUSED;
    else if (!(Adapter->Flags & DC_ACTIVE))
        Status = NDIS_STATUS_NOT_ACCEPTED;
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
            DC_PENDING_FROM_NBL(Nbl) = 1;
            DcCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
            continue;
        }

        DC_PENDING_FROM_NBL(Nbl) = Count;
        DC_NEXT_NB_FROM_NBL(Nbl) = NET_BUFFER_LIST_FIRST_NB(Nbl);

        if (Adapter->SendQueueTail)
            NET_BUFFER_LIST_NEXT_NBL(Adapter->SendQueueTail) = Nbl;
        else
            Adapter->SendQueueHead = Nbl;
        Adapter->SendQueueTail = Nbl;
    }

    TRACE("Send packets\n");

    DcProcessPendingPackets(Adapter, &CompleteHead, &CompleteTail);

    NdisReleaseSpinLock(&Adapter->SendLock);

    DcCompleteSends(Adapter, CompleteHead, NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags));
}

VOID
NTAPI
DcCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl, KeepHead = NULL, KeepTail = NULL;
    PNET_BUFFER NetBuffer;

    TRACE("Called\n");

    NdisAcquireSpinLock(&Adapter->SendLock);

    for (Nbl = Adapter->SendQueueHead; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId &&
            DC_NEXT_NB_FROM_NBL(Nbl) == NET_BUFFER_LIST_FIRST_NB(Nbl))
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;

            for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
                 NetBuffer;
                 NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
            {
                DcCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
            }
            continue;
        }

        if (KeepTail)
            NET_BUFFER_LIST_NEXT_NBL(KeepTail) = Nbl;
        else
            KeepHead = Nbl;
        KeepTail = Nbl;
    }

    Adapter->SendQueueHead = KeepHead;
    Adapter->SendQueueTail = KeepTail;

    NdisReleaseSpinLock(&Adapter->SendLock);

    DcCompleteSends(Adapter, CompleteHead, FALSE);
}
