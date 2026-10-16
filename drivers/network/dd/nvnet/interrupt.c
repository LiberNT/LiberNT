/*
 * PROJECT:     ReactOS nVidia nForce Ethernet Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Interrupt handling
 * COPYRIGHT:   Copyright 2021-2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "nvnet.h"

#define NDEBUG
#include "debug.h"

/* FUNCTIONS ******************************************************************/

ULONG
ProcessTransmitDescriptorsLegacy(
    _In_ PNVNET_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    PNVNET_TCB Tcb = Adapter->Send.LastTcb;
    ULONG TcbProcessed = 0;

    while (Tcb != Adapter->Send.CurrentTcb)
    {
        NVNET_TBD Tbd = Tcb->Tbd;
        ULONG Flags = Tbd.x32->FlagsLength;

        if (Flags & NV_TX_VALID)
            break;

        NDIS_DbgPrint(MIN_TRACE, ("Packet transmitted (flags %lx)\n",
                                  Flags & FLAG_MASK_V1));

        if (Flags & NV_TX_ERROR)
        {
            ++Adapter->Statistics.TransmitErrors;

            if (Flags & NV_TX_UNDERFLOW)
                ++Adapter->Statistics.TransmitUnderrunErrors;
            if (Flags & NV_TX_LATECOLLISION)
                ++Adapter->Statistics.TransmitLateCollisions;
            if (Flags & NV_TX_CARRIERLOST)
                ++Adapter->Statistics.TransmitLostCarrierSense;
            if (Flags & NV_TX_RETRYERROR)
            {
                ++Adapter->Statistics.TransmitExcessiveCollisions;

                if (!(Flags & NV_TX_RETRYCOUNT_MASK))
                {
                    NvNetBackoffReseed(Adapter);
                }
            }
        }
        else
        {
            ++Adapter->Statistics.TransmitOk;

            if (Flags & NV_TX_DEFERRED)
            {
                ++Adapter->Statistics.TransmitDeferred;
            }
            if (!(Flags & NV_TX_RETRYCOUNT_MASK))
                ++Adapter->Statistics.TransmitZeroRetry;
            else if ((Flags & NV_TX_RETRYCOUNT_MASK) == NV_TX_ONE_RETRY)
                ++Adapter->Statistics.TransmitOneRetry;
        }

        NvNetCompleteNetBuffer(Tcb->Nbl, CompleteHead, CompleteTail);

        NV_RELEASE_TCB(Adapter, Tcb);

        ++TcbProcessed;

        Tcb = NV_NEXT_TCB(Adapter, Tcb);
    }

    Adapter->Send.LastTcb = Tcb;

    return TcbProcessed;
}

ULONG
ProcessTransmitDescriptors32(
    _In_ PNVNET_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    PNVNET_TCB Tcb = Adapter->Send.LastTcb;
    ULONG TcbProcessed = 0;

    while (Tcb != Adapter->Send.CurrentTcb)
    {
        NVNET_TBD Tbd = Tcb->Tbd;
        ULONG Flags = Tbd.x32->FlagsLength;

        if (Flags & NV_TX_VALID)
            break;

        NDIS_DbgPrint(MIN_TRACE, ("Packet transmitted (flags %lx)\n",
                                  Flags & FLAG_MASK_V2));

        if (Flags & NV_TX2_ERROR)
        {
            if ((Flags & NV_TX2_RETRYERROR) && !(Flags & NV_TX2_RETRYCOUNT_MASK))
            {
                if (Adapter->Features & DEV_HAS_GEAR_MODE)
                    NvNetBackoffReseedEx(Adapter);
                else
                    NvNetBackoffReseed(Adapter);
            }
        }

        NvNetCompleteNetBuffer(Tcb->Nbl, CompleteHead, CompleteTail);

        NV_RELEASE_TCB(Adapter, Tcb);

        ++TcbProcessed;

        Tcb = NV_NEXT_TCB(Adapter, Tcb);
    }

    Adapter->Send.LastTcb = Tcb;

    return TcbProcessed;
}

