/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, miniport lifecycle and transmit queue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rt2800usb.h"

#define NDEBUG
#include <debug.h>

static NDIS_HANDLE RtMiniportDriverHandle;

PVOID
RtAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, RT_TAG);

    if (Buffer)
        RtlZeroMemory(Buffer, Size);
    return Buffer;
}

VOID
RtFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, RT_TAG);
}

VOID
RtSleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
RtWakeWorker(
    _In_ PRT_ADAPTER Adapter)
{
    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
ULONG
RtNetBufferCount(
    _In_ PNET_BUFFER_LIST Nbl)
{
    PNET_BUFFER NetBuffer;
    ULONG Count = 0;

    for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl); NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        Count++;

    return Count;
}

static
VOID
RtCompleteNbls(
    _In_ PRT_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST Nbls,
    _In_ NDIS_STATUS Status,
    _In_ BOOLEAN AtDispatch)
{
    PNET_BUFFER_LIST Nbl;

    if (!Nbls)
        return;

    for (Nbl = Nbls; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        NET_BUFFER_LIST_STATUS(Nbl) = Status;

    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                    Nbls,
                                    AtDispatch ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

VOID
RtTxPump(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN AtDispatch)
{
    PNET_BUFFER_LIST Nbl, Failed = NULL;
    PDOT11_EXTSTA_SEND_CONTEXT SendContext;
    PNET_BUFFER NetBuffer, Next;
    BOOLEAN Exempt;
    ULONG Count;

    for (;;)
    {
        NdisAcquireSpinLock(&Adapter->TxLock);
        Nbl = Adapter->TxQueueHead;
        if (!Nbl)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            break;
        }
        NdisReleaseSpinLock(&Adapter->TxLock);

        Count = RtNetBufferCount(Nbl);
        if (Count > RT_TX_URBS || Adapter->LinkState != RtLinkAssociated || !Adapter->DataPathRunning)
        {
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
            if (!Adapter->TxQueueHead)
                Adapter->TxQueueTail = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            NET_BUFFER_LIST_NEXT_NBL(Nbl) = Failed;
            Failed = Nbl;
            continue;
        }

        if (RtFreeTxCount(Adapter) < Count)
            break;

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxQueueHead != Nbl)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            continue;
        }
        Adapter->TxQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (!Adapter->TxQueueHead)
            Adapter->TxQueueTail = NULL;
        NdisReleaseSpinLock(&Adapter->TxLock);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        SendContext = NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation);
        Exempt = SendContext && SendContext->usExemptionActionType == DOT11_EXEMPT_ALWAYS;

        for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl); NetBuffer; NetBuffer = Next)
        {
            Next = NET_BUFFER_NEXT_NB(NetBuffer);
            if (!NT_SUCCESS(RtSendNetBuffer(Adapter, NetBuffer, Exempt, Next ? NULL : Nbl)) && !Next)
            {
                NET_BUFFER_LIST_NEXT_NBL(Nbl) = Failed;
                Failed = Nbl;
            }
        }
    }

    RtCompleteNbls(Adapter, Failed, NDIS_STATUS_FAILURE, AtDispatch);
}

static
VOID
NTAPI
RtMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;
    BOOLEAN AtDispatch = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ? TRUE : FALSE;
    PNET_BUFFER_LIST Tail;

    UNREFERENCED_PARAMETER(PortNumber);

    if (!Adapter->DataPathRunning || Adapter->Halting)
    {
        RtCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_PAUSED, AtDispatch);
        return;
    }

    if (Adapter->LinkState != RtLinkAssociated)
    {
        RtCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_MEDIA_DISCONNECTED, AtDispatch);
        return;
    }

    for (Tail = NetBufferLists; NET_BUFFER_LIST_NEXT_NBL(Tail); Tail = NET_BUFFER_LIST_NEXT_NBL(Tail))
    {
    }

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (Adapter->TxQueueTail)
        NET_BUFFER_LIST_NEXT_NBL(Adapter->TxQueueTail) = NetBufferLists;
    else
        Adapter->TxQueueHead = NetBufferLists;
    Adapter->TxQueueTail = Tail;
    NdisReleaseSpinLock(&Adapter->TxLock);

    RtTxPump(Adapter, AtDispatch);
}

static
VOID
NTAPI
RtMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
VOID
RtFlushTxQueue(
    _In_ PRT_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNET_BUFFER_LIST Nbls;

    NdisAcquireSpinLock(&Adapter->TxLock);
    Nbls = Adapter->TxQueueHead;
    Adapter->TxQueueHead = NULL;
    Adapter->TxQueueTail = NULL;
    NdisReleaseSpinLock(&Adapter->TxLock);

    RtCompleteNbls(Adapter, Nbls, Status, FALSE);
}

