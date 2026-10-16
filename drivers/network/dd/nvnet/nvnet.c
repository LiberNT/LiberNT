/*
 * PROJECT:     ReactOS nVidia nForce Ethernet Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miniport driver entrypoint
 * COPYRIGHT:   Copyright 2021-2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "nvnet.h"

#define NDEBUG
#include "debug.h"

/* GLOBALS ********************************************************************/

CODE_SEG("INIT")
DRIVER_INITIALIZE DriverEntry;

static NDIS_HANDLE NvNetDriverHandle;

/* FUNCTIONS ******************************************************************/

CODE_SEG("PAGE")
VOID
NvNetFlushTransmitQueue(
    _In_ PNVNET_ADAPTER Adapter,
    _In_ NDIS_STATUS CompleteStatus)
{
    PNVNET_TCB Tcb;

    PAGED_CODE();

    for (Tcb = Adapter->Send.LastTcb;
         Tcb != Adapter->Send.CurrentTcb;
         Tcb = NV_NEXT_TCB(Adapter, Tcb))
    {
        PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;

        NET_BUFFER_LIST_STATUS(Tcb->Nbl) = CompleteStatus;
        NvNetCompleteNetBuffer(Tcb->Nbl, &CompleteHead, &CompleteTail);
        NvNetCompleteSends(Adapter, CompleteHead, FALSE);

        NV_RELEASE_TCB(Adapter, Tcb);
    }

    Adapter->Send.LastTcb = Adapter->Send.CurrentTcb;
}

DECLSPEC_NOINLINE /* Called from pageable code */
VOID
NvNetPauseProcessing(
    _In_ PNVNET_ADAPTER Adapter)
{
    NvNetDisableInterrupts(Adapter);

    NdisAcquireSpinLock(&Adapter->Send.Lock);
    Adapter->Flags &= ~NV_ACTIVE;
    NdisReleaseSpinLock(&Adapter->Send.Lock);
}

CODE_SEG("PAGE")
VOID
NvNetStopAdapter(
    _In_ PNVNET_ADAPTER Adapter)
{
    PAGED_CODE();

    if (Adapter->MediaDetectionTimer)
    {
        NdisCancelTimerObject(Adapter->MediaDetectionTimer);
    }

    NvNetDisableInterrupts(Adapter);

    KeFlushQueuedDpcs();
}

CODE_SEG("PAGE")
VOID
NvNetStartAdapter(
    _In_ PNVNET_ADAPTER Adapter)
{
    PAGED_CODE();

    NdisMSynchronizeWithInterruptEx(Adapter->InterruptHandle,
                                    0,
                                    NvNetInitPhaseSynchronized,
                                    Adapter);

    /* Setup the link timer right after the NIC is initialized */
    if (Adapter->MediaDetectionTimer)
    {
        LARGE_INTEGER DueTime;

        DueTime.QuadPart = -10000LL * NVNET_MEDIA_DETECTION_INTERVAL;
        NdisSetTimerObject(Adapter->MediaDetectionTimer,
                           DueTime,
                           NVNET_MEDIA_DETECTION_INTERVAL,
                           NULL);
    }
}

static
CODE_SEG("PAGE")
VOID
NTAPI
NvNetResetWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNVNET_ADAPTER Adapter = WorkItemContext;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    PAGED_CODE();

    NvNetStopAdapter(Adapter);

    NvNetIdleTransmitter(Adapter, TRUE);
    NvNetStopTransmitter(Adapter);
    NvNetStopReceiver(Adapter);
    NV_WRITE(Adapter, NvRegTxRxControl,
             Adapter->TxRxControl | NVREG_TXRXCTL_BIT2 | NVREG_TXRXCTL_RESET);
    NdisStallExecution(NV_TXRX_RESET_DELAY);

    NvNetFlushTransmitQueue(Adapter, NDIS_STATUS_FAILURE);
    NvNetFailNetBufferLists(Adapter, NDIS_STATUS_FAILURE);

    NT_VERIFY(NvNetInitNIC(Adapter, FALSE) == NDIS_STATUS_SUCCESS);

    NvNetStartAdapter(Adapter);

    _InterlockedDecrement(&Adapter->ResetLock);

    NdisMResetComplete(Adapter->AdapterHandle, NDIS_STATUS_SUCCESS, TRUE);
}

/* NDIS CALLBACKS *************************************************************/

static
BOOLEAN
NTAPI
MiniportCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;

    NdisAcquireSpinLock(&Adapter->Send.Lock);

    if (Adapter->Flags & NV_ACTIVE &&
        Adapter->Send.TcbSlots < NVNET_TRANSMIT_BLOCKS)
    {
        if (++Adapter->Send.StuckCount > NVNET_TRANSMIT_HANG_THRESHOLD)
        {
            NDIS_DbgPrint(MAX_TRACE, ("Transmit timeout!\n"));

#if defined(SARCH_XBOX)
            /* Apply a HACK to make XQEMU happy... */
            NvNetDisableInterrupts(Adapter);
            NvNetApplyInterruptMask(Adapter);
#endif

            Adapter->Send.StuckCount = 0;
        }
    }

    NdisReleaseSpinLock(&Adapter->Send.Lock);

    return FALSE;
}