ULONG
ProcessTransmitDescriptors64(
    _In_ PNVNET_ADAPTER Adapter,
    _Inout_ PNET_BUFFER_LIST* CompleteHead,
    _Inout_ PNET_BUFFER_LIST* CompleteTail)
{
    PNVNET_TCB Tcb = Adapter->Send.LastTcb;
    ULONG TcbProcessed = 0;

    while (Tcb != Adapter->Send.CurrentTcb)
    {
        NVNET_TBD Tbd = Tcb->Tbd;
        ULONG Flags = Tbd.x64->FlagsLength;

        if (Flags & NV_TX_VALID)
            break;

        if (Adapter->Flags & NV_SEND_ERRATA_PRESENT)
        {
            PNVNET_TCB DeferredTcb;

            --Adapter->Send.PacketsCount;

            DeferredTcb = Adapter->Send.DeferredTcb;

            if (DeferredTcb)
            {
                DeferredTcb->DeferredTbd.x64->FlagsLength |= NV_TX2_VALID;

                ++Adapter->Send.PacketsCount;

                Adapter->Send.DeferredTcb = NV_NEXT_TCB(Adapter, DeferredTcb);
                if (Adapter->Send.DeferredTcb == Adapter->Send.CurrentTcb)
                {
                    Adapter->Send.DeferredTcb = NULL;
                }

                NV_WRITE(Adapter, NvRegTxRxControl, Adapter->TxRxControl | NVREG_TXRXCTL_KICK);
            }
        }

        NDIS_DbgPrint(MIN_TRACE, ("Packet transmitted (flags %lx)\n",
                                  Flags & FLAG_MASK_V2));

        if (Flags & NV_TX2_ERROR)
        {
            if ((Flags & NV_TX2_RETRYERROR) && !(Flags & NV_TX2_RETRYCOUNT_MASK))
            {
                if (Adapter->Features & DEV_HAS_GEAR_MODE)
                    NvNetBackoffReseedEx(Adapter);
                else
                    NvNetBackoffReseed(Adapter);
            }
        }

        NvNetCompleteNetBuffer(Tcb->Nbl, CompleteHead, CompleteTail);

        NV_RELEASE_TCB(Adapter, Tcb);

        ++TcbProcessed;

        Tcb = NV_NEXT_TCB(Adapter, Tcb);
    }

    Adapter->Send.LastTcb = Tcb;

    return TcbProcessed;
}

static
BOOLEAN
HandleLengthError(
    _In_ PVOID EthHeader,
    _Inout_ PUSHORT Length)
{
    PUCHAR Buffer = EthHeader;
    ULONG i;

    DBG_UNREFERENCED_LOCAL_VARIABLE(Buffer);

    /* TODO */
    NDIS_DbgPrint(MAX_TRACE, ("() Length error detected (%u): \n", *Length));
    for (i = 0; i < *Length; ++i)
    {
        NDIS_DbgPrint(MAX_TRACE, ("%02x ", Buffer[i]));
    }
    NDIS_DbgPrint(MAX_TRACE, ("\n\n*** Please report it to the team! ***\n\n"));

    return FALSE;
}

