/*
 * PROJECT:     ReactOS DC21x4 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Interrupt handling
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "dc21x4.h"

#include <debug.h>

/* FUNCTIONS ******************************************************************/

static
VOID
DcAdjustTxFifoThreshold(
    _In_ PDC21X4_ADAPTER Adapter)
{
    ULONG OpMode;

    TRACE("TX underrun\n");

    /* Maximum threshold reached */
    if ((Adapter->OpMode & DC_OPMODE_STORE_AND_FORWARD) ||
        (++Adapter->TransmitUnderruns < DC_TX_UNDERRUN_LIMIT))
    {
        NdisDprAcquireSpinLock(&Adapter->SendLock);

        /* Start the transmit process if it was suspended */
        DC_WRITE(Adapter, DcCsr1_TxPoll, DC_TX_POLL_DOORBELL);

        NdisDprReleaseSpinLock(&Adapter->SendLock);
        return;
    }
    Adapter->TransmitUnderruns = 0;

    NdisDprAcquireSpinLock(&Adapter->ModeLock);

    OpMode = Adapter->OpMode;

    /* Update the FIFO threshold level to minimize Tx FIFO underrun */
    if ((OpMode & DC_OPMODE_TX_THRESHOLD_CTRL_MASK) != DC_OPMODE_TX_THRESHOLD_MAX)
    {
        OpMode += DC_OPMODE_TX_THRESHOLD_LEVEL;

        INFO("New OP Mode %08lx\n", OpMode);
    }
    else
    {
        OpMode |= DC_OPMODE_STORE_AND_FORWARD;

        INFO("Store & Forward\n");
    }

    DcStopTxRxProcess(Adapter);

    Adapter->OpMode = OpMode;

    /* Restart the transmit process */
    DC_WRITE(Adapter, DcCsr6_OpMode, OpMode);

    NdisDprReleaseSpinLock(&Adapter->ModeLock);
}

static
VOID
DcHandleTxJabberTimeout(
    _In_ PDC21X4_ADAPTER Adapter)
{
    WARN("Transmit jabber timer timed out\n");

    NdisWriteErrorLogEntry(Adapter->AdapterHandle, NDIS_ERROR_CODE_HARDWARE_FAILURE, 1, __LINE__);

    NdisDprAcquireSpinLock(&Adapter->ModeLock);

    /* Start the transmit process if it was stopped */
    DC_WRITE(Adapter, DcCsr6_OpMode, Adapter->OpMode);

    NdisDprReleaseSpinLock(&Adapter->ModeLock);
}

static
VOID
DcHandleTxCompletedFrames(
    _In_ PDC21X4_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail,
    _Out_ PULONG DpcEvents)
{
    PDC_TCB Tcb;
    PDC_TBD Tbd;
    ULONG TbdStatus, Collisions;

    for (Tcb = Adapter->LastTcb;
         Tcb != Adapter->CurrentTcb;
         Tcb = DC_NEXT_TCB(Adapter, Tcb))
    {
        Tbd = Tcb->Tbd;
        TbdStatus = Tbd->Status;

        if (TbdStatus & DC_TBD_STATUS_OWNED)
            break;

        ++Adapter->TcbCompleted;

        /* Complete the packet filter change request asynchronously */
        if (Tbd->Control & DC_TBD_CONTROL_SETUP_FRAME)
        {
            Tbd->Control &= ~DC_TBD_CONTROL_SETUP_FRAME;

            if (Tbd->Control & DC_TBD_CONTROL_REQUEST_INTERRUPT)
            {
                *DpcEvents |= DC_EVENT_SETUP_FRAME_COMPLETED;
            }

            continue;
        }

        /* This is our media test packet, so no need to update the TX statistics */
        if (!Tcb->Nbl)
        {
            _InterlockedExchange(&Adapter->MediaTestStatus,
                                 !(TbdStatus & DC_TBD_STATUS_ERROR_SUMMARY));

            ASSERT(Adapter->LoopbackFrameSlots < DC_LOOPBACK_FRAMES);

            ++Adapter->LoopbackFrameSlots;

            continue;
        }

        if (TbdStatus & DC_TBD_STATUS_ERROR_SUMMARY)
        {
            ++Adapter->Statistics.TransmitErrors;

            if (TbdStatus & DC_TBD_STATUS_UNDERFLOW)
                ++Adapter->Statistics.TransmitUnderrunErrors;
            else if (TbdStatus & DC_TBD_STATUS_LATE_COLLISION)
                ++Adapter->Statistics.TransmitLateCollisions;

            if (TbdStatus & DC_TBD_STATUS_RETRY_ERROR)
                ++Adapter->Statistics.TransmitExcessiveCollisions;
            if (TbdStatus & DC_TBD_STATUS_CARRIER_LOST)
                ++Adapter->Statistics.TransmitLostCarrierSense;
        }
        else
        {
            ++Adapter->Statistics.TransmitOk;

            if (TbdStatus & DC_TBD_STATUS_DEFFERED)
                ++Adapter->Statistics.TransmitDeferred;
            if (TbdStatus & DC_TBD_STATUS_HEARTBEAT_FAIL)
                ++Adapter->Statistics.TransmitHeartbeatErrors;

            Collisions = (TbdStatus & DC_TBD_STATUS_COLLISIONS_MASK) >>
                         DC_TBD_STATUS_COLLISIONS_SHIFT;
            if (Collisions == 1)
                ++Adapter->Statistics.TransmitOneRetry;
            else if (Collisions > 1)
                ++Adapter->Statistics.TransmitMoreCollisions;
        }

        DcCompleteNetBuffer(Tcb->Nbl, CompleteHead, CompleteTail);

        DC_RELEASE_TCB(Adapter, Tcb);
    }

    Adapter->LastTcb = Tcb;
}

