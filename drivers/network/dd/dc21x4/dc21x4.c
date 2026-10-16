/*
 * PROJECT:     ReactOS DC21x4 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miniport driver entry
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "dc21x4.h"

#include <debug.h>

static NDIS_HANDLE DcDriverHandle;

/* FUNCTIONS ******************************************************************/

ULONG
DcEthernetCrc(
    _In_reads_bytes_(Size) const VOID* Buffer,
    _In_ ULONG Size)
{
    ULONG i, j, Crc;
    const UCHAR* Data = Buffer;

    Crc = 0xFFFFFFFF;
    for (i = 0; i < Size; ++i)
    {
        Crc ^= Data[i];
        for (j = 8; j > 0; j--)
        {
            /* CRC-32 polynomial little-endian */
            Crc = (Crc >> 1) ^ (-(LONG)(Crc & 1) & 0xEDB88320);
        }
    }

    return Crc;
}

static
VOID
DcFlushTransmitQueue(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PNET_BUFFER NetBuffer;
    PDC_TCB Tcb;

    NdisAcquireSpinLock(&Adapter->SendLock);

    /* Remove pending transmissions from the transmit ring */
    for (Tcb = Adapter->LastTcb;
         Tcb != Adapter->CurrentTcb;
         Tcb = DC_NEXT_TCB(Adapter, Tcb))
    {
        Nbl = Tcb->Nbl;

        if (!Nbl)
            continue;

        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        DcCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);

        DC_RELEASE_TCB(Adapter, Tcb);
    }
    Adapter->CurrentTcb = Tcb;

    /* Remove pending transmissions from the internal queue */
    for (Nbl = Adapter->SendQueueHead; Nbl != NULL; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        for (NetBuffer = DC_NEXT_NB_FROM_NBL(Nbl);
             NetBuffer != NULL;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            DcCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
        }
    }
    Adapter->SendQueueHead = NULL;
    Adapter->SendQueueTail = NULL;

    NdisReleaseSpinLock(&Adapter->SendLock);

    DcCompleteSends(Adapter, CompleteHead, FALSE);
}

static
VOID
DcStopReceivePath(
    _In_ PDC21X4_ADAPTER Adapter)
{
    BOOLEAN RxStopped;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

#if DBG
    NdisAcquireSpinLock(&Adapter->ReceiveLock);
    if (Adapter->RcbFree != Adapter->RcbCount)
    {
        INFO("RX packets: %u/%u\n", Adapter->RcbFree, Adapter->RcbCount);
    }
    NdisReleaseSpinLock(&Adapter->ReceiveLock);
#endif

    while (TRUE)
    {
        NdisAcquireSpinLock(&Adapter->ReceiveLock);

        RxStopped = (Adapter->RcbFree == Adapter->RcbCount);

        NdisReleaseSpinLock(&Adapter->ReceiveLock);

        if (RxStopped)
            break;

        NdisMSleep(10);
    }
}

DECLSPEC_NOINLINE /* Called from pageable code */
VOID
DcStopAdapter(
    _In_ PDC21X4_ADAPTER Adapter,
    _In_ BOOLEAN WaitForPackets)
{
    /* Attempt to disable interrupts to complete more quickly */
    DC_WRITE(Adapter, DcCsr7_IrqMask, 0);

    /* Prevent DPCs from executing and stop accepting incoming packets */
    NdisAcquireSpinLock(&Adapter->SendLock);
    Adapter->Flags &= ~DC_ACTIVE;
    NdisReleaseSpinLock(&Adapter->SendLock);

    NdisCancelTimerObject(Adapter->MediaMonitorTimer);

    /* Wait for any DPCs to complete */
    KeFlushQueuedDpcs();

    /* Disable interrupts */
    DC_WRITE(Adapter, DcCsr7_IrqMask, 0);

    /* Wait for completion of TX/RX and stop the DMA engine inside the NIC */
    DcStopTxRxProcess(Adapter);
    Adapter->OpMode &= ~(DC_OPMODE_RX_ENABLE | DC_OPMODE_TX_ENABLE);

    DcFlushTransmitQueue(Adapter, NDIS_STATUS_FAILURE);

    /* Wait for the packets to be returned to the driver */
    if (WaitForPackets)
    {
        DcStopReceivePath(Adapter);
    }

    /* Make sure there is no pending OID request */
    if (Adapter->OidPending)
    {
        Adapter->OidPending = FALSE;

        DcCompletePendingOid(Adapter, NDIS_STATUS_SUCCESS);
    }
}