static
NDIS_STATUS
RtSetAttributes(
    _In_ PRT_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;
    NDIS_STATUS Status;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = sizeof(Registration);
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                                  NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    Registration.InterfaceType = NdisInterfaceInternal;

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = sizeof(General);
    General.MediaType = NdisMediumNative802_11;
    General.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    General.MtuSize = RT_MTU_SIZE;
    General.MaxXmitLinkSpeed = RT_LINK_SPEED_BPS;
    General.MaxRcvLinkSpeed = RT_LINK_SPEED_BPS;
    General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateFull;
    General.LookaheadSize = RT_MAX_80211_FRAME;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    General.MaxMulticastListSize = 32;
    General.MacAddressLength = RT_ADDRESS_LENGTH;
    RtlCopyMemory(General.PermanentMacAddress, Adapter->PermanentAddress, RT_ADDRESS_LENGTH);
    RtlCopyMemory(General.CurrentMacAddress, Adapter->CurrentAddress, RT_ADDRESS_LENGTH);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_IEEE80211;
    General.IfConnectorPresent = TRUE;
    General.SupportedOidList = RtSupportedOids;
    General.SupportedOidListLength = RtSupportedOidCount * sizeof(NDIS_OID);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
VOID
RtReleaseAdapter(
    _In_ PRT_ADAPTER Adapter)
{
    if (Adapter->WorkerThread)
    {
        Adapter->WorkerStop = TRUE;
        RtWakeWorker(Adapter);
        KeWaitForSingleObject(&Adapter->WorkerStopped, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->WorkerThread);
        Adapter->WorkerThread = NULL;
    }

    Adapter->Halting = TRUE;
    if (Adapter->BulkInPipe)
        RtStopIo(Adapter);
    RtHardwareShutdown(Adapter);
    RtFlushTxQueue(Adapter, NDIS_STATUS_FAILURE);

    if (Adapter->RxNblPool)
        NdisFreeNetBufferListPool(Adapter->RxNblPool);

    RtUsbCleanup(Adapter);
    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeSpinLock(&Adapter->TxLock);
    RtFree(Adapter);
}

static
NDIS_STATUS
NTAPI
RtMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PDEVICE_OBJECT FunctionalDeviceObject;
    HANDLE ThreadHandle;
    PRT_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParameters);

    Adapter = RtAllocate(sizeof(*Adapter));
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    NdisAllocateSpinLock(&Adapter->Lock);
    NdisAllocateSpinLock(&Adapter->TxLock);
    KeInitializeMutex(&Adapter->RegisterLock, 0);
    KeInitializeEvent(&Adapter->WorkerEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkerStopped, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MlmeEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->SaeEvent, SynchronizationEvent, FALSE);

    Adapter->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->CurrentPhyType = dot11_phy_type_erp;
    Adapter->RadioOn = TRUE;
    Adapter->AutoConfigEnabled = TRUE;
    Adapter->RtsThreshold = 2347;
    Adapter->FragmentationThreshold = 2346;
    Adapter->DesiredBssType = dot11_BSS_type_infrastructure;
    Adapter->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
    Adapter->UnicastCipher = DOT11_CIPHER_ALGO_NONE;
    Adapter->MulticastCipher = DOT11_CIPHER_ALGO_NONE;
    Adapter->LinkState = RtLinkIdle;

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           &FunctionalDeviceObject,
                           &Adapter->LowerDeviceObject,
                           NULL,
                           NULL);
    if (!Adapter->LowerDeviceObject)
    {
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Fail;
    }

    NtStatus = RtUsbInitialize(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("RT2800USB: USB setup failed 0x%08lx\n", NtStatus);
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Fail;
    }

    NtStatus = RtHardwareInitialize(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("RT2800USB: hardware initialization failed 0x%08lx\n", NtStatus);
        Status = NDIS_STATUS_HARD_ERRORS;
        goto Fail;
    }

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = RT_TAG;
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(MiniportAdapterHandle, &PoolParameters);
    if (!Adapter->RxNblPool)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    Status = RtSetAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    NtStatus = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, RtWorkerThread, Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID*)&Adapter->WorkerThread, NULL);
    ZwClose(ThreadHandle);

    RtStartReceive(Adapter);
    DPRINT1("RT2800USB: adapter ready\n");
    return NDIS_STATUS_SUCCESS;

Fail:
    RtReleaseAdapter(Adapter);
    return Status;
}

static
VOID
NTAPI
RtMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter->DataPathRunning = FALSE;
    RtReleaseAdapter(Adapter);
}

static
NDIS_STATUS
NTAPI
RtMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;
    ULONG Wait;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->DataPathRunning = FALSE;
    RtFlushTxQueue(Adapter, NDIS_STATUS_PAUSED);
    for (Wait = 0; Wait < 500 && (Adapter->RxIndicated || Adapter->TxPending); Wait++)
        RtSleep(2);

    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
RtMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    Adapter->DataPathRunning = TRUE;
    RtIndicateLinkState(Adapter, Adapter->LinkState == RtLinkAssociated);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
RtMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->DataPathRunning = FALSE;
}

static
VOID
NTAPI
RtMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PRT_ADAPTER Adapter = MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->Halting = TRUE;
        Adapter->DataPathRunning = FALSE;
    }
}

static
VOID
NTAPI
RtMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (RtMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(RtMiniportDriverHandle);
        RtMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.Header.Size = sizeof(Characteristics);
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 20;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = RtMiniportInitializeEx;
    Characteristics.HaltHandlerEx = RtMiniportHaltEx;
    Characteristics.UnloadHandler = RtMiniportUnload;
    Characteristics.PauseHandler = RtMiniportPauseEx;
    Characteristics.RestartHandler = RtMiniportRestartEx;
    Characteristics.OidRequestHandler = RtMiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = RtMiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = RtMiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = RtMiniportCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = RtMiniportDevicePnpEventNotify;
    Characteristics.ShutdownHandlerEx = RtMiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = RtMiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &RtMiniportDriverHandle);
}