static
VOID
DcHandleTx(
    _In_ PDC21X4_ADAPTER Adapter)
{
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    ULONG DpcEvents;

    TRACE("Handle TX\n");

    DpcEvents = 0;

    NdisDprAcquireSpinLock(&Adapter->SendLock);

    DcHandleTxCompletedFrames(Adapter, &CompleteHead, &CompleteTail, &DpcEvents);

    if (Adapter->SendQueueHead)
    {
        DcProcessPendingPackets(Adapter, &CompleteHead, &CompleteTail);
    }

    NdisDprReleaseSpinLock(&Adapter->SendLock);

    DcCompleteSends(Adapter, CompleteHead, TRUE);

    /* We have to complete the OID request outside of the spinlock */
    if (DpcEvents & DC_EVENT_SETUP_FRAME_COMPLETED)
    {
        TRACE("SP completed\n");

        Adapter->OidPending = FALSE;

        DcCompletePendingOid(Adapter, NDIS_STATUS_SUCCESS);
    }
}

static
VOID
DcStopRxProcess(
    _In_ PDC21X4_ADAPTER Adapter)
{
    ULONG i, OpMode, Status;

    OpMode = Adapter->OpMode;
    OpMode &= ~DC_OPMODE_RX_ENABLE;
    DC_WRITE(Adapter, DcCsr6_OpMode, OpMode);

    for (i = 0; i < 5000; ++i)
    {
        Status = DC_READ(Adapter, DcCsr5_Status);

        if ((Status & DC_STATUS_RX_STATE_MASK) == DC_STATUS_RX_STATE_STOPPED)
            return;

        NdisStallExecution(10);
    }

    WARN("Failed to stop the RX process 0x%08lx\n", Status);
}

VOID
NTAPI
DcReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, NextNbl;
    BOOLEAN CompletePause = FALSE;
    PDC_RCB Rcb;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&Adapter->ReceiveLock);

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        Rcb = *DC_RCB_FROM_NBL(Nbl);

        PushEntryList(&Adapter->FreeRcbList, &Rcb->ListEntry);

        ++Adapter->RcbFree;
    }

    if (Adapter->PausePending && (Adapter->RcbFree == Adapter->RcbCount))
    {
        Adapter->PausePending = FALSE;
        CompletePause = TRUE;
    }

    NdisReleaseSpinLock(&Adapter->ReceiveLock);

    if (CompletePause)
        NdisMPauseComplete(Adapter->AdapterHandle);
}

static
BOOLEAN
DcIsReclaimNbl(
    _In_ PNET_BUFFER_LIST Nbl)
{
    return !!((*DC_RCB_FROM_NBL(Nbl))->Flags & DC_RCB_FLAG_RECLAIM);
}