CODE_SEG("PAGE")
VOID
DcStartAdapter(
    _In_ PDC21X4_ADAPTER Adapter)
{
    PAGED_CODE();

    /* Enable interrupts */
    _InterlockedExchange((PLONG)&Adapter->CurrentInterruptMask, Adapter->InterruptMask);
    DC_WRITE(Adapter, DcCsr7_IrqMask, Adapter->InterruptMask);

    Adapter->Flags |= DC_ACTIVE;

    /* Start the RX process */
    Adapter->OpMode |= DC_OPMODE_RX_ENABLE;
    DC_WRITE(Adapter, DcCsr6_OpMode, Adapter->OpMode);

    /* Start the media monitor, wait the selected media to become ready */
    DcSetMediaMonitorTimer(Adapter, 2400);
}

CODE_SEG("PAGE")
VOID
NTAPI
DcResetWorker(
    _In_ PVOID Context,
    _In_ NDIS_HANDLE WorkItem)
{
    PDC21X4_ADAPTER Adapter = Context;
    NDIS_STATUS Status;
    ULONG InterruptStatus;
    LONG ResetReason;

    UNREFERENCED_PARAMETER(WorkItem);

    PAGED_CODE();

    Status = NDIS_STATUS_SUCCESS;

    /* Check if the device is present */
    InterruptStatus = DC_READ(Adapter, DcCsr5_Status);
    if (InterruptStatus == 0xFFFFFFFF)
    {
        ERR("Hardware is gone...\n");

        /* Remove this adapter */
        NdisMRemoveMiniport(Adapter->AdapterHandle);

        Status = NDIS_STATUS_HARD_ERRORS;
        goto Done;
    }

    DcStopAdapter(Adapter, FALSE);

    if (Adapter->LinkUp)
    {
        Adapter->LinkUp = FALSE;

        DcIndicateLinkState(Adapter);
    }

    DcSetupAdapter(Adapter);

    DcStartAdapter(Adapter);

Done:
    ResetReason = _InterlockedExchange(&Adapter->ResetLock, 0);

    /* Complete the pending reset request */
    if (ResetReason == 1)
    {
        NdisMResetComplete(Adapter->AdapterHandle, Status, FALSE);
    }
}

VOID
NTAPI
DcTransmitTimeoutRecoveryWorker(
    _In_ PVOID Context,
    _In_ NDIS_HANDLE WorkItem)
{
    PDC21X4_ADAPTER Adapter = Context;

    UNREFERENCED_PARAMETER(WorkItem);

    NdisAcquireSpinLock(&Adapter->ModeLock);

    DcStopTxRxProcess(Adapter);
    DC_WRITE(Adapter, DcCsr6_OpMode, Adapter->OpMode);

    NdisDprAcquireSpinLock(&Adapter->SendLock);

    DC_WRITE(Adapter, DcCsr1_TxPoll, DC_TX_POLL_DOORBELL);

    NdisDprReleaseSpinLock(&Adapter->SendLock);

    NdisReleaseSpinLock(&Adapter->ModeLock);
}

