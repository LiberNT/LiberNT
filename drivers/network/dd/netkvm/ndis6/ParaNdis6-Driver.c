/*
 * PROJECT:     LiberNT VirtIO Network Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 miniport entry points of the NetKVM driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ParaNdis6.h"

static NDIS_HANDLE DriverHandle;
static ULONG gID = 0;

static VOID NTAPI ParaNdis6_Unload(IN PDRIVER_OBJECT pDriverObject)
{
    NdisMDeregisterMiniportDriver(DriverHandle);
    ParaNdis_DebugCleanup(pDriverObject);
}

static VOID NTAPI ParaNdis6_Shutdown(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    ParaNdis_OnShutdown(pContext);
}

static VOID FreeContext(PARANDIS_ADAPTER *pContext)
{
    if (pContext->DmaHandle)
        NdisMDeregisterScatterGatherDma(pContext->DmaHandle);
    NdisFreeMemory(pContext, 0, 0);
}

static NDIS_STATUS NTAPI ParaNdis6_Initialize(
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE MiniportDriverContext,
    IN PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES ra;
    NDIS_SG_DMA_DESCRIPTION dma;
    PARANDIS_ADAPTER *pContext;
    NDIS_STATUS status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    pContext = (PARANDIS_ADAPTER *)ParaNdis_AllocateMemory(NULL, sizeof(PARANDIS_ADAPTER));
    if (!pContext)
        return NDIS_STATUS_RESOURCES;

    NdisZeroMemory(pContext, sizeof(PARANDIS_ADAPTER));
    pContext->ulUniqueID = NdisInterlockedIncrement(&gID);
    pContext->DriverHandle = DriverHandle;
    pContext->MiniportHandle = MiniportAdapterHandle;

    NdisZeroMemory(&ra, sizeof(ra));
    ra.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    ra.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    ra.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    ra.MiniportAdapterContext = pContext;
    ra.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                        NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                        NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND;
    ra.InterfaceType = NdisInterfacePci;
    status = NdisMSetMiniportAttributes(MiniportAdapterHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&ra);

    if (status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(&dma, sizeof(dma));
        dma.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
        dma.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
        dma.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
        dma.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
        dma.MaximumPhysicalMapping = 0x10000;
        status = NdisMRegisterScatterGatherDma(MiniportAdapterHandle, &dma, &pContext->DmaHandle);
        if (status == NDIS_STATUS_SUCCESS)
            pContext->bDmaInitialized = TRUE;
        else
            pContext->DmaHandle = NULL;
    }

    if (status == NDIS_STATUS_SUCCESS)
        status = ParaNdis_InitializeContext(pContext, MiniportInitParameters->AllocatedResources);

    if (status != NDIS_STATUS_SUCCESS)
    {
        FreeContext(pContext);
        DEBUG_EXIT_STATUS(0, status);
        return status;
    }

    status = ParaNdis_FinishInitialization(pContext);
    if (status == NDIS_STATUS_SUCCESS)
        status = ParaNdis6_SetGeneralAttributes(pContext);

    if (status != NDIS_STATUS_SUCCESS)
    {
        ParaNdis_CleanupContext(pContext);
        FreeContext(pContext);
        DEBUG_EXIT_STATUS(0, status);
        return status;
    }

    ParaNdis_DebugRegisterMiniport(pContext, TRUE);
    DEBUG_EXIT_STATUS(0, status);
    return status;
}

static VOID OnHaltStepDone(VOID *p)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)p;
    NdisSetEvent(&pContext->HaltEvent);
}

static void WaitHaltEvent(PARANDIS_ADAPTER *pContext)
{
    while (!NdisWaitEvent(&pContext->HaltEvent, 5000))
    {
        DPrintf(0, ("[%s]", __FUNCTION__));
    }
}

static VOID NTAPI ParaNdis6_Halt(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HALT_ACTION HaltAction)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    ParaNdis_DebugHistory(pContext, hopHalt, NULL, 1, 0, 0);

    NdisCancelTimerObject(pContext->ConnectTimer);
    NdisResetEvent(&pContext->HaltEvent);
    if (ParaNdis6_StopSend(pContext, TRUE, OnHaltStepDone) != NDIS_STATUS_PENDING)
        NdisSetEvent(&pContext->HaltEvent);
    WaitHaltEvent(pContext);
    NdisResetEvent(&pContext->HaltEvent);
    if (ParaNdis6_StopReceive(pContext, TRUE, OnHaltStepDone) != NDIS_STATUS_PENDING)
        NdisSetEvent(&pContext->HaltEvent);
    WaitHaltEvent(pContext);

    ParaNdis_CleanupContext(pContext);
    ParaNdis_DebugHistory(pContext, hopHalt, NULL, 0, 0, 0);
    ParaNdis_DebugRegisterMiniport(pContext, FALSE);
    FreeContext(pContext);
}

static VOID OnPauseStepDone(VOID *p)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)p;

    if (InterlockedDecrement(&pContext->PauseStepsPending) == 0)
        NdisMPauseComplete(pContext->MiniportHandle);
}

static NDIS_STATUS NTAPI ParaNdis6_Pause(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    pContext->PauseStepsPending = 3;
    if (ParaNdis6_StopSend(pContext, TRUE, OnPauseStepDone) != NDIS_STATUS_PENDING)
        InterlockedDecrement(&pContext->PauseStepsPending);
    if (ParaNdis6_StopReceive(pContext, TRUE, OnPauseStepDone) != NDIS_STATUS_PENDING)
        InterlockedDecrement(&pContext->PauseStepsPending);

    return (InterlockedDecrement(&pContext->PauseStepsPending) == 0) ?
        NDIS_STATUS_SUCCESS : NDIS_STATUS_PENDING;
}

static NDIS_STATUS NTAPI ParaNdis6_Restart(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    ParaNdis6_StopSend(pContext, FALSE, NULL);
    ParaNdis6_StopReceive(pContext, FALSE, NULL);

    if (!pContext->ulMilliesToConnect)
        ParaNdis_ReportLinkStatus(pContext, TRUE);
    else
        ParaNdis_SetTimer(pContext->ConnectTimer, pContext->ulMilliesToConnect);

    return NDIS_STATUS_SUCCESS;
}

static VOID OnSuspendStepDone(VOID *p)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)p;
    NdisSetEvent(&pContext->ResetEvent);
}

VOID ParaNdis_Suspend(PARANDIS_ADAPTER *pContext)
{
    NdisResetEvent(&pContext->ResetEvent);
    if (ParaNdis6_StopSend(pContext, TRUE, OnSuspendStepDone) != NDIS_STATUS_PENDING)
        NdisSetEvent(&pContext->ResetEvent);
    NdisWaitEvent(&pContext->ResetEvent, 0);
    NdisResetEvent(&pContext->ResetEvent);
    if (ParaNdis6_StopReceive(pContext, TRUE, OnSuspendStepDone) != NDIS_STATUS_PENDING)
        NdisSetEvent(&pContext->ResetEvent);
    NdisWaitEvent(&pContext->ResetEvent, 0);
    NdisResetEvent(&pContext->ResetEvent);
}

VOID ParaNdis_Resume(PARANDIS_ADAPTER *pContext)
{
    ParaNdis6_StopSend(pContext, FALSE, NULL);
    ParaNdis6_StopReceive(pContext, FALSE, NULL);
}

static VOID NTAPI OnResetWorkItem(PVOID WorkItemContext, NDIS_HANDLE NdisIoWorkItemHandle)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)WorkItemContext;

    pContext->bResetInProgress = TRUE;
    ParaNdis_IndicateConnect(pContext, FALSE, FALSE);
    ParaNdis_Suspend(pContext);
    ParaNdis_Resume(pContext);
    pContext->bResetInProgress = FALSE;
    ParaNdis_ReportLinkStatus(pContext, FALSE);

    NdisFreeIoWorkItem(NdisIoWorkItemHandle);
    ParaNdis_DebugHistory(pContext, hopSysReset, NULL, 0, NDIS_STATUS_SUCCESS, 0);
    NdisMResetComplete(pContext->MiniportHandle, NDIS_STATUS_SUCCESS, TRUE);
}

static NDIS_STATUS NTAPI ParaNdis6_Reset(
    IN NDIS_HANDLE MiniportAdapterContext,
    OUT PBOOLEAN AddressingReset)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;
    NDIS_HANDLE workItem;

    *AddressingReset = FALSE;
    ParaNdis_DebugHistory(pContext, hopSysReset, NULL, 1, 0, 0);

    workItem = NdisAllocateIoWorkItem(pContext->MiniportHandle);
    if (!workItem)
    {
        ParaNdis_DebugHistory(pContext, hopSysReset, NULL, 0, NDIS_STATUS_FAILURE, 0);
        return NDIS_STATUS_FAILURE;
    }

    NdisQueueIoWorkItem(workItem, OnResetWorkItem, pContext);
    return NDIS_STATUS_PENDING;
}

static BOOLEAN NTAPI ParaNdis6_CheckForHang(IN NDIS_HANDLE MiniportAdapterContext)
{
    return ParaNdis_CheckForHang((PARANDIS_ADAPTER *)MiniportAdapterContext);
}

static VOID NTAPI ParaNdis6_PnPEventNotify(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportAdapterContext;

    ParaNdis_OnPnPEvent(pContext,
                        NetDevicePnPEvent->DevicePnPEvent,
                        NetDevicePnPEvent->InformationBuffer,
                        NetDevicePnPEvent->InformationBufferLength);
}

BOOLEAN NTAPI ParaNdis6_MiniportISR(
    IN NDIS_HANDLE MiniportInterruptContext,
    OUT PBOOLEAN QueueDefaultInterruptDpc,
    OUT PULONG TargetProcessors)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    BOOLEAN b;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;
    b = ParaNdis_OnLegacyInterrupt(pContext, QueueDefaultInterruptDpc);
    DEBUG_EXIT_STATUS(7, (ULONG)b);
    return b;
}

VOID NTAPI ParaNdis6_MiniportDPC(
    IN NDIS_HANDLE MiniportInterruptContext,
    IN PVOID MiniportDpcContext,
    IN PVOID ReceiveThrottleParameters,
    IN PVOID NdisReserved2)
{
    PARANDIS_ADAPTER *pContext = (PARANDIS_ADAPTER *)MiniportInterruptContext;
    ULONG requiresProcessing;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    NdisCancelTimerObject(pContext->DPCPostProcessTimer);
    requiresProcessing = ParaNdis_DPCWorkBody(pContext, PARANDIS_UNLIMITED_PACKETS_TO_INDICATE);
    if (requiresProcessing)
    {
        InterlockedOr(&pContext->InterruptStatus, requiresProcessing);
        ParaNdis_SetTimer(pContext->DPCPostProcessTimer, 10);
    }
}

VOID NTAPI ParaNdis6_DisableInterrupt(IN NDIS_HANDLE MiniportInterruptContext)
{
    ParaNdis_VirtIODisableIrqSynchronized((PARANDIS_ADAPTER *)MiniportInterruptContext, isAny);
}

VOID NTAPI ParaNdis6_EnableInterrupt(IN NDIS_HANDLE MiniportInterruptContext)
{
    ParaNdis_VirtIOEnableIrqSynchronized((PARANDIS_ADAPTER *)MiniportInterruptContext, isAny);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS chars;
    NDIS_STATUS status;

    ParaNdis_DebugInitialize(DriverObject, RegistryPath);

    NdisZeroMemory(&chars, sizeof(chars));
    chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    chars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    chars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    chars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    chars.InitializeHandlerEx = ParaNdis6_Initialize;
    chars.HaltHandlerEx = ParaNdis6_Halt;
    chars.UnloadHandler = ParaNdis6_Unload;
    chars.PauseHandler = ParaNdis6_Pause;
    chars.RestartHandler = ParaNdis6_Restart;
    chars.OidRequestHandler = ParaNdis6_OidRequest;
    chars.SendNetBufferListsHandler = ParaNdis6_SendNetBufferLists;
    chars.ReturnNetBufferListsHandler = ParaNdis6_ReturnNetBufferLists;
    chars.CancelSendHandler = ParaNdis6_CancelSend;
    chars.CheckForHangHandlerEx = ParaNdis6_CheckForHang;
    chars.ResetHandlerEx = ParaNdis6_Reset;
    chars.DevicePnPEventNotifyHandler = ParaNdis6_PnPEventNotify;
    chars.ShutdownHandlerEx = ParaNdis6_Shutdown;
    chars.CancelOidRequestHandler = ParaNdis6_CancelOidRequest;

    status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &chars, &DriverHandle);
    if (status != NDIS_STATUS_SUCCESS)
    {
        DPrintf(0, ("NdisMRegisterMiniportDriver failed"));
        ParaNdis_DebugCleanup(DriverObject);
    }

    DEBUG_EXIT_STATUS(status ? 0 : 4, status);
    return status;
}