static
VOID
DcIndicateReceivePackets(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST* ReceiveArray,
    _In_ ULONG PacketsToIndicate)
{
    PNET_BUFFER_LIST Nbl;
    PDC_RBD Rbd;
    PDC_RCB Rcb;
    ULONG i, j, Flags;
    BOOLEAN Reclaim;

    NdisDprReleaseSpinLock(&Adapter->ReceiveLock);

    for (i = 0; i < PacketsToIndicate; i = j)
    {
        Reclaim = DcIsReclaimNbl(ReceiveArray[i]);

        for (j = i + 1; j < PacketsToIndicate; ++j)
        {
            if (DcIsReclaimNbl(ReceiveArray[j]) != Reclaim)
                break;

            NET_BUFFER_LIST_NEXT_NBL(ReceiveArray[j - 1]) = ReceiveArray[j];
        }
        NET_BUFFER_LIST_NEXT_NBL(ReceiveArray[j - 1]) = NULL;

        Flags = NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;
        if (Reclaim)
            Flags |= NDIS_RECEIVE_FLAGS_RESOURCES;

        NdisMIndicateReceiveNetBufferLists(Adapter->AdapterHandle,
                                           ReceiveArray[i],
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           j - i,
                                           Flags);
    }

    NdisDprAcquireSpinLock(&Adapter->ReceiveLock);

    for (i = 0; i < PacketsToIndicate; ++i)
    {
        Nbl = ReceiveArray[i];
        Rcb = *DC_RCB_FROM_NBL(Nbl);

        /* Reuse the RCB immediately */
        if (Rcb->Flags & DC_RCB_FLAG_RECLAIM)
        {
            Rbd = *DC_RBD_FROM_NBL(Nbl);

            Rbd->Status = DC_RBD_STATUS_OWNED;
        }
    }
}