static
CODE_SEG("PAGE")
VOID
NTAPI
MiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;
    BOOLEAN IsActive = !!(Adapter->Flags & NV_ACTIVE);

    UNREFERENCED_PARAMETER(HaltAction);

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    PAGED_CODE();

    if (IsActive)
    {
        NvNetPauseProcessing(Adapter);
    }

    NvNetStopAdapter(Adapter);

    if (IsActive)
    {
        NvNetIdleTransmitter(Adapter, TRUE);
        NvNetStopTransmitter(Adapter);
        NvNetStopReceiver(Adapter);
        NV_WRITE(Adapter, NvRegTxRxControl,
                 Adapter->TxRxControl | NVREG_TXRXCTL_BIT2 | NVREG_TXRXCTL_RESET);
        NdisStallExecution(NV_TXRX_RESET_DELAY);
    }

    NvNetFlushTransmitQueue(Adapter, NDIS_STATUS_FAILURE);

    NvNetToggleClockPowerGating(Adapter, TRUE);

    NV_WRITE(Adapter, NvRegMacAddrA, Adapter->OriginalMacAddress[0]);
    NV_WRITE(Adapter, NvRegMacAddrB, Adapter->OriginalMacAddress[1]);
    NV_WRITE(Adapter, NvRegTransmitPoll,
             NV_READ(Adapter, NvRegTransmitPoll) & ~NVREG_TRANSMITPOLL_MAC_ADDR_REV);

    SidebandUnitReleaseSemaphore(Adapter);

    NvNetFreeAdapter(Adapter);
}

static
NDIS_STATUS
NTAPI
MiniportResetEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    *AddressingReset = TRUE;

    if (_InterlockedCompareExchange(&Adapter->ResetLock, 1, 0))
    {
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }

    NvNetPauseProcessing(Adapter);

    NdisQueueIoWorkItem(Adapter->ResetWorkItem, NvNetResetWorker, Adapter);

    return NDIS_STATUS_PENDING;
}

static
VOID
NTAPI
MiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    if (Adapter->Flags & NV_ACTIVE)
    {
        if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
        {
            NvNetPauseProcessing(Adapter);
        }

        NvNetIdleTransmitter(Adapter, TRUE);
        NvNetStopTransmitter(Adapter);
        NvNetStopReceiver(Adapter);
        NV_WRITE(Adapter, NvRegTxRxControl,
                 Adapter->TxRxControl | NVREG_TXRXCTL_BIT2 | NVREG_TXRXCTL_RESET);
        NdisStallExecution(NV_TXRX_RESET_DELAY);

        SidebandUnitReleaseSemaphore(Adapter);
    }

    NvNetDisableInterrupts(Adapter);

    NvNetSetPowerState(Adapter, NdisDeviceStateD3, 0);
}

VOID
NTAPI
MiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, NextNbl;
    BOOLEAN CompletePause = FALSE;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&Adapter->Receive.Lock);

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
        --Adapter->ReceiveNblsOutstanding;
    }

    if (Adapter->PausePending && !Adapter->ReceiveNblsOutstanding)
    {
        Adapter->PausePending = FALSE;
        CompletePause = TRUE;
    }

    NdisReleaseSpinLock(&Adapter->Receive.Lock);

    if (CompletePause)
        NdisMPauseComplete(Adapter->AdapterHandle);
}

static
NDIS_STATUS
NTAPI
MiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(PauseParameters);

    NdisAcquireSpinLock(&Adapter->Send.Lock);
    Adapter->Paused = TRUE;
    NdisReleaseSpinLock(&Adapter->Send.Lock);

    NvNetFailNetBufferLists(Adapter, NDIS_STATUS_PAUSED);

    KeFlushQueuedDpcs();

    NdisAcquireSpinLock(&Adapter->Receive.Lock);
    if (Adapter->ReceiveNblsOutstanding)
    {
        Adapter->PausePending = TRUE;
        Status = NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&Adapter->Receive.Lock);

    return Status;
}

static
NDIS_STATUS
NTAPI
MiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PNVNET_ADAPTER Adapter = (PNVNET_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    NdisAcquireSpinLock(&Adapter->Receive.Lock);
    Adapter->PausePending = FALSE;
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->Receive.Lock);

    NdisAcquireSpinLock(&Adapter->Send.Lock);
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->Send.Lock);

    NvNetIndicateLinkState(Adapter);

    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
MiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
NTAPI
MiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    NdisMDeregisterMiniportDriver(NvNetDriverHandle);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics = { 0 };

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
    Characteristics.OidRequestHandler = MiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = MiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = MiniportCancelSend;
    Characteristics.CheckForHangHandlerEx = MiniportCheckForHang;
    Characteristics.ResetHandlerEx = MiniportResetEx;
    Characteristics.DevicePnPEventNotifyHandler = MiniportDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = MiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = MiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Characteristics,
                                       &NvNetDriverHandle);
}