static
BOOLEAN
NTAPI
DcCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    ULONG TcbCompleted;
    BOOLEAN TxHang = FALSE;

    if (!(Adapter->Flags & DC_ACTIVE))
        return FALSE;

    NdisDprAcquireSpinLock(&Adapter->SendLock);

    if (Adapter->TcbSlots != (DC_TRANSMIT_BLOCKS - DC_TCB_RESERVE))
    {
        TcbCompleted = Adapter->TcbCompleted;
        TxHang = (TcbCompleted == Adapter->LastTcbCompleted);
        Adapter->LastTcbCompleted = TcbCompleted;
    }

    NdisDprReleaseSpinLock(&Adapter->SendLock);

    if (TxHang)
    {
        WARN("Transmit timeout, CSR12 %08lx, CSR5 %08lx\n",
             DC_READ(Adapter, DcCsr12_SiaStatus),
             DC_READ(Adapter, DcCsr5_Status));

        NdisQueueIoWorkItem(Adapter->TxRecoveryWorkItem, DcTransmitTimeoutRecoveryWorker, Adapter);
    }

    return FALSE;
}

static
NDIS_STATUS
NTAPI
DcReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;

    WARN("Called\n");

    *AddressingReset = FALSE;

    if (_InterlockedCompareExchange(&Adapter->ResetLock, 1, 0))
    {
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }

    NdisQueueIoWorkItem(Adapter->ResetWorkItem, DcResetWorker, Adapter);

    return NDIS_STATUS_PENDING;
}

static
CODE_SEG("PAGE")
VOID
NTAPI
DcHalt(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    PAGED_CODE();

    INFO("Called\n");

    DcStopAdapter(Adapter, TRUE);

    DcDisableHw(Adapter);

    DcFreeAdapter(Adapter);
}

static
VOID
NTAPI
DcShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    INFO("Called\n");

    DcDisableHw(Adapter);
}

static
NDIS_STATUS
NTAPI
DcPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;
    PNET_BUFFER_LIST Nbl, NextNbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(PauseParameters);

    NdisAcquireSpinLock(&Adapter->SendLock);
    Adapter->Paused = TRUE;
    for (Nbl = Adapter->SendQueueHead; Nbl != NULL; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_PAUSED;
        for (NetBuffer = DC_NEXT_NB_FROM_NBL(Nbl);
             NetBuffer != NULL;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            DcCompleteNetBuffer(Nbl, &CompleteHead, &CompleteTail);
        }
    }
    Adapter->SendQueueHead = NULL;
    Adapter->SendQueueTail = NULL;
    NdisReleaseSpinLock(&Adapter->SendLock);

    DcCompleteSends(Adapter, CompleteHead, FALSE);

    KeFlushQueuedDpcs();

    NdisAcquireSpinLock(&Adapter->ReceiveLock);
    if (Adapter->RcbFree != Adapter->RcbCount)
    {
        Adapter->PausePending = TRUE;
        Status = NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&Adapter->ReceiveLock);

    return Status;
}

static
NDIS_STATUS
NTAPI
DcRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PDC21X4_ADAPTER Adapter = (PDC21X4_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    NdisAcquireSpinLock(&Adapter->ReceiveLock);
    Adapter->PausePending = FALSE;
    NdisReleaseSpinLock(&Adapter->ReceiveLock);

    NdisAcquireSpinLock(&Adapter->SendLock);
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->SendLock);

    DcIndicateLinkState(Adapter);

    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
DcDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
NTAPI
DcUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    NdisMDeregisterMiniportDriver(DcDriverHandle);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics = { 0 };

    INFO("Called\n");

    InitializeListHead(&SRompAdapterList);

    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.InitializeHandlerEx = DcInitializeEx;
    Characteristics.HaltHandlerEx = DcHalt;
    Characteristics.UnloadHandler = DcUnload;
    Characteristics.PauseHandler = DcPause;
    Characteristics.RestartHandler = DcRestart;
    Characteristics.OidRequestHandler = DcOidRequest;
    Characteristics.SendNetBufferListsHandler = DcSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = DcReturnNetBufferLists;
    Characteristics.CancelSendHandler = DcCancelSend;
    Characteristics.CheckForHangHandlerEx = DcCheckForHang;
    Characteristics.ResetHandlerEx = DcReset;
    Characteristics.DevicePnPEventNotifyHandler = DcDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = DcShutdown;
    Characteristics.CancelOidRequestHandler = DcCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Characteristics,
                                         &DcDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    return NDIS_STATUS_SUCCESS;
}