static
VOID
DcHandleRxReceivedFrames(
    _In_ PDC21X4_ADAPTER Adapter)
{
    PDC_RBD Rbd, StartRbd, LastRbd;
    ULONG PacketsToIndicate;
    PNET_BUFFER_LIST ReceiveArray[DC_RECEIVE_ARRAY_SIZE];

    Rbd = StartRbd = LastRbd = Adapter->CurrentRbd;
    PacketsToIndicate = 0;

    while (PacketsToIndicate < RTL_NUMBER_OF(ReceiveArray))
    {
        PDC_RCB Rcb;
        PDC_RCB* RcbSlot;
        PNET_BUFFER_LIST Nbl;
        ULONG RbdStatus, PacketLength, RxCounters;

        if (Rbd->Status & DC_RBD_STATUS_OWNED)
            break;

        /* Work around the RX overflow bug */
        if ((Adapter->Features & DC_NEED_RX_OVERFLOW_WORKAROUND) && (Rbd == LastRbd))
        {
            /* Find the last received packet, to correctly catch invalid packets */
            do
            {
                LastRbd = DC_NEXT_RBD(Adapter, LastRbd);

                if (LastRbd->Status & DC_RBD_STATUS_OWNED)
                    break;
            }
            while (LastRbd != Rbd);

            RxCounters = DC_READ(Adapter, DcCsr8_RxCounters);

            Adapter->Statistics.ReceiveNoBuffers += RxCounters & DC_COUNTER_RX_NO_BUFFER_MASK;

            /* A receive overflow might indicate a data corruption */
            if (RxCounters & DC_COUNTER_RX_OVERFLOW_MASK)
            {
                ERR("RX overflow, dropping the packets\n");

                Adapter->Statistics.ReceiveOverrunErrors +=
                    (RxCounters & DC_COUNTER_RX_OVERFLOW_MASK) >> DC_COUNTER_RX_OVERFLOW_SHIFT;

                NdisDprAcquireSpinLock(&Adapter->ModeLock);

                /* Stop the receive process */
                DcStopRxProcess(Adapter);

                /* Drop all received packets regardless of what the status indicates */
                while (TRUE)
                {
                    if (Rbd->Status & DC_RBD_STATUS_OWNED)
                        break;

                    ++Adapter->Statistics.ReceiveOverrunErrors;

                    Rbd->Status = DC_RBD_STATUS_OWNED;

                    Rbd = DC_NEXT_RBD(Adapter, Rbd);
                }
                LastRbd = Rbd;

                /* Restart the receive process */
                DC_WRITE(Adapter, DcCsr6_OpMode, Adapter->OpMode);

                NdisDprReleaseSpinLock(&Adapter->ModeLock);

                continue;
            }
        }

        RbdStatus = Rbd->Status;

        /* Ignore oversized packets */
        if (!(RbdStatus & DC_RBD_STATUS_LAST_DESCRIPTOR))
        {
            Rbd->Status = DC_RBD_STATUS_OWNED;
            goto NextRbd;
        }

        /* Check for an invalid packet */
        if (RbdStatus & DC_RBD_STATUS_INVALID)
        {
            ++Adapter->Statistics.ReceiveErrors;

            if (RbdStatus & DC_RBD_STATUS_OVERRUN)
                ++Adapter->Statistics.ReceiveOverrunErrors;

            if (RbdStatus & DC_RBD_STATUS_CRC_ERROR)
            {
                if (RbdStatus & DC_RBD_STATUS_DRIBBLE)
                    ++Adapter->Statistics.ReceiveAlignmentErrors;
                else
                    ++Adapter->Statistics.ReceiveCrcErrors;
            }

            Rbd->Status = DC_RBD_STATUS_OWNED;
            goto NextRbd;
        }

        if (Adapter->Paused)
        {
            Rbd->Status = DC_RBD_STATUS_OWNED;
            goto NextRbd;
        }

        ++Adapter->Statistics.ReceiveOk;

        PacketLength = (RbdStatus & DC_RBD_STATUS_FRAME_LENGTH_MASK) >>
                       DC_RBD_STATUS_FRAME_LENGTH_SHIFT;

        /* Omit the CRC */
        PacketLength -= 4;

        RcbSlot = DC_GET_RCB_SLOT(Adapter, Rbd);
        Rcb = *RcbSlot;

        TRACE("RX packet (len %u), RCB %p\n", PacketLength, Rcb);

        NdisAdjustMdlLength(Rcb->Mdl, PacketLength);
        NET_BUFFER_DATA_LENGTH(NET_BUFFER_LIST_FIRST_NB(Rcb->Nbl)) = PacketLength;

        /* Receive buffers are in cached memory */
        KeFlushIoBuffers(Rcb->Mdl, TRUE, TRUE);

        if (RbdStatus & DC_RBD_STATUS_MULTICAST)
        {
            if (ETH_IS_BROADCAST(Rcb->VirtualAddress))
                ++Adapter->Statistics.ReceiveBroadcast;
            else
                ++Adapter->Statistics.ReceiveMulticast;
        }
        else
        {
            ++Adapter->Statistics.ReceiveUnicast;
        }

        Nbl = Rcb->Nbl;

        ReceiveArray[PacketsToIndicate++] = Nbl;

        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;

        if (Adapter->FreeRcbList.Next)
        {
            Rcb->Flags = 0;

            Rcb = (PDC_RCB)DcPopEntryList(&Adapter->FreeRcbList);
            *RcbSlot = Rcb;

            ASSERT(Adapter->RcbFree > 0);
            --Adapter->RcbFree;

            Rbd->Address1 = Rcb->PhysicalAddress;
            DC_WRITE_BARRIER();
            Rbd->Status = DC_RBD_STATUS_OWNED;
        }
        else
        {
            Rcb->Flags = DC_RCB_FLAG_RECLAIM;

            *DC_RBD_FROM_NBL(Nbl) = Rbd;
        }

NextRbd:
        Rbd = DC_NEXT_RBD(Adapter, Rbd);

        /*
         * Check the next descriptor to prevent wrap-around.
         * Since we don't use a fixed-sized ring,
         * the receive ring may be smaller in length than the ReceiveArray[].
         */
        if (Rbd == StartRbd)
            break;
    }

    Adapter->CurrentRbd = Rbd;

    /* Pass the packets up */
    if (PacketsToIndicate)
    {
        DcIndicateReceivePackets(Adapter, ReceiveArray, PacketsToIndicate);
    }
}

static
VOID
DcHandleRx(
    _In_ PDC21X4_ADAPTER Adapter)
{
    NdisDprAcquireSpinLock(&Adapter->ReceiveLock);

    do
    {
        DcHandleRxReceivedFrames(Adapter);
    }
    while (!(Adapter->CurrentRbd->Status & DC_RBD_STATUS_OWNED));

    NdisDprReleaseSpinLock(&Adapter->ReceiveLock);
}