/* TODO: This need to be rewritten. I leave it as-is for now */
static
ULONG
ProcessReceiveDescriptors(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ ULONG TotalRxProcessed)
{
    ULONG i, RxProcessed = 0;
    PNET_BUFFER_LIST ReceiveHead = NULL, ReceiveTail = NULL;
    ULONG ReceiveCount = 0;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;

    NdisDprAcquireSpinLock(&Adapter->Receive.Lock);

    for (i = 0; i < NVNET_RECEIVE_DESCRIPTORS; ++i)
    {
        ULONG Flags;
        USHORT Length;
        PUCHAR EthHeader;
        NV_RBD NvRbd;

        if (Adapter->Features & DEV_HAS_HIGH_DMA)
        {
            NvRbd.x64 = &Adapter->Receive.NvRbd.x64[Adapter->CurrentRx];
            Flags = NvRbd.x64->FlagsLength;
        }
        else
        {
            NvRbd.x32 = &Adapter->Receive.NvRbd.x32[Adapter->CurrentRx];
            Flags = NvRbd.x32->FlagsLength;
        }

        if (Flags & NV_RX_AVAIL)
            break;

        if (TotalRxProcessed + RxProcessed >= NVNET_RECEIVE_PROCESSING_LIMIT)
            break;

        if (!Adapter->PacketFilter)
            goto NextDescriptor;

        if (Adapter->Features & (DEV_HAS_HIGH_DMA | DEV_HAS_LARGEDESC))
        {
            if (!(Flags & NV_RX2_DESCRIPTORVALID))
                goto NextDescriptor;

            Length = Flags & LEN_MASK_V2;
            EthHeader = &Adapter->ReceiveBuffer[Adapter->CurrentRx * NVNET_RECEIVE_BUFFER_SIZE];

            if (Flags & NV_RX2_ERROR)
            {
                if ((Flags & NV_RX2_ERROR_MASK) == NV_RX2_ERROR4)
                {
                    if (!HandleLengthError(EthHeader, &Length))
                        goto NextDescriptor;
                }
                else if ((Flags & NV_RX2_ERROR_MASK) == NV_RX2_FRAMINGERR)
                {
                    if (Flags & NV_RX2_SUBTRACT1)
                        --Length;
                }
                else
                {
                    goto NextDescriptor;
                }
            }

            NDIS_DbgPrint(MIN_TRACE, ("Packet %d received (length %d, flags %lx)\n",
                                      Adapter->CurrentRx, Length, Flags & FLAG_MASK_V2));
        }
        else
        {
            if (!(Flags & NV_RX_DESCRIPTORVALID))
                goto NextDescriptor;

            Length = Flags & LEN_MASK_V1;
            EthHeader = &Adapter->ReceiveBuffer[Adapter->CurrentRx * NVNET_RECEIVE_BUFFER_SIZE];

            if (Flags & NV_RX_ERROR)
            {
                if ((Flags & NV_RX_ERROR_MASK) == NV_RX_ERROR4)
                {
                    if (!HandleLengthError(EthHeader, &Length))
                        goto NextDescriptor;
                }
                else if ((Flags & NV_RX_ERROR_MASK) == NV_RX_FRAMINGERR)
                {
                    if (Flags & NV_RX_SUBTRACT1)
                        --Length;
                }
                else
                {
                    ++Adapter->Statistics.ReceiveErrors;

                    if (Flags & NV_RX_MISSEDFRAME)
                        ++Adapter->Statistics.ReceiveNoBuffers;
                    if (Flags & NV_RX_FRAMINGERR)
                        ++Adapter->Statistics.ReceiveAlignmentErrors;
                    if (Flags & NV_RX_OVERFLOW)
                        ++Adapter->Statistics.ReceiveOverrunErrors;
                    if (Flags & NV_RX_CRCERR)
                        ++Adapter->Statistics.ReceiveCrcErrors;

                    goto NextDescriptor;
                }
            }
            ++Adapter->Statistics.ReceiveOk;

            NDIS_DbgPrint(MIN_TRACE, ("Packet %d received (length %d, flags %lx)\n",
                                      Adapter->CurrentRx, Length, Flags & FLAG_MASK_V1));
        }

        if (Length < sizeof(ETH_HEADER) || Length > NVNET_RECEIVE_BUFFER_SIZE)
            goto NextDescriptor;

        if (Adapter->Paused || !Adapter->FreeReceiveNbls)
        {
            ++Adapter->Statistics.ReceiveNoBuffers;
            goto NextDescriptor;
        }

        Nbl = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        ++Adapter->ReceiveNblsOutstanding;

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NdisMoveMemory(Nbl->MiniportReserved[0], EthHeader, Length);
        NdisAdjustMdlLength(NET_BUFFER_FIRST_MDL(NetBuffer), Length);
        NET_BUFFER_DATA_OFFSET(NetBuffer) = 0;
        NET_BUFFER_CURRENT_MDL(NetBuffer) = NET_BUFFER_FIRST_MDL(NetBuffer);
        NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) = 0;
        NET_BUFFER_DATA_LENGTH(NetBuffer) = Length;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        if (ReceiveTail)
            NET_BUFFER_LIST_NEXT_NBL(ReceiveTail) = Nbl;
        else
            ReceiveHead = Nbl;
        ReceiveTail = Nbl;
        ++ReceiveCount;

NextDescriptor:
        /* Invalidate the buffer length and release the descriptor */
        if (Adapter->Features & DEV_HAS_HIGH_DMA)
            NvRbd.x64->FlagsLength = NV_RX2_AVAIL | NVNET_RECEIVE_BUFFER_SIZE;
        else
            NvRbd.x32->FlagsLength = NV_RX_AVAIL | NVNET_RECEIVE_BUFFER_SIZE;

        Adapter->CurrentRx = (Adapter->CurrentRx + 1) % NVNET_RECEIVE_DESCRIPTORS;
        ++RxProcessed;
    }

    NdisDprReleaseSpinLock(&Adapter->Receive.Lock);

    if (ReceiveHead)
    {
        NdisMIndicateReceiveNetBufferLists(Adapter->AdapterHandle,
                                           ReceiveHead,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           ReceiveCount,
                                           NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    }

    return RxProcessed;
}

static
inline
VOID
ChangeInterruptMode(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ ULONG Workload)
{
    if (Workload > NVNET_IM_THRESHOLD)
    {
        Adapter->InterruptIdleCount = 0;

        /* High activity, polling based strategy */
        Adapter->InterruptMask = NVREG_IRQMASK_CPU;
    }
    else
    {
        if (Adapter->InterruptIdleCount < NVNET_IM_MAX_IDLE)
        {
            ++Adapter->InterruptIdleCount;
        }
        else
        {
            /* Low activity, 1 interrupt per packet */
            Adapter->InterruptMask = NVREG_IRQMASK_THROUGHPUT;
        }
    }
}