static
VOID
DcHandleSystemError(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ ULONG InterruptStatus)
{
    ERR("%s error occured, CSR5 %08lx\n", DcDbgBusError(InterruptStatus), InterruptStatus);

    NdisWriteErrorLogEntry(Adapter->AdapterHandle, NDIS_ERROR_CODE_HARDWARE_FAILURE, 1, __LINE__);

    /* Issue a software reset, which also enables the interrupts */
    if (_InterlockedCompareExchange(&Adapter->ResetLock, 2, 0) == 0)
    {
        NdisQueueIoWorkItem(Adapter->ResetWorkItem, DcResetWorker, Adapter);
    }
}

VOID
NTAPI
DcInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    ULONG InterruptStatus, IoLimit;
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    TRACE("Events %08lx\n", Adapter->InterruptStatus);

    if (!(Adapter->Flags & DC_ACTIVE))
        return;

    IoLimit = DC_INTERRUPT_PROCESSING_LIMIT;
    InterruptStatus = Adapter->InterruptStatus;

    /* Loop until the condition to stop is encountered */
    while (TRUE)
    {
        /* Uncommon interrupts */
        if (InterruptStatus & DC_IRQ_ABNORMAL_SUMMARY)
        {
            /* PCI bus error detected */
            if (InterruptStatus & DC_IRQ_SYSTEM_ERROR)
            {
                DcHandleSystemError(Adapter, InterruptStatus);
                return;
            }

            /* Transmit jabber timeout */
            if (InterruptStatus & DC_IRQ_TX_JABBER_TIMEOUT)
            {
                DcHandleTxJabberTimeout(Adapter);
            }

            /* Link state changed */
            if (InterruptStatus & Adapter->LinkStateChangeMask)
            {
                Adapter->HandleLinkStateChange(Adapter, InterruptStatus);
            }
        }

        /* Handling receive interrupts */
        if (InterruptStatus & (DC_IRQ_RX_OK | DC_IRQ_RX_STOPPED))
        {
            DcHandleRx(Adapter);
        }

        /* Handling transmit interrupts */
        if (InterruptStatus & (DC_IRQ_TX_OK | DC_IRQ_TX_STOPPED))
        {
            DcHandleTx(Adapter);
        }

        /* Transmit underflow error detected */
        if (InterruptStatus & DC_IRQ_TX_UNDERFLOW)
        {
            DcAdjustTxFifoThreshold(Adapter);
        }

        /* Limit in order to avoid doing too much work at DPC level */
        if (!--IoLimit)
            break;

        /* Check if new events have occurred */
        InterruptStatus = DC_READ(Adapter, DcCsr5_Status);
        if (InterruptStatus == 0xFFFFFFFF || !(InterruptStatus & Adapter->InterruptMask))
            break;

        /* Acknowledge the events */
        DC_WRITE(Adapter, DcCsr5_Status, InterruptStatus);
    }

    /* TODO: Add interrupt mitigation (CSR11) */

    /* Reenable interrupts */
    _InterlockedExchange((PLONG)&Adapter->CurrentInterruptMask, Adapter->InterruptMask);
    DC_WRITE(Adapter, DcCsr7_IrqMask, Adapter->InterruptMask);
}

BOOLEAN
NTAPI
DcIsr(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportInterruptContext;
    ULONG InterruptStatus;

    *TargetProcessors = 0;

    if (Adapter->CurrentInterruptMask == 0)
        goto NotOurs;

    InterruptStatus = DC_READ(Adapter, DcCsr5_Status);
    if (InterruptStatus == 0xFFFFFFFF || !(InterruptStatus & Adapter->CurrentInterruptMask))
        goto NotOurs;

    /* Disable further interrupts */
    DC_WRITE(Adapter, DcCsr7_IrqMask, 0);

    /* Clear all pending events */
    DC_WRITE(Adapter, DcCsr5_Status, InterruptStatus);

    Adapter->InterruptStatus = InterruptStatus;
    Adapter->CurrentInterruptMask = 0;

    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;

NotOurs:
    *QueueDefaultInterruptDpc = FALSE;
    return FALSE;
}

VOID
NTAPI
DcDisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportInterruptContext;

    DC_WRITE(Adapter, DcCsr7_IrqMask, 0);
}

VOID
NTAPI
DcEnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportInterruptContext;

    DC_WRITE(Adapter, DcCsr7_IrqMask, Adapter->CurrentInterruptMask);
}