static
VOID
HandleLinkStateChange(
    _In_ PNVNET_ADAPTER Adapter)
{
    ULONG MiiStatus;
    BOOLEAN Connected, Report = FALSE;

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    NdisDprAcquireSpinLock(&Adapter->Lock);

    MiiStatus = NV_READ(Adapter, NvRegMIIStatus);

    /* Clear the link change interrupt */
    NV_WRITE(Adapter, NvRegMIIStatus, NVREG_MIISTAT_LINKCHANGE);

    if (MiiStatus & NVREG_MIISTAT_LINKCHANGE)
    {
        Connected = NvNetUpdateLinkSpeed(Adapter);
        if (Adapter->Connected != Connected)
        {
            Adapter->Connected = Connected;
            Report = TRUE;

            if (Connected)
            {
                /* Link up */
                NvNetToggleClockPowerGating(Adapter, FALSE);
                NdisDprAcquireSpinLock(&Adapter->Receive.Lock);
                NvNetStartReceiver(Adapter);
            }
            else
            {
                /* Link down */
                NvNetToggleClockPowerGating(Adapter, TRUE);
                NdisDprAcquireSpinLock(&Adapter->Receive.Lock);
                NvNetStopReceiver(Adapter);
            }

            NdisDprReleaseSpinLock(&Adapter->Receive.Lock);
        }
    }

    NdisDprReleaseSpinLock(&Adapter->Lock);

    if (Report)
    {
        NvNetIndicateLinkState(Adapter);
    }
}

static
VOID
HandleRecoverableError(
    _In_ PNVNET_ADAPTER Adapter)
{
    /* TODO */
    NDIS_DbgPrint(MAX_TRACE, ("() Recoverable error detected\n"));
}

VOID
NTAPI
MiniportInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportInterruptContext;
    ULONG InterruptStatus = Adapter->InterruptStatus;
    ULONG RxProcessed, TotalTxProcessed = 0, TotalRxProcessed = 0;
    PNET_BUFFER_LIST CompleteHead, CompleteTail;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    NDIS_DbgPrint(MIN_TRACE, ("() Events 0x%lx\n", InterruptStatus));

    if (!(Adapter->Flags & NV_ACTIVE))
        return;

    /* Process the rings and measure network activity */
    while (TotalRxProcessed < NVNET_RECEIVE_PROCESSING_LIMIT)
    {
        CompleteHead = NULL;
        CompleteTail = NULL;

        NdisDprAcquireSpinLock(&Adapter->Send.Lock);

        TotalTxProcessed += Adapter->ProcessTransmit(Adapter, &CompleteHead, &CompleteTail);

        NvNetProcessSendQueue(Adapter, &CompleteHead, &CompleteTail);

        NdisDprReleaseSpinLock(&Adapter->Send.Lock);

        NvNetCompleteSends(Adapter, CompleteHead, TRUE);

        RxProcessed = ProcessReceiveDescriptors(Adapter, TotalRxProcessed);
        if (!RxProcessed)
            break;

        TotalRxProcessed += RxProcessed;
    }

    NDIS_DbgPrint(MIN_TRACE, ("Total TX: %d, RX: %d\n", TotalTxProcessed, TotalRxProcessed));

    /* Moderate the interrupts */
    if (Adapter->OptimizationMode == NV_OPTIMIZATION_MODE_DYNAMIC)
    {
        ChangeInterruptMode(Adapter, TotalTxProcessed + TotalRxProcessed);
    }

    if (InterruptStatus & NVREG_IRQ_RX_NOBUF)
    {
        ++Adapter->Statistics.ReceiveIrqNoBuffers;
    }

    if (InterruptStatus & NVREG_IRQ_LINK)
    {
        HandleLinkStateChange(Adapter);
    }

    if (InterruptStatus & NVREG_IRQ_RECOVER_ERROR)
    {
        HandleRecoverableError(Adapter);
    }

    /* Enable interrupts on the NIC */
    NvNetApplyInterruptMask(Adapter);
}

BOOLEAN
NTAPI
MiniportISR(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportInterruptContext;
    ULONG InterruptStatus;

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    *TargetProcessors = 0;

    InterruptStatus = NV_READ(Adapter, NvRegIrqStatus);

    /* Clear any interrupt events */
    NV_WRITE(Adapter, NvRegIrqStatus, InterruptStatus);

    if (InterruptStatus & Adapter->InterruptMask)
    {
        /* Disable further interrupts */
        NvNetDisableInterrupts(Adapter);

        Adapter->InterruptStatus = InterruptStatus;

        *QueueDefaultInterruptDpc = TRUE;
        return TRUE;
    }
    else
    {
        /* This interrupt is not ours */
        *QueueDefaultInterruptDpc = FALSE;
        return FALSE;
    }
}

VOID
NTAPI
MiniportDisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportInterruptContext;

    NvNetDisableInterrupts(Adapter);
}

VOID
NTAPI
MiniportEnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportInterruptContext;

    NvNetApplyInterruptMask(Adapter);
}
